# comfoair_log — the in-RAM device log ring that `GET /api/log` serves

> This is documentation, not code — re-read the source before relying on a detail.

The device has no file system and is often reached without a USB cable, so the owner needs a
way to read what the firmware logged when something goes wrong. This component keeps the last
`COMFOAIR_LOG_LINES` log lines in a fixed static ring, hooks ESP-IDF's logging so every
`ESP_LOGx` line lands there as well as on the console, and renders the ring as a JSON array of
strings for the HTTP layer. When the ring is full the oldest line is overwritten: during an
incident the interesting lines are the most recent ones, and a log that stops recording when
full is worse than none.

## Responsibility

Owns:
- The ring itself (`s_lines`, `s_next`, `s_count`) and its reset.
- Reassembling the fragments of one logical log line that arrive over several vprintf-hook
  calls into one ring entry (`comfoair_log_feed`).
- Its own JSON string escaping and the array rendering (`comfoair_log_render`).
- Installing the ESP-IDF vprintf hook that chains to the previous writer (the console).

Does NOT do:
- Serve HTTP, auth-gate or decide who may read the log — `comfoair_http` (`comfoair_http_log_get`,
  route `/api/log`) and its policy check `comfoair_http_allowed()` do.
- Decide when the log must be wiped for privacy — `comfoair_button` (`comfoair_button_reset.c`)
  calls `comfoair_log_reset()` after a successful hard reset.
- Filter secrets. Whatever a caller formats into `ESP_LOGx` is stored and later served; keeping
  secrets and client-supplied text out of log calls is every caller's job (`comfoair_auth.h`,
  `comfoair_http_internal.h` say so explicitly).
- Persist anything. The ring is RAM only and is lost on reboot (it does survive an NVS erase).
- Push log lines over the WebSocket; the log is pull-only through `GET /api/log`.

## Public API

Header: `include/comfoair_log.h` (C, `extern "C"` guarded).

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_LOG_LINES` = 80 | Ring capacity in lines. |
| `COMFOAIR_LOG_LINE_LEN` = 160 | Slot size in bytes including the NUL, so at most 159 characters per entry. |
| `void comfoair_log_reset(void)` | Empties the ring AND drops any pending (unterminated) fragment. Any task; no lock. Used by host tests' `setUp` and by the hard-reset path. |
| `void comfoair_log_write(const char *line)` | Commits one whole line: NULL or `""` is ignored, longer than 159 chars is truncated, trailing `\n`/`\r` are trimmed. Never allocates, never blocks; callable from any task, including at boot. No return value. |
| `void comfoair_log_feed(const char *chunk)` | Feeds one vprintf-hook fragment; commits one ring entry per complete line inside it and buffers the text after the last `\n` for the next call. NULL is ignored. Do not mix with direct `comfoair_log_write` calls while a fragment is pending (ordering breaks). |
| `size_t comfoair_log_render(char *out, size_t cap)` | Writes `[ "line", ... ]` oldest first, NUL-terminated; returns the length without the NUL, or **0 if it does not fit** (or `out == NULL`, or `cap < 3`). A 0 means the caller must not serve a partial document — `comfoair_http` answers 500 `{"error":"log does not fit"}`. Caller owns `out`. |
| `void comfoair_log_install(void)` | Device only: installs the vprintf hook via `esp_log_set_vprintf` and keeps the previous writer. Call exactly once, as early as possible. On the host (`!ESP_PLATFORM`) it is an empty function. |

Why 80 × 160: about 13 KB of static RAM, roughly the last minute of a busy boot. Both are round
numbers, not measurements — raise them only against a measured RAM figure. `COMFOAIR_LOG_LINE_LEN`
also sizes a stack buffer in `log_hook`, i.e. in the stack of *every task that logs*, which is a
second reason not to grow it casually.

## Implementation

Files:
- `comfoair_log.c` — everything: the ring, `feed`, the escaper, the renderer, and (under
  `#ifdef ESP_PLATFORM`) the hook and `install`.
- `include/comfoair_log.h` — the contract above, including the evidence for the fragmenting
  behaviour `feed` handles.
- `CMakeLists.txt` — one source, `REQUIRES log`.

Flow on the device: any `ESP_LOGx` → ESP-IDF `esp_log_va()` → `log_hook(format, args)` →
`vsnprintf` of a `va_copy` into `char line[160]` → `comfoair_log_feed(line)` → zero or more
`comfoair_log_write()` commits → then the original `args` go to the previous writer (console).

Why `feed` exists: with `CONFIG_LOG_VERSION=1` (set in `sdkconfig.m5stack-nanoc6`) ordinary
ESP-IDF logging makes one hook call per line with the `\n` included, but the closed WiFi driver
blob makes three calls per logical line through the same hook (prefix, body, a lone `"\n"`).
Committing per call turned each WiFi line into three slots, one empty. `feed` buffers until a
`\n` so the ring only ever sees whole lines.

`feed` rules (each pinned by a `test_log` case):
- `\n` with pending text commits it; `\n` with nothing pending commits nothing (this is what
  kills the empty WiFi slot).
- `\r` is dropped and never buffered, so a `\r\n` ending cannot leave a one-byte `"\r"` pending
  line that would commit as an empty entry.
