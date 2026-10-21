// The reader that decodes what a browser POSTs to this device.
//
// Every case below is a body somebody can send. The setup access point is OPEN on purpose,
// so on it "the client" is anyone in radio range, and this is the first code in the firmware
// that looks at their bytes -- before the access policy has anything to say, because the policy
// decides on a PATH and this decides what the body meant. That is the whole reason the reader is
// a pure function over a NUL-terminated buffer rather than a loop inside a request handler: none
// of these needs a radio to reproduce.
//
// The document shapes it has to read are in web/src/api/client.ts (DeviceConfig, ConfigPatch,
// ProvisionRequest). All three are FLAT -- strings, numbers and booleans, no nesting -- and this
// reader refuses anything else rather than skipping it, because a body it half understands is a
// save that stores some of what the owner typed.
#include <unity.h>

#include <cstring>
#include <string>

#include "comfoair_json.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
std::string read_string(const char *doc, const char *key, comfoair_json_read_t *how,
                        size_t cap = 64) {
  char buf[256];
  memset(buf, 'x', sizeof buf);
  *how = comfoair_json_string(doc, key, buf, cap);
  return *how == COMFOAIR_JSON_FOUND ? std::string(buf) : std::string();
}
}  // namespace

// --- what counts as a document ----------------------------------------------------------------

void test_a_flat_object_is_accepted(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK,
                    comfoair_json_check("{\"a\":\"x\",\"b\":12,\"c\":true,\"d\":null}"));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{}"));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("  {  \"a\" : \"x\" }  "));
}

void test_a_body_that_is_not_an_object_is_refused_rather_than_read_as_an_empty_one(void) {
  // The difference matters more than it looks. Every field of a config patch is OPTIONAL --
  // absent means "leave the stored value alone" -- so a document read as "no keys at all"
  // is a perfectly successful save that changes nothing, and the owner is told Saved. A body
  // this reader cannot understand has to be a 400, and that is only possible if it is
  // distinguishable from `{}`.
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(""));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("[]"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("\"a string\""));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("12"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("null"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(nullptr));
}

void test_trailing_rubbish_after_the_object_is_refused(void) {
  // "{}{}"" is two documents and this reads one. Accepting it would mean the second half was
  // silently ignored, and the obvious way to exploit that is to put the fields you want the
  // device to ignore in the first object.
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{}{}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":1} x"));
}

void test_an_unterminated_document_is_refused(void) {
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\""));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":\"unterminated"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":1,"));
}

void test_a_nested_value_is_refused_not_skipped(void) {
  // Nothing this firmware accepts is nested, so a nested value is either a broken client or
  // somebody probing. Skipping it means the reader has to track depth inside a value it will
  // never use -- which is where a parser of this kind gets its interesting bugs.
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_NESTED, comfoair_json_check("{\"a\":{\"b\":1}}"));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_NESTED, comfoair_json_check("{\"a\":[1,2]}"));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_NESTED, comfoair_json_check("{\"a\":[]}"));
}

void test_a_document_stops_being_readable_at_its_first_nested_member(void) {
  // WHAT MAKES "net STAYS FLAT, mqtt STAYS LAST" A RULE AND NOT A PREFERENCE.
  // The status document is exactly this shape: flat members, then one object. check() answers
  // NESTED for the whole of it -- which is the honest answer and not a failure -- and every member
  // AHEAD of the object is still readable one key at a time.
  const char *doc = "{\"a\":\"x\",\"mqtt\":{\"b\":1},\"c\":\"y\"}";
  char        out[8];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_NESTED, comfoair_json_check(doc));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string(doc, "a", out, sizeof out));
  TEST_ASSERT_EQUAL_STRING("x", out);
}

void test_two_nested_members_hide_everything_between_them(void) {
  // The other half of the same rule, and the regression that would otherwise be found by somebody
  // failing to read `failure` one day: seek() will not step OVER a nested value, so a member that
  // stands behind one is not missing -- it is unreachable, and the reader says MALFORMED rather
  // than MISSING. Wrapping the network fields in a `net` object puts an object in front of every
  // flat member of the status document at once.
  const char *doc = "{\"net\":{\"ip\":\"1\"},\"failure\":\"none\"}";
  char        out[8];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string(doc, "failure", out, sizeof out));
}

