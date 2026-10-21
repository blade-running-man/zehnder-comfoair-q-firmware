// THE FRESHNESS ENTITY: the one thing this firmware publishes on a clock, and the only mechanism
// Home Assistant has for turning silence into `unavailable` WITHOUT the device taking part.
//
// The failure it answers, in the owner's words (docs/interfaces/home-assistant-discovery.md,
// Availability). The house loses power entirely. The broker dies with the device, so the will is
// delivered to nobody, and the retained `online` written before the cut survives the outage on
// whatever comes back up first. Home Assistant then shows yesterday's numbers as current ones, FOR
// EVER: availability topics in HA have no TTL, and nothing on the device is alive to say otherwise.
// `expire_after` is the whole of the answer, and it exists on exactly two platforms --
// `vol.Optional(CONF_EXPIRE_AFTER)` appears at binary_sensor.py:54 and sensor.py:80 of the 2026.9
// install vendored in ../zehnder-comfoair-q-ha, and nowhere else in components/mqtt/.
//
// WHAT THIS ENTITY DOES NOT DO is asserted here too, because the tempting overstatement is what
// the owner would be misled by. It does not grey out the 67 bus-backed entities and it is NOT a
// second level of availability -- `expire_after` reaches neither the 13 writable entities nor the
// fan, so a device that burned down still shows an available fan at yesterday's level under ANY
// choice made here. It is called "data freshness" and nothing else for that reason.
//
// A SUITE OF ITS OWN, for the reason every split in this tree is: test_ha.cpp stands at 589 lines
// and test_ha_cleanup.cpp at 562 against the 600-line ceiling for a Unity suite (AGENTS.md), and
// PlatformIO links one binary per DIRECTORY -- so a suite splits by directory or not at all.
//
// WHAT THIS SUITE CANNOT SEE: that Home Assistant actually expires the entity fifteen minutes
// after the last message, and that a broker holds the retained `online` across a power cut. Both
// are other people's processes; hardware scenarios HV-HA-06 and HV-HA-07
// (docs/hardware-verification/06b-home-assistant.md) carry them.
#include <unity.h>

#include <cstdlib>
#include <cstring>
#include <string>

#include "comfoair_ha.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_buf[2048];

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

const comfoair_entity_t *freshness(void) {
  const comfoair_entity_t *e = comfoair_entity_by_key(COMFOAIR_FRESHNESS_KEY);
  TEST_ASSERT_NOT_NULL_MESSAGE(e, "the generator no longer declares the freshness entity");
  return e;
}

std::string document(const comfoair_entity_t *e) {
  const comfoair_ha_ctx_t c = ctx();
  const size_t            n = comfoair_ha_discovery_payload(&c, e, g_buf, sizeof g_buf);
  TEST_ASSERT_TRUE_MESSAGE(n > 0, e->key);
  TEST_ASSERT_EQUAL_size_t(n, strlen(g_buf));
  return std::string(g_buf);
}

bool has(const std::string &doc, const std::string &fragment) {
  return doc.find(fragment) != std::string::npos;
}

}  // namespace

// --- what it is ---------------------------------------------------------------------------------

void test_the_freshness_entity_is_a_binary_sensor_with_expire_after(void) {
  // binary_sensor AND NOT sensor, and the choice is forced twice over. `expire_after` exists on
  // those two platforms only, so nine of this registry's ten kinds cannot carry it at all; and a
  // `sensor` would need a value to show, while the fact being carried has no value -- it is the
  // ARRIVAL that means something, which is exactly what a connectivity binary_sensor reads.
  const comfoair_entity_t *e = freshness();
  TEST_ASSERT_EQUAL_STRING("binary_sensor", comfoair_ha_component(e));

  const std::string doc = document(e);
  // dev_cla comes from the registry row, so this also pins that the row carries it: HA renders a
  // connectivity binary_sensor as Connected/Disconnected, and Disconnected is never published --
  // the entity goes UNAVAILABLE instead, which is the only state that says "nobody is talking".
  TEST_ASSERT_TRUE_MESSAGE(has(doc, "\"dev_cla\":\"connectivity\""), doc.c_str());
  TEST_ASSERT_TRUE_MESSAGE(has(doc, "\"exp_aft\":900"), doc.c_str());
  // Diagnostic: it says something about this NODE, not about the ventilation unit, and HA's
  // diagnostic category is where a card puts exactly that.
  TEST_ASSERT_TRUE_MESSAGE(has(doc, "\"ent_cat\":\"diagnostic\""), doc.c_str());
  // It is READ-ONLY. A command topic would be a control over a clock, and HA writing to it would
  // reset nothing -- the expiry is fed by publications, not by values.
  TEST_ASSERT_FALSE_MESSAGE(has(doc, "\"cmd_t\""), doc.c_str());
  TEST_ASSERT_FALSE(e->writable);
}

