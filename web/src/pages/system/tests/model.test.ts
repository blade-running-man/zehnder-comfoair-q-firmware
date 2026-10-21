// The System page's pure helpers and the copied summary, against the English catalog.
//
// Run: node src/pages/system/tests/model.test.ts
import { eq, ok, report } from "../../settings/tests/harness.ts";
import { en } from "../../../i18n/catalogs/en.ts";
import { format } from "../../../i18n/format.ts";
import type { SystemInfo } from "../../../api/system.ts";
import {
  ageText, busTone, chipRevision, errorLine, errorsView, kb, mb, nodesText, resetReasonText,
  rssiQuality, rssiText, unitHasValues, uptimeParts, uptimeText, type Tr,
} from "../model.ts";
import { copyText, sections } from "../sections.ts";

const tr: Tr = (key, params) => format(en[key], "en", params);
const MIN = 60_000, HOUR = 60 * MIN, DAY = 24 * HOUR;

function doc(): SystemInfo {
  return {
    unit: {
      state: "ok", age_ms: 3 * MIN + 5000,
      model: "ComfoAir Q350 NL R VV ST", serial: "SIT000000000000000000000000000", firmware: "R1.11.0",
      firmware_raw: 3222285312, article: "471502057", country: "NL", name: "ComfoAir Q",
      errors: { state: "ok", ids: [], items: [] },
    },
    device: {
      firmware: "0.1.0", idf: "v5.5.5", built: "Oct 17 2026 10:00:09", board: "m5stack-nanoc6",
      chip: "esp32c6", chip_revision: 2, flash_bytes: 4194304, partition: "ota_0",
      uptime_ms: 3 * DAY + 4 * HOUR + 12 * MIN + 30_000, reset_reason: "power_on",
      heap_free: 150000, heap_min_free: 120000, heap_largest_block: 90000,
      device_id: "9c139ed48ae4", mac: "9c:13:9e:d4:8a:e4",
    },
    network: { connected: true, ssid: "home-wifi", ip: "192.168.50.123", rssi: -61, channel: 6 },
    bus: {
      started: true, state: "active", rx_frames: 1200, rx_dropped: 0, bus_errors: 0, bus_recoveries: 0,
      tx_error_count: 0, rx_error_count: 0, nodes_seen: [1],
    },
    commands: { queued: 0, sent: 4, failed: 0, dropped: 0 },
    mqtt: { configured: true, connected: true, published: 812, commands: 3, rejected: 0, reconnects: 1 },
  };
}

// --- uptime ------------------------------------------------------------------------------------
eq(uptimeParts(3 * DAY + 4 * HOUR + 12 * MIN + 59_999), { d: 3, h: 4, m: 12 }, "parts round down");
eq(uptimeText(3 * DAY + 4 * HOUR + 12 * MIN, tr), "3 d 4 h 12 min", "days, hours, minutes");
eq(uptimeText(DAY, tr), "1 d 0 h 0 min", "a whole day keeps its zero hours and minutes");
eq(uptimeText(4 * HOUR + 12 * MIN, tr), "4 h 12 min", "no leading zero days");
eq(uptimeText(59_000, tr), "0 min", "under a minute is 0 min");
eq(uptimeText(-5, tr), "0 min", "a negative uptime cannot happen and does not render as one");

// --- sizes, chip -------------------------------------------------------------------------------
eq(kb(150000, tr), "146 KB", "heap in whole KiB");
eq(kb(512, tr), "1 KB", "rounded, not floored");
eq(mb(4194304, tr), "4 MB", "4 MiB flash");
eq(mb(2621440, tr), "2.5 MB", "a fractional size keeps one decimal");
eq(chipRevision(2), "0.2", "rev 2 is v0.2");
eq(chipRevision(101), "1.1", "rev 101 is v1.1");
eq(chipRevision(300), "3.0", "rev 300 is v3.0");

// --- reset reason ------------------------------------------------------------------------------
eq(resetReasonText("power_on", tr), "Power on", "a fixed word is translated");
eq(resetReasonText("brownout", tr), "Supply voltage dropped (brownout)", "brownout is explained");
eq(resetReasonText("cosmic_ray", tr), "cosmic_ray", "an unknown word is shown as sent");
eq(resetReasonText("toString", tr), "toString", "a prototype name is not a known word");

// --- rssi --------------------------------------------------------------------------------------
eq([-40, -60, -61, -70, -71].map(rssiQuality), ["good", "good", "fair", "fair", "weak"], "-60 good, -70 fair, below weak");
eq(rssiText(-61, tr), "-61 dBm (fair)", "rssi with its word");
eq(rssiText(null, tr), "—", "unknown rssi");

// --- bus ---------------------------------------------------------------------------------------
eq(["active", "warning", "passive", "off", "stopped", "new"].map(busTone),
   ["ok", "warning", "problem", "problem", "problem", "problem"], "passive/off/stopped are problems");
