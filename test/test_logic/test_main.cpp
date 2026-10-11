// Host-side tests for lib/BikeLogic. Run: pio test -e native
#include <unity.h>
#include <BikeLogic.h>

using namespace bike;

void setUp() {}
void tearDown() {}

// Drive the levers through a sequence of [start, end) holds and return the last gesture seen.
struct Sim {
    Levers lv;
    uint32_t t = 0;
    Gesture last = Gesture::None;
    bool sawBrake = false;

    void run(uint32_t ms, bool l, bool r) {
        for (uint32_t end = t + ms; t < end; t += 5) {
            Gesture g = lv.update(t, l, r);
            if (g != Gesture::None) last = g;
            sawBrake |= lv.brake();
        }
    }
};

void test_both_levers_brake_immediately() {
    Levers lv;
    lv.update(0, true, true);
    TEST_ASSERT_TRUE(lv.brake());
}

void test_single_lever_brakes_after_window() {
    Levers lv;
    lv.update(0, true, false);
    lv.update(SINGLE_BRAKE_MS - 5, true, false);
    TEST_ASSERT_FALSE(lv.brake());
    lv.update(SINGLE_BRAKE_MS, true, false);
    TEST_ASSERT_TRUE(lv.brake());
    lv.update(SINGLE_BRAKE_MS + 100, false, false);
    TEST_ASSERT_FALSE(lv.brake());
}

void test_left_double_tap_toggles_left() {
    Sim s;
    s.run(100, true, false);
    s.run(100, false, false);
    s.run(100, true, false);
    s.run(50, false, false);
    TEST_ASSERT_EQUAL(Gesture::ToggleLeft, s.last);
    TEST_ASSERT_FALSE(s.sawBrake);
}

void test_right_double_tap_toggles_right() {
    Sim s;
    s.run(100, false, true);
    s.run(100, false, false);
    s.run(100, false, true);
    s.run(50, false, false);
    TEST_ASSERT_EQUAL(Gesture::ToggleRight, s.last);
}

void test_both_double_tap_toggles_hazard() {
    Sim s;
    s.run(100, true, true);
    s.run(100, false, false);
    s.run(100, true, true);
    s.run(50, false, false);
    TEST_ASSERT_EQUAL(Gesture::ToggleHazard, s.last);
}

void test_slow_second_tap_is_not_double() {
    Sim s;
    s.run(100, true, false);
    s.run(400, false, false);  // second tap starts 500 ms after the first
    s.run(100, true, false);
    s.run(50, false, false);
    TEST_ASSERT_EQUAL(Gesture::None, s.last);
}

void test_mixed_sides_is_not_double() {
    Sim s;
    s.run(100, true, false);
    s.run(100, false, false);
    s.run(100, false, true);
    s.run(50, false, false);
    TEST_ASSERT_EQUAL(Gesture::None, s.last);
}

void test_long_squeeze_then_tap_is_not_double() {
    Sim s;
    s.run(300, true, false);  // too long to be a tap
    s.run(50, false, false);
    s.run(100, true, false);
    s.run(50, false, false);
    TEST_ASSERT_EQUAL(Gesture::None, s.last);
}

void test_turn_state_machine() {
    Turn t = Turn::Off;
    t = applyGesture(t, Gesture::ToggleLeft);
    TEST_ASSERT_EQUAL(Turn::Left, t);
    t = applyGesture(t, Gesture::ToggleRight);  // switches sides
    TEST_ASSERT_EQUAL(Turn::Right, t);
    t = applyGesture(t, Gesture::ToggleRight);  // cancels
    TEST_ASSERT_EQUAL(Turn::Off, t);
    t = applyGesture(t, Gesture::ToggleHazard);
    TEST_ASSERT_EQUAL(Turn::Hazard, t);
    t = applyGesture(t, Gesture::ToggleLeft);   // ignored during hazards
    TEST_ASSERT_EQUAL(Turn::Hazard, t);
    t = applyGesture(t, Gesture::ToggleHazard);
    TEST_ASSERT_EQUAL(Turn::Off, t);
}

