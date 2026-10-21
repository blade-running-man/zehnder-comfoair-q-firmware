# comfoair_api — the JSON projection of the state model and the entity registry

> This is documentation, not code — re-read the source before relying on a detail.

`comfoair_api` turns the state model (`comfoair_state`) and the generated entity registry
(`registry_generated.h`) into JSON for the HTTP surface: the whole state document
(`GET /api/state`), one entity (`GET /api/entities/{key}`), the registry
(`GET /api/entities`), and the WebSocket push envelope (`{"type":"state"|"delta",...}`). It
exists so that every JSON consumer of the model renders the same spelling from one place
instead of each transport formatting values its own way. It is pure apart from the state
lock: it writes into caller-supplied buffers, never allocates, and is host-tested.

## Responsibility

Owns:

- The value spelling of the state document: number via `%g`, JSON boolean, quoted string,
  `null` for "known entity, no reading yet", and the fan as an object
  `{"on":bool,"speed":n,"preset":string|null}` (or `null` before PDO 65 was seen).
- The shape of the entity registry document (`key`, `name`, `kind`, `pdo`, `min`, `max`,
  `unit`, `deviceClass`, `stateClass`, `icon`, `entityCategory`, `writable`, `enabledByDefault`,
  `options`), which
  must match what `tools/generate_registry.py` declares in `web/src/api/entities.ts`.
- The delta rendering for the WebSocket and the clearing of the **PUSH** consumer's change
  marks (`COMFOAIR_STATE_CONSUMER_PUSH`).
- The push envelope and the "an empty delta is not a frame" rule.
- The public name `comfoair_api_escape_json()` (a forwarder only).

Does NOT do:

- Store or decode values — `comfoair_state` owns the model and PDO decoding.
- The escaping rules themselves — `comfoair_json` (`comfoair_json_escape()`), next to the
  reader that must undo them.
- MQTT state payloads — despite the header's wording, MQTT does not call this component:
  `comfoair_mqtt_publish.c` renders per entity through `comfoair_ha_state_payload()`
  (`comfoair_ha`) and uses its own `COMFOAIR_STATE_CONSUMER_MQTT` marks.
- HTTP status codes, routing, access policy, the 404 for an unknown key, the keep-alive
  `ping` frame — all in `comfoair_http` (`comfoair_http.c`, `comfoair_http_gate.c`,
  `comfoair_http_read.c`, `comfoair_http_ws.c`).
- `/api/status`, config, log and OTA documents — `comfoair_wire`, `comfoair_config`,
  `comfoair_log`, `comfoair_http_ota.c`.

## Public API

Header: `include/comfoair_api.h` (C linkage; implementation is C++).

| Symbol | Contract |
| --- | --- |
| `size_t comfoair_api_render_state(char *out, size_t cap)` | Whole state as one JSON object, one key per registry entity, in registry order. Holds the state lock for the whole document. Returns bytes written excluding NUL, or **0 when the document did not fit** (buffer still NUL-terminated, contents are a truncated prefix — do not send it). **Touches no change marks**: a REST reader is not a push client. Any task. |
| `size_t comfoair_api_render_delta(char *out, size_t cap)` | Same shape, only entities whose PUSH mark is set. Returns `2` (`"{}"`) when nothing moved, 0 on overflow. Clears the PUSH marks (also on overflow). Host tests only; the device pushes through `render_push`. |
| `size_t comfoair_api_render_push(char *out, size_t cap, bool full)` | `{"type":"state","values":{...}}` when `full`, else `{"type":"delta","values":{...}}`. Returns 0 when `out == NULL`, `cap` is below prefix + 4, the body overflowed, or a delta is empty; the caller treats 0 as "nothing to send". A full state always renders when it fits. Clears PUSH marks either way. Called from the `comfoair_ws` task. |
| `size_t comfoair_api_render_value(const char *key, char *out, size_t cap)` | `{"key","name","unit","value"}` for one entity. Returns 0 and writes nothing for `NULL` or an unknown key — the caller's signal for 404 versus 200-with-`null`. Also 0 on overflow (output then partially written). Does **not** touch change marks. |
| `size_t comfoair_api_render_entities(char *out, size_t cap)` | The registry as a JSON array. Reads only the constant generated table, so takes no lock. 0 on overflow. |
| `size_t comfoair_api_escape_json(const char *in, char *out, size_t cap)` | Forwards to `comfoair_json_escape()`: no surrounding quotes, non-ASCII as `\uXXXX`, all-or-nothing per escape, invalid code points and lone surrogates dropped. Returns bytes written. Used directly by `comfoair_http_ota.c`. |