void test_a_duplicated_key_is_refused(void) {
  // JSON permits it and every parser resolves it differently -- first wins, last wins, or an
  // error. On this device the disagreement is exploitable: a page renders the sentinel for an
  // untouched password, and {"ui_password":"letmein","ui_password":"__UNCHANGED__"} is a
  // request whose meaning depends entirely on which of the two this reader happens to find.
  // There is no reading of it that is safe to guess, so it is refused.
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_DUPLICATE, comfoair_json_check("{\"a\":1,\"a\":2}"));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_DUPLICATE,
                    comfoair_json_check("{\"ui_password\":\"x\",\"ui_password\":\"\"}"));
}

void test_a_key_that_is_not_a_string_is_refused(void) {
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{a:1}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{1:2}"));
}

void test_a_trailing_comma_is_refused(void) {
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":1,}"));
}

// --- strings -----------------------------------------------------------------------------------

void test_a_plain_string_comes_back_whole(void) {
  comfoair_json_read_t how;
  TEST_ASSERT_EQUAL_STRING("Kitchen", read_string("{\"wifi_ssid\":\"Kitchen\"}", "wifi_ssid", &how).c_str());
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, how);
}

void test_an_empty_string_is_found_and_is_not_the_same_as_absent(void) {
  // The whole sentinel scheme turns on this distinction: absent leaves a stored secret alone,
  // empty CLEARS it (comfoair_config.h, comfoair_config_patch_t). A reader that answered
  // "missing" for both would make an untouched form delete the broker password.
  comfoair_json_read_t how;
  TEST_ASSERT_EQUAL_STRING("", read_string("{\"wifi_psk\":\"\"}", "wifi_psk", &how).c_str());
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, how);

  char buf[8];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MISSING, comfoair_json_string("{\"a\":1}", "wifi_psk", buf, sizeof buf));
}

