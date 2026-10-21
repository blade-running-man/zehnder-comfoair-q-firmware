// GET /api/system as five labelled sections -- the ONE description of what each card says, which
// the page renders and copyText() flattens into the support summary. Two consumers of one list,
// so the copied text cannot drift from the screen: a row added to a card is in the copy too.
//
// Pure, like model.ts: words come in through a Tr, imports are type-only.

import type { SystemInfo } from "../../api/system.ts";
import {
  NONE, ageText, busStateText, busTone, chipRevision, errorLine, errorsView, kb, mb, mqttStateText,
  nodesText, orNone, resetReasonText, rssiText, unitHasValues, uptimeText, type BusTone, type Tr,
} from "./model.ts";

export type Tone = BusTone;

export interface Row {
  label: string;
  value: string;
  /** Absent = ordinary. "problem" and "warning" are coloured AND worded: never colour alone. */
  tone?: Tone;
  /** Identifiers (serial, MAC, ids) set in the monospace face. */
  mono?: boolean;
}

export interface Note {
  text: string;
  /** "warning" is a ruled notice; "dim" a quiet hint. */
  tone: "warning" | "dim";
}

export interface ErrorList {
  title: string;
  /** One "id — text" per active error; empty when `empty` says why. */
  lines: string[];
  empty: string | null;
  /** The list could not be read: shown dimmed, not as "no errors". */
  unread: boolean;
}

export interface Section {
  key: "unit" | "device" | "network" | "bus" | "counters";
  title: string;
  /** A short line beside the title, e.g. "read 3 min ago". */
  aside: string | null;
  notes: Note[];
  rows: Row[];
  errors?: ErrorList;
}

function unitSection(info: SystemInfo, tr: Tr): Section {
  const u = info.unit;
  const notes: Note[] = [];
  const has = unitHasValues(u);
  if (!has) notes.push({ text: tr("system.unit.notRead"), tone: "dim" });
  if (u.state === "no_answer") notes.push({ text: tr("system.unit.noAnswer"), tone: "warning" });
  const rows: Row[] = has ? [
    { label: tr("system.unit.model"), value: orNone(u.model) },
    { label: tr("system.unit.firmware"), value: orNone(u.firmware) },
    { label: tr("system.unit.serial"), value: orNone(u.serial), mono: true },
    { label: tr("system.unit.article"), value: orNone(u.article), mono: true },
    { label: tr("system.unit.country"), value: orNone(u.country) },
    { label: tr("system.unit.name"), value: orNone(u.name) },
  ] : [];
  const view = errorsView(u.errors);
  const errors: ErrorList = {
    title: tr("system.errors.heading"),
    lines: view.kind === "list" ? view.items.map((i) => errorLine(i, tr)) : [],
    empty: view.kind === "none" ? tr("system.errors.none") : view.kind === "notRead" ? tr("system.errors.notRead") : null,
    unread: view.kind === "notRead",
  };
  return { key: "unit", title: tr("system.unit.heading"), aside: ageText(u.age_ms, tr), notes, rows, errors };
}

function deviceSection(info: SystemInfo, tr: Tr): Section {
  const d = info.device;
  return {
    key: "device", title: tr("system.device.heading"), aside: null, notes: [],
    rows: [
      { label: tr("system.device.firmware"), value: orNone(d.firmware) },
      { label: tr("system.device.idf"), value: orNone(d.idf) },
      { label: tr("system.device.built"), value: orNone(d.built) },
      { label: tr("system.device.board"), value: orNone(d.board) },
      { label: tr("system.device.chip"), value: tr("system.device.chipRev", { chip: d.chip, rev: chipRevision(d.chip_revision) }) },
      { label: tr("system.device.flash"), value: mb(d.flash_bytes, tr) },
      { label: tr("system.device.partition"), value: orNone(d.partition), mono: true },
      { label: tr("system.device.uptime"), value: uptimeText(d.uptime_ms, tr) },
      { label: tr("system.device.reset"), value: resetReasonText(d.reset_reason, tr) },
      { label: tr("system.device.heapFree"), value: kb(d.heap_free, tr) },
      { label: tr("system.device.heapMin"), value: kb(d.heap_min_free, tr) },
      { label: tr("system.device.heapLargest"), value: kb(d.heap_largest_block, tr) },
      { label: tr("system.device.id"), value: orNone(d.device_id), mono: true },
      { label: tr("system.device.mac"), value: orNone(d.mac), mono: true },
    ],
  };
}

