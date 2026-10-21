# OTA and system

Firmware updates go over HTTP as a `.bin` upload; there is no network-pull OTA. This page covers
these components and files:

| Component | Role |
| --- | --- |
| `comfoair_ota` | **pure** pre-flight checks on the upload and the fault-to-HTTP mapping; host suite `test/test_ota` |
| `comfoair_ota_runtime` | confirming a freshly booted image (rollback), and restarting into a new one |
| `comfoair_http_ota.c`, `comfoair_http_ota_preflight.c` (in `comfoair_http`) | `POST /api/ota`: streams the body into the inactive slot |
| `comfoair_sysinfo` | **pure** renderer of `GET /api/system`; host-tested. Filled by `comfoair_http_system.c` |

The owner procedure is in [Updating](/guide/updating).

## Partition layout

`partitions.csv`, for the NanoC6's 4 MB flash:

| Name | Type | Offset | Size |
| --- | --- | --- | --- |
| `nvs` | data/nvs | `0x9000` | 16 KiB |
| `otadata` | data/ota | `0xD000` | 8 KiB |
| `phy_init` | data/phy | `0xF000` | 4 KiB |
| `ota_0` | app | `0x10000` | 1984 KiB (`0x1F0000`) |
| `ota_1` | app | `0x200000` | 1984 KiB (`0x1F0000`) |

There is no filesystem partition: the web UI is compiled into the application image
(`components/web_assets`). 64 KiB at the top of flash is unused.

::: warning `ota_0` must stay at `0x10000`
The rationale recorded in `partitions.csv`: PlatformIO assembles `firmware.factory.bin` with the
application at `0x10000` regardless of the table. The stock two-OTA table's larger NVS pushes the
first app slot to `0x20000` and the merge then fails with an overlap. That merged image is what
the browser installer flashes, so NVS is kept at 16 KiB to keep `ota_0` where it is.
:::

## Upload flow

`POST /api/ota` with the raw `.bin` as the request body (no multipart) and a `Content-Length`.
The access policy only allows it on a device **with a UI password set** and an authenticated
request; without a password it is `403`. There is no signature check (secure boot is off), so
anyone who can authenticate can install code; the checks below protect against the wrong file,
not a hostile one.