eq(nodesText([1, 0x2a, 0x30], tr), "0x01 ventilation unit, 0x2A this device, 0x30", "nodes are labelled");
eq(nodesText([], tr), "none heard", "no nodes");

// --- unit age and errors -----------------------------------------------------------------------
eq(ageText(null, tr), null, "never read has no age");
eq(ageText(30_000, tr), "read just now", "under a minute");
eq(ageText(3 * MIN + 5000, tr), "read 3 min ago", "minutes");
eq(ageText(5 * HOUR + 59 * MIN, tr), "read 5 h ago", "past an hour, hours");
eq(errorsView({ state: "unknown", ids: [], items: [] }), { kind: "notRead" }, "not read is not 'none'");
eq(errorsView({ state: "ok", ids: [], items: [] }), { kind: "none" }, "read and empty is none");
eq(errorsView({ state: "ok", ids: [21, 99], items: [{ id: 21, text: "Overheating" }] }),
   { kind: "list", items: [{ id: 21, text: "Overheating" }, { id: 99, text: null }] },
   "an id missing from items is listed without text, not dropped");
eq(errorLine({ id: 99, text: null }, tr), "99 — Unknown error", "a null text is Unknown error");
eq(errorLine({ id: 21, text: "Overheating" }, tr), "21 — Overheating", "id — text");
{
  const d = doc();
  ok(unitHasValues(d.unit), "a read unit has values");
  d.unit = { ...d.unit, state: "unknown", age_ms: null };
  ok(!unitHasValues(d.unit), "never read has none");
  d.unit = { ...d.unit, state: "no_answer", age_ms: 7 * HOUR };
  ok(unitHasValues(d.unit), "a silent last read keeps the older values");
}

// --- sections ----------------------------------------------------------------------------------
{
  const d = doc();
  d.unit = {
    ...d.unit, state: "unknown", age_ms: null, model: null, serial: null, firmware: null, firmware_raw: null,
    article: null, country: null, name: null, errors: { state: "unknown", ids: [], items: [] },
  };
  const u = sections(d, tr)[0];
  eq([u.notes, u.rows.length, u.aside], [[{ text: "Not read yet", tone: "dim" }], 0, null], "an unread unit says so and shows no rows");
  eq(u.errors?.empty, "Not read", "and its errors are not read");
}
{
  const d = doc();
  d.unit.state = "no_answer";
  const u = sections(d, tr)[0];
  eq(u.notes.map((n) => n.tone), ["warning"], "no_answer is a warning, values stay");
  eq(u.rows.length, 6, "six identity rows");
}
{
  const d = doc();
  d.network = { connected: false, ssid: null, ip: null, rssi: null, channel: null };
  d.bus = { ...d.bus, state: "off", bus_errors: 12 };
  const [, , net, bus] = sections(d, tr);
  eq(net.notes[0].text, "Not connected to Wi-Fi", "disconnected network");
  eq(bus.rows[0], { label: "Controller state", value: "Bus off", tone: "problem" }, "bus off is a problem");
  eq(bus.rows[3].tone, "warning", "bus errors above zero are a warning");
}

// --- copy text ---------------------------------------------------------------------------------
{
  const d = doc();
  d.unit.errors = { state: "ok", ids: [21, 99], items: [{ id: 21, text: "Overheating" }, { id: 99, text: null }] };
  const text = copyText(d, tr);
  const lines = text.split("\n");
  eq(lines[0], "ComfoAir Q system summary", "a title line");
  ok(lines.includes("Ventilation unit (read 3 min ago)"), "the unit heading carries its age");
  ok(lines.includes("  Model: ComfoAir Q350 NL R VV ST"), "label: value rows");
  ok(lines.includes("  Active errors:") && lines.includes("    21 — Overheating") && lines.includes("    99 — Unknown error"),
     "errors are listed one per line");
  ok(lines.includes("  Chip: esp32c6 rev 0.2"), "the chip with its revision");
  ok(lines.includes("  Uptime: 3 d 4 h 12 min"), "uptime");
  ok(lines.includes("  Signal: -61 dBm (fair)"), "rssi");
  ok(lines.includes("  Nodes on the bus: 0x01 ventilation unit"), "nodes");
  ok(lines.includes("  MQTT: connected · 812 published · 3 commands · 0 rejected · 1 reconnects"), "mqtt");
  ok(lines.includes("  Commands: 0 queued · 4 sent · 0 failed · 0 dropped"), "commands");
  ok(text.endsWith("\n") && !text.includes("{"), "ends in a newline, no placeholder left unfilled");
  for (const s of ["Ventilation unit", "This device", "Network", "CAN bus", "Commands and MQTT"]) {
    ok(lines.some((l) => l.startsWith(s)), `the ${s} card is in the copy`);
  }
}
{
  const d = doc();
  ok(copyText(d, tr).includes("  Active errors: No active errors"), "no errors is one calm line");
}

report("system/model");
