# Schedules and timers

SCHEDULE (unit 21, `0x15`) is the unit's timer engine. Every timed override the unit accepts from
outside — a ventilation level, boost, away mode, a bypass override, a temperature profile,
supply-only or extract-only, manual mode — is a *timer entry* on one of its ten subunits, enabled
with a value and a timeout or released; the unit counts pending changes down in PDOs. Every
command the firmware sends
to the unit, apart from the three sensor-ventilation settings, is a SCHEDULE timer entry.

<UnitAreaSummary area="schedules-and-timers" />

## Firmware reads

The firmware decodes the four countdown PDOs into entities and requests them on its polling cycle
([Polling](/protocol/polling)):

<UnitEntities keys="next_fan_change_in_raw,next_bypass_change_in_raw,next_supply_change_in_raw,next_exhaust_change_in_raw" />

Each is the number of seconds to the next change of that function: **PDO 81** the fan level (it
counts down a temporary level and away mode), **PDO 82** the bypass, **PDOs 86 and 87** the supply
and extract fan. `0xFFFFFFFF` means nothing is pending, and the entity then has no value
(`tools/pdo_table.py`, section "schedule countdowns"). The entity keys are fixed: the Home Assistant integration resolves
them by key and turns them into timestamp sensors (the same section's comment).

What the entries do is read from the PDOs of the functions they control: PDOs 49, 56, 65 and 16 on
[Ventilation](/unit/areas/ventilation), PDOs 66, 338 and 67 on
[Temperature and bypass](/unit/areas/temperature-and-bypass).

The firmware reads no timer entry: it sends no GETTIMERENTRY (`83 15 <subunit> <property>`).

## Firmware writes

The firmware writes two kinds of frame to SCHEDULE, built in
`components/comfoair_protocol/include/rmi_frame.h:56` and `:74`:

```text
enable : 84 15 <subunit> <property> 00 00 00 00 <timeout u32 LE> <value>
release: 85 15 <subunit> <property>
```

The timeout is in seconds and read as signed; −1 (`FF FF FF FF`) means no expiry (`rmi_frame.h:32`).

| Subunit / property | Function | Firmware command | Value | Timeout |
| --- | --- | --- | --- | --- |
| 1 / 1 | ventilation level | level 0–3 | the level | 1 s |
| 1 / 6 | boost | boost; Auto releases it | `03` | the duration; 0 or less releases the entry instead |
| 1 / 11 | away mode | away on / off; Auto releases it | `00` | the duration; 0 or less is −1 |
| 2 / 1 | bypass | open / closed / auto (release) | `01` / `02` | the duration; 0 or less is −1 |
| 3 / 1 | temperature profile | normal / cool / warm | 0 / 1 / 2 | always −1 |
| 6 / 1 | supply fan (supply only) | balance mode | `01` | the duration; 0 or less is −1 |
| 7 / 1 | extract fan (extract only) | balance mode | `01` | the duration; 0 or less is −1 |
| 8 / 1 | manual mode | before each level, if permanent manual is on; Auto releases it | `01` | 1 s |

The firmware does not use subunits 4, 5, 9 and 10, or the other properties of subunit 1. Details
of each command: [Commands](/protocol/commands).

- **The 1-second timeout.** Level and manual-mode entries carry a fixed 1 s timeout, the value the
  official app sends; it is what makes them hold. The firmware does not substitute −1 there
  (`rmi_frame.h:33`).
- **Temporary and permanent levels.** A level entry alone is the unit's temporary change (PDO 49 =
  1): the unit returns to its schedule at the next scheduler step or after at most two hours, and
  PDO 81 counts it down. With permanent manual on — the default, the `permanent_manual` field of
  `POST /api/config` — the manual-mode entry on subunit 8 goes first and the unit is in permanent
  manual mode (PDO 49 = 5) (`components/comfoair_command/comfoair_command_build.cpp:115`).
- **Auto** releases boost and away at once, both of which override the unit's scheduler, and
  manual mode 1000 ms later (`comfoair_command_build.cpp:40`).
- **Balance mode** always sends both subunit 6 and 7 frames, so switching between supply-only and
  extract-only never leaves both entries set (`comfoair_command_build.cpp:87`).
- **Durations.** The operations carry their duration in seconds per request. The entities use
  four stored settings in minutes — `boost_duration`, `bypass_duration` and `balance_duration`
  1–1440, default 60; `away_duration` 0–43200, default 0 = indefinite (`tools/pdo_table.py`, row `away_duration`) —
  converted to seconds when the frame is built. They are kept in NVS and loaded at boot
  (`components/comfoair_control/comfoair_control.cpp:60`). The duration entities are listed on
  [Ventilation](/unit/areas/ventilation) and [Temperature and bypass](/unit/areas/temperature-and-bypass).
- **No profile release.** The firmware never releases subunit 3; a profile it sets has no expiry.

## Unit settings

SCHEDULE holds no settings of its own: its subunits are the timer entries above. Over GET ONE each
subunit answers only two header bytes — property 1, the generic first property, and property 2,
which equals the subunit's own number and is read as a self-index (inferred). Whether an entry is
enabled, its timeout, its remaining time and its value are read only with GETTIMERENTRY.

The firmware does not read or write these properties.

<UnitPropertyTable area="schedules-and-timers" />

## Behaviour

- **Entries are not read back.** After boot the firmware does not know which entries are running;
  it learns the result of any entry, its own or another controller's, only from the PDOs above.
- **Accepted, not confirmed.** A timer write is queued and sent; the firmware does not read the
  unit's response ([Commands — How results are reported](/protocol/commands#how-results-are-reported)).
- **Countdowns.** The four countdown entities are diagnostic in Home Assistant and have no value
  while nothing is pending.
- **Away mode** is a timer entry, timer 11 ("ignore all schedulers" for its duration), counted
  down by PDO 81 and reported by PDO 337 bit 11 — the bit `away_timer_running` reads, and the
  state of the `away_mode` switch (`AWAY_SWITCH` in `tools/pdo_table.py`), because the switch
  starts and cancels that same timer. It is distinct from the Away *level*, which is level 0 on
  subunit 1 property 1. PDO 16 value 7 (`device_state` "Away (preset)") follows the Away
  *level*: on a Q350, switching the level Away → Low on the display moved PDO 16 from 7 to
  1 with bit 11 clear and no countdown, so PDO 16 cannot tell the two apart and the switch does not
  read it.
