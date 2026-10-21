// The publisher task's pass, as a state machine: rungs, the stand-down while there is no
// address, the reset. Its sibling header, comfoair_mqtt_backoff.h, holds the decisions
// this drives: which class a failure is, how long its rung is and which of the five words the
// status document prints.
//
// A SECOND HEADER AND NOT MORE LINES IN THE FIRST, along the seam the sources already have -- the
// table there, the machine here -- because that one reached the 350-line ceiling (AGENTS.md). One
// component still: nothing here needs a REQUIRES either.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_mqtt_backoff.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- the ladder ------------------------------------------------------------------------------
//
// WHY THIS IS HERE AND NOT WRITTEN OUT INSIDE comfoair_mqtt. The obvious home for the ladder's
// state is static variables next to the publisher task, with the logic inline in its body.
// Written that way NOTHING BELOW CAN BE TESTED: comfoair_mqtt is esp-mqtt, FreeRTOS and a client
// handle, so it does not build on the host at all -- yet the suite needs
// test_an_address_returning_does_not_burn_an_attempt as a host case. The state machine is
// therefore the same shape as the table above it: a pure function of its arguments, with the
// clock, the socket and the handle left entirely to the caller.
//
// It buys a second thing, and it is the more important one: the seven fields of
// comfoair_mqtt_signal_t are filled in ONE place, for ONE instance, which is passed to BOTH
// comfoair_mqtt_classify() and comfoair_mqtt_link_of(). A caller that fills four of them for the
// classifier and hands the same half-built struct to link_of reports link "off" for ever on a
// live broker -- a valid value, a green suite and a lying document. Here that cannot be written.
//
// STILL NO CLOCK. `now_ms` is a NUMBER the caller reads from its own clock; the comparisons below
// are int32_t differences, so the machine is correct across the 32-bit millisecond wrap (~49.7
// days) and at now == 0, which is where a zero sentinel would have failed.

// Everything the ladder remembers between two passes. ONE INSTANCE, owned by the publisher task
// and by nothing else -- that is the whole of the concurrency argument, and it is why there is not
// an atomic or a volatile in this struct. Zero-initialised is the correct starting state.
//
// DO NOT clear this from comfoair_mqtt_stop_client(). That function has three callers and two of
// them run on app_main before the publisher task exists (comfoair_mqtt_client.c:138-145); a reset
// from there is a second writer. The contrast worth keeping in view: g_last_connack IS cleared
// there and correctly so, because the EVENT HANDLER writes it, not the publisher.
typedef struct {
    // Separate from retry_at_ms, and the separation is the point. One zero meaning both "no rung
    // is armed" and "the rung is due now" is what made an address coming back cost a whole rung
    // and burn an attempt.
    bool     retry_scheduled;
    uint32_t retry_at_ms;
    // Whether connected_since_ms means anything yet: the same rule, one field per fact.
    bool     session_timed;
    uint32_t connected_since_ms;
    // A previous pass stood the ladder down for want of an address. While there is
    // none no attempt is made and none is counted, and the moment one appears an attempt is made
    // at once, keeping the counter where it was -- a broker that is down must not be forgiven
    // because OUR Wi-Fi blinked, and our Wi-Fi blinking must not cost the broker a rung.
    //
    // DO NOT SPELL THIS `had_address`, THE OTHER WAY ROUND: THAT COSTS A RUNG AT BOOT --
    // zero-initialised it reads as "there was no address a moment ago", so the first pass of every
    // boot grants itself the free immediate attempt meant for a returning address, and the first
    // interval in the log is 0 s instead of the 2 s hardware scenario HV-MQTT-16 expects. Stated as
    // the LOSS, the zero value is the truth -- nothing has been stood down yet -- and the free
    // attempt is granted only where an address actually came back.
    bool     address_lost;
    // Failed attempts since the last SETTLED session. Saturates at the same 16 the table does,
    // HERE, on the publisher's side: without that /api/status reports 4000 attempts beside a delay
    // that has not moved since attempt 6.
    uint32_t attempt;
} comfoair_mqtt_ladder_t;

// What one pass is allowed to know. The first six are facts about the device; the last four are
// the event handler's atomics, unpacked. Nothing here is a handle and nothing here is a pointer
// into esp-mqtt.
typedef struct {
    uint32_t now_ms;
    // A client OBJECT exists. Distinct from `configured`, which is whether a host is STORED: they
    // differ exactly when esp_mqtt_client_init() or _start() failed, and then there is nothing to
    // reconnect and no rung to climb.
    bool     client;
    bool     configured;
    bool     connected;
    // An IP address, not a link-layer association; with none, the ladder stands.
    bool     has_address;
    // FNV-1a of the device id, computed by the caller. Never esp_random().
    uint32_t seed;
    comfoair_mqtt_err_kind_t kind;
    uint32_t connack;
    bool     name_unresolved;
    // The connection that FAILED had been established (the DROPPED row). Not "we are
    // connected now" -- that is `connected` above, and confusing the two turns every dropped
    // session into a TCP failure and doubles its first rung.
    bool     was_connected;
    // Something has failed since the last successful connection. FALSE MEANS THE CLIENT IS STILL
    // DIALLING ON ITS OWN -- a client that has just been built, or rebuilt by a reconfigure, is in
    // MQTT_STATE_INIT and reaches the broker without being asked -- so the ladder arms nothing and
    // counts nothing until this is true. Without it every boot spends its first minute reporting
    // "backoff, one attempt" over an attempt that is in flight and was never ours.
    //
    // NOT the same question as `kind != NONE`: a timed-out esp_mqtt_connect dispatches only
    // MQTT_EVENT_DISCONNECTED and no error, so the kind of a real failure can be
    // NONE -- and a ladder keyed on the kind would then never dial again.
    bool     failed;
} comfoair_mqtt_ladder_in_t;

