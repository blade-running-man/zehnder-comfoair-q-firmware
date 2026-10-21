// web/mock/wire.ts
// Every sentence the firmware puts in an error body, copied BYTE FOR BYTE, and the helpers that
// build the bodies around them.
//
// Byte for byte because the SPA translates them by exact match (src/i18n/firmwareText.ts,
// DETAIL_TEMPLATES): a mock sentence that differs by a full stop is shown to the owner in
// English and the translation path goes untested. tests/wire.test.ts runs every sentence here
// that the SPA lists through those templates, so a drift on either side fails a suite.
//
// Sources: comfoair_wire_strerror() and comfoair_config_strerror() (components/comfoair_wire,
// components/comfoair_config), comfoair_command_strerror() (components/comfoair_command), and the
// literals in components/comfoair_http/.
import type { MockResponse } from "./model.ts";

export const WIRE = {
  badBody: "The request body is not a JSON object this device can read.",
  badField: "A field is missing, of the wrong type, or longer than this device can store.",
  wrongRoute: "The network is set through POST /api/provision, not here.",
} as const;

/** comfoair_config_strerror(), keyed by the name comfoair_http_config.c's config_error_name()
    sends as `field` -- POST /api/config names the RULE that refused, not the key. */
export const CONFIG_ERR = {
  ssid: "the network name must be 1 to 32 characters",
  psk: "the network password must be 8 to 63 characters, or exactly 64 hexadecimal digits, or empty for an open network",
  "wifi-pair": "the network name and its password are stored as one record: send both, and re-enter the password whenever the name changes",
  "mqtt-host": "the broker address must be a host name or an IP address, with no scheme and no port",
  "mqtt-port": "the broker port must be between 1 and 65535",
  "mqtt-user": "the broker user name is longer than this device can store",
  "mqtt-password": "the broker password is longer than this device can store",
  "topic-prefix": "the topic prefix must be 1 to 64 characters, without spaces, + or #, and without an empty level",
  "device-name": "the device name must be 1 to 32 characters and contain no control characters",
  "ui-password": "the web password must be 8 to 128 characters and contain no control characters",
  "no-hash": "this device could not secure the password just now; nothing was changed",
  "read-only": "these settings were written by a newer firmware and cannot be changed until the device is reset",
  "no-document": "the request carried no settings, so nothing was saved",
} as const;
export type ConfigErr = keyof typeof CONFIG_ERR;

export const COMMAND = {
  unknownEntity: "No such writable entity.",
  unknownOp: "No such operation.",
  badValue: "That value is not one this entity accepts.",
  missingParam: "This operation has no optional parameters; one is missing.",
} as const;

export const HTTP = {
  unauthorized: "unauthorized",
  forbidden: "forbidden",
  notImplemented: "not implemented yet",
  noEndpoint: "no such endpoint",
  unknownEntity: "unknown entity",
  bodyTooLarge: "body too large",
  scanUnavailable: "scan unavailable",
  busy: "busy",
  busBusy: "the bus is busy",
  commandsDown: "commands are not running",
  upgradeRequired: "websocket upgrade required",
} as const;

/** comfoair_http_probe.c's sentences, byte for byte. */
export const PROBE = {
  notObject: "not a JSON object",
  badNode: "node must be a whole number",
  badCounter: "another_counter must be a whole number",
  outOfRange: "node must be 1..63 and not this device; another_counter must be 0..3",
  noBus: "the probe is not running on this device (no CAN bus)",
  running: "a probe is already running",
} as const;

/** comfoair_ota_message() (components/comfoair_ota/comfoair_ota.c), byte for byte -- the C
    literals that are split across two lines are joined here as the compiler joins them. */
export const OTA = {
  noLength: "The upload did not say how large the file is; send it with a Content-Length.",
  tooSmall: "The file is too small to be a firmware image.",
  tooLarge: "The file is larger than the update slot.",
  notImage: "This is not a firmware image; pick firmware.bin from .pio/build/<env>/, not the .elf or a compressed file.",
  wrongChip: "This firmware was built for a different chip.",
  truncated: "The upload stopped before the whole file arrived; try again.",
  verifyFailed: "The uploaded image failed verification and was not installed.",
  busy: "Another update is already being uploaded.",
  flashError: "Writing the update to flash failed; the running firmware is unchanged.",
  noSlot: "This device has no partition to install an update into.",
  unconfirmed: "The running firmware was just updated and is still being verified; try again in a minute.",
  wrongProject: "This file is firmware for a different device, not the ComfoAir Q controller.",
} as const;

/** read_body()'s bound: a body of BODY_MAX (2048) bytes or more is refused, and so is NO body --
    content_len <= 0 answers 413 too, which is the firmware's behaviour and not a typo here. */
export const BODY_MAX = 2048;

export function error(status: number, message: string, field?: string): MockResponse {
  return { status, body: field === undefined ? { error: message } : { error: message, field } };
}

/** comfoair_http_send_wire_error() for a parse failure: 400 for an unreadable body, 422 for a
 *  field. */
export const badBody = (): MockResponse => error(400, WIRE.badBody);
export const badField = (field: string): MockResponse => error(422, WIRE.badField, field);

/** read_body(): answers 413, or null when the body may be read. */
export function bodyGate(rawLength: number): MockResponse | null {
  return rawLength <= 0 || rawLength >= BODY_MAX ? error(413, HTTP.bodyTooLarge) : null;
}

/** comfoair_json_check(): the body has to be a JSON object. Arrays and scalars are not. */
export function isObject(body: unknown): body is Record<string, unknown> {
  return typeof body === "object" && body !== null && !Array.isArray(body);
}

/** UTF-8 length, which is what the firmware's fixed buffers measure -- not String.length. */
export function bytes(s: string): number {
  return new TextEncoder().encode(s).length;
}

/** comfoair_json_string() into a buffer of `cap` bytes: undefined when the key is absent, false
    when it is present and unusable (not a string, or too long to fit with its NUL -- refused,
    never truncated), else the string. */
export function readString(body: Record<string, unknown>, key: string, cap: number): string | undefined | false {
  if (!(key in body)) return undefined;
  const v = body[key];
  if (typeof v !== "string" || bytes(v) > cap - 1) return false;
  return v;
}

/** comfoair_json_u32(): a non-negative integer that fits 32 bits. Fractions and negatives fail. */
export function isU32(v: unknown): v is number {
  return typeof v === "number" && Number.isInteger(v) && v >= 0 && v <= 0xffffffff;
}

/** comfoair_json_i32(): a signed integer; a fraction is a typo, not a rounding. */
export function isI32(v: unknown): v is number {
  return typeof v === "number" && Number.isInteger(v) && v >= -0x80000000 && v <= 0x7fffffff;
}
