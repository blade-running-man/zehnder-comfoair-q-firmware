# 08 · OTA and system

Firmware upload over `POST /api/ota`, confirmation and rollback, and installation through ESP Web
Tools. The file checks (`test_ota`) and the policy (`test_http_policy`) are green on the host, but
no byte has yet been written to a slot on the board. What this scenario must show above all: **the
running firmware survives every failed upload, and a new image that dies before it is confirmed is
rolled back.**

An upload changes only the inactive slot (`ota_0` or `ota_1`) and `otadata`; the bootloader, the
partition table and NVS are untouched — which is why rollback works only after one USB flash with
a bootloader built with rollback enabled. Keep the USB cable at hand: if something goes wrong, USB
is the only way back. As-built: [OTA and system](/device/ota-and-system), [Updating](/guide/updating).

## Before the run, and the first USB flash

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-01 | Flash over USB, bootloader included, with the whole log: `~/.platformio/penv/bin/pio run -e m5stack-nanoc6 -t upload -t monitor` | The bootloader log has no errors; the application logs `comfoair_ota: running ota_0, state valid` (otadata is blank after USB, and the bootloader writes `otadata[0]` = VALID before the application starts) | serial log | USB |
| HV-OTA-02 | Prepare four wrong files: `firmware.elf` from `.pio/build/m5stack-nanoc6/`; `firmware.bin.gz` (`gzip -k firmware.bin`); a `firmware.bin` built for any ESP32-C3 board; an ESP32-C6 `firmware.bin` built from any other ESP-IDF project — same chip, another project | Four files ready for HV-OTA-15..18 | — | laptop |
| HV-OTA-03 | No UI password set: `curl -si -X POST --data-binary @firmware.bin http://<ip>/api/ota` | `403 {"error":"forbidden"}`; flash untouched | `curl` | — |
| HV-OTA-04 | The Update page without a password | A clear message — "Firmware updates need a device password. Set one in Settings, then come back." — not a network error | browser | — |
| HV-OTA-05 | Set a password in Settings; reopen Update (the browser asks for it) | The Update page is usable | browser | — |
| HV-OTA-06 | Password set, request without credentials | `POST /api/ota` → `401` | `curl -si` | — |

## A good update

Build a firmware with a visibly different version (for example a later commit), without flashing
it over USB.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-07 | Update → choose `.pio/build/m5stack-nanoc6/firmware.bin` → upload | Progress grows in the UI; the log shows `upload of N bytes into ota_1 (… bytes)`, `image <version> (comfoair-q) -> ota_1`, `25% (…)`, `50% (…)`, `75% (…)`, `installed N bytes into ota_1 in … ms` | browser; serial log | USB |
| HV-OTA-08 | The answer | `200 {"ok":true,"rebooting":true,"partition":"ota_1","bytes":N,"version":"<version>"}` with the **new** image's version; about 1 s later `restarting into the new image` | browser dev tools or `curl -u :<pw>` | — |
| HV-OTA-09 | After the restart | `GET /api/system`: `device.firmware` is the new version, `device.partition` = `ota_1`; the page showed the new version by itself | `curl -s -u :<pw> http://<ip>/api/system` | — |
| HV-OTA-10 | Log right after that boot | `running ota_1, state pending_verify`, `image on probation; confirming in 30000 ms` | serial log | USB |
| HV-OTA-11 | 30 s later | `image confirmed after 30000 ms; rollback cancelled` | serial log | USB |
| HV-OTA-12 | Upload the same image again | It goes to `ota_0`; afterwards `partition` = `ota_0` — the slots alternate | `GET /api/system` | — |
| HV-OTA-13 | Upload again within the first 30 s after an update | `409`, "The running firmware was just updated and is still being verified; try again in a minute."; flash untouched | browser; `curl` | — |
| HV-OTA-14 | Time the upload | Record N bytes in T ms. Over 60 s: report it (Wi-Fi, weak signal) | serial log | — |

