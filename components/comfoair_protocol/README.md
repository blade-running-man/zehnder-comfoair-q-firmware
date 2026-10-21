# comfoair_protocol — pure, header-only ComfoNet wire format: PDO decode, RMI frames, RMI responses

> This is documentation, not code — re-read the source before relying on a detail.

Every rule about what a ComfoNet CAN id or payload *means* lives here: how a PDO broadcast is
recognised and decoded, how an RMI write or read is laid out and chunked onto CAN, how an RMI
response is recognised, reassembled and classified, and how the PDO 230 airflow-constraint bitset
reads as text. The component is header-only C++17 and includes nothing but the standard library,
so the host suites in `test/` compile exactly the bytes the firmware sends and parses. The
transport (`comfoair_can`) only moves bytes; the impure callers only wire these functions to the
bus.

## Responsibility

Owns:
- PDO CAN-id recognition, id/node extraction, the RTR poll id, and DLC-width numeric decoding
  (`pdo_decode.h`).
- RMI write frames — the timer enable/disable and direct property-write shapes, the command
  table (level, boost, away, manual, bypass, supply/exhaust-only, temperature profile, sensor
  ventilation), the 29-bit command CAN id, and multi-frame chunking (`rmi_frame.h`).
- RMI response recognition, field accessors, multi-frame reassembly, the GETTIMERENTRY reply
  layout and the GETACTIVEERRORS bitfield decode (`rmi_response.h`).
- The only two RMI *read* frames the firmware may build, and the response match/classify used by
  the probe (`rmi_read.h`).
- PDO 230 bitset to a Home-Assistant-sized string (`airflow_constraints.h`).

Does NOT do:
- Touch the bus, TWAI, tasks, queues or timeouts — `comfoair_can` (transport), `comfoair_control`
  (sending a command, sequence numbers), `comfoair_poll` (RTR sweeps), `comfoair_probe_task`
  (read requests and waiting for replies).
- Decide which command a key/value maps to, or validate entity values — `comfoair_command`
  (which builds every byte through `rmi_frame.h`).
- Know the entity list or PDO types/scales — `comfoair_registry` (generated from
  `tools/pdo_table.py`); `comfoair_state` applies the decode to the model.
- Check the CAN frame's extended/RTR flags — the caller must (see `pdo_decode.h`).
- Map active-error ids to messages (firmware-dependent table; deliberately absent).

## Public API

Namespaces: `comfoair::` (`pdo_decode.h`, `airflow_constraints.h`) and `comfoair::rmi::`
(`rmi_frame.h`, `rmi_read.h`, `rmi_response.h`). Everything is `inline`/`constexpr`, pure,
reentrant, callable from any task; no allocation except the returned `std::vector`/`std::string`.
No `esp_err_t` anywhere — failure is an empty `Frame`, a `false`, or an enum result.

