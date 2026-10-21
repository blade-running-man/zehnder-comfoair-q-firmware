# Energy and power

The unit meters the electrical power of its two fans and accumulates it into energy counters. The
figures are PDOs 128–130; the POWERSENSOR unit behind them (`0x18`, one subunit) exposes no
setting over RMI.

<UnitAreaSummary area="energy-and-power" />

## Firmware reads

<UnitEntities keys="power,energy_ytd,energy_total" />

PDO 128 carries the fans' power now, in W; PDO 129 the fans' energy in the current calendar year,
in kWh, reset at the year boundary; PDO 130 the fans' energy since commissioning, in kWh
(`tools/pdo_table.py`, section "power"). All three measure the fans only. The pre-heater's power and energy are
PDOs 144–146 ([Heating and frost](/unit/areas/heating-and-frost)), and the heating and cooling the
heat exchanger avoids are PDOs 213–218 ([Temperature and bypass](/unit/areas/temperature-and-bypass)).

The three PDOs are on the common request cycle ([Polling](/protocol/polling)). The firmware reads
no POWERSENSOR property.

## Firmware writes

The firmware writes nothing in this area. It has no frame that resets the energy counters.

## Unit settings

POWERSENSOR answers only its generic first property and holds no setting. The firmware does not
read or write it.

<UnitPropertyTable area="energy-and-power" />

## Behaviour

- The values are published as the raw counts, with no scaling (`tools/pdo_table.py`, rows `power`, `energy_ytd` and `energy_total`).
- In Home Assistant, `power` has device class `power` and state class `measurement`;
  `energy_ytd` and `energy_total` have device class `energy` and state class `total_increasing`
  (`tools/pdo_table.py`, rows `power`, `energy_ytd` and `energy_total`; `components/comfoair_ha/comfoair_ha_payload.c:161`).
