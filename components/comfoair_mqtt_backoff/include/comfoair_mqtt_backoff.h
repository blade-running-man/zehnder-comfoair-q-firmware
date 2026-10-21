// How long to wait before the next attempt at the broker, and what to call the reason. The
// as-built table and the status fields: docs/interfaces/mqtt.md, "Reconnect and backoff".
//
// Why the ladder is ours: esp-mqtt's own default is a fixed ten seconds forever, with no ceiling,
// no jitter, and the same ten seconds for "the password is wrong" as for "the broker is switched
// off for the weekend". So comfoair_mqtt_client.c sets network.disable_auto_reconnect and the
// publisher task dials with esp_mqtt_client_reconnect(); this component is the half of that
// decision which is a table, and therefore the half a host test can read.
//
// PURE, TOTAL, NO CLOCK, NO RANDOMNESS AND NO GLOBALS. Not one ESP-IDF header, which is why the
// component has no REQUIRES at all and why the whole product of inputs is swept in
// test/test_mqtt_backoff rather than reproduced in front of a broker. The IDF-side identifiers --
// MQTT_ERROR_TYPE_TCP_TRANSPORT, ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME, the CONNACK byte -- are
// mapped to the enumerations below by the event handler, which includes mqtt_client.h anyway. The
// same argument, and the same wording, guards comfoair_wire_connack_name
// (comfoair_wire_render.c:136-137) and comfoair_phase.
//
// DO NOT reach for esp_random() to seed the jitter. The seed is a 32-bit FNV-1a of
// the device MAC, which is already read in comfoair_mqtt_client.c:208 and is also the client id.
// That gives every device in a house its own ladder -- which is the whole anti-storm argument --
// but the SAME ladder from boot to boot, so hardware scenario HV-MQTT-16 reproduces and this
// header's contract can be pinned to exact milliseconds by a test. esp_random() buys nothing that
// the MAC does not and costs both of those.
//
// DO NOT make anything here reboot, retry a bounded number of times, or return a terminal state.
// The only effect this component can have is a number of milliseconds, and the uint32_t return is
// what makes that a proof rather than a promise (AGENTS.md).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Six failure classes, and they deliberately do NOT match the six CONNACK codes: a class is a
// decision about HOW LONG TO WAIT and WHAT TO TELL THE OWNER, not a transcription of somebody
// else's enumeration. A seventh value exists if and only if either the owner's next
// action differs or the wait differs -- and adding one means editing the table below and
// the row map in test/test_mqtt_backoff, both of which are total over these.
typedef enum {
    // Nothing failed. Also the answer for a caller with nothing to say: "no refusal was recorded"
    // is the state of a device that has never contacted a broker at all, the same distinction
    // comfoair_wire_connack_name draws for code 0.
    COMFOAIR_MQTT_FAIL_NONE = 0,
    // The host name did not resolve. Shares a row with TCP; the split buys only the word in
    // /api/status, which is the difference between the owner editing the host field and the owner
    // starting his broker. The signal behind it is UNVERIFIED on the
    // plain-TCP path: if it never arrives, DNS and TCP merge and the ladder is unaffected.
    COMFOAIR_MQTT_FAIL_DNS,
    // The transport failed for any other reason: no route, refused, timed out.
    COMFOAIR_MQTT_FAIL_TCP,
    // CONNACK 3, server unavailable. Temporary by construction -- it is what a Mosquitto add-on
    // answers in the first seconds after a restart -- so it climbs the FAST row. Putting it with
    // the refusals would turn a five-second restart into a five-minute hole in the owner's Home
    // Assistant graphs.
    COMFOAIR_MQTT_FAIL_UNAVAILABLE,
    // CONNACK 1 or 2. The owner's action is the broker's settings -- the same action as for 4 and
    // 5, which is why the wait is the same and why comfoair_phase latches BROKER_REJECTED for all
    // four. It is still its own class because /api/status must not say "bad-credentials" to an
    // owner whose password is correct.
    COMFOAIR_MQTT_FAIL_PROTOCOL,
    // CONNACK 4, CONNACK 5, and any code outside 0..5. Owner's call: its own state, its own colour,
    // its own words, and 300 seconds. Five minutes are right HERE specifically because 4/5 is the
    // one class a broker BANS for: a device knocking every ten seconds with a wrong password is
    // blacklisted by IP before the owner fixes it -- together with every other device behind the
    // same NAT.
    COMFOAIR_MQTT_FAIL_REJECTED,
    // An established session dropped. Its own row starting one rung lower, because "the broker has
    // accepted us before" and "we have never got through to it" are different facts: failed
    // attempts are counted only where it never got that far.
    COMFOAIR_MQTT_FAIL_DROPPED,
    COMFOAIR_MQTT_FAIL_COUNT,
} comfoair_mqtt_fail_t;