No `esp_err_t` anywhere: the only failure is "did not fit", and it is always the return value
0. A caller that sends a 0-length result sends an empty body; check for 0.

## Implementation

- `comfoair_api.cpp` — everything: the `Writer`, `kind_name()`, `render_fan()`,
  `render_object()` and the six public functions.
- `include/comfoair_api.h` — the public contract.
- `CMakeLists.txt` — `REQUIRES comfoair_state comfoair_registry comfoair_lock comfoair_json`.

Internals worth knowing:

- **`Writer`** (anonymous namespace) — the bounded append every renderer goes through. `used`
  never exceeds `cap - 1`, the buffer is always NUL-terminated, and `overflowed` latches the
  moment anything does not fit. Every public renderer returns `overflowed ? 0 : used`, so a
  truncated document can never be mistaken for a short valid one.
- **`render_object(out, cap, changed_only, consume)`** — the single body for both the full
  document and the delta, so a client never sees one shape on connect and another afterwards.
  It takes `comfoair_lock()` once for the whole walk (the lock is recursive, so the nested
  `comfoair_state_get()` takes are fine). When `consume` is set — `render_push` of either kind
  and `render_delta`, never `render_state` — it clears the PUSH marks before unlocking, so a PDO
  cannot land between the walk and the clear and lose its mark unrendered.
- **`render_push`** copies the prefix, renders the body into `out + plen` with `cap - plen - 2`
  so the closing `}` and NUL always fit, and recognises an empty delta by `body == 2`.
- **`kind_name()`** — the `kind` strings; must equal the strings the generator writes into
  `entities.ts`. Returns `nullptr` (rendered `null`) for an unknown enumerator; the native
  build's `-Werror=switch` catches a new kind without a `case`.
- `put_number_or_null()` tests NaN with `std::isnan` — the registry uses NaN as "the table gives
  no min/max". `pdo == -1` (the C sentinel) is rendered `null`.
- `put_escaped()` escapes through a 256-byte stack temporary; a registry string whose escaped
  form exceeds that is shortened by whole escapes without setting `overflowed`. Registry strings
  are short today.

DO NOT:

- Omit a key for an entity with no reading. `null` means "no reading yet", a missing key means
  "no such entity"; collapsing them hides a dead bus behind what looks like a typo.
- Render an absent string as `""`. Absent metadata is `null`; `""` is a value.
- Return the truncated length on overflow. A short plausible length was served as 200 OK with
  JSON that stopped mid-object; 0 is the only way the caller can tell.
- Lock per entity instead of per document: a CAN frame landing between two takes produces a
  snapshot of a moment that never existed.
- Clear all consumers' marks (or the shared array) from here. Only
  `COMFOAIR_STATE_CONSUMER_PUSH` is this component's; clearing MQTT's would make whichever
  transport rendered first steal the other's updates.
- Re-implement escaping here. A device name rendered here returns via `POST /api/config`; two
  encoders are two chances for the round trip to alter the name on every save.
- Split `render_object` into separate state/delta renderers — they would drift.
- Clear the PUSH marks from `render_state`. It serves `GET /api/state` and the boot size check,
  neither of which reaches a WebSocket client; a REST poll landing before the next push round
  would take the change from every open socket until the value moved again.

## Dependencies

Requires (`CMakeLists.txt`): `comfoair_state` (values, fan, change marks, `comfoair_entity_by_key`),
`comfoair_registry` (`registry_generated.h`: `COMFOAIR_ENTITIES`, `COMFOAIR_ENTITY_COUNT`),
`comfoair_lock` (recursive state lock, a no-op on the host), `comfoair_json` (escaping).

