# PDO and RMI

ComfoNet carries two kinds of traffic the firmware cares about:

- **PDOs** (process data objects) — values the unit broadcasts: temperatures, fan speeds, modes,
  counters. The firmware reads them.
- **RMI** (remote method invocation) — request/response messages addressed to one node. The
  firmware *writes* through RMI (every command) and *reads* through it only in the probe task
  (owner-started walks and the periodic unit identity read).

The vendor-level protocol — every PDO id, every RMI unit, the captures and their sources — is in
the [PDO/RMI reference](/reference/comfoair-pdo-rmi). This page describes what the firmware
implements of it. All of it lives in five header-only files in
`components/comfoair_protocol/include/` (`pdo_decode.h`, `airflow_constraints.h`, `rmi_frame.h`,
`rmi_response.h`, `rmi_read.h`), which include nothing from ESP-IDF so the host test
suites compile exactly the code the device runs (`test_pdo_decode`, `test_rmi_frame`,
`test_rmi_response`, `test_rmi_read`, `test_airflow_constraints`).

## PDO identifiers

A PDO broadcast is an extended frame whose identifier has bits 27–28 clear and bits 6–13 equal to
`0x01`:

| Bits | Field |
| --- | --- |
| 0–5 | sending node id |
| 6–13 | `0x01` (constant) |
| 14–26 | PDO id, 0–8191 |
| 27–28 | `0` |

```cpp
is_pdo_can_id(id)        // (id & 0x18003FC0) == 0x00000040
pdo_id_from_can_id(id)   // (id >> 14) & 0x1FFF
node_id_from_can_id(id)  // id & 0x3F
pdo_request_can_id(pdo, node)  // (pdo << 14) | 0x40 | node  -- sent as an RTR
```

The field is 13 bits because accessory PDOs exceed 1023 (CO2 sensors 1024..1031, ComfoClime 4145..4208). Requiring only bits 27–28 clear still excludes the other extended-id families: RMI frames carry `0x1F` in bits 24–28 and node heartbeats are `0x10…`, so both have bit 28 set.

The identifier test alone is not sufficient: a remote-transmission request has the same
identifier as the broadcast it asks for. The receive callback therefore also requires an extended
identifier and no RTR flag before it decodes anything. Requesting PDO 65 from node `0x2A` is an
RTR with identifier `0x0010406A`; the unit answers with the ordinary broadcast.

## Decoding a PDO payload

The width of a value comes from the frame's **data length**, never from a hard-coded per-PDO width,
so a field the unit widens in a later firmware still decodes.

`decode_numeric(data, len, is_signed, none_when_all_ones, out)`:

