// The locale union and the locale -> catalog map. Kept separate from i18n/index.ts so a test
// can import the catalogs (plain data) without pulling in @preact/signals or the browser side
// effects that live there.

import type { Message } from "../format.ts";
import { en, type MsgKey } from "./en.ts";
import { de } from "./de.ts";
import { nl } from "./nl.ts";
import { uk } from "./uk.ts";

/** The supported locales. `en` is the default and the fallback. */
export type Locale = "en" | "de" | "nl" | "uk";

/** Every locale carries the full key set (each catalog is Record<MsgKey, Message>). */
export const catalogs: Record<Locale, Record<MsgKey, Message>> = { en, de, nl, uk };

/** The order the switcher shows. `name` is the autonym (a language name is conventionally shown
 *  in that language), kept for accessibility and tooltips; `short` is the compact tag the nav
 *  switcher renders -- UA for Ukrainian, the country tag people expect, not the ISO code "uk".
 *  DO NOT use `short` as a locale code: `code` is what the catalogs and <html lang> take. */
export const LOCALES: ReadonlyArray<{ code: Locale; name: string; short: string }> = [
  { code: "en", name: "English", short: "EN" },
  { code: "de", name: "Deutsch", short: "DE" },
  { code: "nl", name: "Nederlands", short: "NL" },
  { code: "uk", name: "Українська", short: "UA" },
];

export function isLocale(v: unknown): v is Locale {
  return v === "en" || v === "de" || v === "nl" || v === "uk";
}
