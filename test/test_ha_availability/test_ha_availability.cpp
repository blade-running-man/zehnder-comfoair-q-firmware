// WHICH TOPICS AN ENTITY'S AVAILABILITY IS READ FROM, and it is a suite of its own for the reason
// every split in this tree is: test_ha.cpp is 589 lines and test_ha_cleanup.cpp 562, against the
// 600-line ceiling for a Unity suite (AGENTS.md), and PlatformIO links one binary per DIRECTORY --
// so a suite splits by directory or not at all.
//
// The subject, in the owner's words (docs/interfaces/home-assistant-discovery.md, Availability). He
// cuts the breaker to change the filters. Wi-Fi, the broker and the ESP32 keep working, so Home
// Assistant shows the device online, the supply temperature at 21.5 C and fan speed 2 -- the values
// from the moment the power went. An hour later the graph is still a flat line labelled "current
// reading". A second availability topic, `<prefix>/bus`, is what makes the card go grey instead;
// comfoair_bus_health.h is the verdict behind it.
//
// THREE THINGS ARE BEING DEFENDED HERE, AND THE FIRST TWO PULL IN OPPOSITE DIRECTIONS:
//
//  * Every other entity MUST list `<prefix>/bus`, or a dead bus goes on publishing yesterday's
//    numbers as current ones. That includes the three selects temperature_passive,
//    humidity_comfort and humidity_protection: they have no cold-start default, the probe task
//    reads their real value from the unit over RMI at boot and every six hours, and until it
//    answers they read unknown -- a value that comes from the bus, so a dead bus greys them out.
//  * Six entities MUST NOT. Four are the durations (boost, bypass, balance, away), which live on
//    THIS node alone, are restored from NVS by src/main.cpp and are still true with the loom
//    unplugged. Greying them out would be lying a second time.
//
//    THE FIFTH IS `schema_version`, BY THE OWNER'S DECISION. It is this firmware's own constant,
//    not a reading of the installation. It is what a client checks the entity contract with, and
//    the contract still holds while the CAN bus is silent. Greying it out would be a lie told to
//    the reader who came to find out why the others are grey.
//
//    THE SIXTH IS `data_freshness`. Its whole job is to go on answering while everything
//    downstream of it has stopped, so gating it on the bus would be a category error and a cruel
//    one.
//
//  * AND THE THIRD MECHANISM IS A FLAG NO DOCUMENT CARRIES. That same entity's STATE topic is
//    published NOT RETAINED, alone among the entities, because `expire_after` and a retained
//    state topic cancel each other out -- Home Assistant restarts the expiry on every delivery and
//    never looks at `msg.retain`, so a retained `ON` re-anchors a dead device's fifteen minutes on
//    every resubscribe. The case is at the foot of this file and the argument is on
//    `state_retained()` in comfoair_mqtt_publish.c.
//
// AND ONE SHAPE, WHICH IS THE ONE THAT FAILS SILENTLY: `avty_t` and `avty` are vol.Exclusive in
// the SAME group "availability" (mqtt/schemas.py:81 and :97 of the 2026.9 install vendored in
// ../zehnder-comfoair-q-ha, joined by .extend(), which preserves the markers). A payload carrying
// both raises vol.MultipleInvalid, and an entity whose config was rejected does not break --
// it never appears at all, with nothing said to the owner. So the move had to be a REPLACEMENT
// and test_avty_replaces_avty_t_and_never_joins_it is what says it was.
//
// WHAT THIS SUITE CANNOT SEE: whether Home Assistant accepts these documents. It checks the bytes
// against the rules read out of HA's own schema, in a process that is not Home Assistant -- and for
// the retained flag it checks the SOURCE rather than the bytes, for the reason that case gives.
// The hardware scenarios in docs/hardware-verification/06b-home-assistant.md carry the other half.
#include <unity.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>

#include "comfoair_ha.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_buf[2048];

comfoair_ha_ctx_t ctx(const char *prefix = "comfoair/aabbccddeeff") {
  comfoair_ha_ctx_t c = {};
  c.device_id    = "aabbccddeeff";
  c.device_name  = "ComfoAir Q eeff";
  c.topic_prefix = prefix;
  c.sw_version   = "0.1.0";
  c.mac          = "aa:bb:cc:dd:ee:ff";
  c.url          = "http://192.168.1.42/";
  return c;
}

