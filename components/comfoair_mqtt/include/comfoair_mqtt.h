// The broker connection: one task, one bounded queue, and nothing that can stop the rest of the
// device.
//
// comfoair_ha decides WHICH topic and WHAT payload; this owns everything that cannot be
// host-tested -- the client, a FreeRTOS task, a clock and the network. The split is the same one
// comfoair_control has against comfoair_command, and comfoair_net against comfoair_provision.
//
// THREE RULES, AND EACH GUARDS A FAILURE THAT IS EASY TO SHIP:
//
//  * PUBLISHING HAPPENS ONLY FROM THIS TASK. A publish from the CAN callback blocks on a silent
//    broker, the task stops reading the bus, the TWAI receive queue overflows and PDOs are
//    lost -- quieter than a reboot, the same class of failure. Everything else calls
//    comfoair_mqtt_notify_state_changed(), which sets a flag and returns.
//  * NOTHING REBOOTS BECAUSE THE BROKER IS ABSENT. A "broker unreachable for fifteen minutes --
//    reboot" watchdog would give a household that switched its broker off a ventilation unit
//    rebooting every quarter hour, taking the web UI and the CAN polling loop with it. esp-mqtt
//    has no such watchdog and this file adds none. AGENTS.md forbids it.
//  * A RETAINED COMMAND IS IGNORED. `mosquitto_pub -r -t .../bypass_mode/command -m Open`, typed
//    once during testing, would otherwise reopen the bypass on every reconnect and every boot for
//    as long as the broker keeps the message. A client that hides the retain flag can only push
//    this into a broker ACL; esp-mqtt hands us the flag on arrival, so here it is fixable and it
//    is fixed.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// For COMFOAIR_PROV_NEVER, which retry_in_ms below uses as its "no attempt is scheduled" answer.
// The sentinel is spelled once for the whole firmware and this is that one; comfoair_provision is
// already on this component's include path through comfoair_wire's public interface.
#include "comfoair_provision.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Creates the inbound command queue and nothing else. TASK CONTEXT: app_main, ONCE, and it MUST
// come before comfoair_http_start(): a POST /api/config arriving earlier reaches
// comfoair_mqtt_reconfigure() with no queue and no publisher behind it. The primitives used to be
// created lazily inside comfoair_mqtt_start(), and the window was real rather than theoretical --
// src/main.cpp starts the web server first and the broker several calls later, so the owner's
// first save could land in it. ESP_ERR_NO_MEM if the queue cannot be allocated; calling it twice
// is a no-op that returns ESP_OK.
esp_err_t comfoair_mqtt_init(void);

// Starts the client if a broker is configured, and creates the publisher task that from then on
// OWNS the client. TASK CONTEXT: app_main, after comfoair_mqtt_init(). ESP_ERR_INVALID_STATE when
// already running, ESP_ERR_NOT_FOUND when no broker host is set -- which is not an error and is
// the state every device ships in. Nothing is dialled, nothing is retried, nothing is logged as a
// fault. ESP_ERR_NO_MEM means the publisher task did not start, and then nothing in this component
// runs at all.
esp_err_t comfoair_mqtt_start(void);

// Applies the stored broker settings again: stops the client if one is running and starts it if a
// host is configured. Called after POST /api/config, so that saving a broker takes effect without
// a reboot -- the whole point of "everything is configured at runtime" (AGENTS.md, Architecture).
//
// TASK CONTEXT: the httpd task. IT DOES NOT BLOCK and it does not enter esp-mqtt: it posts a
// request and returns, and the stop, the re-read of the settings and the start are all performed
// by the publisher task between two of its passes. ESP_OK means the request was recorded, not that
// the client has restarted. ESP_ERR_INVALID_STATE means comfoair_mqtt_init() never succeeded, so
// there is nobody to serve the request and the caller's warning would be the truth.
esp_err_t comfoair_mqtt_reconfigure(void);

// Something in the state model moved. Safe from any task including the CAN receive path: it sets
// a flag and wakes the publisher, because a burst of PDO frames must not become a burst of
// publishes on a link that may be blocked.
void comfoair_mqtt_notify_state_changed(void);

// Whether the ventilation unit is answering on CAN. This is what `<prefix>/bus` publishes -- the
// second availability topic every entity that reads the UNIT lists beside `<prefix>/status`,
// rather than anything written over those entities' own states. Without it the owner
// who cut the breaker to change his filters is shown an hour-old supply temperature as a current
// reading, indefinitely, because a silent bus changes no value: the model keeps the last reading,
// and only a frame turns a value into `None`.
//
// A bool AND NOT comfoair_bus_health_t, and the reason is not brevity: the projection from the
// four states onto "available" belongs to comfoair_bus_health_available(), which is the ONE
// verdict this topic and the BUS_SILENT phase both read. Taking the state here would put a second
// opinion about NEVER_SEEN inside this component -- and NEVER_SEEN is precisely the row where the
// two answers differ: offline on this topic, and NOT a BUS_SILENT phase, because "check the loom"
// is the wrong instruction for a device that has never had one.
//
// TASK CONTEXT: any task. comfoair_phase_monitor's 1 Hz tick is the only caller and it may call on
// every tick: nothing is published from here -- the publisher task is woken instead, and only when
// the answer has changed. Safe before comfoair_mqtt_init(), with no broker configured and with the
// client destroyed: the value is remembered and goes out on the next connection.
void comfoair_mqtt_note_bus(bool available);

