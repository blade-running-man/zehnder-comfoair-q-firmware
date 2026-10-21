// web/mock/tests/req.ts
// Request builders shared by the mock's suites. Not a suite itself (no .test.ts), so the runner
// does not execute it. Bodies are measured the way the plugin measures them -- UTF-8 bytes of the
// JSON text -- because the firmware's 413 rule reads the byte count, not the parsed object.
import { handle } from "../router.ts";
import { initialState, type MockResponse, type MockState } from "../model.ts";
import { applyScenario } from "../scenarios.ts";

export function fresh(preset = "heating"): MockState {
  const s = initialState();
  applyScenario(s, { preset });
  return s;
}

export function get(s: MockState, path: string, headers: Record<string, string> = {}): MockResponse {
  return handle({ method: "GET", path, headers, body: undefined, rawLength: 0 }, s);
}

export function post(s: MockState, path: string, body?: unknown, headers: Record<string, string> = {}): MockResponse {
  const text = body === undefined ? "" : JSON.stringify(body);
  return handle({ method: "POST", path, headers, body, rawLength: new TextEncoder().encode(text).length }, s);
}

/** The Authorization header a browser sends after its Basic login box. */
export function basic(user: string, password: string): Record<string, string> {
  return { authorization: "Basic " + Buffer.from(`${user}:${password}`, "utf8").toString("base64") };
}

export function errorOf(r: MockResponse): string | undefined {
  return (r.body as { error?: string } | undefined)?.error;
}
export function fieldOf(r: MockResponse): string | undefined {
  return (r.body as { field?: string } | undefined)?.field;
}

export function state(s: MockState): Record<string, unknown> {
  return get(s, "/api/state").body as Record<string, unknown>;
}
