// The unit's active errors in the state model (comfoair_state_set_unit_errors): two rows no PDO
// carries, written by the probe task by their generated indices, unknown until the first read.
//
// The setter stores the flag and the text it is HANDED. Which ids raise the flag is the summary's
// decision (comfoair_sysinfo): an informational id -- 89 "Bypass is in manual mode", 80 service
// mode, 79 "Order new filters" -- is listed in the text but does not turn unit_error on, so the
// informational vectors below arrive with any == false.
#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>

#include "comfoair_state.h"
#include "registry_generated.h"

void setUp(void) { comfoair_state_reset(); }
void tearDown(void) {}

namespace {

// Base-unit texts from the firmware's own error table (comfoair_sysinfo_errors.cpp): 50 and 51 are
// faults of the unit itself, 89 is informational.
const char *const kFault =
    "The supply air flow sensor has a malfunction (SENSOR_SUP ERROR)";
const char *const kOtherFault    = "The extract air fan has a malfunction (FAN_EHA ERROR)";
const char *const kInformational = "Bypass is in manual mode";

comfoair_value_t value_at(size_t index) {
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get(COMFOAIR_ENTITIES[index].key, &v));
  return v;
}

bool changed(size_t index) {
  return comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index) &&
         comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, index);
}

bool unmarked(size_t index) {
  return !comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index) &&
         !comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, index);
}

void clear_both(void) {
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
}

}  // namespace

void test_the_indices_name_a_problem_binary_and_a_text_with_no_pdo(void) {
  const comfoair_entity_t &any  = COMFOAIR_ENTITIES[COMFOAIR_UNIT_ERROR_ANY];
  const comfoair_entity_t &text = COMFOAIR_ENTITIES[COMFOAIR_UNIT_ERROR_TEXT];
  TEST_ASSERT_EQUAL_STRING("unit_error", any.key);
  TEST_ASSERT_EQUAL_STRING("active_errors", text.key);
  TEST_ASSERT_EQUAL(COMFOAIR_KIND_BINARY, any.kind);
  TEST_ASSERT_EQUAL_STRING("problem", any.device_class);
  TEST_ASSERT_EQUAL(COMFOAIR_KIND_SPECIAL, text.kind);
  TEST_ASSERT_EQUAL_INT16(-1, any.pdo);
  TEST_ASSERT_EQUAL_INT16(-1, text.pdo);
  TEST_ASSERT_TRUE(std::isnan(any.default_value));
  TEST_ASSERT_TRUE(std::isnan(text.default_value));
}

void test_both_are_unknown_until_the_first_read(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, value_at(COMFOAIR_UNIT_ERROR_ANY).type);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, value_at(COMFOAIR_UNIT_ERROR_TEXT).type);
}

void test_a_read_with_no_errors_sets_off_and_the_text(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  const comfoair_value_t any = value_at(COMFOAIR_UNIT_ERROR_ANY);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_BOOL, any.type);
  TEST_ASSERT_FALSE(any.boolean);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, value_at(COMFOAIR_UNIT_ERROR_TEXT).type);
  TEST_ASSERT_EQUAL_STRING("No errors", value_at(COMFOAIR_UNIT_ERROR_TEXT).text);
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_TEXT));
}

void test_the_same_answer_again_marks_nothing(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  clear_both();
  const std::string copy("No errors");  // equal by text, not by pointer
  TEST_ASSERT_FALSE(comfoair_state_set_unit_errors(false, copy.c_str()));
  TEST_ASSERT_TRUE(unmarked(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(unmarked(COMFOAIR_UNIT_ERROR_TEXT));
}

void test_a_new_fault_marks_both(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  TEST_ASSERT_TRUE(value_at(COMFOAIR_UNIT_ERROR_ANY).boolean);
  TEST_ASSERT_EQUAL_STRING(kFault, value_at(COMFOAIR_UNIT_ERROR_TEXT).text);
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_TEXT));
}

// The owner's rule: a user-initiated bypass override is listed, never a problem. The setter keeps
// the flag off as it is handed, and only the text row moves.
void test_an_informational_id_changes_the_text_and_leaves_the_flag_off(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, kInformational));
  const comfoair_value_t any = value_at(COMFOAIR_UNIT_ERROR_ANY);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_BOOL, any.type);
  TEST_ASSERT_FALSE(any.boolean);
  TEST_ASSERT_EQUAL_STRING(kInformational, value_at(COMFOAIR_UNIT_ERROR_TEXT).text);
  TEST_ASSERT_TRUE(unmarked(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_TEXT));
}

