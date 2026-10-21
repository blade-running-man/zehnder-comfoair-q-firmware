// The Update page's pure half: the pre-flight on a picked file, the KiB spelling, and the page's
// state machine from "file picked" to "the device is back". No DOM, no XHR, no t(): the page feeds
// events in and maps the phase to catalog keys at render, so this file runs under node in the
// suite (tests/model.test.ts) and a language switch still re-renders every word.
//
// Imports carry .ts, and reach nothing with CSS or the DOM: pages/settings/tests/harness.ts
// explains both constraints.

import type { MsgKey } from "../../i18n/catalogs/en.ts";
import { isThisFirmware, parseAppDesc } from "./image.ts";

// The device's own pre-flight (components/comfoair_ota/comfoair_ota.c), repeated here so a wrong
// file is refused BEFORE a byte of it uploads. The device still checks everything -- these are a
// courtesy, never the gate -- so a drift here costs a pointless upload, not a bricked slot.
/** esp_image_header_t is 24 bytes; the magic is byte 0. The page reads PREFLIGHT_BYTES (image.ts),
 *  which also covers the app descriptor. */
export const HEADER_BYTES = 24;
export const IMAGE_MAGIC = 0xe9;
/** COMFOAIR_OTA_MIN_IMAGE_BYTES: anything shorter is not an app image of this firmware. */
export const MIN_IMAGE_BYTES = 64 * 1024;
/** One OTA slot of partitions.csv (0x1F0000). An image of exactly this size still fits. */
export const SLOT_BYTES = 1984 * 1024;

/** After the 200: poll GET /api/status this often, and give up after this long. A reboot takes
 *  5-15 s; ninety covers a slow Wi-Fi rejoin without leaving the owner watching a spinner for ever. */
export const POLL_MS = 2000;
export const GIVE_UP_MS = 90_000;
/** The device answers 200 and reboots ~1 s LATER, so the first poll can still reach the old
 *  firmware. An unchanged version is therefore believed only once a poll has failed (the device
 *  went away and came back) or after this long -- otherwise "nothing changed" would be reported
 *  about a device that simply had not restarted yet. */
export const SAME_AFTER_MS = 20_000;

/** A local refusal, keyed to the device's own sentence for the same fault (update.detail.*). */
export type Refusal = "tooSmall" | "tooLarge" | "notImage" | "wrongProject";

export const REFUSAL_KEYS: Record<Refusal, MsgKey> = {
  tooSmall: "update.detail.tooSmall",
  tooLarge: "update.detail.tooLarge",
  notImage: "update.detail.notImage",
  wrongProject: "update.detail.wrongProject",
};

/**
 * The device's order: the length first (comfoair_ota_check_length, before the body is read),
 * then the header, then the app descriptor. A .bin.gz of a plausible size is therefore "not an
 * image", a stray text file "too small", and the OpenTherm thermostat's image (a valid C6 app)
 * "a different device" -- the same answers the device would have given.
 */
export function checkImage(size: number, head: Uint8Array): Refusal | null {
  if (size < MIN_IMAGE_BYTES) return "tooSmall";
  if (size > SLOT_BYTES) return "tooLarge";
  if (head.length < HEADER_BYTES || head[0] !== IMAGE_MAGIC) return "notImage";
  if (!isThisFirmware(parseAppDesc(head))) return "wrongProject";
  return null;
}

/** Whole KiB, rounded: "1312". The catalog adds the unit. Locale-neutral like every number here. */
export function kib(bytes: number): number {
  return Math.round(Math.max(0, bytes) / 1024);
}

/** Rounded DOWN, so 100 % is shown only when the last byte has gone. */
export function percent(sent: number, total: number): number {
  if (total <= 0) return 0;
  return Math.min(100, Math.max(0, Math.floor((sent * 100) / total)));
}

// --- the state machine ----------------------------------------------------------------------

/** `version` is the image's own (its app descriptor), null when the file has none -- then it is
 *  refused locally and never uploaded. */
export interface Picked { name: string; size: number; version: string | null }

/** What a refused or failed upload says. `detail` is the device's sentence (the page translates
 *  it), absent for the cases the page words itself. */
export type Failure =
  | { kind: "login" }                      // 401: the browser's login was dismissed
  | { kind: "noPassword" }                 // 403: the policy refuses OTA without a password
  | { kind: "refused"; detail: string }    // every other status: the device's sentence
  | { kind: "lost" };                      // no answer at all, mid-upload

export type Phase =
  | { kind: "idle" }
  | { kind: "picked"; file: Picked; refusal: Refusal | null }
  | { kind: "uploading"; file: Picked; sent: number; total: number }
  | { kind: "failed"; file: Picked; failure: Failure }
  /** `uncertain`: the connection dropped after the whole file had gone, so whether the device
   *  installed it is only known when it answers again. `expected`: the version the device's 200
   *  said it installed (null when it did not say, or no 200 arrived). */
  | { kind: "restarting"; startedMs: number; elapsedMs: number; seenDown: boolean; uncertain: boolean; expected: string | null }
  | { kind: "back"; before: string | null; after: string; expected: string | null }
  | { kind: "gaveUp" };

