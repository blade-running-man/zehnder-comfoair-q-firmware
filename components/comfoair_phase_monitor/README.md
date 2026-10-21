# comfoair_phase_monitor — the 1 Hz task that reads the whole device and publishes its phase

> This is documentation, not code — re-read the source before relying on a detail.

The device has one answer to "what is it doing right now" (`device_phase_t`: BOOT, OTA, SETUP_AP,
FAULT, NET_LOST, BUS_SILENT, NO_BROKER, ONLINE, ...). Computing it needs the network machine, the
CAN driver, the MQTT client, the OTA upload and app_main's seven start verdicts read in the same
moment. This component is the one place that does those reads: a FreeRTOS task wakes once a second,
reads five subsystems, hands the facts to the pure collector in `comfoair_phase`, and pushes the
verdict to the LED, to MQTT's bus topic, to the poller (one probe sweep) and to `GET /api/status`.
It decides nothing itself.

## Responsibility

Owns:

- The 1 Hz monitor task (`comfoair_phase`, stack 3072, priority 3) and its lifecycle.
- The sticky start-fault bitmask (`s_faults`), written by app_main before the task exists.
- The OTA-in-progress flag (`s_ota`), written by the OTA upload handler.
- The last published phase (`s_phase`) for any-task reads.
- Calling the side effects the collector asks for: `comfoair_led_set_phase()`,
  `comfoair_mqtt_note_bus()`, `comfoair_poll_request_now()`, one log line per transition.

Does NOT do:

- Decide the phase, the broker hold-off, the bus verdict, or whether a tick is a transition —
  `comfoair_phase` does (`comfoair_phase_collect.c`, `comfoair_phase.c`, `comfoair_bus_health.c`),
  pure and host-tested.
- Render the colour — `comfoair_led` (`comfoair_led_driver.h`). The immediate OTA overlay is pushed
  by `comfoair_http_ota.c` itself, not by this task.
- Publish MQTT — `comfoair_mqtt_note_bus()` only stores an atomic and wakes `comfoair_mqtt`'s
  publisher.
- Serialise `/api/status` — `comfoair_http` (`comfoair_http_read.c`, `comfoair_http_config.c`) calls
  `comfoair_phase_monitor_get()`.
- Reboot, restart or stop anything. A FAULT phase is a colour and a JSON field, nothing more.

## Public API

Header: `include/comfoair_phase_monitor.h` (re-exports `comfoair_phase.h` and
`comfoair_phase_collect.h`, so callers get `device_phase_t` and `comfoair_phase_fault_t`).

| Symbol | Contract |
| --- | --- |
| `esp_err_t comfoair_phase_monitor_start(void)` | app_main only, once, **last** — after `comfoair_net_start()` and `comfoair_mqtt_start()`, so all seven branches have reported before the first tick. `ESP_ERR_INVALID_STATE` if already started (no change). `ESP_ERR_NO_MEM` if `xTaskCreate` fails: `s_phase` stays FAULT. The caller logs and carries on — never returns/reboots over it. |
| `device_phase_t comfoair_phase_monitor_get(void)` | Any task. One relaxed atomic load, no lock. Answers FAULT if the monitor never started. |
| `void comfoair_phase_monitor_note_fault(comfoair_phase_fault_t which)` | Any task, before or after start (before is the normal case). Atomically ORs `COMFOAIR_PHASE_FAULT_BIT(which)` into a sticky mask; out-of-range values are ignored. Never cleared. |
| `void comfoair_phase_monitor_note_ota(bool active)` | Any task; one atomic store, read on the next tick. Caller is `indicate()` in `comfoair_http_ota.c`: true just before `esp_ota_begin()`, false on every exit that did not install an image, left true on success until the restart. |

Key types (defined in `comfoair_phase/include/comfoair_phase_collect.h`):

