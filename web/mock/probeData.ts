// web/mock/probeData.ts
// What the simulated ComfoAir Q answers to the RMI probe: the walk as an ordered list of steps,
// each adding one row to the report. probe.ts reveals a prefix of it as simulated time passes.
//
// The content is plausible, not captured: a Q450 with a ComfoConnect LAN C beside it. It is
// shaped to exercise every branch of the page -- identity strings, presets with ranges and
// steps, a x10 temperature, a label, an unknown property, an error 32, a missing range (15),
// timeouts, a redacted value, absent units, and other nodes on the bus. The shapes are the
// firmware's report schema (docs/protocol/probe.md, "The report").

export type Answer =
  | { hex: string; len: number; number: number | null; text: string | null; label: string | null }
  | { error: number; error_name: string; hex: string }
  | { timeout: true }
  | { redacted: true };
export type Range =
  | { hex: string; len: number; min: number | null; max: number | null }
  | { error: number; error_name: string; hex: string }
  | { timeout: true }
  | { redacted: true };

export interface PropRow {
  node: number; unit: number; subunit: number; property: number;
  name: string | null; access: "ro" | "rw" | null; format: string | null; unit_label: string | null;
  value: Answer; range: Range | null; step: Answer | null;
}
export interface UnitRow {
  node: number; unit: number; name: string | null;
  status: "present" | "absent" | "timeout"; subunits: number; absent_properties: number;
}

export type Step =
  | { kind: "unit"; row: UnitRow }
  | { kind: "property"; row: PropRow }
  | { kind: "errors" };

export const OTHER_NODES = [42, 48];
export const ACTIVE_ERRORS = { hex: "00".repeat(32), ids: [] as number[] };

const ERROR_NAMES: Record<number, string> = {
  12: "unknown_unit", 13: "unknown_subunit", 14: "unknown_property", 15: "no_range", 32: "not_gettable",
};

function hexOf(bytes: number[]): string {
  return bytes.map((b) => (b & 0xff).toString(16).padStart(2, "0")).join("");
}
function le16(n: number): number[] {
  return [n & 0xff, (n >> 8) & 0xff];
}
const err = (code: number): Answer & Range => ({ error: code, error_name: ERROR_NAMES[code], hex: hexOf([code]) });
const TIMEOUT = { timeout: true } as const;

function num(n: number, raw: number[] = le16(n), label: string | null = null): Answer {
  return { hex: hexOf(raw), len: raw.length, number: n, text: null, label };
}
/** A x10 fixed-point value: 21.5 travels as 215. */
function x10(n: number): Answer {
  return num(n, le16(Math.round(n * 10)));
}
function str(text: string): Answer {
  const raw = [...new TextEncoder().encode(text), 0];
  return { hex: hexOf(raw), len: raw.length, number: null, text, label: null };
}
/** A version carries the raw u32 as `number` AND its "R1.4.0" spelling as `text`. */
function version(raw: number, text: string): Answer {
  const bytes = [raw & 0xff, (raw >>> 8) & 0xff, (raw >>> 16) & 0xff, (raw >>> 24) & 0xff];
  return { hex: hexOf(bytes), len: 4, number: raw, text, label: null };
}
function range(min: number, max: number, scale = 1): Range {
  const raw = [...le16(Math.round(min * scale)), ...le16(Math.round(max * scale))];
  return { hex: hexOf(raw), len: raw.length, min, max };
}

interface Def {
  sub?: number; id: number; name: string | null; access?: "ro" | "rw"; format?: string;
  unit_label?: string; value: Answer; range?: Range | null; step?: Answer | null;
}

// Only the answers that are NOT 14 are listed; every other id up to 0x3F was "no such property".
const NODE_PROPS: Def[] = [
  { id: 0x04, name: "Serial number", format: "string", value: str("DEM0116302125"), range: err(15) },
  { id: 0x06, name: "Firmware version", format: "version", value: version(0xc0101000, "R1.4.0"), range: err(15) },
  { id: 0x08, name: "Model", format: "string", value: str("ComfoAir Q450 B R RF ST Quality NL"), range: err(15) },
  { id: 0x0b, name: "Article number", format: "string", value: str("471502013"), range: err(15) },
  { id: 0x0d, name: "Country", format: "string", value: str("NL"), range: err(15) },
  { id: 0x0e, name: null, value: { hex: "0102", len: 2, number: null, text: null, label: null }, range: err(15) },
  { id: 0x14, name: "Name", access: "rw", format: "string", value: str("ComfoAirQ"), range: err(15) },
];

const preset = (id: number, name: string, v: number, min: number): Def => ({
  id, name, access: "rw", format: "i16", unit_label: "m³/h", value: num(v), range: range(min, 450), step: num(1),
});

