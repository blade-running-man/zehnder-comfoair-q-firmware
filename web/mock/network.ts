// web/mock/network.ts
// POST /api/provision, GET /api/wifi/scan, GET /api/status and GET /api/log.
//
// Provisioning mirrors comfoair_wire_parse_provision(): `wifi_psk` has THREE states -- a key, ""
// for an open network, absent for "keep the stored key" -- and the sentinel is REFUSED as a key,
// because storing it would leave the device holding credentials that cannot associate. The answer
// is sent BEFORE the device joins (200 means accepted, never connected); evolve.ts finishes the
// join a few seconds later, which is when the policy starts treating the device as provisioned.
import { logLine, type MockResponse, type MockState } from "./model.ts";
import { PROVISION_CONNECT_MS } from "./evolve.ts";
import { CONFIG_ERR, HTTP, badBody, badField, bodyGate, bytes, error, isObject, readString } from "./wire.ts";
import { decideSecret } from "./config.ts";

/** The device's identity. sw_version is NOT here: an OTA changes it (MockState.swVersion). */
export const DEVICE = { device_id: "a4cf12b3c4d5", mac: "a4:cf:12:b3:c4:d5" } as const;
const AP_SSID = "comfoair-c4d5";  // build_ap_ssid(): "comfoair-" + the last two MAC bytes

// comfoair_config_check_psk(): empty (open), or 8..64 bytes with no NUL, and 64 means hex.
function pskOk(psk: string): boolean {
  const n = bytes(psk);
  if (n === 0) return true;
  if (n < 8 || n > 64 || psk.includes("\u0000")) return false;
  return n < 64 || /^[0-9a-fA-F]{64}$/.test(psk);
}

/** comfoair_wire_render_provision(): the status document, with or without the broker block. */
export function provisionDoc(s: MockState, withMqtt: boolean): Record<string, unknown> {
  const n = s.net, prov = s.scenario.provisioned;
  const connected = n.state === "connected";
  const apOn = !prov;
  const doc: Record<string, unknown> = {
    state: n.state,
    mode: !apOn ? "station" : connected || n.connectingLeftMs > 0 ? "ap-station" : "access-point",
    phase: phaseOf(s, connected),
    provisioned: prov,
    has_credentials: s.config.wifi_ssid !== "",
    wifi_known_good: n.knownGood,
    connected,
    wifi_ssid: s.config.wifi_ssid,
    ip: connected ? "192.168.1.57" : "",
    ap_ssid: AP_SSID,
    ap_on_air: apOn,
    setup_window_open: apOn && n.windowLeftMs > 0,
    // null, never 0: "no time left" and "no end" are opposite answers (put_duration).
    setup_window_remaining_ms: apOn ? n.windowLeftMs : null,
    fallback_ap_in_ms: null,
    uptime_ms: s.uptimeMs,
    failure: "none",
    failure_reason: 0,
    // null, never 0, when not associated: 0 dBm is a legal reading (comfoair_wire_render_status.c,
    // has_rssi).
    rssi: connected ? -58 : null,
    ...DEVICE,
    sw_version: s.swVersion,
  };
  if (withMqtt) doc.mqtt = mqttBlock(s);
  return doc;
}

/** comfoair_phase_name()'s word for the simulated device -- only the phases the mock can reach. */
function phaseOf(s: MockState, connected: boolean): string {
  if (!s.scenario.provisioned) return "setup-ap";
  if (!connected) return "connecting";
  if (s.scenario.readings !== "live") return "bus-silent";
  return mqttCounters(s).connected || s.config.mqtt_host === "" ? "online" : "no-broker";
}

/** The status document's broker block, comfoair_wire_render_status.c's member order: the
 *  counters, then the diagnosis (link, failure, attempts, retry_in_ms, subscriptions, disconnects,
 *  applied_epoch, last_transport_errno). The mock's broker never fails: a configured one that is
 *  not up yet is "connecting" with no retry scheduled, which pages/state/brokerStatus.ts reads as a
 *  dial in flight rather than a fault -- dev:mock must not raise the dashboard's broker alarm by
 *  default. */
