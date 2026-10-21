// What this device empties when it stops being the thing it was, and in which order.
//
// The whole suite exists to defend one construction and one ordering:
//
//  * A TOP-LEVEL TOPIC DOES NOT EXIST UNTIL IT IS A VALUE OF comfoair_ha_meta_t. Its name can be
//    got from nowhere but comfoair_ha_meta_topic(), and the cleanup walks
//    `for (which = 0; which < COMFOAIR_HA_META_COUNT; which++)` rather than a second list. So a
//    new retained top-level topic cannot be published without the cleanup seeing it -- the enum is
//    the only door, and walking it is free. Every count below is COMPUTED from
//    COMFOAIR_HA_META_COUNT and COMFOAIR_ENTITY_COUNT; a number typed in here would be the second
//    entity list AGENTS.md forbids, one indirection along.
//  * REKEY erases the state side and NEVER a discovery topic; WITHDRAW erases every discovery
//    topic BEFORE any state topic (docs/interfaces/home-assistant-discovery.md, Cleanup and
//    retraction). The reverse order leaves Home Assistant holding config documents that point
//    at topics which no longer carry anything.
//
// WHAT THIS SUITE CANNOT SEE is stated in the report and repeated here because a reader of the
// file deserves it: it checks the bytes this firmware would send, and nothing about a broker
// forgetting a retained message, about Home Assistant deleting an entity, or about the outbox
// draining before the client is destroyed. Those are two other processes and a wall clock.
#include <unity.h>

#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "comfoair_ha.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_buf[256];

comfoair_ha_ctx_t ctx(const char *prefix = "comfoair/aabbccddeeff",
                      const char *name   = "ComfoAir Q eeff") {
  comfoair_ha_ctx_t c = {};
  c.device_id    = "aabbccddeeff";
  c.device_name  = name;
  c.topic_prefix = prefix;
  c.sw_version   = "0.1.0";
  c.mac          = "aa:bb:cc:dd:ee:ff";
  c.url          = "http://192.168.1.42/";
  return c;
}

// The sequence as the publisher would walk it: index 0 upwards, count taken from the component.
// Every entry is asserted non-empty here, so a builder that silently produced nothing shows up as
// a failure in whichever test asked for the sequence rather than as a shorter list nobody counted.
std::vector<std::string> sequence(const comfoair_ha_ctx_t &c, comfoair_ha_cleanup_t what) {
  std::vector<std::string> out;
  const size_t             n = comfoair_ha_cleanup_count(what);
  for (size_t i = 0; i < n; i++) {
    const size_t len = comfoair_ha_cleanup_topic(&c, what, i, g_buf, sizeof g_buf);
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "the cleanup produced an empty topic");
    TEST_ASSERT_EQUAL_size_t(len, strlen(g_buf));
    out.push_back(std::string(g_buf));
  }
  return out;
}

bool is_discovery(const std::string &topic) {
  return topic.compare(0, sizeof(COMFOAIR_HA_DISCOVERY_PREFIX) - 1, COMFOAIR_HA_DISCOVERY_PREFIX) ==
         0;
}

size_t occurrences(const std::vector<std::string> &seq, const std::string &needle) {
  size_t n = 0;
  for (const std::string &s : seq)
    if (s == needle)
      n++;
  return n;
}

std::string status_topic(const comfoair_ha_ctx_t &c) {
  TEST_ASSERT_TRUE(comfoair_ha_meta_topic(&c, COMFOAIR_HA_META_STATUS, g_buf, sizeof g_buf) > 0);
  return std::string(g_buf);
}

}  // namespace

// --- the enumeration is the door -----------------------------------------------------------

void test_the_status_topic_has_exactly_one_producer(void) {
  // THIS TEST REPLACES A GUARD THAT DID NOT GUARD, and the replacement is a deletion rather than
  // a better assertion. Until 2026-09-07 there was a comfoair_ha_status_topic() beside the
  // enumeration, documented as "a THIN WRAPPER ... DO NOT give it a format of its own again",
  // and a test that compared its output with comfoair_ha_meta_topic()'s. A reviewer put the
  // second format back and all 43 cases stayed green: two functions that format the same string
  // the same way agree, and the test only starts failing once the duplicate has already
  // DIVERGED -- which is one edit after the damage, not before it.
  //
  // So the wrapper is gone. Its three callers -- the will (comfoair_mqtt_client.c), the explicit
  // `offline` before a clean stop, and the birth message (comfoair_mqtt_publish.c) -- name
  // COMFOAIR_HA_META_STATUS directly, and there is no longer a place in the tree for a second
  // format of `<prefix>/status` to live. What is left to pin is the one producer's output, and
  // that it is the topic the cleanup walks.
  const comfoair_ha_ctx_t c = ctx();
  char                    meta[256];
  const size_t            n = comfoair_ha_meta_topic(&c, COMFOAIR_HA_META_STATUS, meta, sizeof meta);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/status", meta);
  TEST_ASSERT_EQUAL_size_t(n, strlen(meta));

  // Being reachable through the enum is not the same claim as being walked by the cleanup, and
  // both are needed.
  TEST_ASSERT_EQUAL_size_t(1, occurrences(sequence(c, COMFOAIR_HA_CLEANUP_REKEY), meta));
}