| Symbol | Contract |
| --- | --- |
| `is_pdo_can_id(id)` | id bits only (`PDO_CAN_ID_MASK 0x18003fc0`, `MATCH 0x40`: bits 27..28 clear, bits 6..13 == 0x01). Caller MUST also require extended && !rtr — another node's RTR has the identical id. |
| `pdo_id_from_can_id`, `node_id_from_can_id` | bits 14..26 (13-bit PDO id, `PDO_ID_MAX` 8191; RMI and heartbeat ids stay rejected), bits 0..5. |
| `pdo_request_can_id(pdo, node)` | RTR poll id. PRECONDITION `pdo <= PDO_ID_MAX` (8191); does not clamp (masking would poll a different PDO). |
| `decode_numeric(data,len,signed,none_when_all_ones,out)` | LE int, width from DLC; only 1/2/4/8 accepted else `UNSUPPORTED_LENGTH`. Unsigned all-ones + flag → `NO_VALUE`. `out` written ONLY on `OK` — caller initialises it. |
| `decode_u64(data,len,out)` | zero-extending, accepts 1..8 bytes (PDO 230 needs 7: bit 54 is in byte 6). `false` on null/0/>8. |
| `Frame` | `std::vector<uint8_t>`. |
| `set_timer`, `clear_timer`, `set_property` | `84 15 sub prop 00×4 <timeout i32 LE> <value>` (exactly 13 bytes), `85 15 sub prop` (4), `03 unit sub prop value` (5). No padding. |
| command builders | `ventilation_level`, `boost_on/off`, `away_on/off`, `manual_on/off`, `bypass_activate/deactivate/auto`, `supply_only_on/off`, `exhaust_only_on/off`, `temperature_profile`, `temperature_passive`, `humidity_comfort`, `humidity_protection`. Return a complete frame; no failure path. |
| `command_can_id(src,dst,counter,multi,err,req,seq)` | `0x1f<<24`, `seq<<17`, `req<<16`, `err<<15`, `multi<<14`, `counter<<12`, `dst<<6`, `src`, OR-ed, each field masked. |
| `needs_multi_frame(f)` / `split_into_can_frames(f)` | > 8 bytes → 7-byte chunks prefixed by an index, `0x80` on the last. Empty input → EMPTY vector (nothing to send). |
| `get_one(unit,sub,type,prop)` | `01 unit sub type prop`; EMPTY `Frame` unless `type` is exactly `READ_TYPE_VALUE 0x10`, `_RANGE 0x20` or `_STEP 0x40`. Caller must check `empty()`. |
| `get_active_errors()` | `80 03 01`, every byte literal. |
| `read_request_can_id(src,dst,another_counter,frame,seq)` | request id; multi-frame bit follows the frame; counter is the caller's choice. |
| `is_rmi_can_id`, `is_rmi_response`, `rmi_response_has_error`, `rmi_is_multi_frame`, `rmi_src_node`, `rmi_dst_node`, `rmi_sequence` | `RMI_CAN_ID_MASK 0x1ff80000` / `MATCH 0x1f000000`; bits 14/15/16 = multi/error/is-request. |
| `response_matches(id,src,dst,seq)` | response AND src AND dst AND sequence equal. |
| `ResponseReassembler` | `feed(id,data,len)` → `NEED_MORE` / `COMPLETE` / `PROTOCOL_ERROR` (state reset); `message()` valid after `COMPLETE` until the next `feed`/`reset`. One message at a time; not thread-safe (one owner). |
| `classify_response(id,msg,len)` → `ReadAnswer{is_error,error}` | error bit clear → `{false,0}`; set → `{true,msg[0]}`, empty → `{true,0}`. |
| `parse_timer_entry(data,len,TimerEntry&)` | exactly 14 bytes else `false`; `out` untouched on failure. |
| `active_error_ids(data,len)` | byte i bit b (LSB first) → id `i*8+b`. |
| `airflow_constraints_to_string(u64)` | `"No restrictions"` without bit 45 or with no listed bit (never `"None"`: HA's MQTT sensor reads that payload as unknown), else `"Below: …"` and/or `"Above: …"` joined by `" \| "`, truncated to `AIRFLOW_CONSTRAINTS_MAX_LENGTH` (255, HA's state limit) ending in `...`. |

Key constants: `UNIT_SCHEDULE 0x15`, `UNIT_TEMPHUMCONTROL 0x1d`, `UNIT_ERROR 0x03`;
`CMD_SET_PROPERTY 0x03`, `CMD_ENABLE_TIMER 0x84`, `CMD_DISABLE_TIMER 0x85`, `CMD_GET_ONE 0x01`,
`CMD_GET_ACTIVE_ERRORS 0x80`; subunits FAN 01, BYPASS 02, TEMPERATURE_PROFILE 03, SUPPLY_ONLY 06,
EXHAUST_ONLY 07, MODE 08; `TIMEOUT_INDEFINITE = -1` (no expiry), `TIMEOUT_STICKY = 1` (see Notes).

## Implementation

- `include/pdo_decode.h` — PDO id layout and the numeric/u64 decoders.
- `include/rmi_frame.h` — write-frame builders, command CAN id, chunking.
- `include/rmi_response.h` — RMI id accessors, `ResponseReassembler`, `TimerEntry`, error bitfield.
- `include/rmi_read.h` — the two read frames, request id, `response_matches`, `classify_response`.
  Includes `rmi_frame.h` and `rmi_response.h` and restates no bit layout of its own.
- `include/airflow_constraints.h` — PDO 230 bitset → grouped text.
- `CMakeLists.txt` — `idf_component_register(INCLUDE_DIRS "include")`, no sources, no REQUIRES.

Reassembler invariants: index 0 always (re)starts a message; any out-of-order index, a frame
from a source other than the one that opened the message (on the multi-frame AND the
single-frame path), a zero-length frame, or a reassembled message of zero bytes is
`PROTOCOL_ERROR` and resets. It never looks at the sequence number — matching a reply to its
request is the caller's job (the probe filters with `response_matches()` before `feed()` and has
one request outstanding at a time).

DO NOT:
- Include an ESP-IDF (or any non-std) header in these files — the host suites stop compiling the
  firmware's own code.
- Add a read builder that takes the command byte as a parameter, or give `get_active_errors()` a
  unit argument. Any command byte >= 0x80 to unit 0x01 (NODE) can brick the unit, and 0x80/0x82 on
  NODECONFIGURATION (0x20) break its configuration; one wrong byte turns a diagnostic into that.
- Relax `get_one()`'s type check to "any nonzero": OR-ed types give a concatenated reply with no
  separator, 0x00 asks for nothing, and the empty `Frame` is what makes a bad call visible.
- Make `split_into_can_frames()` emit a frame for an empty command — a DLC-0 frame is a silent
  no-op that looks like a successful send.
