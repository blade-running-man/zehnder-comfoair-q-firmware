// web/mock/plugin.ts
// The Vite dev-server adapter for `npm run dev:mock`. It owns one MockState, advances it once a
// second, answers /api/* and /__mock/* through the pure router, and pushes the state
// over /ws with a hand-rolled RFC 6455 handshake (wsframe.ts) -- no new dependency.
//
// apply: "serve" AND vite.config.ts adds it only in mode "mock": the production build never runs
// it, and nothing under mock/ is imported by src/, so none of it can reach the bundle.
import type { Plugin, ViteDevServer } from "vite";
import type { IncomingMessage, ServerResponse } from "node:http";
import type { Socket } from "node:net";
import { initialState, UNPARSABLE, type MockRequest, type MockResponse } from "./model.ts";
import { tick } from "./evolve.ts";
import { handle } from "./router.ts";
import { PREFLIGHT_BYTES } from "./ota.ts";
import { applyScenario } from "./scenarios.ts";
import { stateValues } from "./values.ts";
import { nextFrame, stateFrame, type Values } from "./ws.ts";
import { acceptKey, closeFrame, encodeText, isClose } from "./wsframe.ts";

const TICK_MS = 1000;

interface Client { socket: Socket; last: Values; lastSentMs: number }

// Which requests are the device's. /update is NOT: it is the SPA's own route and must be served
// the shell, or a reload on the Update page would show a JSON error.
function ours(path: string): boolean {
  return path.startsWith("/api/") || path.startsWith("/__mock/") || path === "/ws";
}

function parseBody(path: string, raw: Buffer): unknown {
  if (raw.length === 0) return undefined;
  // The firmware image is raw bytes, not JSON: ota.ts reads its pre-flight bytes and its length only.
  if (path === "/api/ota") return raw.subarray(0, PREFLIGHT_BYTES);
  try {
    return JSON.parse(raw.toString("utf8"));
  } catch {
    return UNPARSABLE;
  }
}

export function mockPlugin(): Plugin {
  let state = initialState();
  const clients = new Set<Client>();

  const closeAll = (): void => {
    for (const c of clients) c.socket.destroy();
    clients.clear();
  };

  return {
    name: "comfoair-mock-device",
    apply: "serve",
    configureServer(server: ViteDevServer) {
      const log = server.config.logger;
      // Always through a preset, so the unit starts SETTLED (fans at speed, the core at its
      // steady temperatures) rather than spinning up in front of the developer.
      const initial = process.env.MOCK_SCENARIO ?? "heating";
      if (applyScenario(state, { preset: initial }).status !== 200) {
        log.warn(`  mock: MOCK_SCENARIO="${initial}" is not a preset; starting with "heating"`);
        applyScenario(state, { preset: "heating" });
      }

      const timer = setInterval(() => {
        state = tick(state, TICK_MS);
        if (state.scenario.offline) { closeAll(); return; }
        const next = stateValues(state);
        const now = Date.now();
        for (const c of clients) {
          const frame = nextFrame(c.last, next, now - c.lastSentMs);
          c.last = next;
          if (frame === null) continue;
          try {
            c.socket.write(encodeText(frame));
            c.lastSentMs = now;
          } catch {
            clients.delete(c);
          }
        }
      }, TICK_MS);
      server.httpServer?.on("close", () => { clearInterval(timer); closeAll(); });

      server.middlewares.use((req: IncomingMessage, res: ServerResponse, next: () => void) => {
        const url = new URL(req.url ?? "/", "http://localhost");
        const method = (req.method ?? "GET").toUpperCase();
        if (!ours(url.pathname)) return next();
        const chunks: Buffer[] = [];
        req.on("data", (c: Buffer) => chunks.push(c));
        req.on("end", () => {
          const raw = Buffer.concat(chunks);
          const headers: Record<string, string> = {};
          for (const [k, v] of Object.entries(req.headers)) headers[k.toLowerCase()] = Array.isArray(v) ? v.join(",") : String(v ?? "");
          const mreq: MockRequest = { method, path: url.pathname, headers, body: parseBody(url.pathname, raw), rawLength: raw.length };
          const r: MockResponse = handle(mreq, state);
          // "offline": no answer at all. destroy() makes the browser's fetch reject with a
          // TypeError, exactly as a device that has gone away does.
          if (r.drop) { req.socket.destroy(); return; }
          res.statusCode = r.status;
          res.setHeader("Content-Type", "application/json");
          res.setHeader("Cache-Control", "no-store");
          for (const [k, v] of Object.entries(r.headers ?? {})) res.setHeader(k, v);
          res.end(JSON.stringify(r.body ?? {}));
          if (r.restart) setTimeout(closeAll, 200);
        });
      });

      server.httpServer?.on("upgrade", (req: IncomingMessage, socket: Socket) => {
        const url = new URL(req.url ?? "/", "http://localhost");
        if (url.pathname !== "/ws") return;  // Vite's own HMR socket handles everything else
        // The firmware does not run the access policy on the handshake (esp_http_server answers
        // it itself), so neither does the mock: a locked device still pushes to any socket.
        if (state.scenario.offline || state.rebootLeftMs > 0) { socket.destroy(); return; }
        const key = req.headers["sec-websocket-key"];
        if (typeof key !== "string") { socket.destroy(); return; }
        socket.write(
          "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
          `Sec-WebSocket-Accept: ${acceptKey(key)}\r\n\r\n`,
        );
        const values = stateValues(state);
        socket.write(encodeText(stateFrame(values)));
        const client: Client = { socket, last: values, lastSentMs: Date.now() };
        clients.add(client);
        const drop = (): void => { clients.delete(client); };
        socket.on("close", drop);
        socket.on("error", drop);
        // Push-only: data frames are dropped unread; a close is answered and the socket ended.
        socket.on("data", (chunk: Buffer) => {
          if (isClose(chunk)) { socket.end(closeFrame()); drop(); }
        });
      });

      log.info(`  ➜  Mock ComfoAir Q on /api and /ws (scenario "${state.scenario.preset}"; see mock/scenarios.md)`);
    },
  };
}
