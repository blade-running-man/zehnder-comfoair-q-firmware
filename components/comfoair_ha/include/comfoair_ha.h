// The MQTT surface: which topic an entity lives on, and what Home Assistant has to be told
// about it to make it appear.
//
// PURE, for the reason every pure component in this project is pure -- a discovery payload that
// can only be checked by flashing a board and watching somebody's Home Assistant is a payload
// nobody checks. Every key below was verified against Home Assistant's own validating schema
// (a 2026.9 install vendored in ../zehnder-comfoair-q-ha), not against documentation and not
// against memory: an invented discovery key produces an entity that silently never appears.
//
// THE TOPIC TREE IS FLAT, and that is deliberate: no <group> segment sits between the prefix and
// the key. The registry has no group column and must not grow one: the state page derives a
// group heuristically from device_class/unit/kind and says why -- "a key table would be a second
// entity list that drifts silently the first time the firmware gains a sensor". The registry's own
// header already calls `key` the "stable id: API path, MQTT topic, discovery object_id". So:
//
//   <prefix>/status                 online | offline, retained -- the link to the broker
//   <prefix>/bus                    online | offline, retained -- the unit answering on CAN
//   <prefix>/<key>/state            every entity
//   <prefix>/<key>/command          the thirteen writable ones
//   <prefix>/fan/speed/{state,command}    the fan's percentage, which is a separate topic in HA
//   <prefix>/fan/preset/{state,command}   and so is its preset
//   <prefix>/ops/<name>/set         the six operations, JSON
//
// THE SIX OPERATIONS ARE NOT IN DISCOVERY, and that is decided rather than missing: an operation
// takes a mode and a duration in ONE call (the unit's modes are temporary, PDOs 81/86/87 their
// countdowns) and that atomicity is the contract, while HA's `select` has no parameter and its
// `button` has no body. Decomposing each operation into a select plus a number was considered and
// REJECTED, because it destroys the atomicity the contract exists to guarantee. They are reachable
// from Home Assistant through mqtt.publish, and the thirteen writable entities cover everyday
// control.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comfoair_state.h"
#include "registry_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

// Home Assistant's default. It is where HA subscribes, not where this device's own tree lives --
// two different prefixes, and conflating them is why a device publishes a whole tree nobody reads.
#define COMFOAIR_HA_DISCOVERY_PREFIX "homeassistant"

// "online" / "offline" on <prefix>/status, retained, and they are HA's defaults for
// payload_available / payload_not_available -- so nothing has to be published to say so.
#define COMFOAIR_HA_PAYLOAD_ONLINE  "online"
#define COMFOAIR_HA_PAYLOAD_OFFLINE "offline"

// "No value" on every state topic, and HA's own literal for it in every component this device
// publishes: sensor.py:317, binary_sensor.py:203, switch.py:111, select.py:124 (lower-cased),
// number.py:95 and fan.py:158/:161 via DEFAULT_PAYLOAD_RESET (const.py:308), fan.py:347
// (Home Assistant 2026.9, components/mqtt/). It is also how the fan's preset is cleared, and it is
// deliberately NOT one of the six preset names: valid_preset_mode_configuration() would reject that
// (fan.py:119-120). DO NOT publish an empty payload instead: retained, it deletes the broker's
// copy, and every handler ignores it. No label may be spelled "None" (tools/registry_checks.py).
#define COMFOAIR_HA_PAYLOAD_NONE "None"

// Everything a payload needs that the registry does not carry. Borrowed, not copied: every
// pointer must outlive the call.
typedef struct {
    // Twelve lower-case hex characters of the FULL MAC. Identity is the MAC and never the
    // name, or renaming the device duplicates every entity in Home Assistant while the old ones
    // hang retained in the broker and cannot be deleted.
    const char *device_id;
    // Display only. Changing it must not re-key anything.
    const char *device_name;
    // This device's own tree, e.g. "comfoair/aabbccddeeff".
    const char *topic_prefix;
    const char *sw_version;
    // Colon-separated MAC for the device block's `connections`. Home Assistant merges devices by
    // it, so publishing it keeps these entities on the device card Home Assistant already knows
    // for this unit -- which is what the owner chose on 2026-09-04, accepting that while another
    // publisher for the same MAC is still running the card carries two sets of entities with _2
    // suffixes. Empty string omits it.
    const char *mac;
    // For the device block's configuration_url. HA validates it with cv.configuration_url, which
    // is stricter than a string: a bare address with no scheme is REJECTED, so this is
    // "http://192.168.1.42/" and not "192.168.1.42". Empty string omits it.
    const char *url;
} comfoair_ha_ctx_t;

