// The push transport: one socket to the device, reconnecting on its own.
//
// One endpoint -- `/ws` -- sends the full state on connect and deltas afterwards
// (docs/interfaces/websocket.md). The full state on connect is what makes a reconnect
// trivial: the client never catches up on what it missed, it is handed what is true now.
// Polling every entity over REST was the alternative and it is what this file exists to avoid.
//
// This file knows about sockets and nothing about ventilation. Entity keys are opaque
// strings on purpose: AGENTS.md forbids a second entity list, and giving the payload a
// hand-written shape here would be exactly that. `api/device.ts` holds the values, and
// `api/entities.ts` -- generated from pdo_table.py -- types them.

/**
 * One entity value on the wire, mirroring `comfoair_value_t`
 * (components/comfoair_state/include/comfoair_state.h).
 *
 * `null` is COMFOAIR_VALUE_NONE: the entity exists and has no value to show -- not
 * reported yet, or reported as "nothing pending" on a countdown row. DO NOT treat it as
 * "delete this key": an entity that drops out of the model is a bug, not a message, and
 * deleting the key would make the UI render a gap where it should render "--".
 */
export type StateValue = number | boolean | string | null;

/**
 * Frames from the device.
 *
 * `state` and `delta` carry the same shape -- the body of `GET /api/state` -- and differ
 * only in completeness, so the client has one merge path for both. `state` arrives once
 * per connection and replaces everything; `delta` carries only what changed.
 *
 * `ping` exists so silence is distinguishable from a dead socket; see SILENCE_TIMEOUT_MS.
 */
export type InboundFrame =
  | { type: "state"; values: Record<string, StateValue> }
  | { type: "delta"; values: Record<string, StateValue> }
  | { type: "ping" };

// There is no outbound frame type, and that is the contract rather than an omission: this
// socket only ever receives. Commands go over REST, because a command accepted here would
// be an endpoint the REST API does not have -- the privileged path AGENTS.md criterion 4 forbids.
// See setEntity/runOperation in api/device.ts.

export type ConnectionStatus = "connecting" | "open" | "closed";

const WS_PATH = "/ws";

// No handshake ticket yet, and the omission is deliberate rather than forgotten.
//
// `new WebSocket()` cannot set headers, so once the device has a UI password there will be
// no way to attach it to the handshake the way every other request does -- a short-lived
// ticket fetched over the authenticated REST API and passed as a query parameter is the
// usual answer. Until a password exists there is nothing to prove, and asking for a ticket
// the firmware does not serve meant a 404 on every reconnect. Phase 5 brings both halves
// at once; see the note in components/comfoair_http/comfoair_http_ws.c.

const BACKOFF_MIN_MS = 1_000;
const BACKOFF_MAX_MS = 30_000;

// The device is contracted to send something at least every 15 s -- a delta if anything
// changed, a `ping` if not. A socket that goes quiet for longer than this is treated as
// dead and reopened.
//
// This exists because a TCP connection dropped by a NAT box or a sleeping AP stays "open"
// to the browser indefinitely: no `close` event fires and the UI silently shows stale
// readings. DO NOT lower this below roughly twice the device's keepalive interval, or a
// single late frame will cause a reconnect loop.
const SILENCE_TIMEOUT_MS = 35_000;

function socketUrl(): string {
  const url = new URL(WS_PATH, location.href);
  url.protocol = url.protocol === "https:" ? "wss:" : "ws:";
  return url.toString();
}

/**
 * True for anything shaped like a frame we could act on.
 *
 * JSON.parse succeeding proves nothing: `"hello"`, `42` and `{}` all parse cleanly and
 * none of them is a frame. Without this check the InboundFrame type is a claim the
 * runtime does not keep, and a consumer that trusts `frame.values` reads undefined.
 */
function isInboundFrame(value: unknown): value is InboundFrame {
  if (typeof value !== "object" || value === null) return false;
  const type = (value as { type?: unknown }).type;
  if (type === "ping") return true;
  if (type !== "state" && type !== "delta") return false;
  const values = (value as { values?: unknown }).values;
  return typeof values === "object" && values !== null;
}

export interface DeviceSocketHandlers {
  onFrame: (frame: InboundFrame) => void;
  onStatus: (status: ConnectionStatus) => void;
}

/**
 * A self-healing connection to `/ws`.
 *
 * Owns exactly one socket at a time and reopens it with exponential backoff. Callers do
 * not reconnect, retry or inspect the socket -- they call start() once and read frames.
 */