- `comfoair_phase_fault_t` — `CAN, POLL, CONTROL, NET, MQTT_QUEUE, MQTT_CLIENT, HTTP`, `COUNT = 7`.
  MQTT_QUEUE (`comfoair_mqtt_init()`) and MQTT_CLIENT (`comfoair_mqtt_start()`) are separate on
  purpose: merging them would report "check your broker" when the firmware itself failed.
- `COMFOAIR_PHASE_NO_BROKER_MS = 30000` — the hold-off before a configured-but-disconnected broker
  becomes NO_BROKER; applied by the collector, not here.

Constants in `comfoair_phase_monitor.c`:

- `TICK_MS = 1000` — twice slower than the LED's 25 ms render can show, two orders of magnitude
  faster than the shortest hold-off (30 s). Faster buys nothing; slower delays the light.
- `TASK_STACK = 3072` — holds a `comfoair_net_status_t` (SSID, AP name, address strings) plus three
  more snapshots and one `ESP_LOGI` whose `vsnprintf` is the biggest consumer. Same figure as
  `comfoair_poll`/`comfoair_control`.
- `TASK_PRIO = 3` — below CAN rx/bus tasks (10, 9) and the MQTT publisher (4): a watcher that
  preempts what it watches changes what it measures.

## Implementation

Files:

- `comfoair_phase_monitor.c` — the task, the three statics, the four API functions. Entirely inside
  `#ifdef ESP_PLATFORM`.
- `include/comfoair_phase_monitor.h` — the contract above.
- `CMakeLists.txt` — `REQUIRES comfoair_phase`; `PRIV_REQUIRES comfoair_can comfoair_led
  comfoair_mqtt comfoair_net comfoair_poll esp_timer`.

Per tick (`monitor_task`):

1. `read_the_device()` fills a zeroed `comfoair_phase_facts_t`: `now_ms` from `esp_timer`,
   `machine_ready = true`, `s_ota`, `s_faults`, one `comfoair_net_status()` snapshot (state,
   failure, `ap_on_air`), `comfoair_can_get_stats()` (`started`, `rx_frames`),
   `comfoair_mqtt_get_stats()` (`configured`, `connected`, `last_connack`).
2. `comfoair_phase_collect_pass(&s_collect, &facts, &out)` — the pure decision.
3. Store `out.phase` into `s_phase`; call `comfoair_led_set_phase(out.phase)` on **every** tick.
4. `comfoair_mqtt_note_bus(comfoair_bus_health_available(out.bus))` on every tick.
5. `if (out.probe) comfoair_poll_request_now();`
6. `if (out.changed) ESP_LOGI("phase", "%s -> %s (bus %s)", ...)`.
7. `vTaskDelay(TICK_MS)`.

Concurrency: `s_faults`, `s_ota`, `s_phase` are `_Atomic`, all relaxed (independent words; no
ordering between them is relied on). `s_collect` is a function-static owned by the task alone — the
reason the collector has no atomics. `s_task` is written only by `start()` from app_main.

Every read is safe on a subsystem that never came up (`comfoair_net_status()` answers UNCONFIGURED
before its task exists; the CAN/MQTT stats read statics), so a dead peer can never stop the monitor.

DO NOT:

- Add a decision here. Anything that decides is invisible to host tests; a new input goes into
  `comfoair_phase_facts_t`, a new rule into `comfoair_phase_collect_pass()`.
- Replace `machine_ready = true` with a read of `comfoair_net_status().state` — UNCONFIGURED means
  both "machine not initialised" and "nobody configured the device", opposite facts. It is true
  because `start()` runs after `comfoair_net_start()`, which runs `comfoair_prov_init()`.
- Split the net snapshot into separate reads — state, failure and `ap_on_air` must describe one
  moment (CONNECTED beside a wrong-password failure is unactionable). The accepted cost: the call
  waits behind a Wi-Fi scan, which only happens while the settings page is open.
- Call `comfoair_led_set_phase()` only on change — a late-started or reconfigured indication would
  stay on a stale phase.
