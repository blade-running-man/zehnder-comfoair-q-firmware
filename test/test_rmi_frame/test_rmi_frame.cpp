// RMI request construction.
//
// Every expected byte string below is copied from the command table in
// docs/reference/comfoair-pdo-rmi.md §3 -- the document, not the header under test. Where the
// table writes `<secs>`, the duration is spelled out here as
// the little-endian bytes the same section documents ("100e0000 = 3600 s").
#include <unity.h>

#include <vector>

#include "rmi_frame.h"

using namespace comfoair::rmi;

static void assert_frame(const char *what, const Frame &got, const std::vector<uint8_t> &want) {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(want.size(), got.size(), what);
  TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(want.data(), got.data(), want.size(), what);
}

// --- the command table -------------------------------------------------------------------

void test_level_low(void) {
  // "84 15 01 01 00000000 01000000 01"
  assert_frame("level low", ventilation_level(1),
               {0x84, 0x15, 0x01, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x01});
}

void test_level_away_is_value_zero(void) {
  assert_frame("level away", ventilation_level(0),
               {0x84, 0x15, 0x01, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x00});
}

void test_boost_on_carries_its_duration_little_endian(void) {
  // "84 15 01 06 00000000 <secs> 03", and §3: "100e0000 = 3600 s".
  assert_frame("boost on 3600s", boost_on(3600),
               {0x84, 0x15, 0x01, 0x06, 0, 0, 0, 0, 0x10, 0x0e, 0x00, 0x00, 0x03});
}

void test_boost_off(void) {
  assert_frame("boost off", boost_off(), {0x85, 0x15, 0x01, 0x06});
}

void test_away_on_and_off(void) {
  assert_frame("away on 3600s", away_on(3600),
               {0x84, 0x15, 0x01, 0x0b, 0, 0, 0, 0, 0x10, 0x0e, 0x00, 0x00, 0x00});
  assert_frame("away off", away_off(), {0x85, 0x15, 0x01, 0x0b});
}

void test_manual_and_auto(void) {
  // Subunit 08, not subunit 01 property 05: the doc says "Prefer subunit 08".
  assert_frame("manual", manual_on(),
               {0x84, 0x15, 0x08, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x01});
  assert_frame("auto", manual_off(), {0x85, 0x15, 0x08, 0x01});
}

void test_bypass_activate_deactivate_auto(void) {
  assert_frame("bypass activate", bypass_activate(3600),
               {0x84, 0x15, 0x02, 0x01, 0, 0, 0, 0, 0x10, 0x0e, 0x00, 0x00, 0x01});
  assert_frame("bypass deactivate", bypass_deactivate(3600),
               {0x84, 0x15, 0x02, 0x01, 0, 0, 0, 0, 0x10, 0x0e, 0x00, 0x00, 0x02});
  assert_frame("bypass auto", bypass_auto(), {0x85, 0x15, 0x02, 0x01});
}

void test_supply_and_exhaust_only_both_use_value_01(void) {
  // "Supply-only / exhaust-only value byte is 01 for BOTH subunits." 00 is the easy mistake;
  // the doc says use 01. Checked byte for byte like every other row --
  // counter-review pointed out this was the only row spot-checked on two bytes of thirteen.
  assert_frame("supply only, indefinite", supply_only_on(-1),
               {0x84, 0x15, 0x06, 0x01, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0x01});
  assert_frame("exhaust only, indefinite", exhaust_only_on(-1),
               {0x84, 0x15, 0x07, 0x01, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0x01});
}

void test_balanced_is_two_frames_and_neither_had_a_test(void) {
  // The table's "Balanced" row is `85 15 06 01` AND `85 15 07 01` -- both timers cleared.
  // Neither function was called by any test until counter-review said so.
  assert_frame("clear supply-only", supply_only_off(), {0x85, 0x15, 0x06, 0x01});
  assert_frame("clear exhaust-only", exhaust_only_off(), {0x85, 0x15, 0x07, 0x01});
}

void test_the_remaining_levels_and_the_middle_profile(void) {
  // Level medium, Level high and Profile cool are rows of the documented table that no test
  // covered. A preset the user picks most often should not be the untested one.
  assert_frame("level medium", ventilation_level(2),
               {0x84, 0x15, 0x01, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x02});
  assert_frame("level high", ventilation_level(3),
               {0x84, 0x15, 0x01, 0x01, 0, 0, 0, 0, 0x01, 0, 0, 0, 0x03});
  assert_frame("profile cool", temperature_profile(1),
               {0x84, 0x15, 0x03, 0x01, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0x01});
}

