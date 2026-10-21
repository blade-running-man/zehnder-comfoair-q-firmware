// Everything this UI asks the device for.
//
// AGENTS.md criterion 4: the web UI has no privileged endpoint. Every call below is one any other
// client can make on the same terms -- there is no header, cookie or route here that curl
// cannot use. If a screen ever needs something this file cannot express, the API is wrong,
// not this file.

import type { EntityMeta } from "./entities";
import type { MqttStatus } from "./status";
import type { StateValue } from "./ws";

export class ApiError extends Error {
  // Written out rather than declared as a constructor parameter property: the project
  // compiles with erasableSyntaxOnly, which rules out the shorthand.
  readonly status: number;

  constructor(status: number, message: string) {
    super(message);
    this.status = status;
  }
}

export async function send(path: string, init?: RequestInit): Promise<Response> {
  const response = await fetch(path, init);
  if (response.ok) return response;

  // The device answers errors as JSON with an `error` field. A write to a path the access
  // policy allows but no handler serves answers 501 (comfoair_http_write_gate in
  // comfoair_http_assets.c) -- a request the firmware does not implement, not a fault of the
  // device.
  let detail = response.statusText;
  try {
    const body = await response.json();
    if (typeof body?.error === "string") detail = body.error;
  } catch {
    // Not JSON. Keep the status text; a parse failure here is not the interesting error.
  }
  throw new ApiError(response.status, detail);
}

/**
 * A call whose answer IS the result: the body carries the document the caller asked for.
 *
 * An unreadable body is therefore a fault, and it is reported as ONE -- as an ApiError with the
 * status the device actually gave. It used to reach the caller as the SyntaxError from
 * response.json(), which is not an ApiError, so pages/settings/errors.ts fell through to its
 * last case and told the owner "Nothing reached the device, so it has said nothing about
 * this". Something did reach the device: it answered. A page that cannot tell those two apart
 * sends the owner looking for a network fault that is not there.
 */
async function request<T>(path: string, init?: RequestInit): Promise<T> {
  const response = await send(path, init);
  const text = await response.text();
  try {
    return JSON.parse(text) as T;
  } catch {
    const what = text === "" ? "an empty body" : "a body this page could not read";
    throw new ApiError(response.status, `The device answered ${response.status} with ${what}.`);
  }
}

/**
 * A call whose answer is only "yes". The body is not read at all.
 *
 * A write that answered 200 and nothing else used to be reported as a device that had gone
 * missing, for no better reason than that request() ran every successful body through
 * JSON.parse. The status line is the whole answer here; there is nothing in the body this
 * caller would look at even if it arrived.
 */
export async function requestVoid(path: string, init?: RequestInit): Promise<void> {
  await send(path, init);
}

/** Every entity's current value, by key. A key with `null` exists and has no reading yet.
 *  device.ts's Seeder calls it after every socket open: a page reload keeps the device's socket
 *  count flat, so the socket's own full frame never arrives and only this fetch fills the page. */
export function getState(): Promise<Record<string, StateValue>> {
  return request("/api/state");
}

/** The device's own log, oldest line first. The only way to read it without a USB cable. */
export function getLog(): Promise<string[]> {
  return request("/api/log");
}

/**
 * What each entity IS -- name, unit, kind, range, options, whether it can be written.
 *
 * Fetched once and cached by the caller: it is sixteen kilobytes and changes only when the
 * firmware does. The shape is generated from the same table the device renders from, so
 * `entities.ts` is a type for this response, not a second copy of it.
 */
export function getEntities(): Promise<EntityMeta[]> {
  return request("/api/entities");
}

/**
 * Writes one entity.
 *
 * Over REST, not over the socket. The socket is push-only by design: a command arriving
 * there would be an endpoint the REST API does not have, and that is the privileged path
 * the project forbids (docs/interfaces/web-ui.md, "No privileged endpoint").
 */
export function writeEntity(key: string, value: StateValue): Promise<void> {
  return requestVoid(`/api/entities/${encodeURIComponent(key)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ value }),
  });
}

/**
 * Runs one of the six parameterised operations.
 *
 * Mode and duration travel in ONE call. These modes are temporary on the unit and PDOs
 * 81/86/87 are their countdowns, so the API (`docs/interfaces/rest-api.md`, POST /api/ops) makes
 * that atomicity part of the contract. DO NOT add a helper that sets a mode and its duration separately --
 * that split is exactly what the contract exists to prevent.
 */
export function runOperation(
  name: string,
  params: Record<string, unknown> = {},
): Promise<void> {
  return requestVoid(`/api/ops/${encodeURIComponent(name)}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(params),
  });
}

