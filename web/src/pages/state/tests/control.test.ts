// What a writable entity becomes on screen, with no DOM anywhere near it.
//
// Run: node src/pages/state/tests/control.test.ts
import { eq, ok, report } from "../../settings/tests/harness.ts";
import { ENTITIES } from "../../../api/entities.ts";
import { controlKindOf, describeWrite, numberToWrite, rendersControl } from "../control.ts";
import {
  afterWriteFailure,
  beginWrite,
  isConfirmed,
  NO_WRITES,
  settleSent,
  UNCONFIRMED_KEYS,
} from "../writes.ts";
import type { Writes } from "../writes.ts";

// Every writable entity gets a control. A writable entity rendered as dead text is a control
// the owner cannot reach -- which is the whole defect this file exists to close.
for (const meta of ENTITIES) {
  if (!meta.writable) continue;
  ok(controlKindOf(meta) !== "none", `${meta.key} is writable and must render a control`);
}

// And nothing read-only grows one. A control on a value the device refuses to accept is a
// button that answers 422.
for (const meta of ENTITIES) {
  if (meta.writable) continue;
  eq(controlKindOf(meta), "none", `${meta.key} is read-only and must stay text`);
}

// The four shapes, one per entity kind the firmware declares.
const byKey = new Map(ENTITIES.map((m) => [m.key as string, m]));

// A named throw instead of a bare `byKey.get(key)!`: if one of these keys is ever renamed,
// `!` would hand controlKindOf() an `undefined` it has no way to check, and the resulting
// `TypeError: Cannot read properties of undefined` aborts the run before report() ever prints
// a FAIL line -- red, but silent about which key broke. This throws with the key named instead.
function mustFind(key: string) {
  const meta = byKey.get(key);
  if (meta === undefined) throw new Error(`test fixture is stale: "${key}" not found in ENTITIES`);
  return meta;
}

eq(controlKindOf(mustFind("bypass_mode")), "select", "a select entity is a select");
eq(controlKindOf(mustFind("away_mode")), "switch", "a switch entity is a switch");
eq(controlKindOf(mustFind("boost_duration")), "number", "a number entity is a number");
eq(controlKindOf(mustFind("fan")), "fan", "the fan entity is its own shape");

// THE DISTINCTION THIS FILE EXISTS FOR. Three entities have no PDO: the firmware reads them
// over RMI at start-up and every 6 hours, and a write is not read back until that next read,
// so a page that shows their value as a reading right after a write is lying. Everything else
// is confirmed by a later PDO over the socket within seconds.
eq(UNCONFIRMED_KEYS.length, 3, "exactly three controls are not confirmed right after a write");
ok(!isConfirmed("temperature_passive"), "temperature_passive is read back only at the next scheduled read");
ok(!isConfirmed("humidity_comfort"), "humidity_comfort is read back only at the next scheduled read");
ok(!isConfirmed("humidity_protection"), "humidity_protection is read back only at the next scheduled read");
ok(isConfirmed("bypass_mode"), "bypass_mode is confirmed by PDO 66");
ok(isConfirmed("fan"), "the fan is confirmed by PDO 49/65");
ok(
  isConfirmed("boost_duration"),
  "a duration lives on this node, not the Zehnder unit, and the node echoes back what it stored (pdo_table.py, NumberSpec)",
);

// Every unconfirmed key must actually exist. This is the BELT-AND-BRACES half of the two-layer
// guard writes.ts's UNCONFIRMED_KEYS doc describes (matching groups.ts's reasoning, see that file's
// header): the PRIMARY layer is writes.ts's `satisfies readonly (typeof
// ENTITIES)[number]["key"][]`, which fails `tsc -b` on a stale key -- inside the firmware build,
// before anyone runs a test. This loop is what still catches the same drift for a bare `node
// control.test.ts`, which does no typechecking at all.
for (const key of UNCONFIRMED_KEYS) {
  ok(byKey.has(key), `UNCONFIRMED_KEYS entry "${key}" exists in ENTITIES`);
}

// --- what the row says between the click and the confirmation --------------------------------

// A write that has left the page and not yet come back.
eq(
  describeWrite({ key: "bypass_mode", sent: "Open", current: "Auto", failed: null }),
  "Sending Open…",
  "a confirmed control says it is waiting",
);

