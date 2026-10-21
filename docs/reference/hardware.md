# Hardware: exact figures

Everything in this file was **read from the connected device**, not copied from the vendor's
website. Captured on 2026-09-05, with firmware `d7cd83c-dirty` on the device.

Each figure says whether it came from the chip or from the configuration, because the two carry
different levels of trust. The vendor's product page is the source of no row here.

---

## Chip

Captured with `esptool flash-id` from the device.

| | |
| --- | --- |
| Type | **ESP32-C6FH4 (QFN32)**, revision **v0.2** |
| Cores | one RISC-V plus a separate LP core |
| Clock | 160 MHz (from the boot log: `cpu_start: cpu freq: 160000000 Hz`) |
| Crystal | 40 MHz |
| Radio | Wi-Fi 6 (2.4 GHz only), BT 5 LE, IEEE 802.15.4 |
| USB | USB-Serial/JTAG, built into the chip |
| ESP-IDF | 5.5.5 (from the boot log) |

**2.4 GHz only — and that is not a detail.** It is the most common reason for a support request
on any device of this class: the router publishes one name on both bands, the owner sees the
network on the phone, and the device cannot find it. That is why the text "this device has no
5 GHz radio" sits right in the connection error message — the key
`settings.status.failure.notFound` in `web/src/i18n/catalogs/en/settings.ts:282-284` (and the same
phrase in the scan hint, `settings.wifi.scan.hint`, in the same file, :91-95). It is chosen by the
`network-not-found` case in `web/src/pages/settings/status.ts:29-32`, whose comment notes
separately that the claim is verifiable, not folklore.

### Addresses

| | |
| --- | --- |
| BASE MAC | `9c:13:9e:d4:8a:e4` |
| Station MAC | `9c:13:9e:d4:8a:e4` |
| Access-point MAC | `9c:13:9e:d4:8a:e5` (BASE + 1) |
| Access-point name | `comfoair-8ae5` — the last two bytes of the AP MAC |
| MQTT topic prefix | `comfoair/9c139ed48ae4` |
| Default device name | `ComfoAir Q 8ae4` |

The identity is derived from the MAC, not from the configurable name — so renaming the device in
the web UI **cannot** create a second device in Home Assistant.

---

## Flash

Captured with `esptool flash-id`.

| | |
| --- | --- |
| Size | **4 MB**, inside the chip package |
| Manufacturer | `0x46` |
| Device | `0x4016` |
| Mode | DIO at 80 MHz (`CONFIG_ESPTOOLPY_FLASHMODE="dio"`, `FLASHFREQ="80m"`) |

### Partition table

**Read from the device's flash** (`esptool read-flash 0x8000`), not from `partitions.csv`:

```text
# Name,   Type, SubType, Offset,   Size
nvs,      data, nvs,     0x9000,   16K
otadata,  data, ota,     0xd000,   8K
phy_init, data, phy,     0xf000,   4K
ota_0,    app,  ota_0,   0x10000,  1984K
ota_1,    app,  ota_1,   0x200000, 1984K
```

**There is no file-system partition, and that is a decision, not an omission.** Two OTA slots of
1984 KiB each take almost the whole flash. It follows that there is nowhere on the device to keep
a history of readings — for graphs, go to Home Assistant.

`ota_0` must stay at `0x10000`.

---

## Memory use: snapshot of 2026-09-05

From the build output of `pio run -e m5stack-nanoc6` for firmware `d7cd83c-dirty` (2026-09-05).
**This is a snapshot, not the current firmware:** the image has grown since, and the flash and RAM
of the current build have not been re-measured here.

| | |
| --- | --- |
| Flash | **1 212 898 of 2 031 616 bytes — 59.7%** of one OTA slot |
| RAM | **96 376 of 327 680 bytes — 29.4%** |
| Web UI inside the image | 24 912 bytes (compressed SPA) |

The compressed SPA is noticeably larger now: the three embedded files `web/dist/*.gz`
(`index.html.gz`, `app.js.gz`, `style.css.gz`) of the web build of 2026-10-18 add up to
**121 799 bytes** (`wc -c`). That figure changes with every web build.

Heap after initialisation, from the boot log:

```text
At 40833640 len 00048FD0 (291 KiB): RAM
At 4087C610 len 00002F54 ( 11 KiB): RAM
At 50000000 len 00003FE8 ( 15 KiB): RTCRAM
```

---

## Board: M5Stack NanoC6

The pin-out is from `components/board/board_m5stack_nanoc6.c`, the only place in the project where
GPIO numbers are mentioned at all.

