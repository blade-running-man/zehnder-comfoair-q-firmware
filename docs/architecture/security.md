# Security

This page describes who the firmware trusts, what protects each kind of operation, and what is
deliberately not protected. The rules are implemented in four small components —
`comfoair_policy`, `comfoair_auth`, `comfoair_secrets` and `comfoair_config` — and applied by
`comfoair_http` in front of every handler.

## Trust model

| Where the client is | How the firmware treats it |
| --- | --- |
| On the device's own access point | The access point is **open** (no Wi-Fi password), so "the client" is anyone in radio range. While any open access point is on the air the device counts as **unclaimed**, whatever NVS holds; so does a device with no Wi-Fi credentials stored (`comfoair_prov_is_provisioned()` answers `has_credentials && !ap_on_air`). |
| On the household LAN | Not trusted by default. Reading is open until the owner sets a UI password; writing is limited until then (see the policy below). |
| On the MQTT broker | Trusted as far as the broker's own authentication and ACLs go. The device does not authenticate MQTT commands itself. |
| At the device with a hand on the button or a USB cable | Fully trusted: the button can erase the network and password (5 s) or everything (15 s). |

The setup access point is the riskiest surface, so the components that parse what arrives there
are pure functions with host test suites: the DNS parser (`comfoair_captive_dns.h`), the JSON
reader (`comfoair_json.h`), the configuration and provisioning documents (`comfoair_wire.h`), the
`Authorization` parser (`comfoair_auth.h`) and the policy itself (`comfoair_http_policy.h`).

## Request parsing

Every JSON body the device accepts — `POST /api/config`, `POST /api/provision`, the entity and
operation commands — is read by the firmware's own reader in `components/comfoair_json`. On the open
access point the body comes from anyone in radio range, and it is parsed before the access policy
has anything to say about it, so the reader is narrow on purpose
(`components/comfoair_json/include/comfoair_json.h`):

- **Flat objects only.** `comfoair_json_check()` walks the whole document once before any field is
  read. A top level that is not `{…}` is refused as distinct from an empty object (every field of a
  configuration patch is optional, so "no keys" would be a successful save that changed nothing);
  a nested object or array, a malformed token and a repeated key are each refused. The answer is
  `400`.
- **Repeated keys are refused, not resolved.** JSON parsers disagree on whether the first or the
  last copy wins, and here the two readings can mean opposite things —
  `{"ui_password":"letmein","ui_password":"__UNCHANGED__"}`.
- **`\u0000` is refused.** It is legal JSON, but the reader hands back C strings, and decoding it
  would silently truncate a password or an SSID.
- **Nothing is truncated or wrapped.** A string longer than the caller's buffer and a number that
  is fractional or out of range are refused; an absent key is reported apart from an empty string,
  because absent keeps a stored value and `""` clears it.

::: details Why not cJSON
cJSON ships with ESP-IDF and is already linked into the image, but it cannot be built in the host
test environment, which sees only this repository's components. Every malformed body would then be
testable only by flashing a board. The documents this device accepts are all flat, so a reader that
covers exactly that grammar is small enough to own, and every case in `test/test_json` runs on the
host.
:::

## The UI password

The UI password is optional and not set out of the box. When set, it is stored as a one-way
record, never as the password (`components/comfoair_config/include/comfoair_config.h`):

- Format `1$<iterations>$<salt hex>$<digest hex>`: PBKDF2-HMAC-SHA256 (mbedtls) with 10 000
  iterations, a fresh 16-byte random salt per password and a 32-byte digest. The iteration count
  is carried in the record so it can be raised later; the parser refuses counts above 100 times
  the default so a corrupt record cannot make each login take minutes.
- Length 8 to 128 characters.
- Comparison of the digest is constant-time (`comfoair_secret_equals()`).
- Whether a password is set is derived from the record being non-empty; there is no separate flag
  that could disagree with it. A record that does not parse is erased at load, leaving the device
  open rather than locked by nobody.
- The record never leaves `comfoair_net`: the HTTP layer hands it a candidate and gets back yes or
  no (`comfoair_net_check_password()`).

There are no sessions. The browser sends HTTP Basic credentials with every request, and each
request that carries them is checked with one PBKDF2 derivation.

### Checking a request

`comfoair_auth_check()` (`components/comfoair_auth/include/comfoair_auth.h`) accepts only the
`Basic` scheme. The user name is ignored — the device has one password and no accounts. The
header is read into a 512-byte buffer; a password longer than 128 characters is refused as too
long, never truncated. After a failed attempt the HTTP layer waits **1 s** before answering. Because
`esp_http_server` serves all sockets from one task, that delay throttles guessing to about one
attempt per second in total; it also delays every other request during that second. There is
deliberately no lockout: locking out would be a denial of service against the owner, who is the
person most likely to mistype. Nothing in `comfoair_auth` logs.

