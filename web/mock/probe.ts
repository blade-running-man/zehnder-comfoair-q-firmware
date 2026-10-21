// web/mock/probe.ts
// POST/GET /api/probe: the RMI probe, simulated. A POST records when the walk started; GET
// renders the report from how much simulated time has passed since, revealing a prefix of the
// walk in probeData.ts, so the report grows on every tick and reaches "done" after WALK_MS. Pure:
// the clock is MockState.uptimeMs, which evolve.ts's tick() advances.
//
// The status codes and sentences are comfoair_http_probe.c's. The real walk takes minutes; the
// mock's is ten seconds, so the page's progress and polling can be watched without waiting.
import { PROBE, bodyGate, error, isObject } from "./wire.ts";
import { UNPARSABLE, logLine, type MockResponse, type MockState } from "./model.ts";
import { ACTIVE_ERRORS, OTHER_NODES, timeoutsOf, walk, type PropRow, type UnitRow } from "./probeData.ts";

export const WALK_MS = 10_000;
/** The silent scenario gives up after 5 requests x (300 ms + one retry), the firmware's abort
 *  rule for a unit that does not answer RMI (docs/protocol/probe.md). */
export const SILENT_MS = 3_000;
const WALK_REQUESTS = 3160;   // ~50 subunits x 63 properties: a rough size for a full walk
const OUR_NODE = 0x2a;

export interface ProbeRun { startedMs: number; node: number; anotherCounter: number }

function idle(): Record<string, unknown> {
  return {
    state: "idle", failure: null, node: 1, another_counter: 0, elapsed_ms: 0,
    progress: { requests: 0, timeouts: 0, node: 0, unit: 0, subunit: 0, property: 0 },
    nodes_seen: [], units: [], properties: [], active_errors: null, truncated: false,
  };
}

// Silence answers only the default counter: "another_counter = 1" is what the page tells the
// owner to try, so the scenario lets that advice work.
function silent(s: MockState, run: ProbeRun): boolean {
  return s.scenario.probe === "silent" && run.anotherCounter === 0;
}

function u8(body: Record<string, unknown>, key: string): number | undefined | false {
  if (!(key in body)) return undefined;
  const v = body[key];
  return typeof v === "number" && Number.isInteger(v) && v >= 0 && v <= 255 ? v : false;
}

export function postProbe(s: MockState, rawLength: number, body: unknown): MockResponse {
  const gate = bodyGate(rawLength);
  if (gate) return gate;
  if (body === UNPARSABLE || !isObject(body)) return error(400, PROBE.notObject);
  const node = u8(body, "node");
  if (node === false) return error(422, PROBE.badNode);
  const counter = u8(body, "another_counter");
  if (counter === false) return error(422, PROBE.badCounter);
  const n = node ?? 1, c = counter ?? 0;
  if (n < 1 || n > 63 || n === OUR_NODE || c > 3) return error(422, PROBE.outOfRange);
  if (s.scenario.commands === "down") return error(503, PROBE.noBus);
  if (s.probe && renderProbe(s).state === "running") return error(409, PROBE.running);
  s.probe = { startedMs: s.uptimeMs, node: n, anotherCounter: c };
  logLine(s, `probe: walking node ${n} (another_counter ${c})`);
  return { status: 202, body: { accepted: true } };
}

/** The report at this moment, as the device would render it. */
export function renderProbe(s: MockState): Record<string, unknown> {
  const run = s.probe;
  if (!run) return idle();
  const elapsed = Math.max(0, s.uptimeMs - run.startedMs);
  const base = { node: run.node, another_counter: run.anotherCounter, nodes_seen: [1, ...OTHER_NODES], truncated: false };

  if (silent(s, run)) {
    const done = elapsed >= SILENT_MS;
    const requests = Math.min(10, Math.floor(elapsed / 300));
    return {
      ...base, state: done ? "failed" : "running", failure: done ? "no_answer" : null,
      elapsed_ms: Math.min(elapsed, SILENT_MS),
      progress: { requests, timeouts: requests, node: run.node, unit: 1, subunit: 1, property: 1 },
      // The device's walk records a TIMEOUT unit row for each silent discovery read before it gives
      // up (abort_after_silent = 5), so a failed walk is not an empty table.
      units: done ? [1, 2, 3, 4, 5].map((unit) => ({
        node: run.node, unit, name: unit === 1 ? "NODE" : unit === 2 ? "COMFOBUS" : unit === 3 ? "ERROR" : null,
        status: "timeout" as const, subunits: 0, absent_properties: 0,
      })) : [],
      properties: [], active_errors: null,
    };
  }

  const steps = walk(run.node);
  const done = elapsed >= WALK_MS;
  const shown = done ? steps.length : Math.floor((elapsed / WALK_MS) * steps.length);
  const units: UnitRow[] = [], properties: PropRow[] = [];
  let timeouts = 0, errorsRead = false;
  for (const step of steps.slice(0, shown)) {
    timeouts += timeoutsOf(step);
    if (step.kind === "unit") units.push(step.row);
    else if (step.kind === "property") properties.push(step.row);
    else errorsRead = true;
  }
  // Where the walk is now: the next step's position, or the last one once it has finished.
  const at = steps[Math.min(shown, steps.length - 1)];
  const pos = at.kind === "unit" ? { node: at.row.node, unit: at.row.unit, subunit: 1, property: 1 }
    : at.kind === "property" ? { node: at.row.node, unit: at.row.unit, subunit: at.row.subunit, property: at.row.property }
    : { node: run.node, unit: 0x03, subunit: 1, property: 0x03 };
  return {
    ...base, state: done ? "done" : "running", failure: null, elapsed_ms: Math.min(elapsed, WALK_MS),
    progress: { requests: Math.round((shown / steps.length) * WALK_REQUESTS), timeouts, ...pos },
    units, properties, active_errors: errorsRead ? ACTIVE_ERRORS : null,
  };
}

export function getProbe(s: MockState): MockResponse {
  if (s.scenario.commands === "down") return error(503, PROBE.noBus);
  return { status: 200, body: renderProbe(s) };
}
