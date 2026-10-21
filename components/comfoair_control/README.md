# comfoair_control — the task that puts built commands on the CAN bus, and the owner of the four durations

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_command` decides *which* bytes a write produces; this component decides *when* they go.
It owns everything about a command that cannot be host-tested: a FreeRTOS queue, a sender task,
the delay between frames, the RMI sequence counter, the TWAI transmit call, and the NVS-backed
durations (boost / bypass / balance / away minutes) plus the "permanent manual" preference that
`comfoair_command` needs to build a frame. HTTP handlers and the MQTT publisher hand it a
ready-built `comfoair_command_t` and return immediately; the bus is touched later, on this
component's own task.

## Responsibility

Owns:

- The command queue (`QUEUE_LEN` 8) and the sender task `comfoair_ctrl` that drains it.
- Per-frame waiting (`comfoair_command_frame_t.delay_ms`) and chunked transmission of each RMI frame.
- The node-wide 2-bit RMI sequence counter, shared with the probe (`comfoair_probe_task`).
- The local node id (`0x2A`) and the unit node id (`0x01`) every RMI sender on this device uses.
- The in-memory and persisted durations and `permanent_manual` (NVS namespace `cfg_app`).
- Send statistics (`queued` / `sent` / `failed` / `dropped`).

Does NOT do:

- Build frames, choose unit/subunit bytes or validate ranges — `comfoair_command` (pure, host-tested).
- Assemble CAN ids or chunk frames by hand — it calls `rmi_frame.h` (`comfoair_protocol`).
- Talk to the TWAI driver directly — `comfoair_can` (`comfoair_can_transmit()`).
- Decide that a request is a duration write vs a frame write, or report the outcome to clients —
  `comfoair_http` and `comfoair_mqtt` (with `comfoair_mqtt_inbound` deciding).
- Mirror durations into the state model — the callers (`src/main.cpp` at boot, HTTP/MQTT after a
  store) call `comfoair_state_set_number()`.
- Know the current fan level: `fan_resume_level` in the returned durations is always the default
  `0`; callers fill it from `comfoair_state_get_fan()`.
- Retry, wait for an RMI reply, or reboot. An unacknowledged frame is a log line and a counter.

## Public API

Header: `include/comfoair_control.h` (C, `extern "C"`; the implementation is C++ because
`rmi_frame.h` is).

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_CONTROL_LOCAL_NODE` = `0x2A` | The node this firmware speaks as. Must equal what `comfoair_poll` uses for PDO requests — same node, same bus. Also used by `comfoair_probe_task` to filter replies and exclude itself from scans. |
| `COMFOAIR_CONTROL_UNIT_NODE` = `0x01` | The ComfoAir unit; the only destination of commands. Deliberately not configurable: the unit byte inside the frame decides "write" vs "bricking write", and `comfoair_command` pins those by test only for this destination. |
| `esp_err_t comfoair_control_start(void)` | Call once from the app task (`src/main.cpp`, only when CAN came up). Creates mutex + queue, loads NVS, spawns the task. `ESP_ERR_INVALID_STATE` if already started; `ESP_ERR_NO_MEM` if mutex/queue/task allocation fails. Unreadable NVS is not an error: defaults stand. |
| `esp_err_t comfoair_control_submit(const comfoair_command_t *)` | Never blocks (`xQueueSend(..., 0)`); copies the command by value. `ESP_OK` queued; `ESP_ERR_NO_MEM` queue full (callers answer "bus is busy" / 503); `ESP_ERR_INVALID_STATE` never started (callers answer "commands are not running"); `ESP_ERR_INVALID_ARG` NULL, or `count == 0` without `stores_duration`. `count == 0` **with** `stores_duration` returns `ESP_OK` and queues nothing. Says nothing about whether the unit acknowledged. |
| `void comfoair_control_durations(comfoair_command_durations_t *)` | Copy under the mutex. Before `start()` returns `comfoair_command_durations_defaults()` (what the registry advertises). NULL is a no-op. Any task. |
| `esp_err_t comfoair_control_store_duration(const char *key, uint16_t minutes)` | `key` is the registry key: `boost_duration`, `bypass_duration`, `balance_duration`, `away_duration`. Updates memory first, then persists. `ESP_ERR_INVALID_ARG` NULL key; `ESP_ERR_INVALID_STATE` not started (nothing stored); `ESP_ERR_NOT_FOUND` unknown key; any NVS error = **value already in effect, only persistence failed**. No range check (see Notes). |
| `esp_err_t comfoair_control_set_permanent_manual(bool)` | Same shape: `ESP_ERR_INVALID_STATE` before start; memory updated, then NVS u8 written; an NVS error means in effect but not persisted. Called by `comfoair_http_config.c` on a config PATCH. |
| `comfoair_control_stats_t` / `comfoair_control_get_stats()` | `queued` accepted into the queue, `sent` commands whose every chunk was acknowledged, `failed` commands with at least one unacknowledged chunk, `dropped` submits refused because the queue was full. Zeros before start. Read by `comfoair_http_system.c` into sysinfo. |
| `uint8_t comfoair_control_next_sequence(void)` | Next 2-bit SeqNr (CAN-id bits 17–18). Any task, never blocks, callable before `start()` (static `portMUX`). One counter for every RMI sender on this node. |

