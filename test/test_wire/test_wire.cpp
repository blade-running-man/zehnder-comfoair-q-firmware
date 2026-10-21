// The five documents phase 5 puts on the wire.
//
// Every parse case below is a body somebody can POST to an open access point, and every render
// case is a document a browser will read. Two properties run through the whole file and are worth
// stating once rather than in thirty comments:
//
//  * NO SECRET IS EVER RENDERED. Not the Wi-Fi key, not the broker password, not the UI password.
//    The projection turns each into a sentinel or an empty string and this suite asserts the
//    value itself never appears in the bytes.
//  * NO SUBMITTED VALUE IS EVER QUOTED IN AN ERROR. Errors name a FIELD. Everything this device
//    logs is served by GET /api/log and on the setup access point that is world-readable,
//    so a message that echoes what was typed publishes it.
#include <unity.h>

#include <cstring>
#include <string>

#include "comfoair_command.h"
#include "comfoair_json.h"
#include "comfoair_wire.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

char g_out[4096];

comfoair_wifi_t pair(const char *ssid, const char *psk) {
  comfoair_wifi_t w = {};
  w.ssid_len = (uint8_t)strlen(ssid);
  memcpy(w.ssid, ssid, w.ssid_len);
  w.psk_len = (uint8_t)strlen(psk);
  memcpy(w.psk, psk, w.psk_len);
  return w;
}

std::string ssid_of(const comfoair_wifi_t &w) {
  return std::string((const char *)w.ssid, w.ssid_len);
}
std::string psk_of(const comfoair_wifi_t &w) {
  return std::string((const char *)w.psk, w.psk_len);
}

bool contains(const char *haystack, const char *needle) {
  return strstr(haystack, needle) != nullptr;
}

}  // namespace

// --- POST /api/provision ---------------------------------------------------------------------

void test_a_network_and_a_key_are_taken_as_typed(void) {
  comfoair_wifi_t out = {};
  const comfoair_wire_result_t r = comfoair_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"hunter2hunter\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("Kitchen", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING("hunter2hunter", psk_of(out).c_str());
}

void test_an_empty_key_is_an_open_network_and_not_a_missing_one(void) {
  // comfoair_config_check_psk returns OK for length 0 for exactly this reason: half the guest
  // networks in the world have no key. The page says the same thing in its own words
  // (describeWifiKey, web/src/pages/settings/wifi.ts).
  comfoair_wifi_t out = {};
  const comfoair_wire_result_t r = comfoair_wire_parse_provision(
      "{\"wifi_ssid\":\"Guest\",\"wifi_psk\":\"\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("Guest", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_UINT8(0, out.psk_len);
}

void test_an_absent_key_keeps_the_one_already_stored(void) {
  // The owner changed only the SSID -- a router renamed, or the same key on a second access
  // point. The form omits the field rather than sending a placeholder, which is the same rule
  // comfoair_config_patch_t states for every other string.
  const comfoair_wifi_t stored = pair("Old", "storedkey123");
  comfoair_wifi_t       out    = {};
  const comfoair_wire_result_t r =
      comfoair_wire_parse_provision("{\"wifi_ssid\":\"New\"}", &stored, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("New", ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING("storedkey123", psk_of(out).c_str());
}

void test_an_absent_key_with_nothing_stored_is_an_open_network_attempt(void) {
  // KEEP of nothing. The correct reading, and it needs no special case -- a device with no
  // stored key that is told to join a network without one tries it open.
  comfoair_wifi_t out = {};
  const comfoair_wire_result_t r =
      comfoair_wire_parse_provision("{\"wifi_ssid\":\"Open\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_UINT8(0, out.psk_len);
}

void test_the_sentinel_with_a_key_stored_means_keep_it(void) {
  // The page rendered the sentinel for an untouched box and handed it straight back. This is the
  // ordinary case of a form submitted with only the SSID changed.
  const comfoair_wifi_t stored = pair("Kitchen", "storedkey123");
  comfoair_wifi_t       out    = {};
  const comfoair_wire_result_t r = comfoair_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" COMFOAIR_SECRET_SENTINEL "\"}", &stored, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("storedkey123", psk_of(out).c_str());
}

void test_the_sentinel_is_never_stored_as_a_key(void) {
  // THE CASE THIS FUNCTION EXISTS FOR. "__UNCHANGED__" is thirteen printable bytes and passes
  // comfoair_config_check_psk whole: at least eight characters, no NUL, not sixty-four so no hex
  // test. A handler that wrote it through would leave the device holding credentials it cannot
  // associate with -- and credentials existing is what stops the access point coming back up, so
  // the device disappears. comfoair_provision.c:443-446 calls that the likeliest catastrophe in
  // this project; this path reaches it with nobody having made a typo.
  comfoair_wifi_t out = pair("sentinel", "must-not-survive");
  const comfoair_wire_result_t r = comfoair_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" COMFOAIR_SECRET_SENTINEL "\"}", nullptr, &out);
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  // And nothing was written into the caller's record on the way to refusing.
  TEST_ASSERT_EQUAL_STRING("sentinel", ssid_of(out).c_str());
}

void test_a_missing_network_name_is_refused(void) {
  comfoair_wifi_t out = {};
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_WIRE_OK,
                        comfoair_wire_parse_provision("{\"wifi_psk\":\"hunter2hunter\"}", nullptr,
                                                      &out)
                            .status);
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_WIRE_OK,
                        comfoair_wire_parse_provision("{\"wifi_ssid\":\"\"}", nullptr, &out).status);
}

void test_a_body_that_is_not_json_is_refused_as_a_body_not_as_a_field(void) {
  // The two are different answers to the owner: a bad body is a broken client, a bad field is
  // something they typed. Reporting the first as the second sends them back to a form that was
  // never the problem.
  comfoair_wifi_t out = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_BODY,
                    comfoair_wire_parse_provision("not json", nullptr, &out).status);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_BODY,
                    comfoair_wire_parse_provision("", nullptr, &out).status);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_BODY,
                    comfoair_wire_parse_provision(nullptr, nullptr, &out).status);
}

void test_a_key_that_no_radio_could_use_is_refused_before_it_is_stored(void) {
  // Validated on the way IN, which is the whole first rule of comfoair_config: a stored value
  // that is only discovered to be impossible at the next boot is one the surface that could have
  // fixed it is no longer up to fix.
  comfoair_wifi_t out = {};
  // Seven characters: below the WPA2 floor. esp_wifi would take it and never associate.
  const comfoair_wire_result_t short_key = comfoair_wire_parse_provision(
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"1234567\"}", nullptr, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_REFUSED, short_key.status);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PSK, short_key.err);

  // Sixty-four characters that are not hex. wpa_supplicant reads a 64-character key as a raw
  // PSK in hex and silently never associates when it is not.
  std::string not_hex(64, 'z');
  std::string doc = "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" + not_hex + "\"}";
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_REFUSED,
                    comfoair_wire_parse_provision(doc.c_str(), nullptr, &out).status);
}

void test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_whole(void) {
  // Both extremes are LEGAL -- 802.11 caps an SSID at 32 octets, and 64 characters is the
  // 256-bit PSK written as hex. A reader that reserved a byte for a terminator would truncate
  // each by one and the device would never associate, with a symptom nobody can deduce.
  std::string ssid(32, 'S');
  std::string psk(64, 'a');
  std::string doc = "{\"wifi_ssid\":\"" + ssid + "\",\"wifi_psk\":\"" + psk + "\"}";
  comfoair_wifi_t out = {};
  const comfoair_wire_result_t r = comfoair_wire_parse_provision(doc.c_str(), nullptr, &out);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_UINT8(32, out.ssid_len);
  TEST_ASSERT_EQUAL_UINT8(64, out.psk_len);
  TEST_ASSERT_EQUAL_STRING(ssid.c_str(), ssid_of(out).c_str());
  TEST_ASSERT_EQUAL_STRING(psk.c_str(), psk_of(out).c_str());
}