const UNITS: Record<number, { name: string; subunits: number; props: Def[] }> = {
  0x01: { name: "NODE", subunits: 1, props: NODE_PROPS },
  0x02: { name: "COMFOBUS", subunits: 1, props: [{ id: 0x01, name: null, value: num(42, [42]), range: err(15) }] },
  0x03: { name: "ERROR", subunits: 1, props: [] },
  0x15: { name: "SCHEDULE", subunits: 1, props: [{ id: 0x01, name: null, value: num(0, [0]), range: err(15) }] },
  0x16: { name: "VALVE", subunits: 2, props: [
    { sub: 1, id: 0x01, name: "Bypass position", format: "u8", unit_label: "%", value: num(0, [0]), range: range(0, 100), step: num(1, [1]) },
    { sub: 2, id: 0x01, name: "Bypass position", format: "u8", unit_label: "%", value: num(0, [0]), range: range(0, 100), step: num(1, [1]) },
  ] },
  0x17: { name: "FAN", subunits: 2, props: [
    { sub: 1, id: 0x01, name: "Fan duty", format: "u8", unit_label: "%", value: num(38, [38]), range: range(0, 100), step: num(1, [1]) },
    { sub: 2, id: 0x01, name: "Fan duty", format: "u8", unit_label: "%", value: num(37, [37]), range: range(0, 100), step: num(1, [1]) },
    { sub: 2, id: 0x02, name: "Fan speed", format: "u16", unit_label: "rpm", value: TIMEOUT },
  ] },
  0x1c: { name: "FILTER", subunits: 1, props: [
    { id: 0x01, name: "Filter days left", format: "u16", unit_label: "days", value: num(112), range: err(15) },
  ] },
  0x1d: { name: "TEMPHUMCONTROL", subunits: 1, props: [
    { id: 0x02, name: "Comfort temperature", access: "rw", format: "i16", unit_label: "°C", value: x10(21.5), range: range(15, 28, 10), step: x10(0.5) },
    { id: 0x03, name: "Heating season limit", access: "rw", format: "i16", unit_label: "°C", value: x10(12), range: range(5, 20, 10), step: x10(0.5) },
    { id: 0x04, name: "Cooling season limit", access: "rw", format: "i16", unit_label: "°C", value: x10(16), range: range(10, 25, 10), step: x10(0.5) },
    { id: 0x05, name: "Temperature profile", access: "rw", format: "enum", value: num(0, [0], "Normal"), range: err(15) },
    { id: 0x09, name: "Passive temperature", access: "rw", format: "enum", value: TIMEOUT },
  ] },
  0x1e: { name: "VENTILATIONCONFIG", subunits: 1, props: [
    preset(0x03, "Airflow preset away", 50, 0),
    preset(0x04, "Airflow preset low", 210, 75),
    preset(0x05, "Airflow preset medium", 290, 75),
    preset(0x06, "Airflow preset high", 360, 75),
    { id: 0x0a, name: "Airflow reset", access: "rw", format: "u8", value: err(32) },
    { id: 0x0b, name: null, value: num(1, [1]), range: err(15) },
  ] },
  0x20: { name: "NODECONFIGURATION", subunits: 1, props: [
    { id: 0x01, name: "Unit size", format: "enum", value: num(2, [2], "Q450"), range: err(15) },
    { id: 0x03, name: "Installer code", access: "rw", format: "u16", value: { redacted: true }, range: { redacted: true }, step: { redacted: true } },
  ] },
};

// Names the firmware's table knows for units this Q450 does not have.
const ABSENT_NAMES: Record<number, string> = {
  0x18: "POWERSENSOR", 0x19: "PREHEATER", 0x1a: "HMI", 0x1b: "RFCOMMUNICATION", 0x21: "TEMPERATURESENSOR",
  0x22: "HUMIDITYSENSOR", 0x23: "PRESSURESENSOR", 0x25: "ANALOGINPUT", 0x26: "COOKERHOOD",
  0x27: "POSTHEATER", 0x28: "COMFOFOND",
};
const TIMEOUT_UNIT = { unit: 0x24, name: "PERIPHERALS" };

function row(node: number, unit: number, d: Def): PropRow {
  return {
    node, unit, subunit: d.sub ?? 1, property: d.id, name: d.name, access: d.access ?? (d.name ? "ro" : null),
    format: d.format ?? null, unit_label: d.unit_label ?? null, value: d.value,
    range: d.range === undefined ? null : d.range, step: d.step === undefined ? null : d.step,
  };
}

/** The whole walk of `node`, in the firmware's order: units 0x01..0x2F (each followed by its
 *  properties), the active-error bitfield, then the identity of every other node seen. */
export function walk(node: number): Step[] {
  const steps: Step[] = [];
  for (let unit = 0x01; unit <= 0x2f; unit++) {
    const present = UNITS[unit];
    if (present) {
      steps.push({ kind: "unit", row: {
        node, unit, name: present.name, status: "present", subunits: present.subunits,
        absent_properties: present.subunits * 0x3f - present.props.length,
      } });
      for (const d of present.props) steps.push({ kind: "property", row: row(node, unit, d) });
    } else {
      const timeout = unit === TIMEOUT_UNIT.unit;
      steps.push({ kind: "unit", row: {
        node, unit, name: timeout ? TIMEOUT_UNIT.name : ABSENT_NAMES[unit] ?? null,
        status: timeout ? "timeout" : "absent", subunits: 0, absent_properties: 0,
      } });
    }
  }
  steps.push({ kind: "errors" });
  const lan: Def[] = [
    { id: 0x04, name: "Serial number", format: "string", value: str("LU0217034612") },
    { id: 0x06, name: "Firmware version", format: "version", value: version(0xc0101402, "R1.5.2") },
    { id: 0x08, name: "Model", format: "string", value: str("ComfoConnect LAN C") },
    { id: 0x14, name: "Name", access: "rw", format: "string", value: str("ComfoConnect LAN C") },
  ];
  for (const d of lan) steps.push({ kind: "property", row: row(48, 0x01, d) });
  return steps;
}

/** Requests that went unanswered for a step: a silent request is sent twice (one retry). */
export function timeoutsOf(step: Step): number {
  if (step.kind === "unit") return step.row.status === "timeout" ? 2 : 0;
  if (step.kind === "property") return "timeout" in step.row.value ? 2 : 0;
  return 0;
}
