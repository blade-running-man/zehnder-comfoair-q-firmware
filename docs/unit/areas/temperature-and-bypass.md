# Temperature profiles, season and bypass

This area is how the unit decides what a comfortable temperature is and acts on it. The unit keeps
a running mean outdoor temperature (RMOT) and compares it with two limits to decide the heating and
cooling season; it holds a comfort temperature for each of three profiles, warm, normal and cool;
it runs three sensor-based ventilation controls; and it opens and closes a modulating bypass that
lets outdoor air skip the heat exchanger. The settings live in TEMPHUMCONTROL (unit 29, `0x1D`);
the bypass and the active profile are moved by SCHEDULE timer entries and reported by PDOs.

<UnitAreaSummary area="temperature-and-bypass" />

## Firmware reads

The firmware decodes these PDOs into entities and requests them on its polling cycle
([Polling](/protocol/polling)):

<UnitEntities keys="running_mean_outdoor_temp,heating_season,cooling_season,profile_target_temp,bypass_position,bypass_override,avoided_heating,avoided_heating_ytd,avoided_heating_total,avoided_cooling,avoided_cooling_ytd,avoided_cooling_total,intake_air_temp_unverified" />

- **PDO 209** is the RMOT; **PDOs 210 and 211** are the heating and cooling season flags.
- **PDO 212** is the target temperature of the profile in force, signed tenths of a degree
  (`tools/pdo_table.py`, row `profile_target_temp`).
- **PDO 227** is the bypass position in percent.
- **PDO 338** is the bypass instance's running-timer bitset; `bypass_override` is on while bit 1
  (the override timer) is set, independently of PDO 66 (`tools/pdo_table.py`, row `bypass_override`).
- **PDOs 213–218** are the heating and cooling the unit avoided: W now, kWh this year and in total.
- **PDO 220** is a temperature whose position is undocumented; the entity name says so
  (`tools/pdo_table.py`, row `intake_air_temp_unverified`). On the Q350 it equals PDO 276, so it is
  discovered disabled by default.

The two writable selects below take their state from PDOs too: `bypass_mode` from **PDO 66**
(0 Auto, 1 Open, 2 Closed) and `temperature_profile` from **PDO 67** (0 Normal, 1 Cool, 2 Warm)
(`tools/pdo_table.py`, `SELECTS` rows `bypass_mode` and `temperature_profile`).

The firmware reads three RMI properties in this area: the sensor-ventilation controls below.

## Firmware writes

<UnitEntities keys="bypass_mode,bypass_duration,temperature_profile,temperature_passive,humidity_comfort,humidity_protection" />

| Command | Entity / operation | What is sent | Duration |
| --- | --- | --- | --- |
| Bypass open / closed | `bypass_mode`, `set_bypass` | SCHEDULE subunit 2 entry, value `01` (open) or `02` (closed) | `bypass_duration`, or the operation's `duration`; 0 or less is indefinite |
| Bypass auto | `bypass_mode` = Auto, `set_bypass` `auto` | release of the subunit 2 entry | none; a `duration` must be present and is ignored |
| Temperature profile | `temperature_profile`, `set_temperature_profile` | SCHEDULE subunit 3 entry, value 0 normal, 1 cool, 2 warm | none: always no expiry |
| Temperature passive, humidity comfort, humidity protection | `temperature_passive`, `humidity_comfort`, `humidity_protection` | SET PROPERTY `03 1D 01 04\|06\|07 <0\|1\|2>` | none |

The builders are `build_bypass()`, `build_profile()` and `build_off_auto_on()` in
`components/comfoair_command/comfoair_command_build.cpp:69`, `:128` and `:158`; the bytes are on
[Commands](/protocol/commands).

- **Option order is protocol order.** The option index is the byte sent: `temperature_profile` is
  Normal, Cool, Warm and the three sensor controls are Off, Auto only, On
  (the "DO NOT REORDER" comments on `tools/pdo_table.py` `SELECTS` rows `temperature_profile` and `temperature_passive`). Selects are written by label, never by index.
- **No profile release.** `set_temperature_profile` accepts `normal`, `cool` and `warm` only; the
  firmware sends no release of subunit 3, so a profile it sets has no expiry
  (`comfoair_command.cpp:233`).
- **`bypass_duration`** is in minutes, 1–1440, default 60. Writing it sends nothing; it is stored in
  NVS, used for the next bypass command written through `bypass_mode`, and loaded again at boot
  (`components/comfoair_control/comfoair_control.cpp:117`, `:60`). `set_bypass` carries its own
  duration in seconds.
