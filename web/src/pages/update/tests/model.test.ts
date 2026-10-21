// The Update page's pure half: the pre-flight, the KiB/percent spelling and the state machine.
//
// Run: node src/pages/update/tests/model.test.ts
import { eq, ok, report } from "../../settings/tests/harness.ts";
import {
  GIVE_UP_MS, HEADER_BYTES, MIN_IMAGE_BYTES, SAME_AFTER_MS, SLOT_BYTES,
  busy, checkImage, failureFor, kib, outcome, percent, step, type Phase,
} from "../model.ts";
import { prefix } from "./fixture.ts";

// The real firmware.bin's pre-flight bytes with byte 0 replaced, cut to `len` (image.test.ts
// covers the descriptor half; these cases are the length and the header byte).
const head = (first: number, len?: number): Uint8Array => prefix({ first }).subarray(0, len);
const IMAGE = 1_300_000;

// --- the pre-flight -------------------------------------------------------------------------
eq(checkImage(IMAGE, head(0xe9)), null, "a 0xE9 image of a plausible size passes");
eq(checkImage(IMAGE, head(0x1f)), "notImage", "a .bin.gz (1F 8B) is not an image");
eq(checkImage(IMAGE, head(0x7f)), "notImage", "an .elf (7F 'E' 'L' 'F') is not an image");
eq(checkImage(IMAGE, head(0xe9, 10)), "notImage", "fewer than 24 header bytes is not an image");
eq(HEADER_BYTES, 24, "esp_image_header_t is 24 bytes");
eq(checkImage(SLOT_BYTES, head(0xe9)), null, "an image exactly the slot's size still fits");
eq(checkImage(SLOT_BYTES + 1, head(0xe9)), "tooLarge", "one byte over the slot is too large");
eq(SLOT_BYTES, 0x1f0000, "the slot is partitions.csv's 0x1F0000 (1984 KiB)");
eq(checkImage(MIN_IMAGE_BYTES - 1, head(0xe9)), "tooSmall", "under 64 KiB is too small");
eq(checkImage(MIN_IMAGE_BYTES, head(0xe9)), null, "64 KiB exactly is enough");
eq(checkImage(10, head(0x7b)), "tooSmall", "length before header, as the device judges: a small JSON file is too small");
eq(checkImage(SLOT_BYTES * 2, head(0x1f)), "tooLarge", "and a huge archive is too large, not 'not an image'");

// --- spelling --------------------------------------------------------------------------------
eq([kib(0), kib(1024), kib(1_343_488), kib(-5)], [0, 1, 1312, 0], "whole KiB, never negative");
eq([percent(0, 100), percent(999, 1000), percent(1000, 1000), percent(5, 0), percent(200, 100)],
  [0, 99, 100, 0, 100], "percent rounds down, clamps, and survives a zero total");

// --- refusals ---------------------------------------------------------------------------------
eq(failureFor(401, "unauthorized"), { kind: "login" }, "401 is the login");
eq(failureFor(403, "forbidden"), { kind: "noPassword" }, "403 is 'set a password first'");
for (const status of [400, 409, 411, 413, 422, 500]) {
  eq(failureFor(status, "The sentence."), { kind: "refused", detail: "The sentence." }, `${status} shows the device's sentence`);
}

// --- the happy path ---------------------------------------------------------------------------
const file = { name: "firmware.bin", size: IMAGE, version: "v2" };
let p: Phase = { kind: "idle" };
p = step(p, { kind: "pick", file, refusal: null }, "v1");
eq(p.kind, "picked", "a pick waits for Install -- no upload on pick");
p = step(p, { kind: "start" }, "v1");
eq(p, { kind: "uploading", file, sent: 0, total: IMAGE }, "Install starts the upload");
ok(busy(p), "an upload is busy");
p = step(p, { kind: "progress", sent: 650_000, total: IMAGE }, "v1");
eq(p.kind === "uploading" && p.sent, 650_000, "progress moves");
eq(step(p, { kind: "pick", file: { name: "x.bin", size: 1, version: null }, refusal: "tooSmall" }, "v1"), p, "a pick mid-upload is ignored");
p = step(p, { kind: "accepted", nowMs: 1000, version: "v2" }, "v1");
eq(p, { kind: "restarting", startedMs: 1000, elapsedMs: 0, seenDown: false, uncertain: false, expected: "v2" },
  "200: the device is restarting, and said which version it installed");