void test_the_bus_topic_is_named_here_and_erased_like_every_other_meta_topic(void) {
  // <prefix>/bus is the second value of comfoair_ha_meta_t (the bus availability topic): the
  // ventilation unit answering on CAN, which is a different fact from the link to the broker that
  // <prefix>/status carries. It arrived on 2026-09-07 and THE CLEANUP PICKED IT UP WITH NO EDIT TO
  // THIS FILE -- the count above is COMFOAIR_HA_META_COUNT plus the entities, and the walk below
  // is over the enumeration -- which is the construction the whole suite exists to defend, now
  // demonstrated rather than asserted.
  //
  // What still has to be written down is the NAME, for the reason the status topic's is: a rename
  // inside the builder that kept the enumeration intact would move a retained topic on the owner's
  // broker in silence, leaving the old one there for ever with nothing to erase it.
  const comfoair_ha_ctx_t c = ctx();
  char                    name[256];
  const size_t            n = comfoair_ha_meta_topic(&c, COMFOAIR_HA_META_BUS, name, sizeof name);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/bus", name);
  TEST_ASSERT_EQUAL_size_t(n, strlen(name));
  // And it is NOT the status topic, which is the mutation a copied switch arm produces: two enum
  // values naming one string, so the second is published by nobody and erased twice.
  TEST_ASSERT_TRUE(std::string(name) != status_topic(c));
  TEST_ASSERT_EQUAL_size_t(1, occurrences(sequence(c, COMFOAIR_HA_CLEANUP_REKEY), name));
  TEST_ASSERT_EQUAL_size_t(1, occurrences(sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW), name));
}

void test_every_meta_topic_is_erased_by_both_cleanups(void) {
  // The totalising half of the rule: not "status is in there" but "every value of the enum is in
  // there, exactly once, in both sequences". A value added to comfoair_ha_meta_t and then not
  // walked is the failure this catches; -Werror=switch catches the other half, a value added and
  // never given a name.
  const comfoair_ha_ctx_t        c        = ctx();
  const std::vector<std::string> rekey    = sequence(c, COMFOAIR_HA_CLEANUP_REKEY);
  const std::vector<std::string> withdraw = sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW);
  for (size_t which = 0; which < COMFOAIR_HA_META_COUNT; which++) {
    char         meta[256];
    const size_t n =
        comfoair_ha_meta_topic(&c, (comfoair_ha_meta_t)which, meta, sizeof meta);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "a meta value has no topic of its own");
    TEST_ASSERT_EQUAL_size_t(1, occurrences(rekey, meta));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(withdraw, meta));
  }
}

