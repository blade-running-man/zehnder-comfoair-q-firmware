# ComfoAir Q — canonical hardware reference

**Compiled 2026-09-01** from the four Zehnder PDFs in [`docs/manuals/`](../manuals/). Every fact below
carries a page citation into one of them. Nothing here is inferred from general knowledge; where a
figure was read off a drawing rather than a text layer it is marked *(read from artwork)*, and where
the two Zehnder documents disagree the conflict is recorded in §17 rather than silently resolved.

Companion documents: [`comfoair-pdo-rmi.md`](./comfoair-pdo-rmi.md) is the protocol/firmware source of
truth (PDO table, RMI frames). **This file is the device-side source of truth** — what the hardware
physically is, what the manuals say it does, and which PDO each documented behaviour corresponds to.
[`esp32-node-hardware.md`](./esp32-node-hardware.md) is the other end of the ComfoNet cable — the
ESP32 boards and CAN transceivers.

---

## 1. Source documents

| Short cite | File | Pages | Printed identifier |
| --- | --- | --- | --- |
| **Installer** | `Zehnder_CSY_ComfoAir-Q-Installer_INM_SI_en.pdf` | 20 | `ZGNL_Manual_40015975, V0118, EN` (p. 20) |
| **User** | `Zehnder_CSY_ComfoAir_Q_User_INM_SI_en.pdf` | 36 | `ZGNL_Manual_40015974, V0118, EN` (p. 35) |
| **Service** | `Zehnder_CSY_ComfoAir_Q_Service_INM_SI_en.pdf` | 48 | `ZGNL_Manual_10001004869, V0118, EN` (p. 48) |
| **Brochure** | `Zehnder_ZGSI_V0124_CSY_PBR_ComfoAirQ_en_Final.pdf` | 20 | `Z_ZGSI_V0124_CSY_PBR_ComfoAirQ_en` (p. 20) |

- All four are native-text PDFs; no page required OCR.
- The three `V0118` manuals are one documentation set: the Installer manual's foreword (Installer
  p. 2) states that commissioning, maintenance, malfunction procedures, service parts and "extensive
  technical data" live in the Service manual, and display operation in the User manual.
- Manuals: manufacturer **Zehnder Group Nederland B.V.**, Lingenstraat 2, 8028 PM Zwolle (User p. 25);
  CE declaration signed Zwolle, 04-04-2016 (User p. 25).
- Malfunction codes in the Service manual are stated for firmware **R1.6.0** (Service p. 33 fn. 1).
- The Brochure is `V0124` (2024) marketing material and is **six years newer** than the manuals; it
  drops the ERV variant and the ComfoCool entirely and renames the ComfoConnect line. Treat the
  manuals as authoritative for anything the two disagree on.
- Two further Zehnder PDFs in `docs/manuals/` are cited mainly by the unit annotations
  (`docs/unit/data/annotations.json`): `Zehnder_CSY_ComfoAir_Q_Installer_INM_NL_nl.pdf` (the Dutch
  installer manual, V0117, 52 pages — cited there for its installer settings menu, *INSTELL.
  HOOFDPRINT*, pp. 21–23, and here only in §10 for the post-heater) and `Zehnder_CSY_ComfoConnect-Pro_INM_NL-nl.pdf` (cited for the ComfoConnect Pro's
  Modbus register table, pp. 21–22, which corroborates what several RMI properties mean).

Extraction tooling note: `pdftotext -layout` for text, `pdftoppm -r 500` + crops for the wiring
diagrams and dimension drawings.

---

## 2. Model range and identification-plate decoding

Type-plate suffixes (Installer p. 5, identical table at Service p. 2):

| Suffix | Meaning |
| --- | --- |
| `ComfoAir` | Product family name |
| `Q` | Product type name |
| `350` / `450` / `600` | Maximum air volume of 350 / 450 / 600 m³/h |
| `SI` | Country code of the unit |
| `R` | Unit set with supply and extract air on the **right** side as default |
| `ST` | Four **fixed** air connections — *not available on the Q350 and Q450* |
| `TR` | Four air connections which **can turn** — *not available on the Q600* |
| `ERV` | Unit has an **enthalpy exchanger** installed as default |

So the range is effectively **Q350 TR, Q450 TR, Q600 ST** — exactly the three the brochure lists
(Brochure p. 14). The `ST`/`TR` footnotes are printed as superscripts 15/16 and are easy to attach to
the wrong row; the reading above is the one the footnote text supports (Service p. 2).

**Left/right is not a separate SKU.** One unit covers both: "the Zehnder ComfoAir Q design
incorporates the right-hand and left-hand version in the same unit" (Brochure p. 18). Field
conversion R↔L is a 12-step rework — swap the two filters (arrows still pointing up), then pull the
pre-heater with its cable and grommet, rotate it 180°, and reinsert it on the opposite side
(Installer p. 12) — followed by setting `ORIENTATION OF THE UNIT` in the commissioning wizard
(Service p. 22).

**HRV vs ERV is a settable parameter, not just a build option.** `HEAT EXCHANGER TYPE` = `HRV`
(standard/sensible) or `ERV` (enthalpy); it must be changed by hand if the exchanger is physically
swapped during service (Service p. 25). The unit reports it under `STATUS > UNIT > HRU TYPE`, e.g.
"ComfoAir Q350 / INSTALLED: HRV / LEFT UNIT" (User p. 30).

---

## 3. Technical data

Full table, Installer p. 6 = Service p. 6 (identical page, one value differs — see §17).

### Performance

| | Q 350 | Q 450 | Q 600 |
| --- | --- | --- | --- |
| Minimal airflow, pre-heater **off** | 75 m³/h | 75 m³/h | 75 m³/h |
| Minimal airflow, pre-heater **on** | 100 m³/h | 100 m³/h | 100 m³/h |
| Maximal airflow | 350 m³/h | 450 m³/h | 600 m³/h |
| Thermal efficiency (EN 13141-7:2010) | 92 % | 90 % | 89 % |

### Electrical

| | Q 350 | Q 450 | Q 600 |
| --- | --- | --- | --- |
| Max power **incl.** pre-heater (−15 °C, max airflow) | 1850 W / 10.00 A | 2240 W / 10.80 A | 2620 W / 12.70 A |
| Max power **excl.** pre-heater | 180 W / 1.42 A | 250 W / 1.98 A | 350 W / 2.77 A |
| Power supply / cord | 230 V ±10 %, single phase, 50 Hz, 2.5 m cord | | |
| Cos φ | 0.36 – 0.54 | 0.32 – 0.57 | 0.4 – 0.62 |
| Internal fuse | F5010 (10 A) | F5015 | F5015 |

Fuse replacement rule of thumb: "If the unit is a ComfoAir Q 350 get a F5010 fuse. Otherwise get a
F5015 fuse." (Service p. 44). The two manuals print different amperages for F5015 — see §17.

