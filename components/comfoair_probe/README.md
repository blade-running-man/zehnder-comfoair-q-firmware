# comfoair_probe — the pure, read-only RMI walk of the ventilation unit and the report it fills

> This is documentation, not code — re-read the source before relying on a detail.

The firmware needs to learn what a particular ComfoAir Q actually exposes over RMI. That means
walking node → unit → subunit → property, including units nobody has documented. This component
is the walk as a pure state machine: it hands out one abstract read request at a time, is told
what came back, and fills a fixed-size, non-allocating report. It has no bus, no clock and no
task, so every decision it makes can be pinned on the host against a fake unit: when a unit is
absent, when the subunits have run out, when to retry, when to give up. The device half that puts
requests on the CAN bus is `comfoair_probe_task`, and it decides nothing.

## Responsibility

Owns:

- The walk order and every stopping rule: unit discovery, subunit/property iteration,
  VALUE → RANGE → STEP follow-ups, the retry of a timed-out request, run limits, the
  `NO_ANSWER` abort, the one GETACTIVEERRORS read and the identity census of other nodes.
- The report (`comfoair_probe_results_t`): unit rows, property entries, progress counters, the
  active-errors bitfield and the `truncated` flag.
- The guarantee that the walk can only ever describe a read (GET ONE of type 0x10/0x20/0x40, or
  GETACTIVEERRORS `80 03 01`).

Does NOT do:

- Build CAN/RMI bytes, transmit, reassemble replies, time out, or lock — `comfoair_probe_task`
  (through `rmi_read.h` in `comfoair_protocol`, whose builders cannot produce a write).
- Pick the plan's target node or extra nodes from a bus census — `comfoair_probe_task`
  (`comfoair_probe_task.cpp`, `start`).
- Decode values, name units/errors, redact secrets, render JSON — `comfoair_probe_report`.
- Serve anything over HTTP — `comfoair_http` (`comfoair_http_probe.c`) via `comfoair_probe_task`.

## Public API

Header: `include/comfoair_probe.h` (C linkage). No function returns `esp_err_t`; nothing here
can fail beyond what the report records.

| Symbol | Contract |
| --- | --- |
| `comfoair_probe_begin(const comfoair_probe_plan_t *plan)` | Wipes the report and starts a walk. `NULL` = `COMFOAIR_PROBE_DEFAULT_PLAN()`. `extra_node_count` is clamped to `COMFOAIR_PROBE_MAX_EXTRA_NODES`. State becomes `RUNNING` — always, even for `max_unit 0`, because the error read still runs. Caller holds its lock. |
| `bool comfoair_probe_next(comfoair_probe_request_t *out)` | The request to put on the bus, or `false` when the walk is over (`DONE`/`FAILED`/never started). Idempotent until an outcome is recorded: calling twice returns the same request and counts it once in `progress.requests`. After a TIMEOUT with retries left the SAME request comes back — the caller must not retry on its own. `NULL` `out` returns `false` and counts nothing. |
| `void comfoair_probe_record(const comfoair_probe_outcome_t *outcome)` | The answer to the outstanding request. Ignored when nothing is outstanding (a late duplicate cannot advance the walk twice), when `outcome` is `NULL`, or when `kind` is `NONE` or out of range (the request stays outstanding). `outcome->data` is borrowed for the call only; bytes are copied. |
| `const comfoair_probe_results_t *comfoair_probe_results(void)` | The live singleton report. Valid until the next `begin()`. Read under the caller's lock. Before any `begin()` it is `IDLE` and already carries the default plan (so an idle report names node 0x01, not 0). |

Key types and constants:

- `comfoair_probe_plan_t` / `COMFOAIR_PROBE_DEFAULT_PLAN()`: `target_node 0x01` (the ventilation
  unit), `max_unit 0x2F`, `max_subunit 16`, `max_property 0x3F`, `retries 1`,
  `abort_after_silent 5` (0 disables the abort), `extra_nodes[8]` + `extra_node_count`.
- `comfoair_probe_request_t`: `kind` (`REQ_GET` or `REQ_ACTIVE_ERRORS`), `node`, `unit`,
  `subunit`, `type`, `property`. There is deliberately no field that could carry a command byte —
  a command >= 0x80 on unit 0x01 can brick the unit.
- `comfoair_probe_outcome_t`: `kind` (`DATA` / `ERROR` / `TIMEOUT`; never `NONE`), `error`,
  `data`, `len`. A TIMEOUT means "nothing came back for this one attempt"; the walk owns retries.
- `COMFOAIR_PROBE_TYPE_VALUE 0x10`, `_RANGE 0x20`, `_STEP 0x40`: the RMI GET ONE type byte.
- RMI error codes the walk acts on: `11` unknown command, `12` unknown unit, `13` unknown subunit,
  `14` unknown property, `15` no range, `32` not gettable. Any other code is stored as-is.
