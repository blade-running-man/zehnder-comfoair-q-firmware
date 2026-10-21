// The facts the esp-mqtt event handler used to LOSE, and what the owner reads when they are lost.
//
// THE NUMBER ON IT, and it is the whole reason this suite exists: over ten hours of
// hammering an unreachable broker, GET /api/status did not change by a single counter. `connected`
// false as before, `published`, `commands`, `rejected` still, `reconnects` still -- it counts only
// MQTT_EVENT_CONNECTED -- and `last_connack` 0, because only a CONNACK refusal writes it. An owner
// who opened the document twice half a day apart got a BYTE-FOR-BYTE identical answer and could
// not tell "we are trying and failing" from "the task is dead". `disconnects` and
// `last_transport_errno` are what change that, and until this task nothing wrote either.
//
// A NEW DIRECTORY, for the reason test_wire_epoch gives about its own: test_wire_status.cpp stands
// at exactly the 600 lines AGENTS.md allows a suite, test_mqtt_backoff.cpp -- which holds main()
// and every RUN_TEST of that suite -- stands at 599, and PlatformIO links a directory into one
// binary, so neither can take another case.
//
// WHAT THIS SUITE CANNOT SEE, and the report says the same. The two writers are
// `bump(&g_disconnects)` and one store in comfoair_mqtt_event.c, and comfoair_mqtt is not built by
// [env:native] at all -- it is esp-mqtt, FreeRTOS and a client handle. Deleting either leaves every
// case here green. What IS here is the pair of things that are decisions rather than plumbing: what
// a socket errno MEANS when the transport had none to give, and what the document looks like in the
// state described above. Hardware scenarios HV-MQTT-23 and HV-MQTT-24
// (docs/hardware-verification/06-mqtt.md) carry the rest.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "comfoair_json.h"
#include "comfoair_mqtt_backoff.h"
#include "comfoair_wire.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_out[4096];

// A device that is up on Wi-Fi and has never reached its broker: the state an unreachable broker
// leaves it in for ten hours. Copied rather than shared with test_wire_status, for the reason
// that suite gives for copying its own out of test_wire.cpp -- a fixture promoted to a shared place
// is how "it grew a feature for suite B" quietly weakens suite A (AGENTS.md).
comfoair_wire_provision_t online_but_unreached(void) {
  comfoair_wire_provision_t s = {};
  s.state                     = COMFOAIR_PROV_CONNECTED;
  s.mode                      = COMFOAIR_PROV_MODE_STATION;
  s.failure                   = COMFOAIR_PROV_FAIL_NONE;
  s.provisioned               = true;
  s.has_credentials           = true;
  s.connected                 = true;
  s.fallback_in_ms            = COMFOAIR_PROV_NEVER;
  s.phase                     = COMFOAIR_PHASE_ONLINE;
  s.uptime_ms                 = 36000000;
  snprintf(s.ssid, sizeof s.ssid, "%s", "Kitchen");
  snprintf(s.ip, sizeof s.ip, "%s", "192.168.1.42");
  return s;
}

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

}  // namespace

// THE DECISION HALF OF `last_transport_errno`, and it is a decision because esp-mqtt hands us an
// `int` that is not always an errno. esp_mqtt_client_dispatch_transport_error() fills the field
// from esp_transport_get_errno() (mqtt_client.c:2359-2375), and that function RETURNS -1 when the
// transport has no error handle at all (tcp_transport/transport.c:261-271, ESP-IDF 5.5.5).
//
// A CAST IS THEREFORE NOT ENOUGH, and the failure it produces is not subtle: the wire member is a
// uint32, so (uint32_t)(-1) renders `"last_transport_errno":4294967295` -- in the one field
// hardware scenario HV-MQTT-24 tells the owner to read as ECONNREFUSED (111) or EHOSTUNREACH (118;
// 113 is ECONNABORTED in newlib). Zero is the documented answer for "nothing was recorded"
// (comfoair_wire.h), it is already what a timed-out connect produces by construction, and it is
// what the renderer already tells the owner not to read as a defect. So a value that is not a
// positive errno is that same zero.
void test_a_transport_that_had_no_errno_to_give_reports_none_and_not_four_billion(void) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, comfoair_mqtt_transport_errno(-1),
                                   "esp_transport_get_errno()'s own -1 reached the owner's "
                                   "document as 4294967295");
  TEST_ASSERT_EQUAL_UINT32(0u, comfoair_mqtt_transport_errno(0));
  // The whole negative half, so this cannot be a special case for -1 that the next value walks
  // past. INT_MIN included: its cast is 2147483648, which is just as readable and just as wrong.
  for (int e = -1; e > -300; e--) {
    char msg[96];
    snprintf(msg, sizeof msg, "a socket errno of %d was carried out to the owner", e);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, comfoair_mqtt_transport_errno(e), msg);
  }
  TEST_ASSERT_EQUAL_UINT32(0u, comfoair_mqtt_transport_errno(INT_MIN));
}

