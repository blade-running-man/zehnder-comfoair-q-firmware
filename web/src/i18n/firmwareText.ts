// The ONE place firmware English is best-effort translated: the error sentences the device puts
// in an error response's `error` field (api/client.ts carries it as ApiError's detail). The
// firmware sends prose, not a machine code, so a sentence that matches no entry is passed
// through in English. NOTHING here is a contract: a firmware sentence that changes silently
// reverts that one message to English until its pattern is updated -- which is the honest
// floor, not a bug to paper over.
//
// Node-safe (no @preact/signals, no DOM, no import of i18n/index.ts) so tests/firmwareText.test.ts
// runs it directly; t() is applied by the CALLER (i18n/detail.ts), which keeps this module pure
// like format.ts. Relative imports carry .ts (node's own type stripping needs the literal
// specifier), and the catalog import is type-only, so nothing reaches node at all.

import type { MsgKey } from "./catalogs/en.ts";

export interface Template {
  pattern: RegExp;
  key: MsgKey;
}
export interface Match {
  key: MsgKey;
  /** Captured groups, 1-indexed as strings ("1", "2", ...), bound to {1}/{2}/... in the catalog. */
  params: Record<string, string>;
}

/** First matching template, with capture groups bound to {1},{2},... params; null on no match. */
export function matchTemplate(text: string, registry: readonly Template[]): Match | null {
  for (const t of registry) {
    const m = t.pattern.exec(text);
    if (!m) continue;
    const params: Record<string, string> = {};
    for (let i = 1; i < m.length; i++) params[String(i)] = m[i] ?? "";
    return { key: t.key, params };
  }
  return null;
}

