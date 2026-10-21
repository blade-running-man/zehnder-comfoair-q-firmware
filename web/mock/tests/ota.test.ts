// web/mock/tests/ota.test.ts
// POST /api/ota's pre-flight and the simulated reboot, against comfoair_ota.c's order and
// sentences: probation, then the length, then the header's magic byte, then the app descriptor.
//
//   node mock/tests/ota.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { handle } from "../router.ts";
import { tick } from "../evolve.ts";
import type { MockResponse, MockState } from "../model.ts";
import { MIN_IMAGE_BYTES, PROBATION_MS, REBOOT_MS, SLOT_BYTES } from "../ota.ts";
import { LOCKED_PASSWORD } from "../scenarios.ts";
import { OTA } from "../wire.ts";
import { REAL_PREFIX, prefix } from "../../src/pages/update/tests/fixture.ts";
import { basic, errorOf, fresh, get, post } from "./req.ts";

const AUTH = basic("", LOCKED_PASSWORD);
const IMAGE = 1_300_000;
// The body's first bytes as the plugin hands them over: a real firmware.bin's pre-flight prefix,
// with the version rewritten so each install is told apart.
const header = (first = 0xe9, len?: number): Uint8Array => prefix({ first }).subarray(0, len);
const image = (version: string): Uint8Array => prefix({ version });
const upload = (s: MockState, rawLength = IMAGE, body: unknown = image("0.2.0")): MockResponse =>
  handle({ method: "POST", path: "/api/ota", headers: AUTH, body, rawLength }, s);
const version = (s: MockState): unknown => (get(s, "/api/status", AUTH).body as Record<string, unknown>)?.sw_version;
const system = (s: MockState) => (get(s, "/api/system", AUTH).body as { device: { firmware: string; partition: string } }).device;
function run(s: MockState, ms: number): MockState {
  for (let t = 0; t < ms; t += 1000) s = tick(s, 1000);
  return s;
}

// --- the pre-flight, in the device's order ------------------------------------------------
{
  const s = fresh("locked");
  const r = (len: number, body: unknown = header()) => { const x = upload(s, len, body); return [x.status, errorOf(x)]; };
  eq(r(0), [411, OTA.noLength], "no body: 411, no length");
  eq(r(MIN_IMAGE_BYTES - 1), [422, OTA.tooSmall], "under 64 KiB: 422 too small");
  eq(r(SLOT_BYTES + 1), [413, OTA.tooLarge], "over the slot: 413 too large");
  eq(r(SLOT_BYTES + 1, header(0x1f)), [413, OTA.tooLarge], "the length is judged before the header");
  eq(r(IMAGE, header(0x1f)), [422, OTA.notImage], "a .bin.gz (1F 8B): 422 not an image");
  eq(r(IMAGE, header(0xe9, 10)), [422, OTA.notImage], "a short header: not an image");
  eq(r(IMAGE, null), [422, OTA.notImage], "no header bytes at all: not an image");
  eq(r(IMAGE, header(0xe9, 24)), [422, OTA.wrongProject], "a header with no descriptor after it: a different device");
  const badMagic = REAL_PREFIX.slice();
  badMagic[0x20] = 0;
  eq(r(IMAGE, badMagic), [422, OTA.wrongProject], "no descriptor magic: a different device");
  eq(r(IMAGE, prefix({ project: "opentherm-thermostat" })), [422, OTA.wrongProject],
    "the OpenTherm thermostat's image (a valid C6 app): a different device");
  eq(version(s), "0.1.0", "nothing refused changed the running version");
}

// --- accepted: answer, gone for ~5 s, back with the new version on the other slot -------------
{
  let s = fresh("locked");
  const before = system(s);
  eq([before.firmware, before.partition], ["0.1.0", "ota_0"], "starts as 0.1.0 on ota_0");
  const r = upload(s);
  eq([r.status, r.body, r.restart],
    [200, { ok: true, rebooting: true, partition: "ota_1", bytes: IMAGE, version: "0.2.0" }, true],
    "200 names the slot, the bytes and the image's version, and closes the push sockets");
  eq(get(s, "/api/status", AUTH).drop, true, "then /api/status is dropped: the device is rebooting");
  eq(post(s, "/__mock/scenario", {}).status, 200, "the mock control channel still answers");
  s = run(s, REBOOT_MS - 1000);
  eq(get(s, "/api/status", AUTH).drop, true, "still gone a second before the reboot ends");
  s = run(s, 1000);
  eq(version(s), "0.2.0", "back with the image's own version");
  const after = system(s);
  eq([after.firmware, after.partition], ["0.2.0", "ota_1"], "System shows it running from ota_1");
  ok((get(s, "/api/system", AUTH).body as { device: { uptime_ms: number } }).device.uptime_ms < 2000, "uptime restarted");
  const busy = upload(s);
  eq([busy.status, errorOf(busy)], [409, OTA.unconfirmed], "on probation: another image is 409 'still being verified'");
  s = run(s, PROBATION_MS);
  eq(upload(s, IMAGE, image("0.1.0")).status, 200, "after 30 s it confirmed itself and takes the next one -- a downgrade too");
  s = run(s, REBOOT_MS);
  eq([version(s), system(s).partition], ["0.1.0", "ota_0"], "the next image goes to the other slot again");
  eq(upload(s, IMAGE, REAL_PREFIX).status, 409, "on probation again");
  s = run(s, PROBATION_MS);
  eq(upload(s, IMAGE, REAL_PREFIX).status, 200, "a pre-SemVer image is accepted as well");
  s = run(s, REBOOT_MS);
  eq(version(s), "cda52b6", "and comes back with its bare git hash");
}

// --- the rollback and fail knobs ---------------------------------------------------------------
{
  let s = fresh("locked");
  post(s, "/__mock/scenario", { ota: "rollback" });
  eq(upload(s).status, 200, "rollback: the image is accepted");
  s = run(s, REBOOT_MS);
  eq([version(s), system(s).partition], ["0.1.0", "ota_0"], "but the device comes back on the OLD version and slot");
  eq(upload(s).status, 200, "the old image is confirmed already: no probation after a rollback");

  const f = fresh("locked");
  post(f, "/__mock/scenario", { ota: "fail" });
  const r = upload(f);
  eq([r.status, errorOf(r), r.restart], [422, OTA.verifyFailed, undefined], "fail: 422 verification failed, no reboot");
  eq(version(f), "0.1.0", "and the running version is unchanged");
  eq(post(fresh(), "/__mock/scenario", { ota: "policy" }).status, 400, "the old 'policy' knob is gone");
}

report("mock/ota");