std::string meta(const comfoair_ha_ctx_t &c, comfoair_ha_meta_t which) {
  char topic[256];
  TEST_ASSERT_TRUE_MESSAGE(comfoair_ha_meta_topic(&c, which, topic, sizeof topic) > 0,
                           "a meta topic this suite needs has no name");
  return std::string(topic);
}

std::string payload(const comfoair_ha_ctx_t &c, const comfoair_entity_t *e) {
  const size_t n = comfoair_ha_discovery_payload(&c, e, g_buf, sizeof g_buf);
  TEST_ASSERT_TRUE_MESSAGE(n > 0, e->key);
  TEST_ASSERT_EQUAL_size_t(n, strlen(g_buf));
  return std::string(g_buf);
}

bool has(const std::string &document, const std::string &fragment) {
  return document.find(fragment) != std::string::npos;
}

size_t count(const std::string &document, const std::string &fragment) {
  size_t n = 0;
  for (size_t at = document.find(fragment); at != std::string::npos;
       at = document.find(fragment, at + fragment.size()))
    n++;
  return n;
}

// The availability clause as it must appear, built from the BUILDERS and not typed out: a rename
// of the `status` or `bus` segment has to move this expectation with it, or the suite would go on
// asserting a topic the firmware no longer publishes. The literal spelling of the two topics is
// pinned once, in test_ha_cleanup.cpp, where the enumeration lives.
std::string one_topic(const comfoair_ha_ctx_t &c) {
  return "\"avty\":[{\"t\":\"" + meta(c, COMFOAIR_HA_META_STATUS) + "\"}],\"avty_mode\":\"all\"";
}

std::string both_topics(const comfoair_ha_ctx_t &c) {
  return "\"avty\":[{\"t\":\"" + meta(c, COMFOAIR_HA_META_STATUS) + "\"},{\"t\":\"" +
         meta(c, COMFOAIR_HA_META_BUS) + "\"}],\"avty_mode\":\"all\"";
}

// THE SIX, BY NAME, AND THAT IS DELIBERATE RATHER THAN LAZY. The implementation decides
// node-locality from the registry (comfoair_ha_payload.c), and a test that asked the registry the
// same question would agree with the implementation by construction -- it would pass on the rule
// and on any mutation of it that read the same field, which is the exact failure this project has
// now found twenty-one times: a guard that checks something other than what it promises.
//
// So the expectation is stated independently, from the rule (the four durations, which the unit is
// only ever told as part of a command; the firmware's own `schema_version` constant; the freshness
// entity) and from the source they come from (tools/pdo_table.py, the four NUMBERS). It is SIX
// NAMES, not a second entity list: the 70 on the other side are never enumerated here, they are
// COMFOAIR_ENTITY_COUNT minus these. The one exception is BUS_SELECTS below, which pins the three
// selects that moved across.
//
// DO NOT replace this with a loop over the registry. If a key ever leaves this list the change is
// visible in a diff, which is the whole point.
const char *const NODE_LOCAL[] = {
    // "No PDO carries these: a duration is only ever told to the unit as part of a command, so the
    // value lives on this node alone" (generate_registry.py:143-147).
    "boost_duration",
    "bypass_duration",
    "balance_duration",
    "away_duration",
    // Not node-local by the same route -- it has no cold-start default, it is a constant this
    // firmware carries (COMFOAIR_KIND_DIAGNOSTIC_CONST, comfoair_state.cpp seed_defaults()) -- which is exactly
    // why the predicate needs a second clause and this list a comment.
    "schema_version",
    // The freshness entity says that THIS NODE published within the last five minutes. Gating it on
    // the CAN bus would be a category error: a dead bus is precisely the hour in which the owner
    // needs to know the node itself is alive. It has no cold-start default and is not the
    // diagnostic constant, so it needs a THIRD clause in the predicate, not a widening of either.
    "data_freshness",
};
const size_t NODE_LOCAL_COUNT = sizeof NODE_LOCAL / sizeof NODE_LOCAL[0];

// The RMI-read selects: no default, value from the unit, unknown until the probe answers. Pinned
// by name so that giving one a default again (and so exempting it) fails here.
const char *const BUS_SELECTS[] = {"temperature_passive", "humidity_comfort", "humidity_protection"};

