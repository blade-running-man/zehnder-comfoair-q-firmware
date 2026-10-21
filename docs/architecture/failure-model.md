# Failure model

The device sits between a ventilation unit, a Wi-Fi network, an MQTT broker and Home Assistant,
and any of them can disappear for minutes or weeks. This page says what the firmware does in each
case. The governing rule is short:

::: tip The rule
**Nothing reboots because a peer is absent.** A missing unit, router, broker or Home Assistant
is reported — in `GET /api/status`, on the LED, in the log, on MQTT availability topics — and
retried with bounded delays. It is never answered with a restart.
:::

## Where the rule is enforced

- **Two restart call sites, both deliberate.** `esp_restart()` appears in exactly two places:
  `components/comfoair_button/comfoair_button_reset.c` (the owner held the button for 5 s or 15 s)
  and `components/comfoair_ota_runtime/comfoair_ota_runtime.c` (a new image was installed). No
  timer, counter or watchdog in the firmware calls it.
- **Boot never stops on a subsystem.** Every start in `app_main()` logs and continues, and seven of
  them also record a fault bit (see [Runtime](/architecture/runtime)). The only `ESP_ERROR_CHECK`
  calls are in NVS initialisation (`nvs_init()` in `src/main.cpp`), after the recoverable cases have
  been handled by erasing.
- **No `ESP_ERROR_CHECK` on stored or received data.** `comfoair_config_nvs.c` and
  the `comfoair_net` sources return every flash and radio error to a caller that continues with
  defaults: a bad stored value that aborted the boot would be a reboot loop with no way out over the
  air.
- **Retry ladders have no terminal state.** `comfoair_mqtt_backoff_ms()` returns a delay, never 0
  and never "give up"; the attempt count saturates at 16 but the ladder runs forever. The Wi-Fi
  retry backoff in `comfoair_provision` likewise only ever produces a delay.
- **esp-mqtt has no watchdog.** Auto-reconnect is disabled and the publisher task owns the retry
  timing; nothing measures "broker unreachable for N minutes".
- **OTA confirmation ignores peers.** A new image is confirmed 30 s after the web server started;
  it does not wait for the unit, Wi-Fi or the broker, so an absent peer cannot make the bootloader
  roll back a good image (`components/comfoair_ota_runtime/comfoair_ota_runtime.c`).
- **The phase monitor only reports.** `comfoair_phase_monitor` reads every subsystem once a
  second; a `fault` phase is a colour and a JSON field, nothing more.

Two ESP-IDF mechanisms remain that can restart the chip, and neither is about peers: a panic
(`CONFIG_ESP_SYSTEM_PANIC_PRINT_REBOOT`; the interrupt watchdog, `CONFIG_ESP_INT_WDT`, ends in
one) and the brownout detector. The task watchdog is enabled
with a 5 s timeout but does **not** panic (`CONFIG_ESP_TASK_WDT_PANIC` is unset); it only logs.

## Peer by peer

### The ventilation unit is silent (CAN)

| Situation | What the device does |
| --- | --- |
| CAN driver did not start | Fault bit `CAN`, phase `fault`. Poll, command sender and probe are not started; command requests answer `503 "commands are not running"`. Wi-Fi, HTTP and MQTT still start so the owner can see why. |
| Driver up, no frame ever received | Bus health `NEVER_SEEN`: the `<prefix>/bus` availability topic is `offline`, but the phase is **not** `bus-silent`, because "check the loom" is wrong for a bus that never worked. |
| Frames stop arriving (unit powered off, loom unplugged) | Bus health ladder in `components/comfoair_phase/include/comfoair_bus_health.h`: after 60 s of silence one PDO sweep is requested (`comfoair_poll_request_now()`); after 90 s without an answer the bus is `SILENT` — phase `bus-silent` and `<prefix>/bus` = `offline`, so Home Assistant greys out every entity that reads the unit. The first frame clears it. |
| Controller goes bus-off | The `can_bus` task calls `twai_node_recover()` at once, then backs off 250 ms doubling to 5 s between attempts; `bus_recoveries` in the CAN statistics counts it. |
| A command is not acknowledged | Each frame waits at most 200 ms; a failure is logged and counted in `failed`. The command is not retried. A full 8-deep command queue answers `503 "the bus is busy"` instead of applying the command later. |

The state model keeps the last values it decoded; staleness is expressed through the bus
availability topic and the phase, not by clearing values. Details:
[CAN bus](/protocol/can-bus), [Polling](/protocol/polling).

### Wi-Fi is down

The provisioning state machine (`components/comfoair_provision/include/comfoair_provision.h`)
classifies every disconnect into one of four verdicts — `network-not-found`, `wrong-password`,
`refused`, `no-address` — shown in `GET /api/status` with the raw Wi-Fi reason code beside them.
Then:

- The station retries with a backoff of 1 s doubling to 30 s, forever.
- After **30 minutes** of failure of the first three kinds, the device raises its own open access
  point again (fallback, in AP+STA mode) while the station keeps trying underneath; the access
  point is bounded by the setup window.
- A station that associates but never gets an address does **not** raise the access point by
  itself. That fact is persisted, and the next boot offers a 5-minute setup window instead.
- New credentials are on trial for 90 s; if they produce no address, the last known-good pair is
  restored automatically. A pair becomes the known-good one only once it has produced an address
  on the owner's network — saving it is not enough. A first provisioning has no known-good pair
  and cannot roll back. This covers the likeliest catastrophe of all, which is not an attack but a
  typo: without it, a mistyped key in the settings would overwrite a working pair, the access point
  would stay down because credentials exist, and the device would vanish until someone reached its
  button.