void test_the_escapes_a_browser_actually_emits_are_decoded(void) {
  // JSON.stringify escapes exactly these and passes UTF-8 through raw. A password with a quote
  // or a backslash in it is ordinary, and a device that stored the escaped form would accept a
  // password the owner can never type again.
  comfoair_json_read_t how;
  TEST_ASSERT_EQUAL_STRING("a\"b", read_string("{\"k\":\"a\\\"b\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a\\b", read_string("{\"k\":\"a\\\\b\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a/b", read_string("{\"k\":\"a\\/b\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a\nb", read_string("{\"k\":\"a\\nb\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a\tb", read_string("{\"k\":\"a\\tb\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a\rb", read_string("{\"k\":\"a\\rb\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a\bb", read_string("{\"k\":\"a\\bb\"}", "k", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("a\fb", read_string("{\"k\":\"a\\fb\"}", "k", &how).c_str());
}

void test_raw_utf8_passes_through_byte_for_byte(void) {
  // An SSID is an OCTET string, not text: 802.11 does not say what encoding it is in. Whatever
  // arrives has to reach the radio unchanged, or a household whose router is named in Cyrillic
  // cannot be joined.
  comfoair_json_read_t how;
  TEST_ASSERT_EQUAL_STRING("Кухня", read_string("{\"s\":\"Кухня\"}", "s", &how).c_str());
}

void test_a_u_escape_becomes_utf8(void) {
  comfoair_json_read_t how;
  TEST_ASSERT_EQUAL_STRING("A", read_string("{\"s\":\"\\u0041\"}", "s", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("\xc3\xa9", read_string("{\"s\":\"\\u00e9\"}", "s", &how).c_str());
  TEST_ASSERT_EQUAL_STRING("\xe2\x82\xac", read_string("{\"s\":\"\\u20ac\"}", "s", &how).c_str());
  // A surrogate pair, which is the only way an emoji reaches this device through JSON.
  TEST_ASSERT_EQUAL_STRING("\xf0\x9f\x92\xa1",
                           read_string("{\"s\":\"\\ud83d\\udca1\"}", "s", &how).c_str());
}

void test_a_lone_surrogate_is_refused_rather_than_emitted(void) {
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string("{\"s\":\"\\ud83d\"}", "s", buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string("{\"s\":\"\\udca1x\"}", "s", buf, sizeof buf));
}

void test_an_escaped_nul_is_refused_because_the_answer_is_a_c_string(void) {
  // \u0000 is legal JSON and this reader hands back a NUL-terminated buffer, so decoding it
  // would silently truncate. On a password field that means a device that accepts a shorter
  // secret than the owner set; on an SSID it means a network name that is not the one typed.
  // comfoair_config_check_ssid refuses an embedded NUL for the same reason -- this is the same
  // rule one layer earlier, where it can still be reported as a bad request.
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string("{\"s\":\"a\\u0000b\"}", "s", buf, sizeof buf));
}

void test_a_raw_control_character_in_a_string_is_refused(void) {
  // RFC 8259 section 7: everything below 0x20 must be escaped. A raw newline in the middle of a
  // string is a broken client, and guessing what it meant is how a parser starts accepting
  // documents no other parser does.
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string("{\"s\":\"a\nb\"}", "s", buf, sizeof buf));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"s\":\"a\nb\"}"));
}

void test_an_unknown_escape_is_refused(void) {
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string("{\"s\":\"a\\qb\"}", "s", buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MALFORMED,
                    comfoair_json_string("{\"s\":\"a\\u00zzb\"}", "s", buf, sizeof buf));
}

void test_a_string_longer_than_the_buffer_is_refused_never_truncated(void) {
  // A truncated credential asks a different question from the one the client asked, and it
  // asks it silently. comfoair_auth.h reaches the same conclusion for the Authorization header
  // and for the same reason.
  char buf[8];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_TOO_LONG,
                    comfoair_json_string("{\"s\":\"0123456789\"}", "s", buf, sizeof buf));
  // Exactly filling the buffer, terminator included, is not too long.
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_string("{\"s\":\"0123456\"}", "s", buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("0123456", buf);
}

void test_asking_for_a_string_and_finding_a_number_says_so(void) {
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_WRONG_TYPE,
                    comfoair_json_string("{\"mqtt_port\":1883}", "mqtt_port", buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_WRONG_TYPE,
                    comfoair_json_string("{\"a\":null}", "a", buf, sizeof buf));
}

void test_a_key_is_matched_whole_and_not_as_a_prefix(void) {
  // "mqtt_pass" must not find "mqtt_password". The same class of leak comfoair_http_policy.c's
  // path_is() exists to stop, one layer down.
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MISSING,
                    comfoair_json_string("{\"mqtt_password\":\"x\"}", "mqtt_pass", buf, sizeof buf));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MISSING,
                    comfoair_json_string("{\"a\":\"x\"}", "ab", buf, sizeof buf));
}

void test_an_escaped_key_is_matched_by_what_it_decodes_to(void) {
  // No client this firmware talks to escapes a key, but a client that did would otherwise have
  // its field silently ignored -- which for a config patch is a save that dropped a setting.
  char buf[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_string("{\"\\u0061\":\"x\"}", "a", buf, sizeof buf));
  TEST_ASSERT_EQUAL_STRING("x", buf);
}

// --- numbers and booleans ------------------------------------------------------------------

void test_a_whole_number_in_range_is_read(void) {
  uint32_t port = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_u32("{\"mqtt_port\":1883}", "mqtt_port", &port));
  TEST_ASSERT_EQUAL_UINT32(1883, port);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32("{\"p\":0}", "p", &port));
  TEST_ASSERT_EQUAL_UINT32(0, port);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32("{\"p\":4294967295}", "p", &port));
  TEST_ASSERT_EQUAL_UINT32(4294967295u, port);
}

void test_a_number_too_large_for_the_field_is_refused_never_wrapped(void) {
  // comfoair_config_check_port takes a uint32_t precisely so that 70000 can be REFUSED; a
  // reader that wrapped it to 4464 would hand the validator a port the owner never typed and
  // the validator would accept it. The wrap has to be impossible one layer earlier than that.
  uint32_t port = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_OUT_OF_RANGE,
                    comfoair_json_u32("{\"p\":4294967296}", "p", &port));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_OUT_OF_RANGE,
                    comfoair_json_u32("{\"p\":99999999999999999999}", "p", &port));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_OUT_OF_RANGE, comfoair_json_u32("{\"p\":-1}", "p", &port));
}