void test_an_over_long_field_is_refused_rather_than_cut_down_to_a_legal_one(void) {
  std::string ssid(33, 'S');
  std::string doc = "{\"wifi_ssid\":\"" + ssid + "\"}";
  comfoair_wifi_t out = {};
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_WIRE_OK,
                        comfoair_wire_parse_provision(doc.c_str(), nullptr, &out).status);
}

void test_no_error_message_from_provisioning_contains_a_submitted_value(void) {
  // The message goes into an HTTP body and into the log ring, and /api/log hands the ring to
  // whoever can reach the device -- which on the setup access point is everybody.
  const char *const bodies[] = {
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"1234567\"}",
      "{\"wifi_ssid\":\"Kitchen\",\"wifi_psk\":\"" COMFOAIR_SECRET_SENTINEL "\"}",
      "{\"wifi_ssid\":\"SECRETNAME\",\"wifi_psk\":123}",
      "rubbish",
  };
  for (size_t i = 0; i < sizeof bodies / sizeof bodies[0]; i++) {
    comfoair_wifi_t              out = {};
    const comfoair_wire_result_t r   = comfoair_wire_parse_provision(bodies[i], nullptr, &out);
    const char                  *msg = comfoair_wire_strerror(r);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_FALSE_MESSAGE(contains(msg, "1234567"), msg);
    TEST_ASSERT_FALSE_MESSAGE(contains(msg, "SECRETNAME"), msg);
    TEST_ASSERT_FALSE_MESSAGE(contains(msg, "Kitchen"), msg);
  }
}

// --- POST /api/config ------------------------------------------------------------------------

void test_a_full_settings_submission_is_decoded(void) {
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const char *const body =
      "{\"mqtt_host\":\"broker.local\",\"mqtt_port\":1883,\"mqtt_user\":\"comfo\","
      "\"mqtt_password\":\"brokerpass\",\"topic_prefix\":\"comfoair\",\"ha_discovery\":true,"
      "\"device_name\":\"Loft\",\"ui_password\":\"letmein12\"}";
  const comfoair_wire_result_t r = comfoair_wire_parse_config(body, &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("broker.local", patch.mqtt_host);
  TEST_ASSERT_TRUE(patch.has_mqtt_port);
  TEST_ASSERT_EQUAL_UINT32(1883, patch.mqtt_port);
  TEST_ASSERT_EQUAL_STRING("comfo", patch.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("brokerpass", patch.mqtt_password);
  TEST_ASSERT_EQUAL_STRING("comfoair", patch.topic_prefix);
  TEST_ASSERT_TRUE(patch.has_ha_discovery);
  TEST_ASSERT_TRUE(patch.ha_discovery);
  TEST_ASSERT_EQUAL_STRING("Loft", patch.device_name);
  TEST_ASSERT_EQUAL_STRING("letmein12", patch.ui_password);
}

void test_an_absent_field_is_a_null_pointer_and_an_empty_one_is_an_empty_string(void) {
  // The distinction the whole sentinel scheme is built on. NULL leaves the stored value alone;
  // "" clears it. A parser that wrote "" for an absent key would wipe the broker password every
  // time somebody saved an unrelated setting.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const comfoair_wire_result_t  r =
      comfoair_wire_parse_config("{\"mqtt_user\":\"\"}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_NOT_NULL(patch.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("", patch.mqtt_user);
  TEST_ASSERT_NULL(patch.mqtt_host);
  TEST_ASSERT_NULL(patch.mqtt_password);
  TEST_ASSERT_NULL(patch.ui_password);
  TEST_ASSERT_FALSE(patch.has_mqtt_port);
  TEST_ASSERT_FALSE(patch.has_ha_discovery);
}

void test_an_empty_object_is_a_legitimate_save_that_changes_nothing(void) {
  // The settings page submits the whole document, so this only arrives from a hand-made request
  // -- but it has to succeed, because comfoair_config_apply() distinguishes "a patch with every
  // field absent" (fine) from "there was no document" (an error), and that distinction is only
  // available if `{}` parses.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, comfoair_wire_parse_config("{}", &patch, &storage, NULL, NULL).status);
}

void test_a_body_that_is_not_an_object_is_not_read_as_an_empty_save(void) {
  // If it were, the owner would be told Saved over a request that stored nothing.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_BODY,
                    comfoair_wire_parse_config("[]", &patch, &storage, NULL, NULL).status);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_BODY,
                    comfoair_wire_parse_config("", &patch, &storage, NULL, NULL).status);
}

void test_the_network_cannot_be_set_through_the_settings_route(void) {
  // /api/config is not the route the network travels on, and the omission is the design
  // (web/src/api/client.ts, ConfigPatch). Refused BY NAME rather than ignored: silently dropping
  // a credential somebody typed into a form is worse than telling them where it belongs, because
  // the form then reports success over a network that was never stored.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const comfoair_wire_result_t  ssid =
      comfoair_wire_parse_config("{\"wifi_ssid\":\"Kitchen\"}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_WRONG_ROUTE, ssid.status);
  TEST_ASSERT_EQUAL_STRING("wifi_ssid", ssid.field);

  const comfoair_wire_result_t psk =
      comfoair_wire_parse_config("{\"wifi_psk\":\"hunter2hunter\"}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_WRONG_ROUTE, psk.status);
  TEST_ASSERT_EQUAL_STRING("wifi_psk", psk.field);

  // And the sentence names the door that works, rather than describing the value. A caller
  // reaches this by echoing back the whole GET document, which carries wifi_ssid because the
  // page has to show which network is configured -- so it is not a client doing anything
  // unreasonable, and "bad field" would send it looking at the value it typed.
  TEST_ASSERT_TRUE(contains(comfoair_wire_strerror(ssid), "/api/provision"));
}

void test_an_unknown_field_is_ignored_so_a_newer_page_can_still_save(void) {
  // The other side of the rule above. A page that has grown a field an older firmware does not
  // know about must not have every save refused -- that turns one new setting into a device that
  // cannot be configured at all.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const comfoair_wire_result_t  r      = comfoair_wire_parse_config(
      "{\"mqtt_host\":\"broker.local\",\"something_new\":42}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("broker.local", patch.mqtt_host);
}

void test_the_derived_fields_are_ignored_rather_than_taken_as_input(void) {
  // ui_password_set, read_only and wifi_known_good are computed by the device and appear in the
  // projection only. A stored "a password is set" flag that disagrees with the stored record is a
  // device locked by nobody, for ever (comfoair_config.h:257-260) -- so echoing them back has to
  // be harmless, and the page does echo the whole document.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const comfoair_wire_result_t  r = comfoair_wire_parse_config(
      "{\"ui_password_set\":true,\"read_only\":false,\"wifi_known_good\":true}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_NULL(patch.ui_password);
}

void test_a_field_of_the_wrong_type_names_itself(void) {
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const comfoair_wire_result_t  host =
      comfoair_wire_parse_config("{\"mqtt_host\":42}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, host.status);
  TEST_ASSERT_EQUAL_STRING("mqtt_host", host.field);

  const comfoair_wire_result_t flag =
      comfoair_wire_parse_config("{\"ha_discovery\":\"yes\"}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, flag.status);
  TEST_ASSERT_EQUAL_STRING("ha_discovery", flag.field);
}

void test_a_port_outside_the_range_is_refused_and_never_wrapped(void) {
  // comfoair_config_check_port takes a uint32_t precisely so 70000 can be REFUSED. A parser that
  // wrapped it to 4464 would store a broker address the owner never typed.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD,
                    comfoair_wire_parse_config("{\"mqtt_port\":4294967296}", &patch, &storage, NULL, NULL)
                        .status);
  // In range for the reader and out of range for the validator: this is the layer that says
  // "a number", and comfoair_config_apply() is the one that says "a port".
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_config("{\"mqtt_port\":70000}", &patch, &storage, NULL, NULL).status);
  TEST_ASSERT_EQUAL_UINT32(70000, patch.mqtt_port);
}

void test_a_value_too_long_for_the_field_is_refused_rather_than_truncated(void) {
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  std::string                   host(COMFOAIR_CONFIG_HOST_MAX + 1, 'h');
  std::string                   doc = "{\"mqtt_host\":\"" + host + "\"}";
  const comfoair_wire_result_t  r   = comfoair_wire_parse_config(doc.c_str(), &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, r.status);
  TEST_ASSERT_EQUAL_STRING("mqtt_host", r.field);
}

void test_a_refused_submission_leaves_no_half_decoded_patch_behind(void) {
  // ALL OR NOTHING is comfoair_config_apply()'s contract, and it can only keep it if what it is
  // handed is all or nothing too. A patch carrying the good half of a refused document would be
  // applied by a handler that checked the status after passing it on.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const comfoair_wire_result_t  r      = comfoair_wire_parse_config(
      "{\"mqtt_host\":\"broker.local\",\"ha_discovery\":\"yes\"}", &patch, &storage, NULL, NULL);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, r.status);
  TEST_ASSERT_NULL(patch.mqtt_host);
  TEST_ASSERT_FALSE(patch.has_ha_discovery);
}

// --- GET /api/config -------------------------------------------------------------------------

void test_the_config_document_carries_every_key_the_page_reads(void) {
  // The keys are comfoair_config's `name` column, which is what that table defines as "what the
  // API and the owner see". A key missing here is a field the page silently never shows; a key
  // spelled differently is a field that silently never saves.
  comfoair_config_t cfg;
  comfoair_config_defaults(&cfg, "aabbccddeeff");
  comfoair_config_public_t pub;
  comfoair_config_project(&cfg, &pub);

  const size_t n = comfoair_wire_render_config(&pub, false, true, g_out, sizeof g_out);
  TEST_ASSERT_TRUE(n > 0);
  const char *const keys[] = {"\"wifi_ssid\"",   "\"wifi_psk\"",       "\"mqtt_host\"",
                              "\"mqtt_port\"",   "\"mqtt_user\"",      "\"mqtt_password\"",
                              "\"topic_prefix\"", "\"ha_discovery\"",  "\"device_name\"",
                              "\"ui_password\"", "\"ui_password_set\"", "\"read_only\"",
                              "\"wifi_known_good\"", "\"permanent_manual\""};
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, keys[i]), keys[i]);
}

