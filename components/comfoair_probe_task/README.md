# comfoair_probe_task — the device half of the RMI probe: task, reply queue, censuses, unit read

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_probe` is a pure, host-tested state machine that decides *what* to read from a
ComfoAir node over RMI ("walk" its units/subunits/properties) and when to give up. Something has
to put those requests on the CAN bus, wait for the answers and hand them back, without ever
stalling the CAN receive path or racing the HTTP task that renders the report. This component is
that impure glue: one FreeRTOS task, one mutex, one frame queue, a node census filled from every
received frame, and a periodic read of the ventilation unit's identity and active errors for
`GET /api/system` — which also reads back the three selects no PDO reports (`temperature_passive`,
`humidity_comfort`, `humidity_protection`) into the state model — and, every minute between two of
those, the active errors alone, which feed the `unit_error` (problem) and `active_errors` (text)
entities. It is the **only reader of RMI responses** in the firmware, and those two are the only
places RMI feeds an entity.

## Responsibility

Owns:
- The probe task (`comfoair_probe`, priority 3, 4096 B stack) that runs a walk on request and the
  two unit reads (full, errors-only) on a schedule — never two of them at once.
- The single mutex (`s_lock`) serialising every `comfoair_probe_*` call and the unit snapshot.
- The reply queue and the "listening" flag: RMI responses addressed to `COMFOAIR_CONTROL_LOCAL_NODE`
  are queued only while a walk or a unit read is in flight.
- The node census: a 64-bit set of every node id heard on the bus since boot (RMI, PDO and the
  undocumented `0x10000000 | node` heartbeat).
- The one PDO census instance (`comfoair_pdo_census_t`, ~12 KB, static): fed with every received
  PDO frame, rendered into `GET /api/probe`'s `"pdos"` section. What a record holds and the
  256-row ceiling are `comfoair_pdo_census`'s; the clock, the lock and the `known` flag are here.
- Turning an abstract `comfoair_probe_request_t` into bytes **only** through `rmi_read.h`'s
  `get_one()` / `get_active_errors()`, transmitting the chunks, and matching the reply by source,
  destination and sequence.
- The `comfoair_sysinfo_unit_t` snapshot (model, serial, firmware, article, country, name, active
  error bitfield, read states, age) and carrying out its schedule (`comfoair_unit_cadence` decides).
- Writing the two unit-error entities (`comfoair_state_set_unit_errors()`) from every ANSWERED
  errors read, full or errors-only, with the line and the verdict `comfoair_sysinfo` computes.
- The settings read-back (`comfoair_probe_task_settings.cpp`): one GET ONE per
  `COMFOAIR_RMI_READBACK` row, decoded by `comfoair_readback`, applied with
  `comfoair_state_set_option()` behind the command-path guard, and the observer that tells the
  publishers.

Does NOT do:
- Decide which request comes next, retries, absence of a unit, or failure — `comfoair_probe`.
- Render the report JSON — `comfoair_probe_report` (`comfoair_probe_render()`); this component
  only calls it under the lock with the meta (elapsed time, counter, node census, PDO census
  summary and row callback).
- Decide what the PDO census records or ignores — `comfoair_pdo_census` (pure, host-tested in
  `test/test_pdo_census`).
- HTTP status codes, body parsing, auth — `comfoair_http` (`comfoair_http_probe.c`,
  `comfoair_http_system.c`).
- Own the RMI sequence counter or the node ids — `comfoair_control`
  (`comfoair_control_next_sequence()`, `COMFOAIR_CONTROL_LOCAL_NODE` = 0x2A,
  `COMFOAIR_CONTROL_UNIT_NODE` = 0x01).
- Drive the CAN controller — `comfoair_can` (`comfoair_can_transmit()`); frames arrive via
  `src/main.cpp`'s receive callback.
- Write anything to the unit. Ever. (The read-back writes the STATE MODEL, never the unit.)
- Decide what a read-back reply means or whether it may be applied — `comfoair_readback`.
- Decide when the unit is read (`comfoair_unit_cadence`), which error ids raise `unit_error` or how
  the error line reads (`comfoair_sysinfo_errors_any()` / `_errors_summary()`, by the unit's
  firmware numbering: informational ids such as a manual bypass are listed but raise no problem).

## Public API

Header: `include/comfoair_probe_task.h` (C linkage).

| Symbol | Contract |
| --- | --- |
| `void comfoair_probe_task_set_observer(void (*)(void))` | Set before `init` (the task reads the pointer unlocked). Called on the probe task when the settings read-back changed the state model (at most once per read-back) or the two error entities changed; must not block. `src/main.cpp` wires it to `comfoair_http_notify_state_changed()` + `comfoair_mqtt_notify_state_changed()` — an observer because `comfoair_http` depends on this component, so a direct call would be a cycle. NULL tells nobody. |
| `esp_err_t comfoair_probe_task_init(void)` | Once, from `app_main`, after `comfoair_can_start()` and `comfoair_control_start()` succeeded. Creates the mutex, the reply queue and the idle task. `ESP_ERR_INVALID_STATE` on a second call (task exists); `ESP_ERR_NO_MEM` if the mutex, queue or task cannot be made. A failure is non-fatal: `main.cpp` logs it and the rest of the device runs. |
| `comfoair_probe_start_t` | `{ uint8_t node; uint8_t another_counter; }`. Defaults chosen by the HTTP layer: node 1, counter 0. `another_counter` goes into CAN-id bits 12-13. |
| `esp_err_t comfoair_probe_task_start(const comfoair_probe_start_t *)` | HTTP task. Begins a walk under the lock and notifies the task; returns at once. `ESP_ERR_INVALID_ARG` for NULL, node outside 1..63, node == 0x2A, or `another_counter > 3`; `ESP_ERR_NOT_FOUND` if init never created the task (HTTP maps to 503); `ESP_ERR_INVALID_STATE` if a walk is running (HTTP maps to 409). Seeds `plan.extra_nodes` (max `COMFOAIR_PROBE_MAX_EXTRA_NODES` = 8) from the census, skipping node 0, the target and ourselves. |
| `void comfoair_probe_task_on_frame(uint32_t can_id, const uint8_t *data, size_t len)` | CAN receive task (priority 10), for **every** extended non-RTR frame, before PDO/RMI dispatch. Never blocks: census under a spinlock, atomic flag, `xQueueSend(..., 0)`. A full queue drops the frame (the walk sees a timeout). Needs no init. Frames with `len > 8` or NULL data are not queued. |
| `esp_err_t comfoair_probe_task_render(comfoair_probe_write_fn, void *ctx)` | HTTP task. Streams the report under the lock for the whole render. `ESP_ERR_INVALID_STATE` before init (no mutex), `ESP_ERR_INVALID_ARG` for NULL `write`, `ESP_FAIL` if `write` returned false. Elapsed time is live while running, frozen after. Once initialised, every render carries the `"pdos"` section; the census itself records from boot, before init. |
| `esp_err_t comfoair_probe_task_unit(comfoair_sysinfo_unit_t *out)` | Any task; copies the unit snapshot under the lock and fills `identity_age_ms`. Before init: `*out` zeroed (states `UNKNOWN`), `ESP_ERR_INVALID_STATE`. `ESP_ERR_INVALID_ARG` for NULL. `comfoair_http_system.c` ignores the return (`(void)`) and relies on the zeroed struct. |
| `size_t comfoair_probe_task_nodes(uint8_t out[64])` | Any task, never blocks, needs no init. Every node heard since boot, ascending; returns the count. Lets `GET /api/system` list nodes even when the probe task failed to start. |

Constants (all file-local):

| Name | Value | Why |
| --- | --- | --- |
| `TASK_STACK` | 4096 | `comfoair_ctrl` manages in 3072 doing similar work; the reassembler and outcome add more, and an overflow would show only minutes into a walk. The walk's summary log prints the high-water mark. |
| `TASK_PRIORITY` | 3 | Below `can_rx` (10), `can_bus` (9), poll/httpd (5) and `comfoair_ctrl` (4): an owner's command never waits behind a diagnostic read. Starvation cannot fake a timeout (see `wait_reply`). |
| `PROBE_GAP_MS` (internal header) | 20 | Pause between requests so ~3200 reads leave the bus room for PDOs. At 100 Hz it is two ticks; not a timing contract. |
| `REPLY_WINDOW_MS` | 300 | Longest documented reply (GETACTIVEERRORS, 32 B) is ~5 frames x ~2.5 ms at 50 kbit/s; 300 ms is generous slack. |
| `TX_TIMEOUT_MS` | 200 | Same driver patience per chunk as `comfoair_control`. |
| `QUEUE_LEN` | 16 frames | One request in flight, only our replies queued: three longest replies fit. Overflow = one retry, never a blocked receive task. |

The schedule is `comfoair_unit_cadence`'s (`COMFOAIR_UNIT_*`, seconds, converted to ticks in 64 bit
there because `pdMS_TO_TICKS(6 h)` overflows 32 bits above ~199 Hz); this component only carries it
out, in `TickType_t` (asserted 32-bit, which the cadence's wrap-safe arithmetic assumes):

| Read | When | Why |
| --- | --- | --- |
| Full (identity, errors, settings) | 10 s after boot; then 6 h after a complete read, 60 s after an incomplete one | Complete = the model answered AND the settings read-back is final. Ten requests, ~0.2 frame/s even at the retry rate. |
| Errors only (`80 03 01`) | 60 s after the last read of either kind | Home Assistant hears of a unit fault within a minute, not six hours: one request and a ~5-frame answer a minute. A full read restarts this clock (it read the errors); when both are due the full one runs. An unanswered errors read is retried the next minute, not sooner. |

## Implementation

Files:
- `comfoair_probe_task.cpp` — the task loop, the mutex, `init`/`start`/`render`/`unit`, the walk
  loop and its summary log; also carries the component's contract notes in its header comment.
- `comfoair_probe_task_bus.cpp` — node census, PDO census and its two accessors
  (`probe_pdos_snapshot`, `probe_pdos_row`), reply queue, listening flag, `on_frame`, the transmit
  (`probe_bus_ask`) and the reply wait (`wait_reply`).
- `comfoair_probe_task_unit.cpp` — the full unit read (six GET ONE reads of NODE unit 0x01/sub 0x01
  properties 0x04 serial, 0x06 firmware, 0x08 model, 0x0B article, 0x0D country, 0x14 name, then
  GETACTIVEERRORS), the errors-only read, the schedule they run on (`probe_unit_run_due()`), the
  two error entities (`publish_errors()`), and refreshing the snapshot from a finished walk.
- `comfoair_probe_task_settings.cpp` — the settings read-back (`probe_settings_read()`) and the
  observer (`comfoair_probe_task_set_observer()`, `probe_notify_state_changed()`).
- `comfoair_probe_task_internal.h` — private seam between the files (`probe_lock/unlock`,
  `probe_bus_*`, `probe_pdos_*`, `probe_unit_*`, `probe_settings_read`, `PROBE_GAP_MS`).
- `CMakeLists.txt` — `REQUIRES comfoair_probe comfoair_probe_report comfoair_sysinfo
  comfoair_protocol comfoair_control comfoair_can comfoair_readback comfoair_state comfoair_lock
  comfoair_registry comfoair_pdo_census comfoair_unit_cadence esp_timer` (no `PRIV_REQUIRES`). None
  of the read-back four depends on anything probe-side; `comfoair_pdo_census` and
  `comfoair_unit_cadence` require nothing; `esp_timer` is the census clock and is not an IDF common
  requirement.

Lifecycle:
1. `init` creates mutex, queue, task. The task arms the schedule (both reads 10 s out; the full one
   wins), then loops on `ulTaskNotifyTake(pdTRUE, probe_unit_wait_ticks())`: a notification means
   `start()` → `walk()`; a timeout means a unit read is due → `probe_unit_run_due()`, which runs
   the full read or the errors-only read (step 9), whichever `comfoair_unit_cadence_due()` names.
2. `start()` (HTTP task) validates, snapshots the census, and under the lock calls
   `comfoair_probe_begin()`, records counter/start tick, turns listening on, then notifies.
3. `walk()`: lock → `comfoair_probe_next()` → unlock → `probe_bus_ask()` (outside the lock, up to
   ~300 ms) → lock → `comfoair_probe_record()` + `end_walk_if_over()` → unlock → 20 ms gap. On the
   hold where `next()` returns false it summarises, then logs one summary line.
4. `end_walk_if_over()` runs in the same hold as the `record()` that ended the walk: freezes
   `s_elapsed_ms`, turns listening off and, if DONE, refreshes the unit snapshot from the walk.
5. `probe_bus_ask()`: build the frame via `rmi_read.h`, take a fresh sequence from
   `comfoair_control_next_sequence()`, drain stale queue entries, transmit every chunk (errors only
   logged at debug), then `wait_reply()`; classify DATA vs ERROR and hand back **all** reply bytes.
6. `probe_unit_read()` (the full read): works on a static copy of the snapshot, turns listening on
   under the lock, does the seven reads with `another_counter` 0, then the settings read-back
   (step 7), then under
   the lock publishes the copy, sets the next schedule (`comfoair_unit_cadence_full_done()`: 6 h
   only if the model answered AND the settings read-back is complete, else 60 s, and the errors
   clock restarts; `identity_state = NO_ANSWER` whenever the model did not answer), and turns
   listening off unless a walk started meanwhile. Then, outside the lock, the error entities
   (step 10).
7. `probe_settings_read()`: samples `comfoair_control_get_stats()`; if a command is pending
   (`queued != sent + failed`) it asks nothing and returns not-done. Otherwise, per row, a 20 ms
   gap and a GET ONE. DATA is decoded (malformed: WARN, final, state untouched) and applied under
   `comfoair_lock()` only if `comfoair_readback_may_apply()` holds on a second stats sample taken in
   that same hold; a refusal stops the loop, not-done. ERROR is final (WARN once per row per boot).
   TIMEOUT is not-done. If any value changed, the observer is called once. Done = every row final.
   A changed value logs at INFO (`setting key=label`), a confirmation at DEBUG.
8. A DONE walk of the unit refreshes the identity and never touches the due time. The timed cycle
   also carries the settings read, whose "every 6 h" a walk must not stretch, and the walk does not
   apply the settings itself — its reads have no command-path guard around them. Nor does it write
   the error entities: it runs under the probe lock, and `comfoair_lock()` is never taken inside it;
   the next minute's errors read catches up (`/api/system` is current at once).
9. The errors-only read (`probe_errors_read()`): listening on under the lock, one GETACTIVEERRORS
   with `another_counter` 0 (its sequence from `comfoair_control_next_sequence()` like every other
   request), then under the lock the answer goes straight into the snapshot (`take_errors()`: DATA
   → bitfield + `READ_OK`; anything else → `READ_NO_ANSWER`, the old bitfield kept), the errors
   clock restarts, and listening goes off unless a walk started meanwhile. The first unanswered one
   in a row logs WARN, repeats DEBUG, a recovery INFO. A 20 ms gap follows, so a walk started
   meanwhile does not send back to back.
10. The error entities (`publish_errors()`, after either read, outside the probe lock): only when
    the errors were ANSWERED — an unanswered read keeps both entities as they were, so one RMI
    timeout on a live bus does not flap the problem sensor (a dead bus greys them out through
    `<prefix>/bus`). `comfoair_sysinfo_errors_summary()` makes the line,
    `comfoair_sysinfo_errors_any()` the verdict, both with the unit's firmware (until it is known,
    no id counts as informational); `comfoair_state_set_unit_errors()` stores them under
    `comfoair_lock()`. Only a change logs (INFO) and calls the observer. Before the first answered
    read both entities are `None` (unknown).

Concurrency:
- Mutex `s_lock` (FreeRTOS mutex, `portMAX_DELAY`): every `comfoair_probe_*` call, the walk's
  bookkeeping (`s_another_counter`, `s_start_tick`, `s_elapsed_ms`, `s_ended`) and writes of the
  unit snapshot. `comfoair_probe` itself does not lock.
- Census: a `portMUX_TYPE` spinlock around one `uint64_t`, because the CAN receive task writes it
  on every frame and must never wait on a mutex the HTTP task holds.
- PDO census: its own `portMUX_TYPE` (`s_pdos_mux`) around the whole `comfoair_pdo_census_t`.
  - Writer: `on_frame`'s PDO branch reads `esp_timer_get_time() / 1000` (as `u32` ms) OUTSIDE the
    lock, then calls `comfoair_pdo_census_record()` inside it — O(1) amortised, bounded by 256
    index probes, no log, no allocation, no wait.
  - Reader (render, under the probe mutex): `probe_pdos_snapshot()` reads `count`, `frames` and
    `distinct` under the same spinlock, so the exit/enter pair is the RISC-V memory barrier that
    publishes the `pdo` fields of records `[0, count)`. It then sorts OUTSIDE the lock with
    `comfoair_pdo_census_order()`, which is sound because records are append-only and their `pdo`
    never changes. `overflow = distinct - count`; `uptime_ms` is taken at the snapshot.
  - Rows: the renderer's callback copies record `order[i]` whole under the spinlock
    (`probe_pdos_row()`), since every other field changes per frame, then sets `known` by
    `std::binary_search` over `COMFOAIR_REQUEST_PDOS` (generated sorted ascending).
  - The order array is a static `uint16_t[256]` in `comfoair_probe_task.cpp`, safe because every
    render holds the probe mutex; only one row (~48 B) is on the HTTP task's stack.
  - Lock order: probe mutex, then the spinlock; nothing takes them the other way.
- The PDO census is one snapshot only in its row SET: rows heard after the snapshot are not in
  that render, but each row is copied when it is streamed, so on a slow client a row's `count` and
  `last_ms` can be newer than the summary's `frames` and `uptime_ms`. A debugging view; not worth
  copying 12 KB to make it exact.
- Listening flag: `std::atomic<bool>`.
- The unit snapshot is written only by the probe task; that task reads it unlocked, other tasks
  copy it under the lock.
- The read-back's apply holds `comfoair_lock()` (the recursive state lock) and, inside it, takes
  `comfoair_control`'s stats mutex for one copy. Nothing takes them in the other order. The probe
  lock is never held at the same time.
- The error entities: `comfoair_state_set_unit_errors()` takes `comfoair_lock()`, called with the
  probe lock released — the same rule. The walk's refresh, which runs under the probe lock, does
  not write them for that reason.

Invariants:
- Only `rmi::get_one()` (three read types) and `rmi::get_active_errors()` build frames, so this
  component cannot put a write or a `>= 0x80` command (other than GETACTIVEERRORS `80 03 01`) on the
  bus. If `get_one()` returns empty, the request is logged and recorded as a timeout so the walk
  moves on.
- The walk lock is never held across a bus round trip.
- The walk and both unit reads share one task, so they never share the reply queue at once.
- The error entities are written from an answered read only; nothing flips `unit_error` off for a
  read that went unanswered.
- Replies are matched by source, destination **and** sequence; an owner's command to the same unit
  shares source/destination and differs only in sequence.

DO NOT:
- Give `xQueueSend` in `on_frame` a timeout, or take any mutex there — it runs on the CAN receive
  task; waiting loses PDOs.
- Log, allocate or read the clock inside the PDO census's critical section, read `s_pdos.count`
  without `s_pdos_mux`, or sort the census under it (256 rows with interrupts masked).
- Move `end_walk_if_over()` to "when `next()` returns false" — `record()` sets DONE/FAILED, and a
  render landing in the following 20 ms gap would see DONE with `elapsed_ms` = 0; the page stops
  polling on DONE and the downloaded JSON keeps the 0.
- Drain the queue *after* transmitting — the reply can arrive while transmit still waits for the
  driver.
- Return immediately at the reply deadline — `wait_reply` does one zero-timeout receive past the
  deadline so a reply queued while this low-priority task was starved still counts.
- Turn listening off unconditionally at the end of either unit read — a `start()` during the
  read has already turned it on for its walk; switching it off turns that walk into timeouts and a
  `failed/no_answer` report.
- Swap the two branches of the task loop — a pending notification must win over a due read.
- Call `render()` from a task other than the HTTP task without revisiting `start()`: `start()`
  takes the lock unbounded and is "non-blocking" only because the other non-walk holders run on the
  same HTTP task.
- Replace the census spinlock with `std::atomic<uint64_t>` — the C6 has no lock-free 64-bit
  atomics; it would silently become a libatomic call with its own lock.
- Guard the read-back with a value compare-and-set, or take the second stats sample outside the
  `comfoair_lock()` hold that applies — see "Why not a value compare-and-set" below.
- Reschedule the unit read from a walk (6 h or 60 s): 6 h stretches the settings read past its
  promise, 60 s postpones a read that is already due; a redundant identity read costs 7 frames.
- Clear or flip the error entities on an unanswered errors read, or call
  `comfoair_state_set_unit_errors()` under the probe lock (it would nest the two locks).
- Classify error ids here, or call `comfoair_sysinfo_errors_any()` without the firmware: the id
  numbering depends on it (81 is the manual bypass at R1.4.0 and a preheater fault above it).
- Use `pdMS_TO_TICKS`/`pdTICKS_TO_MS` for the 6 h schedule or the identity age — both truncate to
  32-bit `TickType_t`; the age is computed in 64 bit and clamped to `UINT32_MAX`.

## Dependencies

Requires: `comfoair_readback` (decode, guard), `comfoair_state` (`comfoair_state_get/set_option`),
`comfoair_lock`, `comfoair_registry` (`COMFOAIR_RMI_READBACK`, `COMFOAIR_ENTITIES`),
`comfoair_probe` (walk logic, results), `comfoair_probe_report` (rendering, write fn
type), `comfoair_sysinfo` (`comfoair_sysinfo_unit_t`, `COMFOAIR_SYSINFO_TEXT_MAX` = 48),
`comfoair_protocol` (`rmi_read.h` builders/matching/classification, `rmi_response.h` reassembly,
`pdo_decode.h` for census node and PDO extraction), `comfoair_pdo_census` (the PDO census
store), `esp_timer` (its clock), `comfoair_control` (sequence counter, node ids),
`comfoair_can` (transmit).

Depended on by: `src/main.cpp` (`comfoair_probe_task_init()` after the command sender starts;
`comfoair_probe_task_on_frame()` from the CAN receive callback), `comfoair_http`
(`comfoair_http_probe.c`: `start`, `render` for `POST`/`GET /api/probe`; `comfoair_http_system.c`:
`nodes`, `unit` for `GET /api/system`). `src/main.cpp` also sets the observer before `init`.

## Tests

- No host suite compiles this component: no file under `test/` or `tests/` includes
  `comfoair_probe_task.h`, and `[env:native]` does not add its include path. It is impure glue
  (FreeRTOS task, queue, mutex, TWAI transmit).
- The decisions it carries out are host-tested elsewhere: `test/test_probe_scan` and
  `test/test_probe_scan_bounds` (the walk, against a fake unit, driven the same
  `while (next) record(answer)` way this task drives it), `test/test_probe_report` and
  `test/test_probe_report_live` (rendering), `test/test_rmi_read` and `test/test_rmi_response`
  (frame builders, matching, reassembly), `test/test_sysinfo`, `test/test_readback` (the
  read-back's decode and command-path guard, and the generated rows against the write builders),
  `test/test_unit_cadence` (the schedule), `test/test_unit_errors_summary` (the line and the
  verdict), `test/test_state_unit_errors` (the setter).
- Not covered on the host: the read-back's glue (the stats samples, the apply under the lock, the
  observer, the settings-driven 60 s retry), lock/queue interplay, `end_walk_if_over` timing, the start-during-read
  race, the errors-only read and its keep-on-no-answer rule, the identity age arithmetic, census extraction, the PDO census feed and its
  locking, reply matching by sequence in the presence of command traffic. The census's own rules
  are host-tested in `test/test_pdo_census`, its JSON in `test/test_probe_report*`.
- On hardware: `docs/hardware-verification/09-rmi-probe.md` (HV-PROBE-01..20) walks the probe
  end to end (start, 409 on a second start, cable pull mid-walk, no lost PDOs, stack high-water
  mark > 512 B in the summary line).

## Notes

- **Why the selects are read back at all.** No PDO reports them, so without a read the model holds
  only what this firmware last wrote — and nothing after a boot (the registry gives them no default
  any more: an invented factory value shown as fact was the bug).
- **Why not NVS.** Persisting the last write would only know what THIS firmware sent: wrong after a
  change on the unit's display, from ComfoConnect, after a unit reset, or once the ESP moves to
  another unit. The unit holds the setting, so the unit is asked.
- **Why not a value compare-and-set.** HTTP and MQTT call `comfoair_control_submit()` BEFORE their
  optimistic `comfoair_state_set_option()`, and the sender sleeps between frames with up to eight
  commands queued. A GET can reach the wire ahead of a queued SET and return the OLD value while
  the model still shows the old value too, so "unchanged" proves nothing and the read-back would
  overwrite the owner's write for 6 h. The guard is on the command path instead: ask only while the
  sender is idle, apply only if `queued` is unchanged, the second sample and the apply in one
  `comfoair_lock()` hold. A write submitted after that check blocks on the lock inside its own
  `set_option()` and lands on top — which is the order the unit saw too.
- **Why the settings share the identity read's schedule** rather than a timer of their own: one
  place decides when the unit is asked again. The errors-only read is that same schedule's second
  deadline, not a second timer. `identity_state`/`identity_age_ms` keep meaning "the
  model answered"; only the schedule needs both halves.

- **Why the census lives here.** It is filled from the same per-frame hook the reply queue needs,
  and the walk uses it to add other nodes' identity reads. `GET /api/system` also reads it, which
  is why it works before or without `init` — the static spinlock and bitset need no setup.
- **Why the walk refreshes the identity.** A DONE walk of node 0x01 already holds the six identity
  properties and the error bitfield; copying them in the same lock hold that ended the walk means
  `/api/system` never shows a unit older than the report just produced. Only a walk whose
  `target_node` is the unit counts — another node's walk may carry node 0x01 entries but its error
  bitfield is that other node's. The due time is left alone (see step 8).
- **Why "model answered" is the success criterion.** The model is what the page leads with; marking
  a read OK while the unit refused the model would show a stale value as fresh. Any read without
  the model is `NO_ANSWER` (older values kept, per `comfoair_sysinfo.h`).
- **Text handling.** Unit strings are NUL-padded and the probe store keeps at most
  `COMFOAIR_PROBE_VALUE_MAX` (40) bytes, so strings are copied up to the first NUL and clamped to
  47 chars + NUL. Firmware (0x06) is UINT32 little-endian; an answer shorter than 4 bytes is
  ignored, not zero-padded. A short error bitfield is zero-filled (a bit not sent is an error not
  reported).
- **Error payloads are passed through raw.** `classify_response()` assumes the error code is the
  first byte, which is unverified; keeping all bytes is how hardware can prove it wrong.
- **Transmit failures still wait.** `ESP_ERR_TIMEOUT` from `comfoair_can_transmit()` means the
  driver may still send the frame, so the reply window is waited out regardless.
- **Log noise.** A unit that stays silent would otherwise log every minute into the ring served by
  `GET /api/log`; only the first failure in a row logs at INFO, repeats at DEBUG.
- **A notification left over** by a `start()` that landed between a walk's end and its summary
  wakes a pass that hands out nothing (`handed == 0`) and logs nothing, so a walk is never logged
  twice.
