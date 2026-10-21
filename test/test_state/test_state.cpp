// The state model: PDO frames in, entity values out.
//
// The registry these read is generated from pdo_table.py, so the expectations below are
// written against what that table says -- not against numbers copied out of the generated
// header, which would only prove the generator equals itself.
#include <unity.h>

#include <cstring>
#include <initializer_list>

#include "comfoair_state.h"
#include "registry_generated.h"

// Helper: find a generated descriptor by key so a test can state its intent by name.
static const comfoair_entity_t *entity(const char *key) {
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
      return &COMFOAIR_ENTITIES[i];
  return nullptr;
}

void setUp(void) { comfoair_state_reset(); }
void tearDown(void) {}

// --- lookup -------------------------------------------------------------------------------

void test_every_entity_can_be_found_by_its_key(void) {
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    TEST_ASSERT_NOT_NULL(comfoair_entity_by_key(COMFOAIR_ENTITIES[i].key));
}

void test_an_unknown_key_is_not_invented(void) {
  TEST_ASSERT_NULL(comfoair_entity_by_key("no_such_entity"));
}

// --- numeric ------------------------------------------------------------------------------

void test_a_numeric_pdo_lands_on_its_entity_with_the_divider_applied(void) {
  // PDO 221 is a temperature in tenths of a degree: divider 10, signed.
  const comfoair_entity_t *e = entity("supply_air_temp_post_heater");
  TEST_ASSERT_NOT_NULL_MESSAGE(e, "pdo_table lost supply_air_temp_post_heater");
  TEST_ASSERT_EQUAL_INT16(221, e->pdo);
  TEST_ASSERT_EQUAL_INT32(10, e->divider);

  const uint8_t frame[] = {0xd7, 0x00};  // 215 -> 21.5
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(221, frame, sizeof frame));

  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("supply_air_temp_post_heater", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NUMBER, v.type);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, 21.5f, v.number);
}

void test_a_negative_temperature_survives_sign_extension(void) {
  const uint8_t frame[] = {0xff, 0xff};  // -1 -> -0.1
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(221, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("supply_air_temp_post_heater", &v));
  TEST_ASSERT_FLOAT_WITHIN(0.001f, -0.1f, v.number);
}