export type Event =
  | { kind: "pick"; file: Picked; refusal: Refusal | null }
  | { kind: "start" }
  | { kind: "progress"; sent: number; total: number }
  | { kind: "accepted"; nowMs: number; version: string | null }
  | { kind: "refused"; status: number; detail: string }
  | { kind: "lost"; allSent: boolean; nowMs: number }
  | { kind: "pollFailed"; nowMs: number }
  | { kind: "pollAnswered"; version: string; nowMs: number };

/** A refusal status as the page explains it. 401/403 are the access policy and get the page's own
 *  words (the 403 one is the reason OTA needs a password at all); everything else -- 409 busy or
 *  still on probation, 411, 413, 422, 400, 500 -- is the device's own sentence. */
export function failureFor(status: number, detail: string): Failure {
  if (status === 401) return { kind: "login" };
  if (status === 403) return { kind: "noPassword" };
  return { kind: "refused", detail };
}

/**
 * One step. `before` is sw_version as GET /api/status gave it before the upload (null when that
 * read failed); it is the page's, not the phase's, because it is fixed for the whole attempt.
 * An event that does not fit the phase leaves it unchanged -- a late progress callback after the
 * answer, say, must not drag a finished upload back to "uploading".
 */
export function step(phase: Phase, ev: Event, before: string | null): Phase {
  switch (ev.kind) {
    case "pick":
      // A new file is always allowed except mid-flight: the page disables the picker then, and
      // this keeps a stray change event from abandoning an upload the device is still receiving.
      if (phase.kind === "uploading" || phase.kind === "restarting") return phase;
      return { kind: "picked", file: ev.file, refusal: ev.refusal };
    case "start":
      // From a failure too: a 409 ("try again in a minute") or a dropped Wi-Fi is retried with the
      // same file, not by picking it again.
      if (phase.kind === "failed") return { kind: "uploading", file: phase.file, sent: 0, total: phase.file.size };
      if (phase.kind !== "picked" || phase.refusal !== null) return phase;
      return { kind: "uploading", file: phase.file, sent: 0, total: phase.file.size };
    case "progress":
      if (phase.kind !== "uploading") return phase;
      return { ...phase, sent: ev.sent, total: ev.total > 0 ? ev.total : phase.total };
    case "accepted":
      if (phase.kind !== "uploading") return phase;
      return { kind: "restarting", startedMs: ev.nowMs, elapsedMs: 0, seenDown: false, uncertain: false, expected: ev.version };
    case "refused":
      if (phase.kind !== "uploading") return phase;
      return { kind: "failed", file: phase.file, failure: failureFor(ev.status, ev.detail) };
    case "lost":
      if (phase.kind !== "uploading") return phase;
      // The whole file went and no answer came: the device may have taken it and rebooted before
      // the 200 reached us. Waiting for it to answer is the only way to know -- reporting a
      // failure here could send the owner to re-flash a device that is already updated.
      if (ev.allSent) return { kind: "restarting", startedMs: ev.nowMs, elapsedMs: 0, seenDown: true, uncertain: true, expected: null };
      return { kind: "failed", file: phase.file, failure: { kind: "lost" } };
    case "pollFailed": {
      if (phase.kind !== "restarting") return phase;
      const elapsedMs = ev.nowMs - phase.startedMs;
      if (elapsedMs >= GIVE_UP_MS) return { kind: "gaveUp" };
      return { ...phase, elapsedMs, seenDown: true };
    }
    case "pollAnswered": {
      if (phase.kind !== "restarting") return phase;
      const elapsedMs = ev.nowMs - phase.startedMs;
      // An unknown `before` proves nothing either way, so it waits like an unchanged one.
      const changed = before !== null && ev.version !== before;
      if (changed || phase.seenDown || elapsedMs >= SAME_AFTER_MS) {
        return { kind: "back", before, after: ev.version, expected: phase.expected };
      }
      return { ...phase, elapsedMs };
    }
  }
}

/** The page polls only while the device is restarting, and the picker is locked while a file is
 *  in flight or being installed. */
export function busy(phase: Phase): boolean {
  return phase.kind === "uploading" || phase.kind === "restarting";
}

/** What "back" means: a new version, the same one (it may have rolled back), or a device whose
 *  version before the upload was never read. */
export type Outcome = "updated" | "same" | "unknownBefore";

/**
 * `expected` is the 200's version. Running exactly that is an update even when it equals `before`:
 * a deliberate reinstall of the same build is not a rollback, and without the 200's word the two
 * look identical. With no 200 version (an older firmware, or the answer was lost), an unchanged
 * version is reported as "same" -- the cautious reading.
 */
export function outcome(before: string | null, after: string, expected: string | null = null): Outcome {
  if (before === null) return "unknownBefore";
  if (expected !== null && after === expected) return "updated";
  return before === after ? "same" : "updated";
}
