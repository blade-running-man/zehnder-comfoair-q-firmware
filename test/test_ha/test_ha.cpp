// The MQTT surface, and what Home Assistant has to be told to make it appear.
//
// Every discovery key asserted here was verified against Home Assistant's own validating schema
// -- a 2026.9 install vendored under ../zehnder-comfoair-q-ha -- and not against documentation or
// memory. The failure this suite exists to prevent is quiet: an invented key, or a legal key in an
// illegal combination, produces an entity that never appears, with nothing anywhere saying why.
//
// Three of HA's rules are asserted by name below because each is a trap rather than a preference:
//   * `options` requires `device_class: enum` and FORBIDS unit/state_class (mqtt/sensor.py:109-125)
//   * a fan's preset_modes and preset_mode_command_topic are an INCLUSIVE pair (mqtt/fan.py:140-147)
//   * discovery topic segments are [a-zA-Z0-9_-] only (mqtt/discovery.py:64-67)
#include <unity.h>

#include <cstring>
#include <string>

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

const comfoair_entity_t *entity(const char *key) {
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
    if (strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
      return &COMFOAIR_ENTITIES[i];
  return nullptr;
}

bool has(const char *needle) { return strstr(g_buf, needle) != nullptr; }

std::string discovery(const char *key) {
  const comfoair_ha_ctx_t c = ctx();
  const size_t n = comfoair_ha_discovery_payload(&c, entity(key), g_buf, sizeof g_buf);
  return n > 0 ? std::string(g_buf) : std::string();
}

}  // namespace

// --- topics ------------------------------------------------------------------------------------

