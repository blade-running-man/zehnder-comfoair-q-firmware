# First setup

A freshly installed device has no Wi-Fi stored, so it puts its own setup network on the air. You
join that network, tell the device which Wi-Fi to use, and finish the rest of the configuration
from your own network.

## 1. Join the setup network

| | |
| --- | --- |
| Network name | `comfoair-xxxx` — `xxxx` is four hex digits from the board's access-point MAC, unique per board |
| Security | **Open**, no password |
| Address | `http://192.168.4.1` |
| LED | RGB LED **blinking blue** (1 s period) |

Join `comfoair-xxxx` from a phone or a laptop, then **open a browser yourself** and go to
`http://192.168.4.1`. The device answers every DNS name with its own address, so any address you
type lands on its web interface.

::: tip No sign-in window opens by itself
The device answers the connectivity checks of Android, iOS and macOS, Windows and Firefox with the
replies those systems expect from a working network (`components/comfoair_captive`,
`comfoair_captive_probe.c`), so the operating system does not open its captive-portal sign-in
window. Use a normal browser tab instead.

The phone may still say the Wi-Fi has **no internet** or **limited connectivity**. That is true —
the setup network has no internet. Tap through and stay connected.
:::

The setup network is open on purpose: there is no way to tell you a key for it before you can
reach the device. While it is on the air the device counts as **unclaimed**, and it accepts only one
kind of write — a Wi-Fi network to join (`POST /api/provision`). Everything else, including setting
a password or a broker, is refused until the device is on your network.

## 2. Mind the setup window

The setup network does not stay up for ever (`components/comfoair_provision`):

| Situation | How long the setup network stays |
| --- | --- |
| First setup of a device that has never been configured | **15 minutes** from when the network appears |
| Every later window — after a reboot of a device whose first window ran out, after a short button press on a device that has Wi-Fi saved or whose first window ran out, or the automatic recovery network | **5 minutes** |

In both cases every request from your browser restarts the countdown, but no window lasts more
than **60 minutes** in total.

If the first window runs out with no Wi-Fi saved, the setup network goes off the air and the RGB
LED turns **solid blue**. To get it back, either **press the button briefly** (under one second,
after the device has started) or power-cycle the board. Both bring the setup network back without
erasing anything.

## 3. Choose your Wi-Fi

Open **Settings**. The **Wi-Fi** card is the only part of the page that works on the setup network.

1. Press **Scan for networks**. The list is what the *device* can hear, which is not what your
   phone shows:
   - the ESP32-C6 has **no 5 GHz radio** — a router that publishes one name on both bands appears
     only if its 2.4 GHz side is in range;
   - **hidden networks never appear** — choose *Other / hidden network* and type the name exactly;
   - the scan takes a few seconds and moves the radio off the setup channel, so the page may drop
     for a moment. If the list does not come back, type the name instead.
2. Enter the Wi-Fi password. The device accepts 8 to 63 characters, exactly 64 hexadecimal digits,
   or an empty password for an open network
   (`components/comfoair_config/comfoair_config_check.c`, `comfoair_config_check_psk`).
3. Press **Save Wi-Fi and reconnect**.

The device **answers before it starts connecting**, so "Accepted — the device is trying now" means
the credentials were stored, not that your router took them. Within a few seconds the device tunes
its single radio to your router's channel and **the page usually stops responding**. That is the
normal outcome, not a failure.

While it tries, the RGB LED **breathes blue**. A new network gets **90 seconds** to hand the device
an address. When it does, the setup network goes off the air at once.

## 4. Find the device on your network

The device is now at whatever address your router gave it. Any of these reaches it:

- `http://comfoair-xxxx.local` — the same four hex digits as the setup network name (mDNS);
- your router's list of connected clients — the device registers the DHCP host name
  `comfoair-xxxx`;
- its IP address, shown on the device's **System** page and in **Settings → Connection**.

::: tip The four digits differ by one
The default **device name**, `ComfoAir Q xxxx`, ends in the last four hex digits of the *station*
MAC, while `comfoair-xxxx` uses the *access-point* MAC. The two MACs differ by one in the last byte,
so a unit reached at `comfoair-8ae5.local` is named `ComfoAir Q 8ae4`. Both are correct
(`components/comfoair_config/comfoair_config.c`, `default_name`).
:::

## 5. Finish the configuration

Now that the device is on your network, open **Settings** again at its new address:

1. **Set a web interface password** (8 to 128 characters). It is optional, but without one anyone
   on your network can change the settings, and firmware updates are refused until one is set.
   There is no "forgot password" — read [the button](/guide/troubleshooting#the-button) first.
2. **Enter the MQTT broker**: host, port (default 1883), user name and password.
3. Leave **Publish Home Assistant discovery** on (it is on by default).
4. Press **Save settings**. Settings take effect immediately; the device does not reboot.

Every field is described in [Configuration](/guide/configuration). What then appears in Home
Assistant is in [Home Assistant](/guide/home-assistant).

## If the Wi-Fi password was wrong

**Settings → Connection** shows one of four reasons when a connection fails: the network was not
found, it refused the key, it turned the device away for another reason, or the device joined but
was never given an address (the router's DHCP).

- **On a device that has been on a network before**, a new pair that does not produce an address
  within 90 seconds is undone: the device puts back the last network that actually gave it an
  address and returns there by itself.
- **On a brand-new device** there is nothing to go back to. It keeps trying, and the setup network
  stays on the air for the rest of its window, so you can correct the password from it. If the window
  has run out, press the button briefly or power-cycle the board.

## What the LED shows during setup

| RGB LED | Meaning |
| --- | --- |
| Solid white | Starting up |
| Blinking blue | Setup network is on the air with no Wi-Fi saved, or as the recovery network — come and set me up |
| Breathing blue | Joining a network |
| Double-pulse blue | The network refused the device, or there is no working connection to be had |
| Solid blue | Setup window ran out, nothing to connect to — press the button briefly |
| Green flash every 10 s | All well |

Blinking blue is drawn only while the setup network is on the air on a device with **no Wi-Fi
saved**, or as the automatic recovery network. The setup network can be up under another look: while
the device tries the network you just saved (breathing blue), while it keeps retrying one that
failed (breathing or double-pulse blue), and after a short button press on a device that is already
on your Wi-Fi (its usual look, unchanged). To check that it is up, look for `comfoair-xxxx` in your
phone's Wi-Fi list (`xxxx` is the last four hex digits of the board's access-point MAC address).

The full table, including the amber broker and bus states, is in
[Troubleshooting](/guide/troubleshooting#what-the-leds-mean).