function mqttBlock(s: MockState): Record<string, unknown> {
  const c = mqttCounters(s);
  return {
    ...c, last_connack: 0, last_connack_reason: "no refusal",
    link: !c.configured ? "off" : c.connected ? "online" : "connecting",
    failure: "none", attempts: c.connected ? 0 : 1, retry_in_ms: null,
    subscriptions: c.connected ? "granted" : "none",
    disconnects: 0, applied_epoch: 0, last_transport_errno: 0,
  };
}

/** The broker link's counters: one source for /api/status's mqtt block and /api/system's. */
export function mqttCounters(s: MockState): {
  configured: boolean; connected: boolean; published: number; commands: number; rejected: number; reconnects: number;
} {
  const configured = s.config.mqtt_host !== "";
  const up = configured && s.scenario.provisioned && s.scenario.readings === "live";
  return { configured, connected: up, published: s.mqttPublished, commands: 0, rejected: 0, reconnects: up ? 1 : 0 };
}

export function getStatus(s: MockState): MockResponse {
  return { status: 200, body: provisionDoc(s, true) };
}

export function provision(s: MockState, rawLength: number, body: unknown): MockResponse {
  const gate = bodyGate(rawLength);
  if (gate) return gate;
  if (!isObject(body)) return badBody();
  const ssid = readString(body, "wifi_ssid", 33);
  if (typeof ssid !== "string") return badField("wifi_ssid");
  const psk = readString(body, "wifi_psk", 65);
  if (psk === false) return badField("wifi_psk");

  const stored = s.config.wifi_psk;
  let next: string;
  switch (decideSecret(stored !== "", psk)) {
    case "keep": next = stored; break;   // KEEP of nothing is an open-network attempt
    case "clear": next = ""; break;
    case "store": next = psk ?? ""; break;
    default: return error(422, CONFIG_ERR.psk, "wifi_psk");
  }
  if (ssid === "" || ssid.includes("\u0000")) return error(422, CONFIG_ERR.ssid, "wifi_ssid");
  if (!pskOk(next)) return error(422, CONFIG_ERR.psk, "wifi_psk");

  s.config.wifi_ssid = ssid;
  s.config.wifi_psk = next;
  // A device that already had a working network tries the new one on TRIAL and can roll back;
  // one that never had one is simply connecting.
  s.net.state = s.net.knownGood ? "trial" : "connecting";
  s.net.connectingLeftMs = PROVISION_CONNECT_MS;
  logLine(s, `net: provisioning "${ssid}" (${next === "" ? "open" : "secured"})`);
  // No broker block: the radio has not moved to the owner's network yet, so nothing here has been
  // in a position to reach a broker, and zeros would read as a configured broker gone silent.
  return { status: 200, body: provisionDoc(s, false) };
}

/** The radio's sweep. Duplicates (a mesh, one record per BSSID) and a hidden network are kept on
    purpose: the page owns deduplication, and both are what a real sweep returns. */
export function wifiScan(s: MockState): MockResponse {
  if (s.scenario.scan === "unavailable") return error(503, HTTP.scanUnavailable);
  return {
    status: 200,
    body: [
      { ssid: "home-wifi", rssi: -47, secure: true },
      { ssid: "home-wifi", rssi: -68, secure: true },
      { ssid: "Kitchen-Mesh", rssi: -55, secure: true },
      { ssid: "Guest", rssi: -61, secure: false },
      { ssid: "", rssi: -74, secure: true },
      // An SSID is chosen by whoever set up the router next door, JSON included.
      { ssid: "neighbour\"},{\"ssid\":\"x", rssi: -83, secure: true },
      { ssid: "FRITZ!Box 7590 XY", rssi: -88, secure: true },
    ],
  };
}

export function getLog(s: MockState): MockResponse {
  return { status: 200, body: s.log.slice() };
}
