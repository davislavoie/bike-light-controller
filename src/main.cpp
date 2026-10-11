// Bike light controller firmware: Adafruit KB2040 (RP2040, arduino-pico core).
// Behaviour lives in lib/BikeLogic; this file only does pins, timing and I/O.
#include <Arduino.h>
#include <hardware/gpio.h>
#include <initializer_list>
#include <EEPROM.h>
#include <BikeLogic.h>

using namespace bike;

// ---- Pins (see docs/wiring-diagram.html)
constexpr uint8_t PIN_LED_TURN_L = 0, PIN_LED_TURN_R = 1;
constexpr uint8_t PIN_BRAKE_L = 2, PIN_BRAKE_R = 3;              // NC to GND: HIGH = squeezed
constexpr uint8_t PIN_HEAD_ON = 4, PIN_HEAD_STROBE = 5;          // ON-OFF-ON to GND
constexpr uint8_t PIN_HEADLIGHT = 6;                             // IRLB3813 gate, PWM
constexpr uint8_t PIN_REAR_BRAKE = 7, PIN_REAR_TURN_L = 8, PIN_REAR_TURN_R = 9;  // TLP241A LEDs
constexpr uint8_t PIN_BATT_BAR[4] = {10, 18, 19, 20};            // bar 1..4
constexpr uint8_t PIN_BRIGHT_LO = 26, PIN_BRIGHT_HI = 28;        // ON-OFF-ON to GND
constexpr uint8_t PIN_VSENSE = 27;                               // A1: VBAT x 22/122

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
    analogWrite(PIN_HEADLIGHT, 0);
    for (uint8_t p : {PIN_REAR_BRAKE, PIN_REAR_TURN_L, PIN_REAR_TURN_R, PIN_LED_TURN_L, PIN_LED_TURN_R})
        digitalWrite(p, LOW);
    for (uint8_t p : PIN_BATT_BAR) digitalWrite(p, LOW);
}

// Below 9.4 V for 5 s: everything off for good, just a short blink of bar 1
// every few seconds as a reminder to turn the master switch off.
[[noreturn]] void cutoff() {
    allOutputsOff();
    Serial.println("Low-voltage cutoff: turn the master switch off");
    for (;;) {
        digitalWrite(PIN_BATT_BAR[0], HIGH);
        delay(30);
        digitalWrite(PIN_BATT_BAR[0], LOW);
        delay(5000);
    }
}

void setup() {
    Serial.begin(115200);

    for (uint8_t p : {PIN_HEADLIGHT, PIN_REAR_BRAKE, PIN_REAR_TURN_L, PIN_REAR_TURN_R, PIN_LED_TURN_L, PIN_LED_TURN_R})
        pinMode(p, OUTPUT);
    for (uint8_t p : PIN_BATT_BAR) pinMode(p, OUTPUT);
    for (uint8_t p : {PIN_BRAKE_L, PIN_BRAKE_R, PIN_HEAD_ON, PIN_HEAD_STROBE, PIN_BRIGHT_LO, PIN_BRIGHT_HI})
        pinMode(p, INPUT_PULLUP);
    for (Debounced* d : {&brakeL, &brakeR, &headOn, &headStrobe, &brightLo, &brightHi}) d->begin();

    // The TLP241A LEDs want ~6 mA through 330 Ω; the default 4 mA drive sags.
    for (uint8_t p : {PIN_REAR_BRAKE, PIN_REAR_TURN_L, PIN_REAR_TURN_R})
        gpio_set_drive_strength(p, GPIO_DRIVE_STRENGTH_8MA);
    gpio_set_drive_strength(PIN_HEADLIGHT, GPIO_DRIVE_STRENGTH_12MA);

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
    digitalWrite(PIN_REAR_BRAKE, levers.brake());
    digitalWrite(PIN_REAR_TURN_L, leftOn(turn));
    digitalWrite(PIN_REAR_TURN_R, rightOn(turn));

    bool turnPhase = now % HUD_TURN_PERIOD_MS < HUD_TURN_PERIOD_MS / 2;
    digitalWrite(PIN_LED_TURN_L, leftOn(turn) && turnPhase);
    digitalWrite(PIN_LED_TURN_R, rightOn(turn) && turnPhase);

    uint8_t bars = battery.bars();
    bool warnPhase = now % BAR_WARN_PERIOD_MS < BAR_WARN_PERIOD_MS / 2;
    for (uint8_t i = 0; i < 4; i++)
        digitalWrite(PIN_BATT_BAR[i], bars == 0 ? (i == 0 && warnPhase) : i < bars);

    // ---- Serial status once a second (for VBAT_CAL and bench testing)
    static uint32_t lastPrint = 0;
    if (now - lastPrint >= 1000) {
        lastPrint = now;
        Serial.printf("vbat=%.2f bars=%u brake=%d turn=%u head=%u bright=%u strobe=%u\n", battery.volts(), bars,
                      levers.brake(), (unsigned)turn, (unsigned)mode, (unsigned)bright, (unsigned)strobeSel.pattern());
    }
}
