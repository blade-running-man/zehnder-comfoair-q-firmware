// What survives a power cut, and what must never be allowed to.
//
// Every case here is a failure scenario. The happy path of a settings page gets exercised by
// hand on every build; what does not is the power cut between two NVS writes, the 32-character
// SSID that only one household in a hundred has, the corrupt record that locks the owner out
// of their own device, and the OTA that renames a key and sends everybody back to setup.
// Those are the cases below, and they are invariants, not preferences
// (docs/architecture/failure-model.md, docs/device/config-storage.md).
//
// This suite reaches only the PURE half of comfoair_config. comfoair_config_nvs.c is the thin
// layer that opens namespaces and moves bytes; it is device-only on purpose, because a suite
// that tested a fake NVS would be testing the fake. What can be decided without flash is
// decided here, which is nearly all of it.
#include <unity.h>

#include <cstdio>
#include <cstring>

#include "comfoair_config.h"

// A stand-in for PBKDF2, and NOT a key derivation function -- it is fast, it is reversible,
// and it would be a security defect on a device. It exists because mbedtls is on the ESP32 and
// not on this host, and because without an injected hash the record format, the salt handling
// and the answer to a wrong password could not be tested at all. Its one required property is
// that it is a function of all four inputs: every case below turns on one of them changing.
static bool fake_kdf(const char *password, const uint8_t *salt, size_t salt_len,
                     uint32_t iterations, uint8_t *out, size_t out_len) {
  uint32_t h = 2166136261u ^ iterations;
  for (const char *p = password; *p != '\0'; p++)
    h = (h ^ (uint8_t)*p) * 16777619u;
  for (size_t i = 0; i < salt_len; i++)
    h = (h ^ salt[i]) * 16777619u;
  for (size_t i = 0; i < out_len; i++) {
    h = (h ^ (uint32_t)i) * 16777619u;
    out[i] = (uint8_t)(h >> 24);
  }
  return true;
}

// A hardware random number generator that has not started yet, or an mbedtls call that ran out
// of memory. The point is that this returns false rather than half a digest.
static bool broken_kdf(const char *, const uint8_t *, size_t, uint32_t, uint8_t *, size_t) {
  return false;
}

static const uint8_t SALT_A[COMFOAIR_CONFIG_SALT_LEN] = {1, 2,  3,  4,  5,  6,  7,  8,
                                                         9, 10, 11, 12, 13, 14, 15, 16};
static const uint8_t SALT_B[COMFOAIR_CONFIG_SALT_LEN] = {99, 2,  3,  4,  5,  6,  7,  8,
                                                         9,  10, 11, 12, 13, 14, 15, 16};

static comfoair_config_hash_ctx_t hash_with(const uint8_t *salt, comfoair_config_kdf_t kdf) {
  comfoair_config_hash_ctx_t ctx{};
  ctx.kdf        = kdf;
  ctx.salt       = salt;
  ctx.iterations = 0;  // 0 means the compiled-in count; see COMFOAIR_CONFIG_KDF_ITERATIONS
  return ctx;
}

// The checkers take a length because an SSID is not a C string where it comes from. Spelling
// the cast at forty call sites would bury what each case is actually about.
static comfoair_config_err_t ssid_check(const char *s) {
  return comfoair_config_check_ssid((const uint8_t *)s, strlen(s));
}
static comfoair_config_err_t psk_check(const char *s) {
  return comfoair_config_check_psk((const uint8_t *)s, strlen(s));
}

static void repeat(char *out, char c, size_t n) {
  memset(out, c, n);
  out[n] = '\0';
}

// A stored password record assembled here rather than by comfoair_config_hash_password(), so a
// case can carry an iteration count the writer refuses to produce. Everything else about it is
// exactly what the writer would have written, which is the point: a record that fails to verify
// for a second reason proves nothing about the first.
static void record_at(uint32_t iterations, const char *password, char *out, size_t cap) {
  uint8_t digest[COMFOAIR_CONFIG_DIGEST_LEN];
  fake_kdf(password, SALT_A, COMFOAIR_CONFIG_SALT_LEN, iterations, digest, sizeof digest);
  char salt_hex[COMFOAIR_CONFIG_SALT_LEN * 2 + 1];
  char digest_hex[COMFOAIR_CONFIG_DIGEST_LEN * 2 + 1];
  for (size_t i = 0; i < COMFOAIR_CONFIG_SALT_LEN; i++)
    snprintf(salt_hex + i * 2, 3, "%02x", SALT_A[i]);
  for (size_t i = 0; i < sizeof digest; i++)
    snprintf(digest_hex + i * 2, 3, "%02x", digest[i]);
  snprintf(out, cap, "1$%u$%s$%s", (unsigned)iterations, salt_hex, digest_hex);
}

// Does this blob contain that plaintext anywhere in it? Used to ask a question no strcmp can:
// "is the password ANYWHERE in the thing we are about to hand a client", including in a field
// nobody thought about when they added it.
static bool contains_bytes(const void *blob, size_t blob_len, const char *needle) {
  const size_t n = strlen(needle);
  if (n == 0 || n > blob_len)
    return false;
  const unsigned char *p = (const unsigned char *)blob;
  for (size_t i = 0; i + n <= blob_len; i++)
    if (memcmp(p + i, needle, n) == 0)
      return true;
  return false;
}

// Can a renderer measure this string without reading past the field it came out of? Asked with a
// BOUNDED memchr and never with strlen(), because strlen() is the very read this is testing for
// -- a test that had to commit the bug to detect it would be reporting its own crash. memchr is
// specified to stop at the first match, so this is defined for a one-byte "" as well as for an
// unterminated 254-byte array (C11 7.24.5.1).
static bool measurable(const char *s, size_t cap) {
  return s != nullptr && memchr(s, '\0', cap) != nullptr;
}

static const char *const DEVICE_ID = "a1b2c3d4e5f6";

static comfoair_config_t fresh(void) {
  comfoair_config_t cfg{};
  comfoair_config_defaults(&cfg, DEVICE_ID);
  return cfg;
}

void setUp(void) {}
void tearDown(void) {}

// --- the names are a contract ------------------------------------------------------------

void test_the_stored_names_are_pinned_because_renaming_one_costs_the_owner_their_network(void) {
  // An OTA that renames a key does not fail: it silently finds nothing under the new name,
  // and the device that was on the owner's network for a year comes up on its own access
  // point asking to be set up. The literals below are the contract. Changing one is allowed;
  // changing one WITHOUT a migration and a schema bump is what this test forbids.
  TEST_ASSERT_EQUAL_STRING("comfoair", COMFOAIR_CONFIG_NS_LEGACY_NAME);
  TEST_ASSERT_EQUAL_STRING("wifi_ssid", COMFOAIR_CONFIG_LEGACY_KEY_SSID);
  TEST_ASSERT_EQUAL_STRING("wifi_psk", COMFOAIR_CONFIG_LEGACY_KEY_PSK);
  TEST_ASSERT_EQUAL_STRING("cfg_wifi", comfoair_config_ns_name(COMFOAIR_CONFIG_NS_WIFI));
  TEST_ASSERT_EQUAL_STRING("cfg_owner", comfoair_config_ns_name(COMFOAIR_CONFIG_NS_OWNER));
  TEST_ASSERT_EQUAL_STRING("cfg_app", comfoair_config_ns_name(COMFOAIR_CONFIG_NS_APP));
  TEST_ASSERT_EQUAL_STRING("comfoair", comfoair_config_ns_name(COMFOAIR_CONFIG_NS_LEGACY));

  TEST_ASSERT_EQUAL_STRING("sta", COMFOAIR_CONFIG_KEY_STA);
  TEST_ASSERT_EQUAL_STRING("sta_good", COMFOAIR_CONFIG_KEY_STA_GOOD);
  TEST_ASSERT_EQUAL_STRING("schema", COMFOAIR_CONFIG_KEY_SCHEMA);
  // The two facts comfoair_prov_boot_t needs back after a power cut. Renaming one of these does not
  // lose a network, it loses a WAY BACK: `win_closed` forgotten means a device offers the first-run
  // fifteen minutes for ever instead of a closed window's five, and `no_addr` forgotten means the
  // station that associates and is never given an address reproduces that state at every boot with
  // no access point to reach it through (comfoair_provision.h, address_never_arrived).
  TEST_ASSERT_EQUAL_STRING("win_closed", COMFOAIR_CONFIG_KEY_WINDOW_CLOSED);
  // Phase 7's four durations and the manual-mode setting. Renaming one costs the owner a
  // preference, not a device -- but it costs it silently, which is why they are pinned too.
  TEST_ASSERT_EQUAL_STRING("dur_boost", COMFOAIR_CONFIG_KEY_BOOST_MINUTES);
  TEST_ASSERT_EQUAL_STRING("dur_bypass", COMFOAIR_CONFIG_KEY_BYPASS_MINUTES);
  TEST_ASSERT_EQUAL_STRING("dur_balance", COMFOAIR_CONFIG_KEY_BALANCE_MINUTES);
  TEST_ASSERT_EQUAL_STRING("dur_away", COMFOAIR_CONFIG_KEY_AWAY_MINUTES);
  TEST_ASSERT_EQUAL_STRING("manual_perm", COMFOAIR_CONFIG_KEY_PERMANENT_MANUAL);
  TEST_ASSERT_EQUAL_STRING("no_addr", COMFOAIR_CONFIG_KEY_NO_ADDRESS);

  struct {
    comfoair_config_field_t field;
    const char             *name;
    const char             *key;
  } const expected[] = {
      {COMFOAIR_CONFIG_F_WIFI_SSID, "wifi_ssid", "sta"},
      {COMFOAIR_CONFIG_F_WIFI_PSK, "wifi_psk", "sta"},
      {COMFOAIR_CONFIG_F_MQTT_HOST, "mqtt_host", "mqtt_host"},
      {COMFOAIR_CONFIG_F_MQTT_PORT, "mqtt_port", "mqtt_port"},
      {COMFOAIR_CONFIG_F_MQTT_USER, "mqtt_user", "mqtt_user"},
      {COMFOAIR_CONFIG_F_MQTT_PASSWORD, "mqtt_password", "mqtt_password"},
      {COMFOAIR_CONFIG_F_TOPIC_PREFIX, "topic_prefix", "topic_prefix"},
      {COMFOAIR_CONFIG_F_HA_DISCOVERY, "ha_discovery", "ha_discovery"},
      {COMFOAIR_CONFIG_F_DEVICE_NAME, "device_name", "device_name"},
      // The one row where the stored thing is not the named thing: what is kept is a one-way
      // record, and calling its key `ui_password` would invite a reader to believe otherwise.
      {COMFOAIR_CONFIG_F_UI_PASSWORD, "ui_password", "ui_pw_hash"},
  };
  TEST_ASSERT_EQUAL_size_t(COMFOAIR_CONFIG_F_COUNT, sizeof expected / sizeof expected[0]);

  for (size_t i = 0; i < sizeof expected / sizeof expected[0]; i++) {
    const comfoair_config_field_info_t *info = comfoair_config_field(expected[i].field);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_EQUAL_STRING(expected[i].name, info->name);
    TEST_ASSERT_EQUAL_STRING(expected[i].key, info->key);
  }
}

