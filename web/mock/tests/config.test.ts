// web/mock/tests/config.test.ts
// The settings surface end to end: GET /api/config with redacted secrets, POST /api/config with
// the sentinel scheme and comfoair_config_apply()'s refusals, POST /api/provision's three psk
// states, the scan, and GET /api/status.
//
//   node mock/tests/config.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { CONFIG_UNCHANGED } from "../../src/api/secrets.ts";
import { tick } from "../evolve.ts";
import type { MockState } from "../model.ts";
import { basic, errorOf, fieldOf, fresh, get, post } from "./req.ts";

const cfg = (s: MockState) => get(s, "/api/config").body as Record<string, unknown>;

// --- GET: every secret redacted, derived fields present ----------------------------------
{
  const s = fresh();
  const c = cfg(s);
  eq(Object.keys(c), ["wifi_ssid", "wifi_psk", "mqtt_host", "mqtt_port", "mqtt_user", "mqtt_password", "topic_prefix", "ha_discovery", "device_name", "ui_password", "ui_password_set", "read_only", "wifi_known_good", "permanent_manual"], "the document has comfoair_wire_render_config()'s keys in its order");
  eq([c.wifi_psk, c.mqtt_password], [CONFIG_UNCHANGED, CONFIG_UNCHANGED], "a stored secret reads as the sentinel");
  eq([c.ui_password, c.ui_password_set], ["", false], "no UI password: empty, and not set");
  // No GET response may carry a stored secret anywhere in its text.
  const secrets = [s.config.wifi_psk, s.config.mqtt_password];
  for (const p of ["/api/config", "/api/status", "/api/state", "/api/log", "/api/entities"]) {
    const text = JSON.stringify(get(s, p).body);
    ok(secrets.every((x) => !text.includes(x)), `GET ${p} leaks no stored secret`);
  }
}

// --- POST: the sentinel keeps, "" clears, a value stores ---------------------------------
{
  const s = fresh();
  const whole = { ...cfg(s) } as Record<string, unknown>;
  delete whole.wifi_ssid; delete whole.wifi_psk;  // the page submits the document minus the network
  whole.mqtt_host = "broker.lan";
  const r = post(s, "/api/config", whole);
  eq([r.status, r.body], [200, { saved: true }], "saving the echoed document is 200 {saved:true}");
  eq([s.config.mqtt_host, s.config.mqtt_password], ["broker.lan", "broker-secret"], "the sentinel kept the broker password");
  post(s, "/api/config", { mqtt_password: "" });
  eq(cfg(s).mqtt_password, "", "\"\" clears it, and it then reads back as \"\"");
  const rej = post(s, "/api/config", { mqtt_password: CONFIG_UNCHANGED });
  eq([rej.status, fieldOf(rej)], [422, "mqtt-password"], "the sentinel with nothing stored is refused, not stored");
  eq(post(s, "/api/config", {}).status, 200, "an empty document is a save that changes nothing");
  eq(post(s, "/api/config", { future_knob: 1 }).status, 200, "an unknown key is ignored (a newer page, an older firmware)");
  eq(post(s, "/api/config", { permanent_manual: false }).status, 200, "permanent_manual travels on this route");
  eq(cfg(s).permanent_manual, false, "and reads back");
}

// --- POST: refusals, each with the sentence and the field the firmware sends ------------------
{
  const s = fresh();
  const wr = post(s, "/api/config", { wifi_ssid: "x", device_name: "y" });
  eq([wr.status, errorOf(wr), fieldOf(wr)], [422, "The network is set through POST /api/provision, not here.", "wifi_ssid"], "Wi-Fi on the wrong route names the route that works");
  eq(fieldOf(post(s, "/api/config", { wifi_psk: "x" })), "wifi_psk", "wifi_psk is refused by name too");
  const type = post(s, "/api/config", { mqtt_port: "1883" });
  eq([type.status, fieldOf(type)], [422, "mqtt_port"], "a wrong type names the KEY");
  eq(fieldOf(post(s, "/api/config", { device_name: "x".repeat(33) })), "device_name", "too long for its buffer: bad field by key");
  const cases: [Record<string, unknown>, string, string][] = [
    [{ mqtt_host: "mqtt://broker" }, "mqtt-host", "the broker address must be a host name or an IP address, with no scheme and no port"],
    [{ mqtt_port: 0 }, "mqtt-port", "the broker port must be between 1 and 65535"],
    [{ mqtt_port: 70000 }, "mqtt-port", "the broker port must be between 1 and 65535"],
    [{ topic_prefix: "a/#" }, "topic-prefix", "the topic prefix must be 1 to 64 characters, without spaces, + or #, and without an empty level"],
    [{ topic_prefix: "a//b" }, "topic-prefix", "the topic prefix must be 1 to 64 characters, without spaces, + or #, and without an empty level"],
    [{ device_name: "" }, "device-name", "the device name must be 1 to 32 characters and contain no control characters"],
    [{ ui_password: "short" }, "ui-password", "the web password must be 8 to 128 characters and contain no control characters"],
  ];
  for (const [body, field, sentence] of cases) {
    const r = post(s, "/api/config", body);
    eq([r.status, errorOf(r), fieldOf(r)], [422, sentence, field], `${JSON.stringify(body)} is refused by rule ${field}`);
  }
  const before = s.config.mqtt_host;
  post(s, "/api/config", { mqtt_host: "good.host", mqtt_port: 0 });
  eq(s.config.mqtt_host, before, "all or nothing: a good host beside a bad port changes neither");

  const ro = fresh("read_only");
  const r = post(ro, "/api/config", { device_name: "x" });
  eq([r.status, fieldOf(r)], [409, "read-only"], "settings from a newer firmware: 409 read-only");
  eq(cfg(ro).read_only, true, "and the document says so");
}

