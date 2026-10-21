// firmwareText.ts is the pure best-effort registry of firmware error sentences (used through
// detail.ts). Its template list starts empty; these checks pin matchTemplate()'s behaviour on a
// local registry, and pin the rules every real entry must keep once one is added.
//
// Run: node src/i18n/tests/firmwareText.test.ts

import { matchTemplate, DETAIL_TEMPLATES, type Template } from "../firmwareText.ts";
import { en } from "../catalogs/en.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

// Keys only need to be real MsgKeys for the type; the strings they name are irrelevant here.
const REGISTRY: readonly Template[] = [
  { pattern: /^no such entity$/, key: "common.error" },
  { pattern: /^value (\S+) out of range (\d+)\.\.(\d+)$/, key: "common.unknown" },
  { pattern: /^no such entity$/, key: "common.retry" },
];

const fixed = matchTemplate("no such entity", REGISTRY);
ok(fixed !== null, "a fixed sentence matches");
if (fixed) {
  eq(fixed.key, "common.error", "the FIRST matching template wins");
  eq(fixed.params, {}, "a pattern without groups binds no params");
}

const bound = matchTemplate("value 12.5 out of range 1..10", REGISTRY);
ok(bound !== null, "an interpolated sentence matches");
if (bound) {
  eq(bound.key, "common.unknown", "matched the interpolated template");
  eq(bound.params, { "1": "12.5", "2": "1", "3": "10" }, "captures bind to 1-indexed params");
}

eq(matchTemplate("no such entity, really", REGISTRY), null, "anchoring rejects a longer sentence");
eq(matchTemplate("anything", []), null, "an empty registry matches nothing");
eq(matchTemplate("Wi-Fi scan failed", DETAIL_TEMPLATES), null, "an unknown sentence returns null");

// Rules for the real registry (vacuous while it is empty, binding once it is filled).
for (const tpl of DETAIL_TEMPLATES) {
  ok(tpl.pattern.source.startsWith("^") && tpl.pattern.source.endsWith("$"),
     `${tpl.pattern} is anchored at both ends`);
  ok(tpl.key in en, `${tpl.key} is a catalog key`);
  const groups = new RegExp(`${tpl.pattern.source}|`).exec("")!.length - 1;
  const msg = en[tpl.key];
  const forms = typeof msg === "string" ? [msg] : (Object.values(msg) as string[]);
  for (let i = 1; i <= groups; i++) {
    ok(forms.some((f) => f.includes(`{${i}}`)), `${tpl.key} places capture {${i}}`);
  }
}

// --- the settings.detail.* entries ---------------------------------------------------------
// Each English value must BE the sentence it translates: an English reader sees exactly what
// arrived, and errors.test.ts's verbatim passthrough keeps holding once the page routes details
// through translateDetail(). The firmware's sentences interpolate nothing; the SPA's own two
// (api/client.ts) carry the HTTP status as {1}, so a sample status is put back in before the
// match -- with no {n} in the value this is the plain string itself.
for (const tpl of DETAIL_TEMPLATES) {
  const msg = en[tpl.key];
  ok(typeof msg === "string", `${tpl.key} is a plain string`);
  if (typeof msg !== "string") continue;
  const own = matchTemplate(msg.replace(/\{\d+\}/g, "200"), DETAIL_TEMPLATES);
  eq(own?.key, tpl.key, `${tpl.key}'s English value is matched by its own template, first`);
}

// The SPA's own sentences, copied literally from api/client.ts request(), with the status the
// template captures.
eq(matchTemplate("The device answered 200 with an empty body.", DETAIL_TEMPLATES),
   { key: "settings.detail.emptyBody", params: { "1": "200" } }, "client.ts's empty-body sentence");
eq(matchTemplate("The device answered 502 with a body this page could not read.", DETAIL_TEMPLATES),
   { key: "settings.detail.unreadableBody", params: { "1": "502" } }, "client.ts's unreadable-body sentence");
eq(new Set(DETAIL_TEMPLATES.map((tpl) => tpl.key)).size, DETAIL_TEMPLATES.length,
   "one template per catalog key");

// A few sentences copied literally from the C sources, so a pattern edited into something that
// no longer matches the firmware fails here rather than silently reverting to English.
const FIRMWARE: ReadonlyArray<[string, string]> = [
  // comfoair_config_strerror()
  ["the broker port must be between 1 and 65535", "settings.detail.port"],
  ["the topic prefix must be 1 to 64 characters, without spaces, + or #, and without an "
   + "empty level", "settings.detail.prefix"],
  ["these settings were written by a newer firmware and cannot be changed until the device "
   + "is reset", "settings.detail.readOnly"],
  // comfoair_wire_strerror()
  ["The network is set through POST /api/provision, not here.", "settings.detail.wrongRoute"],
  // comfoair_http literals
  ["unauthorized", "settings.detail.unauthorized"],
  ["not implemented yet", "settings.detail.notImplemented"],
  ["scan unavailable", "settings.detail.scanUnavailable"],
  // comfoair_ota_message(), the two string halves of the notImage literal joined as C joins them
  ["This is not a firmware image; pick firmware.bin from .pio/build/<env>/, "
   + "not the .elf or a compressed file.", "update.detail.notImage"],
  ["The running firmware was just updated and is still being verified; "
   + "try again in a minute.", "update.detail.unconfirmed"],
  ["The file is larger than the update slot.", "update.detail.tooLarge"],
  ["This file is firmware for a different device, not the ComfoAir Q controller.",
   "update.detail.wrongProject"],
];
for (const [sentence, key] of FIRMWARE) {
  eq(matchTemplate(sentence, DETAIL_TEMPLATES)?.key, key, `firmware sentence -> ${key}`);
}
eq(matchTemplate("the broker port must be between 1 and 65535.", DETAIL_TEMPLATES), null,
   "a sentence with a trailing character the firmware does not send is not matched");
eq(matchTemplate("The network is set through POST /api/provisionX not here.", DETAIL_TEMPLATES),
   null, "regex metacharacters in a pattern are escaped, not wildcards");

report("i18n/firmwareText");
