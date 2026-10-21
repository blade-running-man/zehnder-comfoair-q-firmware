// Being findable by name: `comfoair-XXXX.local` instead of an address read off the router.
//
// The problem this exists for, stated once. The owner of the device on the bench opens its
// interface at 192.168.50.122, WHICH THEY LOOKED UP IN THE ROUTER, while the sticker on the case
// and the Wi-Fi list on their phone both say `comfoair-8ae5`. The whole argument is one sentence:
// the owner must see THE SAME NAME on the sticker, in the Wi-Fi list and in the address bar.
//
// TWO HALVES, and the split is the same one comfoair_led and comfoair_captive already use.
// comfoair_mdns_name.c is pure -- no ESP-IDF, no socket, no clock -- and test/test_mdns_name covers
// it on the host. comfoair_mdns.c is the espressif/mdns component, wrapped in #ifdef ESP_PLATFORM
// because the host build compiles every .c in this directory the moment a suite includes this
// header.
//
// NOTHING REBOOTS BECAUSE A PEER IS ABSENT (AGENTS.md), IN FULL. Nothing here may take the device
// down or hold the network up: mDNS that does not start is logged and the device carries on,
// exactly as comfoair_led and the CAN bus do in app_main. A device without a name is today's
// device; a device that did not boot because of a name is a brick.
//
// SECRETS: the contract of this component is that it CANNOT leak one. comfoair_mdns_facts_t has
// three fields and none of them is a secret, and that is construction rather than discipline --
// TXT is multicast to 224.0.0.251 on every raised interface, read by anybody on the link, and for
// this device "anybody on the link" includes a guest VLAN and everybody in radio range of the open
// setup access point. The test to apply to a sixth field: would you print it on the outside of
// the case?
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- the names ---------------------------------------------------------------------------------

// "comfoair-" plus four hex characters. Thirteen, and it never varies: the length is part of what
// makes the name printable on a sticker.
#define COMFOAIR_MDNS_HOST_MAX 13

// RFC 6763 §4.1.1: a DNS-SD instance name is up to 63 OCTETS of UTF-8. Octets, not characters --
// see comfoair_mdns_instance().
#define COMFOAIR_MDNS_INSTANCE_MAX 63

// The TXT keys: board, ver, id, path, api. The list is closed, and test_mdns_name pins it.
#define COMFOAIR_MDNS_TXT_MAX 5

// RFC 6763 §6.1: one TXT item goes on the wire as ONE length-prefixed string, so `key` + `=` +
// `value` cannot exceed 255 octets. A pair over it is dropped rather than truncated -- half a
// version string is worse than no version string.
#define COMFOAIR_MDNS_ITEM_MAX 255

// RFC 6763 §6.2: keeping the whole TXT under 200 octets is what lets it travel in one packet
// beside the SRV and the A record. NOT enforced at runtime, and that is deliberate: the only input
// that could exceed it is a board name, which is a string literal in components/board, and a check
// that can only fire on a compile-time constant belongs in a test. test_mdns_name measures it.
#define COMFOAIR_MDNS_TXT_BUDGET 200

// The same layout as espressif/mdns's own mdns_txt_item_t (mdns.h:66-69), DECLARED HERE rather
// than included from there, because this header has to compile on the host and that one does not.
// comfoair_mdns.c holds the static_assert that keeps the two in step.
typedef struct {
    const char *key;
    const char *value;
} comfoair_mdns_txt_t;

// What goes out in TXT, and THE ONLY THING THAT CAN. No SSID, no broker, no
// password, and no "is this device provisioned yet" flag -- the last one is refused by
// construction and not by review, because a field asserting how protected the device is right now
// is the one class worth making unrepresentable. There is no field for any of them, so no edit of
// comfoair_mdns.c can put one on the wire.
//
// `id` IS THE STA MAC AND THAT COSTS SOMETHING, and the cost is named rather than left implicit:
// the same twelve characters are the MQTT client id (comfoair_mqtt_client.c,
// cfg.credentials.client_id), and a second client connecting with them knocks this device off the
// broker. The trade is taken -- the MAC is trivially derivable from the beacons of the access
// point anyway (SoftAP MAC = STA MAC + 1) -- but it is a trade, not a freebie.
typedef struct {
    const char *board;      // board_get()->name
    const char *version;    // esp_app_get_description()->version
    const char *device_id;  // comfoair_net_device_id(), twelve lower-case hex
} comfoair_mdns_facts_t;

// `short_id` is comfoair_net_short_id()'s four characters and is NOT read from a MAC here. That is
// the whole reason this function is testable: the trap is which MAC produced them
// (ESP_MAC_WIFI_SOFTAP, the access point's and the sticker's) and it lives in comfoair_net, behind
// the single producer.
//
// Always writes a legal single DNS label (RFC 6762 §3), whatever it is handed: anything outside
// [0-9a-f] becomes '0'. The only caller can produce nothing but hex, and a contract that holds
// because of its caller is not a contract -- especially across a component boundary.
void comfoair_mdns_hostname(const char *short_id, char out[COMFOAIR_MDNS_HOST_MAX + 1]);

