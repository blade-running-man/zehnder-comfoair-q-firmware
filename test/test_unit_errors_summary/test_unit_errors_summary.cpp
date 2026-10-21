// The unit's active errors for the two Home Assistant entities (comfoair_sysinfo.h): `unit_error`
// (comfoair_sysinfo_errors_any -- a real fault, never a notice) and `active_errors`
// (comfoair_sysinfo_errors_summary -- one line, every id). Texts are written by hand from the error
// table (aiocomfoconnect const.py ERRORS / ERRORS_140, which comfoair_sysinfo_errors.cpp matches
// byte for byte), not copied from the implementation. Bit b of byte i is id i*8+b
// (comfoair_sysinfo.cpp render_errors).
#include <unity.h>

#include <cstring>
#include <set>
#include <string>

#include "comfoair_sysinfo.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

constexpr uint32_t R1_11_0 = 3222285312u;  // 0xC0102C00, the owner's Q350
constexpr uint32_t R1_4_0  = 3222278144u;  // 0xC0101000, the last old numbering
char g_out[COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX + 1];

struct Bits {
  uint8_t b[COMFOAIR_SYSINFO_ERRORS_BYTES] = {};
  Bits &set(unsigned id) { b[id / 8] |= static_cast<uint8_t>(1u << (id % 8)); return *this; }
};

std::string summary(const Bits &bits, bool has_fw = true, uint32_t fw = R1_11_0) {
  const size_t n = comfoair_sysinfo_errors_summary(bits.b, has_fw, fw, g_out, sizeof g_out);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_size_t(strlen(g_out), n);
  return g_out;
}

bool any(const Bits &bits, bool has_fw = true, uint32_t fw = R1_11_0) {
  return comfoair_sysinfo_errors_any(bits.b, has_fw, fw);
}

bool valid_utf8(const std::string &s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    const size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (len == 0 || i + len > s.size())
      return false;
    for (size_t k = 1; k < len; k++)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80)
        return false;
    i += len;
  }
  return true;
}

std::set<unsigned> informational_ids(bool has_fw, uint32_t fw) {
  std::set<unsigned> ids;
  for (unsigned id = 0; id < COMFOAIR_SYSINFO_ERRORS_BYTES * 8u; id++)
    if (comfoair_sysinfo_error_informational(static_cast<uint16_t>(id), has_fw, fw))
      ids.insert(id);
  return ids;
}

}  // namespace

void test_no_bit_is_no_errors_and_not_None(void) {
  const Bits none;
  TEST_ASSERT_FALSE(any(none));
  TEST_ASSERT_EQUAL_STRING("No errors", summary(none).c_str());
}

void test_one_error_is_its_text(void) {
  Bits b;
  b.set(21);
  TEST_ASSERT_TRUE(any(b));
  TEST_ASSERT_EQUAL_STRING("DANGER! OVERHEATING! Two or more sensors are detecting an incorrect "
                           "temperature. Ventilation has stopped.", summary(b).c_str());
}

// The owner's own doing must never raise a Home Assistant problem: a manual bypass alone is not a
// unit error, and the line still says it.
void test_a_manual_bypass_alone_is_no_problem_but_is_listed(void) {
  Bits b;
  b.set(89);
  TEST_ASSERT_FALSE(any(b));
  TEST_ASSERT_EQUAL_STRING("Bypass is in manual mode", summary(b).c_str());
}

void test_notices_only_is_no_problem_and_every_one_is_listed(void) {
  Bits b;
  b.set(79).set(80).set(89).set(96);
  TEST_ASSERT_FALSE(any(b));
  TEST_ASSERT_EQUAL_STRING("Order new filters now, because the remaining filter life time is "
                           "limited | Service mode is active (SERVICE MODE) | Bypass is in manual "
                           "mode | ComfoHood is activated", summary(b).c_str());
}

void test_a_fault_beside_notices_is_a_problem(void) {
  Bits b;
  b.set(89).set(51);
  TEST_ASSERT_TRUE(any(b));
  TEST_ASSERT_EQUAL_STRING("The extract air fan has a malfunction (FAN_EHA ERROR) | Bypass is in "
                           "manual mode", summary(b).c_str());
}

void test_two_errors_ascending_with_the_separator_on_newer_firmware(void) {
  Bits b;
  b.set(79).set(77);
  TEST_ASSERT_TRUE(any(b));  // 77 "replaced now" is an error message on the unit's own display
  TEST_ASSERT_EQUAL_STRING("The filters of the Ventilation Unit must be replaced now | Order new "
                           "filters now, because the remaining filter life time is limited",
                           summary(b).c_str());
}

void test_the_same_bits_read_the_old_numbering_at_r1_4_0(void) {
  Bits b;
  b.set(79).set(77);
  TEST_ASSERT_EQUAL_STRING("Preheater has no communication with the ComfoAir unit (PREHEAT ERROR , "
                           "1081) | Postheater temperature error (POSTHEAT_TEMP ERROR)",
                           summary(b, true, R1_4_0).c_str());
}

