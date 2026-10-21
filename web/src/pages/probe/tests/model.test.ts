// The probe page's pure helpers: grouping, value spelling, the unknown filter, identity rows.
//
// Run: node src/pages/probe/tests/model.test.ts
import { eq, report } from "../../settings/tests/harness.ts";
import {
  activeErrorsCell, answerCell, downloadName, formatElapsed, groupProperties, hasContent,
  hex2, identityRows, otherNodes, rangeCell, rawHex, targetUnits,
} from "../model.ts";
import type { ProbeAnswer, ProbeProperty, ProbeReport } from "../../../api/probe.ts";

const data = (hex: string, over: Partial<{ number: number; text: string; label: string }> = {}): ProbeAnswer => ({
  hex, len: hex.length / 2, number: over.number ?? null, text: over.text ?? null, label: over.label ?? null,
});

function prop(node: number, unit: number, subunit: number, property: number, name: string | null,
  value: ProbeAnswer = data("00")): ProbeProperty {
  return { node, unit, subunit, property, name, access: "ro", format: null, unit_label: null, value, range: null, step: null };
}

function base(over: Partial<ProbeReport> = {}): ProbeReport {
  return {
    state: "done", failure: null, node: 1, another_counter: 0, elapsed_ms: 0,
    progress: { requests: 0, timeouts: 0, node: 1, unit: 0, subunit: 0, property: 0 },
    nodes_seen: [], units: [], properties: [], active_errors: null, truncated: false, ...over,
  };
}

// --- answerCell: label ?? number+unit ?? text ?? hex ----------------------------------------
eq(answerCell(data("0100", { number: 1, label: "Normal" })), { kind: "text", text: "Normal" }, "a label wins over the number");
eq(answerCell(data("d700", { number: 21.5 }), "°C"), { kind: "text", text: "21.5 °C" }, "a number carries its unit label");
eq(answerCell(data("d200", { number: 210 }), "m³/h"), { kind: "text", text: "210 m³/h" }, "an integer has no decimals");
eq(answerCell(data("01", { number: 0.30000000000000004 })), { kind: "text", text: "0.3" }, "float noise is rounded off");
eq(answerCell(data("4142", { text: "AB" })), { kind: "text", text: "AB" }, "text when there is no number");
eq(answerCell(data("0102")), { kind: "text", text: "0102" }, "undecoded data shows its hex");
eq(answerCell({ error: 32, error_name: "not_gettable", hex: "20" }), { kind: "error", name: "not_gettable" }, "an error answer shows its name");
eq(answerCell({ timeout: true }), { kind: "timeout" }, "silence is a timeout");
eq(answerCell({ redacted: true }), { kind: "redacted" }, "a redacted value is hidden");
eq(answerCell(null), { kind: "none" }, "not asked is none");
{
  const version: ProbeAnswer = { hex: "00104010", len: 4, number: 0x10401000, text: "R1.4.0", label: null };
  eq(answerCell(version, null, "version"), { kind: "text", text: "R1.4.0" }, "a version shows its text, not the raw u32");
  eq(answerCell(data("01", { number: 1, text: "x" })), { kind: "text", text: "1" }, "outside versions the number still wins over text");
}

// --- rangeCell -----------------------------------------------------------------------------
eq(rangeCell({ hex: "4b00c201", len: 4, min: 75, max: 450 }, "m³/h"), { kind: "text", text: "75 – 450 m³/h" }, "a full range is min – max unit");
eq(rangeCell({ hex: "4b00", len: 2, min: 75, max: null }), { kind: "text", text: "4b00" }, "half a range shows its hex, not a guess");
eq(rangeCell({ error: 15, error_name: "no_range", hex: "0f" }), { kind: "error", name: "no_range" }, "no range is the unit's error 15");
eq(rangeCell(null), { kind: "none" }, "a range not asked is none");
eq(rangeCell({ redacted: true }), { kind: "redacted" }, "a redacted range is hidden");
eq(answerCell({ redacted: true }), { kind: "redacted" }, "a redacted step is hidden");

// --- rawHex ------------------------------------------------------------------------------
eq(rawHex(data("d200")), "d200", "data carries its hex");
eq(rawHex({ error: 32, error_name: "not_gettable", hex: "20" }), "20", "an error answer carries its hex too");
eq(rawHex({ timeout: true }), "", "silence has no hex");

