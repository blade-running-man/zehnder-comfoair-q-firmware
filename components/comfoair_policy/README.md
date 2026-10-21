# comfoair_policy — the HTTP access rule: who may read or write which path, in which device state

> This is documentation, not code — re-read the source before relying on a detail.

The one decision every HTTP handler asks before doing anything: given the method, the path and
three facts about the device (on the owner's network? password set? request authenticated?),
answer allow / 401 / 403. It is a pure function with no HTTP or ESP-IDF types in it, so the
security rule is exercised by a host suite instead of being discoverable only on a device in the
field. The rule in one sentence: reading is open until the owner sets a password; writing is
closed until then except for a short, named list of exemptions; and radio proximity to an
**unclaimed** device (open access point) authorises nothing but handing it a network.

## Responsibility

Owns:
- The decision table in `comfoair_http_check()` (`comfoair_http_policy.c`).
- Path matching for the exemptions: exact match (`path_is`) and "strictly under a slash-terminated
  prefix" (`path_under`), written once so no call site re-invents a leaky `strncmp`.
- The list of exempt paths: `/api/provision` (access point), `/api/config`, `/api/entities/<x>`,
  `/api/ops/<x>`, `/api/probe` (no password set).

Does NOT do:
- Extract the path from the request, strip the query, or fold a trailing slash — `comfoair_http`
  (`comfoair_http_request_path()` in `comfoair_http_gate.c`) does, before calling in.
- Parse `Authorization`, compare passwords, or impose the wrong-password delay — `comfoair_auth`
  parses and `comfoair_net` verifies; `comfoair_http_allowed()` sleeps and maps the result.
- Decide what "provisioned" or "password set" means — `comfoair_net_is_provisioned()` /
  `comfoair_net_password_set()` (backed by `comfoair_provision` / `comfoair_config`) answer; this
  component only consumes the booleans.
- Send the 401/403 response bodies or `WWW-Authenticate` — `comfoair_http_allowed()` does.
- Gate MQTT. Only HTTP requests reach this function; MQTT commands never pass through it.

## Public API

Header: `include/comfoair_http_policy.h` (re-exported by `comfoair_http.h`).

| Symbol | Contract |
| --- | --- |
| `comfoair_http_method_t` | `COMFOAIR_HTTP_GET`, `COMFOAIR_HTTP_POST`, `COMFOAIR_HTTP_OTHER`. Every other verb (PUT, DELETE, PATCH, unparsed) is `OTHER` and is always refused — never mapped onto the nearest known verb. |
| `comfoair_http_ctx_t` | By-value struct of three `bool`s: `provisioned` (on the owner's network; false = serving its own AP, where anyone in range is "the client"), `password_set`, `authenticated`. The caller must set `authenticated` true **only** for `COMFOAIR_AUTH_GRANTED`, never for "no password" — see Notes. |
| `comfoair_http_decision_t` | `COMFOAIR_HTTP_ALLOW`; `COMFOAIR_HTTP_UNAUTHORIZED` (401: a password exists and this request did not present it); `COMFOAIR_HTTP_FORBIDDEN` (403: no credential could make this request acceptable in this state). |
| `comfoair_http_check(method, path, ctx)` | Pure, reentrant, no allocation, no locks, callable from any task. `path` is a NUL-terminated, already-normalised path (no query, no trailing slash except `/`); it is borrowed, not retained. `NULL` path → `FORBIDDEN`. Returns a decision, not an `esp_err_t`; the caller must treat any value other than `ALLOW` as a refusal (the real caller's `switch` falls through to 403 on `default`). |

Why 401 and 403 are distinct: telling a client "authenticate" when no password exists yet sends it
round a loop it cannot leave. 401 is returned only when a credential would actually help.

## Implementation

Files:
- `comfoair_http_policy.c` — the decision function and its three static path helpers.
- `include/comfoair_http_policy.h` — the types and the one entry point.
- `CMakeLists.txt` — `SRCS comfoair_http_policy.c`, `INCLUDE_DIRS include`, no `REQUIRES`.

Decision order in `comfoair_http_check()` (the order *is* the rule — several properties fall out
of it rather than being stated anywhere):

1. `path == NULL` → `FORBIDDEN`.
2. `GET` → `ALLOW` if no password is set, else `ALLOW` if authenticated, else `UNAUTHORIZED`.
   Provisioning state does not matter for reads (the setup page on the AP must be reachable).
3. Anything not `POST` → `FORBIDDEN`.
4. Not provisioned (AP up): only exactly `/api/provision`; any other path `FORBIDDEN` regardless
   of credentials. On `/api/provision`: password set → needs authentication (401 otherwise);
   no password → `ALLOW`.
5. Provisioned, no password: `/api/config`, `path_is_a_command()` (`/api/entities/<x>`,
   `/api/ops/<x>`) and `path_is_the_probe()` (`/api/probe` exactly) → `ALLOW`; everything else
   (notably `/api/ota`, and `/api/provision`) → `FORBIDDEN`.
6. Provisioned, password set → `ALLOW` if authenticated, else `UNAUTHORIZED`.

Helpers:
- `path_is(path, expected)` — `strcmp` equality, NULL-safe.
- `path_under(path, prefix)` — true only if `prefix` ends in `/`, `path` starts with it, and at
  least one character follows. So `/api/entities`, `/api/entities/` and `/api/entities-secret`
  are all NOT under `/api/entities/`. No normalisation: `/api/entities/../ota` matches, which is
  safe because `esp_http_server` dispatches on the same raw string to the entity handler, which
  will simply not find such an entity.
- `path_is_a_command(path)`, `path_is_the_probe(path)` — named predicates over the above.

No state, no concurrency concerns.

DO NOT:
- Replace `path_is` with a prefix compare, or inline `strncmp` at a call site. `/api/configuration`
  must not inherit `/api/config`'s exemption; `/api/provisioning` must not inherit the AP one.
- Turn `path_is_the_probe()` into `path_under("/api/probe/")` or fold it into
  `path_is_a_command()`. The probe has one route; a prefix would hand the exemption to any future
  route under `/api/probe/`, which might write or erase.
- Relax `path_under`'s trailing-slash or "something follows" requirements.
- Map `OTHER` methods onto GET/POST, or let a 405-ish case become `ALLOW`.
- Let `/api/ota` join the no-password exemptions. Images are unsigned (no secure boot), so an OTA
  endpoint answering before anyone claimed the device is unauthenticated code execution on the LAN.
- Reorder the provisioned check after the password checks: an unclaimed AP must refuse commands
  and the probe even with no password, and that comes only from step 4 preceding step 5.
- Add HTTP or IDF types to this header — that is what would stop the host suite compiling it.

## Dependencies

- Requires: nothing but libc (`string.h`, `stdbool.h`). No `REQUIRES` in `CMakeLists.txt`.
- Depended on by: `comfoair_http` (`REQUIRES comfoair_policy`; `comfoair_http.h` includes the
  header). The single call site is `comfoair_http_allowed()` in
  `components/comfoair_http/comfoair_http_gate.c`, which builds `comfoair_http_ctx_t` from
  `comfoair_net_is_provisioned()`, `comfoair_net_password_set()` and
  `comfoair_auth_check(...).result == COMFOAIR_AUTH_GRANTED`, then maps the decision to 401/403.
  Every handler (`comfoair_http_read.c`, `_config.c`, `_command.c`, `_ws.c`, `_assets.c`,
  `comfoair_http_ota.c`, `comfoair_http_probe.c`, `comfoair_http_system.c`, plus the `/*`
  catch-all `comfoair_http_write_gate` for POST/PUT/DELETE/PATCH)
  calls `comfoair_http_allowed()` first.
- `platformio.ini` `[env:native]` adds `-I components/comfoair_policy/include`.

## Tests

- `test/test_http_policy/test_http_policy.cpp` — the host suite (`pio test -e native -f
  test_http_policy`). Covers: GET open/locked/AP; POST with and without password; the
  `/api/config` bootstrap write; the AP accepting only `/api/provision`, and needing the password
  on an owned (password set, unprovisioned) device; ventilation commands and the probe exempt
  only on a provisioned device without a password; prefix and exact-path traps
  (`/api/configuration`, `/api/provisioning`, `/api/entities`, `/api/entities/`,
  `/api/entities-secret`, `/api/opsimulator`, `/api/probe/x`, `/api/probes`); `OTHER` method; NULL
  path; all four OTA states.
- Other suites (`test_auth`, `test_config`, `test_provision`, `test_captive`, `test_json`) only
  mention this component in comments; they do not call it.

Not covered by any host suite: the glue in `comfoair_http_allowed()` — path extraction and
trailing-slash folding in `comfoair_http_request_path()`, the `GRANTED`-only mapping, the delay, and
the response mapping. `comfoair_http` needs `esp_http_server.h` and is not host-built. Also not
asserted: `/api/ops/` (bare stem with slash) and `/api/entities/../ota`.

## Notes

- **Its own component on purpose.** The native test env builds whatever components a suite
  includes; `comfoair_http` needs `esp_http_server.h`, which does not exist on the host. Keeping
  the rule here, free of HTTP types, is what makes it host-testable.
- **"Unclaimed", not merely "unprovisioned".** A device whose router died also shows an AP and no
  connection, but it holds the owner's data. Once a password exists, re-homing it via
  `/api/provision` needs that password — otherwise a neighbour waits for a power cut and takes the
  device onto their network. Reads on such a device also need the password.
- **`ctx.provisioned` is not "credentials are in NVS".** The caller reports false while an open AP
  is on the air whatever flash holds; otherwise saving credentials would immediately hand
  `POST /api/config` to whoever is still on that AP — the passer-by who supplied them.
- **Why ventilation commands are open without a password.** The UI password is optional and off
  by default, and a device without one is a supported configuration. With only `/api/config`
  exempt, every command would answer 403 out of the box until the owner set a password nothing
  prompted for. The accepted cost: anyone on the household LAN can change the ventilation — which
  anyone at the unit's own panel can already do. Changing the *device* (OTA, provisioning) stays
  closed.
- **Why the probe shares that exemption.** `POST /api/probe` starts a read-only walk of the unit's
  settings; seeing the configuration is no more than changing the fan speed. With a password it
  needs authentication, and on an open AP it is refused — both by the decision order, not by the
  predicate.
- **Why `/api/config` is exempt at all.** Setting the first password is a write; if every write
  needed a password, the device could never be secured. Once a password exists, changing it needs
  the old one.
- **`authenticated` must mean GRANTED only.** `comfoair_auth` also has a "no password" outcome; a
  caller mapping that to `authenticated = true` would make a stranger on the open AP look
  authenticated to this policy.
- The header's opening summary ("writing is refused until they do") omits the config/command/probe
  exemptions; the code and the suite are authoritative.