void test_a_countdown_sentinel_reads_as_unset_not_as_a_huge_number(void) {
  // PDO 81's all-ones means "nothing scheduled". Publishing 4294967295 seconds would be a
  // 136-year countdown that looks like data.
  const comfoair_entity_t *e = entity("next_fan_change_in_raw");
  TEST_ASSERT_TRUE(e->none_when_all_ones);
  const uint8_t frame[] = {0xff, 0xff, 0xff, 0xff};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(81, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("next_fan_change_in_raw", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

// --- binary -------------------------------------------------------------------------------

void test_a_binary_entity_compares_against_its_table_value(void) {
  // manual_mode: PDO 56 equals 1.
  const comfoair_entity_t *e = entity("manual_mode");
  TEST_ASSERT_NOT_NULL_MESSAGE(e, "pdo_table lost manual_mode");
  TEST_ASSERT_EQUAL_INT32(1, e->match_value);
  TEST_ASSERT_FALSE(e->match_negated);

  const uint8_t on[] = {1};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(56, on, sizeof on));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("manual_mode", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_BOOL, v.type);
  TEST_ASSERT_TRUE(v.boolean);

  const uint8_t auto_mode[] = {0xff};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(56, auto_mode, sizeof auto_mode));
  TEST_ASSERT_TRUE(comfoair_state_get("manual_mode", &v));
  TEST_ASSERT_FALSE(v.boolean);
}

// --- enum ---------------------------------------------------------------------------------

void test_an_enum_pdo_becomes_its_label(void) {
  const comfoair_entity_t *e = entity("device_state");
  TEST_ASSERT_NOT_NULL(e);
  const uint8_t frame[] = {1};  // the table's second option
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo((uint16_t)e->pdo, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("device_state", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_STRING(e->options[1], v.text);
}

void test_an_enum_value_the_table_does_not_list_is_not_guessed(void) {
  const comfoair_entity_t *e = entity("device_state");
  const uint8_t frame[] = {200};
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo((uint16_t)e->pdo, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("device_state", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

// --- misc ---------------------------------------------------------------------------------

void test_one_pdo_updates_every_entity_that_claims_it(void) {
  // PDO 337 feeds THREE entities at once: boost_timer_running and away_timer_running (binary,
  // bits 6 and 11) and away_mode (the switch, bit 11). A design of one entity per PDO would
  // silently drop two of them, so this is the test that forbids it.
  const uint8_t away[] = {0x00, 0x08, 0x00, 0x00};  // 2048: timer 11 only
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(337, away, sizeof away));

  comfoair_value_t boost{}, timer{}, mode{};
  TEST_ASSERT_TRUE(comfoair_state_get("boost_timer_running", &boost));
  TEST_ASSERT_TRUE(comfoair_state_get("away_timer_running", &timer));
  TEST_ASSERT_TRUE(comfoair_state_get("away_mode", &mode));

  TEST_ASSERT_FALSE_MESSAGE(boost.boolean, "boost_timer_running did not update");
  TEST_ASSERT_TRUE_MESSAGE(timer.boolean, "away_timer_running did not update");
  TEST_ASSERT_TRUE_MESSAGE(mode.boolean, "away_mode did not update");
}

void test_a_pdo_no_entity_claims_is_reported_as_unhandled(void) {
  const uint8_t frame[] = {0};
  TEST_ASSERT_FALSE(comfoair_state_apply_pdo(1023, frame, sizeof frame));
}

void test_an_entity_with_no_frame_yet_has_no_value(void) {
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("supply_air_temp_post_heater", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NONE, v.type);
}

void test_reading_an_unknown_key_fails_rather_than_returning_empty(void) {
  // "unset" and "no such entity" are different answers and a caller must be able to tell
  // them apart -- a REST 404 versus a null field.
  comfoair_value_t v{};
  TEST_ASSERT_FALSE(comfoair_state_get("no_such_entity", &v));
}

// --- entities derived from more than one PDO ------------------------------------------------
//
// Three entities are not carried by any single PDO, so the generated registry cannot feed
// them and the rules live here, each pinned by a test below.

void test_the_fan_says_nothing_until_a_level_has_arrived(void) {
  // "The level alone is enough to publish; the operating mode only refines the preset."
  // So PDO 49 on its own must not produce a fan reading.
  const uint8_t mode_auto[] = {0xff};
  comfoair_state_apply_pdo(49, mode_auto, sizeof mode_auto);

  comfoair_fan_state_t fan{};
  TEST_ASSERT_TRUE(comfoair_state_get_fan(&fan));
  TEST_ASSERT_FALSE_MESSAGE(fan.known, "the fan published before any level was known");
}

void test_the_fan_is_off_at_level_zero_and_on_above_it(void) {
  const uint8_t level0[] = {0};
  comfoair_state_apply_pdo(65, level0, sizeof level0);
  comfoair_fan_state_t fan{};
  comfoair_state_get_fan(&fan);
  TEST_ASSERT_TRUE(fan.known);
  TEST_ASSERT_FALSE(fan.on);
  // speed stays 1 at level 0: a fan entity cannot express speed 0, and 0 would render as
  // "off at speed 0" rather than "off".
  TEST_ASSERT_EQUAL_UINT8(1, fan.speed);

  const uint8_t level2[] = {2};
  comfoair_state_apply_pdo(65, level2, sizeof level2);
  comfoair_state_get_fan(&fan);
  TEST_ASSERT_TRUE(fan.on);
  TEST_ASSERT_EQUAL_UINT8(2, fan.speed);
}

void test_the_preset_comes_from_the_operating_mode(void) {
  const uint8_t level[] = {2};
  comfoair_state_apply_pdo(65, level, sizeof level);

  struct { uint8_t mode; const char *preset; } cases[] = {
      {0xff, "Auto"}, {11, "Away"}, {6, "Boost"},
  };
  for (auto &c : cases) {
    const uint8_t frame[] = {c.mode};
    comfoair_state_apply_pdo(49, frame, sizeof frame);
    comfoair_fan_state_t fan{};
    comfoair_state_get_fan(&fan);
    TEST_ASSERT_NOT_NULL_MESSAGE(fan.preset, c.preset);
    TEST_ASSERT_EQUAL_STRING(c.preset, fan.preset);
  }
}

void test_under_either_manual_mode_the_preset_follows_the_level(void) {
  // PDO 49 = 1 is "limited manual", 5 is "unlimited manual"; both take the preset from
  // the level, which is why the two cases share one arm.
  for (uint8_t mode : {(uint8_t)1, (uint8_t)5}) {
    const uint8_t m[] = {mode};
    comfoair_state_apply_pdo(49, m, sizeof m);
    struct { uint8_t level; const char *preset; } cases[] = {
        {0, "Away"}, {1, "Low"}, {2, "Medium"}, {3, "High"},
    };
    for (auto &c : cases) {
      const uint8_t l[] = {c.level};
      comfoair_state_apply_pdo(65, l, sizeof l);
      comfoair_fan_state_t fan{};
      comfoair_state_get_fan(&fan);
      TEST_ASSERT_NOT_NULL(fan.preset);
      TEST_ASSERT_EQUAL_STRING(c.preset, fan.preset);
    }
  }
}

void test_an_operating_mode_the_rules_do_not_cover_clears_the_preset(void) {
  const uint8_t level[] = {2};
  comfoair_state_apply_pdo(65, level, sizeof level);
  const uint8_t odd[] = {99};
  comfoair_state_apply_pdo(49, odd, sizeof odd);
  comfoair_fan_state_t fan{};
  comfoair_state_get_fan(&fan);
  TEST_ASSERT_TRUE(fan.known);
  TEST_ASSERT_NULL_MESSAGE(fan.preset, "a preset was invented for an unknown mode");
}

void test_ventilation_mode_mirrors_the_fan_preset(void) {
  const uint8_t level[] = {2};
  const uint8_t mode[] = {0xff};
  comfoair_state_apply_pdo(65, level, sizeof level);
  comfoair_state_apply_pdo(49, mode, sizeof mode);
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("ventilation_mode", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_STRING("Auto", v.text);
}

void test_ventilation_mode_keeps_its_last_value_when_there_is_no_preset(void) {
  // "A null preset is the fan's no-preset state, which a select cannot express: leave the
  // select on its last value rather than inventing one."
  const uint8_t level[] = {2};
  const uint8_t auto_mode[] = {0xff};
  comfoair_state_apply_pdo(65, level, sizeof level);
  comfoair_state_apply_pdo(49, auto_mode, sizeof auto_mode);

  const uint8_t odd[] = {99};
  comfoair_state_apply_pdo(49, odd, sizeof odd);

  comfoair_value_t v{};
  comfoair_state_get("ventilation_mode", &v);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_TEXT, v.type, "the select was cleared");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("Auto", v.text, "the select lost its last value");
}

void test_balance_mode_waits_for_both_halves(void) {
  // "Publishing from one PDO alone flip-flops the entity."
  const uint8_t supply[] = {0xff};
  comfoair_state_apply_pdo(54, supply, sizeof supply);
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("balance_mode", &v));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_NONE, v.type,
                            "balance_mode published from one PDO");
}

void test_balance_mode_reads_all_three_states(void) {
  struct { uint8_t supply, exhaust; const char *label; } cases[] = {
      {0xff, 0xff, "Balanced"},
      {1,    0xff, "Supply Only"},
      // The table's label is "Extract Only", not "Exhaust Only" -- the old C++ enumerator
      // is named EXHAUST but the contract string the user sees is Extract.
      {0xff, 1,    "Extract Only"},
  };
  for (auto &c : cases) {
    const uint8_t s[] = {c.supply};
    const uint8_t e[] = {c.exhaust};
    comfoair_state_apply_pdo(54, s, sizeof s);
    comfoair_state_apply_pdo(55, e, sizeof e);
    comfoair_value_t v{};
    comfoair_state_get("balance_mode", &v);
    TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
    TEST_ASSERT_EQUAL_STRING(c.label, v.text);
  }
}

// --- selects with PDO feedback --------------------------------------------------------------
//
// Found by counter-review: bypass_mode (PDO 66) and temperature_profile (PDO 67) never
// received a value at all. Their wire byte is an INDEX into the options, but they were being
// decoded like an enum, which looks the value up in a table the
// generator only emits for enums. The result was two of the thirteen controllable entities
// silently dead. No test covered a PDO-backed select; this is that test.

void test_a_pdo_backed_select_takes_the_byte_as_an_option_index(void) {
  const comfoair_entity_t *e = comfoair_entity_by_key("bypass_mode");
  TEST_ASSERT_NOT_NULL(e);
  TEST_ASSERT_EQUAL_INT16(66, e->pdo);

  const uint8_t closed[] = {2};  // 0 auto, 1 activated, 2 deactivated -> Auto/Open/Closed
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(66, closed, sizeof closed));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("bypass_mode", &v));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_TEXT, v.type, "bypass_mode never got a value");
  TEST_ASSERT_EQUAL_STRING(e->options[2], v.text);
}