// The socket has delivered the new value: the write is done and the row is ordinary again.
eq(
  describeWrite({ key: "bypass_mode", sent: "Open", current: "Open", failed: null }),
  "",
  "once the device reports the new value there is nothing left to say",
);

// THE CASE THAT MATTERS. No PDO carries this back and the next scheduled read may be six hours
// away, so "Sending…" would hang for hours and a tick would be a lie. The row says what is
// actually true.
eq(
  describeWrite({ key: "humidity_comfort", sent: "On", current: "Off", failed: null }),
  "Sent. The unit's value is read at start-up and every 6 hours; a change is confirmed at the "
    + "next read.",
  "an unconfirmed control says so instead of pretending",
);

// A refusal is the one thing the device DOES answer, and it must not look like a slow success.
eq(
  describeWrite({ key: "bypass_mode", sent: "Open", current: "Auto", failed: "the bus is busy" }),
  "Not sent: the bus is busy",
  "a refusal is reported as one",
);

// The page's real refusal state, not the one above: Task 4's write() deletes `sent[key]` on
// failure, so a live refusal arrives as `{sent: null, failed: msg}`. Checking `sent === null`
// before `failed` would render this as "" -- a refusal silently swallowed -- while every other
// assertion in this file stayed green, because none of them puts a refusal on a cleared `sent`.
eq(
  describeWrite({ key: "bypass_mode", sent: null, current: "Auto", failed: "the bus is busy" }),
  "Not sent: the bus is busy",
  "a refusal is reported even after the page has cleared what it sent",
);

// A refusal outranks the unconfirmed note too: an unconfirmed key that was refused must say so,
// not repeat "Sent." for a write that never left the queue.
eq(
  describeWrite({ key: "humidity_comfort", sent: "On", current: "Off", failed: "unknown option" }),
  "Not sent: unknown option",
  "a refusal outranks the unconfirmed note",
);

eq(
  describeWrite({ key: "bypass_mode", sent: null, current: "Auto", failed: null }),
  "",
  "an untouched control says nothing",
);

ok(
  describeWrite({ key: "humidity_comfort", sent: "On", current: "On", failed: null }) !== "",
  "an unconfirmed control keeps its note even when the values happen to match",
);

// --- numberToWrite: what a typed string becomes on the wire, or why it must not go at all -----

const boostDuration = mustFind("boost_duration"); // unit "min", range [1, 1440]
const awayDuration = mustFind("away_duration"); // unit "min", range [0, 43200] -- 0 = indefinite

eq(
  numberToWrite("", awayDuration),
  null,
  "an empty box is refused, not sent as 0 -- 0 means indefinite for away_duration",
);
eq(numberToWrite("   ", boostDuration), null, "whitespace-only is refused the same as empty");

eq(numberToWrite("NaN", boostDuration), null, "the literal string NaN is refused");
eq(numberToWrite("Infinity", boostDuration), null, "Infinity is refused");
eq(numberToWrite("abc", boostDuration), null, "unparseable text is refused (Number(\"abc\") is NaN)");

// THE CASE THIS FUNCTION EXISTS FOR. comfoair_command.cpp:116 compares the raw float against the
// registry's range and only truncates afterwards (comfoair_command.cpp:120) -- so a fraction
// clears the range check, gets accepted, and is silently stored rounded down. The device then
// echoes back "12" while the page's `sent` still says "12.5", and describeWrite()'s
// `sent === current` never becomes true: "Sending 12.5…" forever, on a write that in fact
// succeeded. Refusing here, before the request leaves the page, is the only place this can be
// caught.
eq(
  numberToWrite("12.5", boostDuration),
  null,
  "a fraction is refused for a minutes field -- the firmware truncates it after the range check",
);
eq(numberToWrite("0.5", awayDuration), null, "a fraction is refused even within range");

// Accepted: a plain integer, in or out of range. Range is the registry's alone to enforce
// (comfoair_command.cpp:112-113); a value outside it is safe to send as-is, because unlike the
// fraction above the firmware never mangles it into something else first -- it is simply refused
// with a 422, the same path every other refusal already takes.
eq(numberToWrite("30", boostDuration), 30, "a plain integer is accepted");
eq(numberToWrite("0", awayDuration), 0, "0 is a real value for away_duration (indefinite), not empty");
eq(numberToWrite("-5", boostDuration), -5, "out-of-range is accepted here and left for the device to refuse");
eq(numberToWrite("99999", boostDuration), 99999, "over-range is accepted here and left for the device to refuse");