The **max power excl. pre-heater** column is the ceiling for PDO 128 *Power Usage*
([`comfoair-pdo-rmi.md` §1](./comfoair-pdo-rmi.md)). `STATUS > ENERGY > POWER CONSUMPTION >
VENTILATION` is defined as "the current electrical consumption of **the fans**", with `PREHEATER`
broken out as a separate item, and `YEAR TO DATE` / `TOTAL` are likewise defined as "of the fans"
(User p. 20) — so PDO 128/129/130 are all fan-only, and this column is genuinely their ceiling. A
reading much above 350 W means the pre-heater is running, which is separately exposed as **PDO 146**
(not PDO 144, which is the pre-heater's kWh **year** counter — see §17). PDO 146 is user-visible as
`STATUS > FROST PREHEATER` = "the current power consumption of the preheater" (User p. 19).

### Connection

| | Q 350 | Q 450 | Q 600 |
| --- | --- | --- | --- |
| Air connection Ø | inside 160 mm / outside 190 mm | inside 160 mm / outside 190 mm | inside 180 mm / outside 200 mm |
| Condensate drain Ø | tube 32 mm / thread 1¼″ | | |

### ComfoNet bus

| | Value |
| --- | --- |
| Maximal power | 400 mA @ 12 V |
| Maximal non-powered devices | 4 |
| Cable | 2× unshielded twisted pair, stiff (solid) wires 0.6 mm², max 50 m |
| Colour code | 12 V red · GND black · CAN_H yellow · CAN_L white |

### Materials and environment

| | Value |
| --- | --- |
| Housing | coated sheet steel |
| Interior | EPP and ABS |
| Heat exchanger | polystyrene |
| Enthalpy exchanger | polyethylene-polyether-copolymer |
| IP / ISO classification | IP40 / B |
| Temp. range, transport & storage | −40 °C … +60 °C |
| Temp. range, moved air | −20 °C … +60 °C |
| Temp. range, installation area | 0 °C … 45 °C |
| RH, installation area | < 90 %, non-condensing |
| Weight | 50 kg |
| Filter class | outdoor air F7 / extract air G4 |

The **moved-air range −20 … +60 °C** is the plausibility window for PDO 274–278; the Service manual's
own sensor-fault logic uses tighter thresholds (§14).

---

## 4. Airflow / pressure / sound envelope

Brochure p. 15, measured with a **G4/G4 (ISO Coarse ≥ 65 %) filter and pre-heater fitted**. `Lw` is
sound *power* in dB(A) ref 10⁻¹² W; supply/extract measured per ISO 5135:1997 incl. end reflection;
SFP calculated per EN 13141-7:2010. `MP` = measurement point.

**Q350 TR**

| MP | Qv m³/h | Pst Pa | P W | Lw supply | Lw extract | Lw unit |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 150 | 25 | 13 | 46 | 35 | 34 |
| 2 | 200 | 50 | 30 | 51 | 38 | 38 |
| 3 | 245 | 50 | 42 | 54 | 41 | 41 |
| 4 | 250 | 100 | 58 | 56 | 43 | 43 |
| 5 | 300 | 100 | 77 | 59 | 46 | 46 |
| 6 | 350 | 100 | 100 | 62 | 48 | 49 |
| 7 | 250 | 150 | 71 | 58 | 45 | 45 |
| 8 | 250 | 200 | 85 | 61 | 47 | 47 |
| 9 | 300 | 200 | 106 | 63 | 49 | 49 |
| 10 | 350 | 200 | 131 | 66 | 51 | 52 |

**Q450 TR**

| MP | Qv m³/h | Pst Pa | P W | Lw supply | Lw extract | Lw unit |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 200 | 25 | 17 | 52 | 42 | 39 |
| 2 | 250 | 50 | 37 | 56 | 44 | 42 |
| 3 | 300 | 50 | 55 | 58 | 46 | 45 |
| 4 | 315 | 50 | 61 | 59 | 46 | 45 |
| 5 | 350 | 100 | 94 | 62 | 48 | 48 |
| 6 | 400 | 100 | 121 | 64 | 50 | 51 |
| 7 | 450 | 100 | 152 | 67 | 52 | 53 |
| 8 | 350 | 150 | 110 | 63 | 49 | 49 |
| 9 | 350 | 200 | 127 | 64 | 50 | 51 |
| 10 | 400 | 200 | 156 | 67 | 51 | 53 |
| 11 | 450 | 140 | 166 | 68 | 52 | 54 |

**Q600 ST**

| MP | Qv m³/h | Pst Pa | P W | Lw supply | Lw extract | Lw unit |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 250 | 25 | 21 | 54 | 43 | 43 |
| 2 | 300 | 25 | 36 | 56 | 45 | 45 |
| 3 | 350 | 50 | 64 | 59 | 48 | 48 |
| 4 | 400 | 50 | 87 | 62 | 50 | 50 |
| 5 | 420 | 50 | 98 | 63 | 51 | 51 |
| 6 | 450 | 100 | 134 | 65 | 53 | 53 |
| 7 | 500 | 100 | 166 | 68 | 55 | 55 |
| 8 | 600 | 100 | 243 | 73 | 59 | 60 |
| 9 | 450 | 150 | 153 | 66 | 53 | 54 |
| 10 | 450 | 200 | 173 | 67 | 54 | 55 |
| 11 | 500 | 200 | 207 | 70 | 56 | 57 |
| 12 | 600 | 200 | 287 | 75 | 60 | 61 |

Each table is paired with a Pst-vs-Qv envelope chart carrying SFP [Wh/m³] and Lv [dB(A)] iso-lines —
SFP 0.2/0.25/0.3/0.35 on both TR models, 0.27/0.34/0.41 on the Q600 ST (Brochure p. 15). The Service
manual carries its own per-model fan curves on pp. 7–9.

Note the Q600 MP 12 figure of **287 W at 600 m³/h / 200 Pa** against the type-plate ceiling of 350 W
excl. pre-heater — i.e. the unit's rated maximum is reached near the top-right corner of its envelope.

---

## 5. Ventilation presets

Factory values before commissioning (Service p. 24):

| Preset | Q350 | Q450 | Q600 |
| --- | --- | --- | --- |
| PRESET A (Away) | 70 | 90 | 120 |
| PRESET 1 (Low) | 165 | 210 | 280 |
| PRESET 2 (Middle) | 235 | 300 | 400 |
| PRESET 3 (High) | 315 | 405 | 540 |

The commissioning wizard's own screens name these same four presets differently: `AWAY AIR FLOW` /
`LOW AIR FLOW` / `MEDIUM AIR FLOW` / `MAXIMUM AIR FLOW` (Service p. 23) — **Medium**, not the User
manual's **Middle**, for PRESET 2. This is the likely origin of a Middle-vs-Medium naming divergence
in the integration and is worth having on record.

**The manual prints these as `l/s`; the values are m³/h.** 315 l/s would be 1134 m³/h on a 350 m³/h
unit. See §17. Rule when editing them: "you cannot set a preset higher than the next preset. It is
best to set the maximum preset first and work back to the lower presets." (Service p. 24)

These four numbers are exactly what RMI `VENTILATIONCONFIG` 0x1E subunit 01 properties 03/04/05/06
read and write as INT16 m³/h — capability #1 in
[`comfoair-pdo-rmi.md` §4](./comfoair-pdo-rmi.md) — which independently confirms the unit is m³/h.

The display's own flow unit is a separate cosmetic setting: `UNITS > FLOW`, default **l/s**
(User p. 24); `FLOW UNIT` (m³/h or l/s) is also asked during commissioning (Service p. 22).

---

## 6. Air path, orientation and duct work

Port codes (Installer p. 16):

| Code | Meaning |
| --- | --- |
| ODA | Outdoor air (in, from outside) |
| SUP | Supply air (out, to dwelling) |
| ETA | Extract air (in, from dwelling) |
| EHA | Exhaust air (out, to outside) |

- **R layout** — ODA top-left, SUP top-right, EHA bottom-left, ETA bottom-right. **L layout** is
  mirrored (Installer p. 16, repeated on the Quick Guide, Installer p. 20).
- Minimum **1.5 m** between the outdoor-air intake and the exhaust-air outlet (Installer p. 16).
- Thermal + vapour-resistant insulation required on the exhaust duct and the outdoor-air duct from
  the unit to the roof/wall passage; recommended on the supply duct up to the valves (Installer p. 16).
- Drain the exhaust duct **towards the unit** (Installer p. 16).
- Minimum internal duct diameter (Installer p. 16): **Q350** 160 mm · **Q450** 160 mm (180 recommended)
  · **Q600** 180 mm (200 recommended).
- **No flexible duct system** — it "disturbs the balanced ventilation operating principle"; only
  Zehnder-approved semi-rigid systems (Installer p. 15). Approved: ComfoPipe (Plus), ComfoWell,
  ComfoFlat, ComfoTube (Installer p. 15).
- Install a silencer directly on the supply and extract connections, straight (Installer p. 15).
- **Safety interlock on installation order:** "Always connect air ducts of at least 900 mm to the unit
  before you connect power" — so the running impeller cannot be touched (Installer pp. 4, 15;
  Service p. 32).
- Valves/grilles: ≥ **750 mm** apart (supply vs extract, prevents short-circuiting), ≥ **350 mm** from
  an adjacent wall/ceiling/floor, and door overflow of ≥ **12 cm² per l/s** — a gap of ≥ 7600 mm² or
  ≥ 10 mm under a standard 760 mm door (Installer p. 17).

---

## 7. Dimensions and mounting

