// Dutch. `Record<MsgKey, Message>` forces every key of en.ts to be present (each namespace file
// is also typed against its English twin, so the error lands in the file to fix); the parity
// test (tests/catalogs.test.ts) additionally forbids empty values, requires the same
// {placeholders} as English, and requires every plural category Dutch's rules use.

import type { Message } from "../format.ts";
import type { MsgKey } from "./en.ts";
import { common } from "./nl/common.ts";
import { nav } from "./nl/nav.ts";
import { tokens } from "./nl/tokens.ts";
import { dashboard } from "./nl/dashboard.ts";
import { settings } from "./nl/settings.ts";
import { state } from "./nl/state.ts";
import { update } from "./nl/update.ts";
import { probe } from "./nl/probe.ts";
import { system } from "./nl/system.ts";
import { broker } from "./nl/broker.ts";
import { log } from "./nl/log.ts";

export const nl: Record<MsgKey, Message> = {
  ...common, ...nav, ...tokens,
  ...dashboard, ...settings, ...state, ...update, ...probe, ...system,
  ...broker, ...log,
};