void test_no_stored_secret_appears_in_the_config_document(void) {
  // The property that unblocks a single universal binary (AGENTS.md). A stored
  // password is never returned by any projection; reads get a sentinel.
  comfoair_config_t cfg;
  comfoair_config_defaults(&cfg, "aabbccddeeff");
  const comfoair_wifi_t stored = pair("Kitchen", "WIFIKEYINTHECLEAR");
  cfg.wifi                     = stored;
  snprintf(cfg.mqtt_password, sizeof cfg.mqtt_password, "%s", "BROKERPASSINTHECLEAR");
  snprintf(cfg.ui_pw_hash, sizeof cfg.ui_pw_hash, "%s", "1$10000$aabb$ccdd");

  comfoair_config_public_t pub;
  comfoair_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(comfoair_wire_render_config(&pub, true, true, g_out, sizeof g_out) > 0);

  TEST_ASSERT_FALSE(contains(g_out, "WIFIKEYINTHECLEAR"));
  TEST_ASSERT_FALSE(contains(g_out, "BROKERPASSINTHECLEAR"));
  // Not the hash either. It is not the password, but it is what an offline attack is run
  // against, and there is no reason for a browser to hold it.
  TEST_ASSERT_FALSE(contains(g_out, "1$10000$aabb$ccdd"));
  // The SSID is not a secret -- it is in every beacon that network sends -- and the page needs
  // it to show which network is configured.
  TEST_ASSERT_TRUE(contains(g_out, "Kitchen"));
  // Each set secret reads back as the sentinel, which is what tells the page a value exists.
  TEST_ASSERT_TRUE(contains(g_out, COMFOAIR_SECRET_SENTINEL));
  TEST_ASSERT_TRUE(contains(g_out, "\"ui_password_set\":true"));
}

void test_an_unset_secret_reads_back_as_an_empty_string_not_as_the_sentinel(void) {
  // The two are what SecretField's placeholder distinguishes: "stored on the device" against
  // "not set". A device with no broker password that answered with the sentinel would tell the
  // owner a password exists that they can never have set.
  comfoair_config_t cfg;
  comfoair_config_defaults(&cfg, "aabbccddeeff");
  comfoair_config_public_t pub;
  comfoair_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(comfoair_wire_render_config(&pub, false, true, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"mqtt_password\":\"\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ui_password\":\"\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ui_password_set\":false"));
}

void test_the_known_good_flag_comes_from_the_machine_and_not_from_the_stored_ssid(void) {
  // An SSID stored beside a mistyped key has never produced an address: the trial that fails
  // goes to RETRYING with nothing restored, and a page that promises a rollback there tells the
  // owner to wait for a return that is not coming.
  comfoair_config_t cfg;
  comfoair_config_defaults(&cfg, "aabbccddeeff");
  cfg.wifi = pair("Kitchen", "hunter2hunter");
  comfoair_config_public_t pub;
  comfoair_config_project(&cfg, &pub);

  TEST_ASSERT_TRUE(comfoair_wire_render_config(&pub, false, true, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"wifi_known_good\":false"));
  TEST_ASSERT_TRUE(comfoair_wire_render_config(&pub, true, true, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"wifi_known_good\":true"));
}

void test_a_document_that_does_not_fit_reports_zero_rather_than_a_short_one(void) {
  // Phase 4's counter-review found the entity list twenty-three bytes from serving truncated
  // JSON with a 200 in front of it. The same rule here: a renderer that does not fit says so.
  comfoair_config_t cfg;
  comfoair_config_defaults(&cfg, "aabbccddeeff");
  comfoair_config_public_t pub;
  comfoair_config_project(&cfg, &pub);
  char small[32];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_wire_render_config(&pub, false, true, small, sizeof small));
}

void test_a_device_name_with_a_quote_in_it_survives_the_round_trip(void) {
  // Rendered by the escaper and read back by the reader in the same component. If the two
  // disagreed, a name with a quote in it would change every time the page was saved.
  comfoair_config_t cfg;
  comfoair_config_defaults(&cfg, "aabbccddeeff");
  snprintf(cfg.device_name, sizeof cfg.device_name, "%s", "Anna\"s \\ loft");
  comfoair_config_public_t pub;
  comfoair_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(comfoair_wire_render_config(&pub, false, true, g_out, sizeof g_out) > 0);
  char escaped_name[128];
  comfoair_json_escape("Anna\"s \\ loft", escaped_name, sizeof escaped_name);

  // The name comes back out of the rendered document byte for byte.
  char back[COMFOAIR_CONFIG_NAME_MAX + 1];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_string(g_out, "device_name", back, sizeof back));
  TEST_ASSERT_EQUAL_STRING("Anna\"s \\ loft", back);

  // And it survives a save, which is what the page actually sends: a ConfigPatch, which omits
  // the two Wi-Fi keys by design (web/src/api/client.ts). The WHOLE GET document is deliberately
  // NOT a legal patch -- test_the_network_cannot_be_set_through_the_settings_route is the other
  // half of that rule.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  const std::string submitted = std::string("{\"device_name\":\"") + escaped_name + "\"}";
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_config(submitted.c_str(), &patch, &storage, NULL, NULL).status);
  TEST_ASSERT_EQUAL_STRING("Anna\"s \\ loft", patch.device_name);
}