void test_a_meta_value_the_builder_does_not_know_yields_nothing(void) {
  // COMFOAIR_HA_META_COUNT is a value of the enum, so the switch must answer it, and the answer
  // must be "no topic" rather than a fall-through that returns a bare prefix. A cleanup that
  // published `<prefix>` itself would empty a topic this device never wrote.
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_meta_topic(&c, COMFOAIR_HA_META_COUNT, g_buf, sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(
      0, comfoair_ha_meta_topic(&c, (comfoair_ha_meta_t)(COMFOAIR_HA_META_COUNT + 7), g_buf,
                                sizeof g_buf));
}

// --- how many, and why the number is not typed here ------------------------------------------

void test_the_state_side_is_status_plus_every_entity_plus_the_fans_three_plus_every_meta_topic(
    void) {
  // COMPUTED, NOT WRITTEN, and this is the case the whole construction is for. `status` is itself a
  // value of comfoair_ha_meta_t, so "status plus every meta topic" is one set and not two -- the
  // name of this test spells them apart only for the reader.
  //
  // The three terms: every meta topic; every entity's `<prefix>/<key>/state` except the fan's,
  // because the fan does not have one; and the fan's retained leaves, which is what publish_fan()
  // sends. That sum is computed -- and the day comfoair_ha_meta_t or
  // comfoair_ha_fan_leaf_t grows a value, both sides of this assertion move together without
  // anybody editing this line. A literal count here would go stale in silence, which is the entire
  // failure mode being designed against.
  //
  // THE THIRD TERM WAS A LITERAL 3 UNTIL 2026-09-07, which made this line the FOURTH copy of the
  // fan's leaf list -- found by mutation: adding a fourth retained leaf failed here with
  // "Expected 78 Was 79", reporting a correct cleanup as a defect while the real defect (the
  // publisher having no payload for it) was caught somewhere else entirely.
  const size_t expected =
      COMFOAIR_HA_META_COUNT + (COMFOAIR_ENTITY_COUNT - 1) + COMFOAIR_HA_FAN_RETAINED_COUNT;
  TEST_ASSERT_EQUAL_size_t(expected, comfoair_ha_cleanup_count(COMFOAIR_HA_CLEANUP_REKEY));

  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_EQUAL_size_t(expected, sequence(c, COMFOAIR_HA_CLEANUP_REKEY).size());
}

void test_a_withdrawal_is_the_discovery_side_and_the_state_side_together(void) {
  // The discovery side is one config document per PUBLISHED entity -- the same filter
  // publish_discovery() applies (comfoair_mqtt_publish.c:116-119) -- and the state side is the one
  // above. Nothing else: the command topics are written by Home Assistant and by whoever holds
  // mosquitto_pub, and erasing those would be erasing somebody else's retained message.
  const size_t published = []() {
    size_t n = 0;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
      if (comfoair_ha_component(&COMFOAIR_ENTITIES[i]) != nullptr)
        n++;
    return n;
  }();
  TEST_ASSERT_EQUAL_size_t(COMFOAIR_ENTITY_COUNT, published);
  TEST_ASSERT_EQUAL_size_t(published + comfoair_ha_cleanup_count(COMFOAIR_HA_CLEANUP_REKEY),
                           comfoair_ha_cleanup_count(COMFOAIR_HA_CLEANUP_WITHDRAW));
}

// --- the two operations, and what separates them ----------------------------------------------

void test_a_rekey_leaves_discovery_alone(void) {
  // The owner changed nothing but the prefix. The config topic is
  // homeassistant/<component>/<device_id>/<key>/config (comfoair_ha.c:148-157) and carries neither
  // the prefix nor the name, so every config document is simply REWRITTEN IN PLACE with the new
  // stat_t the moment the new client connects. Home Assistant moves every entity across without
  // noticing.
  //
  // Emptying them instead would be actively destructive and is the only way to reach "zero
  // devices" from this side: HA would delete every entity with its entity_id, their history
  // and every reference to them in the owner's automations, and recreate them from the next config
  // message -- same uniq_id, but after a registry removal the entity_id is not guaranteed.
  const comfoair_ha_ctx_t c = ctx();
  for (const std::string &topic : sequence(c, COMFOAIR_HA_CLEANUP_REKEY)) {
    TEST_ASSERT_FALSE_MESSAGE(is_discovery(topic), topic.c_str());
    TEST_ASSERT_TRUE_MESSAGE(topic.compare(0, strlen(c.topic_prefix), c.topic_prefix) == 0,
                             topic.c_str());
  }
}

void test_a_withdraw_empties_discovery_before_state(void) {
  // The device is leaving this broker altogether, so the goal is the opposite of a rekey: there
  // must be no entities left. Emptying the configs FIRST gets a clean removal, and nobody reads the
  // states afterwards. The reverse order leaves a window in which live entities point at topics
  // that have just been emptied, and that is the reason this is a property of the sequence rather
  // than a sentence in a comment.
  const comfoair_ha_ctx_t        c   = ctx();
  const std::vector<std::string> seq = sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW);

  bool seen_state = false;
  for (const std::string &topic : seq) {
    if (is_discovery(topic)) {
      TEST_ASSERT_FALSE_MESSAGE(seen_state, "a discovery topic came after a state topic");
      continue;
    }
    // Every non-discovery entry belongs to this device's own tree; there is no third category.
    TEST_ASSERT_TRUE_MESSAGE(topic.compare(0, strlen(c.topic_prefix), c.topic_prefix) == 0,
                             topic.c_str());
    seen_state = true;
  }
  TEST_ASSERT_TRUE_MESSAGE(seen_state, "the withdrawal erased no state topic at all");
  TEST_ASSERT_TRUE_MESSAGE(is_discovery(seq.front()), "the withdrawal did not start with discovery");
}

