// GET /api/status, the members added to it after phase 6, and the rules those members have to obey.
//
// A NEW DIRECTORY AND NOT A THIRTIETH CASE IN test_wire.cpp: test_wire.cpp is already 1199 lines
// against a 600-line ceiling, and the fields added to this one document are one piece of work.
// PlatformIO links every .cpp in a directory into one binary, so this suite is its own binary
// with its own main().
//
// It carries `phase`, the eight fields of the mqtt block, `uptime_ms` and `rssi`.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "comfoair_json.h"
#include "comfoair_phase.h"
#include "comfoair_wire.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_out[4096];

bool contains(const char *doc, const char *needle) { return strstr(doc, needle) != nullptr; }

comfoair_wire_provision_t connected_status(void) {
  comfoair_wire_provision_t s = {};
  s.state                     = COMFOAIR_PROV_CONNECTED;
  s.mode                      = COMFOAIR_PROV_MODE_STATION;
  s.failure                   = COMFOAIR_PROV_FAIL_NONE;
  s.provisioned               = true;
  s.has_credentials           = true;
  s.connected                 = true;
  s.fallback_in_ms            = COMFOAIR_PROV_NEVER;
  s.phase                     = COMFOAIR_PHASE_ONLINE;
  s.uptime_ms                 = 1234567;
  s.has_rssi                  = true;
  s.rssi                      = -67;
  snprintf(s.ssid, sizeof s.ssid, "%s", "Kitchen");
  snprintf(s.ap_ssid, sizeof s.ap_ssid, "%s", "comfoair-3f2a");
  snprintf(s.ip, sizeof s.ip, "%s", "192.168.1.42");
  return s;
}

// Every one of the sixteen filled, and each with a value of its own: a block of zeros cannot tell
// a field that is rendered from a field rendered in the wrong place. The header's rule is that a
// caller passing a pointer has filled ALL of it -- there is no partial form of the block.
comfoair_wire_mqtt_t broker_stats(void) {
  comfoair_wire_mqtt_t m   = {};
  m.configured             = true;
  m.connected              = true;
  m.published              = 1234;
  m.commands               = 7;
  m.rejected               = 1;
  m.reconnects             = 2;
  m.last_connack           = 0;
  m.link                   = COMFOAIR_MQTT_LINK_ONLINE;
  m.failure                = COMFOAIR_MQTT_FAIL_NONE;
  m.attempts               = 3;
  m.retry_in_ms            = COMFOAIR_PROV_NEVER;
  m.subscriptions          = COMFOAIR_MQTT_SUBS_GRANTED;
  m.disconnects            = 5;
  m.applied_epoch          = 4;
  m.last_transport_errno   = 0;
  return m;
}

// The `mqtt` member's object, lifted out as a document of its own -- the same two helpers
// test_wire.cpp carries, copied rather than shared because PlatformIO links each directory into a
// binary of its own. Lifted rather than matched by substring because the OUTER document carries a
// `failure` and a `connected` of its own: a substring match cannot say which of the two it found,
// which is exactly the confusion the `link` member exists to prevent.
std::string mqtt_block(const char *doc) {
  const char *key = strstr(doc, "\"mqtt\":");
  if (key == nullptr)
    return std::string();
  const char *open  = strchr(key, '{');
  const char *close = open != nullptr ? strchr(open, '}') : nullptr;
  if (close == nullptr)
    return std::string();
  return std::string(open, (size_t)(close - open) + 1);
}

std::string without_mqtt_block(const char *doc) {
  const char *key = strstr(doc, ",\"mqtt\":");
  if (key == nullptr)
    return std::string(doc);
  return std::string(doc, (size_t)(key - doc)) + "}";
}

// Where a key stands inside a document, so that ORDER can be asserted and not just presence.
// Fails the caller's assertion by answering npos, which no comparison below accepts.
size_t at(const std::string &doc, const char *key) { return doc.find(key); }

