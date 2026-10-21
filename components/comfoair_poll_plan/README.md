# comfoair_poll_plan — what the PDO request task sends next, and when

> This is documentation, not code — re-read the source before relying on a detail.

## Purpose

`comfoair_poll`'s task sends remote-transmission requests (RTRs) for PDOs: one sweep of every
polled PDO at boot and hourly, an extra sweep when the bus has gone quiet, and — since the
`away_mode` switch reads PDO 337, which the Q350 census never heard between sweeps (whether it is
broadcast on change is unconfirmed) — ONE request for one PDO
about 1.5 s after an away command. This component is the pure half: given a millisecond count, it
answers "send a request for this PDO" or "sleep this long". No clock, no lock, no FreeRTOS, so
every decision is host-tested.

## Responsibility

Owns: the sweep schedule (due at init, then one interval after each sweep's start), requested
sweeps (collapsed), the one read-back slot (coalesced, re-armed to the latest call), and the order
between them.

Does NOT do: read the clock, lock, send, sleep, or know which PDOs exist — `comfoair_poll` does the
first four and passes `COMFOAIR_REQUEST_PDOS` in. Does not decide *which* PDO to read back after a
command — `comfoair_command_readback_pdo()` does, and `comfoair_control` makes the call.

## Public API

`include/comfoair_poll_plan.h` (C linkage). Not thread-safe; the caller serialises every call.

| Symbol | Contract |
| --- | --- |
| `comfoair_poll_plan_t` | The whole state. `pdos` is borrowed and must outlive the plan. |
| `comfoair_poll_step_t` | `send` + `pdo`: send one request, then the caller's frame gap. Otherwise `wait_ms`: sleep at most that long (a request may end it early). |
| `comfoair_poll_plan_init(plan, pdos, count, interval_ms, now_ms)` | A sweep is due at `now_ms`; nothing else pending. |
| `comfoair_poll_plan_request_sweep(plan)` | One extra sweep as soon as possible. Repeated calls before it starts give one; a call during a sweep gives one more after it. |
| `comfoair_poll_plan_request_readback(plan, pdo, now_ms, delay_ms)` | One request for `pdo` at `now_ms + delay_ms`. Same PDO pending → coalesces, due `delay_ms` after this call. A different PDO pending → `false`, nothing changes. |
| `comfoair_poll_plan_next(plan, now_ms)` | A due read-back, else the running sweep's next PDO, else a due or requested sweep (which starts now and sets the next one at `now_ms + interval_ms`), else a wait until the earlier of the next sweep and the pending read-back. |

## Implementation

`comfoair_poll_plan.c`, one file. Times are `uint32_t` milliseconds compared by signed difference
(`until()`), correct across the 49-day wrap for spans under 2^31 ms. A sweep in progress is
`sweep_index < count`; an empty request list starts and ends a sweep at once and only waits.

DO NOT:
- Keep a read-back's earlier deadline when it is re-armed. A request that lands between two
  commands shows the state after the first, and nothing asks again after the second.
- Send two requests in one step, or put the read-back "beside" a sweep frame. One request per
  step is what lets the caller keep the 100 ms gap after every one.
- Count the next sweep from the sweep's END: the period would grow by the sweep's length.

## Dependencies

None. Used by `comfoair_poll` only.

## Tests

`test/test_poll_plan/test_poll_plan.cpp`: the boot sweep in order, the period from the sweep's
start, collapsed requested sweeps, a sweep requested during a sweep, a read-back's delay and its
single send, coalescing two commands into one request after the second, refusal of a second PDO,
a read-back slotting between two sweep frames, the wait being the earlier deadline, and the
millisecond wrap.
