// See comfoair_unit_cadence.h.
#include "comfoair_unit_cadence.h"

#include <stddef.h>

// Seconds to ticks in 64 bits: the product is formed before the cast, so 6 h at any tick rate whose
// result fits cannot overflow on the way. DO NOT replace with pdMS_TO_TICKS: it multiplies in
// TickType_t (projdefs.h), and 6 h in milliseconds times 1000 Hz is past 2^32.
static uint32_t ticks(uint32_t seconds, uint32_t tick_hz)
{
    return (uint32_t)((uint64_t)seconds * tick_hz);
}

// Ticks left until from + after, 0 once due. On the difference `now - from`, never on a computed
// absolute deadline: a deadline past the wrap would compare as long overdue (or never due).
static uint32_t remaining(uint32_t from, uint32_t after, uint32_t now)
{
    const uint32_t waited = now - from;
    return waited >= after ? 0 : after - waited;
}

void comfoair_unit_cadence_arm(comfoair_unit_cadence_t *c, uint32_t now, uint32_t tick_hz)
{
    if (c == NULL)
        return;
    c->full_from = c->errors_from = now;
    c->full_after = c->errors_after = ticks(COMFOAIR_UNIT_FIRST_S, tick_hz);
}

comfoair_unit_read_t comfoair_unit_cadence_due(const comfoair_unit_cadence_t *c, uint32_t now,
                                               uint32_t tick_hz, uint32_t *wait)
{
    comfoair_unit_read_t verdict = COMFOAIR_UNIT_READ_NONE;
    uint32_t w = ticks(COMFOAIR_UNIT_ERRORS_S, tick_hz);
    if (c != NULL) {
        const uint32_t full = remaining(c->full_from, c->full_after, now);
        const uint32_t errors = remaining(c->errors_from, c->errors_after, now);
        // FULL FIRST: it reads the errors too, so a due errors read is folded into it.
        if (full == 0) {
            verdict = COMFOAIR_UNIT_READ_FULL;
            w = 0;
        } else if (errors == 0) {
            verdict = COMFOAIR_UNIT_READ_ERRORS;
            w = 0;
        } else {
            w = full < errors ? full : errors;
        }
    }
    if (wait != NULL)
        *wait = w;
    return verdict;
}

void comfoair_unit_cadence_full_done(comfoair_unit_cadence_t *c, uint32_t now, uint32_t tick_hz,
                                     bool complete)
{
    if (c == NULL)
        return;
    c->full_from = now;
    c->full_after = ticks(complete ? COMFOAIR_UNIT_OK_S : COMFOAIR_UNIT_RETRY_S, tick_hz);
    c->errors_from = now;
    c->errors_after = ticks(COMFOAIR_UNIT_ERRORS_S, tick_hz);
}

void comfoair_unit_cadence_errors_done(comfoair_unit_cadence_t *c, uint32_t now, uint32_t tick_hz)
{
    if (c == NULL)
        return;
    c->errors_from = now;
    c->errors_after = ticks(COMFOAIR_UNIT_ERRORS_S, tick_hz);
}
