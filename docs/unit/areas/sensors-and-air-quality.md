# Sensors and air quality

The unit measures temperature and relative humidity at its air connections, takes up to four
0–10 V control inputs through an Option Box, and reports which controls are holding its airflow
below or pushing it above the requested level. The measurements and that constraint set are PDOs.
The sensor units' RMI properties hold per-position and per-input configuration: TEMPERATURESENSOR
(`0x21`), HUMIDITYSENSOR (`0x22`), PRESSURESENSOR (`0x23`), ANALOGINPUT (`0x25`) and CO2SENSOR
(`0x2B`).

<UnitAreaSummary area="sensors-and-air-quality" />

## Firmware reads

<UnitEntities keys="extract_air_temp,exhaust_air_temp,outdoor_air_temp,preheated_outdoor_air_temp,supply_air_temp,extract_air_humidity,exhaust_air_humidity,outdoor_air_humidity,preheated_outdoor_air_humidity,supply_air_humidity,airflow_constraints" />

- PDOs 274–278 carry the temperatures and PDOs 290–294 the relative humidities at five positions:
  extract air, exhaust air, outdoor air, outdoor air after the pre-heater, and supply air
  (`tools/pdo_table.py`, section "temperature sensors", and section "humidity sensors").
- PDOs 369–372 (the Option Box's 0–10 V inputs) are not published: the firmware targets the base
  unit.
- PDO 230 carries the airflow-constraint bitset (`AIRFLOW_CONSTRAINTS` in `tools/pdo_table.py`).

All of them are on the common request cycle ([Polling](/protocol/polling)). The firmware reads no
RMI property in this area.

## Firmware writes

The firmware writes nothing in this area.

## Unit settings

- **Measuring positions.** TEMPERATURESENSOR and HUMIDITYSENSOR have six subunits each, aligned
  with the positions of PDOs 273–278 and 289–294 (subunits 2–6: extract, exhaust, outdoor, after
  the pre-heater, supply); PRESSURESENSOR has two, the supply-side and exhaust-side static-pressure
  sensors the computed airflow relies on. Property 2 of each subunit reads 1 at the four physical
  measuring points and on both pressure sensors, and 0 on subunit 1 and on the derived
  after-pre-heater point ([33.2.2](/unit/properties#prop-33-2-2),
  [34.2.2](/unit/properties#prop-34-2-2), [35.1.2](/unit/properties#prop-35-1-2) and their
  neighbours); it is read as "a sensor is fitted at this position" (inferred).
- **0–10 V inputs.** ANALOGINPUT has four subunits, one per Option Box input, each with the
  per-input settings of the installer menu, all inferred: the input voltage that maps to 0 %
  ([37.1.2](/unit/properties#prop-37-1-2)) and to 100 % ([37.1.3](/unit/properties#prop-37-1-3)),
  in tenths of a volt; the method ([37.1.4](/unit/properties#prop-37-1-4)), follow the input or
  regulate to a setpoint; and for regulation the setpoint
  ([37.1.5](/unit/properties#prop-37-1-5)), the proportional band in percent
  ([37.1.6](/unit/properties#prop-37-1-6)) and the integral time in seconds
  ([37.1.7](/unit/properties#prop-37-1-7)). On the reference unit all four inputs hold the factory
  defaults: 0 V, 10 V, follow, 5 V, 100 %, 300 s.
- **CO2 zones.** CO2SENSOR has eight subunits, one per CO2 zone; property 1 of each is the zone's bit
  in an eight-bit zone mask, 1, 2, 4 … 128 ([43.1.1](/unit/properties#prop-43-1-1) to
  [43.8.1](/unit/properties#prop-43-8-1)), matching the eight CO2-zone bits of PDO 230. Each zone
  holds, by analogy with the 0–10 V inputs, the CO2 levels mapped to maximum and minimum demand
  ([43.1.2](/unit/properties#prop-43-1-2) = 2000 ppm, [43.1.3](/unit/properties#prop-43-1-3) =
  400 ppm), a setpoint ([43.1.5](/unit/properties#prop-43-1-5) = 1050 ppm), a proportional band
  ([43.1.6](/unit/properties#prop-43-1-6)) and an integral time in seconds
  ([43.1.7](/unit/properties#prop-43-1-7)); these meanings are inferred.

The firmware does not read or write these settings.

<UnitPropertyTable area="sensors-and-air-quality" />

## Behaviour

- **Temperatures** are signed tenths on the wire and are published in °C with one decimal
  (`tools/pdo_table.py`, section "temperature sensors"). The unit is fixed: the firmware converts nothing when the unit's
  own temperature unit is Fahrenheit ([Display and HMI](/unit/areas/display-and-hmi)).
- **Humidities** are published in % as the raw value (`tools/pdo_table.py`, section "humidity sensors").
- **Airflow constraints** are published as text (`components/comfoair_state/comfoair_state_decode.cpp` `apply_one()`, SPECIAL case):
  - `No restrictions` when bit 45 is clear — aiocomfoconnect reads a clear bit 45 as "no
    constraint list", and the Q350 reads 0 at rest — and when bit 45 is set but no decoded bit is
    (`components/comfoair_protocol/include/airflow_constraints.h`). Never `None`: Home
    Assistant's MQTT sensor takes that payload as "unknown";
  - otherwise `Below: …` for the controls throttling the airflow and `Above: …` for those raising
    it, separated by ` | ` (`airflow_constraints.h:130`). Bits 12–15 name 0–10 V inputs 1–4,
    bit 18 the 0–10 V input as a group, and bits 47–54 CO2 zones 1–8 (`airflow_constraints.h:72`,
    `:111`). Bits the decoder does not list are ignored.

  The text is cut to 255 characters, Home Assistant's state limit (`airflow_constraints.h:140`).
