// One verdict about the whole device, derived from facts that today exist nowhere at once.
//
// In the tree as it stands, exactly one function reads both the network and the broker in the
// same moment -- the handler behind GET /api/status -- and it glues the two into JSON and drops
// the result into a socket. No variable, no event, no name survives it: the device's aggregate
// state lives as long as one HTTP response and is born only if somebody asks. device_phase_t is
// where those facts start existing for longer than one log line -- the start failures of app_main
// among them, six of which otherwise end in a log line and carry on.
//
// PURE, TOTAL, NO CLOCK AND NO GLOBALS. Nothing here includes ESP-IDF or FreeRTOS, which is what
// lets the whole product of inputs be swept in test/test_phase rather than reproduced in front of
// a device. The imperative half -- the 1 Hz task, the snapshots and the two hold-offs
// -- is a separate component (comfoair_phase_monitor) for a build reason, not a stylistic
// one: the host environment's dependency finder pulls in whatever a test includes, so a FreeRTOS
// task placed beside this file would drag esp-mqtt and the TWAI driver into the host binary.
//
// DO NOT reach for a clock in here. A hold-off is a MEASUREMENT and belongs to the collector;
// deriving is a DECISION. Mixed, they give a function that cannot be called twice with the same
// argument, and every case in test/test_phase stops being writable. The same rule, and the same
// wording, guards comfoair_provision.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_provision.h"

