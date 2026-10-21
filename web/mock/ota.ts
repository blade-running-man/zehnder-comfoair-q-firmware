// web/mock/ota.ts
// POST /api/ota, the Update page's upload: the raw image as the body, judged the way
// comfoair_http_ota.c judges it, then a simulated reboot.
//
// The access policy has already run (router.ts): no password is 403, no login 401 -- both are
// the firmware's comfoair_http_policy.c and need nothing here. What is here is the pre-flight
// (comfoair_ota.c): the length first, then the header's magic byte, then the app descriptor (its
// magic and project_name "comfoair-q"). The chip id is NOT checked, so a developer can exercise
// the success path with any image of this firmware -- a real firmware.bin, or the crafted one in
// scenarios.md.
//
// After a 200 the device is gone for REBOOT_MS (every request dropped, as an absent device is),
// then evolve.ts boots it into `bootInto`: the image's own version on the other slot, on
// probation for PROBATION_MS -- or, with the `rollback` knob, the old version on the old slot,
// which is what the bootloader does with an image that never confirmed itself.
//
// The descriptor is read with the page's own parser (src/pages/update/image.ts): one reading of
// the offsets, pinned to a real firmware.bin prefix by that directory's suite.
import { isThisFirmware, parseAppDesc } from "../src/pages/update/image.ts";
import { logLine, type MockResponse, type MockState } from "./model.ts";
import { OTA, error } from "./wire.ts";

export { PREFLIGHT_BYTES } from "../src/pages/update/image.ts"; // COMFOAIR_OTA_PREFLIGHT_BYTES
const HEADER_BYTES = 24;                 // COMFOAIR_OTA_HEADER_BYTES
export const MIN_IMAGE_BYTES = 64 * 1024; // COMFOAIR_OTA_MIN_IMAGE_BYTES
export const SLOT_BYTES = 0x1f0000;       // partitions.csv: each OTA slot is 1984 KiB
export const REBOOT_MS = 5000;
export const PROBATION_MS = 30_000;       // comfoair_ota_runtime: confirmed after 30 s of uptime

/** `head` is the first bytes of the body (the plugin passes at most PREFLIGHT_BYTES of them);
    `rawLength` is the whole body's size, which stands in for Content-Length. */
export function postOta(s: MockState, rawLength: number, head: unknown): MockResponse {
  // "Not now" before "not this file", so a retry a minute later is the advice the owner gets.
  if (s.probationLeftMs > 0) return error(409, OTA.unconfirmed);
  if (rawLength <= 0) return error(411, OTA.noLength);
  if (rawLength < MIN_IMAGE_BYTES) return error(422, OTA.tooSmall);
  if (rawLength > SLOT_BYTES) return error(413, OTA.tooLarge);
  if (!(head instanceof Uint8Array) || head.length < HEADER_BYTES || head[0] !== 0xe9) {
    return error(422, OTA.notImage);
  }
  const desc = parseAppDesc(head);
  if (!isThisFirmware(desc)) return error(422, OTA.wrongProject);
  if (s.scenario.ota === "fail") {
    logLine(s, `ota: image of ${rawLength} B failed verification`);
    return error(422, OTA.verifyFailed);
  }

  const target = s.partition === "ota_0" ? "ota_1" : "ota_0";
  s.bootInto = s.scenario.ota === "rollback"
    ? { swVersion: s.swVersion, partition: s.partition, probation: false }
    : { swVersion: desc.version, partition: target, probation: true };
  s.rebootLeftMs = REBOOT_MS;
  logLine(s, `ota: image ${desc.version} of ${rawLength} B written to ${target}, rebooting`);
  return {
    status: 200,
    body: { ok: true, rebooting: true, partition: target, bytes: rawLength, version: desc.version },
    restart: true,
  };
}

/** One tick of the reboot and the probation clock (called from evolve.ts's tick). */
export function stepOta(s: MockState, dtMs: number): void {
  if (s.probationLeftMs > 0) s.probationLeftMs = Math.max(0, s.probationLeftMs - dtMs);
  if (s.rebootLeftMs <= 0) return;
  s.rebootLeftMs = Math.max(0, s.rebootLeftMs - dtMs);
  if (s.rebootLeftMs > 0 || s.bootInto === null) return;
  const boot = s.bootInto;
  s.bootInto = null;
  s.swVersion = boot.swVersion;
  s.partition = boot.partition;
  s.probationLeftMs = boot.probation ? PROBATION_MS : 0;
  s.uptimeMs = 0;
  s.probe = null;  // the report lives in RAM: a reboot forgets it
  logLine(s, `main: booted ${boot.swVersion} from ${boot.partition}`);
}
