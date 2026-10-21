# Components

The firmware is 40 ESP-IDF components under `components/`, plus the entry point in `src/`. One
component is one responsibility, and a component is a directory: the host test build and the
PlatformIO dependency finder both work per directory. Many responsibilities therefore come as a
pure component plus a thin device component beside it. Where a pure decision and its device code
share one directory instead (`comfoair_button`, `comfoair_led`, `comfoair_auth`, `comfoair_config`,
`comfoair_captive`, `comfoair_mdns`, `comfoair_log`), the device files are wrapped in `#ifdef ESP_PLATFORM` so the host build
compiles them to nothing. Three device-side components carry the same guard around their whole
source, so a host suite whose dependency finder reaches them still builds: `comfoair_phase_monitor`
and `comfoair_ota_runtime` compile to nothing on the host, and `comfoair_lock` compiles to a no-op
lock.

Each group below links to the section that covers it in depth.

## Protocol and CAN

Covered in [CAN bus](/protocol/can-bus), [PDO and RMI](/protocol/pdo-rmi),
[Polling](/protocol/polling) and [Commands](/protocol/commands).

| Component | Responsibility |
| --- | --- |
| `comfoair_protocol` | Header-only, pure protocol code: PDO id and payload decoding (`pdo_decode.h`), RMI write frames (`rmi_frame.h`), RMI response reassembly (`rmi_response.h`), read-only RMI requests for the probe (`rmi_read.h`), the PDO 230 airflow-constraint bitset. |
| `comfoair_can` | TWAI transport: starts the controller on the board's pins and bitrate, runs the receive task and the bus-off recovery task, transmits frames it owns until the driver releases them, exposes bus statistics. Decodes nothing. |
| `comfoair_poll` | The PDO request cycle: one RTR sweep of the request PDOs (`COMFOAIR_REQUEST_PDOS`) per hour, 100 ms apart, as node `0x2A`; `comfoair_poll_request_now()` forces a sweep; `comfoair_poll_request_pdo()` sends one delayed read-back request. |
| `comfoair_poll_plan` | Pure: what the request task sends next and how long it sleeps — the hourly sweep, requested sweeps, and the coalesced one-PDO read-back. |
| `comfoair_command` | Pure: turns an entity write or one of the six operations into at most three RMI frames, with every refusal (unknown entity, bad value, missing parameter) decided here. |
| `comfoair_control` | The command sender task: an 8-deep queue, the shared 2-bit RMI sequence counter, the four durations and the permanent-manual setting persisted in NVS. |

## State and API model

Covered in [Entity registry](/development/entity-registry), [Entities](/reference/entities) and
[REST API](/interfaces/rest-api).

| Component | Responsibility |
| --- | --- |
| `comfoair_registry` | Header-only: `registry_generated.h`, generated from `tools/pdo_table.py` — every entity's key, kind, PDO, divider, options and defaults. Never edited by hand. |
| `comfoair_state` | The state model: one value per entity, the fan as a compound value, per-consumer change marks with generations so a publisher never clears a change it did not send. |
| `comfoair_lock` | The single recursive mutex guarding the state model; a no-op on the host. |
| `comfoair_api` | The JSON projection of the state: full state, delta, WebSocket envelope, one entity, the entity list. Writes into caller buffers and never allocates. |
| `comfoair_json` | A pure parser for the flat JSON objects the device accepts, and the string escaper (`comfoair_json_escape()`) the JSON renderers use; the RAM log escapes its own lines. |

## Transports

Covered in [REST API](/interfaces/rest-api), [WebSocket](/interfaces/websocket),
[MQTT](/interfaces/mqtt), [Home Assistant discovery](/interfaces/home-assistant-discovery) and
[Web UI](/interfaces/web-ui).

| Component | Responsibility |
| --- | --- |
| `comfoair_http` | The HTTP server: every route, the access check in front of each handler, the WebSocket push task, the OTA upload handler, the probe and system documents, serving the SPA. |
| `comfoair_wire` | Pure: the configuration, provisioning and status documents in both directions (`/api/config`, `/api/provision`, `/api/wifi/scan`, `/api/status`) and the bodies of entity writes and operations, with secrets never rendered and errors never quoting a submitted value. |
| `comfoair_mqtt` | The broker connection: one publisher task that owns the esp-mqtt client, publishes state, discovery and availability, handles inbound commands, rebuilds the client on reconfigure, and runs the retry ladder. |
| `comfoair_publish_rate` | Pure: whether one entity's changed state goes out over MQTT now, waits, or is already on the broker — the per-row deadband and minimum interval from the entity table, and the 300 s max hold. REST and the WebSocket are never limited. |
| `comfoair_mqtt_inbound` | Pure: what an arriving MQTT message means — topic to target, payload to value, retained-command refusal. Has no effects. |
| `comfoair_mqtt_backoff` | Pure: the reconnect ladder and failure classes, the retraction budget, the "discovery is owed again" gate, the "configuration URL moved" check. No ESP-IDF dependency at all. |
| `comfoair_ha` | Pure: the MQTT topic tree and the Home Assistant discovery payloads for each entity, and the topic lists of the two cleanups `comfoair_mqtt` runs on a saved change (`comfoair_ha_cleanup_count()`, `comfoair_ha_cleanup_topic()`): rekey when only the topic prefix moved, withdraw when the broker host or port moved. Which of the two is owed is decided in `comfoair_mqtt`, by `comfoair_mqtt_pending_cleanup()` in `comfoair_mqtt_client.c`. |

