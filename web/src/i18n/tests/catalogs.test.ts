// The catalog parity guard -- the checks tsc cannot do. The namespace typing already forces the
// key SET to match; this pins what a type does not: no empty values, the same {placeholders}
// in every language (a dropped interpolation is a silent runtime hole), the same
// string-vs-plural SHAPE per key, every plural category the language's own Intl.PluralRules
// requires plus `other`, and no key shared by two namespaces (en.ts spreads them into one
// object, so a shared key would silently drop one of the two strings).
//
// Run: node src/i18n/tests/catalogs.test.ts

import { catalogs } from "../catalogs/index.ts";
import { en } from "../catalogs/en.ts";
import { common } from "../catalogs/en/common.ts";
import { nav } from "../catalogs/en/nav.ts";
import { tokens as tokenNs } from "../catalogs/en/tokens.ts";
import { dashboard } from "../catalogs/en/dashboard.ts";
import { settings } from "../catalogs/en/settings.ts";
import { state } from "../catalogs/en/state.ts";
import { update } from "../catalogs/en/update.ts";
import { probe } from "../catalogs/en/probe.ts";
import { system } from "../catalogs/en/system.ts";
import { broker } from "../catalogs/en/broker.ts";
import { log } from "../catalogs/en/log.ts";
import type { Message } from "../format.ts";
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";

type Locale = keyof typeof catalogs;
const LOCALES = Object.keys(catalogs) as Locale[];
const KEYS = Object.keys(en) as Array<keyof typeof en>;

function forms(msg: Message): string[] {
  return typeof msg === "string" ? [msg] : (Object.values(msg) as string[]);
}

function placeholders(msg: Message): string[] {
  const set = new Set<string>();
  for (const f of forms(msg)) for (const m of f.matchAll(/\{(\w+)\}/g)) set.add(m[1]);
  return [...set].sort();
}

// --- namespaces are disjoint --------------------------------------------------------------
const NAMESPACES: Record<string, object> = {
  common, nav, tokens: tokenNs, dashboard, settings, state, update, probe, system,
  broker, log,
};
const owner = new Map<string, string>();
for (const [ns, obj] of Object.entries(NAMESPACES)) {
  for (const key of Object.keys(obj)) {
    ok(!owner.has(key), `"${key}" is in ${ns} only (also in ${owner.get(key) ?? "-"})`);
    owner.set(key, ns);
  }
}
eq(owner.size, KEYS.length, "en.ts is exactly the union of its namespaces");

// --- identical key sets -------------------------------------------------------------------
const enKeys = KEYS.map(String).sort();
for (const loc of LOCALES) {
  eq(Object.keys(catalogs[loc]).sort(), enKeys, `${loc} has exactly the English key set`);
}

// --- no empty values ----------------------------------------------------------------------
for (const loc of LOCALES) {
  for (const key of KEYS) {
    const msg = catalogs[loc][key];
    ok(
      msg !== undefined && forms(msg).every((f) => f.trim().length > 0),
      `${loc} "${String(key)}" has no empty form`
    );
  }
}

// --- same shape and same placeholders as English ------------------------------------------
for (const key of KEYS) {
  const enMsg: Message = en[key];
  const enIsPlural = typeof enMsg !== "string";
  const enTokens = placeholders(enMsg);
  for (const loc of LOCALES) {
    const msg = catalogs[loc][key];
    if (msg === undefined) continue; // already reported above
    ok(
      (typeof msg !== "string") === enIsPlural,
      `${loc} "${String(key)}" matches English's string-vs-plural shape`
    );
    eq(placeholders(msg), enTokens, `${loc} "${String(key)}" keeps English's placeholders`);
  }
}

// --- plural keys cover every category the locale requires ---------------------------------
for (const key of KEYS) {
  if (typeof en[key] === "string") continue;
  for (const loc of LOCALES) {
    const msg = catalogs[loc][key] as Exclude<Message, string>;
    const required = new Set<string>(new Intl.PluralRules(loc).resolvedOptions().pluralCategories);
    required.add("other");
    for (const cat of required) {
      ok(
        typeof msg[cat as keyof typeof msg] === "string",
        `${loc} "${String(key)}" supplies the "${cat}" plural form`
      );
    }
  }
}

report("i18n/catalogs");