void test_retraction_never_publishes_offline_itself(void) {
  // Retained `offline` on <prefix>/status is already published by comfoair_mqtt_stop_client()
  // (comfoair_mqtt_client.c:150-155, "OFFLINE, EXPLICITLY, before a clean stop"), and the
  // retraction runs immediately before that call. A second publisher of the same fact would be a
  // second place of truth: the day the will topic changes, one of them follows and the other does
  // not.
  //
  // The enumerator has no payload argument at all -- an empty retained payload is what erases a
  // topic (MQTT 3.1.1 section 3.3.1.3) and it is supplied at the single call site. So the shape of
  // a smuggled `offline` here would be either a second occurrence of the status topic or a payload
  // packed into the topic buffer, and both are pinned below.
  const comfoair_ha_ctx_t          c    = ctx();
  const std::string                stat = status_topic(c);
  const comfoair_ha_cleanup_t      both[] = {COMFOAIR_HA_CLEANUP_REKEY, COMFOAIR_HA_CLEANUP_WITHDRAW};
  for (comfoair_ha_cleanup_t what : both) {
    const std::vector<std::string> seq = sequence(c, what);
    TEST_ASSERT_EQUAL_size_t(1, occurrences(seq, stat));
    for (const std::string &topic : seq) {
      TEST_ASSERT_NULL_MESSAGE(strstr(topic.c_str(), COMFOAIR_HA_PAYLOAD_OFFLINE), topic.c_str());
      TEST_ASSERT_NULL_MESSAGE(strstr(topic.c_str(), COMFOAIR_HA_PAYLOAD_ONLINE), topic.c_str());
    }
  }
}

// --- the cleanup has no formatting of its own -------------------------------------------------

void test_the_cleanup_walks_the_same_builders_as_the_publisher(void) {
  // Byte for byte against the four builders the publisher uses. A cleanup that formatted its own
  // topics would describe the tree in two places, and the second one would be the one nobody
  // re-checks when a segment moves.
  const comfoair_ha_ctx_t        c   = ctx();
  const std::vector<std::string> seq = sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW);
  std::set<std::string>          expected;

  char topic[256];
  for (size_t which = 0; which < COMFOAIR_HA_META_COUNT; which++)
    if (comfoair_ha_meta_topic(&c, (comfoair_ha_meta_t)which, topic, sizeof topic) > 0)
      expected.insert(topic);
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
    if (comfoair_ha_component(e) == nullptr)
      continue;
    TEST_ASSERT_TRUE(comfoair_ha_discovery_topic(&c, e, topic, sizeof topic) > 0);
    expected.insert(topic);
    if (e->kind == COMFOAIR_KIND_FAN) {
      // WALKED, NOT LISTED. This loop used to be `{"state", "speed/state", "preset/state"}` --
      // the THIRD hand-written copy of that list, after comfoair_ha_cleanup.c and
      // comfoair_mqtt_publish.c, and the reason a fourth leaf added to publish_fan() was erased
      // by nothing and noticed by nothing. All three now walk comfoair_ha_fan_leaf_t.
      for (size_t leaf = 0; leaf < COMFOAIR_HA_FAN_RETAINED_COUNT; leaf++) {
        TEST_ASSERT_TRUE(
            comfoair_ha_fan_topic(&c, (comfoair_ha_fan_leaf_t)leaf, topic, sizeof topic) > 0);
        expected.insert(topic);
      }
      continue;
    }
    TEST_ASSERT_TRUE(comfoair_ha_state_topic(&c, e->key, topic, sizeof topic) > 0);
    expected.insert(topic);
  }

  const std::set<std::string> actual(seq.begin(), seq.end());
  TEST_ASSERT_EQUAL_size_t(expected.size(), actual.size());
  TEST_ASSERT_EQUAL_size_t(seq.size(), actual.size());  // and no topic is emitted twice
  TEST_ASSERT_TRUE(expected == actual);
}

