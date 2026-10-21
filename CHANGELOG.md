# Changelog

All notable changes to this firmware are documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this
project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html). The version is
taken from the `v*` git tag; see `docs/development/releasing.md`.
Each release moves the entries below into a `## [X.Y.Z] - YYYY-MM-DD` section, and that
section becomes the text of the GitHub release.

## [Unreleased]

### Added

- The unit's active errors as two entities: `unit_error` (binary sensor, `problem`) and
  `active_errors` (the error texts in one line, at most 255 characters), read over RMI every
  minute. Informational ids (a manual bypass, service mode, a filter reminder) are listed in
  `active_errors` but do not turn `unit_error` on. Both stay unknown until the unit's firmware
  version has been read. Over MQTT, REST, the WebSocket, Home Assistant discovery and the HA
  integration.

### Removed

- The entity table is the ComfoAir Q350/450/600 base unit's (`SCHEMA_VERSION` 2): 76 entities with
  the two error entities above, 60 polled PDOs. 45 entities and 44 polled PDOs are gone: every accessory and Option-Box row
  (ComfoCool, post-heater, ComfoFond, CO2 sensors, ComfoClime, the 0–10 V inputs), the raw schedule
  words 33/321/342/343, `rf_pairing_mode` and `away_indicator` (the `away_mode` switch says the
  same). This is a breaking change: retained MQTT topics of these keys stay on the broker until
  cleared by hand — see docs/development/entity-registry.md, "Retiring keys". The keys, with the
  component their config topic used (`homeassistant/<component>/<device_id>/<key>/config`, then
  `<prefix>/<key>/state`):

    ```text
    sensor analog_input_1
    sensor analog_input_2
    sensor analog_input_3
    sensor analog_input_4
    sensor preset_schedule_entry
    sensor comfocool_schedule_entry
    sensor comfocool_off_remaining
    sensor preset_schedule_enabled_raw
    sensor comfocool_schedule_enabled_raw
    sensor comfocool_timers_raw
    sensor supply_fan_timers_raw
    sensor exhaust_fan_timers_raw
    sensor post_heater_temp
    sensor post_heater_output
    sensor comfofond_outdoor_temp
    sensor comfofond_ground_temp
    sensor comfofond_ghe_state
    sensor comfocool_flow_request
    sensor comfocool_condenser_temp
    sensor co2_sensor_1
    sensor co2_sensor_2
    sensor co2_sensor_3
    sensor co2_sensor_4
    sensor co2_sensor_5
    sensor co2_sensor_6
    sensor co2_sensor_7
    sensor co2_sensor_8
    sensor comfoclime_tpma_temp
    sensor comfoclime_target_temp
    sensor comfoclime_comfort_temp
    sensor comfoclime_indoor_temp
    sensor comfoclime_supply_temp
    sensor comfoclime_exhaust_temp
    sensor comfoclime_supply_coil_temp
    sensor comfoclime_exhaust_coil_temp
    sensor comfoclime_compressor_temp
    sensor comfoclime_heat_pump_duty
    sensor comfoclime_power
    sensor comfocool_off_timer
    sensor rf_pairing_mode
    sensor comfoclime_mode
    binary_sensor away_indicator
    binary_sensor post_heater_present_unverified
    binary_sensor comfofond_present
    binary_sensor comfocool_compressor
    ```

    Plus the old `homeassistant/sensor/<device_id>/bypass_override/config`: that key is now a
    `binary_sensor` (below). Its state topic stays. With the list in `retired.txt` and the default
    `topic_prefix`:

    ```sh
    B="-h <broker> -u <user> -P <password>"; ID=<device_id>; PREFIX=comfoair/$ID
    while read -r comp key; do mosquitto_pub $B -r -n -t "homeassistant/$comp/$ID/$key/config"; done < retired.txt
    mosquitto_pub $B -r -n -t "homeassistant/sensor/$ID/bypass_override/config"
    while read -r comp key; do mosquitto_pub $B -r -n -t "$PREFIX/$key/state"; done < retired.txt
    ```

### Changed

- `operating_mode` value 1 reads "Manual" (was "Manual (temporary)"); `changing_filters` value 1
  reads "Normal" (was "Active"); `device_state` value 7 reads "Away (preset)" (was "Away"): it is
  the fan's Away preset, not away mode.