Internal constants (`comfoair_control.cpp`): `QUEUE_LEN 8` — a human produces about one command a
second, so eight is a burst nobody makes by hand, and a longer queue would apply commands minutes
late; `TX_TIMEOUT_MS 200` — per-chunk driver patience (an 8-byte extended frame is ~2.5 ms at
50 kbit/s), bounding how long the task waits on a unit that does not acknowledge; task stack 3072,
priority 4 — it only logs and calls TWAI, no JSON.

## Implementation

- `include/comfoair_control.h` — the contract above, plus the "why a queue" rationale.
- `comfoair_control.cpp` — everything: storage (`load_durations`, `store_u16`), the wire
  (`send_frame`, `control_task`), `submit`, stats, `start`, the sequence counter.
- `CMakeLists.txt` — one source; REQUIRES `comfoair_command comfoair_can comfoair_config
  comfoair_protocol comfoair_poll nvs_flash`.

Flow: `start()` → mutex + queue → `load_durations()` (defaults first, then each NVS key that reads
OK, NVS opened `NVS_READONLY`) → task. Caller builds a command with durations from
`comfoair_control_durations()` → `submit()` increments `queued` *before* `xQueueSend` (and rolls it
back into `dropped` on failure) → `control_task` receives, and for each frame: sleep `delay_ms` if
non-zero, take a fresh sequence number, `send_frame()` → `command_can_id(LOCAL, UNIT, 0,
needs_multi_frame, false, true, seq)` and `split_into_can_frames()` → `comfoair_can_transmit()` per
chunk → `comfoair_command_readback_pdo()`: when a frame started or cancelled the away timer, ONE
`comfoair_poll_request_pdo(pdo, READBACK_DELAY_MS = 1500)` (also after an unacknowledged frame —
the read-back is what shows whether the unit took it) → `sent++` or `failed++`.

Concurrency: `s_lock` (mutex) guards `s_durations` and `s_stats`; `s_sequence` has its own spinlock.
NVS writes happen outside the lock. The sender task is the only transmitter of commands; the probe
transmits separately but shares the sequence counter.

DO NOT:

- Send from the HTTP handler instead of queueing. `comfoair_can_transmit()` waits for the driver,
  an unpowered unit never acknowledges, and `esp_http_server` serves every socket from one task —
  the whole web UI would freeze per chunk; the "Auto" preset's 1000 ms delayed third frame cannot
  be waited for in a handler either.
- Make `submit()` block or enlarge the queue to hide `ESP_ERR_NO_MEM`. "Busy" is the honest answer;
  a late ventilation command is worse than a refused one.
- Hand-assemble the CAN id or the chunk loop. `rmi_frame.h` and `test_rmi_frame` carry the reasoning
  for every bit; a local copy would silently diverge from a fix there.
- Share one sequence number across the frames of one command. A level change sends two 13-byte
  multi-frame messages; one number for both puts them on byte-identical CAN ids, removing the only
  field that tells them apart.
