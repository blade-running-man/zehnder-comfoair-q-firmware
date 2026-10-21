# comfoair_auth — turns an HTTP `Authorization` header into granted / denied / no-password

> This is documentation, not code — re-read the source before relying on a detail.

The web UI and REST API are protected by one optional password sent as HTTP Basic. This
component parses the raw `Authorization` header value and decides the outcome. Its whole
input is attacker-chosen: the setup access point is deliberately open, and the household LAN is
not treated as a trust boundary either. So it copies nothing before the length is known, refuses
rather than truncates, never logs, wipes every plaintext copy it touches, and has no path that
turns "no password is set" into "this password is accepted". It is pure C (no ESP-IDF, no clock,
no allocation, no crypto) so the host suite can feed it hostile headers directly.

## Responsibility

Owns:
- Parsing the `Authorization` header value: scheme `Basic` (whole word, case-insensitive),
  one base64 token, strict base64, split at the FIRST colon, user name discarded.
- Classifying the header (`comfoair_auth_parse_t`) separately from the verdict
  (`comfoair_auth_result_t`).
- The fixed failure delay value (`COMFOAIR_AUTH_FAIL_DELAY_MS`) — it decides it, it does not sleep.
- Wiping the decoded password (`comfoair_auth_forget`, and internally on every refusal).

Does NOT do:
- Store, format or hash the password — the record `1$<iterations>$<salt hex>$<digest hex>`
  (PBKDF2-HMAC-SHA256) belongs to `comfoair_config`; comparison is an injected verifier.
