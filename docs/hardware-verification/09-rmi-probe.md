# 09 · RMI probe

The read-only walk of the unit's RMI tree (`POST /api/probe`, the *Unit capabilities* page). A walk
sends only `01 <unit> <subunit> <type> <property>` reads and one `80 03 01` (active errors) — no
write. The host suites pin the first byte and the unit of every request; the documented dangers (any
command `>= 0x80` to unit `0x01` of node `0x01`, `0x80`/`0x82` to `0x20`) cannot be built by the
probe.

A live walk on the owner's Q350 is recorded in the
[probe findings](/reference/rmi-probe-findings-q350): it answered with `another_counter` 0 and no
heartbeat. These items have not been walked as a checklist; record each result, and record new
findings in that document. As-built: [Probe](/protocol/probe).

## Before the run

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROBE-01 | Photograph the unit's settings screens (flow presets, temperature profile, setpoints, imbalance, filter) — see [HV-SAFE-05](./00-setup-and-safety); keep the display in view and know where the unit's breaker is; keep the whole serial log; a CAN analyser if available | The walk writes nothing; the photographs are for comparison if the unit behaves unexpectedly | — | unit, USB |

## Does the unit answer

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROBE-02 | Boot; `curl -s http://<ip>/api/probe` | No probe error in the log (`RMI probe did not start` would mean no CAN bus or no command sender); `"state":"idle"`; `nodes_seen` contains `1` | `curl`; serial log | unit |
| HV-PROBE-03 | Unit capabilities → Start (or `curl -X POST http://<ip>/api/probe -d '{}'`) | `202 {"accepted":true}`; within 2–3 s `progress.requests` grows and `timeouts` stays near 0 | `GET /api/probe` | unit |
| HV-PROBE-04 | If about 3 s in `state` = `failed` with `failure` = `no_answer` | Repeat with `{"another_counter":1}` | `GET /api/probe` | unit |
| HV-PROBE-05 | If `no_answer` with counter 1 as well | **Stop.** Hand the log and a bus dump over for analysis. The next hypothesis — a node-presence heartbeat `0x10000000 + 0x2A` — needs new code | serial log; analyser | unit |
| HV-PROBE-06 | When it answers | Record which `another_counter` worked in the [probe findings](/reference/rmi-probe-findings-q350) (for the Q350: 0) | — | — |
| HV-PROBE-25 | Boot; read `GET /api/system` every few minutes for an hour, and `GET /api/log` at the end; start a walk meanwhile | `unit.errors.state` is `ok` within 15 s and stays `ok` for the hour. The log carries no per-minute probe line: an answered errors-only read logs nothing, and only a change logs `probe: unit errors: …` (or `unit errors (problem): …`) at INFO — once after boot, then on each change. The walk started meanwhile completes normally | `GET /api/system`; `GET /api/log` | unit |

## The unit during a walk

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROBE-07 | The unit's display during the walk | Works as usual: no restart, no error | unit display | unit |
| HV-PROBE-08 | Ventilation during the walk | The flows do not change (`supply_fan_flow`/`exhaust_fan_flow` steady on the dashboard) | dashboard | unit |
| HV-PROBE-09 | Change the fan level in the UI during the walk | The command takes effect at once; the walk continues | unit display; `GET /api/probe` | unit |
| HV-PROBE-10 | The dashboard during the walk | Values keep updating — no PDOs lost | dashboard | unit |
| HV-PROBE-11 | Duration | Minutes, not hours; the log ends with `probe: walk done in … ms: … requests, … timeouts, … entries, … units; stack free N B` with N > 512 | serial log | unit |

## Is the report true

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROBE-12 | Identity: model, serial, firmware | Match `STATUS > UNIT` on the unit's display | report; display | unit |
| HV-PROBE-13 | Flow presets `0x1E/01/03..06` | Match the display (m³/h) and carry a `range` | report; display | unit |
| HV-PROBE-14 | Setpoints `0x1D/01/0A..0C` | ×10 °C decoded correctly (230 → 23.0) | report | unit |
| HV-PROBE-15 | An error answer (e.g. code 14 for a missing property) | A meaningful `error_name`. If the codes look meaningless (`hex` not 0x0B..0x35), the assumption "the code is the first payload byte" is wrong — attach the `hex` | report | unit |
| HV-PROBE-16 | `0x20/01/03` (the maintainer password) | `{"redacted":true}`; the password's digits appear nowhere in the JSON | report | unit |
| HV-PROBE-17 | `active_errors`, `nodes_seen` | `active_errors` matches the display (usually empty); `nodes_seen` lists the unit `1` and a ComfoSense / LAN C / Option Box if connected. Download the JSON and keep it with the run — it decides which functions are opened next | report; download | unit |

## Failures

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROBE-18 | A second Start during a walk | `409 {"error":"a probe is already running"}` | `curl -si` | unit |
| HV-PROBE-19 | Pull the CAN cable in the middle of a walk | The walk runs to its end with timeouts (minutes); no restart; PDOs return after reconnecting | serial log; `GET /api/probe` | unit |
| HV-PROBE-20 | Device password set, request without credentials | `POST /api/probe` → `401`; `GET /api/probe` → `401` as every GET | `curl -si` | unit |

## The PDO census

The census needs no walk: it records every PDO broadcast from boot and is in every
`GET /api/probe` document. As-built: [Probe, the PDO census](/protocol/probe#the-pdo-census).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-PROBE-21 | **Capture.** After at least 65 minutes of uptime (the boot sweep and one full hourly sweep), `curl -s http://<ip>/api/probe > q350-pdos.json`; save it as `docs/unit/data/q350-pdos.json`. Before committing, search the file for the serial: the census carries none, but an earlier walk's properties are in the same document if one ran since boot — redact the serial if present | The document has a `pdos` object with `uptime_ms` ≥ 3 900 000, `frames` > 0, `rows` ascending by `pdo`, and `nodes` containing `1` on the unit's own PDOs; no serial in the saved file | `curl`; `GET /api/system` (`unit.serial`, to search for) | unit |
| HV-PROBE-22 | **Compare.** `python3 tools/pdo_census_compare.py docs/unit/data/q350-pdos.json` | Exit 0 and no uptime warning. Record as findings, with the unit model and firmware: every PDO under *In the table but never heard → Enabled by default*, and every PDO under *Heard but not in the table* with its `dlc`, nodes and last bytes. A `known` warning means the device runs a build of another table — reflash before recording | the script's output | — |
| HV-PROBE-23 | **Frame lengths.** For each PDO under *Heard and in the table*, compare its `dlc` with the width in the [PDO and RMI reference §1](/reference/comfoair-pdo-rmi#_1-pdo-table) | Each `dlc` is a single length equal to the referenced type's width (UINT8/INT8 1, UINT16/INT16 2, UINT32 4, and so on). A mismatch means the width in the reference is wrong for this unit: record the PDO, its `dlc` and its last bytes | the script's output; the reference | — |
| HV-PROBE-24 | **Overflow.** In the HV-PROBE-21 dump, read `pdos.overflow` and `pdos.distinct` | `overflow` = 0, and the number of `rows` equals `distinct` − `overflow` (here: `distinct`). One row fewer is possible but rare — a record the device could not copy while rendering is left out; re-read the dump before recording it. A larger `overflow` means the 256-row census is too small for this bus and section 3 of the comparison is unreliable: record `distinct` | the dump | — |
