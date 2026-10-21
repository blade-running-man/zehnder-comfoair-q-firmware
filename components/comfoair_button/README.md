# comfoair_button — the physical button gesture: a pure decider plus the poller that carries it out

> This is documentation, not code — re-read the source before relying on a detail.

The device has one button, and on the M5Stack NanoC6 it is GPIO9, which is also the ESP32-C6
download strap. That button is the owner's only way back from a device they can no longer
reach: a short press opens the setup window, a 5 s hold forgets the network and the UI password,
and a 15 s hold is a factory reset. This component turns a sequence of pin samples into exactly
one of those outcomes, in a way that cannot erase a device by accident (a slipped finger, a bag
leaning on the panel, a scheduler stall, a boot with a finger on the strap). The decision half is
pure and host-tested. The impure half reads the pin, drives the LED overlay, erases NVS and
restarts the chip.

## Responsibility

Owns:
- The gesture thresholds. This is the only copy: 60 ms debounce, 1 s, 5 s, 15 s, and 25 s
  "stuck", in `comfoair_button.c` and `comfoair_button.h`.
- The gesture state machine: debounce, the anchor at the first pressed sample, the dead zone,
  the stuck-button cutoff, deafness at boot until the first released sample, and wrap-safe and
  backwards-clock-safe timing.
- What the LED should show while a button is held (tier, ramp length, confirming flash), as a
  plan. `comfoair_button_plan_draw()` builds it; the task only executes it.
- The poller task: the 20 ms sampling loop, the board's active-low inversion applied once at the
  sample, and the GPIO pull configuration.
- Carrying out an erasing tier: NVS reset with one retry, clearing the log ring on a hard reset,
  OTA confirm, the three confirming flashes, the release-gated `esp_restart()`.

Does NOT do:
- Name a pin or polarity. Those come from `board_t` (`components/board/`), passed into
  `comfoair_button_start()`.
- Open the setup window itself. It calls a function pointer that `src/main.cpp` sets to
  `comfoair_net_open_setup_window`, and `comfoair_net` owns the provisioning machine.
- Decide which namespaces a tier erases. That is `comfoair_config`
  (`comfoair_config_nvs_reset()`, `comfoair_config_reset_erases()`).
- Draw anything or own tier colours. `comfoair_led` owns `comfoair_gesture_tier_t`, the BUTTON
  overlay, its packing (`comfoair_led_button_arg()`) and the colours (amber for SOFT, red for
  HARD).
- Retract retained Home Assistant discovery topics on a hard reset. This is a known gap. If a
  retraction is ever added, it belongs in `comfoair_button_erase_and_restart()` before the erase,
  with its own bounded wait.

## Public API

`include/comfoair_button.h` is pure and builds on the host:

| Symbol | Contract |
| --- | --- |
| `comfoair_button_t` | Decider state. The caller owns the storage, but the fields are private: do not read them. Everything comes back by value from `step`/`sample`. One per button, touched from one task only, so there is no lock. |
| `comfoair_button_init(s)` | Puts the machine in `PH_UNTIL_RELEASED`. Nothing can be decided until a released sample is seen. This is the download-strap guard. |
| `comfoair_button_step(s, pressed, now_ms)` | One sample. `pressed` is already in button terms (true = finger on it). `now_ms` is a monotonic uint32 ms clock. All comparisons are elapsed-time, so they survive the 49.7-day wrap. A timestamp lower than the previous one counts as no time passing: the state is unchanged and the previous tier is returned. Samples must come faster than the 60 ms debounce. A wider gap while the button is up reads as a release. `s` must be non-NULL and initialised. |
| `comfoair_button_sample(s, pressed, now_ms)` | **What a poller must call.** Wraps `step`. If the gap since the last sample is at least the debounce, it first feeds a synthetic released sample stamped at `s->last_ms` and drops its result. The press therefore ends at the last moment the poller could vouch for, and two presses either side of a stall cannot be glued into one erasing hold. |
| `comfoair_button_out_t` | `gesture`: the outcome committed by this sample, NONE on almost every sample, at most one per press. `tier`: what releasing now would do. `tier_since_ms`/`tier_ms`: the start and length of the current window (since + len = next threshold). `tier_changed`: the tier changed in either direction, including into NONE. |
| `comfoair_button_plan_draw(out, now_ms, flash)` | Turns an `out` into `{clear, push, arg}` for the BUTTON overlay. A change to NONE gives clear and no flash. A change into a tier starts the flash. A non-NONE tier is pushed on **every** sample. While the flash is up, `arg` carries length 0 (full brightness). `flash` is the caller's `comfoair_button_flash_t {on, since_ms}`. |
| `comfoair_button_release_confirmed(&n, pressed)` | Debounced "finger is off": true after `COMFOAIR_BUTTON_RELEASE_SAMPLES` consecutive released samples, and any pressed sample resets the count. Saturating counter. The caller zeroes `n` first. Used only by the reboot gate. |
| `comfoair_button_tier_erases_everything(tier)` | True only for `COMFOAIR_GESTURE_HARD`. Written as an exhaustive switch, so `-Werror=switch` in `[env:native]` forces an answer for any new tier. |

