# comfoair_wire — pure decode/encode of the configuration, provisioning, status, entity-write and operation bodies

> This is documentation, not code — re-read the source before relying on a detail.

The HTTP handlers and the MQTT inbound path read bodies written by whoever is in radio range of
an **open** setup access point, and they serve documents that a browser parses. Deciding what a
body *means*, and what bytes a document *is*, inside a handler would mean it can only be
exercised by flashing a board. This component does both as pure functions over a NUL-terminated
buffer, so they build and are tested on the host. The handler next door is left with "read the
body, call this, send what it says".

It is separate from `comfoair_api` on purpose: `comfoair_api` projects the generated **state
model** (`tools/pdo_table.py`), while these documents are the **configuration surface** —
different source of truth, field names taken from `comfoair_config`'s `name` column, not entity
keys. The only thing they share is the escaper in `comfoair_json`.

## Responsibility

Owns:
- Decoding `POST /api/provision` (`wifi_ssid` / `wifi_psk`) into a validated `comfoair_wifi_t`.
- Decoding `POST /api/config` into a `comfoair_config_patch_t` plus the out-of-band
  `permanent_manual` flag; refusing the Wi-Fi keys there by name (`COMFOAIR_WIRE_WRONG_ROUTE`).
- Decoding `POST /api/entities/<key>` (`{"value": ...}`) into a typed `comfoair_command_value_t`.
- Decoding `POST /api/ops/<name>` (and the MQTT operation payload) into
  `comfoair_command_params_t`.
- Rendering `GET /api/config`, `GET /api/wifi/scan` and the provisioning/status document
  (`GET /api/provision`, `GET /api/status`, the `POST /api/provision` reply), including the
  nested `mqtt` broker block.
- The single spelling of provisioning state/mode words and of MQTT CONNACK refusal codes.
- One owner-facing error sentence per result (`comfoair_wire_strerror`), never quoting input.

Does NOT do:
- Socket I/O, status codes, the response itself — `comfoair_http` (and `comfoair_net` for the
  provisioning body). This component may never be able to send a response, or the access
  policy in `comfoair_policy` gains a second door.
- Value validation of config fields (port range, prefix, name…) — `comfoair_config_apply()`;
  this layer checks only types and lengths. The exception is provisioning, which runs
  `comfoair_config_check_ssid/psk` and `comfoair_config_wifi_usable` itself (see Notes).
- Deciding which parameters an operation needs or what values are legal — `comfoair_command`.
- Redacting secrets — `comfoair_config`'s public projection (`comfoair_config_public_t`) does it
  before the renderer ever sees the data.
- Spelling `failure`, `phase`, `link`, `failure` (broker), `subscriptions` — those words belong
  to `comfoair_provision` (`comfoair_prov_failure_name`), `comfoair_phase` (`comfoair_phase_name`)
  and `comfoair_mqtt_backoff` (`comfoair_mqtt_link_name/fail_name/subs_name`).
- Collecting the data: `comfoair_net` fills `comfoair_wire_provision_t`, `comfoair_http` maps
  `comfoair_mqtt_stats_t` into `comfoair_wire_mqtt_t` and fills `comfoair_wire_device_t`.

## Public API

