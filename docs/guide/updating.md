# Updating

After the first install over USB, the firmware is updated over the network by uploading the new
image in the device's own web interface. There is no cable and no build involved, and your settings
are kept.

## Before you start

- **Set a web interface password.** Firmware upload (`POST /api/ota`) is refused on a device that
  has none: the firmware does not verify image signatures, so an open upload would let anyone on
  your network run their own code on it. The Update page sends you to Settings if no password is
  set.
- **The device must be on your network.** While its setup network is on the air, every write but
  the Wi-Fi form is refused, uploads included.
- **Get the image.** From a release, that is `comfoair-q-<version>-ota.bin`. From your own build, it
  is `firmware.bin` in `.pio/build/m5stack-nanoc6/`. Not the `.elf`, not a compressed file, and not
  `firmware.factory.bin`.

## Installing an update

1. Open **Update** in the web interface and pick the file.
2. Before anything is sent, the page reads the version out of the file and compares it with the
   running one: *newer*, *same version (reinstall)* or *older — the device will be downgraded*. A
   file built for a different firmware or device is refused on the spot.
3. Press **Install**. The page shows the upload progress, and the board's RGB LED shows it too: it
   lights **white** for a share of every second equal to the share written, in 25 % steps
   (`components/comfoair_http/comfoair_http_ota.c`).
4. The device checks the image as it arrives — first byte, target chip, project name, size against
   the update slot — and only then erases the spare slot. When the whole image is written, ESP-IDF
   verifies it (segments, checksum, SHA-256) before it is marked bootable.
5. The device restarts one second after answering. The page waits for it to come back, for up to
   90 seconds, and then reports *Updated: old → new*.

If the connection drops after the whole file was sent, the page says so and keeps waiting to see
which version comes back.

### When an upload is refused

The device answers with a sentence the page shows as is (`components/comfoair_ota/comfoair_ota.c`):

| Message | Cause |
| --- | --- |
| This is not a firmware image… | Not an ESP application image — an `.elf`, a compressed file, a web page |
| This firmware was built for a different chip. | An image for another ESP32 variant |
| This file is firmware for a different device… | A valid image of another firmware |
| The file is too small to be a firmware image. | Under 64 KiB |
| The file is larger than the update slot. | Over the 1984 KiB slot |
| Another update is already being uploaded. | A second upload while one is running |
| The running firmware was just updated and is still being verified… | The updated image's web server has been up for less than 30 s; see below |
| The upload stopped before the whole file arrived… | The connection broke mid-upload |
| The uploaded image failed verification… | The final ESP-IDF check rejected it |

In every refused case the running firmware is unchanged.

## Confirmation and rollback

The flash holds two application slots of 1984 KiB each (`partitions.csv`). An update is written to
the slot that is not running, and the running one stays intact until the new one has proved itself.

A freshly updated image boots **on probation**. **30 seconds** after its web server has started,
it confirms itself (`components/comfoair_ota_runtime`; the timer is armed only once the web server
is up, `src/main.cpp`). If it crashes or restarts before that,
the bootloader boots the **previous** version instead — automatically, with nothing to do on your
side.

- Confirmation does not wait for Wi-Fi, the broker or the ventilation unit. A missing peer must not
  turn a working update into a rollback.
- During those 30 seconds a further upload is refused with *still being verified; try again in a
  minute*.
- If the page reports that the device came back on the **old** version, the update was rolled back.
  The **System** page shows which partition is running.

::: tip Rollback needs the right bootloader
Rollback is a bootloader feature, and an update over the network never replaces the bootloader. It
works on a board whose bootloader came from this firmware's installer or build
(`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`).
:::

## Settings across updates

Updates keep every setting: Wi-Fi, the broker, the name, the password and the durations live in the
NVS partition, which an update does not touch.

Downgrading is allowed. If an older version cannot understand the layout of settings written by a
newer one, it keeps working with what it can read and refuses to save the broker and device
settings (the Wi-Fi form still works) until you reinstall the newer version or clear them with the
[15-second button hold](/guide/troubleshooting#the-button).

Two limits on downgrades:

- **Only to a version that can update over the network itself.** A build from before network
  updates existed cannot confirm itself, so the device rolls back to the version you came from on
  its next restart; such a build also has no upload endpoint. To go back that far, use USB.
- **Nothing blocks a downgrade for good.** The chip's anti-rollback fuse is deliberately not used:
  it would make going back to an older version impossible.

## When an update misbehaves later

The automatic rollback only covers the first 30 seconds. An image that confirmed itself and then
crashes or misbehaves stays installed. Recover by [flashing over USB](#updating-over-usb), or by
uploading a fixed or older image right after one of its restarts, while its web interface is up.

## Where the version is shown

The version is a SemVer string stamped into the image at build time (`tools/version.py`; a release
built from tag `v0.1.0` reports `0.1.0`). It appears:

- on the **System** page, under *This device → Firmware*, together with the running partition;
- in `GET /api/system`;
- on the device card in Home Assistant, as its firmware version.

## Updating over USB

When the device cannot be reached over the network, flash it again with the
[browser installer](/guide/installation#flashing-with-the-browser-installer) and leave **Erase
device** unticked to keep the settings, or from a source checkout with
`pio run -e m5stack-nanoc6 -t upload`.

Remember that the board's button is also the download strap: holding it while power comes up enters
the bootloader, which is how a USB flash is forced when nothing else connects.

More on the partitions, the confirmation timer and the system summary:
[OTA and system](/device/ota-and-system).
