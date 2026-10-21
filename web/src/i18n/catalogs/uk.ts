// Ukrainian. `Record<MsgKey, Message>` forces every key of en.ts to be present (each namespace file
// is also typed against its English twin, so the error lands in the file to fix); the parity
// test (tests/catalogs.test.ts) additionally forbids empty values, requires the same
// {placeholders} as English, and requires every plural category Ukrainian's rules use.

import type { Message } from "../format.ts";
import type { MsgKey } from "./en.ts";
import { common } from "./uk/common.ts";
import { nav } from "./uk/nav.ts";
import { tokens } from "./uk/tokens.ts";
import { dashboard } from "./uk/dashboard.ts";
import { settings } from "./uk/settings.ts";
import { state } from "./uk/state.ts";
import { update } from "./uk/update.ts";
import { probe } from "./uk/probe.ts";
import { system } from "./uk/system.ts";
import { broker } from "./uk/broker.ts";
import { log } from "./uk/log.ts";

export const uk: Record<MsgKey, Message> = {
  ...common, ...nav, ...tokens,
  ...dashboard, ...settings, ...state, ...update, ...probe, ...system,
  ...broker, ...log,
};
