<script setup>
import UnitBadge from '../.vitepress/theme/components/UnitBadge.vue'
</script>

# Property reference

Every RMI property of the ventilation unit, grouped by unit. A row is one property:

- **Key** — the address `unit.subunit.property`, in decimal, as an RMI read or write uses it.
- **Name** and **Meaning** — what the property is; *details* gives the encoding and the sources.
- **Value** — the value on the reference unit, with its unit of measure; hovering shows the raw
  bytes and the format.
- **Range** and **Step** — the minimum, maximum and step the unit reports, `—` when it reports
  none.
- **Writable** — whether the property can be changed over RMI: *yes*, *no*, or `—` when not
  documented.
- **Firmware use** — where this firmware reads or writes the property, `—` where it does not.

**Confidence** says how the meaning is established: <UnitBadge level="confirmed" /> named in
Zehnder documentation or a published protocol description, <UnitBadge level="inferred" /> derived but not documented,
<UnitBadge level="unknown" /> no documented meaning. The value, range and step are always the
unit's own answer.

<UnitPropertyTable />

Values are those of the reference unit, a ComfoAir Q350 on unit firmware R1.11.0; the
serial number and the maintainer password are redacted.