Dimension drawings: Installer p. 8 (Q350/Q450), p. 9 (Q600); the same drawings appear at Brochure
p. 14. All values in mm, *read from artwork*.

| | Q350 / Q450 | Q600 |
| --- | --- | --- |
| Overall width | 725 | 725 |
| Overall depth (top view) | 570 | 570 |
| Total height | 849.7 (body 745 + top section 40.7) | 850 (top section 40) |
| Duct-centre span | 580 | 490 |
| Duct row spacing | 280.5 | 280.5 |
| Duct Ø (inside / outside) | 160 / 190 | 180 / 200 |
| Weight | 50 kg | 50 kg |

Mounting (Installer pp. 13–14, Quick Guide p. 20):

- Wall must have a mass of **≥ 200 kg/m²**. Lighter walls need the optional Zehnder **floor mounting
  frame**, which also reduces structure-borne noise.
- Mounting bracket width **640 mm**; bracket height from floor **≥ 1050 mm** (floor variant shows
  1100 mm); clearance under the unit for the condensate drain **≥ 250 mm**.
- Bracket has two hanging openings: **opening 1** for Zehnder ComfoPipe Plus ducts, **opening 2** for
  all other ducts (Installer p. 13).
- Level the unit with its two level adjusters (Installer p. 7 pos. C, p. 13).
- **≥ 1 m free space in front of the unit** for maintenance; no side clearance needed
  (Installer p. 5; repeated User p. 26).
- Not for industrial environments (pools, saunas), and not recommended in high-humidity rooms
  (bathroom, w.c.) because of external condensation (Installer p. 5).

---

## 8. Condensate drain

(Installer pp. 14–15; Service pp. 14, 22)

- Two **Zehnder bayonet connections** on the underside (Installer p. 7 pos. M). They are **not
  air-tight** and must be closed — with a sealing cap or a dry siphon.
- **Never fit a U-bend water lock on the unit** — on warm days the water evaporates out of it.
- Dry siphon must withstand a negative pressure of **≥ 500 Pa**. With a ball-type dry lock, use
  ≥ 90 mm of pipe between the lock and the bayonet connection.
- Supplied adapters: smooth tube **Ø 32 mm** and threaded **1¼″**. The adapter is not suitable for
  gluing; Zehnder recommends a sealing sleeve.
- The pipeline must never sit higher than the bottom of the unit, and must discharge with a **free
  outlet** into the domestic waste-water siphon — never a direct connection.
- **Delivery differs by exchanger** (Installer p. 5): standard HRV unit ships with 32 mm adapter,
  1¼″ adapter and one sealing cap; **ERV unit ships with two sealing caps** — an enthalpy exchanger
  transfers the moisture back to the supply air, so there is no condensate and no siphon is needed.
- A dry siphon is **mandatory** with a standard exchanger; the "no condensation drain" option in the
  commissioning wizard is only selectable when an enthalpy exchanger is configured (Service pp. 14, 22).
- User-side maintenance: pour a cup of water into the waste-water seal every 6 months
  (User p. 26 §5.1).

---

## 9. Internal components and service parts

Exterior/interior overview (Service p. 10; equivalent list at Installer p. 7):

| Pos | Part |
| --- | --- |
| A | Semi-transparent visor — access to display and filter caps |
| B | 2 filter caps |
| C | 2 filters |
| D | Display |
| E | Cable tray cover |
| F | Front cover (air-tight seal) |
| G | ComfoNet RJ45 connection |
| H | Mains power connection + identification plate (not visible) |
| I | 2 ComfoNet plug-in connections |
| J | Main board, behind the display cover |
| L | Pre-heater for frost protection (optional; standard in unit version "VV") |

Installer p. 7 additionally names: **B** engraving of the air connections on the cable tray cover,
**C** 2 level adjusters, **M** 2 condensation drains, **N** 4 duct connections.

Orderable service parts, exploded view (Service p. 11) — each ships as a service set with its own
fitting instruction; article codes are in the supplier price list, not the manual:

| # | Part | # | Part |
| --- | --- | --- | --- |
| 2 | Top-section sensor | 11 | Pre-heater |
| 3 | Main board | 12 | Filter cap set (2×) |
| 4 | Filter set G4/F7 (1×/1×) | 13 | Display |
| 5 | Mid-section sensor | 14 | Front cover |
| 6 | Fan pressure hoses | 15 | Power cord |
| 7 | Drain set | 16 | Mounting bracket |
| 8 | Heat exchanger / enthalpy exchanger | 17 | Modulating bypass valve |
| 9 | Fan | 18 | Screws |
| 10 | Modulating bypass actuator | | |

Access notes (Service pp. 26, 28–29, 34–36):

- **Open the unit** = visor → set `SERVICE MODE` → disconnect power when the display says so → 3 front
  cover screws. `SERVICE MODE` retracts the bypass valves out of the heat exchanger so the exchanger
  can be pulled; it also stops basic functions (Service pp. 25–26).
- **Heat exchanger** — pull the strap, never cut it.
- **Fans** — behind the exchanger and the bypass valve; disconnect 2 pressure hoses, release 2 clamps,
  lift the scroll housing (5 edge screws to open it).
- **Top-section sensor** — top of the unit, reachable after removing exchanger, filters and (if fitted)
  the pre-heater.
- **Mid-section sensor** — behind the modulating bypass valve, in a rubber pressure-sensor holder.
- **Main board and pre-heater** — behind the display cover (2 screws) after pulling the cable tray
  cover forward. **ComfoNet connectors** are reachable from the cable tray alone, without removing the
  front cover — but not without preparation: Service p. 34 §6.3 step 1 is "Disconnect the power to the
  unit. Open the semi-transparent visor," then pull the cable tray cover forward. **Main fuse** is
  under the cable tray (6 screws).
- Reassembly: tighten screws by hand to **max 1.5 Nm**, cables in their guiding channels, bottom edge
  of the front cover behind the raised edge of the bottom plate — this is what makes the seal
  air-tight (Service p. 31; Installer p. 12).
- RTC backup battery on the main board: **3 V lithium button cell, 48 mAh, type BR1225** — replace if
  the clock is lost after a power cut (Service p. 45).

Brochure-only detail: the fans use an **ebm-papst RadiCal impeller** in a scroll housing with a flow
grid (Brochure p. 9); the exchanger is the "CORE diamond shape" type with variable duct heights
(Brochure p. 8).

---

## 10. Sensors

The unit carries **four temperature measuring points and two pressure measuring points**, packaged as
two replaceable sensor modules (Service pp. 11, 35). The strongest single confirmation of that count
is the unit's own display: `STATUS > TEMP. AND HUMIDITY` exposes exactly four temperature/humidity
pairs — EXTRACT, EXHAUST, OUTDOOR, SUPPLY — and no pre-heated pair (User p. 19). That is what proves
PDO 277 and PDO 293 (the air after the pre-heater) are **derived values never shown to the owner**,
not a fifth measuring point; the protocol reference's PDO 220 and PDO 277 verdicts lean on this fact
([`comfoair-pdo-rmi.md` §1](./comfoair-pdo-rmi.md)).

| Module | Service part | Location | Carries |
| --- | --- | --- | --- |
| Top-section sensor | #2 | top of the unit | the ODA / ETA pair |
| Mid-section sensor | #5 | behind the modulating bypass valve | the SUP / EHA pair |

§11's connector map corroborates this module boundary by colour: the ODA/ETA pair lands on the two
**red** 4-pin connectors `J401` (S11) / `J402` (S1) — the top-section sensor — while the SUP/EHA pair
lands on the two **yellow** 4-pin connectors `J405` (S33) / `J406` (S3) — the mid-section sensor; the
two **blue** 3-pin `J403` (S22) / `J404` (S2) are the pressure sensors, on neither temperature module
(Service p. 12 = Installer p. 10; see §11 below). The colour coding is itself the top-section /
mid-section module boundary.

Which physical side is which **flips with the orientation setting** (Service p. 12 = Installer p. 10
wiring-diagram legend):