- `POST /api/provision` is answered **before** the connection starts
  (`comfoair_net_provision_body()` returns without connecting, then the handler replies). Joining
  the router moves the radio to the router's channel and the access point with it, so the phone
  that sent the form is about to lose the device; a reply that waited for the connection would make
  a provisioning that worked look like a failure.
- A short button press opens the setup window from any state, without erasing anything.

The invariant behind these rules is that there is never a state with no network, no access point
and no way back. Meanwhile the CAN side keeps running, and the MQTT ladder stands down without
counting attempts while there is no IP address. While any open access point is on the air the
device counts as unclaimed, which restricts writes — see [Security](/architecture/security).
Details: [Networking](/device/networking).

### The broker is unreachable

`comfoair_mqtt` turns every failed attempt into a failure class and waits according to
`components/comfoair_mqtt_backoff/include/comfoair_mqtt_backoff.h`:

| Class | Cause | Delay before the next attempt |
| --- | --- | --- |
| `dns`, `tcp`, `unavailable` | name did not resolve; no route, refused, timed out; CONNACK 3 | 2 s, 4 s, 8 s … ceiling 60 s |
| `dropped` | an established session was lost | 1 s, 2 s, 4 s … ceiling 60 s |
| `protocol` | CONNACK 1 or 2 | 300 s flat |
| `bad-credentials` | CONNACK 4 or 5, or an unknown code | 300 s flat |

Every delay is jittered by ±20 % with a seed derived from the device MAC, so devices in one house
do not retry in step. Refusals wait five minutes because a broker that sees a wrong password every
few seconds may ban the address. The attempt counter resets only after a session has lasted 60 s,
or when the owner saves new broker settings.

What the owner sees: the `mqtt` block of `GET /api/status` (`link` = `off`, `connecting`,
`online`, `backoff` or `rejected`; `failure`, `attempts`, `retry_in_ms`, `last_connack`,
`last_transport_errno`, `disconnects`, `reconnects`); phase `broker-rejected` immediately on a
refusal, and phase `no-broker` once a configured broker has been unreachable for 30 s. With no
broker configured, nothing is dialled and nothing is reported as a fault.

On the broker side the device's last will publishes `offline`, retained, to `<prefix>/status`.
On reconnection the device publishes `online`, its state, and — only when owed — the retained
discovery tree. Discovery is owed when the client was rebuilt (new settings) or when the session
dropped while the device still had an IP address, which means the broker side failed and may have
lost its retained store. A retained message on a command topic is refused, so a stale command
cannot replay on every reconnect. Details: [MQTT](/interfaces/mqtt).

### Home Assistant is gone

Nothing changes on the device. It talks only to the broker; it does not subscribe to
`homeassistant/status` and does not need to. Discovery documents and every state but the
freshness entity's are retained on the broker, so a restarted Home Assistant picks them up from
there. The REST API and web UI keep
working, and so does control over REST.

### NVS errors

| Situation | What the device does |
| --- | --- |
| `nvs_flash_init()` reports no free pages or a new NVS version | The partition is erased and initialised again; settings are lost, function is not. The erase is logged (`NVS unusable (…); erasing`) into the RAM log of that boot only — no marker survives a later reboot, so the loss is visible in `GET /api/log` until the ring rolls over or the device restarts. Any other init error aborts the boot. |
| The settings cannot be read | Defaults are used and logged (`settings did not read`); with no Wi-Fi pair the device starts its setup access point. |
| A stored value is invalid (partial write, bit flip, older firmware) | `comfoair_config_sanitize()` replaces that field with its default and the log names the field, never the value (`stored setting '<name>' was not usable and has been reset to its default`). A stored value that does not fit this build's field is reported the same way, although its default would pass every validator: settings that vanish silently are indistinguishable from a firmware that reset itself. A wrecked Wi-Fi record is cleared, which leads to the access point. |
| The stored schema is newer than this firmware (after a rollback) | The document is read as far as understood and becomes read-only: `read_only` is `true` in `GET /api/config` and `POST /api/config` answers `409`; the Wi-Fi form (`POST /api/provision`) still works. The 15 s button reset is the way out. |
| A duration or the manual-mode setting cannot be written | The new value takes effect in memory and will not survive a reboot. Over REST the write is answered as accepted and the failure is logged; over MQTT a duration write is counted as rejected and the state model is not updated, although the sender already uses the new value. |

The Wi-Fi SSID and key are always written as one blob under one key, so a power cut cannot leave
a new SSID with an old password. Details: [Configuration storage](/device/config-storage).

### An OTA upload fails

| Failure | Result |
| --- | --- |
| No `Content-Length`, or a length of 0 | `411`; nothing touched. |
| Image too small, not an ESP image, wrong chip, another project's firmware | `422`; refused from the first bytes, before anything is erased. |
| Larger than the update slot | `413`. |
| Client stopped mid-upload | `400`; the write is aborted and the running image stays the boot image. |
| Another upload running, or the running image still on probation | `409`; the same upload succeeds later. |
| `esp_ota_end()` reports a validation failure, or set-boot-partition fails | `422`; nothing changes. Any other `esp_ota_end()` error is `500`. |
| Flash error, no update slot | `500`. |
| The new image crashes or restarts before it is confirmed (30 s after its web server started) | The bootloader boots the previous slot (`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`). |

During a write the phase is `ota`, which outranks every other phase. Rollback lives in the
bootloader, which an OTA never replaces, so it only protects a device whose bootloader was flashed
with this setting. Details: [OTA and system](/device/ota-and-system).

### The browser is absent

Nothing depends on it. The WebSocket task keeps running with no clients; a client that reconnects
gets a full state document first.
