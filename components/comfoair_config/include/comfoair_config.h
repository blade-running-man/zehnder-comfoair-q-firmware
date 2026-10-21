// What survives a power cut: the network, the broker, the name, and the two passwords.
//
// This half is PURE -- no ESP-IDF, no NVS, no clock. Everything that decides whether a value
// is acceptable, what a document means when it only mentions half of itself, and which keys a
// reset destroys lives here, where a host test can reach it. comfoair_config_nvs.h is the
// other half: it opens namespaces, reads bytes, and asks the questions below. That split is
// not tidiness. Every rule in this file exists because a bad value stored once outlives every
// reboot, and a device that panics on boot over its own configuration has no way back over the
// air, because the bad value is still there after the reboot (docs/architecture/failure-model.md:
// no ESP_ERROR_CHECK on stored or received data).
//
// Three rules run through the whole file, and each is here because it has already cost
// somebody a device:
//
//  * VALIDATE ON THE WAY IN. A value is refused at the point it is accepted, not when it is
//    used. The alternative is a stored value that is only discovered to be impossible on the
//    next boot, by which time the only surface that could have fixed it is gone.
//  * SSID AND PSK ARE ONE RECORD. NVS gives no atomicity between two keys; a power cut between two
//    writes leaves a new SSID with an old password, which is indistinguishable from a typo and
//    strands the device just as thoroughly. comfoair_wifi_t is that record, it is written under ONE
//    key, and comfoair_config_apply() refuses a document that mentions one half of it without the
//    other.
//  * NOTHING RESERVES A BYTE FOR A TERMINATOR. wifi_sta_config_t.ssid is 32 bytes and
//    .password is 64, neither NUL-terminated (esp_wifi_types_generic.h:559-560). The obvious
//    strncpy(dst, src, sizeof dst - 1) silently truncates a legal 32-character SSID to 31 and
//    a 64-character hex PSK to 63, and the device then never associates -- with a symptom
//    nobody can deduce from outside. Lengths are carried explicitly instead.
//
// The two passwords are stored under DIFFERENT policies and ONE disclosure policy. The broker
// password is stored recoverably because MQTT needs the plaintext to authenticate -- a hash is
// meaningless there. The UI password is stored as a one-way record. Neither is ever returned by
// any projection: AGENTS.md makes write-only fields the property that unblocks a single universal
// binary.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comfoair_secrets.h"  // the sentinel and the constant-time compare; both are reused here

