# Indication

The device reports its condition in three places: the RGB LED, a one-word `phase` field in
`GET /api/status`, and one log line per transition. All three are fed from a single verdict, the
**device phase**, computed once a second by the phase monitor. The LED can differ from the word in
three deliberate ways: an overlay (button, OTA) replaces the phase look, the start-up grace changes
a colour, and before the monitor's first tick the LED shows `boot` while
`comfoair_phase_monitor_get()` (and therefore `/api/status`) answers `fault`, its initial value.

Three components carry it, each split into a pure half (host-tested) and a thin device half:

| Component | Pure half | Device half |
| --- | --- | --- |
| `comfoair_phase` | `comfoair_phase_derive()` (the cascade), `comfoair_phase_collect_pass()` (one monitor tick as arithmetic), `comfoair_bus_health_pass()` (the CAN silence resolver) | none |
| `comfoair_phase_monitor` | none | the 1 Hz task that reads the subsystems and calls the pure functions |
| `comfoair_led` | `comfoair_led_look_of()`, `comfoair_led_style_of()`, `comfoair_led_resolve()` | `comfoair_led_driver.c`: the 25 ms render task, `led_strip` (RMT), the power gate, the heartbeat pin, the overlay slots |

## The pure-function design

Nothing that decides anything reads a clock, a global or a pin. Time arrives as a `now_ms`
argument, the board's capabilities arrive as `comfoair_led_caps_t`, the owner's brightness settings
as `comfoair_led_policy_t`. The consequence is that every combination of inputs is a host test
case (`test/test_phase`, `test/test_led_pattern`) rather than something observed in front of a
board: "OTA at 40 %", "the flash at 9 975 ms" and "a ramp on a tier boundary" are all single
function calls.

The device halves contain only what cannot be pure:

- `comfoair_phase_monitor.c` reads `comfoair_net_status()`, `comfoair_can_get_stats()` and
  `comfoair_mqtt_get_stats()`, calls `comfoair_phase_collect_pass()`, pushes the result to
  `comfoair_led_set_phase()` and `comfoair_mqtt_note_bus()`, and asks `comfoair_poll_request_now()`
  when the bus resolver wants a probe. Task `comfoair_phase`, priority 3, 3 KB stack, 1 s period.
- `comfoair_led_driver.c` copies the phase, policy and overlay slots under a short critical
  section, calls `comfoair_led_resolve()`, writes the heartbeat level and sends the pixel if it
  changed. Task `comfoair_led`, priority 3, 3 KB stack, 25 ms period (`COMFOAIR_LED_TICK_MS`).

### Why the render sleep rounds up

The project runs FreeRTOS at 100 Hz (`CONFIG_FREERTOS_HZ=100` in `sdkconfig.m5stack-nanoc6`), so
the scheduler grid is 10 ms and 25 ms is not on it. `pdMS_TO_TICKS(25)` floors to 2 ticks = 20 ms,
and topping up the remaining 5 ms with `pdMS_TO_TICKS(5)` gives `vTaskDelay(0)`, which is a yield,
not a sleep: the task would busy-loop at priority 3 for a twentieth of every second, and no counter
would report it. The driver therefore keeps the deadline on the exact 25 ms grid, sleeps whole
ticks rounded **up** and never zero (`ticks_at_least()` in `comfoair_led_driver.c`), and renders
each frame from `esp_timer`, not from the intended deadline. The real sample times are 0, 30, 50,
80, 100 ms …, so the 250 ms spark and the 100 ms heartbeat pip, whose edges are sample times,
keep their exact tick counts. The deadline resyncs only when a whole period was lost; resyncing on any
lateness would turn every period into 30 ms, because the rounded-up sleep leaves the deadline a
few milliseconds behind on nearly every pass by design.

Why not the alternatives: raising `CONFIG_FREERTOS_HZ` to 1000 changes the time slicing of every
task in the firmware for one LED, and a 20 ms tick would not divide the 250 ms spark.

Nothing in any of these reboots anything. A subsystem that is down is a colour and a word.

## Device phases

`device_phase_t` is declared in priority order: when several conditions hold, the
lowest-numbered phase wins, because it is the one that must be fixed before the next can be
diagnosed. `comfoair_phase_derive()` is a straight cascade in that order whose last branch is
unconditional, so every input has exactly one phase.