All functions are pure, re-entrant, take no locks and allocate nothing; callable from any task
(in the firmware: httpd's task, and the MQTT publisher task for operations / CONNACK words).

| Symbol | Contract |
| --- | --- |
| `comfoair_wire_status_t` | `OK`, `BAD_BODY` (not a flat JSON object — distinct from `{}`, which is a legal no-op save), `BAD_FIELD` (`field` names which), `REFUSED` (`err` is the `comfoair_config_err_t`), `READ_ONLY` (flash holds a schema this build does not understand), `WRONG_ROUTE` (Wi-Fi key sent to `/api/config`). |
| `comfoair_wire_result_t` | `{status, err, field}`. `field` is a pointer to a **string literal** (the key name) or NULL — never the submitted value. |
| `comfoair_wire_strerror(result)` | One literal sentence, never NULL, no value inside; safe to log and to send. `REFUSED`/`READ_ONLY` delegate to `comfoair_config_strerror`. |
| `comfoair_wire_parse_provision(body, stored, out)` | `stored` may be NULL (never configured). `wifi_psk` is tri-state via `comfoair_secret_decide`: absent = KEEP stored key, `""` = CLEAR (open network), else STORE; the sentinel with nothing stored = REJECT → `REFUSED`/`ERR_PSK`. `wifi_ssid` is required. `*out` written **only** on `OK`, and only after the pair passed `comfoair_config_wifi_usable()`. |
| `comfoair_wire_patch_storage_t` | Backing storage for the patch's string pointers (~740 B: host 254, user 129, broker pass 129, prefix 65, name 33, UI pass 129). Holds passwords in the clear — never log it, never put it on a handler stack (`comfoair_http_config.c` uses a `static`). |
| `comfoair_wire_parse_config(body, out, storage, has_pm, pm)` | Types/lengths only. Absent key → NULL pointer / `has_*` false; `""` → empty string (clears). `wifi_ssid`/`wifi_psk` present → `WRONG_ROUTE`. Unknown and derived keys (`ui_password_set`, `read_only`, `wifi_known_good`) ignored. `*out` written only on `OK`; `*has_pm` is reset to false first. `has_pm`/`pm` may be NULL. |
| `comfoair_wire_render_config(pub, known_good, permanent_manual, out, cap)` | Takes the **public projection** only. Returns bytes written, or **0 on NULL or overflow — 0 is a defect the caller must turn into 500**, never a short body. `known_good` comes from the provisioning machine, `permanent_manual` from `comfoair_control`. |
| `comfoair_wire_network_t` | `{ssid[33], rssi (dBm), secure}`. `""` = hidden network, still reported. |
| `comfoair_wire_render_scan(list, count, out, cap)` | Bare top-level JSON array, possibly `[]`; duplicates (one per BSSID) not filtered. 0 on overflow. |
| `comfoair_wire_provision_t` | Snapshot filled by `comfoair_net_status()` under its lock (typedef'd there as `comfoair_net_status_t`). No PSK, ever. `has_rssi`/`rssi` and `phase` are NOT filled by `comfoair_net_status()` — the HTTP handler fills them (see DO NOT). `COMFOAIR_PROV_NEVER` (`UINT32_MAX`) in a duration renders as `null`. `uptime_ms` is 64-bit. |
| `comfoair_wire_state_name` / `comfoair_wire_mode_name` | Never NULL; out-of-range → `"unknown"`. |
| `comfoair_wire_connack_name(code)` | MQTT 3.1.1 §3.2.2.3: 0 `"no refusal"` (not "accepted"), 1–5 their meanings, else `"unknown refusal code"`. Never NULL. |
| `comfoair_wire_mqtt_t` | Flat copy of `comfoair_mqtt_stats_t` — counters only, no host/user/password. Caller-owned, may live on the stack; renderer keeps no pointer. Fields are a non-atomic snapshot. |
| `comfoair_wire_device_t` | Borrowed `device_id` (12 lower-case hex of STA MAC), `mac` (colon form), `sw_version`. |
| `comfoair_wire_render_provision(status, mqtt, device, out, cap)` | `status` required (NULL → 0). `mqtt == NULL` **omits** the `mqtt` member (not zeros); `device == NULL` omits the three identity keys. 0 on overflow → 500. |
| `comfoair_wire_parse_entity_write(body, out, text, cap)` | `value` must be a string (copied into `text`, `out->text` points there), `true/false`, or a non-negative whole u32 number (stored as `double`). Missing, `null`, negative, fraction, or string longer than `cap` → `BAD_FIELD("value")`. No default. |
| `comfoair_wire_op_storage_t` | `mode[16]`, `profile[16]` backing for the params' string pointers. |
| `comfoair_wire_parse_operation(body, out, storage)` | Reads `mode`, `profile` (strings), `duration`, `level` (signed i32), `enabled` (bool). Absent stays absent (`has_*` false / NULL) — no defaults. 0 and negative durations are legal values; a fraction is `BAD_FIELD`. `*out` written only on `OK`. |

## Implementation

Files:
- `include/comfoair_wire.h` — the whole contract (types, storage structs, rationale).
- `comfoair_wire.c` — the head comment with the two rules every file here obeys (no secret
  rendered, no submitted value quoted in an error) and `comfoair_wire_strerror`, the result
  type's one public function.
- `comfoair_wire_internal.h` — private: the bounded `writer_t` with its `put_*` / `finish`, and
  the result constructors (`ok`, `bad_body`, `bad_field`, `wrong_route`, `refused`), all
  `static inline` so the names stay short and none becomes a link-visible symbol.
- `comfoair_wire_parse.c` — the configuration parsers, `POST /api/provision` and
  `POST /api/config`; its own file because both decode a `comfoair_config` record.
- `comfoair_wire_render.c` — the `GET /api/config` and `GET /api/wifi/scan` renderers and the
  state / mode / CONNACK words, which the status document and the MQTT log line both print.
- `comfoair_wire_render_status.c` — the provisioning/status document and its nested `mqtt` block;
  its own file because it is the largest renderer and the one every new status member enters.
- `comfoair_wire_command.c` — the command parsers, `POST /api/entities/<key>` and
  `POST /api/ops/<name>` (also the MQTT operation payload), whose output is a `comfoair_command`
  value rather than a config patch.
- `CMakeLists.txt` — the five sources listed one by one, `REQUIRES` only.

Flow: every parser calls `comfoair_json_check(body)` first (flat object, no duplicates, no
nesting) and returns `BAD_BODY` if it fails, then reads each key with `comfoair_json_string /
_u32 / _i32 / _bool`, mapping `MISSING` → absent and any other non-`FOUND` answer →
`BAD_FIELD(key)`. Results are built in a local and copied to `*out` only at the end.

Renderers use the private bounded `writer_t` in `comfoair_wire_internal.h` (`put`, `put_u32`,
`put_u64`, `put_i32`, `put_bool`, `put_string`, `put_key`, `put_duration`, `finish`): any append
that does not fit sets `overflowed`, and `finish()` returns 0 so truncation is handled once, not
at forty call sites. `put_string` routes every string through `comfoair_json_escape` (an SSID is
chosen by a neighbour; `"},{"ssid":"` is a broadcastable name). `put_duration` renders
`COMFOAIR_PROV_NEVER` as `null`.

Status document order (top-level): `state, mode, phase, provisioned, has_credentials,
wifi_known_good, connected, wifi_ssid, ip, ap_ssid, ap_on_air, setup_window_open,
setup_window_remaining_ms, fallback_ap_in_ms, uptime_ms, failure, failure_reason, rssi`, then
optional `device_id, mac, sw_version`, then optional `mqtt{...}` **last**. Inside `mqtt`: the
original eight (`configured, connected, published, commands, rejected, reconnects, last_connack,
last_connack_reason`), then `link, failure, attempts, retry_in_ms, subscriptions, disconnects,
applied_epoch, last_transport_errno`.

Invariants:
- No value for which `comfoair_secret_key()` is true is ever rendered; the config renderer only
  accepts `comfoair_config_public_t`.
- No error ever quotes a submitted value; `field` is a literal key.
- On any non-`OK` result the caller's `out` is untouched (no half-decoded patch/credential).
- A render returning 0 is never served.

DO NOT:
- Add `esp_http_server`, `comfoair_http`, `comfoair_net`, `comfoair_mqtt` or any FreeRTOS/IDF
  component to `REQUIRES` — the component stops building on the host and the access policy gets
  a second door.
- Include `comfoair_mqtt.h` to reuse `comfoair_mqtt_stats_t`; keep the flat
  `comfoair_wire_mqtt_t` copy and let `comfoair_http` map it.
- Move the `mqtt` block earlier, or put a flat field after it: `comfoair_json_check()` stops at
  the first nested value, so a front-walking reader loses everything behind it.
- Insert a new `mqtt` member among the original eight; append after them
  (`test_the_new_fields_come_after_the_existing_eight` pins the order).
- Render an omitted `mqtt`/`device` as zeros or empty strings — absent and zero are opposite
  answers.
- Render `COMFOAIR_PROV_NEVER`, a missing RSSI, or no retry as `0`; they are `null`.
- Move RSSI collection into `comfoair_net_status()`: it takes the Wi-Fi stack's lock and
  `comfoair_phase_monitor` calls that snapshot once a second without needing it.
- Truncate an over-long string to fit — refuse it (a truncated credential or option label is a
  different, silently-wrong request).
- Give a CONNACK code a meaning here and not in `comfoair_phase_connack_is_refusal()` (and
  `comfoair_mqtt_backoff`), which read the same code space independently.
- Format `result.field` into the strerror sentence; the handler renders it as its own member.

## Dependencies

`REQUIRES` (public, because the header exposes their types): `comfoair_json`, `comfoair_config`,
`comfoair_provision`, `comfoair_secrets`, `comfoair_command`, `comfoair_phase`,
`comfoair_mqtt_backoff` — all host-buildable.

Depended on by: `comfoair_http` (all parsers except provision, all renderers, `strerror`),
`comfoair_net` (`comfoair_wire_parse_provision` in `comfoair_net_provision_body`; the
`comfoair_wire_provision_t` typedef as `comfoair_net_status_t`), `comfoair_mqtt_inbound`
(PRIV_REQUIRES; `comfoair_wire_parse_operation` for MQTT operation payloads), `comfoair_mqtt`
(`comfoair_wire_connack_name` in `comfoair_mqtt_event.c` log line), and `comfoair_phase_monitor`
(the snapshot type, via `comfoair_net`).

## Tests

Host suites under `[env:native]` (include path `components/comfoair_wire/include` in
`platformio.ini`):
- `test/test_wire/` — provisioning tri-state and sentinel, config patch absent/empty/wrong
  route/unknown keys/derived keys, no secret rendered, overflow → 0, escaping, scan array,
  status document basics, state/mode/connack words, entity write shapes, operation params, a
  POST-to-bus journey, `permanent_manual` round trip.
- `test/test_wire_status/` — `phase`, the mqtt block's added members and their order, `link`
  vs `state`, null retry, `uptime_ms`, `rssi`, worst case fits the 48 KiB HTTP scratch (`COMFOAIR_API_DOC_BUFFER_SIZE`, `comfoair_api.h`).
- `test/test_wire_epoch/` — `applied_epoch` value is carried exactly.
- `test/test_mqtt_facts/` — renders the status document for an unreachable broker
  (`disconnects`, `last_transport_errno`).

Not covered on the host: the handlers that call this (`comfoair_http`, `comfoair_net`,
`comfoair_mqtt_inbound` glue) and the mapping from `comfoair_mqtt_stats_t` to
`comfoair_wire_mqtt_t` in `comfoair_http_read.c` — a field dropped in that mapping stays green here.

## Notes

- **Why provisioning validates here but config does not.** `"__UNCHANGED__"` is 13 printable
  bytes and passes the PSK checker, so a handler writing it through would store credentials that
  cannot associate; credentials existing stops the setup AP from coming back, and the device
  disappears. So the sentinel is refused and the pair is checked with the same
  `comfoair_config_wifi_usable()` predicate NVS uses, giving one answer to "will the AP come
  back". Config fields go through `comfoair_config_apply()`, which validates once; asking twice
  in two places is how the two come to disagree.
- **Absent vs empty, everywhere.** Absent keeps the stored value, empty clears it; conflating
  them once let an unrelated save wipe broker credentials. `permanent_manual` follows the same
  rule so a page that does not know about it cannot switch it off by saving.
- **Wi-Fi keys at `/api/config` are refused, not ignored**: `GET /api/config` carries
  `wifi_ssid`, so a client echoing the document back is reasonable, and silently dropping a
  credential would report success over a network never stored. The message names the route that
  works.
- **Entity values keep their JSON shape** (string / bool / number) instead of being coerced: a
  select written with a number would be an index whose meaning depends on the build.
- **Operation `duration` is signed** because negative is a legal "until told otherwise", and
  `comfoair_json_i32` keeps a minus sign distinct from a fraction — `18.5` must not become
  "forever".
- **`last_transport_errno == 0` is not a defect**: a timed-out connect yields
  `MQTT_EVENT_DISCONNECTED` with no error event, so nothing records an errno; `failure`,
  `attempts` and `retry_in_ms` still describe it.
- **`uptime_ms` is 64-bit** because a 32-bit ms counter wraps every 49.7 days and would report a
  fresh boot; `put_u64` exists for it alone so other counters are not silently widened.
- **`phase` zero value is `COMFOAIR_PHASE_BOOT`**, so a caller that fills nothing reports
  "starting up", which is true; do not add a separate "unknown" spelling.
