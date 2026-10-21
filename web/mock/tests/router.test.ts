// web/mock/tests/router.test.ts
// The dispatcher's status codes and error bodies, against comfoair_http's handlers and the
// access policy in comfoair_http_policy.c.
//
//   node mock/tests/router.test.ts
import { eq, ok, report } from "../../src/pages/settings/tests/harness.ts";
import { handle, foldPath } from "../router.ts";
import { UNPARSABLE } from "../model.ts";
import { LOCKED_PASSWORD } from "../scenarios.ts";
import { basic, errorOf, fresh, get, post } from "./req.ts";
import { REAL_PREFIX } from "../../src/pages/update/tests/fixture.ts";

// --- every GET route answers on a healthy, unlocked device ---------------------------------
{
  const s = fresh();
  for (const p of ["/api/state", "/api/entities", "/api/entities/supply_air_temp", "/api/log", "/api/status", "/api/config", "/api/wifi/scan"]) {
    eq(get(s, p).status, 200, `GET ${p} is 200`);
  }
  ok(Array.isArray(get(s, "/api/entities").body), "the entity list is a top-level JSON array");
  ok(Array.isArray(get(s, "/api/log").body), "the log is a top-level JSON array of lines");
  ok(Array.isArray(get(s, "/api/wifi/scan").body), "the scan is a top-level JSON array");
  const one = get(s, "/api/entities/supply_air_temp").body as Record<string, unknown>;
  eq(Object.keys(one), ["key", "name", "unit", "value"], "one entity is {key,name,unit,value}");

  const missing = get(s, "/api/entities/nope");
  eq([missing.status, errorOf(missing)], [404, "unknown entity"], "unknown entity is 404 unknown entity");
  const nowhere = get(s, "/api/nothing");
  eq([nowhere.status, errorOf(nowhere)], [404, "no such endpoint"], "unknown /api path is 404 no such endpoint");
  eq(get(s, "/api/provision").status, 404, "there is no GET /api/provision route (status is /api/status)");
  const ws = get(s, "/ws");
  eq([ws.status, errorOf(ws)], [426, "websocket upgrade required"], "a plain GET /ws is 426");
  eq(get(s, "/api/config/").status, 200, "a trailing slash names the same resource");
  eq(foldPath("/"), "/", "the root keeps its slash");
}

// --- writes on a provisioned device without a password ---------------------------------
{
  const s = fresh();
  eq(post(s, "/api/entities/bypass_mode", { value: "Open" }).status, 200, "a command is a bootstrap write");
  eq(post(s, "/api/ops/boost", { duration: 600 }).status, 200, "an operation is a bootstrap write");
  eq(post(s, "/api/config", { device_name: "Hall" }).status, 200, "the settings are a bootstrap write");
  const prov = post(s, "/api/provision", { wifi_ssid: "x" });
  eq([prov.status, errorOf(prov)], [403, "forbidden"], "re-homing a claimed device with no password is 403");
  eq(post(s, "/api/state", { a: 1 }).status, 403, "any other POST fails closed: 403");
  eq(post(s, "/api/entities", { value: 1 }).status, 403, "the bare /api/entities stem is not a command");
  const put = handle({ method: "PUT", path: "/api/config", headers: {}, body: {}, rawLength: 2 }, s);
  eq([put.status, errorOf(put)], [403, "forbidden"], "PUT is refused in every state");
  eq(handle({ method: "DELETE", path: "/api/state", headers: {}, body: undefined, rawLength: 0 }, s).status, 403, "DELETE is refused");
}

// --- body gates: read_body() and the JSON-object check -----------------------------------
{
  const s = fresh();
  const empty = post(s, "/api/entities/bypass_mode");
  eq([empty.status, errorOf(empty)], [413, "body too large"], "NO body is 413 body too large, as read_body answers");
  const huge = handle({ method: "POST", path: "/api/config", headers: {}, body: {}, rawLength: 2048 }, s);
  eq(huge.status, 413, "a 2048-byte body is 413");
  const junk = handle({ method: "POST", path: "/api/entities/bypass_mode", headers: {}, body: UNPARSABLE, rawLength: 5 }, s);
  eq([junk.status, errorOf(junk)], [400, "The request body is not a JSON object this device can read."], "unparsable JSON is 400 with the wire sentence");
  eq(post(s, "/api/config", [1, 2]).status, 400, "a JSON array is not an object: 400");
  eq(post(s, "/api/ops/boost", "x").status, 400, "a JSON string is not an object: 400");
}

