// WHEN the probe task asks the ventilation unit again: the full unit read (identity, active errors,
// the three settings) and, between two of those, the active errors alone. One schedule with two
// deadlines -- comfoair_probe_task_unit.cpp's rule that a second timer would be a second answer to
// "when is the unit asked" still holds.
//
// PURE. Ticks in, a verdict out; the probe task owns the clock and the reads. All arithmetic is on
// unsigned differences, so a wrapping tick counter makes nothing due for ever or never. One owner,
// the probe task: not thread-safe. No call fails; a NULL cadence is a no-op (or NONE, see _due).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Seconds. FIRST: the bus and the unit are up well before, and a page opened in the first minute
// has something to show. OK: identity and settings change only when somebody changes them.
// RETRY: after an incomplete full read. ERRORS: the active errors alone, so Home Assistant hears of
// a unit fault within a minute rather than six hours -- one `80 03 01` and a 32-byte answer, about
// six CAN frames a minute against the ~1.5 PDO frames a second the unit broadcasts anyway.
#define COMFOAIR_UNIT_FIRST_S  10u
#define COMFOAIR_UNIT_OK_S     (6u * 60u * 60u)
#define COMFOAIR_UNIT_RETRY_S  60u
#define COMFOAIR_UNIT_ERRORS_S 60u

typedef enum {
    COMFOAIR_UNIT_READ_NONE = 0,
    COMFOAIR_UNIT_READ_FULL,    // identity + errors + settings
    COMFOAIR_UNIT_READ_ERRORS,  // `80 03 01` alone
} comfoair_unit_read_t;

typedef struct {
    uint32_t full_from, full_after;  // due at full_from + full_after, in ticks, wrap-safe
    uint32_t errors_from, errors_after;
} comfoair_unit_cadence_t;

// Both reads due FIRST seconds from `now`; the full one wins, and it reads the errors too.
void comfoair_unit_cadence_arm(comfoair_unit_cadence_t *c, uint32_t now, uint32_t tick_hz);

// What is due at `now`. FULL wins over ERRORS. `*wait` (may be NULL) is always in TICKS: 0 when
// something is due, else the ticks until the nearer deadline. A NULL cadence: NONE, and one
// COMFOAIR_UNIT_ERRORS_S period at `tick_hz` -- the tick rate is a parameter for that case alone,
// so a caller sleeping on `*wait` never meets a number in another unit.
comfoair_unit_read_t comfoair_unit_cadence_due(const comfoair_unit_cadence_t *c, uint32_t now,
                                               uint32_t tick_hz, uint32_t *wait);

// A full read ended at `now`: next full in OK (complete) or RETRY seconds; the errors clock
// restarts too, because the full read read them.
void comfoair_unit_cadence_full_done(comfoair_unit_cadence_t *c, uint32_t now, uint32_t tick_hz,
                                     bool complete);

// An errors-only read ended at `now`, answered or not: the next one in ERRORS seconds. An
// unanswered one is not retried sooner -- the next minute is the retry.
void comfoair_unit_cadence_errors_done(comfoair_unit_cadence_t *c, uint32_t now, uint32_t tick_hz);

#ifdef __cplusplus
}
#endif
