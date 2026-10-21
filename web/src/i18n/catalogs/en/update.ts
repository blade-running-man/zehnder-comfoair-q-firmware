// Firmware update page (pages/update).
//
// THIS object defines the namespace's key set. Every key added here must be added to
// ../de/update.ts, ../nl/update.ts and ../uk/update.ts too: those are typed
// `Record<keyof typeof update, Message>` against this object, so tsc fails the build in each of
// them until it is. Prefix every key "update." -- en.ts spreads all namespaces into one object,
// and a key two namespaces share would silently overwrite one of them (catalogs.test.ts fails
// on that). A count-dependent message is a plural object ({ one, other } in English); the
// other languages supply their own categories, which catalogs.test.ts checks.
//
// The update.detail.* values are comfoair_ota_message()'s sentences BYTE FOR BYTE
// (components/comfoair_ota/comfoair_ota.c): firmwareText.ts matches the device's answer against
// them, and the page's own pre-flight shows the same key, so a file refused before it uploads
// reads exactly as the device would have refused it. DO NOT reword the English side alone.
import type { Message } from "../../format.ts";

export const update = {
  "update.heading": "Firmware Update",
  "update.title": "Install firmware",
  "update.instructions": "Pick the firmware.bin built for this device (.pio/build/<env>/firmware.bin). The device checks the file, writes it into its spare slot and restarts.",
  "update.fileAria": "Firmware .bin file to install",
  "update.file": "{name} · {kib} KiB",
  "update.install": "Install",
  "update.uploading": "Uploading… {percent} % ({sent} of {total} KiB)",
  "update.uploadDone": "Upload complete; the device is installing the image.",
  "update.restarting": "Installing… the device is restarting.",
  "update.uncertain": "The connection dropped after the whole file was sent. Waiting to see whether the device installed it.",
  "update.waiting": "Waiting for the device to answer again ({seconds} s).",
  "update.updated": "Updated: {before} → {after}",
  "update.backRunning": "The device is back, running {after}.",
  "update.confirmNote": "The new firmware confirms itself after 30 s of healthy running; if it fails before that, the device returns to the previous version automatically.",
  "update.same": "The device is back, but it still runs {version}. The update may have been rolled back.",
  "update.systemLink": "The System page shows which partition is running.",
  "update.openSystem": "Open System",
  "update.gaveUp": "The device has not answered for 90 s. It may still be starting; reload this page in a minute, and check the System page once it answers.",
  "update.noPassword": "Firmware updates need a device password. Set one in Settings, then come back.",
  "update.noPasswordWhy": "Without a password anyone on your network could install their own code on this device.",
  "update.openSettings": "Open Settings",
  "update.login": "This device has a password. Log in when the browser asks, then try again.",
  "update.refused": "The device refused the update: {detail}",
  "update.failed": "Upload failed — the connection to the device was lost.",
  "update.toastDone": "Firmware updated",
  "update.compare": "Installed: {installed} → This file: {image}",
  "update.fileVersion": "This file: {image}",
  "update.verdict.newer": "This is a newer version",
  "update.verdict.same": "This is the same version (reinstall)",
  "update.verdict.older": "This is an OLDER version — the device will be downgraded. Settings are kept.",

  "update.detail.noLength": "The upload did not say how large the file is; send it with a Content-Length.",
  "update.detail.tooSmall": "The file is too small to be a firmware image.",
  "update.detail.tooLarge": "The file is larger than the update slot.",
  "update.detail.notImage": "This is not a firmware image; pick firmware.bin from .pio/build/<env>/, not the .elf or a compressed file.",
  "update.detail.wrongChip": "This firmware was built for a different chip.",
  "update.detail.truncated": "The upload stopped before the whole file arrived; try again.",
  "update.detail.verifyFailed": "The uploaded image failed verification and was not installed.",
  "update.detail.busy": "Another update is already being uploaded.",
  "update.detail.flashError": "Writing the update to flash failed; the running firmware is unchanged.",
  "update.detail.noSlot": "This device has no partition to install an update into.",
  "update.detail.unconfirmed": "The running firmware was just updated and is still being verified; try again in a minute.",
  "update.detail.wrongProject": "This file is firmware for a different device, not the ComfoAir Q controller.",

  // --- the USB fallback card ---
  "update.usb.title": "Over USB",
  "update.usb.body":
    "When the browser cannot reach the device, or for a first install, it is flashed over "
    + "USB:",
  "update.usb.strap":
    "Holding the button while powering the board enters the bootloader instead of "
    + "resetting anything — that pin is the download strap. The reset gesture is a hold "
    + "{after} the board has started.",
  "update.usb.strapEm": "after",
} satisfies Record<string, Message>;