eq(step(p, { kind: "progress", sent: IMAGE, total: IMAGE }, "v1"), p, "a late progress event does not reopen the upload");
const early = step(p, { kind: "pollAnswered", version: "v1", nowMs: 3000 }, "v1");
eq(early.kind, "restarting", "the old firmware answering before its reboot is not 'unchanged'");
p = step(p, { kind: "pollFailed", nowMs: 3000 }, "v1");
eq(p.kind === "restarting" && p.seenDown, true, "a failed poll: the device went away");
p = step(p, { kind: "pollAnswered", version: "v2", nowMs: 9000 }, "v1");
eq(p, { kind: "back", before: "v1", after: "v2", expected: "v2" }, "it answers with the new version");
eq(outcome("v1", "v2", "v2"), "updated", "v1 -> v2 is an update");
eq(outcome("v1", "v2"), "updated", "and so it is without the 200's version (an older firmware)");
ok(!busy(p), "back is not busy: another file may be picked");

// --- a new version is believed at once; the same one only after the device went away -----------
{
  const r: Phase = { kind: "restarting", startedMs: 0, elapsedMs: 0, seenDown: false, uncertain: false, expected: null };
  eq(step(r, { kind: "pollAnswered", version: "v2", nowMs: 2000 }, "v1").kind, "back", "a changed version needs no outage");
  const down = step(r, { kind: "pollFailed", nowMs: 2000 }, "v1");
  eq(step(down, { kind: "pollAnswered", version: "v1", nowMs: 8000 }, "v1"), { kind: "back", before: "v1", after: "v1", expected: null },
    "after an outage, the same version is the answer (it may have rolled back)");
  eq(outcome("v1", "v1"), "same", "same version is 'same'");
  eq(outcome("v1", "v1", "v2"), "same", "the old version back where the 200 named another: rolled back");
  eq(outcome("v1", "v1", "v1"), "updated", "the version the 200 named, equal to before: a reinstall, not a rollback");
  eq(outcome("v1", "v3", "v2"), "updated", "a third version is still a change (the line shows what runs)");
  eq(step(r, { kind: "pollAnswered", version: "v1", nowMs: SAME_AFTER_MS }, "v1").kind, "back",
    "with no outage seen, the same version is believed after SAME_AFTER_MS");
  eq(step(r, { kind: "pollAnswered", version: "v1", nowMs: 4000 }, null).kind, "restarting",
    "an unknown 'before' waits like an unchanged one");
  eq(step(down, { kind: "pollAnswered", version: "v2", nowMs: 8000 }, null), { kind: "back", before: null, after: "v2", expected: null },
    "and is back after an outage");
  eq(outcome(null, "v2"), "unknownBefore", "with no 'before', only 'after' can be said");
  eq(step(down, { kind: "pollFailed", nowMs: GIVE_UP_MS - 1 }, "v1").kind, "restarting", "still waiting just before 90 s");
  eq(step(down, { kind: "pollFailed", nowMs: GIVE_UP_MS }, "v1"), { kind: "gaveUp" }, "gives up at 90 s");
}

// --- local refusal, device refusal, a lost connection ----------------------------------------
{
  const refused = step({ kind: "idle" }, { kind: "pick", file, refusal: "notImage" }, "v1");
  eq(step(refused, { kind: "start" }, "v1"), refused, "a locally refused file never starts uploading");
  const up: Phase = { kind: "uploading", file, sent: 4096, total: IMAGE };
  eq(step(up, { kind: "refused", status: 403, detail: "forbidden" }, "v1"),
    { kind: "failed", file, failure: { kind: "noPassword" } }, "403 fails with the password explanation");
  eq(step(up, { kind: "refused", status: 409, detail: "Another update is already being uploaded." }, "v1"),
    { kind: "failed", file, failure: { kind: "refused", detail: "Another update is already being uploaded." } }, "409 keeps the sentence");
  eq(step(up, { kind: "lost", allSent: false, nowMs: 5 }, "v1"), { kind: "failed", file, failure: { kind: "lost" } },
    "a connection lost mid-file is a failure");
  eq(step(up, { kind: "lost", allSent: true, nowMs: 5 }, "v1"),
    { kind: "restarting", startedMs: 5, elapsedMs: 0, seenDown: true, uncertain: true, expected: null },
    "lost after the whole file went: wait and see, it may have installed");
  const failed = step(up, { kind: "lost", allSent: false, nowMs: 5 }, "v1");
  eq(step(failed, { kind: "pick", file, refusal: null }, "v1").kind, "picked", "after a failure another file may be picked");
  eq(step(failed, { kind: "start" }, "v1"), { kind: "uploading", file, sent: 0, total: IMAGE }, "or the same one retried");
}

report("pages/update/model");
