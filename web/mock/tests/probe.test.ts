// web/mock/tests/probe.test.ts
// The simulated RMI probe: comfoair_http_probe.c's statuses, and a report that grows to "done".
//
//   node mock/tests/probe.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { tick } from "../evolve.ts";
import type { MockState } from "../model.ts";
import { SILENT_MS, WALK_MS } from "../probe.ts";
import { LOCKED_PASSWORD } from "../scenarios.ts";
import { basic, errorOf, fresh, get, post } from "./req.ts";

type Report = {
  state: string; failure: string | null; units: Array<{ status: string }>;
  properties: Array<{ node: number; unit: number; property: number; name: string | null; value: Record<string, unknown> }>;
  nodes_seen: number[]; active_errors: unknown; progress: { requests: number; timeouts: number };
};
const probe = (s: MockState): Report => get(s, "/api/probe").body as Report;
function advance(s: MockState, ms: number): MockState {
  for (let t = 0; t < ms; t += 1000) s = tick(s, 1000);
  return s;
}

// --- idle, start, conflict, growth, done -------------------------------------------------
{
  let s = fresh();
  eq(probe(s).state, "idle", "no walk yet is idle");
  eq(post(s, "/api/probe").status, 413, "an empty body is refused");
  eq(post(s, "/api/probe", {}).status, 202, "{} starts a walk with the defaults");
  const busy = post(s, "/api/probe", {});
  eq([busy.status, errorOf(busy)], [409, "a probe is already running"], "a second start is 409");
  s = advance(s, 4000);
  const mid = probe(s);
  eq(mid.state, "running", "four seconds in, still running");
  ok(mid.units.length > 0 && mid.progress.requests > 0, "the report grows while running");
  s = advance(s, WALK_MS);
  const done = probe(s);
  eq(done.state, "done", "the walk finishes");
  eq(done.nodes_seen, [1, 42, 48], "nodes_seen is the bus's");
  ok(done.units.some((u) => u.status === "absent") && done.units.some((u) => u.status === "timeout"), "absent and silent units");
  ok(done.properties.some((p) => p.name === null), "an unknown property");
  ok(done.properties.some((p) => p.value.error === 32), "an error 32 answer");
  ok(done.properties.some((p) => p.unit === 0x20 && p.property === 3 && p.value.redacted === true), "a redacted NODECONFIGURATION 0x03");
  ok(done.properties.some((p) => p.node === 48), "another node's identity");
  ok(done.active_errors !== null, "the active errors were read");
  ok(done.progress.timeouts > 0, "timeouts are counted");
  eq(post(s, "/api/probe", { another_counter: 1 }).status, 202, "a finished walk can be run again");
}

// --- refusals ------------------------------------------------------------------------------
{
  const s = fresh();
  eq(errorOf(post(s, "/api/probe", { node: "x" })), "node must be a whole number", "a node that is not a number is 422");
  eq(post(s, "/api/probe", { node: 42 }).status, 422, "probing this device is refused");
  eq(post(s, "/api/probe", { another_counter: 4 }).status, 422, "another_counter past 3 is refused");
  const down = fresh("bus_down");
  eq(post(down, "/api/probe", {}).status, 503, "no bus is 503");
  const locked = fresh("locked");
  eq(post(locked, "/api/probe", {}).status, 401, "a locked device wants a login");
  eq(post(locked, "/api/probe", {}, basic("x", LOCKED_PASSWORD)).status, 202, "and takes the walk after one");
}

// --- the silent unit ---------------------------------------------------------------------
{
  let s = fresh("probe_silent");
  eq(post(s, "/api/probe", {}).status, 202, "the silent unit still accepts the start");
  s = advance(s, SILENT_MS + 1000);
  const r = probe(s);
  eq([r.state, r.failure], ["failed", "no_answer"], "a silent unit fails with no_answer");
  eq(r.units.map((u) => u.status), ["timeout", "timeout", "timeout", "timeout", "timeout"],
     "and, like the device, a timeout row per silent discovery read");
  eq(r.properties.length, 0, "and no properties");
  post(s, "/api/probe", { another_counter: 1 });
  s = advance(s, WALK_MS + 1000);
  eq(probe(s).state, "done", "another_counter = 1 gets an answer");
}

report("mock probe");