#ifdef __cplusplus
extern "C" {
#endif

// DECLARED IN PRIORITY ORDER, TOP DOWN: the numeric order IS the conflict rule, so
// it is checked by test_lower_numbered_phase_wins_when_both_conditions_hold rather than by
// reading. The rule the order encodes: the higher phase is the one that must be fixed before the
// next can be diagnosed at all.
//
// A phase exists if and only if either the owner's next action differs, or the expected duration
// differs by an order of magnitude. Everything that fails that test stays a field
// in /api/status and never becomes a colour. Adding a fifteenth value means making that argument
// for it -- and also revisiting comfoair_led_look_of() (comfoair_led_map.c), total over these.
typedef enum {
    // The provisioning machine is not initialised yet. A GUARD, NOT A LIVE PHASE: machine_ready
    // goes true inside comfoair_net_start(), before the monitor task exists, so the monitor's
    // very first answer already has it false. Where BOOT is actually observed is as the initial
    // value comfoair_led holds from init until the first transition reaches it -- and a monitor
    // that failed to start is FAULT, never BOOT, because otherwise the same picture
    // would mean "starting up" and "the thing that reports my state is dead".
    COMFOAIR_PHASE_BOOT = 0,
    // A firmware partition is being written. The one phase whose owner action is "do not cut the
    // power", which is why it outranks even the open access point.
    COMFOAIR_PHASE_OTA,
    // Credentials were accepted and the connection they describe is NOT yet confirmed. Above the
    // access-point phases by necessity, not by taste: during a trial the radio stands in AP_STA
    // by construction, so the access point is always on the air and a lower TRIAL would be
    // unreachable rather than rare.
    COMFOAIR_PHASE_TRIAL,
    // The device's own access point, on a unit that was never configured. Minutes, by
    // construction. Above FAULT because "come and set me up" is an instruction while FAULT is
    // only information -- and because an open access point is also a security state: the device
    // counts as unclaimed while it is on the air, whatever NVS holds.
    COMFOAIR_PHASE_SETUP_AP,
    // The same open access point on a unit that already worked and lost its network. A separate
    // phase because the fallback AP (5 min up, 30 down) repeats every 35 minutes for ever: an
    // owner can watch it for weeks, an order of magnitude away from SETUP_AP (the admission rule).
    COMFOAIR_PHASE_RECOVERY_AP,
    // At least one of the seven start branches of app_main returned an error. Narrowed to STARTUP
    // failure and deliberately disjoint from BUS_SILENT: "the driver did not come up" and "the
    // driver is up and no frames arrive" are different facts in different branches.
    COMFOAIR_PHASE_FAULT,
    // The network is there and will not let us in: fix the password or the router's settings.
    COMFOAIR_PHASE_NET_REJECTED,
    // No working connectivity to be had: check the router and its DHCP. Together with the row
    // above, this is the four Wi-Fi verdicts compressed to two -- and the compression is said out
    // loud: failure and failure_reason keep all four by name in /api/status, and only the channel
    // that has nothing but colour maps them by owner action.
    COMFOAIR_PHASE_NET_LOST,
    // The setup window ran out on a device with nothing to connect to. The only phase whose cure
    // lives outside the network code: a short press of the button (comfoair_button).
    COMFOAIR_PHASE_SETUP_EXPIRED,
    // An attempt is in flight, no verdict yet. CONNECTING and RETRYING are one phase on purpose:
    // the machine steps between them on every rung of the 1 s -> 30 s backoff, and two phases
    // would turn that ladder into visible flicker.
    COMFOAIR_PHASE_CONNECTING,
    // The broker answered CONNACK with a refusal (see comfoair_phase_connack_is_refusal).
    // ABOVE BUS_SILENT, and this is where the draft was reversed: a latched code the broker
    // actually sent is a fact, while a silent bus is an inference from a threshold this tree has
    // never measured -- and under the 300 s retry for rejected credentials the wrong order would
    // hide the certain verdict behind the guessed one indefinitely, while the reverse mistake
    // ends on the first frame.
    COMFOAIR_PHASE_BROKER_REJECTED,
    // The CAN driver is up and the installation has not spoken for 90 s (comfoair_bus_health.h).
    COMFOAIR_PHASE_BUS_SILENT,
    // A broker IS configured and we are not connected to it. Conditioned on configured because a
    // device with no broker is how this firmware ships, not a degradation.
    COMFOAIR_PHASE_NO_BROKER,
    // Everything above is false. The last row, and unconditional, which is why there is no input
    // for which no phase exists.
    COMFOAIR_PHASE_ONLINE,
    COMFOAIR_PHASE_COUNT,
} device_phase_t;

// Facts that have ALREADY been debounced. NOT ONE FIELD OF TIME: the two hold-offs
// (30 s before NO_BROKER, and the CAN resolver's own 90 s behind bus_silent) are applied by the
// collector before it calls, for the reason written at the top of this file.
//
// This struct is the seam this component is expected to grow along -- one field per subsystem --
// while the enumeration and comfoair_phase_derive() stay put.
typedef struct {
    // comfoair_prov_init() has run. False means BOOT.
    bool machine_ready;

    bool ota_in_progress;

    // The seven failure branches of app_main. Six of them end in a log line and
    // carry on, and on a device with no USB cable attached all six are invisible; each false
    // here means "that subsystem was started and said no".
    bool can_started;
    bool poll_started;
    bool control_started;
    bool net_started;
    bool mqtt_ready;           // comfoair_mqtt_init() -- the QUEUE
    // comfoair_mqtt_start() -- the CLIENT, and a failure of its own. Without it a device whose
    // queue exists but whose client was never built would report NO_BROKER ("check your broker")
    // where the true answer is "the firmware could not" -- exactly the lie about the cause that
    // comfoair_mqtt's last_connack was introduced to end.
    bool mqtt_client_started;
    bool http_started;

    comfoair_prov_state_t   prov_state;
    comfoair_prov_failure_t prov_failure;
    // The caller's REPORT that an access point is on the air, not an inference from the mode the
    // machine asked for. "Asked for an access point" and "an access point is on the air" are
    // different facts, and comfoair_provision anchors a clock on the second one.
    bool ap_on_air;
    // The driver's state NOW, not app_main's verdict on starting it: comfoair_can_stats_t.started
    // (comfoair_can.h:68) is live, and comfoair_can_stop() exists. can_started above is the other
    // fact -- the one-time return of the start branch -- and the two agree until something stops
    // the driver on a running device.
    //
    // That divergence is the only situation the `bus_started &&` term of the cascade is FOR, and
    // it is easy to read the term as dead code without it: FAULT is decided before BUS_SILENT, so
    // by the time the cascade reaches the bus, can_started is already true. DO NOT delete it on
    // that reading -- a stopped driver with a stale bus_silent would then report BUS_SILENT
    // ("check the loom") for a fault that is ours. Deleting it fails the priority sweep, which
    // mirrors the term, and test_a_stopped_driver_is_not_a_silent_bus is the readable witness
    // that says which situation the sweep is talking about.
    bool bus_started;
    // The CAN resolver's finished verdict: comfoair_bus_health_is_silent(), and nothing else. DO
    // NOT recompute it from a second threshold here -- two thresholds answering one question drift
    // apart, so the resolver is the single owner.
    //
    // THE SAME RESOLVER FEEDS THE <prefix>/bus TOPIC, BUT NOT THE SAME BIT. That
    // topic reads comfoair_bus_health_available(), which is ALSO false for NEVER_SEEN: a device
    // that has never heard a frame publishes bus = offline honestly -- there are no values to
    // report -- and must not be painted BUS_SILENT, whose owner instruction is "check the loom and
    // the unit's power". One verdict, two projections. Filling this field from the topic's bit
    // instead re-creates the boot grace period the four states exist to avoid.
    bool bus_silent;

    // A broker host is STORED. Not "a client was built from it": those differ exactly when
    // esp-mqtt refuses to initialise or start, which is what mqtt_client_started is about.
    bool broker_configured;
    bool broker_connected;
    // The latched CONNACK refusal, as decided by comfoair_phase_connack_is_refusal(). A latch and
    // not a live reading: the code is cleared only by a successful connection or a client stop,
    // and the 300 s retry for rejected credentials keeps connected false for five minutes.
    bool broker_rejected;
} comfoair_phase_inputs_t;

// The one verdict. A cascade in the order of the enumeration whose last branch is unconditional,
// so every input has a phase and none falls through.
//
// TASK CONTEXT: any task, any core. Takes no locks, holds no state, and reads nothing but its
// argument -- calling it twice with the same inputs is guaranteed to give the same answer.
// NULL inputs give BOOT: a caller with nothing to say has not started the machine yet, and
// answering ONLINE there would make the quietest bug look like the healthiest device.
device_phase_t comfoair_phase_derive(const comfoair_phase_inputs_t *in);

// Whether an MQTT CONNACK code means the broker REFUSED us, which is what latches broker_rejected.
//
// True for 1, 2, 4, 5 and for anything outside 0..5; false for 0 (no refusal recorded) and for 3.
// Code 3 (server unavailable) is temporary by construction -- it is what a Mosquitto add-on
// answers in the first seconds after a restart -- and latching it would put the 300-second
// rejected-credentials retry behind a five-second restart, a five-minute hole in the owner's
// graphs. Codes 1 and 2 are included because the owner's action for them is the broker's settings,
// the same action as for 4 and 5; the failure CLASS still separates them (PROTOCOL against
// REJECTED, comfoair_mqtt_backoff.h), and that separation has its own wording in
// /api/status. Sharing a look is not sharing a state.
//
// A uint32_t and not the esp-mqtt enum on purpose, the same reason as comfoair_wire_connack_name:
// this file builds on the host, where mqtt_client.h does not exist.
//
// SECOND READER OF THE SAME CODE SPACE, and it is not the only one: comfoair_wire_connack_name()
// (comfoair_wire_render.c) spells the codes for /api/status, and comfoair_mqtt_backoff adds a third
// that sorts them into failure classes. All three own "0..5 plus everything else" independently. DO
// NOT teach one of them a code without the others: a broker answer that gains a meaning here but
// keeps the generic spelling there is a device whose colour and whose words disagree.
bool comfoair_phase_connack_is_refusal(uint32_t connack);

// One spelling per phase, decided here. The same argument as comfoair_prov_failure_name: REST, the
// log line and the SPA each inventing a spelling is a lost invariant. NEVER NULL, including for a
// number outside the enumeration -- these travel through JSON documents.
//
// The pointer is a string literal with static storage: it outlives every caller and is never freed.
// A phase inside the enumeration never answers "unknown"; that spelling is reserved for a number
// that is not a phase, and test_every_phase_has_a_name refuses it for the fourteen.
const char *comfoair_phase_name(device_phase_t phase);

#ifdef __cplusplus
}
#endif
