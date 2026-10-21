// web/mock/tests/wire.test.ts
// The firmware sentences the mock sends are the ones the SPA translates. Every sentence in
// wire.ts that has a counterpart in src/i18n/firmwareText.ts's DETAIL_TEMPLATES must MATCH one
// of them -- otherwise the mock shows the owner untranslated English the real device would not,
// and the translation path the mock exists to exercise is not exercised.
//
//   node mock/tests/wire.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { DETAIL_TEMPLATES, matchTemplate } from "../../src/i18n/firmwareText.ts";
import { CONFIG_ERR, HTTP, OTA, WIRE, bodyGate, bytes, isI32, isU32, readString } from "../wire.ts";

const translated = (sentence: string): string | null => matchTemplate(sentence, DETAIL_TEMPLATES)?.key ?? null;

for (const [name, sentence] of Object.entries(WIRE)) ok(translated(sentence) !== null, `wire sentence ${name} is one the SPA translates`);
for (const [name, sentence] of Object.entries(CONFIG_ERR)) ok(translated(sentence) !== null, `config sentence ${name} is one the SPA translates`);
for (const [name, sentence] of Object.entries(OTA)) ok(translated(sentence) !== null, `ota sentence ${name} is one the SPA translates`);
eq(translated(OTA.notImage), "update.detail.notImage", "and lands on the Update page's own key");
const httpListed = ["unauthorized", "forbidden", "notImplemented", "noEndpoint", "bodyTooLarge", "scanUnavailable", "busy"] as const;
for (const name of httpListed) ok(translated(HTTP[name]) !== null, `http literal ${name} is one the SPA translates`);
eq(translated(CONFIG_ERR["mqtt-port"]), "settings.detail.port", "and maps to the catalog entry for that rule");

// The helpers' edges, which are the firmware's buffer rules.
eq(bodyGate(0)?.status, 413, "no body: 413");
eq(bodyGate(2047), null, "2047 bytes fit");
eq(bodyGate(2048)?.status, 413, "2048 do not");
eq(bytes("äö"), 4, "lengths are UTF-8 bytes");
eq(readString({ a: "x".repeat(32) }, "a", 33), "x".repeat(32), "32 bytes fit a 33-byte buffer");
eq(readString({ a: "x".repeat(33) }, "a", 33), false, "33 do not");
eq(readString({ a: 1 }, "a", 33), false, "a number is not a string");
eq(readString({}, "a", 33), undefined, "absent is undefined, not empty");
eq(readString({ a: "bad" }, "a", 33), "bad", "the literal string \"bad\" is just a string");
eq([isU32(5), isU32(-1), isU32(1.5), isU32(true)], [true, false, false, false], "u32: whole and non-negative");
eq([isI32(-5), isI32(2.5)], [true, false], "i32: signed, whole");

report("mock/wire");
