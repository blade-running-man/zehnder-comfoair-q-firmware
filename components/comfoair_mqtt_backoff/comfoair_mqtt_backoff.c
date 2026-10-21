// The MQTT retry ladder as arithmetic (the table is in comfoair_mqtt_backoff.h and in
// docs/interfaces/mqtt.md). Every number in this file is a decision, and the comment beside it
// carries the argument for it, so that changing one is visibly changing a decision rather than
// tuning a constant.
#include "comfoair_mqtt_backoff.h"

#include <stddef.h>

// The first rung is 2 s and not 200 ms, and three different prices pay for
// it. EMQX and the cloud brokers count failed attempts and ban the IP, so "the broker is
// unreachable" becomes a diagnosis we created ourselves; a house of ten nodes wakes together after
// a power cut; and an attempt is not free -- network.timeout_ms is spent TWICE inside one attempt
// (mqtt_client.c:1705-1708 and :1715), so the floor of one attempt is about 20 s anyway. Two
// seconds still put the first three attempts inside the first ~15 s.
#define FIRST_RUNG_MS   2000u
// One rung lower for a session that HAD been established. Failed attempts are
// counted only where it never got that far, so the two facts keep two first rungs.
#define DROPPED_RUNG_MS 1000u
// The ceiling is 60 s and not 30 s, and the arithmetic of the argument FOR 30 s is what
// settles it. Rung 6 arrives 2+4+8+16+32 ~ 62 s after the first failure, while the case the 30 s
// is asked for -- a Home Assistant or Mosquitto add-on restart -- lasts 5-20 s and is resolved on
// rung 2-4, BEFORE the ceiling applies at all. The ceiling governs only the long outage, where
// the owner cannot tell 30 s from 60 s but the SYN rate doubles: 2880 a day against 1440.
#define CEILING_MS      60000u
// The refusal rung: five minutes, because a broker BANS by IP for repeated bad logins. FLAT and
// not a ladder. A faster first retry "if this boot ever had a session" was rejected: as a LEVEL
// condition it gives a 90 s cycle forever against a broker that accepts, holds 60 s and refuses
// on the next connect. If one is ever needed it must be a one-shot token, consumed once per boot.
#define FLAT_RUNG_MS    300000u
// Observable ONLY through the jitter -- every row is on its ceiling long before rung
// 16 -- so this is a bound on the counter the publisher task keeps, made visible here. THE NUMBER
// ITSELF LIVES IN THE HEADER because the ladder in comfoair_mqtt_ladder.c clamps that counter with
// it, and a 16 written twice is a 16 that drifts on the first edit.
#define ATTEMPT_MAX     COMFOAIR_MQTT_ATTEMPT_MAX
// The reset rule, whole: a session that LASTED, not a connection that happened.
#define SETTLED_MS      60000u

// delay = base x (80 + hash mod 41) / 100. One jitter rule for the component,
// including the flat rung -- a second multiplier for the 300 s case is exactly the drift the
// single rule exists to prevent.
#define JITTER_FLOOR_PCT 80u
#define JITTER_SPAN_PCT  41u

// 32-bit FNV-1a over the seed and the (already saturated) attempt.
//
// DO NOT substitute esp_random(). The seed is an FNV-1a of the device MAC, taken by the caller
// (comfoair_mqtt_client.c:208, the same bytes as the client id): that gives every device in a
// house its own ladder, which is the entire anti-storm argument, while staying identical from boot
// to boot so hardware scenario HV-MQTT-16 reproduces and this component's answers can be pinned
// to exact milliseconds by a host test. The one objection to a MAC seed describes two devices with
// a cloned MAC -- and those share a CLIENT ID too, so they evict each other from the broker
// whether or not their retries line up.
//
// DO NOT fold `why` into the hash. PROTOCOL and REJECTED must wait the SAME
// length while carrying different words, and a class-dependent multiplier would silently break
// that equality while every band check in the suite stayed green.
static uint32_t jitter_pct(uint32_t seed, uint32_t attempt)
{
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 4u; i++) {
        h ^= (seed >> (8u * i)) & 0xffu;
        h *= 16777619u;
    }
    for (unsigned i = 0; i < 4u; i++) {
        h ^= (attempt >> (8u * i)) & 0xffu;
        h *= 16777619u;
    }
    return JITTER_FLOOR_PCT + (h % JITTER_SPAN_PCT);
}

// Doubling by repeated multiplication and not by `first << attempt`. HONEST ABOUT WHAT THIS BUYS:
// the two agree for every input reachable today, and the mutation that swaps them passes the whole
// suite -- ATTEMPT_MAX is 16, 2000 << 16 fits in a uint32_t, and the caller clamps before calling.
// The loop is here for the input that is NOT reachable today: it is total for any attempt number,
// because the `base < CEILING_MS` term stops it after at most six iterations whatever it is given,
// while `first << attempt` is undefined behaviour once attempt reaches 32 and silently wraps to a
// SMALL number -- a sub-second retry, i.e. the busy-loop this file exists to prevent -- once
// ATTEMPT_MAX is raised past 20. DO NOT swap it back on the grounds that the tests do not care:
// this is the one place in the component where they do not, and that is written here rather than
// discovered by the person who raises ATTEMPT_MAX.
static uint32_t rung_ms(uint32_t first, uint32_t attempt)
{
    uint32_t base = first;
    for (uint32_t n = 0; n < attempt && base < CEILING_MS; n++) {
        base *= 2u;
    }
    return base > CEILING_MS ? CEILING_MS : base;
}