| Purpose | GPIO | Note |
| --- | --- | --- |
| CAN TX | 2 | to the transceiver |
| CAN RX | 1 | to the transceiver |
| Button | 9 | inverted; **also the download strap** |
| Status LED | 7 | |
| RGB pixel, data | 20 | WS2812, exactly one |
| RGB pixel, power | 19 | power gate |

**The button on GPIO9 is also the boot-loader strap, and that cost one debugging session.**
Holding the button **while power is applied** sends the chip into the boot loader: the device does
not start, does not bring up the network, does not answer — it looks dead. The working gesture is a
hold **after** start-up. The same text appears on the settings page and on the update page,
because this is the one mistake that passes itself off as a fault.

A chain of several RGB LEDs is physically impossible: the DO pin is not connected on the
schematic.

---

## CAN bus

| | |
| --- | --- |
| Bit rate | **50 000 bit/s** |
| Node address | `0x2a` |
| Poll cycle | 60 PDOs, every 3 600 000 ms, 100 ms between requests (about 6.0 s per sweep) |

The boot-log lines that confirm it:

```text
can: up on tx=2 rx=1 at 50000 bps
poll: cycle: 60 PDOs every 3600000 ms, 100 ms apart, node 0x2a
```

**`can: up` only means the driver on the board came up** — not that anyone is on the other end of
the bus. The sign that the unit is absent: `GET /api/state` returns values only for the entities
that live on the board itself (the four durations, `schema_version` and `data_freshness`), and
every PDO reading is empty — **6 values of 76** (`COMFOAIR_ENTITY_COUNT` in
`components/comfoair_registry/include/registry_generated.h`; the seeding is `seed_defaults()` in
`components/comfoair_state/comfoair_state.cpp`). The three sensor ventilation selects
(`temperature_passive`, `humidity_comfort`, `humidity_protection`) read `null` as well: their value
comes only from an RMI read of the unit, and there is nobody to answer it; so do `unit_error` and
`active_errors`.

---

## Toolchain

| | |
| --- | --- |
| Platform | `pioarduino/platform-espressif32` @ `55.03.311` |
| Framework | `espidf` (Arduino is absent and must not appear) |
| Board id | `esp32-c6-devkitm-1` |
| Monitor speed | 115 200 |
| Partition table | `partitions.csv` (custom) |

### Logging

| | |
| --- | --- |
| `CONFIG_LOG_VERSION` | **1** (the IDF default, not overridden) |
| `CONFIG_LOG_COLORS` | **off** (the IDF default) |
| Timestamp source | RTOS ticks — milliseconds since boot |

**These three values are load-bearing for the web UI.** The log page tells a line's level by the
regex `^[EWID] \(\d+\)`. Turning colours on puts an ANSI sequence before the letter, and switching
the timestamp to system time gives `HH:MM:SS.sss` with colons — in both cases **every line silently
becomes "info"**, and nothing fails. Details and source references are in the `levelOf` docstring
in `web/src/pages/log/log.ts`.

Under `LOG_VERSION=1` an ordinary `ESP_LOGx` line reaches the hook in **one** call. Only the Wi-Fi
blob fragments: it prints one logical line in three calls — the prefix, the body and a bare
newline. The line assembly in `components/comfoair_log/comfoair_log.c` exists for this: before it,
28 of the ring's 80 entries were useful; after it, 80.

---

## How to capture these figures again

```sh
# chip, flash, MAC
~/.platformio/penv/bin/python -m esptool --port /dev/cu.usbmodemXXX --after no-reset flash-id

# the partition table from the device itself
~/.platformio/penv/bin/python -m esptool --port /dev/cu.usbmodemXXX read-flash 0x8000 0xc00 /tmp/p.bin
~/.platformio/penv/bin/python ~/.platformio/packages/framework-espidf/components/partition_table/gen_esp32part.py /tmp/p.bin

# memory use
~/.platformio/penv/bin/pio run -e m5stack-nanoc6

# boot log (pio device monitor needs an interactive terminal, so pyserial directly)
~/.platformio/penv/bin/python -c "
import serial, time, glob
s = serial.Serial(glob.glob('/dev/cu.usbmodem*')[0], 115200, timeout=1)
s.dtr = False; s.rts = True; time.sleep(0.2); s.rts = False; time.sleep(0.1)
s.reset_input_buffer()
end = time.time() + 12; buf = b''
while time.time() < end: buf += s.read(4096)
print(buf.decode('utf-8', 'replace'))
"
```

`pio` is not on PATH — the full path is spelled out on purpose.

**Do not toggle DTR/RTS by hand if you can avoid it.** On the C6's USB-Serial/JTAG an unlucky
sequence leaves the `force_download_boot` flag set, and the board boots into the boot loader until
a regular `pio run -t upload` clears it. Symptom: the chip answers esptool, but the application does
not start, the port is renamed, and the access point does not appear.
