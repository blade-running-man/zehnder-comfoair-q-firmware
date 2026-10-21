# ESP32 nodes and CAN transceivers — canonical hardware reference

**Compiled 2026-09-01** from the four M5Stack PDFs in [`docs/manuals/`](../manuals/). Every fact below
carries a page citation into one of them. Nothing here is inferred from general knowledge; where a
figure was read off a photograph, silkscreen or schematic rather than a text layer it is marked
*(read from artwork)*, where a conclusion is derived by combining two documents it is marked
**[derived]**, and where the documents are silent or disagree the gap is recorded in §11–§12 rather
than silently filled.

Companion documents:

- [`comfoair-q-hardware.md`](./comfoair-q-hardware.md) — the **ventilation unit** side: what the
  ComfoNet bus physically is, its connectors, colour code and power budget. This file is the
  **ESP32 node** side: what hangs off the other end of that cable.
- [`comfoair-pdo-rmi.md`](./comfoair-pdo-rmi.md) — the protocol source of truth (PDO table, RMI frames).
- `components/board/board_m5stack_nanoc6.c` — this firmware's board descriptor, the one place the
  facts below become GPIO numbers; `platformio.ini` holds the build environment.

---

## 1. Source documents

| Short cite | File | Pages | Printed identifier |
| --- | --- | --- | --- |
| **AtomS3** | `M5AtomS3.pdf` | 7 | `AtomS3`, `SKU:C123`, footer `Update Time: 2026-08-12` |
| **NanoC6** | `M5NanoC6.pdf` | 8 | `NanoC6`, `SKU:C125`, footer `Update Time: 2026-07-07` |
| **MiniCAN** | `M5Unit-Mini_CAN.pdf` | 5 | `Unit Mini CAN`, `SKU:U179`, footer `Update Time: 2026-05-06` |
| **AtomicCAN** | `M5AtomicCAN_Base.pdf` | 5 | `Atomic CAN Base`, `SKU:A103`, footer `Update Time: 2026-04-28` |

All four are browser prints of `docs.m5stack.com` product pages. **Their text layers are thin
(3–7 KB each) and carry almost none of the pin information** — the pin maps, silkscreens and full
schematics are images. Every pin fact below was read by rendering pages with
`pdftoppm -r 300…900` and reading the crops directly.