// --- topics ---------------------------------------------------------------------------------

// The topics that belong to the DEVICE rather than to an entity, and the enum is the construction
// this component's cleanup rests on.
//
// A TOP-LEVEL RETAINED TOPIC DOES NOT EXIST UNTIL IT IS A VALUE HERE. Its name can be obtained
// from nowhere but comfoair_ha_meta_topic(), and the cleanup walks
// `for (which = 0; which < COMFOAIR_HA_META_COUNT; which++)` rather than a second list. So to
// publish a new retained top-level topic somebody must first add a value here -- and the moment
// they do, the cleanup empties it, with no edit anywhere else and no way to forget. The switch in
// comfoair_ha_meta_topic() is total, and [env:native] promotes -Wswitch to an error, so a value
// added without a name does not compile. This is the same construction-not-discipline shape
// comfoair_mdns_facts_t uses to keep the mDNS TXT records closed (comfoair_mdns.h).
//
// DO NOT format `<prefix>/something` anywhere else. A topic built by hand is a topic the cleanup
// cannot see, and it survives on the owner's broker for ever -- there is no second pass that goes
// looking for orphans.
typedef enum {
    // online | offline, retained. THE LINK TO THE BROKER and nothing else: the will
    // (comfoair_mqtt_client.c) and the birth message (comfoair_mqtt_publish.c) are the same topic,
    // which is why it is the first entry of every entity's `avty` list.
    COMFOAIR_HA_META_STATUS = 0,
    // online | offline, retained. WHETHER THE VENTILATION UNIT IS ANSWERING ON CAN, which is a
    // different fact from the one above and the reason this value exists, as a second
    // availability topic rather than anything written over the entities: the owner cuts the
    // breaker to change the filters, Wi-Fi and the broker keep working, and Home Assistant goes on
    // showing the supply temperature the unit reported before the power went -- for ever, as a
    // current reading, because a silent bus changes no value: the model keeps the last reading,
    // and only a frame can turn it into COMFOAIR_HA_PAYLOAD_NONE.
    //
    // ITS PAYLOAD IS comfoair_bus_health_available() AND NOT A SECOND THRESHOLD. One verdict, two
    // readers: this topic and the BUS_SILENT phase (comfoair_bus_health.h, which spends a
    // paragraph on why two thresholds answering one question end up disagreeing in front of the
    // owner -- one colour on the device beside a different card in Home Assistant).
    //
    // NEVER_SEEN PUBLISHES `offline` AND THAT IS HONEST: a device whose loom was never plugged in
    // has no values, so there is nothing to lie with. It is deliberately NOT the same row as the
    // BUS_SILENT phase, which stays clear -- "check the loom" is the wrong instruction for a
    // device that has never had one.
    COMFOAIR_HA_META_BUS,
    COMFOAIR_HA_META_COUNT,
} comfoair_ha_meta_t;

// The name of one meta topic. 0 for COMFOAIR_HA_META_COUNT, for anything outside the enum and for
// a buffer it does not fit -- a truncated topic is a DIFFERENT topic, which publishes successfully
// to nobody.
size_t comfoair_ha_meta_topic(const comfoair_ha_ctx_t *ctx, comfoair_ha_meta_t which, char *out,
                              size_t cap);

// THERE IS NO comfoair_ha_status_topic(), AND THAT IS THE POINT. It existed until 2026-09-07 as a
// wrapper over COMFOAIR_HA_META_STATUS, with a comment forbidding it a format of its own and a
// test comparing its output against comfoair_ha_meta_topic()'s. A reviewer gave it its own format
// again and every case stayed green -- two functions that format one string the same way agree,
// and the test could only fail once the duplicate had already diverged. The wrapper is therefore
// gone rather than better tested: its three callers (the will and the explicit `offline` in
// comfoair_mqtt_client.c, the birth message in comfoair_mqtt_publish.c) name the enum value, and
// a second format of `<prefix>/status` now has nowhere in the tree to live.

// `key` MUST BE A KEY THE REGISTRY CARRIES, and this function checks that rather than trusting it
// (comfoair_entity_by_key, comfoair_state.h). 0 for anything else. That check is the entity half
// of the same rule the meta enum is the top-level half of: without it a publish under an invented
// key produces a retained topic the cleanup cannot see, and no snprintf has to be written to get
// one -- passing a string is enough.
size_t comfoair_ha_state_topic(const comfoair_ha_ctx_t *ctx, const char *key, char *out, size_t cap);