// --- GET /api/wifi/scan ----------------------------------------------------------------------

void test_the_scan_is_a_bare_array(void) {
  const comfoair_wire_network_t list[] = {
      {"Kitchen", -52, true},
      {"Guest", -70, false},
  };
  const size_t n = comfoair_wire_render_scan(list, 2, g_out, sizeof g_out);
  TEST_ASSERT_TRUE(n > 0);
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Kitchen\",\"rssi\":-52,\"secure\":true},"
      "{\"ssid\":\"Guest\",\"rssi\":-70,\"secure\":false}]",
      g_out);
}

void test_an_empty_scan_is_an_empty_array_and_not_an_error(void) {
  // A sweep that heard nothing is a true answer. The page keeps manual entry available for it.
  TEST_ASSERT_TRUE(comfoair_wire_render_scan(nullptr, 0, g_out, sizeof g_out) > 0);
  TEST_ASSERT_EQUAL_STRING("[]", g_out);
}

void test_duplicate_names_are_not_filtered_by_the_device(void) {
  // One sweep returns a record per BSSID, so a mesh answers several times under one name. Which
  // to show is a presentation question and mergeScan() has more information than this does.
  const comfoair_wire_network_t list[] = {{"Mesh", -40, true}, {"Mesh", -80, true}};
  TEST_ASSERT_TRUE(comfoair_wire_render_scan(list, 2, g_out, sizeof g_out) > 0);
  TEST_ASSERT_EQUAL_STRING(
      "[{\"ssid\":\"Mesh\",\"rssi\":-40,\"secure\":true},"
      "{\"ssid\":\"Mesh\",\"rssi\":-80,\"secure\":true}]",
      g_out);
}

void test_a_hidden_network_is_reported_with_an_empty_name(void) {
  const comfoair_wire_network_t list[] = {{"", -60, true}};
  TEST_ASSERT_TRUE(comfoair_wire_render_scan(list, 1, g_out, sizeof g_out) > 0);
  TEST_ASSERT_EQUAL_STRING("[{\"ssid\":\"\",\"rssi\":-60,\"secure\":true}]", g_out);
}

void test_a_scan_that_does_not_fit_reports_zero(void) {
  const comfoair_wire_network_t list[] = {{"Kitchen", -52, true}};
  char                          small[16];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_wire_render_scan(list, 1, small, sizeof small));
}

void test_an_ssid_with_json_in_it_cannot_break_the_document(void) {
  // An SSID is chosen by whoever set up the router, and a neighbour's router can be named
  // anything at all. It is rendered through the escaper for that reason and no other.
  comfoair_wire_network_t list[1] = {};
  snprintf(list[0].ssid, sizeof list[0].ssid, "%s", "\"},{\"ssid\":\"evil");
  list[0].rssi   = -50;
  list[0].secure = true;
  TEST_ASSERT_TRUE(comfoair_wire_render_scan(list, 1, g_out, sizeof g_out) > 0);
  // Exactly one record, whatever the name claims.
  const char *first = strstr(g_out, "\"rssi\"");
  TEST_ASSERT_NOT_NULL(first);
  TEST_ASSERT_NULL(strstr(first + 1, "\"rssi\""));
}

// --- GET /api/provision ----------------------------------------------------------------------

namespace {
comfoair_wire_provision_t connected_status(void) {
  comfoair_wire_provision_t s = {};
  s.state                     = COMFOAIR_PROV_CONNECTED;
  s.mode                      = COMFOAIR_PROV_MODE_STATION;
  s.failure                   = COMFOAIR_PROV_FAIL_NONE;
  s.provisioned               = true;
  s.has_credentials           = true;
  s.has_known_good            = true;
  s.connected                 = true;
  s.window_remaining_ms       = 0;
  s.fallback_in_ms            = COMFOAIR_PROV_NEVER;
  snprintf(s.ssid, sizeof s.ssid, "%s", "Kitchen");
  snprintf(s.ap_ssid, sizeof s.ap_ssid, "%s", "comfoair-3f2a");
  snprintf(s.ip, sizeof s.ip, "%s", "192.168.1.42");
  return s;
}

comfoair_wire_mqtt_t broker_stats(void) {
  comfoair_wire_mqtt_t m = {};
  m.configured           = true;
  m.connected            = true;
  m.published            = 1234;
  m.commands             = 7;
  m.rejected             = 0;
  m.reconnects           = 2;
  m.last_connack         = 0;
  return m;
}

// The `mqtt` member's object, lifted out as a document of its own. Lifted rather than matched by
// substring because what is INSIDE it is then read with the same reader the firmware uses -- and
// comfoair_json_check() stops at the first nested value (comfoair_json.cpp:381-383), so the block
// can only be validated as a document once it stands alone. The block holds no object and no array,
// so its first closing brace is its last.
std::string mqtt_block(const char *doc) {
  const char *key = strstr(doc, "\"mqtt\":");
  if (key == nullptr)
    return std::string();
  const char *open = strchr(key, '{');
  const char *close = open != nullptr ? strchr(open, '}') : nullptr;
  if (close == nullptr)
    return std::string();
  return std::string(open, (size_t)(close - open) + 1);
}

// The same document with the block cut out, so the OUTER object can be checked too. Without this
// the outer fields are only ever seen through comfoair_json_check()'s NESTED answer, which stops
// at the block and says nothing about the bytes after it.
std::string without_mqtt_block(const char *doc) {
  const char *key = strstr(doc, ",\"mqtt\":");
  if (key == nullptr)
    return std::string(doc);
  return std::string(doc, (size_t)(key - doc)) + "}";
}
}  // namespace

void test_the_status_document_says_where_the_device_is(void) {
  const comfoair_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"state\":\"connected\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"mode\":\"station\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"provisioned\":true"));
  TEST_ASSERT_TRUE(contains(g_out, "\"wifi_ssid\":\"Kitchen\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ip\":\"192.168.1.42\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"ap_ssid\":\"comfoair-3f2a\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"failure\":\"none\""));
}

void test_the_status_document_names_the_device_it_came_from(void) {
  // The integration keys its config entry on device_id and puts mac in Home Assistant's
  // device connections, which is what merges this device with the MQTT one onto a single
  // card. A status document without them is a device Home Assistant cannot name -- and
  // until this existed, the three values were assembled for the discovery payload and
  // nowhere else, so a client that does not speak MQTT could not read them at all.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_device_t d = {"aabbccddeeff", "aa:bb:cc:dd:ee:ff", "1.4.0"};
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, &d, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"device_id\":\"aabbccddeeff\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"mac\":\"aa:bb:cc:dd:ee:ff\""));
  TEST_ASSERT_TRUE(contains(g_out, "\"sw_version\":\"1.4.0\""));
}

void test_a_status_document_with_no_device_omits_it_rather_than_lying(void) {
  // NULL omits the three keys instead of rendering empty strings, exactly as a NULL mqtt
  // omits its block. An empty device_id would be a client keying an entry on nothing.
  const comfoair_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_FALSE(contains(g_out, "device_id"));
  TEST_ASSERT_FALSE(contains(g_out, "sw_version"));
}

