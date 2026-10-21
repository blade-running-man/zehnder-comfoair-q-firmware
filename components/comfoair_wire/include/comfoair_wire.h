// The four documents phase 5 puts on the wire, in both directions.
//
// GET  /api/config     what the settings page loads
// POST /api/config     what it saves
// POST /api/provision  the network the owner picked
// GET  /api/provision  why it is not connecting
// GET  /api/wifi/scan  what the radio can hear
//
// SEPARATE FROM comfoair_http ON PURPOSE, and the reason is the same one comfoair_policy exists
// for: a handler that decides what a body means can only be exercised by flashing a board, and
// the bodies this reads are written by whoever is in radio range of an OPEN access point.
// Everything that can be decided without a socket is decided here, where test/test_wire reaches
// it -- the handler next door is left with "read the body, call this, send what it says".
//
// SEPARATE FROM comfoair_api TOO, although both render JSON. comfoair_api projects the STATE
// MODEL, which is generated from pdo_table.py and must never grow a second list (AGENTS.md).
// These five documents are the configuration surface: different source of truth, different
// lifetime, and the field names here are comfoair_config's `name` column rather than an entity
// key. They share one thing, and it is deliberately shared rather than duplicated -- the escaper
// in comfoair_json.
//
// THE ONE RULE THAT OUTRANKS EVERYTHING IN THIS FILE: no value for which comfoair_secret_key() is
// true is ever rendered, and no error message ever quotes a submitted value. Both come from the
// same fact -- everything this device logs is served by GET /api/log, and on the setup access
// point that is world-readable. A message names a FIELD, never its contents.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comfoair_command.h"
#include "comfoair_config.h"
// For the three enumerations of the broker block below -- and for their WORDS, which are
// comfoair_mqtt_backoff's and are never invented here. It costs this component nothing that
// matters: that header needs nothing at all, IDF or otherwise, so including it does not move the
// line that keeps comfoair_wire host-buildable.
#include "comfoair_mqtt_backoff.h"
#include "comfoair_phase.h"
#include "comfoair_provision.h"
#include "comfoair_secrets.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- how a submission can fail ----------------------------------------------------------------

typedef enum {
    COMFOAIR_WIRE_OK,
    // The body is not a flat JSON object at all. DISTINCT from an empty one: every field of a
    // config patch is optional, so `{}` is a legitimate save that changes nothing, and reporting
    // an unreadable body as that would tell the owner Saved over a request that stored nothing.
    COMFOAIR_WIRE_BAD_BODY,
    // A field is present and is not what it has to be -- a number where a string belongs, a
    // string longer than the device could ever store. `field` names which.
    COMFOAIR_WIRE_BAD_FIELD,
    // A validator in comfoair_config refused it. `err` is its answer and
    // comfoair_config_strerror() is the sentence.
    COMFOAIR_WIRE_REFUSED,
    // The flash holds a schema this build does not understand (a rollback). Nothing is written.
    COMFOAIR_WIRE_READ_ONLY,
    // A field that belongs to a different route. Today that is exactly the Wi-Fi pair arriving at
    // POST /api/config, and it has its own status rather than sharing BAD_FIELD because the two
    // need different sentences: BAD_FIELD says "you typed something impossible", and this says
    // "you typed it into the right form and sent it to the wrong door". A caller reaches this by
    // echoing back the whole GET /api/config document -- which carries wifi_ssid, because the
    // page has to show which network is configured -- so it is a mistake somebody makes without
    // doing anything unreasonable, and the answer has to name the route that works.
    COMFOAIR_WIRE_WRONG_ROUTE,
} comfoair_wire_status_t;

typedef struct {
    comfoair_wire_status_t status;
    comfoair_config_err_t  err;    // meaningful when status is COMFOAIR_WIRE_REFUSED
    // The key at fault, as a pointer to a literal. NEVER the value: this string ends up in an
    // HTTP body and in the log ring, and the log ring is public.
    const char *field;
} comfoair_wire_result_t;

// One sentence for the owner, with no value of any kind in it. Safe to log and safe to send.
const char *comfoair_wire_strerror(comfoair_wire_result_t result);

// --- POST /api/provision ------------------------------------------------------------------