void test_every_stored_name_is_short_enough_for_nvs_to_accept_it(void) {
  // NVS_KEY_NAME_MAX_SIZE is 16 INCLUDING the terminator (nvs.h:61) and namespaces share that
  // limit (nvs.h:62). A 16-character name is not truncated -- nvs_set_* returns
  // ESP_ERR_NVS_KEY_TOO_LONG -- and since nothing in this component may ESP_ERROR_CHECK a
  // store result, the write would fail silently and the field would simply never persist.
  // Asked over the whole table so a field added later cannot slip past a per-call-site check.
  for (int i = 0; i < COMFOAIR_CONFIG_NS_COUNT; i++) {
    const char *ns = comfoair_config_ns_name((comfoair_config_ns_t)i);
    TEST_ASSERT_NOT_NULL(ns);
    TEST_ASSERT_TRUE_MESSAGE(strlen(ns) <= COMFOAIR_CONFIG_NVS_NAME_MAX, ns);
    TEST_ASSERT_TRUE_MESSAGE(strlen(ns) > 0, "an empty namespace name is refused by nvs_open");
  }
  for (int i = 0; i < COMFOAIR_CONFIG_F_COUNT; i++) {
    const comfoair_config_field_info_t *info = comfoair_config_field((comfoair_config_field_t)i);
    TEST_ASSERT_NOT_NULL(info);
    TEST_ASSERT_TRUE_MESSAGE(strlen(info->key) <= COMFOAIR_CONFIG_NVS_NAME_MAX, info->key);
    TEST_ASSERT_TRUE_MESSAGE(strlen(info->key) > 0, "an empty key is refused by nvs_set_*");
  }
  TEST_ASSERT_TRUE(strlen(COMFOAIR_CONFIG_KEY_STA) <= COMFOAIR_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(COMFOAIR_CONFIG_KEY_STA_GOOD) <= COMFOAIR_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(COMFOAIR_CONFIG_KEY_SCHEMA) <= COMFOAIR_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(COMFOAIR_CONFIG_KEY_WINDOW_CLOSED) <= COMFOAIR_CONFIG_NVS_NAME_MAX);
  TEST_ASSERT_TRUE(strlen(COMFOAIR_CONFIG_KEY_NO_ADDRESS) <= COMFOAIR_CONFIG_NVS_NAME_MAX);
  const char *const phase7[] = {
      COMFOAIR_CONFIG_KEY_BOOST_MINUTES,  COMFOAIR_CONFIG_KEY_BYPASS_MINUTES,
      COMFOAIR_CONFIG_KEY_BALANCE_MINUTES, COMFOAIR_CONFIG_KEY_AWAY_MINUTES,
      COMFOAIR_CONFIG_KEY_PERMANENT_MANUAL};
  for (size_t i = 0; i < sizeof phase7 / sizeof phase7[0]; i++)
    TEST_ASSERT_TRUE_MESSAGE(strlen(phase7[i]) <= COMFOAIR_CONFIG_NVS_NAME_MAX, phase7[i]);
  // And none of them collides with a key already in cfg_app -- a duration overwriting the broker
  // host is not a compile error, it is a device that stops reaching its broker.
  const char *const app_keys[] = {"mqtt_host", "mqtt_port", "mqtt_user", "mqtt_password",
                                  "topic_prefix", "ha_discovery", "device_name",
                                  COMFOAIR_CONFIG_KEY_SCHEMA};
  for (size_t i = 0; i < sizeof phase7 / sizeof phase7[0]; i++)
    for (size_t j = 0; j < sizeof app_keys / sizeof app_keys[0]; j++)
      TEST_ASSERT_TRUE_MESSAGE(strcmp(phase7[i], app_keys[j]) != 0, phase7[i]);
}

void test_the_provisioning_flags_live_where_forgetting_the_network_forgets_them_too(void) {
  // They are in the Wi-Fi namespace, which is the one the SOFT reset erases. That is the right tier
  // and not an accident of filing: the soft gesture means "forget this network", and a device that
  // has forgotten its network is a device on its first run again -- it should get the first-run
  // fifteen minutes, not the five that `win_closed` would still be asking for. Filing them under
  // cfg_app instead would leave a factory-reset device with a five-minute window and a no-address
  // escape armed against a network it no longer has.
  TEST_ASSERT_TRUE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT,
                                                COMFOAIR_CONFIG_NS_WIFI));
  TEST_ASSERT_TRUE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_HARD,
                                                COMFOAIR_CONFIG_NS_WIFI));
}

void test_no_two_things_are_stored_under_one_name_in_the_wifi_namespace(void) {
  // Four keys share cfg_wifi now, and a collision between two of them is not a compile error --
  // it is a boolean overwriting half a credential, discovered on somebody's kitchen counter.
  const char *const keys[] = {
      COMFOAIR_CONFIG_KEY_STA,
      COMFOAIR_CONFIG_KEY_STA_GOOD,
      COMFOAIR_CONFIG_KEY_WINDOW_CLOSED,
      COMFOAIR_CONFIG_KEY_NO_ADDRESS,
  };
  for (size_t i = 0; i < sizeof keys / sizeof keys[0]; i++)
    for (size_t j = i + 1; j < sizeof keys / sizeof keys[0]; j++)
      TEST_ASSERT_TRUE_MESSAGE(strcmp(keys[i], keys[j]) != 0, keys[i]);
}

void test_the_wifi_pair_is_one_stored_record_and_the_table_says_so(void) {
  // The invariant, expressed as storage rather than as a comment: two fields, ONE key. NVS
  // gives no atomicity between keys, so a power cut between two writes leaves a new SSID with
  // an old password -- indistinguishable from a typo, and it strands the device just as
  // thoroughly. Splitting them "for symmetry with the API" would pass
  // every other test in this file and reintroduce that failure whole.
  const comfoair_config_field_info_t *ssid = comfoair_config_field(COMFOAIR_CONFIG_F_WIFI_SSID);
  const comfoair_config_field_info_t *psk  = comfoair_config_field(COMFOAIR_CONFIG_F_WIFI_PSK);
  TEST_ASSERT_EQUAL_STRING_MESSAGE(ssid->key, psk->key,
                                   "the SSID and the PSK are stored under two keys again");
  TEST_ASSERT_EQUAL_STRING(COMFOAIR_CONFIG_KEY_STA, ssid->key);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_NS_WIFI, ssid->ns);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_NS_WIFI, psk->ns);
}

void test_a_field_that_is_a_secret_is_one_that_comfoair_secrets_also_calls_a_secret(void) {
  // Two independent judgements about the same field, and they must agree. comfoair_secrets
  // redacts by the WORDS in a key, not by a list of key names, so a field named `mqtt_pw`
  // would look perfectly sensible in the table here and be published in full by every
  // projection. The disagreement is the bug; this is where it is caught.
  for (int i = 0; i < COMFOAIR_CONFIG_F_COUNT; i++) {
    const comfoair_config_field_info_t *info = comfoair_config_field((comfoair_config_field_t)i);
    TEST_ASSERT_EQUAL_MESSAGE(info->secret, comfoair_secret_key(info->name), info->name);
  }
  TEST_ASSERT_TRUE(comfoair_config_field(COMFOAIR_CONFIG_F_WIFI_PSK)->secret);
  TEST_ASSERT_TRUE(comfoair_config_field(COMFOAIR_CONFIG_F_MQTT_PASSWORD)->secret);
  TEST_ASSERT_TRUE(comfoair_config_field(COMFOAIR_CONFIG_F_UI_PASSWORD)->secret);
  TEST_ASSERT_FALSE(comfoair_config_field(COMFOAIR_CONFIG_F_WIFI_SSID)->secret);
  TEST_ASSERT_FALSE(comfoair_config_field(COMFOAIR_CONFIG_F_MQTT_USER)->secret);
}

void test_an_unknown_field_is_null_rather_than_a_crash(void) {
  // Field ids arrive from a URL. A caller that gets the entity list wrong should get a 404,
  // not a reboot of the ventilation.
  TEST_ASSERT_NULL(comfoair_config_field(COMFOAIR_CONFIG_F_COUNT));
  TEST_ASSERT_NULL(comfoair_config_field((comfoair_config_field_t)-1));
  TEST_ASSERT_NULL(comfoair_config_ns_name(COMFOAIR_CONFIG_NS_COUNT));
  TEST_ASSERT_NULL(comfoair_config_ns_name((comfoair_config_ns_t)-1));
}

// --- the two-tier reset (docs/device/button.md) --------------------------------------------------

void test_the_soft_reset_clears_the_network_and_the_password_and_keeps_the_broker(void) {
  // Five seconds AFTER boot. The UI password goes with the Wi-Fi deliberately: otherwise a
  // forgotten password on a device that is visible on the network and completely unusable can only
  // be fixed with a USB cable, and the device is behind the front panel of a running ventilation
  // unit.
  TEST_ASSERT_TRUE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT, COMFOAIR_CONFIG_NS_WIFI));
  TEST_ASSERT_TRUE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT, COMFOAIR_CONFIG_NS_OWNER));
  TEST_ASSERT_FALSE_MESSAGE(
      comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT, COMFOAIR_CONFIG_NS_APP),
      "the soft gesture threw away the broker, which it must keep");
}

void test_the_soft_reset_also_erases_what_the_previous_firmware_stored(void) {
  // An earlier comfoair_net wrote the credentials into its own namespace before this component
  // existed. A soft reset that does not take it leaves the old network in flash, the
  // migration finds it at the next boot, and "Forget Wi-Fi" un-forgets itself.
  TEST_ASSERT_TRUE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT, COMFOAIR_CONFIG_NS_LEGACY));
}

void test_the_hard_reset_erases_every_namespace_that_exists_now_or_later(void) {
  // Asked over the enum, not over a hand-written list, and that is the whole point. A key list
  // goes stale the first time a field is added -- and it can never name a key that an OLDER
  // firmware wrote and this build has not heard of. The named failure is a factory reset that
  // leaves the previous owner's broker password in the flash of a device that has been sold.
  for (int i = 0; i < COMFOAIR_CONFIG_NS_COUNT; i++)
    TEST_ASSERT_TRUE_MESSAGE(
        comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_HARD, (comfoair_config_ns_t)i),
        comfoair_config_ns_name((comfoair_config_ns_t)i));
}

