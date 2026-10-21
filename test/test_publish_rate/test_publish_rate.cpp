// How often one entity may go out over MQTT (comfoair_publish_rate.h). Expectations are built from
// each row's OWN generated fields, so retuning a value in pdo_table.py does not break the rule's
// tests; the values themselves are pinned in tests/python/test_registry_publish_rate.py.
#include <unity.h>

#include <cmath>
#include <cstdint>

#include "comfoair_publish_rate.h"
#include "comfoair_state.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

const comfoair_entity_t *row(const char *key) {
  const comfoair_entity_t *e = comfoair_entity_by_key(key);
  TEST_ASSERT_NOT_NULL_MESSAGE(e, key);
  return e;
}

comfoair_value_t num(float x) {
  comfoair_value_t v{};
  v.type = COMFOAIR_VALUE_NUMBER;
  v.number = x;
  return v;
}

comfoair_value_t none(void) {
  comfoair_value_t v{};
  v.type = COMFOAIR_VALUE_NONE;
  return v;
}

comfoair_publish_rate_slot_t sent(const comfoair_value_t &v, uint32_t at) {
  comfoair_publish_rate_slot_t s{};
  comfoair_publish_rate_record(&s, &v, at);
  return s;
}

constexpr uint32_t HOLD_MS = COMFOAIR_PUBLISH_MAX_HOLD_S * 1000u;

comfoair_publish_verdict_t decide(const comfoair_entity_t *e, const comfoair_value_t &v,
                                  const comfoair_publish_rate_slot_t &s, uint32_t now) {
  return comfoair_publish_rate_decide(e, &v, &s, now);
}

}  // namespace

void test_only_numeric_rows_can_be_limited(void) {
  size_t limited = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_publish_rate_limited(e)) {
      TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_KIND_NUMERIC, e->kind, e->key);
      limited++;
    }
  }
  TEST_ASSERT_TRUE(limited > 0);
}

void test_a_row_without_a_policy_always_sends(void) {
  const comfoair_entity_t *e = row("filter_replacement_remaining_days");
  TEST_ASSERT_FALSE(comfoair_publish_rate_limited(e));
  const auto s = sent(num(30), 0);
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, num(29), s, 1));
}

void test_the_first_publication_of_a_connection_is_never_held(void) {
  comfoair_publish_rate_slot_t fresh{};
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(row("supply_fan_speed"), num(783), fresh, 0));
}

void test_jitter_inside_the_deadband_is_held(void) {
  const comfoair_entity_t *e = row("supply_fan_speed");
  TEST_ASSERT_FALSE(std::isnan(e->publish_deadband));
  const auto s = sent(num(783), 0);
  const uint32_t after_gap = (e->publish_min_interval_s + 1u) * 1000u;
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_HOLD,
                    decide(e, num(783 + e->publish_deadband / 2), s, after_gap));
}

void test_a_change_beyond_the_deadband_waits_for_the_min_interval(void) {
  const comfoair_entity_t *e = row("supply_fan_speed");
  const auto s = sent(num(783), 0);
  const comfoair_value_t far = num(783 + e->publish_deadband * 4);
  const uint32_t gap = e->publish_min_interval_s * 1000u;
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_HOLD, decide(e, far, s, gap - 1));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, far, s, gap));
}

// A change of exactly one deadband is a real change and goes out; one just short of it is jitter.
// Pins `<` against `<=` in the deadband test.
void test_a_change_of_exactly_the_deadband_is_sent(void) {
  const comfoair_entity_t *e = row("supply_fan_speed");
  const auto s = sent(num(783), 0);
  const uint32_t gap = e->publish_min_interval_s * 1000u;
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, num(783 + e->publish_deadband), s, gap));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_HOLD, decide(e, num(783 + e->publish_deadband - 1), s, gap));
}

void test_a_held_difference_goes_out_at_the_max_hold(void) {
  const comfoair_entity_t *e = row("supply_fan_speed");
  const auto s = sent(num(783), 0);
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_HOLD, decide(e, num(784), s, HOLD_MS - 1));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, num(784), s, HOLD_MS));
}

void test_an_unchanged_value_is_never_repeated_on_a_clock(void) {
  const auto s = sent(num(783), 0);
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SAME, decide(row("supply_fan_speed"), num(783), s, HOLD_MS * 10));
}

void test_a_reading_going_away_or_coming_back_is_immediate(void) {
  const comfoair_entity_t *e = row("supply_fan_speed");
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, none(), sent(num(783), 0), 1));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, num(783), sent(none(), 0), 1));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SAME, decide(e, none(), sent(none(), 0), 1));
}

void test_a_min_interval_only_row_sends_every_change_after_the_gap(void) {
  const comfoair_entity_t *e = row("outdoor_air_temp");
  TEST_ASSERT_TRUE(std::isnan(e->publish_deadband));
  TEST_ASSERT_TRUE(e->publish_min_interval_s > 0);
  const auto s = sent(num(12.3f), 0);
  const uint32_t gap = e->publish_min_interval_s * 1000u;
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_HOLD, decide(e, num(12.4f), s, gap - 1));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, decide(e, num(12.4f), s, gap));
}

void test_the_millisecond_clock_may_wrap(void) {
  const comfoair_entity_t *e = row("supply_fan_speed");
  const uint32_t at = UINT32_MAX - 999u;
  const auto s = sent(num(783), at);
  const comfoair_value_t far = num(783 + e->publish_deadband * 4);
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_HOLD, decide(e, far, s, at + 500u));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND,
                    decide(e, far, s, at + e->publish_min_interval_s * 1000u));
}

void test_record_remembers_what_went_out(void) {
  comfoair_publish_rate_slot_t s{};
  const comfoair_value_t v = num(42);
  comfoair_publish_rate_record(&s, &v, 7);
  TEST_ASSERT_TRUE(s.sent);
  TEST_ASSERT_TRUE(s.was_number);
  TEST_ASSERT_EQUAL_FLOAT(42, s.number);
  TEST_ASSERT_EQUAL_UINT32(7, s.at_ms);
}

void test_a_missing_argument_fails_open(void) {
  const comfoair_publish_rate_slot_t s{};
  const comfoair_value_t v = num(1);
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND, comfoair_publish_rate_decide(nullptr, &v, &s, 0));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND,
                    comfoair_publish_rate_decide(row("supply_fan_speed"), nullptr, &s, 0));
  TEST_ASSERT_EQUAL(COMFOAIR_PUBLISH_SEND,
                    comfoair_publish_rate_decide(row("supply_fan_speed"), &v, nullptr, 0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_only_numeric_rows_can_be_limited);
  RUN_TEST(test_a_row_without_a_policy_always_sends);
  RUN_TEST(test_the_first_publication_of_a_connection_is_never_held);
  RUN_TEST(test_jitter_inside_the_deadband_is_held);
  RUN_TEST(test_a_change_beyond_the_deadband_waits_for_the_min_interval);
  RUN_TEST(test_a_change_of_exactly_the_deadband_is_sent);
  RUN_TEST(test_a_held_difference_goes_out_at_the_max_hold);
  RUN_TEST(test_an_unchanged_value_is_never_repeated_on_a_clock);
  RUN_TEST(test_a_reading_going_away_or_coming_back_is_immediate);
  RUN_TEST(test_a_min_interval_only_row_sends_every_change_after_the_gap);
  RUN_TEST(test_the_millisecond_clock_may_wrap);
  RUN_TEST(test_record_remembers_what_went_out);
  RUN_TEST(test_a_missing_argument_fails_open);
  return UNITY_END();
}