// Decodes the network the owner picked into a record the radio can be handed.
//
// `stored` is what is in NVS now -- it may be NULL on a device that has never been configured --
// and it is needed because `wifi_psk` has THREE states, not two (web/src/api/client.ts,
// ProvisionRequest):
//
//   {"wifi_ssid":"Kitchen","wifi_psk":"hunter2"}   store this key
//   {"wifi_ssid":"Guest","wifi_psk":""}            an OPEN network: no key at all
//   {"wifi_ssid":"Kitchen"}                        keep the key already stored
//
// The third is the same rule comfoair_config_patch_t states for every other string, so this route
// needs no vocabulary of its own: the field goes through comfoair_secret_decide() and absent is
// KEEP, "" is CLEAR, anything else is STORE.
//
// THE SENTINEL IS REFUSED, and that refusal is the reason this function exists rather than the
// handler doing it inline. "__UNCHANGED__" is thirteen printable bytes and passes
// comfoair_config_check_psk whole -- at least eight characters, no NUL, not sixty-four so no hex
// test -- so a handler that wrote it through would leave the device holding credentials that
// cannot associate. Credentials existing is what stops the access point coming back up, so the
// device would simply disappear: comfoair_provision.c calls that the likeliest catastrophe in the
// project, and this path reaches it with nobody having made a typo.
//
// `out` is written only on COMFOAIR_WIRE_OK, and the pair it holds has already been through
// comfoair_config_wifi_usable().
comfoair_wire_result_t comfoair_wire_parse_provision(const char *body,
                                                     const comfoair_wifi_t *stored,
                                                     comfoair_wifi_t *out);

// --- POST /api/config ---------------------------------------------------------------------

// Where the patch's strings live. comfoair_config_patch_t holds POINTERS, and NULL in it means
// "the key was absent" -- so the strings need somewhere to sit that outlives the parse and is not
// the request body, which the server may reuse.
//
// DO NOT put one of these on a request handler's stack: it is the better part of a kilobyte and
// httpd runs its handlers on one task with a fixed stack. A static in the handler is the intended
// home, and it holds the broker password and the owner's new UI password in the clear until the
// next parse -- so it is also a struct that must never reach ESP_LOG*.
typedef struct {
    char mqtt_host[COMFOAIR_CONFIG_HOST_MAX + 1];
    char mqtt_user[COMFOAIR_CONFIG_USER_MAX + 1];
    char mqtt_password[COMFOAIR_CONFIG_BROKER_PASS_MAX + 1];
    char topic_prefix[COMFOAIR_CONFIG_PREFIX_MAX + 1];
    char device_name[COMFOAIR_CONFIG_NAME_MAX + 1];
    char ui_password[COMFOAIR_CONFIG_UI_PASS_MAX + 1];
} comfoair_wire_patch_storage_t;

// Decodes a settings submission. Nothing is validated here beyond types and lengths -- what a
// value may BE is comfoair_config_apply()'s answer, and asking it twice in two places is how the
// two come to disagree.
//
// AN UNKNOWN KEY IS IGNORED, so a newer page can talk to an older firmware without every save
// failing. THE TWO WI-FI KEYS ARE NOT: they are refused by name, because /api/config is not the
// route the network travels on (web/src/api/client.ts, ConfigPatch) and silently dropping a
// credential the owner typed into a form is worse than telling them where it belongs.
//
// Absent is not empty, everywhere and for every field. Absent leaves the stored value alone;
// empty clears it. The whole sentinel scheme exists because those two were once the same thing
// and one save of an unrelated setting wiped the broker credentials.
// `permanent_manual` is out of band for the same reason it is out of band on the way out: it is
// not a field of the stored document. It is left untouched when the key is absent, so a page that
// does not know about it cannot turn it off by saving.
comfoair_wire_result_t comfoair_wire_parse_config(const char *body, comfoair_config_patch_t *out,
                                                  comfoair_wire_patch_storage_t *storage,
                                                  bool *has_permanent_manual,
                                                  bool *permanent_manual);

// --- GET /api/config ---------------------------------------------------------------------