## Network and provisioning

Covered in [Networking](/device/networking).

| Component | Responsibility |
| --- | --- |
| `comfoair_provision` | Pure state machine: network mode, setup window, trial and rollback of new credentials, fallback access point, the four Wi-Fi failure verdicts. Time is a parameter. |
| `comfoair_net` | Owns the radio, the configuration document in memory and its NVS persistence; runs the provisioning task and scans. It holds the UI password record (checked only through `comfoair_net_check_password()`) and hands the broker password only to `comfoair_mqtt_client.c` through `comfoair_net_broker()`. |
| `comfoair_captive` | The captive portal: answers to OS connectivity probes, a pure DNS responder that resolves every name to the access point, and the UDP task that serves it while the access point is up. |
| `comfoair_mdns` | `comfoair-XXXX.local` and one `_http._tcp` service; a pure name builder plus the responder and its goodbye on restart. |

## Board and indication

Covered in [Board](/device/board), [Indication](/device/indication) and [Button](/device/button).

| Component | Responsibility |
| --- | --- |
| `board` | The board descriptor (`board_get()`): the only place GPIO numbers, LED wiring and the network transport are named. Ships the M5Stack NanoC6. |
| `comfoair_phase` | Pure: the one device phase derived from all subsystems, the CAN bus health resolver (30/60/90 s), and the per-tick collector with its hold-offs. |
| `comfoair_phase_monitor` | The 1 Hz task that reads the network, CAN and MQTT state and the start-up fault bits, derives the phase, drives the LED, publishes bus availability and provokes a poll sweep on a quiet bus. |
| `comfoair_led` | The indication: a pure phase-to-look and pattern table, and a driver with a 25 ms render task for the RGB LED, its power gate and the heartbeat LED. |
| `comfoair_button` | The button gesture: a pure decider (short press, 5 s, 15 s, committed on release) and a 20 ms polling task that executes it, including the two reset tiers. |

## Configuration and security

Covered in [Configuration storage](/device/config-storage) and [Security](/architecture/security).

| Component | Responsibility |
| --- | --- |
| `comfoair_config` | The configuration document: field table, validators, all-or-nothing patch, schema check, sanitising what comes back from flash, the PBKDF2 password record, NVS namespaces and reset tiers. |
| `comfoair_secrets` | Write-only secrets: the read sentinel `__UNCHANGED__`, keep/clear/store/reject on submit, constant-time comparison, and a word-based "is this key secret" classifier that no firmware code calls. |
| `comfoair_auth` | Parses an HTTP Basic `Authorization` header and checks it through an injected verifier; computes the 1 s delay after a failed attempt. Never logs. |
| `comfoair_policy` | Pure access rule: given method, path, "provisioned", "password set" and "authenticated", answers allow, 401 or 403. |

## OTA and system

Covered in [OTA and system](/device/ota-and-system).

| Component | Responsibility |
| --- | --- |
| `comfoair_ota` | Pure pre-flight for an uploaded image: length against the slot, image magic, chip id, app descriptor and project name; the status line and message for every refusal. |
| `comfoair_ota_runtime` | Confirms a freshly booted image 30 s after the HTTP server started (or at once, before a button reset restarts it), and restarts from a short-lived task after an answer has been sent. |
| `comfoair_sysinfo` | Pure renderer of `GET /api/system`: unit identity and active errors, chip, heap, link and bus figures, and the unit error-text table; also the `unit_error` verdict and the one-line `active_errors` text. |
| `comfoair_log` | The RAM log ring (80 lines of up to 159 characters, in 160-byte slots) fed by an ESP-IDF vprintf hook and served by `GET /api/log`. |

## Probe

Covered in [Probe](/protocol/probe).