function networkSection(info: SystemInfo, tr: Tr): Section {
  const n = info.network;
  const title = tr("system.network.heading");
  if (!n.connected) {
    return { key: "network", title, aside: null, notes: [{ text: tr("system.network.disconnected"), tone: "warning" }], rows: [] };
  }
  const weak = n.rssi !== null && n.rssi < -70;
  return {
    key: "network", title, aside: null, notes: [],
    rows: [
      { label: tr("system.network.ssid"), value: orNone(n.ssid) },
      { label: tr("system.network.ip"), value: orNone(n.ip), mono: true },
      { label: tr("system.network.rssi"), value: rssiText(n.rssi, tr), ...(weak ? { tone: "warning" as const } : {}) },
      { label: tr("system.network.channel"), value: n.channel === null ? NONE : String(n.channel) },
    ],
  };
}

function busSection(info: SystemInfo, tr: Tr): Section {
  const b = info.bus;
  const tone = busTone(b.state);
  const count = (label: string, n: number, warnAbove0 = false): Row =>
    ({ label, value: String(n), ...(warnAbove0 && n > 0 ? { tone: "warning" as const } : {}) });
  return {
    key: "bus", title: tr("system.bus.heading"), aside: null, notes: [],
    rows: [
      { label: tr("system.bus.state"), value: busStateText(b.state, tr), ...(tone === "ok" ? {} : { tone }) },
      count(tr("system.bus.rx"), b.rx_frames),
      count(tr("system.bus.dropped"), b.rx_dropped, true),
      count(tr("system.bus.errors"), b.bus_errors, true),
      count(tr("system.bus.recoveries"), b.bus_recoveries, true),
      { label: tr("system.bus.counters"), value: `${b.tx_error_count} / ${b.rx_error_count}` },
      { label: tr("system.bus.nodes"), value: nodesText(b.nodes_seen, tr) },
    ],
  };
}

function countersSection(info: SystemInfo, tr: Tr): Section {
  const m = info.mqtt;
  return {
    key: "counters", title: tr("system.counters.heading"), aside: null, notes: [],
    rows: [
      { label: tr("system.commands"), value: tr("system.commands.value", { ...info.commands }) },
      {
        label: tr("system.mqtt"),
        value: `${mqttStateText(m, tr)} · ${tr("system.mqtt.value", {
          published: m.published, commands: m.commands, rejected: m.rejected, reconnects: m.reconnects,
        })}`,
        ...(m.configured && !m.connected ? { tone: "warning" as const } : {}),
      },
    ],
  };
}

/** The five cards, in the order the page shows them. */
export function sections(info: SystemInfo, tr: Tr): Section[] {
  return [unitSection(info, tr), deviceSection(info, tr), networkSection(info, tr), busSection(info, tr), countersSection(info, tr)];
}

/**
 * The plain-text summary "Copy as text" puts on the clipboard: every card, one "label: value"
 * per line, so it pastes legibly into any chat. `tr` is the English catalog on the page -- see
 * en/system.ts for why the copy is not in the UI language.
 */
export function copyText(info: SystemInfo, tr: Tr): string {
  const out: string[] = [tr("system.copy.title")];
  for (const s of sections(info, tr)) {
    out.push("", s.aside ? `${s.title} (${s.aside})` : s.title);
    for (const n of s.notes) out.push(`  ${n.text}`);
    for (const r of s.rows) out.push(`  ${r.label}: ${r.value}`);
    if (s.errors) {
      if (s.errors.empty !== null) out.push(`  ${s.errors.title}: ${s.errors.empty}`);
      else {
        out.push(`  ${s.errors.title}:`);
        for (const line of s.errors.lines) out.push(`    ${line}`);
      }
    }
  }
  return out.join("\n") + "\n";
}
