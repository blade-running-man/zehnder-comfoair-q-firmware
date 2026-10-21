// The web-side echo of "one entity list, ever": every option string of every entity in the
// generated api/entities.ts must have a token.opt.* catalog key, and every token.opt.* key must
// still be an option some entity has -- so the firmware's vocabulary and the catalog's
// translations cannot drift apart in either direction. Also pins optionLabel()'s contract: the
// display label is translated, an unknown string comes back as itself.
//
// Run: node src/i18n/tests/tokens.test.ts

import { ENTITIES } from "../../api/entities.ts";
import { en } from "../catalogs/en.ts";
import { tokens } from "../catalogs/en/tokens.ts";
import { optionChoices, optionKey, optionLabel } from "../option.ts";
import { setLocale } from "../index.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

const deviceOptions = new Set<string>();
for (const e of ENTITIES) {
  for (const o of e.options ?? []) {
    deviceOptions.add(o);
    ok(`token.opt.${o}` in en, `${e.key}'s option "${o}" has a token.opt key`);
  }
}
ok(deviceOptions.size > 0, "the registry has options at all (the walk is not vacuous)");

for (const key of Object.keys(tokens)) {
  ok(key.startsWith("token.opt."), `${key} is a token.opt key`);
  ok(deviceOptions.has(key.slice("token.opt.".length)), `${key} is still an option of some entity`);
}

// English: the label IS the device's spelling.
setLocale("en");
for (const o of deviceOptions) eq(optionLabel(o), o, `en label of "${o}" is the option itself`);

// Another language translates the label and leaves the value alone.
setLocale("de");
eq(optionLabel("Supply Only"), "Nur Zuluft", "de translates a known option");
eq(optionLabel("No Such Mode"), "No Such Mode", "an unknown option falls back to the raw value");
eq(optionKey("No Such Mode"), null, "an unknown option has no key");
eq(
  optionChoices(["Off", "Auto only"]),
  [{ value: "Off", label: "Aus" }, { value: "Auto only", label: "Nur Auto" }],
  "choices keep the device's string as the value and translate only the label"
);
setLocale("en");

report("i18n/tokens");
