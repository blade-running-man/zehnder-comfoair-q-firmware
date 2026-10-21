// `applied_epoch` in GET /api/status: the ONE member of the mqtt block whose VALUE a caller acts
// on rather than reads, and until this suite the only one no case asserted a value for.
//
// A NEW DIRECTORY AND NOT A NINETEENTH CASE IN test_wire_status.cpp, which is not a preference:
// that file stands at exactly 600 lines, the ceiling AGENTS.md sets for a Unity suite, and
// PlatformIO links every .cpp in a directory into one binary, so a second file beside it would
// collide on main(). No platformio.ini of its own is needed -- [env:native] takes `test_filter =
// test_*` and carries every include path already.
//
// WHY THE VALUE AND NOT THE PRESENCE. comfoair_wire.h:346-352 makes this field the handshake that
// confirms a save: the settings page remembers the number it saw, POSTs a broker, then polls until
// the number CHANGES and `link` is no longer "connecting". Presence is what the existing suite
// pins -- `"applied_epoch":` appears in the ordered list of the eight new members -- and presence
// survives every way the handshake can break. Off by one, the page confirms a save the firmware
// has not made; stuck, "Save" spins until the owner reloads and assumes the device is broken;
// carried from a cache taken before the client was rebuilt, the page reads the PREVIOUS client's
// verdict as the new one's, which is the exact failure the header's own paragraph forbids. All
// three render a perfectly well-formed document.
#include <unity.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "comfoair_json.h"
#include "comfoair_wire.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_out[4096];

// Copied from test_wire_status.cpp rather than shared, for the reason that file gives for copying
// them out of test_wire.cpp: a directory is a binary, and a fake or a fixture promoted to a shared
// place is how "it grew a feature for suite B" quietly weakens suite A (AGENTS.md).
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

// EVERY COUNTER A DIFFERENT NUMBER, and none of them 4: a block where two members share a value
// cannot tell "applied_epoch was rendered" from "disconnects was rendered twice".
comfoair_wire_mqtt_t broker_stats(void) {
  comfoair_wire_mqtt_t m = {};
  m.configured           = true;
  m.connected            = true;
  m.published            = 1234;
  m.commands             = 7;
  m.rejected             = 1;
  m.reconnects           = 2;
  m.last_connack         = 0;
  m.link                 = COMFOAIR_MQTT_LINK_ONLINE;
  m.failure              = COMFOAIR_MQTT_FAIL_NONE;
  m.attempts             = 3;
  m.retry_in_ms          = COMFOAIR_PROV_NEVER;
  m.subscriptions        = COMFOAIR_MQTT_SUBS_GRANTED;
  m.disconnects          = 5;
  m.applied_epoch        = 4;
  m.last_transport_errno = 0;
  return m;
}

// The mqtt member lifted out as a document of its own, so that comfoair_json_u32 walks a flat
// object. Read through the parser and never by substring: "\"applied_epoch\":4" is a prefix of
// "\"applied_epoch\":41", so a contains() assertion would accept a value ten times too large.
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

// The value the document carries, or the assertion fails saying so. UINT32_MAX would be a legal
// reading, so it cannot double as "absent"; the FOUND check is what separates the two.
uint32_t rendered_epoch(const comfoair_wire_mqtt_t &m) {
  const comfoair_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);
  uint32_t n = 0;
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_JSON_FOUND,
                            comfoair_json_u32(block.c_str(), "applied_epoch", &n), g_out);
  return n;
}

}  // namespace

void test_the_epoch_on_the_wire_is_the_number_the_caller_handed_over(void) {
  // Off by one in either direction is the whole finding. Minus one and the page confirms a save
  // the firmware never made -- it is already looking at the number it will call "new". Plus one
  // and the page confirms before the client has been rebuilt, which is the "moved when the request
  // arrived" race -- the reason the publisher bumps the epoch only AFTER the rebuild -- wearing a
  // different sign.
  comfoair_wire_mqtt_t m = broker_stats();
  m.applied_epoch        = 4;
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(4, rendered_epoch(m), "the save handshake is off by one");
}

void test_the_epoch_is_not_a_neighbouring_counter(void) {
  // It stands between `disconnects` and `last_transport_errno` and is the same width as both, so
  // a renderer that put the wrong struct member here produces a document that parses, orders and
  // sweeps clean. Each neighbour is asserted too: reading the right member is not enough if the
  // one beside it was displaced onto the same source.
  comfoair_wire_mqtt_t m = broker_stats();
  m.reconnects           = 900;
  m.disconnects          = 901;
  m.attempts             = 902;
  m.applied_epoch        = 903;
  m.last_transport_errno = 904;
  TEST_ASSERT_EQUAL_UINT32(903, rendered_epoch(m));

  const std::string block = mqtt_block(g_out);
  uint32_t          n     = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "disconnects", &n));
  TEST_ASSERT_EQUAL_UINT32(901, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_u32(block.c_str(), "last_transport_errno", &n));
  TEST_ASSERT_EQUAL_UINT32(904, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "reconnects", &n));
  TEST_ASSERT_EQUAL_UINT32(900, n);
}

void test_a_sequence_of_epochs_reaches_the_wire_unchanged(void) {
  // THE STOP CONDITION OF A POLL, which is what makes a CONSTANT and a STALE value the two
  // expensive defects rather than cosmetic ones. A renderer that answers the same number for ever
  // -- a literal, or a snapshot cached on the first call, which is what "read the stats before
  // applying" looks like from here -- leaves "Save" spinning until the owner reloads the page and
  // concludes the device did not take the setting. Every render is asked for a DIFFERENT number
  // and every answer has to be that number, so no single value can satisfy the case.
  const uint32_t epochs[] = {0u, 1u, 2u, 3u, 4u, 5u, 17u, 4294967294u, 4294967295u, 6u};
  for (size_t i = 0; i < sizeof epochs / sizeof epochs[0]; ++i) {
    comfoair_wire_mqtt_t m = broker_stats();
    m.applied_epoch        = epochs[i];
    char msg[64];
    snprintf(msg, sizeof msg, "epoch %u did not survive the render", (unsigned)epochs[i]);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(epochs[i], rendered_epoch(m), msg);
  }
}

void test_a_fresh_boot_reports_zero_and_not_nothing(void) {
  // Zero is the baseline the page RECORDS before it POSTs, so it has to be a number the reader
  // finds. Rendered as null -- or left out because "nothing has been applied yet" -- the page has
  // no value to compare against and either polls for ever or accepts the first number it sees as
  // a change. This is the same rule `last_transport_errno` carries two members along: an absent
  // count is 0, and null would mean the firmware does not report the field at all.
  comfoair_wire_mqtt_t m = broker_stats();
  m.applied_epoch        = 0;
  TEST_ASSERT_EQUAL_UINT32(0, rendered_epoch(m));
  TEST_ASSERT_TRUE_MESSAGE(strstr(g_out, "\"applied_epoch\":null") == nullptr,
                           "a device that has applied nothing reported an unknown epoch");
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_epoch_on_the_wire_is_the_number_the_caller_handed_over);
  RUN_TEST(test_the_epoch_is_not_a_neighbouring_counter);
  RUN_TEST(test_a_sequence_of_epochs_reaches_the_wire_unchanged);
  RUN_TEST(test_a_fresh_boot_reports_zero_and_not_nothing);
  return UNITY_END();
}
