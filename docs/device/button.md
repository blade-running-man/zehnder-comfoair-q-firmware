# Button

`comfoair_button` turns the board's one button into three outcomes: open the setup window, a soft
reset, and a hard (factory) reset. The owner-facing instructions are in
[First setup](/guide/first-setup) and [Troubleshooting](/guide/troubleshooting); this page is the
implementation.

## Structure

| File | Role |
| --- | --- |
| `include/comfoair_button.h`, `comfoair_button.c` | the **pure decider**: debounce, tiers, release, stuck button, the LED draw plan. No ESP-IDF, no clock, no pin. Host suites `test/test_button_gesture` and `test/test_button_poller` |
| `include/comfoair_button_task.h`, `comfoair_button_task.c` | the poller task: reads the pin, applies the board's polarity, feeds the decider, pushes the LED overlay, executes the outcome |
| `comfoair_button_reset.c` | what happens after a release decided to erase: erase, confirm, wait for release, restart |
| `comfoair_button_internal.h` | the three calls shared by the task and reset files (`comfoair_button_pin_pressed()`, `comfoair_button_wait_a_poll()`, `comfoair_button_erase_and_restart()`); not public |

The pin is configured in `comfoair_button_start()` and read, with the board's polarity applied,
only in `comfoair_button_pin_pressed()`; the decider sees a `pressed` boolean and a millisecond
timestamp.

## The poller

`comfoair_button_start(board, comfoair_net_open_setup_window)` is called from `app_main` right
after the LED, **before** the CAN bus and the network, so the factory reset exists even on a
device whose network never came up. It configures the pin as an input with a pull-up when
`button_inverted` is true (the NanoC6 case), otherwise a pull-down, and starts task
`comfoair_btn` (priority 4, 2560-byte stack).

| Constant | Value | Meaning |
| --- | --- | --- |
| `COMFOAIR_BUTTON_POLL_MS` | 20 ms | sampling period |
| `COMFOAIR_BUTTON_DEBOUNCE_MS` | 60 ms | agreement needed for a press or a release |
| `COMFOAIR_BUTTON_RELEASE_SAMPLES` | 3 | consecutive released samples that count as "the finger is off" |
| `COMFOAIR_BUTTON_FLASH_MS` | 100 ms | full-brightness flash when a tier is entered |

There is no stop function: a poller that can be stopped can be stopped in the middle of a
fifteen-second hold. Start-up errors (`ESP_ERR_NOT_SUPPORTED` for a board with no button,
`ESP_ERR_NO_MEM`, a `gpio_config()` error) are logged and the device continues without a
gesture; the only way back from a forgotten UI password is then USB.

## Gestures and timings

The hold is measured from the **first pressed sample**, not from the moment the debounce expired.
Every outcome is committed on the **release** (after 60 ms of agreement), never on reaching a
threshold.

| Held for | Tier | On release | LED while held |
| --- | --- | --- | --- |
| under 60 ms | none | nothing (not a press) | phase |
| 60 ms – 1 s | SETUP | open the setup window; erases nothing | white ramp |
| 1 s – 5 s | none (dead zone) | nothing | phase |
| 5 s – 15 s | SOFT | soft reset, then restart | amber ramp |
| 15 s – 25 s | HARD | hard reset, then restart | red ramp |
| 25 s or more | none (stuck) | nothing; the button must be released before the next press counts | phase |

The ramp fills over the length of the current tier and reaches full brightness exactly at the next
threshold, so it shows "how much longer". On entering a tier the overlay flashes at full
brightness for 100 ms (`comfoair_button_plan_draw()` pushes the tier with a length of 0, which the
LED resolver draws at full brightness). When the tier falls back to none the poller clears the
overlay. The thresholds live only in `comfoair_button.c`; the LED is handed a tier
and a length, never the numbers.

Robustness rules in the decider:

- A button that reads **pressed on the very first sample** is mute until it has been seen released
  once (`PH_UNTIL_RELEASED`). This is what separates a gesture from a boot with a finger on the
  button.
- `comfoair_button_sample()` (what the poller calls) treats a gap between samples of at least the
  debounce (60 ms) as a release, stamped at the last time the poller could vouch for. A stall that
  swallows a release therefore ends the press instead of gluing two presses into one long hold.
  Such a stall is real here: an NVS write from a web handler stops everything that is not in IRAM.
  Without the rule, three seconds held, a swallowed release and three more seconds would add up to
  a SOFT reset nobody asked for. The direction is deliberate: a starved poller loses a gesture the
  owner repeats, instead of firing one that costs the configuration. The rule lives in the pure
  wrapper, not in the decider (which cannot tell "not seen" from "did not happen") and not in the
  task (device-only code no test reaches); `test_two_presses_glued_by_a_starved_poller_do_not_erase`
  in `test/test_button_poller` pins it.
- A timestamp that goes backwards is ignored.
- Time arithmetic is unsigned "elapsed since", correct across the 49.7-day wrap.

## The setup press

A SETUP release calls `comfoair_net_open_setup_window()`, which queues an event for the
provisioning task and returns. The provisioning machine then opens the setup window from whatever
state it is in (5 minutes on a device that holds credentials or has had a window close before,
otherwise the full 15), and **erases nothing**: not the credentials, not
the known-good pair, not the failure being shown. It is the only way out of `setup-expired` short
of a reboot. While that access point is on the air the device counts as unclaimed, so every
`POST` except `/api/provision` is refused. See [Networking](/device/networking#setup-window).

A press that arrives before `comfoair_net_start()` has created its queue is dropped; pressing again
a moment later works.

A short press also re-anchors the window's 60-minute ceiling (`window_opened_at` in
`components/comfoair_provision/comfoair_provision.c`). The ceiling bounds what **client activity**
can extend, so that a phone left on the open access point cannot hold it up for ever. A finger on
the button is not a client: whoever pressed it is standing at the device and could as well hold it
for fifteen seconds. Refusing a window because the previous one opened an hour ago would make a
power cut the answer to "the page timed out while I fetched the router password".
`test_a_second_press_reopens_a_window_the_ceiling_had_closed` (`test/test_provision_setup_window`)
pins it.