// Told when a command arriving over MQTT changed the state model, so the browser learns about it
// at once rather than on the push channel's fifteen-second keep-alive.
//
// A CALLBACK RATHER THAN A CALL, and the reason is structural: comfoair_http already depends on
// this component (POST /api/config restarts the broker connection), so calling back into it
// directly would be a dependency cycle. main.cpp wires the two together, which is also where every
// other cross-transport wire in this firmware lives.
typedef void (*comfoair_mqtt_observer_t)(void);
void comfoair_mqtt_set_observer(comfoair_mqtt_observer_t observer);

typedef struct {
    // A broker host is stored. NOT that a client was built from it, and the two differ exactly
    // when esp-mqtt refuses to initialise or to start: the owner HAS configured a broker then, and
    // saying otherwise reports the wrong cause -- which is the defect last_connack exists to end.
    bool     configured;
    bool     connected;
    uint32_t published;
    uint32_t commands;     // accepted from the broker
    uint32_t rejected;     // arrived and were refused: bad topic, bad payload, or retained
    uint32_t reconnects;
    // The CONNACK refusal code, 0 when there was no refusal. It separates "the broker rejected us"
    // from "there is no broker": an owner who mistyped the password is otherwise told
    // "broker unreachable" every ten seconds forever -- a lie about the cause. Values are MQTT
    // 3.1.1 section 3.2.2.3 (1 unacceptable protocol version, 4 bad credentials, 5 not authorized).
    uint32_t last_connack;

    // --- the retry ladder ---
    //
    // WHY THESE EXIST AT ALL, in one measured sentence: over ten hours of
    // hammering an unreachable broker not one field above this line moves. `connected` stays
    // false, `published`, `commands`, `rejected` and `reconnects` stand still, `last_connack` is 0
    // because only a CONNACK refusal writes it -- so an owner who opens the document twice half a
    // day apart gets a byte-for-byte identical answer and cannot tell "we are trying and failing"
    // from "the task is dead".
    //
    // A value of comfoair_mqtt_link_t and a value of comfoair_mqtt_fail_t, held as uint8_t so that
    // this header stays free of the ladder's own. THE WORDS ARE NOT INVENTED HERE OR ANYWHERE
    // ELSE: comfoair_mqtt_link_name() and comfoair_mqtt_fail_name() spell them once, and a second
    // spelling in the renderer is the defect comfoair_wire.h:230-236 already records the price of.
    uint8_t  link;
    uint8_t  failure;
    // Failed attempts since the last SETTLED session -- sixty seconds of it, not a connection that
    // happened. Saturates at 16 with the ladder's own delay.
    uint32_t attempts;
    // Milliseconds to the next attempt, or COMFOAIR_PROV_NEVER when NONE IS SCHEDULED -- which the
    // status document renders as null. "No attempt is scheduled" and "an attempt in 0 ms" are
    // opposite answers, and the sentinel is the one this API already uses for that distinction
    // (comfoair_provision.h:50, comfoair_wire_internal.h:97-107); a second one would be a second
    // rule.
    uint32_t retry_in_ms;
    // How many times the PUBLISHER TASK has rebuilt the client, counted AFTER the rebuild. An
    // epoch of APPLICATION and not of request: a browser that saved a broker remembers this value,
    // then polls until it changes AND link is no longer "connecting". Moved when the request
    // arrives instead, it would change while the previous client was still connected and the page
    // would read that connection's verdict as the new one's.
    uint32_t applied_epoch;

    // --- the three the event handler owes ---
    //
    // A value of comfoair_mqtt_subs_t, held as uint8_t by the same rule as `link` above.
    // NOT PRODUCED YET, AND THAT IS A SEQUENCE AND NOT AN OVERSIGHT: the counters it needs --
    // s_subs_requested on the publisher, MQTT_EVENT_SUBSCRIBED and MQTT_ERROR_TYPE_SUBSCRIBE_FAILED
    // in the handler, a 10 s deadline so a silent ACL lands in `refused` -- have not been built
    // yet. Until they land this reads NONE, which is the honest answer for a device that has
    // not observed its subscriptions rather than a claim that they failed.
    uint8_t  subscriptions;
    // Sessions lost, against `reconnects` above, which counts only the ones that came back. THE
    // PAIR IS THE DIAGNOSIS: climbing together while `connected` flickers is what a second device
    // holding the same client id looks like from here -- the broker sends an ordinary disconnect
    // and says nothing else. Every MQTT_EVENT_DISCONNECTED moves it.
    uint32_t disconnects;
    // The raw esp_transport_sock_errno of the last transport failure, 0 when none was recorded.
    // DO NOT read 0 as a defect: a timed-out esp_mqtt_connect dispatches only
    // MQTT_EVENT_DISCONNECTED and never MQTT_EVENT_ERROR, so for that failure --
    // the commonest of them all -- there is nothing to record and the zero is by construction.
    uint32_t last_transport_errno;
} comfoair_mqtt_stats_t;

// A snapshot for GET /api/status. TASK CONTEXT: any task, and it takes no locks -- every counter
// is an atomic and the two flags are read once each and never dereferenced.
void comfoair_mqtt_get_stats(comfoair_mqtt_stats_t *out);

#ifdef __cplusplus
}
#endif
