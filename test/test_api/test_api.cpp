// The JSON the REST projection serves.
//
// This is the API contract, so it is tested here rather than through HTTP: the shape must
// hold whether it arrives over REST, over the WebSocket, or in an MQTT payload. AGENTS.md
// criterion 3 requires REST and MQTT to cover exactly the same ground, and they can only do that
// if they render from one place.
#include <unity.h>

#include <cstring>
#include <string>

#include "comfoair_api.h"
#include "comfoair_state.h"
#include "registry_generated.h"

static std::string render_state() {
  char buf[8192];
  const size_t n = comfoair_api_render_state(buf, sizeof buf);
  return std::string(buf, n);
}

static bool contains(const std::string &haystack, const char *needle) {
  return haystack.find(needle) != std::string::npos;
}

void setUp(void) { comfoair_state_reset(); }
void tearDown(void) {}

// --- state ---------------------------------------------------------------------------------

void test_an_entity_with_no_reading_is_null_not_absent(void) {
  // A missing key and a null value are different answers: absent says "this firmware has no
  // such entity", null says "it has one and has not heard from the unit yet". A consumer
  // that cannot tell them apart cannot report a dead bus.
  const std::string json = render_state();
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"supply_air_temp_post_heater\":null"),
                           "an unreported entity was omitted instead of being null");
}

void test_a_numeric_reading_carries_its_divider(void) {
  const uint8_t frame[] = {0xd7, 0x00};  // 215 -> 21.5 with divider 10
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  TEST_ASSERT_TRUE(contains(render_state(), "\"supply_air_temp_post_heater\":21.5"));
}

void test_a_boolean_reading_is_a_json_boolean(void) {
  const uint8_t on[] = {1};
  comfoair_state_apply_pdo(56, on, sizeof on);
  TEST_ASSERT_TRUE(contains(render_state(), "\"manual_mode\":true"));
}

void test_a_text_reading_is_quoted(void) {
  const uint8_t frame[] = {1};
  comfoair_state_apply_pdo(16, frame, sizeof frame);
  TEST_ASSERT_TRUE(contains(render_state(), "\"device_state\":\"Normal\""));
}

void test_the_q350_labels_are_what_the_unit_means(void) {
  // PDO 49 = 1 with no countdown is manual; 18 = 1 is the normal application mode; 16 = 7 is
  // the fan's Away preset, not away mode (pdo_table.py's comments on each row).
  const uint8_t one[] = {1};
  const uint8_t seven[] = {7};
  comfoair_state_apply_pdo(49, one, sizeof one);
  comfoair_state_apply_pdo(18, one, sizeof one);
  comfoair_state_apply_pdo(16, seven, sizeof seven);
  const std::string json = render_state();
  TEST_ASSERT_TRUE(contains(json, "\"operating_mode\":\"Manual\""));
  TEST_ASSERT_TRUE(contains(json, "\"changing_filters\":\"Normal\""));
  TEST_ASSERT_TRUE(contains(json, "\"device_state\":\"Away (preset)\""));
}

void test_pdo_225_is_published_as_its_raw_number(void) {
  const uint8_t two[] = {2};
  comfoair_state_apply_pdo(225, two, sizeof two);
  TEST_ASSERT_TRUE(contains(render_state(), "\"sensor_ventilation_mode\":2"));
}

void test_the_fan_is_an_object_because_it_is_not_a_scalar(void) {
  const uint8_t level[] = {2};
  const uint8_t mode[] = {0xff};
  comfoair_state_apply_pdo(65, level, sizeof level);
  comfoair_state_apply_pdo(49, mode, sizeof mode);
  const std::string json = render_state();
  TEST_ASSERT_TRUE(contains(json, "\"fan\":{"));
  TEST_ASSERT_TRUE(contains(json, "\"on\":true"));
  TEST_ASSERT_TRUE(contains(json, "\"speed\":2"));
  TEST_ASSERT_TRUE(contains(json, "\"preset\":\"Auto\""));
}

void test_every_entity_appears_in_the_state_document(void) {
  // AGENTS.md criterion 3: REST must cover exactly what MQTT does. Both render from the registry, so the
  // guard is that the registry is rendered whole.
  const std::string json = render_state();
  for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
    std::string key = "\"";
    key += COMFOAIR_ENTITIES[i].key;
    key += "\":";
    TEST_ASSERT_TRUE_MESSAGE(contains(json, key.c_str()), COMFOAIR_ENTITIES[i].key);
  }
}