void test_each_of_the_four_failures_has_its_own_word(void) {
  // The invariant, in the API where the owner can see it: "network not found", "wrong password",
  // "refused" and "no address" are four different things to do about it, and throwing the reason
  // away to say one thing would mean none of them.
  struct {
    comfoair_prov_failure_t failure;
    const char             *word;
  } const expected[] = {
      {COMFOAIR_PROV_FAIL_NONE, "none"},
      {COMFOAIR_PROV_FAIL_NETWORK_ABSENT, "network-not-found"},
      {COMFOAIR_PROV_FAIL_WRONG_PASSWORD, "wrong-password"},
      {COMFOAIR_PROV_FAIL_REFUSED, "refused"},
      {COMFOAIR_PROV_FAIL_NO_ADDRESS, "no-address"},
  };
  for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
    comfoair_wire_provision_t s = connected_status();
    s.failure                   = expected[i].failure;
    s.failure_reason            = 15;
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
    std::string want = std::string("\"failure\":\"") + expected[i].word + "\"";
    TEST_ASSERT_TRUE_MESSAGE(contains(g_out, want.c_str()), g_out);
    // The word is the one comfoair_provision decided. One spelling, so the REST projection and
    // the MQTT one cannot invent a second.
    TEST_ASSERT_EQUAL_STRING(expected[i].word, comfoair_prov_failure_name(expected[i].failure));
  }
}

void test_every_state_and_mode_has_a_word_and_none_of_them_is_null(void) {
  // A renderer that printed (null) for a state would put it into the document as a bare word and
  // the page would fail to parse a perfectly healthy device.
  const comfoair_prov_state_t states[] = {
      COMFOAIR_PROV_UNCONFIGURED, COMFOAIR_PROV_CONNECTING,  COMFOAIR_PROV_CONNECTED,
      COMFOAIR_PROV_TRIAL,        COMFOAIR_PROV_RETRYING,    COMFOAIR_PROV_FALLBACK_AP,
      COMFOAIR_PROV_WINDOW_CLOSED};
  for (size_t i = 0; i < sizeof states / sizeof states[0]; i++) {
    const char *name = comfoair_wire_state_name(states[i]);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
  const comfoair_prov_mode_t modes[] = {COMFOAIR_PROV_MODE_OFF, COMFOAIR_PROV_MODE_ACCESS_POINT,
                                        COMFOAIR_PROV_MODE_STATION, COMFOAIR_PROV_MODE_AP_STA};
  for (size_t i = 0; i < sizeof modes / sizeof modes[0]; i++) {
    const char *name = comfoair_wire_mode_name(modes[i]);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
  }
}

void test_every_connack_code_has_its_own_word_and_refused_is_not_bad_password(void) {
  // The two an owner actually hits are 4 and 5, and they are DIFFERENT problems: 4 is a password
  // to retype, 5 is a broker ACL that will not let this client in whatever it types. A single
  // "bad credentials?" for both -- and for 1-3, which are not credentials at all -- is the same
  // lie about the cause that the MQTT_EVENT_ERROR branch exists to stop. MQTT 3.1.1 s3.2.2.3.
  //
  // THE WORDS THEMSELVES ARE THE ASSERTION. An earlier version of this test checked only that the
  // six were non-empty and pairwise distinct, which "a".."f" would have satisfied -- and the whole
  // of finding B4 is about WHICH words the owner reads. These are the spellings comfoair_wire.h
  // declares, and changing one here is meant to be as deliberate as changing the document.
  const char *const canonical[6] = {
      "no refusal",          // and NOT "accepted": a device that never contacted a broker is here
      "unacceptable protocol version",
      "client identifier rejected",
      "server unavailable",
      "bad username or password",   // 4: a password to retype
      "not authorized",             // 5: an ACL that will refuse whatever is typed
  };
  const char *seen[6];
  for (uint32_t code = 0; code <= 5; code++) {
    seen[code] = comfoair_wire_connack_name(code);
    TEST_ASSERT_NOT_NULL(seen[code]);
    TEST_ASSERT_TRUE(seen[code][0] != '\0');
    TEST_ASSERT_EQUAL_STRING(canonical[code], seen[code]);
  }
  for (uint32_t a = 0; a <= 5; a++)
    for (uint32_t b = a + 1; b <= 5; b++)
      TEST_ASSERT_FALSE_MESSAGE(strcmp(seen[a], seen[b]) == 0, seen[a]);
  // Named on their own so the two the owner meets cannot be collapsed by a later edit.
  TEST_ASSERT_FALSE(strcmp(comfoair_wire_connack_name(4), comfoair_wire_connack_name(5)) == 0);
  TEST_ASSERT_EQUAL_STRING("bad username or password", comfoair_wire_connack_name(4));
  TEST_ASSERT_EQUAL_STRING("not authorized", comfoair_wire_connack_name(5));

  // A code no broker should send is still answered, and with ONE spelling rather than a
  // fabricated one -- the same rule comfoair_wire_state_name follows for an unknown state.
  const char *unknown = comfoair_wire_connack_name(99);
  TEST_ASSERT_NOT_NULL(unknown);
  TEST_ASSERT_TRUE(unknown[0] != '\0');
  TEST_ASSERT_EQUAL_STRING("unknown refusal code", unknown);
  TEST_ASSERT_EQUAL_STRING(unknown, comfoair_wire_connack_name(0xFFFFFFFFu));
  for (uint32_t code = 0; code <= 5; code++)
    TEST_ASSERT_FALSE(strcmp(seen[code], unknown) == 0);
}

void test_never_is_rendered_as_null_and_zero_as_zero(void) {
  // Opposite answers. A window with 0 ms left is CLOSED; a window that never closes is the
  // supported configuration for a device sealed behind a front panel. A page that showed 0 for
  // both would count that device down to a deadline it does not have.
  comfoair_wire_provision_t s = connected_status();
  s.window_open               = true;
  s.window_remaining_ms       = COMFOAIR_PROV_NEVER;
  s.fallback_in_ms            = COMFOAIR_PROV_NEVER;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"setup_window_remaining_ms\":null"));
  TEST_ASSERT_TRUE(contains(g_out, "\"fallback_ap_in_ms\":null"));

  s.window_remaining_ms = 0;
  s.fallback_in_ms      = 1000;
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_TRUE(contains(g_out, "\"setup_window_remaining_ms\":0"));
  TEST_ASSERT_TRUE(contains(g_out, "\"fallback_ap_in_ms\":1000"));
}

void test_no_key_of_any_kind_reaches_the_status_document(void) {
  // There is no field to leak from -- comfoair_wire_provision_t has no PSK and neither does
  // comfoair_prov_t, whose static_assert says so. This asserts the absence rather than trusting
  // it, because the way a key gets into a status document is somebody adding a field "to help
  // with debugging".
  comfoair_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_FALSE(contains(g_out, "psk"));
  TEST_ASSERT_FALSE(contains(g_out, "password"));
}

void test_a_status_document_that_does_not_fit_reports_zero(void) {
  const comfoair_wire_provision_t s = connected_status();
  char                            small[24];
  TEST_ASSERT_EQUAL_size_t(0, comfoair_wire_render_provision(&s, NULL, NULL, small, sizeof small));
}

