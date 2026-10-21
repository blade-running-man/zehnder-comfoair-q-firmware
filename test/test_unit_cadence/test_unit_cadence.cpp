// When the probe task asks the unit again (comfoair_unit_cadence.h): the full read at boot + 10 s
// and every 6 h (60 s after an incomplete one), the active errors alone every 60 s between.
#include <unity.h>

#include <cstdint>

#include "comfoair_unit_cadence.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
constexpr uint32_t HZ = 100;
constexpr uint32_t S = HZ;  // one second in ticks
}  // namespace

void test_nothing_is_due_before_the_first_read(void) {
  comfoair_unit_cadence_t c;
  comfoair_unit_cadence_arm(&c, 0, HZ);
  uint32_t wait = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(&c, 0, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(10 * S, wait);
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(&c, 10 * S - 1, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(1, wait);
}

void test_the_first_read_is_a_full_one(void) {
  comfoair_unit_cadence_t c;
  comfoair_unit_cadence_arm(&c, 0, HZ);
  uint32_t wait = 99;
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_FULL, comfoair_unit_cadence_due(&c, 10 * S, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(0, wait);
}

void test_after_a_complete_read_the_errors_come_every_minute(void) {
  comfoair_unit_cadence_t c;
  comfoair_unit_cadence_arm(&c, 0, HZ);
  comfoair_unit_cadence_full_done(&c, 10 * S, HZ, true);
  uint32_t wait = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(&c, 70 * S - 1, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(1, wait);
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_ERRORS, comfoair_unit_cadence_due(&c, 70 * S, HZ, &wait));
  comfoair_unit_cadence_errors_done(&c, 70 * S, HZ);
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(&c, 71 * S, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(59 * S, wait);
}

void test_the_full_read_comes_back_after_six_hours(void) {
  comfoair_unit_cadence_t c;
  comfoair_unit_cadence_arm(&c, 0, HZ);
  comfoair_unit_cadence_full_done(&c, 0, HZ, true);
  uint32_t wait = 0;
  for (uint32_t t = 60 * S; t < 6u * 3600u * S; t += 60 * S) {
    TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_ERRORS, comfoair_unit_cadence_due(&c, t, HZ, &wait));
    comfoair_unit_cadence_errors_done(&c, t, HZ);
  }
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_FULL, comfoair_unit_cadence_due(&c, 6u * 3600u * S, HZ, &wait));
}

void test_an_incomplete_read_retries_in_a_minute_and_the_full_read_wins(void) {
  comfoair_unit_cadence_t c;
  comfoair_unit_cadence_arm(&c, 0, HZ);
  comfoair_unit_cadence_full_done(&c, 0, HZ, false);
  uint32_t wait = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_FULL, comfoair_unit_cadence_due(&c, 60 * S, HZ, &wait));
}

void test_six_hours_at_1000_hz_does_not_overflow(void) {
  comfoair_unit_cadence_t c;
  comfoair_unit_cadence_arm(&c, 0, 1000);
  comfoair_unit_cadence_full_done(&c, 0, 1000, true);
  TEST_ASSERT_EQUAL_UINT32(21600000u, c.full_after);
}

void test_a_wrapping_tick_counter_still_falls_due(void) {
  comfoair_unit_cadence_t c;
  const uint32_t start = UINT32_MAX - 5u;
  comfoair_unit_cadence_arm(&c, start, HZ);
  uint32_t wait = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(&c, UINT32_MAX, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(10 * S - 5u, wait);
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_FULL, comfoair_unit_cadence_due(&c, start + 10 * S, HZ, &wait));
}

// Without a cadence nothing is due, and the wait is still TICKS like every other wait: one errors
// period at the caller's tick rate, so a NULL never makes the caller spin or sleep for hours.
void test_a_null_cadence_is_answered_in_ticks(void) {
  uint32_t wait = 7;
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(nullptr, 0, HZ, &wait));
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_UNIT_ERRORS_S * HZ, wait);
  TEST_ASSERT_EQUAL(COMFOAIR_UNIT_READ_NONE, comfoair_unit_cadence_due(nullptr, 0, 1000, &wait));
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_UNIT_ERRORS_S * 1000u, wait);
  comfoair_unit_cadence_arm(nullptr, 0, HZ);
  comfoair_unit_cadence_full_done(nullptr, 0, HZ, true);
  comfoair_unit_cadence_errors_done(nullptr, 0, HZ);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_is_due_before_the_first_read);
  RUN_TEST(test_the_first_read_is_a_full_one);
  RUN_TEST(test_after_a_complete_read_the_errors_come_every_minute);
  RUN_TEST(test_the_full_read_comes_back_after_six_hours);
  RUN_TEST(test_an_incomplete_read_retries_in_a_minute_and_the_full_read_wins);
  RUN_TEST(test_six_hours_at_1000_hz_does_not_overflow);
  RUN_TEST(test_a_wrapping_tick_counter_still_falls_due);
  RUN_TEST(test_a_null_cadence_is_answered_in_ticks);
  return UNITY_END();
}
