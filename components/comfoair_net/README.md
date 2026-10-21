# comfoair_net — the radio, the flash and the clock behind provisioning; owner of the config document

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_net` gets the device onto a network and keeps it reachable when that fails. It owns
everything impure about Wi-Fi provisioning: the `esp_wifi`/`esp_netif` driver, the event handler,
the provisioning task, the captive-portal DNS lifetime, the NVS writes for credentials and
provisioning flags, and the device's names (SSID, DHCP host name, MAC-based ids). **It decides
nothing.** The pure state machine `comfoair_provision` (`comfoair_prov_*`) decides what the
device should be doing. This component feeds it facts, reads back a mode and an action, and
carries them out. It is also the one holder of the stored configuration document
(`comfoair_config_t`) at runtime. Every other component sees that document only through the
projections listed here.

## Responsibility

Owns:
- The single task (`"comfoair_net"`) that drives the radio, and the event queue that feeds it.
- Turning the provisioning machine's mode (`OFF`/`ACCESS_POINT`/`STATION`/`AP_STA`) into
  `esp_wifi_set_mode`/`start`/`stop`/`connect`, and its actions (`CONNECT`,
  `COMMIT_CREDENTIALS`, `RESTORE_CREDENTIALS`) into radio calls and NVS writes.
- Starting and stopping the captive DNS responder (`comfoair_captive_dns_start/stop`) exactly
  while the access point is on the air.
- Loading the whole config document from NVS at boot (`comfoair_config_nvs_load`), logging any
  repaired fields by name, and applying and persisting patches.
- The answers to "is this device claimed" (`comfoair_net_is_provisioned`) and "does this
  password match" (`comfoair_net_check_password`).
- The device's names: AP SSID `comfoair-<xxxx>` (also used as the DHCP host name), the 4-hex
  short id, the 12-hex device id and the MAC string.
- Wi-Fi scans, and the one call to `esp_wifi_sta_get_ap_info()` in the tree.

Does NOT do:
- Decide provisioning policy, timers or failure classes. That is `comfoair_provision`
  (`comfoair_prov_tick`, `comfoair_prov_classify`, `comfoair_prov_config_defaults`).
- Validate or project the config, hash passwords, or handle the NVS layout. That is
  `comfoair_config` (`comfoair_config_apply`, `comfoair_config_project`,
  `comfoair_config_wifi_usable`) and `comfoair_config_nvs`.
- Parse request bodies or render JSON. That is `comfoair_wire`
  (`comfoair_wire_parse_provision`, and the renderers for `comfoair_wire_provision_t` and
  `comfoair_wire_network_t`).
- Answer HTTP, including the captive-portal probe paths. That is `comfoair_http`.
- Publish mDNS. That is `comfoair_mdns`, which gets this component's facts as arguments from
  `src/app_mdns.c`.
- Ethernet. `comfoair_net_start` refuses any board whose `net_transport != BOARD_NET_WIFI`.

## Public API

Header: `include/comfoair_net.h`.

| Symbol | Contract |
| --- | --- |
| `comfoair_net_start(board, cb, ctx)` | Call once, from `app_main`. Loads config, initialises the machine, netifs, Wi-Fi and the event handlers, and starts the task. Returns `ESP_ERR_INVALID_ARG` (NULL board), `ESP_ERR_NOT_SUPPORTED` (non-Wi-Fi board), `ESP_ERR_INVALID_STATE` (already started), `ESP_ERR_NO_MEM`, or the error from `esp_netif_init`/`esp_event_loop_create_default`/`esp_wifi_init`/handler registration. The caller logs it and keeps running: nothing reboots. A failed NVS load is NOT an error here. The device runs on defaults. |
| `comfoair_net_cb_t` | Called from the net task on every `comfoair_net_state_t` change (`DOWN`, `ACCESS_POINT`, `CONNECTED`). Keep it short, because it runs inside `step()`. |
| `comfoair_net_get_state()` | Last state pushed to the callback. Read without a lock. |
| `comfoair_net_is_provisioned()` | Locked read of `comfoair_prov_is_provisioned`. **False while an open AP is on the air**, whatever NVS holds. False before start. The access policy asks this on every request. Never cache it. |
| `comfoair_net_has_credentials()` | Whether a usable pair is stored. This is NOT "claimed". |
| `comfoair_net_provision(wifi)` | Saves the pair as one NVS blob, updates `g_net_config.wifi`, queues `EV_CREDENTIALS_SAVED`, and **returns before any connection attempt**. Errors: `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_STATE` (no task), or the NVS error. It does not validate the pair: the caller must already have run `comfoair_config_wifi_usable`. |
| `comfoair_net_provision_body(body, result)` | Decodes a POST body under the machine lock with `comfoair_wire_parse_provision`, so the "keep the stored key" state of `wifi_psk` can be resolved without the handler ever seeing the key. Returns `ESP_ERR_INVALID_ARG` on a refused body (`result` says why, and may be NULL). Otherwise returns what `comfoair_net_provision` returns. |
| `comfoair_net_note_client()` | Queues `EV_CLIENT_SEEN` (extends the setup window). Never blocks. Safe from httpd. |
| `comfoair_net_open_setup_window()` | Queues `EV_SETUP_REQUESTED` (short button press). Erases nothing. Never blocks, because the button poller must be back at its pin within 20 ms. A press before `comfoair_net_start` has no queue and is dropped. |
| `comfoair_net_scan(out, cap, count)` | **Synchronous, seconds long**, holds the radio lock. Briefly adds STA mode if needed and restores the previous mode afterwards. Returns at most `min(cap, COMFOAIR_NET_SCAN_MAX)` rows. Duplicate SSIDs (one row per BSSID) are kept. Returns `ESP_ERR_INVALID_ARG`, `ESP_ERR_INVALID_STATE` (no task, or radio not started), or the scan error. The caller reports "no list" and never retries in a loop. |
| `comfoair_net_scan_entry_t` | `typedef comfoair_wire_network_t`. The row type belongs to the renderer, so there is only one struct layout. |
| `COMFOAIR_NET_SCAN_MAX` = 32 | Size of the static record buffer. Bounded so a crowded spectrum cannot size an allocation. |
| `comfoair_net_status(out)` | Snapshot for the provisioning status document (`comfoair_net_status_t` = `comfoair_wire_provision_t`), taken under the machine lock from one clock reading. Before start it reports `UNCONFIGURED`/`MODE_OFF`. It does **not** fill `has_rssi`/`rssi`: callers that render them call `comfoair_net_rssi()` themselves. The 1 Hz `comfoair_phase_monitor` tick calls `comfoair_net_status` and must not pick up the Wi-Fi stack's lock. Holds only the configured SSID, never a PSK. |
| `comfoair_net_config_snapshot(out, known_good)` | Redacted `comfoair_config_public_t` (secrets replaced by a sentinel), plus `comfoair_prov_has_known_good`, both read under one lock. `known_good` may be NULL. |
| `comfoair_net_config_apply(patch)` | All-or-nothing validate+apply (`comfoair_config_apply`), then `comfoair_config_nvs_save`. A rejected patch changes nothing. A **persist failure still returns `COMFOAIR_CONFIG_OK`** and is logged at ERROR: the change is live but will not survive a reboot. A NULL patch returns `COMFOAIR_CONFIG_ERR_SSID`. |
| `comfoair_net_check_password(candidate)` | Constant-time check against the stored UI password hash. The hash never leaves this component. NULL returns false. |
| `comfoair_net_password_set()` | Derived from the stored record being non-empty. It has no separate flag. |
| `comfoair_net_broker(out)` / `comfoair_net_broker_t` | Broker settings **with the plaintext MQTT password**, because MQTT authenticates with it. This is the only unredacted projection. Never log the struct or any of its fields. Keep it on a short-lived stack. Before start it is all zeros. |
| `comfoair_net_device_id(out[13])` | 12 lower-case hex digits of the **STA** (base) MAC. This is the identity used for the topic prefix and client id. |
| `comfoair_net_mac_string(out[18])` | STA MAC, colon-separated. |
| `comfoair_net_short_id(out[5])` | 4 hex digits from the last two octets of the **SoftAP** MAC. Used for display only, never as an identity. |
| `comfoair_net_ip_string(out[16])` | Current STA IPv4 address, or `""`. Never writes NULL. |
| `comfoair_net_link(out)` / `comfoair_net_link_t` | SSID, RSSI and channel of the associated AP. On any driver refusal (not init, not started, not connected) it returns false and leaves `*out` untouched. Callable from any task. It takes esp_wifi's internal lock, not this component's. |
| `comfoair_net_rssi(out)` | Thin view over `comfoair_net_link`. Returns false when not associated, because no dBm value can serve as a sentinel. |

## Implementation

Files:
- `comfoair_net.c`: the head comment with the three load-bearing arrangements (they govern every
  file below), the definitions of the shared state, the event queue's poster, the loop
  (`feed`, `step`, `comfoair_net_task`), the event handler, and the three public
  posters (`comfoair_net_provision`, `_note_client`, `_open_setup_window`). One file because it
  holds both ends of the event queue: what posts a fact and what consumes it.
- `comfoair_net_radio.c`: the machine's mode and actions turned into `esp_wifi` calls and NVS
  writes, and the captive DNS lifetime. Its own file because it is the one place that drives the
  radio for the machine, under the radio lock.
- `comfoair_net_start.c`: `comfoair_net_start` and the device's names (`short_id`, `device_id`,
  `build_ap_ssid`, `report_repairs`, and the two public id getters). The getters live here so the
  two helpers stay `static`: a prefixed export would collide with the public names.
- `comfoair_net_query.c`: the read side (`get_state`, `has_credentials`, `is_provisioned`,
  `status`, `rssi`, `mac_string`, `ip_string`, `link`). Readers only, from any task.
- `comfoair_net_scan.c`: `comfoair_net_scan`, the one seconds-long synchronous radio call.
- `comfoair_net_settings.c`: the stored configuration document's projections and writes
  (`config_snapshot`, `config_apply`, `check_password`, `broker`, `password_set`,
  `provision_body`), all under the machine lock.
- `comfoair_net_internal.h`: private to this directory. The event type, the `extern`s of the
  shared state, the two lock helpers (`static inline`) and the prototypes one file defines and
  another calls, each with its task context. Anything it makes link-visible carries the
  `g_net_`/`comfoair_net_` prefix, because `comfoair_can` shares a callback and a context of its
  own (`g_can_cb`, `g_can_ctx`) and `comfoair_mqtt` exports a bare `g_task`.
- `include/comfoair_net.h`: the contract, with the rationale for each function.
- `CMakeLists.txt`: the dependencies. Its comments explain why `board`, `comfoair_config` and
  `comfoair_provision` are public `REQUIRES` and why `esp_timer` is named explicitly.

Lifecycle:
1. `comfoair_net_start`: create two recursive mutexes and a queue of 16 events, load and repair
   the config (keyed by the device id), read the boot facts (`has_credentials` via
   `comfoair_config_wifi_usable`, `has_known_good`, and the two persisted prov flags), run
   `comfoair_prov_init(&g_net_prov, NULL, &boot)` (default timers), build the SSID, create **both**
   default netifs once, set the STA host name to the SSID **before** the radio starts (DHCP
   option 12 has to be in the first DISCOVER), run `esp_wifi_init` with `WIFI_STORAGE_RAM`,
   register the handlers, and spawn the task (4096 bytes of stack, priority 5).
2. `comfoair_net_task`: drain the queue with a 250 ms timeout (`TICK_MS`), passing each event to
   `feed()`, then call `step()`.
3. `feed()`: translate one event into `comfoair_prov_on_*` under the machine lock, and track the
   local radio facts (`g_net_sta_ready`, `g_net_associated`, `g_net_have_address`).
4. `step()`: under the machine lock, tick and read the mode, the action, the window flags and
   `address_never_arrived`, then **release the lock**. Next, in this order:
   `comfoair_net_apply_action`, then `comfoair_net_apply_mode`, then
   `comfoair_net_captive_dns_follow_ap`, then `comfoair_net_persist_flags`. Finally log a closing
   window, compute and push the state, and report dropped events.
5. `comfoair_net_on_wifi_event` runs on the system event task, which has a 2304-byte stack. **It
   only enqueues** (and writes `g_net_ip` on GOT_IP). It reads the disconnect reason into the
   event, because `comfoair_prov_classify` needs it.

Internal helpers worth knowing: `comfoair_net_apply_mode` applies a mode change only when the mode
changes (`g_net_radio_mode`). It holds a `CONNECT` in `s_connect_wanted` until `STA_START` has
arrived, and disconnects first if the station is associated. That disconnect comes back as reason
8, which the machine recognises as its own. `wifi_config_from` uses `memcpy` with the stored
lengths.
`comfoair_net_radio_try` logs a failure and continues (`ESP_ERR_WIFI_NOT_CONNECT` is not logged).
`comfoair_net_persist_flags` writes only on a transition.

Locking:
- `g_net_machine` guards `g_net_prov` and `g_net_config`. `g_net_radio` guards every `esp_wifi`
  call except `comfoair_net_link`.
- The radio lock may wrap the machine lock: `comfoair_net_apply_mode` holds the radio lock, and
  `apply_sta_config` inside it takes the machine lock. The reverse never happens. Both locks are
  recursive.
- `g_net_state`, `g_net_have_address` and `g_net_ip` are read without a lock (`get_state`,
  `ip_string`, `status`).

DO NOT:
- Call `esp_wifi_*` from outside this component. Two tasks driving one radio is how a mode change
  lands between a `set_config` and a `connect`.
- Do NVS, JSON or logging-heavy work in `comfoair_net_on_wifi_event`. A blocked or overflowed
  system event task delays `IP_EVENT_STA_GOT_IP`, and the whole design depends on that event.
- Make `post_event` block. Drops are counted (`s_events_dropped`) and logged at ERROR.
- Hold the machine lock across radio or NVS calls. `is_provisioned()` on httpd would then wait
  behind flash or a scan.
- Put `ESP_ERROR_CHECK` on anything downstream of stored or received data, or on radio calls.
  A panic over persisted config is a boot loop that cannot be fixed over the air.
- Swap `ESP_MAC_WIFI_SOFTAP` for `ESP_MAC_WIFI_STA` in `short_id()`. The two differ by +1 in the
  last octet. The result would look right and be one digit off from the SSID and the label. No
  host test can see it; hardware scenario HV-PROV-02 checks it against the boot log line
  `names: access point '...', id ...`.
- Change the `"comfoair-%s"` SSID format. It is on the air and printed on labels, and
  `comfoair_mdns_name.c` builds the `.local` name independently to the same shape.
- Use `strncpy(..., sizeof-1)` for the SSID/PSK. That truncates a legal 32-character SSID or a
  64-character hex PSK.
- Let `esp_wifi` keep its own credential store (`WIFI_STORAGE_FLASH`). That would be a second
  source of truth.
- Fill RSSI inside `comfoair_net_status`. See the table above.

## Dependencies

`REQUIRES board comfoair_config comfoair_provision comfoair_captive comfoair_wire esp_wifi
esp_netif esp_event esp_timer nvs_flash` (all public, because the header exposes `board_t`,
`comfoair_wifi_t`, `comfoair_prov_*` and the `comfoair_wire_*` types). Also uses
`comfoair_config_nvs.h` and `comfoair_captive_dns_server.h`.

Depended on by: `src/main.cpp` (start, state callback, `comfoair_net_open_setup_window` handed to
`comfoair_button_start` as a function pointer, because `comfoair_button` deliberately does not
link this component), `src/app_mdns.c` (ids, snapshot), `comfoair_http` (`comfoair_http_config.c`,
`comfoair_http_read.c`, `comfoair_http_gate.c`: provisioning, scan, status, config, password,
RSSI; `comfoair_http_system.c`: link, state, ids, IP), `comfoair_mqtt` (broker settings, ids, IP;
`comfoair_mqtt_retry.c` calls status and IP), and `comfoair_phase_monitor` (status, 1 Hz).
`comfoair_mdns`, `comfoair_button` and `comfoair_wire` deliberately do **not** depend on it, so that
it never enters a host build.

## Tests

No host suite compiles `comfoair_net`. It is impure glue over esp_wifi, and nothing under
`test/` or `tools/` includes `comfoair_net.h`. What is tested sits in the neighbours it calls:
- `test/test_provision`, `test/test_provision_setup_window`: the decision machine
  (`comfoair_prov_*`).
- `test/test_wire`, `test/test_wire_status`: `comfoair_wire_parse_provision` and the
  status/scan rendering.
- `test/test_config`: config apply, projection and NVS repair.
- `test/test_mdns_name`: the name format, with the SSID format **transcribed** from
  `build_ap_ssid()` rather than imported.
- `test/test_device_url`: the consumer side of the `comfoair_net_ip_string` contract.

Not covered on the host: event-to-fact translation, the mode/CONNECT sequencing, the scan mode
dance, the lock ordering, persist-on-transition, the DNS lifetime, and the SoftAP-vs-STA MAC
choice. These are checked on hardware (`docs/hardware-verification/`, e.g. HV-PROV-02).

## Notes

- **Two events, not one, for the AP.** "Asked for an AP" (the mode) and "AP on the air"
  (`WIFI_EVENT_AP_START`, fed as `comfoair_prov_on_ap_started`) are kept separate, because the
  setup window's clock starts on the second.
- **Provision returns before connecting.** Starting the station retunes the radio to the router's
  channel and takes the AP with it, so the phone loses the setup page. The HTTP reply has to go
  out first.
- **Store first, then tell the machine.** If the machine entered a trial over a pair that is not
  yet in flash, a power cut would roll back for the wrong reason.
- **The open AP is deliberate.** A WPA2 key that nobody has been told is a brick. The exposure is
  bounded by the access policy (unclaimed while the AP is up) and by the setup window, not by the
  radio. `max_connection = 4`.
- **`RESTORE_CREDENTIALS` is the typo escape.** When newly saved credentials never produce an
  address, the last known-good pair is put back and reconnected. The commit to known-good happens
  only on the transition, to save flash wear.
- **Recursive mutexes.** Paths re-enter the machine lock (for example `apply_sta_config` from
  inside `comfoair_net_apply_mode`). A plain mutex would deadlock where those paths meet.
- **Persist failure is reported as success.** `config_apply` returns OK when NVS save fails,
  because the in-memory change did take effect. Saying "rejected" would be false, and the owner
  could not act on it.