| # | Phase (`/api/status`) | Condition | What the owner does |
| --- | --- | --- | --- |
| 0 | `boot` | provisioning machine not initialised | wait |
| 1 | `ota` | a firmware image is being written | do not cut the power |
| 2 | `trial` | new Wi-Fi credentials saved, connection not yet confirmed | wait |
| 3 | `setup-ap` | own access point on air, provisioning state `unconfigured` (no usable Wi-Fi pair stored) | connect to the access point |
| 4 | `recovery-ap` | own access point on air, provisioning state `fallback-ap` (credentials stored, network failing) | connect to the access point |
| 5 | `fault` | one of the seven start branches of `app_main` failed | read the log |
| 6 | `net-rejected` | Wi-Fi failure `wrong-password` or `refused` | fix the password or router settings |
| 7 | `net-lost` | Wi-Fi failure `network-not-found` or `no-address` | check the router |
| 8 | `setup-expired` | provisioning state `window-closed`: the setup window ran out with nothing to connect to | short-press the button |
| 9 | `connecting` | provisioning state `connecting` or `retrying` | wait |
| 10 | `broker-rejected` | the broker answered CONNACK with a refusal (any code except 0 and 3), latched until the next successful connection | fix broker credentials |
| 11 | `bus-silent` | CAN driver up, no frame for 90 s after having heard frames | check the CAN loom and the unit |
| 12 | `no-broker` | a broker is configured and has been disconnected for 30 s | check the broker |
| 13 | `online` | none of the above | nothing |

The seven start branches behind `fault` are CAN, the PDO poll cycle, the command sender, the
network, the MQTT queue, the MQTT client and the HTTP server. Each failing branch calls
`comfoair_phase_monitor_note_fault()`, which sets a sticky bit. A broker that is simply not
configured is not a fault.

`boot` is never produced by a running monitor: the provisioning machine is initialised inside
`comfoair_net_start()`, before the monitor starts. It is what the LED shows from
`comfoair_led_init()` until the monitor's first tick. A monitor that failed to start makes
`comfoair_phase_monitor_get()` answer `fault`, so "starting up" and "the reporter is dead" never
share a picture.

### Hold-offs: facts enter at once, absences late

- **Broker:** a configured broker that is not connected counts as connected for
  `COMFOAIR_PHASE_NO_BROKER_MS` (30 s) from the first tick that saw it down. A refusal latched
  from CONNACK is a fact and enters `broker-rejected` immediately. Code 3 (server unavailable) is
  not treated as a refusal, so a broker restarting does not latch it.
- **CAN bus** (`comfoair_bus_health`): four states derived from the driver's frame counter.
  `never-seen` until the first frame, `fresh` while frames move, `probing` after 60 s of silence
  (one PDO request sweep is triggered via `comfoair_poll_request_now()`), `silent` after 90 s. Only
  `silent` drives the `bus-silent` phase. A device that has never heard a frame is not
  `bus-silent`; it publishes the MQTT bus topic as offline but is not told to check the loom.
  The thresholds are `2 ×` and `3 ×` `COMFOAIR_BUS_QUIET_MS` (30 s) by construction.

## LED looks

The fourteen phases map onto **eleven looks** (`comfoair_led_look_t`). Three pairs collapse,
each because the owner action is the same: `trial`/`connecting`, `setup-ap`/`recovery-ap`,
`net-rejected`/`net-lost`. The distinction stays in `/api/status`.

| Look | Phases | Colour (R, G, B) | Rhythm |
| --- | --- | --- | --- |
| BOOT | `boot` | white (255, 255, 255) | solid |
| OTA | `ota` | white | progress |
| SETUP | `setup-ap`, `recovery-ap` | blue (0, 80, 255) | blink |
| WORKING | `trial`, `connecting` | blue | breathe |
| NET_BROKEN | `net-rejected`, `net-lost` | blue | double |
| STRANDED | `setup-expired` | blue | solid |
| NO_BROKER | `no-broker` | amber (255, 120, 0) | breathe |
| BROKER_REJECTED | `broker-rejected` | amber | blink |
| BUS_SILENT | `bus-silent` | amber | double |
| FAULT | `fault` | red (255, 0, 0) | solid |
| OK | `online` | green (0, 255, 0) | spark |

