# Filters and maintenance

The unit counts down the days to the next filter change and keeps its filter schedule in the
FILTER unit (`0x1C`, one subunit). The countdown and the filter status are PDOs; the replacement
interval is an RMI property. The firmware publishes the countdown and the status and leaves the
schedule to the unit.

<UnitAreaSummary area="filters-and-maintenance" />

## Firmware reads

<UnitEntities keys="filter_replacement_remaining_days,changing_filters" />

PDO 192 carries the days remaining until the filters are due (`tools/pdo_table.py`, row `filter_replacement_remaining_days`). PDO 18
carries the filter status: 1 = normal, 2 = changing filter (`tools/pdo_table.py`, row `changing_filters`). Both are on
the common request cycle ([Polling](/protocol/polling)). The firmware reads no FILTER property.

## Firmware writes

The firmware writes nothing in this area. It has no filter-reset frame: the RMI write frames
address only SCHEDULE (`0x15`) and TEMPHUMCONTROL (`0x1D`)
(`components/comfoair_protocol/include/rmi_frame.h:23`), and none of the
[commands](/protocol/commands) concerns the filters.

## Unit settings

- **Filter lifetime** [28.1.3](/unit/properties#prop-28-1-3): the filter replacement interval in
  days, a writable two-byte value with range 0–65535 and step 1; 180 on the reference unit, the
  documented maximum. The unit shortens the effective interval at high airflow; PDO 192 is the
  live countdown.
- **Pre-warning period** [28.1.2](/unit/properties#prop-28-1-2): 21 days, read as how many days
  before the expected filter change the unit starts warning (inferred).

No RMI command that resets the filter countdown is documented; the unit's own filter wizard resets
it.

The firmware does not read or write these settings.

<UnitPropertyTable area="filters-and-maintenance" />

## Behaviour

- `filter_replacement_remaining_days` is published in days (`d`, Home Assistant device class
  `duration`) as the raw count, with no scaling (`tools/pdo_table.py`, row `filter_replacement_remaining_days`).
- `changing_filters` maps values 1 and 2 only; any other value leaves the entity without a value
  rather than with an invented label (`components/comfoair_state/comfoair_state_decode.cpp` `enum_label()` and `apply_one()`).
- While the unit's filter wizard runs, Device State (PDO 16) reads `Filter Wizard`; that entity
  belongs to [Ventilation](/unit/areas/ventilation).