// What esp-mqtt told us, reduced to the three shapes it actually has (verified by
// reading the vendored mqtt_client.c): nothing, a transport error, or a CONNACK refusal.
typedef enum {
    COMFOAIR_MQTT_ERR_NONE = 0,
    COMFOAIR_MQTT_ERR_TRANSPORT,   // MQTT_ERROR_TYPE_TCP_TRANSPORT
    COMFOAIR_MQTT_ERR_REFUSED,     // MQTT_ERROR_TYPE_CONNECTION_REFUSED
} comfoair_mqtt_err_kind_t;

// The five words of the mqtt.link field. "link" and not "state": the top-level
// state member of the status document is the provisioning state
// (comfoair_wire_render_status.c:105-106), and two members named state meaning different things is
// a prepared confusion.
typedef enum {
    // No broker is configured. How this firmware ships, not a degradation.
    COMFOAIR_MQTT_LINK_OFF = 0,
    // An attempt is in flight. "An attempt is running" is this value paired
    // with retry_in_ms = null, NOT a sixth value called pending -- that value existed to carry a
    // 12 000 ms verdict deadline, and the deadline was disproved (network.timeout_ms is spent
    // TWICE per attempt, mqtt_client.c:1705-1708 and :1715, so the floor is about 22 s).
    COMFOAIR_MQTT_LINK_CONNECTING,
    COMFOAIR_MQTT_LINK_ONLINE,
    // Waiting out a rung of the ladder.
    COMFOAIR_MQTT_LINK_BACKOFF,
    // The broker refused us. Separate from BACKOFF on purpose: the owner's action differs, and
    // so must the colour and the words.
    COMFOAIR_MQTT_LINK_REJECTED,
    COMFOAIR_MQTT_LINK_COUNT,
} comfoair_mqtt_link_t;

// The four words of the mqtt.subscriptions field. A THIRD
// ENUMERATION IN THIS HEADER AND NOT IN comfoair_wire, for the reason this component's
// CMakeLists.txt already gives for the second one: the words of the broker block are spelled once,
// here, where nothing IDF-bound can reach them and a host test can. A spelling in the renderer is
// a second spelling the first time the publisher task needs to log one.
//
// IT ANSWERS A QUESTION NO OTHER MEMBER OF THE DOCUMENT ANSWERS. A broker whose ACL forbids
// SUBSCRIBE accepts the connection, accepts the birth message and accepts every publish: the owner
// sees `online`, every entity live and temperatures that update, and not one button in Home
// Assistant does anything. `link`, `failure` and `last_connack` are all clean in that state.
//
// DO NOT publish `offline` for it instead. That would hide the readings that are arriving
// perfectly well and turn a partial failure into a total one; the truth belongs in this field,
// and <prefix>/status stays `online`. NO PRODUCER YET: nothing records a SUBACK today, so the
// field always reads `none` (docs/interfaces/rest-api.md says the same).
typedef enum {
    // Not connected, or the subscriptions have not been sent yet. Also the answer for a caller
    // with nothing to say -- and the zero value, so a struct nobody filled says "nothing has
    // happened", which is what a device that has never reached a broker means.
    COMFOAIR_MQTT_SUBS_NONE = 0,
    // Sent, and fewer acknowledgements have come back than were sent, with the deadline still
    // running. This is the one value `link` has no equivalent of: `pending` was removed
    // from `link` because its 12 000 ms deadline was disproved, and kept it here
    // because a SUBACK is owed within one round trip and not within two network timeouts.
    COMFOAIR_MQTT_SUBS_PENDING,
    COMFOAIR_MQTT_SUBS_GRANTED,
    // At least one was refused, OR the deadline expired with some unanswered -- silence in answer
    // to SUBSCRIBE is what some ACL configurations do, and it must land here rather than hang in
    // PENDING for ever.
    COMFOAIR_MQTT_SUBS_REFUSED,
    COMFOAIR_MQTT_SUBS_COUNT,
} comfoair_mqtt_subs_t;

