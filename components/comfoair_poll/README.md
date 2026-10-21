# comfoair_poll — the hourly PDO request (RTR) sweep over CAN

> This is documentation, not code — re-read the source before relying on a detail.

The ComfoAir Q unit broadcasts most of its PDOs unprompted, and those broadcasts are the
firmware's primary data source. Some values, though, are sent rarely or never on their own. This
component runs one FreeRTOS task that, once at boot and then once an hour, sends a
remote-transmission request (RTR) for every PDO in the generated request list. The unit
answers with an ordinary broadcast, and that answer comes in through the same receive path
(`comfoair_can` → `on_can_frame` in `src/main.cpp`) as any other frame. This component only
*asks*. It never reads or decodes a reply.

## Responsibility

Owns:
- The request cycle task (`comfoair_poll`): one sweep right away at start, then one sweep per
  `interval_ms`, with `frame_delay_ms` between requests.
- Out-of-schedule sweeps on request (`comfoair_poll_request_now()`).
- One delayed request for one PDO (`comfoair_poll_request_pdo()`): the read-back after an away
  command. What goes out next is decided by the pure `comfoair_poll_plan`; this component reads the
  clock, sends and sleeps.
- The cycle's timing defaults (`COMFOAIR_POLL_DEFAULT_CONFIG()`).

Does NOT do:
- Build the CAN id. `comfoair::pdo_request_can_id()` in `comfoair_protocol`
  (`include/pdo_decode.h`) does that, and it is host-tested in `test/test_pdo_decode`.
- Decide which PDOs are requested. `COMFOAIR_REQUEST_PDOS` / `COMFOAIR_REQUEST_PDO_COUNT` are
  generated from `tools/pdo_table.py` (`ALL_REQUEST_PDOS`) into
  `comfoair_registry/include/registry_generated.h` by `tools/render_c.py`. Never hand-edit that list.
- Receive, decode or store answers. That is `comfoair_can` (receive task), then `src/main.cpp`
  and `comfoair_protocol`, then `comfoair_state`.
- Own the TWAI driver, frame storage or transmit completion. That is `comfoair_can`
  (`comfoair_can_transmit()`).
- Decide *when* an extra sweep is needed. The pure bus-health machine in `comfoair_phase`
  (`comfoair_bus_health_pass()`) sets `probe`, and `comfoair_phase_monitor` turns that into the call.
- Send RMI commands. That is `comfoair_control`, which uses the same node id `0x2A`
  (`COMFOAIR_CONTROL_LOCAL_NODE`).

## Public API

Header: `include/comfoair_poll.h` (C linkage; includes only `esp_err.h`).

