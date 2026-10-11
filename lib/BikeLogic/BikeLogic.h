// Pure control logic for the bike light controller: no Arduino calls, so it
// builds and unit-tests on the host (pio test -e native).
#pragma once
#include <stdint.h>

namespace bike {

// ---- Tunable timing (ms)
constexpr uint32_t TAP_MAX_MS = 250;           // a squeeze shorter than this is a tap
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 400; // second tap must start within this of the first
constexpr uint32_t SINGLE_BRAKE_MS = 400;      // one lever held this long = brake

// ---- Lever gestures --------------------------------------------------------
// Feed debounced lever states every loop. "Squeezed" = NC contact open.
enum class Gesture : uint8_t { None, ToggleLeft, ToggleRight, ToggleHazard };

class Levers {
public:
    // Returns a gesture when a double-tap completes. Read brake() after.
    Gesture update(uint32_t now, bool left, bool right) {
        bool down = left || right;
        Gesture g = Gesture::None;

        if (down && !active_) {
            active_ = true;
            start_ = now;
            kind_ = 0;
        }
        if (active_) kind_ |= (left ? LEFT : 0) | (right ? RIGHT : 0);

        // Both levers = brake now; one lever = brake once it can't be a tap any more.
        brake_ = (left && right) || (active_ && down && now - start_ >= SINGLE_BRAKE_MS);

        if (!down && active_) {
            active_ = false;
            if (now - start_ < TAP_MAX_MS) {
                if (pending_ && pendingKind_ == kind_ && start_ - pendingStart_ <= DOUBLE_TAP_WINDOW_MS) {
                    g = kind_ == LEFT ? Gesture::ToggleLeft : kind_ == RIGHT ? Gesture::ToggleRight : Gesture::ToggleHazard;
                    pending_ = false;
                } else {
                    pending_ = true;
                    pendingKind_ = kind_;
                    pendingStart_ = start_;
                }
            } else {
                pending_ = false;  // a long squeeze was braking, not tapping
            }
        }
        return g;
    }

    bool brake() const { return brake_; }

private:
    static constexpr uint8_t LEFT = 1, RIGHT = 2;
    bool active_ = false, brake_ = false, pending_ = false;
    uint32_t start_ = 0, pendingStart_ = 0;
    uint8_t kind_ = 0, pendingKind_ = 0;
};

// ---- Blinkers / hazards ----------------------------------------------------
enum class Turn : uint8_t { Off, Left, Right, Hazard };

inline Turn applyGesture(Turn t, Gesture g) {
    switch (g) {
        case Gesture::ToggleHazard: return t == Turn::Hazard ? Turn::Off : Turn::Hazard;
        case Gesture::ToggleLeft:   return t == Turn::Hazard ? t : t == Turn::Left ? Turn::Off : Turn::Left;
        case Gesture::ToggleRight:  return t == Turn::Hazard ? t : t == Turn::Right ? Turn::Off : Turn::Right;
        default:                    return t;
    }
}
inline bool leftOn(Turn t) { return t == Turn::Left || t == Turn::Hazard; }
inline bool rightOn(Turn t) { return t == Turn::Right || t == Turn::Hazard; }

// ---- Battery: filtered voltage, 4-bar gauge, warning, latched cutoff --------
class Battery {
public:
    static constexpr float BAR_V[4] = {10.0f, 10.6f, 11.1f, 11.7f};  // bar n lit at/above BAR_V[n-1]
    static constexpr float HYST_V = 0.1f;
    static constexpr float CUTOFF_V = 9.4f;
    static constexpr uint32_t CUTOFF_HOLD_MS = 5000;

    // Call at a steady rate (~20 Hz) with a voltage that is already an average of several ADC reads.
    void update(uint32_t now, float volts) {
        if (!seeded_) { v_ = volts; seeded_ = true; level_ = levelFor(v_); }
        v_ += 0.1f * (volts - v_);  // ~0.5 s time constant at 20 Hz

        // Move one level at a time; rising needs HYST_V of headroom so bars don't flicker.
        if (level_ < 4 && v_ >= BAR_V[level_] + HYST_V) level_++;
        else if (level_ > 0 && v_ < BAR_V[level_ - 1]) level_--;

        if (v_ < CUTOFF_V) {
            if (!low_) { low_ = true; lowSince_ = now; }
            if (now - lowSince_ >= CUTOFF_HOLD_MS) cutoff_ = true;
        } else {
            low_ = false;
        }
    }