The button task asks for the window through a function pointer that `src/main.cpp` passes to
`comfoair_button_start()`, not by calling `comfoair_net_*` itself: PlatformIO's dependency finder
does not evaluate `#ifdef`, so including `comfoair_net.h` here would drag the radio into every host
suite that includes the decider (see [Testing](/development/testing#callbacks-not-includes)).

## The two-level reset

Erasure is by NVS **namespace** (`nvs_erase_all()`), decided by
`comfoair_config_reset_erases()`, never by a list of keys. Namespaces are described on
[Configuration storage](/device/config-storage).

| Namespace | Contents | Soft (5 s) | Hard (15 s) |
| --- | --- | --- | --- |
| `cfg_wifi` | Wi-Fi pair, known-good pair, `win_closed`, `no_addr` | erased | erased |
| `cfg_owner` | UI password record | erased | erased |
| `comfoair` (legacy) | Wi-Fi keys an earlier firmware wrote | erased | erased |
| `cfg_app` | broker, prefix, discovery flag, device name, schema number, the four durations, `manual_perm` | kept | erased |
| log ring (RAM) | the `/api/log` buffer | kept | cleared |

The soft reset therefore means "forget the network and the owner, keep the integration"; the hard
reset returns the device to factory state. Because the hard tier answers for every namespace in
the enumeration, a namespace added later is erased without changing the reset code.

::: warning Retained MQTT topics are not cleaned
A hard reset does not retract the Home Assistant discovery topics retained on the previous
owner's broker. They stay until removed there.
:::

### Sequence after an erasing release

`comfoair_button_erase_and_restart()` runs on the button task:

1. `comfoair_config_nvs_reset(tier)`; on failure, **one** retry.
2. If the erase still failed: log the error and the namespaces that were meant to go, clear the
   overlay, **do not restart**, and keep running so `GET /api/log` can be read. Some namespaces may
   already be gone; the running device keeps serving what it loaded at boot until the next power
   cycle. The restart exists to bring the device up on an erased configuration, and there is none;
   the log ring is RAM, so a restart would destroy the only channel that can tell the owner the
   gesture worked and the erase did not.
3. On success: log which namespaces were erased; on the hard tier, clear the log ring.
4. `comfoair_ota_runtime_confirm_now()`. For 30 s after `comfoair_ota_runtime_start()` armed the
   confirmation timer, a freshly updated image is still on probation and a restart would roll back
   to the previous image; this confirms it first. A failure is logged and the reset continues. (If
   the HTTP server never started, the timer was never armed and this call confirms nothing, so the
   restart rolls back; see [OTA and system](/device/ota-and-system#rollback).)
5. Three confirmation flashes in the tier's colour, 250 ms on / 250 ms off (1.5 s).
6. Wait until the pin reads released for 3 consecutive 20 ms samples, toggling the overlay on every
   sample meanwhile and logging a warning every 5 s. There is no time limit.
7. `comfoair_mdns_goodbye()`, a fixed 150 ms wait (`COMFOAIR_MDNS_GOODBYE_MS`), then `esp_restart()`
   whether or not the goodbye finished. `mdns_free()` can block for ever, so it runs on a task of
   its own (see [Networking](/device/networking#mdns)); a stale name in a neighbour's cache expires
   in minutes, a factory reset that does not reboot does not.

## The GPIO9 boot-strap caveat

On the NanoC6 the button is GPIO9, which is also the ESP32-C6 download strap
(`button_is_download_strap = true`). The ROM samples it at reset: if it is held low at that
moment the chip enters the serial bootloader and the firmware does not run. The firmware treats a
software restart the same way, which is why the reset path waits for the release before calling
`esp_restart()`.

::: danger Do not hold the button while powering on
Holding the button while power is applied enters the bootloader. Every gesture is a hold
**after** the device has booted.
:::

The firmware keeps clear of the strap in three ways:

- No tier fires on reaching a threshold; only a release commits, and only running firmware can see
  a release.
- A button already pressed at the first sample is ignored until released.
- The reset path restarts only after three consecutive released readings (step 6 above), so a
  second press during the confirmation flashes cannot hold the strap through the restart.

**Why the restart is gated on the pin, not on a timer.** The strap is latched on every release of
reset, a software restart included, so `esp_restart()` with the button held lands in the ROM
loader: no CAN, no Wi-Fi, no light, a device that looks dead. The gate reads the pin immediately
before the call, with no upper bound on the wait. A button held for ever (a box leaning on the
panel) leaves a running device with its settings erased on flash, which the next power cycle
resolves; restarting anyway would leave a chip in the ROM loader.

**Why three samples, not one reading.** Every other decision in the button requires 60 ms of
agreement, and the confirmation flashes are driving the WS2812 line at exactly this moment. A
single reading that lands in the contact bounce of a new press would restart the chip with a
finger on the strap. The predicate, `comfoair_button_release_confirmed()`, lives in the pure half
(`comfoair_button.c`) so that `test/test_button_poller` holds it to the same 60 ms; under
`#ifdef ESP_PLATFORM` nothing would test it.

On boot `comfoair_button_start()` logs a warning that the button is also the download strap, and
the board line `app_main` logs names the button's GPIO with "(download strap: hold AFTER boot
only)".
