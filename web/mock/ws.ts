// web/mock/ws.ts
// The push frames, exactly comfoair_api_render_push()'s shape, which is what src/api/ws.ts
// accepts (isInboundFrame): {"type":"state","values":{...}} on connect, {"type":"delta",
// "values":{...changed only}} afterwards, and {"type":"ping"} -- no values -- when nothing changed
// for KEEPALIVE_MS. There is no ticket and no outbound frame type: the firmware has neither.
import type { Value } from "./values.ts";

export type Values = Record<string, Value>;

/** WS_KEEPALIVE_MS: the client closes a socket silent for 35 s, so the device speaks every 15. */
export const KEEPALIVE_MS = 15000;

export function stateFrame(values: Values): string {
  return JSON.stringify({ type: "state", values });
}
export function deltaFrame(values: Values): string {
  return JSON.stringify({ type: "delta", values });
}
export function pingFrame(): string {
  return JSON.stringify({ type: "ping" });
}

// Compared by serialisation, not identity: the fan's value is an object rebuilt on every
// projection, and identity would report it changed on every tick.
export function diffValues(prev: Values, next: Values): Values {
  const out: Values = {};
  for (const k of Object.keys(next)) if (JSON.stringify(prev[k]) !== JSON.stringify(next[k])) out[k] = next[k];
  return out;
}

/** One push decision for one socket: a delta when something moved, a ping when the socket has
    been quiet for KEEPALIVE_MS, else nothing (the firmware sends no empty delta). */
export function nextFrame(prev: Values, next: Values, sinceLastMs: number): string | null {
  const d = diffValues(prev, next);
  if (Object.keys(d).length > 0) return deltaFrame(d);
  return sinceLastMs >= KEEPALIVE_MS ? pingFrame() : null;
}
