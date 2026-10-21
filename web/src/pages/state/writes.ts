// What State.tsx remembers about every write it made, and which writes the unit confirms.
// Pure, so tests/control.test.ts can reach all of it. Split out of control.ts (which keeps what
// a writable entity becomes on screen and the sentence under it) along the seam between "what
// the control looks like" and "what the page knows about its own writes".

import type { ENTITIES } from "../../api/entities.ts";
import type { StateValue } from "../../api/ws.ts";

/**
 * The three controls whose write the page cannot confirm when it is made, and why they are
 * named rather than derived.
 *
 * Every other write is confirmed by a later PDO on the push socket within a second or two --
 * comfoair_http_entity_post(): "ACCEPTED, not applied. Every write is fire-and-forget -- the
 * command path sends RMI and never listens for a reply -- so a refused frame is indistinguishable
 * from an accepted one, and the only confirmation is the unit's own PDO arriving seconds later on
 * /ws." These three have no PDO at all. The firmware reads them from the unit over RMI instead:
 * once at start-up (about 10 s after boot) and every 6 hours after that, retrying every 60 s until
 * the unit answers; until that first read they are unknown. A write is NOT followed by a read:
 * comfoair_http_entity_post() echoes the written option into state optimistically (its OPTIMISTIC
 * block), and the unit's own value replaces that echo only at the next scheduled read, up to six
 * hours later. So in the hours after a write, what the page shows is that echo, not a reading --
 * and when the read does come, it carries the same value, so this page cannot tell the two apart.
 *
 * Named rather than derived because nothing in the entity metadata distinguishes them: they are
 * ordinary writable selects on the wire. That makes this a hand-kept key list of the kind
 * AGENTS.md forbids unless something catches it going stale -- which, as in groups.ts
 * (see that file's header), takes two layers, because no one layer catches both ways this file
 * gets run:
 *
 *   * PRIMARY: `satisfies readonly (typeof ENTITIES)[number]["key"][]` below types every entry
 *     against the live key union, so a rename or removal in pdo_table.py fails `tsc -b` right
 *     here, at the stale literal, inside the firmware build itself
 *     (components/web_assets/CMakeLists.txt runs `tsc -b` on every `pio run`) -- the loudest
 *     failure this project has, and it fires before anyone remembers to run a test.
 *   * BELT-AND-BRACES: tests/control.test.ts's runtime "every UNCONFIRMED_KEYS entry exists in
 *     ENTITIES" loop catches the same drift for a bare `node control.test.ts`, which does not
 *     typecheck (harness.ts's own header explains why these suites are plain node, not a
 *     type-checked runner).
 *
 * Either layer alone would miss one of the two ways this file gets run. Losing this guard is not
 * cosmetic: a stale entry here makes isConfirmed() return true for one of these three, which
 * puts a confirmation tick on a value the unit has not been asked for since the write -- the
 * exact "page inventing a fact" this constant exists to prevent (see the DO NOT just below).
 *
 * DO NOT show a confirmation tick for these after a write. The write is read back only at the
 * next scheduled read, hours away, so a tick now would still be the page inventing a fact.
 */
export const UNCONFIRMED_KEYS = [
  "temperature_passive",
  "humidity_comfort",
  "humidity_protection",
] as const satisfies readonly (typeof ENTITIES)[number]["key"][];

export function isConfirmed(key: string): boolean {
  return !(UNCONFIRMED_KEYS as readonly string[]).includes(key);
}

// --- what State.tsx remembers about every write in flight, and how that changes -------------
//
// Pure transitions over one small value, the same shape status.ts settled on for the Settings
// page's poll loop after a review round found two of three defects in state that used to be
// ref-juggling inside the component, uncaught by any test because there was nothing here a test
// could reach (status.ts's own header). `sent`/`failed` were two separate `useState` calls in an
// earlier draft of State.tsx; folding them into one value here is what lets a single pure
// function update both together (afterWriteFailure() clears one key's `sent` and sets its
// `failed` in the same transition, which two independent `setState` calls cannot express
// atomically).

/** What State.tsx remembers about every write in flight, by key. */
export interface Writes {
  /** What was written and not yet confirmed, by key. Absent means nothing outstanding. */
  sent: Record<string, string>;
  /** A display-ready refusal fragment, by key. See WriteState.failed's own doc for "sticky". */
  failed: Record<string, string>;
}

/** The page on load: nothing has been written yet. */
export const NO_WRITES: Writes = { sent: {}, failed: {} };

function withoutKey(map: Record<string, string>, key: string): Record<string, string> {
  if (!(key in map)) return map;
  const next = { ...map };
  delete next[key];
  return next;
}

/**
 * A write is dispatched. Records what was sent and clears any standing refusal for the same
 * key -- the one place `failed` is allowed to clear on anything OTHER than a fresh look at the
 * device's own state, because a fresh attempt is exactly the "next write" WriteState.failed's
 * own doc names as the only thing allowed to clear it.
 *
 * `shown` is `String(value)`, computed by the caller (State.tsx's write()) rather than here, so
 * this function never has to import `StateValue` just to re-derive a `String()` call its caller
 * already made.
 */