// Renders the projection. Returns bytes written, or 0 if it did not fit -- and 0 is a DEFECT to
// report, never a short document to serve: a truncated JSON body with a 200 in front of it is
// what phase 4's counter-review found the entity list twenty-three bytes away from.
//
// `known_good` comes from the provisioning machine (comfoair_prov_has_known_good), not from the
// stored document, and the page cannot tell the truth without it: it is the ONLY condition under
// which a failed provisioning rolls back, and `wifi_ssid != ""` is not a substitute -- an SSID
// stored beside a mistyped key has never produced an address.
//
// comfoair_config_public_t is what goes in, never comfoair_config_t. The projection is where the
// two passwords become a sentinel or an empty string, and taking the raw document here would put
// the decision to redact inside a renderer instead of in front of it.
// `permanent_manual` is the owner's answer to "should a level change also pin the unit into manual
// mode" (comfoair_command.h). It travels here rather than in comfoair_config_t for the same reason
// `known_good` does: it is not part of the stored DOCUMENT -- comfoair_control owns it, because
// comfoair_control is what builds commands from it -- and a second writer for one value is two
// sources of truth. The document is where the owner can SEE and CHANGE it, which is the only thing
// it was missing.
size_t comfoair_wire_render_config(const comfoair_config_public_t *pub, bool known_good,
                                   bool permanent_manual, char *out, size_t cap);

// --- GET /api/wifi/scan ----------------------------------------------------------------

// One network the radio heard. The row shape lives HERE rather than in comfoair_net because this
// is what turns it into bytes, and comfoair_net cannot be built on the host.
typedef struct {
    // NUL-terminated. "" is a hidden network, which is a true thing to report -- the page drops
    // the row, but "the scan saw seven things and can name five" is worth being able to say.
    char   ssid[COMFOAIR_CONFIG_SSID_MAX + 1];
    int8_t rssi;    // dBm: negative, larger is better
    bool   secure;  // false ONLY for a genuinely open network
} comfoair_wire_network_t;

// A JSON ARRAY at the top level, possibly empty. Not an object with a `networks` key -- the
// handler renders one thing and this is it (web/src/api/client.ts, scanNetworks).
//
// Duplicates are NOT filtered: one sweep returns a record per BSSID, so a mesh answers several
// times under one name, and which to show is a presentation question the page has more
// information about than this does.
size_t comfoair_wire_render_scan(const comfoair_wire_network_t *list, size_t count, char *out,
                                 size_t cap);

// --- GET /api/provision ----------------------------------------------------------------

// Everything the status document is built from. Filled by comfoair_net under its lock so the
// fields agree with one another: a state saying CONNECTED beside a failure of "wrong-password" is
// a page nobody can act on.
//
// NO PSK, EVER, and no SSID but the configured one. An SSID is in every beacon that network
// sends, so it is not a secret, and it is the one fact that makes "it will not connect"
// diagnosable. The key is not in comfoair_prov_t either -- that struct has a static_assert saying
// so -- and it must not arrive here by another road.
typedef struct {
    comfoair_prov_state_t   state;
    comfoair_prov_mode_t    mode;
    comfoair_prov_failure_t failure;
    // The raw wifi_err_reason_t behind `failure`, for an owner looking something up. 0 when there
    // is none: the no-address answer is the ABSENCE of an event, not an event.
    uint8_t  failure_reason;
    bool     provisioned;
    bool     has_credentials;
    bool     has_known_good;
    bool     connected;
    bool     ap_on_air;
    bool     window_open;
    // COMFOAIR_PROV_NEVER for a window that never closes and for a fallback that is not
    // scheduled. Rendered as null, never as 0: "no time left" and "no end" are opposite answers
    // and a page that shows 0 for both counts a device down to a deadline it does not have.
    uint32_t window_remaining_ms;
    uint32_t fallback_in_ms;
    // How long this firmware has been running. 64-BIT AND NOT 32, which is not caution: a 32-bit
    // millisecond counter wraps every 49.7 days, so a device that has run since spring would
    // report a fresh boot -- and telling "the house is still coming up" from "it broke an hour
    // ago" is the entire job of this member. Filled from the SAME clock read the two durations
    // above are measured against (comfoair_net_status()), so a page cannot compute two different
    // boot instants out of one document.
    uint64_t uptime_ms;
    // The signal of the network the station is on, in dBm, and NOT a fifth comfoair_prov_failure_t
    // -- deliberately. "The router is in the next room and DHCP is silent" and
    // "we are at the edge of the cell" are two support questions with one answer; the raw
    // `failure_reason` beside it already separates 201 from 212 with no firmware change, so no
    // fifth verdict is added and this costs comfoair_prov_t zero bytes.
    //
    // A FLAG AND NOT A SENTINEL. esp_wifi_sta_get_ap_info() answers nothing at all when the
    // station is not associated, and dBm has no free value to steal -- 0 is a legal reading of a
    // radio next to an antenna and -128 is a legal int8_t -- so "no reading" is its own field.
    // Same rule, and same reason, as comfoair_mqtt_ladder_t.retry_scheduled being a bool rather
    // than a zero. Rendered as null when false; a page shown 0 would report a perfect signal on a
    // device that is not on a network.
    //
    // THE SECOND MEMBER comfoair_net_status() DOES NOT FILL, and for a different reason from
    // `phase` below: reading it enters the Wi-Fi stack and takes that stack's lock, while
    // comfoair_phase_monitor calls comfoair_net_status() once a second and never looks at the
    // answer. Both HTTP handlers call comfoair_net_rssi() themselves. DO NOT move it into the
    // snapshot to tidy the handlers up -- that puts a lock the monitor does not need into the one
    // task whose contract is that no absent peer can stop it.
    bool     has_rssi;
    int8_t   rssi;
    char     ssid[COMFOAIR_CONFIG_SSID_MAX + 1];
    char     ap_ssid[33];
    char     ip[16];
    // What the WHOLE DEVICE is doing, which is a different question from `state` above and often
    // has a different answer: a device can be `connected` to its network and `bus-silent` at the
    // same moment, and until this member the second half of that sentence existed only as a colour
    // in a technical room -- a colour nobody can paste into a support message.
    //
    // THE OTHER MEMBER comfoair_net_status() DOES NOT FILL (see `rssi` above for the second, which
    // is left out for an unrelated reason). The aggregate has an owner of its own
    // (comfoair_phase_monitor_get()) and the provisioning machine cannot see the bus or the broker;
    // the handler that renders this document sets it after taking the snapshot. That is safe by
    // construction rather than by discipline: COMFOAIR_PHASE_BOOT is the zero value, so a caller
    // who fills nothing reports a device that is starting up, which is exactly what it means
    // (comfoair_phase.h:44-47). DO NOT give it a "not known" spelling -- BOOT already is one.
    device_phase_t phase;
} comfoair_wire_provision_t;