| Constant | Value | Why |
| --- | --- | --- |
| `COMFOAIR_BUTTON_DEBOUNCE_MS` | 60 | Not chosen for contact bounce (< 5 ms). It covers induced pulses, because the GPIO9 trace runs beside the WS2812 data line and the IR LED. |
| `COMFOAIR_BUTTON_POLL_MS` | 20 | Three samples inside the debounce. Exactly 2 ticks at `CONFIG_FREERTOS_HZ=100`. 0.4 % threshold error on 5 s. |
| `COMFOAIR_BUTTON_RELEASE_SAMPLES` | ceil(60/20) = 3 | Derived from the two above rather than written out, so it cannot drift. |
| `COMFOAIR_BUTTON_FLASH_MS` | 100 | The LED render task ticks every 25 ms. A shorter flash could be missed entirely. |

`include/comfoair_button_task.h` is device-only and includes `board.h`, so a caller needs `board`
in its own REQUIRES:

| Symbol | Contract |
| --- | --- |
| `comfoair_button_start(board, on_setup_requested)` | Called once from `app_main`. `board` is borrowed and must outlive the device (it is `board_get()` static storage). Configures the pin as input with a pull matching the polarity, no interrupt. Logs a warning if `button_is_download_strap`. Creates task `comfoair_btn` (priority 4, 2560 B stack), which runs for the life of the device. There is no stop. Errors the caller must handle, all by logging and carrying on: `ESP_ERR_INVALID_ARG` (NULL board), `ESP_ERR_NOT_SUPPORTED` (`button == GPIO_NUM_NC`, a legitimate board with no gesture), `ESP_ERR_INVALID_STATE` (already started), `ESP_ERR_NO_MEM` (the task did not start, so no gesture is read), or any `gpio_config()` error unchanged. `on_setup_requested` may be NULL, in which case a short press only logs. It runs on the button task, at most once per gesture, and may block. |

## Implementation

Files:
- `comfoair_button.c`: the pure decider (`window_of()` threshold table, the phase machine) and
  the three pure poller decisions (`sample`, `plan_draw`, `release_confirmed`,
  `tier_erases_everything`).
- `comfoair_button_task.c`: `#ifdef ESP_PLATFORM`. Pin read, `button_task` loop, `draw()`,
  `execute()`, `comfoair_button_start()`.
- `comfoair_button_reset.c`: `#ifdef ESP_PLATFORM`. `comfoair_button_erase_and_restart()`, the
  confirming flashes, the release-gated restart.
- `comfoair_button_internal.h`: the private seam between task and reset
  (`comfoair_button_pin_pressed()`, `comfoair_button_wait_a_poll()`,
  `comfoair_button_erase_and_restart()`). It is not in `include/` on purpose.

Threshold windows (`window_of()`, measured from the first pressed sample):

| Held for | Tier |
| --- | --- |
| < 60 ms | NONE (not a press yet) |
| 60 ms – 1 s | SETUP |
| 1 s – 5 s | NONE (dead zone: release = cancel) |
| 5 s – 15 s | SOFT (Wi-Fi + owner/UI password; app/broker settings survive) |
| 15 s – 25 s | HARD (every namespace, plus log ring cleared) |
| ≥ 25 s | stuck: back to `PH_UNTIL_RELEASED`, tier NONE, nothing until released |

Phases: `PH_UNTIL_RELEASED` (boot and stuck) → `PH_IDLE` → `PH_MAYBE_PRESS` (needs 60 ms of
continuous contact, no majority vote) → `PH_DOWN` → `PH_MAYBE_RELEASE`. Coming back down within
60 ms returns to DOWN with the anchor unmoved. Coming back down later is a new press, and the
pending outcome is dropped. Staying up for 60 ms commits `window_of(held_ms)`. `held_ms` is frozen
at the first released sample, so a release at 4.99 s confirmed at 5.05 s gives no reset. While
the release is being debounced, the reported tier uses the frozen length.

Task loop (`button_task`): `now_ms()` (`esp_timer_get_time()/1000`, the same clock as
`comfoair_led`), then pin read, then `comfoair_button_sample()`, then `draw()`, then `execute()`,
then `vTaskDelay(20 ms)`. SETUP calls the callback. SOFT/HARD call
`comfoair_button_erase_and_restart()`.

Erase path (`comfoair_button_reset.c`): `comfoair_config_nvs_reset()` with one retry. If it
still fails: log the error and the requested namespaces, clear the overlay, **return without
restarting**. If it succeeds: log the namespaces, call `comfoair_log_reset()` (HARD only), then
`comfoair_ota_runtime_confirm_now()` (a failure is logged and the reset continues). Then 3
flashes of 250/250 ms in the tier colour. Then wait, with no bound, until
`comfoair_button_release_confirmed()` says the finger is off, blinking and logging every 5 s
meanwhile. Then clear the overlay, log the stack high-water mark, call `comfoair_mdns_goodbye()`,
wait `COMFOAIR_MDNS_GOODBYE_MS` (150 ms), and call `esp_restart()` whatever happened.