Colour carries the domain (white transitional, blue network, amber degradation, red "cannot fix
itself", green only as the sign of life); rhythm carries the urgency. No two looks share both. A
value outside either enumeration is drawn as FAULT, never as darkness.

### Rhythms

| Rhythm | Shape |
| --- | --- |
| solid | constant |
| breathe | smoothstep envelope (an integer approximation of a raised cosine), 4000 ms period |
| blink | square wave, 500 ms on / 500 ms off |
| double | on 0–120 ms and 300–420 ms of every 2000 ms |
| progress | 1000 ms period, duty cycle = percentage complete (an upload reports it in 25 % steps) |
| ramp | brightness rises from 0 to full over the length of the current button tier |
| spark | 250 ms at full envelope every 10 000 ms |

**Start-up grace.** During the first 300 s of uptime, looks inferred from an absence
(NO_BROKER, BUS_SILENT) are drawn in the WORKING blue instead of amber. Only the colour changes;
the rhythm, the phase and `/api/status` are untouched.

### Heartbeat

The plain status LED (`status_led`) is lit for 100 ms of every 1000 ms,
always, whatever the phase or gesture. It encodes nothing; its only message is "the firmware is
running". On a board with no mono LED the spark is mixed into every colour look instead.

## Overlays and priority

Three overlays can replace the phase look. One slot per kind; a second push of the same kind
replaces the first. The winner is a fixed total order, highest first:

| Priority | Overlay | Pushed by | Picture |
| --- | --- | --- | --- |
| 1 | BUTTON | the button poller while a tier is reached; the reset path for its confirmation flashes | white ramp (setup tier), amber ramp (soft reset), red ramp (hard reset); a full-brightness flash of the same colour on entering a tier and during the reset confirmation |
| 2 | OTA | `POST /api/ota` handler | white progress, at 0, 25, 50, 75 % |
| 3 | IDENTIFY | not pushed by anything today | white solid, clears itself 5 s after the last push |
| — | phase look | the phase monitor | the table above |

BUTTON outranks OTA on purpose: it is the one moment the owner's hand is on the device, and a
factory-reset countdown hidden behind a progress bar would erase a device by feel. The order is
`comfoair_led_override_strongest()`, a maximum over the enumeration, so it does not depend on
which task pushed last.

A BUTTON overlay whose tier is still "none" (inside the first 60 ms, in the 1–5 s dead zone, or
after a stuck button is given up) falls through to the phase: nothing will happen on release, so
nothing is promised. Gesture timing is on the [Button](/device/button) page.

## Brightness

`comfoair_led_resolve()` puts the envelope through a gamma 2.2 table and then scales it by a
**ceiling**, default `COMFOAIR_LED_CEILING_DEFAULT` = 51 (20 % of 255). A lit frame never rounds to
zero (floor of 1). The policy also has a quiet flag that caps the ceiling at 8.

::: warning Not wired to settings
`comfoair_led_policy_set(ceiling, quiet)` exists in the driver and implements a physical off
switch (ceiling 0 blanks the pixel, drives the data line low and drops the power gate), but **no
code calls it**. The device always runs at the default ceiling of 51 with the quiet window off,
and there is no setting or entity for either.
:::

## Failure behaviour

- `comfoair_led_init()` failing to create the RMT channel (`led_strip_new_rmt_device()`) keeps
  the heartbeat and drops colour: the RGB power gate is switched **off** and the colour capability
  is reported as absent, so the resolver blanks the colour half of every frame. Leaving the chip
  powered would leave its latch in an undefined state with nothing able to rewrite it: a dim
  random colour for ever on a device that believes it is dark. There is **no retry loop**; a
  retry every 25 ms that nobody reads is worse than one honest error line, and a reboot is the
  recovery.
- The render task failing to start means nothing is indicated; the gate is dropped for the same
  reason. Both failures are logged; the device carries on.
- `comfoair_phase_monitor_start()` failing leaves the LED on BOOT and `/api/status` reporting
  `fault`.
- When `comfoair_net_start()` fails, `app_main` returns early but still starts the monitor, so the
  device shows red `fault` instead of sitting on white BOOT.

For what each phase means to an owner in practice, see
[Troubleshooting](/guide/troubleshooting).