void test_battery_bars() {
    struct { float v; uint8_t bars; } cases[] = {{12.4f, 4}, {11.4f, 3}, {10.8f, 2}, {10.3f, 1}, {9.8f, 0}};
    for (auto c : cases) {
        Battery b;
        b.update(0, c.v);
        TEST_ASSERT_EQUAL_UINT8(c.bars, b.bars());
    }
}

void test_battery_hysteresis() {
    Battery b;
    b.update(0, 11.05f);                   // just under 3 bars
    TEST_ASSERT_EQUAL_UINT8(2, b.bars());
    for (uint32_t t = 0; t < 5000; t += 50) b.update(t, 11.15f);  // above 11.1 but inside the hysteresis band
    TEST_ASSERT_EQUAL_UINT8(2, b.bars());
    for (uint32_t t = 5000; t < 10000; t += 50) b.update(t, 11.25f);
    TEST_ASSERT_EQUAL_UINT8(3, b.bars());
}

void test_cutoff_needs_five_seconds() {
    Battery b;
    uint32_t t = 0;
    for (; t < 3000; t += 50) b.update(t, 9.2f);
    TEST_ASSERT_FALSE(b.cutoff());
    for (; t < 8000; t += 50) b.update(t, 9.2f);
    TEST_ASSERT_TRUE(b.cutoff());
}

void test_brief_sag_does_not_cut_off() {
    Battery b;
    uint32_t t = 0;
    for (; t < 3000; t += 50) b.update(t, 9.2f);
    for (; t < 6000; t += 50) b.update(t, 10.5f);  // recovers
    for (; t < 9000; t += 50) b.update(t, 9.2f);
    TEST_ASSERT_FALSE(b.cutoff());
}

void test_headlight_duty() {
    const auto SP = StrobePattern::SteadyPulse, DF = StrobePattern::DaytimeFlash;
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Off, Brightness::High, SP, 0));
    TEST_ASSERT_EQUAL_UINT16(DUTY_LOW, headDuty(HeadMode::On, Brightness::Low, SP, 0));
    TEST_ASSERT_EQUAL_UINT16(DUTY_MED, headDuty(HeadMode::On, Brightness::Med, DF, 0));
}

void test_steady_pulse() {
    const auto SP = StrobePattern::SteadyPulse;
    TEST_ASSERT_EQUAL_UINT16(DUTY_HIGH, headDuty(HeadMode::Strobe, Brightness::Low, SP, 50));   // pulse
    TEST_ASSERT_EQUAL_UINT16(DUTY_LOW, headDuty(HeadMode::Strobe, Brightness::Low, SP, 500));   // steady base
    TEST_ASSERT_EQUAL_UINT16(DUTY_MED, headDuty(HeadMode::Strobe, Brightness::Med, SP, 500));   // MED base
    TEST_ASSERT_EQUAL_UINT16(PULSE_HIGH_BASE, headDuty(HeadMode::Strobe, Brightness::High, SP, 500));  // HIGH base = PULSE_HIGH_BASE
}

void test_daytime_flash() {
    const auto DF = StrobePattern::DaytimeFlash;
    TEST_ASSERT_EQUAL_UINT16(DUTY_HIGH, headDuty(HeadMode::Strobe, Brightness::High, DF, 40));  // flash
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Strobe, Brightness::High, DF, 400));         // dark
    TEST_ASSERT_EQUAL_UINT16(DUTY_LOW, headDuty(HeadMode::Strobe, Brightness::Low, DF, 760));   // next flash, follows switch
}

