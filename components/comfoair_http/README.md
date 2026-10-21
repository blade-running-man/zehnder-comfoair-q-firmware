# comfoair_http — the HTTP REST surface, the `/ws` push channel and the SPA server

> This is documentation, not code — re-read the source before relying on a detail.

The device's one HTTP server (`esp_http_server`, port 80). It serves the REST API that mirrors
MQTT, the WebSocket push channel the web UI uses for live readings, the gzipped SPA embedded in
flash, the captive-portal probe answers, and firmware upload by `.bin`. Every route, the SPA's
included, goes through the same access check: the browser gets no header, cookie or route that
`curl` does not (AGENTS.md criterion 4). This component is impure glue. What a body means, what a
document looks like, who may do what, and what a command is are all decided by pure, host-tested
neighbours; this component reads requests, asks them, and writes answers.

## Responsibility

Owns:
- Route registration and order, the server's tuning (`max_uri_handlers`, sockets, header length,
  stack), and the server's lifecycle (`comfoair_http_start` / `comfoair_http_stop`).
- Turning a request into a `comfoair_http_ctx_t` and applying the policy decision (401 with
  `WWW-Authenticate`, 403), plus the fixed wait after a wrong password.
- Path normalisation for every comparison (`comfoair_http_request_path()`: absolute-form,
  query/fragment, trailing slash).
- Mapping each neighbour's verdict onto an HTTP status line and a `{"error":...}` body.
- The `/ws` push task: coalescing state changes, keep-alive pings, full-state on a new client.
- The OTA upload streaming (`comfoair_http_ota.c`): recv retries, deadlines, draining refused
  bodies, the LED/phase indication, arming the restart.

Does NOT do:
- Decide access: `comfoair_policy` (`comfoair_http_check()`, `comfoair_http_policy.h`).
- Parse credentials or compare passwords: `comfoair_auth` parses; `comfoair_net_check_password()`
  compares. The stored hash never reaches this component.
- Parse or render bodies: `comfoair_wire` (config, provision/status, scan, entity write, op),
  `comfoair_api` (state, entities, value, push delta, JSON escaping), `comfoair_sysinfo`
  (`/api/system`), `comfoair_probe_report` via `comfoair_probe_task` (`/api/probe`), `comfoair_log`.
- Validate config values: `comfoair_config_apply()` via `comfoair_net_config_apply()`.
- Build commands or touch the bus: `comfoair_command` builds, `comfoair_control` queues and sends.
- Judge an OTA image: `comfoair_ota` (length, header, app descriptor, status lines, messages);
  the probation clock and delayed restart are `comfoair_ota_runtime`'s.
- Know the captive-portal probe paths: `comfoair_captive_answer()`.

## Public API (`include/comfoair_http.h`)

| Symbol | Contract |
| --- | --- |
| `comfoair_http_config_t { uint16_t port; }` | Server configuration; only the port. |
| `COMFOAIR_HTTP_DEFAULT_CONFIG()` | `{.port = 80}`. |
| `esp_err_t comfoair_http_start(const comfoair_http_config_t *)` | Called once from `app_main`. `ESP_ERR_INVALID_STATE` if already running, `ESP_ERR_INVALID_ARG` on NULL, otherwise `httpd_start()`'s error (server left NULL). A route that fails to register is LOGGED, not fatal — it answers 404. A push task that fails to create is logged; REST still works. Returns `ESP_OK` once the server listens. The caller (`src/main.cpp`) treats a failure as `COMFOAIR_PHASE_FAULT_HTTP` and does not arm OTA confirmation. |
| `esp_err_t comfoair_http_stop(void)` | Deletes the push task, stops the server; `ESP_OK` if not running, else `httpd_stop()`'s result. No caller in the tree today. |
| `void comfoair_http_notify_state_changed(void)` | Any task, including the CAN receive path; non-blocking (sets a flag, `xTaskNotifyGive`). Safe before start (no-op on a NULL task). Also registered as the MQTT observer (`comfoair_mqtt_set_observer`) in `src/main.cpp`. |

The header re-exports `comfoair_http_policy.h` (from `comfoair_policy`). There is deliberately no
`comfoair_http_set_provisioned()`: "claimed" is asked of `comfoair_net_is_provisioned()` on every
request, because a cached copy lags by exactly the window in which an open access point is still on
the air after credentials were saved.

