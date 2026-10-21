# Installation

The firmware is installed once over USB from a web page, then updated over the network. There is
one binary for every installation; nothing is entered at build time.

## What a release contains

`tools/make_release.py` assembles a release from one board build. It produces:

| File | Purpose |
| --- | --- |
| `index.html` | The browser installer page (from `installer/index.html`), running ESP Web Tools. |
| `manifest.json` | What ESP Web Tools flashes, and where. Offsets are read from the build, not typed by hand. |
| `comfoair-q-<version>-bootloader.bin`, `-partitions.bin`, `-otadata.bin`, `-firmware.bin` | The four flash parts the installer writes. |
| `comfoair-q-<version>-ota.bin` | The same application image, for [updating](/guide/updating) over the network. |

## Getting the installer page

There is no published installer site yet. The release workflow (`.github/workflows/release.yml`)
is written to publish these files on a version tag, as an installer site on GitHub Pages and as
assets of a GitHub release, but it is dormant: this repository has no GitHub remote, so no tag push
has ever triggered it.

Until it runs, the installer page is the one you build and serve yourself, from a checkout of the
repository:

```sh
pio run -e m5stack-nanoc6
python3 tools/make_release.py --allow-unreleased   # only a clean tagged tree builds without the flag
python3 -m http.server -d dist 8000
```

Then open `http://localhost:8000` in Chrome or Edge. Web Serial works only on `https://` pages or
on `localhost`. The same route is described in
[Releasing](/development/releasing#building-a-release-locally).

## Flashing with the browser installer

1. Use **Chrome or Edge on a desktop computer**. Installing needs Web Serial, which Firefox, Safari
   and phone browsers do not have.
2. Connect the NanoC6 with a USB-C cable that carries data, not only power.
3. Open the installer page (`http://localhost:8000`, served as above) and click **Install**. In
   the port list, pick the entry called `USB JTAG/serial debug unit` (`/dev/cu.usbmodem…` on a Mac,
   a `COM` port on Windows). The ESP32-C6 has USB built in, so there is no driver to install.
4. The installer asks whether to **erase the device** first:
   - **Leave it unticked to keep your settings.** Wi-Fi, the web password, the broker and the other
     settings live in the NVS partition, which the four flash parts do not touch.
   - **Tick it for a clean start** — a board that ran other firmware before, or one whose settings
     you want gone.
5. When it finishes, unplug and replug the board **without touching its button**.

::: warning If no port appears or connecting fails
Unplug the board, **hold its button while you plug the cable back in**, release it, and try again.
That starts the chip in its ROM download mode
([ESP32 nodes and CAN transceivers](/reference/esp32-node-hardware)). The button is also the
ESP32-C6 download strap
(`components/board/board_m5stack_nanoc6.c`: `button_is_download_strap = true`), so a board powered
up with the button held stays in download mode and looks dead. After installing, power-cycle it with
the button released.
:::

The release deliberately ships four parts rather than one merged `firmware.factory.bin`: the merged
image would overwrite the NVS region and erase every setting on each install
(`tools/make_release.py`).

## Wiring to the unit

The NanoC6 drives CAN on its Grove port: **TX on GPIO2, RX on GPIO1, 50 kbit/s**
(`components/board/board_m5stack_nanoc6.c`). The Unit Mini CAN plugs into that port with the
supplied Grove cable, straight through.

The ComfoAir Q's 4-pin ComfoNet connectors carry, on pins 4-3-2-1, **12 V (red), GND (black),
CAN_H (yellow), CAN_L (white)** ([ComfoAir Q hardware](/reference/comfoair-q-hardware)). Wire them
to the Unit Mini CAN's terminal block:

```text
ComfoAir Q connector        Unit Mini CAN (HT3.96 terminal)     NanoC6 (Grove)
  12 V   red     ------->   HV  (9-24 V in)        5V   ====>   5V  (powered by the unit)
  GND    black   ------->   GND                    GND  ====>   GND
  CAN_H  yellow  ------->   H                      TXD  ====>   G2  (CAN TX)
  CAN_L  white   ------->   L                      RXD  ====>   G1  (CAN RX)
```

Things to know before you close the panel — each is sourced in
[ESP32 nodes and CAN transceivers](/reference/esp32-node-hardware):

- **Power.** With `HV` connected, the unit powers the transceiver and, through the Grove 5 V line,
  the NanoC6. A NanoC6 with Wi-Fi active draws roughly 0.5 W, well inside the bus budget of
  400 mA at 12 V.
- **USB at the same time.** The Mini CAN's 5 V output and the NanoC6's USB 5 V are paralleled with
  no protection between them. Do not assume it is safe to leave a USB cable plugged in while the bus
  supplies `HV`.
- **Termination.** The Unit Mini CAN has a 120 Ω terminator permanently fitted.
- **Isolation.** The Unit Mini CAN is not isolated: the unit's ground and the board's ground become
  one, and with USB attached the computer's ground joins them.
- **Cable routing.** Keep the bus cable away from 230 V wiring, as the unit's installer manual
  requires.

Once it is powered from the unit, the board's LEDs tell you what it is doing — see
[Troubleshooting](/guide/troubleshooting#what-the-leds-mean). If no frames arrive from the unit, the
device still starts and serves its web interface instead of stopping: `<prefix>/bus` stays
`offline` for Home Assistant, and the **System** page's CAN bus card shows how many frames have
been received.

## Building from source

The firmware can be built and flashed with PlatformIO instead of the installer. The web interface is
compiled into the image as part of the same build. See [Building](/development/building) and
[Releasing](/development/releasing).

## Next

[First setup](/guide/first-setup) — put the device on your Wi-Fi.
