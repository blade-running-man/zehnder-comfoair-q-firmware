// web/mock/tests/system.test.ts
// GET /api/system: the schema's key order and null rules, the identity read's timing, and the
// unitInfo knob.
//
//   node mock/tests/system.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { tick } from "../evolve.ts";
import type { MockState } from "../model.ts";
import { IDENTITY_FIRST_MS, MOCK_ERROR_SUMMARY, MOCK_ERRORS, UNIT_IDENTITY } from "../system.ts";
import { LOCKED_PASSWORD } from "../scenarios.ts";
import { basic, fresh, get, post, state } from "./req.ts";

type Doc = Record<string, Record<string, unknown>>;
const system = (s: MockState): Doc => get(s, "/api/system").body as Doc;
function advance(s: MockState, ms: number): MockState {
  for (let t = 0; t < ms; t += 1000) s = tick(s, 1000);
  return s;
}

// --- shape, and the first ten seconds --------------------------------------------------------
{
  let s = fresh();
  eq(get(s, "/api/system").status, 200, "GET /api/system is 200");
  const boot = system(s);
  eq(Object.keys(boot), ["unit", "device", "network", "bus", "commands", "mqtt"], "top-level key order is the firmware's");
  eq(Object.keys(boot.unit), ["state", "age_ms", "model", "serial", "firmware", "firmware_raw", "article", "country", "name", "errors"],
     "unit key order");
  eq([boot.unit.state, boot.unit.age_ms, boot.unit.model, boot.unit.firmware_raw], ["unknown", null, null, null],
     "before the first identity read the unit is unknown and null");
  eq(boot.unit.errors, { state: "unknown", ids: [], items: [] }, "and its errors are not read");

  s = advance(s, IDENTITY_FIRST_MS + 5000);
  const doc = system(s);
  eq(doc.unit.state, "ok", "ten seconds in, the identity has been read");
  eq(doc.unit.age_ms, 5000, "age counts from that read");
  eq([doc.unit.model, doc.unit.firmware, doc.unit.firmware_raw, doc.unit.country],
     [UNIT_IDENTITY.model, "R1.11.0", 3222285312, "NL"], "the live Q350's identity");
  eq(doc.unit.errors, { state: "ok", ids: [], items: [] }, "no active errors");
  eq([doc.device.board, doc.device.chip, doc.device.chip_revision, doc.device.flash_bytes, doc.device.partition, doc.device.idf],
     ["m5stack-nanoc6", "esp32c6", 2, 4194304, "ota_0", "v5.5.5"], "the device block");
  ok((doc.device.uptime_ms as number) >= IDENTITY_FIRST_MS, "uptime follows the mock clock");
  eq(doc.network, { connected: true, ssid: "home-wifi", ip: "192.168.1.57", rssi: -61, channel: 6 }, "connected network");
  eq([doc.bus.started, doc.bus.state, doc.bus.nodes_seen], [true, "active", [1]], "an active bus that hears the unit");
  ok((doc.bus.rx_frames as number) > 0, "frames are counted");
  eq(Object.keys(doc.mqtt), ["configured", "connected", "published", "commands", "rejected", "reconnects"], "mqtt keys");
}

// --- knobs -----------------------------------------------------------------------------------
{
  const s = advance(fresh("unit_unread"), IDENTITY_FIRST_MS + 1000);
  const u = system(s).unit;
  eq([u.state, u.age_ms, u.serial], ["unknown", null, null], "unit_unread: never read");
}
{
  const s = advance(fresh("unit_errors"), IDENTITY_FIRST_MS + 1000);
  const e = system(s).unit.errors as { state: string; ids: number[]; items: Array<{ id: number; text: string | null }> };
  eq(e.ids, [21, 99], "unit_errors: two active errors");
  eq(e.items.map((i) => i.text === null), [false, true], "one with text, one the firmware does not know");
  eq(e.items[0].text, MOCK_ERRORS[0].text, "the known one carries its text");
}
{
  const s = fresh();
  post(s, "/__mock/scenario", { unitInfo: "no_answer" });
  const later = advance(s, IDENTITY_FIRST_MS + 3000);
  const u = system(later).unit;
  eq([u.state, u.model, (u.errors as { ids: number[] }).ids], ["no_answer", UNIT_IDENTITY.model, []],
     "no_answer keeps the older values and reports no error ids");
  eq(post(s, "/__mock/scenario", { unitInfo: "maybe" }).status, 400, "an unknown unitInfo is refused");
}
{
  const s = advance(fresh("bus_down"), IDENTITY_FIRST_MS + 1000);
  const d = system(s);
  eq([d.bus.started, d.bus.state, d.bus.nodes_seen, d.unit.state], [false, "stopped", [], "unknown"],
     "bus_down: the controller is stopped and the unit unread");
}
{
  const s = fresh("unprovisioned");
  eq(system(s).network, { connected: false, ssid: null, ip: null, rssi: null, channel: null }, "no network: nulls");
}

// --- the two error entities follow the same read -------------------------------------------
{
  // Unknown until the unit has been read -- not "no error" -- even with PDOs already flowing: the
  // firmware sets them from the RMI read 10 s after boot, so /api/state says null until then.
  let s = fresh("unit_errors");
  eq([state(s).unit_error, state(s).active_errors], [null, null],
     "before the first unit read both error entities are null");
  s = advance(s, IDENTITY_FIRST_MS + 1000);
  eq([state(s).unit_error, state(s).active_errors], [true, MOCK_ERROR_SUMMARY],
     "after it, the problem flag and the one-line summary");
  const quiet = advance(fresh(), IDENTITY_FIRST_MS + 1000);
  eq([state(quiet).unit_error, state(quiet).active_errors], [false, "No errors"], "a healthy unit says so");
}

// --- access ----------------------------------------------------------------------------------
{
  const s = fresh("locked");
  eq(get(s, "/api/system").status, 401, "a locked device wants a login to read it");
  eq(get(s, "/api/system", basic("x", LOCKED_PASSWORD)).status, 200, "and answers after one");
  const doc = JSON.stringify(get(s, "/api/system", basic("x", LOCKED_PASSWORD)).body);
  ok(!doc.includes("correct-horse") && !doc.includes("broker-secret") && !doc.includes(LOCKED_PASSWORD),
     "no secret is in the document");
}

report("mock system");
