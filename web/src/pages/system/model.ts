// The System page's pure half: how each number of GET /api/system is spelled, with no DOM, no
// signals and no t(). Every helper that needs a word takes a `Tr` -- the page passes t() for the
// cards and the English catalog for the copied summary (copyText.ts) -- so this file runs under
// node in the suite (tests/model.test.ts) and a language switch still re-renders every word.
//
// Imports are TYPE-only, and carry .ts: pages/settings/tests/harness.ts explains both constraints.

import type { SystemInfo, SystemUnit, UnitErrorItem, UnitErrors } from "../../api/system.ts";
import type { MsgKey } from "../../i18n/catalogs/en.ts";
import type { Params } from "../../i18n/format.ts";

/** A translator: t() for the screen, the English catalog for the copied text. */
export type Tr = (key: MsgKey, params?: Params) => string;

/** This device's node id on the bus (0x2A), and the unit's (0x01) -- docs/protocol/probe.md. */
export const UNIT_NODE = 0x01;
export const OUR_NODE = 0x2a;

/** A string field the unit never answered. */
export const NONE = "—";

export function hex2(n: number): string {
  return "0x" + n.toString(16).toUpperCase().padStart(2, "0");
}

export function orNone(v: string | null | undefined): string {
  return v === null || v === undefined || v === "" ? NONE : v;
}

// --- device ----------------------------------------------------------------------------------

/** Whole days, hours and minutes, each rounded DOWN: "1 h 0 min" until the 61st minute has
 *  passed, never "1 h 1 min" early. */
export function uptimeParts(ms: number): { d: number; h: number; m: number } {
  const minutes = Math.floor(Math.max(0, ms) / 60_000);
  return { d: Math.floor(minutes / 1440), h: Math.floor((minutes % 1440) / 60), m: minutes % 60 };
}

/** "3 d 4 h 12 min"; the leading zero units are dropped ("4 h 12 min", "12 min"). */
export function uptimeText(ms: number, tr: Tr): string {
  const p = uptimeParts(ms);
  if (p.d > 0) return tr("system.uptime.dhm", p);
  if (p.h > 0) return tr("system.uptime.hm", p);
  return tr("system.uptime.m", p);
}

/** Bytes as whole KiB, spelled "KB" like ESP-IDF's own heap reports. */
export function kb(bytes: number, tr: Tr): string {
  return tr("system.kb", { n: Math.round(bytes / 1024) });
}

/** Flash sizes are powers of two, so 4194304 is "4 MB"; a fraction keeps one decimal. */
export function mb(bytes: number, tr: Tr): string {
  const n = bytes / 1_048_576;
  return tr("system.mb", { n: Number.isInteger(n) ? n : Math.round(n * 10) / 10 });
}

/** esp_chip_info's revision is major*100 + minor: 2 is "0.2", 101 is "1.1". */
export function chipRevision(rev: number): string {
  return `${Math.floor(rev / 100)}.${rev % 100}`;
}

export const RESET_KEYS: Record<string, MsgKey> = {
  power_on: "system.reset.power_on",
  software: "system.reset.software",
  panic: "system.reset.panic",
  watchdog: "system.reset.watchdog",
  brownout: "system.reset.brownout",
  deep_sleep: "system.reset.deep_sleep",
  external: "system.reset.external",
  usb: "system.reset.usb",
  jtag: "system.reset.jtag",
  unknown: "system.reset.unknown",
};

/** A word this page does not know (a newer firmware's) is shown as the firmware spelled it. */
export function resetReasonText(reason: string, tr: Tr): string {
  const key = Object.hasOwn(RESET_KEYS, reason) ? RESET_KEYS[reason] : undefined;
  return key ? tr(key) : reason;
}

// --- network ---------------------------------------------------------------------------------

export type RssiQuality = "good" | "fair" | "weak";