// What the caller does, and what it publishes. Every field is written on every pass.
typedef struct {
    // The one and only effect this component can ask for, and the no-reboot rule
    // (AGENTS.md) is visible in the type: a bool and a millisecond count cannot ask
    // for a reboot, an escalation or a bounded number of tries.
    bool                 reconnect;
    comfoair_mqtt_link_t link;
    comfoair_mqtt_fail_t failure;
    uint32_t             attempts;
    // false means "no attempt is scheduled", which the status document renders as null and NOT as
    // zero -- opposite answers.
    bool                 retry_known;
    uint32_t             retry_in_ms;
} comfoair_mqtt_ladder_out_t;

// One pass of the publisher task, as arithmetic. TOTAL: any input is answered, NULLs included.
//
// The caller's whole side of it is: read the clock and the address, unpack the handler's atomics,
// call this, and if `reconnect` came back true call esp_mqtt_client_reconnect().
void comfoair_mqtt_ladder_pass(comfoair_mqtt_ladder_t *state, const comfoair_mqtt_ladder_in_t *in,
                               comfoair_mqtt_ladder_out_t *out);

// esp_mqtt_client_reconnect() answered ESP_FAIL: the client is not parked in WAIT_RECONNECT yet
// (mqtt_client.c:1903), so NOTHING WAS ATTEMPTED. Re-arms the same rung for the next pass without
// consuming it and without counting an attempt.
//
// It is what "ESP_FAIL == not parked yet, repeat on the next tick" means in practice, and
// leaving it out is not neutral: a dial that takes the full network timeout (up to 2 x 10 s,
// mqtt_client.c:1705-1708 and :1715) would otherwise let the ladder count and climb several rungs
// while ONE attempt was still in flight, so /api/status would report attempts nobody made.
void comfoair_mqtt_ladder_not_yet(comfoair_mqtt_ladder_t *state, uint32_t now_ms);

// The second of the two ways the ladder resets, and the only one that is not a level rule: the
// owner saved his settings, so he has just corrected something and must not be made to wait. This
// is the exit from a five-minute BROKER_REJECTED rung within a second. TASK CONTEXT: the publisher
// task, in the branch that rebuilds the client, AFTER it has been rebuilt.
//
// The first path is inside the pass itself: comfoair_mqtt_backoff_settled() is evaluated on every
// pass while connected, not once at connect.
void comfoair_mqtt_ladder_reset(comfoair_mqtt_ladder_t *state);

// --- the failure word ------------------------------------------------------------------------
//
// The four facts the event handler observes, packed into ONE 32-bit word so that a reader that
// takes a single relaxed load gets four fields that agree. Two atomics -- the
// obvious shape -- can be read across a store, and the pair that comes back is then a failure that
// never happened: kind REFUSED from the old value with connack 0 from the new one classifies as
// "nothing failed", so the device would climb the fast row against a broker that had just refused
// its password.
//
// IT LIVES HERE, IN THE COMPONENT THAT BUILDS ON THE HOST, and the reason is that the offsets are
// exactly the kind of thing no test would otherwise touch: put FAILED at bit 10 instead of 11 and
// it collides with the low bit of the kind, so every transport failure classifies as `none` for
// ever -- with a green suite, a working ladder and a status document that reads plausibly.
// test_the_failure_word_survives_a_round_trip is what makes that a red test instead of a story.
#define COMFOAIR_MQTT_FAILWORD_CONNACK_MASK  0xffu
#define COMFOAIR_MQTT_FAILWORD_UNRESOLVED    (1u << 8)
#define COMFOAIR_MQTT_FAILWORD_WAS_CONNECTED (1u << 9)
// SOMETHING FAILED, whatever it was. A whole word of zero therefore means "nothing has failed
// since this dial began", which is the fact comfoair_mqtt_ladder_in_t.failed carries and the
// reason a client that is still dialling is left alone.
#define COMFOAIR_MQTT_FAILWORD_FAILED        (1u << 10)
#define COMFOAIR_MQTT_FAILWORD_KIND_SHIFT    11u
#define COMFOAIR_MQTT_FAILWORD_KIND_MASK     0x3u

// Packs a failure. ALWAYS SETS FAILED, because the only reason to call it is that something did.
uint32_t comfoair_mqtt_failword_pack(comfoair_mqtt_err_kind_t kind, uint32_t connack,
                                     bool unresolved, bool was_connected);

// Unpacks one into the five fields of `in` that describe a failure, leaving the rest of the struct
// alone. NULL-safe; every 32-bit value is a valid input, including ones no packer produced.
void comfoair_mqtt_failword_unpack(uint32_t word, comfoair_mqtt_ladder_in_t *in);

// --- the jitter seed -------------------------------------------------------------------------

// 32-bit FNV-1a of the device id -- the full MAC, which is also the client id. That way
// every device in a house gets its own ladder, which is the whole anti-storm argument, and gets
// the SAME one from boot to boot, so hardware scenario HV-MQTT-16 reproduces and a test can pin
// exact milliseconds.
//
// IN THE COMPONENT AND NOT AT THE CALL SITE, although the caller is the only one with the bytes:
// written next to the client it was invisible to every test in the tree, and a `return 0;` there
// passed the entire suite and built a firmware in which every device in a house retries in
// lockstep. That is the anti-storm argument silently deleted. NULL and the empty string answer
// with the FNV offset basis, which is a constant like any other -- a device with no id has no
// ladder of its own, and there is nothing better to say about it.
//
// DO NOT substitute esp_random(): it buys nothing the MAC does not and costs both of those.
uint32_t comfoair_mqtt_backoff_seed(const char *device_id);

#ifdef __cplusplus
}
#endif