1. **Gate.** One upload at a time (`409` busy; `500` if the 4 KiB upload buffer cannot be
   allocated). Refused with `409` while the running image is still on probation (see
   [Rollback](#rollback)). `500` if there is no update slot.
2. **Length** (`comfoair_ota_check_length()`): no `Content-Length` (or a length of 0) is `411`; under 64 KiB is `422`;
   larger than the inactive slot is `413`.
3. **Pre-flight on the first 112 bytes** (`0x70`), buffered before anything is erased:
   - byte 0 must be the image magic `0xE9` (`422`, catches `.bin.gz`, ELF, HTML);
   - the chip id at bytes 12–13 must equal the running chip's (`CONFIG_IDF_FIRMWARE_CHIP_ID`;
     `422` for an image built for another chip);
   - the app descriptor at `0x20` must carry magic `0xABCD5432` and a `project_name` equal to the
     running firmware's (`comfoair-q`); otherwise `422`, "firmware for a different device".
     The image's version string is read from offset `0x30` for the log and the answer.
4. **Write.** The LED switches to the OTA overlay and the phase to `ota`. `esp_ota_begin()` on the
   inactive slot (`OTA_WITH_SEQUENTIAL_WRITES`; a refusal is `409` if ESP-IDF reports the rollback
   state, otherwise `500`), then 4 KiB chunks through `esp_ota_write()` (a failure is `422` if
   ESP-IDF reports a validation failure, otherwise `500`). Progress is logged and shown on the LED
   at 25, 50 and 75 %.
5. **Verify.** `esp_ota_end()`, where ESP-IDF validates the written image; then
   `esp_ota_set_boot_partition()`. An `esp_ota_end()` failure is `422` when ESP-IDF reports a
   validation failure (`ESP_ERR_OTA_VALIDATE_FAILED`), otherwise `500`; **any**
   `esp_ota_set_boot_partition()` failure is `422`, whatever the error
   (`components/comfoair_http/comfoair_http_ota.c`, `COMFOAIR_OTA_VERIFY_FAILED`). In every case
   the boot partition is unchanged and the running firmware keeps running.
6. **Answer, then restart.** The handler sends
   `{"ok":true,"rebooting":true,"partition":"ota_1","bytes":…,"version":"…"}` (the version is the
   uploaded image's) and a short-lived task restarts the chip 1000 ms later. The busy flag stays set
   until the restart. If that task cannot be created the answer has already gone, the OTA overlay
   and phase are cleared, the new image boots at the next restart, and because the busy flag stays
   set every further upload is refused as busy until then.

Transfer limits: the whole upload has a deadline of `size / 8 KiB/s`, at least 120 s; each receive
retries a socket timeout up to 3 times. A client that stops early gets `400` (truncated) and the
slot write is aborted. A refused upload's remaining body is drained (up to 16 MiB within 30 s) so
the client can read the error; beyond that the connection is closed.

There is no version comparison on the device: any image of the same project is accepted, including
an older one. The Update page reads the version out of the chosen file, shows *installed → this
file* and warns when the file is older (`web/src/pages/update/semver.ts`, `Update.tsx`), but a
downgrade is allowed: it is the owner's manual rollback.

### Downgrades

- **No anti-rollback eFuse.** `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` (the `secure_version` check) is
  deliberately left off (`sdkconfig.defaults`): it burns eFuses against downgrades and would make
  the owner's manual downgrade impossible.
- **Over the network, only to a build that contains `comfoair_ota_runtime`.** An older build
  without it never confirms itself. The bootloader, built with rollback enabled, finds the image
  still `PENDING_VERIFY` at the next boot, marks it `ABORTED` and boots the other slot
  (`bootloader_utility.c` in ESP-IDF 5.5.5). Such a build also has no `POST /api/ota`. Going back
  to a build older than the OTA support therefore needs USB.

## Rollback

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y`. An image booted for the first time after an update is
`PENDING_VERIFY`; if it restarts before being confirmed, the bootloader boots the previous slot.

- `comfoair_ota_runtime_start()` is called from `app_main` **only if the HTTP server started**. If
  the running image is `PENDING_VERIFY`, it arms a one-shot `esp_timer` that calls
  `esp_ota_mark_app_valid_cancel_rollback()` `COMFOAIR_OTA_CONFIRM_DELAY_MS` = 30 s after that call
  (not 30 s of uptime).
- An image whose HTTP server never starts is never confirmed and rolls back at the next restart:
  it could not accept the next update.
- Confirmation does **not** wait for the CAN unit, Wi-Fi or the broker. An absent peer must not
  decide that the firmware is broken; what rollback catches is an image that crashes or
  boot-loops.
- `comfoair_ota_runtime_confirm_now(reason)` stops the timer and confirms immediately. Its one
  caller is the button reset, which would otherwise turn a reset inside the 30 s into a downgrade.
  It acts whenever the image is still marked unconfirmed, and that mark is set as soon as
  `comfoair_ota_runtime_start()` finds the image `PENDING_VERIFY`, before the timer is created: if
  creating or starting the timer then fails, the image stays unconfirmed (and rolls back at the
  next restart) unless a button reset confirms it first. On an image whose HTTP server never
  started, `comfoair_ota_runtime_start()` never ran and it confirms nothing.
- While unconfirmed, new uploads are refused (`409`).
- **Known limit, not a defect.** Confirmation is by elapsed time alone; there is no health check
  beyond the HTTP server having started. An image that crashes or misbehaves **after** it confirmed
  itself stays installed. Recovery is USB, or uploading a fix right after one of its boots, while
  it is still up.

::: tip The rollback flag lives in the bootloader
An OTA update never replaces the bootloader. A device whose bootloader was built without the
rollback option has to be flashed over USB once before any of this applies.
:::

The two deliberate `esp_restart()` calls in the firmware are this one (into a new image) and the
button reset.

## Version string

The version is SemVer 2.0.0 derived only from the `v*` git tag by `tools/version.py`; there is no
version file. `tools/version.cmake` sets `PROJECT_VER` before `project(comfoair-q)`, so it lands in
`esp_app_desc_t.version`.

| Tree | Version |
| --- | --- |
| exactly on `v0.1.0`, clean | `0.1.0` |
| N commits after `v0.1.0` | `0.1.1-dev.N+g<sha7>` |
| the same, dirty | `0.1.1-dev.N+g<sha7>.dirty` |
| N commits after a pre-release `v0.2.0-rc.1` | `0.2.0-rc.1.dev.N+g<sha7>` |
| no `v*` tag | `0.0.0-dev.N+g<sha7>` |
| no git (source archive) | `0.0.0+unknown` |

A dirty tree on the tag is `…-dev.0+g<sha7>.dirty`, never the release number. A version longer than
31 characters (the size of `esp_app_desc_t.version`) or not valid SemVer fails the CMake configure.
`tools/pio_version.py` (a PlatformIO pre-build hook) forces a re-configure whenever the computed
version differs from the one last built, because PlatformIO does not re-run CMake on git changes
by itself. See [Releasing](/development/releasing).

The running version appears in the boot log, `GET /api/system` (`device.firmware`), the device
identity in `GET /api/status` (`sw_version`) and the mDNS TXT `ver`. The OTA answer carries the
uploaded image's version, read from its app descriptor.

## `GET /api/system`

Open to anyone while no UI password is set, otherwise authenticated, like every `GET`. Built by
`comfoair_http_system_get()` from a snapshot of each owning component and rendered by
`comfoair_sysinfo_render()`.

| Section | Fields |
| --- | --- |
| `unit` | the ventilation unit as last read over RMI by the probe task: `state` (`ok`, `no_answer`, `unknown`), `age_ms`, `model`, `serial`, `firmware` (decoded, e.g. `R1.11.0`), `firmware_raw`, `article`, `country`, `name`, and `errors` with `state`, `ids` and `items` (`id` plus English `text`, or `null` for an unknown id) |
| `device` | `firmware`, `idf`, `built`, `board`, `chip`, `chip_revision`, `flash_bytes`, `partition` (running slot), `uptime_ms`, `reset_reason`, `heap_free`, `heap_min_free`, `heap_largest_block`, `device_id`, `mac` |
| `network` | `connected` (the station holds an address), `ssid`, `ip`, `rssi`, `channel` (each `null` when not connected) |
| `bus` | `started`, `state` (`stopped` when the driver is not started, otherwise `active`, `warning`, `passive` or `off`), `rx_frames`, `rx_dropped`, `bus_errors`, `bus_recoveries`, `tx_error_count`, `rx_error_count`, `nodes_seen` (CAN node ids heard, at most 64) |
| `commands` | `queued`, `sent`, `failed`, `dropped` |
| `mqtt` | `configured`, `connected`, `published`, `commands`, `rejected`, `reconnects` |

`reset_reason` is one of `power_on`, `software`, `panic`, `watchdog`, `brownout`, `deep_sleep`,
`external`, `usb`, `jtag`, `unknown`. Unit error texts come from a table selected by the unit's
firmware version (one table above R1.4.0, another at or below it; the newer one when the version
is unknown). A string field the unit never answered is `null`. Full schema:
[REST API](/interfaces/rest-api).

The identity is read about 10 s after boot, every 60 s until the model, the three settings and the
firmware word have all answered, then every 6 h. `unit.errors` is refreshed more often: between two
full reads the probe task sends the errors read (`80 03 01`) alone every 60 s
(`comfoair_unit_cadence`). An unanswered errors read sets `errors.state` to `no_answer`; the last
bitfield is kept internally, but `ids` and `items` render empty until the next answered read, while
the two entities below keep the values they last published. The same answer is also published as two entities, `unit_error` and `active_errors`
(see [Errors and alarms](/unit/areas/errors-and-alarms)) — but only once the firmware word is known,
because without it no id can be classed informational; until then both are `null`.