- accepts lengths 1, 2, 4 and 8 only; anything else is `UNSUPPORTED_LENGTH`;
- reads little-endian;
- sign-extends when the entity is signed;
- returns `NO_VALUE` for the all-ones pattern **only** on an unsigned field that the registry
  flags `none_when_all_ones` (the countdown rows the table marks, where all-ones means "nothing
  pending"). On a signed field
  all-ones is a legitimate `-1` and is never treated as "no value". (The auto/balanced value of
  PDOs 49, 54 and 55 is not affected either way: the registry declares those PDOs as enums, which
  are read unsigned, so it arrives as the listed value 255.)

`decode_u64(data, len, out)` is the zero-extending variant for 1 to 8 bytes, used for the PDO 230
bitset, which routinely arrives in 7 bytes.

What a PDO *means* — signedness, divider, sentinel, the entity kind — is not in these headers. It
is a property of each entity in the registry generated from `tools/pdo_table.py`
(see [Entity registry](/development/entity-registry)), and `comfoair_state_apply_pdo()` applies one
frame to **every** entity that claims that PDO: PDO 337, for example, feeds three entities (the
boost and away timer bits and the `away_mode` switch).

| Entity kind | How the bytes become a value |
| --- | --- |
| numeric | `decode_numeric`, then divided by the entity's divider |
| binary / switch | unsigned value compared with the entity's match value, optionally negated |
| enum | the wire **value** looked up in the entity's value list (operating mode: 1, 5, 6, 11, 255) |
| select | the wire byte used as an **index** into the option list (bypass mode: 0/1/2) |
| special | PDO 230, rendered by `airflow_constraints_to_string()` (below) |
| derived | PDOs 49 + 65 compute the fan and `ventilation_mode`; PDOs 54 + 55 compute `balance_mode` once both have been seen |

A frame that cannot be decoded, a sentinel, or an enum value the table does not list leaves the
entity **without a value** rather than with a guessed one. A PDO that no entity claims is logged at
DEBUG and otherwise ignored — the unit broadcasts more than the firmware publishes. A value counts
as *changed* only when it differs from the stored one; repeated broadcasts of the same value do not
produce updates. The complete list of entities and their PDOs is the generated
[entity reference](/reference/entities).

### Airflow constraints (PDO 230)

`airflow_constraints_to_string()` turns the 64-bit bitset into the text of the
`airflow_constraints` sensor — the unit's answer to "why is it not running at the requested
airflow". The bit indices are those recorded in the [PDO/RMI reference](/reference/comfoair-pdo-rmi); the wording and direction follow the unit's
own STATUS › FLOW RESTRICTIONS screen.

- **Bit 45 gates the list.** Without it the text is `No restrictions` (aiocomfoconnect reads a
  clear bit 45 as no constraint list; the Q350 reads 0 at rest).
- Bits 2–10 are grouped under `Below:` — the unit is throttled below the request (for example
  `Frost Protection`, `Noise Guard`, `Bypass`).
- Bits 12–27 and 47–54 are grouped under `Above:` — a control is asking for more than the request
  (for example `Humidity Comfort`, `Temperature Passive`, `CO2 Zone 1`).
- Both groups present: `Below: … | Above: …`. Bit 45 set but no decoded bit: `No restrictions`
  as well, never `None` — Home Assistant's MQTT sensor takes that payload as "unknown".
- Undocumented bits are ignored. The text is cut to 255 characters (ending in `...`), the limit
  Home Assistant puts on a state.

## RMI identifiers

Every RMI frame — request or response — carries its routing in the 29-bit identifier, built by
`command_can_id()` in `rmi_frame.h`:

| Bits | Field | What the firmware sends |
| --- | --- | --- |
| 0–5 | source node | `0x2A` |
| 6–11 | destination node | `0x01` (commands, unit identity read); the walked node and the other census nodes (probe walk) |
| 12–13 | "AnotherCounter" | `0` (commands, unit identity read); `0`–`3` chosen per probe walk |
| 14 | multi-frame | set when the message is longer than 8 bytes |
| 15 | error | `0` on requests; set by the unit on an error response |
| 16 | is-request | `1` |
| 17–18 | sequence number | see below |
| 19–23 | `0` | — |
| 24–28 | `0x1F` | — |

`is_rmi_can_id()` requires bits 24–28 to be `0x1F` **and** bits 19–23 to be clear; the second half
excludes the firmware-upload identifier `0x1F800000`.

### Sequence numbers

The 2-bit sequence number is taken fresh for **each RMI message** from one counter,
`comfoair_control_next_sequence()`, shared by the command sender and the probe. The chunks of one
message share it; two messages of one command do not. Because a response copies the request's
sequence, it is the only field that tells two outstanding requests to the same node apart, and a
single counter is what keeps an owner's command and a probe read from carrying the same number.
With two bits the number repeats after four messages; the probe waits at most 300 ms for a reply,
so a collision needs four further RMI messages inside one wait — at least two commands, since a
command is at most three messages.

## RMI request payloads

The firmware can build exactly these payloads, all through the builders in `rmi_frame.h` and
`rmi_read.h`:

| Command | Bytes | Length | Used by |
| --- | --- | --- | --- |
| ENABLETIMERENTRY | `84 15 <subunit> <property> 00 00 00 00 <timeout i32 LE> <value>` | 13 | commands |
| DISABLETIMERENTRY | `85 15 <subunit> <property>` | 4 | commands |
| SET PROPERTY | `03 <unit> <subunit> <property> <value>` | 5 | commands (unit `0x1D`) |
| GET ONE | `01 <unit> <subunit> <type> <property>` | 5 | probe; type `0x10` value, `0x20` range, `0x40` step |
| GETACTIVEERRORS | `80 03 01` | 3 | probe and identity read |

Unit `0x15` is SCHEDULE and `0x1D` is TEMPHUMCONTROL. The timer's timeout is a signed 32-bit
number of seconds; `-1` (`FF FF FF FF`) means no expiry. There is no trailing padding: an enable is
exactly 13 bytes and a disable exactly 4. How each command and operation maps onto these is on the
[Commands](/protocol/commands) page.

::: warning What the firmware cannot send
Any command byte of `0x80` or above sent to unit `0x01` (NODE) can brick the ventilation unit, and
`0x80`/`0x82` on NODECONFIGURATION (`0x20`) break its configuration. The firmware's only
`>= 0x80` frames are `84`/`85` on SCHEDULE (`0x15`) and the fixed `80 03 01` on ERROR (`0x03`).
`rmi_read.h` has no builder that takes a command byte or gives `get_active_errors()` a unit
argument, and `test_rmi_read` and `test_command` pin the unit byte of every frame the firmware can
produce.
:::

## Multi-frame messages

A CAN frame carries at most 8 bytes. `split_into_can_frames()` sends a message of 8 bytes or less
verbatim in one frame. A longer message is cut into 7-byte blocks, each prefixed by a block counter
starting at 0, with `0x80` set on the last block's prefix, and the multi-frame bit is set in the
identifier. Every block goes out under the same identifier.

A 13-byte ENABLETIMERENTRY therefore becomes two CAN frames. Setting ventilation level 2 with
sequence number 1:

```text
id 0x1F03406A   00 84 15 01 01 00 00 00     block 0: prefix 00 + bytes 0..6
id 0x1F03406A   81 00 01 00 00 00 02        block 1 (last): prefix 81 + bytes 7..12
```

An empty message is refused rather than sent as a zero-length frame, which would look like a
successful send of nothing.

## RMI responses

**Commands do not listen for responses.** Every write is fire-and-forget: the firmware does not
wait for, match or parse the unit's reply, and the only confirmation of a command is the unit's
own PDO broadcast arriving afterwards.

The **probe task** is the only consumer of RMI responses (see [Probe](/protocol/probe)). While a
walk or the unit identity read is running, the receive callback queues every RMI response addressed
to `0x2A`, and the waiting request accepts a frame only if `response_matches()` holds — an RMI response (is-request bit
clear) whose source is the node asked, whose destination is `0x2A`, and whose sequence is the
request's. Matching frames are fed to a `ResponseReassembler`:

- a single-frame response is complete as it stands; an empty one is a protocol error;
- a multi-frame response is collected block by block; index 0 always starts a new message, an
  out-of-order or repeated index is a protocol error, and the message is complete at the block
  with `0x80` set;
- a frame from a different source node in the middle of a message is rejected, on both paths, so
  two interleaved replies cannot splice together.

A complete message is classified by `classify_response()`: error bit clear means data; error bit
set means the **first payload byte is the error code** (an empty error payload reads as code 0).
That position was an assumption in the sources; a live walk on a Q350 confirmed it
([findings](/reference/rmi-probe-findings-q350)). The raw bytes are always kept beside the code.

::: details Present in the headers but not used by the firmware
`rmi_response.h` also contains `parse_timer_entry()` (the 14-byte GETTIMERENTRY reply: enabled,
configured and remaining seconds, value) and `active_error_ids()` (the GETACTIVEERRORS bitfield as
a list of ids). Both are host-tested, but no firmware code calls them: the firmware never sends
GETTIMERENTRY, so it does not know how long a running boost or away timer has left, and the probe
report decodes the error bitfield itself.
:::