// Whether any KEY of the document contains `needle`. Keys and not the whole text: the text
// legitimately contains the word, because comfoair_wire_connack_name(4) is the sentence "bad
// username or password" -- a spelling of a code, not a secret. A leak is a MEMBER, so a member is
// what is searched. Every key ends in `":` and no value in this document does.
bool a_key_contains(const std::string &doc, const char *needle) {
  for (size_t p = doc.find("\":"); p != std::string::npos && p > 0; p = doc.find("\":", p + 1)) {
    const size_t open = doc.rfind('"', p - 1);
    if (open != std::string::npos &&
        doc.substr(open + 1, p - open - 1).find(needle) != std::string::npos)
      return true;
  }
  return false;
}

// The eight the document has shipped since commit b068b8c, IN THE ORDER IT SHIPPED THEM. A list
// written out here rather than derived from the renderer: a test that read the order back out of
// the thing it is pinning would agree with any order at all.
const char *const kOld[] = {"\"configured\":",  "\"connected\":",   "\"published\":",
                            "\"commands\":",    "\"rejected\":",    "\"reconnects\":",
                            "\"last_connack\":", "\"last_connack_reason\":"};
const char *const kNew[] = {"\"link\":",          "\"failure\":",        "\"attempts\":",
                            "\"retry_in_ms\":",   "\"subscriptions\":",  "\"disconnects\":",
                            "\"applied_epoch\":", "\"last_transport_errno\":"};

}  // namespace

void test_the_status_document_carries_the_phase(void) {
  // The aggregate existed for four tasks with nowhere to be read. Until this member, the ONLY
  // channel it had was a colour in a technical room -- and a colour cannot be pasted into a support
  // message. `state` beside it is the provisioning machine's, which is a different question with a
  // different answer: a device can be `connected` and `bus-silent` at the same moment.
  const comfoair_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"phase\":\"online\""));
  TEST_ASSERT_TRUE_MESSAGE(contains(g_out, "\"state\":\"connected\""),
                           "the phase displaced the provisioning state instead of joining it");
}

void test_the_phase_is_spelled_by_the_aggregate_and_not_by_the_renderer(void) {
  // One spelling per phase, decided in comfoair_phase_name() -- the same invariant
  // comfoair_prov_failure_name() carries, and for the same reason: REST, the log line and the SPA
  // each inventing a spelling is a lost invariant, and this document is the one a support message
  // gets pasted from. All fourteen, because a table that covered twelve would be a renderer that
  // silently answers "unknown" for the two an owner is most likely to be looking at.
  for (int p = 0; p < (int)COMFOAIR_PHASE_COUNT; ++p) {
    comfoair_wire_provision_t s = connected_status();
    s.phase                     = (device_phase_t)p;
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);

    char expected[64];
    snprintf(expected, sizeof expected, "\"phase\":\"%s\"", comfoair_phase_name((device_phase_t)p));
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, expected), expected);
  }
}

void test_a_number_that_is_not_a_phase_is_answered_and_not_crashed(void) {
  // comfoair_phase_name() is NEVER NULL, including for a number outside the enumeration, and this
  // is the caller that makes that contract load-bearing: put_string() handed a null pointer would
  // write a bare word into a JSON document and the page would fail to parse a healthy device.
  comfoair_wire_provision_t s = connected_status();
  s.phase                     = (device_phase_t)99;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"phase\":\"unknown\""));
}

void test_a_caller_that_filled_nothing_says_boot_rather_than_nothing(void) {
  // The zero value of the field is COMFOAIR_PHASE_BOOT, and that is deliberate rather than
  // convenient: a caller who has not asked the monitor anything has a device that is starting up,
  // which is what BOOT means (comfoair_phase.h:44-47). Rendering nothing, or an empty string, would
  // be the answer "we have no idea" written where a client expects a word it can switch on.
  comfoair_wire_provision_t s = {};
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"phase\":\"boot\""));
}

