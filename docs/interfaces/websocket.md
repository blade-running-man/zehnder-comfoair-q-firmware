<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities).
</script>

# WebSocket

`/ws` is the push channel: the device sends the state model to the browser as it changes, so a
page does not have to poll {{ data.total }} entities over REST. It is implemented in
`components/comfoair_http/comfoair_http_ws.c` (`comfoair_http_ws_get`, `ws_send_state`,
`comfoair_http_ws_task`) with the frames
rendered by `comfoair_api_render_push()` (`components/comfoair_api`). The reference client is
`web/src/api/ws.ts`.

## The channel is push-only

Frames go from the device to the client and nothing goes the other way. Every data frame a client
sends is read, drained and dropped; a client frame larger than 128 bytes makes
`httpd_ws_recv_frame()` fail, `comfoair_http_ws_get` returns the error and the server closes the
socket. Commands travel over REST (`POST /api/entities/{key}`, `POST /api/ops/{name}`) — a command
accepted on this socket would be an endpoint the REST API does not have, which is the privileged
path the project forbids (see [Web UI](/interfaces/web-ui)).

## Handshake and authentication

Open the socket with a plain WebSocket upgrade on `GET /ws` — no subprotocol, no query
parameters. The route is registered with `.is_websocket = true` and no subprotocol, and
`CONFIG_HTTPD_WS_SUPPORT=y` is set in `sdkconfig.defaults`:

```js
const ws = new WebSocket(`ws://${location.host}/ws`);
```

`esp_http_server` answers the upgrade itself and does not call the route's handler for it, so
**the access policy is not applied to the handshake**.

::: warning The handshake is not authenticated
With a UI password set, every REST route needs HTTP Basic credentials, but `/ws` still accepts
an upgrade from anyone who can reach the device and pushes the full state to them. The read side of
the state model is therefore not protected by the password on this channel. No handshake ticket is
implemented on either side; `comfoair_http_ws.c` and `web/src/api/ws.ts` both record the gap.
Nothing can be written over the socket.
:::

A plain `GET /ws` **without** an `Upgrade` header does reach the handler: it is checked by the
access policy like any GET (so `401` when a password is set and none was sent) and otherwise
answered `426 {"error":"websocket upgrade required"}`.

```sh
curl -i http://DEVICE/ws
# HTTP/1.1 426 Upgrade Required
# {"error":"websocket upgrade required"}
```

## Frames

Every frame is one JSON text frame with a `type`:

```json
{"type": "state", "values": {"supply_fan_duty": 34, "bypass_mode": "Auto", "fan": {"on": true, "speed": 2, "preset": "Medium"}}}
{"type": "delta", "values": {"supply_fan_duty": 36}}
{"type": "ping"}
```

| `type` | Meaning | Client action |
| --- | --- | --- |
| `state` | The whole state model — the same object as `GET /api/state` | **Replace** the local map |
| `delta` | Only the entities that changed since the last push | **Merge** into the local map |
| `ping` | Keep-alive, nothing changed | Reset the silence timer |

`values` uses the same value spelling as `GET /api/state`: a number, a boolean, a string, or
`null` for an entity that exists but has no reading. `null` is a value, not a deletion. `fan` is
an object `{"on", "speed", "preset"}` or `null`. The keys are the registry's; see
[Entities](/reference/entities).

### When a full state is sent

The device does not keep per-socket bookkeeping — the handshake gives it no hook to start any.
Each push round it asks the server for its open sockets and counts the WebSockets. **When the
count grew since the last round, everybody gets a `state` frame that round**; otherwise everybody
gets a `delta`. A second tab opening therefore costs the first tab one redundant full document.

The web UI does not rely on that alone: when its socket opens it also fetches `GET /api/state`
(`web/src/api/seed.ts` orders the fetch against deltas that arrive meanwhile).

### Cadence

| Rule | Value | Source |
| --- | --- | --- |
| Minimum interval between pushes | 250 ms | `WS_MIN_INTERVAL_MS` |
| Keep-alive when nothing changed | every 15 s | `WS_KEEPALIVE_MS` |
| Client silence timeout (web UI) | 35 s | `SILENCE_TIMEOUT_MS` in `ws.ts` |

State changes from the CAN reader, from REST writes and from MQTT commands all call
`comfoair_http_notify_state_changed()`, which wakes the push task. A burst of PDO frames is
coalesced into one document by the 250 ms floor. An empty delta is never sent; a `ping` goes out
instead once 15 s have passed without any frame. The push itself runs on the HTTP server's own
task via `httpd_queue_work()`, because sockets may only be written from there.

The WebSocket is **not** subject to MQTT's per-row [publication rate](/interfaces/mqtt#publication-rate):
it is a LAN delta with no broker behind it, so every changed value goes out on the next push.

A live WebSocket also counts as client activity for the setup window: a phone resting on the
settings page sends no HTTP requests, and without this the window would close under the person
reading it.

## Reconnecting

The push code never closes a socket. The server can: with `lru_purge_enable`, when all seven
sockets are taken and a new connection arrives, the least-recently-used one is closed, and that
may be a WebSocket. A client should reconnect when the socket
closes **or** when it hears nothing for well over the 15 s keep-alive — a TCP connection dropped
by a NAT box or a sleeping access point can stay "open" in the browser for ever.

`DeviceSocket` in `web/src/api/ws.ts` does this:

- exponential backoff from 1 s to 30 s (`BACKOFF_MIN_MS`, `BACKOFF_MAX_MS`), each wait jittered to
  50–100 % so several tabs do not reconnect in lockstep; the backoff resets when a socket opens;
- a 35 s silence timer re-armed on every message (garbage included), which closes the socket and
  lets the close handler reconnect;
- a tab becoming visible again (`visibilitychange`) while no socket is open resets the backoff and
  reconnects immediately;
- malformed or unrecognised frames are dropped — the next full state repairs whatever they
  carried.

Do not set a silence timeout below about twice the keep-alive interval, or one late frame causes a
reconnect loop.

## Limits

- **Seven sockets in total**, shared between WebSockets and ordinary HTTP requests
  (`max_open_sockets = 7`). Open one socket per client, not one per page: the web UI opens a single
  socket for the life of the page (`connectDevice()` in `web/src/api/device.ts`, called once from
  `web/src/main.tsx`). Least-recently-used sockets are purged when the server is full.
- The seven are **client** sockets. `esp_http_server` uses three more of its own — the listening
  socket and a UDP pair for its internal control messages — and refuses to start unless
  `max_open_sockets + 3` fits `CONFIG_LWIP_MAX_SOCKETS`. That check reserves nothing: the MQTT
  client's broker connection and the captive DNS responder draw on the same lwIP socket table.
  The table is therefore **13**: 7 clients + 3 internal + 1 MQTT + 1 captive DNS + 1 spare
  (`sdkconfig.defaults`; the terms are in `components/comfoair_http/comfoair_http_sockets.h`, and
  a `_Static_assert` beside `cfg.max_open_sockets` fails the build if they stop fitting). A table
  one short makes lwIP refuse the next connection with `error in accept (23)` before the
  least-recently-used purge above can run, because the purge starts only once all seven client
  sockets are taken.
- The device can track up to seven sockets per push round (`MAX_WS_CLIENTS`), so every open
  socket is reached.
- The rendered document shares the 48 KiB scratch buffer with the REST documents.
- Machine consumers that only need the values should prefer [MQTT](/interfaces/mqtt), which has
  its own transport and retained topics.