// AND ITS NEIGHBOUR IS DELIBERATELY LAX ABOUT `key`, WHICH IS NOT AN OVERSIGHT TO TIDY UP. A
// command topic is SUBSCRIBED to, never published, so it cannot orphan a retained message -- the
// whole reason state_topic() checks. And subscribe_all() (comfoair_mqtt_publish.c) builds its
// wildcard by passing "+" through this very function; a registry check here would return 0 for it
// and the device would subscribe to nothing at all, answering no command from Home Assistant with
// nothing logged. DO NOT "restore the symmetry".
size_t comfoair_ha_command_topic(const comfoair_ha_ctx_t *ctx, const char *key, char *out,
                                 size_t cap);

// The fan's own leaves, and this enumeration is the third door -- after comfoair_ha_meta_t and the
// registry -- through which a topic of this device can come into existence.
//
// HA reaches a fan's percentage and its preset on topics of their own, and each FEATURE is
// switched on by its COMMAND topic rather than its state topic (mqtt/fan.py:265-270): a payload
// that names preset_modes without preset_mode_command_topic is rejected by the schema, while the
// reverse silently produces a fan with no presets at all (mqtt/fan.py:140-147).
//
// IT IS AN ENUM AND NOT A STRING because a string was the hole. Until 2026-09-07 this builder took
// `const char *leaf` and would produce `<prefix>/fan/<anything>`; the three leaves the fan
// publishes lived as a hand-written array in comfoair_ha_cleanup.c, a second one in
// comfoair_mqtt_publish.c and a third in the suite -- the very "second list" the header above
// promises this component does not have. A fourth leaf added to publish_fan() was erased by
// nothing and caught by nothing, which was verified by mutation. Now the leaf must be a value
// here first and the switch in comfoair_ha_fan_topic() is total.
//
// THAT HALF IS GUARDED BY A BUILD AND NOT BY A CASE IN THE SUITE, and the build is the HOST one:
// platformio.ini promotes -Wswitch to an error in [env:native] only, and ESP-IDF leaves -Werror
// commented out (tools/cmake/build.cmake:74, :135), so `pio run -e m5stack-nanoc6` answers an
// unnamed leaf with a warning in the middle of the build log and links it anyway. A leaf added
// without ever building the host suite therefore ships. The other half -- that the cleanup walks
// every retained value -- is test_every_retained_fan_leaf_is_erased_by_both_cleanups, and that a
// COMMAND leaf the builder names is one the parser answers for is
// test_every_command_leaf_the_builder_names_is_understood_by_the_parser (test/test_ha).
typedef enum {
    // The three publish_fan() writes, RETAINED. THEY COME FIRST AND NOTHING MAY BE PUT AMONG
    // THEM: COMFOAIR_HA_FAN_RETAINED_COUNT below is where they stop, and the publisher, the
    // cleanup and the suite all walk 0 .. that.
    COMFOAIR_HA_FAN_STATE = 0,   // ON | OFF -- and OFF here is the AWAY level, not a stopped fan
    COMFOAIR_HA_FAN_SPEED_STATE,
    COMFOAIR_HA_FAN_PRESET_STATE,
    // Written by Home Assistant and by whoever holds mosquitto_pub, NEVER by this device. They
    // are values here so that the discovery document and the subscription can name them without a
    // hand-written string; the cleanup must not empty them, because emptying one is emptying
    // somebody else's retained message. The fan's on/off command topic is
    // not among them -- it is `<prefix>/fan/command`, which comfoair_ha_command_topic() builds
    // from the registry key like every other entity's.
    COMFOAIR_HA_FAN_SPEED_COMMAND,
    COMFOAIR_HA_FAN_PRESET_COMMAND,
    // `fan/+/command`: the SUBSCRIPTION PATTERN, not a topic. Nothing is ever published to it and
    // the cleanup never sees it. It is a value here for the same reason as the two above -- the
    // alternative is a hand-written "+/command" at the subscribe site, which is exactly the shape
    // this enumeration exists to make impossible.
    COMFOAIR_HA_FAN_ANY_COMMAND,
    COMFOAIR_HA_FAN_LEAF_COUNT,
} comfoair_ha_fan_leaf_t;