void test_the_phase_is_a_flat_field_ahead_of_the_mqtt_block(void) {
  // comfoair_json_check() stops at the FIRST nested value it meets and seek() refuses to step over
  // one, so every flat field has to stand ahead of the mqtt block or become unreachable to the
  // reader that walks this document from the front (comfoair_wire_render_status.c:113-115).
  // A `phase` nested inside a `device` object, or placed after `mqtt`, would pass a contains()
  // check and fail that reader.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const char *phase = strstr(g_out, "\"phase\":");
  const char *block = strstr(g_out, "\"mqtt\":");
  TEST_ASSERT_NOT_NULL(phase);
  TEST_ASSERT_NOT_NULL(block);
  TEST_ASSERT_TRUE_MESSAGE(phase < block, "the phase landed behind the only nested member");
  // And it is a string at the top level, not an object of its own.
  TEST_ASSERT_TRUE(contains(g_out, "\"phase\":\"online\""));
}

// --- the eight new members of the mqtt block ---------------------------------------------------

void test_the_new_fields_come_after_the_existing_eight(void) {
  // BACKWARD COMPATIBILITY PINNED BY ORDER, which is the only way it can be pinned: the eight
  // above have been on the wire since b068b8c, and a consumer that walks this block from the front
  // -- which is what comfoair_json's seek() does -- reads them in the order they are written.
  // Inserting a new member BETWEEN two old ones is the change this case exists to fail, and it is
  // a change that passes every contains() assertion in the tree.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);

  for (size_t i = 0; i < sizeof kOld / sizeof kOld[0]; ++i)
    TEST_ASSERT_TRUE_MESSAGE(at(block, kOld[i]) != std::string::npos, kOld[i]);
  for (size_t i = 0; i < sizeof kNew / sizeof kNew[0]; ++i)
    TEST_ASSERT_TRUE_MESSAGE(at(block, kNew[i]) != std::string::npos, kNew[i]);

  for (size_t i = 1; i < sizeof kOld / sizeof kOld[0]; ++i)
    TEST_ASSERT_TRUE_MESSAGE(at(block, kOld[i - 1]) < at(block, kOld[i]), kOld[i]);
  for (size_t i = 1; i < sizeof kNew / sizeof kNew[0]; ++i)
    TEST_ASSERT_TRUE_MESSAGE(at(block, kNew[i - 1]) < at(block, kNew[i]), kNew[i]);

  // The seam itself: the last of the old stands ahead of the first of the new.
  TEST_ASSERT_TRUE_MESSAGE(at(block, kOld[7]) < at(block, kNew[0]),
                           "a new member was inserted among the eight already on the wire");

  // And SIXTEEN members, no seventeenth. Every key in this block ends in `":` and no value
  // contains that pair, so counting them counts members -- which is what turns "somebody added a
  // field" into a red test rather than a discovery made later by a consumer.
  size_t keys = 0;
  for (size_t i = block.find("\":"); i != std::string::npos; i = block.find("\":", i + 1))
    keys++;
  TEST_ASSERT_EQUAL_size_t_MESSAGE(16, keys, block.c_str());
}

void test_link_is_not_called_state(void) {
  // THE TRAP THIS CASE EXISTS FOR, and it is live on the owner's device today: the document
  // already has a top-level `state` (the provisioning machine's) and a top-level `failure` (the
  // Wi-Fi one). Two members named `state` meaning different things is a prepared confusion -- so
  // the broker's is `link` -- and `failure` IS duplicated by name,
  // deliberately, so the two must be told apart by WHERE they are and never by a substring.
  comfoair_wire_provision_t s = connected_status();
  s.failure                   = COMFOAIR_PROV_FAIL_NONE;  // the Wi-Fi is fine
  comfoair_wire_mqtt_t m      = broker_stats();
  m.connected                 = false;
  m.link                      = COMFOAIR_MQTT_LINK_REJECTED;  // the broker is not
  m.failure                   = COMFOAIR_MQTT_FAIL_REJECTED;
  m.last_connack              = 5;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  const std::string outer = without_mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);

  TEST_ASSERT_TRUE(at(block, "\"link\":\"rejected\"") != std::string::npos);
  TEST_ASSERT_TRUE_MESSAGE(at(block, "\"state\":") == std::string::npos,
                           "the broker block grew a second member called state");
  TEST_ASSERT_TRUE(at(outer, "\"state\":\"connected\"") != std::string::npos);

  // The same document, one moment: the network has not failed and the broker has refused us.
  TEST_ASSERT_TRUE_MESSAGE(at(outer, "\"failure\":\"none\"") != std::string::npos, outer.c_str());
  TEST_ASSERT_TRUE_MESSAGE(at(block, "\"failure\":\"bad-credentials\"") != std::string::npos,
                           block.c_str());
}