- Log every tick — 86 400 lines/day would flush the bounded `GET /api/log` ring.
- Call `comfoair_poll_request_now()` on anything but `out.probe` — one probing entry must be one
  sweep (hardware scenario HV-CAN-12 in `docs/hardware-verification/02-can-bus-and-polling.md`).
- Change the initial `s_phase` from FAULT to BOOT, or trim `TASK_STACK` without a measured
  high-water mark on the board.

## Dependencies

Requires: `comfoair_phase` (public: types in the header), and privately `comfoair_net`
(`comfoair_net_status`), `comfoair_can` (`comfoair_can_get_stats`), `comfoair_mqtt`
(`comfoair_mqtt_get_stats`, `comfoair_mqtt_note_bus`), `comfoair_led` (`comfoair_led_set_phase`),
`comfoair_poll` (`comfoair_poll_request_now`), `esp_timer`, FreeRTOS, `esp_log`.

Depended on by:

- `src/main.cpp` — `note_fault()` on each failing start branch; `start()` at the end of app_main,
  and also on the `comfoair_net_start()` failure path, which returns early (otherwise the device
  would never start the monitor and sit on BOOT forever).
- `comfoair_http/comfoair_http_read.c` — `get()` for the `phase` field of `/api/status`;
  `comfoair_http/comfoair_http_config.c` the same for the `POST /api/provision` answer.
- `comfoair_http/comfoair_http_ota.c` — `note_ota()` in `indicate()`.

## Tests

No host suite compiles this component: no test includes `comfoair_phase_monitor.h`, and the `.c`
is wrapped in `#ifdef ESP_PLATFORM` in case one ever does (PlatformIO's `lib_extra_dirs =
components` would otherwise build it on the host).

What it calls is covered: `test/test_phase` sweeps `comfoair_phase_collect_pass()`
(`test_phase_collect.cpp`) and the phase derivation (`test_phase_cases.cpp`, `test_phase_sweeps.cpp`); `test/test_bus_health` covers the
bus verdict and the probe edge.

NOT covered by any host test: the reads themselves (which stat field is copied where), the tick
rate, the every-tick LED/MQTT feeds, the single `poll_request_now()` per probe entry, the task
creation/FAULT-on-failure path, and the fault/OTA atomics. These are hardware-verified
(`docs/hardware-verification/02-can-bus-and-polling.md` HV-CAN-12,
`docs/hardware-verification/07-indication-and-button.md` HV-LED-*).

## Notes

- **Why a separate component and not a file in `comfoair_phase`.** The host build and its
  dependency finder work per component directory, so a FreeRTOS task beside
  `comfoair_phase_collect.c` would drag FreeRTOS, esp-mqtt and TWAI into `test_phase` and end its
  exhaustive sweep. Same split as `comfoair_provision`/`comfoair_net`.
- **Why `REQUIRES comfoair_phase` but everything else private.** The header names
  `device_phase_t` and `comfoair_phase_fault_t`, so consumers need that include path; the five
  read subsystems never appear in the header, and making them public would push TWAI and esp-mqtt
  into every consumer's requirements.
- **Why FAULT, not BOOT, as the initial `s_phase`.** If the task never starts, "starting up" and
  "the reporter is dead" would be the same answer. The LED keeps its own BOOT initial value until
  the first `set_phase()`, so after a failed start `/api/status` says FAULT while the pixel still
  shows BOOT.
- **Why faults are a sticky bitmask in a static.** They are one-time start verdicts, reported
  before the task exists; the live CAN state arrives separately via `comfoair_can_get_stats().started`.
- **Why OTA outranks everything (even the open AP).** Its owner instruction is "do not cut the
  power". The phase reaches the LED up to a second late, which is why the OTA handler also pushes
  an immediate LED overlay.
- **Why `note_bus()` on every tick is cheap.** It stores one atomic and wakes the publisher only
  when the answer changed, and is safe with no broker or a destroyed client. Projecting the bus
  verdict here keeps the `<prefix>/bus` topic and the BUS_SILENT phase one fact, not two thresholds.