/** The usual Wi-Fi rule of thumb: -60 dBm and better is good, down to -70 fair, below weak. */
export function rssiQuality(rssi: number): RssiQuality {
  if (rssi >= -60) return "good";
  if (rssi >= -70) return "fair";
  return "weak";
}

export function rssiText(rssi: number | null, tr: Tr): string {
  if (rssi === null) return NONE;
  return tr("system.network.rssiValue", { rssi, quality: tr(`system.network.${rssiQuality(rssi)}`) });
}

// --- bus -------------------------------------------------------------------------------------

export type BusTone = "ok" | "warning" | "problem";

/** passive and off are the states where this device can no longer talk to the unit reliably
 *  (passive: it may not send error frames; off: it sends nothing at all), and stopped means the
 *  controller never ran. warning is the counters climbing while traffic still flows. */
export function busTone(state: string): BusTone {
  if (state === "active") return "ok";
  if (state === "warning") return "warning";
  return "problem";
}

const BUS_KEYS: Record<string, MsgKey> = {
  active: "system.bus.state.active",
  warning: "system.bus.state.warning",
  passive: "system.bus.state.passive",
  off: "system.bus.state.off",
  stopped: "system.bus.state.stopped",
};

export function busStateText(state: string, tr: Tr): string {
  const key = Object.hasOwn(BUS_KEYS, state) ? BUS_KEYS[state] : undefined;
  return key ? tr(key) : state;
}

/** "0x01 ventilation unit", "0x2A this device", or the bare id for any other node. */
export function nodeText(id: number, tr: Tr): string {
  if (id === UNIT_NODE) return tr("system.bus.node.unit", { id: hex2(id) });
  if (id === OUR_NODE) return tr("system.bus.node.self", { id: hex2(id) });
  return hex2(id);
}

export function nodesText(ids: number[], tr: Tr): string {
  return ids.length ? ids.map((id) => nodeText(id, tr)).join(", ") : tr("system.bus.noNodes");
}

// --- unit ------------------------------------------------------------------------------------

/** When the identity was read, as "read N min ago"; null when it never was. Past an hour the
 *  minutes become hours: the read repeats every six, and "read 341 min ago" is not a time. */
export function ageText(ageMs: number | null, tr: Tr): string | null {
  if (ageMs === null) return null;
  const minutes = Math.floor(Math.max(0, ageMs) / 60_000);
  if (minutes < 1) return tr("system.unit.readNow");
  if (minutes < 60) return tr("system.unit.readMin", { n: minutes });
  return tr("system.unit.readHours", { n: Math.floor(minutes / 60) });
}

/** The unit card has values to show: it answered once, even if the latest read went silent. */
export function unitHasValues(unit: SystemUnit): boolean {
  return unit.state !== "unknown" || unit.age_ms !== null;
}

export type ErrorsView =
  | { kind: "notRead" }
  | { kind: "none" }
  | { kind: "list"; items: UnitErrorItem[] };

/** Active errors as the card shows them. `items` is the firmware's id -> text join; an id that
 *  is in `ids` and missing from `items` is still listed, with no text, rather than dropped. */
export function errorsView(errors: UnitErrors): ErrorsView {
  if (errors.state !== "ok") return { kind: "notRead" };
  const items = errors.items.slice();
  for (const id of errors.ids) {
    if (!items.some((i) => i.id === id)) items.push({ id, text: null });
  }
  return items.length ? { kind: "list", items } : { kind: "none" };
}

/** "21 — DANGER! OVERHEATING! …", or "Unknown error" for an id the firmware has no text for. */
export function errorLine(item: UnitErrorItem, tr: Tr): string {
  return `${item.id} — ${item.text ?? tr("system.errors.unknown")}`;
}

/** The MQTT link in one word: connected, not connected, or not configured at all. */
export function mqttStateText(info: SystemInfo["mqtt"], tr: Tr): string {
  if (!info.configured) return tr("system.mqtt.unconfigured");
  return tr(info.connected ? "system.mqtt.connected" : "system.mqtt.disconnected");
}