void test_the_broker_block_names_its_failure_class(void) {
  // One spelling per class, decided in comfoair_mqtt_fail_name() and NOT here: the log line the
  // publisher writes and this document have to use the same word, and comfoair_wire.h:230-236
  // already records what a second spelling costs. All seven, plus a number that is not a class --
  // a renderer handed one must answer "unknown" rather than hand put_string() a null pointer.
  for (int f = 0; f <= (int)COMFOAIR_MQTT_FAIL_COUNT; ++f) {
    const comfoair_mqtt_fail_t why = (comfoair_mqtt_fail_t)f;
    const comfoair_wire_provision_t s = connected_status();
    comfoair_wire_mqtt_t            m = broker_stats();
    m.failure                         = why;
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

    char expected[64];
    snprintf(expected, sizeof expected, "\"failure\":\"%s\"", comfoair_mqtt_fail_name(why));
    TEST_ASSERT_TRUE_MESSAGE(at(mqtt_block(g_out), expected) != std::string::npos, expected);
  }
}

void test_the_link_word_is_the_ladders_and_not_a_second_spelling(void) {
  // The five words of `mqtt.link`, from comfoair_mqtt_link_name(). Same argument as above,
  // and the same total-over-the-enumeration sweep: `off` is the value a half-built signal answers
  // for ever on a live broker, so the word that reports it must at least be the ladder's own.
  for (int l = 0; l <= (int)COMFOAIR_MQTT_LINK_COUNT; ++l) {
    const comfoair_mqtt_link_t link = (comfoair_mqtt_link_t)l;
    const comfoair_wire_provision_t s = connected_status();
    comfoair_wire_mqtt_t            m = broker_stats();
    m.link                            = link;
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

    char expected[64];
    snprintf(expected, sizeof expected, "\"link\":\"%s\"", comfoair_mqtt_link_name(link));
    TEST_ASSERT_TRUE_MESSAGE(at(mqtt_block(g_out), expected) != std::string::npos, expected);
  }
}

void test_the_subscription_words_are_total(void) {
  // Four values and a number that is none of them. `refused` is the whole
  // point of the member: a device whose ACL forbids SUBSCRIBE publishes `online`, shows every
  // entity live and updating temperatures, and not one button in Home Assistant works. Nothing else
  // in this document says so.
  for (int v = 0; v <= (int)COMFOAIR_MQTT_SUBS_COUNT; ++v) {
    const comfoair_mqtt_subs_t subs = (comfoair_mqtt_subs_t)v;
    const comfoair_wire_provision_t s = connected_status();
    comfoair_wire_mqtt_t            m = broker_stats();
    m.subscriptions                   = subs;
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

    char expected[64];
    snprintf(expected, sizeof expected, "\"subscriptions\":\"%s\"", comfoair_mqtt_subs_name(subs));
    TEST_ASSERT_TRUE_MESSAGE(at(mqtt_block(g_out), expected) != std::string::npos, expected);
  }
}