void test_the_other_pdo_backed_select_too(void) {
  const uint8_t cool[] = {1};  // 0 normal, 1 cool, 2 warm
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(67, cool, sizeof cool));
  comfoair_value_t v{};
  comfoair_state_get("temperature_profile", &v);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_STRING("Cool", v.text);
}

void test_an_index_past_the_end_of_the_options_is_not_published(void) {
  const uint8_t out_of_range[] = {9};
  comfoair_state_apply_pdo(66, out_of_range, sizeof out_of_range);
  comfoair_value_t v{};
  comfoair_state_get("bypass_mode", &v);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_NONE, v.type, "an out-of-range index was published");
}

void test_an_enum_still_maps_by_value_not_by_index(void) {
  // device_state's options are values 0..8, so index and value coincide -- operating_mode's
  // do not: its values are 1, 5, 6, 11, 255. If enums were switched to index lookup this
  // would start returning the wrong label, so it is pinned here.
  const comfoair_entity_t *e = comfoair_entity_by_key("operating_mode");
  TEST_ASSERT_NOT_NULL(e);
  const uint8_t away[] = {11};
  comfoair_state_apply_pdo((uint16_t)e->pdo, away, sizeof away);
  comfoair_value_t v{};
  comfoair_state_get("operating_mode", &v);
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("Away", v.text, "an enum was looked up by index");
}