| Code | Orientation: Right | Orientation: Left |
| --- | --- | --- |
| S1 | Sensor outdoor air | Sensor extract air |
| S2 | Exhaust air pressure sensor | Supply air pressure sensor |
| S3 | Sensor supply air | Sensor exhaust air |
| S11 | Sensor extract air | Sensor outdoor air |
| S22 | Supply air pressure sensor | Exhaust air pressure sensor |
| S33 | Sensor exhaust air | Sensor supply air |
| M1 | Exhaust motor | Supply motor |
| M2 | Supply motor | Exhaust motor |

Troubleshooting text states the side directly: with orientation RIGHT, `SENSOR_ETA` sits "in top of
the unit on the right side" and `SENSOR_SUP` "in middle of the unit on the right side"; with LEFT,
both are on the left (Service p. 41). Likewise `FAN_EHA` is on the right with RIGHT orientation, and
the pre-heater is on the **opposite** side from the connections (RIGHT orientation → pre-heater left)
(Service pp. 39–40).

**Airflow is not measured by a flow sensor.** There is no hot-wire or vane element in the parts list.
Flow is derived from fan characteristics plus the two static-pressure sensors, corrected for site
air density via the `ALTITUDE` setting — the menu exists so the unit knows "the average environmental
pressure needed for its airflow calculations" (Service p. 22). Commissioning uses an **external flow
meter** at each valve to balance the system (Service p. 23). This is why PDO 119/120 are computed
values and why `EXT_PRESSURE_*` faults present as airflow faults.

Humidity: there is no RH sensor in the unit's own parts list, yet the firmware exposes PDO 290–294
(extract / exhaust / outdoor / pre-heated-outdoor / supply humidity) and `HUMIDITY COMFORT` /
`HUMIDITY PROTECTION` controls read "the relative humidity of the extract air" (User p. 23). PDO 294
is user-visible in the same STATUS menu as the other three live points: `SUPPLY AIR HUM.` = "view the
current humidity level of the supply airflow" (User p. 19). The manuals never name the
humidity element as a separate part — it is integrated in the temperature sensor modules. The
brochure describes the bypass as driven by "intelligent temperature and humidity sensors"
(Brochure p. 4). External **Zehnder Hygro sensor** and **CO₂ sensor** are separate wired accessories
that need an Option Box (Installer p. 18).

ComfoCool accessory sensors, for completeness: condenser and supply NTCs, **≈ 10 kΩ at 25 °C**
(Service p. 37).

### Mapping to the PDO table

Correspondence between the manual's sensor names and
[`comfoair-pdo-rmi.md` §1](./comfoair-pdo-rmi.md). Pairing each sensor's temperature PDO with its
humidity PDO makes the four-measuring-point structure visible and matches the unit's own
`TEMP. AND HUMIDITY` display exactly (User p. 19). *This mapping is the integration's own — the
manuals never mention PDO numbers.*

| Manual name | Temp PDO | Hum PDO | On the display? |
| --- | --- | --- | --- |
| Sensor outdoor air (ODA) | 276 | 292 | yes — OUTDOOR AIR TEMP. / HUM. |
| Sensor supply air (SUP) | 278 | 294 | yes — SUPPLY AIR TEMP. / HUM. |
| Sensor extract air (ETA) | 274 | 290 | yes — EXTRACT AIR TEMP. / HUM. |
| Sensor exhaust air (EHA) | 275 | 291 | yes — EXHAUST AIR TEMP. / HUM. |
| after the pre-heater — **no physical sensor** | 277 | 293 | **no** |
| after an external post-heater — accessory, RMI 0x27 | 221 | — | no. The four PDFs above do not mention a post-heater; the Dutch installer manual does (below the table) |
| unattributed | 220 | — | no |
| Supply / exhaust pressure sensors | — | — | no; only as `EXT_PRESSURE_*` faults and PDO 230 `RESISTANCE` / `RESISTANCE GUARD` |

**The post-heater is documented, in the Dutch installer manual only**
(`Zehnder_CSY_ComfoAir_Q_Installer_INM_NL_nl.pdf`, cited here as *NL*). It is an Option Box
accessory: the Option Box wiring diagram has terminal 7a *Sensor naverwarmer* (analogue, 3.3 V DC)
and 7b *Regeling naverwarmer* (0–10 V DC output) (NL p. 12, §4.8); the installer menu has
*INSTELL. OPTION BOX › POSTHEATER*, the post-heater's PI controller (proportional band, default
2 °C; integration time, default 120 s), shown only while the accessory is connected (NL pp. 20,
22, §7.3.3); the malfunction table lists `POSTHEAT_TEMP ERROR` and `POSTHEAT_ CONNECT ERROR`
(NL p. 34); and the accessory list says a post-heater can be controlled only with an Option Box
connected (NL p. 48). None of this names a PDO.

---

## 11. Main board connector map

Wiring diagram: Service p. 12 = Installer p. 10. The J-designators live in the drawing artwork, not
the text layer; the map below was **verified by rendering the page at 500 dpi and reading the
crops directly** (see §17 for the one designator the two extractions disagreed on).

### Communication

| Connector | Type | Function |
| --- | --- | --- |
| `J100` (port "19") | RJ45, 8-pin | ComfoNet |
| `J101` (port "17") | 4-pin plug-in | ComfoNet — pins **4-3-2-1 = 12 Vdc (red) / GND (black) / CAN_H (yellow) / CAN_L (white)** |
| `J102` (port "18") | 4-pin plug-in | ComfoNet, same pinout as J101 |
| `RF` | on-board module footprint | RF option; legend reads "Not applicable" for both orientations on the base unit |

### Low-voltage peripherals

| Connector | Goes to | Wires |
| --- | --- | --- |
| `J800` | BYP (modulating bypass actuator) | YE, VT, BU, GY |
| `J801` | BYP (opposite side) | GY, BU, VT, YE |
| `J401` | S11 sensor (red connector body, 4-pin) | RD, WH, BK, BU |
| `J402` | S1 sensor (red, 4-pin) | BU, BK, WH, RD |
| `J403` | S22 pressure sensor (blue, 3-pin) | YE, BU, GN |
| `J404` | S2 pressure sensor (blue, 3-pin) | GN, BU, YE |
| `J405` | S33 sensor (yellow, 4-pin) | RD, WH, BK, VT |
| `J406` | S3 sensor (yellow, 4-pin) | VT, BK, WH, RD |
| `J400` | VV (pre-heater) | BU, BL, WH, RD |
| `J203` | fan-motor loom, M2 side | YE, BU, WH |
| `J205` | fan-motor loom, M1 side | BU, YE, WH |

### Mains

A single **IEC-style 230 V inlet** at the bottom of the board feeds the distribution block through the
internal fuse (F5010/F5015), and screw terminal blocks fan it out:

| Block | Feeds |
| --- | --- |
| `J200` | mains distribution from the 230 V inlet (L / N / PE) |
| `J201` | M1 motor — L, N, PE |
| `J202` | M2 motor — L, N, PE (line choke in series) |
| `J204` | VV pre-heater — L, N, PE |

The display connects by a flat black ribbon, not by a numbered terminal. Each fan therefore has two
harnesses: mains from its terminal block, plus a 3-wire low-voltage loom (`J203`/`J205`) for control
and feedback — consistent with PDO 117/118 (duty %) and 121/122 (rpm) being reported per fan.

**There are no Modbus/RS-485, 0-10 V or dry-contact terminals on the main board.** The only bus on the
unit is ComfoNet (CAN). The `0-10V INPUT 1..4`, `ANALOG INPUT` and `BATHROOM SWITCH` menu items exist
only when an **Option Box** is connected (Service pp. 20–21, 25), and the standby switch mentioned in
the `SERVICE MODE` fault tree is "connected to the option box" (Service p. 42).

### EMC

Keep a barrier or **≥ 150 mm** between 230 V cabling and interference-sensitive cabling (control, low
voltage, interface, LAN, analogue/digital signal); where they must cross, cross them perpendicularly
(Installer p. 11). Relevant when routing an ESP32's CAN wiring inside the unit's cable tray.

---

## 12. Accessories and how they attach

Operating devices (Installer p. 18; User pp. 6–7):

