<script setup>
import { data } from '../reference/entities.data'
// Entity counts on this page come from the generated registry (see /reference/entities).
</script>

# Introduction

This firmware turns a small ESP32 board, wired to the ComfoNet (CAN) bus of a **Zehnder ComfoAir
Q** ventilation unit, into a network bridge for that unit. It reads what the unit reports, sends it
commands, and offers three ways to use it:

- **MQTT with Home Assistant discovery** — the main control path. Home Assistant finds the device by
  itself through its built-in MQTT integration; no custom integration is installed.
- **An HTTP REST API** that covers exactly what MQTT covers.
- **A web interface of its own**, served by the device: live state, controls, settings, the device
  log and firmware updates.

All three are views of one model. The entity list behind them is generated from a single table
(`tools/pdo_table.py`), so a value has the same key in the REST path, the MQTT topic and the Home
Assistant entity.

::: warning Not yet accepted on a real unit
The firmware builds and its host test suites pass, but the hardware test scenarios in
[Hardware verification](/hardware-verification/) have not been walked yet. Treat a first install as a
bring-up and keep a USB cable at hand.
:::

## Nothing to compile

There is **nothing to compile and no YAML**. One published binary suits every installation —
Wi-Fi, the MQTT broker, the device name and the password are entered in a browser after installing
and stored on the device. No secret is compiled into the image.

## What it does

- Talks to the unit over CAN at 50 kbit/s: it decodes the values the unit broadcasts and polls the
  rest, **{{ data.total }} entities** in all — temperatures, humidities, fan speeds and flows, power and energy,
  filter state, operating mode, bypass, and diagnostics (full list:
  [Entities](/reference/entities)).
- Controls the unit through **{{ data.writable }} writable entities**: the fan with its presets,
  selects for modes such as bypass, balance and temperature profile, the away-mode switch and the
  operation durations (full list: [Entities](/reference/entities)). Six parameterised operations (`boost`, `set_bypass`, `set_balance_mode`,
  `set_ventilation_level`, `set_temperature_profile`, `set_away`) take all their parameters in one
  call over REST or MQTT — for example a bypass mode together with how long it holds.
- Sets itself up from a browser: its own open access point, a captive portal, a bounded setup
  window, and a rollback to the last working Wi-Fi if a new password is mistyped
  ([First setup](/guide/first-setup)).
- Updates over the network by uploading a `.bin` file in the web interface, with automatic rollback
  if the new image does not come up ([Updating](/guide/updating)).
- Shows its state on the board's LEDs and recovers through the board's button: a short press opens
  the setup network, longer holds clear the Wi-Fi and the web password, or every setting
  ([Troubleshooting](/guide/troubleshooting#the-button)).
- **Never reboots because something else is missing.** A dead router, a stopped broker or a silent
  CAN bus are retried for as long as they last; the device keeps serving its web interface
  meanwhile.

## What it does not do

- **One board only.** The firmware runs on the M5Stack NanoC6. Other boards are anticipated by the
  code layout (pins live only in `components/board/`) but none is supported.
- **No TLS to the broker.** MQTT runs over plain TCP (default port 1883), so the broker password
  crosses the network in the clear.
- **No network-pulled updates.** An update is a file you upload; the device does not fetch
  firmware from a URL by itself.
- **No Improv Wi-Fi.** The installer cannot hand Wi-Fi credentials to the device over USB; Wi-Fi is
  always set through the device's setup network.
- **No Ethernet.** Wi-Fi only, and only 2.4 GHz — the ESP32-C6 has no 5 GHz radio.
- **Three settings are read back slowly.** Temperature Passive, Humidity Comfort and Humidity
  Protection have no PDO, so the firmware asks the unit for them about 10 s after boot and then every
  6 h. They show as unknown until the first answer, and a change made on the unit's own display is
  seen at the next read, within 6 h.
- **No filter reset.** Zehnder documents no remote filter reset; the counter is reset with the
  filter wizard on the unit.
- **ComfoAir Q only, on the CAN bus only.** Other ComfoAir models and the ComfoConnect LAN C
  gateway are not supported.
- **The six operations are not in Home Assistant discovery.** They are reachable from Home
  Assistant through `mqtt.publish`; everyday control uses the {{ data.writable }} writable discovered entities.

## Hardware you need

| Part | Notes |
| --- | --- |
| **M5Stack NanoC6** | ESP32-C6, 4 MB flash, USB-C. The firmware is built for this board alone (`components/board/board_m5stack_nanoc6.c`). |
| **M5Stack Unit Mini CAN** (U179) | CAN transceiver on the NanoC6's Grove port. Its terminal block takes the unit's 12 V, GND, CAN_H and CAN_L. |
| Cable to the unit | To one of the ComfoAir Q's 4-pin ComfoNet connectors. See [Installation](/guide/installation#wiring-to-the-unit). |
| USB-C data cable | For the first install, from a desktop computer running Chrome or Edge. |

The transceivers and their electrical details (termination, isolation, power budget) are described
in [ESP32 nodes and CAN transceivers](/reference/esp32-node-hardware) and
[ComfoAir Q hardware](/reference/comfoair-q-hardware).

## Next

1. [Installation](/guide/installation) — flash the firmware and wire the board.
2. [First setup](/guide/first-setup) — put the device on your Wi-Fi.
3. [Configuration](/guide/configuration) — broker, name, password.
4. [Home Assistant](/guide/home-assistant) — what appears there.