void test_the_status_document_carries_the_broker_counters(void) {
  // Finding H5: comfoair_mqtt_get_stats() existed and had no caller anywhere in the tree, so
  // every one of these numbers was reachable only with a serial cable. They are the whole
  // diagnosis of a broker connection -- "configured but never connected" and "connected and
  // publishing" are different faults with different answers.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

  const std::string block = mqtt_block(g_out);
  TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);
  // A document in its own right, so the reader can be pointed at it. NESTED, not MALFORMED, is
  // what the whole document answers -- the block is the only nested value in it.
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(block.c_str()));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_NESTED, comfoair_json_check(g_out));
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(without_mqtt_block(g_out).c_str()));

  bool     flag = false;
  uint32_t n    = 0;
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_bool(block.c_str(), "configured", &flag));
  TEST_ASSERT_TRUE(flag);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_bool(block.c_str(), "connected", &flag));
  TEST_ASSERT_TRUE(flag);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "published", &n));
  TEST_ASSERT_EQUAL_UINT32(1234, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "commands", &n));
  TEST_ASSERT_EQUAL_UINT32(7, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "rejected", &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "reconnects", &n));
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "last_connack", &n));
  TEST_ASSERT_EQUAL_UINT32(0, n);

  // Counters only. The broker's password has no field here to arrive in, and this asserts the
  // absence rather than trusting it -- a diagnostic block is exactly the shape somebody later
  // adds "the host and the user, to help with debugging" to.
  //
  // NOTE why this is a search for `"password"` WITH ITS QUOTES and not the bare word: CONNACK
  // code 4 spells "bad username or password" into last_connack_reason, so the bare word appears
  // legitimately in this document. This test's fixture has last_connack 0, but the next person to
  // parameterise it over the codes would find a bare search failing for the right-looking wrong
  // reason. A key is what would leak; a key is what is searched for.
  TEST_ASSERT_FALSE(contains(g_out, "\"password\""));
  TEST_ASSERT_FALSE(contains(g_out, "mqtt_host"));
}

void test_a_refused_broker_is_visible_without_a_serial_cable(void) {
  // Finding B4, second half. The MQTT_EVENT_ERROR branch records the refusal and the log names
  // it; until this member existed the owner who mistyped a broker password still read
  // "broker unreachable" every ten seconds and had no way to see the word "not authorized".
  for (uint32_t code = 1; code <= 5; code++) {
    const comfoair_wire_provision_t s = connected_status();
    comfoair_wire_mqtt_t            m = broker_stats();
    m.connected                       = false;  // a refusal is not a connection
    m.last_connack                    = code;
    TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);

    const std::string block = mqtt_block(g_out);
    TEST_ASSERT_TRUE_MESSAGE(!block.empty(), g_out);

    uint32_t seen = 0;
    TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_u32(block.c_str(), "last_connack", &seen));
    TEST_ASSERT_EQUAL_UINT32(code, seen);

    // ASSERTED AGAINST THE FUNCTION, not against a copy of the words. comfoair_wire_connack_name()
    // is what the MQTT log line prints (comfoair_mqtt_client.c:126), and a literal here would let
    // the document and the log drift apart one edit at a time -- which is the phase 5 invariant
    // "one spelling of each answer" lost.
    char reason[64];
    TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                      comfoair_json_string(block.c_str(), "last_connack_reason", reason,
                                           sizeof reason));
    TEST_ASSERT_EQUAL_STRING(comfoair_wire_connack_name(code), reason);

    bool connected = true;
    TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND, comfoair_json_bool(block.c_str(), "connected", &connected));
    TEST_ASSERT_FALSE_MESSAGE(connected, block.c_str());
    // And the block still parses, refusal or not.
    TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(block.c_str()));
  }

  // Code 0 is not a refusal, and it must not read as one: a device that has never contacted a
  // broker is here too.
  const comfoair_wire_provision_t s = connected_status();
  const comfoair_wire_mqtt_t      m = broker_stats();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, &m, NULL, g_out, sizeof g_out) > 0);
  char reason[64];
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_FOUND,
                    comfoair_json_string(mqtt_block(g_out).c_str(), "last_connack_reason", reason,
                                         sizeof reason));
  TEST_ASSERT_EQUAL_STRING(comfoair_wire_connack_name(0), reason);
}

void test_no_broker_block_is_not_a_broker_reporting_zeros(void) {
  // Opposite answers, and the reason the parameter is a pointer rather than a value. Zeros say
  // "the broker connection was looked at and nothing has happened"; no member at all says nobody
  // was in a position to look. POST /api/provision's reply is the second case -- it is answered
  // before the radio has moved to the owner's network -- and a page shown "published": 0 there
  // would read a configured broker gone silent, which is the fault this member exists to expose.
  const comfoair_wire_provision_t s = connected_status();
  TEST_ASSERT_TRUE(comfoair_wire_render_provision(&s, NULL, NULL, g_out, sizeof g_out) > 0);
  TEST_ASSERT_FALSE_MESSAGE(contains(g_out, "\"mqtt\""), g_out);
  TEST_ASSERT_FALSE(contains(g_out, "last_connack"));
  TEST_ASSERT_FALSE(contains(g_out, "reconnects"));
  // Still one flat object, so nothing was left dangling where the member would have gone.
  TEST_ASSERT_EQUAL(COMFOAIR_JSON_DOC_OK, comfoair_json_check(g_out));
}

void test_the_worst_case_status_document_still_fits_the_servers_scratch_buffer(void) {
  // comfoair_http.c sizes ONE static buffer for every JSON document the server renders --
  // SCRATCH_SIZE, i.e. COMFOAIR_API_DOC_BUFFER_SIZE (48 KiB) -- and answers a renderer that
  // reports 0 with a 500.
  // A member that pushed this document over would take the whole endpoint down, which is a worse
  // failure than the one it was added to fix. Measured rather than reasoned about, the way
  // test_ha.cpp:471 pins the publish buffer.

  comfoair_wire_provision_t s = connected_status();
  // Every string at its maximum, and non-ASCII: comfoair_json_escape turns each such character
  // into a six-byte \uXXXX, and an SSID is chosen by whoever set up the router next door.
  for (int i = 0; i < 16; i++)
    memcpy(s.ssid + i * 2, "\xd0\xaf", 2);  // CYRILLIC CAPITAL LETTER YA
  s.ssid[32] = '\0';
  TEST_ASSERT_EQUAL_size_t(32, strlen(s.ssid));
  memset(s.ap_ssid, 'a', sizeof s.ap_ssid - 1);
  s.ap_ssid[sizeof s.ap_ssid - 1] = '\0';
  snprintf(s.ip, sizeof s.ip, "%s", "255.255.255.255");
  // Not COMFOAIR_PROV_NEVER: that renders as the four bytes "null", so the longest duration is
  // the largest number that is not it.
  s.window_remaining_ms = COMFOAIR_PROV_NEVER - 1;
  s.fallback_in_ms      = COMFOAIR_PROV_NEVER - 1;
  s.failure             = COMFOAIR_PROV_FAIL_NETWORK_ABSENT;
  s.failure_reason      = 255;

  comfoair_wire_mqtt_t m = {};
  m.configured = m.connected = true;
  m.published = m.commands = m.rejected = m.reconnects = 4294967295u;

  // The identity block at ITS maximum as well. Added here and not merely to the happy-path test
  // because this is the only test that bounds the document, and three members measured nowhere
  // are three members that can quietly consume the headroom -- which is what this test exists to
  // prevent. device_id is twelve hex characters and mac is seventeen, both fixed; sw_version is
  // esp_app_desc_t.version, which is a 32-byte field.
  char version[32];
  memset(version, 'v', sizeof version - 1);
  version[sizeof version - 1] = '\0';
  const comfoair_wire_device_t d = {"ffffffffffff", "ff:ff:ff:ff:ff:ff", version};

  // Over every refusal spelling rather than a guessed longest one: which of the seven is longest
  // is not this test's business, and a new one added later must not quietly stop being measured.
  size_t largest = 0;
  const uint32_t codes[] = {0, 1, 2, 3, 4, 5, 99};
  for (size_t i = 0; i < sizeof codes / sizeof codes[0]; i++) {
    m.last_connack     = codes[i];
    const size_t bytes = comfoair_wire_render_provision(&s, &m, &d, g_out, sizeof g_out);
    TEST_ASSERT_TRUE_MESSAGE(bytes > 0, comfoair_wire_connack_name(codes[i]));
    if (bytes > largest)
      largest = bytes;
  }
  // Measured at 669 bytes on 2026-09-05. TWO buffers bound this document and the SMALLER one is
  // the live bound: comfoair_http.c's scratch is COMFOAIR_API_DOC_BUFFER_SIZE (48 KiB), but this test renders into g_out, which is
  // 4096 -- so a document that outgrew the server's buffer would have failed the `bytes > 0` check
  // above long before reaching the server buffer size. Asserted against sizeof g_out for that reason: a ceiling the
  // test cannot reach is not a ceiling. The point is not the headroom either way, but that a member
  // added later cannot quietly consume it unnoticed.
  TEST_ASSERT_TRUE_MESSAGE(largest < sizeof g_out,
                           "the status document outgrew this test's render buffer");
  // A floor as well, so a renderer that stopped emitting the block would not pass this by
  // producing a comfortably small document.
  TEST_ASSERT_TRUE_MESSAGE(largest > 500, "the worst case is not being exercised any more");
}