// "1e3" and "0x10" both parse to whole numbers (Number("1e3") === 1000, Number("0x10") === 16),
// so Number.isInteger lets them through -- safe only because the caller sends and displays the
// NUMBER this function returns, never the text the owner typed.
eq(numberToWrite("1e3", boostDuration), 1000, "exponential notation that resolves to a whole number is accepted");
eq(numberToWrite("0x10", boostDuration), 16, "hex notation that resolves to a whole number is accepted");

// --- rendersControl: whether a control actually appears, as opposed to meta.writable ----------

// Every kind with a real EntityControl.tsx branch renders one.
ok(rendersControl(mustFind("bypass_mode")), "a select renders a control");
ok(rendersControl(mustFind("away_mode")), "a switch renders a control");
ok(rendersControl(mustFind("boost_duration")), "a number renders a control");

// THE CASE THIS FUNCTION EXISTS FOR. The fan is writable -- meta.writable is true -- but
// EntityControl.tsx's own interim guard draws nothing for kind "fan" (Task 5 fills that branch
// in). State.tsx used to partition on meta.writable directly, which sent the fan into the
// controls block, got back null, and both produced an empty bordered row AND dropped the fan's
// live reading from the page (it was then also excluded from the read-only <dl>). This is the
// assertion that would have caught that regression before it shipped.
eq(rendersControl(mustFind("fan")), false, "the fan is writable but renders no control yet");
ok(mustFind("fan").writable, "confirms the case above is the interesting one: writable but not");

// And nothing read-only claims to render one either -- rendersControl narrows meta.writable,
// never widens it.
for (const meta of ENTITIES) {
  if (meta.writable) continue;
  eq(rendersControl(meta), false, `${meta.key} is read-only and renders no control`);
}

// --- describeWrite's `kind`: a switch's "Sending true…" reads as a word, not a boolean --------

// THE CASE THIS FIXES. Task 4's write() stores String(value); for a switch that is the literal
// strings "true"/"false", and nobody says "Sending true…" out loud.
eq(
  describeWrite({ key: "away_mode", sent: "true", current: "false", failed: null, kind: "switch" }),
  "Sending On…",
  "a switch's in-flight note reads as a word, not String(boolean)",
);
eq(
  describeWrite({ key: "away_mode", sent: "false", current: "true", failed: null, kind: "switch" }),
  "Sending Off…",
  "both directions translate",
);

// Every other kind is unaffected -- `kind` is optional and defaults to no translation, so every
// assertion earlier in this file (none of which pass `kind`) keeps its original behaviour.
eq(
  describeWrite({ key: "bypass_mode", sent: "Open", current: "Auto", failed: null }),
  "Sending Open…",
  "omitting `kind` leaves a select's own word alone, unchanged from before this field existed",
);
eq(
  describeWrite({ key: "bypass_mode", sent: "Open", current: "Auto", failed: null, kind: "select" }),
  "Sending Open…",
  "and a select passed explicitly is also left alone -- only kind \"switch\" translates",
);

// --- settleSent: PROOF that an unswept `sent` lies forever once the device changes on its own -

// Reproduced sequence (a review finding, not a hypothetical): the owner picks Boost. The device
// confirms it. Half an hour later boost expires BY ITSELF -- comfoair reverts ventilation_mode
// to Auto with nothing in flight. Demonstrated here first with describeWrite() ALONE, which
// already exists and needs no fix: it proves the defect is real given a `sent` nobody ever
// cleared, which is exactly what State.tsx did before this fix (see writes.ts's Writes header).
eq(
  describeWrite({ key: "ventilation_mode", sent: "Boost", current: "Auto", failed: null }),
  "Sending Boost…",
  "an unswept `sent` reads a self-expired change as still in flight, forever -- the bug settleSent fixes",
);

