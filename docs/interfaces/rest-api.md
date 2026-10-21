# REST API

The device serves a small JSON API over plain HTTP on port 80. It is the same API the built-in
web UI uses — there is no private endpoint (see [Web UI](/interfaces/web-ui)) — and its ventilation
writes are exactly the ones MQTT offers (see [MQTT](/interfaces/mqtt)); configuration,
provisioning, the probe and firmware upload are REST only.

Every route is registered in one table in `comfoair_http_start()`
(`components/comfoair_http/comfoair_http.c`). Handlers live in `comfoair_http_read.c`,
`comfoair_http_config.c`, `comfoair_http_command.c`, `comfoair_http_ws.c`, `comfoair_http_assets.c`,
`comfoair_http_probe.c`, `comfoair_http_system.c` and `comfoair_http_ota.c` (whose image checks
are in `comfoair_http_ota_preflight.c`). Every handler calls
the one access check, `comfoair_http_allowed()`, before it does anything else — with two
exceptions: the captive-portal probe paths (`/generate_204`, `/hotspot-detect.html` and the like)
are answered with fixed strings before the check, and the WebSocket upgrade on `/ws` is answered by
`esp_http_server` itself, so `/ws` stays open even with a password set (see
[Security](/architecture/security#who-may-do-what)).

::: tip Conventions on this page
`DEVICE` in the examples is the device's address — its IP, or its mDNS name
`comfoair-xxxx.local`. Every JSON response is `Content-Type: application/json`. No
`Access-Control-Allow-Origin` header is ever sent: the UI is served from the device itself, so
there is no cross-origin case, and a page on another origin cannot read the device's state.
:::

## Route table

| Method | Path | Handler | What it does |
| --- | --- | --- | --- |
| GET | `/api/state` | `comfoair_http_state_get` | Every entity's current value, one flat object |
| GET | `/api/entities` | `comfoair_http_entities_get` | The entity registry with metadata |
| GET | `/api/entities/{key}` | `comfoair_http_entity_get` | One entity's value and display metadata |
| POST | `/api/entities/{key}` | `comfoair_http_entity_post` | Write one entity |
| POST | `/api/ops/{name}` | `comfoair_http_operation_post` | Run one of the six parameterised operations |
| GET | `/api/log` | `comfoair_http_log_get` | The device's ring log |
| GET | `/api/status` | `comfoair_http_status_get` | Network, provisioning, identity, aggregate phase, broker counters |
| GET | `/api/config` | `comfoair_http_config_get` | Stored settings, secrets redacted |
| POST | `/api/config` | `comfoair_http_config_post` | Change stored settings |
| GET | `/api/wifi/scan` | `comfoair_http_wifi_scan_get` | Networks in radio range |
| POST | `/api/provision` | `comfoair_http_provision_post` | Hand the device Wi-Fi credentials |
| GET | `/api/probe` | `comfoair_http_probe_get` | The RMI probe's report (streamed) |
| POST | `/api/probe` | `comfoair_http_probe_post` | Start a read-only RMI walk |
| GET | `/api/system` | `comfoair_http_system_get` | Unit identity, device, Wi-Fi, CAN, command and MQTT counters |
| POST | `/api/ota` | `comfoair_http_ota_post` | Upload a firmware image |
| GET | `/ws` | `comfoair_http_ws_get` | The push channel — see [WebSocket](/interfaces/websocket) |
| POST, PUT, DELETE, PATCH | `/*` | `comfoair_http_write_gate` | Catch-all for writes, see [below](#the-write-gate-and-the-fallbacks) |
| GET | `/*` | `comfoair_http_captive_probe_get` | Captive-portal probes, then the SPA assets and shell |

`esp_http_server` matches routes in registration order with wildcard matching
(`httpd_uri_match_wildcard`), which is why every concrete POST is registered before the `/*`
write gate and every concrete GET before the `/*` GET fallback.

The query string, a `#fragment`, an absolute-form `http://host` prefix and a trailing slash (on
anything but `/`) are stripped before the path is matched against the access policy and the
handlers' own comparisons (`comfoair_http_request_path()`), so
`/api/config/` and `/api/config?x=1` are judged as `/api/config`.

## Authentication and access

Access is decided by one pure function, `comfoair_http_check()` in
`components/comfoair_policy/comfoair_http_policy.c`, from three facts: the method, the path, and a
context of `provisioned`, `password_set` and `authenticated`.

**Authentication is HTTP Basic**, realm `comfoair`. There are no accounts: the user name is
ignored, but the decoded credential must contain a colon (`anything:password`). The password is
the owner's UI password, set through `POST /api/config` as `ui_password`; only a hash of it is
stored. Only `COMFOAIR_AUTH_GRANTED` counts as authenticated.

| Request | No UI password set | UI password set |
| --- | --- | --- |
| Any GET | allowed | needs auth, else `401` |
| POST `/api/config`, device provisioned | allowed | needs auth |
| POST `/api/entities/{key}`, `/api/ops/{name}`, device provisioned | allowed | needs auth |
| POST `/api/probe` (exact path), device provisioned | allowed | needs auth |
| POST `/api/provision`, device **provisioned** | `403` | needs auth |
| POST `/api/provision`, device **unprovisioned** (setup access point) | allowed | needs auth |
| Any other POST, device provisioned | `403` | needs auth |
| Any other POST (config, commands and probe included), device unprovisioned | `403` | `403` |
| PUT, DELETE, PATCH, anything else | `403` | `403` |

Notes on the table:

- **Reading is open until a password exists.** Writing is refused until one exists, with three
  exemptions: setting the first password (`/api/config`), controlling the ventilation
  (`/api/entities/*`, `/api/ops/*`) and starting the read-only probe (`/api/probe`). The command
  exemption means anybody on the LAN can change the ventilation on a device without a password —
  the same as anybody who can reach the unit's own front panel. OTA, provisioning a provisioned
  device, and every unknown write stay closed.
- **Unprovisioned** means "no Wi-Fi credentials stored, or an open setup access point on the air"
  (`comfoair_prov_is_provisioned()`): stored credentials alone do not make the device claimed
  while the open access point is up. On it, the one write
  accepted is `POST /api/provision` — and once a password exists, even that needs it, so a
  neighbour cannot re-home an owned device whose router has died.
- **`401` and `403` are different answers.** `401` carries `WWW-Authenticate: Basic
  realm="comfoair"` and body `{"error":"unauthorized"}`: a password exists and was not presented.
  `403` with `{"error":"forbidden"}` means no credential could make the request acceptable in
  this state.
- **A wrong password costs one second.** When a password is set and a wrong or malformed
  `Authorization` header arrives, the handler sleeps `COMFOAIR_AUTH_FAIL_DELAY_MS` (1000 ms)
  before answering. There is no lockout. Because one task serves every socket, the delay is
  global — every other request waits it out too. Do not retry a `401` automatically.
- The prefix rules (`path_under()` in the policy) require a trailing slash and something after
  it, so `/api/entities-x` does not inherit the `/api/entities/` exemption, and bare `/api/ops`
  is not a command.

```sh
# Read, password set
curl -u :hunter2hunter2 http://DEVICE/api/state

# Same thing, wrong password: 401 after a one-second delay
curl -i -u :wrong http://DEVICE/api/state
```

See [Security](/architecture/security) for the threat model behind these rules.

### Write-only secrets and the read sentinel

Three fields are secrets: `wifi_psk`, `mqtt_password` and `ui_password`. **No API projection ever
returns a stored secret.** `GET /api/config` renders each one as:

| Stored | Reads back as |
| --- | --- |
| something | `"__UNCHANGED__"` (`COMFOAIR_SECRET_SENTINEL`, `comfoair_secrets.h`) |
| nothing | `""` |

On the way back in (`comfoair_secret_decide()`), each secret field has four outcomes:

| Submitted | Effect |
| --- | --- |
| key absent | keep what is stored |
| `"__UNCHANGED__"` while something is stored | keep — the page echoed the sentinel back |
| `"__UNCHANGED__"` while nothing is stored | **refused** — the sentinel is a published string and must never become a password |
| `""` | clear the stored value |
| anything else | store it |

This is what lets a settings form load the whole document and post it back unchanged without
wiping credentials. An empty secret on a device that has none stored is a no-op. For the other
string fields of `POST /api/config` an absent key also keeps the stored value, but `""` is not
uniformly "clear": `mqtt_host` and `mqtt_user` accept it (no broker, no user), while
`topic_prefix` and `device_name` refuse it with `422`. Error messages never quote a submitted
value: they name the field. That matters because `GET /api/log` serves the log ring, which is world-readable on an
open setup access point.

## Error bodies

Every error is JSON with one `error` string, sometimes with a `field`:

```json
{"error": "the broker port must be between 1 and 65535", "field": "mqtt-port"}
```

| Source | Shape | Example |
| --- | --- | --- |
| Access policy | `{"error":"unauthorized"}` / `{"error":"forbidden"}` | |
| Body reader (`comfoair_http_read_body`) | `413` `{"error":"body too large"}`, `400` `{"error":"truncated body"}` | an empty body is also `413` |
| Body decoder (`comfoair_wire`) | `400` `{"error":"The request body is not a JSON object this device can read."}` for a body that is not one flat JSON object (a nested value, a duplicate key and trailing text are refused too); `422` with `field` for a bad field | `{"error":"A field is missing, of the wrong type, or longer than this device can store.","field":"value"}` |
| Config validator (`comfoair_config_strerror`) | `422` (or `409` for read-only) with `field` | `field` is a hyphenated name on `POST /api/config`, see below |
| Command builder (`comfoair_command_strerror`) | `404` or `422` | `{"error":"No such operation."}` |
| Handler literals (`comfoair_http_send_error`) | a fixed sentence, never a submitted value | `{"error":"the bus is busy"}`; each one is listed under its endpoint below |
| Renderer overflow | `500` `{"error":"<document> does not fit"}` | the rendered document outgrew its buffer — a defect, not a client error; listed under each endpoint that can return it |

Request bodies are capped at 2047 bytes on the JSON routes and 127 bytes on `/api/probe`: the
buffers are `COMFOAIR_HTTP_BODY_MAX` (2048) and 128 bytes, and a body is refused when its length
reaches the buffer size, keeping one byte for the terminator.

::: warning Two spellings of `field`
A decoder error (`COMFOAIR_WIRE_BAD_FIELD`) names the JSON key, e.g. `"field":"mqtt_port"`, and so
does the Wi-Fi refusal on `POST /api/config` (`"field":"wifi_ssid"`). A validator refusal on
`POST /api/config` names the check, from `config_error_name()` in `comfoair_http_config.c`:
`ssid`, `psk`, `wifi-pair`, `mqtt-host`, `mqtt-port`, `mqtt-user`, `mqtt-password`,
`topic-prefix`, `device-name`, `ui-password`, `no-hash`, `read-only`, `no-document`.
A validator refusal on `POST /api/provision` names the JSON key instead (`wifi_ssid` or
`wifi_psk`), because `comfoair_wire_parse_provision()` sets it.
:::

## State and entities

The entity list is generated from `tools/pdo_table.py`; it is not repeated here. See
[Entities](/reference/entities) for every key, kind, unit and option.

### GET /api/state

One object with every registry entity, in registry order, entity key to value, rendered by
`comfoair_api_render_state()` under one lock so it is a single snapshot. A value is a number
(printed with `%g`), a boolean, a string, or `null` when the entity exists but has no reading. The
`fan` entity is the one object-valued entry. An excerpt:

```json
{
  "next_fan_change_in_raw": null,
  "supply_fan_duty": 34,
  "bypass_mode": "Auto",
  "boost_duration": 60,
  "away_mode": false,
  "fan": {"on": true, "speed": 2, "preset": "Medium"}
}
```

`fan` is `null` until the unit has reported its fan level (PDO 65); `speed` is `1`..`3` (the Away
level reads as `on: false`, `speed: 1`), and `preset` is `null` when the operating mode maps to no
preset.

`null` here is `None` on the matching MQTT state topic (see [MQTT](/interfaces/mqtt#state-topics));
a `null` `fan` is `None` on all three of its leaves.

The unit's active errors are two keys read over RMI, not from a PDO: `unit_error` (a boolean, `true`
while an error that is not merely informational is active) and `active_errors` (the error texts in
one line of at most 255 characters, `"No errors"` when none is active). Both are `null` until the
unit has answered an errors read and its firmware word is known; the ids behind them are in
`GET /api/system` `unit.errors`. See [Errors and alarms](/unit/areas/errors-and-alarms).

`GET /api/state` is **not** rate-limited: it returns what the unit last said, exactly. Only MQTT
holds back jitter (see [MQTT, Publication rate](/interfaces/mqtt#publication-rate)).

### GET /api/entities

A JSON array, one element per entity (`comfoair_api_render_entities()`):

```json
[
  {
    "key": "bypass_mode", "name": "Bypass Mode", "kind": "select", "pdo": 66,
    "min": null, "max": null, "unit": null, "deviceClass": null, "stateClass": null,
    "icon": "mdi:valve", "entityCategory": null, "writable": true,
    "enabledByDefault": true, "options": ["Auto", "Open", "Closed"]
  }
]
```

Every element carries the same fourteen members in this order. `kind` is one of `numeric`,
`binary`, `enum`, `special`, `select`, `number`, `switch`, `fan`, `diagnostic_const`,
`freshness`. `pdo` is `null` for an entity with no PDO of its own; `min`, `max`, `unit`,
`deviceClass`, `stateClass`, `icon`, `entityCategory` and `options` are `null` when the registry
gives none. `enabledByDefault` mirrors discovery's `en`: `false` for a row Home Assistant creates
disabled. The JSON field names are camelCase here (`deviceClass`,
`stateClass`, `entityCategory`) while the rest of the API is snake_case.

### GET /api/entities/{key}

```json
{"key": "boost_duration", "name": "Boost Duration", "unit": "min", "value": 60}
```

Rendered by `comfoair_api_render_value()`: `key`, `name`, `unit` (or `null`) and `value`, which has
the same spelling as in `GET /api/state`. `404 {"error":"unknown entity"}` for a key the registry
does not carry. A known entity with no
reading is `200` with `"value": null` — a client can tell a typo from a silent bus.

### POST /api/entities/{key}

Body: `{"value": ...}`. The value must be a **string, a boolean or a non-negative whole number**;
`null`, a fraction, a negative or a missing `value` is refused with `422` and `"field":"value"`. A
string longer than 63 bytes is refused rather than truncated. Thirteen keys are writable
(`comfoair_command_knows_entity()`): the seven selects, `away_mode`, `fan` and the four durations.
The write shape is not the read shape:

| Entity kind | Value to send |
| --- | --- |
| `select` | the option **label**, exactly as in `options` (`"Open"`), never an index |
| `switch` (`away_mode`) | `true` / `false` |
| `number` (the four `*_duration` entities) | a whole number of minutes inside the registry's `min`..`max` (1..1440, `away_duration` 0..43200) |
| `fan` | `false` = the Away level (an HRV never stops); `true` = resume the level the unit reports while on, else Low; a number `0`..`3` (0 is Away); or one of the six preset labels `Auto`, `Away`, `Low`, `Medium`, `High`, `Boost` |

```sh
curl -X POST http://DEVICE/api/entities/bypass_mode \
     -H 'Content-Type: application/json' -d '{"value":"Open"}'
# {"accepted":true}
```

| Status | Body | When |
| --- | --- | --- |
| 200 | `{"accepted":true}` | queued for the bus, or a duration stored |
| 400 | wire error | body not a JSON object |
| 404 | `{"error":"No such writable entity."}` | unknown or read-only key |
| 404 | `{"error":"no such endpoint"}` | no key at all (`/api/entities/`); that path is not a command to the policy, so without a password it is `403` first |
| 413 / 400 | body reader errors | empty, oversized or truncated body |
| 422 | `{"error":"That value is not one this entity accepts."}` | wrong kind, unknown label, out of range |
| 503 | `{"error":"the bus is busy"}` | the command queue is full |
| 503 | `{"error":"commands are not running"}` | the command sender never started (no CAN bus on this boot) — for a duration too |

`accepted` means **queued, not applied**. Writes are fire-and-forget RMI frames; the only
confirmation is the unit's own PDO arriving afterwards on `/ws` (or the state topic over MQTT).
Two cases change the state model immediately:

- The four duration entities (`boost_duration`, `bypass_duration`, `balance_duration`,
  `away_duration`) produce **no frame at all**. The value is stored on the device, used by later
  commands, and written into the state model so every projection shows it.
- Every select written by label is set optimistically once the command is queued. Selects with a
  feedback PDO are corrected by it within seconds; the three with none (`temperature_passive`,
  `humidity_comfort`, `humidity_protection`) are read back over RMI only every 6 hours (every minute
  until a full read has completed), so without the echo they would show the pre-write value until
  then.

See [Commands](/protocol/commands) for the frames each write produces.

### POST /api/ops/{name}

The six parameterised operations. The body **is** the parameter object, not wrapped. Every listed
parameter is **required** — none has a default — and a missing one is `422` with
`{"error":"This operation has no optional parameters; one is missing."}`.

| Name | Parameters |
| --- | --- |
| `boost` | `duration` (seconds; `0` or negative cancels a running boost) |
| `set_bypass` | `mode`: `auto` \| `open` \| `closed`; `duration` (seconds) |
| `set_balance_mode` | `mode`: `balanced` \| `supply_only` \| `exhaust_only`; `duration` (seconds) |
| `set_ventilation_level` | `level`: `0`..`3`, where **0 is Away, not off** |
| `set_temperature_profile` | `profile`: `normal` \| `cool` \| `warm` |
| `set_away` | `enabled` (bool); `duration` (seconds) |

Durations are **seconds**, signed. For `set_bypass`, `set_balance_mode` and `set_away` a duration
of `0` or less means "until told otherwise"; for `boost` it means cancel. A fractional number is
refused, never rounded. Unknown keys in the body are ignored.

```sh
curl -X POST http://DEVICE/api/ops/set_bypass \
     -H 'Content-Type: application/json' -d '{"mode":"open","duration":3600}'
# {"accepted":true}

curl -X POST http://DEVICE/api/ops/boost -d '{"duration":0}'   # cancel boost
```

| Status | When |
| --- | --- |
| 200 `{"accepted":true}` | queued |
| 400 | body not a JSON object |
| 404 `{"error":"No such operation."}` | unknown name |
| 404 `{"error":"no such endpoint"}` | no name at all (`/api/ops/`); `403` first without a password, as for `/api/entities/` |
| 413 / 400 | empty, oversized or truncated body (an empty body is `413`, so `boost` with no body is refused before its parameters are read) |
| 422 | wrong parameter type (`"field"` names the key), missing parameter, value out of its set (`"That value is not one this entity accepts."`) |
| 503 | `the bus is busy` / `commands are not running` |

Mode and duration travel in one call on purpose: the modes are temporary on the unit, and splitting
them into "set mode, then set duration" is exactly what the operation exists to avoid. This is also
why the operations are not Home Assistant discovery entities.


## Status, log and system

### GET /api/status

Network and provisioning state, the device's aggregate phase, its identity and broker counters,
rendered by `comfoair_wire_render_provision()`:

```json
{
  "state": "connected", "mode": "station", "phase": "online",
  "provisioned": true, "has_credentials": true, "wifi_known_good": true, "connected": true,
  "wifi_ssid": "Kitchen", "ip": "192.168.1.42",
  "ap_ssid": "comfoair-8ae5", "ap_on_air": false,
  "setup_window_open": false, "setup_window_remaining_ms": null, "fallback_ap_in_ms": null,
  "uptime_ms": 86400000, "failure": "none", "failure_reason": 0, "rssi": -61,
  "device_id": "aabbccddeeff", "mac": "aa:bb:cc:dd:ee:ff", "sw_version": "1.0.0",
  "mqtt": {
    "configured": true, "connected": true, "published": 1234, "commands": 5,
    "rejected": 0, "reconnects": 1, "last_connack": 0, "last_connack_reason": "no refusal",
    "link": "online", "failure": "none", "attempts": 0, "retry_in_ms": null,
    "subscriptions": "none", "disconnects": 1, "applied_epoch": 2, "last_transport_errno": 0
  }
}
```

| Field | Values |
| --- | --- |
| `state` | `unconfigured`, `connecting`, `connected`, `trial`, `retrying`, `fallback-ap`, `window-closed` |
| `mode` | `off`, `access-point`, `station`, `ap-station` |
| `phase` | `boot`, `ota`, `trial`, `setup-ap`, `recovery-ap`, `fault`, `net-rejected`, `net-lost`, `setup-expired`, `connecting`, `broker-rejected`, `bus-silent`, `no-broker`, `online` |
| `failure` | `none`, `network-not-found`, `wrong-password`, `refused`, `no-address`; `failure_reason` is the raw Wi-Fi reason code |
| `*_ms` durations | `null` means "no end" / "not scheduled", never 0 |
| `rssi` | dBm, or `null` when the station is not associated |
| `device_id` | twelve lower-case hex digits of the STA MAC — **the identity** to key a record on |
| `mqtt.link` | `off`, `connecting`, `online`, `backoff`, `rejected` |
| `mqtt.failure` | `none`, `dns`, `tcp`, `unavailable`, `protocol`, `bad-credentials`, `dropped` |

`mqtt` is the last member; the MQTT page explains its [retry fields](/interfaces/mqtt#reconnect-and-backoff).
`mqtt.subscriptions` has no producer yet and always reads `none`. There is no
`GET /api/provision` route: this document is the provisioning status, and `GET /api/provision`
answers `404`. `500 {"error":"status does not fit"}` if the document cannot be rendered.

### GET /api/log

A JSON array of strings — the last 80 log lines (`COMFOAIR_LOG_LINES`), oldest first, each at most
159 characters (a `COMFOAIR_LOG_LINE_LEN` slot of 160 bytes; a longer line spills into the next
slot). Every byte outside printable ASCII is escaped as `\u00xx` (`append_escaped()` in
`comfoair_log.c`). `500 {"error":"log does not fit"}` if the ring cannot be rendered.

```json
["I (1234) http: listening on :80 (network)", "I (1302) mqtt: broker connected; state republished"]
```

### GET /api/system

Who the ventilation unit is, who this device is, and how the links are doing. Assembled in
`comfoair_http_system.c`, rendered by `comfoair_sysinfo_render()` (`components/comfoair_sysinfo`)
into one object with the members:

| Member | Fields |
| --- | --- |
| `unit` | identity read by the probe task: `state` (`ok`, `no_answer`, `unknown`), `age_ms`, `model`, `serial`, `firmware`, `firmware_raw`, `article`, `country`, `name`, `errors` (`{"state", "ids", "items": [{"id", "text"}]}`) |
| `device` | `firmware`, `idf`, `built`, `board`, `chip`, `chip_revision`, `flash_bytes`, `partition`, `uptime_ms`, `reset_reason`, `heap_free`, `heap_min_free`, `heap_largest_block`, `device_id`, `mac` |
| `network` | `connected`, `ssid`, `ip`, `rssi`, `channel` — every link field `null` while disconnected |
| `bus` | `started`, `state`, `rx_frames`, `rx_dropped`, `bus_errors`, `bus_recoveries`, `tx_error_count`, `rx_error_count`, `nodes_seen` (array) |
| `commands` | `queued`, `sent`, `failed`, `dropped` |
| `mqtt` | `configured`, `connected`, `published`, `commands`, `rejected`, `reconnects` |

No secret is in reach of this handler. `500 {"error":"system does not fit"}` is a defect. See
[OTA and system](/device/ota-and-system).

## Configuration and provisioning

### GET /api/config

```json
{
  "wifi_ssid": "Kitchen", "wifi_psk": "__UNCHANGED__",
  "mqtt_host": "192.168.1.10", "mqtt_port": 1883, "mqtt_user": "comfoair",
  "mqtt_password": "__UNCHANGED__", "topic_prefix": "comfoair/aabbccddeeff",
  "ha_discovery": true, "device_name": "ComfoAir Q 8ae4", "ui_password": "",
  "ui_password_set": false, "read_only": false, "wifi_known_good": true,
  "permanent_manual": true
}
```

`ui_password_set` is derived from the stored hash. `read_only` is true when the flash holds a
configuration schema newer than this build understands; every `POST /api/config` is then `409`
(`POST /api/provision` still works).
`wifi_known_good` is whether a rollback target exists. `permanent_manual` is owned by the command
layer, not the stored document: when true, a ventilation level change is paired with the
permanent-manual command so the unit does not drift back to its schedule. Defaults on a fresh
device: port 1883, `ha_discovery` true, prefix `comfoair/` plus `device_id`.
`500 {"error":"config does not fit"}` if the document cannot be rendered.

### POST /api/config

A partial document. Accepted keys: `mqtt_host`, `mqtt_port`, `mqtt_user`, `mqtt_password`,
`topic_prefix`, `ha_discovery`, `device_name`, `ui_password`, `permanent_manual`.

- Absent keeps, `""` clears, the sentinel keeps a stored secret (see above). `{}` is a valid save
  that changes nothing.
- Unknown keys are ignored, so a newer page can talk to an older firmware.
- `wifi_ssid` / `wifi_psk` are **refused by name** with `422` and
  `"The network is set through POST /api/provision, not here."` — echoing the whole GET document
  back hits this, so drop those two keys.
- Limits come from `comfoair_config_check_*()` in
  `components/comfoair_config/comfoair_config_check.c`:
  - `mqtt_port` 1..65535 (a JSON number);
  - `mqtt_host` up to 253 characters from letters, digits and `. - _ : [ ]` — so no scheme or
    path (`:` is allowed, so a `host:port` is not refused; the port belongs in `mqtt_port`);
    `""` means no broker;
  - `topic_prefix` 1..64 characters, not starting with `$` or `/`, not ending with `/`, no `//`,
    no spaces, `+`, `#` or control characters;
  - `mqtt_user` and `mqtt_password` up to 128 bytes, any content;
  - `device_name` 1..32 bytes, no control characters;
  - `ui_password` 8..128 characters, no control characters;
  - `ha_discovery` and `permanent_manual` must be JSON booleans.

```sh
curl -X POST http://DEVICE/api/config -H 'Content-Type: application/json' \
     -d '{"mqtt_host":"192.168.1.10","mqtt_user":"comfoair","mqtt_password":"s3cret"}'
# {"saved":true}
```

| Status | When |
| --- | --- |
| 200 `{"saved":true}` | stored |
| 400 | body not a JSON object |
| 409 | `read_only` (`"field":"read-only"`) |
| 413 / 400 | empty, oversized or truncated body |
| 422 | a field of the wrong type or too long for its buffer, a Wi-Fi key, the sentinel sent for a secret that is not stored, or a validator refusal |

The answer is sent **before** the side effects: every accepted save — even `{}` — asks the MQTT
task to rebuild the broker connection without a reboot (`comfoair_mqtt_reconfigure()`), and the
mDNS instance name follows the stored device name (the host name `comfoair-xxxx.local` does not
change).

### GET /api/wifi/scan

A top-level array, one row per BSSID (a mesh answers several times under one name; duplicates are
not filtered). `""` is a hidden network.

```json
[{"ssid": "Kitchen", "rssi": -58, "secure": true}, {"ssid": "", "rssi": -80, "secure": true}]
```

Rendered by `comfoair_wire_render_scan()`. `503 {"error":"scan unavailable"}` when the radio
cannot scan; `500 {"error":"scan does not fit"}` if the rows cannot be rendered.

### POST /api/provision

```json
{"wifi_ssid": "Kitchen", "wifi_psk": "hunter2hunter2"}
```

`wifi_ssid` is required. `wifi_psk` follows `comfoair_secret_decide()`: absent keeps the stored
key, `"__UNCHANGED__"` also keeps it when one is stored and is refused when none is, `""` means an
open network, anything else is the new key. The pair is validated (SSID 1..32 bytes, key empty,
8..63 characters, or exactly 64 hex digits).

The answer is the `/api/status` document **without** the `mqtt` block, sent before the device
starts connecting — the station moves to the router's channel, the access point follows, and the
phone that sent the request usually loses the connection a moment later. `400` for a body that is
not a JSON object; `413` / `400` from the body reader for an empty, oversized or truncated body;
`422` names the refused field by its JSON key; `503 {"error":"busy"}` when the provisioning task is
not running or the credentials could not be written to flash; `500 {"error":"status does not fit"}`
if the answer cannot be rendered (the credentials have been accepted by then). See
[Networking](/device/networking) for the trial and rollback that follow.

## Probe and firmware upload

### POST /api/probe, GET /api/probe

`POST` starts a read-only RMI walk of the unit's settings. The body is optional — an empty body
walks with the defaults `{"node": 1, "another_counter": 0}`. The body is read only when it is
non-empty, and a body of 128 bytes or more is the body reader's `413`.

| Status | Body | When |
| --- | --- | --- |
| 202 | `{"accepted":true}` | the walk started; `GET /api/probe` shows its progress |
| 400 | `{"error":"not a JSON object"}` | a non-empty body that is not a JSON object (`comfoair_json_check()`) |
| 413 / 400 | body reader errors | oversized or truncated body |
| 422 | `{"error":"node must be a whole number"}` | `node` present but not a whole number 0..255 |
| 422 | `{"error":"another_counter must be a whole number"}` | `another_counter` present but not a whole number 0..255 |
| 422 | `{"error":"node must be 1..63 and not this device; another_counter must be 0..3"}` | both are whole numbers, but the probe task refuses the pair |
| 409 | `{"error":"a probe is already running"}` | a walk is in progress |
| 503 | `{"error":"the probe is not running on this device (no CAN bus)"}` | the probe task never started (no CAN bus on this boot) |
| 500 | `{"error":"the probe did not start"}` | any other failure to start the walk |

`GET` streams the report (chunked transfer encoding) — the partial report with progress while the
walk runs. Before the first chunk has gone out, a failure is an ordinary error:
`503 {"error":"the probe is not running on this device (no CAN bus)"}` when the probe task never
started, `500 {"error":"report failed"}` for anything else. A failure after the first chunk closes
the socket rather than sending broken JSON. The document is described on [Probe](/protocol/probe).

### POST /api/ota

The raw `firmware.bin` as the request body (`application/octet-stream`), with a `Content-Length`.
Not multipart.

```sh
curl -u :hunter2hunter2 -X POST http://DEVICE/api/ota \
     -H 'Content-Type: application/octet-stream' \
     --data-binary @.pio/build/m5stack-nanoc6/firmware.bin
# {"ok":true,"rebooting":true,"partition":"ota_1","bytes":1310720,"version":"1.0.1"}
```

Every check that can refuse without touching flash runs first: length against the slot, the image
header's chip id, and the app descriptor's project name, so a wrong file never erases a sector.
The device restarts one second after answering.

| Status | Meaning |
| --- | --- |
| 200 | installed, restart armed |
| 400 | upload stopped before `Content-Length` bytes arrived, or exceeded its deadline |
| 409 | another upload is waiting for its restart, or the running image is still unconfirmed after an update |
| 411 | no `Content-Length` (or a length of 0) |
| 413 | larger than the update slot |
| 422 | too small, not an image, wrong chip, wrong project, failed verification |
| 500 | flash error, or no update slot |

Every refusal is `{"error":"<sentence>"}`, the sentence from `comfoair_ota_message()`.

OTA always needs a password: with none set it is `403`. See [OTA and system](/device/ota-and-system)
for the rollback and confirmation rules.

## The write gate and the fallbacks

### The `/*` write gate

POST, PUT, DELETE and PATCH on `/*` are routed to `comfoair_http_write_gate`, registered after every
real write route. It exists so that the access policy applies to **every** write, not only to paths
that happen to have a handler. PUT, DELETE and PATCH are always `403`. A POST to a path with no
handler is judged by the policy first (`401` or `403`); if the policy lets it through, the answer is
`501 {"error":"not implemented yet"}` — the request was not applied.

### The `/*` GET fallback and captive probes

The last route, `GET /*`, goes to `comfoair_http_captive_probe_get`:

1. **Captive-portal probes** answer first, from the table in
   `components/comfoair_captive/comfoair_captive_probe.c`, with `Cache-Control: no-store`:

   | Path | Answer | Asked by |
   | --- | --- | --- |
   | `/generate_204`, `/gen_204` | `204` empty | Android |
   | `/hotspot-detect.html` | `200` Apple "Success" HTML | iOS, macOS |
   | `/ncsi.txt` | `200` `Microsoft NCSI` | Windows 8 and earlier |
   | `/connecttest.txt` | `200` `Microsoft Connect Test` | Windows 10 and later |
   | `/success.txt`, `/canonical.html` | `200` Firefox bodies | Firefox |

   Matching is exact on the path; the `Host` header is ignored. These answers say "the network has
   internet", so a phone on the setup access point stays on it instead of switching to mobile data
   mid-form. They are answered in every mode and **without** the access check, and they count as
   client activity for the setup window.
2. Otherwise the request goes through the access check and the embedded assets: `/` (the SPA
   shell), `/app.js` and `/style.css`, all served pre-gzipped (`Content-Encoding: gzip`).
3. An unknown path under `/api/` is `404 {"error":"no such endpoint"}` — never the HTML shell.
4. Any other path returns the SPA shell with `200`, so a reload on a client-side route such as
   `/settings` works.

## Limits

| Limit | Value | Source |
| --- | --- | --- |
| Open sockets (HTTP and WebSocket together) | 7 | `cfg.max_open_sockets` |
| Request header size | 2048 bytes | `cfg.max_req_hdr_len` |
| Request body (JSON routes) | 2047 bytes | `COMFOAIR_HTTP_BODY_MAX` |
| Rendered document buffer | 48 KiB | `SCRATCH_SIZE` (`COMFOAIR_API_DOC_BUFFER_SIZE`) |
| Concurrency | one request at a time | `esp_http_server` serves every socket from one task |

Because one task serves everything, a slow client, an OTA upload or a wrong-password delay holds
every other request. Least-recently-used sockets are purged when the limit is reached
(`lru_purge_enable`).