void test_a_command_topic_is_never_erased(void) {
  // We empty only what we publish. <prefix>/<key>/command and <prefix>/ops/<name>/set are written
  // by Home Assistant or by a person with mosquitto_pub; a retained command left there during
  // debugging is somebody else's message, and this device already ignores it by its retain flag.
  const comfoair_ha_ctx_t c = ctx();
  for (const std::string &topic : sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW)) {
    TEST_ASSERT_NULL_MESSAGE(strstr(topic.c_str(), "/command"), topic.c_str());
    TEST_ASSERT_NULL_MESSAGE(strstr(topic.c_str(), "/ops/"), topic.c_str());
  }
}

// --- the two identities -----------------------------------------------------------------------

void test_the_cleanup_of_an_old_identity_never_names_the_new_prefix(void) {
  // The retraction runs while g_ctx still describes the OUTGOING identity
  // (comfoair_mqtt.c, the s_reconfigure branch, before comfoair_mqtt_stop_client()). If a single
  // topic came out under the new prefix, the device would empty a topic it is about to publish
  // under -- and the owner would lose the value that had just been written there.
  const comfoair_ha_ctx_t old_id = ctx("comfoair/aabbccddeeff");
  const char             *fresh  = "attic/hrv";
  for (const std::string &topic : sequence(old_id, COMFOAIR_HA_CLEANUP_WITHDRAW))
    TEST_ASSERT_NULL_MESSAGE(strstr(topic.c_str(), fresh), topic.c_str());

  // And the same sequence under the new identity names nothing of the old one.
  const comfoair_ha_ctx_t new_id = ctx(fresh);
  for (const std::string &topic : sequence(new_id, COMFOAIR_HA_CLEANUP_WITHDRAW))
    TEST_ASSERT_NULL_MESSAGE(strstr(topic.c_str(), "comfoair/aabbccddeeff/"), topic.c_str());
}

void test_the_discovery_topic_does_not_depend_on_the_prefix_or_the_name(void) {
  // The invariant the whole cleanup design rests on: the discovery topic is built from the fixed
  // `homeassistant` prefix, the component, the MAC-derived device id and the registry key, and
  // from NO setting the owner can edit -- which is why changing the prefix or the name
  // cannot produce a duplicate device in Home Assistant. If COMFOAIR_HA_DISCOVERY_PREFIX ever
  // becomes an editable field, the guarantee leaves with it -- and this test is what says so.
  const comfoair_ha_ctx_t a = ctx("comfoair/aabbccddeeff", "ComfoAir Q eeff");
  const comfoair_ha_ctx_t b = ctx("attic/hrv", "Loft ventilation");

  std::vector<std::string> from_a, from_b;
  for (const std::string &t : sequence(a, COMFOAIR_HA_CLEANUP_WITHDRAW))
    if (is_discovery(t))
      from_a.push_back(t);
  for (const std::string &t : sequence(b, COMFOAIR_HA_CLEANUP_WITHDRAW))
    if (is_discovery(t))
      from_b.push_back(t);

  TEST_ASSERT_EQUAL_size_t(COMFOAIR_ENTITY_COUNT, from_a.size());
  TEST_ASSERT_EQUAL_size_t(from_a.size(), from_b.size());
  for (size_t i = 0; i < from_a.size(); i++)
    TEST_ASSERT_EQUAL_STRING(from_a[i].c_str(), from_b[i].c_str());
}

// --- the fan's leaves are an enumeration, not a list -------------------------------------------

void test_every_retained_fan_leaf_is_erased_by_both_cleanups(void) {
  // THE SECOND HOLE a review found on 2026-09-07, and the one the file's own header denied
  // having: the three leaves publish_fan() writes existed as a hand-written array in
  // comfoair_ha_cleanup.c, a second array in comfoair_mqtt_publish.c and a third in this suite.
  // A fourth leaf added to publish_fan() was therefore erased by nothing and caught by nothing --
  // verified by mutation, all cases stayed green. comfoair_ha_fan_topic() now takes a value of
  // comfoair_ha_fan_leaf_t and nothing else, so the leaf cannot exist before the enum has it, and
  // the moment it does this loop walks it.
  //
  // The totalising half, exactly as test_every_meta_topic_is_erased_by_both_cleanups does for the
  // top-level topics; -Werror=switch catches the other half, a value added and never named.
  const comfoair_ha_ctx_t        c        = ctx();
  const std::vector<std::string> rekey    = sequence(c, COMFOAIR_HA_CLEANUP_REKEY);
  const std::vector<std::string> withdraw = sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW);
  TEST_ASSERT_TRUE_MESSAGE(COMFOAIR_HA_FAN_RETAINED_COUNT > 0, "no fan leaf is retained at all");
  for (size_t leaf = 0; leaf < COMFOAIR_HA_FAN_RETAINED_COUNT; leaf++) {
    char         name[256];
    const size_t n = comfoair_ha_fan_topic(&c, (comfoair_ha_fan_leaf_t)leaf, name, sizeof name);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "a retained fan leaf has no topic of its own");
    TEST_ASSERT_EQUAL_size_t(1, occurrences(rekey, name));
    TEST_ASSERT_EQUAL_size_t(1, occurrences(withdraw, name));
  }
}