- Send any HTTP response, 401 or `WWW-Authenticate` — `comfoair_http` does, and the access
  decision itself is `comfoair_http_check()` (the policy in `comfoair_http`'s policy layer).
- Sleep — the caller (`comfoair_http_allowed()` in `components/comfoair_http/comfoair_http_gate.c`)
  applies `delay_ms` with `vTaskDelay`.
- Lockout / failure counting — deliberately absent (see Notes).
- Logging — nothing here may log (see Notes).

## Public API

All in `include/comfoair_auth.h`. All functions are synchronous, reentrant, hold no state, and
may be called from any task (in practice the single esp_http_server task). None returns `esp_err_t`.

| Symbol | Contract |
| --- | --- |
| `comfoair_auth_parse(header, out)` | `header` is the raw field value (no name, no colon); NULL allowed. `out` is zeroed first, so a refusal never leaves a previous password in it; on any non-`OK` result the password buffer is wiped. `out == NULL` returns `MALFORMED`. On `OK`, `out->password` is NUL-terminated and the caller must `comfoair_auth_forget()` it. |
| `comfoair_auth_forget(creds)` | Volatile-pointer wipe of the password buffer (a plain `memset` could be removed as a dead store). NULL-safe. |
| `comfoair_auth_check(header, stored, verify)` | The one-call decision. `stored` NULL/empty → `NO_PASSWORD` without decoding the header at all. Otherwise parses, then `GRANTED` only if parse is `OK`, the candidate is non-empty, `verify != NULL`, and `verify(stored, candidate)` is true. Wipes its local credential before returning. Returns `comfoair_auth_outcome_t` by value. |
| `comfoair_auth_device_verifier()` | Device-only (`#ifdef ESP_PLATFORM` in `comfoair_auth_device.c`): returns a verifier binding `comfoair_config_check_ui_password_against()` to `comfoair_config_device_kdf()`. Off-device it does not exist — calling it from host code is a link error by design, not a stub that says "no". |
| `comfoair_auth_verifier_t` | `bool (*)(const char *stored, const char *candidate)`. |
| `comfoair_auth_credentials_t` | `char password[COMFOAIR_AUTH_PASS_MAX + 1]` only — no user name field: the device has no accounts. |
| `comfoair_auth_outcome_t` | `{ result, delay_ms, header }`. `delay_ms` is `COMFOAIR_AUTH_FAIL_DELAY_MS` after a presented-and-failed attempt, else 0. `header` is the parse status, safe to log; it is `ABSENT` whenever `result == NO_PASSWORD` regardless of the header. |

Enums:
- `comfoair_auth_parse_t`: `OK`, `ABSENT` (NULL, empty or all-whitespace — what a browser sends
  before a challenge), `NOT_BASIC` (other scheme, or e.g. `Basicx`), `MALFORMED` (bad base64,
  no colon, NUL byte, second token, empty token after `Basic`), `TOO_LONG` (token > 512 chars, or
  password > 128 bytes). Kept separate so a missing header, a broken client and an attack/overlong
  password do not collapse into one answer.
- `comfoair_auth_result_t`: `GRANTED`, `DENIED`, `NO_PASSWORD`. **Only `GRANTED` may be mapped to
  the policy's `authenticated = true`.** `NO_PASSWORD` means "no question to answer"; treating it
  as authenticated would hand an unclaimed device to anyone on the open AP.

Constants and why:
- `COMFOAIR_AUTH_PASS_MAX 128` — must be ≥ `COMFOAIR_CONFIG_UI_PASS_MAX` (128) or the longest
  password the settings page accepts could never be used to log in. Enforced by a
  `_Static_assert` in `comfoair_auth.c`.
- `COMFOAIR_AUTH_FAIL_DELAY_MS 1000u` — esp_http_server serves all sockets from ONE task, so a
  caller sleeping here serialises every guess across all 7 sockets: ~1 guess/s total. The cost is
  that every other request waits too (an attacker can use that), which is why it is 1 s, not 5.
- `ENCODED_MAX 512` (private, `comfoair_auth.c`) — a cap on work, not storage: 512 base64 chars =
  384 bytes, room for a 128-byte password behind a 255-byte user name.

## Implementation

Files:
- `comfoair_auth.c` — the pure parser and decision (`comfoair_auth_parse`, `_forget`, `_check`).
- `comfoair_auth_device.c` — the five-line device verifier, wrapped in `#ifdef ESP_PLATFORM`
  because the host build compiles every `.c` in the directory (PlatformIO `lib_extra_dirs`).
- `include/comfoair_auth.h` — the contract and the rationale for what is absent.
- `CMakeLists.txt` — `REQUIRES comfoair_config` (public, because `comfoair_auth.c` includes
  `comfoair_config.h` for the `_Static_assert`).

Parse flow: zero `out` → skip whitespace → empty = `ABSENT` → match `Basic` + whitespace/end →
isolate one token, refuse trailing non-whitespace → `tok_len > 512` = `TOO_LONG` (early, before
decode) → `b64_decode` streams each byte into `sink_byte` → map flags in fixed order: not base64,
then NUL, then no colon (all `MALFORMED`), then `too_long` (`TOO_LONG`) → one exit, one wipe on
any refusal.

Internal helpers worth knowing:
- `sink_byte` — the only place decoded bytes reach memory. Bytes before the first colon are
  dropped unbuffered (so the user name costs the password nothing); after it, bytes go straight
  into `out->password` bounded by `pass_len >= COMFOAIR_AUTH_PASS_MAX`. A NUL anywhere (name
  included) sets a flag.
- `b64_decode` — requires length multiple of 4; `=` only as trailing padding (`b64_value('=')`
  is -1 so misplaced `=` like `YQ==YQ==` is refused). Non-zero leftover bits in a padded group are
  accepted (RFC 4648 §3.5 allows either; it changes no answer).
- `wipe` — volatile byte loop.

Invariants:
- `stored` empty ⇒ header never decoded ⇒ `NO_PASSWORD`, even with an always-true verifier.
- Missing verifier ⇒ `DENIED` (fails closed).
- Empty candidate (`admin:`) ⇒ `DENIED`, checked here, not left to the verifier.
- Every presented-and-failed attempt gets the same delay; `ABSENT` gets none.
- No credential leaves `comfoair_auth_check`; no plaintext survives a refusal.

Concurrency: no shared state, no locks.

DO NOT:
- Add a hash or a second stored-password format here: `comfoair_config_sanitize()` erases a
  `ui_pw_hash` its parser cannot read, so another format would be accepted then vanish at reboot.
- Remove the `sink_byte` bound as "redundant" to `ENCODED_MAX` — one counts encoded chars, the
  other decoded bytes; the rounding between them once allowed a 2-byte overrun on a stack built
  without a canary (`CONFIG_COMPILER_STACK_CHECK_MODE_NONE`).
- Re-introduce a buffer holding `user:password` together, or size the cap as name+password: a
  long user name (e.g. an email from a password manager) would then refuse the correct password.
- Split at the last colon — RFC 7617 allows colons in the password.
- Truncate an overlong password or a NUL-containing one — that answers a different question.
- Make `b64_decode` constant-time — it only touches sender-chosen data.
- Sleep or log inside this component.
- Map `COMFOAIR_AUTH_OK` (a parse value) to `authenticated` in a caller — it compiles because both
  enums start at 0, but the right test is `result == COMFOAIR_AUTH_GRANTED`.
- Add `comfoair_http` or `mbedtls` to `REQUIRES`: the component must not be able to send a
  response (a second door past the policy), and mbedtls is reached only through `comfoair_config`.

## Dependencies

- Requires: `comfoair_config` (`COMFOAIR_CONFIG_UI_PASS_MAX`; on device also
  `comfoair_config_check_ui_password_against`, `comfoair_config_device_kdf` from
  `comfoair_config_nvs.h`). libc `string.h`.
- Depended on by: `comfoair_http` (`CMakeLists.txt` REQUIRES it; `comfoair_http_allowed()` in
  `comfoair_http_gate.c` calls `comfoair_auth_check`). Host include path in `platformio.ini`
  (`-I components/comfoair_auth/include`).

How the real caller wires it: `comfoair_http_allowed()` reads the header into a 512-byte buffer,
passes `stored = password_set ? "set" : ""` (the real record never reaches the HTTP layer) and its
own `verify_against_device`, which ignores `stored` and calls `comfoair_net_check_password()` —
that does the same `comfoair_config_check_ui_password_against(..., comfoair_config_device_kdf())`
binding under `comfoair_net`'s lock. It maps only `GRANTED` to `authenticated`, and sleeps
`delay_ms` when a password is set, a header was present and the result is not `GRANTED`.

## Tests

- `test/test_auth/test_auth.cpp` (host, `pio test -e native -f test_auth`, 32 cases): base64
  against RFC 7617's example with an independent encoder; absent/foreign scheme/`Basic` with no
  token/case-insensitive scheme; invalid base64 and misplaced padding; no colon; NUL byte;
  8 KB header; longest storable password; overlong password refused not truncated; a canary
  behind the struct (`test_nothing_a_header_can_carry_writes_past_the_password_buffer`); long user
  name not charged to the password (`test_a_user_name_never_costs_the_password_its_length`);
  first-colon split; empty password / empty name; whitespace; NULL `out`; `forget` clears every
  byte; no-password grants nothing even to an always-true verifier and costs no delay; verifier
  receives arguments unchanged; wrong password delayed; empty candidate; NULL verifier denies;
  absent header not delayed; uniform delay across failure kinds; not a lockout; outcome header
  classification.
- Two `static_assert`s in the suite pin `comfoair_auth_verifier_t`'s shape and the signature of
  `comfoair_config_check_ui_password_against()`.

Not covered on the host: `comfoair_auth_device.c` (never compiled off-device; the declaration of
`comfoair_config_device_kdf()` is reachable only by a device build), and the impure glue in
`comfoair_http_allowed()` (header read, the actual `vTaskDelay`, the 401/403 responses).

## Notes

- **Delay, not lockout.** Counting failures and refusing afterwards is a denial of service against
  the owner — the person most likely to mistype — and recovery (reset button, USB) is behind the
  front panel of a running ventilation unit.
- **No logging, and no credential in the outcome.** The log ring is served by `GET /api/log`,
  world-readable on the open AP. Returning only the parse shape means a caller cannot put the
  password into a format string by accident.
- **No user name in the API.** One password, no accounts; whatever the browser's name box holds is
  ignored, including its length.
- **`ABSENT` costs no delay** because it is the first request of every page load on a protected
  device; delaying it would stall the single HTTP task for every normal load.
- **Only NUL is refused among control bytes.** Other control characters are refused where a
  password is SET (`comfoair_config_check_ui_password`), so they can match nothing stored;
  repeating the rule here would put one decision in two places.
- **Verifier injection** keeps mbedtls off the host so the parser is testable against hostile
  input, mirroring how `comfoair_config` takes its KDF.
