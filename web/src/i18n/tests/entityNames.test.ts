// The generated entity-name overlay covers exactly the registry, in every language.
//
// The generator already refuses to emit a mismatched table (tools/render_entity_names_ts.py),
// so this is the web-side echo of that guard: it catches a hand edit of the generated file, and
// pins that its English column is the same string entities.ts carries -- the two are rendered
// from one source and must not drift. Also pins entityName()'s fallback.
//
// Run: node src/i18n/tests/entityNames.test.ts

import { ENTITIES } from "../../api/entities.ts";
import { ENTITY_NAMES } from "../entityNames.generated.ts";
import { entityName } from "../entityName.ts";
import { setLocale } from "../index.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

const registryKeys = ENTITIES.map((e) => e.key as string).sort();
eq(Object.keys(ENTITY_NAMES).sort(), registryKeys, "the overlay covers exactly the registry keys");

for (const e of ENTITIES) {
  const row = ENTITY_NAMES[e.key];
  ok(row !== undefined, `${e.key} is in the overlay`);
  if (!row) continue;
  eq(row.en, e.name, `${e.key} en column equals entities.ts name`);
  for (const loc of ["en", "de", "nl", "uk"] as const) {
    ok(row[loc].trim().length > 0, `${e.key} has a non-empty ${loc} name`);
  }
}

setLocale("nl");
eq(entityName("fan", "Ventilation"), ENTITY_NAMES["fan"].nl, "a known key reads the current locale");
eq(entityName("no_such_key", "Fallback"), "Fallback", "an unknown key returns the fallback");
setLocale("en");
eq(entityName("fan", "x"), "Ventilation", "English is the registry name");

report("i18n/entityNames");