// --- a password draws the boundary ----------------------------------------------------
{
  const s = fresh("locked");
  const locked = get(s, "/api/state");
  eq([locked.status, errorOf(locked)], [401, "unauthorized"], "locked GET is 401 unauthorized");
  eq(locked.headers?.["WWW-Authenticate"], 'Basic realm="comfoair"', "the 401 carries the device's realm");
  eq(get(s, "/api/state", basic("anyone", LOCKED_PASSWORD)).status, 200, "the user name is ignored; the password is what counts");
  eq(get(s, "/api/state", basic("admin", "wrong-password")).status, 401, "a wrong password is 401");
  eq(get(s, "/api/state", { authorization: "Bearer x" }).status, 401, "a non-Basic scheme is 401");
  eq(post(s, "/api/entities/bypass_mode", { value: "Open" }).status, 401, "a command without the password is 401");
  eq(post(s, "/api/entities/bypass_mode", { value: "Open" }, basic("", LOCKED_PASSWORD)).status, 200, "with it, 200");
  const gate = post(s, "/api/whatever", { a: 1 }, basic("", LOCKED_PASSWORD));
  eq([gate.status, errorOf(gate)], [501, "not implemented yet"], "an authorised POST with no handler is 501 from the write gate");
  eq(post(s, "/api/provision", { wifi_ssid: "x" }, basic("", LOCKED_PASSWORD)).status, 200, "an owner may re-home their device");
}

// --- an unprovisioned device takes one write -----------------------------------------
{
  const s = fresh("unprovisioned");
  eq(get(s, "/api/config").status, 200, "reading is open on the setup access point");
  eq(post(s, "/api/config", { device_name: "x" }).status, 403, "settings are refused until the device is claimed");
  eq(post(s, "/api/entities/bypass_mode", { value: "Open" }).status, 403, "so are commands");
  eq(post(s, "/api/provision", { wifi_ssid: "home", wifi_psk: "correct-horse" }).status, 200, "a network to join is accepted");
}

// --- the device cannot be reached; the control channel still can --------------------------
{
  const s = fresh("offline");
  const r = get(s, "/api/state");
  eq([r.status, r.drop], [0, true], "offline drops the connection instead of answering");
  eq(post(s, "/api/entities/bypass_mode", { value: "Open" }).drop, true, "writes are dropped too");
  eq(get(s, "/__mock/scenario").status, 200, "the mock control channel ignores offline");
  eq(post(s, "/__mock/scenario", { preset: "heating" }).status, 200, "and can bring the device back");
  eq(get(s, "/api/state").status, 200, "after which it answers again");
  eq(get(s, "/__mock/nope").status, 404, "an unknown mock route is 404");
}

// --- POST /api/ota goes through the access policy (mock/tests/ota.test.ts has the route) -------
{
  const image = (headers: Record<string, string> = {}) => ({
    method: "POST", path: "/api/ota", headers, body: REAL_PREFIX, rawLength: 1 << 20,
  });
  const open = handle(image(), fresh());
  eq([open.status, errorOf(open)], [403, "forbidden"], "no password: OTA is refused outright (403)");
  const lk = fresh("locked");
  eq(handle(image(), lk).status, 401, "a password is set: OTA wants the login (401)");
  eq(handle(image(basic("", LOCKED_PASSWORD)), lk).status, 200, "and takes the image with it");
  eq(handle(image(basic("", LOCKED_PASSWORD)), fresh("unprovisioned")).status, 403, "an unprovisioned device takes no image");
  eq(post(fresh("locked"), "/update", {}, basic("", LOCKED_PASSWORD)).status, 501, "the old POST /update is no route at all");
}

report("mock/router");
