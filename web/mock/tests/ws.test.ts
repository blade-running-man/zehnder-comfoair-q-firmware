// web/mock/tests/ws.test.ts
// The push frames are exactly what src/api/ws.ts accepts, and the RFC 6455 framing is right.
//
//   node mock/tests/ws.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { ENTITIES } from "../../src/api/entities.ts";
import { KEEPALIVE_MS, deltaFrame, diffValues, nextFrame, pingFrame, stateFrame } from "../ws.ts";
import { acceptKey, closeFrame, encodeText, isClose } from "../wsframe.ts";
import { stateValues } from "../values.ts";
import { tick } from "../evolve.ts";
import { fresh, post } from "./req.ts";

// isInboundFrame() from src/api/ws.ts, restated because that module does not export it: a
// state/delta frame needs an object `values`; a ping needs nothing else.
function accepted(text: string): boolean {
  const v = JSON.parse(text) as { type?: unknown; values?: unknown };
  if (v.type === "ping") return true;
  if (v.type !== "state" && v.type !== "delta") return false;
  return typeof v.values === "object" && v.values !== null;
}

const s = fresh();
const full = JSON.parse(stateFrame(stateValues(s)));
eq(Object.keys(full), ["type", "values"], "a state frame is {type, values}");
eq(Object.keys(full.values), ENTITIES.map((e) => e.key), "and carries every registry key, like GET /api/state");
ok(accepted(stateFrame(stateValues(s))), "the client accepts the state frame");
ok(accepted(deltaFrame({ a: 1 })), "the client accepts a delta frame");
eq(pingFrame(), '{"type":"ping"}', "ping is byte for byte the firmware's");

// Deltas carry only what changed -- compared by value, so the fan object does not churn.
const a = stateValues(s);
const b = stateValues(s);
eq(diffValues(a, b), {}, "two projections of one state differ in nothing (the fan object included)");
eq(nextFrame(a, b, 1000), null, "nothing changed and not quiet long enough: no frame at all");
eq(nextFrame(a, b, KEEPALIVE_MS), pingFrame(), "quiet for the keep-alive interval: a ping");
post(s, "/api/entities/temperature_profile", { value: "Cool" });
const c = stateValues(s);
const d = JSON.parse(nextFrame(b, c, 0) ?? "{}");
eq([d.type, d.values.temperature_profile, d.values.profile_target_temp], ["delta", "Cool", 20], "a write shows up in the next delta");
ok(!("supply_air_temp" in d.values), "and a delta does not repeat what did not change");
let later = s;
for (let i = 0; i < 10; i++) later = tick(later, 1000);
ok(Object.keys(diffValues(c, stateValues(later))).length > 0, "the evolving unit produces deltas within seconds");

// RFC 6455.
eq(acceptKey("dGhlIHNhbXBsZSBub25jZQ=="), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", "the accept key matches the RFC's example");
const small = encodeText("hi");
eq([small[0], small[1], small.subarray(2).toString()], [0x81, 2, "hi"], "FIN+text, unmasked, 7-bit length");
const mid = encodeText("x".repeat(300));
eq([mid[1], mid.readUInt16BE(2), mid.length], [126, 300, 304], "16-bit extended length");
const big = encodeText("x".repeat(70000));
eq([big[1], Number(big.readBigUInt64BE(2))], [127, 70000], "64-bit extended length");
eq([...closeFrame()], [0x88, 0x00], "close frame");
eq([isClose(Buffer.from([0x88, 0x80, 1, 2, 3, 4])), isClose(Buffer.from([0x81, 0x80]))], [true, false], "a masked client close is recognised; text is not");

report("mock/ws");