void test_a_null_retry_is_null_and_not_zero(void) {
  // COMFOAIR_PROV_NEVER through put_duration, the sentinel this document already uses for
  // `fallback_ap_in_ms` -- one value, one spelling. "No attempt is
  // scheduled" and "an attempt in 0 ms" are OPPOSITE answers: the first is a device that is
  // dialling right now, the second is a device about to dial. A page shown 0 for both counts down
  // to a deadline that does not exist, and a page shown 4294967295 shows 49 days.
  const comfoair_wire_provision_t s = connected_status();
  comfoair_wire_mqtt_t            m = broker_stats();

  m.retry_in_ms = COMFOAIR_PROV_NEVER;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  const std::string never = mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(at(never, "\"retry_in_ms\":null") != std::string::npos, never.c_str());
  TEST_ASSERT_TRUE(at(never, "\"retry_in_ms\":0") == std::string::npos);
  TEST_ASSERT_TRUE(at(never, "4294967295") == std::string::npos);

  // And zero is still zero: the sentinel must not swallow a legitimate value either.
  m.retry_in_ms = 0;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(at(mqtt_block(g_out), "\"retry_in_ms\":0") != std::string::npos);

  m.retry_in_ms = 271000;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  uint32_t          ms    = 0;
  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "retry_in_ms", &ms));
  TEST_ASSERT_EQUAL_UINT32(271000, ms);
}

void test_a_refusal_with_an_unknown_connack_code_still_renders(void) {
  // Arbitration B: a code outside 0..5 is REJECTED, and comfoair_wire_connack_name() answers
  // "unknown refusal code" rather than an empty string. Both halves matter -- the number an owner
  // can look up and the sentence they can read travel together, and neither may be invented.
  const comfoair_wire_provision_t s = connected_status();
  comfoair_wire_mqtt_t            m = broker_stats();
  m.connected                       = false;
  m.last_connack                    = 9;
  m.link                            = COMFOAIR_MQTT_LINK_REJECTED;
  m.failure                         = COMFOAIR_MQTT_FAIL_REJECTED;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_TRUE(at(block, "\"last_connack\":9") != std::string::npos);
  TEST_ASSERT_TRUE(at(block, "\"last_connack_reason\":\"unknown refusal code\"") !=
                   std::string::npos);
  TEST_ASSERT_TRUE(at(block, "\"failure\":\"bad-credentials\"") != std::string::npos);
}

void test_a_transport_timeout_leaves_errno_zero_by_construction(void) {
  // THE CAVEAT THIS CASE EXISTS TO RECORD, and it is obligatory: when esp_mqtt_connect times out,
  // esp-mqtt dispatches MQTT_EVENT_DISCONNECTED and NO MQTT_EVENT_ERROR at all. Nothing ever fills
  // esp_transport_sock_errno for that failure, so the field stays 0 -- BY CONSTRUCTION, not by
  // accident. DO NOT read a zero here as a defect and DO NOT invent a value for it: the failure is
  // fully described by `failure`, `attempts` and `retry_in_ms`, which all move for exactly this
  // case.
  //
  // What the renderer owes: the member is ALWAYS present and is a number, so a reader can tell
  // "no errno was recorded" from "the firmware does not report one".
  const comfoair_wire_provision_t s = connected_status();
  comfoair_wire_mqtt_t            m = broker_stats();
  m.connected                       = false;
  m.link                            = COMFOAIR_MQTT_LINK_BACKOFF;
  m.failure                         = COMFOAIR_MQTT_FAIL_TCP;
  m.attempts                        = 4;
  m.retry_in_ms                     = 16000;
  m.disconnects                     = 4;
  m.last_transport_errno            = 0;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  uint32_t          n     = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_u32(block.c_str(), "last_transport_errno", &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);
  TEST_ASSERT_TRUE_MESSAGE(at(block, "\"last_transport_errno\":null") == std::string::npos,
                           "an absent errno is 0, not null: null would say the field is unknown");
  // The rest of the failure is on the wire, which is what makes the zero readable.
  TEST_ASSERT_TRUE(at(block, "\"failure\":\"tcp\"") != std::string::npos);
  TEST_ASSERT_TRUE(at(block, "\"attempts\":4") != std::string::npos);
  TEST_ASSERT_TRUE(at(block, "\"retry_in_ms\":16000") != std::string::npos);
  TEST_ASSERT_TRUE(at(block, "\"disconnects\":4") != std::string::npos);
  // And a real errno is carried raw, by the precedent this document already set for
  // `failure_reason`: ECONNREFUSED, ETIMEDOUT and EHOSTUNREACH need three different actions from
  // the owner and the class `tcp` collapses all three.
  m.last_transport_errno = 111;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(at(mqtt_block(g_out), "\"last_transport_errno\":111") != std::string::npos);
}