// Where the retained leaves stop. DERIVED FROM THE ORDER ABOVE and never typed: a leaf added
// among the first three moves this constant with it, and both walkers follow without an edit.
enum { COMFOAIR_HA_FAN_RETAINED_COUNT = (int)COMFOAIR_HA_FAN_SPEED_COMMAND };

// The name of one fan leaf. 0 for COMFOAIR_HA_FAN_LEAF_COUNT, for anything outside the enum and
// for a buffer it does not fit -- a truncated topic is a DIFFERENT topic, which publishes
// successfully to nobody.
size_t comfoair_ha_fan_topic(const comfoair_ha_ctx_t *ctx, comfoair_ha_fan_leaf_t leaf, char *out,
                             size_t cap);
// <prefix>/ops/<name>/set. The name is the same one POST /api/ops/<name> uses: one vocabulary for
// an operation across both transports, which is what AGENTS.md criterion 3 asks for.
size_t comfoair_ha_op_topic(const comfoair_ha_ctx_t *ctx, const char *name, char *out, size_t cap);

// <discovery_prefix>/<component>/<device_id>/<key>/config
//
// HA's TOPIC_MATCHER accepts only [a-zA-Z0-9_-] in the node and object segments
// (mqtt/discovery.py:64-67), which is why device_id is bare hex and never a colon-separated MAC.
size_t comfoair_ha_discovery_topic(const comfoair_ha_ctx_t *ctx, const comfoair_entity_t *entity,
                                   char *out, size_t cap);

// --- what this device empties when it stops being the thing it was ----------------------------
//
// MQTT 3.1.1 section 3.3.1.3: a PUBLISH with retain=1 and a zero-length payload deletes the topic's
// retained message at the broker AND is delivered to current subscribers as an ordinary zero-length
// message. Both consequences are wanted -- the broker forgets, and Home Assistant removes the
// entity. The payload is therefore supplied at the single call site and is not part of this
// enumeration, which yields TOPICS ONLY.
//
// TWO OPERATIONS, NOT ONE, and the difference is the one rule everything else follows from:
// NEVER EMPTY A TOPIC A LIVE DISCOVERY DOCUMENT STILL POINTS AT.
typedef enum {
    // The prefix changed. Only the state side of the OLD identity is emptied, and the discovery
    // documents are NOT touched: the config topic is
    // homeassistant/<component>/<device_id>/<key>/config and carries neither the prefix nor the
    // name, so each is simply rewritten in place with the new stat_t when the new client connects
    // and Home Assistant moves the entities across without noticing. Emptying them instead is the
    // only way to reach "zero devices" from here -- HA would delete all the entities together with
    // their entity_ids, their history and every reference in the owner's automations, then recreate
    // them from the next config message with the same uniq_id but no guarantee of the same
    // entity_id.
    COMFOAIR_HA_CLEANUP_REKEY,
    // The device leaves this broker altogether: discovery turned off, the broker replaced, a
    // factory reset, a board about to be unscrewed. Every discovery topic FIRST -- that is what
    // makes the removal clean -- and the state side after it, which by then nobody reads. The
    // reverse order leaves a window in which live entities point at topics that have just been
    // emptied.
    COMFOAIR_HA_CLEANUP_WITHDRAW,
} comfoair_ha_cleanup_t;

// How many topics this cleanup yields. DERIVED by walking the registry and the meta enum, never a
// literal: a number written down here would be the second entity list AGENTS.md forbids, one
// indirection along, and it would go stale in silence.
size_t comfoair_ha_cleanup_count(comfoair_ha_cleanup_t what);

// The `index`-th topic of the sequence, 0 <= index < comfoair_ha_cleanup_count(what). Returns 0
// for an index past the end AND for a topic that does not fit `cap`; the caller logs the second
// and carries on rather than stopping the sweep, exactly as publish_discovery() already does for a
// document that does not fit. DO NOT loop until zero -- one over-long topic would silently end the
// cleanup and leave the rest of the tree on the broker for ever.
//
// The topics come from the four builders above and from nowhere else. The cleanup owns no
// formatting of its own, or the shape of the tree would be described in two places and the second
// one is the one nobody re-checks when a segment moves.
size_t comfoair_ha_cleanup_topic(const comfoair_ha_ctx_t *ctx, comfoair_ha_cleanup_t what,
                                 size_t index, char *out, size_t cap);

// --- what an entity becomes in Home Assistant ------------------------------------------------