void test_every_field_lands_on_the_side_of_the_soft_reset_that_d6_puts_it_on(void) {
  // The tier is carried by the NAMESPACE, so a field added to the wrong one changes what a
  // reset does without anyone touching the reset code. This is the test that notices.
  for (int i = 0; i < COMFOAIR_CONFIG_F_COUNT; i++) {
    const comfoair_config_field_t       f    = (comfoair_config_field_t)i;
    const comfoair_config_field_info_t *info = comfoair_config_field(f);
    const bool soft_clears = comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT, info->ns);
    const bool should_clear = (f == COMFOAIR_CONFIG_F_WIFI_SSID || f == COMFOAIR_CONFIG_F_WIFI_PSK ||
                               f == COMFOAIR_CONFIG_F_UI_PASSWORD);
    TEST_ASSERT_EQUAL_MESSAGE(should_clear, soft_clears, info->name);
  }
}

void test_an_unknown_namespace_is_erased_by_neither_tier(void) {
  TEST_ASSERT_FALSE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_HARD, COMFOAIR_CONFIG_NS_COUNT));
  TEST_ASSERT_FALSE(comfoair_config_reset_erases(COMFOAIR_CONFIG_RESET_SOFT, (comfoair_config_ns_t)-1));
}

// --- validation on the way in -------------------------------------------------------------

void test_a_thirty_two_character_ssid_is_legal_and_a_thirty_three_character_one_is_not(void) {
  // 802.11 caps an SSID at 32 octets, and wifi_sta_config_t.ssid is exactly 32 with no length
  // field beside it (esp_wifi_types_generic.h:559-593). So 32 is the legal maximum AND the
  // buffer, and the obvious `sizeof dst - 1` truncates a real network's name to 31 characters,
  // after which the device never associates and nothing in the symptom says why.
  char s[64];
  repeat(s, 'x', 32);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, ssid_check(s));
  repeat(s, 'x', 33);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_SSID, ssid_check(s));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, ssid_check("a"));
}

void test_an_empty_ssid_is_not_a_network(void) {
  // A `Forget Wi-Fi` that stores a zeroed blob leaves a device that spends a boot trying to
  // associate with "". An empty SSID is refused here so that state cannot be created at all.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_SSID, ssid_check(""));
}

void test_an_ssid_with_a_nul_in_it_is_refused_because_the_radio_cannot_express_it(void) {
  // wifi_sta_config_t has no ssid_len (esp_wifi_types_generic.h:559-593), so a name shorter
  // than 32 bytes is terminated -- which means an embedded NUL truncates the SSID at the
  // driver, and the device associates with a different network name than the one stored.
  // Refused here rather than discovered on a roof.
  const uint8_t with_nul[] = {'M', 'y', 0x00, 'N', 'e', 't'};
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_SSID, comfoair_config_check_ssid(with_nul, sizeof with_nul));
}

void test_a_sixty_four_character_psk_is_legal_and_must_be_hexadecimal(void) {
  // 64 characters is not a long passphrase, it is the 256-bit PSK written as hex, and
  // wpa_supplicant treats it that way by LENGTH ALONE: strlen(password) == 64 goes to
  // hexstr2bin, anything else to pbkdf2_sha1 (rsn_supp/wpa.c:2517-2524). When hexstr2bin
  // fails, that function simply returns -- no PMK is ever computed, no error surfaces, and
  // the device just never associates. A 64-character non-hex value is therefore refused on
  // the way in, because on the way out it is silence.
  char s[80];
  repeat(s, 'a', 64);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, psk_check(s));
  repeat(s, 'z', 64);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_CONFIG_ERR_PSK, psk_check(s),
                            "a 64-character non-hex key was stored; it can never associate");
  repeat(s, 'z', 63);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_CONFIG_OK, psk_check(s),
                            "63 characters is a passphrase, and any character is legal in one");
  repeat(s, 'a', 65);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PSK, psk_check(s));
}

void test_a_passphrase_shorter_than_eight_characters_is_not_one(void) {
  // WPA2 sets the floor at 8. esp_wifi accepts a shorter one and then never associates, which
  // reads to the owner exactly like a wrong password on a good network.
  char s[16];
  repeat(s, 'x', 7);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PSK, psk_check(s));
  repeat(s, 'x', 8);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, psk_check(s));
}

void test_an_open_network_has_no_key_and_that_is_not_an_error(void) {
  // A guest network with no password is a network. Refusing it here would make the device
  // impossible to put on one.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, psk_check(""));
}

void test_a_psk_with_a_nul_in_it_is_refused(void) {
  // Same reason as the SSID, and worse: wpa.c:2517 measures the password with strlen(), so a
  // 64-byte key with a NUL at byte 10 is not even taken as hex -- it is run through
  // pbkdf2_sha1 as a 10-character passphrase.
  const uint8_t with_nul[] = {'s', 'e', 'c', 'r', 'e', 't', 0x00, 'x', 'y', 'z'};
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PSK, comfoair_config_check_psk(with_nul, sizeof with_nul));
}

void test_a_port_outside_the_range_is_refused_rather_than_wrapped(void) {
  // The checker takes a uint32_t on purpose. A JSON body carrying 70000 into a uint16_t
  // becomes 4464, and the device then quietly talks to a port the owner never typed --
  // wrapping is how "validated" turns into "accepted something else".
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_port(1883));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_port(1));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_port(65535));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PORT, comfoair_config_check_port(0));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PORT, comfoair_config_check_port(65536));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_CONFIG_ERR_PORT, comfoair_config_check_port(70000),
                            "70000 was accepted; stored as a uint16_t it is port 4464");
}

void test_a_broker_host_is_a_host_and_not_a_url(void) {
  // The port is its own field, so a scheme in this one is a configuration the device can never
  // act on: it resolves nothing and connects to nothing. Refusing it at the form is a sentence
  // the owner can act on; accepting it is a support request that starts "it just says
  // disconnected".
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host("mqtt.lan"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host("192.168.1.10"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host("fd00::1"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host("home-assistant_1.local"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_HOST, comfoair_config_check_host("mqtt://broker.lan"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_HOST, comfoair_config_check_host("broker lan"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_HOST, comfoair_config_check_host("broker\n.lan"));
}

void test_no_broker_is_a_supported_configuration(void) {
  // The device ventilates a house whether or not anyone runs a broker, and AGENTS.md's rule
  // that nothing reboots because a peer is absent starts here: an empty host must be storable,
  // or "I do not use MQTT" is not expressible.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host(""));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_user(""));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_broker_password(""));
}

void test_a_host_longer_than_dns_allows_is_refused(void) {
  // RFC 1035 2.3.4. The cap is the protocol's, not a round number: a smaller one would refuse
  // a name the owner's resolver answers, and that is a bug report nobody can act on.
  char s[512];
  repeat(s, 'a', COMFOAIR_CONFIG_HOST_MAX);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host(s));
  repeat(s, 'a', COMFOAIR_CONFIG_HOST_MAX + 1);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_HOST, comfoair_config_check_host(s));
}

void test_credentials_longer_than_the_buffer_are_refused_not_truncated(void) {
  // Truncation stores a password that is not the one the owner typed, and the broker then
  // refuses a credential the owner can see is correct on their screen.
  char s[512];
  repeat(s, 'u', COMFOAIR_CONFIG_USER_MAX);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_user(s));
  repeat(s, 'u', COMFOAIR_CONFIG_USER_MAX + 1);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_USER, comfoair_config_check_user(s));
  repeat(s, 'p', COMFOAIR_CONFIG_BROKER_PASS_MAX);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_broker_password(s));
  repeat(s, 'p', COMFOAIR_CONFIG_BROKER_PASS_MAX + 1);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_BROKER_PASSWORD, comfoair_config_check_broker_password(s));
}

void test_a_topic_prefix_may_not_contain_a_wildcard_or_start_at_the_brokers_own_tree(void) {
  // MQTT 3.1.1 4.7.1: `+` and `#` are subscription syntax and are not legal in a topic being
  // published to. A broker's answer to one is to drop the connection, which reads here as a
  // broker that keeps disconnecting for no reason. `$SYS` is the broker's own tree: publishing
  // into it is refused by every broker worth using and accepted quietly by the rest.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_prefix("comfoair/a1b2c3d4e5f6"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("comfoair/+"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("comfoair/#"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("$SYS/comfoair"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("comfo air"));
}

void test_a_topic_prefix_with_an_empty_level_is_refused(void) {
  // Topics are built as prefix + "/" + entity, so a trailing slash makes `comfoair//state`.
  // That is a legal MQTT topic with an empty level in it, which is the worst kind of wrong: it
  // works, it is subscribable, and it does not match the topic anybody typed by hand.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("comfoair/"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("/comfoair"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix("comfoair//q"));
}

void test_a_device_without_a_topic_prefix_or_a_name_is_refused(void) {
  // Both are cleared to nothing by an empty submission under the general rule, and for these
  // two that rule has to lose: an empty prefix publishes to topics that start with a slash,
  // and an empty name is a nameless device in Home Assistant. The default is restored by
  // comfoair_config_sanitize(), which knows the device id; comfoair_config_apply() does not,
  // so it refuses instead of inventing one.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PREFIX, comfoair_config_check_prefix(""));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NAME, comfoair_config_check_name(""));
}

void test_a_device_name_is_bounded_and_printable(void) {
  char s[64];
  repeat(s, 'n', COMFOAIR_CONFIG_NAME_MAX);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_name(s));
  repeat(s, 'n', COMFOAIR_CONFIG_NAME_MAX + 1);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NAME, comfoair_config_check_name(s));
  // A pasted newline is the realistic case, and it ends up in a log line and an MQTT payload.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NAME, comfoair_config_check_name("Comfo\nAir"));
  // Non-ASCII is a name, not an error: the renderer escapes it (comfoair_api_escape_json).
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_name("Вентиляция"));
}

void test_a_ui_password_has_a_floor_because_the_kdf_is_the_only_thing_slowing_a_guess(void) {
  // The device answers as fast as the LAN allows and there is no lockout -- a lockout on a
  // device you cannot physically reach is a way to lose it. What stands between a guesser and
  // the device is the cost of one derivation, so the search space has to carry its own weight.
  char s[256];
  repeat(s, 'p', COMFOAIR_CONFIG_UI_PASS_MIN - 1);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_UI_PASSWORD, comfoair_config_check_ui_password(s));
  repeat(s, 'p', COMFOAIR_CONFIG_UI_PASS_MIN);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_ui_password(s));
  repeat(s, 'p', COMFOAIR_CONFIG_UI_PASS_MAX);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_ui_password(s));
  repeat(s, 'p', COMFOAIR_CONFIG_UI_PASS_MAX + 1);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_UI_PASSWORD, comfoair_config_check_ui_password(s));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_UI_PASSWORD, comfoair_config_check_ui_password("pass\tword"));
}

