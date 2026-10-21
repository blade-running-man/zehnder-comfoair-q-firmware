// What the PDO request task sends next, and when: the hourly sweep and the one-PDO read-back, as
// arithmetic over a millisecond count.
//
// PURE, NO CLOCK, NO LOCK. The caller (comfoair_poll's task) reads the time, holds the lock, sends
// the RTR a step names and sleeps for the gap or the wait. Kept apart from that task because the
// task reaches FreeRTOS and the TWAI driver, and a host suite could not link it; this file is the
// part whose mistakes cost bus traffic or a stale state, so it is the part that is tested.
//
// ONE RTR PER STEP. A step is either "send one request for this PDO" or "wait this long". The
// caller puts its frame gap (100 ms) after EVERY send, sweep or read-back alike, so a read-back
// that falls due during a sweep slots in between two sweep frames instead of beside one: the bus
// never sees two requests closer than the gap.
//
// Times are uint32 milliseconds that may wrap; every comparison is a signed difference, valid for
// spans under 2^31 ms (24 days) -- the interval is an hour.
//
// Not thread-safe: the caller serialises every call (comfoair_poll uses one critical section).
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const uint16_t *pdos;           // the sweep's request list; borrowed, must outlive the plan
    size_t          count;
    uint32_t        interval_ms;    // sweep start to next sweep start
    uint32_t        next_sweep_ms;  // when the next scheduled sweep is due
    size_t          sweep_index;    // next PDO of the running sweep; == count when none runs
    bool            sweep_requested;
    bool            readback_pending;
    uint16_t        readback_pdo;
    uint32_t        readback_due_ms;
} comfoair_poll_plan_t;

typedef struct {
    bool     send;     // true: send ONE request for `pdo`, then the frame gap
    uint16_t pdo;
    uint32_t wait_ms;  // send == false: sleep at most this long (a request may end it early)
} comfoair_poll_step_t;

// A sweep is due at `now_ms`: the boot sweep fills in what the unit does not volunteer.
void comfoair_poll_plan_init(comfoair_poll_plan_t *plan, const uint16_t *pdos, size_t count,
                             uint32_t interval_ms, uint32_t now_ms);

// One extra sweep as soon as possible. Any number of calls before it starts give one sweep; a
// call during a sweep gives one more right after it.
void comfoair_poll_plan_request_sweep(comfoair_poll_plan_t *plan);

// One request for `pdo`, `delay_ms` from now. A second call for the same PDO while one is pending
// COALESCES: the single request moves to `delay_ms` after the LATEST call, because a read-back is
// there to see the result of the last command, and one sent between two commands shows the first.
// Returns false, and changes nothing, while a read-back of a DIFFERENT PDO is pending: one slot is
// all the one caller (the away timer after an away command) needs, and dropping either silently
// would be worse than saying so.
bool comfoair_poll_plan_request_readback(comfoair_poll_plan_t *plan, uint16_t pdo,
                                         uint32_t now_ms, uint32_t delay_ms);

// The next step. A due read-back goes first, then the running sweep's next PDO, then a sweep that
// is due or requested (scheduled sweeps count from the START of the previous one); otherwise a
// wait until the earlier of the next sweep and the pending read-back.
comfoair_poll_step_t comfoair_poll_plan_next(comfoair_poll_plan_t *plan, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
