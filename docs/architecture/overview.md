<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities).
</script>

# Architecture overview

The firmware is an ESP-IDF application (C and C++, built with PlatformIO) that sits on the
ComfoAir Q's CAN bus as one more node, keeps a model of what the unit reports, and exposes that
model over three transports. This page is the big picture; the other pages in this section go
one level deeper, and the Protocol, Interfaces and Device sections carry the details.

## One model, three transports

There is exactly one state and command model, and every transport is a projection of it:

| Transport | Direction | Where it lives | Covered in |
| --- | --- | --- | --- |
| HTTP REST (`/api/*`) | read and write | `components/comfoair_http/` | [REST API](/interfaces/rest-api) |
| WebSocket (`/ws`) | push only, device to browser | `components/comfoair_http/comfoair_http_ws.c` | [WebSocket](/interfaces/websocket) |
| MQTT (with Home Assistant discovery) | read and write | `components/comfoair_mqtt/`, `components/comfoair_ha/` | [MQTT](/interfaces/mqtt), [Home Assistant discovery](/interfaces/home-assistant-discovery) |

None of the transports keeps its own copy of the entity values: they read them from
`comfoair_state`. None of them decodes a CAN frame; the HTTP layer reads only the CAN driver's
statistics for `GET /api/system` (`components/comfoair_http/comfoair_http_system.c`). REST and the
WebSocket render through the same renderer in `components/comfoair_api/`; MQTT publishes the same
values entity by entity. The
set of entities is not written by hand anywhere: it is generated into
`components/comfoair_registry/include/registry_generated.h` ({{ data.total }} entities, plus
the polled PDO list and the schema version) and `web/src/api/entities.ts` from `tools/pdo_table.py` — see
[Entity registry](/development/entity-registry).

Two rules follow from "one model":

- **Every ventilation write is on both REST and MQTT.** The {{ data.writable }} writable entities and the six parameterised
  operations are reachable from both, and both go through the same command builder
  (`comfoair_command`) and the same sender (`comfoair_control`). Device management
  (`/api/config`, `/api/provision`, the probe, OTA) is REST only; MQTT carries no configuration.
- **The web UI is a client and nothing more.** Every path the SPA calls (`/api/state`,
  `/api/entities`, `/api/entities/<key>`, `/api/ops/<name>`, `/api/config`, `/api/provision`,
  `/api/wifi/scan`, `/api/status`, `/api/system`, `/api/log`, `/api/probe`, `/api/ota`, `/ws`) is
  a public route registered in `comfoair_http_start()`, reachable by `curl` with the same rules
  (the `/ws` upgrade itself is answered before any rule runs; see
  [Security](/architecture/security)). The WebSocket is push-only: data frames a client sends are
  read and discarded, and a frame larger than 128 bytes closes the socket, so it can never become a
  command path the REST API does not have.

::: details Why not a JSON-RPC API
A single RPC endpoint (method name plus parameters) was the
obvious alternative to per-entity routes. It would not remove the MQTT surface: Home Assistant
discovery needs a state topic, a command topic and an availability topic per entity, so the
firmware would carry an RPC surface **and** a per-entity MQTT surface — two contracts, and in
practice a second entity list to keep in step with the first. Per-entity REST routes are the same
shape as the MQTT tree, which is what lets both be generated from the one registry.
:::

## Data flow

```text
                 ComfoAir Q unit (CAN, 50 kbit/s, node 0x01)
                     |  PDO broadcasts            ^  RMI command frames,
                     |  + replies to RTR polls    |  RTR polls (node 0x2A)
                     v                            |
  comfoair_can   [can_rx task] ---callback---+    [comfoair_ctrl task]  [comfoair_poll task]
                     |                       |          ^                     (1 sweep / h)
  protocol           v                       |          | queue (8)
  (pure)        pdo_decode.h  rmi_*.h        |    comfoair_control_submit()
                     |                       |          ^
  state              v                       |          |
  (pure, locked) comfoair_state  <------ optimistic set for selects/durations
                     |   change marks per consumer (PUSH, MQTT)
          +----------+-----------+                      ^
          |                      |                      |  comfoair_command (pure):
  API     v                      v                      |  key/op + value -> up to 3 frames
  projection  comfoair_api     comfoair_ha (topics,     |
          |   (JSON)           discovery payloads)      |
          |                      |                      |
  transports                                            |
     GET /api/state    WS /ws    MQTT <prefix>/<key>/state     POST /api/entities/<key>
     GET /api/entities           (publisher task)              POST /api/ops/<name>
          |                      |                             MQTT <prefix>/<key>/command
          v                      v                             MQTT <prefix>/ops/<name>/set
        browser / curl        broker -> Home Assistant
```

Reading, step by step:

1. **CAN.** `comfoair_can` runs the TWAI controller on the pins and bitrate the board describes
   and hands every received frame to one callback, on its `can_rx` task. The callback in
   `src/main.cpp` drops standard-id and RTR frames, feeds the probe's node census, and passes PDO
   frames on.
