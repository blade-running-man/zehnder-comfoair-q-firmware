# comfoair_json — the firmware's one JSON reader (flat objects only) and one JSON string escaper

> This is documentation, not code — re-read the source before relying on a detail.

A pure, dependency-free JSON reader for the request bodies this device accepts, plus the one
string escaper every JSON emitter in the firmware uses. The reader is the first code that touches
bytes a client wrote: the setup access point is open, so the client can be anyone in radio range,
and the reader runs before any access decision about the *content*. That is why it is a pure
function over a NUL-terminated buffer (every hostile body can be reproduced on the host) and why
it refuses anything outside a deliberately narrow grammar instead of guessing.

## Responsibility

Owns:

- Deciding whether a body is a well-formed **flat** JSON object (`comfoair_json_check()`):
  not-an-object, malformed, nested and duplicate-key are four distinct refusals.
- Reading one key as a decoded string, `uint32_t`, `int32_t` or `bool`, with refusal codes that
  keep "absent", "empty", "wrong type", "too long" and "out of range" apart.
- Escaping one C string into a JSON string body (`comfoair_json_escape()`), including the rules for
  `\uXXXX`, surrogate pairs, control characters and invalid UTF-8.

Does NOT do:

- Map a refusal to an HTTP status or a field name — `comfoair_wire` (`comfoair_wire_parse_*`) and
  `comfoair_http_probe.c` do that.
- Read the HTTP body or send a response — `comfoair_http`. This component deliberately cannot
  reach `esp_http_server` (see `CMakeLists.txt`), so the access policy in `comfoair_policy` has no
  second door.