export class DeviceSocket {
  private handlers: DeviceSocketHandlers;
  private socket: WebSocket | null = null;
  private retryMs = BACKOFF_MIN_MS;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private silenceTimer: ReturnType<typeof setTimeout> | null = null;
  private stopped = true;
  // connect() is synchronous today (no ticket request), but `socket` is assigned only after
  // onStatus("connecting") has run, and the ticket fetch above would widen that gap to a whole
  // request -- a second connect() in it would see null and open a parallel socket. This covers it.
  private connecting = false;

  constructor(handlers: DeviceSocketHandlers) {
    this.handlers = handlers;
  }

  start(): void {
    if (!this.stopped) return;
    this.stopped = false;
    document.addEventListener("visibilitychange", this.onVisibilityChange);
    void this.connect();
  }

  stop(): void {
    this.stopped = true;
    document.removeEventListener("visibilitychange", this.onVisibilityChange);
    this.clearTimers();
    // Detach before closing: the handler would otherwise schedule a reconnect for a
    // socket we are deliberately shutting down.
    const socket = this.socket;
    this.socket = null;
    if (socket) {
      socket.onclose = null;
      socket.close();
    }
    this.handlers.onStatus("closed");
  }

  private connect(): void {
    if (this.stopped || this.socket || this.connecting) return;
    this.connecting = true;
    this.handlers.onStatus("connecting");
    try {
      this.openSocket();
    } finally {
      this.connecting = false;
    }
  }

  private openSocket(): void {
    const socket = new WebSocket(socketUrl());
    this.socket = socket;

    socket.onopen = () => {
      this.retryMs = BACKOFF_MIN_MS;
      this.handlers.onStatus("open");
      this.armSilenceTimer();
    };

    socket.onmessage = (event: MessageEvent<string>) => {
      this.armSilenceTimer();
      let parsed: unknown;
      try {
        parsed = JSON.parse(event.data);
      } catch {
        return;
      }
      // A malformed or unrecognised frame is the device's bug, and dropping it is right:
      // the next full state on reconnect repairs whatever this one would have carried.
      // The silence timer was already restarted above, so a device emitting only garbage
      // still counts as alive -- it is reachable, just wrong, and reconnecting in a loop
      // would not improve it.
      if (!isInboundFrame(parsed)) return;
      this.handlers.onFrame(parsed);
    };

    socket.onclose = () => {
      this.socket = null;
      this.clearTimers();
      this.handlers.onStatus("closed");
      this.scheduleReconnect();
    };

    // `error` is always followed by `close`, so reconnecting is left to onclose alone --
    // handling both would double the backoff on every failure.
    socket.onerror = () => {};
  }

  private scheduleReconnect(): void {
    if (this.stopped || this.reconnectTimer) return;
    // Jitter keeps several tabs from reconnecting in lockstep, which matters here more
    // than usual: the device serves seven sockets in total, shared with every ordinary HTTP
    // request (max_open_sockets in comfoair_http.c; docs/interfaces/websocket.md).
    const delay = this.retryMs * (0.5 + Math.random() * 0.5);
    this.retryMs = Math.min(this.retryMs * 2, BACKOFF_MAX_MS);
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      void this.connect();
    }, delay);
  }

  private armSilenceTimer(): void {
    if (this.silenceTimer) clearTimeout(this.silenceTimer);
    this.silenceTimer = setTimeout(() => {
      // close() rather than reconnecting here: onclose is the single place that reopens,
      // and routing through it keeps the backoff and status reporting in one path.
      this.socket?.close();
    }, SILENCE_TIMEOUT_MS);
  }

  private clearTimers(): void {
    if (this.silenceTimer) clearTimeout(this.silenceTimer);
    if (this.reconnectTimer) clearTimeout(this.reconnectTimer);
    this.silenceTimer = null;
    this.reconnectTimer = null;
  }

  // A laptop that wakes from sleep finds a dead socket and, with backoff already at 30 s,
  // would leave the user looking at stale readings for half a minute. Coming back to the
  // tab is a strong signal that someone is about to read it, so the wait is reset.
  private onVisibilityChange = (): void => {
    if (this.stopped || document.hidden || this.socket || this.connecting) return;
    this.retryMs = BACKOFF_MIN_MS;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    void this.connect();
  };
}
