# comfoair_probe_report — what the RMI probe's bytes mean, rendered as streamed JSON

> This is documentation, not code — re-read the source before relying on a detail.

The RMI probe (`comfoair_probe`) walks node/unit/subunit/property space and stores raw answers:
value, range and step bytes, error codes, timeouts. This component turns those results into the
JSON report served at `GET /api/probe`. It holds the one catalogue of known RMI properties (names,
formats, scales, enum labels), the decoders that read bytes by catalogue or by shape, and a
streaming JSON emitter. It needs no heap and no buffer the size of the report. It is pure (no
FreeRTOS, no IDF calls), so the meaning of the bytes is host-tested apart from which bytes were
asked for. It also owns the one firmware-version-word decoder, which `GET /api/system` uses too.

## Responsibility

Owns:
- The **catalogue** of known RMI properties (`comfoair_probe_catalogue.cpp`). It is the only list
  of them. The web UI renders whatever names the JSON carries and keeps no copy.
- The documented unit names, the RMI error-code names, and the redaction rule for the
  maintainer password.
- Value decoding: little-endian integers, strings, version words, enum labels, the
  uncatalogued-shape heuristic, and the "range says unsigned" correction (`settle_sign`).
- The report's JSON schema and key order, emitted in pieces of at most `EMIT_PIECE` bytes.
- `comfoair_probe_version_text()`, the one `"R1.11.0"` formatter.

Does NOT do:
- Walk the bus, choose what to ask, or store answers. `comfoair_probe` does that (the pure walk
  and the results struct).
- Locking, timing, the node census, `another_counter`. `comfoair_probe_task` does those: it holds
  the probe lock for the whole render and fills `comfoair_probe_meta_t`.
- HTTP chunking and status codes. `comfoair_http` does that (`comfoair_http_probe.c`, via
  `comfoair_probe_task_render()`).
- Registry entities. RMI properties are not PDOs or commands and are not in
  `tools/pdo_table.py`, so the catalogue is not a second entity list.
- The `GET /api/system` document itself. `comfoair_sysinfo` builds it and only borrows the version
  formatter.

## Public API

`include/comfoair_probe_report.h` (C linkage):