comfoair_mqtt_fail_t comfoair_mqtt_classify(const comfoair_mqtt_signal_t *signal)
{
    if (signal == NULL) {
        return COMFOAIR_MQTT_FAIL_NONE;
    }

    // FIRST, AND IT OUTRANKS THE ERROR FIELDS. last_connack is a LATCH cleared only by a
    // successful connection or a client stop (comfoair_mqtt.h:86-90), so a session that drops
    // still has whatever refusal code an earlier attempt recorded lying in the atomic. Reading
    // that as "the broker refused our password" would put a live installation on the five-minute
    // rung and paint it with the broker-refusal colour, for a broker that was working a second ago.
    // DO NOT move this below the switch.
    if (signal->was_connected) {
        return COMFOAIR_MQTT_FAIL_DROPPED;
    }

    switch (signal->kind) {
    case COMFOAIR_MQTT_ERR_NONE:
        break;
    case COMFOAIR_MQTT_ERR_TRANSPORT:
        // The connack field is not consulted here on purpose: a transport error carries no CONNACK
        // and whatever is in that field belongs to an earlier attempt.
        return signal->name_unresolved ? COMFOAIR_MQTT_FAIL_DNS : COMFOAIR_MQTT_FAIL_TCP;
    case COMFOAIR_MQTT_ERR_REFUSED:
        // THE THIRD READER OF THIS CODE SPACE, and the other two are named so that none of the
        // three is edited alone: comfoair_wire_connack_name() spells the codes for /api/status
        // (comfoair_wire_render.c:144-160) and comfoair_phase_connack_is_refusal() decides which of
        // them latch the BROKER_REJECTED phase (comfoair_phase.c:73-79). The split below is the
        // same partition as that second one -- PROTOCOL plus REJECTED is exactly "a refusal" -- and
        // test_the_refusal_classes_agree_with_the_phase_reader holds the two together rather than a
        // comment asking politely. DO NOT give a code a meaning in one of the three and not the
        // others: a broker answer that changes the colour here and keeps the generic spelling there
        // is a device whose light and whose words disagree.
        //
        // Stated as the codes that are NOT the credentials, because the interesting half of the
        // rule is the open end: a code outside 0..5 tells us nothing, and "the login is wrong" is
        // the answer whose cost of being wrong is lowest -- five minutes of waiting against a ban.
        if (signal->connack == 0u) { break; }
        if (signal->connack == 3u) { return COMFOAIR_MQTT_FAIL_UNAVAILABLE; }
        if (signal->connack == 1u || signal->connack == 2u) { return COMFOAIR_MQTT_FAIL_PROTOCOL; }
        return COMFOAIR_MQTT_FAIL_REJECTED;
    }
    // Both the NONE branch and a `kind` that is not a value of the enumeration land here. A
    // failure class is not invented for a signal that reports nothing.
    return COMFOAIR_MQTT_FAIL_NONE;
}

uint32_t comfoair_mqtt_backoff_ms(comfoair_mqtt_fail_t why, uint32_t attempt, uint32_t seed)
{
    // Saturated BEFORE the hash and not only before the rung, and that ordering is the only thing
    // that makes ATTEMPT_MAX observable: every row is already on its ceiling by rung 6, so if the
    // raw number reached jitter_pct() then attempt 16 and attempt 1000 would differ by up to 40 %
    // and the counter would not be saturating at all. test_attempt_saturates_at_sixteen is the
    // witness. DO NOT hash the caller's number.
    if (attempt > ATTEMPT_MAX) {
        attempt = ATTEMPT_MAX;
    }

    uint32_t base;
    switch (why) {
    case COMFOAIR_MQTT_FAIL_DROPPED:
        base = rung_ms(DROPPED_RUNG_MS, attempt);
        break;
    case COMFOAIR_MQTT_FAIL_PROTOCOL:
    case COMFOAIR_MQTT_FAIL_REJECTED:
        base = FLAT_RUNG_MS;
        break;
    case COMFOAIR_MQTT_FAIL_DNS:
    case COMFOAIR_MQTT_FAIL_TCP:
    case COMFOAIR_MQTT_FAIL_UNAVAILABLE:
    case COMFOAIR_MQTT_FAIL_NONE:
    case COMFOAIR_MQTT_FAIL_COUNT:
    default:
        // NONE, COUNT and any number that is not a class get the fast row. Not zero, and not a
        // short retry either: zero is a reconnect busy-loop, and the caller that asked has already
        // failed at something it could not name. `default:` is present because `why` arrives as an
        // integer from an atomic and may hold a value no enumerator has; the enumerators are all
        // listed above it so that -Werror=switch still reports a seventh class (platformio.ini).
        base = rung_ms(FIRST_RUNG_MS, attempt);
        break;
    }

    // Every base in the table is a multiple of 100, so this division is exact and the ratio
    // between two rows survives the jitter -- which is what lets the suite assert that the DROPPED
    // row is the fast row shifted by exactly one rung. Widest product: 300000 x 120 = 36 000 000,
    // comfortably inside uint32_t.
    return base / 100u * jitter_pct(seed, attempt);
}

