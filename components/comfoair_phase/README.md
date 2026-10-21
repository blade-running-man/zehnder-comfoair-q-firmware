# comfoair_phase — pure device-phase verdict, CAN bus-health resolver, and one monitor pass as arithmetic

> This is documentation, not code — re-read the source before relying on a detail.

The device's health is spread over subsystems that are never read together: provisioning,
the start results of `app_main`, the CAN driver, the MQTT client, OTA. This component turns
those facts into **one verdict**, `device_phase_t`, which feeds the status LED, `/api/status`
and the log. It also decides whether the ventilation unit is still answering on CAN (the
bus-health resolver behind the `<prefix>/bus` availability topic and the `BUS_SILENT` phase).
Everything here is **pure, total, clockless and global-free**: no ESP-IDF or FreeRTOS header,
so the whole input space is swept on the host. The 1 Hz task that reads the device and acts on
the answer lives in the neighbour `comfoair_phase_monitor`.

## Responsibility

Owns:
- The 14 phases, their priority order and the cascade `comfoair_phase_derive()`.
- One spelling per phase (`comfoair_phase_name()`) and per bus state (`comfoair_bus_health_name()`).
- Which MQTT CONNACK codes count as a refusal for phase purposes (`comfoair_phase_connack_is_refusal()`).
- The four-state bus resolver, its thresholds (30/60/90 s) and the single-probe latch.
- The two hold-offs (30 s broker-down → `NO_BROKER`; the resolver's 90 s → `BUS_SILENT`) and
  transition detection, via `comfoair_phase_collect_pass()`.
- The mapping of raw reads (`comfoair_phase_facts_t`) onto the 17 phase inputs.

Does NOT do:
- Read any subsystem, run a task, read a clock, log — `comfoair_phase_monitor` (`comfoair_phase_monitor.c`).
- Request a CAN sweep — it only returns `probe = true`; the monitor calls `comfoair_poll_request_now()`.
- Publish `<prefix>/bus` — the monitor projects via `comfoair_bus_health_available()` into
  `comfoair_mqtt_note_bus()`; `comfoair_mqtt` publishes.
- Map a phase to a colour/pattern — `comfoair_led` (`comfoair_led_look_of()`, `comfoair_led_map.c`).
- Render the JSON status — `comfoair_wire` (it calls `comfoair_phase_name()`).
- Classify CONNACK codes into retry/failure classes — `comfoair_mqtt_backoff`; spell them for
  `/api/status` — `comfoair_wire_connack_name()`.
- Record start faults / OTA — `comfoair_phase_monitor_note_fault()` / `_note_ota()` store them; this
  component only reads them as a bitmask in the facts.

## Public API

Three headers in `include/`.

### `comfoair_phase.h`

| Symbol | Contract |
| --- | --- |
| `device_phase_t` | 14 values + `COMFOAIR_PHASE_COUNT`, **declared in priority order** (lower number wins). `BOOT = 0`. |
| `comfoair_phase_inputs_t` | 17 already-debounced booleans/enums; **no time field**. The intended growth seam (one field per subsystem). |
| `comfoair_phase_derive(in)` | Any task/core, no locks, no state, deterministic. `NULL` → `BOOT`. Always returns a phase (last branch `ONLINE` is unconditional). |
| `comfoair_phase_connack_is_refusal(code)` | `true` for everything except 0 and 3 (so 1, 2, 4, 5 and any out-of-range code). `uint32_t`, not the esp-mqtt enum, so it builds on the host. |
| `comfoair_phase_name(p)` | Static string literal, never `NULL`; `"unknown"` only for a value outside the enum. Spellings: `boot`, `ota`, `trial`, `setup-ap`, `recovery-ap`, `fault`, `net-rejected`, `net-lost`, `setup-expired`, `connecting`, `broker-rejected`, `bus-silent`, `no-broker`, `online` (the SPA depends on these). |

Priority order and the condition for each (from `comfoair_phase.c`):
`BOOT` (!machine_ready) > `OTA` > `TRIAL` (prov TRIAL) > `SETUP_AP` (AP on air & UNCONFIGURED) >
`RECOVERY_AP` (AP on air & FALLBACK_AP) > `FAULT` (any of 7 start branches failed) >
`NET_REJECTED` (WRONG_PASSWORD/REFUSED) > `NET_LOST` (NETWORK_ABSENT/NO_ADDRESS) >
`SETUP_EXPIRED` (WINDOW_CLOSED) > `CONNECTING` (CONNECTING or RETRYING) > `BROKER_REJECTED` >
`BUS_SILENT` (bus_started && bus_silent) > `NO_BROKER` (configured && !connected) > `ONLINE`.

### `comfoair_bus_health.h`

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_BUS_QUIET_MS` = 30000 | The one tunable. Not measured: the unit's PDO broadcast period is unknown; hardware scenario HV-CAN-05 is meant to produce N, then this becomes N. |
| `COMFOAIR_BUS_PROBE_MS` = 2×QUIET (60 s) | Enter `PROBING`, ask for one sweep. |
| `COMFOAIR_BUS_SILENT_MS` = 3×QUIET (90 s) | The verdict; leaves room for a sweep of a few seconds (one frame per polled PDO × 100 ms) to be answered. Derivation pinned by `_Static_assert`s at the end of `comfoair_bus_health.c`. |
| `comfoair_bus_health_t` | `NEVER_SEEN = 0`, `FRESH`, `PROBING`, `SILENT`, `COUNT`. |
| `comfoair_bus_health_state_t` | Per-pass memory (`seen`, `last_frames`, `last_move_ms`, `probed`). One instance, monitor task only; zero-init is correct. |
| `comfoair_bus_health_pass(st, in, out)` | Monitor task only. Total: `out == NULL` → returns; `st`/`in` NULL → `out = {NEVER_SEEN, probe=false}`. `out` is written before the argument check so a bad call cannot keep a stale verdict/probe alive. |
| `comfoair_bus_health_available(s)` | `<prefix>/bus` payload: true for `FRESH`/`PROBING` only. |
| `comfoair_bus_health_is_silent(s)` | `bus_silent` input: true for `SILENT` only. **Not** `!available()`. |
| `comfoair_bus_health_name(s)` | `never-seen`/`fresh`/`probing`/`silent`, else `"unknown"`; never `NULL`. |

### `comfoair_phase_collect.h`

| Symbol | Contract |
| --- | --- |
| `comfoair_phase_fault_t` + `COMFOAIR_PHASE_FAULT_BIT(w)` | Bit index per start branch: CAN, POLL, CONTROL, NET, MQTT_QUEUE, MQTT_CLIENT, HTTP. A **set** bit means that branch failed. |
| `COMFOAIR_PHASE_NO_BROKER_MS` = 30000 | Broker hold-off ≈ three esp-mqtt reconnect attempts (default ~10 s, never overridden in this tree); anchored at the first tick that saw it down. |
| `comfoair_phase_facts_t` | One field per *read* the monitor makes (`now_ms`, `faults` bitmask, net snapshot, CAN `started`/`rx_frames`, MQTT `configured`/`connected`/`last_connack`). |
| `comfoair_phase_collect_t` | Pass memory: embedded bus state, broker-down anchor, last `phase`. One instance (monitor task); zero-init is correct (phase BOOT = what the LED shows before the first transition). |
| `comfoair_phase_collect_pass(state, facts, out)` | Monitor task only, no locks, no clock. Fills `out` fully every pass: `inputs` (what derive saw), `phase`, `previous`, `changed`, `bus`, `probe`. `state` or `facts` NULL → BOOT/BOOT/NEVER_SEEN, state untouched; `out` NULL tolerated. |

No function here returns `esp_err_t`; nothing can fail.

## Implementation

- `comfoair_phase.c` — the cascade, `a_startup_branch_failed()` (the 7-term OR that grows with
  `app_main`), the CONNACK rule, the name table.
- `comfoair_bus_health.c` — the four-state machine, two projections, names, compile-time
  threshold asserts.
- `comfoair_phase_collect.c` — one pass: run the bus resolver, apply the broker hold-off, map
  facts → inputs, derive, detect transition. Helper `elapsed()` does the unsigned-difference test.

Flow per monitor tick: monitor reads facts → `comfoair_phase_collect_pass()` →
(internally) `comfoair_bus_health_pass()` → broker hold-off → `comfoair_phase_derive()` →
monitor stores the phase, calls `comfoair_led_set_phase()`, `comfoair_mqtt_note_bus(available(bus))`,
`comfoair_poll_request_now()` if `probe`, and logs only if `changed`.

Bus resolver: "did the counter **move**" (`!=`), not "grow"; `seen` is sticky within a driver
session; any movement resets the anchor and clears `probed`; `!started` resets everything to
NEVER_SEEN (counters restart at zero on the next `comfoair_can_start()`).

Invariants and concurrency: no shared state, no atomics — each state struct has exactly one
owner (the monitor task). All time comparisons are `(uint32_t)(now - since) >= span`, correct
across the ~49.7-day wrap and at `now_ms == 0`.

DO NOT:
- Include ESP-IDF, FreeRTOS, `comfoair_poll.h`, `comfoair_mqtt` or `comfoair_can` headers here.
  PlatformIO's host dependency finder works per component directory; one such include drags the
  TWAI driver/esp-mqtt into `test_phase` and `test_bus_health` and kills them.
- Read a clock in `comfoair_phase_derive()` or the collector. Hold-offs are measurements made from
  the caller-supplied `now_ms`; derivation must stay callable twice with the same answer.
- Reorder the cascade to "group" provisioning states — the relative order of
  OTA/FAULT/BROKER_REJECTED/BUS_SILENT/NO_BROKER is a decision, not grouping.
- Delete the `bus_started &&` term as dead code (FAULT already implies `can_started`). It covers a
  driver stopped after a successful start (`bus_started` is the driver *now*, `can_started` is the
  one-time start result); `test_a_stopped_driver_is_not_a_silent_bus` pins it.
- Fill `bus_silent` from `!comfoair_bus_health_available()` or fuse the two projections — that
  paints a never-connected loom as BUS_SILENT ("check the loom").
- Add a second silence threshold anywhere; the resolver is the single owner.
- Shorten the refusal test to `last_connack != 0` (code 3 is a temporary Mosquitto restart).
- Give BROKER_REJECTED a hold-off — a latched CONNACK is a fact and enters at once.
- Probe on every PROBING pass or "once a minute" — a full sweep of RTRs on a 50 kbps bus; the `probed` latch
  makes it one per entry into PROBING.
- Add "and state is not CONNECTED" to the NET_* tests — provisioning clears the failure on an
  address; a belt would hide a machine that stopped doing so.
- Spell 60000/90000 as independent numbers.

## Dependencies

- `REQUIRES comfoair_provision` only (`comfoair_prov_state_t`, `comfoair_prov_failure_t` appear in
  `comfoair_phase.h`). No PRIV_REQUIRES.
- Depended on by: `comfoair_phase_monitor` (calls `comfoair_phase_collect_pass`,
  `comfoair_bus_health_available`, `comfoair_phase_name`, `comfoair_bus_health_name`; its
  `note_fault()` is fed `COMFOAIR_PHASE_FAULT_*` from `src/main.cpp`), `comfoair_led`
  (`device_phase_t` in `comfoair_led.h`/`comfoair_led_driver.h`), `comfoair_wire` (`device_phase_t`
  in the status struct, `comfoair_phase_name()` in `comfoair_wire_render_status.c`).
  `comfoair_mqtt`, `comfoair_ha` and `comfoair_mqtt_backoff` reference these functions in comments
  only.

## Tests

- `test/test_phase/` — one binary of three files plus `phase_inputs.h`:
  `test_phase_cases.cpp` (each phase condition and pairwise priority, CONNACK 0/3/unknown, names),
  `test_phase_sweeps.cpp` (exhaustive sweep of inputs: no fall-through, every phase reachable,
  priority order mirrors the cascade, enum has 14 values, derive is deterministic),
  `test_phase_collect.cpp` (mapping of each fault bit, broker hold-off incl. wrap and re-grant,
  NEVER_SEEN vs SILENT reaching the phase, single probe, transition reported once, NULLs).
- `test/test_bus_health/test_bus_health.cpp` — the four states, 60/90 s thresholds, one probe per
  entry, counter wrap, millisecond wrap, stopped driver, names, NULLs.
- Also exercised from other suites: `test/test_mqtt_backoff` (checks agreement of
  `comfoair_phase_connack_is_refusal()` with the backoff classes), `test/test_wire_status`
  (`comfoair_phase_name()` in the JSON), `test/test_led_pattern` (every `device_phase_t` maps to a look).
- NOT covered on the host: the monitor glue (`comfoair_phase_monitor.c` is `#ifdef ESP_PLATFORM`) —
  that one probe produces exactly one sweep is hardware scenario HV-CAN-12; the real PDO period
  (the 30 s base) is HV-CAN-05.

## Notes

- **Admission rule for a phase:** a phase exists only if the owner's next action differs, or the
  expected duration differs by an order of magnitude. Everything else stays a field in
  `/api/status`. Adding a phase also means updating `comfoair_led_look_of()`, which is total over
  the enum.
- **BOOT is a guard, not a live phase.** `machine_ready` is true before the monitor starts; BOOT is
  actually seen as the LED's initial value. A monitor that failed to start shows FAULT (the
  monitor's `s_phase` is initialised to FAULT), never BOOT.
- **TRIAL above the AP phases** because during a trial the radio is AP_STA, so the AP is always on
  air and a lower TRIAL would be unreachable. **OTA above everything but BOOT** because "do not cut
  power" beats "come set me up". **AP phases above FAULT** because an open AP is an instruction and
  a security state; FAULT is information.
- **BROKER_REJECTED above BUS_SILENT:** a CONNACK refusal is a received fact; bus silence is an
  inference from an unmeasured threshold. During the 300 s rejected-credentials retry the opposite
  order would hide the certain verdict indefinitely.
- **CONNACK 1 and 2 count as refusals** (owner action is the broker's settings, like 4/5) while 3
  does not (temporary). Three independent readers own the 0..5 code space — this function,
  `comfoair_wire_connack_name()` and `comfoair_mqtt_backoff`; change all three together.
- **NEVER_SEEN replaces a boot grace period.** Separating "never heard" from "heard and stopped"
  avoids a third number and an uptime input. NEVER_SEEN is offline on `<prefix>/bus` (no values to
  report) but not BUS_SILENT.
- **Availability hysteresis is asymmetric:** offline after the threshold, online on the first frame
  with no debounce.
- **Known and accepted:** a CAN driver that stops and restarts entirely between two passes is never
  seen as stopped; its zeroed counter reads as movement for one pass. Fixing it would need a session
  number in `comfoair_can_stats_t`, and no code path restarts the driver within a second.
- **Faults are a bitmask, not seven bools,** because `comfoair_phase_monitor_note_fault()` is called
  from `app_main` before the monitor task exists. `MQTT_QUEUE` and `MQTT_CLIENT` stay separate so a
  client that never built reports FAULT, not NO_BROKER.
