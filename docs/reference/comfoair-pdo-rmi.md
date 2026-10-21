# ComfoAir Q — canonical PDO & RMI reference

This file is the single source of truth for the firmware's PDO table and RMI frames. Section 0
records which layers of the protocol knowledge are first-hand and which are not. Where a width,
divider or label was ambiguous, the chosen value and its evidence are recorded here. Bus evidence
from the owner's own unit is in [`rmi-probe-findings-q350.md`](./rmi-probe-findings-q350.md), which
outranks this file wherever the two disagree.

Companion document: [`comfoair-q-hardware.md`](./comfoair-q-hardware.md) is the **device-side source
of truth** — compiled from the four official Zehnder PDFs in [`docs/manuals/`](../manuals/), every
fact carrying a page citation. **This file stays authoritative for the protocol** (PDO widths and
dividers, RMI frame layout, opcodes); the manuals contain no protocol material. Where the hardware
reference documents the *behaviour* a frame reaches for, or supplies physical evidence for a
verdict, it is cited below by section and Zehnder page. The two files point at each other on
purpose: a claim about what the unit *does* belongs there, a claim about what goes on the wire
belongs here.

## 0. How far each layer is first-hand

Much of the protocol knowledge below was gathered through Zehnder's ComfoConnect LAN C gateway,
which speaks length-prefixed protobuf over TCP 56747 and translates it into bus traffic. The LAN C
never exposes a CAN identifier. That single fact decides how much of each layer to believe:

| Layer | First-hand? | Verdict |
| --- | --- | --- |
| **RMI payload bytes** (`84 15 01 01 …`, `03 1D 01 04 …`) | Yes — the LAN C carries them verbatim inside `CnRmiRequest{nodeId, message}` | Authoritative. |
| **PDO ids, types, dividers** | Yes — the same values arrive in `CnRpdoNotification` | Authoritative; where a stated width or divider contradicts the values themselves, the values win (§1). |
| **CAN bit rate, CAN-id layout** | No — not visible through the LAN C | Believe only what has been corroborated on a working CAN bus. |

Two CAN-layer facts are settled by working hardware, not by the LAN C captures. **Do not "fix" the
firmware away from them.**

- **Bit rate: 50 kbps.** A figure of 100 kbit/s is wrong. The bus runs at 50 kbps; the board
  descriptors in `components/board/` set `can_bitrate = 50000`, and the owner's Q350 answered
  4226 RMI requests at that rate with no timeouts
  ([`rmi-probe-findings-q350.md`](./rmi-probe-findings-q350.md) §1). Keep 50 kbps.
- **`AnotherCounter`, bits 12-13 of the RMI CAN id: send 0.** Captures taken from a LAN C set it
  to **1**: decoding `1F011074` with the bit grouping `11111 0000 0001 0001 00 0001 110100`, bits
  15-12 are `0001` — ErrorOccured 0, MultiMsg 0, AnotherCounter **1**. A plain CAN node sending
  **0** is answered: the owner's Q350 replied to every request from node `0x2A` with
  `AnotherCounter = 0` ([`rmi-probe-findings-q350.md`](./rmi-probe-findings-q350.md) §1). The
  frame builders in `rmi_frame.h`/`rmi_read.h` take the value as a parameter; the callers choose
  0: every command (`send_frame()` in `components/comfoair_control/comfoair_control.cpp`) and the
  unit identity read (`components/comfoair_probe_task/comfoair_probe_task_unit.cpp`). A probe walk
  uses the `another_counter` of its `POST /api/probe` request, default 0
  (`components/comfoair_http/comfoair_http_probe.c`). Keep 0, and treat the LAN C value as
  LAN-C-specific rather than as a contradiction to resolve on the bus.

### Commands that can brick the unit

This is the only protocol operation known to be destructive. Command `0x80` or `0x82` on
NODECONFIGURATION (`0x20`) will probably break the unit's configuration, and **any** `>= 0x80`
command on NODE (`0x01`) can probably brick the ventilation unit completely — it enters factory
mode or tries to perform a firmware update.

Concretely, before adding any command byte `>= 0x80` to the firmware:

- **Never** send any `>= 0x80` command to unit `0x01` (NODE). Read NODE properties with `0x01` or
  `0x02` only.
- **Never** send `0x80` or `0x82` to unit `0x20` (NODECONFIGURATION).
- The `>= 0x80` commands this firmware sends are all on other units: `0x84` ENABLETIMERENTRY and
  `0x85` DISABLETIMERENTRY on SCHEDULE (`0x15`) — the only `>= 0x80` builders in `rmi_frame.h`,
  whose unit byte is fixed to `UNIT_SCHEDULE` — and the fixed `80 03 01` GETACTIVEERRORS on ERROR
  (`0x03`, `get_active_errors()` in `rmi_read.h`). `0x83` GETTIMERENTRY on SCHEDULE (captured, §3)
  and `0x82` RESETALLERRORS on ERROR (§4) are documented on those units, but this firmware does not
  send them.
- A single wrong unit byte turns a diagnostic read into a bricking write. Any new `>= 0x80` frame
  builder must pin its unit byte in a host test before it is ever sent.

## 1. PDO table

**What the firmware publishes.** Every row of this table except 369–372 (the Option Box's 0–10 V
inputs) and 402 (the unverified post-heater flag); PDO 230 (§4); and, of the "Accessory and
schedule PDOs" table below, 219, 228 and 337. Together they are `ALL_REQUEST_PDOS` in
`tools/pdo_table.py`, requested at boot and hourly. The firmware targets the ComfoAir
Q350/450/600 base unit: the other rows are protocol knowledge for decoding a census, not entities.