// settleSent() is the fix: it removes a key from `sent` the moment it is FIRST seen settled, so
// a later autonomous change is never compared against a stale write again.
{
  let writes: Writes = { ...NO_WRITES, sent: { ventilation_mode: "Boost" } };
  // Step 1: the device echoes "Boost" back -- the write settles.
  writes = settleSent(writes, { ventilation_mode: "Boost" });
  eq(writes.sent, {}, "a settled write is removed from `sent`, not merely masked by sent === current");

  // Step 2: half an hour later, boost expires on its own. Nothing is in flight for this key.
  const note = describeWrite({
    key: "ventilation_mode",
    sent: writes.sent["ventilation_mode"] ?? null,
    current: "Auto",
    failed: null,
  });
  eq(note, "", "FIXED: once swept, a later autonomous change reads as an ordinary reading, not a stuck write");
}

// A key still in flight (not yet settled) is left alone.
{
  const writes = { ...NO_WRITES, sent: { bypass_mode: "Open" } };
  const next = settleSent(writes, { bypass_mode: "Auto" }); // device has not caught up yet
  eq(next.sent, { bypass_mode: "Open" }, "an unsettled write stays in `sent`");
  ok(next === writes, "and nothing changed, so the same object is returned -- no needless re-render");
}

// THE THREE UNCONFIRMED KEYS MUST SURVIVE THE SWEEP -- their note stays by design: the page
// cannot tell the optimistic echo from the later scheduled read, which carries the same value
// (UNCONFIRMED_KEYS' own doc), and describeWrite() only shows the note while `sent` stays set.
{
  const writes = { ...NO_WRITES, sent: { humidity_comfort: "On" } };
  // The optimistic echo "catches up" -- sent === current -- exactly as it does within seconds of
  // every successful write to one of these three (comfoair_http_command.c
  // comfoair_http_entity_post()'s OPTIMISTIC block), hours before the unit's value is next read.
  const next = settleSent(writes, { humidity_comfort: "On" });
  eq(next.sent, { humidity_comfort: "On" }, "an unconfirmed key is never swept, even once sent === current");
  ok(
    describeWrite({ key: "humidity_comfort", sent: next.sent["humidity_comfort"] ?? null, current: "On", failed: null })
      !== "",
    "so its permanent note is still there afterwards",
  );
}

// --- afterWriteFailure: PROOF that an earlier write's late failure must not clobber a later one

// Reproduced sequence: write A (bypass_mode -> "Open") is slow. Before it answers, the owner
// picks again -- write B (bypass_mode -> "Auto"). beginWrite() for B overwrites sent.bypass_mode
// with "Auto". A's answer then arrives late as a 503 ("the bus is busy"). Without a seq guard,
// A's failure handler would delete sent.bypass_mode -- B's entry, not A's -- and set
// failed.bypass_mode, so the control would read "Not sent: the bus is busy" forever over a
// write (B) that in fact went through.
{
  let writes = NO_WRITES;
  writes = beginWrite(writes, "bypass_mode", "Open"); // A dispatched, seq 1
  const seqA = 1;
  writes = beginWrite(writes, "bypass_mode", "Auto"); // B dispatched before A answers, seq 2
  const seqB = 2;
  eq(writes.sent, { bypass_mode: "Auto" }, "B's dispatch already owns `sent` for this key");

  // A's answer lands late. writeSeqRef (simulated here as `seqB`, the latest dispatched) is 2,
  // not A's own seq of 1 -- A has been superseded.
  const afterA = afterWriteFailure(writes, "bypass_mode", seqA, seqB, "the bus is busy");
  eq(afterA, writes, "A's stale failure changes nothing -- identity, same as afterPollFailure()");
  eq(afterA.sent, { bypass_mode: "Auto" }, "B's entry survives A's late, superseded failure");
  eq(afterA.failed, {}, "no refusal is shown for a write (B) that has not actually failed");
}

// The SAME failure, but for the CURRENT (only) write, still reports it -- the guard must not
// swallow every failure, only stale ones.
{
  let writes = NO_WRITES;
  writes = beginWrite(writes, "bypass_mode", "Open");
  const seq = 1;
  const latest = 1; // no later write has been dispatched
  const after = afterWriteFailure(writes, "bypass_mode", seq, latest, "the bus is busy");
  eq(after.sent, {}, "the current write's failure DOES clear `sent`");
  eq(after.failed, { bypass_mode: "the bus is busy" }, "and DOES report the refusal");
}

report("control");