// One spelling of each state and mode, decided here. Four different answers to the owner is an
// invariant (docs/device/networking.md), and the REST projection and the MQTT one inventing two
// spellings of them is exactly how that invariant gets lost.
const char *comfoair_wire_state_name(comfoair_prov_state_t state);
const char *comfoair_wire_mode_name(comfoair_prov_mode_t mode);

// And one spelling of a broker's refusal, decided here for the same reason: the MQTT log line
// needs the words now and GET /api/status needs the same words later, and the two inventing their
// own would be that invariant lost again. Codes are MQTT 3.1.1 section 3.2.2.3 -- 1
// unacceptable protocol version, 2 identifier rejected, 3 server unavailable, 4 bad username or
// password, 5 not authorized -- and 0 means no refusal happened. Only 4 and 5 are about
// credentials, so ONE message covering all five would be a lie about the cause for three of them.
// Never NULL, and an unrecognised code gets the "unknown" spelling rather than an invented one.
const char *comfoair_wire_connack_name(uint32_t code);

// The broker connection as GET /api/status reports it. A FLAT COPY OF comfoair_mqtt_stats_t AND
// DELIBERATELY NOT THAT STRUCT: comfoair_mqtt drags in esp-mqtt and FreeRTOS, so a comfoair_wire
// that included its header could no longer be built or exercised on the host -- which is the whole
// reason this document is rendered here rather than in the handler (see this component's
// CMakeLists.txt, and the same argument comfoair_policy is built on). comfoair_http performs the
// mapping between the two, because it is the only translation unit that already depends on both.
//
// COUNTERS ONLY. No host, no user, no password: which broker is configured is GET /api/config's
// answer, where the projection has already turned every secret into a sentinel, and a diagnostic
// struct is exactly the shape somebody later adds "the host, to help with debugging" to.
//
// OWNED BY THE CALLER and filled from comfoair_mqtt_get_stats() immediately before rendering.
// TASK CONTEXT: whichever task renders -- in the firmware that is httpd's. Every field is a
// SNAPSHOT of counters the publisher task keeps moving, so two members may disagree by one publish
// and no reader may treat them as simultaneous. The renderer copies nothing and keeps no pointer,
// so the struct may live on the caller's stack.
typedef struct {
    bool     configured;   // a broker host is stored -- NOT that one was ever reached
    bool     connected;
    uint32_t published;
    uint32_t commands;     // accepted from the broker
    uint32_t rejected;     // arrived and were refused: bad topic, bad payload, or retained
    uint32_t reconnects;
    // The CONNACK refusal code, 0 when no refusal was recorded. Rendered beside
    // comfoair_wire_connack_name()'s spelling of it, so the number an owner can look up and the
    // sentence they can read cannot come to disagree. MQTT 3.1.1 section 3.2.2.3.
    uint32_t last_connack;

    // --- the retry ladder and what it makes visible ---
    //
    // AFTER THE EIGHT ABOVE AND NEVER AMONG THEM. The eight have been on the wire since b068b8c
    // and a reader walks this block from the front; inserting a member between two of them is a
    // breaking change that no contains() assertion notices, which is why
    // test_the_new_fields_come_after_the_existing_eight pins the ORDER and not the presence.
    //
    // WHY THEY EXIST AT ALL, measured rather than argued: over ten hours of
    // hammering an unreachable broker not one of the eight above moves. `connected` stays false,
    // the four counters stand still and `last_connack` is 0 because only a CONNACK refusal writes
    // it -- so an owner who opens the document twice half a day apart gets a byte-for-byte
    // identical answer and cannot tell "we are trying and failing" from "the task is dead".
    //
    // The three enumerations are comfoair_mqtt_backoff's, held as its own types so -Werror=switch
    // reaches every reader of them, and their WORDS are its too: one spelling for the log line and
    // for this document, the same invariant `mode` and `failure` above carry.
    comfoair_mqtt_link_t link;
    comfoair_mqtt_fail_t failure;
    // Failed attempts since the last SETTLED session -- sixty seconds of one, not a connection
    // that happened. Saturates at COMFOAIR_MQTT_ATTEMPT_MAX with the delay.
    uint32_t             attempts;
    // Milliseconds to the next attempt, or COMFOAIR_PROV_NEVER when NONE IS SCHEDULED, which is
    // rendered as null. "No attempt is scheduled" and "an attempt in 0 ms" are opposite answers --
    // the first is a dial in flight -- and the sentinel is the one this document already uses for
    // `fallback_ap_in_ms`. A second sentinel would be a second rule.
    uint32_t             retry_in_ms;
    comfoair_mqtt_subs_t subscriptions;
    // Sessions lost. THE PAIR WITH `reconnects` IS THE POINT and is why there is no
    // `identity-conflict` value anywhere in this block: a second device dialling the broker with
    // the same client id is NOT observable on the wire -- it arrives as an ordinary disconnect --
    // but it is observable as these two climbing together while `connected` flickers. That makes
    // it a sentence on a diagnostics screen, not a value of an enumeration.
    uint32_t             disconnects;
    // How many times the PUBLISHER TASK has rebuilt the client, counted AFTER the rebuild. An
    // epoch of APPLICATION and not of request: a browser that saved a broker remembers this
    // number, then polls until it changes AND link is no longer "connecting". Moved when the
    // request arrived it would change while the previous client was still connected, and the page
    // would read that connection's verdict as the new one's.
    uint32_t             applied_epoch;
    // The raw esp_transport_sock_errno, 0 when none was recorded. RAW BY PRECEDENT AND NOT BY
    // TASTE: `failure_reason` above is already a raw Wi-Fi reason code on this same wire.
    // ECONNREFUSED (the broker is switched off), ETIMEDOUT (wrong address, or a firewall) and
    // EHOSTUNREACH (no route) need three different actions from the owner, and the class `tcp`
    // collapses all three.
    //
    // DO NOT READ 0 AS A DEFECT. When esp_mqtt_connect times out, esp-mqtt dispatches
    // MQTT_EVENT_DISCONNECTED and no MQTT_EVENT_ERROR at all, so nothing fills the
    // socket errno for that failure and this stays 0 BY CONSTRUCTION. The failure is still fully
    // described, by `failure`, `attempts` and `retry_in_ms`, which all move for it.
    uint32_t             last_transport_errno;
} comfoair_wire_mqtt_t;