// The HA component name, or NULL for an entity this device does not publish.
//
// The mapping is not one-to-one with the registry's kinds, and the two rows where it is not are
// the interesting ones:
//
//  * `enum` becomes a PLAIN sensor, with no `options` and no `device_class: enum`. HA validates
//    those together (mqtt/sensor.py:109-125). An undocumented unit value decodes to no value and
//    publishes `None`, which HA accepts before the options check (sensor.py:317), so options would
//    now be safe; they are still not added because that changes the entity's class for every
//    existing MQTT user, which is a decision of its own and not a side effect of this one.
//  * `special` (airflow_constraints, active_errors) is a plain sensor too: it publishes sentences
//    like "Below: Frost Protection | Above: Humidity Comfort".
const char *comfoair_ha_component(const comfoair_entity_t *entity);

// --- what an arriving topic means -------------------------------------------------------------

typedef enum {
    COMFOAIR_HA_TARGET_NONE,     // not one of ours
    COMFOAIR_HA_TARGET_ENTITY,   // <prefix>/<key>/command
    COMFOAIR_HA_TARGET_FAN_ON,   // <prefix>/fan/command
    COMFOAIR_HA_TARGET_FAN_SPEED,   // <prefix>/fan/speed/command
    COMFOAIR_HA_TARGET_FAN_PRESET,  // <prefix>/fan/preset/command
    COMFOAIR_HA_TARGET_OP,       // <prefix>/ops/<name>/set
} comfoair_ha_target_t;

// Splits an arriving topic into what it addresses and what it names.
//
// PURE, and for the same reason the JSON reader is: a broker is reachable by anything on the
// household LAN, this runs before anything has decided the message is ours, and `topic` from
// esp-mqtt is NOT NUL-terminated -- it is a pointer and a length into the client's receive buffer.
// A parser that assumed a terminator would read whatever followed it.
//
// `name` receives the entity key or the operation name; it is untouched for the fan targets,
// which name themselves. Returns COMFOAIR_HA_TARGET_NONE for anything outside this device's tree,
// including a topic that merely starts with the prefix.
comfoair_ha_target_t comfoair_ha_parse_topic(const comfoair_ha_ctx_t *ctx, const char *topic,
                                             size_t topic_len, char *name, size_t cap);

// --- payloads --------------------------------------------------------------------------------

// The retained discovery document for one entity. Returns 0 when it does not fit, which is a
// defect to report rather than a short document to publish.
//
// Keys are ABBREVIATED (stat_t, cmd_t, dev_cla, uniq_id, ...). That is not golf: the device and
// origin blocks repeat in every message, and HA expands the short forms itself
// (mqtt/discovery.py:175-186, abbreviations.py) so nothing is lost.
size_t comfoair_ha_discovery_payload(const comfoair_ha_ctx_t *ctx, const comfoair_entity_t *entity,
                                     char *out, size_t cap);

// The state payload for one entity: a BARE SCALAR, not a JSON document -- `123`, `21.5`, `ON`,
// `Auto` -- because that is what an MQTT entity reads without a value_template, and no discovery
// document carries one (test_ha_none pins that). The fan has no state payload of its own -- see
// comfoair_ha_fan_state_payload().
//
// NO READING PUBLISHES COMFOAIR_HA_PAYLOAD_NONE, which every HA component reads as unknown -- the
// MQTT face of REST's `null`. A reading that went away (a sentinel, an unlisted enum value, a
// short frame) therefore replaces the retained value instead of leaving it on the broker as if
// current. 0 with `skip` set only for a NULL argument or a buffer it does not fit.
size_t comfoair_ha_state_payload(const comfoair_entity_t *entity, const comfoair_value_t *value,
                                 bool *skip, char *out, size_t cap);

// The fan's three payloads, which HA reads from three topics.
//  * state:   "ON" / "OFF" -- and OFF here is the AWAY level, not a stopped fan
//  * speed:   "1".."3", mapped by HA through speed_range_min/max onto a percentage
//  * preset:  one of the six names, or "None" to clear it
// Before the unit has described the fan (`known` false) all three are COMFOAIR_HA_PAYLOAD_NONE.
// 0 only for a NULL fan or a buffer it does not fit.
size_t comfoair_ha_fan_state_payload(const comfoair_fan_state_t *fan, char *out, size_t cap);
size_t comfoair_ha_fan_speed_payload(const comfoair_fan_state_t *fan, char *out, size_t cap);
size_t comfoair_ha_fan_preset_payload(const comfoair_fan_state_t *fan, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