Private seam, `comfoair_http_internal.h` (beside the sources, not in `include/`, so no other
component can answer a request past the policy): `comfoair_http_allowed()`,
`comfoair_http_request_path()`, `comfoair_http_this_device()`, `comfoair_http_read_body()`,
`comfoair_http_send_error()`, `comfoair_http_send_json()`, `comfoair_http_send_wire_error()`, every
route handler the route table names, `comfoair_http_ws_task()`, `comfoair_http_ota_preflight()`,
the three shared variables (`g_http_server`, `g_http_scratch`, `g_http_ws_task`; `g_http_*` after
`comfoair_mqtt`'s `g_` rule, with the component infix so the two never collide),
`COMFOAIR_HTTP_BODY_MAX` (2048 — a config document is a few hundred bytes), `MAX_WS_CLIENTS` (7 —
sized against `max_open_sockets`) and `COMFOAIR_HTTP_OTA_VERSION_CAP` (33 —
`esp_app_desc_t.version[32]` + NUL). Every function in it carries the `comfoair_http_` prefix and
every variable the `g_http_` prefix, because dropping `static` made them link-visible; the macros
are private to this component's sources and keep their names.

## Routes (registration order matters — first match wins)

| Route | Handler | Notes |
| --- | --- | --- |
| `GET /api/state`, `/api/entities`, `/api/entities/*` | `comfoair_api_render_*` | unknown key 404; known key with no reading is 200 + null |
| `GET /api/log` | `comfoair_log_render` | 500 if it does not fit |
| `GET /api/status` | `comfoair_wire_render_provision` | adds MQTT stats, phase, RSSI, identity |
| `GET`/`POST /api/config` | `comfoair_wire_render_config` / `_parse_config` | POST answers `{"saved":true}` BEFORE reconfiguring MQTT and renaming mDNS |
| `GET /api/wifi/scan` | `comfoair_wire_render_scan` | 503 if scan unavailable |
| `POST /api/provision` | `comfoair_net_provision_body` | answered first; MQTT block NULL (never zeros) |
| `POST /api/entities/*`, `/api/ops/*` | `comfoair_command_from_*` → `comfoair_control_submit` | `{"accepted":true}`; 503 busy / not running |
| `GET`/`POST /api/probe` | `comfoair_http_probe.c` | GET streamed chunked; POST 202, empty body = defaults |
| `GET /api/system` | `comfoair_http_system.c` | own 17 KiB static buffer |
| `POST /api/ota` | `comfoair_http_ota.c` | raw octet-stream body |
| `/ws` (websocket) | `comfoair_http_ws_get` | data frames drained and dropped; plain GET → 426 |
| `POST/PUT/DELETE/PATCH /*` | `comfoair_http_write_gate` | policy applied, then 501 |
| `GET /*` | `comfoair_http_captive_probe_get` → `asset_get` | probe answer, else asset, else `/api/*` 404, else SPA shell |

## Implementation

Files (one responsibility each; whatever crosses a file is declared in
`comfoair_http_internal.h`):
- `comfoair_http.c` — the lifecycle and the route table (`comfoair_http_start` /
  `comfoair_http_stop`, the server's tuning, the socket-budget `_Static_assert`s that
  `tests/python/test_socket_budget.py` reads here), the three shared variables, and the helpers
  every handler answers through (`comfoair_http_send_json`, `_read_body`, `_send_error`).
- `comfoair_http_gate.c` — the access check (`comfoair_http_allowed`), path normalisation
  (`comfoair_http_request_path`) and the device identity the documents report
  (`comfoair_http_this_device`): the one door every request passes, kept apart from what is behind
  it.
- `comfoair_http_ws.c` — the `/ws` route and the push task (`ws_send_state`, the keep-alive and the
  coalescing floor, `comfoair_http_notify_state_changed`): the only code that runs off a request.
- `comfoair_http_read.c` — the read-only documents: state, entities, one entity, the log, and
  `/api/status` (the one place the MQTT counters are mapped into the wire's shape).
- `comfoair_http_config.c` — provisioning and settings: the Wi-Fi scan, `POST /api/provision`,
  `GET`/`POST /api/config`, and `comfoair_http_send_wire_error` (the refused field), which the
  command handlers share.
- `comfoair_http_command.c` — `POST /api/entities/*` and `/api/ops/*`: build a command, queue it,
  answer "accepted"; the duration write and the optimistic select echo.
- `comfoair_http_assets.c` — the catch-alls: the write gate (policy, then 501), the captive-portal
  probe answers, and the embedded SPA with its `/api/*` 404.
- `comfoair_http_probe.c` — `GET`/`POST /api/probe`.
- `comfoair_http_system.c` — `GET /api/system` gathering into `comfoair_sysinfo_t`.
- `comfoair_http_ota.c` — `POST /api/ota` streaming into the next OTA slot.
- `comfoair_http_ota_preflight.c` — header + app-descriptor check against the RUNNING chip/project.
- `comfoair_http_internal.h` — the private seam above.
- `comfoair_http_sockets.h` — the lwIP socket budget: httpd's 3 internal sockets, every other
  socket user in the firmware (MQTT, captive DNS) and one spare. A `_Static_assert` beside
  `cfg.max_open_sockets` checks `7 + 3 + 2 + 1 <= CONFIG_LWIP_MAX_SOCKETS` (13,
  `sdkconfig.defaults`). The 7 is `COMFOAIR_HTTP_CLIENT_SOCKETS`, defined once there and used by
  `cfg.max_open_sockets` and the `MAX_WS_CLIENTS` check.

Access check (`comfoair_http_allowed`, `comfoair_http_gate.c`): notes the client to `comfoair_net`
(extends the setup window), reads `Authorization` into a 512-byte buffer, calls
`comfoair_auth_check()` with a verifier that delegates to `comfoair_net_check_password()`, builds
the ctx with `authenticated = (result == COMFOAIR_AUTH_GRANTED)` only, sleeps `auth.delay_ms` on a
wrong password, then asks `comfoair_http_check()`. Every handler calls it first and returns `ESP_OK`
on false (the reply is already sent). `comfoair_http_captive_probe_get` is the one route that
answers a probe path without it (it falls through to `asset_get`, which checks).

Push channel (`comfoair_http_ws.c`): `comfoair_http_ws_task` (stack 4096, prio 4) wakes on a
notification or every 15 s (`WS_KEEPALIVE_MS`), queues `ws_send_state` onto the server's own context
via `httpd_queue_work` (sockets may only be written from there), then sleeps 250 ms
(`WS_MIN_INTERVAL_MS`) so a PDO sweep costs one document. `ws_send_state` asks the server for its
client list (`httpd_get_client_list`), counts WebSockets, notes a client to `comfoair_net` only if
there is at least one, sends a full document when the count grew, a delta otherwise, and a
`{"type":"ping"}` when the delta is empty and 15 s have passed.

OTA (`comfoair_http_ota.c`): refusals that need no flash run first (probation, no slot, length,
then the 0x70-byte preflight read into the first chunk); only then `indicate(true)` and
`esp_ota_begin(OTA_WITH_SEQUENTIAL_WRITES)`; 4096-byte heap chunks (`OTA_CHUNK`, one sector);
`esp_ota_end` + `esp_ota_set_boot_partition`; answer; `comfoair_ota_runtime_restart_later(1000)`.
Timeouts: 3 recv retries per silence (each 5 s), a whole-upload deadline of
max(120 s, size at 8 KiB/s), a refusal drain bounded by 16 MiB and 30 s.

Concurrency: `esp_http_server` runs every handler on ONE task. That is the only reason the static
buffers (`g_http_scratch` 48 KiB (`COMFOAIR_API_DOC_BUFFER_SIZE`, `comfoair_api.h`), `s_doc` 17 KiB,
`s_sys`, the config patch storage, the Wi-Fi scan array) and `s_busy` need no lock.
`comfoair_http_notify_state_changed()` is the only entry point used from other tasks.

DO NOT:
- Register a route after the `/*` wildcards — it will never match. POSTs go before
  `comfoair_http_write_gate`, GETs before `comfoair_http_captive_probe_get`.
- Hard-code `max_uri_handlers`; it is derived from the route array. Exceeding it aborted every boot.
- `ESP_ERROR_CHECK` route registration — a device with one missing route can be reached and fixed;
  one that aborts in `app_main` cannot.
- Shrink `MAX_WS_CLIENTS` below `max_open_sockets` (7): `httpd_get_client_list()` fails outright
  (does not truncate) when the array is too small, and the push channel goes silent for everyone.
- Open a new socket anywhere in the firmware without counting it in `comfoair_http_sockets.h`.
  httpd's own start-up check (`max_open_sockets + 3 <= LWIP_MAX_SOCKETS`) reserves nothing for
  other users; with 10 in the table the MQTT socket made the seventh browser connection fail in
  `accept()` with ENFILE (23) before the LRU purge could run, and the web UI stopped answering.
- Hoist `comfoair_net_note_client()` out of the `websockets > 0` test — the setup window would never
  close before its one-hour ceiling.
- Lower `stack_size` (8192) or `max_req_hdr_len` (2048) without a measurement: POST `/api/config`
  is the deepest stack, and real browser headers overrun 1024 (seen as 431 on the settings page).
- Map `COMFOAIR_AUTH_NO_PASSWORD` or the parse enum's `COMFOAIR_AUTH_OK` to `authenticated`.
- Format client input into `comfoair_http_send_error()` — messages must be literals; error bodies
  can reach the log ring that `GET /api/log` serves.
- Reply after side effects in `comfoair_http_config_post` / `comfoair_http_provision_post`: they
  answer first because the radio may move (provision) and a slow follow-up would block the one
  httpd task.
- Touch the bus from a handler. Commands are queued (`comfoair_control_submit`), the probe walk is
  queued (`comfoair_probe_task_start`).
- Send a zero-length chunk mid-stream in `/api/probe` (it terminates the response), or terminate a
  half-sent report: return `ESP_FAIL` so the socket closes.
- Swap the preflight order (header before app descriptor), clear the OTA indication after a
  success, or clear it on the BUSY refusal (an installed image is waiting for its restart).
- Re-arm the OTA deadline per recv, or raise `recv_wait_timeout` instead of retrying.
- Add `Access-Control-Allow-Origin` — the UI is same-origin; `*` lets any page read the device.
- Extend the optimistic `comfoair_state_set_option()` after a select write to `away_mode`: the device
  requests PDO 337 about 1.5 s after the command and its bit 11 confirms it, and showing AWAY for a
  refused command is worse than showing it late.

## Dependencies

REQUIRES (all public, `CMakeLists.txt`): `comfoair_wire comfoair_mqtt comfoair_command
comfoair_control esp_http_server comfoair_api comfoair_policy comfoair_state comfoair_log
comfoair_auth comfoair_captive comfoair_config comfoair_mdns comfoair_net comfoair_phase_monitor
web_assets comfoair_json comfoair_probe_task comfoair_probe_report comfoair_sysinfo comfoair_can
board esp_app_format app_update spi_flash esp_timer comfoair_ota comfoair_ota_runtime comfoair_led`.
No PRIV_REQUIRES.

Depended on by: `src/` only (`src/CMakeLists.txt`; `src/main.cpp` starts it and calls
`comfoair_http_notify_state_changed()` from the CAN receive path and as the MQTT observer). Because
this component depends on `comfoair_mqtt`, MQTT reaches back through that callback, never a call.

## Tests

No host suite builds this component — it needs `esp_http_server`, which `[env:native]` does not
have. The decisions it relies on are tested in their own suites: `test/test_http_policy` (the
access policy), `test/test_auth`, `test/test_wire`, `test/test_wire_status` (config, provision,
scan, status documents), `test/test_api`, `test/test_captive`, `test/test_command`,
`test/test_ota` (image checks), `test/test_sysinfo` (the 17 KiB bound), `test/test_probe_report`,
`test/test_log`, `test/test_provision_setup_window` (the `note_client` contract the push loop uses).
`tests/python/test_socket_budget.py` checks the socket budget without a board build and fails when
a source file opens a socket that `comfoair_http_sockets.h` does not name.

NOT covered by any test: route order, `comfoair_http_request_path()`, status-code mapping, the push
task's coalescing/keep-alive/new-client logic, body reading, the OTA streaming/drain/deadline logic,
and the answer-first ordering. These need a device (`docs/hardware-verification/`).

## Notes

- `/ws` handshake is NOT access-checked: `esp_http_server` answers the upgrade itself and never
  calls the handler for it. Once a UI password is set, `GET /api/state` is 401 but a WebSocket can
  still receive the state. The intended fix is a ticket fetched over authenticated REST and passed
  as a query parameter (`web/src/api/ws.ts` says the same); it does not exist yet.
- The wrong-password delay is a `vTaskDelay` on the single httpd task, so it stalls every socket
  for its duration. A delay rather than a lockout, because a lockout is a denial of service against
  the owner, who is the most likely person to mistype.
- A duration write stores locally and produces no frame; it is mirrored into the state model
  (`comfoair_state_set_number`) because no PDO reports it. It answers 503 when the command sender
  never started (no CAN bus) rather than claiming success.
- Entity/op writes answer "accepted", not "applied": RMI is fire-and-forget, and the only
  confirmation is the unit's PDO arriving on `/ws`.
- Captive probe paths get their exact expected answer (e.g. 204) instead of the SPA shell, because
  a phone that sees HTML there decides the network has no internet and drops back to mobile data.
- Unknown `/api/*` paths get a JSON 404, not the SPA shell — "endpoint returned HTML" is much harder
  to diagnose than "no such endpoint".
