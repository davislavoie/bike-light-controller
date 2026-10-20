// Bike light controller firmware: Adafruit KB2040 (RP2040, arduino-pico core).
// Behaviour lives in lib/BikeLogic; this file only does pins, timing and I/O.
#include <Arduino.h>
#include <hardware/gpio.h>
#include <initializer_list>
#include <EEPROM.h>
#include <BikeLogic.h>

using namespace bike;

// ---- Pins (see docs/wiring-diagram.html). RP2040 PWM pins share hardware in pairs (e.g.
// GP2/GP18, GP10/GP26 drive the same output): every PWM pin below (GP0, 1, 6-10) has a
// partner that is unused or an input.
constexpr uint8_t PIN_HEADLIGHT = 0;                             // IRLB3813 gate, PWM
constexpr uint8_t PIN_LED_TURN_L = 1, PIN_LED_TURN_R = 6;
constexpr uint8_t PIN_BRAKE_L = 2, PIN_BRAKE_R = 3;              // NC to GND: HIGH = squeezed
constexpr uint8_t PIN_HEAD_ON = 4, PIN_HEAD_STROBE = 5;          // ON-OFF-ON to GND
constexpr uint8_t PIN_REAR_BRAKE = 29, PIN_REAR_TURN_L = 18, PIN_REAR_TURN_R = 20; // TLP241A LEDs
constexpr uint8_t PIN_REAR_TAIL = 27;                            // A1: U6 TLP241A LED, runner
constexpr uint8_t PIN_BATT_BAR[4] = {7, 8, 9, 10};               // bar 1..4
constexpr uint8_t PIN_BRIGHT_LO = 26, PIN_BRIGHT_HI = 19;        // ON-OFF-ON to GND
constexpr uint8_t PIN_VSENSE = 28;                               // A2: VBAT x 22/122

// ---- Battery sense. Measure the battery with a multimeter, compare with the
// "vbat" value printed on the serial monitor, and set VBAT_CAL = meter / printed.
constexpr float VBAT_DIVIDER = 122.0f / 22.0f;
constexpr float VBAT_CAL = 1.00f;

// Above hearing range: audible PWM made the board's ceramic caps whine. The IRLB3813's
// big gate charge needs fast edges at this rate: 12 mA pin drive + a 22 Ω gate resistor.
constexpr uint32_t HEAD_PWM_HZ = 25000;
constexpr uint32_t DEBOUNCE_MS = 20;
constexpr uint32_t BATTERY_PERIOD_MS = 50;
constexpr uint32_t HUD_TURN_PERIOD_MS = 667;  // ~1.5 Hz blink
constexpr uint32_t BAR_WARN_PERIOD_MS = 500;  // 2 Hz blink below 10 V

// A pin that must hold its new level for DEBOUNCE_MS before it counts.
struct Debounced {
    uint8_t pin;
    bool stable = false, last = false;
    uint32_t changedAt = 0;

    void begin() { stable = last = digitalRead(pin); }  // start from the real level, no boot glitch

    bool read(uint32_t now) {
        bool raw = digitalRead(pin);
        if (raw != last) { last = raw; changedAt = now; }
        if (now - changedAt >= DEBOUNCE_MS) stable = raw;
        return stable;
    }
};

Debounced brakeL{PIN_BRAKE_L}, brakeR{PIN_BRAKE_R};
Debounced headOn{PIN_HEAD_ON}, headStrobe{PIN_HEAD_STROBE};
Debounced brightLo{PIN_BRIGHT_LO}, brightHi{PIN_BRIGHT_HI};

Levers levers;
Battery battery;
StrobeSelector strobeSel;
constexpr int EEPROM_STROBE_ADDR = 0;  // remembered strobe pattern (0-2)
Turn turn = Turn::Off;
uint32_t lastBatteryAt = 0;

float readVbat() {
    uint32_t sum = 0;
    for (int i = 0; i < 16; i++) sum += analogRead(PIN_VSENSE);  // average out headlight PWM ripple
    return sum / 16.0f / 4095.0f * 3.3f * VBAT_DIVIDER * VBAT_CAL;
}

void allOutputsOff() {
    for (uint8_t p : {PIN_HEADLIGHT, PIN_LED_TURN_L, PIN_LED_TURN_R}) analogWrite(p, 0);
    for (uint8_t p : PIN_BATT_BAR) analogWrite(p, 0);
    for (uint8_t p : {PIN_REAR_TAIL, PIN_REAR_BRAKE, PIN_REAR_TURN_L, PIN_REAR_TURN_R}) digitalWrite(p, LOW);
}

// Below 9.4 V for 5 s: everything off for good, just a short blink of bar 1
// every few seconds as a reminder to turn the master switch off.
[[noreturn]] void cutoff() {
    allOutputsOff();
    Serial.println("Low-voltage cutoff: turn the master switch off");
    for (;;) {
        analogWrite(PIN_BATT_BAR[0], PWM_RANGE);
        delay(30);
        analogWrite(PIN_BATT_BAR[0], 0);
        delay(5000);
    }
}

