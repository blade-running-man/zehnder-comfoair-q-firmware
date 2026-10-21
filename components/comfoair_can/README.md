# comfoair_can — the TWAI (CAN) byte transport for the ComfoNet bus

> This is documentation, not code — re-read the source before relying on a detail.

The ComfoAir Q talks over ComfoNet: classic CAN, 29-bit ids, 50 kbps. This component brings up
the ESP32's on-chip TWAI controller on the pins and bitrate the board descriptor names, hands
every received frame to one consumer callback on a task (never from the ISR), sends extended
frames with storage that outlives the call, brings a bus-off controller back by itself, and
keeps counters for diagnostics. It is deliberately thin: it moves bytes and never interprets an
id or a payload — that is `comfoair_protocol`'s job, which is host-tested.

## Responsibility

Owns:
- The single TWAI node (`twai_new_node_onchip`), its ISR callbacks, and its lifetime
  (`comfoair_can_start()` / `comfoair_can_stop()`).
- The receive path: ISR -> 32-deep queue -> `can_rx` task -> consumer callback.
- The transmit slots that keep a frame alive until the driver's TX-done interrupt returns it.
- Automatic bus-off recovery with back-off (`can_bus` task), forever.
- Per-session counters and the controller's error state (`comfoair_can_get_stats()`).

Does NOT do:
- Decode or build CAN ids/payloads (PDO ids, RMI framing, multi-frame split) —
  `comfoair_protocol` (`pdo_decode.h`, the RMI builders).
- Filter frames. It passes `extended` and `rtr` through; the consumer (`on_can_frame` in
  `src/main.cpp`) drops `!extended || rtr` itself.
- Decide what to send or when — `comfoair_poll` (hourly RTR sweep), `comfoair_control`
  (RMI writes), `comfoair_probe_task` (probe RMI).
- Name a pin or a bitrate — `components/board/` (`board_t.can_tx`, `can_rx`, `can_bitrate`).
- Turn stats into a phase/health verdict or a status word — `comfoair_phase`
  (`comfoair_bus_health.c`), `comfoair_phase_monitor`, `comfoair_http_system.c`.
- Reboot, ever. A dead bus is recovered or reported, never answered with a restart.

## Public API

Header: `include/comfoair_can.h` (wrapped in `extern "C"` because `main.cpp` and several
consumers are C++).

| Symbol | Contract |
| --- | --- |
| `comfoair_can_rx_cb_t` | `void (*)(uint32_t can_id, bool extended, bool rtr, const uint8_t *data, size_t len, void *ctx)`. Runs on the `can_rx` task (priority 10), one call per frame. `data` is valid only during the call; `len` is 0..8 and is 0 for every RTR. Must not block for long: while it runs, frames pile up in the 32-deep queue and overflow is counted as `rx_dropped`. |
| `comfoair_can_start(board, cb, ctx)` | Creates queues, the node, registers ISR callbacks (while the node is still stopped — the driver refuses afterwards), creates `can_rx` (4096 B, prio 10) and `can_bus` (3072 B, prio 9), enables the node. Zeroes all counters and frees all TX slots. Returns `ESP_ERR_INVALID_STATE` if already started, `ESP_ERR_INVALID_ARG` for NULL `board`/`cb`, `ESP_ERR_NO_MEM` for a queue/task failure, or the driver's error. On any failure it calls `comfoair_can_stop()` itself, so nothing is left half-up. The caller must continue without the bus on error (`main.cpp` notes `COMFOAIR_PHASE_FAULT_CAN` and keeps Wi-Fi/web alive). |
| `comfoair_can_stop()` | Disables/deletes the node, then asks both tasks to exit and waits up to 1 s for each. `ESP_OK` when both retired; `ESP_ERR_TIMEOUT` when one did not — queues are then deliberately leaked rather than freed under a live task. Only the tests and `start`'s failure path call it today. Must not be called from inside the rx callback (it would wait on itself). |
| `comfoair_can_transmit(id, data, len, rtr, timeout_ms)` | Sends one **extended** (29-bit) frame; `rtr` sends no data (a PDO poll). `data` is copied, so the caller's buffer is free on return. Blocks the calling task until the driver reports all queued frames done or `timeout_ms` passes. Returns `ESP_ERR_INVALID_STATE` (not started), `ESP_ERR_INVALID_ARG` (`len > 8`, or `len > 0 && data == NULL`), `ESP_ERR_NO_MEM` (all 9 slots held by the controller), `ESP_ERR_TIMEOUT` (not done in time — the frame may still go out later), or the driver's refusal. Callable from any task, concurrently. |
| `comfoair_can_bus_state_t` | `ACTIVE=0`, `WARNING`, `PASSIVE`, `OFF`. Spelled without driver types so host-tested layers can include the header without `esp_driver_twai`. |
| `comfoair_can_stats_t` | `started` (node exists now), `rx_frames`, `rx_dropped` (queue full **or** RX interrupt with nothing to take), `bus_errors`, `bus_recoveries` (successful `twai_node_recover` requests), `state`, `tx_error_count`, `rx_error_count`. Counters are zeroed by every `start`, so they describe the current session. |
| `comfoair_can_get_stats(out)` | Any task; NULL `out` is a no-op. Zeroes `out` first. When not started, `state` reads `ACTIVE` (zero) and the error counts are 0 — **always check `started` first**; the driver itself reports a stopped node as bus-off, which is why `comfoair_http_system.c` says "stopped" before it looks at `state`. |

