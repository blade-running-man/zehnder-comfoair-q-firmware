// An entity with no value publishes Home Assistant's own "no value" payload, `None`, on every
// state topic -- so a retained reading that has gone away stops being shown as current. The live
// case that raised it: next_fan_change_in_raw retained 6766 on the broker while REST said null.
//
// Its own suite because test_ha.cpp sits at the 600-line suite ceiling. It walks the registry, so a
// row added later is covered without editing this file.
//
// What Home Assistant 2026.9 does with the literal (homeassistant/components/mqtt/):
//   sensor        sensor.py:317-319, before the numeric (:321) and the text (:327-341) branches
//   binary_sensor binary_sensor.py:203-204       switch  switch.py:108-112
//   select        select.py:124-126 (lower-cased) number  number.py:95 + :179, default const.py:308
//   fan           fan.py:347 state, :158 + :359 percentage, :161 + :394 preset
// const.py:383 PAYLOAD_NONE = "None". An EMPTY payload is no alternative: retained, it deletes the
// broker's copy (MQTT 3.1.1 3.3.1.3), and the handlers ignore it (sensor.py:322, select.py:117).
#include <unity.h>

#include <cstring>
#include <strings.h>

#include "comfoair_ha.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_buf[4096];

comfoair_ha_ctx_t ctx(void) {
  comfoair_ha_ctx_t c = {};
  c.device_id    = "aabbccddeeff";
  c.device_name  = "ComfoAir Q eeff";
  c.topic_prefix = "comfoair/aabbccddeeff";
  c.sw_version   = "0.1.0";
  c.mac          = "aa:bb:cc:dd:ee:ff";
  c.url          = "http://192.168.1.42/";
  return c;
}

}  // namespace

void test_the_literal_is_home_assistants_own(void) {
  TEST_ASSERT_EQUAL_STRING("None", COMFOAIR_HA_PAYLOAD_NONE);
}

void test_no_reading_publishes_None_on_every_state_topic(void) {
  size_t covered = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_ha_component(e) == nullptr || e->kind == COMFOAIR_KIND_FAN)
      continue;
    comfoair_value_t v = {};
    v.type = COMFOAIR_VALUE_NONE;
    bool skip = true;
    TEST_ASSERT_TRUE_MESSAGE(comfoair_ha_state_payload(e, &v, &skip, g_buf, sizeof g_buf) > 0,
                             e->key);
    TEST_ASSERT_FALSE_MESSAGE(skip, e->key);
    TEST_ASSERT_EQUAL_STRING_MESSAGE("None", g_buf, e->key);
    covered++;
  }
  TEST_ASSERT_TRUE(covered > 70);
}

void test_an_empty_or_missing_text_publishes_None_never_an_empty_payload(void) {
  const comfoair_entity_t *e = comfoair_entity_by_key("airflow_constraints");
  TEST_ASSERT_NOT_NULL(e);
  comfoair_value_t v = {};
  v.type = COMFOAIR_VALUE_TEXT;
  bool skip = true;
  v.text = nullptr;
  TEST_ASSERT_TRUE(comfoair_ha_state_payload(e, &v, &skip, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("None", g_buf);
  v.text = "";
  TEST_ASSERT_TRUE(comfoair_ha_state_payload(e, &v, &skip, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("None", g_buf);
}

void test_a_value_still_publishes_the_value(void) {
  comfoair_value_t v = {};
  v.type = COMFOAIR_VALUE_NUMBER;
  v.number = 21.5f;
  bool skip = true;
  TEST_ASSERT_TRUE(comfoair_ha_state_payload(comfoair_entity_by_key("supply_air_temp"), &v, &skip,
                                             g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("21.5", g_buf);
}

void test_a_fan_the_unit_has_not_described_publishes_None_on_all_three_leaves(void) {
  comfoair_fan_state_t fan = {};
  fan.known = false;
  TEST_ASSERT_TRUE(comfoair_ha_fan_state_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("None", g_buf);
  TEST_ASSERT_TRUE(comfoair_ha_fan_speed_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("None", g_buf);
  TEST_ASSERT_TRUE(comfoair_ha_fan_preset_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("None", g_buf);
}

void test_no_label_is_spelled_none(void) {
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    for (uint8_t o = 0; e->options != nullptr && o < e->option_count; o++)
      TEST_ASSERT_NOT_EQUAL_MESSAGE(0, strcasecmp(e->options[o], "none"), e->key);
  }
}

void test_no_document_overrides_the_none_handling(void) {
  // A value_template runs BEFORE the comparison (sensor.py:302-306) and a payload_reset changes
  // the literal (number.py:95, fan.py:158/161); either would undo everything above.
  const comfoair_ha_ctx_t c = ctx();
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_ha_component(e) == nullptr)
      continue;
    TEST_ASSERT_TRUE_MESSAGE(comfoair_ha_discovery_payload(&c, e, g_buf, sizeof g_buf) > 0, e->key);
    TEST_ASSERT_NULL_MESSAGE(strstr(g_buf, "val_tpl"), e->key);
    TEST_ASSERT_NULL_MESSAGE(strstr(g_buf, "value_template"), e->key);
    TEST_ASSERT_NULL_MESSAGE(strstr(g_buf, "pl_rst"), e->key);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_literal_is_home_assistants_own);
  RUN_TEST(test_no_reading_publishes_None_on_every_state_topic);
  RUN_TEST(test_an_empty_or_missing_text_publishes_None_never_an_empty_payload);
  RUN_TEST(test_a_value_still_publishes_the_value);
  RUN_TEST(test_a_fan_the_unit_has_not_described_publishes_None_on_all_three_leaves);
  RUN_TEST(test_no_label_is_spelled_none);
  RUN_TEST(test_no_document_overrides_the_none_handling);
  return UNITY_END();
}