// --- configuration -----------------------------------------------------------------------
//
// Everything below is the configuration and provisioning routes (docs/interfaces/rest-api.md).
// A write the firmware has no handler for answers 501 (comfoair_http_write_gate) and an
// unregistered GET 404; neither is a fault of the device. pages/settings/errors.ts is where that
// gets said to the owner.
//
// THE TWO GETs MUST ANSWER WITH A JSON BODY. Their body is the answer, and a 200 carrying
// nothing is a fault this page can only report as one.
//
// The two writes may answer with an empty body; `{}` is still the better habit, because a
// field added to the answer later has somewhere to go. What they must NOT do is answer a
// status this file cannot tell from silence -- see request() and requestVoid() above for why
// that distinction is worth two functions.

/**
 * The document `GET /api/config` answers with.
 *
 * The keys are the `name` column of the field table in
 * components/comfoair_config/comfoair_config.c:31-50, which that file defines as "what the API
 * and the owner see". This interface is a transcription of that column, not a second list of
 * fields: a field added there and not here is simply not shown, and a name that disagrees is
 * a field that silently never saves.
 *
 * Every secret arrives redacted -- CONFIG_UNCHANGED when one is stored, "" when none is, never
 * the value (comfoair_secret_redact, comfoair_secrets.h:36-38).
 */
export interface DeviceConfig {
  wifi_ssid: string;
  wifi_psk: string;
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_password: string;
  topic_prefix: string;
  ha_discovery: boolean;
  device_name: string;
  ui_password: string;
  // The last three are DERIVED by the device and appear in the projection only
  // (comfoair_config_public_t, comfoair_config.h:244-262). They are read, never written back.
  // `ui_password_set` is what the page needs to warn about locking the device before the
  // password exists; `read_only` is true when the flash holds a schema this build does not
  // understand -- an OTA rolled back under a newer configuration -- and every write will be
  // refused until that is resolved.
  ui_password_set: boolean;
  read_only: boolean;
  /**
   * comfoair_prov_has_known_good() (comfoair_provision.h:299): a stored credential pair has
   * actually produced an address on the owner's network.
   *
   * SHAPE THE FIRMWARE HAS TO MATCH -- this key does not exist yet, and it is here because the
   * settings page cannot tell the truth without it. It is the ONLY condition under which the
   * device rolls a failed provisioning back (comfoair_provision.c:442-451), and the flag is set
   * only where an address arrives (comfoair_provision.c:304-311). `wifi_ssid !== ""` is not a
   * substitute: an SSID stored beside a mistyped key has never produced an address, the trial
   * that fails goes to RETRYING with nothing restored, and a page that promises a rollback
   * there tells the owner to wait for a return that is not coming.
   *
   * It comes from the provisioning state machine and not from the stored record, so it is a
   * projection field like the two above -- comfoair_config has no business holding it.
   */
  wifi_known_good: boolean;
  /**
   * Whether a level change also pins the unit into permanent manual mode.
   *
   * With it on, a preset chosen here or in Home Assistant sticks. With it off, the unit reverts to
   * AUTO within two hours or at the next scheduler step — and its three sensor-ventilation
   * controls stay active, which manual mode disables (comfoair_command.h:106-112).
   *
   * NOT a row of the field table above, unlike every other key in this interface: it is owned by
   * comfoair_control and rides alongside the projection, joined in by the wire layer
   * (comfoair_wire.h:161-166). Absent leaves it untouched on the way in (comfoair_wire.h:139-141).
   */
  permanent_manual: boolean;
}

/**
 * What `POST /api/config` carries, and the two omissions ARE the design.
 *
 * `wifi_ssid`/`wifi_psk` are absent because Wi-Fi goes through provisionWifi() alone. An
 * absent key means "leave the stored value alone" -- the patch struct spells this out
 * (comfoair_config.h:365-368: "A NULL string means the key was ABSENT... absent leaves the
 * stored value alone, empty clears it") -- so saving the broker cannot reach the network under
 * any circumstance, including a bug in this page.
 *
 * `ui_password_set`/`read_only`/`wifi_known_good` are absent because the device derives them.
 * Echoing back a value the device computed invites a firmware that stores it, and a stored "a
 * password is set" flag that disagrees with the stored record is a device locked by nobody,
 * for ever (comfoair_config.h:257-260).
 *
 * DO NOT make this a Partial<DeviceConfig> "so the page can send less". The page submits the
 * whole document every time; that is what the sentinel scheme is for, and a page that sends
 * only what it thinks changed is the design the sentinel replaced.
 */
