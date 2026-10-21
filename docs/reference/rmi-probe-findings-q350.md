# RMI probe findings — the owner's ComfoAir Q350

**Source:** `GET /api/probe` of a full default walk; the repository keeps it as
`docs/unit/data/q350-probe.json`, with the serial number redacted. The decoder defects this walk
exposed are fixed in the current code (§5).

This is first-hand bus evidence, and it outranks [`comfoair-pdo-rmi.md`](comfoair-pdo-rmi.md)
wherever the two disagree — each disagreement is named below. Names marked *(inferred)* are this
document's reading of the values, not anybody's documentation.

## 1. The unit answers RMI reads from a plain CAN node

- 4226 requests in 118 s, **0 timeouts**, from node `0x2A` with `AnotherCounter = 0`, no
  node-presence heartbeat sent. So on this unit and firmware a plain CAN node is answered:
  neither the LAN C's counter value 1 nor a heartbeat is needed.
- Error replies carry the code in the **first payload byte**. Codes 12, 13, 14 and 15 arrived and
  steered the walk exactly as documented. One undocumented code was seen: **53**, on HMI
  `0x1A/01/0x09`.
- `GETACTIVEERRORS` (`80 03 01`) returned the documented 32-byte all-zero bitfield (no errors).
- Only node `0x01` was heard on the bus: no ComfoSense, no LAN C, no Option Box on this
  installation.

## 2. Identity (NODE `0x01/01`)

| Prop | Value |
| --- | --- |
| `0x04` serial | the serial number (redacted) (24+ characters; longer than the 15-character example in the protocol documentation) |
| `0x06` firmware | `0xC0102C00` → **R1.11.0**, newer than anything the protocol documentation describes (~R1.6). Above R1.4.0, so the error table for R1.4.0 and later applies, not the one for earlier firmware |
| `0x08` model | `ComfoAir Q350 NL R VV ST` |
| `0x0B` article | `471502057` |
| `0x0D` country | `NL` |
| `0x14` name | `ComfoAir Q` |
| `0x01..0x03, 0x05, 0x07, 0x09, 0x0A, 0x0C` | undocumented: `1, 1, 1, 6`, u32 `0xC0109400`, `4`, `0x41600000` (14.0 as an IEEE float — reading unconfirmed), string `NULL` |

## 3. Settings the firmware could not see before

Every writable property answered its `0x20` range and `0x40` step.

| Address | Meaning | Value | Range · step |
| --- | --- | --- | --- |
| `1E/01/03..06` | airflow preset away / low / medium / high (INT16 m³/h) | 60 / 90 / 150 / 255 | 60–90 · 60–150 · 60–325 · 150–325, step 5 |
| `1E/01/08` | *airflow upper limit (inferred)* | 325 | read-only |
| `1E/01/12` | unbalance (INT16 ‰) | 0.0 % | −15.0 … +15.0 %, step 0.1 |
| `1E/01/07, 09` | altitude, control mode | 0–500 m, flow control | — |
| `1E/01/0B..0D` | bathroom switch on-delay / off-delay / mode | 0 s / 30 min / fixed | — |
| `1D/01/02, 03` | RMOT heating / cooling limit (×10 °C) | 11.0 / 22.8 °C | 0–15 · 15–40, step 1.0 |
| `1D/01/04, 06, 07` | temperature passive / humidity comfort / humidity protection | **on / off / off** | 0–2 |
| `1D/01/08` | temperature profile mode | fixed | 0–1 (no 2 = external setpoint on this unit) |
| `1D/01/0A..0C` | target warm / normal / cool (×10 °C) | 23.0 / 20.0 / 19.0 °C | 23.0–28.9 · 18.9–23.3 · 17.8–19.0, step 1.0 |
| `1C/01/02` | filter pre-warning | 21 days | **u16 on the wire** — the protocol documentation says UINT8 |
| `1C/01/03` | filter lifetime | 180 days | 0–65535 |
| `20/01/04` | orientation | right | read-only |