- Validate values (port range, SSID rules, secrets' keep/clear/store) — `comfoair_config`,
  `comfoair_secrets`, `comfoair_command`.
- Build whole documents — `comfoair_api`, `comfoair_ha`, `comfoair_wire` write their own JSON and
  call the escaper for every string.
- Parse nested objects or arrays, `null` as a typed value, or numbers with fraction/exponent.

## Public API

Header: `include/comfoair_json.h` (C linkage; implemented in C++). All functions are pure,
reentrant, allocate nothing, keep no state, and are callable from any task. Inputs are
caller-owned NUL-terminated buffers; nothing reads past the terminator.

| Symbol | Contract |
| --- | --- |
| `comfoair_json_check(doc)` | Walks the whole document once. Returns `COMFOAIR_JSON_DOC_OK` only for a single flat `{...}` with nothing but whitespace after it. `NULL` → `NOT_AN_OBJECT`. Call it before any getter; it is the only answer about the body as a whole and the one a handler turns into 400. |
| `comfoair_json_string(doc, key, out, cap)` | Copies the DECODED string (escapes resolved, `\uXXXX` → UTF-8) and NUL-terminates it on `FOUND`. `cap` includes the terminator; a value that does not fit is `TOO_LONG`, never truncated. `out == NULL` or `cap == 0` → `TOO_LONG`. |
| `comfoair_json_u32(doc, key, out)` | Whole, non-negative, ≤ `UINT32_MAX`. Fraction, exponent, minus or overflow → `OUT_OF_RANGE`. `out == NULL` → `MISSING`. |
| `comfoair_json_i32(doc, key, out)` | Whole, in `INT32_MIN..INT32_MAX`. A negative is `FOUND`; a fraction/exponent is `OUT_OF_RANGE`. `out == NULL` → `MISSING`. |
| `comfoair_json_bool(doc, key, out)` | Only the literals `true`/`false`; `1`, `"true"`, `null` → `WRONG_TYPE`. `out == NULL` → `MISSING`. |
| `comfoair_json_escape(in, out, cap)` | Writes the escaped body without surrounding quotes, always NUL-terminated (when `cap > 0`); returns bytes written excluding the terminator. `in == NULL` → empty output. `out == NULL`/`cap == 0` → returns 0, writes nothing. No overflow indicator. |

`comfoair_json_doc_t`: `DOC_OK`, `DOC_NOT_AN_OBJECT` (also empty input — distinct from `{}`
because every config-patch field is optional, so "no keys" would be a successful no-op save),
`DOC_MALFORMED`, `DOC_NESTED`, `DOC_DUPLICATE` (first-wins vs last-wins disagree, and
`{"ui_password":"x","ui_password":"__UNCHANGED__"}` means opposite things under each).

`comfoair_json_read_t`: `FOUND`, `MISSING` (absent — must stay distinct from `""`, which clears a
stored value), `WRONG_TYPE`, `TOO_LONG`, `OUT_OF_RANGE` (so `70000` reaches the port validator as a
refusal, not wrapped to `4464`), `MALFORMED` (reported per key too, so a getter is safe on an
unchecked body). Callers must treat anything other than `FOUND`/`MISSING` as a refusal.

Internal limits (in `comfoair_json.cpp`, not exported): keys are decoded into a 64-byte buffer
(≤ 63 bytes); `comfoair_json_check()` remembers at most 16 distinct keys for duplicate detection
(the largest accepted document has about ten fields).

## Implementation

- `comfoair_json.cpp` — everything: the scanner, the per-key getters, the escaper.
- `include/comfoair_json.h` — the contract and the "why not cJSON" rationale.
- `CMakeLists.txt` — no `REQUIRES`/`PRIV_REQUIRES` at all.

Internal helpers (anonymous namespace):

- `Scan` — a cursor that never advances past the NUL; `spaces()` accepts exactly RFC 8259
  whitespace (space, tab, LF, CR — not VT/FF).
- `Sink` — destination for decoded bytes; `out == nullptr` means count only. Sets `overflow`
  instead of writing past `cap - 1`.
- `read_string()` — the ONE string walk, shared by `check`, key comparison and value copy, so there
  is one set of escape rules. Refuses raw bytes < 0x20, unknown escapes, `\u0000`, lone or
  mismatched surrogates; passes raw UTF-8 through byte for byte (SSIDs are octet strings).
- `read_number()` — JSON's number grammar exactly (no leading zero, no `+`, no `1.`); tracks
  `whole`, `negative`, `overflow` in a `uint64_t`.
- `read_literal()` — `true`/`false`/`null`, requiring a token-ending byte after the word.
- `read_value()` — dispatches by first byte; `{`/`[` returns `WRONG_TYPE` (mapped to `DOC_NESTED`
  by `check`) without trying to skip it.
- `seek()` — re-scans from the start to the requested key; first occurrence wins; it cannot step
  over a nested value, so any key behind one is `MALFORMED` (unreachable), not `MISSING`.
- `value_is_closed()` — after a value the next non-space byte must be `,` or `}`; without it
  `{"a":12` would answer 12.

Invariants:

- Nothing reads past the terminator, whatever the bytes claim (asserted over every prefix of a
  realistic body in `test_json`).
- Getters never trust a prior `check()`; each does its own bounded scan.
- The escaper is all-or-nothing per escape sequence: an escape that does not fit is dropped whole,
  never half-written.
- The escaper emits only ASCII: bytes ≥ 0x80 become `\uXXXX` (surrogate pair above the BMP);
  malformed UTF-8 bytes, codepoints > U+10FFFF and encoded surrogates are dropped.

DO NOT:

- Replace it with cJSON. cJSON is in the IDF image but cannot be built in `[env:native]`
  (`lib_extra_dirs` sees only this repo's components), so every hostile-body case would need a
  flashed board.
- Make the reader skip nested values. Depth tracking inside a never-used value is where parsers
  of this shape get their bugs; the status document's layout relies on "flat members first, one
  object last" precisely because `seek()` stops at a nested member.
- Truncate a too-long string, or accept `\u0000`, to be "lenient". Both silently change a
  credential or SSID.
- Fold `_i32` into `_u32` with a flag. A caller recovering a negative from `_u32`'s
  `OUT_OF_RANGE` cannot tell `-1` from `18.5`, and `-1` means "forever" for timed operations.
- Add a second escaper elsewhere. `comfoair_api_escape_json()` forwards here; the device name
  must round-trip through `GET`/`POST /api/config` unchanged.
- Add `REQUIRES esp_http_server` (or anything that reaches it).

## Dependencies

Requires: nothing (libc only: `cstring`, `cstdio` for `snprintf`).

Used by:

- `comfoair_wire` (`comfoair_wire_parse.c`, `comfoair_wire_command.c`, the writer in
  `comfoair_wire_internal.h`) — `check` + getters for provisioning, `POST /api/config`,
  entity writes (string-first then bool then u32 to discover the value kind) and operations
  (`_i32` for durations); `escape` for strings it emits.
- `comfoair_http` (`comfoair_http_probe.c`) — `check` + `_u32` for the probe request body.
- `comfoair_api` (`comfoair_api.cpp`) — `comfoair_api_escape_json()` forwards to `escape`.
- `comfoair_ha` (`comfoair_ha_internal.h`, `put_string()`) — `escape` into a 512-byte buffer.
- `platformio.ini` `[env:native]` adds `components/comfoair_json/include` to the include path.

## Tests

- `test/test_json/test_json.cpp` — the dedicated suite: document shapes (non-object, trailing
  rubbish, nested, duplicates, trailing comma, bare keys), string decoding (escapes, raw UTF-8,
  `\u` → UTF-8, lone surrogate, `\u0000`, raw control chars, too long vs exactly fits), key
  matching (whole key, escaped key), numbers and booleans, every-prefix terminator safety,
  unchecked documents, NULL arguments, and escape → read round trip.
- `test/test_api/test_api.cpp` — escaper edge cases via `comfoair_api_escape_json()`: control
  chars, UTF-8, truncation never cutting an escape, out-of-range codepoint, lone surrogate,
  astral pair.
- Also used as a test helper to read firmware-emitted documents in `test/test_wire`,
  `test/test_wire_status`, `test/test_wire_epoch`, `test/test_mqtt_facts`.

Not covered: the 16-key duplicate-tracking cap and duplicates of keys ≥ 64 bytes; the escaper's
behaviour once the buffer is full (see Notes); overlong UTF-8 input to the escaper.

## Notes

- **Overflow in the escaper is silent and not a clean prefix.** ASCII stops the loop when it no
  longer fits, but a multi-byte escape that does not fit is skipped and the loop continues, so a
  later shorter escape (e.g. `\n`) can still be written. The return value does not signal it.
  Callers size buffers generously (512 bytes in `comfoair_ha`/`comfoair_wire`).
- **`comfoair_json_string()` writes into `out` while decoding.** The header says `out` is
  untouched on failure; in the code the sink writes directly into `out`, so on `TOO_LONG` or a
  late `MALFORMED` it can hold unterminated partial bytes. Only `FOUND` NUL-terminates. Current
  callers treat every non-`FOUND` as a refusal and never read the buffer, so this is harmless
  today — do not start relying on "untouched".
- **Duplicate detection is bounded.** `check()` records up to 16 keys and skips keys whose decoded
  form exceeds 63 bytes; beyond those limits a duplicate is not reported. Getters can never match a
  key that long, and with duplicates `seek()` returns the first occurrence.
- **`null` is a value but no getter's type.** `check()` accepts `{"a":null}`; every getter answers
  `WRONG_TYPE` for it.
- **Keys are compared after decoding**, so `{"a":1}` answers to `"a"`: a client that escaped a
  key would otherwise have a setting silently dropped from a save.
- **Overlong UTF-8 in the escaper** (e.g. `C0 80`) is decoded rather than rejected and can emit
  `\u0000`, which this component's own reader then refuses. The reader passes raw bytes in strings
  through unvalidated, so whether such bytes can reach the escaper depends on what the stored
  value's validator (`comfoair_config`) accepts — check there before relying on either side.