void test_a_small_buffer_is_never_overflowed(void) {
  // Nothing is written past the end, and -- since the counter-review -- a document that
  // does not fit reports 0 rather than a plausible short length.
  char small[16];
  const size_t n = comfoair_api_render_state(small, sizeof small);
  TEST_ASSERT_EQUAL_size_t(0, n);
  TEST_ASSERT_EQUAL_CHAR_MESSAGE('\0', small[sizeof small - 1], "no terminator was written");
}

// --- entities ------------------------------------------------------------------------------

void test_the_entity_list_describes_what_a_client_needs(void) {
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE(contains(json, "\"key\":\"away_mode\""));
  TEST_ASSERT_TRUE(contains(json, "\"writable\":true"));
  TEST_ASSERT_TRUE(contains(json, "\"unit\":\"\\u00b0C\""));
}

void test_a_secret_never_appears_in_any_rendering(void) {
  // Secrets are write-only in every projection (docs/architecture/security.md, Write-only
  // secrets), and this is the guard that says so for REST.
  const std::string state = render_state();
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string entities(buf, n);
  for (const char *forbidden : {"password", "psk", "secret"}) {
    TEST_ASSERT_FALSE_MESSAGE(contains(state, forbidden), forbidden);
    TEST_ASSERT_FALSE_MESSAGE(contains(entities, forbidden), forbidden);
  }
}

void test_the_broker_diagnostics_are_not_entities(void) {
  // ONE ENTITY LIST, EVER (AGENTS.md), guarded at the place the temptation lands.
  // GET /api/status grew eight members describing the broker connection, and "let us add a broker
  // status sensor to Home Assistant" is the next thing somebody wants -- which would be a second
  // list, hand-written, beside the one pdo_table.py generates. The broker's diagnosis belongs to
  // the status document and to the settings page; it is not a property of the ventilation unit.
  const std::string state = render_state();
  char              buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t      n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string entities(buf, n);
  for (const char *diagnostic :
       {"mqtt_connected", "\"link\"", "retry_in_ms", "subscriptions", "applied_epoch",
        "last_connack", "last_transport_errno", "disconnects"}) {
    TEST_ASSERT_FALSE_MESSAGE(contains(state, diagnostic), diagnostic);
    TEST_ASSERT_FALSE_MESSAGE(contains(entities, diagnostic), diagnostic);
  }
}

// --- escaping ------------------------------------------------------------------------------

void test_quotes_and_backslashes_are_escaped(void) {
  char out[64];
  const size_t n = comfoair_api_escape_json("a\"b\\c", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c", out);
  TEST_ASSERT_EQUAL_size_t(7, n);
}

void test_control_characters_become_unicode_escapes(void) {
  char out[64];
  comfoair_api_escape_json("a\nb", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("a\\nb", out);
  comfoair_api_escape_json("a\x01" "b", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("a\\u0001b", out);
}

void test_utf8_above_ascii_is_escaped_rather_than_passed_through(void) {
  // The degree sign arrives from pdo_table as UTF-8. Escaping it keeps the output valid
  // regardless of what a consumer assumes about encoding.
  char out[64];
  comfoair_api_escape_json("\xc2\xb0" "C", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("\\u00b0C", out);
}


// --- escaping must never produce invalid JSON -----------------------------------------------
//
// All three found by counter-review. The contract this file states is that the output is
// valid whatever the consumer assumes about encoding; each of these broke it in a way that
// still looked like JSON at a glance.

void test_truncation_never_cuts_an_escape_sequence_in_half(void) {
  // The degree sign escapes to six characters. A buffer with room for only part of it must
  // drop the whole sequence, not emit "\u0" and leave the document unparseable.
  char out[4];
  comfoair_api_escape_json("\xc2\xb0", out, sizeof out);
  const std::string s(out);
  TEST_ASSERT_FALSE_MESSAGE(s.rfind("\\u", 0) == 0 && s.size() < 6,
                            "a \\u escape was cut in half");
}

void test_a_codepoint_outside_unicode_is_dropped_not_encoded(void) {
  // f7 bf bf bf decodes to 0x1fffff, past the last legal codepoint. Encoding it produced a
  // surrogate pair whose high half came from the low range -- invalid JSON.
  char out[64];
  comfoair_api_escape_json("\xf7\xbf\xbf\xbf", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", out, "an out-of-range codepoint was encoded");
}

void test_a_lone_surrogate_is_dropped(void) {
  // ed a0 80 is U+D800, a high surrogate with no pair. It is not a legal codepoint and a
  // bare \ud800 in the output is not legal JSON either.
  char out[64];
  comfoair_api_escape_json("\xed\xa0\x80", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", out, "a lone surrogate was emitted");
}

void test_a_valid_astral_codepoint_becomes_a_surrogate_pair(void) {
  // U+1F600, four bytes. Nothing in pdo_table reaches here, but a device name from NVS can.
  char out[64];
  comfoair_api_escape_json("\xf0\x9f\x98\x80", out, sizeof out);
  TEST_ASSERT_EQUAL_STRING("\\ud83d\\ude00", out);
}

// --- the entity list must match the shape the generated types declare -----------------------

void test_the_entity_list_carries_every_field_entities_ts_declares(void) {
  // web/src/api/entities.ts declares EntityMeta with kind, pdo, min and max. The C renderer
  // omitted all four, so a client typed against those generated types would read undefined
  // for the range of a number and could not tell a select from an enum.
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"kind\":"), "kind is missing");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"pdo\":"), "pdo is missing");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"min\":"), "min is missing");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"max\":"), "max is missing");
  // away_duration is the one whose range matters and whose min is legitimately 0.
  TEST_ASSERT_TRUE(contains(json, "\"key\":\"away_duration\""));
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"max\":43200"), "the range never reaches a client");
}

