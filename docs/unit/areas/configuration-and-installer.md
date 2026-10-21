# Configuration and installer settings

NODECONFIGURATION (`0x20`, one subunit) holds part of what an installer sets when the unit is
commissioned: the maintainer password of the display's installer menu, the unit's orientation, and
further values whose meaning is undocumented. The firmware does not use this unit.

<UnitAreaSummary area="configuration-and-installer" />

## Firmware reads

The firmware reads nothing in this area.

## Firmware writes

The firmware writes nothing in this area. Its RMI write frames address only SCHEDULE (`0x15`)
and TEMPHUMCONTROL (`0x1D`) (`components/comfoair_protocol/include/rmi_frame.h:23`), and it sends
no command of `0x80` or above to NODECONFIGURATION
([PDO and RMI — request payloads](/protocol/pdo-rmi#rmi-request-payloads)).

## Unit settings

- **Maintainer password**, [32.1.3](/unit/properties#prop-32-1-3). The property exists; the
  firmware never returns its value, and a probe walk renders it as redacted by a fixed rule
  (`components/comfoair_probe_report/comfoair_probe_catalogue.cpp:264`).
- **Orientation**, [32.1.4](/unit/properties#prop-32-1-4): which side the supply and extract
  connections are on, 0 = left, 1 = right (`comfoair_probe_catalogue.cpp:41`). The reference unit
  reads right. The orientation decides which physical sensor each temperature and humidity
  position is ([Sensors and air quality](/unit/areas/sensors-and-air-quality)).
- The remaining NODECONFIGURATION properties are undocumented.

Other commissioning values live in their own units and are described on their area pages: the
airflow presets, unbalance, control mode, altitude and bathroom switch in VENTILATIONCONFIG
([Ventilation](/unit/areas/ventilation)), the temperature setpoints and season limits in
TEMPHUMCONTROL ([Temperature and bypass](/unit/areas/temperature-and-bypass)), and the filter
lifetime in FILTER ([Filters and maintenance](/unit/areas/filters-and-maintenance)).

The firmware does not read or write these settings. Only the on-demand
[probe walk](/protocol/probe) reads them.

<UnitPropertyTable area="configuration-and-installer" />

## Behaviour

No entity comes from this area: none of these settings is published.
