# Other units

This page covers the RMI unit numbers that belong to no functional area: the numbers the
ventilation unit does not implement, and the units it implements without a documented function.
None of them carries a setting or a value the firmware uses. The full list of unit numbers is the
[unit map](/unit/units).

<UnitAreaSummary area="other" />

## Firmware reads

The firmware reads nothing in this area. No PDO is associated with any unit on this page.

## Firmware writes

The firmware writes nothing in this area.

## Unit settings

**Absent unit numbers.** The unit answers a read of these numbers with RMI error 12, *unknown
unit*: they have no subunits and no properties, and provide no function.

- `0x04`–`0x14` (4–20)
- `0x1F` (31)
- `0x2D`–`0x2F` (45–47)

Absence is a statement about RMI unit numbers, not about the unit's functions: the PDOs the
firmware decodes do not depend on any of these numbers.

**Present units with no documented function.** Each answers the generic first property, value 1:

- **COOKERHOOD (`0x26`, 38)** — the placeholder for a ComfoHood cooker hood. Only
  [38.1.1](/unit/properties#prop-38-1-1) answers.
- **Unit `0x29` (41)** — unnamed. Only [41.1.1](/unit/properties#prop-41-1-1) answers.
- **Unit `0x2A` (42)** — unnamed, and the only one of the four with content, all undocumented:
  three u8 values ([42.1.2](/unit/properties#prop-42-1-2)–[42.1.4](/unit/properties#prop-42-1-4)),
  four unsigned 16-bit values ([42.1.5](/unit/properties#prop-42-1-5)–[42.1.8](/unit/properties#prop-42-1-8))
  and ten signed 16-bit values ([42.1.9](/unit/properties#prop-42-1-9)–[42.1.18](/unit/properties#prop-42-1-18)),
  each −400 on the reference unit. This RMI unit number is unrelated to the firmware's CAN node id,
  which is also `0x2A`.
- **SERVICEPRINT (`0x2C`, 44)** — the name this unit number is known by
  (inferred); its function is undocumented. Only [44.1.1](/unit/properties#prop-44-1-1) answers.

Unit `0x2B` is described under [Sensors and air quality](/unit/areas/sensors-and-air-quality).

The firmware does not read or write these units.

<UnitPropertyTable area="other" />

## Behaviour

Only a probe walk reaches these units. A walk asks unit numbers `0x01`–`0x2F` of the target node
(`components/comfoair_probe/include/comfoair_probe.h:89`); a unit whose first read, subunit 1
property `0x01`, is answered with error 12 is reported `absent`, one that gives no answer even
after a retry `timeout`, and any other answer makes it `present`
(`components/comfoair_probe/comfoair_probe_scan.cpp:162`). Unit numbers above `0x2F` are not
asked. Nothing a walk learns about these units enters the firmware's state
([Probe](/protocol/probe#what-a-walk-asks)).
