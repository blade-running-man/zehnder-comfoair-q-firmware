// How the client gets its FIRST full state.
//
// The socket was designed to send it (ws.ts:3-6), but the device can only guess at a new
// client: esp_http_server answers the WebSocket handshake without calling any handler
// (httpd_uri.c:362), so comfoair_http_ws.c infers "somebody new" from the socket count
// GROWING between two pushes. A page reload closes one socket and opens another between
// samples, the count stays flat, and the fresh page receives deltas for a state it never
// saw -- three live values and a hole for every other entity. GET /api/state is the reliable source;
// device.ts fetches it whenever the socket opens.
//
// This module owns the ordering problem that fetch creates: a delta can arrive on the
// socket while the snapshot is still in flight. The delta is newer than the snapshot for
// its keys -- the device rendered it after the connection opened, the snapshot may have
// been rendered before -- so neither "apply deltas as they come" nor "let the snapshot
// replace everything" is right. Deltas are HELD until the snapshot lands, then merged on
// top of it.
//
// Pure by design, like every decision module in this SPA: no fetch, no socket, no
// signals. device.ts wires those and applies the actions returned here.

import type { StateValue } from "./ws";

/**
 * What the caller must do to its state map.
 *
 * `replace` is a full document: assign it, discard everything held before. `merge` is
 * partial: spread it over what is already shown. `none` is "nothing yet" -- either the
 * frame was consumed into the hold buffer, or it belonged to a connection that no longer
 * exists.
 */
export type SeedAction =
  | { kind: "replace"; values: Record<string, StateValue> }
  | { kind: "merge"; values: Record<string, StateValue> }
  | { kind: "none" };

const NONE: SeedAction = { kind: "none" };

export class Seeder {
  // Which reset() the pending snapshot belongs to. A slow fetch from connection N can
  // resolve after connection N+1 opened; its document predates everything on the new
  // socket and must not clobber it. Comparing tokens is how snapshot() tells the two apart.
  private generation = 0;
  private seeded = false;
  private held: Record<string, StateValue> = {};

  /**
   * The socket (re)opened. Returns the token the caller must hand back with the snapshot
   * it is about to fetch -- and only that fetch's outcome, nobody else's.
   */
  reset(): number {
    this.generation += 1;
    this.seeded = false;
    this.held = {};
    return this.generation;
  }

  /** A `delta` frame from the socket. Held until a full state exists to merge it into. */
  delta(values: Record<string, StateValue>): SeedAction {
    if (this.seeded) return { kind: "merge", values };
    this.held = { ...this.held, ...values };
    return NONE;
  }

  /**
   * A `state` frame from the socket -- the device did recognise a new client. The socket
   * is ordered, so this frame was rendered after every held delta and already contains
   * them; merging them back would resurrect older values. They are dropped.
   */
  full(values: Record<string, StateValue>): SeedAction {
    this.seeded = true;
    this.held = {};
    return { kind: "replace", values };
  }

  /**
   * GET /api/state resolved. Held deltas win over the snapshot for their keys -- see the
   * header. Ignored when stale, and when the socket's own full frame got here first: that
   * frame is the device's later word.
   */
  snapshot(token: number, values: Record<string, StateValue>): SeedAction {
    if (token !== this.generation || this.seeded) return NONE;
    this.seeded = true;
    const merged = { ...values, ...this.held };
    this.held = {};
    return { kind: "replace", values: merged };
  }

  /**
   * GET /api/state failed. Degraded, not stuck: the held deltas are released and live
   * ones flow from here on, so the screen shows what truth there is. The holes stay until
   * the device's own full frame or the next reconnect fills them -- a fetch failing while
   * the socket works is rare enough that a retry loop is not worth its states.
   */
  snapshotFailed(token: number): SeedAction {
    if (token !== this.generation || this.seeded) return NONE;
    this.seeded = true;
    const held = this.held;
    this.held = {};
    return { kind: "merge", values: held };
  }
}