- When the pending buffer reaches 159 characters it commits immediately without waiting for a
  newline. Reason: `log_hook`'s `vsnprintf` already truncated any over-long call to 159 chars,
  cutting off its `\n`; waiting for that newline would glue the *next, unrelated* line onto it.
  An over-long line therefore lands as consecutive whole slots and the next call starts clean.
- Leftover text after the last full slot of a single oversized chunk stays pending and merges
  with the next call — `log_hook` never produces such a chunk (it hands at most 159 chars).

Render: when the ring is full the oldest entry is at `s_next`, otherwise at 0. Escaping is local
(`append_escaped`): `"` `\` `\n` `\r` `\t` get their short escapes, every other byte outside
0x20..0x7e becomes `\u00XX` **per byte**. Every bounds check returns 0 rather than truncating.

Concurrency: there is **no lock**, by deliberate choice. Nothing in ESP-IDF's v1 log path holds a
lock across the hook call (the tag-level lookup locks and unlocks before `esp_log_vprint_func`
is reached, and the binary-log mode that would hold `esp_log_impl_lock()` is not compiled in), so
two tasks logging at once can interleave writes to `s_next`/`s_count` and to the pending buffer.
The accepted exposure is a torn or merged line, or an extra/missing entry at the seam; `render`
may likewise read a line that is being written. The "never blocks" contract is what rules out a
mutex in a hook that runs in every task.

DO NOT:
- Call `comfoair_log_install()` twice: the second call would record `log_hook` itself as the
  previous writer and the hook would recurse into itself.
- Drop the chaining to `s_previous`: USB is the only channel when the network is what is broken;
  the ring is an addition to the console, never a replacement.
- Reuse `args` for `vsnprintf` instead of `va_copy`: the list is consumed again by the console
  writer, and walking a `va_list` twice is undefined.
- "Simplify" `log_hook` to call `comfoair_log_write` directly — that brings back the three
  slots per WiFi line, one empty.
- Make `comfoair_log_write` the place that rejects framing-only input after the fact; it accepts
  `"\n"` (non-empty) and stores an empty entry. `feed` filters framing before it gets there.
- Share the JSON escaper with `comfoair_api`: the log has to work when nothing else does, so it
  depends only on IDF `log`.
- Add a lock or allocation here: the write path must never block or fail, including at boot.

## Dependencies

- Requires: IDF `log` only (`esp_log.h` under `ESP_PLATFORM`); libc otherwise. No PRIV_REQUIRES.
- Depended on by: `src/main.cpp` (`comfoair_log_install()` as the first line of `app_main`, so
  boot lines are captured), `comfoair_http` (`REQUIRES`; `comfoair_log_render` in
  `comfoair_http_log_get` into its 48 KiB `g_http_scratch` (`COMFOAIR_API_DOC_BUFFER_SIZE`)),
  `comfoair_button` (`PRIV_REQUIRES`; `comfoair_log_reset()` after a successful hard reset).
  `src/CMakeLists.txt` lists it as a requirement.
- Host build: `platformio.ini` `[env:native]` adds `-I components/comfoair_log/include`; the
  source is picked up as a PlatformIO library via `lib_extra_dirs = components`.

## Tests

- `test/test_log/test_log.cpp` (Unity, `pio test -e native -f test_log`): empty array, order,
  oldest-dropped wrap, truncation of an over-long line, JSON escaping of quotes/newlines/
  backslash, NULL ignored, too-small buffer returns 0, and the `feed` cases — no newline pending,
  WiFi-style split line joined, several lines in one chunk, bare newline, trailing pending text,
  overflow at the cap and recovery, exactly-slot-length, one-past-slot-length, `\r` dropped,
  reset clears pending.
- Not covered: `log_hook` and `comfoair_log_install` (device-only, no host suite or fake), the
  real WiFi blob fragmenting pattern (only simulated), concurrent writers, and the full-ring
  worst case against `comfoair_http`'s 48 KiB scratch buffer.

## Notes

- **Privacy is a reason for reset, not just hygiene.** The ring survives an NVS erase and holds
  SSIDs, device names and whatever else was logged, and after a hard reset no UI password is set,
  so `GET /api/log` is open to anyone on the access point. That is why the button hard-reset path
  wipes it — only after the erase succeeded, since on failure the ring is the only evidence.
- **The log is world-readable when no password is set**, so nothing secret and nothing a client
  sent may ever be formatted into a log call anywhere in the firmware. This component cannot
  enforce that; it only stores.
- **Render can legitimately fail.** An ASCII-only full ring renders to about 13 KB, which fits
  `comfoair_http`'s 48 KiB scratch. Escaping expands a control or non-ASCII byte to 6 characters,
  so a ring full of such bytes can exceed 48 KiB; the contract is then a 500, never a truncated
  200 — same as every other renderer in the firmware.
- **Non-ASCII is escaped byte by byte**, not decoded as UTF-8: a multi-byte UTF-8 character comes
  back as several `\u00XX` escapes (readable as Latin-1 mojibake, not the original text). Log
  strings are expected to be ASCII.
- Early/ROM logging (`ESP_EARLY_LOGx`, bootloader) does not go through the vprintf hook and so
  never reaches the ring.
