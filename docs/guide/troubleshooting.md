# Troubleshooting

The device reports its state in three places: the **LEDs** on the board, the **web interface**
(Settings → Connection, the System page and the Log page), and the REST endpoints behind them. It
recovers from almost everything by itself; what it cannot fix, the button can.

## What the LEDs mean

The NanoC6 has two LEDs.

- **The blue status LED** flashes for 100 ms once a second, whatever else is happening. It says only
  that the firmware is running. If it is dark, the firmware is not running — no power, or the chip
  is sitting in its USB download mode.
- **The RGB LED** shows one verdict about the whole device. The colour says which part of the
  system it concerns (white: the device itself, blue: Wi-Fi, amber: the broker or the ventilation
  unit, red: a fault, green: all well); the rhythm says what to do.

| RGB LED | State | What to do |
| --- | --- | --- |
| Solid white | Starting up | Wait |
| White, lit for part of each second | A firmware update is being written; the lit share is the share written, in 25 % steps | **Do not cut the power** |
| Blinking blue (1 s) | The setup network `comfoair-xxxx` is on the air on a device with no Wi-Fi saved (first setup, after a soft reset, or after a short press once the window ran out), or as the automatic recovery network of a device that cannot reach its saved Wi-Fi. Not shown when the setup network is opened on a device that is connected or still trying — see below the table | Join it and open Settings ([First setup](/guide/first-setup)) |
| Breathing blue (4 s) | Joining a network, or trying newly saved credentials | Wait |
| Double pulse blue (every 2 s) | The network refused the device (wrong password, rejected), or no working connection is to be had (network not found, no address from DHCP) | Check the password, the router, its DHCP; Settings → Connection says which |
| Solid blue | The setup window ran out and there is no network to join | Press the button briefly, or power-cycle |
| Breathing amber | A broker is configured and the device has not been connected to it for 30 s | Nothing — it retries by itself; check the broker is running |
| Blinking amber | The broker refused the connection (for example a bad user name or password) | Fix the broker settings |
| Double pulse amber | The ventilation unit, heard before, has been silent on the CAN bus for 90 s | Check the wiring and that the unit has power; the web interface still works |
| Solid red | A part of the firmware failed to start | Read the log |
| Green flash, 250 ms every 10 s | Everything is fine | — |