export interface ConfigPatch {
  mqtt_host: string;
  mqtt_port: number;
  mqtt_user: string;
  mqtt_password: string;
  topic_prefix: string;
  ha_discovery: boolean;
  device_name: string;
  ui_password: string;
  permanent_manual: boolean;
}

export function getConfig(): Promise<DeviceConfig> {
  return request("/api/config");
}

export function saveConfig(patch: ConfigPatch): Promise<void> {
  return requestVoid("/api/config", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(patch),
  });
}

/**
 * The body of POST /api/provision, and the three states `wifi_psk` has.
 *
 * SHAPE THE FIRMWARE HAS TO MATCH:
 *
 *   {"wifi_ssid": "Kitchen", "wifi_psk": "hunter2"}   store this key
 *   {"wifi_ssid": "Guest",   "wifi_psk": ""}          an OPEN network: no key at all
 *   {"wifi_ssid": "Kitchen"}                          keep the key already stored
 *
 * The third line is the one to get right, and it is the same rule comfoair_config_patch_t
 * already states for every other string: "A NULL string means the key was ABSENT... absent
 * leaves the stored value alone, empty clears it" (comfoair_config.h:365-368). Run the field
 * through comfoair_secret_decide() -- absent is KEEP, "" is CLEAR, anything else is STORE --
 * and this route needs no vocabulary of its own.
 *
 * DO NOT accept COMFOAIR_SECRET_SENTINEL as a key here, and DO NOT store this field without
 * deciding first. "__UNCHANGED__" is 13 printable bytes and passes comfoair_config_check_psk
 * whole (>= PSK_MIN 8, no NUL, not length 64 so no hex check), so a handler that writes it
 * through leaves the device holding credentials that cannot associate -- and credentials
 * existing is what stops the access point from coming back up. That is the disappearance
 * comfoair_provision.c:443-446 calls the likeliest catastrophe in this project, reached with
 * nobody having made a typo. provisionRequest() in pages/settings/wifi.ts keeps the sentinel
 * off the wire from this side; this note is the other half, because a handler is not obliged
 * to trust its clients.
 *
 * An absent key with nothing stored is KEEP of nothing, which is an open-network attempt. That
 * is the correct reading and it needs no special case.
 */
export interface ProvisionRequest {
  wifi_ssid: string;
  /** Absent means "keep the stored key". "" means the network has none. */
  wifi_psk?: string;
}

/**
 * Wi-Fi credentials, and the only route that carries them.
 *
 * POST, never GET. A PSK in a query string is written to the log ring by the server, and
 * /api/log hands that ring to anybody who can reach it -- "provisioning by POST only" is the
 * minimum price of leaving the setup access point open (docs/architecture/security.md, "Who may
 * do what").
 *
 * It is also the ONLY write an unprovisioned device accepts (comfoair_http_policy.c:31-46), so
 * it is the route that has to work in the one situation that matters: a fresh device on its
 * own open access point. Routing the network through POST /api/config instead would give a
 * form that works on the bench and is refused in the kitchen.
 *
 * The keys are the config document's, deliberately: one vocabulary for the network on both
 * routes means the firmware decodes it in one place instead of two that drift.
 *
 * The device answers BEFORE it starts connecting (docs/architecture/failure-model.md; the AP
 * stays up for the length of the setup window so the answer can arrive --
 * comfoair_provision.c:553-558). A 200 here means ACCEPTED, never CONNECTED, and the caller
 * has to say so; the caller must also expect the request to fail with no answer at all, which
 * after this particular call is what success looks like from the browser's side.
 */
export function provisionWifi(creds: ProvisionRequest): Promise<void> {
  return requestVoid("/api/provision", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(creds),
  });
}

/** One network the device's radio heard. */
export interface ScanResult {
  /** The SSID as sent in the beacon. "" for a hidden network -- see scanNetworks(). */
  ssid: string;
  /** dBm, so negative and larger is better: -40 is the same room, -90 is barely there. */
  rssi: number;
  /** False ONLY for a genuinely open network (authmode WIFI_AUTH_OPEN). */
  secure: boolean;
}