// --- setting the first password locks the API ----------------------------------------------
{
  const s = fresh();
  eq(post(s, "/api/config", { ui_password: "owner-pass-1" }).status, 200, "the first password is a bootstrap write");
  eq(get(s, "/api/config").status, 401, "after which reading needs it");
  const c = get(s, "/api/config", basic("", "owner-pass-1")).body as Record<string, unknown>;
  eq([c.ui_password, c.ui_password_set], [CONFIG_UNCHANGED, true], "and it reads back as the sentinel, set");
  post(s, "/api/config", { ui_password: "" }, basic("", "owner-pass-1"));
  eq(get(s, "/api/config").status, 200, "clearing it opens the device again");
}

// --- provisioning: three psk states, the sentinel refused, accepted before connected ----------
{
  const s = fresh("unprovisioned");
  const sentinel = post(s, "/api/provision", { wifi_ssid: "Kitchen", wifi_psk: CONFIG_UNCHANGED });
  eq([sentinel.status, fieldOf(sentinel)], [422, "wifi_psk"], "the sentinel is never stored as a key");
  const shortKey = post(s, "/api/provision", { wifi_ssid: "Kitchen", wifi_psk: "1234567" });
  eq(errorOf(shortKey), "the network password must be 8 to 63 characters, or exactly 64 hexadecimal digits, or empty for an open network", "a 7-char key is refused with the config sentence");
  eq(post(s, "/api/provision", { wifi_ssid: "Kitchen", wifi_psk: "g".repeat(64) }).status, 422, "64 chars must be hex");
  eq(fieldOf(post(s, "/api/provision", { wifi_psk: "x" })), "wifi_ssid", "no ssid is a bad field");
  eq(fieldOf(post(s, "/api/provision", { wifi_ssid: "" })), "wifi_ssid", "an empty ssid is refused");
  eq(fieldOf(post(s, "/api/provision", { wifi_ssid: "x".repeat(33) })), "wifi_ssid", "a 33-byte ssid does not fit");
  const r = post(s, "/api/provision", { wifi_ssid: "Kitchen", wifi_psk: "correct-horse" });
  const doc = r.body as Record<string, unknown>;
  eq([r.status, doc.state, doc.connected, "mqtt" in doc], [200, "connecting", false, false], "200 means ACCEPTED: connecting, no broker block yet");
  eq(doc.device_id, "a4cf12b3c4d5", "the reply says which device was configured");
  ok(!JSON.stringify(doc).includes("correct-horse"), "the reply never echoes the key");
  let t = s;
  for (let i = 0; i < 5; i++) t = tick(t, 1000);
  eq([t.scenario.provisioned, t.net.state], [true, "connected"], "a few seconds later the device has joined");
  eq(post(t, "/api/provision", { wifi_ssid: "Other" }).status, 403, "and now refuses re-homing without a password");

  const k = fresh("locked");
  const auth = basic("", "comfoair-mock");
  post(k, "/api/provision", { wifi_ssid: "Guest", wifi_psk: "" }, auth);
  eq(k.config.wifi_psk, "", "\"\" is an OPEN network");
  post(k, "/api/provision", { wifi_ssid: "Kitchen", wifi_psk: "kitchen-key" }, auth);
  post(k, "/api/provision", { wifi_ssid: "Kitchen" }, auth);
  eq(k.config.wifi_psk, "kitchen-key", "an absent key KEEPS the stored one");
  eq(k.net.state, "trial", "a device with a known-good network tries the new one on trial");
}

// --- scan and status -----------------------------------------------------------------
{
  const s = fresh();
  const list = get(s, "/api/wifi/scan").body as { ssid: string; rssi: number; secure: boolean }[];
  ok(list.every((n) => typeof n.ssid === "string" && typeof n.rssi === "number" && n.rssi < 0 && typeof n.secure === "boolean"), "every row is {ssid, rssi<0, secure}");
  ok(list.filter((n) => n.ssid === "home-wifi").length > 1, "duplicates (one per BSSID) are not filtered");
  ok(list.some((n) => n.ssid === ""), "a hidden network is reported with an empty ssid");
  const un = get(fresh("scan_unavailable"), "/api/wifi/scan");
  eq([un.status, errorOf(un)], [503, "scan unavailable"], "a radio that cannot scan: 503 scan unavailable");
  const st = get(s, "/api/status").body as Record<string, unknown>;
  eq([st.state, st.mode, st.provisioned, st.setup_window_remaining_ms], ["connected", "station", true, null], "a claimed device: connected, station, no window (null, never 0)");
  eq(Object.keys(st.mqtt as object), ["configured", "connected", "published", "commands", "rejected", "reconnects", "last_connack", "last_connack_reason", "link", "failure", "attempts", "retry_in_ms", "subscriptions", "disconnects", "applied_epoch", "last_transport_errno"], "the broker block is the last member, with comfoair_wire's keys");
  eq(Object.keys(st).at(-1), "mqtt", "and it is the last member");
  eq([st.phase, typeof st.uptime_ms, (st.mqtt as Record<string, unknown>).link], ["online", "number", "online"], "phase, uptime_ms and the link word are present");
  const ap = get(fresh("unprovisioned"), "/api/status").body as Record<string, unknown>;
  eq([ap.state, ap.mode, ap.ap_on_air, ap.setup_window_open], ["unconfigured", "access-point", true, true], "an unclaimed device is on its access point");
}

report("mock/config");