void test_an_entity_with_no_pdo_reports_null_rather_than_minus_one(void) {
  // -1 is the C sentinel; leaking it would make a client think there is a PDO numbered -1.
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"pdo\":-1"), "the C sentinel leaked into JSON");
}

void test_the_entity_list_carries_state_class_for_ha_statistics(void) {
  // comfoair_entity_t in registry_generated.h already carries state_class, generated from
  // tools/pdo_table.py, and comfoair_ha_discovery_payload() already publishes it as stat_cla
  // over MQTT discovery. This renderer skipped it, so a client that only speaks REST could not
  // offer Home Assistant long-term statistics (history, min/max/mean) for anything -- one
  // firmware, one table, two consumers seeing different metadata for the same entity.
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"key\":\"exhaust_fan_duty\""), "fixture entity missing");
  // exhaust_fan_duty carries state_class "measurement" (its row in tools/pdo_table.py).
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"stateClass\":\"measurement\""),
                           "a known state class was not emitted");
}

void test_an_entity_with_no_state_class_reports_null_not_empty_string(void) {
  // next_fan_change_in_raw carries state_class NULL (its row in tools/pdo_table.py). Absent
  // metadata must render as JSON null -- the same rule put_string_or_null already enforces
  // for deviceClass and entityCategory. An empty string is a value; null says the table
  // gives none.
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"key\":\"next_fan_change_in_raw\""),
                           "fixture entity missing");
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"stateClass\":null"),
                           "an absent state class rendered as something other than null");
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"stateClass\":\"\""),
                            "an absent state class rendered as an empty string");
}


// The one entity object for `key` in /api/entities: from its "key" to the next one (or the end).
static std::string entity_object(const std::string &json, const char *key) {
  const std::string needle = std::string("\"key\":\"") + key + "\"";
  const size_t from = json.find(needle);
  if (from == std::string::npos)
    return std::string();
  const size_t to = json.find("\"key\":", from + needle.size());
  return json.substr(from, to == std::string::npos ? std::string::npos : to - from);
}

void test_the_entity_list_carries_enabled_by_default(void) {
  // Mirrors discovery's `en`: the HA integration builds its entities from this document, so a
  // row the table disables must say so here too, or it would arrive enabled there.
  static char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  const std::string select = entity_object(json, "ventilation_mode");
  const std::string fan = entity_object(json, "fan");
  TEST_ASSERT_FALSE_MESSAGE(select.empty(), "fixture entity ventilation_mode missing");
  TEST_ASSERT_FALSE_MESSAGE(fan.empty(), "fixture entity fan missing");
  TEST_ASSERT_TRUE_MESSAGE(contains(select, "\"enabledByDefault\":false"),
                           "the disabled select did not say so");
  TEST_ASSERT_TRUE_MESSAGE(contains(fan, "\"enabledByDefault\":true"),
                           "an enabled entity did not say so");
}


// --- one entity ------------------------------------------------------------------------------