// --- groupProperties ---------------------------------------------------------------------
{
  const r = base({
    units: [
      { node: 1, unit: 0x1e, name: "VENTILATIONCONFIG", status: "present", subunits: 1, absent_properties: 50 },
      { node: 1, unit: 0x01, name: "NODE", status: "present", subunits: 1, absent_properties: 43 },
    ],
    properties: [
      prop(1, 0x1e, 1, 5, "Airflow preset medium"),
      prop(1, 0x1e, 1, 4, "Airflow preset low"),
      prop(1, 0x01, 1, 4, "Serial number"),
      prop(1, 0x31, 2, 1, null),
      prop(1, 0x31, 1, 9, null),
      prop(48, 0x01, 1, 4, "Serial number"),
    ],
  });
  const g = groupProperties(r);
  eq(g.map((x) => [x.unit, x.name]), [[1, "NODE"], [0x1e, "VENTILATIONCONFIG"], [0x31, null]],
    "groups in unit order; a unit missing from units[] still appears, nameless");
  eq(g[1].rows.map((p) => p.property), [4, 5], "rows sorted by property id");
  eq(g[2].rows.map((p) => [p.subunit, p.property]), [[1, 9], [2, 1]], "rows sorted by subunit first");
  eq(g.flatMap((x) => x.rows).some((p) => p.node === 48), false, "another node's answers are not the target's");
  const unknown = groupProperties(r, true);
  eq(unknown.map((x) => x.unit), [0x31], "the unknown filter keeps only units with unknown properties");
  eq(unknown[0].rows.every((p) => p.name === null), true, "the unknown filter keeps only nameless rows");
}

// --- identity, units, other nodes ---------------------------------------------------------
{
  const r = base({
    nodes_seen: [48, 1, 42],
    units: [
      { node: 1, unit: 0x02, name: "COMFOBUS", status: "present", subunits: 1, absent_properties: 60 },
      { node: 1, unit: 0x01, name: "NODE", status: "present", subunits: 1, absent_properties: 43 },
      { node: 48, unit: 0x01, name: "NODE", status: "present", subunits: 1, absent_properties: 0 },
    ],
    properties: [
      prop(1, 0x01, 1, 8, "Model"), prop(1, 0x01, 1, 4, "Serial number"), prop(1, 0x1e, 1, 4, "x"),
      prop(48, 0x01, 1, 4, "Serial number"),
    ],
  });
  eq(identityRows(r, 1).map((p) => p.property), [4, 8], "identity is the target's NODE unit, by id");
  eq(targetUnits(r).map((u) => u.unit), [1, 2], "units are the target's, in unit order");
  eq(otherNodes(r).map((n) => [n.node, n.rows.length]), [[42, 0], [48, 1]],
    "other nodes exclude the target and carry their own identity rows");
}

// --- active errors -------------------------------------------------------------------------
eq(activeErrorsCell(null), { kind: "none" }, "not read is none");
eq(activeErrorsCell({ hex: "00", ids: [] }), { kind: "ids", ids: [] }, "an empty bitfield is an empty list");
eq(activeErrorsCell({ hex: "", ids: [53, 21] }), { kind: "ids", ids: [21, 53] }, "ids are sorted");
eq(activeErrorsCell({ timeout: true }), { kind: "timeout" }, "a silent error read is a timeout");

// --- small spellings ---------------------------------------------------------------------
eq(hex2(0x1e), "0x1E", "hex ids are two upper-case digits");
eq(hex2(3), "0x03", "hex ids are zero-padded");
eq(formatElapsed(0), "0:00", "zero elapsed");
eq(formatElapsed(125_900), "2:05", "minutes and seconds, truncated");
eq(formatElapsed(3_725_000), "1:02:05", "hours past an hour");
eq(downloadName(new Date(2026, 9, 17, 14, 3, 9)), "comfoair-probe-2026-10-17T14-03-09.json", "a sortable, colon-free file name");
eq(hasContent(base({ state: "idle" })), false, "an idle empty report has nothing to show");
eq(hasContent(base({ state: "running" })), true, "a running report has content");

report("probe model");