void test_the_mqtt_block_stays_last(void) {
  // WHY net STAYS FLAT, and the order of the arguments matters. First: the
  // eight fields are shipped and pinned, and renaming them into a `net` object is a breaking API
  // change made for symmetry -- AGENTS.md criterion 4 forbids bending the API to suit a screen.
  // Second, and it is what this case can actually execute: comfoair_json_check() answers
  // DOC_NESTED at the FIRST nested value and seek() answers MALFORMED if it meets one before the
  // key it wants, so a second object anywhere would hide every field behind it.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string doc   = g_out;
  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);

  // The block is the LAST member: the document ends with its closing brace and the object's.
  TEST_ASSERT_EQUAL_size_t(doc.size(), at(doc, "\"mqtt\":") + block.size() +
                                           strlen("\"mqtt\":") + 1 /* the outer } */);
  // It is also the ONLY nested value: no object inside it either.
  TEST_ASSERT_TRUE_MESSAGE(block.find('{', 1) == std::string::npos, block.c_str());
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_NESTED, comfoair_json_check(g_out));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(block.c_str()));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(without_mqtt_block(g_out).c_str()));

  // And the consequence that matters: every flat member is still reachable through the reader that
  // walks from the front, INCLUDING the two this task adds. A `net` object before them, or an
  // `uptime_ms` placed after `mqtt`, turns each of these into MALFORMED or MISSING.
  uint32_t n = 0;
  int32_t  i = 0;
  char     word[32];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_string(g_out, "state", word, sizeof word));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(g_out, "failure_reason", &n));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(g_out, "uptime_ms", &n));
  TEST_ASSERT_EQUAL_UINT32(1234567, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_i32(g_out, "rssi", &i));
  TEST_ASSERT_EQUAL_INT32(-67, i);
}

// --- the two top-level members -----------------------------------------------------------------

void test_uptime_is_rendered_at_the_top_level(void) {
  // Without it "the house is still coming up" is indistinguishable from "it broke an hour ago" --
  // the two states that need opposite actions from the owner, and the only two the rest of this
  // document cannot tell apart. It stands beside the other two clocks, `setup_window_remaining_ms`
  // and `fallback_ap_in_ms`, because it is read from the same clock in the same snapshot.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string doc = g_out;
  TEST_ASSERT_TRUE_MESSAGE(at(doc, "\"uptime_ms\":1234567") != std::string::npos, g_out);
  TEST_ASSERT_TRUE(at(doc, "\"fallback_ap_in_ms\":") < at(doc, "\"uptime_ms\":"));
  TEST_ASSERT_TRUE_MESSAGE(at(doc, "\"uptime_ms\":") < at(doc, "\"mqtt\":"),
                           "uptime landed behind the only nested member");

  // NOT a 32-bit millisecond counter. It wraps at 49.7 days, and a device that has run for two
  // months would report a fresh boot -- which is precisely the lie this member exists to end.
  comfoair_wire_provision_t old = connected_status();
  old.uptime_ms                 = 5000000000ull;  // 57.9 days
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&old, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE_MESSAGE(contains(g_out, "\"uptime_ms\":5000000000"), g_out);
}

