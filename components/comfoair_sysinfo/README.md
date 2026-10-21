# comfoair_sysinfo — the GET /api/system document renderer, the unit's error-text table and its Home Assistant summary

> This is documentation, not code — re-read the source before relying on a detail.

`GET /api/system` is the page an owner opens first when something looks wrong: who the
ventilation unit is, who this device is, and how the links between them (Wi-Fi, CAN, the command
queue, MQTT) are doing. This component is the **pure** half of that endpoint. The HTTP handler
gathers numbers from the components that own them into one `comfoair_sysinfo_t` snapshot; this
component turns the snapshot into compact JSON, and it alone decides what "unknown" looks like
(every null rule lives here, so the shape is host-tested). It is also the single home of the
unit's error-id -> English text table, including the firmware-dependent numbering and which ids
are mere notices, and of the two forms Home Assistant gets them in: the `unit_error` verdict and
the one-line `active_errors` text.

## Responsibility

Owns:

- The exact key order and shape of the `GET /api/system` JSON (unit, device, network, bus,
  commands, mqtt), rendered into a caller-supplied bounded buffer.
- Every null rule: `""`/`NULL` strings, a never-read unit, a disconnected network, a missing
  firmware word, an unanswered error read, an unknown error id.
- JSON string escaping that is safe for bytes that came off the CAN bus or an access point.
- The error-id -> text tables (`BASE`, `NEWER`, `OLDER`), the R1.4.0 selector, and each id's
  informational flag (a notice that never raises a Home Assistant problem).
- The `unit_error` verdict and the `active_errors` line (`comfoair_sysinfo_errors_any()`,
  `comfoair_sysinfo_errors_summary()`), bounded to Home Assistant's 255-byte state limit.

Does NOT do:

- Gather any value. `comfoair_http` (`comfoair_http_system.c`) fills the snapshot from IDF,
  `board`, `comfoair_net`, `comfoair_can`, `comfoair_control`, `comfoair_mqtt` and the probe task.
- Read the unit. `comfoair_probe_task` (`comfoair_probe_task_unit.cpp`) reads NODE 0x01
  properties and GETACTIVEERRORS on its own schedule and hands a copy over via
  `comfoair_probe_task_unit()`.
- Decode the firmware version word. That is `comfoair_probe_version_text()` in
  `comfoair_probe_report`, shared with the probe report so one firmware is never printed two ways.
- Map reset reasons or CAN states to words — the handler does (`reset_word()`, `bus_word()` in
  `comfoair_http_system.c`); this component prints the word it is given.
- Allocate, lock, or serve HTTP.
- Decide when the errors are read or write the two entities. `comfoair_unit_cadence` schedules
  the errors read every 60 s between the full unit reads (10 s after boot, every 60 s until the
  model, the settings and the firmware word have answered, then every 6 h). `comfoair_probe_task`
  hands the line and the verdict to `comfoair_state` only from an answered read and only once the
  firmware word is known — without it no id is informational, so a manual bypass would raise a
  problem; the entities stay unknown instead. An unanswered read changes neither entity.

## Public API

Header: `include/comfoair_sysinfo.h` (C ABI, implemented in C++).

