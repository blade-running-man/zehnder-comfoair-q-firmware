// German. `Record<MsgKey, Message>` forces every key of en.ts to be present (each namespace file
// is also typed against its English twin, so the error lands in the file to fix); the parity
// test (tests/catalogs.test.ts) additionally forbids empty values, requires the same
// {placeholders} as English, and requires every plural category German's rules use.

import type { Message } from "../format.ts";
import type { MsgKey } from "./en.ts";
import { common } from "./de/common.ts";
import { nav } from "./de/nav.ts";
import { tokens } from "./de/tokens.ts";
import { dashboard } from "./de/dashboard.ts";
import { settings } from "./de/settings.ts";
import { state } from "./de/state.ts";
import { update } from "./de/update.ts";
import { probe } from "./de/probe.ts";
import { system } from "./de/system.ts";
import { broker } from "./de/broker.ts";
import { log } from "./de/log.ts";

export const de: Record<MsgKey, Message> = {
  ...common, ...nav, ...tokens,
  ...dashboard, ...settings, ...state, ...update, ...probe, ...system,
  ...broker, ...log,
};