// The DNS-SD instance name: the owner's own name for the device, which is a DIFFERENT STRING FROM
// THE HOST NAME and deliberately so. Renaming moves this and leaves
// `comfoair-XXXX.local` alone, so the bookmark, the sticker and the tab the owner is renaming from
// all keep working.
//
// Falls back to `hostname` when `device_name` is empty or NULL. THAT FALLBACK IS DEFENSIVE AND
// UNREACHABLE FROM A BOOTED DEVICE, and saying so is the correction of a claim that used to stand
// here: comfoair_config_sanitize() refills device_name from default_name() whenever it is empty or
// invalid (comfoair_config.c:308-310), and default_name() writes "ComfoAir Q <last four of the STA
// MAC>", never "" (:145-164). So the instance name on the bench unit is `ComfoAir Q 8ae4` while its
// host name is `comfoair-8ae5` -- two strings from two MACs, because not every name can agree and
// the pair kept in step is the one a person TYPES. The fallback stays because a record
// with no instance name is invalid on the wire and this function must not be able to produce one.
//
// Truncates at 63 OCTETS WITHOUT SPLITTING A UTF-8 SEQUENCE. COMFOAIR_CONFIG_NAME_MAX is 32, so
// that branch is unreachable from the settings page today; test_mdns_name pins the arithmetic so
// that raising NAME_MAX fails on the host rather than shipping half a character.
void comfoair_mdns_instance(const char *device_name, const char *hostname,
                            char out[COMFOAIR_MDNS_INSTANCE_MAX + 1]);

// The five TXT pairs, in the order above, into a caller-owned array. Returns how many were
// written, which is FIVE ONLY IF EVERY FACT WAS USABLE: a fact that is NULL, empty, or long enough
// to break RFC 6763 §6.1 is dropped, and the other pairs still go out. `esp_app_get_description()
// ->version` is a 32-byte field that is not required to be terminated, which is the reachable way
// to arrive here with nothing usable in `version`.
size_t comfoair_mdns_txt(const comfoair_mdns_facts_t *facts, comfoair_mdns_txt_t *out, size_t cap);

// How many octets those pairs occupy on the wire: for each, one length prefix plus key, '=' and
// value. Exists so the packet budget above is a measurement in a test and not an assertion in a
// comment.
size_t comfoair_mdns_txt_bytes(const comfoair_mdns_txt_t *items, size_t n);

// Copy a fact into a bounded buffer, always terminating, and READING NO MORE THAN `cap - 1` OCTETS
// FROM `src`. That second half is the whole reason this is a function rather than snprintf("%s"):
// esp_app_get_description()->version is a 32-BYTE FIELD, not a string (esp_app_format.h), and an
// image whose version fills it carries no terminator -- so `%s` walks off the end of the
// descriptor. Called with cap one greater than the field, this reads exactly the field and stops.
//
// Returns the number of octets written. NULL `src` gives an empty string, which comfoair_mdns_txt()
// then drops -- an empty TXT value is not a valid pair (RFC 6763 §6.4).
//
// In the pure half so that the bound is a host case and not a claim: the caller that needs it is
// the driver, which no test can reach.
size_t comfoair_mdns_copy_field(char *out, size_t cap, const char *src);

// --- the responder -----------------------------------------------------------------------------
//
// Everything below is #ifdef ESP_PLATFORM in comfoair_mdns.c and does nothing on the host.

// Everything comfoair_mdns needs, HANDED IN BY THE CALLER rather than fetched.
//
// This component includes NO OTHER COMPONENT'S HEADER, and that is a build constraint before it is
// a taste: comfoair_mdns.c is compiled by the host test build (it is #ifdef'd out, but PlatformIO's
// dependency finder does not evaluate the preprocessor), so an #include of comfoair_net.h here
// drags the radio into every host suite that reaches this component -- test_mdns_name itself, and
// the two button suites through comfoair_button_reset.c. That is not a hypothetical: it was
// measured, `pio test -e native -f test_mdns_name` failing on comfoair_net.c's esp_event.h. The
// same reasoning is written out in comfoair_button/CMakeLists.txt for comfoair_net.
//
// So src/main.cpp fills this in: `short_id` from comfoair_net_short_id(), `device_name` from the
// REDACTED projection comfoair_net_config_snapshot() gives (never comfoair_net_broker(), which
// carries the broker password in the clear), `facts` from board_get(), esp_app_get_description()
// and comfoair_net_device_id().
//
// The strings are BORROWED FOR THE LENGTH OF THE CALL ONLY -- everything that has to outlive it is
// copied inside -- so a caller may build them all on its own stack.
typedef struct {
    const char *short_id;     // comfoair_net_short_id()'s four characters
    const char *device_name;  // the owner's name for the device; NULL or "" is normal
    // The port comfoair_http actually came up on, or 0 WHEN IT DID NOT START. Zero registers the
    // host name alone and no service, because a name that resolves to a refused
    // connection tells the owner the device is broken.
    uint16_t              http_port;
    comfoair_mdns_facts_t facts;
} comfoair_mdns_config_t;