void setup() {
    Serial.begin(115200);

    for (uint8_t p : {PIN_HEADLIGHT, PIN_REAR_TAIL, PIN_REAR_BRAKE, PIN_REAR_TURN_L, PIN_REAR_TURN_R, PIN_LED_TURN_L, PIN_LED_TURN_R})
        pinMode(p, OUTPUT);
    for (uint8_t p : PIN_BATT_BAR) pinMode(p, OUTPUT);
    for (uint8_t p : {PIN_BRAKE_L, PIN_BRAKE_R, PIN_HEAD_ON, PIN_HEAD_STROBE, PIN_BRIGHT_LO, PIN_BRIGHT_HI})
        pinMode(p, INPUT_PULLUP);
    for (Debounced* d : {&brakeL, &brakeR, &headOn, &headStrobe, &brightLo, &brightHi}) d->begin();

    // The TLP241A LEDs want ~6 mA through 330 Ω; the default 4 mA drive sags.
    for (uint8_t p : {PIN_REAR_TAIL, PIN_REAR_BRAKE, PIN_REAR_TURN_L, PIN_REAR_TURN_R})
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PIN_HEADLIGHT, GPIO_DRIVE_STRENGTH_12MA);
    // HUD turn LEDs run ~6 mA through 220 Ω to read in sunlight; the battery bar stays on
    // 330 Ω (~4 mA) so every GPIO together stays near the RP2040's ~50 mA budget.
    for (uint8_t p : {PIN_LED_TURN_L, PIN_LED_TURN_R}) gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_8MA);

    analogWriteFreq(HEAD_PWM_HZ);
    analogWriteRange(PWM_RANGE);
    analogReadResolution(12);

    EEPROM.begin(16);
    uint8_t saved = EEPROM.read(EEPROM_STROBE_ADDR);
    if (saved < STROBE_PATTERN_COUNT) strobeSel = StrobeSelector((StrobePattern)saved);
    allOutputsOff();
}

void loop() {
    uint32_t now = millis();

    // ---- Battery
    if (now - lastBatteryAt >= BATTERY_PERIOD_MS) {
        lastBatteryAt = now;
        battery.update(now, readVbat());
        if (battery.cutoff()) cutoff();
    }

    // ---- Levers -> brake + blinker gestures
    turn = applyGesture(turn, levers.update(now, brakeL.read(now), brakeR.read(now)));

    // ---- Headlight switches (active LOW)
    bool on = !headOn.read(now), strobe = !headStrobe.read(now);
    HeadMode mode = on ? HeadMode::On : strobe ? HeadMode::Strobe : HeadMode::Off;
    bool lo = !brightLo.read(now), hi = !brightHi.read(now);
    Brightness bright = lo ? Brightness::Low : hi ? Brightness::High : Brightness::Med;

    // ---- Outputs. The rear lamp flashes its own turn signal: hold the input steady.
    if (strobeSel.update(now, mode)) {  // two quick flicks into STROBE step to the next pattern
        EEPROM.write(EEPROM_STROBE_ADDR, (uint8_t)strobeSel.pattern());
        EEPROM.commit();
    }
    analogWrite(PIN_HEADLIGHT, headDuty(mode, bright, strobeSel.pattern(), now));
    digitalWrite(PIN_REAR_TAIL, rearTailOn(mode, strobeSel.pattern()));
    digitalWrite(PIN_REAR_BRAKE, rearBrakeOn(levers.brake(), mode, strobeSel.pattern(), now));
    digitalWrite(PIN_REAR_TURN_L, leftOn(turn));
    digitalWrite(PIN_REAR_TURN_R, rightOn(turn));

    // HUD LEDs are PWM-dimmed (same 25 kHz) so they don't glare at night.
    uint16_t hud = hudDuty(mode, bright, strobeSel.pattern());
    bool turnPhase = now % HUD_TURN_PERIOD_MS < HUD_TURN_PERIOD_MS / 2;
    analogWrite(PIN_LED_TURN_L, leftOn(turn) && turnPhase ? hud : 0);
    analogWrite(PIN_LED_TURN_R, rightOn(turn) && turnPhase ? hud : 0);

    uint8_t bars = battery.bars();
    bool warnPhase = now % BAR_WARN_PERIOD_MS < BAR_WARN_PERIOD_MS / 2;
    for (uint8_t i = 0; i < 4; i++)
        analogWrite(PIN_BATT_BAR[i], (bars == 0 ? (i == 0 && warnPhase) : i < bars) ? hud : 0);

    // ---- Serial status once a second (for VBAT_CAL and bench testing)
    static uint32_t lastPrint = 0;
    if (now - lastPrint >= 1000) {
        lastPrint = now;
        Serial.printf("vbat=%.2f bars=%u brake=%d turn=%u head=%u bright=%u strobe=%u\n", battery.volts(), bars,
                      levers.brake(), (unsigned)turn, (unsigned)mode, (unsigned)bright, (unsigned)strobeSel.pattern());
    }
}
