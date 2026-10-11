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
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Off, Brightness::High, 0));
    TEST_ASSERT_EQUAL_UINT16(PWM_RANGE / 5, headDuty(HeadMode::On, Brightness::Low, 0));
    TEST_ASSERT_EQUAL_UINT16(PWM_RANGE / 2, headDuty(HeadMode::On, Brightness::Med, 0));
    TEST_ASSERT_EQUAL_UINT16(PWM_RANGE / 5, headDuty(HeadMode::Strobe, Brightness::Low, 10));  // flash 1
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Strobe, Brightness::Low, 100));             // gap
    TEST_ASSERT_EQUAL_UINT16(PWM_RANGE, headDuty(HeadMode::Strobe, Brightness::High, 180));    // flash 2
    TEST_ASSERT_EQUAL_UINT16(0, headDuty(HeadMode::Strobe, Brightness::High, 500));            // dark
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
    return UNITY_END();
}
