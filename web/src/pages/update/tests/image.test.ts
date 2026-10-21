// The app descriptor read from a picked file, and the pre-flight's "a different device" refusal.
//
// Run: node src/pages/update/tests/image.test.ts
import { eq, ok, report } from "../../settings/tests/harness.ts";
import { PREFLIGHT_BYTES, PROJECT_NAME, isThisFirmware, parseAppDesc } from "../image.ts";
import { checkImage } from "../model.ts";
import { REAL_PREFIX, REAL_VERSION, prefix } from "./fixture.ts";

const IMAGE = 1_300_000;

eq(REAL_PREFIX.length, PREFLIGHT_BYTES, "the fixture is the whole 0x70-byte pre-flight");
eq(parseAppDesc(REAL_PREFIX), { version: REAL_VERSION, project: PROJECT_NAME },
  "the real firmware.bin: magic at 0x20, version at 0x30, project_name at 0x50");
ok(isThisFirmware(parseAppDesc(REAL_PREFIX)), "and it is this firmware");
eq(parseAppDesc(prefix({ version: "0.1.1-dev.3+gabc1234.dirty" }))?.version, "0.1.1-dev.3+gabc1234.dirty",
  "a SemVer version string is read up to its NUL");
eq(parseAppDesc(prefix({ version: "x".repeat(32) }))?.version, "x".repeat(32),
  "a field with no NUL is its full 32 bytes, not a read past it");
const larger = new Uint8Array(PREFLIGHT_BYTES + 16);
larger.set(REAL_PREFIX, 8);
eq(parseAppDesc(larger.subarray(8))?.version, REAL_VERSION,
  "a view into a larger buffer is read from its own start (the mock's subarray of the body)");

// --- refusals -------------------------------------------------------------------------------
eq(parseAppDesc(REAL_PREFIX.subarray(0, PREFLIGHT_BYTES - 1)), null, "one byte short of 0x70: no descriptor");
const badMagic = REAL_PREFIX.slice();
badMagic[0x23] = 0xac;
eq(parseAppDesc(badMagic), null, "a wrong magic: no descriptor");
eq(parseAppDesc(prefix({ project: "opentherm-thermostat" }))?.project, "opentherm-thermostat",
  "a foreign image's name is read");
ok(!isThisFirmware(parseAppDesc(prefix({ project: "opentherm-thermostat" }))), "but it is not this firmware");
ok(!isThisFirmware(parseAppDesc(prefix({ project: "comfoair-q2" }))), "a name that only starts the same is not either");
ok(!isThisFirmware(null), "no descriptor is not this firmware");

eq(checkImage(IMAGE, REAL_PREFIX), null, "the real image passes the whole pre-flight");
eq(checkImage(IMAGE, prefix({ project: "opentherm-thermostat" })), "wrongProject", "a foreign project is refused");
eq(checkImage(IMAGE, badMagic), "wrongProject", "a missing descriptor is refused the same way, as the device does");
eq(checkImage(IMAGE, REAL_PREFIX.subarray(0, 24)), "wrongProject", "only the image header read: no descriptor");
eq(checkImage(IMAGE, prefix({ first: 0x1f, project: "x" })), "notImage", "the image magic is judged before the descriptor");
eq(checkImage(10, prefix({ project: "x" })), "tooSmall", "and the length before both");

report("pages/update/image");
