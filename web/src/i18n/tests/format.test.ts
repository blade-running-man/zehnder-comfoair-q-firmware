// The pure formatter: {token} substitution and plural-category selection.
//
// Run: node src/i18n/tests/format.test.ts

import { format, substitute } from "../format.ts";
import { eq, report } from "../../pages/settings/tests/harness.ts";

// --- substitute ---------------------------------------------------------------------------
eq(substitute("Write: {name}", { name: "Flow" }), "Write: Flow", "fills a named token");
eq(substitute("no tokens here"), "no tokens here", "a template with no token is untouched");
eq(substitute("{a} and {b}", { a: "x" }), "x and {b}", "an unfilled token is left verbatim");
eq(substitute("{n}"), "{n}", "no params leaves every token verbatim");
eq(substitute("{n} left", { n: 5 }), "5 left", "a number param is stringified");
eq(substitute("{1} of {2}", { "1": "a", "2": "b" }), "a of b", "numeric tokens bind (firmwareText captures)");

// --- format, string messages --------------------------------------------------------------
eq(format("Set", "en"), "Set", "a plain string passes through");
eq(format("Write: {name}", "en", { name: "X" }), "Write: X", "a string message substitutes");
eq(format("Set", "uk", undefined, 3), "Set", "a count on a string message changes nothing");

// --- format, plural messages (English: one/other) -----------------------------------------
const days = { one: "{count} day", other: "{count} days" };
eq(format(days, "en", undefined, 1), "1 day", "en count 1 -> one");
eq(format(days, "en", undefined, 0), "0 days", "en count 0 -> other");
eq(format(days, "en", undefined, 2), "2 days", "en count 2 -> other");
eq(format(days, "en"), "{count} days", "no count -> other, {count} left verbatim");
eq(format(days, "en", { count: "many" }, 1), "many day", "an explicit param overrides {count}");
eq(format({ other: "{count} x" }, "en", undefined, 1), "1 x", "a missing category falls back to other");

// --- format, plural messages (Ukrainian: one/few/many/other) ------------------------------
const ukDays = { one: "{count} день", few: "{count} дні", many: "{count} днів", other: "{count} дня" };
eq(format(ukDays, "uk", undefined, 1), "1 день", "uk count 1 -> one");
eq(format(ukDays, "uk", undefined, 2), "2 дні", "uk count 2 -> few");
eq(format(ukDays, "uk", undefined, 5), "5 днів", "uk count 5 -> many");
eq(format(ukDays, "uk", undefined, 21), "21 день", "uk count 21 -> one");
eq(format(ukDays, "uk", undefined, 1.5), "1.5 дня", "uk fraction -> other");

report("i18n/format");
