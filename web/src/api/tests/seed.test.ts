// How the client gets its FIRST full state, with no socket and no fetch anywhere near it.
//
// Run: node src/api/tests/seed.test.ts
//
// The device's own full-state-on-connect is best-effort: it detects a new client by the
// WebSocket count GROWING between two pushes (comfoair_http_ws.c, ws_send_state), so a page
// reload -- one socket closed, one opened between samples -- keeps the count flat and the
// new client sees only deltas. GET /api/state is the reliable source, and this module owns
// the one hard part: a delta can arrive on the socket while the snapshot is still in
// flight, and each carries truth the other lacks.
import { eq, ok, report } from "../../pages/settings/tests/harness.ts";
import { Seeder } from "../seed.ts";

import type { SeedAction } from "../seed.ts";

function values(action: SeedAction): Record<string, unknown> {
  ok(action.kind !== "none", "expected an action that carries values");
  return action.kind === "none" ? {} : action.values;
}

// --- the normal path: open, buffer, snapshot, live -------------------------------------

{
  const s = new Seeder();
  const token = s.reset();

  // A delta before the snapshot cannot be applied: the screen would show three values and
  // a hole for every other entity, which is exactly the symptom this module exists to remove.
  eq(s.delta({ fan_speed: 2 }).kind, "none", "a delta before the snapshot is held");
  eq(s.delta({ temp: 21.5 }).kind, "none", "so is the next one");

  const seeded = s.snapshot(token, { fan_speed: 1, temp: 20, mode: "auto" });
  eq(seeded.kind, "replace", "the snapshot replaces everything");
  // The buffered deltas were rendered by the device AFTER the connection opened; the
  // snapshot may predate them. Held deltas win over the snapshot's values.
  eq(values(seeded).fan_speed, 2, "a held delta beats the snapshot for its key");
  eq(values(seeded).temp, 21.5, "each held delta by its own key");
  eq(values(seeded).mode, "auto", "keys only the snapshot knows survive the merge");

  const live = s.delta({ fan_speed: 3 });
  eq(live.kind, "merge", "after seeding, deltas apply directly");
  eq(values(live).fan_speed, 3, "and carry their values through");
}

// --- the device's own full frame -------------------------------------------------------

{
  const s = new Seeder();
  const token = s.reset();

  s.delta({ fan_speed: 2 });
  const full = s.full({ fan_speed: 1, temp: 20 });
  eq(full.kind, "replace", "a state frame from the socket seeds on its own");
  // The socket is ordered: the full frame was rendered after the held deltas, so they are
  // already inside it. Merging them back would resurrect older values.
  eq(values(full).fan_speed, 1, "held deltas are dropped -- the full frame supersedes them");

  eq(
    s.snapshot(token, { fan_speed: 0 }).kind,
    "none",
    "a snapshot resolving after the socket's full frame is ignored",
  );
  eq(s.delta({ temp: 19 }).kind, "merge", "and deltas keep flowing");
}

// --- a failed snapshot ------------------------------------------------------------------

{
  const s = new Seeder();
  const token = s.reset();

  s.delta({ fan_speed: 2 });
  const flushed = s.snapshotFailed(token);
  // Degraded, not stuck: the held deltas are the only truth we have, and live ones must
  // keep flowing. The device's next full frame or the next reconnect repairs the holes.
  eq(flushed.kind, "merge", "a failed snapshot releases what was held");
  eq(values(flushed).fan_speed, 2, "with the held values intact");
  eq(s.delta({ temp: 19 }).kind, "merge", "and the connection stays live");
}

// --- reconnects and stale fetches -------------------------------------------------------

{
  const s = new Seeder();
  const first = s.reset();
  s.snapshot(first, { fan_speed: 1 });

  const second = s.reset();
  eq(s.delta({ temp: 19 }).kind, "none", "reset() holds deltas again for the new connection");

  // The previous connection's fetch can resolve after the reconnect. Its document predates
  // everything on the new socket and must not clobber the fresh snapshot's merge.
  eq(s.snapshot(first, { fan_speed: 9 }).kind, "none", "a stale snapshot is ignored");
  eq(s.snapshotFailed(first).kind, "none", "so is a stale failure");

  const seeded = s.snapshot(second, { fan_speed: 1, temp: 20 });
  eq(seeded.kind, "replace", "the current connection's snapshot still seeds");
  eq(values(seeded).temp, 19, "with the newly held deltas on top");
}

// --- held deltas coalesce ---------------------------------------------------------------

{
  const s = new Seeder();
  const token = s.reset();
  s.delta({ fan_speed: 1 });
  s.delta({ fan_speed: 2 });
  const seeded = s.snapshot(token, { fan_speed: 0 });
  eq(values(seeded).fan_speed, 2, "for one key, the last held delta wins");
}

report("seed");