// --- values the table supplies without any PDO ------------------------------------------------

void test_the_durations_start_at_their_cold_start_default(void) {
  // No PDO carries a duration; src/main.cpp restores them from NVS, and until it does (or on a
  // first boot) the table's default is the value in use. away_duration's 0 must survive too.
  const struct { const char *key; float want; } cases[] = {
      {"boost_duration", 60}, {"bypass_duration", 60}, {"balance_duration", 60}, {"away_duration", 0}};
  for (const auto &c : cases) {
    comfoair_value_t v{};
    TEST_ASSERT_TRUE(comfoair_state_get(c.key, &v));
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_NUMBER, v.type, c.key);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(0.001f, c.want, v.number, c.key);
  }
}

void test_the_read_back_selects_have_no_value_until_the_unit_answers(void) {
  // These three are read from the unit over RMI (COMFOAIR_RMI_READBACK). A seeded factory default
  // was a value nobody had read -- the owner's unit held different ones -- so null until it answers.
  for (const char *key : {"temperature_passive", "humidity_comfort", "humidity_protection"}) {
    comfoair_value_t v{};
    TEST_ASSERT_TRUE(comfoair_state_get(key, &v));
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_NONE, v.type, key);
  }
}

void test_the_schema_version_is_reported_not_left_null(void) {
  // This is the entity a client checks the contract against; null would make the check
  // impossible on the very device that needs it.
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("schema_version", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NUMBER, v.type);
  TEST_ASSERT_FLOAT_WITHIN(0.001f, (float)COMFOAIR_SCHEMA_VERSION, v.number);
}