// Everything the decider is allowed to know. NOT ONE FIELD OF TIME and not one handle: the
// publisher task owns the clock and the client, this owns the arithmetic.
typedef struct {
    comfoair_mqtt_err_kind_t kind;
    // The CONNACK byte, significant only when kind is REFUSED. A uint32_t and not the esp-mqtt
    // enum on purpose -- this file builds on the host, where mqtt_client.h does not exist.
    uint32_t connack;
    // ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME was seen in esp_tls_last_esp_err.
    bool     name_unresolved;
    // MQTT_EVENT_CONNECTED has already happened in this session. Outranks every other field in
    // comfoair_mqtt_classify(); see its comment.
    bool     was_connected;

    // The three below are read by comfoair_mqtt_link_of() and by nothing else. They live in the
    // same struct rather than in a second one because a second struct over the same event is two
    // spellings of one fact, which is the argument that kept a separate comfoair_mqtt_verdict
    // component out of the tree.
    //
    // A broker host is STORED -- the same distinction comfoair_mqtt_stats_t.configured draws
    // (comfoair_mqtt.h:79-81), and not "a client was built from it".
    bool configured;
    bool connected;
    // A rung is being waited out right now. False while an attempt is actually in flight, which is
    // exactly the CONNECTING-with-null-retry pair.
    bool retry_scheduled;
} comfoair_mqtt_signal_t;

// Which class a signal is. TOTAL: every input has a class and none falls through.
//
// TASK CONTEXT: any task, any core. Takes no locks, holds no state and reads nothing but its
// argument. NULL gives NONE -- a caller with nothing to say has not failed at anything.
comfoair_mqtt_fail_t comfoair_mqtt_classify(const comfoair_mqtt_signal_t *signal);

// Milliseconds to wait before attempt number `attempt` (0-based) of class `why`, jittered by
// `seed`. The table:
//
//   DNS, TCP, UNAVAILABLE   2000  4000  8000 16000 32000 60000 60000 ...   ceiling  60 s
//   DROPPED                 1000  2000  4000  8000 16000 32000 60000 ...   ceiling  60 s
//   PROTOCOL, REJECTED    300000 flat                                      ceiling 300 s
//
// then delay = base x (80 + hash(seed, attempt) mod 41) / 100, i.e. +/-20 % on every row AND on
// the flat rung -- one jitter rule for the whole component, because a second magic multiplier
// inside a pure decider is how the two drift apart.
//
// `attempt` SATURATES AT COMFOAIR_MQTT_ATTEMPT_MAX and larger values answer identically; the
// ladders are infinite and the ceiling is a ceiling on the DELAY, never a count of attempts, so
// there is no input from which the caller can fall out permanently.
//
// NEVER RETURNS 0, for any input, including a `why` that is not a value of the enumeration: zero
// would be a reconnect busy-loop, and an unrecognised class gets the fast row rather than a spin.
// The saturation point. IN THE HEADER because two things share it: the table above, which stops
// climbing here, and the counter the ladder keeps, which stops counting here (without the clamp
// /api/status reports four thousand attempts beside a delay that has not moved since rung 6).
// Two spellings of 16 drift apart on the first edit; the host suite transcribes it as a literal
// rather than reading it back.
#define COMFOAIR_MQTT_ATTEMPT_MAX 16u