bool comfoair_mqtt_backoff_settled(uint32_t connected_ms)
{
    return connected_ms >= SETTLED_MS;
}

comfoair_mqtt_link_t comfoair_mqtt_link_of(const comfoair_mqtt_signal_t *signal)
{
    if (signal == NULL || !signal->configured) {
        return COMFOAIR_MQTT_LINK_OFF;
    }
    // Connected outranks the latched failure: the class describes the last thing that went wrong,
    // and on a device that is publishing right now that is history, not state.
    if (signal->connected) {
        return COMFOAIR_MQTT_LINK_ONLINE;
    }

    comfoair_mqtt_fail_t why = comfoair_mqtt_classify(signal);
    // The two refusal classes are their own word, their own colour and their own text (the
    // owner's choice), because the owner's action differs from "wait". They share this value
    // because both send him to the broker's settings -- and the two
    // are still told apart by comfoair_mqtt_fail_name in the same document.
    if (why == COMFOAIR_MQTT_FAIL_REJECTED || why == COMFOAIR_MQTT_FAIL_PROTOCOL) {
        return COMFOAIR_MQTT_LINK_REJECTED;
    }
    // "An attempt is running" is CONNECTING paired with retry_in_ms = null,
    // and a rung being waited out is BACKOFF. There is no sixth value for "we have asked and not
    // heard back" -- that was `pending`, and the 12 000 ms deadline it existed to carry was
    // disproved by network.timeout_ms being spent twice per attempt.
    return signal->retry_scheduled ? COMFOAIR_MQTT_LINK_BACKOFF : COMFOAIR_MQTT_LINK_CONNECTING;
}

const char *comfoair_mqtt_fail_name(comfoair_mqtt_fail_t why)
{
    switch (why) {
    case COMFOAIR_MQTT_FAIL_NONE:        return "none";
    case COMFOAIR_MQTT_FAIL_DNS:         return "dns";
    case COMFOAIR_MQTT_FAIL_TCP:         return "tcp";
    case COMFOAIR_MQTT_FAIL_UNAVAILABLE: return "unavailable";
    case COMFOAIR_MQTT_FAIL_PROTOCOL:    return "protocol";
    // "bad-credentials" and not "rejected", because the refused login was given its own TEXT,
    // against PROTOCOL's. The two share a colour and must not share a word -- an owner whose
    // password is correct must not be sent hunting for it.
    case COMFOAIR_MQTT_FAIL_REJECTED:    return "bad-credentials";
    case COMFOAIR_MQTT_FAIL_DROPPED:     return "dropped";
    case COMFOAIR_MQTT_FAIL_COUNT:       break;
    }
    // Never NULL and never a fabricated meaning, the same rule as comfoair_wire_connack_name: a
    // renderer that printed (null) would put a bare word into the status document and the page
    // would fail to parse a perfectly healthy device.
    return "unknown";
}

const char *comfoair_mqtt_link_name(comfoair_mqtt_link_t link)
{
    switch (link) {
    case COMFOAIR_MQTT_LINK_OFF:        return "off";
    case COMFOAIR_MQTT_LINK_CONNECTING: return "connecting";
    case COMFOAIR_MQTT_LINK_ONLINE:     return "online";
    case COMFOAIR_MQTT_LINK_BACKOFF:    return "backoff";
    case COMFOAIR_MQTT_LINK_REJECTED:   return "rejected";
    case COMFOAIR_MQTT_LINK_COUNT:      break;
    }
    return "unknown";
}

const char *comfoair_mqtt_subs_name(comfoair_mqtt_subs_t subs)
{
    switch (subs) {
    case COMFOAIR_MQTT_SUBS_NONE:    return "none";
    case COMFOAIR_MQTT_SUBS_PENDING: return "pending";
    case COMFOAIR_MQTT_SUBS_GRANTED: return "granted";
    case COMFOAIR_MQTT_SUBS_REFUSED: return "refused";
    case COMFOAIR_MQTT_SUBS_COUNT:   break;
    }
    return "unknown";
}

uint32_t comfoair_mqtt_transport_errno(int sock_errno)
{
    // The header carries the whole argument. In one line: esp_transport_get_errno() answers -1 for
    // "there was no error handle to ask", and a cast would render that as 4294967295 in the field
    // the owner is told to read as an errno.
    return sock_errno > 0 ? (uint32_t)sock_errno : 0u;
}