// --- entities no PDO ever reports back ---------------------------------------------------------

void test_a_write_only_select_can_be_published_optimistically(void) {
  // No PDO carries these three selects: the RMI read-back and the write path both land here.
  comfoair_value_t v = {};
  TEST_ASSERT_TRUE(comfoair_state_set_option("temperature_passive", "Auto only"));
  TEST_ASSERT_TRUE(comfoair_state_get("temperature_passive", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_TEXT, v.type);
  TEST_ASSERT_EQUAL_STRING("Auto only", v.text);
}

void test_an_optimistic_option_must_be_one_the_registry_lists(void) {
  // The stored pointer is the REGISTRY's own string -- comfoair_state.h says text "points into the
  // generated registry; never freed, never copied" -- so a caller's buffer must never reach it.
  // Refusing an unknown label is what makes that true rather than hoped for.
  TEST_ASSERT_FALSE(comfoair_state_set_option("temperature_passive", "Sideways"));
  TEST_ASSERT_FALSE(comfoair_state_set_option("temperature_passive", ""));
  TEST_ASSERT_FALSE(comfoair_state_set_option("not_an_entity", "Off"));
  TEST_ASSERT_FALSE(comfoair_state_set_option("supply_temperature", "Off"));
}

void test_publishing_a_write_only_select_marks_it_changed_so_the_socket_pushes_it(void) {
  // Without the mark the owner's own change is the one thing the page does not show.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
  TEST_ASSERT_TRUE(comfoair_state_set_option("humidity_comfort", "On"));
  size_t index = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    if (strcmp(COMFOAIR_ENTITIES[i].key, "humidity_comfort") == 0)
      index = i;
  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, index));
}

void test_a_duration_is_published_as_a_number_because_no_pdo_carries_one(void) {
  // The four durations live on this node alone (pdo_table.py, NumberSpec). If the state model did not
  // hold them, GET /api/state would report null for a value the owner had just set and the device
  // was about to use.
  comfoair_value_t v = {};
  TEST_ASSERT_TRUE(comfoair_state_set_number("boost_duration", 45));
  TEST_ASSERT_TRUE(comfoair_state_get("boost_duration", &v));
  TEST_ASSERT_EQUAL(COMFOAIR_VALUE_NUMBER, v.type);
  TEST_ASSERT_EQUAL_FLOAT(45.0f, v.number);

  // And not for something a PDO does carry: a number written over a sensor would be a reading the
  // unit never sent.
  TEST_ASSERT_FALSE(comfoair_state_set_number("supply_temperature", 20));
}

void test_two_consumers_do_not_steal_each_others_changes(void) {
  // THE PROPERTY PHASE 6 TURNS ON. The WebSocket delta renderer clears the marks it reads. With
  // one shared array, whichever transport rendered first took the other's updates -- a value
  // reached Home Assistant and never reached the browser, or the reverse, depending on task
  // scheduling. Silent, asymmetric, and impossible to reproduce on demand.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);

  TEST_ASSERT_TRUE(comfoair_state_set_option("humidity_comfort", "On"));
  size_t index = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    if (strcmp(COMFOAIR_ENTITIES[i].key, "humidity_comfort") == 0)
      index = i;

  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, index));
  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index));

  // The browser renders its delta. MQTT must still have the change.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
  TEST_ASSERT_FALSE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, index));
  TEST_ASSERT_TRUE_MESSAGE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index),
                           "the push renderer took MQTT's update");
}

