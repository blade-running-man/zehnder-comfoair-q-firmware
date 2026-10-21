// PDO 230's text lives in a buffer of comfoair_state's own, not in the registry, and the change
// marks compare TEXT by pointer (comfoair_state_decode.cpp same_value). Rendered into one buffer,
// every change after the first compared equal and was never marked: MQTT and the WebSocket kept
// the boot's first constraint text while GET /api/state showed the current one.
//
// Vectors: bit 45 (byte 5, 0x20) says a list follows; bit 9 = Frost Protection, bit 10 = Bypass
// (byte 1) -- components/comfoair_protocol/include/airflow_constraints.h.
#include <unity.h>

#include <cstring>

#include "comfoair_state.h"
#include "registry_generated.h"

void setUp(void) { comfoair_state_reset(); }
void tearDown(void) {}

namespace {

const uint8_t FROST[]  = {0x00, 0x02, 0x00, 0x00, 0x00, 0x20};
const uint8_t BYPASS[] = {0x00, 0x04, 0x00, 0x00, 0x00, 0x20};

size_t index_of(const char *key) {
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    if (strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
      return i;
  TEST_FAIL_MESSAGE(key);
  return 0;
}

const char *text_of(const char *key) {
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get(key, &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  return v.text;
}

void clear_both(void) {
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
}

}  // namespace

void test_a_changed_constraint_is_marked_for_both_consumers(void) {
  const size_t i = index_of("airflow_constraints");
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  TEST_ASSERT_EQUAL_STRING("Below: Frost Protection", text_of("airflow_constraints"));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, BYPASS, sizeof BYPASS));
  TEST_ASSERT_EQUAL_STRING("Below: Bypass", text_of("airflow_constraints"));
  TEST_ASSERT_TRUE_MESSAGE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, i),
                           "MQTT would never publish the new constraint");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, i),
                           "the WebSocket would never push the new constraint");
}

void test_the_same_constraint_again_is_not_a_change(void) {
  const size_t i = index_of("airflow_constraints");
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  TEST_ASSERT_FALSE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, i));
}

void test_a_reader_holding_the_old_pointer_still_reads_the_old_text(void) {
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  const char *old = text_of("airflow_constraints");
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, BYPASS, sizeof BYPASS));
  TEST_ASSERT_EQUAL_STRING("Below: Frost Protection", old);
}

void test_an_undecodable_frame_becomes_no_value_and_is_marked(void) {
  const size_t i = index_of("airflow_constraints");
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, sizeof FROST));
  clear_both();
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, FROST, 0));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("airflow_constraints", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, i));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_changed_constraint_is_marked_for_both_consumers);
  RUN_TEST(test_the_same_constraint_again_is_not_a_change);
  RUN_TEST(test_a_reader_holding_the_old_pointer_still_reads_the_old_text);
  RUN_TEST(test_an_undecodable_frame_becomes_no_value_and_is_marked);
  return UNITY_END();
}