/**
 * The networks the DEVICE can see, which is not the list the phone can see.
 *
 * That difference is the most common support request for any 2.4 GHz device and the reason
 * this list is an invariant rather than a feature: a router publishes one name on both bands,
 * the C6 has only a 2.4 GHz radio, and the owner reasonably concludes the device is broken
 * (docs/guide/first-setup.md).
 *
 * SHAPE THE FIRMWARE HAS TO MATCH -- written here first so the handler is built to it:
 *
 *   200 -> [{"ssid": "Kitchen", "rssi": -52, "secure": true}, ...]
 *
 *   * A JSON array at the top level, possibly empty. Not an object with a `networks` key: the
 *     handler renders one thing and this is it.
 *   * DUPLICATE SSIDs ARE EXPECTED AND MUST NOT BE FILTERED by the device. One scan sweep
 *     returns one record per BSSID, so a mesh answers several times under one name; which of
 *     them to show is a presentation question and mergeScan() in pages/settings/wifi.ts owns
 *     it. A device that deduplicates has to choose, and it has less information than the page.
 *   * A hidden network answers with an empty `ssid`. Send it as "" rather than dropping the
 *     record -- the page discards it, but "the scan saw seven things and can name five" is a
 *     true statement worth being able to make.
 *   * The call can take SECONDS. A full 2.4 GHz sweep is thirteen channels, and on a device
 *     serving this page from its own access point the radio leaves that channel while it
 *     sweeps -- the phone may lose the page mid-request. The page therefore treats a failed
 *     scan as "no list this time" and keeps manual entry available; the firmware must not let
 *     a failed or slow scan restart, reboot or drop anything (AGENTS.md: nothing reboots
 *     because a peer is absent, and here the absent peer is the browser).
 */
export function scanNetworks(): Promise<ScanResult[]> {
  return request("/api/wifi/scan");
}

/**
 * Why the device is or is not on the owner's network.
 *
 * `failure` is one of four words the firmware decides ONCE, in comfoair_provision, so that REST
 * and MQTT cannot invent two spellings of them: "none", "network-not-found", "wrong-password",
 * "refused", "no-address". Four different answers instead of one "it will not connect" is a phase
 * 5 invariant, not a nicety -- "the router is rebooting" and "you changed the password" need
 * different things done about them.
 *
 * The two millisecond fields are `null` for "no end", never 0: a window with 0 ms left is closed,
 * a window that never closes is the supported configuration for a unit sealed behind a front panel.
 */
export interface ProvisionStatus {
  state: string;
  mode: string;
  /**
   * What the WHOLE DEVICE is doing, which is a different question from `state` and often has a
   * different answer: a device can be `connected` to its network and `bus-silent` at the same
   * moment. comfoair_phase_name()'s word, never invented anywhere else, so a colour on the device
   * and a word in this document cannot come to disagree.
   */
  phase: string;
  provisioned: boolean;
  has_credentials: boolean;
  wifi_known_good: boolean;
  connected: boolean;
  wifi_ssid: string;
  ip: string;
  ap_ssid: string;
  ap_on_air: boolean;
  setup_window_open: boolean;
  setup_window_remaining_ms: number | null;
  fallback_ap_in_ms: number | null;
  /** How long the firmware has been running, from the same clock read the two durations above are
   *  measured against. Without it "the house is still coming up" reads exactly like "it broke an
   *  hour ago", and nothing else in this document separates them. */
  uptime_ms: number;
  failure: string;
  failure_reason: number;
  /** dBm, and null -- never 0 -- when the station is not associated: 0 dBm is a legal reading and
   *  would report a perfect signal on a device that is on no network at all. Read beside
   *  `failure_reason`: 201 with a strong signal is a different fault from 201 with none. */
  rssi: number | null;
  /**
   * Who answered: the twelve lower-case hex characters of the STA MAC, the colon-separated form
   * of it, and the running firmware's version.
   *
   * ALL THREE ABSENT TOGETHER, rather than empty, when the caller had nothing to say
   * (comfoair_wire.h on comfoair_wire_render_provision; the POST /api/provision reply carries them
   * and older documents may not). A consumer keys its record of this device on device_id, and an
   * empty one is not an identity -- which is also what lets the settings page narrow a by-hand
   * cleanup command to this device instead of the whole `homeassistant/#` tree.
   */
  device_id?: string;
  mac?: string;
  sw_version?: string;
  /**
   * The broker's counters.
   *
   * OPTIONAL BECAUSE THE FIRMWARE OMITS THE WHOLE MEMBER rather than rendering zeros when nobody
   * was in a position to look -- POST /api/provision's reply is answered before the radio has even
   * moved, so comfoair_http_config.c passes NULL for it there. A block of zeros and no block at all
   * are opposite answers, so `mqtt === undefined` must never be read as "not connected":
   * pages/settings/broker.ts calls that third state `unknown` and treats it as the dangerous one.
   */
  mqtt?: MqttStatus;
}

export function getStatus(): Promise<ProvisionStatus> {
  return request("/api/status");
}