void test_temperature_profiles_are_indefinite(void) {
  // "84 15 03 01 00000000 ffffffff 00|01|02" -- ffffffff = -1 = indefinite.
  assert_frame("profile normal", temperature_profile(0),
               {0x84, 0x15, 0x03, 0x01, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0x00});
  assert_frame("profile warm", temperature_profile(2),
               {0x84, 0x15, 0x03, 0x01, 0, 0, 0, 0, 0xff, 0xff, 0xff, 0xff, 0x02});
}

void test_sensor_ventilation_is_a_direct_property_write(void) {
  // "write: 03 <unit> <subunit> <property> <value u8>", unit 0x1d TEMPHUMCONTROL.
  assert_frame("humidity comfort", humidity_comfort(2), {0x03, 0x1d, 0x01, 0x06, 0x02});
}

// --- framing rules -----------------------------------------------------------------------

void test_enable_is_thirteen_bytes_and_disable_is_four(void) {
  // "Trailing padding is not part of the protocol." Padding with extra 000000 is the easy
  // mistake; do not.
  TEST_ASSERT_EQUAL_size_t(13, boost_on(600).size());
  TEST_ASSERT_EQUAL_size_t(4, boost_off().size());
}

void test_short_commands_are_sent_verbatim(void) {
  const auto frames = split_into_can_frames(boost_off());
  TEST_ASSERT_EQUAL_size_t(1, frames.size());
  assert_frame("single frame", frames[0], {0x85, 0x15, 0x01, 0x06});
}

void test_long_commands_are_chunked_with_a_final_bit(void) {
  // 13 bytes > 8, so it is chunked into 7-byte blocks, each prefixed by its index,
  // with 0x80 set on the last.
  const auto frames = split_into_can_frames(boost_on(3600));
  TEST_ASSERT_EQUAL_size_t(2, frames.size());
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x00, frames[0][0], "first chunk index");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(8, frames[0].size(), "first chunk is prefix + 7");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0x81, frames[1][0], "last chunk index with 0x80");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(7, frames[1].size(), "last chunk is prefix + 6");
}

void test_an_empty_command_produces_no_frames(void) {
  // A DLC=0 CAN frame would be a silent no-op that looks like a successful send.
  TEST_ASSERT_EQUAL_size_t(0, split_into_can_frames(Frame{}).size());
}

void test_command_can_id_places_its_fields(void) {
  // Bits 24..28 = 0x1f; dst in bits 6..11, src in bits 0..5, IsRequest in bit 16.
  const uint32_t id = command_can_id(1, 2, 0, false, false, true, 0);
  TEST_ASSERT_EQUAL_UINT32(0x1fu, id >> 24);
  TEST_ASSERT_EQUAL_UINT8(1, id & 0x3f);
  TEST_ASSERT_EQUAL_UINT8(2, (id >> 6) & 0x3f);
  TEST_ASSERT_TRUE((id & (1u << 16)) != 0);
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_level_low);
  RUN_TEST(test_level_away_is_value_zero);
  RUN_TEST(test_boost_on_carries_its_duration_little_endian);
  RUN_TEST(test_boost_off);
  RUN_TEST(test_away_on_and_off);
  RUN_TEST(test_manual_and_auto);
  RUN_TEST(test_bypass_activate_deactivate_auto);
  RUN_TEST(test_supply_and_exhaust_only_both_use_value_01);
  RUN_TEST(test_balanced_is_two_frames_and_neither_had_a_test);
  RUN_TEST(test_the_remaining_levels_and_the_middle_profile);
  RUN_TEST(test_temperature_profiles_are_indefinite);
  RUN_TEST(test_sensor_ventilation_is_a_direct_property_write);
  RUN_TEST(test_enable_is_thirteen_bytes_and_disable_is_four);
  RUN_TEST(test_short_commands_are_sent_verbatim);
  RUN_TEST(test_long_commands_are_chunked_with_a_final_bit);
  RUN_TEST(test_an_empty_command_produces_no_frames);
  RUN_TEST(test_command_can_id_places_its_fields);
  return UNITY_END();
}