- `bypass_override` is a binary sensor ("Bypass Override Active", PDO 338 bit 1), no longer an
  Auto/Overridden text. In Home Assistant it is a new entity, its id ending in
  `_bypass_override_active`; the old `sensor` entity is left orphaned, so automations and
  dashboards that named it must be pointed at the new one.
- `sensor_ventilation_mode` (PDO 225) is published as its raw number and created disabled.
- `airflow_constraints` reads "No restrictions" when PDO 230 names no reason: bit 45 clear (was
  "Unknown") or bit 45 set with no listed bit (was "None", which Home Assistant's MQTT sensor
  shows as unknown).
- `supply_fan_mode`, `exhaust_fan_mode` and `intake_air_temp_unverified` are created disabled.
- The fan flows carry `device_class: volume_flow_rate`; the fan speeds are in `rpm` (was `RPM`).
  Home Assistant stops compiling long-term statistics for `exhaust_fan_speed` and
  `supply_fan_speed` until the unit change is accepted under Developer tools → Statistics
  ("units changed").
- `supply_air_temp_post_heater` is displayed as "Supply Air Temperature (controller)"; the key is
  unchanged.
- The State page has no accessory groups any more.
- MQTT: an entity with no value publishes `None` (Home Assistant's "unknown"); a retained value
  that went away no longer stays on the broker as if current.
- MQTT: fan speed, fan flow, avoided heating/cooling and the air temperatures are published with a
  deadband and a minimum interval declared in the entity table (at most five minutes behind);
  REST and the WebSocket are unchanged.

### Fixed

- A changed Airflow Constraints text now reaches MQTT and the WebSocket (it stayed at the first
  value of the boot).

## [0.1.0] - 2026-10-18

The first tagged version. Nothing here has been accepted on the real unit yet.

### Added

- ESP-IDF firmware for the M5Stack NanoC6 (ESP32-C6): one
  universal binary, everything configured at runtime, no Arduino layer.
- CAN link to the ComfoAir Q: the PDO request cycle, the state model and derived entities,
  all generated from the one entity table.
- REST API over the state, a WebSocket push channel for live updates, and a built-in web UI.
- First-run provisioning in the browser: the device's own access point, a captive portal, Wi-Fi
  and broker settings, an optional web password.
- MQTT with Home Assistant discovery: the unit appears as one device with its sensors and
  controls, its availability follows the device, and old entities are cleaned up.
- Commands: fan level, bypass, timers and the unit's other controls, from Home
  Assistant, REST or the web UI.
- Setup and recovery from the button (open the setup window, forget Wi-Fi), and a status light
  that says what the device is doing.
- mDNS: the device answers to the name printed on it.
- A read-only RMI probe of the unit and a Unit capabilities page.
- `GET /api/system` and a System page: firmware, chip, memory, the unit's identity.
- Firmware update from the browser, with automatic rollback if the new image does not come up.
- An ESP Web Tools install page for flashing over USB from Chrome, and a release workflow.
- A web interface in English, German, Dutch and Ukrainian, with a dashboard of the unit.
- Firmware versions follow SemVer, taken from the git tag; this changelog.
- The web UI shows the running firmware's version in the corner of every page.

### Changed

- The web UI has a new visual style.
- The generic entity list moved to `/diagnostics`; `/` is the dashboard.

### Fixed

- Home Assistant no longer shows stale values or "Connected" for a device that has gone dark.
- A mistyped broker no longer deletes the device's entities in Home Assistant.
- A router reboot no longer re-sends the whole discovery set.
- The web page no longer says "Live" while Home Assistant receives nothing.
- After a page reload the browser no longer gets updates to a state it never saw.
- The status light is visible: "all is well" is no longer drawn almost black.
- Temperature Passive, Humidity Comfort and Humidity Protection show the unit's real value after
  a reboot instead of the factory default, and follow a change made on the unit's display.

### Security

- Stored passwords are write-only: no API ever returns them.
- An owned device on its own access point can no longer be re-homed by a stranger.
- The access policy is applied to every write, not only checked on paper.