A forgotten password is recovered by holding the button for 5 s, which erases the Wi-Fi
credentials and the UI password and keeps the broker settings. See [Button](/device/button).

## Who may do what

`comfoair_http_check()` (`components/comfoair_policy/comfoair_http_policy.c`) is a pure function
of the method, the path and three facts: is the device provisioned (Wi-Fi credentials stored and
no open access point on the air), is a password set, did this request authenticate.

| Request | Unclaimed (no credentials, or open AP on the air) | Claimed, no password | Claimed, password set |
| --- | --- | --- | --- |
| Any `GET` (state, entities, config, status, system, log, probe report, Wi-Fi scan, the SPA) | allowed if no password set; otherwise needs auth (401) | allowed | needs auth (401) |
| `POST /api/provision` | allowed if no password set; otherwise needs auth (401) | 403 | needs auth (401) |
| `POST /api/config` | 403 | allowed (this is how the first password is set) | needs auth (401) |
| `POST /api/entities/<key>`, `POST /api/ops/<name>` | 403 | allowed | needs auth (401) |
| `POST /api/probe` (read-only walk) | 403 | allowed | needs auth (401) |
| `POST /api/ota` and any other `POST` | 403 | **403** | needs auth (401) |
| `PUT`, `DELETE`, `PATCH` | 403 | 403 | 403 |

Points worth knowing:

- **Controlling the ventilation without a password is allowed on the LAN.** A device with no
  password is a supported configuration, and refusing commands there would make it useless. The
  cost, stated plainly: anyone on the household LAN can change the fan speed or start a boost —
  the same thing anyone at the unit's own panel can do.
- **Firmware upload always needs a password.** There is no image signature check (secure boot is
  off), so whoever can `POST /api/ota` can run code. With no password set, `/api/ota` answers 403.
- **An unclaimed device accepts only a network.** On the open access point the one write allowed
  is `POST /api/provision`. Once a password exists, re-homing the device to another network needs
  it too, so a neighbour cannot wait for a router outage and take the device. The same applies
  while a short button press has reopened the setup window.
- **What the access point shows is what the LAN shows.** Reads follow the one GET rule above on
  every interface. On a device with no UI password, anyone in range of an open access point —
  including the fallback access point of a device that already holds its owner's data — can read
  `/api/state`, `/api/log` and `/ws`, and ventilation and humidity readings over time reveal whether
  people are at home. Setting a UI password closes every read except the probe answers and the
  `/ws` upgrade (see the warning below).
- **Wi-Fi credentials go only through `/api/provision`.** `POST /api/config` refuses a `wifi_ssid`
  or `wifi_psk` field by name (`comfoair_wire_parse_config()`) with a refusal of its own: `422`,
  `"field"` naming the key and the sentence "The network is set through POST /api/provision, not
  here." A client that echoes back the whole `GET /api/config` document (which carries
  `wifi_ssid`) is told where the field belongs rather than that its value is wrong. Credentials are
  accepted by `POST` only, so the home network key never travels in a query string that a request
  log would keep. On a claimed device with no password, `/api/provision` is not among the open writes, so
  changing networks there means either setting a UI password and authenticating, or opening the
  setup access point with a short button press.
- **Other methods have no route.** No handler is registered for `HEAD`, `OPTIONS` or any method
  besides `GET`, `POST`, `PUT`, `DELETE` and `PATCH`, so `esp_http_server` answers those itself
  (405) before any handler or policy runs.
- **Matching is exact.** `/api/config` does not cover `/api/configuration`; a prefix rule such as
  `/api/entities/` requires the trailing slash and a non-empty remainder.
- **Every route but two goes through the check.** Every handler calls `comfoair_http_allowed()`
  first, and catch-all handlers for `POST`, `PUT`, `DELETE` and `PATCH` on `/*` apply the policy to
  writes that have no handler before answering 501. The two exceptions:
  - **Captive-portal probe answers.** `GET` on one of the seven fixed probe paths
    (`/generate_204`, `/gen_204`, `/hotspot-detect.html`, `/ncsi.txt`, `/connecttest.txt`,
    `/success.txt`, `/canonical.html`) is answered from the `comfoair_captive` table
    (`components/comfoair_captive/comfoair_captive_probe.c`) before the check runs, whether or not
    the access point is up and even with a password set.
    Those answers are constant strings and carry no device state.
  - **The `/ws` upgrade.** It is answered by `esp_http_server` itself, never by the route handler
    — see the warning below.
- **No CORS header is sent.** The UI is served by the device itself, so other origins in the
  browser cannot read the API.
- **The web UI has no privileged path.** It uses the same routes, the same Basic credentials and
  the same policy as `curl`.