## A wrong file is refused before anything is written

For each: the answer comes as **text**, not as a reset connection; the log has `upload refused: …`
and **no** `esp_ota_begin`; afterwards `GET /api/system` shows the same partition and version.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-15 | `firmware.elf` (~12.7 MB) | `413` "The file is larger than the update slot." The answer comes after the browser has sent the file (the server drains up to 16 MiB within 30 s); the UI waits without a network error | browser; serial log | — |
| HV-OTA-16 | `firmware.bin.gz` | `422` "This is not a firmware image; pick firmware.bin from .pio/build/&lt;env&gt;/, not the .elf or a compressed file." | browser | — |
| HV-OTA-17 | the ESP32-C3 `firmware.bin` | `422` "This firmware was built for a different chip." | browser | — |
| HV-OTA-18 | the other project's C6 `firmware.bin` | `422` "This file is firmware for a different device, not the ComfoAir Q controller." The log shows `image … is not comfoair-q firmware`, then `upload refused: …`; no `esp_ota_begin`, no OTA picture on the LED. The chip check passes; the `project_name` check refuses | browser; serial log; LED | — |
| HV-OTA-19 | a text file under 64 KiB | `422` "The file is too small to be a firmware image." | browser | — |
| HV-OTA-20 | `curl -u :<pw> -X POST -H 'Transfer-Encoding: chunked' --data-binary @firmware.bin http://<ip>/api/ota` | `411` "The upload did not say how large the file is; send it with a Content-Length." If instead a bare `400` from the server without that text: record it — `esp_http_server` refused chunked before the handler ran | `curl -si` | — |
| HV-OTA-21 | Throughout HV-OTA-15..20 | The rest of the UI answers after each refusal; the device never restarted | browser; `uptime_ms` | — |

## An interrupted upload

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-22 | Close the tab at about 50 % | `upload stopped at … of … bytes`; no restart; version and partition unchanged | serial log; `GET /api/system` | USB |
| HV-OTA-23 | Switch the laptop's Wi-Fi off at about 50 % | The same; after at most 20 s of silence (4 receive attempts × 5 s) the upload is declared cut; other clients' UI answers again | serial log | USB |
| HV-OTA-24 | Cut the NanoC6's power at about 50 % | After power returns the old firmware boots, `state valid` as before the upload | serial log | USB |
| HV-OTA-25 | A slow upload: `curl -u :<pw> --limit-rate 1k -X POST --data-binary @firmware.bin http://<ip>/api/ota` | After max(120 s, size / 8 KiB/s) — about 170 s for 1.3 MB — `upload cut off by its deadline at … of … bytes`; no restart; version and partition unchanged; the UI and `/api/status` answer straight after | serial log; `curl` | USB |
| HV-OTA-26 | A good upload after HV-OTA-22..25 | Goes through as in HV-OTA-07 | serial log | USB |

## Rollback of an image that dies

Build an image that crashes at start: temporarily `abort();` as the first line of `app_main` (do
not commit), `pio run -e m5stack-nanoc6`, and upload that `firmware.bin` through Update.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-27 | Upload the crashing image | `200`, restart; the new image aborts (`abort()` in the log) | serial log | USB |
| HV-OTA-28 | The next boot | The bootloader marks the slot aborted and boots the previous one; the application logs the old version and `state valid` | serial log | USB |
| HV-OTA-29 | `GET /api/system` | The previous `firmware` and `partition` | `curl` | — |
| HV-OTA-30 | Variant: an image that crashes at second 10 (`vTaskDelay(10 s); abort();` in a separate task) | The same rollback — confirmation comes only at 30 s | serial log | USB |
| HV-OTA-31 | Remove the `abort()`, rebuild, upload | An ordinary update, as in HV-OTA-07..11 | serial log | USB |