void test_rssi_is_rendered_beside_the_raw_failure_reason(void) {
  // ONE FIELD INSTEAD OF A FIFTH Wi-Fi VERDICT (arbitration A, contradiction 19). "The router is
  // in the next room and DHCP is silent" and "we are at the edge of the cell" are two support
  // questions with one answer, and the raw `failure_reason` beside it already separates 201 from
  // 212 with no firmware change at all. That is why no fifth comfoair_prov_failure_t is added.
  comfoair_wire_provision_t s = connected_status();
  s.failure_reason            = 201;  // NO_AP_FOUND, as the radio reports it
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);

  const std::string doc = g_out;
  TEST_ASSERT_TRUE(at(doc, "\"failure_reason\":201") != std::string::npos);
  TEST_ASSERT_TRUE_MESSAGE(at(doc, "\"rssi\":-67") != std::string::npos, g_out);
  TEST_ASSERT_TRUE(at(doc, "\"failure_reason\":") < at(doc, "\"rssi\":"));

  // NOT ASSOCIATED IS null AND NOT 0. esp_wifi_sta_get_ap_info() answers nothing at all when the
  // station is not on a network, and 0 dBm is a legal reading -- a radio pressed against an
  // antenna -- so a sentinel would have to steal a value that means something. The pair
  // `has_rssi`/`rssi` is the same "one field per fact" rule that keeps retry_scheduled out of a
  // zero (comfoair_mqtt_ladder.h). A page shown 0 would report a perfect signal on a device that
  // is not connected to anything.
  s.has_rssi = false;
  s.rssi     = 0;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE_MESSAGE(contains(g_out, "\"rssi\":null"), g_out);
  TEST_ASSERT_FALSE_MESSAGE(contains(g_out, "\"rssi\":0"),
                            "an unassociated radio reported a perfect signal");
}

// --- the two rules the whole document inherits -------------------------------------------------

void test_no_field_carries_a_secret(void) {
  // COUNTERS ONLY (comfoair_wire.h:246-248), and this is the guard on the sentence "a diagnostic
  // struct is exactly the shape somebody later adds the host, to help with debugging, to". Not the
  // password, not its length, not its hash, and NOT a "the password is not empty" flag -- the last
  // is the one that gets added in good faith, and it hands an attacker on the OPEN setup access
  // point the one bit that turns a guess into a campaign.
  //
  // Nothing in comfoair_wire_mqtt_t can hold a secret, which is why the assertion is on the
  // rendered DOCUMENT: a new member is what would carry one, and a new member shows up here.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  const comfoair_wire_device_t    d = {"9c139ed48ae4", "9c:13:9e:d4:8a:e4", "1.0.0"};
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, &d, g_out, sizeof g_out) > 0);

  // TWO LISTS, because the two halves of the document are under different rules. The BLOCK is
  // counters only and may not name a credential in any form, `credent` included. The OUTER half
  // already carries `has_credentials` -- the unclaimed rule's input about the Wi-Fi pair and the
  // fact the access policy acts on, shipped and not this task's to remove -- so the outer sweep is
  // the password-shaped fragments alone, `_len` and `_set` among them: a length or an "it is not
  // empty" flag hands whoever is in radio range of the OPEN setup access point the bit that turns a
  // guess into a campaign. `ui_password_set` is GET /api/config's answer, never this one.
  static const char *const kBlock[] = {"psk",   "password", "passwd", "secret", "hash", "token",
                                       "credent", "user",   "host",   "_len",   "_set"};
  static const char *const kOuter[] = {"psk", "password", "passwd", "secret", "hash", "token",
                                       "_len", "_set"};
  // Swept at every failure class and at a CONNACK refusal too, because a refusal branch is exactly
  // where somebody would hang "and here is what we tried to log in with".
  for (int f = 0; f <= (int)COMFOAIR_MQTT_FAIL_COUNT; ++f) {
    comfoair_wire_mqtt_t bad = broker_stats();
    if (f < (int)COMFOAIR_MQTT_FAIL_COUNT) {
      bad.failure      = (comfoair_mqtt_fail_t)f;
      bad.connected    = false;
      bad.last_connack = 4;
    }
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &bad, &d, g_out, sizeof g_out) > 0);
    const std::string block = mqtt_block(g_out);
    const std::string outer = without_mqtt_block(g_out);
    for (size_t i = 0; i < sizeof kBlock / sizeof kBlock[0]; ++i)
      TEST_ASSERT_FALSE_MESSAGE(a_key_contains(block, kBlock[i]), kBlock[i]);
    for (size_t i = 0; i < sizeof kOuter / sizeof kOuter[0]; ++i)
      TEST_ASSERT_FALSE_MESSAGE(a_key_contains(outer, kOuter[i]), kOuter[i]);
  }
}