::: warning The WebSocket handshake is not authorised
`esp_http_server` answers the WebSocket upgrade on `/ws` itself and does not call the route
handler for it, and the pre-handshake callback is disabled in this build. The access policy
therefore cannot be applied to the upgrade: **with a password set, anyone who can reach the device
can still open `/ws` and receive state pushes.** The channel is push-only — data frames from a
client are read and discarded — so it cannot be used to change anything. Nothing in the firmware
authorises the upgrade: there is no ticket, token or other check on `/ws`.
:::

## Write-only secrets

Stored secrets never leave the device through any API projection
(`components/comfoair_secrets/include/comfoair_secrets.h`):

- Redaction is done field by field in the configuration projection
  (`comfoair_config_project()` in `components/comfoair_config/comfoair_config.c`): the Wi-Fi key,
  the broker password and the UI password record each become the sentinel or `""` there, and the
  renderers only ever see that projection. `comfoair_secrets` also has a word-based classifier
  (`comfoair_secret_key()`: `password`, `passwd`, `psk`, `secret`, `token`, `key`), but no firmware
  code calls it, so a newly added secret field is redacted only if the projection is extended.
- `GET /api/config` returns the sentinel `__UNCHANGED__` for a secret that is set and `""` for one
  that is not.
- On `POST`, an absent field or the sentinel keeps the stored value; `""` clears it; anything else
  is stored. Submitting the sentinel when nothing is stored is rejected, so the published sentinel
  string can never become a password. For the Wi-Fi key the refusal is what keeps the device
  reachable: `__UNCHANGED__` is thirteen printable characters and passes the key validator, so
  storing it would leave credentials that can never associate — and stored credentials are what
  keep the setup access point down (`comfoair_wire_parse_provision()`).
- Error messages name a field, never its value, and no secret is ever logged — the log ring is
  served by `GET /api/log`, which is open on a device without a password. Every error body is
  built from a fixed sentence (`comfoair_http_send_error()`); nothing a client submitted is
  formatted into it. Repaired settings are logged by field name only.

How each secret is stored:

| Secret | Stored as | Who can read it back |
| --- | --- | --- |
| UI password | PBKDF2 record in the `cfg_owner` namespace | nobody; only a yes/no comparison |
| Wi-Fi key | plaintext, in one blob with the SSID in `cfg_wifi` (the radio needs it) | `comfoair_net` only; never in any document |
| Broker password | plaintext in `cfg_app` (MQTT authenticates with the plaintext) | `comfoair_net_broker()`, called only from `comfoair_mqtt_client.c` — by the client builder, and by `comfoair_mqtt_pending_cleanup()`, which compares host, port and prefix; each copy is wiped with `explicit_bzero()` as soon as it has been read (the builder's once esp-mqtt has taken its own) |

The two passwords are stored differently and returned identically: the broker password must be
plaintext because MQTT authenticates with the plaintext, so a hash would be useless there; the UI
password is only ever compared, so it is kept as a one-way record. Neither is returned by any API.

NVS encryption and flash encryption are not enabled, so anyone with the device and a USB cable can
read the flash. mDNS TXT records carry no secret by construction (`comfoair_mdns.h`).

## Identity

The device's identity is its full MAC, never its name. The `device_id` (twelve lower-case hex
digits) is the MQTT client id, the Home Assistant device identifier, the root of every `uniq_id`
and the default topic prefix (`components/comfoair_net/comfoair_net_start.c`). Renaming the device
therefore re-keys nothing: Home Assistant keeps one device, not a live one and a dead one that
cannot be removed. The access point name and the `.local` host name, `comfoair-XXXX`, carry only
the last two octets of the MAC — enough for a person to tell two units apart, not enough for a
broker, where two devices with the same client id would knock each other off for ever.

## Transport security

- **HTTP is plain HTTP.** There is no HTTPS server; Basic credentials cross the LAN unencrypted.
- **MQTT is plain TCP.** The client is built with an `mqtt://` URI; TLS to the broker is not
  implemented. Username and password are optional; both are sent only when a user name is
  configured.
- The MQTT client id is the device's full MAC, and the device subscribes only to its own topic
  tree (three patterns under the configured prefix; a prefix containing `+` or `#`, or starting
  with `$`, is refused). Anyone the broker lets publish to that tree can control the unit, so broker ACLs are
  the access control for MQTT.
- A retained message on a command topic is refused, so a command published with the retain flag
  cannot replay on every reconnect or boot.

## OTA protection

Protection against a hostile image is the password requirement above; nothing else verifies who
built an image. What the upload path does check (`components/comfoair_ota/include/comfoair_ota.h`)
protects against the wrong file:

- the `Content-Length` must be present and fit the update slot (1984 KiB);
- the image header magic and chip id must match the running chip;
- the app descriptor's project name must match this firmware;

all from the first bytes, before anything is erased. ESP-IDF's own verification in
`esp_ota_end()` then checks segments and the appended SHA-256. Bootloader rollback undoes an image
that crashes or restarts before it is confirmed, 30 s after its web server started. See [OTA and system](/device/ota-and-system).