export function beginWrite(writes: Writes, key: string, shown: string): Writes {
  return {
    sent: { ...writes.sent, [key]: shown },
    failed: withoutKey(writes.failed, key),
  };
}

/**
 * A write's answer came back as a refusal -- UNLESS it has been superseded by a later write to
 * the same key, in which case this is identity, same as afterPollFailure() in status.ts and for
 * the same reason: an outcome that is no longer the latest attempt must not touch state a newer
 * attempt already owns.
 *
 * Reproduced sequence this guards against (control.test.ts pins it): write A to `bypass_mode` is
 * slow; before it answers, the owner picks again, write B. `beginWrite()` for B has already
 * overwritten `sent.bypass_mode` with B's value. A's answer then arrives late as a 503 ("the bus
 * is busy"). Without this check, A's failure handler would delete `sent.bypass_mode` -- B's
 * entry, not A's -- and set `failed.bypass_mode`, so the control reads "Not sent: the bus is
 * busy" forever over a write (B) that in fact went through. `seq`/`latestSeq` are what tell A's
 * outcome from B's: State.tsx's write() captures an incrementing per-key counter SYNCHRONOUSLY,
 * before its `await`, in a `useRef` (`writeSeqRef` -- see that function's own comment for why the
 * counter itself has to be a ref: it must be assignable the instant a write is DISPATCHED,
 * before any state update a pure function could observe has committed, the same "capture before
 * the await" discipline Settings.tsx's `statusInFlightRef` doc describes). `latestSeq` is
 * whatever that ref holds for `key` the moment an answer arrives -- higher than `seq` exactly
 * when a later write has been dispatched since. `undefined` (a key with no counter at all, which
 * should not happen if this is only ever called from a write's own catch block) compares unequal
 * to any real `seq` -- sequence numbers start at 1 -- so an outcome for an untracked key is
 * treated as stale rather than trusted by accident.
 *
 * `message` is a display-ready fragment already, from `oneLineFragment(explainError(err))`
 * (`../settings/errors.ts`) -- this function does not further shape it, matching
 * `WriteState.failed`'s own contract.
 */
export function afterWriteFailure(
  writes: Writes,
  key: string,
  seq: number,
  latestSeq: number | undefined,
  message: string,
): Writes {
  if (latestSeq !== seq) return writes;
  return {
    sent: withoutKey(writes.sent, key),
    failed: { ...writes.failed, [key]: message },
  };
}

/**
 * Drops every `sent` entry that has settled -- the device now reports back exactly what was
 * written, so `describeWrite()` would show "" for it anyway (its own `sent === current` check).
 * Removing it here, rather than leaving it to sit forever once matched, is what stops a lie this
 * page would otherwise tell indefinitely: the unit changes state on its own constantly -- boost
 * expires, a bypass override lapses (bypass_duration is exactly that timer), the wall panel or
 * Home Assistant issues a command -- and none of that is a write this page made. Without this
 * sweep, `sent.ventilation_mode` stays "Boost" for ever after the owner's own write settles, and
 * the moment the unit later reports "Auto" on its own, `describeWrite()` compares the stale
 * "Boost" against the new "Auto" and reads "Sending Boost…" on a control nothing is in flight
 * for. Call from an effect keyed on the device state (State.tsx), the same "transition on new
 * information, not on a timer" shape `afterPollSuccess()` uses in status.ts.
 *
 * The three UNCONFIRMED_KEYS never settle here and must not be swept -- see their own doc.
 * Right after a write, `current` for them is not a device reading; it is the firmware's own
 * optimistic echo (comfoair_http_command.c comfoair_http_entity_post()'s OPTIMISTIC block), so
 * `sent === current` for one of them means only that the echo caught up, not that the unit has
 * confirmed anything -- that waits for the next scheduled read, up to six hours later, and carries
 * the same value, so no frame here can tell the read from the echo. `describeWrite()`'s "Sent. The
 * unit's value is read at start-up and every 6 hours…" note depends on `sent` staying set for as
 * long as the control exists (`isConfirmed` is checked BEFORE `sent === current`, see that
 * function's own doc) -- sweeping one of these three would make the note vanish the instant the
 * echo matched, which is most of the time, not the rare case.
 *
 * Identity when nothing settled (returns `writes` itself, not a copy), matching afterPollFailure
 * ()'s style in status.ts: State.tsx calls this from an effect on every incoming frame, and a
 * bail-out on an unchanged reference is what keeps a frame with nothing to settle from queuing a
 * `setWrites` no one asked for.
 */
export function settleSent(writes: Writes, values: Readonly<Record<string, StateValue>>): Writes {
  let changed = false;
  const next: Record<string, string> = {};
  for (const [key, sent] of Object.entries(writes.sent)) {
    if (!isConfirmed(key)) {
      next[key] = sent;
      continue;
    }
    const value = values[key];
    const current = value === undefined || value === null ? "" : String(value);
    if (sent === current) {
      changed = true;
      continue;
    }
    next[key] = sent;
  }
  return changed ? { ...writes, sent: next } : writes;
}
