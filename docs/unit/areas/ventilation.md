# Ventilation (fans, airflow, modes)

Ventilation is how the unit moves air: two fans, supply and extract, driven to one of four
airflow presets — Away, Low, Medium and High — or overridden by boost, away mode, supply-only or
extract-only, and by the unit's own sensor-based controls. The unit broadcasts the fan level, the
operating mode and each fan's duty, airflow and speed as PDOs; it takes level, boost, away and
balance commands as SCHEDULE timer entries; and it holds the airflow of each preset and the
installer's fan settings in VENTILATIONCONFIG (unit 30). The firmware reads the PDOs, sends the
commands, and does not use VENTILATIONCONFIG or FAN.

<UnitAreaSummary area="ventilation" />

## Firmware reads

The firmware decodes these PDOs into entities and requests them on its polling cycle
([Polling](/protocol/polling)):

<UnitEntities keys="fan_level,operating_mode,manual_mode,device_state,supply_fan_mode,exhaust_fan_mode,supply_only_active,exhaust_only_active,exhaust_fan_duty,supply_fan_duty,exhaust_fan_flow,supply_fan_flow,exhaust_fan_speed,supply_fan_speed,sensor_ventilation_mode,fan_speed_modulated" />

- **PDO 65** is the preset in force: 0 Away, 1 Low, 2 Medium, 3 High.
- **PDO 49** is the operating mode: 1 manual (the Q350 shows it with no countdown), 5 manual
  (permanent; not seen on a unit), 6 boost, 11 away,
  255 auto (`tools/pdo_table.py`, row `operating_mode`); any other value leaves the entity without a value rather
  than with an invented label (`components/comfoair_state/comfoair_state_decode.cpp` `enum_label()` and `apply_one()`).
- **PDO 16** value 7 follows the fan's Away preset (PDO 65 = 0) and is published as
  `device_state` "Away (preset)"; it is not the `away_mode` switch's state. The switch reads
  PDO 337 bit 11, the away timer its command starts and cancels (`tools/pdo_table.py`,
  `AWAY_SWITCH`; [Schedules and timers](/unit/areas/schedules-and-timers)).
- **PDOs 119 and 120** are airflow in m³/h that the unit computes from the fan characteristic and
  its pressure sensors, corrected by the installation altitude; they are not a flow measurement
  (`tools/pdo_table.py`, section "fans").
- **PDO 226** is the unit's internal airflow demand. It steps 0/100/200/300 with the four presets
  and runs continuously between them while a sensor-based control overrules the preset; it carries
  no unit (`tools/pdo_table.py`, row `fan_speed_modulated`).
- **PDO 225** is published raw and disabled by default (`sensor_ventilation_mode`): Zehnder
  documents no values, and on the Q350 it follows the fan level (`tools/pdo_table.py`, row
  `sensor_ventilation_mode`).

Three writable entities, listed under *Firmware writes*, have no PDO of their own and are derived
(their PDO column shows "—"):

- **`fan` and `ventilation_mode`** come from PDOs 49 and 65 (`FAN_PDOS` in `tools/pdo_table.py`).
  The fan is on when PDO 65 is above 0, and its speed is the level, 1 for level 0. The
  `ventilation_mode` option follows PDO 49: Auto, Away or Boost for 255, 11 and 6, and under either
  manual mode the label of the level. While PDO 49 holds any other value the select keeps its last
  option
  (`components/comfoair_state/comfoair_state_derived.cpp` `recompute_fan()`).
- **`balance_mode`** comes from PDOs 54 and 55 (`BALANCE_PDOS` in `tools/pdo_table.py`), and only
  once both have arrived: Supply Only when PDO 54 is 1, otherwise Extract Only when PDO 55 is 1,
  otherwise Balanced (`components/comfoair_state/comfoair_state_derived.cpp` `recompute_balance()`).

The firmware reads no RMI property in this area.

## Firmware writes

<UnitEntities keys="fan,ventilation_mode,away_mode,balance_mode,boost_duration,balance_duration,away_duration" />

Every write is a SCHEDULE timer entry built in
`components/comfoair_command/comfoair_command_build.cpp`; the timer subunits are on [Schedules and
timers](/unit/areas/schedules-and-timers) and the bytes on [Commands](/protocol/commands).

| Command | Entity / operation | What is sent | Duration |
| --- | --- | --- | --- |
| Level 0–3 | `fan`, `ventilation_mode` (Away/Low/Medium/High), `set_ventilation_level` | manual mode on (if permanent manual is on), then the level | fixed 1 s timeout (`comfoair_command_build.cpp:115`) |
| Boost | `ventilation_mode` = Boost, `boost` | boost on; a duration of 0 or less cancels it | `boost_duration`, or the operation's `duration` (`comfoair_command_build.cpp:59`) |
| Away mode | `away_mode`, `set_away` | away on or off | `away_duration`, or `duration`; 0 or less is indefinite (`comfoair_command_build.cpp:138`) |
| Balance mode | `balance_mode`, `set_balance_mode` | two frames, always: one fan's entry on, the other's released; Balanced releases both | `balance_duration`, or `duration`; 0 or less is indefinite (`comfoair_command_build.cpp:87`) |
| Auto | `ventilation_mode` = Auto | boost off, away off, then manual mode off 1000 ms later | none (`comfoair_command_build.cpp:44`) |

- **A level selects a preset; it does not set an airflow.** How many m³/h each level delivers is
  the unit's own preset setting (below).
