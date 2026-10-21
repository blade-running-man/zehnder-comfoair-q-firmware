# Device identity

The NODE unit (`0x01`) states who the ventilation unit is: its serial number, firmware version,
model string, article number, country and unit name. It has one subunit, every identity field is
read-only, and none of it changes how the unit ventilates. The firmware reads it to show the owner
which unit is on the bus and to pick the right error-text numbering.

<UnitAreaSummary area="device-identity" />

## Firmware reads

No PDO carries identity; the firmware reads this area over RMI only.

**The identity read.** The probe task sends one GET ONE to node `0x01`, NODE subunit 1, for each
of six properties, in this order
(`components/comfoair_probe_task/comfoair_probe_task_unit.cpp:33`), then GETACTIVEERRORS
(`comfoair_probe_task_unit.cpp:150`; see [Errors and alarms](/unit/areas/errors-and-alarms)):

| Property | Field in `GET /api/system` | Encoding |
| --- | --- | --- |
| [1.1.4](/unit/properties#prop-1-1-4) | `unit.serial` | NUL-terminated ASCII |
| [1.1.6](/unit/properties#prop-1-1-6) | `unit.firmware`, `unit.firmware_raw` | u32 little-endian version word |
| [1.1.8](/unit/properties#prop-1-1-8) | `unit.model` | NUL-terminated ASCII |
| [1.1.11](/unit/properties#prop-1-1-11) | `unit.article` | NUL-terminated ASCII |
| [1.1.13](/unit/properties#prop-1-1-13) | `unit.country` | NUL-terminated ASCII |
| [1.1.20](/unit/properties#prop-1-1-20) | `unit.name` | NUL-terminated ASCII |

- **Strings** are copied up to the first NUL and never trusted to be terminated
  (`comfoair_probe_task_unit.cpp:64`).
- **The firmware version** is taken only from an answer of at least four bytes, as a little-endian
  u32 (`comfoair_probe_task_unit.cpp:89`). Bits 31–30 are the release type (U/D/P/R), 29–20 the
  major, 19–10 the minor and 9–0 the patch number; `firmware` is that word as text (for example
  `R1.11.0`) and `firmware_raw` the word itself
  (`components/comfoair_probe_report/comfoair_probe_decode.cpp:100`,
  `components/comfoair_sysinfo/comfoair_sysinfo.cpp:160`).
- **Schedule.** The first read is 10 s after the probe task starts at boot, then every 6 h after a
  complete read and every 60 s after an incomplete one (`COMFOAIR_UNIT_FIRST_S`, `_OK_S` and
  `_RETRY_S` in `components/comfoair_unit_cadence/include/comfoair_unit_cadence.h`). A read is
  complete only if the model answered, the three settings all have a final answer and the firmware
  word is known — the error entities wait for it (see [Errors and alarms](/unit/areas/errors-and-alarms)).
  Between two full reads the active errors are read alone every 60 s. The probe task runs whichever
  read is due through `probe_unit_run_due()` (`comfoair_probe_task_unit.cpp`); a read due during a
  probe walk waits for the walk to end ([Probe — the unit read](/protocol/probe#the-unit-read)).
- **Refresh from a walk.** When a probe walk of node `0x01` ends `done`, its NODE subunit 1 answers
  replace the same fields; the schedule is left alone (`probe_unit_from_walk_locked()`, called from
  `end_walk_if_over()` in `components/comfoair_probe_task/comfoair_probe_task.cpp`).

Only an answer that comes back as data changes a field; an RMI error or a silence keeps the
previous value (`comfoair_probe_task_unit.cpp:76`).

## Firmware writes

The firmware writes nothing in this area. It sends NODE only GET ONE reads, and no command byte of
`0x80` or above ([PDO and RMI — request payloads](/protocol/pdo-rmi#rmi-request-payloads)).

## Unit settings

NODE holds no setting: the six identity fields answer the range request with RMI error 15 (no
range), and no NODE property is documented as writable. The **model** string is the variant the
display shows as STATUS > UNIT > HRU TYPE (`ComfoAir Q350 NL R VV ST` on the reference unit); the
**article number** identifies the product variant, not the individual unit; the **serial number**
identifies the individual unit. The remaining NODE properties
([1.1.1](/unit/properties#prop-1-1-1)–[1.1.3](/unit/properties#prop-1-1-3),
[1.1.5](/unit/properties#prop-1-1-5), [1.1.7](/unit/properties#prop-1-1-7),
[1.1.9](/unit/properties#prop-1-1-9), [1.1.10](/unit/properties#prop-1-1-10),
[1.1.12](/unit/properties#prop-1-1-12)) are undocumented. The firmware reads the six identity
fields above and does not read or write the other NODE properties.

<UnitPropertyTable area="device-identity" />

## Behaviour

- **REST.** The fields are the `unit` block of `GET /api/system`, with `state` (`ok`,
  `no_answer`, `unknown`) and `age_ms`, the time since the last successful read
  (`comfoair_sysinfo.cpp:160`; [REST API](/interfaces/rest-api#get-api-system)). `no_answer` means
  the last read got no model; the fields keep the values of the last read that answered. The web
  UI's System page shows the same block (`web/src/pages/system/sections.ts:56`).
- **Error texts.** The firmware version selects the error-text numbering: a version word at or
  below `0xC0101000` (R1.4.0) selects the older table, any other version or no version read the
  newer one (`components/comfoair_sysinfo/comfoair_sysinfo_errors.cpp:168`).
- **MQTT and Home Assistant.** No identity field is published on MQTT. The Home Assistant device
  block carries the constants `Zehnder` and `ComfoAir Q` as manufacturer and model, and this
  firmware's own version as `sw`, not the unit's
  (`components/comfoair_ha/comfoair_ha_payload.c:78`). No entity depends on the model.
- **Scope.** The firmware assumes one ventilation unit at node `0x01`
  (`components/comfoair_control/include/comfoair_control.h:40`); the identity read addresses only
  that node.
