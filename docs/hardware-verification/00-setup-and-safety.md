# 00 · Setup and safety

Read and do this before any other scenario. Three facts decide whether a run leaves the device and
the ventilation unit as they were: the button is also a boot-strap pin, two of its gestures erase
settings, and the firmware **writes** to the unit.

## The button is a strap pin, and two holds erase

As-built: [Button](/device/button#the-gpio9-boot-strap-caveat), [two-level reset](/device/button#the-two-level-reset).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-SAFE-01 | Never hold the button while power is applied. To see what it looks like once: with the device unpowered, hold the button, apply power, keep holding 10 s, release | Nothing lit, no `comfoair-XXXX` network for a minute, `GET /api/status` unreachable, no application line on the serial console. **This is the ROM bootloader (GPIO9 is the download strap), not a fault.** Power-cycle without touching the button: the device boots normally | The board line of the boot log ends in `button=9 (download strap: hold AFTER boot only)`, and `comfoair_button` warns `the button is also the download strap: hold it AFTER boot …` | USB |
| HV-SAFE-02 | Know the gestures before touching the button: under 1 s opens the setup window; **5–15 s is a soft reset** (Wi-Fi and UI password erased); **15–25 s is a hard reset** (everything erased); 1–5 s and over 25 s do nothing. Every gesture commits on release | No hold is made "to see what happens". The erasing gestures are walked only in the destructive section of [07](./07-indication-and-button), last | The LED while held: white ramp (setup), amber ramp (soft), red ramp (hard) | — |

## Writes to the unit

As-built: [Commands](/protocol/commands), [PDO and RMI](/protocol/pdo-rmi#rmi-request-payloads).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-SAFE-03 | Before the first command of a run: watch the serial log, use a unit you can afford to stop, keep its breaker reachable. Send the first command by hand, alone | Every command frame goes to node `0x01` and unit SCHEDULE `0x15` or sensor-ventilation `0x1D`; the probe sends only `01 …` reads and one `80 03 01`. **If any frame goes to another unit — stop the run, do not continue, and record that first.** It is the one failure that can cost the unit | A CAN analyser if present; otherwise the unit's display and `GET /api/log` | unit, USB |

## Before the run

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-SAFE-04 | Run the host suites before going to the unit: `~/.platformio/penv/bin/pio test -e native` | All suites pass. A red host suite makes a hardware run meaningless | the `pio test` summary | laptop |
| HV-SAFE-05 | Record the baseline: `curl -s http://<ip>/api/system` (`device.firmware`, `device.partition`); photograph the unit's settings screens (flow presets, temperature profile, setpoints, imbalance, filter); write down Wi-Fi SSID and key, broker host/port/user, topic prefix, device name, UI password and the four durations | A baseline to compare against if the unit or the device behaves unexpectedly, and everything a reset or a withdraw item will need to restore | — | unit, laptop |
| HV-SAFE-06 | Keep the whole serial log of every run (`~/.platformio/penv/bin/pio run -e m5stack-nanoc6 -t upload -t monitor` for a USB flash, `pio device monitor` otherwise) and keep a run log as described in [Recording a run](/hardware-verification/#recording-a-run) | Each item ends as passed / failed / not run with the measured number | — | USB |