// THE PUBLISHER'S SOURCE, READ AS TEXT, and the compromise has to be named rather than hidden. The
// retained flag is an argument to esp_mqtt_client_publish: it appears in no discovery document, in
// no payload and in no header, so nothing this suite can link against can be asked for it. And
// comfoair_mqtt cannot be linked here at all -- [env:native] builds no ESP-IDF, the same reason the
// discovery gate's three rules were moved out into comfoair_mqtt_backoff. What is left is to read
// the file the device compiles.
//
// COMMENTS OUT, WHITESPACE COLLAPSED, so the guard reads CODE and survives a line wrap. DO NOT
// write the token sequences the case below counts into prose anywhere in that file: a `//` line is
// cut here, but a string typed into a header comment would not be.
std::string source_of(const char *file) {
  const std::string me(__FILE__), here = "test/test_ha_availability/";
  const size_t      at = me.rfind(here);
  TEST_ASSERT_TRUE_MESSAGE(at != std::string::npos, me.c_str());
  const std::string path = me.substr(0, at) + "components/comfoair_mqtt/" + file;
  FILE             *f    = fopen(path.c_str(), "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, path.c_str());
  std::string raw;
  char        chunk[4096];
  for (size_t n = fread(chunk, 1, sizeof chunk, f); n > 0; n = fread(chunk, 1, sizeof chunk, f))
    raw.append(chunk, n);
  fclose(f);
  TEST_ASSERT_TRUE_MESSAGE(raw.size() > 1024, path.c_str());

  std::string code;
  for (size_t line = 0; line <= raw.size();) {
    size_t eol = raw.find('\n', line);
    if (eol == std::string::npos)
      eol = raw.size();
    std::string text = raw.substr(line, eol - line);
    const size_t comment = text.find("//");
    if (comment != std::string::npos)
      text.erase(comment);
    code += text;
    code += ' ';
    line = eol + 1;
  }
  std::string flat;
  bool        gap = true;
  for (size_t i = 0; i < code.size(); i++) {
    const char c = code[i];
    if (c == ' ' || c == '\t' || c == '\r') {
      if (!gap)
        flat += ' ';
      gap = true;
      continue;
    }
    flat += c;
    gap = false;
  }
  return flat;
}

// THE PUBLISHER IS TWO FILES since <prefix>/bus moved to comfoair_mqtt_bus.c for the line ceiling.
// The counts below run over both, so the move changed none of them.
std::string publisher_source(void) {
  return source_of("comfoair_mqtt_publish.c") + " " + source_of("comfoair_mqtt_bus.c");
}

const comfoair_entity_t *entity(const char *key) {
  const comfoair_entity_t *e = comfoair_entity_by_key(key);
  TEST_ASSERT_NOT_NULL_MESSAGE(e, key);
  return e;
}

}  // namespace

// --- the two sides ------------------------------------------------------------------------------

void test_a_bus_entity_lists_both_availability_topics_in_all_mode(void) {
  // A supply air temperature is a reading of the UNIT. With the loom unplugged the last value is
  // an hour old and Home Assistant has no way to know it, because an absent reading publishes
  // NOTHING (comfoair_ha_payload.c) -- right for an entity that never answered, useless for one
  // that answered yesterday.
  //
  // ALL, NOT any AND NOT latest, and the mode is load-bearing rather than decorative.
  // entity.py:725-726: `all` requires both topics to say online. `any` fails FATALLY -- after a
  // hard power cut the broker still holds a retained `bus = online`, because the will is one per
  // connection (cfg.session.last_will) and `status` has it, so under `any` every entity of a dead
  // device stays available. `latest` fails more quietly: its answer is whichever message arrived
  // last (entity.py:729), and after a reconnect the delivery order of retained messages is the
  // broker's to choose.
  const comfoair_ha_ctx_t c = ctx();
  const std::string       document = payload(c, entity("supply_air_temp"));
  TEST_ASSERT_TRUE_MESSAGE(has(document, both_topics(c)), document.c_str());

  // And it moves with the prefix, or the second topic would be a fixed string that happens to
  // match the first one's device today.
  const comfoair_ha_ctx_t other = ctx("attic/hrv");
  TEST_ASSERT_TRUE(has(payload(other, entity("supply_air_temp")), both_topics(other)));
}

void test_a_node_local_entity_lists_only_the_device_topic(void) {
  // The durations are put into the model before the bus has been asked anything at all
  // (comfoair_state.cpp seed_defaults() seeds them from the registry's defaults; src/main.cpp restores
  // what the owner last set over the top), and schema_version and data_freshness are this node's
  // own. A dead bus says nothing about them, so greying them out would be the second lie of the
  // day.
  const comfoair_ha_ctx_t c   = ctx();
  const std::string       bus = meta(c, COMFOAIR_HA_META_BUS);
  for (size_t i = 0; i < NODE_LOCAL_COUNT; i++) {
    const std::string document = payload(c, entity(NODE_LOCAL[i]));
    TEST_ASSERT_TRUE_MESSAGE(has(document, one_topic(c)), NODE_LOCAL[i]);
    // Not merely "the two-topic clause is absent": the bus topic must not appear ANYWHERE in the
    // document, which also rules out its arriving as a third availability entry or inside some
    // other key.
    TEST_ASSERT_FALSE_MESSAGE(has(document, bus), NODE_LOCAL[i]);
  }
}

