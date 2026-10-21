# comfoair_unit_cadence — when the probe task asks the ventilation unit again

> This is documentation, not code — re-read the source before relying on a detail.

## Purpose

The probe task reads the ventilation unit over RMI in two sizes: the **full** unit read (identity,
active errors, the three settings) and the **active errors alone** (`80 03 01`, GETACTIVEERRORS).
This component is the one schedule that says which of the two is due and how long to sleep until
the next. It is pure — ticks in, a verdict out — so the wrap-safe arithmetic is host-tested; the
probe task is FreeRTOS-bound and `[env:native]` never compiles it.

## Responsibility

Owns:

- The four periods: first read at boot + 10 s, the full read every 6 h after a complete one and
  60 s after an incomplete one, the errors alone every 60 s between full reads.
- Which read wins when both are due (the full one) and the ticks until the nearer deadline.
- Seconds → ticks without overflow, and every comparison on an unsigned difference.

Does NOT do:

- Read the clock, send a frame, take a lock or sleep — `comfoair_probe_task`
  (`comfoair_probe_task_unit.cpp`) passes `xTaskGetTickCount()` and `configTICK_RATE_HZ` in and
  performs the read the verdict names.
- Decide what an answer means — `comfoair_sysinfo` names and classifies the error ids,
  `comfoair_state` holds the two entities.

## Public API

Header: `include/comfoair_unit_cadence.h` (C). One owner, the probe task: not thread-safe. No call
fails; a NULL cadence is a no-op.

| Symbol | Contract |
| --- | --- |
| `comfoair_unit_cadence_t` | `full_from/full_after`, `errors_from/errors_after`: each deadline as a start tick and a length in ticks. Plain data, owned by the caller. |
| `comfoair_unit_cadence_arm(c, now, tick_hz)` | Both deadlines `COMFOAIR_UNIT_FIRST_S` (10 s) from `now`. |
| `comfoair_unit_cadence_due(c, now, tick_hz, *wait)` | `COMFOAIR_UNIT_READ_FULL` if the full read is due (it wins), else `_ERRORS` if the errors read is, else `_NONE`. `*wait` (may be NULL), always in ticks: 0 when due, else the ticks to the nearer deadline. NULL `c`: `_NONE`, `*wait` = one `COMFOAIR_UNIT_ERRORS_S` period at `tick_hz` (the only use of `tick_hz` here). |
| `comfoair_unit_cadence_full_done(c, now, tick_hz, complete)` | Next full read in `COMFOAIR_UNIT_OK_S` (6 h) or `COMFOAIR_UNIT_RETRY_S` (60 s); the errors clock restarts at 60 s too. |
| `comfoair_unit_cadence_errors_done(c, now, tick_hz)` | Next errors read in `COMFOAIR_UNIT_ERRORS_S` (60 s), answered or not. |

## Implementation

`comfoair_unit_cadence.c`, about 70 lines. `ticks()` multiplies in 64 bits before narrowing, so
6 h at 1000 Hz (21 600 000 ticks) is exact. `remaining()` works on `now - from` against `after`,
never on an absolute deadline, so a tick counter that wraps between arming and checking still
falls due on time. `_due()` folds a due errors read into a due full read, because the full read
reads the errors too.

## Tests

`test/test_unit_cadence/test_unit_cadence.cpp` (host, `pio test -e native -f test_unit_cadence`):
nothing due before 10 s and the exact wait; the first read is a full one; after a complete read
the errors come every minute with the right wait; 359 errors reads and then the full read at 6 h;
an incomplete read retries in a minute and the full read wins over the errors read; 6 h at
1000 Hz without overflow; a wrapping tick counter; NULL arguments, with a NULL cadence's wait in
ticks at two tick rates.

## Notes

- **Why every minute.** One `80 03 01` request (1 frame) and a 32-byte answer (about 5 frames) a
  minute is about 0.1 frame/s, against the ~1.5 PDO frames/s the unit broadcasts anyway — roughly
  20 ms of a 50 kbit/s bus per minute. In exchange Home Assistant hears of a unit fault within a
  minute instead of within six hours. It is read-only, so unlike the settings read it cannot race
  a command and needs no command-path guard.
- **What "complete" means is the caller's.** `comfoair_unit_cadence_full_done()` takes a bool;
  the probe task passes true only when the model answered, the three settings have a final answer
  and the unit's firmware word is known. The error entities are published only once the firmware
  word is known, so a firmware read that timed out at boot is retried in a minute rather than
  leaving both entities unknown for six hours.
- **Why the full read wins.** It reads the errors as well; running both back to back would ask
  the unit the same question twice.
- **Why one schedule with two deadlines, not a second timer.** The probe task's rule is that a
  second timer would be a second answer to "when is the unit asked". Here both answers come from
  one `_due()` call, so the two reads can never interleave on their own.
- **Why an unanswered errors read is not retried sooner.** The next minute is the retry; the last
  answered value is kept meanwhile (a single RMI timeout on a live bus must not flap a problem
  sensor), and a dead bus is the `bus` availability topic's job.
