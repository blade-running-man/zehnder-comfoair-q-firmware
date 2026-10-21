// The publisher-task pass, as arithmetic: which rung is armed, when it comes due, when
// the counter resets and what /api/status is told -- with the clock, the socket and the client
// handle left to the caller. The header beside this file carries the argument for it being here
// and not written out inside comfoair_mqtt; the short form is that comfoair_mqtt does not build
// on the host, and test_an_address_returning_does_not_burn_an_attempt is a HOST
// case.
//
// SPLIT FROM comfoair_mqtt_backoff.c along the seam the two already had -- the table there, the
// state machine here -- because one file reached the 350-line ceiling (AGENTS.md), and the headers
// were split along the same seam for the same reason. One component; the constant the two sources
// share, COMFOAIR_MQTT_ATTEMPT_MAX, is spelled once, in comfoair_mqtt_backoff.h.
#include "comfoair_mqtt_ladder.h"

#include <assert.h>
#include <stddef.h>

static comfoair_mqtt_signal_t signal_of(const comfoair_mqtt_ladder_in_t *in, bool retry_scheduled)
{
    // ALL SEVEN FIELDS, ONE INSTANCE, ONE PLACE. This is the Task 4 review's first requirement and
    // the most expensive trap in the task: a signal built for comfoair_mqtt_classify() out of the
    // four error fields and then handed to comfoair_mqtt_link_of() answers "off" -- a valid value,
    // a green suite, a rendered document and a diagnostic that lies about a live broker. There is
    // exactly one builder so that a caller cannot build half of one.
    comfoair_mqtt_signal_t s;
    s.kind            = in->kind;
    s.connack         = in->connack;
    s.name_unresolved = in->name_unresolved;
    s.was_connected   = in->was_connected;
    s.configured      = in->configured;
    s.connected       = in->connected;
    // The value AFTER this pass has updated it, never before: link_of tells BACKOFF from
    // CONNECTING by this field alone, so a stale one reports a rung being waited out during an
    // attempt that is actually in flight.
    s.retry_scheduled = retry_scheduled;
    return s;
}

void comfoair_mqtt_ladder_pass(comfoair_mqtt_ladder_t *st, const comfoair_mqtt_ladder_in_t *in,
                               comfoair_mqtt_ladder_out_t *out)
{
    if (st == NULL || in == NULL || out == NULL) {
        return;
    }
    out->reconnect = false;

    if (!in->client) {
        // No client object: either no broker is stored -- the state every device ships in -- or
        // building one failed. Neither is repaired by reconnecting something that does not exist,
        // and the rebuild is the owner's save, which arrives as a reconfigure.
        st->retry_scheduled = false;
        st->session_timed   = false;
        st->address_lost    = !in->has_address;
    } else if (in->connected) {
        if (!st->session_timed) {
            st->session_timed      = true;
            st->connected_since_ms = in->now_ms;
        }
        // EVERY PASS, not once at connect: level-triggered, the same style as s_reconfigure, which
        // is what removes the need to remember "the reset has already happened". A
        // broker that accepts and drops 200 ms later never reaches this and never resets, which is
        // the whole reason the rule is 60 seconds of SESSION rather than a connection.
        if (comfoair_mqtt_backoff_settled(in->now_ms - st->connected_since_ms)) {
            st->attempt = 0;
        }
        st->retry_scheduled = false;
        st->address_lost    = !in->has_address;
    } else {
        st->session_timed = false;
        if (!in->has_address) {
            // No address: the ladder STANDS. Dialling TCP with no address is not a test of the
            // broker, and counting the failure would meet a two-minute Wi-Fi outage with a
            // 60-second rung.
            st->retry_scheduled = false;
            st->address_lost    = true;
        } else if (!in->failed && !st->retry_scheduled) {
            // Nothing has failed and no rung is armed: the client is dialling by itself, either
            // because it was just built or because a reconfigure rebuilt it. There is nothing to
            // retry, and saying otherwise would report a rung being waited out during a live
            // attempt and count an attempt that was never ours.
            //
            // NOT A PLACE THE LADDER CAN GET STUCK: the input is false only until the event
            // handler records a failure, and every way a client can stop dialling produces one --
            // including the timeout that dispatches no error event at all.
            // address_lost is left as it is, so an outage that ends here still gets its immediate
            // attempt once a failure is recorded.
        } else {
            if (st->address_lost) {
                // The address is back. Attempt AT ONCE -- no new rung, no increment -- and the
                // arithmetic below does exactly that: retry_at_ms == now_ms is due on this pass.
                st->address_lost    = false;
                st->retry_at_ms     = in->now_ms;
                st->retry_scheduled = true;
            }
            if (!st->retry_scheduled) {
                const comfoair_mqtt_signal_t s = signal_of(in, false);
                st->retry_at_ms =
                    in->now_ms + comfoair_mqtt_backoff_ms(comfoair_mqtt_classify(&s), st->attempt,
                                                          in->seed);
                st->retry_scheduled = true;
                if (st->attempt < COMFOAIR_MQTT_ATTEMPT_MAX) {
                    st->attempt++;
                }
                // A signed difference and not `now >= retry_at`: the clock is a 32-bit millisecond
                // counter that passes through zero every ~49.7 days, and the unsigned comparison
                // would park the ladder for the whole of the next wrap.
            } else if ((int32_t)(in->now_ms - st->retry_at_ms) >= 0) {
                st->retry_scheduled = false;
                out->reconnect      = true;
            }
        }
    }

    const comfoair_mqtt_signal_t s = signal_of(in, st->retry_scheduled);
    out->link                      = comfoair_mqtt_link_of(&s);
    out->failure                   = comfoair_mqtt_classify(&s);
    out->attempts                  = st->attempt;
    out->retry_known               = st->retry_scheduled;
    const int32_t left             = (int32_t)(st->retry_at_ms - in->now_ms);
    out->retry_in_ms               = st->retry_scheduled && left > 0 ? (uint32_t)left : 0u;
}

