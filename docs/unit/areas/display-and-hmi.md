# Display and HMI

The unit's own display and buttons are the HMI unit (`0x1A`, one subunit). The firmware does not
drive the display. It reads the two display-unit settings the unit broadcasts as PDOs, because
they say in which units the unit itself shows temperatures and airflow.

<UnitAreaSummary area="display-and-hmi" />

## Firmware reads

<UnitEntities keys="temperature_unit,airflow_unit" />

PDO 208 carries the temperature unit: 0 = Celsius, 1 = Fahrenheit (`tools/pdo_table.py`, row `temperature_unit`).
PDO 224 carries the airflow unit: 1 = kg/h, 2 = l/s, 3 = m³/h (`tools/pdo_table.py`, row `airflow_unit`). Both are
on the common request cycle ([Polling](/protocol/polling)). The firmware reads no HMI property.

## Firmware writes

The firmware writes nothing in this area.

## Unit settings

HMI has nine properties. Two are read as the display's basic options, both inferred:

- **Display language** [26.1.4](/unit/properties#prop-26-1-4): the text `ENGLISH`, with no range.
- **Display brightness** [26.1.3](/unit/properties#prop-26-1-3): 70 %, range 10–100 in steps of 5.

The other HMI properties are undocumented, and [26.1.9](/unit/properties#prop-26-1-9) answers a
read with an undocumented RMI error.

The firmware does not read or write these settings.

<UnitPropertyTable area="display-and-hmi" />

## Behaviour

- Both entities are published as text and placed in Home Assistant's diagnostic category
  (`tools/pdo_table.py`, rows `temperature_unit` and `airflow_unit`). A value outside their maps leaves the entity without a value
  (`components/comfoair_state/comfoair_state_decode.cpp` `apply_one()`).
- The firmware converts nothing. Temperatures are published in °C and the fan flows (PDOs 119
  and 120) in m³/h whatever these two settings say; when the unit is set otherwise, these two
  entities are the only indication (the comment above `tools/pdo_table.py` rows `temperature_unit` and `airflow_unit`).