void test_the_topic_tree_is_flat_and_keyed_by_the_entity_key(void) {
  // The registry's own header calls `key` the "stable id: API path, MQTT topic, discovery
  // object_id". An earlier draft put a <group> segment in front of it -- a column this project
  // does not have and must not grow, because a key table here would be the second entity list
  // AGENTS.md forbids.
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_TRUE(comfoair_ha_meta_topic(&c, COMFOAIR_HA_META_STATUS, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/status", g_buf);

  TEST_ASSERT_TRUE(comfoair_ha_state_topic(&c, "supply_air_temp", g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/supply_air_temp/state", g_buf);

  TEST_ASSERT_TRUE(comfoair_ha_command_topic(&c, "bypass_mode", g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/bypass_mode/command", g_buf);

  TEST_ASSERT_TRUE(comfoair_ha_fan_topic(&c, COMFOAIR_HA_FAN_PRESET_COMMAND, g_buf, sizeof g_buf)
                   > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/fan/preset/command", g_buf);
}

void test_the_operation_topic_uses_the_same_name_as_the_rest_route(void) {
  // One vocabulary for an operation across both transports. AGENTS.md criterion 3: REST covers exactly
  // what MQTT covers, and two spellings of one operation is how that stops being true.
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_TRUE(comfoair_ha_op_topic(&c, "set_balance_mode", g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("comfoair/aabbccddeeff/ops/set_balance_mode/set", g_buf);
}

void test_the_discovery_topic_carries_only_characters_home_assistant_accepts(void) {
  // mqtt/discovery.py:64-67 matches [a-zA-Z0-9_-] in the node and object segments. A
  // colon-separated MAC there would make the message unroutable -- HA would never see it, and
  // nothing would report that.
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_TRUE(comfoair_ha_discovery_topic(&c, entity("supply_air_temp"), g_buf,
                                               sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("homeassistant/sensor/aabbccddeeff/supply_air_temp/config", g_buf);

  const char *node = strstr(g_buf, "aabbccddeeff");
  TEST_ASSERT_NOT_NULL(node);
  for (const char *p = g_buf; *p; p++)
    TEST_ASSERT_TRUE_MESSAGE((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                                 (*p >= '0' && *p <= '9') || *p == '_' || *p == '-' || *p == '/',
                             g_buf);
}

void test_a_topic_that_does_not_fit_reports_zero_rather_than_a_truncated_one(void) {
  // A truncated topic is a different topic. It publishes successfully, to nobody.
  const comfoair_ha_ctx_t c = ctx();
  char small[8];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_state_topic(&c, "supply_air_temp", small,
                                                      sizeof small));
}

// --- which component an entity becomes ---------------------------------------------------------

void test_every_entity_maps_to_a_component_home_assistant_supports(void) {
  // The sweep that stops an entity being added to pdo_table.py and silently never reaching Home
  // Assistant -- the MQTT twin of test_command's registry walk.
  const char *const supported[] = {"sensor", "binary_sensor", "select", "number", "switch", "fan"};
  size_t published = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    const char *component = comfoair_ha_component(&COMFOAIR_ENTITIES[i]);
    if (component == nullptr)
      continue;
    published++;
    bool ok = false;
    for (size_t s = 0; s < sizeof supported / sizeof supported[0]; s++)
      ok = ok || strcmp(component, supported[s]) == 0;
    TEST_ASSERT_TRUE_MESSAGE(ok, COMFOAIR_ENTITIES[i].key);
  }
  // 76: 73 base-unit table rows, the two unit-error rows, plus `data_freshness`, which
  // tools/generate_registry.py declares itself (test_ha_freshness pins what it is, tests/python
  // that it is not hand-written).
  TEST_ASSERT_EQUAL_size_t_MESSAGE(76, published,
                                   "an entity stopped being published to Home Assistant");
}

void test_the_kinds_map_the_way_home_assistant_needs_them_to(void) {
  TEST_ASSERT_EQUAL_STRING("sensor", comfoair_ha_component(entity("supply_air_temp")));
  TEST_ASSERT_EQUAL_STRING("select", comfoair_ha_component(entity("bypass_mode")));
  TEST_ASSERT_EQUAL_STRING("number", comfoair_ha_component(entity("boost_duration")));
  TEST_ASSERT_EQUAL_STRING("switch", comfoair_ha_component(entity("away_mode")));
  TEST_ASSERT_EQUAL_STRING("fan", comfoair_ha_component(entity("fan")));
  // An enum is a PLAIN sensor here -- see the header for why HA's enum handling is refused.
  TEST_ASSERT_EQUAL_STRING("sensor", comfoair_ha_component(entity("operating_mode")));
  TEST_ASSERT_EQUAL_STRING("sensor", comfoair_ha_component(entity("airflow_constraints")));
  TEST_ASSERT_EQUAL_STRING("binary_sensor", comfoair_ha_component(entity("bypass_override")));
  TEST_ASSERT_EQUAL_STRING("sensor", comfoair_ha_component(entity("sensor_ventilation_mode")));
}

// --- discovery payloads --------------------------------------------------------------------

void test_a_sensor_carries_what_it_needs_and_nothing_it_must_not(void) {
  const std::string doc = discovery("supply_air_temp");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_TRUE(has("\"stat_t\""));
  TEST_ASSERT_TRUE(has("\"uniq_id\":\"aabbccddeeff_supply_air_temp\""));
  TEST_ASSERT_TRUE(has("\"dev_cla\":\"temperature\""));
  TEST_ASSERT_TRUE(has("\"unit_of_meas\":\"\\u00b0C\""));
  TEST_ASSERT_TRUE(has("\"stat_cla\":\"measurement\""));
  // `avty` AND NOT `avty_t` since 2026-09-07: availability is a LIST of topics, because a reading
  // of the unit is unavailable when the CAN bus goes silent as well as when the broker link drops
  // (docs/interfaces/home-assistant-discovery.md, Availability). The two keys are vol.Exclusive in
  // one group, so this is a replacement and never an addition -- test/test_ha_availability is the
  // suite that holds the whole rule, including which entities carry the second topic and which must
  // not.
  TEST_ASSERT_TRUE(has("\"avty\":[{\"t\":"));
  TEST_ASSERT_FALSE(has("\"avty_t\""));
}

void test_absent_metadata_is_omitted_and_never_published_as_an_empty_string(void) {
  // generate_registry.py:58-59: "Absent metadata must be NULL, not "", so that a consumer can tell
  // 'no unit' from 'the empty unit'." A discovery payload carrying "unit_of_measurement": "" gives
  // HA an entity whose unit is the empty string, which is not the same as having none.
  const std::string doc = discovery("operating_mode");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_FALSE_MESSAGE(has("\"unit_of_meas\":\"\""), g_buf);
  TEST_ASSERT_FALSE_MESSAGE(has("\"dev_cla\":\"\""), g_buf);
  TEST_ASSERT_FALSE_MESSAGE(has("\"stat_cla\":\"\""), g_buf);
}

void test_an_enum_is_a_plain_sensor_with_no_options_and_no_enum_device_class(void) {
  // THE TRAP. mqtt/sensor.py:109-125 validates the three together: `options` REQUIRES
  // device_class enum and FORBIDS unit_of_measurement and state_class. And once options are
  // declared, HA rejects any state outside them -- while this firmware renders a value the
  // reference document does not cover as "Unknown (n)", which by construction is not in the list.
  const std::string doc = discovery("operating_mode");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_FALSE_MESSAGE(has("\"ops\""), "an enum must not publish options");
  TEST_ASSERT_FALSE_MESSAGE(has("\"dev_cla\":\"enum\""), "and must not claim the enum class");
}

void test_a_select_carries_its_options_and_its_command_topic(void) {
  // mqtt/select.py: options and command_topic are both REQUIRED. A select missing either is
  // rejected by the schema and the entity never appears.
  const std::string doc = discovery("bypass_mode");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_TRUE(has("\"cmd_t\":\"comfoair/aabbccddeeff/bypass_mode/command\""));
  TEST_ASSERT_TRUE(has("\"ops\":[\"Auto\",\"Open\",\"Closed\"]"));
  TEST_ASSERT_TRUE(has("\"stat_t\""));
}

void test_the_ventilation_mode_select_is_published_but_disabled_by_default(void) {
  // It is the same control as the fan: the select is the registry entity the six preset labels
  // belong to, the one the fan's preset topic writes through. Publishing both visible would put
  // two entities in Home Assistant that move each other, which reads as a defect. The owner chose
  // on 2026-09-04 to keep it available for automations and out of the way by default.
  const std::string doc = discovery("ventilation_mode");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_TRUE_MESSAGE(has("\"en\":false"), g_buf);
  // And nothing else is hidden.
  discovery("bypass_mode");
  TEST_ASSERT_FALSE(has("\"en\":false"));
}

void test_a_number_carries_its_range_and_its_unit(void) {
  // The unit is the half of the minutes/seconds trap discovery can fix: the topic tree has no
  // units anywhere, so a consumer reads away_duration -> 60 (minutes) and writes a duration of 60
  // (seconds), getting one minute instead of one hour.
  const std::string doc = discovery("boost_duration");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_TRUE(has("\"cmd_t\""));
  TEST_ASSERT_TRUE_MESSAGE(has("\"unit_of_meas\":\"min\""), g_buf);
  TEST_ASSERT_TRUE(has("\"min\":1"));
  TEST_ASSERT_TRUE(has("\"max\":1440"));

  // away_duration's minimum really is 0 and must survive. generate_registry.py:248-251 records
  // that a literal zero once made REST answer "min 0 max 0" for every entity without a range, so the test
  // there is "is None", never a comparison against zero -- and the same applies here.
  const std::string away = discovery("away_duration");
  TEST_ASSERT_TRUE(away.size() > 0);
  TEST_ASSERT_TRUE_MESSAGE(has("\"min\":0"), g_buf);
  TEST_ASSERT_TRUE(has("\"max\":43200"));
}

void test_the_fan_declares_presets_and_speed_as_home_assistant_requires(void) {
  // mqtt/fan.py:140-147 -- preset_modes and preset_mode_command_topic are an INCLUSIVE pair:
  // the list alone fails validation, and the topic alone gives a fan with zero presets and no
  // error at all. :265-270 -- it is the COMMAND topic that switches each feature on, not the
  // state topic.
  const std::string doc = discovery("fan");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_TRUE(has("\"cmd_t\":\"comfoair/aabbccddeeff/fan/command\""));
  TEST_ASSERT_TRUE_MESSAGE(has("\"pr_mode_cmd_t\""), "presets need their own command topic");
  TEST_ASSERT_TRUE_MESSAGE(has("\"pr_modes\":[\"Auto\",\"Away\",\"Low\",\"Medium\",\"High\",\"Boost\"]"),
                           g_buf);
  TEST_ASSERT_TRUE_MESSAGE(has("\"pct_cmd_t\""), "speed needs its own command topic");
  // 1..3, so HA's percentage maps onto three steps rather than a hundred.
  TEST_ASSERT_TRUE(has("\"spd_rng_min\":1"));
  TEST_ASSERT_TRUE(has("\"spd_rng_max\":3"));
  // "None" must not be one of the presets -- valid_preset_mode_configuration rejects that, and it
  // is the payload that CLEARS a preset.
  TEST_ASSERT_FALSE(has("\"None\""));
}

void test_every_payload_identifies_the_device_by_mac_and_never_by_name(void) {
  // Identity is the MAC (docs/architecture/security.md): renaming must not re-key anything, or
  // Home Assistant ends up with two devices, one dead and undeletable. `connections` is published
  // as well (owner's choice, 2026-09-04), keeping this device on the card Home Assistant already
  // knows for the unit's MAC.
  const std::string doc = discovery("supply_air_temp");
  TEST_ASSERT_TRUE(doc.size() > 0);
  TEST_ASSERT_TRUE(has("\"ids\":[\"aabbccddeeff\"]"));
  TEST_ASSERT_TRUE_MESSAGE(has("\"cns\":[[\"mac\",\"aa:bb:cc:dd:ee:ff\"]]"), g_buf);
  TEST_ASSERT_TRUE(has("\"name\":\"ComfoAir Q eeff\""));
  TEST_ASSERT_TRUE(has("\"sw\":\"0.1.0\""));
  TEST_ASSERT_TRUE(has("\"cu\":\"http://192.168.1.42/\""));
  // The unique id is the MAC plus the key, never the display name.
  TEST_ASSERT_TRUE(has("\"uniq_id\":\"aabbccddeeff_supply_air_temp\""));
}

void test_an_empty_url_or_mac_is_omitted_rather_than_published_empty(void) {
  // cv.configuration_url REJECTS a value with no scheme, so an empty one has to be absent rather
  // than present-and-empty -- a rejected device block takes the whole entity with it.
  comfoair_ha_ctx_t c = ctx();
  c.url = "";
  c.mac = "";
  TEST_ASSERT_TRUE(comfoair_ha_discovery_payload(&c, entity("supply_air_temp"), g_buf,
                                                 sizeof g_buf) > 0);
  TEST_ASSERT_FALSE_MESSAGE(has("\"cu\""), g_buf);
  TEST_ASSERT_FALSE_MESSAGE(has("\"cns\""), g_buf);
  // Identifiers alone still satisfy HA's "at least one identifying value" rule.
  TEST_ASSERT_TRUE(has("\"ids\""));
}

void test_a_device_name_with_a_quote_in_it_cannot_break_the_payload(void) {
  comfoair_ha_ctx_t c = ctx();
  c.device_name = "Anna\"s loft";
  TEST_ASSERT_TRUE(comfoair_ha_discovery_payload(&c, entity("supply_air_temp"), g_buf,
                                                 sizeof g_buf) > 0);
  TEST_ASSERT_TRUE(has("Anna\\\"s loft"));
}

void test_a_payload_that_does_not_fit_reports_zero(void) {
  const comfoair_ha_ctx_t c = ctx();
  char small[64];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_discovery_payload(&c, entity("fan"), small,
                                                            sizeof small));
}

void test_every_entity_produces_a_payload_that_fits_the_publish_buffer(void) {
  // The sweep. One retained document per entity goes out at connect; one that does not fit is an
  // entity that silently never appears, and the biggest is not the one anybody would guess.
  const comfoair_ha_ctx_t c = ctx();
  size_t largest = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    if (comfoair_ha_component(&COMFOAIR_ENTITIES[i]) == nullptr)
      continue;
    const size_t n = comfoair_ha_discovery_payload(&c, &COMFOAIR_ENTITIES[i], g_buf, sizeof g_buf);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, COMFOAIR_ENTITIES[i].key);
    if (n > largest)
      largest = n;
  }
  // Pinned so that a payload growing past what the client is configured to send is a test failure
  // here rather than a missing entity on somebody's dashboard.
  TEST_ASSERT_TRUE_MESSAGE(largest < 1024, "a discovery payload outgrew the publish buffer");
}

// --- state payloads ---------------------------------------------------------------------------

void test_a_state_payload_is_a_bare_scalar(void) {
  comfoair_value_t v = {};
  bool skip = true;

  v.type = COMFOAIR_VALUE_NUMBER;
  v.number = 21.5f;
  TEST_ASSERT_TRUE(comfoair_ha_state_payload(entity("supply_air_temp"), &v, &skip, g_buf,
                                             sizeof g_buf) > 0);
  TEST_ASSERT_FALSE(skip);
  TEST_ASSERT_EQUAL_STRING("21.5", g_buf);

  v.type = COMFOAIR_VALUE_BOOL;
  v.boolean = true;
  TEST_ASSERT_TRUE(comfoair_ha_state_payload(entity("away_mode"), &v, &skip, g_buf,
                                             sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("ON", g_buf);

  v.type = COMFOAIR_VALUE_TEXT;
  v.text = "Auto";
  TEST_ASSERT_TRUE(comfoair_ha_state_payload(entity("bypass_mode"), &v, &skip, g_buf,
                                             sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("Auto", g_buf);
}

void test_the_fan_publishes_three_payloads_and_off_is_the_away_level(void) {
  comfoair_fan_state_t fan = {};
  fan.known = true;
  fan.on = true;
  fan.speed = 3;
  fan.preset = "Boost";

  TEST_ASSERT_TRUE(comfoair_ha_fan_state_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("ON", g_buf);
  TEST_ASSERT_TRUE(comfoair_ha_fan_speed_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("3", g_buf);
  TEST_ASSERT_TRUE(comfoair_ha_fan_preset_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("Boost", g_buf);

  // "Off" on this unit is the Away level; an HRV never stops ventilating entirely.
  fan.on = false;
  TEST_ASSERT_TRUE(comfoair_ha_fan_state_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("OFF", g_buf);

  // A null preset is a real state the unit reports, and HA clears a preset with the literal
  // string "None" (mqtt/fan/const.py, DEFAULT_PAYLOAD_RESET).
  fan.preset = nullptr;
  TEST_ASSERT_TRUE(comfoair_ha_fan_preset_payload(&fan, g_buf, sizeof g_buf) > 0);
  TEST_ASSERT_EQUAL_STRING("None", g_buf);
}

void test_a_null_argument_is_answered_rather_than_dereferenced(void) {
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_state_topic(nullptr, "x", g_buf, sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_state_topic(&c, nullptr, g_buf, sizeof g_buf));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_ha_discovery_payload(&c, nullptr, g_buf, sizeof g_buf));
  TEST_ASSERT_NULL(comfoair_ha_component(nullptr));
}


// --- what an arriving topic means -------------------------------------------------------------

namespace {
comfoair_ha_target_t parse(const char *topic, char *name, size_t cap) {
  const comfoair_ha_ctx_t c = ctx();
  return comfoair_ha_parse_topic(&c, topic, strlen(topic), name, cap);
}
}  // namespace

void test_an_arriving_topic_is_split_into_what_it_addresses(void) {
  char name[64] = {0};
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_ENTITY,
                    parse("comfoair/aabbccddeeff/bypass_mode/command", name, sizeof name));
  TEST_ASSERT_EQUAL_STRING("bypass_mode", name);

  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_OP,
                    parse("comfoair/aabbccddeeff/ops/set_away/set", name, sizeof name));
  TEST_ASSERT_EQUAL_STRING("set_away", name);

  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_FAN_ON,
                    parse("comfoair/aabbccddeeff/fan/command", name, sizeof name));
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_FAN_SPEED,
                    parse("comfoair/aabbccddeeff/fan/speed/command", name, sizeof name));
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_FAN_PRESET,
                    parse("comfoair/aabbccddeeff/fan/preset/command", name, sizeof name));
}

void test_a_topic_outside_this_devices_tree_is_refused(void) {
  // Including one that merely STARTS with the prefix. Two units on one broker whose prefixes
  // share a stem must not hear each other's commands.
  char name[64] = {0};
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE, parse("comfoair/other/bypass_mode/command", name, sizeof name));
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE, parse("comfoair/aabbccddeeffX/x/command", name, sizeof name));
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE, parse("comfoair/aabbccddeeff/bypass_mode/state", name, sizeof name));
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE, parse("comfoair/aabbccddeeff/status", name, sizeof name));
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE, parse("", name, sizeof name));
  // A key is ONE segment: <prefix>/a/b/command must not be read as the key "a/b".
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE, parse("comfoair/aabbccddeeff/a/b/command", name, sizeof name));
}

void test_the_topic_is_read_by_length_because_esp_mqtt_does_not_terminate_it(void) {
  // esp-mqtt hands over a pointer and a length into its receive buffer. A parser that assumed a
  // terminator would read whatever followed -- here, a second topic sitting right behind it.
  const char packed[] = "comfoair/aabbccddeeff/bypass_mode/commandcomfoair/aabbccddeeff/fan/command";
  char name[64] = {0};
  const comfoair_ha_ctx_t c = ctx();
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_ENTITY,
                    comfoair_ha_parse_topic(&c, packed, strlen("comfoair/aabbccddeeff/bypass_mode/command"),
                                            name, sizeof name));
  TEST_ASSERT_EQUAL_STRING("bypass_mode", name);
}

void test_a_key_longer_than_the_buffer_is_refused_not_truncated(void) {
  char name[8] = {0};
  TEST_ASSERT_EQUAL(COMFOAIR_HA_TARGET_NONE,
                    parse("comfoair/aabbccddeeff/a_very_long_entity_key/command", name, sizeof name));
}

namespace {
// True when `topic` is what the single-level wildcard in `pattern` matches: head and tail equal,
// and ONE segment where the `+` stands. Only the single `+` this component ever builds is handled.
bool matches(const std::string &pattern, const std::string &topic) {
  const size_t star = pattern.find('+');
  const std::string head = pattern.substr(0, star), tail = pattern.substr(star + 1);
  if (star == std::string::npos || topic.size() < head.size() + tail.size() ||
      topic.compare(0, head.size(), head) != 0 ||
      topic.compare(topic.size() - tail.size(), tail.size(), tail) != 0)
    return false;
  return topic.substr(head.size(), topic.size() - head.size() - tail.size()).find('/') ==
         std::string::npos;
}
}  // namespace

void test_every_command_leaf_the_builder_names_is_understood_by_the_parser(void) {
  // THE CIRCLE, and it is the only thing tying the two halves together. Every WRITER of a fan
  // command topic goes through a builder -- discovery, the subscription, the cleanup -- while
  // comfoair_ha_parse_topic() matches its own hand-written strings, a fifth copy of a list this
  // component otherwise keeps in one enumeration. Nothing connected them.
  //
  // NOT A HYPOTHESIS: on 2026-09-07 a one-word mutation of the builder ("speed/command" ->
  // "pct/command") was found sitting in this tree with every host case green. On the device it is
  // silent and total -- HA is told `pct_cmd_t`, `fan/+/command` still matches the arriving
  // message, the parser answers TARGET_NONE, and fan speed from Home Assistant stops working.
  //
  // So the topics here are BUILT, never typed. The leaf-to-target map is the one thing stated
  // independently -- that is what makes this an assertion rather than a tautology.
  const comfoair_ha_ctx_t c = ctx();
  const struct {
    comfoair_ha_fan_leaf_t leaf;
    comfoair_ha_target_t   target;
  } round_trip[] = {
      {COMFOAIR_HA_FAN_SPEED_COMMAND, COMFOAIR_HA_TARGET_FAN_SPEED},
      {COMFOAIR_HA_FAN_PRESET_COMMAND, COMFOAIR_HA_TARGET_FAN_PRESET},
  };

  char pattern[256] = {0};
  TEST_ASSERT_TRUE(
      comfoair_ha_fan_topic(&c, COMFOAIR_HA_FAN_ANY_COMMAND, pattern, sizeof pattern) > 0);

  char name[64] = {0};
  for (const auto &row : round_trip) {
    char built[256] = {0};
    TEST_ASSERT_TRUE_MESSAGE(comfoair_ha_fan_topic(&c, row.leaf, built, sizeof built) > 0, built);
    TEST_ASSERT_EQUAL_MESSAGE(row.target, parse(built, name, sizeof name), built);
    // And it has to ARRIVE, or the round trip above is a conversation nobody subscribed to:
    // subscribe_all() asks for this one pattern and no other (comfoair_mqtt_publish.c).
    TEST_ASSERT_TRUE_MESSAGE(matches(pattern, built), built);
  }

  // The fan's on/off command is not a fan leaf: it is `<prefix>/fan/command`, built from the
  // registry key like every other entity's (comfoair_ha.h, above COMFOAIR_HA_FAN_SPEED_COMMAND).
  // Same circle, other builder -- and it arrives through the entity subscription, not the pattern.
  char on[256] = {0};
  TEST_ASSERT_TRUE(comfoair_ha_command_topic(&c, "fan", on, sizeof on) > 0);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_HA_TARGET_FAN_ON, parse(on, name, sizeof name), on);
}


void test_the_worst_case_payload_still_fits_the_publish_buffer(void) {
  // Counter-review found this one by measuring rather than by reading: the fan's document grows
  // with the topic prefix AND the device name, and comfoair_json_escape expands every non-ASCII
  // character to six bytes. At the prefix's 64-character maximum with a 32-character non-ASCII
  // name the fan payload reaches about 1040 bytes -- so a 1024-byte publish buffer would have made
  // the fan silently never appear, with one log line to say why.
  comfoair_ha_ctx_t c = ctx();
  static char prefix[65];
  memset(prefix, 'p', sizeof prefix - 1);
  prefix[sizeof prefix - 1] = '\0';
  c.topic_prefix = prefix;
  // COMFOAIR_CONFIG_NAME_MAX is 32 BYTES, so the worst case is sixteen two-byte characters --
  // each of which comfoair_json_escape expands to a six-byte \uXXXX. Built as one string rather
  // than character by character: an earlier version of this test terminated after every character
  // and was measuring a two-byte name while claiming to measure the maximum.
  static char name[COMFOAIR_CONFIG_NAME_MAX + 1];
  for (int i = 0; i < 16; i++)
    memcpy(name + i * 2, "\xd0\xaf", 2);  // CYRILLIC CAPITAL LETTER YA
  name[32] = '\0';
  TEST_ASSERT_EQUAL_size_t(32, strlen(name));
  c.device_name = name;

  size_t largest = 0;
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    if (comfoair_ha_component(&COMFOAIR_ENTITIES[i]) == nullptr)
      continue;
    const size_t n = comfoair_ha_discovery_payload(&c, &COMFOAIR_ENTITIES[i], g_buf, sizeof g_buf);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, COMFOAIR_ENTITIES[i].key);
    if (n > largest)
      largest = n;
  }
  // The publisher's buffer is 1536 (comfoair_mqtt_internal.h, BUFFER_SIZE). Pinned here so that a
  // payload outgrowing it is a test failure rather than an entity missing from somebody's
  // dashboard.
  //
  // THE NUMBER MOVED ON 2026-09-07 AND THIS IS WHERE THAT WAS DECIDED. Availability became a LIST
  // of two topics, which costs `,"avty":[{"t":"<status>"},{"t":"<bus>"}], "avty_mode":"all"` where
  // `,"avty_t":"<status>"` used to be: +101 bytes at the prefix's maximum, measured, not estimated.
  // The fan's document goes from 1040 -- the figure comfoair_mqtt_internal.h's own comment names --
  // to 1141, and 395 bytes of the buffer are left. The rule: if this test disagrees, BUFFER_SIZE
  // moves and the test does not -- the buffer is sized to the documents, never the documents
  // trimmed to the buffer.
  TEST_ASSERT_TRUE_MESSAGE(largest < 1536, "the worst-case payload outgrew the publish buffer");
  // The lower bound is not decoration: it is what notices a change that stops exercising the
  // 64-character prefix, the 32-byte non-ASCII name or the availability list at all -- each of
  // which would relax the guarantee above while leaving it green.
  TEST_ASSERT_TRUE_MESSAGE(largest > 1100, "the worst case is not being exercised any more");
  // Which document is the largest is read out of the sweep above, never assumed: the fan's four
  // topics and six preset names make it, and a plain registry row added to the table does not.
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_topic_tree_is_flat_and_keyed_by_the_entity_key);
  RUN_TEST(test_the_operation_topic_uses_the_same_name_as_the_rest_route);
  RUN_TEST(test_the_discovery_topic_carries_only_characters_home_assistant_accepts);
  RUN_TEST(test_a_topic_that_does_not_fit_reports_zero_rather_than_a_truncated_one);

  RUN_TEST(test_every_entity_maps_to_a_component_home_assistant_supports);
  RUN_TEST(test_the_kinds_map_the_way_home_assistant_needs_them_to);

  RUN_TEST(test_a_sensor_carries_what_it_needs_and_nothing_it_must_not);
  RUN_TEST(test_absent_metadata_is_omitted_and_never_published_as_an_empty_string);
  RUN_TEST(test_an_enum_is_a_plain_sensor_with_no_options_and_no_enum_device_class);
  RUN_TEST(test_a_select_carries_its_options_and_its_command_topic);
  RUN_TEST(test_the_ventilation_mode_select_is_published_but_disabled_by_default);
  RUN_TEST(test_a_number_carries_its_range_and_its_unit);
  RUN_TEST(test_the_fan_declares_presets_and_speed_as_home_assistant_requires);
  RUN_TEST(test_every_payload_identifies_the_device_by_mac_and_never_by_name);
  RUN_TEST(test_an_empty_url_or_mac_is_omitted_rather_than_published_empty);
  RUN_TEST(test_a_device_name_with_a_quote_in_it_cannot_break_the_payload);
  RUN_TEST(test_a_payload_that_does_not_fit_reports_zero);
  RUN_TEST(test_every_entity_produces_a_payload_that_fits_the_publish_buffer);

  RUN_TEST(test_a_state_payload_is_a_bare_scalar);
  RUN_TEST(test_the_fan_publishes_three_payloads_and_off_is_the_away_level);
  RUN_TEST(test_a_null_argument_is_answered_rather_than_dereferenced);
  RUN_TEST(test_an_arriving_topic_is_split_into_what_it_addresses);
  RUN_TEST(test_a_topic_outside_this_devices_tree_is_refused);
  RUN_TEST(test_the_topic_is_read_by_length_because_esp_mqtt_does_not_terminate_it);
  RUN_TEST(test_a_key_longer_than_the_buffer_is_refused_not_truncated);
  RUN_TEST(test_every_command_leaf_the_builder_names_is_understood_by_the_parser);
  RUN_TEST(test_the_worst_case_payload_still_fits_the_publish_buffer);
  return UNITY_END();
}