Key constants (`CAN_MAX_DLC`, `TX_QUEUE_DEPTH` and `TX_SLOTS` in `comfoair_can_internal.h`,
`RX_QUEUE_LEN` and `fail_retry_cnt` in `comfoair_can_api.c`, the back-off in `comfoair_can.c`):
- `CAN_MAX_DLC 8` — classic CAN; ComfoNet has no CAN-FD.
- `RX_QUEUE_LEN 32` — rides out a burst of PDO broadcasts while the consumer is busy; on overflow
  the ISR counts and drops instead of blocking.
- `TX_QUEUE_DEPTH 8`, `TX_SLOTS 9` — the driver queue is configured 8 deep and one more frame
  can sit in the shift register, so 9 is the most the controller can hold; fewer slots would
  refuse a send the hardware would accept.
- `fail_retry_cnt = 3` — `-1` would retransmit forever, an ISR that never stops on a bus whose
  unit is unpowered; a bounded count fails the send and lets the caller decide.
- `RECOVER_BACKOFF_FIRST_MS 250`, `RECOVER_BACKOFF_MAX_MS 5000` — first recovery is immediate,
  then 250 ms doubling to 5 s; reset to immediate when the bus comes back.
- Bitrate comes from `board->can_bitrate` (50 kbps on ComfoNet), so a future board cannot
  silently disagree with it.

## Implementation

Files:
- `comfoair_can.c` — the interrupt side: the four ISR callbacks, the `can_rx` and `can_bus` tasks,
  the back-off, and the one definition of the state both files share (node, queues, TX slots,
  counters, `g_can_stopping`). A file of its own because everything in it runs in ISR or task
  context the driver and the start path hand it, never on a caller's thread.
- `comfoair_can_api.c` — the public API: `start`/`stop`/`transmit`/`get_stats`, the slot claim
  and the two task handles. A file of its own because it runs on the caller's task and owns the
  node's lifetime.
- `comfoair_can_internal.h` — private to the component, not in `include/`: the shared constants
  and types, `extern`s for the shared state (`g_can_` = shared by both files) and the prototypes
  of the ISRs and tasks that `start` registers and creates.
- `include/comfoair_can.h` — the public contract.
- `CMakeLists.txt` — `REQUIRES board esp_driver_twai`; no `PRIV_REQUIRES`.

Receive flow: `comfoair_can_on_rx_done` (IRAM, ISR) calls `twai_node_receive_from_isr` into a
stack buffer, copies into an `rx_item_t`, `xQueueSendFromISR`s it, counts `rx_frames` or
`rx_dropped`. For an RTR it forces `len = 0` (the HAL fills no buffer for an RTR while the DLC is
whatever the requester chose), and clamps a DLC above 8. `comfoair_can_rx_task` blocks on the
queue and calls the consumer.

Transmit flow: `tx_slot_claim()` takes a free slot with an atomic compare-exchange, copies the
payload into the slot, builds the `twai_frame_t` in the slot (`ide = 1` always), and calls
`twai_node_transmit`. If the driver refuses, the slot is released at once; otherwise it stays
busy until `comfoair_can_on_tx_done` finds it by pointer identity and clears `busy`. Then
`twai_node_transmit_wait_all_done(timeout_ms)` turns "accepted" into "went out".

Bus-off flow: `comfoair_can_on_state_change` (ISR) posts `BUS_EVT_OFF` on entry to bus-off and
`BUS_EVT_ACTIVE` on leaving it, ignoring warning/passive churn. `comfoair_can_bus_task` sleeps the
back-off, calls `twai_node_recover()` (counting `bus_recoveries` on `ESP_OK`;
`ESP_ERR_INVALID_STATE` means it already left bus-off and is not retried), and on `ACTIVE` resets
the back-off.
`comfoair_can_on_error` only counts `bus_errors`.

Stop flow: node disabled and deleted first; then `g_can_stopping = true`, a stop sentinel posted
to each task queue with timeout 0, and one "done" token awaited per task on `g_can_done_queue`
(1 s each). Each task exits through `signal_done(); vTaskDelete(NULL)`.

Concurrency: the ISRs touch only the queues, the counters, and the slots' `atomic_bool`. Counters
are `volatile uint32_t` written from ISR context and read unlocked by `get_stats`. Several tasks
may transmit at once (poll, control, probe task); slot claim is the only shared mutation.

DO NOT:
- Build the frame on the caller's stack. `twai_node_transmit` stores the **pointer** and the
  TX-done ISR (and the post-recovery resend path) dereferences it later; the wait can time out
  with the frame still queued, which is the normal case on an unpowered unit — returning would
  leave the driver pointing at a dead stack frame. The slots exist for this.