void test_a_fan_command_leaf_is_never_erased(void) {
  // The other side of the same enumeration. A fan's command topics are written by Home Assistant
  // and by whoever holds mosquitto_pub; emptying one is emptying somebody else's retained
  // message. They are values of the enum so that the discovery document and the
  // subscription can name them without a hand-written string -- being a value must NOT be the
  // same as being erased, and that is what this pins.
  const comfoair_ha_ctx_t        c   = ctx();
  const std::vector<std::string> seq = sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW);
  for (size_t leaf = COMFOAIR_HA_FAN_RETAINED_COUNT; leaf < COMFOAIR_HA_FAN_LEAF_COUNT; leaf++) {
    char         name[256];
    const size_t n = comfoair_ha_fan_topic(&c, (comfoair_ha_fan_leaf_t)leaf, name, sizeof name);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "a fan leaf the publisher names has no topic");
    TEST_ASSERT_EQUAL_size_t(0, occurrences(seq, name));
  }
}

void test_the_retained_fan_leaves_come_first_and_are_the_ones_the_publisher_writes(void) {
  // The boundary is an ORDERING, so it has to be pinned as one: everything below
  // COMFOAIR_HA_FAN_RETAINED_COUNT is a `/state` leaf this device publishes retained, and
  // everything at or above it is a `/command` leaf it only ever reads. Inserting a command leaf
  // among the first three would make the cleanup erase it -- a command topic belongs to whoever
  // published to it, never to this device; appending a state leaf after them would make the cleanup
  // miss it, which is the orphan this whole enumeration exists to prevent.
  const comfoair_ha_ctx_t c = ctx();
  char                    name[256];
  for (size_t leaf = 0; leaf < COMFOAIR_HA_FAN_LEAF_COUNT; leaf++) {
    TEST_ASSERT_TRUE(comfoair_ha_fan_topic(&c, (comfoair_ha_fan_leaf_t)leaf, name, sizeof name) > 0);
    const bool retained = leaf < COMFOAIR_HA_FAN_RETAINED_COUNT;
    TEST_ASSERT_EQUAL_MESSAGE(retained, strstr(name, "/command") == nullptr, name);
  }
  // And the three the publisher actually sends, by name, because a rename in the enum that kept
  // the count would move every topic on the owner's broker in silence.
  TEST_ASSERT_TRUE(comfoair_ha_fan_topic(&c, COMFOAIR_HA_FAN_STATE, name, sizeof name) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/fan/state", name);
  TEST_ASSERT_TRUE(comfoair_ha_fan_topic(&c, COMFOAIR_HA_FAN_SPEED_STATE, name, sizeof name) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/fan/speed/state", name);
  TEST_ASSERT_TRUE(comfoair_ha_fan_topic(&c, COMFOAIR_HA_FAN_PRESET_STATE, name, sizeof name) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/fan/preset/state", name);
}

void test_a_fan_leaf_the_builder_does_not_know_yields_nothing(void) {
  // COMFOAIR_HA_FAN_LEAF_COUNT is a value of the enum, so the switch must answer it, and the
  // answer must be "no topic" rather than a fall-through to `<prefix>/fan` -- a topic this
  // device never wrote and must never empty.
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_fan_topic(&c, COMFOAIR_HA_FAN_LEAF_COUNT, g_buf,
                                                    sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(
      0, comfoair_ha_fan_topic(&c, (comfoair_ha_fan_leaf_t)(COMFOAIR_HA_FAN_LEAF_COUNT + 7), g_buf,
                               sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_fan_topic(nullptr, COMFOAIR_HA_FAN_STATE, g_buf,
                                                    sizeof g_buf));
}

// --- the two ways round the enumeration, closed ------------------------------------------------

void test_a_state_topic_can_only_be_built_for_a_key_in_the_registry(void) {
  // THE FIRST OF THE TWO HOLES a review found on 2026-09-07. The rule this component rests on is
  // "a retained topic does not exist until the enumeration can name it", and it was stated as a
  // ban on hand-written snprintf -- but comfoair_ha_state_topic() would build
  // `<prefix>/<anything>/state` for any string at all. One line in the publisher naming a key the
  // registry does not carry produces a retained topic the cleanup walks straight past, and it
  // stays on the owner's broker for ever: there is no second pass that goes looking for orphans.
  //
  // The registry is the door for entity topics exactly as comfoair_ha_meta_t is the door for
  // top-level ones, and comfoair_entity_by_key() is how it is asked (comfoair_state.h:79).
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_TRUE(comfoair_ha_state_topic(&c, "supply_air_temp", g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_state_topic(&c, "not_a_key", g_buf, sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_state_topic(&c, "", g_buf, sizeof g_buf));
  // A key with the right prefix but a suffix of its own: the shape a hand-rolled "one more
  // topic for this entity" takes.
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_state_topic(&c, "supply_air_temp/raw", g_buf,
                                                      sizeof g_buf));

  // Every key the registry does carry still works, or the check has been made too strict and the
  // cleanup would refuse to name topics the publisher writes.
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    TEST_ASSERT_TRUE_MESSAGE(
        comfoair_ha_state_topic(&c, COMFOAIR_ENTITIES[i].key, g_buf, sizeof g_buf) > 0,
        COMFOAIR_ENTITIES[i].key);
}

// --- the entity that publishes on a clock ------------------------------------------------------

void test_the_freshness_entity_publishes_into_the_ordinary_state_topic(void) {
  // THE CHEAPEST HALF OF THE FRESHNESS ENTITY, and the half a draft got wrong. `data_freshness`
  // (docs/interfaces/home-assistant-discovery.md, Availability) is the only thing this firmware
  // sends on a timer, and the tempting shape for it is a top-level `<prefix>/heartbeat` -- one more
  // retained topic, published by hand, erased by nobody, living on the owner's broker for ever.
  // Because it is a REGISTRY ROW instead, its state lands on `<prefix>/<key>/state` inside the
  // enumeration that already existed, and this file needed one case rather than a new rule: the
  // count above moved 79 -> 80 on its own.
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_TRUE(comfoair_ha_state_topic(&c, COMFOAIR_FRESHNESS_KEY, g_buf, sizeof g_buf) > 0);
  const std::string state = g_buf;
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/data_freshness/state", state.c_str());

  const std::vector<std::string> rekey = sequence(c, COMFOAIR_HA_CLEANUP_REKEY);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(1, occurrences(rekey, state),
                                   "the clock's topic is not the one the cleanup empties");
  // Its whole contribution is that one state topic plus, on a withdrawal, its config document --
  // no third topic anywhere. A `<prefix>/heartbeat` would be a THIRD value of comfoair_ha_meta_t,
  // so the enum's length is what says it does not exist; the substring sweep catches the other
  // spelling, a topic built by hand outside the enumeration entirely.
  TEST_ASSERT_EQUAL_size_t(2, (size_t)COMFOAIR_HA_META_COUNT);
  const std::vector<std::string> withdraw = sequence(c, COMFOAIR_HA_CLEANUP_WITHDRAW);
  size_t named = 0;
  for (const std::string &topic : withdraw) {
    TEST_ASSERT_TRUE_MESSAGE(topic.find("heartbeat") == std::string::npos, topic.c_str());
    if (topic.find(COMFOAIR_FRESHNESS_KEY) != std::string::npos)
      named++;
  }
  TEST_ASSERT_EQUAL_size_t_MESSAGE(2, named, "the config document and the state topic, and no more");
}

// --- refusals ---------------------------------------------------------------------------------

void test_a_topic_that_does_not_fit_reports_zero_rather_than_a_truncated_one(void) {
  // A truncated topic is a DIFFERENT topic: it would empty something else, successfully, and the
  // thing that had to be emptied would stay. The publisher logs the index and moves on rather than
  // stopping the sweep, so the refusal has to be visible per topic.
  const comfoair_ha_ctx_t c = ctx();
  char                    tiny[8];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_cleanup_topic(&c, COMFOAIR_HA_CLEANUP_WITHDRAW, 0, tiny,
                                                        sizeof tiny));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_meta_topic(&c, COMFOAIR_HA_META_STATUS, tiny, sizeof tiny));
}

void test_no_retraction_topic_truncates_at_the_longest_prefix_the_config_allows(void) {
  // The publisher formats into s_topic[192] (comfoair_mqtt_publish.c:22) and a truncated topic is
  // a DIFFERENT topic -- it would empty something else, successfully, while the topic that had to
  // go stayed. The stored prefix is capped at COMFOAIR_CONFIG_PREFIX_MAX = 64
  // (comfoair_config.h:148), so THAT is the case worth measuring, and it is measured here rather
  // than estimated: the assertion below prints the worst length it saw.
  std::string longest(64, 'p');
  const comfoair_ha_ctx_t c = ctx(longest.c_str());

  size_t worst = 0;
  for (const comfoair_ha_cleanup_t what :
       {COMFOAIR_HA_CLEANUP_REKEY, COMFOAIR_HA_CLEANUP_WITHDRAW}) {
    const size_t n = comfoair_ha_cleanup_count(what);
    for (size_t i = 0; i < n; i++) {
      char         publisher_buffer[192];
      const size_t len =
          comfoair_ha_cleanup_topic(&c, what, i, publisher_buffer, sizeof publisher_buffer);
      TEST_ASSERT_TRUE_MESSAGE(len > 0, "a retraction topic did not fit the publisher's buffer");
      if (len > worst)
        worst = len;
    }
  }
  // Pinned from both sides: below the buffer, and high enough that a change which stopped
  // exercising the long prefix would be noticed rather than silently relaxing the guarantee.
  TEST_ASSERT_TRUE_MESSAGE(worst < 192, "the worst retraction topic outgrew the publisher's buffer");
  TEST_ASSERT_TRUE_MESSAGE(worst > 80, "the longest prefix is not being exercised any more");
}

void test_an_index_past_the_end_and_a_missing_context_are_answered_not_dereferenced(void) {
  const comfoair_ha_ctx_t c = ctx();
  const size_t            n = comfoair_ha_cleanup_count(COMFOAIR_HA_CLEANUP_WITHDRAW);
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_cleanup_topic(&c, COMFOAIR_HA_CLEANUP_WITHDRAW, n, g_buf,
                                                        sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_cleanup_topic(nullptr, COMFOAIR_HA_CLEANUP_REKEY, 0, g_buf,
                                                        sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_cleanup_topic(&c, COMFOAIR_HA_CLEANUP_REKEY, 0, nullptr, 0));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_meta_topic(nullptr, COMFOAIR_HA_META_STATUS, g_buf,
                                                     sizeof g_buf));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_status_topic_has_exactly_one_producer);
  RUN_TEST(test_the_bus_topic_is_named_here_and_erased_like_every_other_meta_topic);
  RUN_TEST(test_every_meta_topic_is_erased_by_both_cleanups);
  RUN_TEST(test_a_meta_value_the_builder_does_not_know_yields_nothing);

  RUN_TEST(test_the_state_side_is_status_plus_every_entity_plus_the_fans_three_plus_every_meta_topic);
  RUN_TEST(test_a_withdrawal_is_the_discovery_side_and_the_state_side_together);

  RUN_TEST(test_a_rekey_leaves_discovery_alone);
  RUN_TEST(test_a_withdraw_empties_discovery_before_state);
  RUN_TEST(test_retraction_never_publishes_offline_itself);

  RUN_TEST(test_the_cleanup_walks_the_same_builders_as_the_publisher);
  RUN_TEST(test_a_command_topic_is_never_erased);
  RUN_TEST(test_every_retained_fan_leaf_is_erased_by_both_cleanups);
  RUN_TEST(test_a_fan_command_leaf_is_never_erased);
  RUN_TEST(test_the_retained_fan_leaves_come_first_and_are_the_ones_the_publisher_writes);
  RUN_TEST(test_a_fan_leaf_the_builder_does_not_know_yields_nothing);
  RUN_TEST(test_a_state_topic_can_only_be_built_for_a_key_in_the_registry);
  RUN_TEST(test_the_freshness_entity_publishes_into_the_ordinary_state_topic);

  RUN_TEST(test_the_cleanup_of_an_old_identity_never_names_the_new_prefix);
  RUN_TEST(test_the_discovery_topic_does_not_depend_on_the_prefix_or_the_name);

  RUN_TEST(test_a_topic_that_does_not_fit_reports_zero_rather_than_a_truncated_one);
  RUN_TEST(test_no_retraction_topic_truncates_at_the_longest_prefix_the_config_allows);
  RUN_TEST(test_an_index_past_the_end_and_a_missing_context_are_answered_not_dereferenced);
  return UNITY_END();
}