| Component | Responsibility |
| --- | --- |
| `comfoair_probe` | Pure: a read-only walk of the unit's RMI tree as a state machine, and the result store. Can express only GET-ONE and GETACTIVEERRORS requests. |
| `comfoair_probe_report` | Pure: the catalogue of known RMI properties, value decoders, and the streamed JSON report. |
| `comfoair_probe_task` | The task that sends the walk's requests, collects replies addressed to this node, keeps the bus node census, reads the unit's identity, settings and active errors (10 s after start, every 6 h once complete), the active errors alone every 60 s in between, and writes the two error entities. |
| `comfoair_unit_cadence` | Pure: when the probe task reads the unit again — the full read or the active errors alone — wrap-safe on the tick counter. |

## Web assets

Covered in [Web UI](/interfaces/web-ui) and [Building](/development/building).

| Component | Responsibility |
| --- | --- |
| `web_assets` | Runs `npm ci` (if needed) and `npm run build` in `web/` at CMake configure time and embeds the three gzipped files (`/`, `/app.js`, `/style.css`) with `EMBED_FILES`. |

## Dependency direction

The `REQUIRES` / `PRIV_REQUIRES` lines in each `CMakeLists.txt` give a strict layering. Arrows
point from a component to what it depends on; nothing points upward.

```text
 src/main.cpp ---------------------------------------------- wires everything, owns the order
     |
 transports        comfoair_http ---------> comfoair_mqtt
     |                 |  (+ almost every component below)    |
     v                 v                                      v
 device side    comfoair_net  comfoair_control  comfoair_poll  comfoair_probe_task
                comfoair_phase_monitor  comfoair_led  comfoair_button  comfoair_ota_runtime
                comfoair_can  comfoair_captive  comfoair_mdns  comfoair_log
     |
     v
 pure model     comfoair_wire  comfoair_ha  comfoair_mqtt_inbound  comfoair_api
 and decisions  comfoair_state  comfoair_command  comfoair_config  comfoair_auth
                comfoair_phase  comfoair_sysinfo  comfoair_probe_report  comfoair_publish_rate
     |
     v
 leaves         comfoair_registry  comfoair_protocol  comfoair_lock  comfoair_json
 (no project    comfoair_secrets  comfoair_policy  comfoair_provision  comfoair_mqtt_backoff
  dependency)   comfoair_ota  comfoair_probe  comfoair_unit_cadence  web_assets
                board (GPIO driver only)
```

The device side holds everything that touches FreeRTOS, a driver, the radio or the flash; some of
its members (`comfoair_log`, `comfoair_captive`, `comfoair_mdns`, `comfoair_ota_runtime`) depend on
no other project component at all.

Notable edges, all taken from the `CMakeLists.txt` files:

- `comfoair_state` needs only `comfoair_registry`, `comfoair_protocol` and `comfoair_lock`;
  `comfoair_api` adds `comfoair_json`. The model knows nothing about any transport.
- `comfoair_command` needs only `comfoair_protocol` and `comfoair_registry`; `comfoair_control`
  adds `comfoair_can` and `comfoair_config` (for the durations it persists).
- `comfoair_wire` depends on `comfoair_json`, `comfoair_config`, `comfoair_provision`,
  `comfoair_secrets`, `comfoair_command`, `comfoair_phase` and `comfoair_mqtt_backoff` — all
  host-buildable — so the documents
  build on the host. `comfoair_net` depends on `comfoair_wire`, never the other way.
- `comfoair_mqtt` depends on `comfoair_ha`, `comfoair_mqtt_inbound`, `comfoair_mqtt_backoff`,
  `comfoair_publish_rate`, `comfoair_net`, `comfoair_control`, `comfoair_command`, `comfoair_wire`
  and `comfoair_state`.
- `comfoair_http` sits on top and depends on almost everything, including `comfoair_mqtt`
  (saving the configuration restarts the broker connection).
- `board` is required by `comfoair_can`, `comfoair_net`, `comfoair_led`, `comfoair_button` and
  `comfoair_http`. The first four receive the descriptor as a parameter of their start call;
  `comfoair_http` calls `board_get()` itself, only to name the board in `GET /api/system`
  (`comfoair_http_system.c`).

Where a reverse call is needed, it is passed in rather than linked, which keeps the graph
acyclic:

| Reverse call | How it is wired |
| --- | --- |
| MQTT command changed the state, tell WebSocket clients | `comfoair_mqtt_set_observer(comfoair_http_notify_state_changed)` in `src/main.cpp` |
| Short button press opens the setup window | `comfoair_button_start(b, comfoair_net_open_setup_window)` — a function pointer, so the button component does not include `comfoair_net.h` |
| mDNS needs the device name, ids, board name, version and HTTP port | `src/app_mdns.c` gathers the name and ids from `comfoair_net`, takes the board, app descriptor and port from `app_main()`, and passes them in; `comfoair_mdns` includes no other component header |
| Phase monitor reads CAN, MQTT and network state and asks poll for a sweep | `PRIV_REQUIRES` only, so the pure `comfoair_phase` stays host-buildable |
