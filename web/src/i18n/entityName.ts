// The localized display name of an entity. Reads the `locale` signal, so a component calling
// it in render re-renders on a language switch.
//
// The table is generated from pdo_table.py + tools/entity_names_i18n.py. English is the
// canonical registry name -- the same string entities.ts carries and Home Assistant shows -- so
// the fallback below never misses for a real key; it exists for a key the caller invents.
// DO NOT use the translated name as an identifier (a lookup key, an MQTT topic, an HA name):
// identifiers are `key`, and the English `name` is the one the HA integration resolves.

import { ENTITY_NAMES } from "./entityNames.generated.ts";
import { locale } from "./index.ts";

/** The entity's name in the current UI language. `fallback` is used only for an unknown key
 *  (pass the entity's English `name` from entities.ts, or the key itself). */
export function entityName(key: string, fallback: string): string {
  return ENTITY_NAMES[key]?.[locale.value] ?? fallback;
}