void test_no_other_entity_carries_expire_after(void) {
  // THE REJECTED ALTERNATIVE, made into a guard: `expire_after` on the DATA entities themselves.
  // Its timer is reset by every message, so on a firmware that publishes by delta a slow-moving
  // reading -- a filter countdown, an energy total -- would have to be republished on a clock for
  // ever just to stay visible. That is the whole publication model given up to buy one property,
  // and one entity buys the same property for 288 messages a day. A guard on the freshness entity
  // alone would not see it happen.
  size_t carriers = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_ha_component(e) == nullptr)
      continue;
    if (has(document(e), "\"exp_aft\""))
      carriers++;
  }
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, carriers, "expire_after reached an entity that carries data");
}

// --- the two numbers ------------------------------------------------------------------------

void test_the_period_and_the_expiry_live_in_one_define(void) {
  // THE REAL GUARD ON THIS IS THE BUILD, and this case is its receipt rather than its enforcement.
  // registry_generated.h emits the two numbers adjacent and follows them with a static_assert that
  // the expiry is three periods; this file includes that header through comfoair_ha.h, so a
  // mutation that moves one number without the other does not COMPILE -- here, and in every other
  // translation unit in the tree. That is the difference asked for: a build failure, not a red
  // case somebody could have skipped.
  //
  // Three, and not two or four: the expiry has to survive two publications lost to a slow broker
  // without greying the owner's card, and it has to be shorter than the time it takes him to
  // believe a stale number. 300 s and 900 s are the owner's choice.
  TEST_ASSERT_EQUAL_INT(300, COMFOAIR_FRESHNESS_PERIOD_S);
  TEST_ASSERT_EQUAL_INT(900, COMFOAIR_FRESHNESS_EXPIRE_S);
  TEST_ASSERT_EQUAL_INT(3 * COMFOAIR_FRESHNESS_PERIOD_S, COMFOAIR_FRESHNESS_EXPIRE_S);

  // And the document carries THE MACRO, not a number typed beside it: the payload writer reading
  // its own 900 would be the third copy, and the one that keeps working after the other two move.
  const std::string doc      = document(freshness());
  const std::string expected = "\"exp_aft\":" + std::to_string(COMFOAIR_FRESHNESS_EXPIRE_S);
  TEST_ASSERT_TRUE_MESSAGE(has(doc, expected), doc.c_str());

  // 288 messages a day, and that number is the whole reason 300 was chosen over 60 (which is
  // 1440) or 180 (which is 480, and was derived from a publication period rather than from any
  // need). Stated here because it is the cost the owner accepted, and a period edited downward
  // spends his broker's day without anything else noticing.
  TEST_ASSERT_EQUAL_INT(288, 86400 / COMFOAIR_FRESHNESS_PERIOD_S);
}

// --- the registry ---------------------------------------------------------------------------

void test_the_registry_entity_count_is_written_out(void) {
  // 76, and the number is written out here on purpose while every other count in this
  // tree is computed. This one is not an arithmetic consequence of the table: it is 75 rows
  // projected from tools/pdo_table.py plus ONE the generator declares itself, and the thing worth
  // pinning is that the second term is exactly one. A computed assertion would agree with any
  // number of synthetic rows, including none.
  //
  // IT IS NOT THE ONLY GUARD OF THE NUMBER and must not become one: tests/python asserts that the
  // extra row is declared by the generator and NOT by hand in a payload writer, and test_ha counts
  // what actually reaches Home Assistant. This case would pass on a hand-written 77th entity; that
  // one would not.
  TEST_ASSERT_EQUAL_size_t(76, COMFOAIR_ENTITY_COUNT);

  // The key is one MQTT segment and one discovery object_id: HA's TOPIC_MATCHER accepts only
  // [a-zA-Z0-9_-] there (mqtt/discovery.py:64-67), so a key with a slash or a space in it would
  // produce a config topic HA never reads.
  const char *key = COMFOAIR_FRESHNESS_KEY;
  for (const char *p = key; *p != '\0'; p++)
    TEST_ASSERT_TRUE_MESSAGE((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                                 (*p >= '0' && *p <= '9') || *p == '_' || *p == '-',
                             key);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_freshness_entity_is_a_binary_sensor_with_expire_after);
  RUN_TEST(test_no_other_entity_carries_expire_after);
  RUN_TEST(test_the_period_and_the_expiry_live_in_one_define);
  RUN_TEST(test_the_registry_entity_count_is_written_out);
  return UNITY_END();
}