uint32_t comfoair_mqtt_backoff_ms(comfoair_mqtt_fail_t why, uint32_t attempt, uint32_t seed);

// Whether a session that has lasted `connected_ms` counts as SETTLED, which is the one thing that
// resets the attempt counter.
//
// DO NOT reset the ladder on connecting instead. A broker that accepts and drops 200 ms later --
// a duplicate client id, an exhausted max_connections, an ACL that cuts after CONNACK -- would
// give an endless cycle of one-second reconnects, each one republishing the whole state
// (comfoair_mqtt.c:142-145). Sixty seconds from below: a full republish takes a fraction of a
// second and the default keepalive is 120 s, so unexplained drops show up inside the first minute.
// From above: a house whose broker honestly reconnects every few minutes still resets and does not
// creep to the ceiling.
//
// Meant to be evaluated on EVERY pass of the publisher task, not once at connect: level-triggered,
// the same style as s_reconfigure, which removes the need to remember "the reset already happened".
bool comfoair_mqtt_backoff_settled(uint32_t connected_ms);

// One word for the mqtt.link field, from the class plus connected, configured and whether a rung
// is being waited out. Still a pure function of the same input and still free of
// every IDF header: this does not turn the decider into a renderer, it keeps the five words from
// being invented a second time in comfoair_wire.
//
// NULL gives OFF. A caller with nothing to say has no broker; answering ONLINE there would make
// the quietest bug look like the healthiest device.
comfoair_mqtt_link_t comfoair_mqtt_link_of(const comfoair_mqtt_signal_t *signal);

// One spelling per class, decided HERE and not in comfoair_wire. The word is needed by the log
// line (esp-mqtt's business) and by /api/status (httpd's business), and comfoair_wire.h:230-236
// already records what a second spelling costs. NEVER NULL, including for a number outside the
// enumeration -- these travel through JSON documents, and a renderer that printed (null) would put
// a bare word into the document and the page would fail to parse a perfectly healthy device.
//
// The pointers are string literals with static storage: they outlive every caller and are never
// freed. A value inside the enumeration never answers "unknown"; that spelling is reserved for a
// number that is not a value.
// What the status document's `last_transport_errno` is, given the int esp-mqtt handed the event
// handler. A POSITIVE errno passes through untouched; anything else is 0, which is what that
// member already means by "nothing was recorded" (comfoair_wire.h).
//
// A CAST WOULD NOT DO, AND THE REASON IS IN THE VENDORED TREE RATHER THAN IN TASTE.
// esp_mqtt_client_dispatch_transport_error() fills esp_transport_sock_errno from
// esp_transport_get_errno() (mqtt_client.c:2359-2375), and THAT function returns -1 when the
// transport has no error handle at all (tcp_transport/transport.c:261-271, ESP-IDF 5.5.5). The
// wire member is a uint32, so (uint32_t)(-1) puts `"last_transport_errno":4294967295` in front of
// an owner whom hardware scenario HV-MQTT-24 has just told to read 111 or 118 there.
//
// RAW ABOVE ZERO, WITH NO TABLE AND NO ALLOW-LIST. ECONNREFUSED, ETIMEDOUT and EHOSTUNREACH ask
// the owner for three different actions and the class `tcp` collapses all three; the document
// already carries a raw Wi-Fi reason code in `failure_reason`, so the precedent
// is set. A filter here would eat exactly the errno nobody anticipated, which is the one worth
// looking up.
//
// HERE, and not beside its one caller in comfoair_mqtt_event.c, for the reason every decision in
// this component moved here: [env:native] does not build comfoair_mqtt, so a rule written in there
// is checked by nothing at all. test/test_mqtt_facts drives it.
uint32_t comfoair_mqtt_transport_errno(int sock_errno);

const char *comfoair_mqtt_fail_name(comfoair_mqtt_fail_t why);
const char *comfoair_mqtt_link_name(comfoair_mqtt_link_t link);
const char *comfoair_mqtt_subs_name(comfoair_mqtt_subs_t subs);

#ifdef __cplusplus
}
#endif