Invariants:
- Every outcome is committed by the release edge plus 60 ms of released samples, never by
  reaching a threshold. Only running firmware can see a release, so the strap cannot be held
  through the restart that follows a gesture.
- A button that reads pressed on the first sample stays mute until released.
- At most one gesture per press. A starved sampler loses a gesture and never invents one.
- Exactly one task touches `comfoair_button_t` and the flash state. Nothing is shared, so there
  are no locks.

DO NOT:
- Call `comfoair_button_step()` from the poller. Use `comfoair_button_sample()`, or a stall
  while the button is held glues two presses into a SOFT erase.
- Act on the synthetic sample's result inside `sample()`. It could fire a reset from a
  made-up release.
- Rebase `last_ms` on a backwards timestamp. A rewound anchor turns a 15 s hold into a 5 s one.
- Anchor the press at the end of the debounce, or drop the "new press outside the debounce"
  guard in `PH_MAYBE_RELEASE`.
- Flash on a change into NONE, or push only on changes. The two rules in `plan_draw` are both
  pinned by tests.
- Raise `POLL_MS` (at 30 ms, one missed wake is already a release), or switch to
  `vTaskDelayUntil` (catch-up bursts).
- Swap the order to `execute()` before `draw()`. The committing sample also takes the tier to
  NONE, and the clear would overwrite the confirmation flashes.
- Restart on a failed erase. The log ring is RAM and is the only evidence left.
- Restart on one released reading. The owner is invited to press again during the flashes
  (HV-BTN-13).
- Call `mdns_free()` directly or wait on the goodbye. It has unbounded waits, which would put an
  unbounded wait between an erased NVS and the reboot.
- Add a GPIO interrupt or a `stop()`.

## Dependencies

- `REQUIRES comfoair_led` (public): `comfoair_gesture_tier_t` and `comfoair_led_button_arg()`
  are part of this component's interface and are declared there, once.
- `PRIV_REQUIRES board comfoair_config comfoair_log comfoair_mdns comfoair_ota_runtime driver
  esp_timer esp_system`. `board` is private so the host suites that include the decider do not
  inherit it.
- `comfoair_net` is deliberately absent. PlatformIO's dependency finder ignores `#ifdef`, so
  including `comfoair_net.h` would drag the radio into the host suites (it once broke the build
  with `esp_event.h` not found). The function pointer replaces that include.
- Used by: `src/main.cpp`, which calls `comfoair_button_start(b, comfoair_net_open_setup_window)`
  after the LED is up and before CAN and the network, so the reset exists even when the network
  branch fails. `src/CMakeLists.txt` lists the component. `platformio.ini` `[env:native]` adds
  `-I components/comfoair_button/include`.

## Tests

- `test/test_button_gesture/test_button_gesture.cpp` (26 cases): tiers and thresholds,
  announce-once, ramp length, the stuck button at 25 s, release-commits-not-threshold, the
  boot-pressed strap case, bounce and short pulses, gap-equals-release, frozen ramp during
  release debounce, repeated and backwards timestamps, the 32-bit wrap, a 60 s hold.
- `test/test_button_poller/test_button_poller.cpp` (15 cases): `comfoair_button_sample()` stall
  handling, `plan_draw()` clear/push/flash rules, `release_confirmed()`,
  `tier_erases_everything()`.
- Both suites compile every `.c` in this directory, so the two device files must stay fully
  inside `#ifdef ESP_PLATFORM`. The same applies to `comfoair_mdns` and `comfoair_ota_runtime`
  sources reached through `comfoair_button_reset.c`.
- Not covered by any host suite: `comfoair_button_task.c` and `comfoair_button_reset.c` (pin
  polarity, task creation, erase/retry, OTA confirm, flashes, the reboot gate loop, the mDNS
  goodbye ordering). These are checked on hardware in
  `docs/hardware-verification/07-indication-and-button.md` (HV-BTN-*). The 2560 B stack size is
  unverified until HV-BTN-07 reads the logged high-water mark.

## Notes

- **Why the dead zone (1–5 s).** It gives four seconds in which any release is a clean cancel.
  A slipped finger or a lean on the panel then neither opens an AP nor erases a network.
- **Why stuck = 25 s and not "HARD forever".** Something pressing the button indefinitely must
  never erase anything. The machine rearms after one clean release, so the device keeps its
  factory reset without needing a power cycle.
- **Why the reboot wait is unbounded.** If the button stays held after an erase, the device
  keeps ventilating with its settings already erased, and the next power cycle sorts that out.
  Restarting with the strap held would leave a chip in the ROM loader and nothing to read.
- **Why OTA confirm comes before the restart.** In the first 30 s after an OTA reboot the image
  is on probation, and a restart would roll back to the previous slot. A reset would silently
  become a downgrade with the credentials already erased.
- **The no-reboot rule still holds.** "Nothing reboots because a peer is absent" is about peers.
  This restart is the owner's explicit gesture. The other `esp_restart()` in the firmware is
  OTA's.
- **Partial erase.** The NVS store continues past a bad namespace and returns the first error.
  On failure some namespaces may already be gone while the running device serves its boot-time
  copy. The log names what was requested, and the device is consistent again after a power
  cycle.