void test_a_single_entity_renders_with_its_metadata(void) {
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[1024];
  const size_t n = comfoair_api_render_value("supply_air_temp_post_heater", buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE(contains(json, "\"key\":\"supply_air_temp_post_heater\""));
  TEST_ASSERT_TRUE(contains(json, "\"value\":21.5"));
  TEST_ASSERT_TRUE(contains(json, "\"unit\":\"\\u00b0C\""));
}

void test_a_single_entity_with_no_reading_is_null(void) {
  char buf[1024];
  const size_t n = comfoair_api_render_value("supply_air_temp_post_heater", buf, sizeof buf);
  TEST_ASSERT_TRUE(contains(std::string(buf, n), "\"value\":null"));
}

void test_rendering_an_unknown_entity_writes_nothing(void) {
  char buf[1024];
  const size_t n = comfoair_api_render_value("no_such_entity", buf, sizeof buf);
  TEST_ASSERT_EQUAL_size_t(0, n);
}


// --- gaps found by the phase 4 counter-review ------------------------------------------------

void test_a_range_the_table_does_not_give_is_null_not_zero(void) {
  // The generator wrote 0.0 into the C header for "no range" while writing null into
  // entities.ts for the same entity, so REST said min 0 max 0 for every entity
  // without a range and the TypeScript said null. Two projections of one registry
  // disagreeing is the exact thing generating both from one table exists to prevent.
  char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t n = comfoair_api_render_entities(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"min\":null"), "no entity reported a null range");
  // away_duration legitimately has min 0, so a null must not swallow a real zero.
  TEST_ASSERT_TRUE(contains(json, "\"key\":\"away_duration\""));
  TEST_ASSERT_TRUE_MESSAGE(contains(json, "\"max\":43200"), "a real range went missing");
}

void test_the_airflow_constraint_bitset_is_decoded(void) {
  // PDO 230 answers the recurring "why is it not reaching the setpoint" question, and the
  // decoder for it was ported and tested in phase 2 -- but nothing ever called it, so the
  // entity read null for ever. Bit 45 is the validity gate; bit 9 is Frost Protection.
  const uint8_t frame[8] = {0, 0x02, 0, 0, 0, 0x20, 0, 0};  // bits 9 and 45
  TEST_ASSERT_TRUE(comfoair_state_apply_pdo(230, frame, sizeof frame));
  comfoair_value_t v{};
  TEST_ASSERT_TRUE(comfoair_state_get("airflow_constraints", &v));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_VALUE_TEXT, v.type, "PDO 230 was decoded to nothing");
  TEST_ASSERT_EQUAL_STRING("Below: Frost Protection", v.text);
}

void test_a_constraint_bitset_without_bit_45_says_no_restrictions(void) {
  // Bit 45 clear is no constraint list at all; the bits beside it are not read.
  const uint8_t frame[8] = {0, 0x02, 0, 0, 0, 0, 0, 0};
  comfoair_state_apply_pdo(230, frame, sizeof frame);
  comfoair_value_t v{};
  comfoair_state_get("airflow_constraints", &v);
  TEST_ASSERT_EQUAL_STRING("No restrictions", v.text);
}

void test_a_document_that_does_not_fit_reports_failure_rather_than_a_short_answer(void) {
  // The entity list is 23 bytes short of its buffer. One more entity and it would have
  // been served truncated with 200 OK -- valid-looking JSON that stops mid-object, with
  // nothing in the log. Truncation must be detectable by the caller.
  char small[64];
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, comfoair_api_render_entities(small, sizeof small),
                                   "a truncated document was reported as a length");
  TEST_ASSERT_EQUAL_size_t(0, comfoair_api_render_state(small, sizeof small));
}

void test_a_document_that_fits_still_reports_its_length(void) {
  char big[COMFOAIR_API_DOC_BUFFER_SIZE];
  TEST_ASSERT_GREATER_THAN_size_t(0, comfoair_api_render_entities(big, sizeof big));
  TEST_ASSERT_GREATER_THAN_size_t(0, comfoair_api_render_state(big, sizeof big));
}


// --- deltas ----------------------------------------------------------------------------------
//
// The WebSocket contract (docs/interfaces/websocket.md): whole state on connect, deltas afterwards.
// Sending the whole document on every change is correct but wasteful -- one PDO frame costing 1,886
// bytes on every open socket, on a device with four of them.

void test_a_delta_carries_only_what_changed(void) {
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);

  char buf[8192];
  const size_t n = comfoair_api_render_delta(buf, sizeof buf);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE(contains(json, "\"supply_air_temp_post_heater\":21.5"));
  TEST_ASSERT_FALSE_MESSAGE(contains(json, "\"extract_air_temp\""),
                            "an unchanged entity was included");
}

