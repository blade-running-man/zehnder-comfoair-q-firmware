// The reactive, side-effecting face of the i18n layer. It follows Nav.tsx's theme pattern: a
// signal seeded from localStorage at module load, a setter that persists and mirrors the
// choice onto <html lang>, and reader functions that touch the signal so any component calling
// them in render re-renders when the language changes.
//
// This is the ONE module of the layer that imports @preact/signals and touches the browser.
// Suites DO import it (through option.ts, or to call setLocale() and pin a wording), which only
// works because localStorage and document are guarded by `typeof`: under node there are no
// browser globals, so init falls back to English instead of throwing. DO NOT drop the guards
// as "always true in a browser" -- every suite that reaches this module would die at import.

import { signal } from "@preact/signals";
import { format, type Params } from "./format.ts";
import { catalogs, isLocale, type Locale } from "./catalogs/index.ts";
import { en, type MsgKey } from "./catalogs/en.ts";

export { LOCALES } from "./catalogs/index.ts";
export type { Locale } from "./catalogs/index.ts";
export type { MsgKey } from "./catalogs/en.ts";

// Namespaced by product: the SPA is served from the device's own origin, but a developer who
// runs both this and the thermostat SPA on localhost shares one origin between them.
const STORAGE_KEY = "comfoair.lang";

// No auto-detection (owner's choice): an unset or unknown stored value is English, never
// navigator.language. A half-translated page in a guessed language is worse than English.
function stored(): Locale {
  try {
    if (typeof localStorage === "undefined") return "en";
    const v = localStorage.getItem(STORAGE_KEY);
    return isLocale(v) ? v : "en";
  } catch {
    return "en";
  }
}

/** The current UI language. Read it (directly or through t/tp) inside render and the render
 *  re-subscribes on every switch. */
export const locale = signal<Locale>(stored());

// Mirror the initial choice onto <html lang> for the same reason Nav sets data-theme at load:
// assistive tech and the browser should agree with the UI from the first paint.
if (typeof document !== "undefined") {
  document.documentElement.lang = locale.value;
}

/** Switches the language, persists it, and updates <html lang>. Persistence is best-effort:
 *  a browser that refuses storage (private mode, quota) must not break the switch. */
export function setLocale(next: Locale): void {
  locale.value = next;
  try {
    if (typeof localStorage !== "undefined") localStorage.setItem(STORAGE_KEY, next);
  } catch {
    // ignore -- the in-memory signal is still switched.
  }
  if (typeof document !== "undefined") document.documentElement.lang = next;
}

/** Translates one key in the current language, with optional {token} substitution. Falls back
 *  to the English catalog for a key some catalog is (transiently) missing -- the type system
 *  forbids that at build time, so this only ever matters if a catalog is edited by hand into
 *  an inconsistent state between builds. */
export function t(key: MsgKey, params?: Params): string {
  const catalog = catalogs[locale.value];
  return format(catalog[key] ?? en[key], locale.value, params);
}

/** Translates a plural key against `count`, exposing it to the template as {count}. */
export function tp(key: MsgKey, count: number, params?: Params): string {
  const catalog = catalogs[locale.value];
  return format(catalog[key] ?? en[key], locale.value, params, count);
}