- Stop sending a command's remaining frames, or a frame's remaining chunks, after one fails. A
  truncated multi-frame message leaves half a command in the receiver's reassembler; a balance
  command's second frame releases the other subunit's timer, and skipping it leaves both timers set.
- Move `s_sequence` under `s_lock`. The probe can ask for a number when `start()` failed or never
  ran, and `s_lock` would be NULL.
- Open NVS `NVS_READWRITE` in `load_durations()` — it would create the namespace on every first
  boot. Or read `permanent_manual` as a `bool` from flash: it is stored as u8 and normalised
  (`!= 0`) because erased flash returns `0xff`, which is not a valid `_Bool`.
- Roll the in-memory value back when persisting fails — the owner's change happened; it just will
  not survive a reboot.
- Add a reboot or infinite retry on an unacknowledged bus — nothing reboots because a peer is absent.
- Send the read-back RTR from this task, or sleep here for it. The poll task owns the request
  schedule and the 100 ms gap between requests; a second sender would put two RTRs back to back
  and stall the next command for the delay.

## Dependencies

Requires: `comfoair_command` (the queued item type, `comfoair_command_durations_defaults()`),
`comfoair_can` (`comfoair_can_transmit()`), `comfoair_config` (NVS namespace/key names:
`cfg_app`, `dur_boost`, `dur_bypass`, `dur_balance`, `dur_away`, `manual_perm`),
`comfoair_protocol` (`rmi_frame.h`), `comfoair_poll` (`comfoair_poll_request_pdo()`), `nvs_flash`; FreeRTOS and `esp_log` from IDF.

Depended on by: `src/main.cpp` (start, seeds the state model with durations), `comfoair_http`
(`comfoair_http_command.c`: durations, store, submit; `comfoair_http_config.c`: durations,
set_permanent_manual; `comfoair_http_system.c`: stats), `comfoair_mqtt` (`comfoair_mqtt_command.c`
`comfoair_mqtt_handle_inbound`: durations, store, submit),
`comfoair_probe_task` (`comfoair_probe_task_bus.cpp`, `comfoair_probe_task.cpp`,
`comfoair_probe_task_unit.cpp`: next_sequence and both node ids). `comfoair_wire` and
`comfoair_mqtt_inbound` mention it only in comments and do not link it.

## Tests

No host suite compiles this component — grep over `test/` and `tests/` finds no reference to it.
Covered indirectly by neighbours' suites: `test/test_command` (frame bytes, unit byte, durations
defaults), `test/test_rmi_frame` (CAN id and chunking it calls), `test/test_mqtt_inbound` (the
decision that leads to store vs submit).

Not covered by any test: the queue/task, per-frame delay, "send every chunk and frame even after a
failure", stats arithmetic, sequence sharing with the probe, NVS load/store and the u8
normalisation, and the error codes of `submit`/`store_duration` that HTTP/MQTT map to responses.
These are hardware-only.

## Notes

- **Why no range check in `store_duration`.** `comfoair_command` already checked the value against
  the registry; a second set of numbers here is how the two would drift apart.
- **Why durations live here and not in `comfoair_config`'s document.** This component builds
  commands from them, so it is their one writer; HTTP shows and changes them through this API.
  No PDO carries them, so the state model only knows them because callers mirror them in.
- **The sequence counter's limit.** Two bits wrap after four frames. A probe read waits at most
  300 ms and a command is at most three frames, so two commands inside one probe wait could make
  a command's reply look like the probe's answer — visible as an odd probe report, never as a
  wrong frame sent. Two private counters would be worse: both senders would routinely issue the
  same number.
- **Fire-and-forget.** `sent` means every chunk was ACKed at CAN level, not that the unit applied
  the RMI command; nothing here reads RMI replies.
- **`start()` only when CAN is up.** `src/main.cpp` calls it inside the CAN-ok branch; on a boot
  without CAN, `submit`, `store_duration` and `set_permanent_manual` return
  `ESP_ERR_INVALID_STATE`, `durations` returns defaults, stats read zero, and callers report
  "commands are not running". `next_sequence` still works, so the probe is unaffected by design
  (though `main.cpp` only starts the probe after a successful `start()`).