- **`fan` off is level 0**, the Away preset — a heat-recovery unit is never stopped. `fan` on
  resumes the level PDO 65 last reported, or Low if none is known or the last level was 0
  (`comfoair_command.cpp:131`). The Away *level* is not away *mode*: away mode is the
  `away_mode` switch and its own timer.
- **Durations.** The operations of this area that take one, `boost`, `set_away` and
  `set_balance_mode`, carry it in seconds in the request and never read the stored settings
  (`comfoair_command.cpp:199`). The entities use `boost_duration`, `balance_duration` and
  `away_duration`, in minutes: writing one sends nothing, stores it in NVS and takes effect for
  the next command (`components/comfoair_control/comfoair_control.cpp:117`). They are loaded from
  NVS at boot, falling back to 60, 60 and 0 (indefinite) (`comfoair_command.cpp:59`), and
  published into the state model at once (`src/main.cpp:229`).
- **Permanent manual mode** is the `permanent_manual` field of `POST /api/config`, on by default
  and stored in NVS (`comfoair_control.cpp:151`). It decides whether a level is preceded by the
  manual-mode entry; see [Commands — Permanent manual mode](/protocol/commands#permanent-manual-mode).
- **State after a write.** Every write is accepted, not confirmed. `fan`, `ventilation_mode`,
  `away_mode` and `balance_mode` show what PDOs 49, 65, 337, 54 and 55 report next; the option a
  client wrote to `ventilation_mode` or `balance_mode` is shown at once and corrected by those PDOs
  (`components/comfoair_http/comfoair_http_command.c:114`).

## Unit settings

VENTILATIONCONFIG (unit 30) defines what each ventilation level means. The four airflow presets
are the m³/h the unit runs at for each level — Away [30.1.3](/unit/properties#prop-30-1-3), Low
[30.1.4](/unit/properties#prop-30-1-4), Medium [30.1.5](/unit/properties#prop-30-1-5) and High
[30.1.6](/unit/properties#prop-30-1-6) — writable 16-bit values in steps of 5 m³/h, each with the
range the unit reports for it. On the reference unit they are 60, 90, 150 and 255 m³/h, so a level
command there selects one of those four airflows. [30.1.8](/unit/properties#prop-30-1-8), 325 m³/h,
is read as the unit's maximum airflow (inferred).

The same unit holds the installer's fan settings:

- **Control mode** [30.1.9](/unit/properties#prop-30-1-9): the fan control law, flow control
  (regulates around the preset, steadier fan speed) or constant flow (regulates exactly to it).
- **Altitude** [30.1.7](/unit/properties#prop-30-1-7): the installation altitude band, used for the
  air-density correction of the computed airflow (PDOs 119 and 120).
- **Unbalance** [30.1.18](/unit/properties#prop-30-1-18): a deliberate supply/extract imbalance in
  percent, −15.0 to +15.0 in steps of 0.1; positive is over-pressure, negative under-pressure.
- **Bathroom switch** [30.1.11](/unit/properties#prop-30-1-11)–[30.1.13](/unit/properties#prop-30-1-13):
  with an Option Box, the delay before a closed bathroom switch raises the airflow to preset 3, the
  run-on time after it opens, and whether the run-on is a fixed time or mirrors the switch.

The other VENTILATIONCONFIG properties are undocumented. FAN (unit 23) holds the two fans, one
subunit each, and answers only undocumented values; the fans' live figures are PDOs 117–122.

The firmware does not read or write these settings. Only the on-demand
[probe walk](/protocol/probe) reads them.

<UnitPropertyTable area="ventilation" />

## Behaviour

- **Boot.** `fan` has no state until the first PDO 65 arrives, `ventilation_mode` none until PDOs 49
  and 65 have both arrived, and `balance_mode` none until PDOs 54 and 55 have both arrived. The three durations hold their NVS values
  from the moment the command sender starts.
- **Where it is published.** All entities above are on `GET /api/state`, the WebSocket push and
  MQTT. In Home Assistant the ventilation is one `fan` entity with six preset modes; the
  `ventilation_mode` select is the same control and is discovered disabled by default
  (`enabled_by_default=False` on its row in `tools/pdo_table.py`). So are `supply_fan_mode` and
  `exhaust_fan_mode`, a third copy of the balance state, and `sensor_ventilation_mode`, a raw
  number. The durations stay available in Home
  Assistant while the bus is silent, because they live on the firmware
  (`comfoair_ha_payload.c:54`).
- **No airflow limits.** The firmware sends a level, never an airflow, and enforces no minimum or
  maximum airflow; the unit applies its own presets and constraints.
- **Airflow constraints.** Why the unit runs below or above the airflow its preset asks for is PDO
  230, published as `airflow_constraints` and split into reasons below and above the request
  (`components/comfoair_protocol/include/airflow_constraints.h`); see
  [Sensors and air quality](/unit/areas/sensors-and-air-quality).
- **Supply-only and extract-only.** Whether an entry took effect shows in PDOs 54 and 55
  (`supply_fan_mode`, `exhaust_fan_mode`, `balance_mode`), PDOs 70 and 71 (`supply_only_active`,
  `exhaust_only_active`) and the countdowns PDOs 86 and 87.
- **Failure.** A command that is not acknowledged on the bus is counted as failed and not retried;
  the published state keeps following the PDOs ([Commands](/protocol/commands#the-command-task)).