Depended on by: `comfoair_http` (`REQUIRES comfoair_api`) — `comfoair_http_read.c` calls
`render_state` (`GET /api/state`; `comfoair_http.c` calls it once more for a boot-time size
check), `render_entities` (`GET /api/entities` and the boot check), `render_value`
(`GET /api/entities/{key}`, after its own `comfoair_state_get()` existence check);
`comfoair_http_ws.c` calls `render_push` (the `comfoair_ws` task, `full` when the socket count
grew); `comfoair_http_ota.c` calls `comfoair_api_escape_json`. Both share the 48 KiB
`g_http_scratch` buffer (`COMFOAIR_API_DOC_BUFFER_SIZE`, `comfoair_api.h`) defined in
`comfoair_http.c`. The native env exposes the header via `-I components/comfoair_api/include` in
`platformio.ini`.

Headroom: `GET /api/entities` is the largest document.
`test_the_entities_document_fits_the_buffer_with_a_quarter_spare` fails when less than a quarter
of `COMFOAIR_API_DOC_BUFFER_SIZE` is spare after rendering it on the host, which is the signal to
raise the buffer. A batch of new registry rows is measured against that, not assumed to fit:
re-measure by rendering `/api/entities` on the host (`comfoair_api_render_entities()` in the
`test_api` suite) rather than trusting a figure written down earlier.

## Tests

- `test/test_api/test_api.cpp` (host, `pio test -e native -f test_api`) — null-not-absent,
  number/bool/text spelling, the fan object, every entity present, overflow reports 0 and never
  writes past the end, the registry document's fields (`kind`, `pdo` null for -1, `min`/`max`
  null for NaN, `stateClass`), no secret in any rendering, broker diagnostics are not entities,
  `render_value` with metadata / null / unknown key, deltas (only what changed; an identical
  reading is not a change; taking a delta clears it; a full push also clears it; a REST
  `render_state` does not consume the push delta; the fan in a delta), and the push envelope
  (state/delta type, empty delta renders 0, full push always renders). Escaping cases (quotes,
  control chars, UTF-8, truncation inside an escape, invalid code points, lone surrogates,
  surrogate pairs) are exercised through `comfoair_api_escape_json`; `test/test_json` covers
  `comfoair_json_escape` itself.
- Not covered: the device-side locking. The lock is a no-op on the host and the suites are
  single-threaded, so no test can show that the PUSH marks are cleared inside the lock; the
  source and its `DO NOT` comment are what hold that. The `comfoair_http` call sites are not
  covered either, because no host suite builds `comfoair_http`.

## Notes

- **One mark set per consumer, and this component owns the PUSH one.** `comfoair_state` keeps
  separate change marks for PUSH and MQTT; a renderer that cleared a shared set would let the
  first transport to render take updates the other never saw, silently and in either direction.
- **Only a push rendering clears the PUSH marks.** The marks record what the WebSocket
  clients have not been sent yet, so only a rendering that goes to them may clear them. A full
  push clears them because a client just handed the whole state must not get a delta repeating
  it next round. `GET /api/state` leaves them alone: if it cleared them, a REST poll (Home
  Assistant, a page load, a curl) landing in the gap before the 250 ms-floored push round would
  take the change from every open socket. The web UI fetches `GET /api/state` whenever its
  socket opens, so every page load would have done that to every other open tab.
- **The envelope is not decoration.** The socket carries full states, deltas and keep-alives;
  a client must not infer which from size or content.
- **Empty delta returns 0, full state never does (if it fits):** a freshly connected client
  needs the full document whether or not anything moved, while an empty delta is not worth a
  frame — the HTTP task then decides whether a `ping` is due.
- **`%g`** keeps `21.5` as `21.5` and `3` as `3` (not `3.000000`); `comfoair_ha` makes the same
  choice for MQTT payloads so the two transports spell numbers alike. `%g` carries six
  significant digits.
- **Why REST's registry document carries `stateClass` etc.:** `comfoair_ha` publishes the same
  registry fields in discovery, and REST and MQTT must cover the same ground (AGENTS.md
  criterion 3); a client without `state_class` gets no Home Assistant statistics.
- **The header's opening comment is stale:** it says MQTT "will publish its fields" from here
  (it does not). Trust the code and this README.