void test_the_rmi_read_selects_are_bus_backed(void) {
  // No default, so unknown until the probe's RMI read answers: a dead bus must grey them out.
  const comfoair_ha_ctx_t c = ctx();
  for (const char *key : BUS_SELECTS)
    TEST_ASSERT_TRUE_MESSAGE(has(payload(c, entity(key)), both_topics(c)), key);
}

void test_every_entity_lists_at_least_the_device_topic(void) {
  // THE TOTAL, and every number in it is computed. COMFOAIR_ENTITY_COUNT is whatever the table
  // makes it and this line does not care; NODE_LOCAL_COUNT is the length of the list above and not
  // a 6 typed here. What is actually pinned is a partition: each published entity carries the
  // device topic, and carries the bus topic or does not, and the two sides add up to the registry,
  // and this line does not know any of those numbers.
  //
  // AN ENTITY THAT LISTED NEITHER would be permanently available -- HA's `available` returns True
  // when _avail_topics is empty (entity.py:722-723) -- which is the pre-2026-09-07 behaviour this
  // whole section exists to end. An entity that listed only the bus topic would go unavailable
  // whenever the broker link dropped without the will being delivered.
  const comfoair_ha_ctx_t c      = ctx();
  const std::string       status = meta(c, COMFOAIR_HA_META_STATUS);
  const std::string       bus    = meta(c, COMFOAIR_HA_META_BUS);

  std::set<std::string> only_status;
  size_t                with_bus = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    // Every entity in this registry maps to a component today; the guard is here so that the day
    // one does not, this counts what is published rather than failing in payload().
    if (comfoair_ha_component(e) == nullptr)
      continue;
    const std::string document = payload(c, e);
    TEST_ASSERT_TRUE_MESSAGE(has(document, status), e->key);
    if (has(document, bus)) {
      TEST_ASSERT_TRUE_MESSAGE(has(document, both_topics(c)), e->key);
      with_bus++;
      continue;
    }
    TEST_ASSERT_TRUE_MESSAGE(has(document, one_topic(c)), e->key);
    only_status.insert(e->key);
  }

  TEST_ASSERT_EQUAL_size_t(COMFOAIR_ENTITY_COUNT, with_bus + only_status.size());
  TEST_ASSERT_EQUAL_size_t(NODE_LOCAL_COUNT, only_status.size());
  TEST_ASSERT_EQUAL_size_t(COMFOAIR_ENTITY_COUNT - NODE_LOCAL_COUNT, with_bus);
  // BY NAME AND NOT ONLY BY COUNT. Count-for-count is satisfied by any swap -- greying out a
  // duration while publishing a temperature as always-available would keep both totals right and
  // be exactly the defect. THIS is the assertion a mutation that moves one entity across the line
  // has to get past.
  //
  // AND THE NUMBER IN THE MESSAGE IS COMPUTED, not spelled. A spelled number in a
  // diagnostic drifts from the list it describes and sends its reader hunting for an entity that
  // was never missing. A number in a diagnostic is a copy like any other.
  const std::set<std::string> expected(NODE_LOCAL, NODE_LOCAL + NODE_LOCAL_COUNT);
  const std::string           complaint = "the entities that ignore the bus are not the " +
                                std::to_string(NODE_LOCAL_COUNT) + " named ones";
  TEST_ASSERT_TRUE_MESSAGE(expected == only_status, complaint.c_str());
}

// --- the shape that fails in silence ------------------------------------------------------------