// The firmware's error detail sentences, one entry per sentence, each keyed to a
// `settings.detail.*` catalog entry (catalogs/<lang>/settings.ts) -- or, for POST /api/ota, an
// `update.detail.*` one (catalogs/<lang>/update.ts). Fixed sentences match exactly;
// an interpolated one captures its variable parts, which the catalog places as {1}, {2}, ...
// Patterns are anchored (^...$; tests/firmwareText.test.ts enforces it) so a longer, unrelated
// sentence cannot partially match and be shown as something the device did not say. A capture
// is passed through verbatim -- it is device data (a key, a number), not prose to translate.
//
// Enumerated from the firmware's own sources -- every sentence that can reach the Settings
// page's routes (/api/config, /api/provision, /api/wifi/scan, and the policy gate in front of them).
// None of them interpolates anything: the firmware keeps these literal on purpose, so nothing a
// client submitted can end up inside one (comfoair_http.c, send_error). Each English catalog
// value is the sentence byte for byte, which firmwareText.test.ts checks.
export const DETAIL_TEMPLATES: readonly Template[] = [
  // comfoair_config_strerror() (components/comfoair_config/comfoair_config_check.c). "accepted" is
  // the OK code's word and never travels in an error body, so it has no entry.
  { pattern: /^the network name must be 1 to 32 characters$/, key: "settings.detail.ssid" },
  {
    pattern: /^the network password must be 8 to 63 characters, or exactly 64 hexadecimal digits, or empty for an open network$/,
    key: "settings.detail.psk",
  },
  {
    pattern: /^the network name and its password are stored as one record: send both, and re-enter the password whenever the name changes$/,
    key: "settings.detail.wifiPair",
  },
  {
    pattern: /^the broker address must be a host name or an IP address, with no scheme and no port$/,
    key: "settings.detail.host",
  },
  { pattern: /^the broker port must be between 1 and 65535$/, key: "settings.detail.port" },
  {
    pattern: /^the broker user name is longer than this device can store$/,
    key: "settings.detail.user",
  },
  {
    pattern: /^the broker password is longer than this device can store$/,
    key: "settings.detail.brokerPassword",
  },
  {
    pattern: /^the topic prefix must be 1 to 64 characters, without spaces, \+ or #, and without an empty level$/,
    key: "settings.detail.prefix",
  },
  {
    pattern: /^the device name must be 1 to 32 characters and contain no control characters$/,
    key: "settings.detail.name",
  },
  {
    pattern: /^the web password must be 8 to 128 characters and contain no control characters$/,
    key: "settings.detail.uiPassword",
  },
  {
    pattern: /^this device could not secure the password just now; nothing was changed$/,
    key: "settings.detail.noHash",
  },
  {
    pattern: /^these settings were written by a newer firmware and cannot be changed until the device is reset$/,
    key: "settings.detail.readOnly",
  },
  {
    pattern: /^the request carried no settings, so nothing was saved$/,
    key: "settings.detail.noDocument",
  },
  { pattern: /^the settings were refused$/, key: "settings.detail.refused" },

  // comfoair_wire_strerror() (components/comfoair_wire/comfoair_wire.c). "Accepted." is the OK
  // status's word, as above; the REFUSED and READ_ONLY cases return the config sentences.
  {
    pattern: /^The request body is not a JSON object this device can read\.$/,
    key: "settings.detail.badBody",
  },
  {
    pattern: /^A field is missing, of the wrong type, or longer than this device can store\.$/,
    key: "settings.detail.badField",
  },
  {
    pattern: /^The network is set through POST \/api\/provision, not here\.$/,
    key: "settings.detail.wrongRoute",
  },
  { pattern: /^The request could not be applied\.$/, key: "settings.detail.notApplied" },

  // Literal error bodies in components/comfoair_http/: the access gate (allowed(), _gate.c),
  // comfoair_http_write_gate and asset_get's /api/ 404 (_assets.c), read_body (comfoair_http.c),
  // and the scan, provision and config handlers (_config.c).
  { pattern: /^unauthorized$/, key: "settings.detail.unauthorized" },
  { pattern: /^forbidden$/, key: "settings.detail.forbidden" },
  { pattern: /^not implemented yet$/, key: "settings.detail.notImplemented" },
  { pattern: /^no such endpoint$/, key: "settings.detail.noEndpoint" },
  { pattern: /^body too large$/, key: "settings.detail.bodyTooLarge" },
  { pattern: /^truncated body$/, key: "settings.detail.truncatedBody" },
  { pattern: /^scan unavailable$/, key: "settings.detail.scanUnavailable" },
  { pattern: /^scan does not fit$/, key: "settings.detail.scanTooLarge" },
  { pattern: /^busy$/, key: "settings.detail.busy" },
  { pattern: /^status does not fit$/, key: "settings.detail.statusTooLarge" },
  { pattern: /^config does not fit$/, key: "settings.detail.configTooLarge" },
  // comfoair_ota_message() (components/comfoair_ota/comfoair_ota.c): every refusal of POST
  // /api/ota. The Update page's own pre-flight shows the same keys, so a file it refuses locally
  // reads exactly as the device's refusal would.
  {
    pattern: /^The upload did not say how large the file is; send it with a Content-Length\.$/,
    key: "update.detail.noLength",
  },
  { pattern: /^The file is too small to be a firmware image\.$/, key: "update.detail.tooSmall" },
  { pattern: /^The file is larger than the update slot\.$/, key: "update.detail.tooLarge" },
  {
    pattern: /^This is not a firmware image; pick firmware\.bin from \.pio\/build\/<env>\/, not the \.elf or a compressed file\.$/,
    key: "update.detail.notImage",
  },
  { pattern: /^This firmware was built for a different chip\.$/, key: "update.detail.wrongChip" },
  {
    pattern: /^The upload stopped before the whole file arrived; try again\.$/,
    key: "update.detail.truncated",
  },
  {
    pattern: /^The uploaded image failed verification and was not installed\.$/,
    key: "update.detail.verifyFailed",
  },
  { pattern: /^Another update is already being uploaded\.$/, key: "update.detail.busy" },
  {
    pattern: /^Writing the update to flash failed; the running firmware is unchanged\.$/,
    key: "update.detail.flashError",
  },
  {
    pattern: /^This device has no partition to install an update into\.$/,
    key: "update.detail.noSlot",
  },
  {
    pattern: /^The running firmware was just updated and is still being verified; try again in a minute\.$/,
    key: "update.detail.unconfirmed",
  },
  {
    pattern: /^This file is firmware for a different device, not the ComfoAir Q controller\.$/,
    key: "update.detail.wrongProject",
  },

  // Not firmware: the SPA's own sentence (api/client.ts request()) for a 2xx answer whose body
  // could not be parsed. It reaches the page as an ApiError detail like the device's sentences,
  // so it is translated here rather than by pulling t() into client.ts, which node suites import.
  // DO NOT reword one side without the other -- the pattern must match client.ts byte for byte.
  { pattern: /^The device answered (\d+) with an empty body\.$/, key: "settings.detail.emptyBody" },
  { pattern: /^The device answered (\d+) with a body this page could not read\.$/, key: "settings.detail.unreadableBody" },
];