void test_every_error_has_a_sentence_and_none_of_them_quotes_the_value(void) {
  // These strings go into an HTTP body AND into the log ring, and /api/log is readable by
  // whoever can reach the device. An error that echoes the rejected value
  // publishes a password the moment somebody mistypes one into the wrong field.
  for (int i = COMFOAIR_CONFIG_OK; i <= COMFOAIR_CONFIG_ERR_NO_DOCUMENT; i++) {
    const char *msg = comfoair_config_strerror((comfoair_config_err_t)i);
    TEST_ASSERT_NOT_NULL(msg);
    TEST_ASSERT_TRUE(strlen(msg) > 0);
  }
  TEST_ASSERT_NOT_NULL(comfoair_config_strerror((comfoair_config_err_t)999));

  // The second half of the name, which used to go untested: real values are pushed through
  // apply() and the sentence it produced is searched for them. Asserting only that a message
  // exists says nothing about the property that matters, and the property that matters is the
  // one that publishes somebody's Wi-Fi key.
  comfoair_config_t cfg = fresh();
  const comfoair_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);

  char long_host[512];
  repeat(long_host, 'h', COMFOAIR_CONFIG_HOST_MAX + 1);
  comfoair_config_patch_t host{};
  host.mqtt_host                 = long_host;
  const comfoair_config_err_t eh = comfoair_config_apply(&cfg, &host, &hash);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_HOST, eh);
  TEST_ASSERT_NULL_MESSAGE(strstr(comfoair_config_strerror(eh), "hhhhhhhh"),
                           "the message quotes the address it rejected");

  // The realistic one: a password typed into a field it does not belong in. `hunter2` is
  // refused for being seven characters long, and the refusal is read by anyone with /api/log.
  comfoair_config_patch_t ui{};
  ui.ui_password                 = "hunter2";
  const comfoair_config_err_t eu = comfoair_config_apply(&cfg, &ui, &hash);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_UI_PASSWORD, eu);
  TEST_ASSERT_NULL_MESSAGE(strstr(comfoair_config_strerror(eu), "hunter2"),
                           "the message quotes the password it rejected");

  // And the one that costs the most: the network key, which arrives over an OPEN access point
  // in the first place and must not then be written into a public log ring as well.
  comfoair_config_patch_t net{};
  net.wifi_ssid                  = "HomeNet";
  net.wifi_psk                   = "wpa2key";
  const comfoair_config_err_t en = comfoair_config_apply(&cfg, &net, &hash);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PSK, en);
  TEST_ASSERT_NULL_MESSAGE(strstr(comfoair_config_strerror(en), "wpa2key"),
                           "the message quotes the network key it rejected");
  TEST_ASSERT_NULL(strstr(comfoair_config_strerror(en), "HomeNet"));
}

// --- defaults ------------------------------------------------------------------------------

void test_a_fresh_device_has_no_network_no_password_and_a_broker_that_is_not_set(void) {
  comfoair_config_t cfg = fresh();
  TEST_ASSERT_EQUAL(0, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL(0, cfg.wifi.psk_len);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_password);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_config_password_set(&cfg),
                            "a device out of the box demanded a password nobody has set");
  TEST_ASSERT_FALSE(cfg.read_only);
  TEST_ASSERT_EQUAL_UINT16(1883, cfg.mqtt_port);
}

void test_the_default_topic_prefix_carries_the_device_id_so_two_units_do_not_collide(void) {
  // The shape is `comfoair/<device>` (docs/interfaces/mqtt.md, Topic tree). What goes in it
  // (docs/architecture/security.md, Identity): the identity is the full MAC and never the name,
  // because a rename that re-keys the topics leaves the old retained discovery messages in the
  // broker for ever and the owner sees two devices, one of them dead and undeletable.
  comfoair_config_t cfg = fresh();
  TEST_ASSERT_EQUAL_STRING("comfoair/a1b2c3d4e5f6", cfg.topic_prefix);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_prefix(cfg.topic_prefix));
}

void test_a_device_that_cannot_read_its_own_mac_still_boots(void) {
  // A worse default -- two devices in one house would share a topic tree -- and still better
  // than refusing to start over it. What must not happen is "comfoair/(null)" or a prefix that
  // fails the very validator this component publishes.
  comfoair_config_t cfg{};
  comfoair_config_defaults(&cfg, nullptr);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_prefix(cfg.topic_prefix));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_name(cfg.device_name));
  TEST_ASSERT_FALSE(contains_bytes(cfg.topic_prefix, strlen(cfg.topic_prefix), "null"));

  comfoair_config_defaults(&cfg, "not-hex");
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_prefix(cfg.topic_prefix));
  TEST_ASSERT_FALSE(contains_bytes(cfg.topic_prefix, strlen(cfg.topic_prefix), "not-hex"));
}

void test_the_default_name_matches_the_access_point_the_owner_saw_on_their_phone(void) {
  // comfoair_net_start.c names the access point after the last two MAC bytes, so a device the owner
  // joined as `comfoair-e5f6` announces itself in Home Assistant with the same four digits.
  // The name is display only -- this is about the owner recognising their own device
  // among two, not about identity.
  comfoair_config_t cfg = fresh();
  TEST_ASSERT_TRUE(contains_bytes(cfg.device_name, strlen(cfg.device_name), "e5f6"));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_name(cfg.device_name));
}

// --- the projection: what a client is allowed to see -----------------------------------------

// The projection's layout as this test understands it. It exists to FAIL: a field added to
// comfoair_config_public_t and not to this list changes one size and not the other, and the
// assertion below sends whoever added it back here to say what the new field may hold. Without
// it, "including the field added after the test was written" is a claim nothing enforces --
// which is what a counter-review found the previous version of this test quietly asserting.
struct projection_as_this_test_knows_it {
  char        ssid[COMFOAIR_CONFIG_SSID_MAX + 1];
  const char *psk;
  const char *mqtt_host;
  uint16_t    mqtt_port;
  const char *mqtt_user;
  const char *mqtt_password;
  const char *topic_prefix;
  bool        ha_discovery;
  const char *device_name;
  const char *ui_password;
  bool        ui_password_set;
  bool        read_only;
};

// Is this plaintext anywhere a client can reach it? BOTH halves are needed and neither is
// enough. The struct's own bytes catch an ARRAY field added later, like the SSID. Following the
// pointers catches the leak that actually happens -- `const char *api_token = cfg->token;` --
// which a scan of the struct cannot see at all, because what sits in the struct is eight bytes
// of address and the password is at the other end of it.
static bool projection_exposes(const comfoair_config_public_t *pub, const char *needle) {
  const char *const reached[] = {pub->ssid,          pub->psk,          pub->mqtt_host,
                                 pub->mqtt_user,     pub->mqtt_password, pub->topic_prefix,
                                 pub->device_name,   pub->ui_password};
  for (size_t i = 0; i < sizeof reached / sizeof reached[0]; i++)
    if (reached[i] != nullptr && contains_bytes(reached[i], strlen(reached[i]), needle))
      return true;
  return contains_bytes(pub, sizeof *pub, needle);
}

void test_no_stored_secret_appears_anywhere_in_what_a_client_is_handed(void) {
  // One disclosure policy over two storage policies (docs/device/config-storage.md). The broker
  // password is kept recoverably because MQTT needs the plaintext; the UI password is kept as a
  // one-way record; NEITHER is ever returned.
  TEST_ASSERT_EQUAL_size_t_MESSAGE(
      sizeof(struct projection_as_this_test_knows_it), sizeof(comfoair_config_public_t),
      "the projection grew a field; say here what a client may see of it");

  comfoair_config_t cfg = fresh();
  comfoair_config_patch_t p{};
  p.wifi_ssid     = "HomeNet";
  p.wifi_psk      = "the-wifi-password";
  p.mqtt_password = "the-broker-password";
  p.ui_password   = "the-ui-password";
  const comfoair_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, &hash));

  comfoair_config_public_t pub{};
  comfoair_config_project(&cfg, &pub);

  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, "the-wifi-password"),
                            "the Wi-Fi key is in the projection");
  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, "the-broker-password"),
                            "the broker password is in the projection");
  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, "the-ui-password"),
                            "the UI password is in the projection");
  // The stored RECORD is not the password and is still not a client's business: it carries the
  // salt and the digest, which are exactly what an offline guess needs.
  TEST_ASSERT_FALSE_MESSAGE(projection_exposes(&pub, cfg.ui_pw_hash),
                            "the stored password record is in the projection");
  TEST_ASSERT_EQUAL_STRING(COMFOAIR_SECRET_SENTINEL, pub.psk);
  TEST_ASSERT_EQUAL_STRING(COMFOAIR_SECRET_SENTINEL, pub.mqtt_password);
  TEST_ASSERT_EQUAL_STRING(COMFOAIR_SECRET_SENTINEL, pub.ui_password);
  // The SSID is not a secret: the owner has to see which network the device thinks it is on,
  // and it is broadcast in every beacon in the house anyway.
  TEST_ASSERT_EQUAL_STRING("HomeNet", pub.ssid);
}

void test_an_unset_secret_reads_back_empty_so_the_page_can_say_nothing_is_set(void) {
  comfoair_config_t        cfg = fresh();
  comfoair_config_public_t pub{};
  comfoair_config_project(&cfg, &pub);
  TEST_ASSERT_EQUAL_STRING("", pub.psk);
  TEST_ASSERT_EQUAL_STRING("", pub.mqtt_password);
  TEST_ASSERT_EQUAL_STRING("", pub.ui_password);
  TEST_ASSERT_FALSE(pub.ui_password_set);
}

void test_password_set_is_derived_from_the_record_and_is_never_a_key_of_its_own(void) {
  // A flag in its own key can be written while the record is not -- a power cut between two
  // NVS writes -- and the device then demands a password that does not exist. Locked, by
  // nobody, for ever, on a device behind a front panel. So there is no flag: the answer is the
  // record being non-empty, computed in one place that both the projection and the access
  // policy (comfoair_http_policy.h, ctx.password_set) read.
  comfoair_config_t cfg = fresh();
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));

  comfoair_config_patch_t p{};
  p.ui_password                         = "a-good-password";
  const comfoair_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, &hash));
  TEST_ASSERT_TRUE(comfoair_config_password_set(&cfg));

  comfoair_config_public_t pub{};
  comfoair_config_project(&cfg, &pub);
  TEST_ASSERT_TRUE(pub.ui_password_set);

  // And erasing the record is the whole of erasing the password -- there is no second thing to
  // forget to clear, which is what makes the soft reset a complete answer.
  cfg.ui_pw_hash[0] = '\0';
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));
}

void test_a_thirty_two_character_ssid_survives_the_projection_whole(void) {
  // The record is not NUL-terminated and has no room to be. A projection that copies with
  // `sizeof - 1` shows the owner 31 characters of their own network name and, worse, invites
  // the same mistake in the code that hands it to esp_wifi.
  comfoair_config_t cfg = fresh();
  char              s[64];
  repeat(s, 'S', 32);
  comfoair_config_patch_t p{};
  p.wifi_ssid = s;
  p.wifi_psk  = "12345678";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, nullptr));

  comfoair_config_public_t pub{};
  comfoair_config_project(&cfg, &pub);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(32, strlen(pub.ssid), "the SSID lost a character to a terminator");
  TEST_ASSERT_EQUAL_STRING(s, pub.ssid);
}