// --- POST /api/entities/<key> ------------------------------------------------------------------

void test_an_entity_write_carries_one_value_of_three_shapes(void) {
  // {"value": ...} is what writeEntity() sends (web/src/api/client.ts), and StateValue is a
  // number, a boolean or a string. Which one it is decides which entity can accept it, so the
  // shape is carried out of here rather than coerced.
  comfoair_command_value_t v = {};
  char text[64];

  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_entity_write("{\"value\":3}", &v, text, sizeof text).status);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_VALUE_NUMBER, v.kind);
  TEST_ASSERT_EQUAL_INT32(3, (int32_t)v.number);

  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_entity_write("{\"value\":true}", &v, text, sizeof text).status);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_VALUE_BOOL, v.kind);
  TEST_ASSERT_TRUE(v.boolean);

  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_entity_write("{\"value\":\"Auto only\"}", &v, text,
                                                     sizeof text).status);
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_VALUE_TEXT, v.kind);
  TEST_ASSERT_EQUAL_STRING("Auto only", v.text);
}

void test_an_entity_write_without_a_value_is_refused(void) {
  // Not defaulted to anything. A write with no value that succeeded would be a request the owner
  // made, the device answered 200 to, and nothing happened for.
  comfoair_command_value_t v = {};
  char text[64];
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD,
                    comfoair_wire_parse_entity_write("{}", &v, text, sizeof text).status);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD,
                    comfoair_wire_parse_entity_write("{\"value\":null}", &v, text, sizeof text).status);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_BODY,
                    comfoair_wire_parse_entity_write("nonsense", &v, text, sizeof text).status);
}

void test_an_option_label_longer_than_any_option_is_refused_not_truncated(void) {
  // A truncated label matches a different option, or none. Refusing is the only answer that
  // cannot silently send the wrong command.
  comfoair_command_value_t v = {};
  char text[8];
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD,
                    comfoair_wire_parse_entity_write("{\"value\":\"Supply Only\"}", &v, text,
                                                     sizeof text).status);
}

// --- POST /api/ops/<name> ----------------------------------------------------------------------

void test_an_operation_body_is_decoded_into_its_parameters(void) {
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  const comfoair_wire_result_t r = comfoair_wire_parse_operation(
      "{\"mode\":\"supply_only\",\"duration\":600}", &p, &storage);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, r.status);
  TEST_ASSERT_EQUAL_STRING("supply_only", p.mode);
  TEST_ASSERT_TRUE(p.has_duration);
  TEST_ASSERT_EQUAL_INT32(600, p.duration_seconds);
}

void test_an_operation_duration_of_zero_is_present_and_not_absent(void) {
  // THE ONE THAT MATTERS. 0 is a legal duration with a meaning -- "cancel" for boost, "forever"
  // for the other three -- so a parser that treated it as "not given" would turn a cancellation
  // into a refusal, and a deliberate "forever" into one too.
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_operation("{\"duration\":0}", &p, &storage).status);
  TEST_ASSERT_TRUE(p.has_duration);
  TEST_ASSERT_EQUAL_INT32(0, p.duration_seconds);
}

void test_a_negative_duration_survives_because_the_builder_decides_what_it_means(void) {
  // Negative folds into the same branch as zero downstream. This layer says "a number"; what it
  // MEANS is comfoair_command's answer, and asking twice is how the two come to disagree.
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_operation("{\"duration\":-1}", &p, &storage).status);
  TEST_ASSERT_TRUE(p.has_duration);
  TEST_ASSERT_EQUAL_INT32(-1, p.duration_seconds);
}

void test_a_fraction_is_refused_and_does_not_become_forever(void) {
  // THE ONE THAT WOULD HAVE SHIPPED. A duration is signed because negative means "until told
  // otherwise", and the obvious way to recover a negative from an unsigned reader is to treat
  // every out-of-range answer as one. That turns {"duration": 18.5} -- a typo -- into -1, which
  // is FOREVER on three of the four timed operations. A fraction is not a duration and is
  // refused; only a genuine negative survives.
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  const comfoair_wire_result_t r =
      comfoair_wire_parse_operation("{\"duration\":18.5}", &p, &storage);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, r.status);
  TEST_ASSERT_EQUAL_STRING("duration", r.field);
  TEST_ASSERT_FALSE(p.has_duration);

  // And a number no int32 can hold is refused too, rather than folding to -1.
  const comfoair_wire_result_t big =
      comfoair_wire_parse_operation("{\"duration\":99999999999}", &p, &storage);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, big.status);
}

void test_a_level_of_zero_is_present_and_is_away_not_off(void) {
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_operation("{\"level\":0}", &p, &storage).status);
  TEST_ASSERT_TRUE(p.has_level);
  TEST_ASSERT_EQUAL_INT32(0, p.level);
}

void test_an_operation_parameter_of_the_wrong_type_names_itself(void) {
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  const comfoair_wire_result_t r =
      comfoair_wire_parse_operation("{\"duration\":\"600\"}", &p, &storage);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, r.status);
  TEST_ASSERT_EQUAL_STRING("duration", r.field);

  const comfoair_wire_result_t e =
      comfoair_wire_parse_operation("{\"enabled\":1}", &p, &storage);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, e.status);
  TEST_ASSERT_EQUAL_STRING("enabled", e.field);
}

void test_an_absent_operation_parameter_stays_absent_rather_than_becoming_a_default(void) {
  // None of the six operations has a default (docs/interfaces/rest-api.md lists them). This layer must not
  // invent one, and comfoair_command refuses what is missing -- for three of the six an invented
  // duration would mean "forever".
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, comfoair_wire_parse_operation("{}", &p, &storage).status);
  TEST_ASSERT_FALSE(p.has_duration);
  TEST_ASSERT_FALSE(p.has_level);
  TEST_ASSERT_FALSE(p.has_enabled);
  TEST_ASSERT_NULL(p.mode);
  TEST_ASSERT_NULL(p.profile);
}