Linked hardware revisions (from the PDFs' own link targets): AtomS3 schematic `Sch_M5_AtomS3_v1.0.pdf`
(AtomS3 p. 4), NanoC6 schematic `Sch_M5NanoC6_v0.0.1.pdf` (NanoC6 p. 5), Mini CAN schematic
`SCH_UNIT_Mini_CAN_V1.0.pdf` (MiniCAN p. 2).

**Not covered by any PDF in this repo:** the M5Stack ATOM Lite and the Olimex ESP32-EVB, two other
ESP32 boards a CAN node can be built on. See §12.

---

## 2. The two supported node stacks

This project puts an ESP32 on the ComfoNet CAN bus as just another node. That needs an MCU board
plus a CAN transceiver, and the four PDFs describe two mutually exclusive ways of pairing them:

| | **Stack A — Grove** | **Stack B — stacking base** |
| --- | --- | --- |
| Transceiver | Unit Mini CAN (U179), TJA1051T/3 | Atomic CAN Base (A103), CA-IS3050G |
| Attaches by | HY2.0-4P Grove cable, 20 cm | ATOM bottom stacking headers |
| Host boards | AtomS3, NanoC6 (any board with a Grove port) | ATOM-family only |
| Host GPIOs | **TX = G2, RX = G1** on both AtomS3 and NanoC6 | **TX = G22, RX = G19** on an ATOM Lite |
| Galvanic isolation | **none** (MiniCAN p. 5) | **yes**, B0505S-1W + CA-IS3050G (AtomicCAN p. 1, p. 3) |
| 120 Ω terminator | **permanently fitted** (MiniCAN p. 3) | **none fitted** (AtomicCAN p. 3) |
| Can take 12 V from ComfoNet | **yes**, `HV` input 9–24 V (MiniCAN p. 1, p. 2) | **no** — no power input at all (AtomicCAN p. 3) |
| Bus-side connector | HT3.96-4P: GND, HV, CAN_L, CAN_H | HT3.96-4P: CAN_H, CAN_L, NC, GND |

This firmware's only board, the NanoC6, uses **Stack A**: CAN TX = G2, RX = G1 on the Grove port
(`components/board/board_m5stack_nanoc6.c`). On an ATOM Lite, Stack A means the ATOM's Grove pair
G26/G32 instead. Nothing in this firmware uses Stack B.

---

## 3. M5Stack AtomS3 (C123)

> **Read this first.** The PDF in `docs/manuals/` is the **AtomS3 with the 0.85″ LCD (C123)**. It
> never mentions "AtomS3 Lite", "AtomS3U" or any variant (AtomS3 pp. 1–7). The AtomS3 variant paired
> with a CAN unit here has an RGB LED and a button and no LCD — the AtomS3 **Lite**, a different SKU
> whose datasheet is not in this repo. Facts that carry across
> the two are marked; facts that may not are flagged in §11.

### SoC and memory

| Item | Value | Cite |
| --- | --- | --- |
| SoC | ESP32-S3FN8, Xtensa LX7 dual-core, 240 MHz | AtomS3 p. 3 |
| Flash | **8 MB** ("8 MB on-chip Flash") | AtomS3 p. 1, p. 3 |
| PSRAM | **never mentioned** — neither confirmed nor denied | §12 |
| Crystal | X1 40 MHz *(read from artwork)* | AtomS3 p. 4 |
| Antenna | ANT1 PROANT440 on-board; IPEX footprint J1 present but fed through **R2 = NC**, i.e. the external-antenna path is depopulated *(read from artwork)* | AtomS3 p. 4 |
| Wireless detail | only the bare word "Wi-Fi". **No 802.11 standard, no band, and Bluetooth/BLE is never mentioned at all** | AtomS3 p. 1; §12 |

### Grove port — the CAN port

| Position | Wire colour | Silkscreen | Signal |
| --- | --- | --- | --- |
| 1 | Black | `G` | GND |
| 2 | Red | `5V` | VIN_5V |
| 3 | **Yellow** | `G2` | **GPIO2** |
| 4 | **White** | `G1` | **GPIO1** |

Colour table: AtomS3 p. 6 (row labelled `PORT.CUSTOM`). Silkscreen: AtomS3 pp. 1, 5
*(read from artwork)*. Schematic `J6` (`GH2.0-4P`): PIN1 = GPIO1, PIN2 = GPIO2, PIN3 = VIN_5V,
PIN4 = GND, with `D5`/`D8` ESD5Z3V3 clamps on both signal lines (AtomS3 p. 4) *(read from artwork)*.
The infographic calls the same port `PORT.A` and annotates it `I2C/GPIO/ADC/UART` (AtomS3 p. 1).

### Full pin map

| GPIO | Wired to | Cite |
| --- | --- | --- |
| G0 | BOOT circuit — driven by the `GS321` comparator output, shared with `LED1 (GREEN)`. Holding RESET ≈2 s pulls it LOW. **Not free.** | p. 4 *(artwork)*, p. 3 |
| **G1** | Grove white — free | p. 6 |
| **G2** | Grove yellow — free | p. 6 |
| G3 | 10 K pull-up to 3V3, not brought out | p. 4 *(artwork)* |
| G4 | IR transmit LED, 22 R series to GND | p. 4, p. 5 *(artwork)* |
| G5, G6, G7, G8 | Bottom-left 1×5 header `J3` (pin 1 = 3V3) — free | p. 3, p. 4 *(artwork)* |
| G9–G14 | Unrouted stubs | p. 4 *(artwork)* |
| G15 / G16 / G17 / G21 / G33 / G34 | LCD: CS / backlight-enable / SCK / MOSI / RS / RST | p. 6 |
| G19 / G20 | Native USB D− / D+ | p. 4 *(artwork)* |
| **G35** | `SK_DIN` → `U3` **WS2812** RGB LED, pin 3 (DI). `SK_DOUT` goes nowhere. | p. 4 *(artwork)* — see §11.1 |
| G38 / G39 | I²C SDA / SCL to the MPU6886 (0x68), **also** on the bottom-right 1×4 header `J4`; 10 K pull-ups sit on the IMU sub-board. "The I2C pins on the back share the same bus" | p. 5, p. 6 *(artwork)* |
| **G41** | User button `S1` to GND, **`R4` 10 K pull-up to 3V3** → active LOW, external pull-up present | p. 4, p. 5 *(artwork)*, silkscreen `BTN: G41` |
| G43 / G44 | `U0TXD` / `U0RXD`, routed only to solder jumpers JP1/JP2 — not to USB | p. 4 *(artwork)*; GPIO numbers not printed |
| G45 / G46 | Marked NC | p. 4 *(artwork)* |

Free for the taking, per the PDF's own spec row: **`G5/G6/G7/G8/G38/G39`** (AtomS3 p. 3), plus
G1/G2 on the Grove port.

### Power and flashing

- 5 V in over **USB Type-C only**; `F1` 6 V/1 A PPTC in series with VBUS; 5.1 K CC resistors
  (sink role). `U4` **SY8089** buck makes 3.3 V (AtomS3 p. 3, p. 4 *(read from artwork)*).
- **No battery, no charger** (§12). No power-consumption figure is given anywhere.
- **Native USB, no UART bridge IC** — `USB_D_P` = G20, `USB_D_N` = G19 straight to the Type-C
  connector; no CH9102/CP210x/CH340 appears in the schematic (AtomS3 p. 4) *(read from artwork)*.
  Consequence for firmware: the USB serial console is USB-CDC, not a bridge.
- Download mode: "press and hold the reset button (about 2 seconds) until the internal green LED
  lights up, then release" (AtomS3 p. 3); silkscreen `RST↻ Hold 2S -> G0=0`.
- The vendor's own PlatformIO reference environment (AtomS3 p. 7):

  ```ini
  [env:m5stack-atoms3]
  board = esp32-s3-devkitc-1
  build_flags = -DARDUINO_USB_CDC_ON_BOOT=1 -DARDUINO_USB_MODE=1
  monitor_speed = 115200
  ```

  — a generic devkit id with the AtomS3's variant (`esp32s3`) and flash (8 MB), so it fits.

### Physical

24.0 × 24.0 × 12.9 mm, 6.9 g (AtomS3 p. 3; three other height figures exist, §11.4). One **M2**
mounting hole on the back (AtomS3 pp. 1, 5) *(read from artwork)*. Operating temperature 0–40 °C
(AtomS3 p. 3) — note the ComfoAir Q's own installation-area range is 0–45 °C
([`comfoair-q-hardware.md` §3](./comfoair-q-hardware.md)), so the ESP32 is the tighter constraint.

---

## 4. M5Stack NanoC6 (C125)

### SoC and radios

| Item | Value | Cite |
| --- | --- | --- |
| SoC | **ESP32-C6FH4**, RISC-V **single-core**, 160 MHz, "supports CDC" | NanoC6 p. 4 |
| Flash | **4 MB** | NanoC6 p. 4 |
| Radios | Wi-Fi 6 (802.11ax, 2.4 GHz, backward compatible b/g/n) · **Zigbee 3.0** · **Thread 1.3** · **Matter** | NanoC6 p. 4, p. 2 |
| Bluetooth / BLE | **never mentioned anywhere** — not in the features, not in the spec table, not among the four radio badges on the packaging | §12 |
| PSRAM | never mentioned | §12 |
| Antenna | `ANT1` **ANT016008LCS2442MA2** ceramic chip antenna. **No U.FL/IPEX footprint, no external-antenna option** *(read from artwork)* | NanoC6 p. 5 |

The four radios matter because this firmware's NanoC6 build leaves `CONFIG_OPENTHREAD_ENABLED` unset
— a deliberate trade of a headline feature of this board for flash and RAM, not an accident.

### Grove port — the CAN port

| Position | Wire colour | Silkscreen | Signal |
| --- | --- | --- | --- |
| 1 | Black | `G` | GND |
| 2 | Red | `5V` | **VBUS, straight from USB, post-fuse** |
| 3 | **Yellow** | `G2` | **GPIO2** |
| 4 | **White** | `G1` | **GPIO1** |

Colour table: NanoC6 p. 6 (`PORT.CUSTOM`). Schematic `J1` (`HY-2.0`): pin1 = IO1/GPIO1,
pin2 = IO2/GPIO2, pin3 = +5V, pin4 = GND, ESD diodes `D4`/`D5`/`D6` (NanoC6 p. 5)
*(read from artwork)*. **There is no 5 V regulator on this board** — the Grove 5 V pin is USB VBUS
after the `F1` 6 V/1 A PPTC, so it is an input as readily as an output (NanoC6 p. 5)
*(read from artwork)*. Grove maximum output: **DC 5 V @ 600 mA, "depends on USB power supply
capacity"** (NanoC6 p. 4).

### On-board peripherals — the exact pins

| Function | GPIO | Detail | Cite |
| --- | --- | --- | --- |
| IR transmitter | **G3** | `IR1` anode on G3, cathode → `R1` 22 R → GND ⇒ **active HIGH** | p. 6, p. 5 *(artwork)* |
| RGB LED data | **G20** | `U2` **WS2812**, `DI` on G20 | p. 6, p. 1, p. 5 |
| **RGB power enable** | **G19** | `RGBPWR` → `EN` of load switch `U6` AW35122FDR. Doc's own words: *"When using the RGB function, set pin G19 to a high level to enable power supply."* Active HIGH. | **p. 6** |
| Blue status LED | **G7** | `LED1` anode on G7, cathode → `R9` 22 R → GND ⇒ **active HIGH** | p. 6, p. 5 *(artwork)* |
| Button | **G9** | `S1` to GND, `R2` 10 K pull-up to 3V3 ⇒ **active LOW**. Also the download-mode strap. | p. 6, p. 5 *(artwork)* |

**RGB LED quantity: exactly ONE.** The schematic block `RGB LED` contains a single `U2 WS2812`
whose `DO` pin is drawn with a red cross = not connected, so there is no daisy chain
(NanoC6 p. 5) *(read from artwork)*. The marketing prose on p. 2 says "programmable RGB **LEDs**"
in the plural and the spec row on p. 4 gives no count — the schematic is the only quantitative
source. The WS2812 is fed from the **switched 3.3 V** rail `F3V3`, not 5 V (NanoC6 p. 5)
*(read from artwork)*.

### Pins that are not available

Only **G1 and G2 leave the board** as user I/O, via Grove (NanoC6 p. 6). G12/G13 are native USB D−/D+;
G16/G17 (U0TXD/U0RXD) go only to test pads `JP2`/`JP3`; G8 carries a lone 10 K pull-up and is
otherwise unconnected; **G0, G4, G5, G6, G14, G15, G18, G21, G22, G23 are unrouted** (NanoC6 p. 5)
*(read from artwork)*.

### Power and flashing

- USB Type-C or the Grove +5 V pin (NanoC6 p. 4). `U3` **BL8075CB5TR33** LDO makes 3.3 V; no rating
  is printed (NanoC6 p. 5) *(read from artwork)*.
- Measured currents (NanoC6 p. 4): operating in Wi-Fi mode **106.2 mA @ 5 V**; deep sleep 125.5 µA
  (USB) / 50 µA (Grove); ULP 252 µA / 201.5 µA. **These are the only power figures in any of the four
  PDFs** and they are what makes a ComfoNet-powered node budgetable — see §8.
- **Native USB, no bridge chip** (NanoC6 p. 5) *(read from artwork)*; the SoC "supports CDC"
  (NanoC6 p. 4).
- Download mode: **"hold down the GPIO9 button and then connect the data cable"** (NanoC6 p. 5).
- Vendor PlatformIO reference (NanoC6 p. 8): `[env:m5stack-nanoc6]`, **`board = esp32-c6-devkitc-1`**,
  `-D ARDUINO_USB_MODE=1`, `-DARDUINO_USB_CDC_ON_BOOT=1`. This firmware uses `devkit**m**-1`
  (`platformio.ini`), and that is right: PlatformIO's `esp32-c6-devkitc-1` is the **8 MB** C6 and
  `esp32-c6-devkitm-1` the **4 MB** one, and the NanoC6 has 4 MB (NanoC6 p. 4). There is no
  `m5stack-nanoc6` board id. Do not "fix" it to match the vendor page: it would declare 8 MB of
  flash on a 4 MB part.

### Physical

23.5 × 12.0 × 9.5 mm, 2.7 g (NanoC6 p. 4; the packaging says 2.5 g, §11.5). **No mounting holes at
all** in the 5-view mechanical drawing (NanoC6 p. 7) *(read from artwork)*. Operating temperature
0–40 °C (NanoC6 p. 4).

---

## 5. Unit Mini CAN (U179) — the Grove transceiver

| Item | Value | Cite |
| --- | --- | --- |
| Transceiver | **TJA1051T/3** (`U3`) | p. 1, p. 5, p. 3 *(artwork)* |
| Standards | ISO 11898-2:2016; SAE J2284-1…5 | p. 2 |
| Rate | "Up to 1 Mbit/s" — **no minimum stated**, nothing about 50 kbit/s | p. 2, p. 5 |
| Isolation | **Non-isolated** | p. 5 |
| Logic levels | `VIO` fed from 3.3 V, `VCC` from 5 V ⇒ 3.3 V TXD/RXD *(read from artwork)* | p. 3 |
| Silent/standby pin | `S` (pin 8) hard-tied to GND ⇒ permanently in normal mode; no GPIO needed *(read from artwork)* | p. 3 |
| Operating current | 4.02 mA @ 5 V; 2 mA @ 24 V. Standby 3.09 mA @ 5 V | p. 2 |
| Size / weight | 32.0 × 24.0 × 10.2 mm, 8.0 g; Grove cable 20 cm included | p. 2 |

### Grove side

| Position | Wire colour | Pin-map name (p. 3) | Silkscreen (p. 1) | TJA1051T/3 pin |
| --- | --- | --- | --- | --- |
| 1 | Black | `GND` | `G` | 2 (GND) |
| 2 | Red | `5V` | `V` | 3 (VCC) |
| 3 | **Yellow** | **`CAN_TX`** | `T` / `TXD` | **1 (TXD — an input to the module)** |
| 4 | **White** | **`CAN_RX`** | `R` / `RXD` | **4 (RXD — an output from the module)** |

Three independent sources in the PDF agree — the pin-map table (p. 3), the silkscreen (p. 1) and the
schematic's drawn direction arrows (p. 3) *(read from artwork)*. **There is no crossover:** the pin
labelled `TXD` must be driven by the host's transmit pin.

**[derived] Combining this table with §3/§4:** a straight-through Grove cable joins yellow to yellow
and white to white. Yellow is `G2` on both the AtomS3 (AtomS3 p. 6) and the NanoC6 (NanoC6 p. 6), and
yellow is the module's `TXD`. Therefore for **both** host boards:

```
tx_pin = GPIO2      rx_pin = GPIO1
```

Neither PDF states a GPIO number for the other — the Mini CAN datasheet prints **no host GPIO for any
board** and never mentions AtomS3 Lite or NanoC6 at all (MiniCAN p. 4, §12) — but the wire colours
align across the two documents, which settles it without appeal to outside knowledge.

### Bus side — HT3.96-4P terminal

| Pole | Net | Silkscreen |
| --- | --- | --- |
| 1 | `GND` | `G` / `GND` |
| 2 | **`HPWR+`** | **`HV`** |
| 3 | `CAN_L` | `L` |
| 4 | `CAN_H` | `H` |

Schematic `JP1` (MiniCAN p. 3) *(read from artwork)*. The two printed faces of the case show this
strip in **mirrored** left-to-right order (`H L HV G` on one, `GND HV L H` on the other) — identify
the poles from the face you are actually looking at, never by counting from a photo of the other side
(§11.6).

### `HV` — the 12 V input, and its side effect

- **9–24 V DC input**, feeding an on-board buck converter: *"HT3.96 terminal wide voltage input range
  (9-24V)"* (p. 1); *"Wide Voltage Input — HT3.96 terminal: 9-24V / Grove: 5V"* (p. 2);
  hero-image callout *"HV->5V DC/DC INCLUDED"* (p. 1) *(read from artwork)*.
- Chain: `HPWR+` → `U1` **ME3116** buck → Schottky `D4` → net **`VCC_5V`**, which **is the same net as
  the Grove connector's 5 V pin** (p. 3) *(read from artwork)*. So driving `HV` from ComfoNet's 12 V
  **back-powers the host board through the Grove cable**. That is what the spec row
  `Load Capacity — DC 5V/Max: 700mA` (p. 2) is describing.
- The document never says this in words, never warns about it, and never states whether `HV` and
  Grove 5 V may be energised at the same time (§11.7, §12).

### Termination — permanently fitted

**`R1 120R` sits directly across `CAN_H`/`CAN_L` with no jumper, no solder bridge, no DIP switch and
no cuttable trace** (MiniCAN p. 3) *(read from artwork)*. The words "termination", "120" and "jumper"
appear nowhere in the PDF's text on any page. See §9.

Protection: `D3` SMF30CA TVS on `HV`, `D1` LESD3Z5.0CMT1G on 5 V, `DZ1`/`DZ2` PESD5V0L1BA on CAN_H and
CAN_L. **No common-mode choke** (MiniCAN p. 3) *(read from artwork)*.

---

## 6. Atomic CAN Base (A103) — the stacking transceiver

| Item | Value | Cite |
| --- | --- | --- |
| Transceiver | **CA-IS3050G** (`U4`), isolated | p. 1, p. 2 |
| Rate / nodes | up to **1 Mbps**, 110 nodes, loop delay 150 ns, common-mode −12…+12 V | p. 1, p. 2 |
| **Isolation** | **galvanic** — `U2` **B0505S-1W** isolated DC-DC plus the transceiver's own barrier; logic `GND` and bus `SGND` are separate nets *(read from artwork)*. **No withstand-voltage rating is given.** | p. 1, p. 3 |
| External port | HT3.96-4P screw terminal (`J1`), one plug in the box | p. 2, p. 3 |
| Size / weight | 24 × 48 × 18 mm, 11.1 g; 0–40 °C | p. 2 |

### Pin map to the ATOM

Three statements exist and agree positionally, but **only the PCB photograph carries GPIO numbers**
(§11.2):

| Header position | Silkscreen (p. 1) | Schematic net (p. 3) | PinMap label (p. 3) | CA-IS3050G pin |
| --- | --- | --- | --- | --- |
| 5-pin #1 | `3V3` | `+3.3V` | `3V3` | VCC1 |
| 5-pin #2 | **`22 / TX`** | `CAN_TX` | `UART_TX` | **TXD (pin 3, input)** |
| 5-pin #3 | **`19 / RX`** | `CAN_RX` | `UART_RX` | **RXD (pin 2, output)** |
| 5-pin #4 | `23` | unconnected | — | — |
| 5-pin #5 | `33` | unconnected | — | — |
| 4-pin #1 | `21` | no-connect ✕ | — | — |
| 4-pin #2 | `25` | no-connect ✕ | — | — |
| 4-pin #3 | `5V` | `+5VIN` | `5V` | → B0505S-1W `+Vin` |
| 4-pin #4 | `GND` | `GND` | `GND` | GND1 |

**On an ATOM Lite this reads `tx_pin = GPIO22`, `rx_pin = GPIO19`** — the six exposed pins
(19, 21, 22, 23, 25, 33) are the ATOM's stacking-header signals, and the labels are written from the
controller's perspective onto transceiver pins named the same way, so there is no crossover.
The PinMap table's `UART_TX`/`UART_RX` naming is a docs-page copy-paste error: the board has no UART
peripheral and the CA-IS3050G's only logic pins are TXD/RXD (§11.3).

### Bus side

| Pole | Net | Case label |
| --- | --- | --- |
| 1 | `CAN_H` | `H` |
| 2 | `CAN_L` | `L` |
| 3 | **`NC`** | `✕` |
| 4 | `GND` (tied to the `SHIELD` symbol) | `G` |

(AtomicCAN p. 1, p. 3) *(read from artwork)*. **There is no 12 V pole and no power input of any
kind on this base.** Both its rails (`+3.3V`, `+5VIN`) come *from* the ATOM through the stacking
headers, and `+5VIN` only feeds the 1 W isolated DC-DC that powers the bus side (AtomicCAN p. 3)
*(read from artwork)*. A node built this way must be powered by USB.

### Termination — none fitted

**No 120 Ω terminator exists on this base, and no jumper or solder bridge to add one.** The complete
set of parts touching the bus lines is protective: `TSS1/2/3` P0080TA, `GDT1/2` SMD4532-075,
`DZ1/DZ2` PESD5V0L1BA, `R4` **1 MΩ** + `C10` 1 nF bleed, and **`F1`/`F2` 100 mA resettable fuses in
series with CAN_H and CAN_L** (AtomicCAN p. 3) *(read from artwork)*. The PDF never uses the word
"termination" at all. See §9.

### **[derived]** If this base is used with an AtomS3 instead of an ATOM Lite

The base's silkscreen carries **classic-ESP32** GPIO numbers, which do not exist on an ESP32-S3.
Aligning the two datasheets by physical header position — both 5-pin headers start with `3V3`
(AtomicCAN p. 1; AtomS3 p. 4 `J3` = 3V3, G5, G6, G7, G8) and both 4-pin headers end with `5V, GND`
(AtomicCAN p. 1; AtomS3 `J4` = G39, G38, 5V, GND) — gives:

```
AtomS3 + Atomic CAN Base:   tx_pin = GPIO5     rx_pin = GPIO6
```

**This is derived, not stated in either document, and is untested.** It also depends on AtomS3
compatibility, which the Atomic CAN Base PDF leaves unresolved: its only compatibility information is
a `COMPATIBLE & EXCLUDE` packaging graphic showing four ATOM icons — AtomS3 among them — **with no
tick/cross legend** (AtomicCAN p. 1) *(read from artwork)*. Confirm with a continuity check before
relying on it.

---

## 7. Wiring an ESP32 node to ComfoNet

ComfoNet's own side, from [`comfoair-q-hardware.md` §3 and §11](./comfoair-q-hardware.md): the unit
exposes `J100` (RJ45) plus two 4-pin plug-in connectors `J101`/`J102` whose pins **4-3-2-1 are
12 Vdc (red) / GND (black) / CAN_H (yellow) / CAN_L (white)**; the bus budget is **400 mA @ 12 V**
and the cable is 2 × unshielded twisted pair, 0.6 mm² solid, max 50 m.

### Stack A — Mini CAN Unit

```
ComfoAir Q  (J101/J102)         Unit Mini CAN (U179)              AtomS3 / NanoC6
                                HT3.96-4P          Grove HY2.0-4P
  12 V   red     ------------>  HV   (9–24 V in)       5V  red  ====>  5V     (back-powered)
  GND    black   ------------>  G / GND                GND black ====>  GND
  CAN_H  yellow  ------------>  H  (CAN_H)             TXD yellow ===>  G2  = tx_pin
  CAN_L  white   ------------>  L  (CAN_L)             RXD white  ===>  G1  = rx_pin
```

Bus-side colour mapping: ComfoAir yellow = CAN_H → the pole labelled `H`
([`comfoair-q-hardware.md` §11](./comfoair-q-hardware.md) + MiniCAN p. 3). Grove-side colour mapping
is straight-through and is the derivation in §5.

### Stack B — Atomic CAN Base

```
ComfoAir Q  (J101/J102)         Atomic CAN Base (A103)          ATOM Lite
  12 V   red     ---- unused (no power pole on this base) ----
  GND    black   ------------>  G  (pole 4, SHIELD)      3V3 / 5V / GND from the ATOM's own USB supply
  CAN_H  yellow  ------------>  H  (pole 1)              22/TX  = tx_pin = GPIO22
  CAN_L  white   ------------>  L  (pole 2)              19/RX  = rx_pin = GPIO19
                                pole 3 = NC
```

The node needs its own USB supply, and the bus segment needs a 120 Ω terminator that this base does
not provide (§9).

---

## 8. Powering the node from the bus

Only Stack A can do this, through the Mini CAN's `HV` pin.

| | Figure | Cite |
| --- | --- | --- |
| ComfoNet budget | **400 mA @ 12 V ≈ 4.8 W** for all powered devices on the bus | [`comfoair-q-hardware.md` §3](./comfoair-q-hardware.md) |
| Mini CAN own draw | 2 mA @ 24 V / 4.02 mA @ 5 V | MiniCAN p. 2 |
| Mini CAN 5 V output ceiling | **700 mA** | MiniCAN p. 2 |
| NanoC6 draw, Wi-Fi active | **106.2 mA @ 5 V ≈ 0.53 W** | NanoC6 p. 4 |
| AtomS3 draw | **not stated in its PDF** | §12 |

**[derived]** A NanoC6 node at 0.53 W plus converter losses lands near 60 mA of the 400 mA 12 V
budget — comfortable. The AtomS3's consumption is undocumented, so the same claim cannot be made for
it without measuring. Note also the ComfoNet limit of **max 4 non-powered devices**
([`comfoair-q-hardware.md` §3](./comfoair-q-hardware.md)) and that the Mini CAN's 5 V output and the
host's USB 5 V rail are simply paralleled with no OR-ing on the Grove side (§11.7) — **do not assume
it is safe to leave USB plugged in while the bus supplies `HV`**; the datasheet does not address it.

The Mini CAN is **non-isolated** (MiniCAN p. 5), so ComfoAir GND and the host's ground become common;
with a USB cable attached, the PC's ground joins them too. The Atomic CAN Base is the isolated option
(§6), at the cost of losing bus power and the terminator.

Cable routing: the ComfoAir installer manual requires a barrier or **≥ 150 mm** between 230 V cabling
and interference-sensitive cabling, crossing perpendicularly where unavoidable
([`comfoair-q-hardware.md` §11](./comfoair-q-hardware.md)) — that applies to CAN wiring run inside the
unit's cable tray.

---

## 9. Bus termination — an open question, stated plainly

| Source | What it says |
| --- | --- |
| Unit Mini CAN | `R1 120R` **permanently across the bus**, not switchable (MiniCAN p. 3) *(artwork)*; the word "termination" never appears |
| Atomic CAN Base | **no terminator at all**, and no way to fit one (AtomicCAN p. 3) *(artwork)*; the word never appears |
| Zehnder manuals | [`comfoair-q-hardware.md`](./comfoair-q-hardware.md) records no termination statement anywhere in the four Zehnder PDFs |

So none of the seven documents available to this project says how many terminators a ComfoNet segment
should carry or where they sit. A correctly terminated CAN segment normally has exactly two, one at
each physical end — **that is general knowledge, not a citation**, and it means:

- **Stack A adds a third terminator** if the ComfoAir and the far end of the bus are already
  terminated. This is unresolved by documentation. What *is* known is that this combination, the
  Mini CAN's terminator included, is used in practice and reported to work — evidence of practice,
  not of specification.
- **Stack B adds none**, so the segment must be terminated elsewhere.

Do not "fix" either one on the strength of this document. Measure the bus resistance instead: a
healthy two-terminator segment reads ≈60 Ω across CAN_H/CAN_L with the bus unpowered.

---

## 10. Firmware-facing quick reference

| | AtomS3 (C123) | NanoC6 (C125) |
| --- | --- | --- |
| Variant | esp32s3 | esp32c6 |
| Flash | 8 MB (p. 3) | 4 MB (p. 4) |
| CAN TX (Grove yellow) | **G2** | **G2** |
| CAN RX (Grove white) | **G1** | **G1** |
| RGB LED data | G35 *(artwork only, p. 4)* | G20 (p. 6) |
| RGB LED count | **1** WS2812 drawn *(artwork, p. 4)* | **1** WS2812, `DO` = NC *(artwork, p. 5)* |
| RGB colour order | **not stated** | **not stated** |
| RGB power enable | none | **G19, must be driven HIGH** (p. 6) |
| Plain status LED | none (the green LED is on the boot comparator net) | **G7**, active HIGH (p. 6) |
| Button | **G41**, active LOW, 10 K external pull-up (p. 4/5) | **G9**, active LOW, 10 K external pull-up (p. 6) |
| IR LED | G4 (p. 4/5) | G3 (p. 6) |
| USB | native CDC, no bridge (p. 4) | native CDC, no bridge (p. 5) |
| Enter download mode | hold RESET ≈2 s (p. 3) | hold the G9 button, then plug in (p. 5) |
| Vendor PlatformIO `board` | `esp32-s3-devkitc-1` (p. 7) | `esp32-c6-devkitc-1` (p. 8) |

This firmware's descriptor (`components/board/board_m5stack_nanoc6.c`) uses the NanoC6 column's CAN,
button, status-LED and RGB rows as they stand. Its two values with no documentary source — the RGB colour order (GRB) and the G19 power
gate's 10 ms settle time — are marked there as unverified on hardware (§12).

---

## 11. Documentation defects and contradictions

Recorded rather than silently resolved, in the style of
[`comfoair-q-hardware.md` §17](./comfoair-q-hardware.md).

1. **The AtomS3 PDF's RGB LED is invisible to its own documentation.** A schematic block literally
   titled `RGB & IR` contains `U3 = WS2812` with `DI` on GPIO35 (AtomS3 p. 4) *(read from artwork)*,
   yet the spec table (p. 3), the features list (p. 2), the PinMap graphic (p. 5) and every pin table
   (p. 6) omit the RGB LED entirely — the PinMap's own colour legend has no LED category. Exactly
   **one** device is drawn and `SK_DOUT` goes nowhere. No count, no colour order, and no statement of
   whether it is populated on a board whose top face is an LCD.
2. **The Atomic CAN Base's GPIO numbers exist only in a photograph.** `22`, `19`, `23`, `33`, `21`,
   `25` are legible on the PCB silkscreen in two product photos (AtomicCAN p. 1) *(read from
   artwork)*; they appear in no table and no text, and the schematic's headers are generic
   `Header 5` / `Header 4` symbols. Cross-read on two independent photos, but a loopback or scope
   check on first bring-up is cheap insurance.
3. **`UART_TX`/`UART_RX` vs `CAN_TX`/`CAN_RX`.** The Atomic CAN Base PinMap table names the two
   signal pins `UART_TX`/`UART_RX` (AtomicCAN p. 3); the schematic names the same nets
   `CAN_TX`/`CAN_RX` and the silkscreen says `TX`/`RX`. The board has no UART peripheral and the
   CA-IS3050G's only logic pins are TXD/RXD, so the "UART" prefix is a docs-page error. Positions
   agree in all three.
4. **AtomS3 height: four different values.** 13.0 mm (p. 1 text), 12.9 mm (p. 3 spec table),
   12.8 mm (p. 1 infographic) *(artwork)*, 12.90 mm (p. 6 drawing) *(artwork)*.
5. **NanoC6 weight: 2.7 g vs 2.5 g.** Spec table 2.7 g (p. 4) and a scale photo reading 2.7 (p. 2)
   *(artwork)*, against `WEIGHT: 2.5g` on the packaging (p. 1) *(artwork)*.
6. **Mini CAN silkscreen strips are mirrored between the two case faces.** `H L HV G` on one,
   `GND HV L H` on the other (MiniCAN p. 1) *(read from artwork)*. They agree about which label
   belongs to which pole; they disagree about which end you count from. Schematic `JP1` settles the
   physical order as GND–HV–CAN_L–CAN_H.
7. **The Mini CAN never warns that `HV` back-feeds the Grove 5 V rail**, nor states whether `HV` and
   Grove 5 V may be applied simultaneously. The `Load Capacity DC 5V/Max: 700mA` row (p. 2) implies
   the first; the schematic proves it (p. 3) *(artwork)*. The second is simply unanswered.
8. **Mini CAN transceiver part number.** The description prose and the datasheet link say
   `TJA1051T` (MiniCAN p. 1, p. 4); the features bullet, the comparison table, the silkscreen and
   the schematic refdes all say `TJA1051T/3` (p. 1, p. 5, p. 3). The schematic shows a separate
   `VIO` pin, which only the `/3` variant has — so `/3` is the right reading.
9. **Grove connector pin-1 direction is ambiguous on the AtomS3.** The colour table lists
   GND, 5V, G2, G1 (AtomS3 p. 6) while the schematic numbers `J6` the opposite way, PIN1 = GPIO1
   (p. 4) *(artwork)*. The signal set is identical either way; only "which end is pin 1" is unclear.
   Wire by colour, not by pin number.
10. **The Atomic CAN Base's "Model Size PDF" link points at the wrong product** —
    `A092-atom-pwm.pdf` (AtomicCAN p. 3 link annotation).
11. **Port naming is inconsistent across M5's own pages.** `PORT.A` on the AtomS3 infographic and
    silkscreen vs `PORT.CUSTOM` in its pin table (AtomS3 p. 1/p. 5 vs p. 6); `PORT.C` in the Mini
    CAN pin table vs `PORT. CAN` on its silkscreen (MiniCAN p. 3 vs p. 1).

---

## 12. Not stated in any of these PDFs

Do not assume values for these; they are genuinely absent.

- **The word "CAN" never appears in the AtomS3 or NanoC6 datasheets.** No TWAI pin recommendation, no
  bit rate, no transceiver guidance. The Grove port is documented only as `PORT.CUSTOM`, "customizable
  … UART/I2C" (NanoC6 p. 2, p. 6).
- **No host GPIO number appears in either transceiver datasheet.** The Mini CAN PDF names only
  M5CoreS3, M5AtomS3 and M5Core, as bare Arduino example link titles (MiniCAN p. 4) — **AtomS3 Lite,
  NanoC6, ATOM Lite and StampS3 are never mentioned**. The Atomic CAN Base names no model in text at
  all.
- **No minimum CAN bit rate** anywhere — only "up to 1 Mbps". Nothing confirms 50 kbit/s, which is
  what ComfoNet uses.
- **PSRAM**: never mentioned for either MCU board.
- **Bluetooth/BLE**: never mentioned for either MCU board. The NanoC6's radio list is Wi-Fi 6,
  Zigbee 3.0, Thread 1.3, Matter (NanoC6 p. 4); the AtomS3's is the single word "Wi-Fi".
- **WS2812 colour order (RGB vs GRB)**: never stated on either board.
- **NanoC6 RGB power-enable settling time**: only "set G19 to a high level" (NanoC6 p. 6); no timing
  figure, so the 10 ms settle time in the board descriptor has no documentary source.
- **AtomS3 power consumption** in any state; **NanoC6 3.3 V rail current limit**; **input voltage
  range** for the NanoC6 beyond "DC 5V".
- **Isolation withstand voltage** for the Atomic CAN Base; **reverse-polarity rating** for the Mini
  CAN's `HV`.
- **Strapping-pin tables** on either MCU board. Only the download-mode instructions are given.
- **AtomS3 Lite / AtomS3U**: no datasheet, no differences table, no shared pin map. §3's caveat
  stands.
- **M5Stack ATOM Lite**: no datasheet in this repo at all. Its Grove pair (G26/G32) is evidenced
  only by a build system's board catalogue, not by a manufacturer document, and the catalogue
  names no LED or button GPIO for it.
- **Olimex ESP32-EVB**: no datasheet in this repo, and it is not an M5Stack product, so none of the
  four PDFs touches it. Its usual CAN pins (GPIO5 TX / GPIO35 RX) come only from a wiring guide,
  and its 4 MB flash only from PlatformIO's board definition — neither is a manufacturer source.
  Some revisions ship 16 MB; `esptool.py flash_id` reads the real size off the board.

---

## 13. Quick page index

| Looking for | Go to |
| --- | --- |
| AtomS3 spec table (SoC, flash, GPIO list) | AtomS3 p. 3 |
| AtomS3 full schematic | AtomS3 p. 4 |
| AtomS3 PinMap graphic + IMU sub-board | AtomS3 p. 5 |
| AtomS3 Grove / LCD / I²C pin tables | AtomS3 p. 6 |
| AtomS3 PlatformIO reference env | AtomS3 p. 7 |
| NanoC6 spec table incl. all current figures | NanoC6 p. 4 |
| NanoC6 full schematic + download mode | NanoC6 p. 5 |
| **NanoC6 PinMap + the G19 RGB-power notice** | **NanoC6 p. 6** |
| NanoC6 PlatformIO reference env | NanoC6 p. 8 |
| Mini CAN spec table (voltages, currents, load capacity) | MiniCAN p. 2 |
| **Mini CAN schematic (120 Ω, ME3116 buck, TJA1051T/3) + pin map** | **MiniCAN p. 3** |
| Mini CAN vs Unit CAN comparison (isolation, input power) | MiniCAN p. 5 |
| Atomic CAN Base spec table | AtomicCAN p. 2 |
| **Atomic CAN Base schematic + PinMap** | **AtomicCAN p. 3** |
| Atomic CAN Base GPIO silkscreen (photos) | AtomicCAN p. 1 |
| ComfoNet colour code, bus budget, connectors | [`comfoair-q-hardware.md` §3, §11](./comfoair-q-hardware.md) |
| This firmware's board descriptor | `components/board/board_m5stack_nanoc6.c` |