## The device during an upload

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-32 | The dashboard open in a second tab | The push pauses while the upload runs (one HTTP task serves everything) and reconnects by itself afterwards | browser | — |
| HV-OTA-33 | The bus during the flash write | PDOs keep arriving; `bus.rx_dropped` in `/api/system` does not grow noticeably (erasing a sector disables the cache for tens of ms) | `GET /api/system` before and after | unit |
| HV-OTA-34 | MQTT / HA during the upload | Values in HA keep updating | HA | unit, broker, HA |
| HV-OTA-35 | The unit's display | Works as usual, no communication error | unit display | unit |

## The LED during an upload, and the button on probation

The upload draws the OTA overlay: white, 1 s period, lit for the fraction written, updated at 0, 25,
50, 75 and 100 %. **At 0 % the pixel is dark** (that is how progress 0 is drawn); the heartbeat
continues.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-36 | A good upload, watching the pixel | After 25 %: white, lit ¼ of each second; 50 %: half; 75 %: ¾; after 100 %: steady white **until the restart** (it does not go out when the `200` is sent). Between uploads `/api/status` does not show phase `ota` | LED; `GET /api/status` | — |
| HV-OTA-37 | Interrupt an upload (as HV-OTA-22 or HV-OTA-25) | The white OTA picture disappears right after `upload stopped…` / `upload cut off…`; within 1 s the pixel shows the normal phase again | LED; serial log | USB |
| HV-OTA-38 | A wrong file (as HV-OTA-16) | No OTA picture at all — the refusal comes before `esp_ota_begin` | LED | — |
| HV-OTA-39 | Within 30 s after an OTA restart (`image on probation` in the log), hold the button ≥ 5 s and release (**soft reset — erases Wi-Fi and password**) | `image confirmed early: soft reset by button; rollback cancelled`, then the reset and restart; afterwards `GET /api/system` shows the **new** version and partition, `state valid` — **not** a rollback | serial log; `GET /api/system` | USB |
| HV-OTA-40 | The same with ≥ 15 s (**hard reset — erases everything**) | `image confirmed early: hard reset by button; rollback cancelled`; the new version after the restart | serial log | USB |
| HV-OTA-41 | A ≥ 5 s hold later than 30 s after an update | No `confirmed early` line (the image is already confirmed); an ordinary reset | serial log | USB |

## Installation through ESP Web Tools

Build, `python3 tools/make_release.py --allow-unreleased`, `python3 -m http.server -d dist 8000`,
open `http://localhost:8000` in Chrome or Edge ([Installation](/guide/installation)). Record
`GET /api/system` before HV-OTA-42.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-OTA-42 | Install on a configured device **without** ticking "Erase device" | The write completes; after the restart the device is back on the home Wi-Fi with its password and broker, HA sees it as before; `device.partition` = `ota_0` | `GET /api/system`; HA | USB, broker, HA |
| HV-OTA-43 | Install **with** "Erase device" ticked | After the restart the open `comfoair-xxxx` appears (the same name this device had before); `http://192.168.4.1` opens the setup | phone | USB, phone |
| HV-OTA-44 | After HV-OTA-42 and HV-OTA-43 | The version on the System page matches `version` in `dist/manifest.json` and the version on the installer page | browser | USB |
| HV-OTA-45 | If the port does not appear in Chrome's list | Holding the button while plugging the cable in makes the `USB JTAG/serial debug unit` port appear; after the write, replug without the button and the firmware starts. Record whether this was needed at all | Chrome | USB |
| HV-OTA-46 | After a 30-minute soak with MQTT connected, a browser WebSocket open and one OTA upload, read `GET /api/system`. The JSON buffer is a static 48 KiB (`COMFOAIR_API_DOC_BUFFER_SIZE`), so the heap is what is left after it; record the build's RAM line alongside | `device.heap_min_free` ≥ ~40 KB; record the value | `GET /api/system` | broker, browser |