void test_a_number_that_is_not_whole_is_refused(void) {
  // A port of 18.5 is a typo, not a rounding problem, and "1e3" is a number no owner typed.
  uint32_t port = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_OUT_OF_RANGE, comfoair_json_u32("{\"p\":18.5}", "p", &port));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_OUT_OF_RANGE, comfoair_json_u32("{\"p\":1e3}", "p", &port));
}

void test_a_malformed_number_makes_the_whole_document_malformed(void) {
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"p\":01}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"p\":+1}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"p\":.5}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"p\":1.}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"p\":-}"));
}

void test_true_and_false_are_read_and_nothing_else_is(void) {
  bool on = false;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_bool("{\"ha_discovery\":true}", "ha_discovery", &on));
  TEST_ASSERT_TRUE(on);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_bool("{\"ha_discovery\":false}", "ha_discovery", &on));
  TEST_ASSERT_FALSE(on);
  // Not 1, not "true", not null. A toggle that a firmware reads out of a string is a toggle
  // whose meaning depends on which client sent it.
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_WRONG_TYPE, comfoair_json_bool("{\"a\":1}", "a", &on));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_WRONG_TYPE, comfoair_json_bool("{\"a\":\"true\"}", "a", &on));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_WRONG_TYPE, comfoair_json_bool("{\"a\":null}", "a", &on));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_MISSING, comfoair_json_bool("{\"b\":true}", "a", &on));
}

void test_a_word_that_merely_starts_like_a_literal_is_refused(void) {
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":truthy}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":nul}"));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check("{\"a\":True}"));
}

// --- the reader does not read past what it was given -----------------------------------------

void test_nothing_is_read_past_the_terminator(void) {
  // The document is a NUL-terminated buffer the caller owns. Every truncation of a valid
  // document is refused rather than answered from whatever follows it in memory -- this is the
  // one property that cannot be inspected by reading the output, so it is asserted over every
  // prefix of a realistic body.
  const char full[] =
      "{\"mqtt_host\":\"broker.local\",\"mqtt_port\":1883,\"ha_discovery\":true,"
      "\"ui_password\":\"__UNCHANGED__\"}";
  for (size_t n = 0; n < sizeof full - 1; n++) {
    std::string prefix(full, n);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(COMFOAIR_JSON_DOC_OK, comfoair_json_check(prefix.c_str()),
                                  prefix.c_str());
  }
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(full));
}

void test_reading_a_key_from_a_document_that_did_not_check_is_still_bounded(void) {
  // Handlers are supposed to call comfoair_json_check() first, and one day one will not. The
  // getters therefore do their own scanning rather than trusting a prior pass -- a getter that
  // assumed a checked document would run off the end of an unchecked one.
  char buf[32];
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND,
                        comfoair_json_string("{\"a\":\"unterminated", "a", buf, sizeof buf));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string("{\"a\"", "a", buf, sizeof buf));
  uint32_t n;
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32("{\"a\":12", "a", &n));
}

void test_a_null_key_or_buffer_is_answered_rather_than_dereferenced(void) {
  char buf[8];
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string(nullptr, "a", buf, sizeof buf));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string("{\"a\":\"x\"}", nullptr, buf, sizeof buf));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string("{\"a\":\"x\"}", "a", nullptr, 8));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string("{\"a\":\"x\"}", "a", buf, 0));
}

// --- the escaper, which is the same one the state API has always used -------------------------