// AND THE OTHER HALF: a real errno is carried RAW, because the three that matter ask the owner for
// three different actions and the class `tcp` collapses all of them. This is the
// precedent the document already set for `failure_reason`, which is a raw Wi-Fi reason code on the
// same wire -- so a "translation" here would be a second, poorer spelling of a number the owner can
// look up.
void test_the_three_errnos_that_ask_for_three_different_actions_survive_intact(void) {
  // ECONNREFUSED: the broker is switched off. ETIMEDOUT: wrong address, or a firewall.
  // EHOSTUNREACH: no route. One class, three completely different things to go and do.
  TEST_ASSERT_EQUAL_UINT32(111u, comfoair_mqtt_transport_errno(111));
  TEST_ASSERT_EQUAL_UINT32(110u, comfoair_mqtt_transport_errno(110));
  TEST_ASSERT_EQUAL_UINT32(113u, comfoair_mqtt_transport_errno(113));
  // No table, no ceiling and no allow-list: an errno this firmware has never heard of is still the
  // owner's fastest way to a search engine, and a filter here would eat exactly the unknown case.
  TEST_ASSERT_EQUAL_UINT32(1u, comfoair_mqtt_transport_errno(1));
  TEST_ASSERT_EQUAL_UINT32(2147483647u, comfoair_mqtt_transport_errno(INT_MAX));
}

// THE TEN-HOUR DOCUMENT, BUILT ONCE AND READ. Every other case in this repository renders the
// mqtt block from a device that is CONNECTED -- test_wire_status' fixture has reconnects 2 and
// disconnects 5 -- and the state the defect is measured in is the opposite one: a broker that has
// never been reached at all.
//
// WHAT IT PINS: that the two counters are separate members carrying separate facts, so a document
// can say "nothing ever came back, and it dropped six hundred times". Tie them together anywhere on
// the path -- derive one from the other, render one twice -- and this is the shape that stops being
// expressible, taking with it the ONLY observation of "a second device is holding our client id"
// that exists (docs/interfaces/mqtt.md, "Two devices, one broker"): the pair climbing together
// while `connected` flickers.
void test_ten_hours_at_an_unreachable_broker_is_a_document_that_moved(void) {
  const comfoair_wire_provision_t s = online_but_unreached();

  comfoair_wire_mqtt_t m   = {};
  m.configured             = true;
  m.connected              = false;
  m.link                   = COMFOAIR_MQTT_LINK_BACKOFF;
  m.failure                = COMFOAIR_MQTT_FAIL_TCP;
  m.subscriptions          = COMFOAIR_MQTT_SUBS_NONE;
  // NEVER ONCE ACCEPTED, and dropped on every one of six hundred dials. The two numbers are the
  // point of the case and they must be able to disagree by that much.
  m.reconnects             = 0;
  m.disconnects            = 600;
  m.attempts               = COMFOAIR_MQTT_ATTEMPT_MAX;
  m.retry_in_ms            = 52000;
  m.last_connack           = 0;
  m.last_transport_errno   = comfoair_mqtt_transport_errno(111);
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, nullptr, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  uint32_t          n     = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "reconnects", &n));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, n, "a broker that was never reached reported a reconnect");
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "disconnects", &n));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(600u, n,
                                   "the drops were rendered from the counter of successes");
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_u32(block.c_str(), "last_transport_errno", &n));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(111u, n, "ECONNREFUSED did not reach the owner's document");
  // And the document says the same thing in words, so the numbers are readable rather than merely
  // present: it is trying, it is failing at the transport, and it will try again.
  TEST_ASSERT_TRUE(block.find("\"link\":\"backoff\"") != std::string::npos);
  TEST_ASSERT_TRUE(block.find("\"failure\":\"tcp\"") != std::string::npos);
  TEST_ASSERT_TRUE(block.find("\"retry_in_ms\":52000") != std::string::npos);
  TEST_ASSERT_TRUE(block.find("\"connected\":false") != std::string::npos);
}

// THE CAVEAT, PINNED SO THAT NOBODY LATER "FIXES" IT. A timed-out esp_mqtt_connect dispatches
// MQTT_EVENT_DISCONNECTED and NO MQTT_EVENT_ERROR at all (mqtt_client.c:1715-1718 -> :858-860), so
// for the commonest failure of all there is nothing to record and the errno stays 0. The counter,
// though, is NOT excused: DISCONNECTED is exactly the event that arrives, so a document in this
// state must still show a `disconnects` that has moved. That asymmetry is the whole of what makes
// the zero readable rather than suspicious, and scenario HV-MQTT-18 depends on it.
void test_a_timeout_moves_the_counter_even_though_it_leaves_the_errno_at_zero(void) {
  const comfoair_wire_provision_t s = online_but_unreached();

  comfoair_wire_mqtt_t m = {};
  m.configured           = true;
  m.link                 = COMFOAIR_MQTT_LINK_BACKOFF;
  m.failure              = COMFOAIR_MQTT_FAIL_TCP;
  m.disconnects          = 37;
  m.last_transport_errno = 0;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, nullptr, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  uint32_t          n     = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "disconnects", &n));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(37u, n,
                                   "the failure that produces no error event produced no count "
                                   "either, which is the whole of defect 5.10");
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_u32(block.c_str(), "last_transport_errno", &n));
  TEST_ASSERT_EQUAL_UINT32(0u, n);
  // Zero, and NOT null: "no errno was recorded" and "this firmware does not report one" are
  // different answers, and only the second would be a defect.
  TEST_ASSERT_TRUE_MESSAGE(block.find("\"last_transport_errno\":null") == std::string::npos,
                           "an unrecorded errno became null, which says the field is unknown");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_transport_that_had_no_errno_to_give_reports_none_and_not_four_billion);
  RUN_TEST(test_the_three_errnos_that_ask_for_three_different_actions_survive_intact);
  RUN_TEST(test_ten_hours_at_an_unreachable_broker_is_a_document_that_moved);
  RUN_TEST(test_a_timeout_moves_the_counter_even_though_it_leaves_the_errno_at_zero);
  return UNITY_END();
}