**Ranges are live, not fixed.** The preset and setpoint bounds track the neighbouring values: the
away maximum (90) is the current low, the low maximum (150) the current medium, the high minimum
(150) the current medium. The setpoint ranges look neighbour-bound the same way, but their exact
rule is not readable from one sample (the cool range 17.8–19.0 ends at the cool value itself). This is the unit enforcing "a preset cannot exceed the next one" (Service p. 24) itself.
A future writer must re-read a property's range after every write to a neighbour, and write the
highest preset first when raising them.

The three sensor-ventilation values in `1D/01/04, 06, 07` are readable over RMI, and the firmware
reads them: the `temperature_passive`, `humidity_comfort` and `humidity_protection` selects are
filled from the unit's own answers in the [unit read](/protocol/probe#the-unit-read), about 10 s
after boot and every 6 h after. Until the first answer each is unknown; no factory default is
published. This unit held on / off / off.

## 4. Units present, and what is undocumented

Present on node `0x01`: every unit of the documented unit table (`0x01–0x03`, `0x15–0x1E`, `0x20–0x28`)
plus four undocumented ones, `0x29`, `0x2A`, `0x2B`, `0x2C`. Absent (error 12): `0x04–0x14`,
`0x1F`, `0x2D–0x2F`. Subunit counts match the documented table (SCHEDULE 10, VALVE 2, FAN 2,
TEMPERATURESENSOR 6, HUMIDITYSENSOR 6, PRESSURESENSOR 2, ANALOGINPUT 4).

- **`0x2B` — CO₂ zones *(inferred)*.** Eight identical subunits whose property `0x01` is a one-hot
  bit (1, 2, 4 … 128) — the eight `CO2ZoneX1..8` bits 47–54 of PDO 230 that §4 #4 of
  `comfoair-pdo-rmi.md` could find in no manual. Per zone: `0x02` = 2000, `0x03` = **400**,
  `0x05` = 1050, all range 0–2000 step 50 (ppm-shaped; 400 is outdoor CO₂), `0x06` = 50,
  `0x07` = 300 (30–7200, step 10, seconds-shaped).
- **`0x2A`** — properties `0x09..0x12` all −400 (full INT16 range): ten slots reading what looks
  like "no sensor" (−40.0). Unconfirmed.
- **`0x29`, `0x2C`** — property `0x01` only.
- **HMI `0x1A`** — `0x03` = 70, range 10–100 step 5 (*display brightness, inferred*); `0x04` =
  `ENGLISH` (display language).
- **ANALOGINPUT `0x25`**, four identical 0–10 V inputs: `0x02` 0 / `0x03` 100 (0–100), `0x04` 0
  (0–1), `0x05` 50 (0–100), `0x06` 100 (1–250), `0x07` 300 (30–7200 s).
- **POSTHEATER `0x27`** — `0x03` = 100 (50–150 step 50), `0x04` = 180 (60–600 step 60),
  `0x05` = 300 (100–520 step 10).
- **SCHEDULE `0x15`** — GET ONE only returns a two-byte header per subunit (`01`, subunit index).
  The timers themselves need `0x83` GETTIMERENTRY, which the probe does not send.
- Nearly every subunit answers property `0x01` = 1 with range 0–255: a generic "present/enabled"
  byte, not a setting.

## 5. Decoder defects this walk exposed (fixed in the current code)

- A two-byte value ending in NUL (`64 00`) was shown as the text `"d"`. An uncatalogued value is
  now text only with a terminating NUL and at least two printable characters before it (`guess()`
  in `components/comfoair_probe_report/comfoair_probe_decode.cpp`).
- Unsigned 16-bit values whose range is `0000ffff` were shown as `0..-1`. A range that decodes
  min > max signed is now decoded unsigned (`settle_sign()`, same file).
- `1C/01/02` was catalogued as u8; it is u16, and the catalogue row now says so
  (`components/comfoair_probe_report/comfoair_probe_catalogue.cpp`, FILTER `0x02`).