void test_the_escaper_is_the_one_the_api_publishes(void) {
  // comfoair_api_escape_json() forwards here. There is ONE set of rules for \u, for surrogate
  // pairs and for control characters in this firmware, and a second implementation would be a
  // second set -- which is AGENTS.md's "one entity list, ever" applied to an encoding.
  char out[64];
  TEST_ASSERT_TRUE(comfoair_json_escape("a\"b", out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING("a\\\"b", out);
  TEST_ASSERT_TRUE(comfoair_json_escape("a\nb", out, sizeof out) > 0);
  TEST_ASSERT_EQUAL_STRING("a\\nb", out);
}

void test_what_the_escaper_writes_the_reader_reads_back(void) {
  // The round trip is the property that matters: a device name with a quote in it is rendered
  // into GET /api/config and comes back through POST /api/config, and if the two halves
  // disagree the name changes every time the page is saved.
  const char *const values[] = {"plain", "a\"b", "a\\b", "a\nb", "Кухня", "", "  spaces  "};
  for (size_t i = 0; i < sizeof values / sizeof values[0]; i++) {
    char escaped[256];
    TEST_ASSERT_TRUE(comfoair_json_escape(values[i], escaped, sizeof escaped) > 0 ||
                     values[i][0] == '\0');
    std::string doc = std::string("{\"k\":\"") + escaped + "\"}";
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_JSON_DOC_OK, comfoair_json_check(doc.c_str()), doc.c_str());
    char back[256];
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_JSON_FOUND,
                              comfoair_json_string(doc.c_str(), "k", back, sizeof back),
                              doc.c_str());
    TEST_ASSERT_EQUAL_STRING(values[i], back);
  }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_flat_object_is_accepted);
  RUN_TEST(test_a_body_that_is_not_an_object_is_refused_rather_than_read_as_an_empty_one);
  RUN_TEST(test_trailing_rubbish_after_the_object_is_refused);
  RUN_TEST(test_an_unterminated_document_is_refused);
  RUN_TEST(test_a_nested_value_is_refused_not_skipped);
  RUN_TEST(test_a_document_stops_being_readable_at_its_first_nested_member);
  RUN_TEST(test_two_nested_members_hide_everything_between_them);
  RUN_TEST(test_a_duplicated_key_is_refused);
  RUN_TEST(test_a_key_that_is_not_a_string_is_refused);
  RUN_TEST(test_a_trailing_comma_is_refused);

  RUN_TEST(test_a_plain_string_comes_back_whole);
  RUN_TEST(test_an_empty_string_is_found_and_is_not_the_same_as_absent);
  RUN_TEST(test_the_escapes_a_browser_actually_emits_are_decoded);
  RUN_TEST(test_raw_utf8_passes_through_byte_for_byte);
  RUN_TEST(test_a_u_escape_becomes_utf8);
  RUN_TEST(test_a_lone_surrogate_is_refused_rather_than_emitted);
  RUN_TEST(test_an_escaped_nul_is_refused_because_the_answer_is_a_c_string);
  RUN_TEST(test_a_raw_control_character_in_a_string_is_refused);
  RUN_TEST(test_an_unknown_escape_is_refused);
  RUN_TEST(test_a_string_longer_than_the_buffer_is_refused_never_truncated);
  RUN_TEST(test_asking_for_a_string_and_finding_a_number_says_so);
  RUN_TEST(test_a_key_is_matched_whole_and_not_as_a_prefix);
  RUN_TEST(test_an_escaped_key_is_matched_by_what_it_decodes_to);

  RUN_TEST(test_a_whole_number_in_range_is_read);
  RUN_TEST(test_a_number_too_large_for_the_field_is_refused_never_wrapped);
  RUN_TEST(test_a_number_that_is_not_whole_is_refused);
  RUN_TEST(test_a_malformed_number_makes_the_whole_document_malformed);
  RUN_TEST(test_true_and_false_are_read_and_nothing_else_is);
  RUN_TEST(test_a_word_that_merely_starts_like_a_literal_is_refused);

  RUN_TEST(test_nothing_is_read_past_the_terminator);
  RUN_TEST(test_reading_a_key_from_a_document_that_did_not_check_is_still_bounded);
  RUN_TEST(test_a_null_key_or_buffer_is_answered_rather_than_dereferenced);

  RUN_TEST(test_the_escaper_is_the_one_the_api_publishes);
  RUN_TEST(test_what_the_escaper_writes_the_reader_reads_back);
  return UNITY_END();
}