void test_the_whole_journey_from_a_post_body_to_the_bytes_on_the_bus(void) {
  // The join this project keeps getting wrong in the gaps between components. A body the page
  // sends, decoded here, built there, and asserted as the bytes that reach the unit.
  comfoair_command_params_t p = {};
  comfoair_wire_op_storage_t storage = {};
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_operation("{\"mode\":\"exhaust_only\",\"duration\":600}",
                                                  &p, &storage).status);
  comfoair_command_durations_t d;
  comfoair_command_durations_defaults(&d);
  comfoair_command_t c = {};
  TEST_ASSERT_EQUAL(COMFOAIR_COMMAND_OK,
                    comfoair_command_from_op("set_balance_mode", &p, &d, &c));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(2, c.count, "both frames, every time");
  TEST_ASSERT_EQUAL_HEX8(0x84, c.frames[0].bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x15, c.frames[0].bytes[1]);
  TEST_ASSERT_EQUAL_HEX8(0x07, c.frames[0].bytes[2]);
  TEST_ASSERT_EQUAL_HEX8(0x85, c.frames[1].bytes[0]);
  TEST_ASSERT_EQUAL_HEX8(0x06, c.frames[1].bytes[2]);
}


void test_the_manual_mode_setting_round_trips_and_absent_leaves_it_alone(void) {
  // The owner's decision, taken deliberately with the price named, was reachable only with a
  // serial cable until this key existed. Absent must leave it alone: a page that has never heard
  // of it would otherwise turn it off every time it saved the broker.
  comfoair_config_patch_t       patch   = {};
  comfoair_wire_patch_storage_t storage = {};
  bool has = true, value = true;

  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK,
                    comfoair_wire_parse_config("{\"permanent_manual\":false}", &patch, &storage,
                                               &has, &value).status);
  TEST_ASSERT_TRUE(has);
  TEST_ASSERT_FALSE(value);

  has = true;
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_OK, comfoair_wire_parse_config("{\"mqtt_host\":\"b\"}", &patch,
                                                                 &storage, &has, &value).status);
  TEST_ASSERT_FALSE_MESSAGE(has, "absent must not be read as false");

  const comfoair_wire_result_t bad = comfoair_wire_parse_config(
      "{\"permanent_manual\":\"yes\"}", &patch, &storage, &has, &value);
  TEST_ASSERT_EQUAL(COMFOAIR_WIRE_BAD_FIELD, bad.status);
  TEST_ASSERT_EQUAL_STRING("permanent_manual", bad.field);
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_network_and_a_key_are_taken_as_typed);
  RUN_TEST(test_an_empty_key_is_an_open_network_and_not_a_missing_one);
  RUN_TEST(test_an_absent_key_keeps_the_one_already_stored);
  RUN_TEST(test_an_absent_key_with_nothing_stored_is_an_open_network_attempt);
  RUN_TEST(test_the_sentinel_with_a_key_stored_means_keep_it);
  RUN_TEST(test_the_sentinel_is_never_stored_as_a_key);
  RUN_TEST(test_a_missing_network_name_is_refused);
  RUN_TEST(test_a_body_that_is_not_json_is_refused_as_a_body_not_as_a_field);
  RUN_TEST(test_a_key_that_no_radio_could_use_is_refused_before_it_is_stored);
  RUN_TEST(test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_whole);
  RUN_TEST(test_an_over_long_field_is_refused_rather_than_cut_down_to_a_legal_one);
  RUN_TEST(test_no_error_message_from_provisioning_contains_a_submitted_value);

  RUN_TEST(test_a_full_settings_submission_is_decoded);
  RUN_TEST(test_an_absent_field_is_a_null_pointer_and_an_empty_one_is_an_empty_string);
  RUN_TEST(test_an_empty_object_is_a_legitimate_save_that_changes_nothing);
  RUN_TEST(test_a_body_that_is_not_an_object_is_not_read_as_an_empty_save);
  RUN_TEST(test_the_network_cannot_be_set_through_the_settings_route);
  RUN_TEST(test_an_unknown_field_is_ignored_so_a_newer_page_can_still_save);
  RUN_TEST(test_the_derived_fields_are_ignored_rather_than_taken_as_input);
  RUN_TEST(test_a_field_of_the_wrong_type_names_itself);
  RUN_TEST(test_a_port_outside_the_range_is_refused_and_never_wrapped);
  RUN_TEST(test_a_value_too_long_for_the_field_is_refused_rather_than_truncated);
  RUN_TEST(test_a_refused_submission_leaves_no_half_decoded_patch_behind);

  RUN_TEST(test_the_config_document_carries_every_key_the_page_reads);
  RUN_TEST(test_no_stored_secret_appears_in_the_config_document);
  RUN_TEST(test_an_unset_secret_reads_back_as_an_empty_string_not_as_the_sentinel);
  RUN_TEST(test_the_known_good_flag_comes_from_the_machine_and_not_from_the_stored_ssid);
  RUN_TEST(test_a_document_that_does_not_fit_reports_zero_rather_than_a_short_one);
  RUN_TEST(test_a_device_name_with_a_quote_in_it_survives_the_round_trip);

  RUN_TEST(test_the_scan_is_a_bare_array);
  RUN_TEST(test_an_empty_scan_is_an_empty_array_and_not_an_error);
  RUN_TEST(test_duplicate_names_are_not_filtered_by_the_device);
  RUN_TEST(test_a_hidden_network_is_reported_with_an_empty_name);
  RUN_TEST(test_a_scan_that_does_not_fit_reports_zero);
  RUN_TEST(test_an_ssid_with_json_in_it_cannot_break_the_document);

  RUN_TEST(test_the_status_document_says_where_the_device_is);
  RUN_TEST(test_the_status_document_names_the_device_it_came_from);
  RUN_TEST(test_a_status_document_with_no_device_omits_it_rather_than_lying);
  RUN_TEST(test_each_of_the_four_failures_has_its_own_word);
  RUN_TEST(test_every_state_and_mode_has_a_word_and_none_of_them_is_null);
  RUN_TEST(test_every_connack_code_has_its_own_word_and_refused_is_not_bad_password);
  RUN_TEST(test_never_is_rendered_as_null_and_zero_as_zero);
  RUN_TEST(test_no_key_of_any_kind_reaches_the_status_document);
  RUN_TEST(test_a_status_document_that_does_not_fit_reports_zero);
  RUN_TEST(test_the_status_document_carries_the_broker_counters);
  RUN_TEST(test_a_refused_broker_is_visible_without_a_serial_cable);
  RUN_TEST(test_no_broker_block_is_not_a_broker_reporting_zeros);
  RUN_TEST(test_the_worst_case_status_document_still_fits_the_servers_scratch_buffer);
  RUN_TEST(test_an_entity_write_carries_one_value_of_three_shapes);
  RUN_TEST(test_an_entity_write_without_a_value_is_refused);
  RUN_TEST(test_an_option_label_longer_than_any_option_is_refused_not_truncated);
  RUN_TEST(test_an_operation_body_is_decoded_into_its_parameters);
  RUN_TEST(test_an_operation_duration_of_zero_is_present_and_not_absent);
  RUN_TEST(test_a_negative_duration_survives_because_the_builder_decides_what_it_means);
  RUN_TEST(test_a_fraction_is_refused_and_does_not_become_forever);
  RUN_TEST(test_a_level_of_zero_is_present_and_is_away_not_off);
  RUN_TEST(test_an_operation_parameter_of_the_wrong_type_names_itself);
  RUN_TEST(test_an_absent_operation_parameter_stays_absent_rather_than_becoming_a_default);
  RUN_TEST(test_the_whole_journey_from_a_post_body_to_the_bytes_on_the_bus);
  RUN_TEST(test_the_manual_mode_setting_round_trips_and_absent_leaves_it_alone);
  return UNITY_END();
}
