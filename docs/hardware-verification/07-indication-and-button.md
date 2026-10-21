# 07 · Indication and button

The RGB LED, the heartbeat LED, the phase they show, and the button. The first two sections settle
two board facts the firmware still guesses; the last section **erases settings** and is walked last
of the whole run.

The LED looks (as-built [Indication](/device/indication#led-looks)): white solid = `boot`, white
progress = `ota`, blue blink = `setup-ap`/`recovery-ap`, blue breathe = `trial`/`connecting`, blue
double = `net-rejected`/`net-lost`, blue solid = `setup-expired`, amber breathe = `no-broker`, amber
blink = `broker-rejected`, amber double = `bus-silent`, red solid = `fault`, a green spark every 10 s
= `online`. The brightness ceiling and the quiet window exist in the driver but nothing sets them,
so the LED always runs at the default ceiling.

## Board facts: colour order and power gate

As-built: [Board](/device/board), `components/board/board_m5stack_nanoc6.c` (both values are marked
"not verified on hardware").

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-LED-01 | **WS2812 colour order.** Produce a logical pure red and a logical pure green and note the colour each **appears** as. **Green:** the `online` spark. **Red:** the button's hard-reset ramp — device booted, press and hold; from 15 s to 25 s the LED draws the `fault` colour (255, 0, 0) rising from dark to full; at 25 s the overlay drops by itself (the stuck-button rule). **Keep holding past 25 s and only then release** — that release does nothing ([HV-BTN-05](#button-without-erasing)); **a release between 15 s and 25 s is the hard reset and erases everything.** One colour alone cannot decide it — use the pair | Red looks red and green looks green: the current `BOARD_RGB_ORDER_GRB` is right. Otherwise read the true order from the table below and change **one constant in one file** (`.color_order` in `board_m5stack_nanoc6.c`), with the date of this run in the comment beside it | LED; stopwatch | — |
| HV-LED-02 | **Power-gate settle time** (`power_gate_settle_ms` = 10, an estimate with no documentary source). Twenty **cold** power cycles (power fully removed — a reset may leave the gate up). Optionally: a meter on the switched rail (U6 output) with the firmware running; an oscilloscope on that rail, triggered by GPIO19 | All 20 starts show a correct first picture (white, not a random colour) and the following looks are right from their first frame. The rail reads 3.3 V while running (0 V means the gate never rose and no colour can work). With a scope: record the actual rise time. If any start shows a wrong or lost first frame, raise the value until 20 of 20 are clean and record the measured number in `board_m5stack_nanoc6.c` | LED; meter; scope | USB (meter, scope) |

| Red appears as | Green appears as | The chip's true order | `.color_order` |
| --- | --- | --- | --- |
| red | green | GRB — the guess is right | unchanged |
| red | blue | BRG | `BOARD_RGB_ORDER_BRG` |
| green | red | RGB | `BOARD_RGB_ORDER_RGB` |
| green | blue | BGR | `BOARD_RGB_ORDER_BGR` |
| blue | red | RBG | `BOARD_RGB_ORDER_RBG` |
| blue | green | GBR | `BOARD_RGB_ORDER_GBR` |

The packer is set to GRB, so a logical red puts 255 in the **second** byte on the wire and shows
which channel the chip reads there; a logical green puts it in the **first**. The pair fixes all
three positions. The six rows are pinned by `test/test_board_rgb_order`.

Why the button and not the `fault` look: on a healthy board the phase never reaches `fault`. It is
set only when a start branch of `app_main` fails (`a_startup_branch_failed()` in
`components/comfoair_phase/comfoair_phase.c`), and those are driver, task and memory errors — CAN,
for one, fails only in `twai_new_node_onchip()`, its callback registration, its task creation or
`twai_node_enable()` (`components/comfoair_can/comfoair_can_api.c`), none of which needs a
transceiver, so a removed CAN base still boots to a running driver. The hard tier of the button
draws its ramp in the `fault` style (`button_style()` in
`components/comfoair_led/comfoair_led_pattern.c`), so it is the same logical red, reachable without
reflashing. No phase look gives a second pure primary to use instead: white is all three channels,
amber is (255, 120, 0) and blue is (0, 80, 255).

## The looks and the heartbeat

As-built: [Indication](/device/indication).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-LED-03 | Walk through the phases (provoke them with the scenarios of [05](./05-provisioning-and-recovery), [05b](./05b-network-outages), [06](./06-mqtt) and [02](./02-can-bus-and-polling)) and write down the colour and rhythm of each | No two looks share both colour and rhythm; inside one colour the rhythms are pairwise different — this is the colour-blindness check | LED | as the scenarios |
| HV-LED-04 | At any phase, photograph the LED and read `GET /api/status` at the same moment | `phase` and the look name the same state (table above). A mismatch is a distribution defect: both come from one call | photo; `curl` | — |
| HV-LED-05 | The device in its real place. Walk past without stopping or looking straight at it | The owner names the **group** (network / degraded / fault / all well) correctly in 4 of 5 passes. A failure means revising the colour table, not the code | eyes | — |
| HV-LED-06 | Any state. Count the blue status-LED flashes over 10 s and over 60 s; optionally a logic analyser on GPIO7 for 10 s | 10 in 10 s, **60 ± 1** in 60 s; each 100 ms on / 900 ms off (± 10 ms on an analyser, stable from period to period). The rhythm does not change with the phase, does not double in `fault`, does not stop during OTA or during a button gesture. It proves the scheduler and the render task are alive — **not** that the CAN or MQTT tasks are | stopwatch; analyser | USB (analyser) |
| HV-LED-07 | Fresh boot, watch the log | `comfoair_led: indication up: heartbeat yes, colour yes`. If instead the RGB channel failed to start, the device must keep working: the heartbeat runs, the web UI comes up, no reboot | serial log | USB |
| HV-LED-08 | Device `online`. Watch the RGB LED for 60 s | The green spark comes **6 times**, each time visibly lit. A missing spark with a working RGB LED means the brightness floor or the frame de-duplication is broken, not the LED | eyes | — |
| HV-LED-09 | Device `online`. Pull the CAN loom (driver stays up); stopwatch until the `bus-silent` verdict (about 90 s) | The colour changes within **1 s** of the verdict — the phase monitor really wakes every second | LED; `GET /api/log` | unit |
| HV-LED-10 | A steady phase, nothing touched for 10 minutes; read `GET /api/log`, filter tag `phase`. Then stop the broker | No new `phase:` line in the 10 minutes. After the broker has been down 30 s: **exactly one** line `phase: online -> no-broker (bus fresh)`. One line per tick would be 86 400 a day and push the transition out of the 80-line ring | `GET /api/log` | broker |
| HV-LED-11 | Open Settings and scan for Wi-Fi networks while watching the LEDs | The RGB colour may freeze for the seconds of the scan (an accepted cost); the heartbeat keeps going, the web UI stays, no reboot. A stopped heartbeat would mean the scheduler stalled | LEDs | — |
| HV-LED-12 | A fresh device with Wi-Fi configured and **no** broker. Wait 2 minutes | Phase `online` (green spark), **not** `no-broker`: a device without a broker is how it ships, not a degradation | LED; `GET /api/status` | — |
| HV-LED-13 | Device `online`. Stop the broker and start it again within 30 s | The colour never turns amber, not even briefly (the 30 s hold-off); the flap shows only as `mqtt.reconnects` grown by one | LED; `GET /api/status` | broker |

## Button, without erasing

As-built: [Button](/device/button#gestures-and-timings). Every gesture is a hold **after** boot
([HV-SAFE-01](./00-setup-and-safety)).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-BTN-01 | Press the button right after boot (so it reads pressed at the poller's first sample) and hold 25 s | The LED never changes, no reboot, Wi-Fi intact: a button pressed at the first sample is mute until released. After releasing, a new press counts normally. Without this rule a button jammed by a lid would raise the open access point again and again | LED; `GET /api/status` | — |
| HV-BTN-02 | Device configured. Press and release after 3 s | The LED returns to the phase look while held in the 1–5 s dead zone; nothing happens on release; `GET /api/config` still needs the password; `uptime_ms` keeps growing (no restart) | LED; `curl` | — |
| HV-BTN-03 | Short press (under 1 s) within the first 2 s after power-on, before the network is up | Nothing; the device keeps booting. A press before the network's queue exists is dropped on purpose; a press a few seconds later opens the window | `GET /api/log` | — |
| HV-BTN-04 | Hold through the 5 s and 15 s boundaries, watching the RGB LED (here hold on past 25 s and release as in HV-BTN-05; releasing inside a tier belongs to the destructive section) | At each boundary a short full-brightness flash (about 0.1 s), then the new tier's colour rising from dark: white ramp under 1 s, amber ramp 5–15 s, red ramp 15–25 s. Each flash must be noticeable | eyes | — |
| HV-BTN-05 | Hold for 30 s and release | At **exactly 25 s** the overlay drops and the phase look returns — with **no** flashes. The release does nothing: password still required, Wi-Fi intact, no restart | LED; `GET /api/config` | — |
| HV-BTN-06 | During a 15 s hold, load the device: the web UI open, `GET /api/state` in a loop, live MQTT | The tier boundaries still fall at 5 s and 15 s (± 0.1 s). A stalled poll longer than 60 ms reads as a release — the one failure no host test can see | stopwatch; LED | broker |

## The two-level reset — destructive, last

**After this section the device must be set up again.** Do not start until every other scenario is
recorded. Write down first (HV-SAFE-05): Wi-Fi SSID and key, broker host/port/user, prefix, device
name, UI password, the four durations. The soft reset erases the Wi-Fi and the owner's password;
the hard reset erases everything. Neither retracts anything on the broker — to leave a clean broker,
run [HV-HA-16](./06b-home-assistant) before the hard reset. As-built:
[two-level reset](/device/button#the-two-level-reset),
[sequence after an erasing release](/device/button#sequence-after-an-erasing-release).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-BTN-07 | **Soft reset.** Hold until the LED shows the amber ramp (5 s), release before 15 s, stopwatch from the release. Keep the button pressed again during the confirmation flashes and read `GET /api/log` while the device waits | Three **amber** flashes (250 ms on / 250 ms off), then the restart about 1.5 s after release. While the button is held the device does **not** restart; the log shows `comfoair_btn: soft reset done by button; erased:`, `namespace cfg_wifi`, `namespace cfg_owner` (and the legacy `comfoair` namespace), then every 5 s `reset done; waiting for the button to be released before restarting`. After releasing: `restarting; stack high water mark N bytes` — **record N** (under 512 means the 2560-byte stack must grow) — and the **normal application banner**, not the ROM bootloader | serial log; `GET /api/log`; stopwatch | USB |
| HV-BTN-08 | After the soft reset | `comfoair-XXXX` comes up; `GET /api/config` answers **without** a password; `mqtt_host`, `topic_prefix` and `device_name` are **the same as before**; `wifi_ssid` is empty. The log ring was kept until the restart and is empty after it (it lives in RAM) | `curl -s http://192.168.4.1/api/config` | phone |
| HV-BTN-09 | Provision the same network again | The device connects, MQTT reconnects, discovery is republished and the device returns in HA. The only trace in HA is the availability gap while it was being set up | HA | broker, HA |
| HV-BTN-10 | **Hard reset.** Set the device up again, broker live, device visible in HA, `dns-sd -B _http._tcp local` running. Hold until the red ramp (15 s), release before 25 s | Three **red** flashes, then the restart. The `ComfoAir Q XXXX` entry leaves the `dns-sd` list at once (the mDNS goodbye), and the device restarts within ~150 ms of the flashes ending even if the goodbye is lost — **no restart at all would be a severe defect, NVS being already erased**. After the restart everything is factory: setup access point, no password, no broker, prefix and name at their defaults. The log ring is cleared | serial log; `dns-sd`; `curl` | USB, laptop |
| HV-BTN-11 | During HV-BTN-10, watch the broker dump; afterwards check HA | **No empty payload is published** — a reset retracts nothing. `<prefix>/status` goes `offline` only through the will, after the broker's keepalive. The 76 retained discovery documents stay, and after deleting the card in HA and restarting HA it **comes back**. Clean the broker by hand, or next time withdraw first (HV-HA-16) | `mosquitto_sub -v -t '#'`; HA | broker, HA |
| HV-BTN-12 | Set up again; switch the broker **off**; hard reset | The reset happens regardless (three red flashes, restart). A broker that is not there does not block a reset | LED; serial log | USB, broker |
| HV-BTN-13 | **The strap check.** Set up again. Hold 8 s, release, and **immediately press again**, holding 10 s | No restart while the button is held; every 5 s `waiting for the button to be released before restarting`; after release the restart shows the **normal application banner**, not a ROM-bootloader line. This is the only check that a reset cannot drop the chip into download mode | serial log | USB |
