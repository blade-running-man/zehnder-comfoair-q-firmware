# Heating and frost protection

This area is the heating around the heat exchanger: the internal pre-heater that warms incoming
outdoor air to keep the exchanger from freezing, an optional post-heater driven through an Option
Box that warms the supply air, and the ComfoFond ground heat exchanger. The unit runs frost
protection and pre-heating on its own; it reports pre-heater power and energy and the supply
temperature after the post-heater as PDOs, and holds its heater settings in PREHEATER (unit 25),
POSTHEATER (unit 39) and COMFOFOND (unit 40). The firmware only reports.

<UnitAreaSummary area="heating-and-frost" />

## Firmware reads

The firmware decodes these PDOs into entities and requests them on its polling cycle
([Polling](/protocol/polling)):

<UnitEntities keys="preheater_power,preheater_energy_ytd,preheater_energy_total,supply_air_temp_post_heater" />

- **PDO 146** is the pre-heater's power in W, and **PDOs 144 and 145** its energy this year and in
  total, in kWh. They are separate from the fans' power and energy, PDOs 128–130
  ([Energy and power](/unit/areas/energy-and-power)) (`tools/pdo_table.py`, section "pre-heater").
- **PDO 221** is "Supply Air Temperature (controller)", TEMPHUMCONTROL's own supply-air figure:
  after the post-heater on a unit that has one, equal to PDO 278 on a unit without one. The key
  keeps its `post_heater` name (`tools/pdo_table.py`, row `supply_air_temp_post_heater`).

The outdoor temperature and humidity after the pre-heater, PDOs 277 and 293, are on
[Sensors and air quality](/unit/areas/sensors-and-air-quality).

The firmware reads no RMI property in this area.

## Firmware writes

The firmware writes nothing in this area: no frame it builds addresses PREHEATER, POSTHEATER or
COMFOFOND (`components/comfoair_protocol/include/rmi_frame.h`), and no entity or operation on
[Commands](/protocol/commands) touches them.

## Unit settings

- **PREHEATER** (unit 25) holds five small values. [25.1.3](/unit/properties#prop-25-1-3), an
  on/off value (range 0–1) that reads 1 on the reference unit, which has the pre-heater fitted, is
  read as the pre-heater switch (inferred); the others are undocumented.
- **POSTHEATER** (unit 39) holds three settings with ranges and steps
  ([39.1.3](/unit/properties#prop-39-1-3)–[39.1.5](/unit/properties#prop-39-1-5)); none has a
  documented meaning.
- **COMFOFOND** (unit 40) answers only its generic first property; a ComfoFond reports its data as
  PDOs 416–419.

The firmware does not read or write these settings, and it does not read PDOs 416–419. Only the
on-demand [probe walk](/protocol/probe) reads the settings.

<UnitPropertyTable area="heating-and-frost" />

## Behaviour

- **Frost protection is the unit's own.** The firmware sends nothing that starts, stops or limits
  the pre-heater, and it does not change the ventilation level in response to frost.
- **Airflow under frost protection.** When the pre-heater or frost protection holds the airflow
  below or lifts it above the request, the reason appears in `airflow_constraints` (PDO 230):
  "Preheater" in either direction, "Frost Protection" below the request
  (`components/comfoair_protocol/include/airflow_constraints.h`). The effect on the fans shows in
  PDOs 117–122 ([Ventilation](/unit/areas/ventilation)).
- **Power.** `power` (PDO 128) is the fans alone; the pre-heater is `preheater_power` (PDO 146).
- **Publication.** All four entities are on `GET /api/state`, the WebSocket push and MQTT.