// Start the responder: one mdns_init(), the host name, the instance name, and -- if the web server
// is up -- one `_http._tcp` service carrying the TXT above.
//
// IF THE NAME IS ALREADY TAKEN, THIS DEVICE SIMPLY DOES NOT APPEAR. That is read from the
// component's source rather than guessed at: espressif/mdns has NO
// conflict resolution. A lost probe reaches mdns_priv_pcb_set_probe_failed(), which increments a
// counter (mdns_pcb.c:331-334) that is read in exactly one place -- to slow the retry from 120 ms
// to 1000 ms (:385). No suffix is appended, no name is changed, no event is raised. The PCB keeps
// probing and never announces, so the symptom is SILENCE, not `comfoair-a1b2-2.local`.
//
// We add no scheme of our own, and that is a decision, not an omission: an automatic
// rename produces a name that is on neither the label nor the Wi-Fi list. The owner who draws the
// 1-in-65536 already sees two identical SSIDs; the way out is renaming one device, which moves the
// instance name (comfoair_mdns_rename) and not the host name.
//
// CALL IT AFTER comfoair_net_start() AND AFTER comfoair_http_start(). The first is not a
// preference: mdns needs esp_netif_init() and the default event loop, and comfoair_net is what
// creates both. The second keeps the SRV from naming a port nobody is listening on.
//
// NEVER STOPPED AND NEVER CYCLED afterwards. The espressif/mdns component subscribes
// to WIFI_EVENT, IP_EVENT and ETH_EVENT itself and raises and drops its per-interface PCBs on
// address changes on its own. DO NOT hang this off comfoair_net_cb_t: that callback fires on STATE
// changes only, and an address that changes without a disconnect never produces one
// (comfoair_net.c, set_state()) -- the record would go stale in silence.
//
// Returns the first error that stopped it, ESP_OK otherwise. A caller LOGS IT AND CARRIES ON.
esp_err_t comfoair_mdns_start(const comfoair_mdns_config_t *config);

// The owner renamed the device: move the DNS-SD instance name and NOTHING ELSE. Cheap, idempotent,
// and a no-op before comfoair_mdns_start() or when no service was registered. Best-effort by
// design -- a rename that the responder refuses is a cosmetic loss, never a failed save, so this
// returns nothing and logs at WARN.
void comfoair_mdns_rename(const char *device_name);

// How long a caller should give the goodbye before it stops caring. See below for why a caller has
// to make that judgement at all.
#define COMFOAIR_MDNS_GOODBYE_MS 150

// Goodbye: mdns_free() sends TTL 0 for the host and the services, so the neighbours' caches drop
// the record instead of holding an address that leads nowhere for up to two minutes.
//
// THIS FUNCTION RETURNS AT ONCE AND mdns_free() RUNS ON A TASK OF ITS OWN, and that is not tidiness
// -- mdns_free() CAN BLOCK FOR EVER. It was read, not assumed: mdns_service_remove_all() takes the
// service semaphore with portMAX_DELAY (mdns_responder.c:1629 through the MDNS_SERVICE_LOCK macro,
// mdns_service.c:43), and service_task_stop() then spins
//
//     while (s_service_task_handle) { vTaskDelay(10 / portTICK_PERIOD_MS); }
//
// (mdns_service.c:360-363) until the mdns task picks ACTION_TASK_STOP off its queue. Neither wait
// has a timeout. Normally this is tens of milliseconds; an mdns task wedged in a send on a netif
// that has just gone away never finishes it.
//
// THE CALLER OF THIS IS ONE STATEMENT FROM esp_restart(), ON A DEVICE WHOSE NVS IS ALREADY ERASED.
// Blocking there is not a lost goodbye, it is an erased device that never reboots and shows no
// light. So: this call hands the work to a task and comes straight back, the caller waits
// COMFOAIR_MDNS_GOODBYE_MS, and then restarts WHETHER OR NOT the goodbye finished. A packet that
// did not get out costs the neighbours a stale cache entry for a couple of minutes; a restart that
// did not happen costs the owner their device.
//
// Best-effort in the same sense as the retained `offline` published before a clean MQTT stop
// (comfoair_mqtt_client.c) -- but that one really does return, and this one is why the distinction
// had to be written down. A no-op if the responder never started, so the caller needs no flag.
void comfoair_mdns_goodbye(void);

#ifdef __cplusplus
}
#endif