    float volts() const { return v_; }
    uint8_t bars() const { return level_; }   // 0 = below 10.0 V (rear lamp off): blink bar 1
    bool cutoff() const { return cutoff_; }   // latched until power cycle

private:
    static uint8_t levelFor(float v) {
        uint8_t n = 0;
        while (n < 4 && v >= BAR_V[n]) n++;
        return n;
    }
    bool seeded_ = false, low_ = false, cutoff_ = false;
    float v_ = 0;
    uint8_t level_ = 0;
    uint32_t lowSince_ = 0;
};

// ---- Headlight ---------------------------------------------------------------
enum class HeadMode : uint8_t { Off, On, Strobe };
enum class Brightness : uint8_t { Low, Med, High };

constexpr uint16_t PWM_RANGE = 1000;

// Headlight duty per brightness setting, out of PWM_RANGE. LEDs look far brighter
// than their duty cycle, so LOW sits well under 20 %.
constexpr uint16_t DUTY_LOW = 50;    // 5 %
constexpr uint16_t DUTY_MED = 250;   // 25 %
constexpr uint16_t DUTY_HIGH = PWM_RANGE;

inline uint16_t brightnessDuty(Brightness b) {
    return b == Brightness::Low ? DUTY_LOW : b == Brightness::Med ? DUTY_MED : DUTY_HIGH;
}

// ---- Strobe patterns. Flick SW4 into STROBE twice within STROBE_FLICK_MS to step to the next.
enum class StrobePattern : uint8_t { SteadyPulse, DaytimeFlash, DoubleFlash };
constexpr uint8_t STROBE_PATTERN_COUNT = 3;

constexpr uint32_t STROBE_FLICK_MS = 1000;
constexpr uint32_t PULSE_PERIOD_MS = 1000, PULSE_ON_MS = 100;  // steady + full-power pulse
// Steady + pulse: {steady, pulse} per brightness, out of PWM_RANGE (LOW 5->25 %, MED 25->50 %, HIGH 50->100 %).
constexpr uint16_t PULSE_LEVELS[3][2] = {{DUTY_LOW, 250}, {250, 500}, {500, 1000}};
constexpr uint32_t FLASH_PERIOD_MS = 750, FLASH_ON_MS = 80;    // sharp flash, dark between
constexpr uint32_t DOUBLE_PERIOD_MS = 1000;                    // 60 on, 100 off, 60 on, rest off

class StrobeSelector {
public:
    explicit StrobeSelector(StrobePattern start = StrobePattern::SteadyPulse) : pattern_(start) {}

    // Returns true when this call changed the pattern (so the caller can save it).
    bool update(uint32_t now, HeadMode mode) {
        bool entered = mode == HeadMode::Strobe && last_ != HeadMode::Strobe;
        last_ = mode;
        if (!entered) return false;
        bool flick = hasEntry_ && now - lastEntry_ <= STROBE_FLICK_MS;
        lastEntry_ = now;
        hasEntry_ = !flick;  // a completed flick needs two fresh entries for the next one
        if (flick) pattern_ = StrobePattern(((uint8_t)pattern_ + 1) % STROBE_PATTERN_COUNT);
        return flick;
    }

    StrobePattern pattern() const { return pattern_; }

private:
    StrobePattern pattern_;
    HeadMode last_ = HeadMode::Off;
    bool hasEntry_ = false;
    uint32_t lastEntry_ = 0;
};

inline uint16_t headDuty(HeadMode m, Brightness b, StrobePattern p, uint32_t now) {
    if (m == HeadMode::Off) return 0;
    if (m == HeadMode::On) return brightnessDuty(b);
    if (p == StrobePattern::SteadyPulse) {
        const uint16_t* lv = PULSE_LEVELS[(uint8_t)b];
        return now % PULSE_PERIOD_MS < PULSE_ON_MS ? lv[1] : lv[0];
    }
    if (p == StrobePattern::DaytimeFlash) return now % FLASH_PERIOD_MS < FLASH_ON_MS ? brightnessDuty(b) : 0;
    uint32_t t = now % DOUBLE_PERIOD_MS;
    return t < 60 || (t >= 160 && t < 220) ? brightnessDuty(b) : 0;
}

}  // namespace bike
