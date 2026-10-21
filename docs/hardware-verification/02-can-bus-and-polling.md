# 02 · CAN bus and polling

Reading the unit: that its broadcasts arrive and mean what the unit's display says, that a second
node on the bus does not corrupt them, and how the device decides the unit has gone quiet. Every
item here needs the unit. PDO frames are logged only at DEBUG, so the observations go through
`GET /api/state`, the `bus` block of `GET /api/system` and the phase.

## Reading the unit

As-built: [CAN bus](/protocol/can-bus), [PDO and RMI](/protocol/pdo-rmi), [Polling](/protocol/polling).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CAN-01 | Device on the unit's bus. Read the state and the bus block twice, a few seconds apart | `GET /api/state` holds numbers for the temperatures and fans, `null` only for entities the unit has not sent yet. `bus.state` = `active`, `bus.rx_frames` grows between the two reads, `bus.bus_recoveries` = 0, `commands.failed` = 0, `bus.nodes_seen` contains `1` (the unit) | `curl -s http://<ip>/api/state`; `curl -s http://<ip>/api/system` | unit |
| HV-CAN-02 | Compare with the unit's own display: `supply_air_temp`, `extract_air_temp`, `outdoor_air_temp`; change the ventilation level on the unit; watch `supply_fan_speed`/`exhaust_fan_speed` | Temperatures agree with the display to the displayed precision; the level entity follows the change at the unit within seconds; the fan speeds rise with the level. A value off by a factor of 10 or by a constant is a defect now — the scaling lives in `tools/pdo_table.py`. Record every mismatch with its key | dashboard or `GET /api/state`; the unit's display | unit |
| HV-CAN-03 | Leave the device on the unit for an hour (one full request sweep) | Record every entity still `null` in `GET /api/state` after the hour. Each is either a PDO this unit does not send or a table error in `tools/pdo_table.py` — either way a finding, not noise | `curl -s http://<ip>/api/state` | unit |
| HV-CAN-04 | Put a second node on the bus that sends remote-transmission requests (for example 62 times an hour). Watch for several minutes | `device_state` and `away_mode` never fall back to `null` or flicker; `fan.speed` changes only when the level really changes at the unit. Another node's RTR must not reset entities or produce a random fan level | `GET /api/state` repeatedly; dashboard | unit, second node |

## When the unit stops answering