void test_the_projection_hands_out_nothing_a_renderer_can_run_off_the_end_of(void) {
  // The projection defends the SSID length against a document that has not been sanitized -- its
  // own comment says so -- and then hands out four char arrays as `const char *` for a renderer
  // to strlen(). The same 0xff document that the terminated() DO NOT in comfoair_config.c was
  // measured against reaches the same end through this function: with `read_only` true, so that
  // no byte after mqtt_host is zero, strlen(pub.mqtt_host) reports
  // `READ of size 743, 0 bytes after 840-byte region` under AddressSanitizer -- on the HTTP task,
  // over a heap object, from a pointer a client asked for.
  //
  // Both parts of the trigger are states this component exists to survive: read_only is a store
  // written by a newer build (rollback), and an array with no NUL is a short read or a bit flip.
  comfoair_config_t cfg;
  memset(&cfg, 0xff, sizeof cfg);
  // Real booleans, for the reason given in test_a_document_of_pure_garbage_from_flash: a _Bool
  // holding 0xff is a bit pattern no C program may read.
  cfg.ha_discovery = true;
  cfg.read_only    = true;

  comfoair_config_public_t pub{};
  comfoair_config_project(&cfg, &pub);

  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.mqtt_host, sizeof cfg.mqtt_host), "mqtt_host");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.mqtt_user, sizeof cfg.mqtt_user), "mqtt_user");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.topic_prefix, sizeof cfg.topic_prefix), "topic_prefix");
  TEST_ASSERT_TRUE_MESSAGE(measurable(pub.device_name, sizeof cfg.device_name), "device_name");
  // The other three are asked too, because the property is that EVERY string leaving here can be
  // measured -- not that four of them can. These three reach it another way: the secrets are
  // redacted to published literals, and the SSID is copied under the clamp above.
  TEST_ASSERT_TRUE(measurable(pub.psk, sizeof COMFOAIR_SECRET_SENTINEL));
  TEST_ASSERT_TRUE(measurable(pub.mqtt_password, sizeof COMFOAIR_SECRET_SENTINEL));
  TEST_ASSERT_TRUE(measurable(pub.ui_password, sizeof COMFOAIR_SECRET_SENTINEL));
  TEST_ASSERT_EQUAL_size_t(COMFOAIR_CONFIG_SSID_MAX, strlen(pub.ssid));

  // What is shown for a field the flash could not answer for is NOTHING, never the bytes: an
  // owner reading an empty broker address learns something true, and a truncated one does not.
  TEST_ASSERT_EQUAL_STRING("", pub.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("", pub.device_name);
}

// --- applying a submitted document ------------------------------------------------------------

void test_a_document_with_one_bad_field_changes_nothing_at_all(void) {
  // The owner fixes the port and saves again -- and if the good half of the first attempt had
  // been applied, the second attempt lands on a document that is no longer the one they were
  // looking at. All or nothing, checked by comparing the whole struct.
  comfoair_config_t cfg = fresh();
  comfoair_config_t before;
  memcpy(&before, &cfg, sizeof before);

  comfoair_config_patch_t p{};
  p.mqtt_host     = "broker.lan";
  p.device_name   = "Kitchen";
  p.has_mqtt_port = true;
  p.mqtt_port     = 70000;
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_PORT, comfoair_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before, &cfg, sizeof cfg,
                                   "a rejected document was applied in part");
}

void test_changing_the_network_without_retyping_its_password_is_refused(void) {
  // The one that strands devices. The settings page renders a stored PSK as the sentinel, so a
  // form where only the SSID was edited submits a NEW network with the OLD password attached.
  // Storing that pair is the atomicity failure arriving through the front door instead of
  // through a power cut, and the device leaves the house for good.
  comfoair_config_t       cfg = fresh();
  comfoair_config_patch_t first{};
  first.wifi_ssid = "OldNet";
  first.wifi_psk  = "old-password";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &first, nullptr));

  comfoair_config_patch_t second{};
  second.wifi_ssid = "NewNet";
  second.wifi_psk  = COMFOAIR_SECRET_SENTINEL;
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_CONFIG_ERR_WIFI_PAIR,
                            comfoair_config_apply(&cfg, &second, nullptr),
                            "a new SSID was stored with the previous network's password");
  TEST_ASSERT_EQUAL(6, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL_MEMORY("OldNet", cfg.wifi.ssid, 6);
}

void test_saving_an_unrelated_setting_leaves_the_stored_network_untouched(void) {
  // The other half of the same rule, and the reason it cannot simply refuse every sentinel:
  // the page submits the WHOLE document, so changing the broker address re-submits the SSID
  // unchanged with the PSK as the sentinel. Refusing that would make every other setting
  // unsavable once a network is stored.
  comfoair_config_t       cfg = fresh();
  comfoair_config_patch_t first{};
  first.wifi_ssid = "OldNet";
  first.wifi_psk  = "old-password";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &first, nullptr));

  comfoair_config_patch_t second{};
  second.wifi_ssid = "OldNet";
  second.wifi_psk  = COMFOAIR_SECRET_SENTINEL;
  second.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &second, nullptr));
  TEST_ASSERT_EQUAL(12, cfg.wifi.psk_len);
  TEST_ASSERT_EQUAL_MEMORY("old-password", cfg.wifi.psk, 12);
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
}

void test_half_a_wifi_pair_is_refused_whichever_half_it_is(void) {
  // Neither half can be applied on its own without inventing the other, and the invented one
  // is always the stored one -- which is the exact pairing this component exists to prevent.
  comfoair_config_t cfg = fresh();

  comfoair_config_patch_t ssid_only{};
  ssid_only.wifi_ssid = "SomeNet";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_WIFI_PAIR, comfoair_config_apply(&cfg, &ssid_only, nullptr));

  comfoair_config_patch_t psk_only{};
  psk_only.wifi_psk = "some-password";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_WIFI_PAIR, comfoair_config_apply(&cfg, &psk_only, nullptr));
}

void test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_byte_for_byte(void) {
  // Both extremes are legal, both are what a serious router hands out, and both are what a
  // `- 1` for a terminator quietly destroys. Compared with memcmp against the exact lengths,
  // because strlen over either of these is the bug being guarded against.
  comfoair_config_t cfg = fresh();
  char              ssid[64];
  char              psk[80];
  repeat(ssid, 'S', 32);
  repeat(psk, 'f', 64);

  comfoair_config_patch_t p{};
  p.wifi_ssid = ssid;
  p.wifi_psk  = psk;
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(32, cfg.wifi.ssid_len, "a 32-character SSID lost a byte");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(64, cfg.wifi.psk_len, "a 64-character key lost a byte");
  TEST_ASSERT_EQUAL_MEMORY(ssid, cfg.wifi.ssid, 32);
  TEST_ASSERT_EQUAL_MEMORY(psk, cfg.wifi.psk, 64);
}

void test_forgetting_the_network_clears_both_halves_at_once(void) {
  // The only way the settings page can express "this device is not on a network any more", and
  // it has to clear both halves in the same act for the same reason they are stored in one.
  comfoair_config_t       cfg = fresh();
  comfoair_config_patch_t p{};
  p.wifi_ssid = "OldNet";
  p.wifi_psk  = "old-password";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, nullptr));

  comfoair_config_patch_t forget{};
  forget.wifi_ssid = "";
  forget.wifi_psk  = "";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &forget, nullptr));
  TEST_ASSERT_EQUAL(0, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, cfg.wifi.psk_len,
                                  "the network was forgotten and its password was kept");
}

void test_an_absent_key_keeps_and_an_empty_key_clears(void) {
  // Absent and empty are different, and the whole sentinel scheme in comfoair_secrets exists
  // because they were once the same: one save of an unrelated setting wiped the broker
  // credentials.
  comfoair_config_t       cfg = fresh();
  comfoair_config_patch_t p{};
  p.mqtt_host = "broker.lan";
  p.mqtt_user = "sensor";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, nullptr));

  comfoair_config_patch_t absent{};  // every pointer NULL
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &absent, nullptr));
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("sensor", cfg.mqtt_user);

  comfoair_config_patch_t cleared{};
  cleared.mqtt_user = "";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &cleared, nullptr));
  TEST_ASSERT_EQUAL_STRING("", cfg.mqtt_user);
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
}

void test_the_broker_password_is_stored_so_it_can_be_used_and_the_ui_password_is_not(void) {
  // Two storage policies, one disclosure policy. MQTT authenticates with the plaintext, so a hash
  // there is not a hardening measure, it is a broker that never connects -- which is exactly why a
  // hash was rejected for it.
  comfoair_config_t                cfg  = fresh();
  const comfoair_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  comfoair_config_patch_t          p{};
  p.mqtt_password = "broker-secret";
  p.ui_password   = "ui-secret-value";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &p, &hash));

  TEST_ASSERT_EQUAL_STRING_MESSAGE("broker-secret", cfg.mqtt_password,
                                   "the broker password was mangled; MQTT will not authenticate");
  TEST_ASSERT_FALSE_MESSAGE(contains_bytes(cfg.ui_pw_hash, sizeof cfg.ui_pw_hash, "ui-secret-value"),
                            "the UI password was stored in the clear");
  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(cfg.ui_pw_hash, "ui-secret-value",
                                                             fake_kdf));
}

void test_a_ui_password_with_nowhere_to_hash_it_is_refused_rather_than_stored(void) {
  // The submission is fine and the device cannot honour it. Its own error code because the
  // owner did nothing wrong and retyping will not help -- and because the alternative branch,
  // the one where a missing hash context falls through to storing the plaintext, is a
  // one-line mistake that no other test in this file would catch.
  comfoair_config_t       cfg = fresh();
  comfoair_config_patch_t p{};
  p.ui_password = "a-good-password";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NO_HASH, comfoair_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));

  const comfoair_config_hash_ctx_t broken = hash_with(SALT_A, broken_kdf);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NO_HASH, comfoair_config_apply(&cfg, &p, &broken));
  TEST_ASSERT_FALSE_MESSAGE(comfoair_config_password_set(&cfg),
                            "a failed derivation left something behind that looks like a password");
}

void test_the_published_sentinel_cannot_become_the_password_of_an_unconfigured_device(void) {
  // COMFOAIR_SECRET_SENTINEL is a published string. On a device with nothing stored it cannot
  // be an untouched field, so it is a value being submitted -- and storing it would give every
  // never-configured device the same password, printed in the source.
  comfoair_config_t       cfg = fresh();
  comfoair_config_patch_t p{};
  p.ui_password                         = COMFOAIR_SECRET_SENTINEL;
  const comfoair_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_UI_PASSWORD, comfoair_config_apply(&cfg, &p, &hash));
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));

  comfoair_config_patch_t broker{};
  broker.mqtt_password = COMFOAIR_SECRET_SENTINEL;
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_BROKER_PASSWORD,
                    comfoair_config_apply(&cfg, &broker, nullptr));
}

