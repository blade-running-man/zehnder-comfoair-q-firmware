# Ventilation unit interface

How this firmware interacts with the ComfoAir Q ventilation unit: what it reads, what it writes,
and which parts of the unit it does not use. Each functional area has its own page; this page is
the interface as a whole.

## The unit as the firmware sees it

The ventilation unit is **one ComfoNet node, `0x01`**, and it is the only node the firmware
addresses in normal operation: commands go to `COMFOAIR_CONTROL_UNIT_NODE` and to nothing else
(`components/comfoair_control/include/comfoair_control.h:40`), and the timed identity read goes to
the same node. Only a probe walk, started on request, addresses another node. The firmware itself is node `0x2A`
(`components/comfoair_control/include/comfoair_control.h:33`), the same id its PDO requests carry
(`components/comfoair_poll/include/comfoair_poll.h:27`).

The unit has two address spaces:

- **PDOs** — process data objects, numbered by a 13-bit id carried in the CAN identifier
  (`components/comfoair_protocol/include/pdo_decode.h:10`). The unit broadcasts them; any node
  can request one with a remote-transmission request. See [PDO and RMI](/protocol/pdo-rmi).
- **RMI properties** — addressed as *unit · subunit · property*. A **unit** is a functional block
  of the ventilation unit (NODE `0x01`, ERROR `0x03`, SCHEDULE `0x15`, TEMPHUMCONTROL `0x1D`, …),
  a **subunit** is one instance of that block (numbered from 1), and a **property** is one value
  inside the instance. An RMI request starts with a command byte that says what to do with the
  address. The [unit map](/unit/units) lists every unit number; the
  [property reference](/unit/properties) every property, keyed `unit.subunit.property`.

## Three channels

### PDO telemetry

Every value the firmware reports about the unit's running state comes from a PDO.

- **Receive.** Every PDO broadcast is decoded the moment it arrives, against the generated
  registry; one PDO can feed several entities (`src/main.cpp:73`). A PDO no entity uses is logged
  at DEBUG and dropped (`src/main.cpp:84`).
- **Request.** The firmware requests every PDO of `COMFOAIR_REQUEST_PDOS` by RTR once at boot and
  then once an hour, 100 ms apart (`components/comfoair_poll/include/comfoair_poll.h:27`), and once
  more after 60 s with no frame on the bus
  (`components/comfoair_phase_monitor/comfoair_phase_monitor.c:144`). The list is every PDO an
  entity is read from plus the PDOs the fan and the balance mode are derived from
  (`ALL_REQUEST_PDOS` in `tools/pdo_table.py`). Details: [Polling](/protocol/polling).