void test_avty_replaces_avty_t_and_never_joins_it(void) {
  // vol.Exclusive(CONF_AVAILABILITY_TOPIC, "availability") at mqtt/schemas.py:81 and
  // vol.Exclusive(CONF_AVAILABILITY, "availability") at :97 -- ONE GROUP, two members, and
  // _MQTT_AVAILABILITY_SCHEMA is _MQTT_AVAILABILITY_SINGLE_SCHEMA.extend(...LIST_SCHEMA.schema),
  // which carries the markers across. A document with both raises vol.MultipleInvalid and the
  // entity SILENTLY NEVER APPEARS: there is no error on the owner's dashboard, no half-working
  // card, just a device whose entities are not there.
  //
  // Checked here rather than remembered, and checked over EVERY entity rather than one: the
  // shortest road back to `avty_t` is a branch that keeps it for the one-topic ones on the
  // grounds that they have only one topic anyway. That branch validates as long as nobody looks,
  // and it is the reason they are given a one-element list instead.
  const comfoair_ha_ctx_t c = ctx();
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_ha_component(e) == nullptr)
      continue;
    const std::string document = payload(c, e);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, count(document, "\"avty_t\""), e->key);
    // EXACTLY ONE of each, not "at least one": a second clause appended after the first is what a
    // careless "add the bus topic" looks like, and JSON with a duplicate key is parsed by Python's
    // json without complaint -- the last one wins, and which one that is depends on the order the
    // payload was written in.
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, count(document, "\"avty\":"), e->key);
    TEST_ASSERT_EQUAL_size_t_MESSAGE(1, count(document, "\"avty_mode\":"), e->key);
  }
}

// --- the flag that no document carries ----------------------------------------------------------

void test_the_freshness_state_is_the_one_publication_that_is_not_retained(void) {
  // THE SUBJECT, in the owner's words again. The house loses power entirely, so the will reaches
  // nobody and the retained `online` outlives the outage; the freshness entity was raised so that
  // Home Assistant would stop showing yesterday's numbers as current ones fifteen minutes later.
  // Published RETAINED it would do the opposite: HA restarts the expiry on every message the state
  // topic delivers and never asks whether it was retained (binary_sensor.py:171-183 resets
  // `_expiration_trigger`; `msg.retain` is read nowhere in binary_sensor.py, entity.py or models.py
  // of the 2026.9 install vendored in ../zehnder-comfoair-q-ha). Every resubscribe -- an HA
  // restart, a reload of the MQTT integration, a broker link that blinked -- would then hand a DEAD
  // device another quarter of an hour of "Connected", and a link that blinks oftener than that
  // would hand it for ever.
  const std::string code = publisher_source();

  // ONE PREDICATE, AND IT NAMES THE KIND. The mutation "give the freshness entity its retain back"
  // -- by returning true, or by deleting the predicate and passing a literal -- takes this line.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
      1, count(code, "return entity->kind != COMFOAIR_KIND_FRESHNESS;"),
      "comfoair_mqtt_publish.c no longer decides retention from the freshness kind");

  // AND THE STATE PUBLISH ASKS IT, rather than the predicate sitting unread beside a literal.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
      1, count(code, "QOS_STATE, state_retained(entity))"),
      "the entity state publish no longer takes its retained flag from state_retained()");

  // AND EXACTLY ONE STATE PUBLISH IS STILL UNCONDITIONALLY RETAINED -- the fan's three leaves. The
  // mutation "drop the retain everywhere" fails on these two: the retained payloads are what a
  // reconnecting Home Assistant is handed instead of an empty device, and a firmware that stopped
  // writing them would show a blank card after every restart until each value next moved.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, count(code, "QOS_STATE, true)"),
                                   "the fan's retained leaves stopped being published retained");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, count(code, "QOS_STATE, false"),
                                   "a state topic other than the freshness one lost its retain");

  // AND BOTH AVAILABILITY TOPICS KEEP THEIRS, which is the distinction the change is likeliest to
  // be "fixed" across. `<prefix>/bus` and the birth `<prefix>/status` are retained ON PURPOSE: a
  // subscriber that arrives after the verdict moved has to be told it, and neither carries an
  // expire_after for a redelivery to re-anchor. TWO, because both are published from the two
  // publisher files -- the will is the third and lives in comfoair_mqtt_client.c, which is not
  // read here.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(2, count(code, "QOS_META, true)"),
                                   "an availability topic in this file stopped being retained");
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, count(code, "QOS_META, false"),
                                   "an availability topic in this file is published unretained");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_bus_entity_lists_both_availability_topics_in_all_mode);
  RUN_TEST(test_a_node_local_entity_lists_only_the_device_topic);
  RUN_TEST(test_the_rmi_read_selects_are_bus_backed);
  RUN_TEST(test_every_entity_lists_at_least_the_device_topic);
  RUN_TEST(test_avty_replaces_avty_t_and_never_joins_it);
  RUN_TEST(test_the_freshness_state_is_the_one_publication_that_is_not_retained);
  return UNITY_END();
}
