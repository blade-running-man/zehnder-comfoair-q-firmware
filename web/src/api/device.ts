// The live device state, kept current by the push socket.
//
// One place holds the values; everything else reads a signal. This is the client-side
// mirror of `comfoair_state` on the device -- and, like it, it is not a second entity
// list: the keys come from the device, and `api/entities.ts` (generated from pdo_table.py)
// is what types them.

import { signal } from "@preact/signals";
import { getState, writeEntity, runOperation as postOperation } from "./client";
import { Seeder } from "./seed";
import { DeviceSocket } from "./ws";
import type { SeedAction } from "./seed";
import type { ConnectionStatus, StateValue } from "./ws";

/**
 * Every entity the device has reported, by key.
 *
 * A key absent from this map has not been heard of; a key present with `null` exists and
 * has no value (see StateValue). The two are different and screens should render them
 * differently -- "unknown entity" is a bug, "--" is a normal reading.
 */
export const deviceState = signal<Record<string, StateValue>>({});

export const connection = signal<ConnectionStatus>("closed");

/**
 * True once a full state frame has arrived on the current connection.
 *
 * Screens need this to tell "the device says nothing is set" from "we have not been told
 * yet", which look identical if you only look at deviceState.
 */
export const stateReceived = signal(false);

// The ordering brain for "where does the first full state come from". The socket's own
// full-frame-on-connect misses a page reload -- the device detects a new client by its
// socket COUNT growing, and a reload keeps it flat (see seed.ts) -- so every open is
// followed by a GET /api/state, and the Seeder decides what may reach the screen when.
const seeder = new Seeder();

function apply(action: SeedAction): void {
  switch (action.kind) {
    case "replace":
      // Replace, do not merge: a full document is the whole truth, and merging would keep
      // an entity alive that the device has stopped reporting.
      deviceState.value = { ...action.values };
      stateReceived.value = true;
      break;

    case "merge":
      // A new object every time, because signals compare by reference and mutating the
      // existing one would update nothing on screen.
      deviceState.value = { ...deviceState.value, ...action.values };
      break;

    case "none":
      break;
  }
}

const socket = new DeviceSocket({
  onStatus(status) {
    connection.value = status;
    // The next seed re-sends everything, so what we hold is provisional the moment
    // the socket drops. The values stay on screen deliberately -- blanking the UI on a
    // brief reconnect is worse than showing readings a few seconds old, and `connection`
    // is what tells the user which they are looking at.
    if (status !== "open") {
      stateReceived.value = false;
      return;
    }
    // The token pins this fetch to this connection: if the socket drops and reopens while
    // the request is in flight, its answer is stale and the Seeder will refuse it.
    const token = seeder.reset();
    getState().then(
      (values) => apply(seeder.snapshot(token, values)),
      () => apply(seeder.snapshotFailed(token)),
    );
  },

  onFrame(frame) {
    switch (frame.type) {
      case "state":
        apply(seeder.full(frame.values));
        break;

      case "delta":
        apply(seeder.delta(frame.values));
        break;

      case "ping":
        // Nothing to do: receiving it is the point. DeviceSocket has already restarted
        // its silence timer by the time this runs.
        break;
    }
  },
});

/** Opens the connection. Called once at app start; safe to call again. */
export function connectDevice(): void {
  socket.start();
}

/**
 * Writes one entity.
 *
 * Over REST, not over the socket. The socket is push-only: a command arriving there would
 * be an endpoint the REST API does not have, and that is precisely the privileged path
 * AGENTS.md criterion 4 forbids. It also means a command is not silently queued behind a
 * reconnect -- a boost replayed a minute later is a command the user no longer wants.
 *
 * Rejects with ApiError (docs/interfaces/rest-api.md lists the status codes).
 */
export function setEntity(key: string, value: StateValue): Promise<void> {
  return writeEntity(key, value);
}

/**
 * Runs one of the six parameterised operations (boost, set_bypass, set_balance_mode,
 * set_ventilation_level, set_temperature_profile, set_away).
 *
 * Mode and duration go in one call because these modes are temporary on the unit and PDOs
 * 81/86/87 are their countdowns; the API (docs/interfaces/rest-api.md,
 * POST /api/ops) makes that atomicity part of the contract. DO NOT add a helper that sets a mode and its duration
 * separately -- that is the split the contract exists to prevent.
 */
export function runOperation(
  name: string,
  params?: Record<string, unknown>,
): Promise<void> {
  return postOperation(name, params);
}