| Device | Attachment |
| --- | --- |
| Unit display | on-board |
| ComfoSense C 55 / C 67 | directly via ComfoNet |
| ComfoSwitch C 55 / C 67 | directly via ComfoNet |
| ComfoControl App (Android/iOS) | requires an installed ComfoConnect LAN C |
| RFZ, Timer RF | wireless; require a connected ComfoSense C |
| Main sensor CO₂ RF | wireless; requires a connected ComfoSense C |
| Upgrade sensor CO₂ RF | requires an installed Main sensor CO₂ RF |
| Hygro sensor, CO₂ sensor | wired; require an Option Box to translate the signal |

Optional ancillaries (Installer p. 19; User p. 7):

| Device | Attachment / power |
| --- | --- |
| ComfoConnect LAN C | directly via ComfoNet; also a portable service-engineer version |
| ComfoConnect KNX C | directly via ComfoNet |
| ComfoCool Q600 | via ComfoNet; **needs its own power supply** |
| Option Box | via ComfoNet; own supply only if its accessories draw 230 V through it |
| ComfoFond-L Q (sub-soil heat exchanger) | via an Option Box, which then needs its own supply |
| ComfoAir Q pre-heater | fits inside the unit, connector `J400` |
| RF Repeater | on-site within RF range; own power supply |

**Everything reaches the unit over ComfoNet.** For this project that is the point: an ESP32 on the
CAN bus is electrically just another ComfoNet node, subject to the bus budget in §3 (400 mA @ 12 V,
max 4 non-powered devices).

RF pairing, if ever relevant: needs a ComfoSense C as master; enable `ADVANCED MODE > RF SETTINGS` on
the unit and `RF PAIRING` on the ComfoSense C, then hold the device's buttons ≥ 6 s (RFZ, Timer RF) or
≥ 8 s (CO₂ RF) inside a 10-minute registration window (Service pp. 15–16).

Firmware updates are done by a registered installer through a **ComfoConnect LAN C**, locally or over
Remote Support; re-commissioning is not required afterwards (Service p. 25; User p. 31).

---

## 13. Operating behaviour

### 13.1 Airflow presets and modes

- Four presets: **A (Away) / 1 (Low) / 2 (Middle) / 3 (High)** (User p. 9 icon N). ↔ PDO 65.
- Mode indicator: **AUTO** = airflow set by the scheduler, **MANUAL** = set by the user (User p. 9
  icon G). ↔ PDO 49 / PDO 56.
- **Temporary** change from the main screen reverts to AUTO "when the next step of the SCHEDULER
  VENTILATION starts or after a maximum of two hours" (User p. 12). ↔ PDO 81 counts this down. This
  two-hour rule is specific to setting an airflow with the up/down buttons on the main screen
  (User p. 12 §3.4.1) — **it does not govern every temporary change**: BOOST, AWAY, the fan-stop
  modes, the bypass override and the ComfoCool override all take an explicit user-set duration instead
  and are not bounded by two hours (User pp. 13–14, 18).
- **Permanent** change via `TASK MENU > AUTO/MANUAL > MANUAL`, with the warning that "demand control
  ventilation will be ignored or partially ignored in manual mode" (User p. 12) — the on-screen
  confirmation for this reads "DEMAND CONTROL VENTILATION WILL BE IGNORED OR PARTIALLY IGNORED IN
  MANUAL MODE" (User p. 12). Once active, "all set schedules will be ignored" (User p. 16).
- **BOOST** = PRESET 3 for a set duration (`TASK MENU > BOOST > TIMER`), cancellable with `BOOST > OFF`
  (User p. 13).
- **AWAY** = PRESET A until an explicit end date/time; "during this set time all schedulers will be
  ignored" (User p. 14). The screen reads `AWAY UNTIL YYYY/MM/DD HH:MM` (User p. 14) — the concrete
  form a firmware must parse to compute a seconds value from the unit's own clock.
- **Fan stop**: `TASK MENU > VENTILATION` → `SUPPLY ONLY` (stops extracting) or `EXTRACT ONLY` (stops
  supplying), for a set duration; back via `BALANCE` (User p. 13). ↔ PDO 70/71 with PDO 86/87 as the
  countdowns. The manual explicitly forbids using this to stop ventilation in a disaster. `EXTRACT
  ONLY` is conditional: the user menu itself prints "EXTRACT ONLY if you would like to stop supplying
  air **(if available)**" (User p. 13), and Service p. 24 §4.8.5 documents `FIRE PLACE PRESENT = NO`
  (the factory default) → "the unit will allow the function EXTRACT ONLY" — the manual prints only
  this `NO` row of the table, so the `YES` behaviour (presumably disallowing `EXTRACT ONLY`) is implied
  rather than stated. The confirmation screen is the cleanest statement of which fan each mode stops:
  "EXTRACT ONLY — SUPPLY FAN WILL BE TURNED OFF" (User p. 13).
- Scheduler overlap rule: "the shortest program is the leading program"; on equal length, "the program
  with the highest airflow PRESET" wins. With no schedule programmed, AUTO runs **PRESET 2**
  (User p. 16).

### 13.2 Bypass

- Three settings: **AUTO / ACTIVATED (minimise heat recovery) / DEACTIVATED (maximise heat recovery)**;
  the two manual settings require a duration and are temporary overrides (User p. 18). ↔ PDO 66 with
  PDO 82 as the countdown.
- `STATUS > BYPASS STATE` shows "the percentage of bypassed air" (User p. 19) — a **percentage, not a
  boolean**, matching PDO 227 (100 = fully open) and the "modulating bypass" hardware.
- "When the automated heat recovery control (bypass) is active the maximum air flow (rate) is
  restricted to avoid extra noise due to higher resistance in the system" (User p. 18) — this is the
  `− BYPASS` entry in `FLOW RESTRICTIONS` and the `Bypass` bit of PDO 230.
- Hardware: in-line modulating bypass, valve + actuator as separate service parts (#17, #10,
  Service p. 11); "the modulating by-pass regulates how much heat or cold is transmitted from extract
  to supply air" (Brochure p. 8).
- Failure mode worth knowing: the actuator spindle can fall out of the bypass actuator — there is a
  documented re-seating procedure (Service p. 44).

### 13.3 Temperature profile

Basic mode: **WARM / NORMAL (default) / COOL**, "the amount of heat recovery is controlled
automatically based on the set temperature profile", most noticeable in the shoulder seasons
(User p. 15). ↔ PDO 67.

Advanced mode adds `SET MODE` = **ADAPTIVE** or **FIXED** (User p. 24):

| Profile | FIXED default | ADAPTIVE |
| --- | --- | --- |
| WARM | 24 °C / 76 °F | NORMAL + 1.5 °C |
| NORMAL | 20 °C / 68 °F | — |
| COOL | 18 °C / 64 °F | NORMAL − 1.5 °C |

In ADAPTIVE mode "the unit uses the RMOT for determining the desired indoor temperature… The NORMAL
setting is **21.5 °C @ 0 °C RMOT**. For every °C increase in RMOT the temperature is adjusted by
**+0.11 °C**" (User p. 24). The resulting setpoint is PDO 212, and the three fixed values plus the
adaptive/fixed switch are RMI `TEMPHUMCONTROL` 0x1D subunit 01 properties 0x0A/0x0B/0x0C and 0x08 —
capability #2 in [`comfoair-pdo-rmi.md` §4](./comfoair-pdo-rmi.md).

### 13.4 Season detection

RMOT = "running mean outdoor temperature (average temperature over past five days)" (User pp. 20, 24).
↔ PDO 209, with PDO 210/211 as the heating/cooling season flags.

| Setting | Default |
| --- | --- |
| `HEATING LIMIT RMOT` | 11 °C / 51 °F |
| `COOLING LIMIT RMOT` | 20 °C / 68 °F |