// Renders the status document. Returns bytes written, or 0 if it did not fit -- and 0 is a DEFECT
// the caller must turn into an error, never a short document to serve: the handler answers it with
// 500, because a truncated JSON body behind a 200 is what phase 4's counter-review found the
// entity list twenty-three bytes away from.
//
// `mqtt` MAY BE NULL, AND NULL OMITS THE WHOLE `mqtt` MEMBER RATHER THAN RENDERING ZEROS. The two
// are opposite answers and not a formatting choice. A block of zeros is a device that looked at its
// broker connection and found nothing had happened yet; no block at all is a document rendered
// where nobody was in a position to look. POST /api/provision's reply is the second case -- it is
// answered before the radio has even moved to the owner's network -- and a page that read
// "published": 0 there would be shown a configured broker gone silent, which is the fault this
// member exists to make visible. A caller that passes a pointer MUST have filled every field of it;
// there is no partial form of the block.
// Who this device is, as GET /api/status reports it. BORROWED, like comfoair_wire_mqtt_t and for
// the same reason: the values come from esp_read_mac and esp_app_get_description, which
// comfoair_wire must never call -- a component that cannot build on the host cannot have its
// renderer tested against the document it renders.
//
// device_id is the twelve lower-case hex characters of the STA MAC, the same string
// comfoair_net_device_id() gives the MQTT client, and the same identity comfoair_ha uses for
// discovery. Two spellings of "who this device is" is the identity invariant lost again -- and the
// consumer that reads this keys a Home Assistant config entry on it, so a second spelling would
// duplicate every entity the first time the two disagreed.
typedef struct {
    const char *device_id;
    const char *mac;         // colon-separated, for Home Assistant's device `connections`
    const char *sw_version;  // esp_app_get_description()->version
} comfoair_wire_device_t;

