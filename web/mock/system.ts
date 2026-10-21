// web/mock/system.ts
// GET /api/system: the summary comfoair_sysinfo renders (docs/device/ota-and-system.md), projected
// from the MockState with the same null rules.
// Pure: the clock is MockState.uptimeMs.
//
// The unit's identity is the live Q350's (docs/reference/rmi-probe-findings-q350.md §2), with the
// serial's middle masked. The device block is the m5stack-nanoc6 the firmware runs on. The
// identity is read the way comfoair_probe_task reads it -- 10 s after boot, then every 6 h -- so a
// freshly started mock shows "Not read yet" for its first ten seconds, like the device.
import type { MockResponse, MockState } from "./model.ts";
import { DEVICE, mqttCounters } from "./network.ts";

export const IDENTITY_FIRST_MS = 10_000;
export const IDENTITY_EVERY_MS = 6 * 60 * 60 * 1000;
const UNIT_NODE = 0x01;

export const UNIT_IDENTITY = {
  model: "ComfoAir Q350 NL R VV ST",
  serial: "SIT000000000000000000000000000",
  firmware: "R1.11.0",
  firmware_raw: 3222285312,  // 0xC0102C00
  article: "471502057",
  country: "NL",
  name: "ComfoAir Q",
} as const;

// The `errors` knob: one id the firmware's table knows and one it does not (text null). Wording
// in the style of the firmware's error table; the mock's, not a firmware contract.
export const MOCK_ERRORS = [
  { id: 21, text: "DANGER! OVERHEATING! Two or more sensors are detecting an incorrect temperature. Ventilation has stopped." },
  { id: 99, text: null },
] as const;

/** MOCK_ERRORS as the firmware's one line (comfoair_sysinfo_errors_summary()): ascending ids,
 *  " | " between texts, "Unknown error <id>" for an id its table does not know. */
export const MOCK_ERROR_SUMMARY = MOCK_ERRORS.map((e) => e.text ?? `Unknown error ${e.id}`).join(" | ");

const NULL_IDENTITY = {
  model: null, serial: null, firmware: null, firmware_raw: null, article: null, country: null, name: null,
};

/** The bus delivers nothing: the unit cannot have been read. */
function busSilent(s: MockState): boolean {
  return s.scenario.commands === "down" || s.scenario.readings === "none";
}

/** The unit has never answered a read: the unread knob, a silent bus, or before the first read.
 *  One predicate for GET /api/system's unit block AND the two error entities (values.ts), so the
 *  entities cannot say "no error" while the system document still says "unknown". */
export function unitNeverRead(s: MockState): boolean {
  return s.scenario.unitInfo === "unread" || busSilent(s) || s.uptimeMs < IDENTITY_FIRST_MS;
}

function unitBlock(s: MockState): Record<string, unknown> {
  const knob = s.scenario.unitInfo;
  if (unitNeverRead(s)) {
    return { state: "unknown", age_ms: null, ...NULL_IDENTITY, errors: { state: "unknown", ids: [], items: [] } };
  }
  const sinceFirst = s.uptimeMs - IDENTITY_FIRST_MS;
  if (knob === "no_answer") {
    // Only the first read answered; every one since went silent, so the age keeps growing.
    return { state: "no_answer", age_ms: sinceFirst, ...UNIT_IDENTITY, errors: { state: "no_answer", ids: [], items: [] } };
  }
  const items = knob === "errors" ? MOCK_ERRORS.map((e) => ({ ...e })) : [];
  return {
    state: "ok", age_ms: sinceFirst % IDENTITY_EVERY_MS, ...UNIT_IDENTITY,
    errors: { state: "ok", ids: items.map((e) => e.id), items },
  };
}

function deviceBlock(s: MockState): Record<string, unknown> {
  const sec = Math.floor(s.uptimeMs / 1000);
  return {
    firmware: s.swVersion, idf: "v5.5.5", built: "Oct 17 2026 10:00:09", board: "m5stack-nanoc6",
    chip: "esp32c6", chip_revision: 2, flash_bytes: 4 * 1024 * 1024, partition: s.partition,
    uptime_ms: s.uptimeMs, reset_reason: "power_on",
    // A little movement, so a poll that changed nothing is visible as one that did not happen.
    heap_free: 150_000 - (sec % 7) * 512, heap_min_free: 120_000, heap_largest_block: 90_112,
    device_id: DEVICE.device_id, mac: DEVICE.mac,
  };
}

function networkBlock(s: MockState): Record<string, unknown> {
  const up = s.scenario.provisioned && s.net.state === "connected";
  if (!up) return { connected: false, ssid: null, ip: null, rssi: null, channel: null };
  return { connected: true, ssid: s.config.wifi_ssid, ip: "192.168.1.57", rssi: -61, channel: 6 };
}

function busBlock(s: MockState): Record<string, unknown> {
  if (s.scenario.commands === "down") {
    return {
      started: false, state: "stopped", rx_frames: 0, rx_dropped: 0, bus_errors: 0, bus_recoveries: 0,
      tx_error_count: 0, rx_error_count: 0, nodes_seen: [],
    };
  }
  const live = s.scenario.readings === "live";
  return {
    started: true, state: "active",
    // ~45 PDO frames a second off a Q350 at rest.
    rx_frames: live ? Math.floor(s.uptimeMs / 1000) * 45 : 0,
    rx_dropped: 0, bus_errors: 0, bus_recoveries: 0, tx_error_count: 0, rx_error_count: 0,
    nodes_seen: live ? [UNIT_NODE] : [],
  };
}

export function renderSystem(s: MockState): Record<string, unknown> {
  // Key order is the firmware's (comfoair_sysinfo_render()): unit, device, network, bus,
  // commands, mqtt.
  return {
    unit: unitBlock(s),
    device: deviceBlock(s),
    network: networkBlock(s),
    bus: busBlock(s),
    commands: { queued: 0, sent: 0, failed: 0, dropped: 0 },
    mqtt: mqttCounters(s),
  };
}

export function getSystem(s: MockState): MockResponse {
  return { status: 200, body: renderSystem(s) };
}