void test_a_changed_text_with_the_flag_unchanged_marks_only_the_text(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kOtherFault));
  TEST_ASSERT_TRUE(unmarked(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_TEXT));
}

void test_a_changed_flag_with_the_text_unchanged_marks_only_the_flag(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, kFault));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  TEST_ASSERT_TRUE(value_at(COMFOAIR_UNIT_ERROR_ANY).boolean);
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(unmarked(COMFOAIR_UNIT_ERROR_TEXT));
}

void test_a_fault_clearing_marks_both(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  TEST_ASSERT_FALSE(value_at(COMFOAIR_UNIT_ERROR_ANY).boolean);
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_ANY));
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_TEXT));
}

void test_a_change_raises_the_generation_and_a_repeat_does_not(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  const uint32_t g0 =
      comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, COMFOAIR_UNIT_ERROR_TEXT);
  TEST_ASSERT_FALSE(comfoair_state_set_unit_errors(false, "No errors"));
  TEST_ASSERT_EQUAL_UINT32(
      g0, comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, COMFOAIR_UNIT_ERROR_TEXT));
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  TEST_ASSERT_FALSE(comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT,
                                                   COMFOAIR_UNIT_ERROR_TEXT, g0));
  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, COMFOAIR_UNIT_ERROR_TEXT));
}

void test_a_reader_holding_the_old_text_still_reads_it(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "No errors"));
  const char *old = value_at(COMFOAIR_UNIT_ERROR_TEXT).text;
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  TEST_ASSERT_EQUAL_STRING("No errors", old);
  TEST_ASSERT_EQUAL_STRING(kFault, value_at(COMFOAIR_UNIT_ERROR_TEXT).text);
}

void test_the_stored_text_is_a_copy_not_the_callers_buffer(void) {
  char buffer[128];
  std::strcpy(buffer, kFault);
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, buffer));
  std::strcpy(buffer, "scribbled");
  const comfoair_value_t text = value_at(COMFOAIR_UNIT_ERROR_TEXT);
  TEST_ASSERT_TRUE(text.text != buffer);
  TEST_ASSERT_EQUAL_STRING(kFault, text.text);
}

void test_a_text_over_the_state_limit_or_null_is_refused(void) {
  const std::string long_text(COMFOAIR_STATE_TEXT_MAX + 1, 'x');
  TEST_ASSERT_FALSE(comfoair_state_set_unit_errors(true, long_text.c_str()));
  TEST_ASSERT_FALSE(comfoair_state_set_unit_errors(true, nullptr));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, value_at(COMFOAIR_UNIT_ERROR_ANY).type);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, value_at(COMFOAIR_UNIT_ERROR_TEXT).type);
  TEST_ASSERT_TRUE(unmarked(COMFOAIR_UNIT_ERROR_ANY));
  const std::string at_limit(COMFOAIR_STATE_TEXT_MAX, 'x');
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, at_limit.c_str()));
  TEST_ASSERT_EQUAL_STRING(at_limit.c_str(), value_at(COMFOAIR_UNIT_ERROR_TEXT).text);
}

void test_reset_forgets_them(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  comfoair_state_reset();
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, value_at(COMFOAIR_UNIT_ERROR_ANY).type);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, value_at(COMFOAIR_UNIT_ERROR_TEXT).type);
  // After a reset the same answer is new again and must be published.
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, kFault));
  TEST_ASSERT_TRUE(changed(COMFOAIR_UNIT_ERROR_ANY));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_indices_name_a_problem_binary_and_a_text_with_no_pdo);
  RUN_TEST(test_both_are_unknown_until_the_first_read);
  RUN_TEST(test_a_read_with_no_errors_sets_off_and_the_text);
  RUN_TEST(test_the_same_answer_again_marks_nothing);
  RUN_TEST(test_a_new_fault_marks_both);
  RUN_TEST(test_an_informational_id_changes_the_text_and_leaves_the_flag_off);
  RUN_TEST(test_a_changed_text_with_the_flag_unchanged_marks_only_the_text);
  RUN_TEST(test_a_changed_flag_with_the_text_unchanged_marks_only_the_flag);
  RUN_TEST(test_a_fault_clearing_marks_both);
  RUN_TEST(test_a_change_raises_the_generation_and_a_repeat_does_not);
  RUN_TEST(test_a_reader_holding_the_old_text_still_reads_it);
  RUN_TEST(test_the_stored_text_is_a_copy_not_the_callers_buffer);
  RUN_TEST(test_a_text_over_the_state_limit_or_null_is_refused);
  RUN_TEST(test_reset_forgets_them);
  return UNITY_END();
}