- Table sizes: `COMFOAIR_PROBE_MAX_ENTRIES 400` (~110 properties are documented on node 0x01, the
  walk covers undocumented units too, so generous), `COMFOAIR_PROBE_MAX_UNITS 64` (0x2F units on
  the target plus up to 8 identity rows), `COMFOAIR_PROBE_VALUE_MAX 40` (longest documented
  value is the 32-byte model string incl. NUL), `COMFOAIR_PROBE_AUX_MAX 8` (min+max of a UINT32),
  `COMFOAIR_PROBE_ERRORS_MAX 32` (GETACTIVEERRORS bitfield). A value longer than its field keeps
  the prefix and its true `len` (clamped to 255), so `len > stored bytes` means truncated.
- The report is static (`.bss`), not allocated: `sizeof(comfoair_probe_results_t)` measured
  34 472 B on a 64-bit host (the header comment's "~28 KB" undercounts the 4-byte enums).

What gets an entry: a property that answered DATA, an error other than 12/13/14, or timed out.
A plain 14 is only counted in its unit row's `absent_properties`. `range` is filled only when the
value was DATA; `step` only when the range was DATA. Unit rows: `PRESENT` (with `subunits` =
how many answered), `ABSENT` (answered 12) or `TIMEOUT` (the discovery read went unanswered).

## Implementation

- `include/comfoair_probe.h` — the contract above.
- `comfoair_probe_scan.cpp` — the cursor and every rule of the walk (`begin`/`next`/`record`).
- `comfoair_probe_store.h` — PRIVATE: the report's writers, namespace `comfoair_probe_store`.
- `comfoair_probe_store.cpp` — `g_results`, the full-table sinks, the copy rule, and
  `comfoair_probe_results()`.
- `CMakeLists.txt` — no `REQUIRES` at all, on purpose (see Dependencies).

Flow. The cursor (`g_phase`, `g_node`/`g_unit`/`g_subunit`/`g_property`, `g_ask`) IS the
outstanding request: `current_request()` renders it, `record()` moves it. Phases run
`UNITS` → `ERRORS` → `EXTRA` → `END`:

1. `UNITS`: units 1..`max_unit` on `target_node`. The discovery read is subunit 1 property 1.
   Timeout → `TIMEOUT` row; 12 → `ABSENT` row; 13 → `PRESENT` row with no subunits; anything else
   → `PRESENT` row, then the answer is handled as an ordinary property. Each DATA value is followed
   by a RANGE read, and a DATA range by a STEP read.
2. `ERRORS`: one GETACTIVEERRORS to the target.
3. `EXTRA`: each `extra_nodes` entry gets identity reads only — unit 0x01, subunit 1, properties
   1..`0x14`, VALUE only (no range/step). Skipped: 0 (empty slot), the target, our own node
   `0x2A`, and duplicates.
4. `END`: `DONE`, or `FAILED` + `FAILURE_NO_ANSWER` if the abort fired.

`begin()` positions the cursor on "unit 0" and calls `end_unit()`, so the first unit (or, for
`max_unit 0`, the error read) is reached by the same rule every later unit uses.

Stopping rules inside a unit (all pinned by request count in `test_probe_scan_bounds`):

- 13 mid-subunit ends that subunit; 12 or 13 on a later subunit's first property ends the unit.
- A later subunit whose first property times out: entry kept, unit left, not counted in
  `subunits`.
- `SILENT_RUN_LIMIT 3` consecutive final VALUE timeouts end the subunit (else a dead subunit costs
  63 × (1 + retries) × 300 ms ≈ 40 s).
- `UNKNOWN_COMMAND_RUN_LIMIT 8` consecutive 11s end the subunit (an 11 is an answer and costs one
  request, so a longer run than the silence limit is cheap).
- A subunit s > 1 that produced no DATA ends the unit's subunits; if it answered only 14 it is
  also uncounted from `subunits`. Subunit 1 is exempt.
- RANGE/STEP timeouts never feed the silent run: the value just answered, so the subunit is alive.
- A 12 after the discovery read said PRESENT is kept as an entry (a contradiction worth reporting)
  and ends the subunit.

Retries and abort (`record()`): a TIMEOUT with `g_retries_left > 0` decrements it and leaves the
cursor untouched; any final answer resets it to `plan.retries`. `progress.timeouts` counts every
individual silence, `progress.requests` every handout including retries. Until the first
non-timeout answer, final timeouts accumulate in `g_silent_finals`; reaching
`abort_after_silent` fails the walk. Checked after the answer is applied, so the report shows the
rows that were asked.

Storage (`comfoair_probe_store.cpp`): `take()` returns the next slot or, once the table is full,
a zeroed scratch sink and sets `truncated`. `keep()` copies the prefix that fits and the received
`len`; a TIMEOUT stores only the kind.

Concurrency: none. The state is a module singleton and nothing locks; the caller serialises every
call, including reads of `comfoair_probe_results()`. `comfoair_probe_task` holds one mutex
around all of them and never across a bus round trip.

DO NOT:

- Add a `REQUIRES` on `comfoair_can`, `comfoair_protocol`'s builders, or anything that can
  transmit. Once this component can send, "what goes on the wire" stops being a value a host test
  can inspect.
- Add a command/opcode field to `comfoair_probe_request_t`, or a request kind other than a read.
- Branch on "is there room" instead of writing to the sink: the walk's shape and request count
  must not depend on `COMFOAIR_PROBE_MAX_ENTRIES`.
- Initialise `g_results` with a non-zero initialiser to get the IDLE default plan: it moves the
  whole ~34 KB struct from `.bss` into `.data`, i.e. into the image. The static `DefaultPlan`
  object does it instead (same translation unit, declared after `g_results`, so ordered).
- Fix the IDLE node in the renderer instead: "IDLE carries the default plan" is a fact about the
  results, read by more than the JSON.
- Use `plan.max_property` for identity reads: a plan narrowed for a quick look must not blind the
  census; `IDENTITY_LAST_PROPERTY 0x14` is fixed.
- Reset `g_silent_finals`, or keep counting silences, after the first answer: a unit that answered
  once does answer RMI from this node, and a later silent stretch is information, not failure.
- Weaken the "no DATA in a later subunit ends the unit" rule to "only-14 ends it": 11 counts as
  "anything but 14", so a unit answering 11 to everything would cost 8 × 16 = 128 requests per unit
  instead of 16.
- Retry in the caller: the walk already re-hands the same request; a caller retry doubles it.

## Dependencies

Requires: nothing (no `REQUIRES` / `PRIV_REQUIRES`; C++ standard library only).

Depended on by:

- `comfoair_probe_task` (`REQUIRES comfoair_probe`) — the only runtime driver: builds the plan
  from the bus census, `begin`/`next`/`record` under its mutex, reads `results()` for the summary,
  the identity hand-off (`comfoair_probe_task_unit.cpp`) and rendering.
- `comfoair_probe_report` (`REQUIRES comfoair_probe`) — decodes and renders
  `comfoair_probe_results_t`; uses only the types.
- `[env:native]` in `platformio.ini` puts `components/comfoair_probe/include` on the host path.

## Tests

- `test/test_probe_scan/` — the walk against a table fake unit; asserts the request log (the
  walk's shape on the wire) and the report: full default walk, range/step placement, only-reads
  and one error read, identity-only reads of other nodes, retry re-handout, stray/empty outcomes,
  14 on the discovery read, long-value truncation, `begin()` wiping and the NULL default plan.
- `test/test_probe_scan_bounds/` — every stopping rule against a rule-based fake, with exact
  request counts: the 5-silence abort and no abort once heard, silent runs, silent ranges, 12/13
  handling, the 14-only and no-DATA subunit rules, the 11 run limit, and a full report that keeps
  walking and sets `truncated`. Owns its fake; do not share it with `test_probe_scan`.
- `test/test_probe_report_live/` — mainly `comfoair_probe_report`, but its first test reads
  `comfoair_probe_results()` before any `begin()` and pins the IDLE default-plan node (so it must
  stay first).
- `test/test_probe_report/` builds hand-made results structs and does not drive the walk.

Not covered here: the wire encoding and timing (`comfoair_probe_task`, no host suite; the byte
builders are pinned by `test_rmi_read`), and behaviour against a real unit
(`docs/hardware-verification/`).

## Notes

- `OWN_NODE 0x2A` in `comfoair_probe_scan.cpp` duplicates `COMFOAIR_CONTROL_LOCAL_NODE` from
  `comfoair_control.h` because this component may not depend on anything. Asking ourselves can
  only time out, and two such silences would push a walk that has heard nothing toward
  `NO_ANSWER` for a reason unrelated to the unit. `comfoair_probe_task` also filters it from the
  plan; keep the two values equal.
- `FAILURE_NO_ANSWER` is not a statement about the unit's tree: it means the unit does not answer
  RMI from this node (for example, it answers only its own gateway's node id), so every further
  request would also time out.
- A plan bound of 0 for `max_subunit`/`max_property` is treated as 1: the discovery read is
  subunit 1 property 1 by definition, and there is no walk of a unit that asks nothing.
- A silent unit gets a `TIMEOUT` row and no entry: the row already says it did not answer.
- The protocol background (RMI frame layout, error codes, the bricking warning) is in
  `docs/reference/comfoair-pdo-rmi.md`; the probe's as-built description in
  `docs/protocol/probe.md`.