void comfoair_mqtt_ladder_not_yet(comfoair_mqtt_ladder_t *st, uint32_t now_ms)
{
    if (st == NULL) {
        return;
    }
    st->retry_scheduled = true;
    st->retry_at_ms     = now_ms;
}

void comfoair_mqtt_ladder_reset(comfoair_mqtt_ladder_t *st)
{
    if (st == NULL) {
        return;
    }
    // address_lost is NOT touched, and that is deliberate: setting it would make the next pass
    // take the "the address has just come back" branch, which is a fact about Wi-Fi and not about
    // the owner's save. Rung 0 follows immediately anyway -- retry_scheduled false schedules it --
    // so nothing is gained and a false fact would be recorded.
    st->retry_scheduled = false;
    st->session_timed   = false;
    st->attempt         = 0;
}

// --- the failure word, and the seed --------------------------------------------------------

// The one thing about the layout that a test cannot state: that the field is WIDE ENOUGH. A
// seventh failure class would be caught by -Werror=switch all over this component, but a fourth
// error KIND would simply be truncated on its way through the atomic and arrive as a different
// one, silently and for ever. This says so at compile time instead.
_Static_assert((uint32_t)COMFOAIR_MQTT_ERR_REFUSED <= COMFOAIR_MQTT_FAILWORD_KIND_MASK,
               "a new comfoair_mqtt_err_kind_t needs a wider COMFOAIR_MQTT_FAILWORD_KIND_MASK and "
               "a higher COMFOAIR_MQTT_FAILWORD_KIND_SHIFT");

uint32_t comfoair_mqtt_failword_pack(comfoair_mqtt_err_kind_t kind, uint32_t connack,
                                     bool unresolved, bool was_connected)
{
    return ((uint32_t)kind << COMFOAIR_MQTT_FAILWORD_KIND_SHIFT) |
           (connack & COMFOAIR_MQTT_FAILWORD_CONNACK_MASK) | COMFOAIR_MQTT_FAILWORD_FAILED |
           (unresolved ? COMFOAIR_MQTT_FAILWORD_UNRESOLVED : 0u) |
           (was_connected ? COMFOAIR_MQTT_FAILWORD_WAS_CONNECTED : 0u);
}

void comfoair_mqtt_failword_unpack(uint32_t word, comfoair_mqtt_ladder_in_t *in)
{
    if (in == NULL) {
        return;
    }
    // MASKED, so that a bit nobody has defined yet cannot land in the kind. It is not a promise
    // that the result is a value of the enumeration -- the mask holds one number the enumeration
    // does not use -- and it does not need to be: comfoair_mqtt_classify() answers NONE for a kind
    // it does not recognise, and comfoair_mqtt_backoff_ms() gives it the fast row rather than a
    // spin. The static assertion above is what keeps the field wide enough to be worth masking.
    in->kind = (comfoair_mqtt_err_kind_t)((word >> COMFOAIR_MQTT_FAILWORD_KIND_SHIFT) &
                                          COMFOAIR_MQTT_FAILWORD_KIND_MASK);
    in->connack         = word & COMFOAIR_MQTT_FAILWORD_CONNACK_MASK;
    in->name_unresolved = (word & COMFOAIR_MQTT_FAILWORD_UNRESOLVED) != 0u;
    in->was_connected   = (word & COMFOAIR_MQTT_FAILWORD_WAS_CONNECTED) != 0u;
    in->failed          = (word & COMFOAIR_MQTT_FAILWORD_FAILED) != 0u;
}

uint32_t comfoair_mqtt_backoff_seed(const char *device_id)
{
    uint32_t h = 2166136261u;
    for (const char *p = device_id; p != NULL && *p != '\0'; p++) {
        h ^= (uint32_t)(unsigned char)*p;
        h *= 16777619u;
    }
    return h;
}