| PDO | Canonical name | Width / sign | Div | Unit / values |
| --- | --- | --- | --- | --- |
| 16 | Device State | UINT8 | 1 | 0=init, 1=normal, 2=filterwizard, 3=commissioning, 4=supplierfactory, 5=zehnderfactory, 6=standby, **7=away** (on a Q350: the fan's Away preset, PDO 65 = 0 — not TASK MENU > AWAY), 8=DFC |
| 18 | Filter Status | UINT8 | 1 | 1=normal (application mode), 2=changing filter — 0 undocumented |
| 49 | Operating Mode | UINT8 (0xFF = −1) | 1 | −1=auto, 1=limited manual, 5=unlimited manual, 6=boost, 11=away; 1 seen with no countdown on a Q350 (manual), 5 not seen on a unit |
| 54 | Supply Fan Mode | UINT8 (0xFF = −1) | 1 | −1=balanced, 1=supply only |
| 55 | Exhaust Fan Mode | UINT8 (0xFF = −1) | 1 | −1=balanced, 1=exhaust only |
| 56 | Operating Mode (manual flag) | UINT8 (0xFF = −1) | 1 | −1=auto, 1=unlimited manual |
| 65 | Fan Speed | UINT8 | 1 | 0=away, 1=low, 2=medium, 3=high |
| 66 | Bypass Activation State | UINT8 | 1 | 0=auto, 1=activated, 2=deactivated |
| 67 | Temperature Profile Mode | UINT8 | 1 | 0=normal, 1=cool, 2=warm |
| 70 | Supply Fan Mode | UINT8 | 1 | 0=balanced, 1=supply only |
| 71 | Exhaust Fan Mode | UINT8 | 1 | 0=balanced, 1=exhaust only |
| 81 | Fan Speed Next Change | **UINT32** (0xFFFFFFFF = none) | 1 | s |
| 82 | Bypass Next Change | **UINT32** (0xFFFFFFFF) | 1 | s |
| 86 | Supply Fan Next Change | **UINT32** (0xFFFFFFFF) | 1 | s |
| 87 | Exhaust Fan Next Change | **UINT32** (0xFFFFFFFF) | 1 | s |
| 117 | Exhaust Fan Duty | UINT8 | 1 | % |
| 118 | Supply Fan Duty | UINT8 | 1 | % |
| 119 | **Exhaust** Fan Flow | UINT16 | 1 | m³/h — *computed, not measured* |
| 120 | **Supply** Fan Flow | UINT16 | 1 | m³/h — *computed, not measured* |
| 121 | Exhaust Fan Speed | UINT16 | 1 | rpm |
| 122 | Supply Fan Speed | UINT16 | 1 | rpm |
| 128 | Power Usage (**fans**) | UINT16 | 1 | W — ceiling 180/250/350 W by model |
| 129 | Power Usage (**fans**, year) | UINT16 | 1 | kWh — resets at the calendar year |
| 130 | Power Usage (**fans**, total) | UINT16 | 1 | kWh — since commissioning |
| 144 | Pre-heater Energy (year) | UINT16 | 1 | kWh |
| 145 | Pre-heater Energy (total) | UINT16 | 1 | kWh |
| 146 | Pre-heater Power | UINT16 | 1 | W |
| 192 | Days remaining to replace filter | UINT16 | 1 | days — "DAYS LEFT - INDICATIVE" |
| 208 | Device Temperature Unit | UINT8 | 1 | 0=celsius, 1=fahrenheit |
| 209 | Running Mean Outdoor Temperature | INT16 | 10 | °C |
| 210 | Heating Season active | BOOL (1 B) | 1 | 0/1 |
| 211 | Cooling Season active | BOOL (1 B) | 1 | 0/1 |
| 212 | Target Temperature (profile) | **INT16** | 10 | °C |
| 213 | Avoided Heating (actual) | UINT16 | 1 | W |
| 214 | Avoided Heating (year) | UINT16 | 1 | kWh |
| 215 | Avoided Heating (total) | UINT16 | 1 | kWh |
| 216 | Avoided Cooling (actual) | UINT16 | 1 | W |
| 217 | Avoided Cooling (year) | UINT16 | 1 | kWh |
| 218 | Avoided Cooling (total) | UINT16 | 1 | kWh |
| 220 | **UNVERIFIED** — controller-side intake temperature | INT16 | 10 | °C |
| 221 | Supply Air Temperature (unit; after the post-heater when one is fitted) | INT16 | 10 | °C |
| 224 | Device Airflow Unit | UINT8 | 1 | 1=kg/h, 2=l/s, 3=m³/h |
| 225 | Sensor-based ventilation mode | UINT8 | 1 | 0=disabled, 1=active, 2=overruling — unconfirmed; follows the fan level on a Q350, published raw |
| 226 | Fan Speed (modulated) | UINT16 | 1 | 0/100/200/300 with the four presets; 0–300 continuous while PDO 225=2 |
| 227 | Bypass State | UINT8 | 1 | % (100 = fully open) |
| 274 | Extract Air Temperature (ETA) | INT16 | 10 | °C |
| 275 | Exhaust Air Temperature (EHA) | INT16 | 10 | °C |
| 276 | Outdoor Air Temperature (ODA) | INT16 | 10 | °C |
| 277 | Outdoor Air Temp after pre-heater (P-ODA) | INT16 | 10 | °C |
| 278 | Supply Air Temperature (SUP sensor) | INT16 | 10 | °C |
| 290 | Extract Air Humidity | UINT8 | 1 | % |
| 291 | Exhaust Air Humidity | UINT8 | 1 | % |
| 292 | Outdoor Air Humidity | UINT8 | 1 | % |
| 293 | Outdoor Air Humidity after pre-heater | UINT8 | 1 | % |
| 294 | Supply Air Humidity | UINT8 | 1 | % |
| 338 | Bypass Override | **UINT32** | 1 | running-timer bitset: bit 1 = timer 1, the override (0=auto, 2=overridden) |
| 369–372 | Analog Input 1–4 — **Option Box only** | UINT8 | 10 | V |
| 402 | **UNVERIFIED** — post-heater present | BOOL (1 B) | 1 | 0/1 |

### Row notes from the hardware reference

Behaviour the Zehnder manuals document for rows above, via
[`comfoair-q-hardware.md`](./comfoair-q-hardware.md). None of it changes a width, a divider or a
name; it changes how the value should be read.

- **PDO 119 / 120 are computed, not measured.** There is no flow element anywhere in the service
  parts list. Flow is derived from the fan characteristic plus the two static-pressure sensors
  (S2/S22) and corrected for site air density by the installer's `ALTITUDE` setting, which exists
  precisely so the unit knows "the average environmental pressure needed for its airflow
  calculations" (hardware §10; Service p. 22). Commissioning balances the system with an
  **external** flow meter at each valve, not with these readings (Service p. 23). The display
  corroborates the grouping: it reports FAN SPEED / FAN DUTY / FLOW together, per fan (User p. 20).
  Two practical consequences: accuracy depends on the unit having been commissioned and on
  `ALTITUDE` being set correctly; and `UNBALANCE` (default 0 %, Service p. 25) deliberately makes
  supply ≠ extract, so **119 ≠ 120 is not by itself a fault**.
- **PDO 128 / 129 / 130 are fan power and fan energy, not whole-unit.** `STATUS > ENERGY > POWER
  CONSUMPTION` defines them (User p. 20): `VENTILATION` = "the current electrical consumption of
  **the fans**" → 128; `YEAR TO DATE` = "energy consumption **of the fans** since the beginning of
  the year" → 129; `TOTAL` = "since the unit was commissioned" → 130. `PREHEATER` is a separate
  item in the same menu → PDO 146. So PDO 128's ceiling is the technical table's "max power
  **excl.** pre-heater" column — **180 / 250 / 350 W** by model — not the 1850/2240/2620 W
  including-pre-heater figure (hardware §3; Installer p. 6 = Service p. 6). Reset semantics:
  **129 resets at the calendar year, 130 at commissioning.**
- **PDO 144 / 145 / 146 order.** 144 = pre-heater
  energy **year**, 145 = pre-heater energy **total**, 146 = pre-heater **power** — i.e. the reverse
  of the 128/129/130 ordering, which is easy to get backwards. 146 is user-visible twice: as
  `STATUS > FROST PREHEATER` = "the current power consumption of the preheater" (User p. 19) and as
  `ENERGY > POWER CONSUMPTION > PREHEATER` (User p. 20). See also §4 capability #5.
- **PDO 192 counter semantics** (hardware §15; Service p. 24, User pp. 28–29): maximum interval
  **180 days**; factory pre-warning **21 days**, adjustable **upward only**; the counter **shortens
  automatically** when the unit runs above nominal airflow, so it is not a plain calendar
  countdown — the unit itself labels the figure "DAYS LEFT - INDICATIVE" (User p. 28). Warning
  chain: `EXPECT FILTER CHANGE SOON` (1079, warning) → `CHANGE FILTERS NOW` (1077, error);
  `SUPPRESS` postpones by exactly one day.
- **PDO 209 / 210 / 211 — definition and the limits' direction** (hardware §13.4). RMOT = "running
  mean outdoor temperature (average temperature over past five days)" (User p. 20 fn. 5). Defaults:
  `HEATING LIMIT RMOT` **11 °C**, `COOLING LIMIT RMOT` **20 °C** (User p. 24). The direction is
  what makes 210/211 interpretable: LIMIT RMOT HEAT is "the set RMOT **below** which the (central)
  heating system is normally active"; LIMIT RMOT COOL is "the set RMOT **above** which the
  (central) cooling system is normally active" (User p. 19). Purpose: "to prevent the unit from
  counteracting with external heating … and cooling … units in the dwelling".
- **PDO 225 — the UI it corresponds to is tri-state, but do not equate the values.** Each of
  `TEMPERATURE PASSIVE`, `HUMIDITY COMFORT`, `HUMIDITY PROTECTION` takes **OFF / AUTO ONLY / ON**
  (hardware §13.5, User p. 23) — the same three-valued shape as the `03 1D 01 04/06/07 <0|1|2>`
  writes, ON responding in both AUTO and MANUAL, AUTO ONLY deactivating itself in manual mode.
  Triggers: HUMIDITY PROTECTION ventilates on PRESET 3 when extract-air **RH > 75 %**; HUMIDITY
  COMFORT raises airflow when indoor absolute humidity exceeds the reference; TEMPERATURE PASSIVE
  needs cooling season, indoor ≫ outdoor, bypass in AUTO, ComfoCool off, both fans running, ETA+SUP
  sensors healthy and the unit not switched off in the last 2 min. Defaults: TEMPERATURE PASSIVE
  **OFF**, HUMIDITY COMFORT **OFF**, HUMIDITY PROTECTION **AUTO ONLY**. **Do not map PDO 225's
  `0=disabled / 1=active / 2=overruling` onto OFF / AUTO ONLY / ON**: 225 is a single value while
  the UI has three independent controls, so 225 is more likely a runtime *state* than the setting.
  Those three labels are **unconfirmed**: PDO 225 is known only as a comfort-control mode with
  **no authoritative value map at all**, and the manuals do not support the words. See §5.
- **PDO 227 is a percentage by construction.** `STATUS > BYPASS STATE` — "The percentage of
  bypassed air is shown" (User p. 19) — and the hardware is an in-line **modulating** bypass, valve
  (#17) and actuator (#10) shipped as separate service parts (Service p. 11). This rebuts any
  boolean treatment of the bypass.
- **PDO 369–372 exist only with an Option Box.** `STATUS > ANALOG INPUT` is footnoted "only visible
  when the ancillary is connected", and its items are named `0-10V INPUT 1..4` (User p. 19). There
  are no 0-10 V terminals on the main board at all (hardware §11; Service p. 12 = Installer p. 10).
  Entity naming should follow Zehnder: **`0-10V Input n`**, not "Analog Input n".

### Widths and dividers that are easy to get wrong

- **PDO 212** — a `UINT8` width is sometimes given for it, but the value is **INT16 ÷10**
  (`238 → 23.8 °C`). Trust INT16/10.
  The argument "a UINT8 would cap at 25.5 °C" does **not** close on its own, because
  the documented FIXED maximum is 24 °C = raw 240, which fits in a UINT8. What closes it is
  ADAPTIVE mode (hardware §13.3, User p. 24): the NORMAL setpoint is **21.5 °C @ 0 °C RMOT** and
  rises **+0.11 °C per °C of RMOT**, with WARM = NORMAL + 1.5 °C and COOL = NORMAL − 1.5 °C. WARM
  therefore exceeds 25.5 °C once RMOT > ≈ 22.7 °C — a reachable European five-day mean — so
  UINT8÷10 would overflow in **normal operation**, not merely at some contrived extreme. The same
  passage independently proves the ÷10 divider: 21.5 °C and 0.11 °C steps are not representable at
  1 °C resolution. The setpoints PDO 212 reports:

  | Profile | FIXED default | ADAPTIVE |
  | --- | --- | --- |
  | WARM | 24 °C | NORMAL + 1.5 °C |
  | NORMAL | 20 °C | 21.5 °C @ 0 °C RMOT, +0.11 °C per °C RMOT |
  | COOL | 18 °C | NORMAL − 1.5 °C |

- **PDO 213 / 216** — an annotation `441 → 4.41 W` (implying ÷100) is wrong: the value carries
  no divider and its unit is W. Raw watts is correct; the hardware makes this decisive rather than
  a judgement call. The unit's own definition of these
  figures is "the amount of energy which has been saved because an external heating / cooling
  source did not need to be switched on" (User p. 20 fn. 6/7) — i.e. the *thermal* power the
  exchanger recovers, not an electrical figure. From maximal airflow 350/450/600 m³/h and thermal
  efficiency 92/90/89 % (hardware §3; Installer p. 6 = Service p. 6), recovered thermal power at
  the manuals' own −15 °C rating condition (ΔT ≈ 36 K) is ≈ **3.9 / 4.9 / 6.4 kW**; even at a mild
  ΔT of 20 K it is ≈ 2.2–3.6 kW. Under ÷100 those are raw counts of **215 000 – 644 000, which
  overflow the UINT16 field** outright. And a ÷100 reading would cap avoided heating at
  **655.35 W** — under twice the *electrical* draw of a Q600's own fans (350 W ceiling; 287 W
  measured at 600 m³/h / 200 Pa, Brochure p. 15) — which is absurd for an 89–92 % efficient
  exchanger: it would mean the recovery never much exceeds the cost of running the fans.

### Accessory and schedule PDOs (from aiocomfoconnect)

None of these is first-hand. Source: aiocomfoconnect (MIT), commit `a5957743`, `sensors.py` for the
type and `docs/PROTOCOL-PDO.md` for the meaning. **†** = that source saw the meaning only in the
official ComfoControl app, never on a unit. Of this table the firmware publishes three rows: 219
(`frost_protection_preheater_power`, named "(unverified)" and created disabled), 228
(`frost_protection_unbalance`) and 337 (`boost_timer_running`, `away_timer_running` and the state
of the `away_mode` switch). Every other row belongs to an accessory, the Option Box or the raw
schedule words, outside the firmware's ComfoAir Q350/450/600 scope, and stays here so a census can
be decoded. The ComfoClime ids (41xx) are that source's ComfoClime HTTP telemetry ids; that the
ComfoClime sends them on CAN is unproven. HV-CAN-16 records which of them a base unit still sends
on its own.

| PDO | Type | Meaning | Values |
| --- | --- | --- | --- |
| 33 | UINT8 | Preset: active schedule entry † | idle 1 |
| 37 | UINT8 | ComfoCool off: active schedule entry † | idle 0 |
| 53 | UINT8 | ComfoCool off: active timer | 255 none, 1 timer |
| 85 | UINT32 | ComfoCool off: remaining | seconds; all-ones = no timer † |
| 176 | UINT8 | RF pairing mode | 0 not running, 1 running, 2 done, 3 failed, 4 aborted |
| 219 | UINT16 | Frost protection: pre-heater power † | W |
| 228 | UINT8 | Frost protection unbalance | % of supply-air reduction (also §4 below) |
| 321 | UINT16 | Preset: enabled schedule entries, bitset † | idle 3 |
| 325 | UINT16 | ComfoCool off: enabled schedule entries, bitset † | idle 1; 0 and 3 seen |
| 337 | UINT32 | Preset: running timers, bitset | bit 1 timer 1, bit 5 timer 5, **bit 6 boost**, **bit 11 away**; bits add (34, 2052) |
| 341 | UINT32 | ComfoCool off: running timers, bitset | 0 none, 2 timer 1 |
| 342 | UINT32 | Supply fan mode (exhaust fan off): running timers | 0 balanced, 2 supply only |
| 343 | UINT32 | Exhaust fan mode (supply fan off): running timers | 0 balanced, 2 exhaust only |
| 384 | INT16 | ComfoHood temperature † | ÷10 °C † |
| 386 | BOOL | ComfoHood present † | 0/1 |
| 400 | INT16 | Post-heater temperature | ÷10 °C; equal to 221 in every sample |
| 401 | UINT8 | Post-heater state | % 0–100 |
| 416 | INT16 | ComfoFond outdoor air temperature | ÷10 °C |
| 417 | INT16 | ComfoFond ground temperature | ÷10 °C |
| 418 | UINT8 | ComfoFond GHE state | % |
| 419 | BOOL | ComfoFond GHE present | 0/1 |
| 784 | UINT8 | ComfoCool compressor state | 0 off, 1 on |
| 785 | UINT8 | ComfoCool flow request † | % † (sensors.py says BOOL; the DLC decides) |
| 802 | INT16 | ComfoCool condenser temperature (T13) | ÷10 °C † |
| 1024–1031 | UINT16 | CO2 sensor 1..8 (product id 0x0A), zone 1, in node-id order | ppm; 1024/1025 seen on a unit |
| 4145 | INT16 | ComfoClime TPMA temperature | ÷10 °C |
| 4148 | INT16 | ComfoClime target temperature | ÷10 °C |
| 4149 | UINT8 | ComfoClime device mode | 0 off, 1 heating, 2 cooling |
| 4151 | INT16 | ComfoClime current comfort temperature | ÷10 °C |
| 4154 | INT16 | ComfoClime indoor temperature | ÷10 °C |
| 4193–4197 | INT16 | ComfoClime supply air, exhaust air, supply coil, exhaust coil, compressor temperature | ÷10 °C |
| 4198 | UINT8 | ComfoClime heat-pump duty | % |
| 4201 | UINT16 | ComfoClime heat-pump power | W |
| 4202–4208 | UINT16 | unknown | — |

The PDO number occupies bits 14..26 of the CAN id (13 bits, 0..8191; `pdo_decode.h`), so PDOs
1024 and up are ordinary PDO broadcasts on the bus.

## 2. Resolved ambiguities in PDO identity

The PDO block structure settles most of these. PDO ids map to RMI units as
`unit = (pdo >> 4) + 0x10`, corroborated across the whole table: 81/82/85/86/87 → SCHEDULE
(0x15) subunits 1/2/5/6/7; 128–130 → POWERSENSOR (0x18); 144–146 → PREHEATER (0x19);
192 → FILTER (0x1C); 208–221 → TEMPHUMCONTROL (0x1D); 224–230 → VENTILATIONCONFIG (0x1E);
273–278 → TEMPERATURESENSOR (0x21); 289–294 → HUMIDITYSENSOR (0x22).

The block names line up with the unit's own menu and component structure, which is independent
evidence that they are not arbitrary labels: SCHEDULE 0x15 ↔ `SCHEDULER`; POWERSENSOR 0x18 ↔
`STATUS > ENERGY`; PREHEATER 0x19 ↔ the pre-heater accessory, which has its own status item and its
own error family; FILTER 0x1C ↔ `FILTER SETTINGS`; TEMPHUMCONTROL 0x1D ↔ the `ADVANCED SETTINGS`
group that holds TEMPERATURE PROFILE, SEASON DETECTION and SENSOR VENTILATION together;
VENTILATIONCONFIG 0x1E ↔ `MAIN BOARD SETTINGS > VENTILATION PRESETS / VENTILATION CONTROL /
ALTITUDE / UNBALANCE`; TEMPERATURESENSOR 0x21 and HUMIDITYSENSOR 0x22 ↔ `STATUS > TEMP. AND
HUMIDITY` (hardware §13, §18).

The temperature and humidity blocks are index-aligned 1:1, and the humidity names are
documented without hedging, so they transfer identity to the temperature block:

| index | temperature | humidity |
| --- | --- | --- |
| 2 | 274 | 290 Extract |
| 3 | 275 | 291 Exhaust |
| 4 | 276 | 292 Outdoor |
| 5 | 277 | 293 Pre-heated Outdoor |
| 6 | 278 | 294 Supply |

| Question | Verdict |
| --- | --- |
| **PDO 221** | Supply air *after* the post-heater — it is defined as the supply temperature and glossed "Supply Air (PostHeated)". A "post-heater temperature *before*" label is wrong. Shipped as "Supply Air Temperature (controller)"; on a unit without a post-heater it equals 278. |
| **PDO 278** | The unit's internal SUP sensor (TEMPERATURESENSOR subunit 6), *before* any post-heater. The post-heater is a separate RMI unit (0x27). Labelling 221 and 278 the other way round inverts the pair. |
| **PDO 277** | Outdoor air after the pre-heater. An "after ComfoFond/GHE" reading is misleading: the ComfoFond has its own PDOs (416 outdoor, 417 ground). Corroborated device-side — the ComfoFond-L Q has its own display group with its own OUTDOOR AIR TEMP. and an *estimated* GROUND TEMPERATURE (User p. 20), so the sub-soil exchanger reports separately and does not feed 277. |
| **PDO 220** | **Not confirmed.** Its name is marked unknown, with no unit; a *Preheated* guess for it is the reverse of a "pre-heater temperature *before*" label, and neither is supported. What is solid: 220 is in the TEMPHUMCONTROL block, not the sensor block, so it is a controller-side intake temperature paired with 221. In a recorded capture 220 = 7.5 °C while 276 = 277 = 6.0 °C, which contradicts "before the pre-heater". **Do not label it "before the pre-heater".** Use a neutral label and mark it unverified until 220/276/277 can be logged together on a unit with an active pre-heater or ComfoFond. The hardware narrows the space further without closing it — see below. |
| **PDO 119 / 120** | 119 = exhaust, 120 = supply. Reinforced by the FAN block ordering, where exhaust always precedes supply (117/118 duty, 121/122 rpm) — undisputed, and 119/120 sit between them. |
| **32-bit counters** | None of 129, 130, 214, 215, 217, 218 is 32-bit — all are UINT16, so a UINT16 decode truncates nothing. The genuine UINT32 PDOs are the UINT32 rows of §1 (the countdowns, the running-timer bitsets and PDO 338). A decoder that takes the width only from the declared type and ignores the CAN DLC would not survive a field being widened; one that dispatches on the DLC would. |

### PDO 220 — what the hardware adds (verdict unchanged: UNVERIFIED)

The manuals cannot name 220, but they can enumerate everything it is *not*. The unit has exactly
**four temperature measuring points and two pressure measuring points**, listed in the wiring-diagram
legend (hardware §10; Installer p. 10 = Service p. 12): S1/S11 = outdoor / extract, S3/S33 =
supply / exhaust, S2/S22 = the two static-pressure sensors — packaged as just two replaceable
modules, the top-section sensor (#2) and the mid-section sensor (#5). The display exposes exactly
four temperature/humidity pairs: `STATUS > TEMP. AND HUMIDITY` lists EXTRACT / EXHAUST / OUTDOOR /
SUPPLY (User p. 19). There is no pre-heated pair and no controller-side reading anywhere in the menu.

| Physical point | Temp PDO | Hum PDO | On the display? |
| --- | --- | --- | --- |
| Sensor outdoor air (ODA), S1/S11 | 276 | 292 | yes |
| Sensor extract air (ETA), S11/S1 | 274 | 290 | yes |
| Sensor supply air (SUP), S3/S33 | 278 | 294 | yes |
| Sensor exhaust air (EHA), S33/S3 | 275 | 291 | yes |
| after the pre-heater — no physical sensor | 277 | 293 | no |
| after an external post-heater (accessory, RMI 0x27) | 221 | — | no |
| **220** | 220 | — | **no** |
| supply / exhaust pressure, S2/S22 | — | — | no |

(The S-code pairs are written *right/left* because which physical side is which flips with the
`ORIENTATION` setting — hardware §10, Service p. 12.)

**Verdict:** all four physical measuring points are already accounted for by 274/275/276/278, and
there is no fifth display item for 220 to be, so 220 is necessarily **computed or controller-side**.
The hardware corroborates the existing reasoning — 220 sits in TEMPHUMCONTROL, paired with 221 — but
it cannot supply the identity. **Keep it UNVERIFIED and keep the neutral label.**

One honest caveat against over-reading the enumeration: the pre-heater is a *communicating* module
with its own connector `J400` and its own comms fault code `PREHEAT_PRES ERROR` (1068), so a sensing
element inside the pre-heater accessory is not strictly excluded by the parts list. Nothing in the
four PDFs names one — and such a sensor would explain **277**, not 220.

### PDO 16 and PDO 67 values that are easy to mislabel

An `away` flag derived from PDO 16 must test `== 7`, not `== 0x08`: 8 is DFC — **away is 7**.
That 7 is the device state "Away", and it follows the fan's Away *level* too: on a Q350, setting
the level Away → Low moved PDO 16 from 7 to 1 while PDO 337 bit 11 stayed clear and PDO 81 counted
nothing down. Whether away *mode* (timer 11) is running is PDO 337 bit 11, not PDO 16.
PDO 67 value 0 is `normal`, not `"auto"`.

## 3. RMI "set with timer" frame format

The SCHEDULE commands are documented by name, but their byte layout is not spelled out anywhere.
The layout below is reconstructed from known-good frames plus captured `0x83` GETTIMERENTRY
replies, and is consistent with every known example.

```
enable : 84 <unit=15> <subunit> <property> 00 00 00 00 <timeout u32 LE signed> <value u8>   (13 bytes)
disable: 85 <unit=15> <subunit> <property>                                                  (4 bytes)
read   : 83 <unit=15> <subunit> <property>                                                  (4 bytes)
reply  : <enabled u8> <u32 = 0> <timeout u32 LE> <remaining u32 LE> <value u8>              (14 bytes)
```

- `0x84` = ENABLETIMERENTRY, `0x85` = DISABLETIMERENTRY, `0x83` = GETTIMERENTRY.
- `0x15` is the **unit** SCHEDULE, not a node id. The destination node is encoded in the CAN id.
- The **four zero bytes** are a u32 that reads back as `00000000` in every capture. Its meaning
  is undocumented; it precedes the timeout in the GET reply, so it is not the timeout. Always
  send zeros.
- The **u32 little-endian duration is signed**: `ffffffff` = −1 = indefinite. `100e0000` = 3600 s.
  The GET reply proves the semantics — boost active reads
  `01 00000000 58020000 56020000 03`: configured 600 s, 598 s remaining.
- **Trailing padding is not part of the protocol.** An enable frame is exactly 13 bytes. Frames
  with three extra trailing `00` bytes are accepted, and so are the same frames without them —
  proving the padding is not required. Harmless, but do not emit it.

### Command table

| Action | subunit | property | timeout | value | Frame |
| --- | --- | --- | --- | --- | --- |
| Level away | 01 | 01 | `01000000` | `00` | `84 15 01 01 00000000 01000000 00` |
| Level low | 01 | 01 | `01000000` | `01` | `84 15 01 01 00000000 01000000 01` |
| Level medium | 01 | 01 | `01000000` | `02` | `84 15 01 01 00000000 01000000 02` |
| Level high | 01 | 01 | `01000000` | `03` | `84 15 01 01 00000000 01000000 03` |
| Boost on | 01 | 06 | seconds | `03` | `84 15 01 06 00000000 <secs> 03` |
| Boost off | 01 | 06 | — | — | `85 15 01 06` |
| Away on | 01 | 0B | seconds | `00` | `84 15 01 0B 00000000 <secs> 00` |
| Away off | 01 | 0B | — | — | `85 15 01 0B` |
| Manual | 08 | 01 | `01000000` | `01` | `84 15 08 01 00000000 01000000 01` |
| Auto | 08 | 01 | — | — | `85 15 08 01` |
| Bypass activate | 02 | 01 | seconds | `01` | `84 15 02 01 00000000 <secs> 01` |
| Bypass deactivate | 02 | 01 | seconds | `02` | `84 15 02 01 00000000 <secs> 02` |
| Bypass auto | 02 | 01 | — | — | `85 15 02 01` |
| Supply only | 06 | 01 | seconds / `ffffffff` | `01` | `84 15 06 01 …` **plus** `85 15 07 01` |
| Exhaust only | 07 | 01 | seconds / `ffffffff` | `01` | `84 15 07 01 …` **plus** `85 15 06 01` |
| Balanced | 06 & 07 | 01 | — | — | `85 15 06 01` **and** `85 15 07 01` |
| Profile normal | 03 | 01 | `ffffffff` | `00` | `84 15 03 01 00000000 ffffffff 00` |
| Profile cool | 03 | 01 | `ffffffff` | `01` | `84 15 03 01 00000000 ffffffff 01` |
| Profile warm | 03 | 01 | `ffffffff` | `02` | `84 15 03 01 00000000 ffffffff 02` |

Notes:

- The `01000000` (1 second) timeout on level and manual-mode changes is what the official app
  sends. No known source explains why a 1 s timer makes the change stick. Keep it; do not
  substitute `ffffffff`.
- **What the 1 s level command actually buys you: "limited manual".** The hardware documents the
  behaviour the unit falls back to. A temporary airflow change made from the main screen reverts to
  AUTO "when the next step of the SCHEDULER VENTILATION starts or after a maximum of two hours"
  (hardware §13.1, User p. 12). That is exactly what PDO 49's own value names describe:
  - `84 15 01 01 00000000 01000000 <n>` → PDO 49 = **1, limited manual** — bounded by the scheduler
    step or the two-hour ceiling, whichever comes first;
  - `84 15 08 01 00000000 01000000 01` → PDO 49 = **5, unlimited manual** — permanent, and the exact
    counterpart of `TASK MENU > AUTO/MANUAL > MANUAL`, whose on-screen confirmation reads "DEMAND
    CONTROL VENTILATION WILL BE IGNORED OR PARTIALLY IGNORED IN MANUAL MODE" (User p. 12) and of
    which User p. 16 says "When MANUAL mode is activated all set schedules will be ignored."

  **Firmware consequence:** a level command *alone* reverts within ≤ 2 h (sooner if a schedule step
  begins). To make a preset stick, the firmware must **also** send the subunit-08 manual command.
  **Marked as an inference, not a documented fact:** the manual describes the display's up/down
  buttons. That the same two-hour ceiling governs a level set over RMI is a strong inference from
  PDO 49 reporting "limited manual" for that write — it is not stated anywhere. Together with the
  still-unexplained *why* of the 1 s timer, this is recorded in §5.
- **"Exhaust only" is conditional on an installer setting.** The user manual prints "EXTRACT ONLY
  if you would like to stop supplying air **(if available)**" (User p. 13), and the condition is
  `FIRE PLACE PRESENT` — with the factory default `NO`, "the unit will allow the function EXTRACT
  ONLY" (Service p. 24 §4.8.5; hardware §13.7). The manual prints only the `NO` row, so the `YES`
  behaviour is *implied*, not documented. The write can therefore be silently refused: firmware must
  confirm the state via a **PDO 71 / PDO 87 read-back** rather than assume the frame took effect.
- **"Away on" takes an end time, not a duration.** On the unit AWAY is entered as
  `AWAY UNTIL YYYY/MM/DD HH:MM`, and "during this set time all schedulers will be ignored"
  (hardware §13.1, User p. 14). So `<secs>` must be computed as *now → a chosen end time*, and the
  result depends on the unit's own real-time clock being correct — that clock is backed by a BR1225
  button cell whose failure mode is "clock not saved after a power cut" (Service p. 45).
- **Supply-only / exhaust-only value byte is `01` for both subunits**, not `00` for either: the
  GETTIMERENTRY read-back reports `01`. Use `01`.
- Subunit 01 / property 05 = MANUALMODE is documented as a second route to manual mode. It is not
  the route known-good frames use, and this firmware does not send it. Prefer subunit 08.

### Complete SCHEDULE (0x15) subunit and property map

The command table above lists only what this firmware sends. The whole documented address space of
the timer engine is below, so that an unlisted subunit is
recognised as *known and out of scope* rather than as an unknown.

| SubUnit | Property | Description | Used here |
| --- | --- | --- | --- |
| 0x01 | 0x01 | Preset (`00` away / `01` low / `02` medium / `03` high) | yes |
| 0x01 | 0x02 | PRESETRF | no |
| 0x01 | 0x03 | PRESETANALOG | no — Option Box |
| 0x01 | 0x04 | PRESETRFANALOG | no |
| 0x01 | 0x05 | MANUALMODE | no — subunit 08 is preferred (note above) |
| 0x01 | 0x06 | BOOST | yes |
| 0x01 | 0x07 | BOOSTRF | no |
| 0x01 | 0x08 | BOOSTSWITCH | no — bathroom switch, Option Box |
| 0x01 | 0x0b | AWAY | yes |
| 0x02 | 0x01 | Bypass control (auto / on / off) | yes |
| 0x03 | 0x01 | Temperature profile (warm / normal / cool) | yes |
| 0x05 | 0x01 | ComfoCool control | no — no ComfoCool in scope |
| 0x06 | 0x01 | Supply fan control | yes |
| 0x07 | 0x01 | Exhaust fan control | yes |
| 0x08 | 0x01 | Ventilation mode (auto / manual) | yes |

Subunit `0x04` is not documented, although the unit is declared as having ten subunits.

### There is no "profile back to auto"

Every other schedule aspect has a `0x85` release path, and this firmware exposes it: bypass Auto is
`85 15 02 01`, balanced is `85 15 06 01` + `85 15 07 01`, auto mode is `85 15 08 01`. The
temperature profile does not. Known-good profile writes always use an **indefinite** timeout
(`-1`) and never issue `85 15 03 01`, and this firmware does the same (`rmi_frame.h`,
`temperature_profile()` hardcodes `ffffffff`).

The consequence is real and otherwise undocumented: **a profile set from Home Assistant or the web
UI permanently overrides any profile the unit's own scheduler would apply**, with no way back short
of the unit's display. By analogy with every other subunit, `85 15 03 01` should be the release, and
`0x85` on a subunit that has no active timer is harmless elsewhere — but **it has never been
observed being sent**, so it is a hypothesis, not a documented command. Verify on hardware before adding an
`Auto` option to the Temperature Profile select.

### Away duration: the wire format is a plain duration

The note above ("Away on takes an end time, not a duration") describes the **unit's own UI**, which
asks for `AWAY UNTIL YYYY/MM/DD HH:MM`, and its conclusion — that `<secs>` is computed as
*now → a chosen end time* — stands. The **wire** format, however, is a plain duration in seconds
in the same field as every other timer (an away of one hour is `timeout = 3600`), and the
GETTIMERENTRY read-back `0100000000550200005302000000` decodes as 597 s configured / 595 s
remaining. So the RTC caveat applies to *choosing* the number, not to encoding it.

### RMI read commands and the response frame

Everything above writes. Three generic commands are documented to exist on every unit. The
firmware reads with only `get one` (`0x01`, with the types `0x10`, `0x20` and `0x40` below) and the
active-error read (`80 03 01`), both from the probe task and nowhere else
([probe](/protocol/probe), [PDO and RMI](/protocol/pdo-rmi)):

- **a walk** the owner starts with `POST /api/probe` (findings in
  [`rmi-probe-findings-q350.md`](rmi-probe-findings-q350.md));
- **the unit read**, unprompted, in one cycle: the unit identity, six `get one` of type `0x10` on NODE `0x01`/subunit 1,
  properties `0x04`, `0x06`, `0x08`, `0x0B`, `0x0D`, `0x14`, then `80 03 01`, and the three
  sensor-ventilation selects, `01 1D 01 10 04`, `06` and `07` (TEMPHUMCONTROL subunit 1) — 10 s
  after the probe task starts at boot, every 6 h after a complete read, every 60 s after an
  incomplete one (no model, a select without a final answer, or no firmware word yet)
  (`components/comfoair_probe_task/comfoair_probe_task_unit.cpp`). The identity fills the `unit`
  block of `GET /api/system`; the three values feed the `temperature_passive`, `humidity_comfort`
  and `humidity_protection` selects (declared in `tools/pdo_table.py` as `SelectSpec.rmi`, generated
  into `COMFOAIR_RMI_READBACK`; see [probe](/protocol/probe#the-unit-read));
- **the active errors alone**, `80 03 01`, every 60 s between two full reads
  (`components/comfoair_unit_cadence`). Every answered error read also feeds the `unit_error` and
  `active_errors` entities, once the firmware word is known.

`get many` is never sent. `set one` is sent, but only as a command, never as a read: it is the
`03 1D 01 04/06/07 <0|1|2>` sensor-ventilation write (`set_property()` in `rmi_frame.h`). The only
entities fed from an RMI read are those three selects and the two error entities.

```
get one   : 01 <unit> <subunit> <type> <property>
get many  : 02 <unit> <subunit> 01 <type|count> <prop1> … <propN>
set one   : 03 <unit> <subunit> <property> <value…>
```

The `<type>` byte selects **what** is returned and the values are OR-able:

| Type | Returns |
| --- | --- |
| `0x00` | None |
| `0x10` | Actual value |
| `0x20` | **Range** — the permitted minimum and maximum |
| `0x40` | Step size |

For `0x02` the property **count** is OR'ed into the same byte: five actual values is
`0x10 | 0x05` = `0x15`, as in the documented `02 01 01 01 15 03 04 06 05 14`.

`0x20` matters beyond curiosity: it asks the unit itself for a writable property's bounds. `§5`
lists "adjustment ranges for the FIXED setpoints and for `UNBALANCE`" as an open question that the
manuals do not answer — `01 1E 01 20 12` and `01 1D 01 20 0B` answer it directly on the bus.

Writes are range-checked by the unit: a value outside the `0x01`/`0x20` range is refused with error
**30**. Step size is *not* checked.

**Error codes**, returned when the response's `ErrorOccured` bit is set:

| Code | Meaning |
| --- | --- |
| 11 | Unknown Command |
| 12 | Unknown Unit |
| 13 | Unknown SubUnit |
| 14 | Unknown property |
| 15 | Type can not have a range |
| 30 | Value given not in Range |
| 32 | Property not gettable or settable |
| 40 | Internal error |
| 41 | Internal error, maybe your command is wrong |

The probe sees these codes: it waits for each reply, reassembles it and reports an error answer
with its code. The command path does not: every write in the command table above is
fire-and-forget, so a refused write is indistinguishable from an accepted one, and only the PDOs
that follow it tell.

**Response framing.** A response is an RMI CAN id with the `IsRequest` bit
clear. Reassembly is the exact mirror of this firmware's own `split_into_can_frames()`:

- **≤ 8 bytes** — `MultiMsg` clear, CAN payload *is* the message.
  `01 1D 01 10 0A` (get TEMPHUMCONTROL warm setpoint) → `E6 00` = `0x00E6` = 230 = 23.0 °C.
- **> 8 bytes** — `MultiMsg` set, message split into 7-byte blocks, each prefixed by its index from
  0, with `0x80` OR'ed into the prefix of the **last** block.

The documented worked example is the active-error read, and it is the reason capability #7 below is
implementable:

```
can1  1F011074   [3]  80 03 01                       # request
can1  1F005D01   [8]  00 00 00 00 00 00 00 00        # block 0
can1  1F005D01   [8]  01 00 00 00 00 00 00 00        # block 1
can1  1F005D01   [8]  02 00 00 00 00 00 00 00        # block 2
can1  1F005D01   [8]  03 00 00 00 00 00 00 00        # block 3
can1  1F005D01   [5]  84 00 00 00 00                 # block 4, last (0x80 | 4)
```

Reassembled: 32 zero bytes — no active errors.

**Warning:** the RMI CAN-id prefix check must require bits 19-23 to be clear as well as bits 24-28
to equal `0x1f`. Firmware uploads use `0x1F800000`, which passes a naive
bits-24-28 test and is not an RMI response.

## 4. Further documented capabilities

Ordered by value to a Home Assistant integration. All are RMI **property** operations, which —
unlike the schedule reads — need no response parsing when written.

| # | Capability | Access | Address | Notes |
| --- | --- | --- | --- | --- |
| 1 | **Airflow presets in m³/h** | read + write | `VENTILATIONCONFIG` 0x1E, subunit 01, properties 03=away, 04=low, 05=medium, 06=high, INT16 m³/h. Write: `03 1E 01 <prop> <int16 LE>` | Biggest gap. Lets HA redefine what low/medium/high physically mean. Defaults, bounds and the ordering rule below. |
| 2 | **Temperature setpoints per profile** | read + write | `TEMPHUMCONTROL` 0x1D subunit 01: 0x0A heating, 0x0B normal, 0x0C cooling, INT16 ×10 °C. Also 0x02/0x03 RMOT season thresholds, 0x08 profile mode (0=adaptive, 1=fixed, 2=ext. setpoint?) | Prerequisite for a `climate` entity with real target-temperature control. Documented defaults below. |
| 3 | **Node identity** | read | `NODE` 0x01/01: 0x04 serial, 0x06 firmware version, 0x08 model, 0x0B article number, 0x0D country code, 0x14 name. One multi-get `02 01 01 01 15 03 04 06 05 14` returns all | Good HA device-info attributes. Requires RMI response parsing. Matches what Zehnder itself surfaces — below. |
| 4 | **PDO 230 airflow constraints** | read | bitset; the bits name constraints such as FrostProtection, Bypass, NoiseGuard, … | Explains *why* the unit is not reaching its setpoint. PDO, so no RMI parsing needed. Zehnder's own strings and two bit-label caveats below. |
| 5 | **PDO 144/145/146 pre-heater power** | read | **144 = kWh year, 145 = kWh total, 146 = W** (the reverse of 128/129/130) | 146 is `STATUS > FROST PREHEATER` on the display. |
| 6 | **Filter lifetime setting** | read + write | `FILTER` 0x1C/01: 0x03 lifetime days (UINT16), 0x02 pre-warning period | **A filter *reset* command is not documented anywhere** — no unit-0x1C command ≥ 0x80 is known. Do not promise a reset button on the basis of these docs. Zehnder documents only the on-unit wizard — below. |
| 7 | Active-error list | read | `ERROR` 0x03: `80 03 01` GETACTIVEERRORS, `82 03 01` RESETALLERRORS | **The read response format is documented, not unknown** — see the note below. Both the reset and the read are actionable once RMI responses are parsed. Device-side code table: hardware §16. |
| 8 | Schedules | — | `0x80`/`0x81`/`0x82`/`0x86`/`0x87` are named but no payload format is documented | Not actionable. |
| 9 | **Unbalance** (`UNBALANCE` on the unit) | read + write* | `VENTILATIONCONFIG` 0x1E, subunit 01, property `0x12`, INT16 in tenths of a percent (`9dff` = −99 → **−9.9 %**). Write: `03 1E 01 12 <int16 LE>` | The hardware reference (§13.7, Service p. 25) documents the *behaviour* — positive = over-pressure by reducing supply, negative = under-pressure by reducing extract, positive blocked when `FIRE PLACE PRESENT` = YES — but the address was never recorded here. Permitted range is unknown from the manuals; ask the unit with `01 1E 01 20 12`. |
| 10 | Other `VENTILATIONCONFIG` installer settings | read + write* | 0x1E/01: `0x07` height above sea level (0=0–500, 1=500–1000, 2=1000–1500, 3=1500–2000 m), `0x09` ventilation control mode (0 = flow control, 1 = constant flow), `0x0b` bathroom-switch activation delay (INT16 s), `0x0c` deactivation delay (UINT8 min), `0x0d` bathroom-switch mode (0 = fixed, 1 = mirrored) | Commissioning values, not day-to-day controls. The bathroom-switch trio needs an Option Box to have any effect. Listed so that a future `climate`/commissioning iteration does not have to rediscover them. |

\* Write access is stated (`rw`) for `0x03`–`0x06` above (capability #1) but left unstated for
`0x07`, `0x09`, `0x0b`, `0x0c`, `0x0d` and `0x12`. Write access for rows 9 and 10 is presumed from
the general rule that "each writable property may have a range & step size", not stated directly.

**Capability #7 — why the response format is known.** The read response format is documented, not
something that needs reverse engineering, and that moves the active-error list from "blocked" to
"implementable".

- The complete `80 03 01` exchange has been captured (§3, response framing): a **32-byte** response
  arriving as five CAN frames prefixed `00 01 02 03 84`. It is the worked example of multi-frame
  reassembly — though the capture itself is all zeros, so it demonstrates framing and length, not
  payload semantics.
- The 32 bytes are a **bitfield**. The decode: walk each byte from its least significant bit; the
  index of every set bit **is** the error id.
- This is **almost certainly** the same field as the LAN C push path's
  `CnAlarmNotification.errors` (`max_size = 32`) — matching size, matching subject, and the LAN C
  is just a bus translator (§0). But it is an inference, not a first-hand fact: the bitfield decode
  is established only for the LAN C push path and has not been confirmed on an RMI `80 03 01`
  response carrying an active error. Treat it as one format inferred across two transports, not a
  confirmed identity.
- The code table splits at id **70**. A base set of **43** entries (ids 21–69, with six gaps) is
  shared; the table for firmware above R1.4.0 totals **78**, the table for firmware R1.4.0 and
  below totals **60**. The selector is a program version `<= 3222278144`, which decodes to exactly
  `R1.4.0`.
- Choosing between the two tables therefore needs the firmware version, which is `NODE` 0x01/01
  property `0x06` — capability #3, and a `0x01` read, never a `>= 0x80` command (see §0).

### What the manuals add to each capability

Device-side detail from [`comfoair-q-hardware.md`](./comfoair-q-hardware.md); addresses and widths
above are unchanged.

**#1 — airflow presets.** Factory values before commissioning, in **m³/h** (hardware §5,
Service p. 24):

| Preset | Q350 | Q450 | Q600 |
| --- | --- | --- | --- |
| A (Away) | 70 | 90 | 120 |
| 1 (Low) | 165 | 210 | 280 |
| 2 (Middle) | 235 | 300 | 400 |
| 3 (High) | 315 | 405 | 540 |

The manual labels this table `l/s`, which is a documented defect on its side (315 l/s = 1134 m³/h on
a 350 m³/h unit) — the RMI read settles it, because these same properties are **INT16 m³/h**
(hardware §17 defect 1). Constraint any writer must enforce: "you cannot set a preset higher than
the next preset. It is best to set the maximum preset first and work back to the lower presets."
(Service p. 24). Physical bounds: minimum **75 m³/h** (100 m³/h with the pre-heater running),
maximum **350 / 450 / 600 m³/h** by model (Installer p. 6 = Service p. 6). Naming: Zehnder's
installer-side names are `AWAY / LOW / MEDIUM / MAXIMUM AIR FLOW` while the user manual says
`Away / Low / Middle / High` — which is why both "Middle" and "Medium" are in use for preset 2.

**#2 — temperature setpoints.** Documented defaults (hardware §13.3–13.4, User p. 24): FIXED
**WARM 24 °C / NORMAL 20 °C / COOL 18 °C**; ADAPTIVE NORMAL = **21.5 °C @ 0 °C RMOT, +0.11 °C per °C
of RMOT**, WARM = NORMAL + 1.5 °C, COOL = NORMAL − 1.5 °C. Season limits (properties 0x02/0x03):
`HEATING LIMIT RMOT` **11 °C**, `COOLING LIMIT RMOT` **20 °C**. The manuals give no adjustment
*ranges* for the FIXED values — see §5.

**#3 — node identity.** `STATUS > UNIT` shows exactly `HRU TYPE`, `FIRMWARE VERSION`,
`SERIAL NUMBER` (User p. 19) — precisely NODE 0x01/01 properties 0x08 / 0x06 / 0x04, so those three
are the ones a user will expect to recognise. The display additionally reports the exchanger type
(HRV/ERV) and the orientation (LEFT/RIGHT) alongside the model, e.g. "ComfoAir Q350 / INSTALLED:
HRV / LEFT UNIT" (User p. 30, hardware §2); both are settable installer parameters rather than NODE
properties.

**#4 — PDO 230.** Zehnder's own `STATUS > FLOW RESTRICTIONS` rows, with the sign prefix the display
prints (hardware §13.6, User p. 19): `+ HUMIDITY PROTECTION` · `+ HUMIDITY COMFORT` ·
`+ TEMPERATURE PASSIVE` · `+ RF SENSOR` · `+ 0-10V INPUT` · `+ 0-10V INPUT (1-4)` · `+ COMFOCOOL` ·
`+ PREHEATER` · `− RESISTANCE` · `− RESISTANCE GUARD` · `− NOISE GUARD` · `− FROST PROTECTION` ·
`− PREHEATER` · `− BYPASS`. Two bit-label caveats follow:

- bit 25 `TemperatureComfort` is **wrong** — Zehnder calls this control `TEMPERATURE PASSIVE`, the
  same name it uses in `ADVANCED SETTINGS`, so the two would otherwise contradict each other in the
  same UI;
- bit 16 `Hood` and bits 47–54 `CO2ZoneX1..8` have **no counterpart anywhere in the four PDFs** —
  keep them (they are known bits, presumably for other models or later firmware) but do not
  present them as documented by Zehnder.

The direction split is by bit position: **bits 2–10 are `−`** (below the request), **bits 12–27 and
47–54 are `+`** (above it) — which is also why the bit list has both a `PreheaterNegative` (bit 4) and a
`PreheaterPositive` (bit 22): Zehnder's table simply lists PREHEATER twice, once per direction.
Worth surfacing in the HA UI alongside the constraint list: "A request from an automatic safety
control will always take precedent over other devices and automatic controls" (User p. 31) — the
documented reason a commanded preset appears to be ignored.

**#6 — filter reset.** The "no reset command" finding now has a second, independent source. Zehnder
documents exactly one reset path, and it is on the unit: "you can run the filter wizard by starting
user menu CHANGE FILTERS in the FILTERS menu. **After finishing the filter wizard, the filter
counter will be reset automatically.**" (Service p. 24). And it forbids the alternative: "Do not
change the filters when the unit is powered without using the filter wizard. For safety reasons the
unit will stop the ventilation during the filter replacement instructions." So the six-step on-unit
wizard is the only documented reset — which also corroborates PDO 16 value 2 (`filterwizard`) and
gives the unread PDO 18 (changing-filters state) an obvious meaning.

**#7 — error codes.** Device-side table: hardware §16 gives the unit's 32 display strings with the
ComfoSense C numeric codes and per-code decision trees (Service p. 33, pp. 37–43), for firmware
**R1.6.0**.
**Caveat:** whether the ComfoSense C's 1000-range numbers are the *same numbering* as the bus's
error ids (#7 above) is **not established** by either side — the two schemes look
different (1091 on one side, "numbering differs above 69" on the other). Do not merge the tables on
the strength of the range alone; see §5. Two behaviours worth encoding: `SERVICE MODE` is itself an
alert (1080) that presents as a *warning* (1 Hz LED, same as a filter warning), and
`UNBALANCE ERROR` self-clears once its cause is fixed.

### Other settings the hardware documents

Three of the seven rows below now have a documented address, added as capabilities #9 and #10
above; they stay here for the behavioural detail, which the manuals remain the only source for.
The other four — `FIRE PLACE PRESENT`, `HEAT EXCHANGER TYPE`, `ORIENTATION`, and
`UNITS > FLOW`/`UNITS > TEMPERATURE` — are settable on the unit itself, so they are almost
certainly RMI-addressable, but none has a documented address; each would need
discovery before it can be written.

| Setting | Documented behaviour | Source |
| --- | --- | --- |
| `UNBALANCE` | **Address: capability #9 above.** ± %, default **0**. Positive = over-pressure by reducing supply; negative = under-pressure by reducing extract. Positive is blocked when `FIRE PLACE PRESENT` = YES | Service p. 25; hardware §13.7 |
| `FIRE PLACE PRESENT` | default **NO**; gates `EXTRACT ONLY` and positive unbalance. Explicitly *not* a certified safety device | Service pp. 5, 24; hardware §13.7 |
| `ALTITUDE` | **Address: capability #10 above** (`0x1E`/01/`0x07`). Default **0–500 m**; feeds the PDO 119/120 airflow calculation | Service pp. 22, 24; hardware §10 |
| `VENTILATION CONTROLS` | **Address: capability #10 above** (`0x1E`/01/`0x09`). `FLOW CONTROL` (default, regulates around the setpoint) vs `CONSTANT FLOW` (regulates to it, constant fan adjustment) | Service p. 24; hardware §13.8 |
| `HEAT EXCHANGER TYPE` | `HRV` / `ERV`; must be changed by hand if the exchanger is swapped | Service p. 25; hardware §2 |
| `ORIENTATION` | `LEFT` / `RIGHT`; flips which physical side every sensor is on | Service p. 22; hardware §10 |
| `UNITS > FLOW`, `UNITS > TEMPERATURE` | display units; flow defaults to **l/s** even though the protocol is m³/h | User p. 24; hardware §5 |

### Other unread PDOs

Still unread: the CO2 sensor temperature, humidity and ambient-pressure PDOs (1056–1063, 1088–1095,
1120–1127; aiocomfoconnect marks them † with "scaling unknown"), and the schedule PDO families for
the schedule instances not listed in the table in §1 ("Accessory and schedule PDOs"), documented
in aiocomfoconnect `docs/PROTOCOL-PDO.md`. This list holds only what is still unread; the accessory
and schedule PDOs (ComfoFond, ComfoCool and the rest) are in §1, "Accessory and schedule PDOs".

**PDO 228** (§1 row) is the display's `STATUS > FROST PROTECTION` = "the percentage of supply air
reduction" (User p. 19) — with the twist that when a fireplace is present the extract airflow is
reduced by the same amount, so no unbalance results. So: a **percentage of supply-air reduction**,
not a boolean and not an unbalance figure.

## 5. Open questions

What neither the protocol evidence nor the Zehnder manuals settle. Recorded so that nothing above is
mistaken for a documented fact.

| Question | State of the evidence | How it could be closed |
| --- | --- | --- |
| **PDO 220's identity** | All four physical measuring points are taken (274/275/276/278) and there is no fifth display item, so 220 is computed/controller-side — but nothing names it. Verdict stays UNVERIFIED (§2). | Log 220 / 276 / 277 together on a unit with an active pre-heater or ComfoFond. |
| **Does the 2 h "limited manual" ceiling apply to an RMI-set level?** | The manual describes the *display's* up/down buttons (User p. 12). That the same ceiling governs `84 15 01 01 …` is an inference from PDO 49 reading "limited manual" for that write (§3). | With **no schedule programmed**, set a level over CAN and watch PDO 49 / 65 / 81 for over two hours. |
| **Why does the official app write a 1 s timer at all?** | Unexplained by any source. A 1 s timeout on a mode that then persists is not obviously coherent; it may be the app's way of saying "apply now, then hand back to the unit's own limited-manual logic". **New evidence, unresolved:** after a level write the timer entry does not read back as a 1 s timer at all — GETTIMERENTRY captures of the level show `0100000000ffffffffffffffff03`, i.e. enabled, configured timeout `ffffffff`, remaining `ffffffff`. So the 1 s write becomes a *standing entry with an indefinite timer*, which is consistent with the preset persisting but does not explain why 1 s produces that and `ffffffff` reportedly does not. | Capture the app's traffic against a unit whose PDO 49/81 are logged, and read back `83 15 01 01` after writing the level with `ffffffff` instead of `01000000`. |
| **The four zero bytes in the `84`/`83` frames** | Read back as `00000000` in every capture; not the timeout (they precede it in the GET reply). Always send zeros (§3). | Nothing short of firmware disassembly or a Zehnder source. |
| **Are the ComfoSense C's 1000-range codes the bus's error-id numbering?** | The hardware reference (§16) states they are the same numbering; that identification is **not established** by either side, and the two schemes look different (1091 vs "numbering differs above 69"). Treat as open. | Trigger a known fault (e.g. pull a filter warning forward) and compare the code the display shows with the code the bus reports. |
| **PDO 225's numeric mapping** | `0=disabled / 1=active / 2=overruling` is unconfirmed: PDO 225 is known only as a comfort-control mode with no authoritative value map, and the manuals do not support the words — the firmware publishes the raw number. The UI's OFF / AUTO ONLY / ON is a *different* thing — three independent controls, one PDO (§1). | Toggle each of the three controls on the unit and watch 225. |
| **`STATUS > ENERGY > TOTAL SAVINGS`** | The display has this item (User p. 20) and §1 has no PDO for it. Separately, **PDO 219** (frost protection: pre-heater power, W †) has a row in §1, "Accessory and schedule PDOs", from aiocomfoconnect only; nothing first-hand links it to TOTAL SAVINGS — do not assert that it does. | Scan 219 on a live bus and compare with the display's TOTAL SAVINGS. |
| **PDO 230 bit 16 (`Hood`) and bits 47–54 (`CO2ZoneX1..8`)** | Known bits with **no counterpart in any of the four PDFs** (§4 #4). Possibly other models, later firmware, or Option Box zoning. | A unit with CO₂ zoning and an Option Box. |
| **Adjustment ranges for the FIXED setpoints and for `UNBALANCE`** | The manuals give the defaults (24/20/18 °C; 0 %) but never the permitted range. **The unit will say.** RMI type byte `0x20` returns a writable property's range (§3), so `01 1D 01 20 0B` and `01 1E 01 20 12` answer this without guessing — and a write outside the range is refused with error 30 rather than silently clamped. Open only until RMI reads exist. | Issue the two `0x20` reads on a live bus once the RMI response parser is wired up. |