void test_clearing_the_ui_password_is_allowed_because_the_owner_may_be_locked_out_of_it(void) {
  // The password is optional and the soft reset erases it. An authenticated owner removing it
  // through the API is the same decision reached from the other side; refusing it would mean the
  // only way to undo a password is a physical gesture on a device in a wall.
  comfoair_config_t                cfg  = fresh();
  const comfoair_config_hash_ctx_t hash = hash_with(SALT_A, fake_kdf);
  comfoair_config_patch_t          set{};
  set.ui_password = "a-good-password";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &set, &hash));

  comfoair_config_patch_t clear{};
  clear.ui_password = "";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_apply(&cfg, &clear, &hash));
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));
}

void test_a_store_that_holds_a_newer_schema_refuses_every_write(void) {
  // An OTA rollback lands here: the flash was written by a build that knew more than this
  // one. Writing into it would destroy what the newer build stored, and the owner who rolls
  // forward again finds their settings gone. So the store goes read-only -- the device keeps
  // ventilating, keeps its network, keeps its broker, and says why.
  comfoair_config_t cfg = fresh();
  cfg.read_only         = true;
  comfoair_config_t before;
  memcpy(&before, &cfg, sizeof before);

  comfoair_config_patch_t p{};
  p.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_READ_ONLY, comfoair_config_apply(&cfg, &p, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

void test_a_null_document_is_refused_rather_than_reported_as_saved(void) {
  // A body that failed to parse arrives here as nothing at all, on the HTTP task. It must not
  // crash -- and it must not answer OK either, because OK is what the handler turns into 200 and
  // what comfoair_config_strerror() renders as the word "accepted". The owner would read Saved
  // over a request that stored nothing, close the page, and find the settings gone.
  //
  // A patch with every field absent is a different thing and still succeeds: the page submits
  // the whole document, and "nothing was changed" is a legitimate save. What is refused here is
  // the absence of a document, not the absence of changes.
  comfoair_config_t cfg = fresh();
  comfoair_config_t before;
  memcpy(&before, &cfg, sizeof before);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NO_DOCUMENT, comfoair_config_apply(&cfg, nullptr, nullptr));
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
  TEST_ASSERT_TRUE_MESSAGE(
      strcmp(comfoair_config_strerror(COMFOAIR_CONFIG_OK),
             comfoair_config_strerror(COMFOAIR_CONFIG_ERR_NO_DOCUMENT)) != 0,
      "a request that stored nothing is answered with the sentence for a request that did");

  // The other NULL is a programming error rather than a bad request, and it gets the same
  // answer for the same reason: there is nothing this call could have stored.
  comfoair_config_patch_t p{};
  p.mqtt_host = "broker.lan";
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_ERR_NO_DOCUMENT, comfoair_config_apply(nullptr, &p, nullptr));
}

// --- the password record -----------------------------------------------------------------------

void test_the_stored_record_is_not_the_password_and_carries_its_own_cost(void) {
  // `1$<iterations>$<salt>$<digest>`. The iteration count is IN the record so that raising it
  // later does not invalidate what is already stored -- a device whose owner cannot log in
  // after an update is the same loss as a lost password, arriving by a different road.
  char record[COMFOAIR_CONFIG_HASH_MAX + 1];
  const comfoair_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_TRUE(comfoair_config_hash_password("hunter2-and-more", &ctx, record, sizeof record));

  TEST_ASSERT_FALSE(contains_bytes(record, strlen(record), "hunter2-and-more"));
  TEST_ASSERT_EQUAL('1', record[0]);
  TEST_ASSERT_EQUAL('$', record[1]);
  TEST_ASSERT_TRUE(contains_bytes(record, strlen(record), "$10000$"));
  TEST_ASSERT_TRUE_MESSAGE(strlen(record) <= COMFOAIR_CONFIG_HASH_MAX,
                           "the record does not fit the field it is stored in");
}

void test_the_right_password_verifies_and_a_wrong_one_does_not(void) {
  char record[COMFOAIR_CONFIG_HASH_MAX + 1];
  const comfoair_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_TRUE(comfoair_config_hash_password("correct-horse", &ctx, record, sizeof record));

  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(record, "correct-horse", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against(record, "correct-hors", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against(record, "", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against(record, nullptr, fake_kdf));
}

void test_two_devices_with_the_same_password_do_not_share_a_record(void) {
  // One recovered flash image would otherwise answer for every device whose owner picked the
  // same word. The salt is per stored password, not per device and not per build.
  char a[COMFOAIR_CONFIG_HASH_MAX + 1];
  char b[COMFOAIR_CONFIG_HASH_MAX + 1];
  const comfoair_config_hash_ctx_t ctx_a = hash_with(SALT_A, fake_kdf);
  const comfoair_config_hash_ctx_t ctx_b = hash_with(SALT_B, fake_kdf);
  TEST_ASSERT_TRUE(comfoair_config_hash_password("same-password", &ctx_a, a, sizeof a));
  TEST_ASSERT_TRUE(comfoair_config_hash_password("same-password", &ctx_b, b, sizeof b));
  TEST_ASSERT_TRUE_MESSAGE(strcmp(a, b) != 0, "the salt is not being used");
  // And each still verifies its own, which is what says the salt is carried, not just mixed in.
  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(a, "same-password", fake_kdf));
  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(b, "same-password", fake_kdf));
}

void test_a_record_written_at_another_iteration_count_still_verifies(void) {
  // The reason the count is in the record. A future build that raises it must still let in
  // everyone who set a password under the old one.
  comfoair_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  ctx.iterations                 = 4096;
  char record[COMFOAIR_CONFIG_HASH_MAX + 1];
  TEST_ASSERT_TRUE(comfoair_config_hash_password("old-password", &ctx, record, sizeof record));
  TEST_ASSERT_TRUE(contains_bytes(record, strlen(record), "$4096$"));
  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(record, "old-password", fake_kdf));
}

void test_a_record_that_does_not_parse_lets_nobody_in(void) {
  // Garbage from a bit flip or from a firmware that wrote another format. It must not throw,
  // must not read past the end, and must not accidentally match. What HAPPENS to such a record
  // is decided by sanitize(), not here -- this only says it never authenticates anyone.
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against("", "anything", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against(nullptr, "anything", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against("garbage", "anything", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against("1$", "anything", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against("1$10000$zz$ff", "anything", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against("9$10000$aabb$ccdd", "anything", fake_kdf));
  TEST_ASSERT_FALSE(comfoair_config_check_ui_password_against("1$0$aabb$ccdd", "anything", fake_kdf));
}

void test_an_iteration_count_nobody_could_wait_for_is_not_a_record(void) {
  // The cost is carried IN the record, which is what lets a future build raise it -- and it is
  // therefore a number that arrives from flash, where a bit flip lives. At this file's own figure
  // of 100 ms per 10,000 iterations on a C6, 100,000,000 is about seventeen minutes of PBKDF2
  // per login attempt, run on the HTTP task, in front of an owner who is being asked for a
  // password. There is no lockout to stop them being asked again, so nothing bounds how
  // often that is paid.
  //
  // Built by hand with the RIGHT digest for that count. A record with a wrong digest would be
  // refused for the ordinary reason and would prove nothing about the count -- which is exactly
  // how this test passed before it was looked at twice.
  char record[COMFOAIR_CONFIG_HASH_MAX + 1];
  record_at(COMFOAIR_CONFIG_KDF_ITERATIONS, "a-good-password", record, sizeof record);
  TEST_ASSERT_TRUE_MESSAGE(
      comfoair_config_check_ui_password_against(record, "a-good-password", fake_kdf),
      "the hand-built record does not verify, so nothing below is about the iteration count");

  record_at(100000000u, "a-good-password", record, sizeof record);
  TEST_ASSERT_FALSE_MESSAGE(
      comfoair_config_check_ui_password_against(record, "a-good-password", fake_kdf),
      "a login was allowed to cost seventeen minutes of the HTTP task");

  // And it is not left in flash to be re-parsed at every attempt. sanitize() throws away a
  // record it cannot read, for the reason in test_a_password_record_that_cannot_be_read: the
  // device ends up open on the LAN, which the owner can see and fix.
  comfoair_config_t cfg = fresh();
  memcpy(cfg.ui_pw_hash, record, strlen(record) + 1);
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_UI_PASSWORD));
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));

  // The cap has to sit ABOVE the compiled-in count with room to spare, or raising that count in
  // a later build locks out everyone who set a password under the old one -- the same loss as a
  // forgotten password, arriving by a different road.
  TEST_ASSERT_TRUE(COMFOAIR_CONFIG_KDF_ITERATIONS_MAX > COMFOAIR_CONFIG_KDF_ITERATIONS);

  // Whatever this build can WRITE it must be able to READ BACK. A parser stricter than the
  // writer is a device that stores a password and then refuses it, with nobody to ask; so the
  // count at the cap round-trips, and one past the cap is refused at the WRITER, before anything
  // unreadable reaches the flash.
  comfoair_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  ctx.iterations                 = COMFOAIR_CONFIG_KDF_ITERATIONS_MAX;
  TEST_ASSERT_TRUE(comfoair_config_hash_password("a-good-password", &ctx, record, sizeof record));
  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(record, "a-good-password", fake_kdf));

  char guarded[COMFOAIR_CONFIG_HASH_MAX + 1];
  memset(guarded, 'Z', sizeof guarded);
  ctx.iterations = COMFOAIR_CONFIG_KDF_ITERATIONS_MAX + 1;
  TEST_ASSERT_FALSE_MESSAGE(
      comfoair_config_hash_password("a-good-password", &ctx, guarded, sizeof guarded),
      "a record was written at a count this build's own parser refuses");
  for (size_t i = 0; i < sizeof guarded; i++)
    TEST_ASSERT_EQUAL('Z', guarded[i]);
}

void test_a_record_is_never_written_into_a_buffer_it_does_not_fit(void) {
  char small[8];
  memset(small, 'Z', sizeof small);
  const comfoair_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_FALSE(comfoair_config_hash_password("a-good-password", &ctx, small, sizeof small));
  for (size_t i = 0; i < sizeof small; i++)
    TEST_ASSERT_EQUAL_MESSAGE('Z', small[i], "a failed hash wrote a partial record");
}

// --- what came back out of the flash --------------------------------------------------------

void test_a_good_document_is_left_exactly_as_it_was(void) {
  comfoair_config_t cfg = fresh();
  comfoair_config_t before;
  memcpy(&before, &cfg, sizeof before);
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_UINT32(0, r);
  TEST_ASSERT_EQUAL_MEMORY(&before, &cfg, sizeof cfg);
}