The bus verdict: `fresh` while frames move, `probing` after 60 s of silence (one extra request
sweep), `silent` after 90 s (MQTT `<prefix>/bus` = `offline`, phase `bus-silent`), and `never-seen`
until the first frame. As-built: [Polling](/protocol/polling#when-the-unit-stops-answering),
[Indication](/device/indication#hold-offs-facts-enter-at-once-absences-late),
[Home Assistant availability](/interfaces/home-assistant-discovery#availability).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CAN-05 | With a CAN analyser on a healthy bus for **60 minutes** (not 15 — the request sweep is hourly): measure the largest gap between any two received frames, and per identifier; then, at a provoked silence, the time from the device's probing sweep to the unit's first answer | The overall largest gap is well under the 60 s `probing` threshold (at least a factor of two; record whether it also holds a factor of three, which would argue for a larger `COMFOAIR_BUS_QUIET_MS`). The unit answers the probing sweep within 30 s, before `silent` at 90 s. The thresholds are argued, not measured — this item is what measures them | analyser log | unit, analyser |
| HV-CAN-06 | Silent bus: does the controller go bus-off or stay error-passive? Power the unit off (or pull the bus connector with the transceiver still powered). Send ten `curl -X POST http://<ip>/api/ops/set_ventilation_level -d '{"level":2}'` so the node transmits; read `GET /api/system` every few minutes for 30 minutes | Record which outcome occurs. **A:** `bus.state` = `passive`, `tx_error_count` freezes near 128–136, `bus_errors` grows, `bus_recoveries` stays 0, never `off`. **B:** `bus.state` reaches `off` and `bus_recoveries` grows. Both are legitimate; the recorded signature is what the troubleshooting guide will describe | `curl -s http://<ip>/api/system` | unit |
| HV-CAN-07 | Device online. Switch the unit off at its breaker. Stopwatch | After about 90 s: phase `bus-silent`, the log shows one transition line `phase: online -> bus-silent (bus silent)`, `<prefix>/bus` = `offline` (retained); `<prefix>/status` stays `online`; no reboot | `GET /api/log`; `mosquitto_sub -v -t '<prefix>/bus'` | unit, broker |
| HV-CAN-08 | Same situation, in Home Assistant | The unit's readings and the fan are **unavailable** instead of showing the last values; the device card stays; the six device-held entities (the four durations, Schema Version, Data Freshness) stay available; the three sensor-ventilation selects grey out with the readings — see [HV-HA-05](./06b-home-assistant) | HA device card | unit, broker, HA |
| HV-CAN-09 | Unit still off. Reboot the device | After it connects to the broker, `<prefix>/bus` is published `offline` **before** the states are republished, and Home Assistant never shows yesterday's values, not even for a moment | `mosquitto_sub -v -t '<prefix>/#'` from before the reboot; HA history | unit, broker, HA |
| HV-CAN-10 | Switch the unit back on | `<prefix>/bus` = `online` within seconds of the first frame (no debounce), the entities come back with fresh values, the phase and LED return to `online` | `mosquitto_sub`; `GET /api/status` | unit, broker |
| HV-CAN-11 | Unit **powered** but the bus connector pulled (transceiver powered, no line). Repeat HV-CAN-06 and HV-CAN-07 | Record the `bus` block and compare with the unpowered-unit signature. If they are identical, an unpowered unit and a pulled connector cannot be told apart from `/api/system`, and the troubleshooting guide must say so rather than pretend to | `GET /api/system` | unit |
| HV-CAN-12 | Healthy bus, device left for two hours | No transition to `bus-silent`, and no extra sweep: the probing sweep is sent once per **entry** into `probing`, never on a timer. With an analyser: exactly one sweep of requests per provoked silence, none on a healthy bus. One sweep a minute would be sixty-fold the bus traffic | `GET /api/log` (no `bus-silent` line); analyser | unit (analyser) |
| HV-CAN-13 | Boot the device with the CAN loom **disconnected** and leave it an hour | `<prefix>/bus` = `offline` (nothing has been heard, so nothing is shown as current), but the phase **never** becomes `bus-silent` and the LED never shows the amber double pulse: the bus is `never-seen`, not "went quiet" | `GET /api/status` (`phase`); `mosquitto_sub -v -t '<prefix>/bus'`; LED | broker |
| HV-CAN-14 | **The start-up grace.** Unit on, broker up. Power-cycle the device; as soon as `GET /api/state` shows readings, switch the unit off at its breaker (by about 120 s of uptime). Watch the LED and read `GET /api/status` (`phase`, `uptime_ms`) until uptime passes 300 s; then switch the unit on | About 90 s after the unit went off: phase `bus-silent` and the log line `phase: online -> bus-silent (bus silent)`, but the LED shows the **double pulse in blue**, not amber. At 300 s of uptime the colour turns **amber** with the rhythm unchanged and **no** new `phase:` line — the grace recolours the LED only, never the phase. While blue, the look is the same as `net-lost`; `/api/status` tells them apart. Once the unit is on, the values fill within one sweep without any action on the device and the phase returns to `online` | LED; `GET /api/status`; `GET /api/log`; `GET /api/state` | unit, broker |
| HV-CAN-15 | With an analyser: the start-up sweep after the device boots on a live bus | One request per entry of `COMFOAIR_REQUEST_PDOS` (60) at 100 ms spacing, about 6.0 s in total. Without an analyser: the card fills within about 6 s of boot | analyser; dashboard | unit (analyser) |

Why HV-CAN-14 has the unit heard first: a unit that is already off when the device boots is never
heard, so the bus stays `never-seen` and the phase never becomes `bus-silent` (HV-CAN-13) — there
is no amber look to soften and the grace has nothing to do. The grace is an LED rule alone
(`GRACE_MS` and `look_is_inferred_from_absence()` in `components/comfoair_led/comfoair_led_pattern.c`):
during the first 300 s of uptime it repaints exactly two looks, `bus-silent` and `no-broker`, in the
blue of the `working` look and keeps their rhythm; the phase, `/api/status` and MQTT are untouched.
Reaching `bus-silent` inside the window needs a bus that was heard and then lost, 90 s before the
window closes.

## Schedule, bypass and frost PDOs

The firmware publishes the base unit's PDOs only. Of the reference doc's "Accessory and schedule
PDOs" table that is 219, 228 and 337. These items check those, the bypass override bit, the
corrected labels and that every table PDO is heard. HV-CAN-17 and HV-CAN-19 are struck out: the
accessory and raw timer rows they checked are not in the table. As-built: [PDO and RMI reference
§1](/reference/comfoair-pdo-rmi#accessory-and-schedule-pdos-from-aiocomfoconnect).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CAN-16 | After at least 65 minutes of uptime, capture the PDO census and compare it with the table ([HV-PROBE-21, HV-PROBE-22](./09-rmi-probe#the-pdo-census)) | *In the table but never heard* is empty on a VV unit. On a unit without a pre-heater only 144–146, 219, 277 and 293 may be there. *Heard but not in the table* holds the unknown 17, 222, 305/306 and 385 and any accessory PDO the base unit broadcasts unasked (401 and 418 on the reference Q350): record them with the unit model and firmware, as findings, not defects. A heard PDO whose entity is still `null` in `GET /api/state` is a decoding finding | `python3 tools/pdo_census_compare.py`; `curl -s http://<ip>/api/state` | unit |
| ~~HV-CAN-17~~ | ~~With a CO2 sensor or a ComfoClime fitted: does it broadcast PDO 1024+ / 41xx on raw CAN?~~ | Obsolete: the firmware publishes no CO2 or ComfoClime row. An accessory PDO the base unit hears is recorded under HV-CAN-16 | — | — |
| HV-CAN-18 | Start a boost from the unit's display, then TASK MENU > AWAY; cancel both; then set the ventilation level to Away | `boost_timer_running` true for the boost only; `away_timer_running` **and the `away_mode` switch** true for TASK MENU > AWAY only; all false after cancelling. At the Away *level* `away_mode` stays false (`device_state` reads `Away (preset)` — it follows the level) | `GET /api/state` | unit |
| ~~HV-CAN-19~~ | ~~Over a day, record the largest value of the raw timer rows~~ | Obsolete: the raw timer rows (PDOs 341–343) and the ComfoCool countdown (85) are not published | — | — |
| HV-CAN-20 | In winter, with frost protection active (`airflow_constraints` names FrostProtection) | `frost_protection_unbalance` > 0 while active, 0 otherwise | `GET /api/state` | unit, cold weather |
| HV-CAN-21 | In Home Assistant after discovery | Exactly six entities are disabled: Ventilation Mode, Supply Fan Mode, Extract Fan Mode, Intake Air Temperature (unverified), Sensor Ventilation Mode (raw, unverified), Frost Protection Pre-heater Power (unverified). No accessory, 0–10 V, RF pairing, schedule-word or Away Indicator entity exists | HA entity list filtered by the device | unit, HA |
| HV-CAN-22 | Set Bypass Mode to Open (or Closed) in the web UI; then back to Auto | `bypass_override` is `true` while the override runs and `false` after Auto; PDO 338 in the census reads `02000000` during it | `GET /api/state`; census `last` of 338 | unit |
| HV-CAN-23 | Compare the labels with the display: the level set by hand (MANUAL), the filter status, the fan at the Away preset | `operating_mode` = `Manual` with no countdown (`next_fan_change_in_raw` null); `changing_filters` = `Normal`; `device_state` = `Away (preset)` while the fan level is Away and `Normal` at Low. Record the value of PDO 49 if a permanent manual mode is set: a 5 confirms the "Manual (permanent)" label | `GET /api/state`; the unit's display | unit |