void test_mqtt_clears_one_entity_at_a_time_because_it_publishes_one_at_a_time(void) {
  // MQTT sends per entity, not as one document. A broker that accepts three publishes and then
  // drops the connection must not cost the rest their marks.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  TEST_ASSERT_TRUE(comfoair_state_set_number("boost_duration", 11));
  TEST_ASSERT_TRUE(comfoair_state_set_number("bypass_duration", 12));

  size_t a = 0, b = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    if (strcmp(COMFOAIR_ENTITIES[i].key, "boost_duration") == 0) a = i;
    if (strcmp(COMFOAIR_ENTITIES[i].key, "bypass_duration") == 0) b = i;
  }
  comfoair_state_clear_change(COMFOAIR_STATE_CONSUMER_MQTT, a);
  TEST_ASSERT_FALSE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, a));
  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, b));
  // And the push consumer is untouched by any of it.
  TEST_ASSERT_TRUE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, a));
}

// --- the change generation ----------------------------------------------------------------

// Helper: the index the mark functions take. Deliberately a loop over the generated table rather
// than a list of keys of its own -- a second entity list is the one thing AGENTS.md
// forbids outright.
static size_t index_of(const char *key) {
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    if (strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
      return i;
  return COMFOAIR_ENTITY_COUNT;
}

void test_a_change_landing_mid_publish_is_not_erased_by_the_clear(void) {
  // THE LOST UPDATE. The publisher reads the value, sends it, then clears the mark; a PDO applied
  // between the read and the clear used to have its mark erased by that clear, and the newer value
  // went out nowhere. boost_duration is the case that never heals: no PDO rebroadcasts it, so the
  // retained payload on the broker stays wrong until the owner next moves it by hand.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  const size_t index = index_of("boost_duration");

  TEST_ASSERT_TRUE(comfoair_state_set_number("boost_duration", 30));
  const uint32_t seen = comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index);
  // The publisher is inside publish_entity at this point; this line is the CAN task.
  TEST_ASSERT_TRUE(comfoair_state_set_number("boost_duration", 45));

  TEST_ASSERT_FALSE_MESSAGE(
      comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT, index, seen),
      "a clear at a stale generation reported that it had taken the mark");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index),
                           "the mark for the value that landed mid-publish was lost");
}

void test_a_clear_at_the_generation_it_sampled_does_take_the_mark(void) {
  // The other half, and not a formality: a clear_change_if that refused everything would satisfy
  // the test above and leave MQTT republishing every entity on every pass forever.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  const size_t index = index_of("bypass_duration");

  TEST_ASSERT_TRUE(comfoair_state_set_number("bypass_duration", 20));
  const uint32_t seen = comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index);
  TEST_ASSERT_TRUE(comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT, index, seen));
  TEST_ASSERT_FALSE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index));
  // A repeat at the same generation is still true: false is reserved for "a newer change arrived",
  // which is the one case the publisher must not treat as published. Overloading it with "there was
  // nothing to clear" would make the caller unable to tell the two apart.
  TEST_ASSERT_TRUE(comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT, index, seen));
}

void test_the_generation_is_per_consumer_like_the_mark_above_it(void) {
  // test_two_consumers_do_not_steal_each_others_changes, one level down. One shared counter would
  // make the browser's clear move MQTT's generation, and MQTT would then refuse a clear it had
  // every right to make -- publishing the same entity again on every pass, silently and forever.
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  const size_t index = index_of("away_duration");

  TEST_ASSERT_TRUE(comfoair_state_set_number("away_duration", 60));
  const uint32_t seen_push = comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_PUSH, index);
  const uint32_t seen_mqtt = comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index);

  // The browser renders its delta and clears its own mark.
  TEST_ASSERT_TRUE(comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_PUSH, index, seen_push));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      seen_mqtt, comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index),
      "the push renderer moved MQTT's generation");
  TEST_ASSERT_TRUE_MESSAGE(comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, index),
                           "the push renderer took MQTT's mark");
  TEST_ASSERT_TRUE_MESSAGE(
      comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT, index, seen_mqtt),
      "MQTT's own clear was refused because another consumer had cleared first");
}

