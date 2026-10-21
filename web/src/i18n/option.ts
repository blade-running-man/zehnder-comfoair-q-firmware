// The display label of a device option string ("Auto", "Supply Only", "Auto only", ...), in
// the current UI language. The vocabulary is catalogs/<lang>/tokens.ts, keyed by the option
// exactly as the device spells it.
//
// DO NOT send a label to the device. The value a select or a preset button WRITES is always the
// raw option string from api/entities.ts -- the firmware matches it byte for byte, and a
// translated "Abwesend" is not an option it knows. Only what is SHOWN goes through here; use
// optionChoices() to get both halves for a <Select> without having to remember which is which.
//
// Reads the locale signal through t(), so a component calling it in render re-renders on a
// language switch.

import { en } from "./catalogs/en.ts";
import { t, type MsgKey } from "./index.ts";

/** The catalog key of `value`, or null for a string the vocabulary does not have. */
export function optionKey(value: string): MsgKey | null {
  const key = `token.opt.${value}`;
  return key in en ? (key as MsgKey) : null;
}

/** The translated label of `value`; the raw value itself for a string the vocabulary does not
 *  have (a newer firmware's option shows up in English rather than as a blank). */
export function optionLabel(value: string): string {
  const key = optionKey(value);
  return key ? t(key) : value;
}

/** `options` as <Select> choices: `value` is the device's own string (what gets written),
 *  `label` its translation (what is shown). */
export function optionChoices(
  options: readonly string[],
): Array<{ value: string; label: string }> {
  return options.map((value) => ({ value, label: optionLabel(value) }));
}
