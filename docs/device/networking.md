# Networking

Four components put the device on a network and keep it reachable when that fails:

| Component | Role |
| --- | --- |
| `comfoair_provision` | the **pure** provisioning state machine: states, setup window, retries, rollback, access-point fallback. Time is a parameter; host suite `test/test_provision` |
| `comfoair_net` | the device half: owns the radio, NVS and the clock; feeds events to the machine and carries out its mode and actions |
| `comfoair_captive` | the captive portal: a DNS responder that answers every name with the access point's address, and fixed answers to OS connectivity probes |
| `comfoair_mdns` | the `comfoair-XXXX.local` host name and an `_http._tcp` service |

The step-by-step owner flow is in [First setup](/guide/first-setup); this page is how it works
underneath.

## Transport

Wi-Fi only (2.4 GHz). `comfoair_net_start()` refuses a board whose `net_transport` is not
`BOARD_NET_WIFI` with `ESP_ERR_NOT_SUPPORTED`; see [Board](/device/board#what-would-need-real-work).
The Wi-Fi driver is set to `WIFI_STORAGE_RAM`: credentials live in the firmware's own NVS
namespace (see [Configuration storage](/device/config-storage)), never in the IDF's Wi-Fi blob.

## Names and identity

| Name | Source | Example |
| --- | --- | --- |
| Access-point SSID | `comfoair-` + last two octets of the **SoftAP** MAC, lower-case hex | `comfoair-8ae5` |
| DHCP host name (station) | the same string | `comfoair-8ae5` |
| mDNS host name | the same string, `.local` | `comfoair-8ae5.local` |
| Device id | full **STA** MAC, 12 lower-case hex characters | `…8ae4` |
| Default device name | `ComfoAir Q`, a space, and the last four characters of the device id | `ComfoAir Q 8ae4` |

With `CONFIG_ESP_MAC_ADDR_UNIVERSE_WIFI_AP=y` (`sdkconfig.m5stack-nanoc6`) ESP-IDF derives the
SoftAP MAC as the STA MAC plus one, so the four digits in the SSID and the default name differ by
one. This is deliberate: the SSID, the sticker and the address bar (what
the owner types) agree with each other; the device name is a display label stored in NVS and is
not moved. The device id is the identity used for the MQTT client id and the default topic
prefix; the four-character short id is never an identity.

## Architecture of `comfoair_net`

- **One task** (`comfoair_net`, priority 5, 4 KB stack) owns the radio. It drains an event queue
  (16 entries) with a 250 ms timeout, feeds each event to the machine, then runs one step: tick the
  machine, take its mode and pending action, apply them, follow the access point with the DNS
  responder, persist the two boot flags if they changed, and recompute the coarse state
  (`connected` / `access point` / `down`) reported to `app_main`.
- **The Wi-Fi/IP event handler only enqueues.** It runs on the system event task, whose stack is
  small, and the event that must not arrive late is `IP_EVENT_STA_GOT_IP`.
- **Two locks:** one around the machine and document, one around the radio. The only place both
  are held is `comfoair_net_apply_mode()`, which takes the radio lock and then briefly the machine
  lock to read the station config; nothing waits for the radio while holding the machine lock. A
  request handler asking `comfoair_net_is_provisioned()` takes only the machine lock and so never
  waits behind a scan.
- **No `ESP_ERROR_CHECK`** on anything downstream of stored or received data; radio calls are
  logged and the device carries on.

## Provisioning states

| State | Meaning | Radio mode |
| --- | --- | --- |
| `unconfigured` | nothing usable stored (first boot, or after a reset) | access point |
| `connecting` | credentials stored, attempt in flight | station (AP+station while a window runs) |
| `connected` | an IP address on the owner's network | station (AP+station while a window runs) |
| `trial` | new credentials just saved, not yet proven by an address | station (AP+station while a window runs) |
| `retrying` | an attempt failed; waiting out the backoff | as `connecting` |
| `fallback-ap` | own access point back while the station keeps trying | AP+station |
| `window-closed` | the setup window ran out with nothing to connect to | off |

The machine starts in `connecting` when NVS holds a usable Wi-Fi pair, otherwise in
`unconfigured`. A device whose previous run never got a DHCP address starts in `fallback-ap`
(see below).

### Timing (`comfoair_prov_config_defaults()`)

| Parameter | Default | Meaning |
| --- | --- | --- |
| `setup_window_ms` | 15 min | first setup window on a never-configured device |
| `reopen_window_ms` | 5 min | every window on a device that holds credentials or has had a window close before (including one opened by a short button press) |
| `setup_window_ceiling_ms` | 60 min | hard cap from the moment the access point came up, however much activity extends it |
| `fallback_after_ms` | 30 min | sustained failure before the access point returns by itself |
| `trial_timeout_ms` | 90 s | time new credentials get to produce an address before rollback |
| `address_timeout_ms` | 30 s | time an associated station gets to obtain an address |
| `retry_backoff_min_ms` / `max_ms` | 1 s / 30 s | reconnect backoff, doubling |

### Setup window

The window is anchored on the access point actually coming **on the air**
(`WIFI_EVENT_AP_START`), not on boot or on the decision to raise it. Client activity extends it
while it is open: a station joining the access point (`WIFI_EVENT_AP_STACONNECTED`), every HTTP
request that passes through `comfoair_http_allowed()`, every captive-probe answer, every push round
while a WebSocket is open (all via `comfoair_net_note_client()`), and saving new credentials. The
extension restarts the window length but cannot pass the 60-minute ceiling, measured from when the
window opened; a short button press re-anchors both. When the window expires the flag `win_closed`
is persisted (so later windows are the 5-minute ones), and:

- on an `unconfigured` device the radio is switched **off** and the state becomes
  `window-closed`;
- on a `fallback-ap` device the access point goes down, the state becomes `retrying` and the
  30-minute fallback clock restarts;
- in any other state (a window opened by a button press) the access point goes down and the
  station carries on.

Ways back from `window-closed`: a reboot (5-minute window) or a short button press (see
[Button](/device/button#the-setup-press)). On `connected` the window ends as soon as the address
arrives and the access point goes down.

### Retries and failure classes

Each disconnect reason (`wifi_event_sta_disconnected_t.reason`) is classified by
`comfoair_prov_classify()`:

| Failure (`failure` field) | Reasons |
| --- | --- |
| `network-not-found` | 200 beacon timeout, 201 no AP found, 210, 211, 212 |
| `wrong-password` | 14 MIC failure, 15 4-way handshake timeout, 202 auth fail, 204 handshake timeout |
| `refused` | 5, 23, 203, 205 and every other code |
| `no-address` | associated, but no `GOT_IP` within 30 s (no event; detected by absence) |
| none | 8 (`ASSOC_LEAVE`): the firmware's own disconnect before a reconnect |

Every disconnect arms a retry at 1 s, 2 s, 4 s … capped at 30 s. Nothing ever reboots because the
network is absent.

### Access-point fallback

After **30 minutes** of sustained `network-not-found`, `wrong-password` or `refused`, the machine
moves to `fallback-ap`: the open access point comes up beside the station for a 5-minute window,
then goes down again while retries continue. The cycle repeats (about every 35 minutes) until the
network returns. `/api/status` reports the countdown as `fallback_ap_in_ms`.

`no-address` deliberately does **not** raise the access point by itself (a router still booting
would otherwise put an open AP on the air). Instead, once a no-address failure has lasted 30
minutes, the flag `no_addr` is persisted and the **next boot** starts in `fallback-ap`. An address,
or new credentials, clears it.

### Trial and rollback

Saving new credentials stores the pair, starts a 90-second **trial** and connects. The first
address commits the pair to the known-good slot (`sta_good`). If the trial times out and a
known-good pair exists, it is written back over `sta` and the device reconnects with it; on a
first provisioning there is nothing to roll back to and the device keeps retrying.

## Provisioning API

| Request | Purpose |
| --- | --- |
| `GET /api/wifi/scan` | networks the device can see: SSID, RSSI, secure flag; up to 32 records, one per BSSID (not de-duplicated). Synchronous, seconds long; on the access point the phone may lose the page while the radio sweeps. `503` if the scan fails |
| `POST /api/provision` | hand the device a network |
| `GET /api/status` | the provisioning and phase document (below), plus the MQTT block |

`POST /api/provision` body:

```json
{"wifi_ssid": "Kitchen", "wifi_psk": "hunter2"}
```

`wifi_psk` follows `comfoair_secret_decide()`: a value stores it, `""` means an open network, and
an absent key or the sentinel `__UNCHANGED__` keeps the stored key. The sentinel is refused only
when no key is stored. The pair is validated (SSID 1–32 bytes with no NUL; key empty, or 8–63
characters, or exactly 64 hex digits) and written as one blob. The handler **answers before
connecting**: the station may move the radio to the router's channel and drop the phone, so the
reply (the status document) must already be on its way. `400` when the body is not a JSON object,
`422` for a missing or mistyped field or a refused value, `503` if the pair could not be stored or
the provisioning task is not running.

Wi-Fi fields are not accepted on `POST /api/config`: `wifi_ssid` and `wifi_psk` are refused by
name with `422` and a pointer to `/api/provision`, because silently dropping a credential someone
typed would let the form report success over a network that was never stored
(`components/comfoair_wire/comfoair_wire_parse.c`). `GET /api/config` still shows both: the SSID as
stored (it is in every beacon anyway), and the key as the sentinel `__UNCHANGED__` when one is
stored or `""` when none is, never the key itself.

### Access rule while the access point is on the air

`comfoair_net_is_provisioned()` is `has credentials AND no access point on air`, asked on every
request. While the open access point is up the device counts as unclaimed: `POST /api/provision`
is the only write accepted (and requires the UI password if one is set); every other `POST` is
`403`. Once the device is on its network with the access point down, `POST /api/provision` is an
ordinary protected write: it needs a UI password to be set and the request to authenticate, and
without a password it is `403`. Full policy: [Security](/architecture/security).

### Status document fields

`state`, `mode` (`off`, `access-point`, `station`, `ap-station`), `phase`, `provisioned`,
`has_credentials`, `wifi_known_good`, `connected`, `wifi_ssid` (configured network only, never a
key), `ip`, `ap_ssid`, `ap_on_air`, `setup_window_open`, `setup_window_remaining_ms` and
`fallback_ap_in_ms` (`null` for "never"), `uptime_ms`, `failure`, `failure_reason` (raw reason
code), `rssi` (`null` when not associated), plus device identity. See
[REST API](/interfaces/rest-api) for the full schema.

## Captive portal

The access point is **open** (no WPA key, up to 4 stations). Two halves make a phone that joins it
reach the setup page.

**DNS** (`comfoair_captive_dns.c`, `comfoair_captive_dns_server.c`): a UDP responder on port 53,
bound to the access point's own address (not `0.0.0.0`, so it never answers on the owner's
network while the radio is in AP+station). Every `A`/`IN` question is answered with that address,
TTL 0. Any other type (notably `AAAA`) gets an empty `NOERROR`. Responses never exceed 512 bytes;
compression pointers in questions, packets already flagged as responses and malformed packets get
no answer. It starts when the access point comes up and stops when it goes down. The DHCP server
advertises the AP address as resolver only because `CONFIG_LWIP_DHCPS_ADD_DNS` is enabled in
`sdkconfig.m5stack-nanoc6`; turning that off silently disables the portal.

**Probe answers** (`comfoair_captive_probe.c`): OS connectivity checks are told the network is
fine, byte-exact, served ahead of the access policy and with `Cache-Control: no-store`:

| Path | Answer | Asked by |
| --- | --- | --- |
| `/generate_204`, `/gen_204` | `204` | Android |
| `/hotspot-detect.html` | Apple "Success" page | iOS, macOS |
| `/ncsi.txt` | `Microsoft NCSI` | Windows 8 and earlier |
| `/connecttest.txt` | `Microsoft Connect Test` | Windows 10+ |
| `/success.txt`, `/canonical.html` | Firefox's expected bodies | Firefox |

Intended consequence: the OS is not given a reason to open its sign-in sheet; the owner opens a
browser, and any name typed there resolves to the device. A path that is not in the table is
served as a normal web asset.

## mDNS

`app_mdns_start()` in `src/app_mdns.c` gathers the inputs and calls `comfoair_mdns_start()`.
`app_main` calls it after `comfoair_net_start()` succeeded and after the HTTP server start was
attempted (it is skipped entirely when the network failed to start).

- **Host name:** `comfoair-XXXX` (13 characters, the access-point short id). Never changes.
- **Instance name:** the configured device name (truncated to 63 octets on a UTF-8 boundary;
  the host name if the device name is empty). `POST /api/config` calls `comfoair_mdns_rename()`
  after a save, which moves only the instance name, and only when the service is registered.
- **Service:** one `_http._tcp` on the HTTP server's port, registered only if the server started;
  otherwise the host name is published alone.
- **TXT** (at most these five, in this order):

| Key | Value |
| --- | --- |
| `board` | board name, e.g. `m5stack-nanoc6` |
| `ver` | firmware version |
| `id` | device id (STA MAC) |
| `path` | `/` |
| `api` | `/api` |

No SSID, broker, password or "is provisioned" flag can appear: `comfoair_mdns_facts_t` has no
field for them. A pair whose value is empty, or whose `key=value` exceeds 255 octets, is left out
rather than truncated (the values are first copied into fixed buffers: board and id 63 octets,
version 32).

**Limits:** the firmware adds no name-conflict handling of its own: no suffix is added and no
rename is attempted if another host holds `comfoair-XXXX.local`. The firmware never stops or
restarts the responder after start, except that before a button reset restarts the device,
`comfoair_mdns_goodbye()` calls `mdns_free()` on a separate task and the reset path waits a fixed
150 ms (`COMFOAIR_MDNS_GOODBYE_MS`). A failure to start mDNS is logged and the device stays
reachable by address.

`comfoair_mdns` includes no other component's header; `app_mdns_start()` hands it every fact in a
`comfoair_mdns_config_t`. The reason is the host build: see
[Testing](/development/testing#callbacks-not-includes).

### Why the goodbye runs on its own task

`mdns_free()` sends TTL 0 for the host name and the service, so neighbours drop the record instead
of caching an address that leads nowhere. It also contains two waits with no timeout:
`mdns_service_remove_all()` takes the service semaphore with `portMAX_DELAY`, and
`service_task_stop()` spins on `vTaskDelay(10 ms)` until the mDNS task picks up its stop request
(`mdns_service.c:43`, `:360-363` in `espressif/mdns` 1.12.0). Normally that takes tens of
milliseconds; an mDNS task wedged in a send on a vanished interface never finishes it. The caller
is one statement away from `esp_restart()` on a device whose NVS is already erased, so
`comfoair_mdns_goodbye()` creates a short-lived task (`comfoair_bye`) for `mdns_free()` and returns
at once; the reset path waits 150 ms and restarts whatever happened. A lost goodbye costs the
neighbours a stale cache entry for a couple of minutes; a restart that never happens costs the
owner the device.

### Browsing and the console are compiled out

`sdkconfig.defaults` sets `CONFIG_MDNS_ENABLE_BROWSE=n` and `CONFIG_MDNS_ENABLE_CONSOLE_CLI=n`
(both default to on in the component). Nothing in the firmware browses or uses the console. With
browsing compiled in, `mdns_receive.c` walks every **incoming** multicast packet into browse
structures with heap allocations behind them, and the senders include whoever has just joined the
open setup access point. Removing code an unauthenticated radio can drive is the reason;
the 6 016 bytes of flash it saves are a side effect.