void test_a_document_of_pure_garbage_from_flash_comes_back_usable(void) {
  // The second half of "validate on the way in": the way in was checked, and the flash can hand
  // back something else anyway -- a short read, a bit flip, a blob written by another firmware.
  // What is pinned here is that the device still comes up with a document every validator
  // accepts, because the alternative is a boot that panics on its own settings, and settings
  // survive a reboot: that is a loop with no way in over the air.
  //
  // NOT pinned here, and it cannot be: that sanitize() never READS past a field while deciding
  // this. Every array is MAX+1 bytes, so an unterminated one measures too long either way and
  // the outcome is the same whether or not the terminator guard exists. See the DO NOT on
  // terminated() in comfoair_config.c for what an AddressSanitizer run says instead.
  comfoair_config_t cfg;
  memset(&cfg, 0xff, sizeof cfg);
  // The two booleans are set to real booleans afterwards, and that is not the test being made
  // convenient: a _Bool holding 0xff is a bit pattern no C program may read, so a loader that
  // does nvs_get_blob() straight into this struct has already lost. comfoair_config_nvs.c reads
  // them as u8 and normalises; sanitize() is downstream of that and never sees one.
  cfg.read_only    = false;
  cfg.ha_discovery = true;
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r != 0);
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_host(cfg.mqtt_host));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_prefix(cfg.topic_prefix));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_name(cfg.device_name));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_user(cfg.mqtt_user));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_OK, comfoair_config_check_broker_password(cfg.mqtt_password));
}

void test_a_wrecked_network_record_sends_the_device_to_its_own_access_point(void) {
  // Not a loss -- an access point is a way back, and the invariant that outranks everything else
  // here is that no state exists with no network, no access point and no way in
  // (docs/architecture/failure-model.md). A half-readable credential that cannot associate is worse
  // than none: it keeps the device looking configured while it is unreachable.
  comfoair_config_t cfg = fresh();
  memcpy(cfg.wifi.ssid, "HomeNet", 7);
  cfg.wifi.ssid_len = 200;  // impossible: the array is 32 bytes
  memcpy(cfg.wifi.psk, "12345678", 8);
  cfg.wifi.psk_len = 8;

  // The half of this the suite could not previously reach, and it is the half the name is about:
  // clearing the document is not what raises the access point. comfoair_prov_boot_t
  // .has_credentials is, and it is filled from comfoair_config_nvs_has_credentials(), which asks
  // comfoair_config_wifi_usable() about the very same record. Asked here so that the answer the
  // device acts on is pinned in the same test as the answer the document gets.
  TEST_ASSERT_FALSE_MESSAGE(comfoair_config_wifi_usable(&cfg.wifi),
                            "the device was told it has a network it is about to be denied");

  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_WIFI_SSID));
  TEST_ASSERT_EQUAL(0, cfg.wifi.ssid_len);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, cfg.wifi.psk_len,
                                  "half a credential was kept; half a pair is not a credential");
}

void test_the_predicate_that_raises_the_access_point_is_the_one_that_clears_the_record(void) {
  // The one place two functions could build the state the invariant above forbids outright.
  // comfoair_config_nvs_has_credentials() decides whether the device raises its own access
  // point; comfoair_config_sanitize() decides what the station is handed. When they disagree
  // about one stored blob the device is told it has a network, puts no access point on air, and
  // hands the station an empty record -- for ever, because nothing rewrites the blob.
  //
  // They disagreed. has_credentials() asked only whether the SSID length was 1..32, so one
  // flipped bit in the PSK length byte -- 0x08 becoming 0x88 -- was a present credential to the
  // provisioning state machine and a wrecked one to the document. So there is now ONE predicate,
  // and this table is what keeps it one: every record is run through both, and the answers must
  // match.
  char not_hex[80];
  repeat(not_hex, 'z', 64);
  char hex_key[80];
  repeat(hex_key, 'a', 64);
  static const char NUL_SSID[] = {'H', 'o', 'm', 'e', '\0', 'N', 'e', 't'};

  const struct {
    const char *what;
    const char *ssid;
    size_t      ssid_bytes;
    uint8_t     ssid_len;
    const char *psk;
    size_t      psk_bytes;
    uint8_t     psk_len;
    bool        usable;
  } records[] = {
      {"a network with a passphrase", "HomeNet", 7, 7, "secret12", 8, 8, true},
      {"an open network", "HomeNet", 7, 7, "", 0, 0, true},
      {"a 32-character SSID and a 64-character hex key", "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", 32,
       32, hex_key, 64, 64, true},
      // The reviewer's trigger, and the reason this test exists: one flipped bit in a length
      // byte. The pair is intact, the record is not, and the length-only test called it present.
      {"a PSK length of 136", "HomeNet", 7, 7, "secret12", 8, 136, false},
      {"a PSK shorter than WPA2 allows", "HomeNet", 7, 7, "secret1", 7, 7, false},
      // 64 bytes is the 256-bit key as hex by LENGTH ALONE (rsn_supp/wpa.c:2517-2524); when it is
      // not hex, no PMK is computed and the device silently never associates.
      {"a 64-byte key that is not hex", "HomeNet", 7, 7, not_hex, 64, 64, false},
      {"an SSID with a NUL inside it", NUL_SSID, 8, 8, "secret12", 8, 8, false},
      {"an SSID length past the end of the array", "HomeNet", 7, 200, "secret12", 8, 8, false},
      {"nothing stored at all", "", 0, 0, "", 0, 0, false},
  };

  for (size_t i = 0; i < sizeof records / sizeof records[0]; i++) {
    comfoair_wifi_t w{};
    memcpy(w.ssid, records[i].ssid, records[i].ssid_bytes);
    memcpy(w.psk, records[i].psk, records[i].psk_bytes);
    w.ssid_len = records[i].ssid_len;
    w.psk_len  = records[i].psk_len;

    TEST_ASSERT_EQUAL_MESSAGE(records[i].usable, comfoair_config_wifi_usable(&w), records[i].what);

    comfoair_config_t cfg = fresh();
    cfg.wifi              = w;
    comfoair_config_sanitize(&cfg, DEVICE_ID);
    const bool kept = cfg.wifi.ssid_len != 0;
    TEST_ASSERT_EQUAL_MESSAGE(comfoair_config_wifi_usable(&w), kept, records[i].what);
  }

  // NULL is not a record. The caller is a store that may have failed to read anything at all.
  TEST_ASSERT_FALSE(comfoair_config_wifi_usable(nullptr));
}

void test_a_legacy_pair_this_build_cannot_use_never_reaches_the_new_key(void) {
  // The migration is the one path that writes flash from bytes this build never validated, and
  // it destroys the source as it goes. "Every value is validated on the way in, at the point it
  // is accepted" -- and a migration is a point of acceptance. A pair written under the new key
  // without being checked is one comfoair_config_nvs_has_credentials() reports as present for
  // the rest of the device's life while comfoair_config_sanitize() clears it from the document
  // at every boot: credentials the radio cannot use, and no access point.
  //
  // The decision is made here, in the half a host test can reach; comfoair_config_nvs.c only
  // reads the two strings and does what this says.
  comfoair_wifi_t w{};
  TEST_ASSERT_TRUE(comfoair_config_wifi_from_legacy(&w, "HomeNet", "secret12"));
  TEST_ASSERT_EQUAL_UINT8(7, w.ssid_len);
  TEST_ASSERT_EQUAL_UINT8(8, w.psk_len);
  TEST_ASSERT_EQUAL_MEMORY("HomeNet", w.ssid, 7);
  TEST_ASSERT_EQUAL_MEMORY("secret12", w.psk, 8);

  // An open network is a network, and a missing password is not a missing credential -- the same
  // rule comfoair_config_check_psk() applies to every key.
  TEST_ASSERT_TRUE(comfoair_config_wifi_from_legacy(&w, "HomeNet", nullptr));
  TEST_ASSERT_EQUAL_UINT8(0, w.psk_len);
  TEST_ASSERT_TRUE(comfoair_config_wifi_from_legacy(&w, "HomeNet", ""));
  TEST_ASSERT_EQUAL_UINT8(0, w.psk_len);

  // Both extremes survive whole. A migration that truncated either would hand the owner a
  // device that never associates again and no symptom that says why.
  char ssid32[64];
  char hex64[80];
  repeat(ssid32, 'S', 32);
  repeat(hex64, 'a', 64);
  TEST_ASSERT_TRUE(comfoair_config_wifi_from_legacy(&w, ssid32, hex64));
  TEST_ASSERT_EQUAL_UINT8(32, w.ssid_len);
  TEST_ASSERT_EQUAL_UINT8(64, w.psk_len);

  // And everything this build cannot store is refused rather than moved.
  char not_hex[80];
  repeat(not_hex, 'z', 64);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_config_wifi_from_legacy(&w, "HomeNet", not_hex),
                            "a 64-character non-hex key was migrated; it can never associate");
  TEST_ASSERT_FALSE(comfoair_config_wifi_from_legacy(&w, "HomeNet", "short"));
  char ssid33[64];
  repeat(ssid33, 'S', 33);
  TEST_ASSERT_FALSE(comfoair_config_wifi_from_legacy(&w, ssid33, "secret12"));
  char psk65[80];
  repeat(psk65, 'p', 65);
  TEST_ASSERT_FALSE(comfoair_config_wifi_from_legacy(&w, "HomeNet", psk65));
  TEST_ASSERT_FALSE(comfoair_config_wifi_from_legacy(&w, "", "secret12"));
  TEST_ASSERT_FALSE(comfoair_config_wifi_from_legacy(&w, nullptr, "secret12"));
  TEST_ASSERT_FALSE(comfoair_config_wifi_from_legacy(nullptr, "HomeNet", "secret12"));

  // The record is zeroed whatever the answer, so a caller that ignores the return value writes
  // nothing rather than half a pair -- which is the failure this whole component is shaped
  // around.
  TEST_ASSERT_EQUAL_UINT8(0, w.ssid_len);
  TEST_ASSERT_EQUAL_UINT8(0, w.psk_len);
  TEST_ASSERT_FALSE(comfoair_config_wifi_usable(&w));
}

void test_a_password_record_that_cannot_be_read_unlocks_the_device_rather_than_sealing_it(void) {
  // The uncomfortable one, and it is deliberate. A corrupt record matches no password that
  // exists, so keeping it locks the owner out of a device that is visible on their network and
  // completely unusable -- with no way in over the air and the unit's front panel between them
  // and the button. Clearing it leaves the device open on the LAN, which the owner can SEE and
  // fix. That is the no-way-back invariant applied to the password rather than to the
  // radio. DO NOT "harden" this into keeping the record.
  comfoair_config_t cfg = fresh();
  memcpy(cfg.ui_pw_hash, "1$10000$not-a-record", 21);
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_UI_PASSWORD));
  TEST_ASSERT_FALSE(comfoair_config_password_set(&cfg));
}

void test_a_readable_password_record_is_kept(void) {
  // The other side of the previous case: sanitize must not throw away a record it merely does
  // not recognise the digest of. It has no password to check against and must not invent one.
  comfoair_config_t                cfg = fresh();
  const comfoair_config_hash_ctx_t ctx = hash_with(SALT_A, fake_kdf);
  TEST_ASSERT_TRUE(comfoair_config_hash_password("a-good-password", &ctx, cfg.ui_pw_hash,
                                                 sizeof cfg.ui_pw_hash));
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_FALSE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_UI_PASSWORD));
  TEST_ASSERT_TRUE(comfoair_config_password_set(&cfg));
  TEST_ASSERT_TRUE(comfoair_config_check_ui_password_against(cfg.ui_pw_hash, "a-good-password",
                                                             fake_kdf));
}