// `device` MAY BE NULL, AND NULL OMITS THE THREE KEYS RATHER THAN RENDERING EMPTY STRINGS -- the
// same rule `mqtt` follows, for the same reason put_duration renders NEVER as null: "" and "we
// have nothing to say" are different answers, and a client keying a config entry on an empty
// string keys it on nothing at all.
size_t comfoair_wire_render_provision(const comfoair_wire_provision_t *status,
                                      const comfoair_wire_mqtt_t *mqtt,
                                      const comfoair_wire_device_t *device, char *out, size_t cap);

// --- POST /api/entities/<key> ------------------------------------------------------------

// Decodes `{"value": ...}` -- what writeEntity() sends (web/src/api/client.ts). StateValue is a
// number, a boolean or a string, and WHICH decides what the entity can accept, so the shape is
// carried out rather than coerced: a select written with a number would be an index whose meaning
// depends on the firmware's build.
//
// `text` is where a string value is copied to; `out->text` points into it and dies with it. An
// option label longer than `cap` is REFUSED rather than truncated -- a truncated label matches a
// different option, or none, and there is no reading of that which cannot send the wrong command.
//
// There is no default. A body with no `value`, or with `null`, is refused: a write the owner made,
// the device answered 200 to, and did nothing for is worse than an error.
comfoair_wire_result_t comfoair_wire_parse_entity_write(const char *body,
                                                        comfoair_command_value_t *out, char *text,
                                                        size_t cap);

// --- POST /api/ops/<name> ----------------------------------------------------------------

// Where an operation's two string parameters live. comfoair_command_params_t holds POINTERS and
// NULL in it means "the key was absent", so the strings need somewhere that outlives the parse.
typedef struct {
    char mode[16];     // auto|open|closed|balanced|supply_only|exhaust_only
    char profile[16];  // normal|cool|warm
} comfoair_wire_op_storage_t;

// Decodes an operation body into its parameters. Nothing is validated here beyond types: WHICH
// parameters an operation needs, and what a value may be, is comfoair_command's answer.
//
// ABSENT STAYS ABSENT. None of the six operations has a default (docs/interfaces/rest-api.md),
// and for three of them an invented duration would mean "forever". Note also that 0 is a legal
// duration with a meaning -- "cancel" for boost, "forever" for the rest -- so `has_duration` and a
// zero are a different answer from `has_duration` false, and a parser that conflated them would
// turn a cancellation into a refusal.
comfoair_wire_result_t comfoair_wire_parse_operation(const char *body,
                                                     comfoair_command_params_t *out,
                                                     comfoair_wire_op_storage_t *storage);

#ifdef __cplusplus
}
#endif
