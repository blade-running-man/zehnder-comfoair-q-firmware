// The pure core of the i18n layer: a message shape, {name} substitution, and plural
// resolution. NOTHING is imported here, and no browser global is touched, for the same
// reason the rest of web/src's tested modules keep to that rule (harness.ts): the suites
// run under node's type stripping, which has no DOM and no localStorage. The signal and the
// localStorage/document side effects live in i18n/index.ts, behind `typeof` guards.

/** The CLDR plural categories, as Intl.PluralRules.prototype.select returns them. Spelled
 *  out rather than taken from Intl.LDMLPluralRule so the layer does not depend on which TS
 *  lib the tsconfig happens to pull in. */
export type PluralRule = "zero" | "one" | "two" | "few" | "many" | "other";

/** One catalog entry. A plain string, or -- when the wording depends on a count -- an object
 *  keyed by plural category. A language supplies only the categories its own rules require;
 *  `other` is the mandatory fallback (see catalogs.test.ts, which pins exactly that). */
export type Message = string | Partial<Record<PluralRule, string>>;

/** Interpolation values. Numbers are stringified as-is: number formatting in this project is
 *  deliberately locale-neutral (`.toFixed`, a `.` decimal), so the catalog must not become a
 *  second place that reformats them. */
export type Params = Record<string, string | number>;

/** Replaces every `{token}` for which `params` has a key; an unknown token is left verbatim
 *  so a typo shows up in the UI as `{typo}` rather than vanishing. */
export function substitute(template: string, params?: Params): string {
  if (!params) return template;
  return template.replace(/\{(\w+)\}/g, (whole, key) =>
    key in params ? String(params[key]) : whole
  );
}

/**
 * Resolves one message to its final string.
 *
 * A string message is substituted straight. A plural message picks its form by the count's
 * category under `locale` -- `new Intl.PluralRules(locale).select(count)` -- falling back to
 * `other`, which catalogs.test.ts guarantees exists. `count` is exposed to the template as
 * `{count}` unless an explicit param overrides it.
 *
 * Intl.PluralRules is a standard global present in node, so this stays Node-safe and testable
 * without a browser.
 */
export function format(
  msg: Message,
  locale: string,
  params?: Params,
  count?: number
): string {
  if (typeof msg === "string") return substitute(msg, params);
  const category =
    count === undefined
      ? "other"
      : (new Intl.PluralRules(locale).select(count) as PluralRule);
  const form = msg[category] ?? msg.other ?? "";
  const merged: Params | undefined =
    count === undefined ? params : { count, ...params };
  return substitute(form, merged);
}