| Symbol | Contract |
| --- | --- |
| `comfoair_poll_config_t` | `local_node_id` (sender node, must be `0x01..0x3F`), `interval_ms` (one full sweep period), `frame_delay_ms` (gap after each request). Copied by value into a static at start; the caller's struct can be discarded afterwards. |
| `COMFOAIR_POLL_DEFAULT_CONFIG()` | Compound literal `{.local_node_id = 0x2A, .interval_ms = 3600000, .frame_delay_ms = 100}`. |
| `esp_err_t comfoair_poll_start(const comfoair_poll_config_t *config)` | Call once, from `app_main`, and only after `comfoair_can_start()` succeeded. Creates the task. Returns `ESP_ERR_INVALID_STATE` if already started, `ESP_ERR_INVALID_ARG` for a NULL config or a node id outside 1..63, `ESP_ERR_NO_MEM` if `xTaskCreate` fails (the task handle stays NULL, so a retry is possible), and `ESP_OK` otherwise. The caller must handle failure without aborting. `main.cpp` logs it ("the unit still broadcasts") and records `COMFOAIR_PHASE_FAULT_POLL`. There is no stop function. |
| `void comfoair_poll_request_now(void)` | Wakes the task for one extra sweep. Safe from any task context (not ISR: it uses `xTaskNotifyGive`). It is a silent no-op before a successful start. Calls collapse: any number of calls before the task next waits produce exactly one extra sweep. |
| `void comfoair_poll_request_pdo(uint16_t pdo, uint32_t delay_ms)` | ONE request for `pdo`, `delay_ms` from now. Returns at once (a critical section and a notification; never sends or waits on the caller's task). Calls for the same PDO before it goes out coalesce into one, `delay_ms` after the last. A different PDO while one is pending is dropped with a WARN (one slot, one caller: `comfoair_control` after an away command). No-op before start; not from an ISR. Logs `read-back of PDO <n> in <ms> ms` at DEBUG. |

Why the defaults are what they are:
- **`0x2A`**: the node id this firmware has always used on the bus. It must stay equal to
  `COMFOAIR_CONTROL_LOCAL_NODE` because both are the same node on the same bus. Changing it is
  harmless but pointless unless a second node shows up.
- **1 hour / 100 ms**: these have run against the real unit for a long time. A sweep is one RTR per polled PDO.
  Sent back to back on a 50 kbps bus, that is a burst the unit has no reason to expect, so do not
  shorten either value without a reason. `COMFOAIR_REQUEST_PDO_COUNT` × 100 ms — seconds — nominal per sweep, and
  `comfoair_phase`'s bus-quiet thresholds are argued from that figure.

## Implementation

Files:
- `comfoair_poll.cpp`: the glue — the static config, the plan and its spinlock, the task handle,
  `request()`, `poll_task()`, start and the two request functions. C++ only so that it can call
  the `inline` `comfoair::pdo_request_can_id()` from `pdo_decode.h`.
- `include/comfoair_poll.h`: the public contract above.
- `CMakeLists.txt`: one source file, with `REQUIRES comfoair_can comfoair_registry
  comfoair_protocol comfoair_poll_plan esp_timer`.

Flow:
1. `comfoair_poll_start()` validates the config, copies it into `s_config`, initialises the plan
   with the generated request list and a sweep due now, and creates the task `"comfoair_poll"`
   (3072-byte stack, priority 5). It logs one INFO line with the cycle parameters.
2. `poll_task()` loops: under the spinlock, `comfoair_poll_plan_next(&s_plan, now_ms())`. A send
   step → `comfoair_can_transmit(id, nullptr, 0, /*rtr*/ true, /*timeout_ms*/ 100)`, then
   `vTaskDelay(frame_delay_ms)` — after every request, sweep or read-back. A wait step →
   `ulTaskNotifyTake(pdTRUE, wait)`.
3. The plan (see `components/comfoair_poll_plan/README.md`): a due read-back first, then the
   running sweep's next PDO, then a due or requested sweep; the next scheduled sweep is one
   interval after the previous one STARTED. Otherwise wait until the earlier of the next sweep and
   the pending read-back.
4. Both request functions change the plan under the spinlock and notify the task, which re-plans.
   A sweep requested during a sweep runs once more right after it; after any early sweep the
   hourly schedule counts from that sweep.
5. `now_ms()` is `esp_timer_get_time() / 1000` cut to 32 bits; the plan compares by signed
   difference, so the 49-day wrap is harmless.

Invariants:
- A failed transmit (`ESP_ERR_TIMEOUT`, `ESP_ERR_NO_MEM`, …) is logged at DEBUG and skipped. It is
  never retried within the sweep and never escalated. The unit may simply be unpowered, and the
  next sweep tries again. Nothing reboots because a peer is absent.
- `s_config` is written once, before the task exists, and is read-only afterwards. `s_task` is
  written once by `xTaskCreate`. `s_plan` is touched only under `s_plan_mux`, and only for the
  plan's few comparisons — never across a send or a wait.
- The task never exits.

DO NOT:
- Rebuild the CAN id inline (`pdo << 14 | 0x40 | node`). The tested rule lives in `pdo_decode.h`,
  and a second copy is a second implementation that nobody tests.
- Shorten `interval_ms` or `frame_delay_ms`, or set `frame_delay_ms` to 0, "for fresher data". The
  broadcasts already carry the live data, and back-to-back RTRs are a burst on a 50 kbps bus. Note
  that only the node id is validated: `interval_ms = 0` or `frame_delay_ms = 0` is accepted and
  turns this into continuous or back-to-back sending.
- Abort the sweep, or reboot, on a transmit error.
- Add a timer or a periodic caller for `comfoair_poll_request_now()`. Extra sweeps are meant to come
  once per *entry* into the bus-health `probing` state. One sweep a minute would be sixty times the
  bus traffic (hardware scenario HV-CAN-12).
- Send a read-back from the caller's task, or let `comfoair_poll_request_pdo()` wait. The caller is
  the command sender; the gap between requests is only kept if this task sends every one of them.
- Include `comfoair_poll.h` from a pure, host-tested component (`comfoair_phase`). The call reaches
  FreeRTOS, and `test_phase` / `test_bus_health` would stop linking. That is why
  `comfoair_bus_health_out_t.probe` is a bool that the impure caller acts on.

## Dependencies

- Requires `comfoair_can` (`comfoair_can_transmit`), `comfoair_registry` (`registry_generated.h`:
  the request list and its count), `comfoair_protocol` (`pdo_decode.h`: `pdo_request_can_id`),
  `comfoair_poll_plan` (the decisions) and `esp_timer` (the clock). It also uses FreeRTOS and
  `esp_log` implicitly.
- Used by `src/main.cpp`, which starts it with the defaults inside the "CAN started" branch only
  (`src/CMakeLists.txt` lists it in `REQUIRES`), and by `comfoair_phase_monitor`
  (`comfoair_phase_monitor.c`, `PRIV_REQUIRES comfoair_poll`), which calls
  `comfoair_poll_request_now()` when `out.probe` is set, and by `comfoair_control`, which calls
  `comfoair_poll_request_pdo()` after a command that moved the away timer.

## Tests

- `test/test_poll_plan` covers every decision the task makes (`comfoair_poll_plan`): the boot
  sweep, sweep period, collapsed requests, the read-back's delay, coalescing, interleaving with a
  sweep, and the millisecond wrap. The glue itself has no host suite.
- No host suite compiles this component. No file under `test/` includes `comfoair_poll.h`, and the
  task's clock, sleeping and notification are not covered on the host.
- Covered next to it: the CAN id rule (`test/test_pdo_decode`, `pdo_request_can_id`), the
  transport's RTR send and its stalled-controller error path (`test/test_can_transport`), when a
  probe sweep is asked for (`test/test_bus_health`), and the `COMFOAIR_PHASE_FAULT_POLL` → phase
  mapping (`test/test_phase`).
- On hardware: `docs/hardware-verification/02-can-bus-and-polling.md`, which has HV-CAN-05 (gaps
  between frames, and the unit's answer to the probing sweep) and HV-CAN-12 (exactly one sweep per
  provoked silence and none on a healthy bus).

## Notes

- **The poll is a refresh, not the data path.** If the cycle fails to start, the device still
  decodes broadcasts. That is why a start failure is a recorded fault and not a fatal one, and why
  the cycle runs only when the CAN controller came up.
- **Why a flag in the plan plus a task notification, not a queue:** the plan's sweep flag and its
  one read-back slot collapse many requests into one sweep or one RTR by construction, and the
  notification only wakes the task to re-plan. Several callers, a repeated `probe`, or a burst of
  away commands cannot multiply bus traffic, so callers need no rate limiting of their own.
- **Why the period counts from the sweep's start:** the plan holds a clock reading, so the next
  sweep is simply one interval after this one began — no drift by the sweep's own length, measured
  or nominal.
- **Why the per-request transmit timeout (100 ms) is hard-coded and not configurable:** it bounds
  how long one request on a dead bus can stall the sweep. It equals the default gap, so the worst
  case is about double the nominal sweep length.
