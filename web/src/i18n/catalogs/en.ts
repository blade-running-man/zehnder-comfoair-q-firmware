// The English catalog -- the source of truth for the KEY SET, composed from the per-namespace
// files under en/. `MsgKey` is `keyof typeof en`. The spread of `satisfies`-typed namespace
// objects keeps the literal keys, so a key missing from another language is still a tsc error.
//
// A page owns one namespace (a new page adds its own here, in de/nl/uk.ts and in
// tests/catalogs.test.ts) and edits only its own en/de/nl/uk
// files. DO NOT add a page's strings to this file or to another page's namespace.

import type { Message } from "../format.ts";
import { common } from "./en/common.ts";
import { nav } from "./en/nav.ts";
import { tokens } from "./en/tokens.ts";
import { dashboard } from "./en/dashboard.ts";
import { settings } from "./en/settings.ts";
import { state } from "./en/state.ts";
import { update } from "./en/update.ts";
import { probe } from "./en/probe.ts";
import { system } from "./en/system.ts";
import { broker } from "./en/broker.ts";
import { log } from "./en/log.ts";

export const en = {
  ...common, ...nav, ...tokens,
  ...dashboard, ...settings, ...state, ...update, ...probe, ...system,
  ...broker, ...log,
} satisfies Record<string, Message>;

/** The key set every catalog must cover. */
export type MsgKey = keyof typeof en;