void test_a_broker_port_of_zero_from_flash_becomes_the_default_and_is_reported(void) {
  // A zeroed key -- an interrupted write, or a field this build reads and an older one never
  // wrote. Zero is not a port, and handing it to esp-mqtt is a connection that never happens.
  comfoair_config_t cfg = fresh();
  cfg.mqtt_port         = 0;
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_PORT));
  TEST_ASSERT_EQUAL_UINT16(1883, cfg.mqtt_port);
}

void test_sanitize_names_every_field_it_replaced_so_the_owner_can_be_told(void) {
  // A device that quietly loses settings is indistinguishable from one that reset itself,
  // and the owner concludes the firmware is unreliable. Nothing here logs -- a rejected value
  // may itself be a secret and the log ring is public -- so the bitmask is
  // how the fact travels.
  comfoair_config_t cfg = fresh();
  cfg.mqtt_port         = 0;
  memset(cfg.device_name, 'x', sizeof cfg.device_name);  // no terminator
  const comfoair_config_repairs_t r = comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_TRUE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_PORT));
  TEST_ASSERT_TRUE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_DEVICE_NAME));
  TEST_ASSERT_FALSE(r & COMFOAIR_CONFIG_REPAIRED(COMFOAIR_CONFIG_F_MQTT_HOST));
}

void test_sanitize_does_not_take_the_broker_password_away_with_the_host(void) {
  // A repair is per field. Clearing a whole namespace because one value in it was unreadable
  // is a factory reset nobody asked for.
  comfoair_config_t cfg = fresh();
  memcpy(cfg.mqtt_host, "broker.lan", 11);
  memcpy(cfg.mqtt_password, "broker-secret", 14);
  cfg.mqtt_port = 0;
  comfoair_config_sanitize(&cfg, DEVICE_ID);
  TEST_ASSERT_EQUAL_STRING("broker.lan", cfg.mqtt_host);
  TEST_ASSERT_EQUAL_STRING("broker-secret", cfg.mqtt_password);
}

// --- schema ---------------------------------------------------------------------------------

void test_the_schema_version_is_pinned(void) {
  // Bumped when the MEANING or the layout of a stored value changes, never for a field added
  // with a default a fresh read already produces. A bump without a migration is what sends an
  // owner back to the setup page after an update.
  TEST_ASSERT_EQUAL_UINT32(1u, COMFOAIR_CONFIG_SCHEMA_VERSION);
}

void test_a_fresh_device_and_an_older_one_take_the_same_path(void) {
  // A device with nothing stored reads 0. Making that a third answer would mean three code
  // paths where two suffice: migration from nothing finds nothing, which is exactly right, and
  // migration from the legacy namespace is then not a special case either.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_SCHEMA_MIGRATE, comfoair_config_schema_check(0));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_SCHEMA_CURRENT,
                    comfoair_config_schema_check(COMFOAIR_CONFIG_SCHEMA_VERSION));
}

void test_a_schema_from_the_future_is_read_only_rather_than_erased(void) {
  // An OTA rollback. Both obvious answers are wrong: erasing destroys a working owner's
  // configuration to fix a problem they do not have, and refusing to boot is a brick on a
  // device behind a front panel. Read-only keeps the device ventilating and leaves the
  // fifteen-second gesture as the way out.
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_SCHEMA_FUTURE,
                    comfoair_config_schema_check(COMFOAIR_CONFIG_SCHEMA_VERSION + 1));
  TEST_ASSERT_EQUAL(COMFOAIR_CONFIG_SCHEMA_FUTURE, comfoair_config_schema_check(0xffffffffu));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_stored_names_are_pinned_because_renaming_one_costs_the_owner_their_network);
  RUN_TEST(test_every_stored_name_is_short_enough_for_nvs_to_accept_it);
  RUN_TEST(test_the_provisioning_flags_live_where_forgetting_the_network_forgets_them_too);
  RUN_TEST(test_no_two_things_are_stored_under_one_name_in_the_wifi_namespace);
  RUN_TEST(test_the_wifi_pair_is_one_stored_record_and_the_table_says_so);
  RUN_TEST(test_a_field_that_is_a_secret_is_one_that_comfoair_secrets_also_calls_a_secret);
  RUN_TEST(test_an_unknown_field_is_null_rather_than_a_crash);

  RUN_TEST(test_the_soft_reset_clears_the_network_and_the_password_and_keeps_the_broker);
  RUN_TEST(test_the_soft_reset_also_erases_what_the_previous_firmware_stored);
  RUN_TEST(test_the_hard_reset_erases_every_namespace_that_exists_now_or_later);
  RUN_TEST(test_every_field_lands_on_the_side_of_the_soft_reset_that_d6_puts_it_on);
  RUN_TEST(test_an_unknown_namespace_is_erased_by_neither_tier);

  RUN_TEST(test_a_thirty_two_character_ssid_is_legal_and_a_thirty_three_character_one_is_not);
  RUN_TEST(test_an_empty_ssid_is_not_a_network);
  RUN_TEST(test_an_ssid_with_a_nul_in_it_is_refused_because_the_radio_cannot_express_it);
  RUN_TEST(test_a_sixty_four_character_psk_is_legal_and_must_be_hexadecimal);
  RUN_TEST(test_a_passphrase_shorter_than_eight_characters_is_not_one);
  RUN_TEST(test_an_open_network_has_no_key_and_that_is_not_an_error);
  RUN_TEST(test_a_psk_with_a_nul_in_it_is_refused);
  RUN_TEST(test_a_port_outside_the_range_is_refused_rather_than_wrapped);
  RUN_TEST(test_a_broker_host_is_a_host_and_not_a_url);
  RUN_TEST(test_no_broker_is_a_supported_configuration);
  RUN_TEST(test_a_host_longer_than_dns_allows_is_refused);
  RUN_TEST(test_credentials_longer_than_the_buffer_are_refused_not_truncated);
  RUN_TEST(test_a_topic_prefix_may_not_contain_a_wildcard_or_start_at_the_brokers_own_tree);
  RUN_TEST(test_a_topic_prefix_with_an_empty_level_is_refused);
  RUN_TEST(test_a_device_without_a_topic_prefix_or_a_name_is_refused);
  RUN_TEST(test_a_device_name_is_bounded_and_printable);
  RUN_TEST(test_a_ui_password_has_a_floor_because_the_kdf_is_the_only_thing_slowing_a_guess);
  RUN_TEST(test_every_error_has_a_sentence_and_none_of_them_quotes_the_value);

  RUN_TEST(test_a_fresh_device_has_no_network_no_password_and_a_broker_that_is_not_set);
  RUN_TEST(test_the_default_topic_prefix_carries_the_device_id_so_two_units_do_not_collide);
  RUN_TEST(test_a_device_that_cannot_read_its_own_mac_still_boots);
  RUN_TEST(test_the_default_name_matches_the_access_point_the_owner_saw_on_their_phone);

  RUN_TEST(test_no_stored_secret_appears_anywhere_in_what_a_client_is_handed);
  RUN_TEST(test_an_unset_secret_reads_back_empty_so_the_page_can_say_nothing_is_set);
  RUN_TEST(test_password_set_is_derived_from_the_record_and_is_never_a_key_of_its_own);
  RUN_TEST(test_a_thirty_two_character_ssid_survives_the_projection_whole);
  RUN_TEST(test_the_projection_hands_out_nothing_a_renderer_can_run_off_the_end_of);

  RUN_TEST(test_a_document_with_one_bad_field_changes_nothing_at_all);
  RUN_TEST(test_changing_the_network_without_retyping_its_password_is_refused);
  RUN_TEST(test_saving_an_unrelated_setting_leaves_the_stored_network_untouched);
  RUN_TEST(test_half_a_wifi_pair_is_refused_whichever_half_it_is);
  RUN_TEST(test_a_thirty_two_character_ssid_and_a_sixty_four_character_key_survive_byte_for_byte);
  RUN_TEST(test_forgetting_the_network_clears_both_halves_at_once);
  RUN_TEST(test_an_absent_key_keeps_and_an_empty_key_clears);
  RUN_TEST(test_the_broker_password_is_stored_so_it_can_be_used_and_the_ui_password_is_not);
  RUN_TEST(test_a_ui_password_with_nowhere_to_hash_it_is_refused_rather_than_stored);
  RUN_TEST(test_the_published_sentinel_cannot_become_the_password_of_an_unconfigured_device);
  RUN_TEST(test_clearing_the_ui_password_is_allowed_because_the_owner_may_be_locked_out_of_it);
  RUN_TEST(test_a_store_that_holds_a_newer_schema_refuses_every_write);
  RUN_TEST(test_a_null_document_is_refused_rather_than_reported_as_saved);

  RUN_TEST(test_the_stored_record_is_not_the_password_and_carries_its_own_cost);
  RUN_TEST(test_the_right_password_verifies_and_a_wrong_one_does_not);
  RUN_TEST(test_two_devices_with_the_same_password_do_not_share_a_record);
  RUN_TEST(test_a_record_written_at_another_iteration_count_still_verifies);
  RUN_TEST(test_a_record_that_does_not_parse_lets_nobody_in);
  RUN_TEST(test_an_iteration_count_nobody_could_wait_for_is_not_a_record);
  RUN_TEST(test_a_record_is_never_written_into_a_buffer_it_does_not_fit);

  RUN_TEST(test_a_good_document_is_left_exactly_as_it_was);
  RUN_TEST(test_a_document_of_pure_garbage_from_flash_comes_back_usable);
  RUN_TEST(test_a_wrecked_network_record_sends_the_device_to_its_own_access_point);
  RUN_TEST(test_the_predicate_that_raises_the_access_point_is_the_one_that_clears_the_record);
  RUN_TEST(test_a_legacy_pair_this_build_cannot_use_never_reaches_the_new_key);
  RUN_TEST(test_a_password_record_that_cannot_be_read_unlocks_the_device_rather_than_sealing_it);
  RUN_TEST(test_a_readable_password_record_is_kept);
  RUN_TEST(test_a_broker_port_of_zero_from_flash_becomes_the_default_and_is_reported);
  RUN_TEST(test_sanitize_names_every_field_it_replaced_so_the_owner_can_be_told);
  RUN_TEST(test_sanitize_does_not_take_the_broker_password_away_with_the_host);

  RUN_TEST(test_the_schema_version_is_pinned);
  RUN_TEST(test_a_fresh_device_and_an_older_one_take_the_same_path);
  RUN_TEST(test_a_schema_from_the_future_is_read_only_rather_than_erased);
  return UNITY_END();
}
