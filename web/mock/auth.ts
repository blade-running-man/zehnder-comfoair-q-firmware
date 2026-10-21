// web/mock/auth.ts
// The access policy, a pure mirror of comfoair_http_check() (components/comfoair_policy) plus the
// Basic-auth check in front of it (comfoair_auth).
//
// 401 means a credential could satisfy the request (and carries WWW-Authenticate, so the browser
// shows its login box); 403 means nothing can in the device's current state. The SPA's error
// copy (pages/settings/errors.ts) tells the two apart, so the mock must not blur them.
export interface PolicyCtx { provisioned: boolean; passwordSet: boolean; authenticated: boolean }
export type Verdict = "allow" | 401 | 403;

// A path UNDER a prefix with something after it -- "/api/entities-x" and the bare stem do not
// inherit the rule (path_under in comfoair_http_policy.c).
function under(path: string, prefix: string): boolean {
  return path.startsWith(prefix) && path.length > prefix.length;
}

/** Changing the ventilation, as distinct from changing the device. */
export function isCommand(path: string): boolean {
  return under(path, "/api/entities/") || under(path, "/api/ops/");
}

export function checkPolicy(method: string, path: string, ctx: PolicyCtx): Verdict {
  if (method === "GET") {
    // Reading is open until the owner draws a boundary.
    if (!ctx.passwordSet) return "allow";
    return ctx.authenticated ? "allow" : 401;
  }
  // Every non-POST write method is refused outright, in every state.
  if (method !== "POST") return 403;

  if (!ctx.provisioned) {
    // The one write an unclaimed device must take is a network to join -- and once a password
    // exists, re-homing it to another network is the owner's to authorise.
    if (path !== "/api/provision") return 403;
    if (ctx.passwordSet) return ctx.authenticated ? "allow" : 401;
    return "allow";
  }

  if (!ctx.passwordSet) {
    // Setting the first password, and controlling the ventilation, are the two writes a device
    // without a password accepts. Everything else -- an OTA image above all -- fails CLOSED.
    // The probe is a READ-ONLY walk of the unit's settings, and shares the commands' exemption
    // (path_is_the_probe in comfoair_http_policy.c): whoever may change the fan speed may see how
    // the unit is set up. EXACT path, as there: nothing under /api/probe/ inherits it.
    if (path === "/api/config" || isCommand(path) || path === "/api/probe") return "allow";
    return 403;
  }
  return ctx.authenticated ? "allow" : 401;
}

/**
 * True when `header` carries the stored password.
 *
 * The user name is DISCARDED: the device has one password and no accounts, so whatever the owner
 * types into the browser's name box is ignored, as comfoair_auth does. A device with no password
 * authenticates nobody -- an empty guess is not a match for an empty record.
 */
export function authenticated(header: string | undefined, password: string): boolean {
  if (password === "" || !header || !/^basic /i.test(header)) return false;
  let decoded: string;
  try {
    decoded = atob(header.slice(6).trim());
  } catch {
    return false;
  }
  const colon = decoded.indexOf(":");
  if (colon < 0) return false;
  // atob yields Latin-1 code units; re-read them as the UTF-8 bytes they are.
  const bytes = Uint8Array.from(decoded.slice(colon + 1), (c) => c.charCodeAt(0));
  return new TextDecoder().decode(bytes) === password;
}

/** Header value for a 401, byte for byte comfoair_http_gate.c's. */
export const WWW_AUTHENTICATE = 'Basic realm="comfoair"';
