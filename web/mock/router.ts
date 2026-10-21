// web/mock/router.ts
// The one pure dispatcher: a parsed MockRequest and the mutable MockState in, a MockResponse out.
// plugin.ts adapts Node's req/res to it; the tests call it directly.
//
// Order is comfoair_http's: the path is folded (a trailing slash names the same resource), every
// route goes through the access policy first (allowed()), and only then is its handler reached.
// There is no CSRF/content-type gate -- the firmware has none, and a mock that invented one would
// let the SPA depend on a refusal the device never makes. The DNS-rebinding and captive-portal
// paths are not modelled: they concern a phone on the setup access point, not the SPA.
import { HTTP, error } from "./wire.ts";
import { authenticated, checkPolicy, WWW_AUTHENTICATE } from "./auth.ts";
import { renderEntities, renderEntity, renderState } from "./entities.ts";
import { writeEntity, runOperation } from "./commands.ts";
import { getConfig, postConfig } from "./config.ts";
import { getLog, getStatus, provision, wifiScan } from "./network.ts";
import { applyScenario, readScenario } from "./scenarios.ts";
import { getProbe, postProbe } from "./probe.ts";
import { getSystem } from "./system.ts";
import { postOta } from "./ota.ts";
import type { MockRequest, MockResponse, MockState } from "./model.ts";

/** comfoair_http_request_path(): a trailing slash on anything but the root is the same resource. */
export function foldPath(path: string): string {
  return path.length > 1 && path.endsWith("/") ? path.slice(0, -1) : path;
}

const ENTITY = "/api/entities/";
const OPS = "/api/ops/";

function getRoute(s: MockState, path: string): MockResponse {
  if (path === "/api/state") return renderState(s);
  if (path === "/api/entities") return renderEntities();
  if (path.startsWith(ENTITY)) return renderEntity(s, decodeURIComponent(path.slice(ENTITY.length)));
  if (path === "/api/log") return getLog(s);
  if (path === "/api/status") return getStatus(s);
  if (path === "/api/config") return getConfig(s);
  if (path === "/api/wifi/scan") return wifiScan(s);
  if (path === "/api/probe") return getProbe(s);
  if (path === "/api/system") return getSystem(s);
  // comfoair_http_ws_get() for a GET with no Upgrade header: it is not a WebSocket client.
  if (path === "/ws") return error(426, HTTP.upgradeRequired);
  return error(404, HTTP.noEndpoint);
}

function postRoute(s: MockState, req: MockRequest, path: string): MockResponse {
  const { rawLength, body } = req;
  if (path === "/api/config") return postConfig(s, rawLength, body);
  if (path === "/api/provision") return provision(s, rawLength, body);
  if (path === "/api/probe") return postProbe(s, rawLength, body);
  if (path === "/api/ota") return postOta(s, rawLength, body);
  if (path.startsWith(ENTITY) && path.length > ENTITY.length) {
    return writeEntity(s, decodeURIComponent(path.slice(ENTITY.length)), rawLength, body);
  }
  if (path.startsWith(OPS) && path.length > OPS.length) {
    return runOperation(s, decodeURIComponent(path.slice(OPS.length)), rawLength, body);
  }
  // comfoair_http_write_gate(): the policy let it through and nothing is behind it. 501 says so
  // honestly.
  return error(501, HTTP.notImplemented);
}

function unauthorized(): MockResponse {
  return { status: 401, headers: { "WWW-Authenticate": WWW_AUTHENTICATE }, body: { error: HTTP.unauthorized } };
}

export function handle(req: MockRequest, s: MockState): MockResponse {
  const path = foldPath(req.path);
  const method = req.method.toUpperCase();

  // Mock-only control channel, routed BEFORE the device's gates on purpose: it must be able to
  // switch a locked, unprovisioned or offline device back, which is the state it exists to set.
  if (path.startsWith("/__mock/")) {
    if (path === "/__mock/scenario" && method === "GET") return readScenario(s);
    if (path === "/__mock/scenario" && method === "POST") return applyScenario(s, req.body);
    return { status: 404, body: { error: "no such mock route" } };
  }

  // Unreachable: nothing answers at all. The browser's fetch rejects, which is the
  // "nothing reached the device" path in pages/settings/errors.ts.
  // Rebooting after an accepted image (ota.ts) looks the same from outside: nobody answers.
  if (s.scenario.offline || s.rebootLeftMs > 0) return { status: 0, drop: true };

  const auth = authenticated(req.headers["authorization"], s.config.ui_password);

  const verdict = checkPolicy(method, path, {
    provisioned: s.scenario.provisioned, passwordSet: s.config.ui_password !== "", authenticated: auth,
  });
  if (verdict === 401) return unauthorized();
  if (verdict === 403) return error(403, HTTP.forbidden);

  if (method === "GET") return getRoute(s, path);
  return postRoute(s, req, path);
}