void test_double_flick_swaps_pattern() {
    StrobeSelector s;
    TEST_ASSERT_FALSE(s.update(0, HeadMode::Off));
    TEST_ASSERT_FALSE(s.update(100, HeadMode::Strobe));   // first flick
    TEST_ASSERT_FALSE(s.update(300, HeadMode::Off));
    TEST_ASSERT_TRUE(s.update(600, HeadMode::Strobe));    // second within 1 s: swap
    TEST_ASSERT_EQUAL(StrobePattern::DaytimeFlash, s.pattern());
    s.update(700, HeadMode::Off);
    TEST_ASSERT_FALSE(s.update(800, HeadMode::Strobe));   // a third flick starts a new pair
    TEST_ASSERT_EQUAL(StrobePattern::DaytimeFlash, s.pattern());
}

void test_slow_flicks_do_not_swap() {
    StrobeSelector s;
    s.update(0, HeadMode::Strobe);
    s.update(500, HeadMode::Off);
    TEST_ASSERT_FALSE(s.update(1600, HeadMode::Strobe));  // 1.6 s later
    TEST_ASSERT_EQUAL(StrobePattern::SteadyPulse, s.pattern());
    s.update(1700, HeadMode::On);                         // passing through ON isn't a STROBE entry
    TEST_ASSERT_FALSE(s.update(1800, HeadMode::On));
    TEST_ASSERT_EQUAL(StrobePattern::SteadyPulse, s.pattern());
}

void test_double_flash() {
    const auto DB = StrobePattern::DoubleFlash;
    TEST_ASSERT_EQUAL_UINT16(DUTY_MED, headDuty(HeadMode::Strobe, Brightness::Med, DB, 10));    // flash 1
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Strobe, Brightness::Med, DB, 100));          // gap
    TEST_ASSERT_EQUAL_UINT16(DUTY_MED, headDuty(HeadMode::Strobe, Brightness::Med, DB, 180));   // flash 2
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Strobe, Brightness::Med, DB, 500));          // dark
}

void test_flicks_cycle_all_three_patterns() {
    StrobeSelector s;
    uint32_t t = 0;
    auto doubleFlick = [&]() {
        s.update(t, HeadMode::Off); s.update(t + 100, HeadMode::Strobe);
        s.update(t + 200, HeadMode::Off); s.update(t + 300, HeadMode::Strobe);
        s.update(t + 400, HeadMode::Off);
        t += 2000;
    };
    doubleFlick(); TEST_ASSERT_EQUAL(StrobePattern::DaytimeFlash, s.pattern());
    doubleFlick(); TEST_ASSERT_EQUAL(StrobePattern::DoubleFlash, s.pattern());
    doubleFlick(); TEST_ASSERT_EQUAL(StrobePattern::SteadyPulse, s.pattern());
}

void test_selector_starts_from_saved_pattern() {
    StrobeSelector s(StrobePattern::DaytimeFlash);
    TEST_ASSERT_EQUAL(StrobePattern::DaytimeFlash, s.pattern());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_both_levers_brake_immediately);
    RUN_TEST(test_single_lever_brakes_after_window);
    RUN_TEST(test_left_double_tap_toggles_left);
    RUN_TEST(test_right_double_tap_toggles_right);
    RUN_TEST(test_both_double_tap_toggles_hazard);
    RUN_TEST(test_slow_second_tap_is_not_double);
    RUN_TEST(test_mixed_sides_is_not_double);
    RUN_TEST(test_long_squeeze_then_tap_is_not_double);
    RUN_TEST(test_turn_state_machine);
    RUN_TEST(test_battery_bars);
    RUN_TEST(test_battery_hysteresis);
    RUN_TEST(test_cutoff_needs_five_seconds);
    RUN_TEST(test_brief_sag_does_not_cut_off);
    RUN_TEST(test_headlight_duty);
    RUN_TEST(test_steady_pulse);
    RUN_TEST(test_daytime_flash);
    RUN_TEST(test_double_flick_swaps_pattern);
    RUN_TEST(test_slow_flicks_do_not_swap);
    RUN_TEST(test_double_flash);
    RUN_TEST(test_flicks_cycle_all_three_patterns);
    RUN_TEST(test_selector_starts_from_saved_pattern);
    return UNITY_END();
}
