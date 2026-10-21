# Errors and alarms

The ERROR unit (`0x03`) is the ventilation unit's error store. Its content is not a set of
properties: the unit reports its active errors as one bitfield in answer to the RMI operation
GETACTIVEERRORS, `80 03 01`, and clears them with RESETALLERRORS, `82 03 01`. Each set bit is an
error id, and the id's meaning depends on the unit's firmware version. The firmware reads the
active-error list, shows it with a text for each id, and publishes it as two entities; it never
clears errors.

<UnitAreaSummary area="errors-and-alarms" />

## Firmware reads

The firmware reads no PDO in this area. Live states the unit's alarms relate to come from PDOs of
other areas: the device state (PDO 16) under [Ventilation](/unit/areas/ventilation), the filter
status and countdown (PDOs 18, 192) under [Filters and maintenance](/unit/areas/filters-and-maintenance),
and the airflow constraints (PDO 230) under [Sensors and air quality](/unit/areas/sensors-and-air-quality).

**The active-error list.** GETACTIVEERRORS, built by `get_active_errors()` with its unit byte fixed
to ERROR `0x03` (`components/comfoair_protocol/include/rmi_read.h:54`), returns a 32-byte bitfield
(`components/comfoair_sysinfo/include/comfoair_sysinfo.h:23`). Byte *i*, bit *b*, counted from the
least significant bit, is error id *i* × 8 + *b*
(`components/comfoair_sysinfo/comfoair_sysinfo.cpp:131`). A shorter answer is zero-filled: a bit
the unit did not send is an error it did not report
(`components/comfoair_probe_task/comfoair_probe_task_unit.cpp:106`).

The list is read on three occasions, all on the probe task and scheduled by
`components/comfoair_unit_cadence`:

- **With every full unit read**, after the identity: 10 s after the probe task starts, then every
  60 s until the model, the three settings and the firmware word have all answered, then every 6 h
  (the full read in `comfoair_probe_task_unit.cpp:146`; see
  [Device identity](/unit/areas/device-identity)).
- **Alone, every 60 s between two full reads** (`probe_errors_read()`, `:232`): one request and a
  32-byte answer a minute, so Home Assistant hears of a fault within a minute rather than six
  hours. A full read restarts this clock; an unanswered read is retried the next minute, not sooner.
- **By a probe walk** of node `0x01`: the walk sends GETACTIVEERRORS after its last unit
  (`components/comfoair_probe/comfoair_probe_scan.cpp:244`), and a walk that ends `done` replaces
  the list with that answer (`components/comfoair_probe_task/comfoair_probe_task.cpp:94`,
  `comfoair_probe_task_unit.cpp:304`). It does not write the two entities; the next minute's read
  does.

## Firmware writes

The firmware writes nothing in this area. It has no RESETALLERRORS builder: its only command bytes
of `0x80` or above are `84` and `85` on SCHEDULE and `80 03 01` on ERROR
([PDO and RMI — request payloads](/protocol/pdo-rmi#rmi-request-payloads)). An active error stays
on the unit until the unit itself clears it.

## Unit settings

ERROR has nothing to configure. Over GET ONE it answers only the generic first property,
[3.1.1](/unit/properties#prop-3-1-1); every other property is unknown to the unit. The firmware does
not read or write this property.

<UnitPropertyTable area="errors-and-alarms" />

## Behaviour

- **REST.** The list is `unit.errors` in `GET /api/system`: `state` (`ok`, `no_answer`,
  `unknown`), `ids`, the set bit ids in ascending order, and `items`, one `{"id", "text"}` per id
  (`components/comfoair_sysinfo/comfoair_sysinfo.cpp:123`;
  [REST API](/interfaces/rest-api#get-api-system)). The web UI's System page lists them, and shows
  an unread list as not read rather than as "no errors" (`web/src/pages/system/sections.ts:63`).
- **After a failed read** `state` is `no_answer` and `ids` and `items` are empty: an error that
  cleared in the meantime does not stay on screen as active
  (`components/comfoair_sysinfo/comfoair_sysinfo.cpp:128`).
- **Error texts.** The texts are English and compiled into the firmware. Ids 21–69
  have one meaning on every firmware; from id 70 the numbering depends on the unit's firmware
  version: at or below R1.4.0 (`0xC0101000`) the older table applies, otherwise, or with no
  version read, the newer one
  (`components/comfoair_sysinfo/comfoair_sysinfo_errors.cpp:201`). An id with no text has
  `"text": null`.
- **Two entities.** The same answer is published over MQTT, REST, the WebSocket and Home
  Assistant discovery as `unit_error` and `active_errors` (`tools/pdo_table.py` `UNIT_ERRORS`):
  - `unit_error` is a binary sensor with device class `problem`: on while an active id is not
    merely **informational**. Informational ids are the owner's own doing or a routine notice —
    "Bypass is in manual mode" (89; 81 at R1.4.0 and below), service mode (80; 76), "Order new
    filters now" (79; 75) and "ComfoHood is activated" (96) — and never raise it; an unknown id
    does (`comfoair_sysinfo_errors_any()`; the evidence per id is beside the tables in
    `comfoair_sysinfo_errors.cpp`).
  - `active_errors` is a diagnostic sensor: every active id's text, informational ones included,
    in ascending order joined by ` | `, `No errors` when none is active, at most 255 bytes (Home
    Assistant's state limit; a longer line is cut and ends `...`, and every id is still in
    `GET /api/system`).
- **When the entities change.** Only an answered read writes them, and only once the unit's
  firmware word is known: the numbering depends on it, so without it no id can be classed
  informational, and a manual bypass would raise a problem. Until then both are unknown (`null`
  in REST, `None` on MQTT). An unanswered read keeps the last published values — a single RMI
  timeout does not flap them; a dead bus greys them out through `<prefix>/bus`.