2. **Protocol.** The id and payload rules live in header-only, host-tested code under
   `components/comfoair_protocol/include/` (`pdo_decode.h`, `rmi_frame.h`, `rmi_response.h`,
   `rmi_read.h`, `airflow_constraints.h`). The transport moves bytes and decides nothing.
3. **State.** `comfoair_state_apply_pdo()` applies one PDO to every entity that claims it (PDO 337
   feeds three entities), using the divider, signedness and sentinel from the generated registry.
   It records a change only when the value actually differs, with one change mark per consumer
   (the WebSocket push and MQTT) so neither can consume the other's updates.
4. **Notification.** The callback then calls `comfoair_http_notify_state_changed()` and
   `comfoair_mqtt_notify_state_changed()`. Both only set a flag and wake a task; neither blocks the
   bus-reading task.
5. **Projection and transport.** The WebSocket task pushes a delta document at most every 250 ms
   (a `ping` every 15 s when nothing changed); the MQTT publisher task publishes the changed
   entities. REST renders on request.

Writing goes the other way. A request (`POST /api/entities/<key>`, `POST /api/ops/<name>`, or an
MQTT `.../command` or `.../ops/<name>/set` message) is turned by the pure `comfoair_command` into
up to three RMI frames, queued with `comfoair_control_submit()`, and sent by the `comfoair_ctrl`
task. The HTTP handler answers as soon as the command is queued; a full queue is answered
`503 "the bus is busy"` rather than applied minutes later. Two kinds of write also update the
state model directly. A duration produces no frame at all: it is stored by `comfoair_control` and
written into the state model, which no PDO would otherwise do. A select write, once queued, sets
the chosen option in the state model at once (`comfoair_state_set_option()`, called by both
`comfoair_http_command.c` and `comfoair_mqtt_command.c`); for the three RMI-read selects it stands
until the probe task's next settings read (up to 6 h), and for the others the unit's next PDO
overwrites it. The switch and the fan are shown only once the unit confirms them by PDO. Details:
[Commands](/protocol/commands).

## Runtime configuration

Nothing that differs between installations is compiled in. Wi-Fi credentials, the broker (host,
port, user, password), the topic prefix, the discovery switch, the device name and the optional
UI password are entered in the web UI and stored in NVS by `comfoair_config`
(`components/comfoair_config/include/comfoair_config.h`). The four command durations are registry
entities, written like any other entity; the permanent-manual setting is the `permanent_manual`
field of `POST /api/config`. Both are persisted by `comfoair_control` in the `cfg_app` namespace.
One published `.bin` therefore suits every unit.

Changes apply without a reboot: saving broker settings posts a reconfigure request that the MQTT
publisher task carries out between two passes (`comfoair_mqtt_reconfigure()`), and Wi-Fi changes go
through the provisioning state machine with a 90 s trial and a rollback to the last known-good
network. See [Configuration storage](/device/config-storage) and [Networking](/device/networking).

## Pure decisions, thin device halves

Almost every component that decides something is pure C or C++ with no ESP-IDF header, so it builds in
the host test environment and is tested there; the device half beside it owns the task, the
clock, the radio or the flash and only carries out the decision. The pairs:

| Decides (pure, host-tested) | Carries out (device) |
| --- | --- |
| `comfoair_command` | `comfoair_control` |
| `comfoair_provision` | `comfoair_net` |
| `comfoair_ha`, `comfoair_mqtt_inbound`, `comfoair_mqtt_backoff` | `comfoair_mqtt` |
| `comfoair_wire`, `comfoair_policy`, `comfoair_auth`, `comfoair_ota` | `comfoair_http` |
| `comfoair_phase` | `comfoair_phase_monitor` |
| `comfoair_led` (pattern), `comfoair_button` (gesture) | their `_driver` / `_task` files |
| `comfoair_probe`, `comfoair_probe_report` | `comfoair_probe_task` |

Time always enters a pure component as a `now_ms` argument, never from a clock. See
[Testing](/development/testing).

## No Arduino

The firmware is ESP-IDF only: `platformio.ini` sets `framework = espidf` and pins
`pioarduino/platform-espressif32#55.03.311`; there is no Arduino environment, and no component
includes `Arduino.h` directly or through a library. The reasons are practical:

- The design depends on IDF facilities used directly: the TWAI driver with explicit bus-off
  recovery, `esp_http_server` with WebSocket support, esp-mqtt with auto-reconnect disabled so the
  firmware owns the retry ladder, app rollback in the bootloader, and NVS namespaces.
- Host testing needs the protocol and decision layers free of any framework, and the ESP-IDF
  component layout lets the same sources build both for the device and as PlatformIO libraries
  for the native test environment.

The platform pin is deliberate: a floating platform URL can move the toolchain and IDF under the
code, so it is bumped only on purpose.