- **The three sensor controls are optimistic, and read back.** After a write is queued the firmware
  shows the written option at once (`components/comfoair_http/comfoair_http_command.c:114`; the MQTT
  path does the same). It keeps it in RAM only: the option is not stored in NVS. The unit's own
  value is read back by the [unit read](/protocol/probe#the-unit-read).

## Unit settings

TEMPHUMCONTROL (unit 29) exposes the unit's comfort settings, all writable:

- **Season limits.** The heating limit [29.1.2](/unit/properties#prop-29-1-2) (0.0–15.0 °C) and
  the cooling limit [29.1.3](/unit/properties#prop-29-1-3) (15.0–40.0 °C), in tenths of a degree:
  while the RMOT is below the heating limit the heating season is active, while it is above the
  cooling limit the cooling season. They decide PDOs 210 and 211.
- **Profile mode** [29.1.8](/unit/properties#prop-29-1-8): how the comfort temperature of each
  profile is set — adaptive (0), where it follows the RMOT (normal is 21.5 °C at an RMOT of 0 °C and
  rises 0.11 °C per °C), or fixed (1), where each profile uses its stored setpoint. The reference
  unit is in fixed mode.
- **Fixed setpoints**, in tenths of a degree: the comfort temperature of the warm
  [29.1.10](/unit/properties#prop-29-1-10), normal [29.1.11](/unit/properties#prop-29-1-11) and cool
  [29.1.12](/unit/properties#prop-29-1-12) profiles in fixed mode. The profile in force, set by
  `temperature_profile`, picks one of them; PDO 212 reports the result.
- **Sensor-based ventilation**, each 0 off, 1 auto only, 2 on. Temperature passive
  [29.1.4](/unit/properties#prop-29-1-4) ventilates on preset 3 when cooling is useful (cooling
  season, indoor well above outdoor, bypass in AUTO, both fans running); humidity comfort
  [29.1.6](/unit/properties#prop-29-1-6) raises the airflow when the indoor absolute humidity
  exceeds its reference; humidity protection [29.1.7](/unit/properties#prop-29-1-7) ventilates on
  preset 3 when the extract air exceeds 75 % RH. They are the only settings in this area the
  firmware uses; it writes them and reads them back.

The other TEMPHUMCONTROL properties are undocumented. VALVE (unit 22) answers only undocumented
values; the bypass position the firmware publishes is PDO 227.

The firmware does not read or write the season limits, the profile mode or the fixed setpoints.
Only the on-demand [probe walk](/protocol/probe) reads them.

<UnitPropertyTable area="temperature-and-bypass" />

## Behaviour

- **The three sensor controls are read back.** No PDO carries them; the probe task reads them with
  GET ONE `01 1D 01 10 04` / `06` / `07` in its [unit read](/protocol/probe#the-unit-read): first
  about 10 s after boot, then every 6 h, and every 60 s while any of the three has had no final
  answer (an RMI error is final, a timeout is not). Until the first answer each is unknown; the
  firmware publishes no factory default. A read is skipped while a command is queued or being sent,
  and a reply is dropped if a command was submitted while it was in flight, so a fresh write is
  never overwritten by the unit's pre-write value. A change made on the unit's display or in
  ComfoConnect is seen at the next read, within 6 h. Nothing is stored in NVS: that would only
  know what this firmware wrote. The reference unit (Q350, R1.11.0) read On / Off / Off.
  Whether a control is acting shows indirectly too:
  PDO 225, PDO 226 running continuously, and the Temperature Passive, Humidity Comfort and Humidity
  Protection reasons of `airflow_constraints` (PDO 230)
  (`components/comfoair_protocol/include/airflow_constraints.h`).
- **Availability.** `bypass_duration` stays available in Home Assistant while the bus is silent,
  because its value lives on the firmware (`components/comfoair_ha/comfoair_ha_payload.c:54`). The
  three sensor controls, `bypass_mode` and `temperature_profile` follow the bus: their values come
  from the unit, so a dead bus greys them out.
- **Bypass and temperature passive.** Temperature passive acts only with the bypass in AUTO
  ([29.1.4](/unit/properties#prop-29-1-4)), so for as long as an open or closed override from the
  firmware runs, that control does not act. PDO 82 counts the override down
  ([Schedules and timers](/unit/areas/schedules-and-timers)).
- **Confirmation.** A bypass or profile write is accepted, not confirmed; PDOs 66, 338 and 67
  report the result. The option written to `bypass_mode` or `temperature_profile` is shown at once
  and replaced by the next PDO.
- **Units.** Temperatures are published in °C unconditionally; PDO 208 reports the unit the
  display uses (`tools/pdo_table.py`, row `temperature_unit`, [Display and HMI](/unit/areas/display-and-hmi)).