Purpose: "to prevent the unit from counteracting with external heating… and cooling… units in the
dwelling" (User p. 24). `STARTS NOW` snaps the current RMOT as the new limit. `STATUS > SEASON
DETECTION` exposes SEASON, LIMIT RMOT HEAT, LIMIT RMOT COOL, CURRENT RMOT, and spells out the
direction of each limit: `LIMIT RMOT HEAT` is "the set RMOT **below** which the (central) heating
system is normally active"; `LIMIT RMOT COOL` is "the set RMOT **above** which the (central) cooling
system is normally active" (User p. 20) — this is what PDO 210/211 threshold against. The two limits
are RMI 0x1D/01 properties 0x02/0x03.

### 13.5 Sensor-based ventilation (advanced)

Each of the three controls takes **ON / AUTO ONLY / OFF** (User p. 23) — ON responds in both AUTO and
MANUAL mode, AUTO ONLY deactivates itself in manual mode:

| Control | Trigger | Default |
| --- | --- | --- |
| `TEMPERATURE PASSIVE` | ventilates on PRESET 3 when it is cooling season, indoor ≫ outdoor, bypass in AUTO, ComfoCool off, both fans running, ETA+SUP sensors healthy, unit not switched off in the last 2 min | OFF |
| `HUMIDITY COMFORT` | raises airflow when indoor absolute humidity exceeds the reference | OFF |
| `HUMIDITY PROTECTION` | ventilates on PRESET 3 when extract-air **RH > 75 %** | AUTO ONLY |

The manual is candid that this is a central sensor: "Using a local humidity sensor for the wet rooms
where the humidity is produced, is always faster and more reliable", and "Humidity protection works
for high humidity only. It doesn't protect the building from low humidity." (User p. 23). ↔ PDO 225,
written via `03 1D 01 04/06/07 <0|1|2>`.

### 13.6 Flow restrictions

`STATUS > FLOW RESTRICTIONS` appears only when an automated control is asking for a different airflow
than requested (User p. 19). This table is the human-readable form of **PDO 230**, decoded by
`util.calculate_airflow_constraints()` — capability #4 in
[`comfoair-pdo-rmi.md` §4](./comfoair-pdo-rmi.md):

| Direction | Reason | Meaning |
| --- | --- | --- |
| + | `HUMIDITY PROTECTION` | request from the humidity protection control |
| + | `HUMIDITY COMFORT` | request from the humidity comfort control |
| + | `TEMPERATURE PASSIVE` | request from the temperature passive control |
| + | `RF SENSOR` | request from an RF sensor |
| + | `0-10V INPUT` / `0-10V INPUT (1-4)` | request from an analogue input |
| + | `COMFOCOOL` | request from the ComfoCool Q600 |
| + | `PREHEATER` | request from the pre-heater |
| − | `RESISTANCE` | one or two fans are at maximum fan duty |
| − | `RESISTANCE GUARD` | the maximum allowed static pressure is reached |
| − | `NOISE GUARD` | the maximum allowed noise level for the preset is reached |
| − | `FROST PROTECTION` | the minimal air temperature is reached |
| − | `PREHEATER` | request from the pre-heater |
| − | `BYPASS` | request from the bypass |

"A request from an automatic safety control will always take precedent over other devices and
automatic controls" (User p. 31) — worth surfacing in the HA UI, since it is the documented reason a
commanded preset appears to be ignored.

**Bit-map cross-check.** The PDO 230 bit map the firmware decodes with
(`components/comfoair_protocol/include/airflow_constraints.h`) names more constraints than these four
PDFs document: bit 16 `Hood` and bits 47–54 `CO2ZoneX1..8` have **no counterpart in any of the four
PDFs** — searched for "hood", "cooker" and any CO₂-zone flow-restriction
entry and found none. They may belong to other models or later firmware. Conversely, the manual
corroborates one pair the table above does not mention: `RF SENSOR FUNCTION` = `FLOW PROPORTIONAL` /
`FLOW PRESET` (User p. 24) matches the bit map's two RF bits, even though the `STATUS > FLOW
RESTRICTIONS` display collapses both to `+ RF SENSOR`. For the record, since this file is where the
`+`/`−` table lives: the bit map's direction split falls at bits 2–10 (`−` group) and bits 12–27 and
47–54 (`+` group), with bit 45 only a validity marker.

### 13.7 Unbalance

`UNBALANCE`, default **0 %** — supply and extract balanced, compensating for differing duct
resistance. A positive setting creates over-pressure by reducing supply airflow by the set %; a
negative setting creates under-pressure by reducing extract airflow (Service p. 25). A positive
unbalance is **not available when `FIRE PLACE PRESENT` = YES** (Service pp. 24–25). The
`UNBALANCE ERROR` fires when the balance can no longer be guaranteed.

`FIRE PLACE PRESENT` (default NO) enables under-pressure protection when a fireplace exists, but the
manual is explicit that it is **not a certified safety device — a delta-pressure switch is still
required** (Service pp. 5, 24).

### 13.8 Ventilation control law

`VENTILATION CONTROLS` (Service p. 24):

- **`FLOW CONTROL`** — factory default. Regulates *around* the setpoint, correcting only when
  necessary and tolerating small short variations, "ensures a more steady behaviour of the fan speed".
- **`CONSTANT FLOW`** — regulates to the exact setpoint, correcting immediately; fan speed constantly
  adjusted.

`DIRECT FAN CONTROL` exists in the installer menu with the flat instruction **"Do not activate this
menu"** — it disables the ventilation-control/unbalance regulation between the two fans (Service p. 25).

### 13.9 ComfoCool Q600 (accessory)

Blocked from starting when any of these hold (User p. 31): forced off by its timer or scheduler; unit
in PRESET A; heating season; a unit fan turned off; **bypass not in AUTO**; delivered supply air less
than 3 °C hotter than requested; an existing ComfoCool error. Condenser 52–58 °C → the unit raises
airflow; **> 58 °C → `COMFOCOOL_HEAT ERROR`** and stop until reset (User p. 20).

---

## 14. Frost protection and the pre-heater

- The pre-heater is an **optional internal accessory**, standard in unit version **"VV"**
  (Service p. 10 pos. L), connected to `J400` (low-voltage/comms) and `J204` (mains).
- It costs airflow headroom: minimum airflow rises from **75 to 100 m³/h** when it is on
  (Installer/Service p. 6), and it appears in `FLOW RESTRICTIONS` in **both** directions (§13.6).
- It dominates the electrical rating: 180 W → 1850 W on a Q350 when it runs at −15 °C (§3).
- Its location is orientation-dependent and checked at commissioning; a mismatch raises
  `PREHEAT_LOCATION ERROR` (Service pp. 22, 40).
- Cleaning: turn it upside down, soft brush or vacuum only — **never wet-clean it** (Service p. 29).
- `STATUS > FROST PROTECTION` and `FROST PREHEATER` (the latter only with the accessory fitted) expose
  its state (User p. 19).
- `TEMP_SUP_MIN ERROR` is the frost-protection backstop: the supply fan is switched off for **≥ 1 hour**
  to prevent condensation; the fault tree checks outdoor temp < 0 °C, return air < 12 °C, extract fan
  running, and whether the bypass valve actually moved (Service pp. 42–43).
- Brochure framing: the pre-heater "adapts perfectly to the temperature, volume flow and air humidity
  of the outdoor air"; delta shape, negligible pressure loss (Brochure p. 8).
- Sensor plausibility windows used by the fault trees: `TEMP_SENSOR_*` resets if the temperature was
  within **−40 … +70 °C**; `TEMP_HRU` (unit switches off) if within **−30 … +120 °C**; `DANGER!
  OVERHEATING!` treats < −40 °C or > 70 °C as an extreme-temperature event needing damage inspection
  (Service pp. 38, 42).

---

## 15. Filters

| Fact | Value | Source |
| --- | --- | --- |
| Classes | outdoor air **F7**, extract air **G4** | Installer/Service p. 6 |
| Service set | Filter set G4/F7 (1×/1×), part #4 | Service p. 11 |
| Order number | **400502013** | User p. 29 |
| Maximum interval | **180 days** / "at least every six months" | Service p. 24; User pp. 5, 26, 28 |
| Default pre-warning | **21 days** before expected replacement, adjustable upward | Service p. 24 |
| Shortening | "If the unit is running at a high airflow rate then nominal, the unit will automatically shorten this time" | Service p. 24 |

Warning chain (User p. 28): LED flashes → display shows `EXPECT FILTER CHANGE SOON` (warning) →
`CHANGE FILTERS NOW` (error). `FILTERS > FILTER STATUS` shows days remaining, labelled on-screen
**"DAYS LEFT - INDICATIVE"** (User p. 28) — a useful qualifier for PDO 192. `SUPPRESS` postpones the
warning **by exactly one day**. ↔ PDO 192 (days remaining) and error codes 1079 / 1077.

`FILTERS > CHANGE FILTERS` starts the filter wizard at any time, which resets the counter on
completion — the documented path for scheduled contract replacement (Service p. 24). **"Do not change
the filters when the unit is powered without using the filter wizard"** — the unit stops ventilation
during replacement for safety (Service p. 24; User p. 29). Concretely: the wizard is six steps ending
`6/6 END OF THE FILTER WIZARD`; step 5 reads "Select CONFIRM to close the filter replacement
instructions and **start up the ventilation again**" (User p. 29) — that `CONFIRM` is what restarts
ventilation.

Note for the integration: RMI `FILTER` 0x1C/01 exposes lifetime (0x03) and pre-warning period (0x02)
as read/write, but **no filter-reset command is documented** — see capability #6 in
[`comfoair-pdo-rmi.md` §4](./comfoair-pdo-rmi.md). Don't promise a reset button on the strength of
these manuals either; the wizard is the only documented reset.

Brochure adds that the indicator "factors not only the elapsed time, but also the air volume
filtered" (Brochure p. 9), and that its own performance data is measured with **G4/G4**, not F7/G4
(Brochure p. 15 fn. 2).

---

## 16. Malfunction codes

Complete table, Service p. 33, for firmware **R1.6.0**. The middle column is the numeric code a
ComfoSense C displays. Whether this is the same numbering the ComfoConnect RMI / alarm path uses is
**unverified** — see capability #7 in [`comfoair-pdo-rmi.md` §4](./comfoair-pdo-rmi.md). Per-code
decision trees run Service pp. 37–43.

| Display code | ComfoSense C | Meaning |
| --- | --- | --- |
| `CCOOL_COMPRESSOR ERROR` | 1091 | The ComfoCool Q600 compressor has a malfunction |
| `CCOOL_CONNECT ERROR` | 1075 | No communication between the ComfoCool Q600 and the unit |
| `CCOOL_TEMP ERROR` | 1092, 1093, 1094 | One or more ComfoCool Q600 temperature sensors have a malfunction |
| `CHANGE FILTERS NOW` | 1077 | The internal filters need to be replaced |
| `COMFOCOOL_HEAT ERROR` | 1090 | The ComfoCool Q600 condenser is overheating |
| `CONFIGURATION ERROR` | 1099 | The configuration is not up to date |
| `DANGER! OVERHEATING!` | 1021 | Two or more sensors are detecting an incorrect temperature |
| `EXPECT FILTER CHANGE SOON` | 1079 | The internal filters almost need to be replaced |
| `EXT_PRESSURE_EHA ERROR` | 1053 | System resistance in the extract-exhaust airflow is too high |
| `EXT_PRESSURE_SUP ERROR` | 1054 | System resistance in the outdoor-supply airflow is too high |
| `EXTERNAL FILTER ALARM` | 1078 | The external filter is dirty |
| `FAN_EHA ERROR` | 1051, 1055 | The exhaust air fan has a malfunction |
| `FAN_SUP ERROR` | 1052, 1056 | The supply air fan has a malfunction |
| `GENERAL ERROR xxxxx` | xxxx | An error without description occurred; take note of the number |
| `GROUND_HEAT_CONNECT ERROR` | 1076 | No communication between the ComfoFond-L Q temperature sensor and the unit |
| `INIT ERROR` | 1033 | The unit has not been commissioned |
| `OPTION_BOX CONNECT ERROR` | 1067 | No communication between the option box and the unit |
| `PREHEAT ERROR` | 1037, 1038, 1059, 1081 | The pre-heater has a malfunction |
| `PREHEAT_LOCATION ERROR` | 1035 | The pre-heater is not in the correct location |
| `PREHEAT_PRES ERROR` | 1068 | No communication between the pre-heater and the unit |
| `SENSOR_EHA ERROR` | 1025, 1041, 1049 | The exhaust air sensor has a malfunction |
| `SENSOR_ETA ERROR` | 1023, 1039 | The extract air sensor has a malfunction |
| `SENSOR_ODA ERROR` | 1027, 1029, 1043, 1045 | The outdoor air sensor has a malfunction |
| `SENSOR_SUP ERROR` | 1031, 1047, 1050 | The supply air sensor has a malfunction |
| `SERVICE MODE` | 1080 | Basic functions stopped to enable maintenance |
| `TEMP_HRU ERROR` | 1022 | The outdoor air temperature is too high or low |
| `TEMP_SENSOR_EHA ERROR` | 1026 | The exhaust air temperature sensor reads an incorrect temperature |
| `TEMP_SENSOR_ETA ERROR` | 1024 | The extract air temperature sensor reads an incorrect temperature |
| `TEMP_SENSOR_ODA ERROR` | 1028, 1030 | The outdoor air temperature sensor reads an incorrect temperature |
| `TEMP_SENSOR_SUP ERROR` | 1032 | The supply air temperature sensor reads an incorrect temperature |
| `TEMP_SUP_MIN ERROR` | 1061 | The supply air temperature is too low |
| `UNBALANCE ERROR` | 1062 | The airflow balance (or unbalance setting) cannot be guaranteed |

Notes that matter for an integration:

- **`SERVICE MODE` is an alert, not just a state** (code 1080) and it presents as a *warning* — the LED
  flashes once per second, same as a filter warning (Service p. 19; User p. 8).
- The user-facing reset path is `RESET ERROR` → wait 2 minutes → if it returns, read
  `STATUS > ERRORS`, `UNIT > HRU TYPE` and the firmware version and call the installer (User p. 30).
- Installer-side resets are separate: `FACTORY DEFAULTS` (forces re-commissioning), `RESET ERRORS`
  (excludes peripheral errors), `RESET ANALYSIS` (clears STATUS data), `PERIPHERALS ERRORS` (clears
  errors for ancillaries no longer detected — the manual warns the unit then "forgets" the ancillary
  until redetected) (Service p. 25).
- `UNBALANCE ERROR` self-clears once the cause is fixed (Service p. 43).
- The **User manual contains no numeric code table** — only these display strings (User p. 30).

Faults with no display alert (Service pp. 44–45): display and fans dead (fuse), high supply temp in
summer (bypass disabled or wrong season limit), low supply temp in winter, bypass actuator spindle
fallen out, noise (whistling → seal air gap; slurping → fill the condensate drain), water leak, clock
not saved (BR1225 battery), and **fans varying extensively → require a stable supply of ≥ 225 V**,
using a stabiliser or a dedicated fuse-box group.

---

## 17. Documentation defects and contradictions

Recorded rather than silently corrected, in the style of
[`comfoair-pdo-rmi.md`](./comfoair-pdo-rmi.md).

1. **Preset table is labelled `l/s` but the values are m³/h.** Service p. 24 lists PRESET 3 = 315 l/s
   for a Q350 — that is 1134 m³/h on a unit rated 350 m³/h. Every value matches the m³/h envelope
   (PRESET A 70 ≈ the 75 m³/h minimum; PRESET 3 315 = 90 % of maximum). RMI `VENTILATIONCONFIG`
   reads these same properties as **INT16 m³/h**, which settles it. **Treat the table as m³/h.**
2. **Internal fuse amperage disagrees between manuals.** Installer p. 6 prints `F5015 (15A)` for the
   Q450/Q600; Service p. 6 prints `F5015 (16A)` for the same models. The part number is identical in
   both, so order by part number, not by the amperage in the table. Not resolvable from these documents.
3. **`ST`/`TR` footnote attachment is easy to misread.** The suffix table (Installer p. 5, Service p. 2)
   marks `ST¹⁵` and `TR¹⁶`, with footnote 15 = "Not available on the ComfoAir Q 350 and 450" and
   footnote 16 = "Not available on the ComfoAir Q 600". So **ST = Q600, TR = Q350/Q450**, confirmed
   independently by the brochure's model names (Brochure p. 14).
4. **Bypass connector designator.** Two independent readings of the same wiring diagram produced
   `J800`/`J801` and `J302`/`J801`. Re-rendered at 500 dpi and read directly: the left-column bypass
   connector is **`J800`**, the right-column one **`J801`**. There is no `J302` on the diagram.
5. **Wire colour `BL` on the pre-heater line.** `J400`'s second wire is printed `BL`, drawn black,
   while the diagram's own legend defines only `BK` for brown/black. Printed as-is, presumably a typo
   (Installer p. 10 / Service p. 12).
6. **Dutch leftovers in the English edition.** The General block prints temperature ranges as
   "−40 °C **tot** +60 °C" (Installer/Service p. 6).
7. **The 2024 brochure contradicts the 2018 manuals by omission.** It names no ERV/enthalpy variant,
   no left/right SKUs, no ComfoCool, no post-heater, and no article numbers; it also renames the
   connectivity line to *ComfoConnect Pro* / *ComfoConnect Pro KNX* and introduces *ComfoClime*
   (Brochure pp. 11, 13, 18). It gives efficiency as a single marketing "up to 94 %" figure footnoted
   to the **Q350 only** per EU Reg. 1254/2014 (Brochure pp. 5, 8) — against the manuals' measured
   92 % / 90 % / 89 % per EN 13141-7:2010. **Use the manuals' figures.**
8. **Brochure dimension drawings were read from a compressed vector schematic.** They agree with the
   Installer manual's drawings, which should be preferred for anything dimensional.
9. **The claim that the ComfoSense C uses the ComfoConnect protocol's own numbering was unsupported.**
   An earlier draft of §16 asserted the malfunction table's numeric code column is "the same
   1000-range numbering the ComfoConnect protocol uses". Nothing in the four PDFs says this, and it
   sits awkwardly against `comfoair-pdo-rmi.md`'s own description of the ComfoConnect alarm-path code
   table as "~90-entry" with "numbering differs above 69" — small integers, not a 1000-range scheme.
   §16 now states the relationship is unverified. **Do not assume the two numbering schemes are the
   same without independent confirmation.**
10. **This file's own §3 previously cited PDO 144 where PDO 146 was meant.** An earlier draft of the
    pre-heater-power paragraph in §3 said the instantaneous pre-heater wattage is "separately exposed
    as PDO 144" — PDO 144 is actually the pre-heater's kWh **year** counter; the instantaneous power
    is PDO 146. Corrected in this revision (2026-09-01); a reader working from an older copy of this
    file should re-check any code built against that paragraph.

---

## 18. Commissioning and installer settings

Installer password: **4210** (Service pp. 14, 19, 22). Login: hold `SHIFT` for 4 s. Both the password
session and `SERVICE MODE` clear automatically on a power cycle (Service pp. 19, 25).

**Part 1 — basic configuration** (Service p. 22). Runs automatically on first power-up and *must* be
completed immediately "to protect the unit from frost and water damage" (Installer p. 13):
language → clock → continue → password → **orientation LEFT/RIGHT** → condensate-drain connection
(the "no drain" option only with an enthalpy exchanger) → filter position check → pre-heater position
check → flow unit (m³/h or l/s) → **altitude** (default 0–500 m, feeds the airflow calculation) →
fireplace present (default NO).

**Part 2 — balancing** (Service p. 23): `MAXIMUM FLOW` test with all valves open and windows/doors
closed → set MEDIUM/MAXIMUM/LOW/AWAY targets for presets 2/3/1/A → pick a `COMMISSIONING AIR FLOW` →
fine-tune every valve with an external flow meter **starting from the valve farthest from the unit** →
`CHECK AIRFLOW` (warns, overridably, if the target cannot be reached) → store.

Installer menu tree (Service pp. 20–21): `COMMISSIONING`, `MAIN BOARD SETTINGS` (→ `VENTILATION
PRESETS`, `VENTILATION CONTROL`, `FILTER SETTINGS`, `ALTITUDE`, `FIRE PLACE PRESENT`, `UNBALANCE`,
`HEAT EXCHANGER TYPE`, `DIRECT FAN CONTROL`, `SERVICE MODE`), `OPTION BOX SETTINGS`, `LOG OUT`,
`RESET`, `BATHROOM SWITCH`, `0-10V INPUT 1..4`, `0-10V FUNCTION`, `0-10V PRIORITY` — the last four
only with an Option Box.

User menu tree: User p. 10 (full diagram). `STATUS` is read-only; everything settable is under
`TASK MENU`, with `ADVANCED SETTINGS` visible only in advanced mode. Conditional items are flagged in
the manual's own footnotes: ¹ only when errors occur, ² only in advanced mode, ³ only when the
ancillary is connected, ⁴ only when an automated control wants a different airflow, ⁸ only with RF.

Display behaviour (Service pp. 17–19; User pp. 8, 11): auto-off after **15 minutes**; wakes on opening
the visor or any key. Status LED — **on** = operating correctly, **off** = no power or display in use,
**slow flash (1 Hz)** = warning (filters or SERVICE MODE), **rapid flash (4 Hz)** = error. Child lock
= hold `MENU` for 4 s on the main screen.

---

## 19. Maintenance intervals

User, every 6 months (User p. 26): replace filters · clean valves · clean grilles · clean the
operating device · fill the condensate drain of the waste-water system. Cleaning the operating device
is dry only — "Do not use water or any other liquid."

Installer / service engineer, **every 4 years** (Service pp. 27–30): casing · heat exchanger · fans ·
modulating bypass valves · pre-heater · condensate drain · air ducts. Duct cleaning specifically:
**exhaust duct every 4 years, supply duct every 8 years**; loosen the dirt and vacuum it out — never
use the unit itself to extract duct dirt (Service p. 30).

Cleaning constraints (Service pp. 27–29):

- **Heat exchanger** — submerge several times in hot water **max 40 °C**, rinse with clean hot tap
  water max 40 °C, shake off; no aggressive cleaners or solvents (they damage the air seal). Refit with
  the red bottom plate facing front.
- **Casing** — no soap on the front foam/EPP parts; it destroys air- and water-tightness.
- **Pre-heater** — dry cleaning only.
- **Fans** — soft brush plus vacuum, without damaging the impellers.

Valve cleaning has a rule the integration should never fight: mark each valve's position and setting,
**do not change the setting and do not swap valves** — either "would negatively influence the system
performance" (User p. 27). The commissioning balance lives in those valve positions, not in the unit.

The User manual ships a maintenance log for years 1–14 (User pp. 33–35) and a blank commissioning /
test report with per-room required-vs-tested airflow tables and press/suction pressure in Pa
(User p. 32) — the latter is the document to ask an owner for when their measured flows look wrong.

---

## 20. Quick page index

| Looking for | Go to |
| --- | --- |
| Type-plate suffixes | Installer p. 5 · Service p. 2 |
| Full technical data table | Installer p. 6 · Service p. 6 |
| Fan curves per model | Service pp. 7–9 |
| Airflow / pressure / sound / SFP tables | Brochure p. 15 |
| Unit overview, labelled parts | Service p. 10 · Installer p. 7 |
| Exploded service-parts view | Service p. 11 |
| **Main-board wiring diagram** | Service p. 12 = Installer p. 10 |
| Dimension drawings | Installer pp. 8–9 · Brochure p. 14 |
| R↔L conversion procedure | Installer p. 12 |
| Wall / floor mounting | Installer pp. 13–14 |
| Condensate drain | Installer pp. 14–15 |
| Duct rules, ODA/SUP/ETA/EHA | Installer pp. 15–16 |
| Valve / grille spacing | Installer p. 17 |
| Accessory tables | Installer pp. 18–19 · User pp. 6–7 |
| Display icons and LED | User pp. 8–9 · Service pp. 17–19 |
| Full menu tree | User p. 10 · Service pp. 20–21 |
| Modes, BOOST, AWAY, fan stop | User pp. 12–14 |
| Temperature profile, scheduler, bypass | User pp. 15–18 |
| STATUS menu contents | User pp. 19–20 |
| Advanced settings, season detection | User pp. 23–24 |
| Filters | User pp. 28–29 · Service p. 24 |
| Maintenance procedures | Service pp. 26–31 · User p. 26 |
| **Malfunction code table** | Service p. 33 |
| Per-code decision trees | Service pp. 37–43 |
| Faults with no display alert | Service pp. 44–45 |
| Commissioning wizard | Service pp. 22–23 |
| Installer settings menus | Service pp. 24–25 |
