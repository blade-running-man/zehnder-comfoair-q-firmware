// The RMI read-back of the three Off/Auto only/On selects (comfoair_readback.h).
//
// Three things are pinned here, and each is a way the read-back can be wrong while compiling:
//  * the decode: one U8 that indexes the entity's own options, nothing longer, nothing shorter;
//  * the command-path guard: a reply is applied only when no command was queued, in flight or
//    submitted across the read -- equality on the counters, so wraparound is not a special case;
//  * the generated table against the WRITE builders: a read-back that reads a different property
//    than the one the owner's command writes would show one setting and change another.
#include <unity.h>

#include <math.h>
#include <stdint.h>
#include <string.h>

#include "comfoair_readback.h"
#include "registry_generated.h"
#include "rmi_frame.h"

namespace rmi = comfoair::rmi;

static const comfoair_rmi_readback_t *row(size_t i) { return &COMFOAIR_RMI_READBACK[i]; }

// --- decode ------------------------------------------------------------------------------------

void test_each_option_index_decodes(void) {
  for (size_t i = 0; i < COMFOAIR_RMI_READBACK_COUNT; i++) {
    for (uint8_t v = 0; v <= 2; v++) {
      const uint8_t data[] = {v};
      uint8_t index = 0xEE;
      TEST_ASSERT_TRUE(comfoair_readback_decode(row(i), data, 1, &index));
      TEST_ASSERT_EQUAL_UINT8(v, index);
    }
  }
}

void test_a_value_past_the_options_is_refused(void) {
  const uint8_t data[] = {3};
  uint8_t index = 0xEE;
  TEST_ASSERT_FALSE(comfoair_readback_decode(row(0), data, 1, &index));
  const uint8_t ff[] = {0xFF};
  TEST_ASSERT_FALSE(comfoair_readback_decode(row(0), ff, 1, &index));
}

void test_a_wrong_length_is_refused(void) {
  const uint8_t data[] = {1, 0};
  uint8_t index = 0xEE;
  TEST_ASSERT_FALSE(comfoair_readback_decode(row(0), data, 0, &index));
  TEST_ASSERT_FALSE(comfoair_readback_decode(row(0), data, 2, &index));
}

void test_null_arguments_are_refused(void) {
  const uint8_t data[] = {1};
  uint8_t index = 0xEE;
  TEST_ASSERT_FALSE(comfoair_readback_decode(row(0), NULL, 1, &index));
  TEST_ASSERT_FALSE(comfoair_readback_decode(NULL, data, 1, &index));
  TEST_ASSERT_FALSE(comfoair_readback_decode(row(0), data, 1, NULL));
}

void test_an_entry_outside_the_registry_is_refused(void) {
  const comfoair_rmi_readback_t bad = {COMFOAIR_ENTITY_COUNT, 0x1d, 0x01, 0x04};
  const uint8_t data[] = {1};
  uint8_t index = 0xEE;
  TEST_ASSERT_FALSE(comfoair_readback_decode(&bad, data, 1, &index));
}

// --- the command-path guard --------------------------------------------------------------------

void test_an_idle_sender_with_nothing_new_may_apply(void) {
  TEST_ASSERT_TRUE(comfoair_readback_may_apply(5, 5, 5));
  TEST_ASSERT_TRUE(comfoair_readback_may_apply(0, 0, 0));
}

void test_a_command_in_flight_refuses(void) {
  // Queued but not yet sent: its SET may reach the wire after our GET.
  TEST_ASSERT_FALSE(comfoair_readback_may_apply(5, 4, 5));
}

void test_a_command_submitted_during_the_read_refuses(void) {
  TEST_ASSERT_FALSE(comfoair_readback_may_apply(5, 5, 6));
}

void test_wraparound_is_equality_only(void) {
  TEST_ASSERT_TRUE(comfoair_readback_may_apply(UINT32_MAX, UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_FALSE(comfoair_readback_may_apply(UINT32_MAX, UINT32_MAX, 0));
  TEST_ASSERT_FALSE(comfoair_readback_may_apply(0, UINT32_MAX, 0));
}

// --- the generated table against the write builders --------------------------------------------

// The frame the owner's command sends for each key: 03 <unit> <subunit> <property> <value>.
static rmi::Frame write_frame_for(const char *key) {
  if (strcmp(key, "temperature_passive") == 0)
    return rmi::temperature_passive(0);
  if (strcmp(key, "humidity_comfort") == 0)
    return rmi::humidity_comfort(0);
  if (strcmp(key, "humidity_protection") == 0)
    return rmi::humidity_protection(0);
  return rmi::Frame{};
}

static uint8_t property_for(const char *key) {
  if (strcmp(key, "temperature_passive") == 0)
    return rmi::PROP_TEMPERATURE_PASSIVE;
  if (strcmp(key, "humidity_comfort") == 0)
    return rmi::PROP_HUMIDITY_COMFORT;
  if (strcmp(key, "humidity_protection") == 0)
    return rmi::PROP_HUMIDITY_PROTECTION;
  return 0;
}

void test_every_row_reads_what_its_command_writes(void) {
  TEST_ASSERT_EQUAL(3, COMFOAIR_RMI_READBACK_COUNT);
  for (size_t i = 0; i < COMFOAIR_RMI_READBACK_COUNT; i++) {
    const comfoair_rmi_readback_t *r = row(i);
    TEST_ASSERT_LESS_THAN(COMFOAIR_ENTITY_COUNT, r->entity);
    const char *key = COMFOAIR_ENTITIES[r->entity].key;
    const rmi::Frame w = write_frame_for(key);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(5, w.size(), key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(rmi::CMD_SET_PROPERTY, w[0], key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(rmi::UNIT_TEMPHUMCONTROL, r->unit, key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(w[1], r->unit, key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(w[2], r->subunit, key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(w[3], r->property, key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(property_for(key), r->property, key);
  }
}

void test_every_row_is_a_three_option_select_with_no_default(void) {
  for (size_t i = 0; i < COMFOAIR_RMI_READBACK_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[row(i)->entity];
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_KIND_SELECT, e->kind, e->key);
    TEST_ASSERT_EQUAL_INT16_MESSAGE(-1, e->pdo, e->key);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(3, e->option_count, e->key);
    TEST_ASSERT_TRUE_MESSAGE(e->writable, e->key);
    // No invented factory default: the value is unknown until the unit answers.
    TEST_ASSERT_TRUE_MESSAGE(isnan(e->default_value), e->key);
  }
}

void setUp(void) {}
void tearDown(void) {}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_each_option_index_decodes);
  RUN_TEST(test_a_value_past_the_options_is_refused);
  RUN_TEST(test_a_wrong_length_is_refused);
  RUN_TEST(test_null_arguments_are_refused);
  RUN_TEST(test_an_entry_outside_the_registry_is_refused);
  RUN_TEST(test_an_idle_sender_with_nothing_new_may_apply);
  RUN_TEST(test_a_command_in_flight_refuses);
  RUN_TEST(test_a_command_submitted_during_the_read_refuses);
  RUN_TEST(test_wraparound_is_equality_only);
  RUN_TEST(test_every_row_reads_what_its_command_writes);
  RUN_TEST(test_every_row_is_a_three_option_select_with_no_default);
  return UNITY_END();
}