void test_the_bulk_clear_does_not_rewind_the_generation(void) {
  // comfoair_state_clear_changes wipes a whole row of marks. If it also reset the counters, a
  // publisher holding a generation sampled before the sweep could match a later one by accident
  // and clear a mark it had never published -- the very defect this pair of calls exists to close.
  const size_t index = index_of("balance_duration");
  TEST_ASSERT_TRUE(comfoair_state_set_number("balance_duration", 15));
  const uint32_t before = comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index);
  comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_MQTT);
  TEST_ASSERT_EQUAL_UINT32(before, comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index));
  TEST_ASSERT_TRUE(comfoair_state_set_number("balance_duration", 25));
  TEST_ASSERT_FALSE_MESSAGE(
      comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT, index, before),
      "a generation sampled before the bulk clear still matched after it");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_every_entity_can_be_found_by_its_key);
  RUN_TEST(test_an_unknown_key_is_not_invented);
  RUN_TEST(test_a_numeric_pdo_lands_on_its_entity_with_the_divider_applied);
  RUN_TEST(test_a_negative_temperature_survives_sign_extension);
  RUN_TEST(test_a_countdown_sentinel_reads_as_unset_not_as_a_huge_number);
  RUN_TEST(test_a_binary_entity_compares_against_its_table_value);
  RUN_TEST(test_an_enum_pdo_becomes_its_label);
  RUN_TEST(test_an_enum_value_the_table_does_not_list_is_not_guessed);
  RUN_TEST(test_one_pdo_updates_every_entity_that_claims_it);
  RUN_TEST(test_a_pdo_no_entity_claims_is_reported_as_unhandled);
  RUN_TEST(test_an_entity_with_no_frame_yet_has_no_value);
  RUN_TEST(test_reading_an_unknown_key_fails_rather_than_returning_empty);
  RUN_TEST(test_the_fan_says_nothing_until_a_level_has_arrived);
  RUN_TEST(test_the_fan_is_off_at_level_zero_and_on_above_it);
  RUN_TEST(test_the_preset_comes_from_the_operating_mode);
  RUN_TEST(test_under_either_manual_mode_the_preset_follows_the_level);
  RUN_TEST(test_an_operating_mode_the_rules_do_not_cover_clears_the_preset);
  RUN_TEST(test_ventilation_mode_mirrors_the_fan_preset);
  RUN_TEST(test_ventilation_mode_keeps_its_last_value_when_there_is_no_preset);
  RUN_TEST(test_balance_mode_waits_for_both_halves);
  RUN_TEST(test_balance_mode_reads_all_three_states);
  RUN_TEST(test_a_pdo_backed_select_takes_the_byte_as_an_option_index);
  RUN_TEST(test_the_other_pdo_backed_select_too);
  RUN_TEST(test_an_index_past_the_end_of_the_options_is_not_published);
  RUN_TEST(test_an_enum_still_maps_by_value_not_by_index);
  RUN_TEST(test_the_durations_start_at_their_cold_start_default);
  RUN_TEST(test_the_read_back_selects_have_no_value_until_the_unit_answers);
  RUN_TEST(test_the_schema_version_is_reported_not_left_null);
  RUN_TEST(test_a_write_only_select_can_be_published_optimistically);
  RUN_TEST(test_an_optimistic_option_must_be_one_the_registry_lists);
  RUN_TEST(test_publishing_a_write_only_select_marks_it_changed_so_the_socket_pushes_it);
  RUN_TEST(test_a_duration_is_published_as_a_number_because_no_pdo_carries_one);
  RUN_TEST(test_two_consumers_do_not_steal_each_others_changes);
  RUN_TEST(test_mqtt_clears_one_entity_at_a_time_because_it_publishes_one_at_a_time);
  RUN_TEST(test_a_change_landing_mid_publish_is_not_erased_by_the_clear);
  RUN_TEST(test_a_clear_at_the_generation_it_sampled_does_take_the_mark);
  RUN_TEST(test_the_generation_is_per_consumer_like_the_mark_above_it);
  RUN_TEST(test_the_bulk_clear_does_not_rewind_the_generation);
  return UNITY_END();
}