- Replace `TIMEOUT_STICKY` with `TIMEOUT_INDEFINITE` for level/manual changes.
- Treat all-ones as "no value" on a signed PDO field — it is a real -1 (PDOs 49/54/55/56 =
  auto/balanced); the sentinel is unsigned only (the countdown rows flagged `none_when_all_ones`).
- Clamp/mask the PDO id in `pdo_request_can_id()`.
- Flatten the airflow-constraint direction groups, or decode bits not listed in the header.
- Loosen the RMI id test to bits 24..28 only — firmware-upload id `0x1F800000` would pass.

## Dependencies

Requires: nothing (C++ standard library only). The native env reaches the headers via
`-I components/comfoair_protocol/include` in `platformio.ini`.

Depended on by (CMake `REQUIRES comfoair_protocol`): `src/` (`main.cpp`: PDO/RMI id routing),
`comfoair_command` (every write byte), `comfoair_control` (`command_can_id`,
`split_into_can_frames`), `comfoair_poll` (`pdo_request_can_id`), `comfoair_state`
(`decode_numeric`, `decode_u64`, `airflow_constraints_to_string`), `comfoair_probe_task`
(`rmi_read.h`, `ResponseReassembler`). `comfoair_probe` deliberately does NOT depend on it, so the
probe's planner cannot reach a frame builder.

## Tests

Host suites (`pio test -e native -f <suite>`), each including the header directly:
- `test/test_pdo_decode` — id recognition/extraction, RTR round trip, LE/sign extension,
  all-ones sentinel only when unsigned, bad DLC, 7-byte `decode_u64`.
- `test/test_rmi_frame` — every command builder byte-for-byte, 13/4 byte lengths, chunking with
  final bit, empty command → no frames, `command_can_id` field placement.
- `test/test_rmi_read` — documented `get_one` example, refusal of none/OR-ed types, a sweep over
  every unit/type/property proving no `get_one` frame has a command byte >= 0x80,
  `get_active_errors` pinned by number, request id vs captures, `response_matches`,
  `classify_response`.
- `test/test_rmi_response` — id recognition (incl. firmware-upload rejection), accessors,
  single/multi-frame reassembly, out-of-order and cross-node splice rejection, GETTIMERENTRY
  parse, indefinite durations, active-error bit order.
- `test/test_airflow_constraints` — "No restrictions" without bit 45 and with no listed bit, both directions, dedup of two-bit
  reasons, preheater per direction, bit 25 naming, ignored bits, 255-char limit.

Indirectly: `test/test_command` exercises `rmi_frame.h` through `comfoair_command`.
Not covered here: the wiring in `main.cpp`, `comfoair_control`, `comfoair_poll` and
`comfoair_probe_task` (bus timing, sequence allocation, RTR/extended filtering), and the
assumptions only hardware can confirm (error code = first payload byte; which `another_counter`
reads want).

## Notes

- **Width from the DLC, not from a per-PDO table.** A PDO whose field the unit widens in a later
  firmware still decodes; a hard-coded width would silently misread it.
- **Sticky 1-second timer.** The official app sends level and manual-mode changes as a timer with
  timeout 1 s, and that is what makes them persist; an indefinite timer does not behave the same.
  Temperature profile, by contrast, is sent indefinite.
- **Supply-only and exhaust-only both carry value `01`.** `00` is the easy mistake; the
  GETTIMERENTRY read-back reports `01`.
- **The four zero bytes in ENABLETIMERENTRY** read back as zero in every capture; always send
  zeros. Trailing padding is not part of the protocol.
- **Chunk counter wrap.** The TX chunk counter is an unmasked `uint8_t`; at chunk 128 the prefix
  would be indistinguishable from "index 0, final". Unreachable (longest documented RMI response
  is 32 bytes), so left alone — the TX/RX mirror holds only up to 896 bytes.
- **Error code = first payload byte** is an observation from the owner's Q350, not documentation;
  callers keep the raw bytes beside `ReadAnswer` so a differing unit shows up on hardware.
- **`another_counter` is a parameter** because documented captures carry 1 while this firmware's
  writes send 0 and work; which one reads want is unverified.
- **Airflow-constraint wording** follows Zehnder's STATUS > FLOW RESTRICTIONS screen: "-" bits
  2–10 are "Below" (throttled), "+" bits 12–27 and 47–54 are "Above" (a control asks for more).
  Bit 25 is "Temperature Passive" (the manuals' name, matching the published select), RF sensor
  bits 23/24 keep preset vs proportional apart, and "Hood"/"CO2 Zone" bits are kept from the
  bitset although no Zehnder manual lists them.
- `parse_timer_entry()`/`TimerEntry` and `active_error_ids()` have no firmware caller today; they
  are host-tested so a future consumer is wiring, not research.
- The byte-level reference is `docs/reference/comfoair-pdo-rmi.md` (§0 bricking rules, §3 frame
  layouts, §4 bitsets); the rationale above stands without it.