- **Staleness.** A value stays what the last frame said; there is no per-value age. Whether the
  data is current is decided for the bus as a whole
  ([Polling — how fresh a value is](/protocol/polling#how-fresh-a-value-is)).

The entities and the PDO each is read from are listed on [Entities](/reference/entities).

### RMI writes

Every write is one of three frames, built in `components/comfoair_protocol/include/rmi_frame.h`
and sent from node `0x2A` to node `0x01`:

| Frame | Shape | Where |
| --- | --- | --- |
| ENABLETIMERENTRY | `84 15 <subunit> <property> 00 00 00 00 <timeout u32 LE> <value>` | `rmi_frame.h:56` |
| DISABLETIMERENTRY | `85 15 <subunit> <property>` | `rmi_frame.h:74` |
| SET PROPERTY | `03 <unit> <subunit> <property> <value>` | `rmi_frame.h:79` |

What each control sends:

| Control | Target | Where |
| --- | --- | --- |
| Ventilation level Away / Low / Medium / High | SCHEDULE subunit 1, property `0x01`, value 0–3, 1 s timeout; preceded by the manual-mode timer on subunit 8 when permanent manual mode is on | `components/comfoair_command/comfoair_command_build.cpp:115` |
| Auto | clears boost, away and manual mode, the last 1000 ms after the others | `comfoair_command_build.cpp:44` |
| Boost | SCHEDULE subunit 1, property `0x06` | `rmi_frame.h:88` |
| Away mode | SCHEDULE subunit 1, property `0x0B`, value `0x00` | `rmi_frame.h:92` |
| Bypass open / closed / auto | SCHEDULE subunit 2; auto clears the timer | `rmi_frame.h:100` |
| Temperature profile normal / cool / warm | SCHEDULE subunit 3, no expiry | `rmi_frame.h:117` |
| Balance mode | SCHEDULE subunits 6 (supply only) and 7 (extract only), always both frames | `comfoair_command_build.cpp:87` |
| Temperature passive, humidity comfort, humidity protection | SET PROPERTY on TEMPHUMCONTROL `0x1D` subunit 1, properties `0x04`, `0x06`, `0x07`, value 0–2 | `comfoair_command_build.cpp:158` |

The writable entities that produce these frames:

<UnitEntities keys="ventilation_mode,fan,away_mode,bypass_mode,temperature_profile,balance_mode,temperature_passive,humidity_comfort,humidity_protection,boost_duration,bypass_duration,balance_duration,away_duration" />

The four durations send no frame of their own; they are the timeouts the boost, bypass, balance
and away frames carry. The six parameterised operations of `comfoair_command_from_op()`
(`comfoair_command.cpp:199`) build the same frames. Details: [Commands](/protocol/commands).

A command is **accepted, not confirmed**: the firmware does not read the unit's RMI response
(`src/main.cpp:89`). The confirmation is the unit's next PDO broadcast. The three
TEMPHUMCONTROL settings have no PDO: the firmware reads them back by RMI in the unit read
(see below), and they are unknown until the first answer.

### RMI reads

The firmware sends two kinds of RMI read, built in
`components/comfoair_protocol/include/rmi_read.h`: GET ONE `01 <unit> <subunit> <type> <property>`
for a value, range or step (`rmi_read.h:45`), and GETACTIVEERRORS `80 03 01` (`rmi_read.h:54`).

- **Identity read.** GET ONE of NODE subunit 1 properties `0x04` (serial number), `0x06`
  (firmware version), `0x08` (model), `0x0B` (article number), `0x0D` (country) and `0x14` (unit
  name), then GETACTIVEERRORS
  (`components/comfoair_probe_task/comfoair_probe_task_unit.cpp:31`, `:180`). It runs 10 s after
  boot, then every 6 h after a complete read (the model, the three settings and the firmware word
  answered) and every 60 s after an incomplete one; between two full reads GETACTIVEERRORS runs
  alone every 60 s (`components/comfoair_unit_cadence/include/comfoair_unit_cadence.h:23`). The result is the `unit` block of `GET /api/system`
  (`components/comfoair_sysinfo/comfoair_sysinfo.cpp:160`) and the web UI's System page
  (`web/src/pages/system/sections.ts:56`). The firmware version selects which error-text numbering
  applies (`components/comfoair_sysinfo/comfoair_sysinfo_errors.cpp:201`).
- **Sensor-ventilation read.** In the same cycle, GET ONE of TEMPHUMCONTROL `0x1D` subunit 1
  properties `0x04`, `0x06` and `0x07`, feeding `temperature_passive`, `humidity_comfort` and
  `humidity_protection`. It retries every 60 s until each has a final answer (an RMI error counts,
  a timeout does not), is skipped while a command is queued or being sent, and its reply is dropped
  if a command was submitted meanwhile. See [Temperature and bypass](/unit/areas/temperature-and-bypass).
- **Active-error list.** The GETACTIVEERRORS bitfield, rendered with its error texts in the
  `errors` field of the same block (`comfoair_sysinfo.cpp:123`). It is refreshed by every identity
  read and by every probe walk of node `0x01` that completes
  (`comfoair_probe_task_unit.cpp:183`). It is not published on MQTT.
- **Probe walk.** Only on request (`POST /api/probe`, node `0x01` unless the request names another,
  `components/comfoair_http/comfoair_http_probe.c:103`): GET ONE of the value of every property of
  units `0x01`–`0x2F`, subunits 1–16, properties `0x01`–`0x3F`, and of its range and step where
  the unit answers
  (`components/comfoair_probe/include/comfoair_probe.h:89`), then GETACTIVEERRORS, then the NODE
  identity of up to eight other nodes the firmware has heard on the bus
  (`components/comfoair_probe_task/comfoair_probe_task.cpp:204`). The report is
  served by `GET /api/probe` and held in RAM. When a walk of node `0x01` completes, its NODE
  subunit 1 answers and its error bitfield refresh the identity and the active-error list
  (`components/comfoair_probe_task/comfoair_probe_task.cpp:89`); nothing else from a walk enters
  the firmware's state. Details: [Probe](/protocol/probe).

The command bytes above are the only ones the firmware has a builder for: `01`, `03`, `80`, `84`
and `85`. It sends no RESETALLERRORS (`82 03 01`), no GETTIMERENTRY (`83`), and no command of `0x80`
or above to NODE `0x01` or to NODECONFIGURATION `0x20`
([PDO and RMI — request payloads](/protocol/pdo-rmi#rmi-request-payloads)).

## Interaction matrix

One row per functional area. *Reads* are PDOs decoded into entities; *writes* are RMI frames;
*unit settings* are RMI properties the firmware reads or writes in normal operation (the probe
walk, which reads everything on request, is not counted).

| Area | Reads (PDO telemetry) | Writes | Unit settings | Summary |
| --- | --- | --- | --- | --- |
| [Device identity](/unit/areas/device-identity) | — | — | yes: reads NODE `0x04`, `0x06`, `0x08`, `0x0B`, `0x0D`, `0x14` | the unit's identity in `GET /api/system` |
| [Ventilation](/unit/areas/ventilation) | PDOs 16, 49, 54, 55, 56, 65, 70, 71, 117–122, 225, 226 | level, auto, boost, away, balance mode (SCHEDULE timers) | no | fan state and duty, flow and speed; the FAN and VENTILATIONCONFIG properties are not used |
| [Temperature and bypass](/unit/areas/temperature-and-bypass) | PDOs 66, 67, 209–218, 220, 227, 338 | bypass, temperature profile (SCHEDULE); temperature passive, humidity comfort, humidity protection (SET PROPERTY) | yes: writes TEMPHUMCONTROL `0x04`, `0x06`, `0x07` and reads them back | temperatures, seasons and bypass state; setpoints and season limits are not used |
| [Heating and frost](/unit/areas/heating-and-frost) | PDOs 144–146, 221 | — | no | pre-heater power and energy, the controller's supply temperature; PREHEATER and POSTHEATER properties are not used |
| [Filters and maintenance](/unit/areas/filters-and-maintenance) | PDOs 18, 192 | — | no | filter status and remaining days; the filter lifetime setting is not used |
| [Sensors and air quality](/unit/areas/sensors-and-air-quality) | PDOs 230, 274–278, 290–294 | — | no | temperatures, humidities, airflow constraints; sensor settings are not used |
| [Schedules and timers](/unit/areas/schedules-and-timers) | PDOs 81, 82, 86, 87 | ENABLETIMERENTRY / DISABLETIMERENTRY on SCHEDULE subunits 1, 2, 3, 6, 7, 8 | no | every timed override goes through SCHEDULE; timer entries are not read back |
| [Errors and alarms](/unit/areas/errors-and-alarms) | — | — | no; GETACTIVEERRORS `80 03 01` is read | active errors in `GET /api/system`; not on MQTT, never reset |
| [Communication and network](/unit/areas/communication-and-network) | — | — | no | the firmware keeps its own census of nodes heard (`src/main.cpp:64`) |
| [Configuration and installer settings](/unit/areas/configuration-and-installer) | — | — | no | not used |
| [Display and HMI](/unit/areas/display-and-hmi) | PDOs 208, 224 | — | no | the temperature and airflow units the display uses |
| [Energy and power](/unit/areas/energy-and-power) | PDOs 128–130 | — | no | ventilation power and energy |
| [Other units](/unit/areas/other) | — | — | no | not used |

The PDO numbers are those of the generated registry (`tools/pdo_table.py`); the fan and the
balance mode are derived from PDOs 49 and 65, and 54 and 55 (`FAN_PDOS` and `BALANCE_PDOS` in `tools/pdo_table.py`).

## Not used by the firmware

- **Airflow presets.** The level selects one of the unit's four presets; the airflow each one
  stands for ([30.1.3](/unit/properties#prop-30-1-3)–[30.1.6](/unit/properties#prop-30-1-6)) is
  not read or written.
- **Temperature settings.** The fixed setpoints
  ([29.1.10](/unit/properties#prop-29-1-10)–[29.1.12](/unit/properties#prop-29-1-12)), the
  profile mode ([29.1.8](/unit/properties#prop-29-1-8)) and the heating and cooling season limits
  ([29.1.2](/unit/properties#prop-29-1-2), [29.1.3](/unit/properties#prop-29-1-3)) are not read or
  written. The firmware publishes their effect: the target temperature (PDO 212) and the season
  flags (PDOs 210, 211).
- **Filter lifetime.** [28.1.3](/unit/properties#prop-28-1-3) is not read or written; the firmware
  publishes the countdown, PDO 192. It sends no filter reset.
- **VENTILATIONCONFIG installer settings** — unbalance, control mode, altitude, the bathroom
  switch — are not read or written.
- **The unit model.** It is read with the identity and shown only in `GET /api/system`. Home
  Assistant's device info carries the constant model `ComfoAir Q`
  (`components/comfoair_ha/comfoair_ha_payload.c:78`), and no entity depends on the model.
- **Timer read-back.** GETTIMERENTRY is not sent; the countdown PDOs 81, 82, 86 and 87 are what
  the firmware publishes about running timers.
- **Error reset.** RESETALLERRORS is not sent, and the active-error list has no MQTT topic or Home
  Assistant entity.
- **Untouched units.** VENTILATIONCONFIG, FAN, VALVE, FILTER, POWERSENSOR, PREHEATER, HMI,
  RFCOMMUNICATION, COMFOBUS, PERIPHERALS, NODECONFIGURATION, the sensor and CO₂ units,
  ANALOGINPUT, POSTHEATER, COMFOFOND, COOKERHOOD and the unidentified units are read only by a probe
  walk. Where such a unit has a live side, the firmware reads it as PDOs — fans 117–122, power
  128–130, pre-heater 144–146, temperatures 274–278, humidities 290–294.