void test_the_document_still_fits_the_scratch_buffer(void) {
  // 0 is a DEFECT the caller turns into a 500 (comfoair_wire.h), never a short document behind a
  // 200 -- and growing this block by eight members is exactly the change that could stop the
  // biggest document fitting. Everything at its maximum: a 32-byte SSID of characters the escaper
  // expands sixfold, a full ap_ssid, the longest address and UINT32_MAX in every counter.
  comfoair_wire_provision_t s = connected_status();
  for (size_t i = 0; i + 1 < sizeof s.ssid; ++i)
    s.ssid[i] = '\\';
  s.ssid[sizeof s.ssid - 1]       = '\0';
  for (size_t i = 0; i + 1 < sizeof s.ap_ssid; ++i)
    s.ap_ssid[i] = 'A';
  s.ap_ssid[sizeof s.ap_ssid - 1] = '\0';
  snprintf(s.ip, sizeof s.ip, "%s", "255.255.255.255");
  s.window_remaining_ms = 4294967294u;
  s.fallback_in_ms      = 4294967294u;
  s.uptime_ms           = 18446744073709551615ull;
  s.failure_reason      = 255;
  s.rssi                = -128;

  comfoair_wire_mqtt_t m     = broker_stats();
  m.published                = 4294967295u;
  m.commands                 = 4294967295u;
  m.rejected                 = 4294967295u;
  m.reconnects               = 4294967295u;
  m.last_connack             = 4294967295u;
  m.attempts                 = 4294967295u;
  m.retry_in_ms              = 4294967294u;
  m.disconnects              = 4294967295u;
  m.applied_epoch            = 4294967295u;
  m.last_transport_errno     = 4294967295u;
  const comfoair_wire_device_t d = {"9c139ed48ae4", "9c:13:9e:d4:8a:e4", "v1.0.0-2026-09-07-a3e"};

  const size_t used = comfoair_wire_render_provision(&s, &m, &d, g_out, sizeof g_out);
  TEST_ASSERT_TRUE_MESSAGE(used > 0, "the fullest possible status document no longer fits");
  TEST_ASSERT_EQUAL_size_t(used, strlen(g_out));
  // And the answer for a buffer that is genuinely too small is still 0 rather than a short read.
  char small[64];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_wire_render_provision(&s, &m, &d, small, sizeof small));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_status_document_carries_the_phase);
  RUN_TEST(test_the_phase_is_spelled_by_the_aggregate_and_not_by_the_renderer);
  RUN_TEST(test_a_number_that_is_not_a_phase_is_answered_and_not_crashed);
  RUN_TEST(test_a_caller_that_filled_nothing_says_boot_rather_than_nothing);
  RUN_TEST(test_the_phase_is_a_flat_field_ahead_of_the_mqtt_block);
  RUN_TEST(test_the_new_fields_come_after_the_existing_eight);
  RUN_TEST(test_link_is_not_called_state);
  RUN_TEST(test_the_broker_block_names_its_failure_class);
  RUN_TEST(test_the_link_word_is_the_ladders_and_not_a_second_spelling);
  RUN_TEST(test_the_subscription_words_are_total);
  RUN_TEST(test_a_null_retry_is_null_and_not_zero);
  RUN_TEST(test_a_refusal_with_an_unknown_connack_code_still_renders);
  RUN_TEST(test_a_transport_timeout_leaves_errno_zero_by_construction);
  RUN_TEST(test_the_mqtt_block_stays_last);
  RUN_TEST(test_uptime_is_rendered_at_the_top_level);
  RUN_TEST(test_rssi_is_rendered_beside_the_raw_failure_reason);
  RUN_TEST(test_no_field_carries_a_secret);
  RUN_TEST(test_the_document_still_fits_the_scratch_buffer);
  return UNITY_END();
}