Blinking blue is not the only look the setup network can be on the air under. On a device that has
Wi-Fi saved and is not in recovery, the access point joins the station without changing the
verdict (`components/comfoair_phase/comfoair_phase.c`, `comfoair_phase_derive`): while newly saved
credentials are tried it is breathing blue, while a failing network is retried it is breathing or
double-pulse blue, and on a connected device — after a short button press — it keeps its usual
look. To check that the setup network is up, look for `comfoair-xxxx` in your phone's Wi-Fi list
(`xxxx` is the last four hex digits of the board's access-point MAC address).

When several of these hold at once, the device shows exactly one, in this order
(`components/comfoair_phase/comfoair_phase.c`, `comfoair_phase_derive`): starting up; trying newly
saved credentials (breathing blue); the setup network on the air with no Wi-Fi saved, or the
recovery network (blinking blue); a part that failed to start (solid red); a refused or lost
network (double pulse blue); an expired setup window (solid blue); joining a network (breathing blue); the broker refusing the device (blinking amber);
a silent bus (double pulse amber); a broker not connected (breathing amber); and only then the
green flash. A firmware update and a held button draw over all of these.

For the **first five minutes after power-up**, breathing amber and double-pulse amber are drawn in
**blue** instead: after a power cut the broker and the unit are often still starting, and that is
not a fault. Only the colour is softened; the rhythm and the state reported by the web interface
are unchanged.

A broker state appears only when a broker host is configured. A device without one goes straight
to the green flash.

The full design of the indication, including brightness and timing, is in
[Indication](/device/indication).

## The button

The button on the NanoC6 does three things, decided by **how long you hold it and when you let
go**. Nothing happens when a threshold is reached — only the release counts.

::: danger Hold it only after the device has started
The button is also the ESP32-C6 **download strap** (`components/board/board_m5stack_nanoc6.c`:
`button_is_download_strap = true`). Holding it **while power comes up** puts the chip into its ROM
bootloader instead: the firmware does not run, the status LED stays dark, and the device looks
dead. Power the board up, wait until it is running, then press. If it did start in the bootloader,
power-cycle it with the button released.

A button that is already pressed when the firmware starts is ignored until it has been released
once.
:::

| Let go after | RGB LED while held | What happens |
| --- | --- | --- |
| Less than 1 s | White, from the moment the press is noticed | **Opens the setup network** — for 5 minutes on a device that has Wi-Fi saved or whose first window ran out; a brand-new device keeps its full 15. Erases nothing. The LED turns blinking blue only on a device with no Wi-Fi saved; on a device already on your Wi-Fi the setup network comes up beside the connection and the LED keeps its usual look, so check for `comfoair-xxxx` in your phone's Wi-Fi list. |
| 1 to 5 s | Back to its usual look | **Nothing.** The gap is deliberate: a knock or a sleeve cannot erase anything. |
| 5 to 15 s | Amber, growing brighter towards 15 s | **Soft reset**: erases the Wi-Fi credentials and the web interface password, then restarts. Keeps the broker settings, topic prefix, device name, discovery switch and durations. |
| 15 to 25 s | Red, growing brighter towards 25 s | **Hard reset**: erases every setting, broker included, and clears the log, then restarts. For a device leaving the house. |
| More than 25 s | Back to its usual look | **Nothing** — the button is treated as stuck. Let go and start again. |

Each time the hold crosses into a new step, the LED flashes once at full brightness so you can feel
the boundary without counting
(thresholds: `components/comfoair_button/comfoair_button.c`).

After a soft or hard reset is released, the LED **flashes three times** in the step's colour
(about 1.5 s), and the device restarts only once the button is confirmed released — so the restart
can never happen with a finger on the download strap. It comes back with its setup network on the
air and no web interface password.

::: tip Which reset do I need?

- **The Wi-Fi password changed, or the device lost its network** — short press, then use the setup
  network to enter the new password. Nothing is lost.
- **Forgot the web interface password** — 5-second hold. The broker settings survive; re-enter the
  Wi-Fi and set a new password.
- **Giving the device away** — 15-second hold. It does **not** remove the device from Home
  Assistant; see [Removing the device](/guide/home-assistant#removing-the-device) first.
:::

If the erase itself fails, the device does **not** restart: the log is the only record of what
happened, and a restart would clear it. Read `GET /api/log` at once.

More detail on the gesture: [Button](/device/button).

## When something else fails

Nothing in this firmware reboots because a router, a broker or the ventilation unit is missing. It
retries for as long as the outage lasts, and its web interface stays reachable whenever there is a
network to reach it on.

### Wi-Fi is down

- The device retries with a growing delay, from 1 second up to 30 seconds between attempts, for
  ever (`components/comfoair_provision/comfoair_provision.c`).
- After **30 minutes** of failure in which the network is not found or refuses the device, it puts
  its **setup network back on the air for 5 minutes**, while still trying your network underneath.
  This repeats for as long as the failure lasts, so a device whose Wi-Fi password changed can always
  be reached again. A router rebooting at night does not trigger it.
- A device that joins the network but **never receives an address** (a DHCP problem) does not raise
  the setup network by itself. Once that has lasted 30 minutes, it offers the setup network for 5
  minutes at the next power-up instead.
- A short button press brings the setup network up at any time.

### The broker is down

- The device retries starting at 2 seconds, doubling up to 60 seconds between attempts, each wait
  varied by up to ±20 % with a per-device seed so several devices do not retry in step. A session that dropped after working
  starts again at 1 second (`components/comfoair_mqtt_backoff`).
- If the broker **refuses the connection** (bad credentials, or a refused protocol version or client
  id), the device waits **about 5 minutes** between attempts, so
  that a wrong password does not get the device banned by the broker. Saving the broker settings
  resets the wait.
- The **Diagnostics** page (and an alert on the **Home** page) and `GET /api/status` say why the
  last attempt failed.

### The ventilation unit is silent

- After **90 seconds** without CAN traffic from a unit it has heard before, the device reports the
  bus silent: double-pulse amber on the LED, and `<prefix>/bus` set to `offline`, which turns the
  unit's readings **unavailable** in Home Assistant rather than leaving their last values on screen.
- The web interface, the API and MQTT keep working. When the unit speaks again, everything comes
  back without intervention.
- If the bus never comes up at all, check the CAN wiring, the transceiver's power and the unit's
  power — see [Installation](/guide/installation#wiring-to-the-unit). A unit that has never been
  heard does not produce the double-pulse amber (the bus counts as silent only after it was heard,
  `components/comfoair_phase/comfoair_bus_health.c`), but `<prefix>/bus` stays `offline` and the
  **System** page's CAN bus card shows no frames received.

How each failure is classified and handled is in [Failure model](/architecture/failure-model).

## Reading the log

The device keeps its own log in RAM: the **last 80 lines**, each up to 159 characters, oldest
dropped first (`components/comfoair_log/include/comfoair_log.h`). It is lost at every restart and
power cut, and the hard reset clears it.

- **In the web interface:** the **Log** page. Press **Refresh** to fetch the current lines.
- **Over HTTP:** `GET /api/log` returns a JSON array of strings, oldest first.

```sh
curl http://comfoair-xxxx.local/api/log
# with a web interface password set (the user name is ignored):
curl -u admin:YOUR-PASSWORD http://comfoair-xxxx.local/api/log
```

Passwords and keys are never written to the log. Network names and the device name can be, and
the log is readable by anyone who can reach the device while no password is set.

Other places to look:

- **Settings → Connection** — Wi-Fi state, the configured network, the address, and one of four
  reasons when Wi-Fi fails: not found, wrong password, refused, no address.
- **System** — firmware version, running partition, uptime and the **reason for the last reset**
  (power on, software restart, crash, watchdog, brownout…), Wi-Fi signal, CAN controller state.
  **Copy as text** puts the whole summary on the clipboard for a bug report.
- `GET /api/status` and `GET /api/system` — the same facts as JSON; see
  [REST API](/interfaces/rest-api).
- **USB serial console** — with a cable attached, `pio device monitor` shows the full ESP-IDF log
  from the moment of boot, including anything before Wi-Fi was up.

## Common problems

| Symptom | Likely cause and fix |
| --- | --- |
| The Wi-Fi scan does not list my network | It is 5 GHz-only here, or hidden. The ESP32-C6 has no 5 GHz radio; choose *Other / hidden network* and type the name. |
| The phone says the setup network has no internet | True — it has none. Tap through and stay connected; open `http://192.168.4.1` yourself. |
| The page stopped responding after saving Wi-Fi | Expected: the device is retuning its radio. Find it at `http://comfoair-xxxx.local` or in your router's client list. |
| Settings refuse to save with *forbidden* | The setup network is on the air; only the Wi-Fi form works there. Or, on your network, you are changing Wi-Fi with no password set — see [Who may change what](/guide/configuration#who-may-change-what). |
| The browser keeps asking for a password I forgot | Hold the button 5 to 15 seconds after the device has started (soft reset). |
| Settings say they were written by a newer firmware | You installed an older version. Reinstall the newer one, or do a hard reset. |
| The Update page will not install | Set a web interface password first; or the image is for another device; or the last update is still in its probation (30 s after its web server started). |
| Home Assistant shows every reading unavailable | `<prefix>/bus` is `offline`: the unit is silent on CAN, or the device lost the broker (`<prefix>/status`). |
| The device looks dead after pressing the button | It was powered up with the button held and is in the bootloader. Power-cycle with the button released. |
| Solid blue LED | The first setup window ran out. Short press, or power-cycle, for another 5 minutes. |