#ifdef __cplusplus
extern "C" {
#endif

// --- the names, which are a contract ---------------------------------------------------------
//
// An OTA that renames one of these sends the owner back to the setup page with a device that
// has forgotten its network. test_config pins every literal below for that reason: changing one
// has to be a deliberate act with a migration behind it, not a rename that looked tidy.

// Bumped when the meaning or the layout of a stored value changes -- NOT when a field is added
// with a default that a fresh read already produces. comfoair_config_schema_check() says what a
// given stored number means for this build.
#define COMFOAIR_CONFIG_SCHEMA_VERSION 1u

// NVS_KEY_NAME_MAX_SIZE is 16 INCLUDING the terminator (nvs.h:61), and namespaces share that
// limit (nvs.h:62). A 16-character key is not truncated, it is refused with
// ESP_ERR_NVS_KEY_TOO_LONG -- and since nothing here may ESP_ERROR_CHECK a store result, an
// over-long key would fail silently and the field would simply never persist. Pinned by a test
// over the whole table rather than checked at each call site.
#define COMFOAIR_CONFIG_NVS_NAME_MAX 15

// Three namespaces, and which one a field lives in is what decides whether the soft reset destroys
// it (docs/device/button.md). They are spelled out here rather than only inside
// comfoair_config_ns_name() because comfoair_config_nvs.c opens them by name and a test pins them
// by name, and a string that appears twice is a string that gets changed once.
#define COMFOAIR_CONFIG_NS_WIFI_NAME  "cfg_wifi"
#define COMFOAIR_CONFIG_NS_OWNER_NAME "cfg_owner"
#define COMFOAIR_CONFIG_NS_APP_NAME   "cfg_app"

// The namespace an earlier comfoair_net wrote its two Wi-Fi keys into, before this component
// existed (read by migrate_legacy_wifi() in comfoair_config_nvs.c). It is
// named here because comfoair_config_nvs.c has to find it to migrate it, and because a factory
// reset that does not erase it leaves the previous owner's network behind.
#define COMFOAIR_CONFIG_NS_LEGACY_NAME "comfoair"
#define COMFOAIR_CONFIG_LEGACY_KEY_SSID "wifi_ssid"
#define COMFOAIR_CONFIG_LEGACY_KEY_PSK  "wifi_psk"

// Where the schema number lives. In the namespace the soft reset KEEPS, so that clearing the
// network does not also throw away the device's knowledge of its own layout.
#define COMFOAIR_CONFIG_KEY_SCHEMA "schema"

// The Wi-Fi pair, and the last pair that was known to produce an address. Both are blobs, both
// are one key, and that is the atomicity invariant expressed as storage rather than as a
// comment. The second one is what comfoair_prov's COMMIT and RESTORE actions move
// (comfoair_provision.h, COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS): without a slot for it the
// rollback that protects against a typo has nowhere to roll back to.
#define COMFOAIR_CONFIG_KEY_STA      "sta"
#define COMFOAIR_CONFIG_KEY_STA_GOOD "sta_good"

// The two facts comfoair_prov_boot_t needs handed back after a power cut, and neither is a
// setting. They are here because every NVS name in this firmware is written down in one place,
// and they are in the WI-FI namespace because that is the tier the soft reset erases: the
// gesture means "forget this network", and a device that has forgotten its network is on its
// first run again -- it should get the first-run fifteen-minute setup window, not the five
// `win_closed` would still be asking for.
//
// What each one buys is a WAY BACK, which is why losing one is worse than losing a setting:
//  * win_closed -- the rule "the access point comes back for five minutes at each boot", which only
//    means anything if the device remembers that a window ran out at all.
//  * no_addr -- a station that joined the owner's network and was never given an address. The
//    thirty-minute return of the access point excludes that failure BY NAME, so nothing raises an access
//    point for it; this flag is what makes the NEXT boot offer one instead
//    (comfoair_provision.h, comfoair_prov_address_never_arrived).
#define COMFOAIR_CONFIG_KEY_WINDOW_CLOSED "win_closed"
#define COMFOAIR_CONFIG_KEY_NO_ADDRESS    "no_addr"

// The four durations and the one behavioural setting phase 7 keeps on this node.
//
// In cfg_app, which is where this header always said they would land: "broker, prefix, discovery,
// name -- and the durations, when phase 5 starts storing them. They land here without anyone
// touching the reset code, which is the point of a namespace carrying the tier instead of each
// field carrying it."
//
// They are NOT part of the configuration document and do not appear in GET /api/config. They are
// registry ENTITIES, written through POST /api/entities/<key> like everything else in the table --
// one value with two doors would be two sources of truth on the wire. This is storage for them,
// nothing more.
#define COMFOAIR_CONFIG_KEY_BOOST_MINUTES   "dur_boost"
#define COMFOAIR_CONFIG_KEY_BYPASS_MINUTES  "dur_bypass"
#define COMFOAIR_CONFIG_KEY_BALANCE_MINUTES "dur_balance"
#define COMFOAIR_CONFIG_KEY_AWAY_MINUTES    "dur_away"
// The owner's answer to "should a level change also pin the unit into manual mode". Default on,
// so a level that is set stays set; off gives the unit's scheduler and its three sensor-ventilation
// controls back, at the price of a level that reverts within two hours.
#define COMFOAIR_CONFIG_KEY_PERMANENT_MANUAL "manual_perm"


// --- sizes -----------------------------------------------------------------------------------

// 802.11 caps an SSID at 32 octets and WPA2 caps a passphrase at 63 characters, with 64 meaning
// the 256-bit PSK written as hex (IEEE 802.11i Annex H). Both extremes are LEGAL and both must
// survive this store byte for byte.
#define COMFOAIR_CONFIG_SSID_MAX 32
#define COMFOAIR_CONFIG_PSK_MAX  64
// Below this a WPA2 passphrase is not a passphrase. Refused on the way in rather than handed to
// esp_wifi, which would take it and never associate.
#define COMFOAIR_CONFIG_PSK_MIN  8

// RFC 1035 §2.3.4: the longest legal domain name. Deliberately not a rounder, smaller number --
// a cap that refuses a name the owner's DNS resolves is a bug report nobody can act on, and the
// bytes cost nothing next to the flash page they live in.
#define COMFOAIR_CONFIG_HOST_MAX   253
#define COMFOAIR_CONFIG_USER_MAX   128
#define COMFOAIR_CONFIG_BROKER_PASS_MAX 128
#define COMFOAIR_CONFIG_PREFIX_MAX 64
#define COMFOAIR_CONFIG_NAME_MAX   32
// The plaintext the owner types, not what is stored. Long enough for a passphrase, bounded so a
// POST body cannot make the hash step take arbitrarily long.
#define COMFOAIR_CONFIG_UI_PASS_MAX 128
// There is no lockout and there cannot be one: locking out attempts on a device nobody can
// reach is a way to lose the device, not to defend it. So the only cost an attacker on the
// LAN pays per guess is one derivation, and the search space has to carry the rest.
// DO NOT lower this to "let people use a PIN" -- four digits at 100 ms is a quarter of an hour.
#define COMFOAIR_CONFIG_UI_PASS_MIN 8

// The full MAC rendered as lower-case hex. Identity is the MAC, never the name -- renaming a
// device must not re-key anything, or Home Assistant ends up with two devices and the dead one
// cannot be removed (docs/architecture/security.md, "Identity").
#define COMFOAIR_CONFIG_DEVICE_ID_LEN 12

// --- the field table ---------------------------------------------------------------------------

typedef enum {
    COMFOAIR_CONFIG_F_WIFI_SSID,
    COMFOAIR_CONFIG_F_WIFI_PSK,
    COMFOAIR_CONFIG_F_MQTT_HOST,
    COMFOAIR_CONFIG_F_MQTT_PORT,
    COMFOAIR_CONFIG_F_MQTT_USER,
    COMFOAIR_CONFIG_F_MQTT_PASSWORD,
    COMFOAIR_CONFIG_F_TOPIC_PREFIX,
    COMFOAIR_CONFIG_F_HA_DISCOVERY,
    COMFOAIR_CONFIG_F_DEVICE_NAME,
    COMFOAIR_CONFIG_F_UI_PASSWORD,
    COMFOAIR_CONFIG_F_COUNT,
} comfoair_config_field_t;

// Erasure is by NAMESPACE, never by a list of keys, and that is the whole reason namespaces
// exist in this component. A hand-written key list goes stale the first time a field is added --
// and worse, it cannot name a key that an OLDER firmware wrote and this build has never heard
// of. nvs_erase_all() over a namespace takes both. The named failure is a "factory reset" that
// leaves the previous owner's broker password in the flash.
typedef enum {
    COMFOAIR_CONFIG_NS_WIFI,    // the pair, and the known-good pair
    COMFOAIR_CONFIG_NS_OWNER,   // the UI password record: what says this device is claimed
    COMFOAIR_CONFIG_NS_APP,     // broker, prefix, discovery, name -- and the durations, when
                                // phase 5 starts storing them. They land here without anyone
                                // touching the reset code, which is the point of a namespace
                                // carrying the tier instead of each field carrying it.
    COMFOAIR_CONFIG_NS_LEGACY,  // what comfoair_net wrote before this component existed
    COMFOAIR_CONFIG_NS_COUNT,
} comfoair_config_ns_t;

// The two-tier reset (docs/device/button.md), and the two durations are the gesture's, not this
// component's: 5 s after boot is soft, 15 s is hard. The button is GPIO9, which is also the
// download strap -- holding it AT POWER-ON enters the bootloader instead, so neither tier can be
// triggered that way (AGENTS.md).
typedef enum {
    // Wi-Fi and the UI password. The UI password goes deliberately: a forgotten password on a
    // device that is visible on the network and completely unusable is otherwise unfixable
    // without USB.
    COMFOAIR_CONFIG_RESET_SOFT,
    COMFOAIR_CONFIG_RESET_HARD,
} comfoair_config_reset_t;

typedef struct {
    // The name the owner and the API see, and -- where the field is its own NVS entry -- the
    // key it is stored under. One name, not two, except where what is stored is not what is
    // named: `ui_password` is stored as `ui_pw_hash` because the record is not the password.
    const char *name;
    const char *key;
    comfoair_config_ns_t ns;
    // True when comfoair_secret_key(name) must agree. The redaction in comfoair_secrets matches
    // by word, not by list, so a field named `mqtt_pw` would be published in full while looking
    // perfectly sensible here. The test asserts the two agree for every row.
    bool secret;
} comfoair_config_field_info_t;

// NULL for an id outside the table -- callers get the entity list wrong, and a crash on the
// HTTP task is a worse answer than a 404.
const comfoair_config_field_info_t *comfoair_config_field(comfoair_config_field_t field);

const char *comfoair_config_ns_name(comfoair_config_ns_t ns);

// Which namespaces a tier destroys. Ask this rather than listing namespaces at the call site:
// the reset code should not be somewhere a new namespace can be forgotten.
bool comfoair_config_reset_erases(comfoair_config_reset_t tier, comfoair_config_ns_t ns);

// --- the document ------------------------------------------------------------------------------

// One atomic record. DO NOT split it into two fields "for symmetry with the API" -- the whole
// point is that no power cut can land between the SSID and the PSK.
//
// Neither array is NUL-terminated and neither has room to be. Read exactly `*_len` bytes; a
// strlen() over either is a bug that only shows up on a 32-character SSID, which is to say in
// somebody's house and not on the bench.
typedef struct {
    uint8_t ssid[COMFOAIR_CONFIG_SSID_MAX];
    uint8_t psk[COMFOAIR_CONFIG_PSK_MAX];
    uint8_t ssid_len;
    uint8_t psk_len;
} comfoair_wifi_t;

// The record format is `1$<iterations>$<salt hex>$<digest hex>`, which at the current sizes is
// 105 characters. The margin is for a larger iteration count, not for a larger digest.
#define COMFOAIR_CONFIG_HASH_MAX 127

// The whole stored document, in memory.
//
// DO NOT hand this struct to a renderer, a logger or an event payload. It holds the broker
// password in plaintext by design and the UI password record beside it; the absolute rule is that
// nothing for which comfoair_secret_key() is true reaches ESP_LOG*, because logging is captured
// whole and served to anyone by /api/log (docs/architecture/security.md).
// comfoair_config_project() is the only thing that leaves this struct.
typedef struct {
    comfoair_wifi_t wifi;
    char     mqtt_host[COMFOAIR_CONFIG_HOST_MAX + 1];
    uint16_t mqtt_port;
    char     mqtt_user[COMFOAIR_CONFIG_USER_MAX + 1];
    char     mqtt_password[COMFOAIR_CONFIG_BROKER_PASS_MAX + 1];
    char     topic_prefix[COMFOAIR_CONFIG_PREFIX_MAX + 1];
    bool     ha_discovery;
    char     device_name[COMFOAIR_CONFIG_NAME_MAX + 1];
    // The one-way record, never the password. Empty means no password is set -- see
    // comfoair_config_password_set(), which is why this is not accompanied by a flag.
    char     ui_pw_hash[COMFOAIR_CONFIG_HASH_MAX + 1];
    // Set by the loader when the flash holds a schema this build does not understand, which is
    // what an OTA rollback looks like from here. The flag lives in the document rather than
    // in the store so that every write path passes through one check instead of each caller
    // remembering; comfoair_config_apply() refuses while it is set.
    bool     read_only;
} comfoair_config_t;

// --- what a client is allowed to see -------------------------------------------------------------

// Built by comfoair_config_project(). Every pointer except the three secrets borrows from the
// comfoair_config_t it was built from and dies with it -- except where the document came back
// from flash without a terminator in a field, and then it borrows a literal "" instead. A
// renderer may strlen() every one of these; that is the whole contract of this struct.
typedef struct {
    // Terminated here even though the record is not, because this is what gets rendered. 33
    // bytes, so a 32-character SSID arrives whole.
    char        ssid[COMFOAIR_CONFIG_SSID_MAX + 1];
    const char *psk;             // the sentinel or ""; never the value
    const char *mqtt_host;
    uint16_t    mqtt_port;
    const char *mqtt_user;
    const char *mqtt_password;   // the sentinel or ""; never the value
    const char *topic_prefix;
    bool        ha_discovery;
    const char *device_name;
    const char *ui_password;     // the sentinel or ""; never the value
    // DERIVED from the record being non-empty, and never stored. A flag of its own would be a
    // second key, and a half-finished write that stored the flag without the record would leave
    // the device demanding a password that does not exist -- locked, by nobody, for ever.
    bool        ui_password_set;
    bool        read_only;
} comfoair_config_public_t;

void comfoair_config_project(const comfoair_config_t *cfg, comfoair_config_public_t *out);

// The derivation itself, exposed because the access policy needs it too
// (comfoair_http_policy.h, ctx.password_set) and must not grow its own idea of what "set" means.
bool comfoair_config_password_set(const comfoair_config_t *cfg);

// --- what is acceptable --------------------------------------------------------------------------

typedef enum {
    COMFOAIR_CONFIG_OK,
    COMFOAIR_CONFIG_ERR_SSID,
    COMFOAIR_CONFIG_ERR_PSK,
    // One half of the Wi-Fi pair without the other. Its own code because the owner has to be
    // told something better than "bad request": the form always submits both, so a document
    // with one is either a hand-made request or a broken client, and guessing which password
    // was meant is how a device leaves the network for good.
    COMFOAIR_CONFIG_ERR_WIFI_PAIR,
    COMFOAIR_CONFIG_ERR_HOST,
    COMFOAIR_CONFIG_ERR_PORT,
    COMFOAIR_CONFIG_ERR_USER,
    COMFOAIR_CONFIG_ERR_BROKER_PASSWORD,
    COMFOAIR_CONFIG_ERR_PREFIX,
    COMFOAIR_CONFIG_ERR_NAME,
    COMFOAIR_CONFIG_ERR_UI_PASSWORD,
    // The submission was fine and there is no way to store it: no hash function was supplied.
    // Separate from ERR_UI_PASSWORD because the owner did nothing wrong and retyping will not
    // help.
    COMFOAIR_CONFIG_ERR_NO_HASH,
    // The flash holds a newer schema than this build understands. See comfoair_config_t.read_only.
    COMFOAIR_CONFIG_ERR_READ_ONLY,
    // There was no document to apply. Its own code rather than OK, because OK is what a handler
    // turns into 200 and what comfoair_config_strerror() renders as the word "accepted": the
    // owner would read Saved over a request that stored nothing and close the page. A patch with
    // every field ABSENT is a different thing and still succeeds -- the settings page submits the
    // whole document, and "nothing changed" is a legitimate save.
    COMFOAIR_CONFIG_ERR_NO_DOCUMENT,
} comfoair_config_err_t;

// A sentence for the owner. Never contains a value -- these strings end up in an HTTP body and
// in the log ring, and the log ring is served by /api/log to whoever can reach it.
const char *comfoair_config_strerror(comfoair_config_err_t err);

// Length is passed, not inferred: `ssid` is not a C string in the record it comes from.
comfoair_config_err_t comfoair_config_check_ssid(const uint8_t *ssid, size_t len);
comfoair_config_err_t comfoair_config_check_psk(const uint8_t *psk, size_t len);
comfoair_config_err_t comfoair_config_check_host(const char *host);
// uint32_t, not uint16_t. A port arrives as a JSON number and 70000 has to be REFUSED; taking it
// as uint16_t wraps it to 4464 and stores a broker address the owner never typed.
comfoair_config_err_t comfoair_config_check_port(uint32_t port);
comfoair_config_err_t comfoair_config_check_user(const char *user);
comfoair_config_err_t comfoair_config_check_broker_password(const char *password);
comfoair_config_err_t comfoair_config_check_prefix(const char *prefix);
comfoair_config_err_t comfoair_config_check_name(const char *name);
comfoair_config_err_t comfoair_config_check_ui_password(const char *password);

// The ONE answer to "is there a network in this record", asked by both of the functions that
// could otherwise disagree about it -- and the disagreement builds the one state
// docs/architecture/failure-model.md forbids outright: no network, no access point, no way back.
//
// comfoair_config_nvs_has_credentials() fills comfoair_prov_boot_t.has_credentials, which is what
// decides whether the device raises its own access point. comfoair_config_sanitize() decides what
// the station is handed. When the first says present and the second clears the record, the device
// puts no access point on air and hands the station nothing: no network, no way in, and permanent,
// because nothing rewrites the blob. One flipped bit in the PSK length byte was enough.
//
// "An SSID worth trying is stored", never "the key exists": a saved-but-zeroed blob still exists,
// so a test for the key alone is true after `Forget Wi-Fi` and the device spends a boot
// associating with "". Length alone is not enough either, which is the other half of what this
// exists to stop: a 64-byte key that is not hex, a seven-character passphrase and an SSID with a
// NUL in it are all stored records no radio can use.
bool comfoair_config_wifi_usable(const comfoair_wifi_t *wifi);

// Builds the record from the two C strings an earlier comfoair_net stored (see
// COMFOAIR_CONFIG_NS_LEGACY_NAME) and says whether it may be written. FALSE MEANS DO NOT STORE IT AND DO NOT ERASE THE SOURCE.
//
// The migration is the one path that writes flash from bytes this build never validated, and
// validating them is not optional: a pair moved under the new key unchecked is one
// comfoair_config_nvs_has_credentials() calls present for the rest of the device's life while
// comfoair_config_sanitize() clears it from the document at every boot. The decision lives here,
// in the half a host test can reach, and comfoair_config_nvs.c only reads the two strings.
//
// `out` is zeroed whatever the answer, so a caller that ignores the return value writes nothing
// rather than half a pair. A NULL or empty `psk` is an OPEN NETWORK and not a missing credential
// -- the same rule comfoair_config_check_psk() (comfoair_config_check.c) applies to every key.
bool comfoair_config_wifi_from_legacy(comfoair_wifi_t *out, const char *ssid, const char *psk);

// --- defaults ----------------------------------------------------------------------------------

// `device_id` is COMFOAIR_CONFIG_DEVICE_ID_LEN hex characters from the full MAC. It is a
// parameter and not a call into esp_mac.h for the same reason time is a parameter in
// comfoair_provision: a component that reads its own identity cannot be tested for what it does
// with a different one.
//
// NULL or malformed is tolerated and produces a prefix with no id in it. That is a WORSE default
// -- two devices in one house would then share a topic tree -- but refusing to boot over it is
// worse still, and comfoair_config_sanitize() reports it.
void comfoair_config_defaults(comfoair_config_t *cfg, const char *device_id);

// --- applying a submitted document ------------------------------------------------------------

// A POST body, decoded. A NULL string means the key was ABSENT, which is not the same as an
// empty string: absent leaves the stored value alone, empty clears it. The whole sentinel scheme
// in comfoair_secrets exists because those two were once the same thing and one save of an
// unrelated setting wiped the broker credentials.
typedef struct {
    const char *wifi_ssid;
    const char *wifi_psk;
    const char *mqtt_host;
    const char *mqtt_user;
    const char *mqtt_password;
    const char *topic_prefix;
    const char *device_name;
    const char *ui_password;
    bool        has_mqtt_port;
    uint32_t    mqtt_port;
    bool        has_ha_discovery;
    bool        ha_discovery;
} comfoair_config_patch_t;

#define COMFOAIR_CONFIG_SALT_LEN       16
#define COMFOAIR_CONFIG_DIGEST_LEN     32
// PBKDF2-HMAC-SHA256 at this count costs order 100 ms on a C6 with the SHA accelerator. It is
// carried IN the record, so raising it later does not invalidate what is already stored.
// DO NOT run the check once per request: verify at login and carry a session, or every page of
// the UI pays for it.
#define COMFOAIR_CONFIG_KDF_ITERATIONS 10000u
// The largest count this build will WRITE, and the largest it will READ BACK. Both halves, and
// they have to be the same number: a parser stricter than the writer stores a password and then
// refuses it, with nobody to ask.
//
// A hundred times the compiled-in count is room to raise it and no room for a bit flip. The
// number that has to be excluded is what a corrupt digit string reaches: at the 100 ms per
// 10,000 iterations above, 100,000,000 -- which a uint32_t holds comfortably -- is about
// seventeen minutes of PBKDF2 on the HTTP task, paid on EVERY login attempt, on a device whose
// owner is standing in front of it. There is no lockout to bound how often it is asked for (see
// COMFOAIR_CONFIG_UI_PASS_MIN), so nothing else bounds the cost either.
#define COMFOAIR_CONFIG_KDF_ITERATIONS_MAX (100u * COMFOAIR_CONFIG_KDF_ITERATIONS)

// The one-way function, supplied by the caller. Injected because it is the only thing in this
// component that needs a crypto library: mbedtls is on the device and not on the host, and a
// component that cannot build on the host cannot have its record format, its salt handling or
// its answer to a wrong password tested at all. Returns false if the derivation failed.
typedef bool (*comfoair_config_kdf_t)(const char *password, const uint8_t *salt, size_t salt_len,
                                      uint32_t iterations, uint8_t *out, size_t out_len);

typedef struct {
    comfoair_config_kdf_t kdf;
    // COMFOAIR_CONFIG_SALT_LEN bytes of fresh randomness, per stored password. Fresh because two
    // devices with the same password must not produce the same record: one recovered flash image
    // would otherwise answer for every device whose owner chose the same word.
    const uint8_t *salt;
    // 0 means COMFOAIR_CONFIG_KDF_ITERATIONS.
    uint32_t iterations;
} comfoair_config_hash_ctx_t;

// Renders the stored record for `password`. Writes nothing on failure.
bool comfoair_config_hash_password(const char *password, const comfoair_config_hash_ctx_t *ctx,
                                   char *out, size_t cap);

// Constant-time in the digest, via comfoair_secret_equals. False for a record that does not
// parse -- and note that comfoair_config_sanitize() will already have thrown such a record away;
// see the comment there for why an unreadable password unlocks the device rather than sealing it.
bool comfoair_config_check_ui_password_against(const char *record, const char *candidate,
                                               comfoair_config_kdf_t kdf);

// ALL OR NOTHING. Everything in `patch` is validated before anything is written, so a document
// with a good host and a bad port changes neither. A half-applied configuration is the failure
// this is built to avoid: the owner sees the error, retries, and the second attempt is applied
// on top of a document that is no longer the one they were looking at.
//
// `hash` may be NULL when the patch does not set a UI password. When it does, a NULL hash
// context is COMFOAIR_CONFIG_ERR_NO_HASH -- never a password stored in the clear.
comfoair_config_err_t comfoair_config_apply(comfoair_config_t *cfg,
                                            const comfoair_config_patch_t *patch,
                                            const comfoair_config_hash_ctx_t *hash);

// --- what came back out of the flash -------------------------------------------------------------

// One bit per field, using the comfoair_config_field_t ids.
typedef uint32_t comfoair_config_repairs_t;
#define COMFOAIR_CONFIG_REPAIRED(field) ((comfoair_config_repairs_t)1u << (field))

// Runs the same validators over a document that was just READ, and replaces anything unusable
// with its default. This is the second half of "validate on the way in": the way in was checked,
// and the flash can still hand back something else -- a partial write, a bit flip, a document
// written by a firmware that is not this one.
//
// Returns which fields were replaced so the owner can be told. Nothing here logs: a rejected
// value may itself be a secret, and the log ring is public.
//
// A wrecked Wi-Fi record is cleared, which sends the device to its own access point. That is the
// right answer and not a loss: an access point is a way back, and the invariant that outranks
// everything else here is that no state exists with no network, no access point and no way in
// (docs/architecture/failure-model.md).
comfoair_config_repairs_t comfoair_config_sanitize(comfoair_config_t *cfg, const char *device_id);

// --- schema ---------------------------------------------------------------------------------------

typedef enum {
    COMFOAIR_CONFIG_SCHEMA_CURRENT,
    COMFOAIR_CONFIG_SCHEMA_MIGRATE,
    // Newer than this build. An OTA rollback lands exactly here, and both obvious answers
    // are wrong: erasing destroys a working owner's configuration to fix a problem they do not
    // have, and refusing to boot is a brick on a device behind a front panel. So the document is
    // read with whatever this build understands and the store goes READ-ONLY -- the device keeps
    // ventilating, keeps its network and keeps its broker, and refuses to write settings until
    // someone does the 15-second reset, which is a way back.
    COMFOAIR_CONFIG_SCHEMA_FUTURE,
} comfoair_config_schema_action_t;

comfoair_config_schema_action_t comfoair_config_schema_check(uint32_t stored);

#ifdef __cplusplus
}
#endif
