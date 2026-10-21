// The `bit` operator: a binary row or the switch true while one bit of its PDO is set.
//
// Its own suite because test_state.cpp sits at the 600-line suite ceiling. Vectors come from
// aiocomfoconnect docs/PROTOCOL-PDO.md, PDOs 337 and 338: 64 = boost, 2048 = away, bits add
// (2112 = both); 2 = bypass override (timer 1).
#include <unity.h>

#include <cstring>

#include "comfoair_state.h"
#include "registry_generated.h"

void setUp(void) { comfoair_state_reset(); }
void tearDown(void) {}

static bool get_bool(const char *key) {
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get(key, &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_BOOL, v.type);
  return v.boolean;
}

void test_boost_alone_sets_only_the_boost_row(void) {
  const uint8_t frame[] = {0x40, 0x00, 0x00, 0x00};  // 64, little-endian UINT32
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_TRUE(get_bool("boost_timer_running"));
  TEST_ASSERT_FALSE(get_bool("away_timer_running"));
}

void test_away_alone_sets_only_the_away_row(void) {
  const uint8_t frame[] = {0x00, 0x08, 0x00, 0x00};  // 2048
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_FALSE(get_bool("boost_timer_running"));
  TEST_ASSERT_TRUE(get_bool("away_timer_running"));
}

void test_other_timer_bits_set_neither(void) {
  const uint8_t frame[] = {0x22, 0x00, 0x00, 0x00};  // 34 = timers 1 and 5
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_FALSE(get_bool("boost_timer_running"));
  TEST_ASSERT_FALSE(get_bool("away_timer_running"));
}

void test_both_bits_together(void) {
  const uint8_t frame[] = {0x40, 0x08, 0x00, 0x00};  // 2112
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_TRUE(get_bool("boost_timer_running"));
  TEST_ASSERT_TRUE(get_bool("away_timer_running"));
}

void test_an_eq_row_is_untouched_by_the_bit_path(void) {
  // manual_mode is PDO 56 == 1. Under a bit test 1 would read as bit 1 of 1, which is clear.
  const uint8_t frame[] = {1};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(56, frame, sizeof frame));
  TEST_ASSERT_TRUE(get_bool("manual_mode"));
}

void test_255_is_a_labelled_enum_value_not_the_all_ones_sentinel(void) {
  // PDO 49 = 255 is "Auto": an enum reads its label table, never the countdown rows' no-value rule.
  const uint8_t frame[] = {0xff};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(49, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("operating_mode", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_STRING("Auto", v.text);
}

void test_the_bypass_override_is_bit_1_of_pdo_338(void) {
  const uint8_t overridden[] = {0x02, 0x00, 0x00, 0x00};  // timer 1
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(338, overridden, sizeof overridden));
  TEST_ASSERT_TRUE(get_bool("bypass_override"));
  const uint8_t automatic[] = {0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(338, automatic, sizeof automatic));
  TEST_ASSERT_FALSE(get_bool("bypass_override"));
}

void test_another_bypass_timer_is_not_an_override(void) {
  // The old enum over {0, 2} published null for this; the bit test says "no override".
  const uint8_t other[] = {0x04, 0x00, 0x00, 0x00};  // timer 2
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(338, other, sizeof other));
  TEST_ASSERT_FALSE(get_bool("bypass_override"));
}

void test_a_bit_past_the_end_of_a_short_frame_is_unknown_not_false(void) {
  // One byte carries bits 0..7: it says whether boost (bit 6) runs, and nothing about away
  // (bit 11). A confident false for away would be a guess.
  const uint8_t frame[] = {0x40};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_TRUE(get_bool("boost_timer_running"));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("away_timer_running", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

// --- away_mode: the switch reads the timer its command drives -------------------------------
//
// The switch starts and cancels SCHEDULE timer 11 (84 15 01 0B / 85 15 01 0B), so its state is
// timer 11 running, PDO 337 bit 11. On the owner's Q350 PDO 16 read 7 for the fan's Away PRESET
// with bit 11 clear and no countdown, so a switch on PDO 16 showed ON that its OFF could not clear.

void test_away_mode_is_on_while_the_away_timer_runs(void) {
  const uint8_t frame[] = {0x00, 0x08, 0x00, 0x00};  // 2048 = timer 11
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_TRUE(get_bool("away_mode"));
}

void test_away_mode_is_off_when_only_boost_runs(void) {
  const uint8_t frame[] = {0x40, 0x00, 0x00, 0x00};  // 64 = timer 6
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  TEST_ASSERT_FALSE(get_bool("away_mode"));
}

void test_the_away_preset_does_not_turn_the_switch_on(void) {
  // The fan at its Away level: device state 7, no timer running.
  const uint8_t state[] = {7};
  const uint8_t timers[] = {0x00, 0x00, 0x00, 0x00};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(16, state, sizeof state));
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, timers, sizeof timers));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("device_state", &v));
  TEST_ASSERT_EQUAL_STRING("Away (preset)", v.text);
  TEST_ASSERT_FALSE(get_bool("away_mode"));
}

void test_pdo_16_alone_leaves_the_switch_unknown(void) {
  const uint8_t state[] = {7};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(16, state, sizeof state));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("away_mode", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

void test_away_mode_is_unknown_on_a_frame_too_short_for_bit_11(void) {
  const uint8_t frame[] = {0x00};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("away_mode", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_boost_alone_sets_only_the_boost_row);
  RUN_TEST(test_away_alone_sets_only_the_away_row);
  RUN_TEST(test_other_timer_bits_set_neither);
  RUN_TEST(test_both_bits_together);
  RUN_TEST(test_an_eq_row_is_untouched_by_the_bit_path);
  RUN_TEST(test_255_is_a_labelled_enum_value_not_the_all_ones_sentinel);
  RUN_TEST(test_the_bypass_override_is_bit_1_of_pdo_338);
  RUN_TEST(test_another_bypass_timer_is_not_an_override);
  RUN_TEST(test_a_bit_past_the_end_of_a_short_frame_is_unknown_not_false);
  RUN_TEST(test_away_mode_is_on_while_the_away_timer_runs);
  RUN_TEST(test_away_mode_is_off_when_only_boost_runs);
  RUN_TEST(test_the_away_preset_does_not_turn_the_switch_on);
  RUN_TEST(test_pdo_16_alone_leaves_the_switch_unknown);
  RUN_TEST(test_away_mode_is_unknown_on_a_frame_too_short_for_bit_11);
  return UNITY_END();
}