| Symbol | Contract |
| --- | --- |
| `comfoair_probe_meta_t` | What the device half knows and the pure walk does not: `elapsed_ms`, `another_counter` (CAN-id bits 12-13), `nodes_seen` (ascending) + `nodes_seen_count`, then the PDO census: `pdo_summary`, `pdo_rows`, `pdo_row`, `pdo_ctx` (appended last, so older initialisers zero them). The caller owns the arrays, and they must stay valid for the whole render. A NULL `meta` renders zeros, an empty `nodes_seen` and no `pdos`. |
| `comfoair_probe_pdo_summary_t` | `uptime_ms` (u32, wraps after 49.7 days), `frames` (every PDO frame since boot), `distinct` (PDO numbers heard), `overflow` (distinct minus rows recorded). NULL `pdo_summary`: no `pdos` key at all. |
| `comfoair_probe_pdo_row_t` | One census row: `pdo`, `known`, `count`, `first_ms`/`last_ms` (the summary's clock), `dlc_mask` (bit n: length n, 0..8), `node_mask` (bit n: node n, 0..63), `last_len` + `last[8]`. Declared here, not in the census component, so the renderer depends on no store. |
| `comfoair_probe_pdo_row_fn` | `bool (*)(void *ctx, size_t i, comfoair_probe_pdo_row_t *out)`: fills row `i` of `pdo_rows`, ascending by PDO, on the render's task under the caller's lock. Returning false skips that row; the rest still render. A NULL `pdo_row` renders `rows: []`. |
| `comfoair_probe_write_fn` | `bool (*)(void *ctx, const char *text, size_t len)`. Gets each piece, `len` 1..`EMIT_PIECE` (512). Returning false means "client gone" and stops the render. |
| `comfoair_probe_render(results, meta, write, ctx)` | Streams the whole report through `write`. Returns false if `results` or `write` is NULL, or if any write failed. The output is then incomplete and the caller must not treat it as a document. Takes no lock: the caller holds whatever guards `results` for the whole call. Runs on the caller's task (the HTTP task in practice). |
| `comfoair_probe_version_text(raw, out, cap)` | Formats a version word as `<kind><major>.<minor>.<patch>` (`0xC0102C00` -> `"R1.11.0"`). Returns the length without the NUL, or 0 if `cap` is too small (`nullptr, 0` is legal and returns 0). A `cap` of 16 always fits. |

Report shape (top-level key order is fixed): `state`, `failure`, `node`, `another_counter`,
`elapsed_ms`, `progress{requests,timeouts,node,unit,subunit,property}`, `nodes_seen[]`,
`units[]{node,unit,name,status,subunits,absent_properties}`,
`properties[]{node,unit,subunit,property,name,access,format,unit_label,value,range,step}`,
`active_errors`, `pdos` (only when the device supplies a census), `truncated`. `pdos` is
`{uptime_ms,frames,distinct,overflow,rows[]{pdo,known,count,first_ms,last_ms,dlc[],nodes[],last}}`:
`dlc` and `nodes` are the set bits of the masks, ascending; `last` is lowercase hex of `last_len`
bytes (`""` for 0). Every answer slot (`value`/`range`/`step`/`active_errors`) takes one
of these forms: `null` (not asked), `{"timeout":true}`, `{"error":N,"error_name":..,"hex":..}`,
`{"redacted":true}`, or the data form with `hex` and `len` plus decoded fields. The full schema is
described in `docs/protocol/probe.md`, "The report", and pinned byte for byte by
`test_full_render_matches_the_schema_exactly` (the census section by `test_probe_report_census`).

Private seams (`comfoair_probe_report_internal.h`, namespace `probe_report`, not under
`include/`): `Format` (`None, String, U8, U16, I16, U32, Version, Enum`), `Property`,
`catalogue_find`, `unit_name`, `error_name`, `is_redacted`, `Number`/`Decoded`/`Text`,
`decode_value`, `settle_sign`, `decode_int`, `version_text`, `format_number`, `Emitter`,
`EMIT_PIECE = 512`. The host suites include it by relative path. 512 is enough to keep the HTTP
task's stack small while using far fewer chunks than one per token.

## Implementation

- `comfoair_probe_report.cpp` holds the renderer: head, units, properties, active errors,
  `truncated`, and `put_value`/`put_range`/`put_step`/`put_not_data`.
- `comfoair_probe_report_pdos.cpp` holds `put_pdos`, the census section: summary, then one row
  per `pdo_row` call, streamed through the same `Emitter`. A refused row is skipped (one record the
  device cannot copy must not cost the rest); the masks are walked only up to their meaningful top
  bit (length 8, node 63) and `last_len` is clamped to the 8 bytes of `last`.
- `comfoair_probe_decode.cpp` holds the decoders, the uncatalogued heuristic `guess()`, number
  formatting, the `Emitter`, and the public `comfoair_probe_version_text` wrapper.
- `comfoair_probe_catalogue.cpp` holds the `CATALOGUE[]` rows (`ROW`/`ENUM`/`ZONE` macros), the
  `UNITS[]` names, `error_name`, `is_redacted`, and the `shared_identity` node rule.
- `comfoair_probe_report_internal.h` holds the private seams listed above.

Flow per property entry (`put_entry`):
1. `catalogue_find(target, node, unit, subunit, property)` finds the row. On the target node, any
   row matches. On another node, only NODE 0x01/01 properties 0x04 (serial), 0x06 (firmware) and
   0x14 (unit name) match.
2. If the value is DATA, `decode_value` runs: the catalogue format if there is a row, else
   `guess()`. Then, if the range is DATA too, `settle_sign` may re-read I16 as U16.
3. The renderer emits the value, then the range (min/max at the value's width, only when
   `len == 2 * width` and nothing was truncated), then the step (one value at the value's format).
   Every data form keeps its raw `hex`, so a wrong decode misleads but loses nothing.
4. `is_redacted` overrides value, range **and** step with `{"redacted":true}`. The range and step
   are covered too because a unit may echo the value for any request type.

Invariants:
- Counts are clamped to their arrays (`COMFOAIR_PROBE_MAX_UNITS`, `MAX_ENTRIES`, the
  `bytes`/`errors` sizes), so a corrupt count never reads past `results`.
- A numeric whose received width differs from its format's width gets `number: null`, never a
  partial decode. Truncated (`len > stored`) values get no number either.
- `format_number` uses integer arithmetic, rounds half away from zero, and always prints exactly
  one decimal when `divisor > 1`. The sign is printed separately so -5/10 comes out as `"-0.5"`.
- Device strings are escaped as Latin-1 (every byte < 0x20 or >= 0x7F becomes `\u00XX`), which is
  always valid JSON. Catalogue strings are our own UTF-8 and pass bytes >= 0x80 through.
- `active_errors.ids` walks each byte LSB-first, so byte i bit b is id `i*8+b`.
- Once a write fails, the `Emitter` stays failed and every later `put` does nothing. The render
  loops check `ok()` between entries.

Concurrency: none inside. `comfoair_probe_task_render()` holds the probe lock for the whole
streamed render, so a slow client delays the walk but cannot corrupt the results it reads.

DO NOT:
- Move `comfoair_probe_report_internal.h` into `include/`. The public contract is one render
  function, and the web UI must take names from the JSON, not from these tables.
- Make the maintainer-password redaction a catalogue column. It must survive a row being deleted
  or renamed. It also applies on **every** node, not only the target.
- Drop the `nullptr`-named "??" rows. Their documented format (e.g. INT16) beats a guess.
- Relax the width check in `decode_value` to `>=`, or restore FILTER 0x02 to U8. The live unit
  answers two bytes there, and a wrong width means no number at all.
- Lower the text heuristic below "at least two printable bytes plus a NUL". Small little-endian
  INT16s end in 00 (`6400` would read as `"d"`).
- Widen `settle_sign` to U8/U32. No live range of those widths has contradicted its format.
- Escape catalogue strings as device bytes. `°` would become `Â°`.
- Use `ANY_SUBUNIT` outside CO2ZONES. It names values on subunits nobody has seen.
- Walk `active_errors` MSB-first. Every id would land on its mirror bit.
- Copy the version decode into `comfoair_sysinfo`. One firmware must never print two ways.

## Dependencies

- `REQUIRES comfoair_probe` (`CMakeLists.txt`) for the results, entry, value and aux types and
  their size constants. Nothing else: no IDF, no FreeRTOS, no heap.
- Depended on by:
  - `comfoair_probe_task`, which calls `comfoair_probe_render` under its lock and exposes
    `comfoair_probe_task_render()`.
  - `comfoair_http`, through `comfoair_http_probe.c` (`GET /api/probe`, streamed chunk by chunk).
  - `comfoair_sysinfo`, which calls `comfoair_probe_version_text` for `GET /api/system`.
- `platformio.ini` `[env:native]` adds `-I components/comfoair_probe_report/include`.

## Tests

- `test/test_probe_report/test_probe_report.cpp` covers the decoders (strings, I16 tenths,
  little-endian widths, version words, enum labels, the heuristic) and the catalogue node rule.
  It also covers unit and error names, password redaction across value, range and step, escaping,
  the `active_errors` forms, failed or truncated walks, an exact full-render schema match, a
  failed write stopping the render, and the size and joining of pieces.
- `test/test_probe_report_census/test_probe_report_census.cpp` covers the `pdos` section: absent
  without a summary, spliced byte-exact before `truncated` (empty rows, two rows, the top mask
  bits, an empty payload, garbage mask bits and a corrupt `last_len`), a refused row skipped, a
  failed write mid-rows stopping the render and the row fetches, and 256 worst-case rows streamed
  in pieces of at most `EMIT_PIECE`.
- `test/test_probe_report_live/test_probe_report_live.cpp` replays vectors from a real Q350
  R1.11.0 report. It covers the I16/U16 sign settlement (`0000ffff` vs genuinely signed ranges),
  two-byte NUL-ending numbers that are not text, FILTER 0x02 as U16, the HMI, POSTHEATER and
  airflow-limit rows, the CO2ZONES wildcard, and undocumented error codes.
- `test/test_sysinfo/test_sysinfo.cpp` covers the public `comfoair_probe_version_text`, including
  `cap` edge cases.

Not covered here: the locked, streamed glue (`comfoair_probe_task_render`, which has no host suite
of its own) and the HTTP chunking in `comfoair_http_probe.c`. Whether the catalogue's names and
formats are right is pinned only against the documented table and one live unit.

## Notes

- **Why a component of its own:** with decoding separate from the walk, the catalogue and the
  decoders can be tested against hand-built results. A render-level test alone cannot tell "wrong
  divisor" from "the formatter drops the sign", because both show up as one wrong character in a
  multi-kilobyte string.
- **Why streamed:** the report can be larger than the HTTP layer's shared scratch buffer. Pieces
  of at most 512 bytes need only a small stack.
- **Key order is part of the schema.** Owners diff two walks' downloaded JSON by eye, and a stable
  order is what makes that diff readable.
- **Node rule:** other nodes (e.g. a LAN C) share only the serial, firmware and name properties
  with the unit. The documented table gives their other NODE properties different, unexplained
  values, so applying the unit's names to them would label real values wrongly. `unit_name`
  likewise names only NODE on a non-target node.
- **"(inferred)" names** come from reading live values, not from documentation. Do not promote
  them to Home Assistant entities without a second source.
- **Access** is copied as documented. A blank stays `null` even where a write is presumed.
- **Fallbacks:** an unnamed `state` enum value renders as `"idle"`, and an unnamed unit status
  as `"timeout"`. `error_name` returns `"unknown"` for undocumented codes.
- **Divisor and failed values (observed):** if a property's value is not DATA (error or
  timeout), `put_entry` keeps the catalogue format but leaves `divisor` at 1. A DATA range for that
  entry is then printed unscaled (e.g. 150 rather than 15.0).