void test_a_value_written_again_with_the_same_reading_is_not_a_change(void) {
  // The unit rebroadcasts constantly. Treating every frame as a change would make a delta
  // as large as the full document and defeat the point.
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[8192];
  comfoair_api_render_delta(buf, sizeof buf);            // drains it

  comfoair_state_apply_pdo(221, frame, sizeof frame);    // same reading again
  const size_t n = comfoair_api_render_delta(buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("{}", std::string(buf, n).c_str(),
                                   "an unchanged rebroadcast produced a delta");
}

void test_taking_a_delta_clears_it(void) {
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[8192];
  comfoair_api_render_delta(buf, sizeof buf);
  const size_t n = comfoair_api_render_delta(buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("{}", std::string(buf, n).c_str());
}

void test_a_full_push_also_clears_the_delta(void) {
  // A client that has just been sent the whole state must not immediately receive a delta
  // repeating part of it.
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[8192];
  comfoair_api_render_push(buf, sizeof buf, true);
  const size_t n = comfoair_api_render_delta(buf, sizeof buf);
  TEST_ASSERT_EQUAL_STRING("{}", std::string(buf, n).c_str());
}

void test_a_rest_read_of_the_state_does_not_consume_the_push_delta(void) {
  // The PUSH marks belong to the WebSocket clients. GET /api/state renders the same document
  // for a REST reader (Home Assistant's poll, a page load, a curl), and none of those is a push
  // client: if that render cleared the marks, a poll landing before the next push round would
  // take the change from every open socket, which would then not see it until it moved again.
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[8192];
  comfoair_api_render_state(buf, sizeof buf);
  const size_t n = comfoair_api_render_push(buf, sizeof buf, false);
  TEST_ASSERT_TRUE_MESSAGE(contains(std::string(buf, n), "\"supply_air_temp_post_heater\":21.5"),
                           "a REST render consumed the change before the push delta sent it");
}

void test_the_fan_appears_in_a_delta_when_it_moves(void) {
  const uint8_t level[] = {2};
  comfoair_state_apply_pdo(65, level, sizeof level);
  char buf[8192];
  const size_t n = comfoair_api_render_delta(buf, sizeof buf);
  TEST_ASSERT_TRUE(contains(std::string(buf, n), "\"fan\":{"));
}


// --- the push envelope -------------------------------------------------------------------
//
// The socket carries three kinds of message and the client has to tell them apart without
// guessing: a full state, a delta, and a keep-alive. A bare object cannot say which it is,
// and inferring it from size or content is the kind of thing that works until it does not.

void test_a_pushed_full_state_says_so(void) {
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[8192];
  const size_t n = comfoair_api_render_push(buf, sizeof buf, true);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE(contains(json, "\"type\":\"state\""));
  TEST_ASSERT_TRUE(contains(json, "\"values\":{"));
  TEST_ASSERT_TRUE(contains(json, "\"supply_air_temp_post_heater\":21.5"));
}

void test_a_pushed_delta_says_so(void) {
  const uint8_t frame[] = {0xd7, 0x00};
  comfoair_state_apply_pdo(221, frame, sizeof frame);
  char buf[8192];
  const size_t n = comfoair_api_render_push(buf, sizeof buf, false);
  const std::string json(buf, n);
  TEST_ASSERT_TRUE(contains(json, "\"type\":\"delta\""));
  TEST_ASSERT_TRUE(contains(json, "\"supply_air_temp_post_heater\":21.5"));
}

void test_a_delta_with_nothing_in_it_renders_nothing_at_all(void) {
  // The caller needs to know there is nothing worth sending, and an envelope wrapping an
  // empty object is indistinguishable from a real message that happens to be empty.
  char buf[8192];
  comfoair_api_render_push(buf, sizeof buf, false);   // drains whatever the seed left
  TEST_ASSERT_EQUAL_size_t(0, comfoair_api_render_push(buf, sizeof buf, false));
}

void test_a_full_push_is_sent_even_when_nothing_changed(void) {
  // A client that has just connected gets the state whether or not anything moved since
  // the last one did.
  char buf[8192];
  comfoair_api_render_push(buf, sizeof buf, true);
  TEST_ASSERT_GREATER_THAN_size_t(0, comfoair_api_render_push(buf, sizeof buf, true));
}

void test_the_entities_document_fits_the_buffer_with_a_quarter_spare(void) {
  // GET /api/entities is the largest document and grows with every registry row; this pins the
  // headroom (comfoair_api.h states the per-row figure). The boot log reports the size too, but only on a device; this makes outgrowing the buffer a host
  // failure the moment pdo_table.py gains the row that breaks it.
  static char buf[COMFOAIR_API_DOC_BUFFER_SIZE];
  const size_t len = comfoair_api_render_entities(buf, sizeof buf);
  TEST_ASSERT_TRUE_MESSAGE(len > 0, "the entities document did not fit at all");
  TEST_ASSERT_TRUE_MESSAGE(len < (size_t)COMFOAIR_API_DOC_BUFFER_SIZE * 3 / 4,
                           "less than a quarter of the JSON buffer is spare: raise "
                           "COMFOAIR_API_DOC_BUFFER_SIZE in comfoair_api.h");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_entity_with_no_reading_is_null_not_absent);
  RUN_TEST(test_a_numeric_reading_carries_its_divider);
  RUN_TEST(test_a_boolean_reading_is_a_json_boolean);
  RUN_TEST(test_a_text_reading_is_quoted);
  RUN_TEST(test_the_q350_labels_are_what_the_unit_means);
  RUN_TEST(test_pdo_225_is_published_as_its_raw_number);
  RUN_TEST(test_the_fan_is_an_object_because_it_is_not_a_scalar);
  RUN_TEST(test_every_entity_appears_in_the_state_document);
  RUN_TEST(test_a_small_buffer_is_never_overflowed);
  RUN_TEST(test_the_entity_list_describes_what_a_client_needs);
  RUN_TEST(test_a_secret_never_appears_in_any_rendering);
  RUN_TEST(test_the_broker_diagnostics_are_not_entities);
  RUN_TEST(test_quotes_and_backslashes_are_escaped);
  RUN_TEST(test_control_characters_become_unicode_escapes);
  RUN_TEST(test_utf8_above_ascii_is_escaped_rather_than_passed_through);
  RUN_TEST(test_truncation_never_cuts_an_escape_sequence_in_half);
  RUN_TEST(test_a_codepoint_outside_unicode_is_dropped_not_encoded);
  RUN_TEST(test_a_lone_surrogate_is_dropped);
  RUN_TEST(test_a_valid_astral_codepoint_becomes_a_surrogate_pair);
  RUN_TEST(test_the_entity_list_carries_every_field_entities_ts_declares);
  RUN_TEST(test_an_entity_with_no_pdo_reports_null_rather_than_minus_one);
  RUN_TEST(test_the_entity_list_carries_state_class_for_ha_statistics);
  RUN_TEST(test_an_entity_with_no_state_class_reports_null_not_empty_string);
  RUN_TEST(test_the_entity_list_carries_enabled_by_default);
  RUN_TEST(test_a_single_entity_renders_with_its_metadata);
  RUN_TEST(test_a_single_entity_with_no_reading_is_null);
  RUN_TEST(test_rendering_an_unknown_entity_writes_nothing);
  RUN_TEST(test_a_range_the_table_does_not_give_is_null_not_zero);
  RUN_TEST(test_the_airflow_constraint_bitset_is_decoded);
  RUN_TEST(test_a_constraint_bitset_without_bit_45_says_no_restrictions);
  RUN_TEST(test_a_document_that_does_not_fit_reports_failure_rather_than_a_short_answer);
  RUN_TEST(test_a_document_that_fits_still_reports_its_length);
  RUN_TEST(test_a_delta_carries_only_what_changed);
  RUN_TEST(test_a_value_written_again_with_the_same_reading_is_not_a_change);
  RUN_TEST(test_taking_a_delta_clears_it);
  RUN_TEST(test_a_full_push_also_clears_the_delta);
  RUN_TEST(test_a_rest_read_of_the_state_does_not_consume_the_push_delta);
  RUN_TEST(test_the_fan_appears_in_a_delta_when_it_moves);
  RUN_TEST(test_a_pushed_full_state_says_so);
  RUN_TEST(test_a_pushed_delta_says_so);
  RUN_TEST(test_a_delta_with_nothing_in_it_renders_nothing_at_all);
  RUN_TEST(test_a_full_push_is_sent_even_when_nothing_changed);
  RUN_TEST(test_the_entities_document_fits_the_buffer_with_a_quarter_spare);
  return UNITY_END();
}