// The informational flag follows the firmware's numbering, never the bare id: 81 is the manual
// bypass at R1.4.0 and a preheater fault above it; 89 is the manual bypass only above R1.4.0.
void test_informational_follows_the_numbering(void) {
  Bits b81;
  b81.set(81);
  TEST_ASSERT_FALSE(any(b81, true, R1_4_0));
  TEST_ASSERT_EQUAL_STRING("Bypass is in manual mode", summary(b81, true, R1_4_0).c_str());
  TEST_ASSERT_TRUE(any(b81, true, R1_11_0));
  Bits b89;
  b89.set(89);
  TEST_ASSERT_TRUE(any(b89, true, R1_4_0));  // an id the old table does not know
}

// Before the firmware word is read, which numbering applies is unknown, so no id is a notice: the
// newer table's notices would hide an old unit's faults (old 79 POSTHEAT_TEMP, old 80
// GROUND_HEAT_TEMP). The text still guesses the newer table; only the verdict refuses to.
void test_before_the_firmware_is_read_every_bit_is_a_problem(void) {
  Bits b;
  b.set(89);
  TEST_ASSERT_TRUE(any(b, false, 0));
  TEST_ASSERT_EQUAL_STRING("Bypass is in manual mode", summary(b, false, 0).c_str());
  Bits old;
  old.set(79).set(80);
  TEST_ASSERT_TRUE(any(old, false, 0));
}

// Every id's class, pinned as a whole: a row turned informational by accident fails here.
void test_the_informational_ids_of_each_table(void) {
  TEST_ASSERT_TRUE((std::set<unsigned>{79, 80, 89, 96}) == informational_ids(true, R1_11_0));
  TEST_ASSERT_TRUE(informational_ids(false, 0).empty());
  TEST_ASSERT_TRUE((std::set<unsigned>{75, 76, 81}) == informational_ids(true, R1_4_0));
}

void test_without_a_firmware_read_the_newer_table_is_used(void) {
  Bits b;
  b.set(77);
  TEST_ASSERT_EQUAL_STRING("The filters of the Ventilation Unit must be replaced now",
                           summary(b, false, 0).c_str());
}

void test_an_id_the_table_does_not_know(void) {
  Bits b;
  b.set(0).set(255);
  TEST_ASSERT_TRUE(any(b));  // unknown is never assumed harmless
  TEST_ASSERT_EQUAL_STRING("Unknown error 0 | Unknown error 255", summary(b).c_str());
}

void test_a_long_list_is_cut_to_the_state_limit_and_says_so(void) {
  Bits b;
  for (unsigned id = 21; id <= 69; id++)
    b.set(id);
  const std::string s = summary(b);
  TEST_ASSERT_TRUE(s.size() <= COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX);
  TEST_ASSERT_EQUAL_STRING("...", s.substr(s.size() - 3).c_str());
  TEST_ASSERT_TRUE(valid_utf8(s));
}

void test_every_cut_is_valid_utf8(void) {
  // Ids 100, 102 and 104 carry multi-byte characters; growing a prefix around them moves the cut
  // across every byte position of those texts at least once.
  for (unsigned last = 21; last <= 104; last++) {
    Bits b;
    b.set(100).set(102).set(104);
    for (unsigned id = 21; id <= last; id++)
      b.set(id);
    for (bool old : {false, true}) {
      const std::string s = summary(b, true, old ? R1_4_0 : R1_11_0);
      TEST_ASSERT_TRUE_MESSAGE(s.size() <= COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX, s.c_str());
      TEST_ASSERT_TRUE_MESSAGE(valid_utf8(s), s.c_str());
    }
  }
}

void test_a_buffer_without_room_for_the_limit_is_refused(void) {
  Bits b;
  char small[COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_sysinfo_errors_summary(b.b, true, R1_11_0, small, sizeof small));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_sysinfo_errors_summary(nullptr, true, R1_11_0, g_out, sizeof g_out));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_sysinfo_errors_summary(b.b, true, R1_11_0, nullptr, sizeof g_out));
  TEST_ASSERT_FALSE(comfoair_sysinfo_errors_any(nullptr, true, R1_11_0));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_no_bit_is_no_errors_and_not_None);
  RUN_TEST(test_one_error_is_its_text);
  RUN_TEST(test_a_manual_bypass_alone_is_no_problem_but_is_listed);
  RUN_TEST(test_notices_only_is_no_problem_and_every_one_is_listed);
  RUN_TEST(test_a_fault_beside_notices_is_a_problem);
  RUN_TEST(test_two_errors_ascending_with_the_separator_on_newer_firmware);
  RUN_TEST(test_the_same_bits_read_the_old_numbering_at_r1_4_0);
  RUN_TEST(test_informational_follows_the_numbering);
  RUN_TEST(test_before_the_firmware_is_read_every_bit_is_a_problem);
  RUN_TEST(test_the_informational_ids_of_each_table);
  RUN_TEST(test_without_a_firmware_read_the_newer_table_is_used);
  RUN_TEST(test_an_id_the_table_does_not_know);
  RUN_TEST(test_a_long_list_is_cut_to_the_state_limit_and_says_so);
  RUN_TEST(test_every_cut_is_valid_utf8);
  RUN_TEST(test_a_buffer_without_room_for_the_limit_is_refused);
  return UNITY_END();
}
