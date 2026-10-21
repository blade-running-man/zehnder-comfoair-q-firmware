// The RMI probe: a read-only walk of the unit's RMI tree that the firmware runs on request and
// keeps in RAM until the next run or reboot (docs/protocol/probe.md). These types are a
// transcription of the report schema documented there under "The report"; the firmware renders
// it, this file only names its shape. A key the firmware adds later is simply not shown.
//
// Same terms as every other call in client.ts: no header, cookie or route curl cannot use.

import { ApiError, requestVoid, send } from "./client.ts";

/** The unit answered an RMI error code instead of data: 12 no such unit, 13 no such subunit,
 *  14 no such property, 32 not readable, 15 no range. `error_name` is the firmware's spelling. */
export interface ProbeError {
  error: number;
  error_name: string;
  hex: string;
}

/** Nothing came back, even after the one retry. */
export interface ProbeTimeout {
  timeout: true;
}

/** The firmware withheld the value on purpose (a stored secret on the unit). */
export interface ProbeRedacted {
  redacted: true;
}

/** A "version"-format value carries BOTH: `number` is the raw u32, `text` the "R1.4.0" spelling. */
export interface ProbeData {
  hex: string;
  len: number;
  number: number | null;
  text: string | null;
  label: string | null;
}

export type ProbeAnswer = ProbeData | ProbeError | ProbeTimeout | ProbeRedacted;

export interface ProbeRangeData {
  hex: string;
  len: number;
  min: number | null;
  max: number | null;
}

/** Redacted wherever the value is: the firmware withholds a secret's range and step too. */
export type ProbeRange = ProbeRangeData | ProbeError | ProbeTimeout | ProbeRedacted;

export interface ProbeProperty {
  node: number;
  unit: number;
  subunit: number;
  property: number;
  /** null when the firmware's table does not know this property. */
  name: string | null;
  access: "ro" | "rw" | null;
  format: string | null;
  unit_label: string | null;
  value: ProbeAnswer;
  /** null: not asked (the value did not answer with data). */
  range: ProbeRange | null;
  /** null: not asked (no range came back). */
  step: ProbeAnswer | null;
}

export interface ProbeUnit {
  node: number;
  unit: number;
  name: string | null;
  status: "present" | "absent" | "timeout";
  subunits: number;
  absent_properties: number;
}

export type ProbeActiveErrors =
  | { hex: string; ids: number[] }
  | ProbeError
  | ProbeTimeout;

/** One PDO number heard on the bus since boot. `first_ms`/`last_ms` are on `uptime_ms`'s clock. */
export interface ProbePdoRow {
  pdo: number;
  /** The PDO is in the firmware's entity table. */
  known: boolean;
  count: number;
  first_ms: number;
  last_ms: number;
  /** Every frame length seen, ascending. */
  dlc: number[];
  /** Every sender node id seen, ascending. */
  nodes: number[];
  /** The last payload, lowercase hex; "" for a zero-length frame. */
  last: string;
}

/**
 * The PDO census: every PDO the device has heard on the bus since boot, whether or not a walk
 * ever ran. `uptime_ms` is a u32 and wraps after 49.7 days. `overflow` is how many distinct
 * PDOs were heard but did not fit in the device's table (`distinct` minus the rows).
 */
export interface ProbePdos {
  uptime_ms: number;
  frames: number;
  distinct: number;
  overflow: number;
  rows: ProbePdoRow[];
}

export type ProbeState = "idle" | "running" | "done" | "failed";

export interface ProbeReport {
  state: ProbeState;
  failure: null | "no_answer";
  node: number;
  another_counter: number;
  elapsed_ms: number;
  progress: {
    requests: number;
    timeouts: number;
    node: number;
    unit: number;
    subunit: number;
    property: number;
  };
  nodes_seen: number[];
  units: ProbeUnit[];
  properties: ProbeProperty[];
  /** null: not read (yet). */
  active_errors: ProbeActiveErrors | null;
  /** Absent on firmware that keeps no census. */
  pdos?: ProbePdos;
  /** The report ran out of room on the device; what is here is true but not complete. */
  truncated: boolean;
}

export interface ProbeStart {
  node?: number;
  another_counter?: 0 | 1;
}

/**
 * Starts a walk. 202 = accepted; the caller then polls getProbe().
 *
 * Fails as ApiError: 409 a walk is already running (poll it instead), 503 no bus, 401 a UI
 * password is set and the browser's login was dismissed. The body is ALWAYS a JSON object, `{}`
 * at the least: the device refuses a POST with no body at all (413, read_body()).
 */
export function startProbe(opts: ProbeStart = {}): Promise<void> {
  return requestVoid("/api/probe", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(opts),
  });
}

/**
 * The report, parsed, and the body exactly as the device sent it.
 *
 * The raw text is kept for "Download JSON": the saved file is what the device said, byte for
 * byte, not this page's re-serialisation of it -- a field these types do not name survives.
 */
export async function getProbe(): Promise<{ report: ProbeReport; raw: string }> {
  const response = await send("/api/probe");
  const raw = await response.text();
  try {
    return { report: JSON.parse(raw) as ProbeReport, raw };
  } catch {
    const what = raw === "" ? "an empty body" : "a body this page could not read";
    throw new ApiError(response.status, `The device answered ${response.status} with ${what}.`);
  }
}