| Symbol | Contract |
| --- | --- |
| `size_t comfoair_sysinfo_render(const comfoair_sysinfo_t *info, char *out, size_t cap)` | Any task; no shared state. The caller owns `info` and every pointer inside it for the call. Writes compact JSON into `out`, NUL-terminated; returns bytes written (excluding NUL). Returns **0** if the document did not fit — never writes past `cap`, and on failure writes only `out[0] = '\0'` (when `cap > 0`). A 0 is a sizing defect, not a big unit: the caller must handle it (the handler answers 500). `info` must not be NULL. |
| `const char *comfoair_sysinfo_error_text(uint16_t id, bool has_firmware, uint32_t firmware_raw)` | Any task. Returns a static English string for a unit error id, or NULL if no table knows it. Not owned by the caller; never freed. |
| `bool comfoair_sysinfo_error_informational(uint16_t id, bool has_firmware, uint32_t firmware_raw)` | Any task. True when the id, in the firmware-selected table, is a notice (newer: 79, 80, 89, 96; R1.4.0 and below: 75, 76, 81). False for an unknown id, and for every id while `has_firmware` is false (the numbering is unknown; the text's newer-table guess is not trusted for the verdict). |
| `bool comfoair_sysinfo_errors_any(const uint8_t *errors, bool has_firmware, uint32_t firmware_raw)` | Any task. The `unit_error` verdict: true when a set bit names a non-informational id (an unknown id counts). False for NULL. |
| `size_t comfoair_sysinfo_errors_summary(const uint8_t *errors, bool has_firmware, uint32_t firmware_raw, char *out, size_t cap)` | Any task. Writes the `active_errors` line: ascending id, `" \| "` between, informational ids included, `"Unknown error <id>"` for an unknown one, `"No errors"` for no bit. At most `COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX` (255) bytes, cut on a UTF-8 boundary and ending `"..."`. Returns the length; **0** for a NULL pointer or `cap <= 255` (a caller defect, nothing written). |
| `comfoair_sysinfo_t` | The whole snapshot: `unit`, `device`, `network`, `bus`, `commands`, `mqtt`. |
| `comfoair_sysinfo_unit_t` | The unit as last read. String fields are fixed arrays, `""` when never answered, and **may be unterminated** at full width. `firmware_raw` meaningful only with `has_firmware`; `errors[]` valid only when `errors_state == READ_OK`; `identity_age_ms` ignored unless `identity_ever_read`. |
| `comfoair_sysinfo_read_t` | `READ_UNKNOWN` (0: not read yet — boot or no bus) → `"unknown"`; `READ_OK` → `"ok"`; `READ_NO_ANSWER` (last read unanswered, older values kept) → `"no_answer"`. Zero-initialised snapshot == unknown. |
| `comfoair_sysinfo_device_t`, `_network_t`, `_bus_t`, `_commands_t`, `_mqtt_t` | Borrowed `const char *` strings (NULL or `""` both render `null`) and plain counters. `bus.nodes_seen` is an ascending array of `nodes_seen_count` bytes. |

Constants:

- `COMFOAIR_SYSINFO_TEXT_MAX = 48` — unit string field size. The live Q350 serial is 31 chars and
  its model 25; 48 leaves headroom without a second size.
- `COMFOAIR_SYSINFO_ERRORS_BYTES = 32` — the GETACTIVEERRORS bitfield, 256 possible ids.
- `COMFOAIR_SYSINFO_MAX_NODES = 64` — the `nodes_seen` bound; the handler `_Static_assert`s its
  census buffer against it.
- `COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX = 255` — Home Assistant's `MAX_LENGTH_STATE_STATE`; an MQTT
  sensor state past it turns unknown rather than cut (`mqtt/util.py` `check_state_too_long()`).
- `COMFOAIR_SYSINFO_NO_ERRORS = "No errors"` — never `"None"`, which Home Assistant reads as
  "no value".

## Implementation

Files:

- `comfoair_sysinfo.cpp` — the renderer: an `Out` append-only writer and one `render_*` per section.
- `comfoair_sysinfo_errors.cpp` — the three error tables with each row's informational flag and
  its evidence, `LAST_OLD_NUMBERING`, the lookup.
- `comfoair_sysinfo_summary.cpp` — the `unit_error` verdict and the `active_errors` line.
- `include/comfoair_sysinfo.h` — snapshot types and the entry points.
- `CMakeLists.txt` — `REQUIRES comfoair_probe_report` (public), nothing else.

Flow: `comfoair_sysinfo_render()` writes `{"unit":…,"device":…,"network":…,"bus":…,
"commands":…,"mqtt":…}` in the schema's key order, then `Out::finish()` either NUL-terminates
and returns the length or returns 0.

`Out` (anonymous namespace): once any `put` does not fit it latches `ok_ = false` and writes
nothing more, so the caller checks once at the end. It always keeps one byte for the NUL
(`n >= cap_ - len_` fails). Helpers:

- `put_string(s, n, own)` — escapes `"` and `\`; bytes `< 0x20` become `\u00XX`; bytes `>= 0x7F`
  become `\u00XX` (read as Latin-1) unless `own` is true.
- `put_text(s)` — NULL or `""` → `null`, otherwise an escaped string.
- `put_field(f)` — a unit string bounded by `memchr` over the 48-byte array, never `strlen`.

Null and filtering rules worth knowing:

- `unit.age_ms` is `null` unless `identity_ever_read`; with `NO_ANSWER` it still shows the age of
  the last success, and the identity strings are the last values read.
- `unit.firmware` and `firmware_raw` are `null` without `has_firmware` (and `firmware` is also
  null if the decoder returns 0). The local buffer is 16 bytes: the decoder's worst case
  `"R1023.1023.1023"` + NUL.
- `unit.errors.ids`/`items` are emitted only when `errors_state == READ_OK`; otherwise both are
  `[]`. Bit order: byte `i`, bit `b` (LSB first) is id `i*8 + b`. Each item's `text` is
  `comfoair_sysinfo_error_text()` or `null`.
- `network`: when `connected` is false, `ssid`, `ip`, `rssi`, `channel` are all `null` whatever
  the snapshot holds. `rssi` also needs `has_rssi`; `channel == 0` means unknown → `null`.
- `bus.nodes_seen`: a NULL array renders `[]` regardless of the count; the count is clamped to
  `COMFOAIR_SYSINFO_MAX_NODES`.
- Numbers are always printed (no null) for device, bus, commands and mqtt counters.

Error lookup: `BASE` (ids 21–69, same on all firmware, with gaps — e.g. 36, 40, 42 have no
row) first, then `OLDER` if `has_firmware && firmware_raw <= 3222278144` (0xC0101000, exactly
R1.4.0), else `NEWER` (ids 70–104). All extra rows start at 70, so no extra row shadows a base
row. Linear search; tables are tiny.

Summary: walks the bitset in id order and appends each text while it fits whole. The first item
that does not fit is cut so that `"..."` still fits, backing off past UTF-8 continuation bytes;
when not even the separator fits, the text already written is shortened instead. No bit at all
gives `"No errors"`; notices alone are listed (the line names what is active, the verdict decides
whether it is a problem).

Concurrency: none. The component holds no state; `Out` and the tables are per-call or const.
Serialising access to the output buffer is the caller's job (the handler uses one static
`s_doc` and relies on esp_http_server's single task).

DO NOT:

- `strlen()` a unit string field — a full field carries no NUL and the read runs into the next
  field. Use `put_field()`.
- Pass non-`own` bytes `>= 0x80` through unescaped. Unit strings and SSIDs are not known to be
  UTF-8; one stray 0xFF in a serial would make the whole document unparseable.
- Change the selector to `<`. R1.4.0 itself uses the old numbering.
- Show error bits from a non-OK read. After a failed read the array may hold an older answer, and
  a cleared error must not stay on screen as active.
- Copy the version decode into this component — call `comfoair_probe_version_text()`.
- "Fix" the typos in the error strings ("condensor", "PREHEAT ERROR , 1081"): they are verbatim
  so a service manual or a web search matches them.
- Make `comfoair_sysinfo_errors_any()` "any bit set", or mark an id informational without
  evidence: a user's own bypass override must never raise a Home Assistant problem, and a real
  fault must never be hidden. `test_the_informational_ids_of_each_table` pins the whole set.
- Look up the informational flag by bare id: 81 is the manual bypass at R1.4.0 and a preheater
  fault above it.
- Add a field without re-checking the worst case against the handler's 17 KiB buffer
  (`test_worst_case_size` fails if it grows past it).

## Dependencies

- Requires: `comfoair_probe_report` (public, for `comfoair_probe_version_text()`). No IDF
  component, no `board` — the board name arrives as a plain `const char *`.
- Depended on by: `comfoair_http` (`comfoair_http_system.c`, the only caller of
  `comfoair_sysinfo_render()`); `comfoair_probe_task` (public `REQUIRES` — its header and
  `comfoair_probe_task_unit.cpp` fill a `comfoair_sysinfo_unit_t` and use
  `COMFOAIR_SYSINFO_TEXT_MAX`). The SPA's `web/src/api/system.ts` is a hand transcription of the
  rendered schema (no code dependency).
- Native env: `platformio.ini` adds `-I components/comfoair_sysinfo/include`.

## Tests

`test/test_sysinfo/test_sysinfo.cpp` (host, `pio test -e native -f test_sysinfo`). Expected JSON
is written by hand from the schema, not from the implementation. Covers: the full render of the
live Q350 R1.11.0 snapshot byte-for-byte; a never-read unit is all null; NO_ANSWER keeps old
identity but shows no stale errors; the error tables and the R1.4.0 selector (including no
firmware → newer); rendered errors following the firmware; own UTF-8 passing through; a hostile
serial being escaped; an unterminated field stopping at its array; a disconnected network all
null; NULL pointers rendering null (and a count without an array → `[]`); every too-small `cap`
returning 0 without touching bytes at or past `cap`; the worst-case document (all 256 error
bits, every string escaping to six bytes, more than 64 nodes) staying within 17 KiB for both
tables; and `comfoair_probe_version_text()` itself.

`test/test_unit_errors_summary/test_unit_errors_summary.cpp` (host, `pio test -e native -f
test_unit_errors_summary`): `"No errors"`; one text; a manual bypass alone is no problem but is
listed; notices only (79, 80, 89, 96) → no problem, all listed; a fault beside a notice → problem;
the old numbering for the same bits; the flag following the numbering (81, 89); no firmware read
→ every bit a problem; the whole informational set of each table (empty without firmware); no firmware → newer table; unknown ids (a problem); the 255-byte
cut ending `"..."`; every cut position valid UTF-8; NULL and short-buffer refusals.

Not covered by this suite: the handler glue in `comfoair_http_system.c` (impure, IDF calls; no
host suite) and the probe task's filling of the unit struct. The error-id texts are not yet
confirmed on hardware with an active error.

## Notes

- **Why render whole rather than stream.** The document is a one-shot snapshot with a bounded
  worst case, so a single fixed buffer is simpler and lets the size bound be a test. The probe
  report, whose size depends on the walk, streams instead.
- **Why NULL and `""` mean the same.** The schema has no empty-string state, and the producers
  spell "not known" either way (e.g. the handler leaves `ip` as `""` without an address).
- **Why disconnected forces nulls.** An SSID or IP beside `"connected": false` reads as
  "connected to"; the schema's rule is null without a network, not "the last one".
- **Why clamp `nodes_seen` instead of failing.** The caller's buffer is sized for 64 nodes; a
  longer list would otherwise turn into a 0 return — no document at all — rather than a
  shorter one.
- **Why the newer table without a firmware read.** Every unit this firmware has met runs
  R1.11.0, and a wrong guess only costs a misleading text beside the id, which the document
  always carries. The *verdict* is not guessed the same way (no id is informational without the
  firmware), and the two Home Assistant entities are not published at all until it is known.
- **Why three error tables.** At R1.4.0 and below the unit has no analog-input rows, so ids from
  70 are shifted (70 is "ComfoHood …" there, "Analog input 1 …" on newer firmware). The table comes from
  the project's protocol reference (`docs/reference/comfoair-pdo-rmi.md` §4); Zehnder does not
  publish it.
- **Why informational ids exist.** The unit reports faults and notices through one bitset, and a
  `problem` binary sensor that turns on because the owner overrode the bypass, entered service
  mode, switched the cooker hood on or got a filter reminder trains everyone to ignore it. The
  classification is per id and per table; its evidence — the service manual's status-LED legend
  (Warning = "Change filters or SERVICE MODE active"), the user manual's "warning message" vs
  "error message" for the two filter rows, and each text — is in the comment above the tables.
  Debatable: 77 ("filters must be replaced now", an *error message* per the user manual) stays a
  fault; 100 ("Error analysis is in progress…", undocumented) stays a fault until seen live.
- **Why `own` UTF-8.** Three table strings carry UTF-8 ("…" U+2026, "–" U+2013, "CO₂" U+2082);
  only those firmware-owned bytes are trusted to be valid UTF-8.
