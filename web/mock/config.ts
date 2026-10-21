// web/mock/config.ts
// GET and POST /api/config, mirroring comfoair_wire_render_config / comfoair_wire_parse_config /
// comfoair_config_apply.
//
// SECRETS ARE WRITE-ONLY. A stored secret reads back as the sentinel, an empty one as "", and
// the value itself is in no projection -- the same rule the firmware enforces in
// comfoair_config_public_t. The sentinel is imported from the SPA's own constant so the two can
// never be spelled differently.
import { CONFIG_UNCHANGED } from "../src/api/secrets.ts";
import { logLine, type MockConfig, type MockResponse, type MockState } from "./model.ts";
import {
  CONFIG_ERR, WIRE, badBody, badField, bodyGate, error, isObject, isU32, readString, type ConfigErr,
} from "./wire.ts";

export const SENTINEL = CONFIG_UNCHANGED;

const redact = (stored: string): string => (stored !== "" ? SENTINEL : "");

export function getConfig(s: MockState): MockResponse {
  const c = s.config;
  // Key order is comfoair_wire_render_config()'s, so a byte-level diff against a device is quiet.
  return {
    status: 200,
    body: {
      wifi_ssid: c.wifi_ssid, wifi_psk: redact(c.wifi_psk),
      mqtt_host: c.mqtt_host, mqtt_port: c.mqtt_port, mqtt_user: c.mqtt_user,
      mqtt_password: redact(c.mqtt_password), topic_prefix: c.topic_prefix,
      ha_discovery: c.ha_discovery, device_name: c.device_name, ui_password: redact(c.ui_password),
      ui_password_set: c.ui_password !== "", read_only: s.scenario.configReadOnly,
      wifi_known_good: s.net.knownGood, permanent_manual: c.permanent_manual,
    },
  };
}

/** comfoair_secret_decide(): what a submitted secret means given what is stored. */
export type SecretAction = "keep" | "clear" | "store" | "reject";
export function decideSecret(hasStored: boolean, submitted: string | undefined): SecretAction {
  if (submitted === undefined) return "keep";
  // The sentinel with nothing stored is not an untouched box -- it is the PUBLISHED string being
  // submitted as a value, and storing it would make it every unconfigured device's password.
  if (submitted === SENTINEL) return hasStored ? "keep" : "reject";
  if (submitted === "") return hasStored ? "clear" : "keep";
  return "store";
}

// comfoair_config_check_*: what a value may BE.
const HOST_CHARS = /^[A-Za-z0-9._:\[\]-]*$/;
const CONTROL = /[\u0000-\u001f\u007f]/;
function checkPrefix(p: string): boolean {
  return p !== "" && p.length <= 64 && p[0] !== "$" && p[0] !== "/" && !p.endsWith("/") &&
    !/[+# ]/.test(p) && !CONTROL.test(p) && !p.includes("//");
}
const checkName = (n: string): boolean => n !== "" && new TextEncoder().encode(n).length <= 32 && !CONTROL.test(n);
const checkUiPassword = (p: string): boolean => {
  const n = new TextEncoder().encode(p).length;
  return n >= 8 && n <= 128 && !CONTROL.test(p);
};

// Buffer sizes of comfoair_wire_patch_storage_t (each field's maximum plus its NUL).
const CAPS = { mqtt_host: 254, mqtt_user: 129, mqtt_password: 129, topic_prefix: 65, device_name: 33, ui_password: 129 } as const;
type StringKey = keyof typeof CAPS;

const refused = (err: ConfigErr): MockResponse =>
  error(err === "read-only" ? 409 : 422, CONFIG_ERR[err], err);

export function postConfig(s: MockState, rawLength: number, body: unknown): MockResponse {
  const gate = bodyGate(rawLength);
  if (gate) return gate;
  if (!isObject(body)) return badBody();

  // The network does not travel on this route; refused BY NAME, because silently dropping a
  // credential somebody typed is worse than saying where it belongs.
  if ("wifi_ssid" in body) return error(422, WIRE.wrongRoute, "wifi_ssid");
  if ("wifi_psk" in body) return error(422, WIRE.wrongRoute, "wifi_psk");

  const patch: Partial<Pick<MockConfig, StringKey>> = {};
  for (const key of Object.keys(CAPS) as StringKey[]) {
    const v = readString(body, key, CAPS[key]);
    if (v === false) return badField(key);
    if (v !== undefined) patch[key] = v;
  }
  let port: number | undefined;
  if ("mqtt_port" in body) {
    if (!isU32(body.mqtt_port)) return badField("mqtt_port");
    port = body.mqtt_port;
  }
  let discovery: boolean | undefined;
  if ("ha_discovery" in body) {
    if (typeof body.ha_discovery !== "boolean") return badField("ha_discovery");
    discovery = body.ha_discovery;
  }
  let manual: boolean | undefined;
  if ("permanent_manual" in body) {
    if (typeof body.permanent_manual !== "boolean") return badField("permanent_manual");
    manual = body.permanent_manual;
  }
  // Everything else is ignored -- including the derived ui_password_set/read_only/wifi_known_good
  // the page echoes back, and any key a newer page knows about.

  // comfoair_config_apply(): read-only first, then every check BEFORE anything is written, so a
  // document with a good host and a bad port changes neither.
  if (s.scenario.configReadOnly) return refused("read-only");
  const c = s.config;
  if (patch.mqtt_host !== undefined && !HOST_CHARS.test(patch.mqtt_host)) return refused("mqtt-host");
  if (port !== undefined && (port === 0 || port > 65535)) return refused("mqtt-port");
  const broker = decideSecret(c.mqtt_password !== "", patch.mqtt_password);
  if (broker === "reject") return refused("mqtt-password");
  if (patch.topic_prefix !== undefined && !checkPrefix(patch.topic_prefix)) return refused("topic-prefix");
  if (patch.device_name !== undefined && !checkName(patch.device_name)) return refused("device-name");
  const ui = decideSecret(c.ui_password !== "", patch.ui_password);
  if (ui === "reject") return refused("ui-password");
  if (ui === "store" && !checkUiPassword(patch.ui_password ?? "")) return refused("ui-password");

  if (patch.mqtt_host !== undefined) c.mqtt_host = patch.mqtt_host;
  if (port !== undefined) c.mqtt_port = port;
  if (patch.mqtt_user !== undefined) c.mqtt_user = patch.mqtt_user;
  if (patch.topic_prefix !== undefined) c.topic_prefix = patch.topic_prefix;
  if (discovery !== undefined) c.ha_discovery = discovery;
  if (patch.device_name !== undefined) c.device_name = patch.device_name;
  if (broker === "store") c.mqtt_password = patch.mqtt_password ?? "";
  if (broker === "clear") c.mqtt_password = "";
  // Storing a UI password is what locks the API: the next request without it answers 401.
  if (ui === "store") c.ui_password = patch.ui_password ?? "";
  if (ui === "clear") c.ui_password = "";
  if (manual !== undefined) c.permanent_manual = manual;
  logLine(s, "http: settings saved");
  return { status: 200, body: { saved: true } };
}
