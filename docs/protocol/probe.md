# Probe

The RMI probe is a **read-only walk** of the ventilation unit's RMI tree: it asks every unit,
subunit and property in a bounded range what it holds, and keeps the answers as a report. It exists
to find out what a particular unit and firmware actually support — which settings exist, their
values, ranges and steps — instead of trusting documentation written for other models. What it found
on the owner's Q350 is in the [probe findings](/reference/rmi-probe-findings-q350).

Beside the walk, the same report carries a passive **PDO census**: every PDO number the bus has
broadcast since boot, with how often, how long and from which node. It needs no walk and sends
nothing (see [The PDO census](#the-pdo-census)).

It is four components:

| Component | Role |
| --- | --- |
| `comfoair_probe` | the walk as a pure state machine: what to ask next, when to retry, when to give up (host suites `test_probe_scan`, `test_probe_scan_bounds`) |
| `comfoair_probe_report` | the catalogue of known RMI properties, the decoders and the JSON renderer (host suites `test_probe_report`, `test_probe_report_live`, `test_probe_report_census`) |
| `comfoair_pdo_census` | the PDO census as a pure, fixed-size record (host suite `test_pdo_census`) |
| `comfoair_probe_task` | the device half: one task that puts requests on the bus, waits for replies and hands them back, and the census fed from the CAN receive task; it decides nothing |

## It only reads

A probe request is one of exactly two shapes, built by `rmi_read.h`:

```text
01 <unit> <subunit> <type> <property>    GET ONE, type 10 value / 20 range / 40 step
80 03 01                                 GETACTIVEERRORS
```

There is no field anywhere in the probe that could carry a command byte, and `get_one()` returns
an empty frame for any type other than the three reads; the probe task logs such a request, sends
nothing and records it as a timeout (`probe_bus_ask()` in `comfoair_probe_task_bus.cpp`).
This matters because a `>= 0x80` command to unit `0x01` can brick the unit
(see [PDO and RMI](/protocol/pdo-rmi#rmi-request-payloads)).

## Running a walk

A walk runs only when someone asks for it. Nothing starts one automatically.

- **Web UI:** the *Unit capabilities* page (`/capabilities`) has the start button, follows the walk
  every 2 s while it runs, shows the report and offers it as a JSON download.
- **REST:** `POST /api/probe`, then `GET /api/probe`.

```json
POST /api/probe
{"node": 1, "another_counter": 0}
```

Both fields are optional; an empty body walks node 1 with AnotherCounter 0 — the identifier bits
the command sender uses, which the owner's Q350 answered
([findings](/reference/rmi-probe-findings-q350)).

| Answer | Meaning |
| --- | --- |
| `202 {"accepted":true}` | the walk started on its own task; poll `GET /api/probe` |
| `409 a probe is already running` | one walk at a time |
| `400 not a JSON object` | a non-empty body that is not a JSON object |
| `422` | `node` or `another_counter` is not a whole number 0–255; `node` is not 1–63 or is this device (`0x2A`); `another_counter` is not 0–3 |
| `503 the probe is not running on this device (no CAN bus)` | the probe task was not started at boot: the CAN controller or the command sender did not come up, or the task could not be created |

Starting a walk is allowed without a password when none is set — reading the unit's configuration
is treated like changing its fan speed — and needs authentication once a password exists. It is
refused while the device is an unprovisioned access point (see [Security](/architecture/security)).

`GET /api/probe` returns the report as it stands: the partial report with its progress while a
walk runs, the final one afterwards, and `state: "idle"` before the first walk. The report lives
**in RAM** until the next walk or a reboot. It is streamed in chunks because it can be larger than
the HTTP server's 48 KiB buffer; if the client goes away mid-stream the connection is closed rather
than ending with broken JSON.

## What a walk asks

Default plan (`COMFOAIR_PROBE_DEFAULT_PLAN()`), with the target node from the request:

| Bound | Default |
| --- | --- |
| Units | `0x01`–`0x2F` |
| Subunits per unit | 1–16 |
| Properties per subunit | `0x01`–`0x3F` |
| Retries after a timeout | 1 |
| Reply window per request | 300 ms |
| Gap between requests | 20 ms |
| Abort when the first answers are all silent | after 5 |

The order is:

1. **Units of the target node.** For each unit, subunit 1 property 1 is the *discovery read*: no
   answer marks the unit `timeout`, error 12 marks it `absent`, anything else marks it `present`
   and the walk continues through its subunits and properties. For every property the walk asks
   the **value**; if the value came back as data it asks the **range**; if the range came back as
   data it asks the **step**.
2. **Active errors:** one `80 03 01` to the target.
3. **Other nodes:** up to 8 other nodes the census has heard (excluding node 0, the target and
   `0x2A`) get an identity read only — NODE unit `0x01`, subunit 1, properties `0x01`–`0x14`,
   values only.

The walk prunes itself, so a unit with gaps does not cost thousands of pointless requests:

| Answer | Effect |
| --- | --- |
| 14 (unknown property) | counted on the unit row as `absent_properties`, no entry; next property |
| 13 at the discovery read or at the first property of a later subunit | the unit's subunits are over |
| 13 at a later property of a subunit | no entry; next subunit |
| 12 at the first property of a later subunit | read as 13 |
| 12 after the unit was found present | kept as an entry; next subunit |
| a subunit after the first with no data at all | the unit's subunits are over |
| 3 timeouts in a row in a subunit | next subunit |
| 8 × error 11 (unknown command) in a row in a subunit | next subunit |
| timeout at the first property of a later subunit | kept as an entry; the unit is left |

A timeout is retried once before it stands. If the **first five** final answers of a walk are all
silences, the walk ends `failed` with `failure: "no_answer"`: the unit does not answer RMI from
this node, and every further request would time out too. Once anything has answered, later
silences are information, not failure.

### Living beside commands

The probe task runs at priority 3, below the command sender, so an owner's command never waits
behind a diagnostic read. Each request takes its sequence number from the counter shared with the
command sender, and a reply is accepted only if source (the walked node), destination (`0x2A`) and
sequence all match, so a reply to a command cannot be taken for the probe's answer. Replies are
queued by the CAN receive task without waiting (16 frames); a reply dropped because the queue was
full shows up as a timeout and is retried. Late answers to the previous request are discarded
before the next one is sent.

## Bounds of the report

| Limit | Value | When exceeded |
| --- | --- | --- |
| Property entries | 400 | the walk continues, `truncated: true` |
| Unit rows | 64 | `truncated: true` |
| Stored value bytes | 40 | stored cut; `len` still says how many arrived |
| Stored range / step bytes | 8 | as above |
| Active-error bitfield | 32 bytes | — |
| PDO census rows | 256 (`COMFOAIR_PDO_CENSUS_MAX`) | a PDO heard after the table is full gets no row; `pdos.overflow` counts it |

## The report

The top level of `GET /api/probe`, in the order it is emitted:

```json
{
  "state": "done",
  "failure": null,
  "node": 1,
  "another_counter": 0,
  "elapsed_ms": 118000,
  "progress": {"requests": 4226, "timeouts": 0, "node": 1, "unit": 0, "subunit": 0, "property": 0},
  "nodes_seen": [1, 42],
  "units": [ ... ],
  "properties": [ ... ],
  "active_errors": {"hex": "0000…", "ids": []},
  "pdos": {"uptime_ms": 3912345, "frames": 48211, "distinct": 71, "overflow": 0, "rows": [ ... ]},
  "truncated": false
}
```

| Key | Meaning |
| --- | --- |
| `state` | `idle`, `running`, `done` or `failed` |
| `failure` | `no_answer`, or `null` |
| `node`, `another_counter` | what the walk was started with |
| `elapsed_ms` | running time so far, or of the whole walk once it ended |
| `progress` | requests and timeouts (retries included), and where the walk is |
| `nodes_seen` | every node id heard on the bus since boot |
| `units[]` | one row per unit asked: `node`, `unit`, `name`, `status` (`present` / `absent` / `timeout`), `subunits` that answered, `absent_properties` |
| `properties[]` | one entry per property worth reporting — data, an error other than 13 or 14, or silence — following the pruning rules above |
| `active_errors` | the GETACTIVEERRORS answer; `ids` lists the set bits, each byte read from its least significant bit |
| `pdos` | the [PDO census](#the-pdo-census); absent on a device that supplies none |
| `truncated` | an entry or unit row did not fit |

A property entry:

```json
{
  "node": 1, "unit": 28, "subunit": 1, "property": 3,
  "name": "Filter lifetime", "access": "rw", "format": "u16", "unit_label": "days",
  "value": {"hex": "b400", "len": 2, "number": 180, "text": null, "label": null},
  "range": {"hex": "0000ffff", "len": 4, "min": 0, "max": 65535},
  "step":  {"hex": "…", "len": 2, "number": 1, "text": null, "label": null}
}
```

(The filter lifetime of the Q350 walk; the step is illustrative.) `name`, `access` and
`unit_label` come from the report's catalogue of known RMI properties and are `null` for a property
it does not know; the raw `hex` is always there. For an unknown property `format` is guessed from
the bytes — at least two printable ASCII bytes followed by a final NUL is a `string`, otherwise 1,
2 and 4 bytes read as `u8`, `i16` and `u32` (a 2-byte value is re-read as `u16` when its 4-byte
range, read signed, has min above max). `format` is one of `string`, `u8`, `u16`, `i16`, `u32`,
`version`, `enum`, or `null` when neither the catalogue nor the guess gives one (for example a value
cut short by the 40-byte store).
A `version` value carries both the raw number and its text (for example `R1.11.0`); an `enum`
carries its `label`. The catalogue is not an entity list — registry entities are PDOs and commands,
and RMI properties are not in the registry.

`value`, `range`, `step` and `active_errors` each take one of these forms (`active_errors` is never
withheld):

| Form | Shape |
| --- | --- |
| data | `{"hex", "len", …}` as above |
| RMI error | `{"error": 14, "error_name": "...", "hex": "..."}` — the raw error payload is kept |
| no answer | `{"timeout": true}` |
| withheld | `{"redacted": true}` |
| not asked | `null` |

::: warning One value is never reported
The maintainer password, NODECONFIGURATION `0x20` subunit 1 property `0x03`, is always rendered
as `{"redacted": true}`, on every node. The redaction is a fixed rule in the renderer, not a
catalogue row, so editing the catalogue cannot leak it.
:::

## The unit read

Separately from walks, the probe task reads who the unit is, for the `unit` block of
`GET /api/system`, and what its three sensor-ventilation controls are set to. It asks node `0x01`,
NODE unit `0x01`, subunit 1, properties `0x04` (serial), `0x06` (firmware version), `0x08` (model),
`0x0B` (article number), `0x0D` (country) and `0x14` (name), then `80 03 01` for the active errors,
then the three selects: GET ONE `01 1D 01 10 04`, `06` and `07` (TEMPHUMCONTROL subunit 1; U8,
0 Off, 1 Auto only, 2 On) — through the same request path as a walk, so it can send nothing else.
Which selects are read, and from where, is declared in `tools/pdo_table.py` (`SelectSpec.rmi`) and
generated into the `COMFOAIR_RMI_READBACK` table.

Between two full reads the probe task sends `80 03 01` alone every 60 s (`comfoair_unit_cadence`);
an answered read sets the two error entities, `unit_error` and `active_errors`. They are written only
once the unit's firmware word is known: the error numbering depends on it, and without it no id can
be classed informational, so a manual bypass would raise a problem. Until then both stay unknown. An
unanswered errors read keeps the last published values — a single RMI timeout does not flap them,
and a dead bus greys them out through `<prefix>/bus`.

| When | |
| --- | --- |
| first read | 10 s after the probe task started at boot |
| after a complete read — the model, the three selects and the firmware word all answered | every 6 h |
| after an incomplete one — no model, a select with no final answer (an RMI error is final; a timeout is not), or no firmware word yet | every 60 s |
| active errors alone | every 60 s between full reads; a full read restarts this clock, and an unanswered one is retried the next minute |
| during a walk | postponed until the walk ends |
| when a walk of node `0x01` ends `done` | the identity is refreshed from the walk's results; the schedule is left alone, so a walk never delays the settings read |

Only answers that came back as data change a field; an error or a silence leaves the previous value
in place.

The three selects are read-backs of values the firmware can also write, so two guards keep a read
from overwriting a newer write:

- a select read is **skipped** while a command is queued or being sent, and retried in 60 s;
- a reply is **dropped** if a command was submitted while the read was in flight (a guard on
  `comfoair_control`'s `queued`, `sent` and `failed` counters), and the select is read again in
  60 s. A fresh write from Home Assistant or REST is never replaced by the unit's pre-write value.

Until the first answer each select is unknown: the firmware publishes no invented factory default.
Writes stay optimistic (fire-and-forget) and are confirmed by the next read, so a change made on the
unit's display or with ComfoConnect is seen within 6 h. Nothing is stored in NVS for these three: it
would only know what this firmware wrote, which is wrong after a change on the display, in
ComfoConnect, after a unit reset, or after the ESP is moved to another unit. The probe task is started at boot after the command sender, and only if the sender
started; if it cannot start, `RMI probe did not start` is logged and only the probe and the identity
read are lost.

## The node census

Every extended, non-RTR frame the bus delivers that is a PDO broadcast, an RMI frame, or a
heartbeat (`0x10000000 | node`) records its source node in a 64-bit census, from boot onwards and whether
or not the probe task ever started. It feeds `bus.nodes_seen` in `GET /api/system`, the report's
`nodes_seen`, and the list of other nodes a walk reads the identity of.

## The PDO census

Every PDO broadcast the bus delivers (the frames the node census above records) is also recorded
by PDO number, from boot onwards: no walk is needed, nothing is sent, and the census is in every
`GET /api/probe` document, `state: "idle"` included. It answers what a list of entities cannot:
which PDOs this unit actually broadcasts, how long each one is, who sends it, and which PDOs it
broadcasts that the firmware's table does not know.

```json
"pdos": {
  "uptime_ms": 3912345,
  "frames": 48211,
  "distinct": 71,
  "overflow": 0,
  "rows": [
    {"pdo": 16, "known": true, "count": 412, "first_ms": 1830, "last_ms": 3911020,
     "dlc": [1], "nodes": [1], "last": "01"}
  ]
}
```

| Key | Meaning |
| --- | --- |
| `uptime_ms` | milliseconds since boot when the document was rendered; a `u32`, so it wraps after 49.7 days. `first_ms` and `last_ms` are on the same clock |
| `frames` | every PDO frame recorded since boot, frames of PDOs that got no row included |
| `distinct` | distinct PDO numbers heard, from an 8192-bit heard set that never fills |
| `overflow` | `distinct` minus the rows recorded: PDOs heard after the 256-row table was full |
| `rows[]` | one row per recorded PDO, ascending by `pdo` |
| `rows[].known` | the PDO is in the firmware's entity table (`COMFOAIR_REQUEST_PDOS`) |
| `rows[].count` | frames of this PDO |
| `rows[].first_ms`, `last_ms` | when the first and the latest frame arrived |
| `rows[].dlc` | every data length seen, ascending |
| `rows[].nodes` | every sender node id seen, ascending |
| `rows[].last` | the latest payload as lowercase hex, two characters per byte; `""` for a zero-length frame |

A PDO number above 8191, a node id above 63 or more than 8 data bytes cannot come out of a valid
frame and is ignored, counted nowhere. The record is about 12 KB of static memory in
`comfoair_probe_task_bus.cpp`: 256 records of 40 B, the 1 KB heard set and the 1 KB index. Each
frame costs a constant amount of work under a spinlock on the CAN receive task — a hash lookup and
a few stores, no allocation, nothing moved — so the receive path never waits on the census. A row
is appended once and never moves, so the renderer copies one row at a time under the lock while it
streams: the summary is taken when rendering starts, and a row's counters may be a few frames newer
than it. A row that cannot be copied is left out and the rest of the census is still rendered.

`known` is decided by the firmware from the table it was built with. The census is reset only by a
reboot.

### Comparing with the table

`tools/pdo_census_compare.py` reads a saved dump and prints the comparison with
`tools/pdo_table.py` as Markdown:

```sh
curl -s http://<ip>/api/probe > q350-pdos.json
python3 tools/pdo_census_compare.py q350-pdos.json
```

1. **Heard and in the table:** each PDO with its entity keys, data lengths, senders and count.
2. **Heard but not in the table:** each PDO with its data lengths, senders, count and last payload
   — candidates for new rows.
3. **In the table but never heard,** split by the entities' `enabled_by_default`. A silent
   enabled-by-default PDO is a finding on any unit; a silent disabled-by-default one is expected
   without the accessory or schedule that sends it. A PDO with any enabled entity counts as
   enabled.
4. **Summary,** with a warning when `uptime_ms` is below 65 minutes (the request sweep runs at
   boot and then hourly, so a PDO the unit sends only when asked may not have been asked again),
   when `overflow` is above 0 (section 3 may then list PDOs that were heard but got no row), and
   when a row's `known` disagrees with the table (the dump comes from a build of another table).

The script takes the entity keys from the registry generator, so it reflects the same table the
firmware is generated from. It exits 0 after printing and 2 when the file cannot be read, is not
JSON, or has no `"pdos"`. The hardware items that use it are
[HV-PROBE-21 to HV-PROBE-24](/hardware-verification/09-rmi-probe#the-pdo-census).

A census of the owner's ComfoAir Q350 NL R VV ST, firmware R1.11.0, is kept as
`docs/unit/data/q350-pdos.json`: 67 minutes of uptime, 5934 frames, 85 distinct PDOs, no overflow,
with a full walk of node `0x01` in the same document (the serial redacted). The MQTT
[publication rate](/interfaces/mqtt#publication-rate) of the jittery rows is taken from its frame
counts.
