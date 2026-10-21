// comfoair_state_get_copy(): a value whose text is copied into the CALLER's buffer while the state
// lock is still held. The MQTT publisher renders its payload after the lock is released; with
// comfoair_state_get() it held only a POINTER into the model's text buffers (PDO 230's constraint
// text, the active-errors text), and a writer that flipped the double buffer twice inside that
// window could hand it a string being rewritten. A copy taken under the lock is the caller's for as
// long as it likes -- which is what the cases below pin: the copy survives every later write.
//
// Vectors: PDO 230 bit 45 (byte 5, 0x20) says a list follows; bit 9 = Frost Protection, bit 10 =
// Bypass (byte 1) -- components/comfoair_protocol/include/airflow_constraints.h.
#include <unity.h>

#include <cstring>
#include <string>

#include "comfoair_state.h"
#include "registry_generated.h"

void setUp(void) { comfoair_state_reset(); }
void tearDown(void) {}

namespace {

const uint8_t FROST[]  = {0x00, 0x02, 0x00, 0x00, 0x00, 0x20};
const uint8_t BYPASS[] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x20};

constexpr size_t CAP = COMFOAIR_STATE_TEXT_MAX + 1;

}  // namespace

void test_an_unknown_key_or_a_missing_argument_is_refused(void) {
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_FALSE(comfoair_state_get_copy("no_such_entity", &v, buf, sizeof buf));
  TEST_ASSERT_FALSE(comfoair_state_get_copy(nullptr, &v, buf, sizeof buf));
  TEST_ASSERT_FALSE(comfoair_state_get_copy("active_errors", nullptr, buf, sizeof buf));
  TEST_ASSERT_FALSE(comfoair_state_get_copy("active_errors", &v, nullptr, sizeof buf));
}

void test_a_buffer_smaller_than_the_longest_text_is_refused_whatever_the_value(void) {
  // Refused by SIZE, not by what happens to be stored: a cap that fits today's short text and not
  // tomorrow's long one would fail on a live unit only, the day an error list grew.
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_FALSE(comfoair_state_get_copy("supply_fan_speed", &v, buf, CAP - 1));
  TEST_ASSERT_FALSE(comfoair_state_get_copy("schema_version", &v, buf, 0));
}

void test_a_number_reads_as_comfoair_state_get_reads_it(void) {
  comfoair_value_t copy{};
  comfoair_value_t plain{};
  char             buf[CAP];
  TEST_ASSERT_TRUE(comfoair_state_get_copy("schema_version", &copy, buf, sizeof buf));
  TEST_ASSERT_TRUE(comfoair_state_get("schema_version", &plain));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NUMBER, copy.type);
  TEST_ASSERT_EQUAL_FLOAT(plain.number, copy.number);
}

void test_no_value_reads_as_none(void) {
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_TRUE(comfoair_state_get_copy("active_errors", &v, buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

void test_the_errors_text_is_the_callers_copy_and_survives_two_later_writes(void) {
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, "first"));
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_TRUE(comfoair_state_get_copy("active_errors", &v, buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_PTR_MESSAGE(buf, v.text, "the text must point at the caller's buffer");
  // Two writes flip the double buffer back onto the half the first text lived in -- exactly the
  // overwrite a reader holding the model's pointer would have rendered half of.
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, "second"));
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(false, "third, and longer than the first"));
  TEST_ASSERT_EQUAL_STRING("first", v.text);
}

void test_the_longest_errors_text_is_copied_whole(void) {
  const std::string longest(COMFOAIR_STATE_TEXT_MAX, 'x');
  TEST_ASSERT_TRUE(comfoair_state_set_unit_errors(true, longest.c_str()));
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_TRUE(comfoair_state_get_copy("active_errors", &v, buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING(longest.c_str(), v.text);
}

void test_the_constraint_text_is_the_callers_copy_and_survives_two_later_frames(void) {
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_TRUE(comfoair_state_get_copy("airflow_constraints", &v, buf, sizeof buf));
  TEST_ASSERT_EQUAL_PTR(buf, v.text);
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, BYPASS, sizeof BYPASS));
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, BYPASS, sizeof BYPASS));
  TEST_ASSERT_EQUAL_STRING("Below: Frost Protection", v.text);
}

void test_a_registry_label_is_copied_too(void) {
  // One rule for every text, so the caller never has to know which kind of pointer it was handed.
  TEST_ASSERT_TRUE(comfoair_state_set_option("humidity_comfort", "On"));
  comfoair_value_t v{};
  char             buf[CAP];
  TEST_ASSERT_TRUE(comfoair_state_get_copy("humidity_comfort", &v, buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_PTR(buf, v.text);
  TEST_ASSERT_EQUAL_STRING("On", v.text);
}

void test_no_registry_label_is_longer_than_the_copy_holds(void) {
  // The copy is never cut because nothing the model stores is longer: the two rendered texts are
  // capped at COMFOAIR_STATE_TEXT_MAX by their writers, and this pins the labels.
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    for (uint8_t o = 0; o < COMFOAIR_ENTITIES[i].option_count; o++)
      TEST_ASSERT_TRUE_MESSAGE(strlen(COMFOAIR_ENTITIES[i].options[o]) <= COMFOAIR_STATE_TEXT_MAX,
                               COMFOAIR_ENTITIES[i].key);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_an_unknown_key_or_a_missing_argument_is_refused);
  RUN_TEST(test_a_buffer_smaller_than_the_longest_text_is_refused_whatever_the_value);
  RUN_TEST(test_a_number_reads_as_comfoair_state_get_reads_it);
  RUN_TEST(test_no_value_reads_as_none);
  RUN_TEST(test_the_errors_text_is_the_callers_copy_and_survives_two_later_writes);
  RUN_TEST(test_the_longest_errors_text_is_copied_whole);
  RUN_TEST(test_the_constraint_text_is_the_callers_copy_and_survives_two_later_frames);
  RUN_TEST(test_a_registry_label_is_copied_too);
  RUN_TEST(test_no_registry_label_is_longer_than_the_copy_holds);
  return UNITY_END();
}