- Release a slot anywhere but `comfoair_can_on_tx_done` or an outright `twai_node_transmit`
  refusal.
- Trust the DLC of an RTR, or copy more than 8 bytes.
- Call the consumer, log, or block in any ISR callback.
- Call `twai_node_recover()` from `comfoair_can_on_state_change`: it is not in the driver's IRAM
  set (`linker.lf`), so under `CONFIG_TWAI_ISR_CACHE_SAFE` it would be a flash access with cache
  off, and an ISR has nowhere to wait out a back-off.
- Make recovery give up after N attempts — a unit switched on tomorrow must still be found.
- `vTaskDelete()` the task handles from `stop`: `comfoair_can_rx_task` lives inside the
  consumer's callback and would be killed mid-callback. The sentinel + `g_can_stopping` flag +
  done queue exist for that; the flag matters because a sentinel cannot be posted onto a full rx
  queue.
- Block when posting the stop sentinel: a full queue means the task is running and will see
  `g_can_stopping`; blocking could deadlock against a consumer waiting on the stopper.
- Free the queues after a `stop` timeout.
- Register callbacks after `twai_node_enable` — the driver refuses and the node would hear nothing.
- Remove the counter reset in `start`: consumers (`comfoair_bus_health.c`) rely on per-session
  counters.

## Dependencies

Requires: `board` (`board_t` with `can_tx`, `can_rx`, `can_bitrate`), `esp_driver_twai`
(`esp_twai.h`, `esp_twai_onchip.h`), FreeRTOS, `esp_log`.

Depended on by:
- `src/main.cpp` — `comfoair_can_start(board, on_can_frame, nullptr)`; the one rx consumer.
- `comfoair_poll` (`comfoair_poll.cpp`) — RTR sweep via `comfoair_can_transmit`.
- `comfoair_control` (`comfoair_control.cpp`) — RMI command chunks.
- `comfoair_probe_task` (`comfoair_probe_task_bus.cpp`) — probe RMI chunks.
- `comfoair_http` (`comfoair_http_system.c`) and `comfoair_phase_monitor` — `comfoair_can_get_stats`.
- `comfoair_command`, `comfoair_mqtt`, `comfoair_mqtt_inbound`, `comfoair_ha`, `comfoair_probe`
  deliberately do NOT require it (stated in their `CMakeLists.txt`): they must not be able to
  transmit.

## Tests

- `test/test_can_transport/` — the host suite, 30 tests, compiling this very `comfoair_can.c` and
  `comfoair_can_api.c` against a fake ESP-IDF private to the suite
  (`test/test_can_transport/idf/`, `fake_twai.c`, `fake_rtos.c`; the include path is in
  `[env:native]` of `platformio.ini`). Covers: pins/bitrate from the board, bounded retry,
  callback registration before enable, double start / NULL cb refusal, stop releasing everything
  and never tearing the consumer down mid-callback, frame delivery intact, RTR with no payload, no
  callback or logging in ISR context, DLC clamp, empty receive and overrun counted without
  blocking, transmit argument checks, extended frames, RTR with no data, frame lifetime after a
  timeout / after the sender returns / independent of the caller's buffer, stalled controller
  reported, bus errors, error counters, bus state, counters zeroed on start, bus-off recovery,
  traffic after recovery, no repeat recovery once recovered.
- Run: `pio test -e native -f test_can_transport`.
- Not covered on the host: the real TWAI driver and transceiver, IRAM/cache-safety of the ISRs,
  real timing of the back-off, and the `stop` timeout (leak) path on real FreeRTOS. Those need
  the hardware scenarios under `docs/hardware-verification/`.

## Notes

- `transmit` waits with `twai_node_transmit_wait_all_done`, which waits for **all** queued
  frames, not just the caller's. With concurrent senders one caller's wait can include another's
  frames; a timeout therefore means "the controller has not drained", not "my frame failed".
  Callers treat `ESP_ERR_TIMEOUT` as "may still be sent" (`comfoair_probe_task_bus.cpp` still
  waits for the reply window).
- Only 29-bit frames can be sent: ComfoNet is extended-id throughout, so there is no `ide` parameter.
- `rx_dropped` covers two different losses (queue overflow, spurious RX interrupt) so that every
  frame is either delivered or counted — diagnostics never show a silent loss.
- `bus_recoveries` climbing steadily is the signature of a wiring/termination fault or an
  unpowered unit with a biased transceiver; without recovery that fault would freeze every
  entity at its last value until a power cycle, a hang that breaks "nothing reboots because a
  peer is absent" just as surely as a reboot would.
- After a `stop` that timed out, `g_can_stopping` is cleared and the queue handles are left
  non-NULL; a later `start` overwrites them (bounded leak). This path means a task is already
  wedged.
- Task priorities 10 (`can_rx`) and 9 (`can_bus`) are referenced by other components (LED, button,
  probe task) choosing priorities below them; changing them ripples into those comments.
