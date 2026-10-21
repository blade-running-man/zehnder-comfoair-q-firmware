// enabled_by_default: one registry field drives Home Assistant's `en`, for every entity.
//
// Its own suite because test_ha.cpp sits at the 600-line suite ceiling. Walks the whole
// registry so a row added later is covered without anyone editing this file.
#include <unity.h>

#include <cstring>

#include "comfoair_config.h"
#include "comfoair_ha.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_buf[4096];

comfoair_ha_ctx_t ctx(void) {
  comfoair_ha_ctx_t c = {};
  c.device_id   = "aabbccddeeff";
  c.device_name = "ComfoAir Q eeff";
  c.topic_prefix = "comfoair/aabbccddeeff";
  c.sw_version  = "0.1.0";
  c.mac         = "aa:bb:cc:dd:ee:ff";
  c.url         = "http://192.168.1.42/";
  return c;
}

}  // namespace

void test_en_false_appears_exactly_on_the_rows_the_table_disables(void) {
  const comfoair_ha_ctx_t c = ctx();
  size_t discovered = 0;
  size_t disabled_count = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_ha_component(e) == nullptr)
      continue;  // a kind Home Assistant does not get a document for
    // A published kind whose document does not render is a failure, not a skip: skipping it
    // would let the very row this suite is about drop out of the check unnoticed.
    const size_t n = comfoair_ha_discovery_payload(&c, e, g_buf, sizeof g_buf);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, e->key);
    discovered++;
    const bool disabled = strstr(g_buf, "\"en\":false") != nullptr;
    disabled_count += disabled ? 1 : 0;
    TEST_ASSERT_EQUAL_MESSAGE(!e->enabled_by_default, disabled, e->key);
  }
  TEST_ASSERT_TRUE(discovered > 0);
  // Not vacuous: the table disables at least the ventilation_mode select today.
  TEST_ASSERT_TRUE(disabled_count > 0);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_en_false_appears_exactly_on_the_rows_the_table_disables);
  return UNITY_END();
}
