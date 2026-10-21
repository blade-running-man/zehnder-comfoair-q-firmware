# comfoair_led — the status indication: phase-to-picture tables, a pure frame resolver, and the WS2812 + heartbeat driver

> This is documentation, not code — re-read the source before relying on a detail.

The device has one RGB pixel (WS2812) and, on some boards, one plain status LED. This component
decides what the owner sees for each `device_phase_t` and for the three overlays that can
temporarily take over the pixel (button gesture, OTA upload, identify), and then drives the
hardware. It is two halves in one directory: a **pure** half (`comfoair_led.h`,
`comfoair_led_map.c`, `comfoair_led_pattern.c`) that turns (phase, overlay, board caps, policy,
time) into one frame and is host-tested, and an **impure** driver (`comfoair_led_driver.h`,
`comfoair_led_driver.c`) that owns the RMT channel, the power gate, the heartbeat pin and a 25 ms
render task.

## Responsibility

Owns:
- The look alphabet: 7 rhythms (`comfoair_led_rhythm_t`), 11 looks (`comfoair_led_look_t`), and
  the phase → look and look → (colour, rhythm) tables.
- The overlay priority (BUTTON > OTA > IDENTIFY > phase) and how each overlay is drawn.
- The BUTTON overlay's wire format (`comfoair_led_button_arg()`), declared once, here.
- `comfoair_gesture_tier_t` — declared here because this component draws it; `comfoair_button`
  includes `comfoair_led.h` only for this type.
- Gamma, the brightness ceiling, the quiet-hour cap, the boot grace window, the heartbeat pip.
- The WS2812 power sequencing (data low before gate up; blank, latch, release data, then gate down).

Does NOT do:
- Decide the phase — `comfoair_phase` / `comfoair_phase_monitor` do, and push it via
  `comfoair_led_set_phase()` once a second.
- Decide the gesture tier or know the 5 s / 15 s thresholds — `comfoair_button` (thresholds in
  `comfoair_config.h`) hands over a tier and that tier's length.
- Name a GPIO or a byte order — everything arrives in the `board_t` from `components/board/`;
  the colour permutation is `board_rgb_positions()` (pure, tested in `test/test_board_rgb_order`).
- Persist or expose settings — nothing calls `comfoair_led_policy_set()` today (see Notes).

## Public API

`include/comfoair_led.h` (pure; requires only `comfoair_phase`):

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_LED_TICK_MS` = 25 | Render period, part of the contract: every duration is argued in ticks (SPARK on = 10 ticks, heartbeat pip = 4). 40 Hz gives BREATHE 160 steps per 4 s with no visible staircase. The driver's loop period IS this macro. |
| `COMFOAIR_LED_CEILING_DEFAULT` = 51 | 20 % of 255. One WS2812 at full white is over half the board's measured ~106 mA budget; 20 % is visible in a dark room without being a floodlight. |
| `comfoair_led_look_of(phase)` | Total over the 14 phases; a non-phase value answers `COMFOAIR_LOOK_FAULT` (never darkness — darkness looks like a healthy/dead device). Any task, no state. |
| `comfoair_led_style_of(look)` | The look table (r, g, b, rhythm); out-of-range answers the FAULT row (red SOLID). Public so "no two looks share colour+rhythm" is checkable from outside. |
| `comfoair_led_resolve(phase, ovr, caps, policy, now_ms)` | The whole frame for one tick. Pure and deterministic. `ovr` may be NULL; kind NONE, or BUTTON with tier NONE, falls through to the phase. Unknown kind → FAULT. `now_ms` is uptime (`esp_timer_get_time()/1000`). |
| `comfoair_led_override_strongest(slots, count)` | Max over `kind` (the enum order is the priority); NULL if none raised. Returned pointer aliases `slots`. Public so the priority is host-testable even though the driver holds the slots. |
| `comfoair_led_button_arg(tier, tier_ms)` | `static inline`. Packs tier in bits 0..7, tier length in bits 8..31; `tier_ms` saturates at 0xFFFFFF (~4 h 39 min) so it can never clobber the tier bits. Callers MUST use it, never pack by hand. |
| `comfoair_led_scale(channel, brightness)` | `channel * brightness / 255` — the byte that reaches the wire. Public because the brightness floor of 1 does not survive this for channels < 255 (see Notes). |
| `comfoair_led_gamma(linear)` | 2.2 gamma table; 0→0, 255→255, non-decreasing, never above its argument (so applying the ceiling after it still bounds the result). |

Key types: `comfoair_led_override_t {kind, arg, since_ms}` (`arg` = OTA percent 0..100 or packed
BUTTON arg; `since_ms` = when the current BUTTON tier began, ignored for OTA/IDENTIFY);
`comfoair_led_caps_t {has_mono, has_rgb}`; `comfoair_led_policy_t {ceiling, quiet}`;
`comfoair_led_frame_t {r, g, b, brightness, mono_on}` (logical channels; `mono_on` before board
inversion). `comfoair_led_override_kind_t` is declared low-to-high priority: NONE, IDENTIFY, OTA,
BUTTON. `COMFOAIR_GESTURE_COUNT` is not a tier; `comfoair_button.c` static-asserts against it.

`include/comfoair_led_driver.h` (device only; includes `board.h`, so callers need `board`):

| Symbol | Contract |
| --- | --- |
| `comfoair_led_init(const board_t *board)` | Once, from `app_main`. `board` is borrowed and must outlive the device (it is `board_get()`'s static). Returns `ESP_ERR_INVALID_ARG` (NULL), `ESP_ERR_INVALID_STATE` (second call; changes nothing), `ESP_ERR_NO_MEM` (render task not created; nothing is indicated, gate dropped). `ESP_OK` also for a board with no indicators (no task created) and for "RMT failed, heartbeat only" (logged at ERROR). Every error is handled by logging and continuing. |
| `comfoair_led_set_phase(phase)` | Any task, not ISR. Idempotent, cheap; call on every evaluation. Initial value is `COMFOAIR_PHASE_BOOT`. |
| `comfoair_led_override_push(kind, arg)` | Any task, not ISR. One slot per kind: a re-push replaces. Stamps `since_ms` — except a BUTTON push whose `arg` equals what the slot holds (same tier, so the ramp must not restart). IDENTIFY self-clears 5 s after its LAST push. NONE / out-of-range kinds are ignored. |
| `comfoair_led_override_clear(kind)` | Any task, not ISR. Clearing an empty slot is a no-op. |
| `comfoair_led_policy_set(ceiling, quiet)` | Any task, not ISR. `ceiling == 0` is a physical off switch (blank, data low, gate down); heartbeat untouched. |

## Implementation

Files:
- `comfoair_led_map.c` — the two tables, as exhaustive `switch`es with no `default:`.
- `comfoair_led_pattern.c` — rhythm envelopes, gamma table, grace window, quiet cap, overlay
  resolution, `override_strongest`, `scale`.
- `comfoair_led_driver.c` — whole file inside `#ifdef ESP_PLATFORM`; RMT/led_strip, gate,
  heartbeat, spinlock-guarded state, render task.

Resolve flow (`comfoair_led_resolve`): pick style from the overlay (BUTTON → `button_style(tier)`
with RAMP rhythm; OTA → white PROGRESS; IDENTIFY → white SOLID) or from the phase; during the first
300 000 ms of uptime, NO_BROKER and BUS_SILENT (looks *inferred from absence*) get the WORKING
(blue) colour but keep their rhythm; compute the envelope; on a board without mono LED, force the
SPARK flash into every look; ceiling = `policy.ceiling`, capped to 8 when `quiet`; `brightness =
gamma(envelope) * ceiling / 255`, floored to 1 when lit and ceiling > 0; `mono_on` = 100 ms of every
1000 ms; a board without RGB gets r=g=b=brightness=0.

Rhythm periods (`comfoair_led_pattern.c`): BREATHE 4000 ms (smoothstep, not `cosf`, to avoid libm);
BLINK 1000 ms 50 %; DOUBLE 120 on / 180 off / 120 on in 2000 ms; PROGRESS 1000 ms with duty = percent;
RAMP = `(now - since) * 255 / tier_ms`, full at the boundary or when `tier_ms == 0`; SPARK 250 ms at
level 255 every 10 000 ms. `_Static_assert`s pin the pip and flash to whole, ≥2 tick counts.

Palette (`comfoair_led_map.c`): white = transitional, blue = network, amber = degradation, red =
cannot self-recover, green only as ONLINE's rare flash. Three collapses: TRIAL+CONNECTING → WORKING,
SETUP_AP+RECOVERY_AP → SETUP, NET_REJECTED+NET_LOST → NET_BROKEN (same owner action each).
Button tier colours: SETUP white, SOFT amber, HARD red.

Driver lifecycle: `init` drives mono LED to "off", drives RGB data low, raises the gate (if any)
and waits `power_gate_settle_ms`, creates the RMT strip and sends a blanking frame (the WS2812 latch
is undefined at power-up), then starts the task (`RENDER_PRIO` 3 — below CAN and MQTT — stack
3072). Each tick `render()` copies phase/policy/slots under `s_lock`, expires IDENTIFY, resolves
with caps built from what actually came up, writes the mono pin every tick (XOR with
`status_led_inverted`), and `apply_colour()` sends the pixel only if it differs from `s_last`.

Concurrency: one `portMUX_TYPE` spinlock; setters do a struct-sized critical section; the render
task copies out under the same lock, so a frame is never built from half a push. Gate changes
(`rgb_power`) happen only on the render task (or in `init` before it exists) and may `vTaskDelay`
for the settle/latch waits — the heartbeat pauses for that long.

DO NOT:
- Turn the tables into arrays or add `default:` — `-Werror=switch` on the native build is the
  guard that a new phase/look gets a row.
- Drop the `style` initialiser in `resolve()` because the switch is "exhaustive" — the hole is a
  value outside the enum.
- Replace modular `now - since` with signed comparisons (ms counter wraps at 49.7 days).
- Make the zero-length ramp dark — dark reads as "the firmware never saw the finger".
- Add a confirming flash to the ramp — the button pushes the flash as separate overlay events.
- Lower `SPARK_ON_MS`/`SPARK_LEVEL` for discretion — at the old 50 ms / level 64 the flash was
  brightness 2/255 and invisible on the real board; discretion is bought by rarity.
- Drop the brightness floor of 1 while keeping the "skip identical frame" check — they are one
  decision (a lit frame must never compare equal to darkness).
- Remember `s_last` after a failed `led_strip_refresh` — the pixel would sit wrong until the
  picture changed.
- Drive the gate down with data high, or reverse "level first, then direction" in `drive()`.
- Use `pdMS_TO_TICKS` for the waits — at `CONFIG_FREERTOS_HZ=100` it rounds 5 ms to 0 (a busy
  spin); `ticks_at_least()` rounds up and never returns 0.
- Resync the render deadline on any lateness, or render from `next` — only a whole-period lag
  resyncs; otherwise the 25 ms grid becomes 30 ms.
- Gate the power by "frame is dark" — only the ceiling moves the gate (ONLINE is dark 97.5 % of
  the time and would pay a settle wait per flash).

## Dependencies

- `REQUIRES comfoair_phase` only (public header names just `device_phase_t`). Everything else is
  `PRIV_REQUIRES board esp_driver_gpio esp_timer espressif__led_strip`, on purpose: listing them
  publicly would drag `esp_driver_gpio` into `comfoair_button`'s requirements. `espressif__led_strip`
  is the managed-component name; its manifest is `src/idf_component.yml` (a root-level manifest is
  silently ignored).
- Depended on by: `src/main.cpp` (`comfoair_led_init(b)` before the bus and network),
  `comfoair_phase_monitor` (`set_phase`), `comfoair_http/comfoair_http_ota.c` (OTA overlay push/clear),
  `comfoair_button` (`comfoair_button.c` packs `button_arg`; `comfoair_button_task.c` and
  `comfoair_button_reset.c` push/clear the BUTTON overlay; `comfoair_button.h` uses
  `comfoair_gesture_tier_t`).

## Tests

- `test/test_led_pattern/test_led_pattern.cpp` — 28 cases over the pure half: look uniqueness,
  phase totality and unknown-phase → FAULT, priority and push-order independence, ramp monotonicity
  and boundary, ceiling/quiet/zero-ceiling, grace window (and CONNACK refusal not softened),
  heartbeat, no-mono and no-RGB degradation, gamma, `button_arg` packing, tick divisibility, and
  `scale` never turning a lit frame into wire darkness.
- `test/test_button_poller` and `test/test_button_gesture` use `comfoair_led_button_arg` /
  `comfoair_gesture_tier_t` from the button side; `test/test_board_rgb_order` covers the colour
  permutation the driver hands to led_strip.
- NOT covered on the host: all of `comfoair_led_driver.c` — the BUTTON no-restamp rule, IDENTIFY
  expiry, power sequencing, frame dedup, the real task period. These are hardware scenarios in
  `docs/hardware-verification/07-indication-and-button.md` (HV-LED-*, HV-BTN-*).

## Notes

- **Looks are not phases.** 14 phases cannot get 14 distinct pictures from 7 rhythms × a few
  colour groups, so three collapses are written down by owner action; the surviving invariant is
  "no two looks share colour and rhythm". A new phase must collapse into an existing look or name
  what the owner does differently.
- **Rhythm over colour**: peripheral vision resolves movement, not hue, so rhythm carries the
  message at a distance. Green is never steady because to a red-green colour-blind owner it reads
  as FAULT's red.
- **ONLINE is green SPARK, not darkness** — a dark pixel is also what a dead RGB path shows.
- **Quiet dims FAULT and BROKER_REJECTED too**: nothing this device reports is a safety issue.
- **Grace window softens colour only**, and only for looks inferred from absence; a stated refusal
  (BROKER_REJECTED) or a start error is never softened. `/api/status` still reports the real phase.
- **BUTTON outranks OTA** because the owner's finger is on the device; a factory-reset countdown
  hidden behind a progress bar erases a device by feel. A fixed total order (not last-writer-wins)
  keeps the picture independent of the scheduler.
- **The floor of 1 does not reach the wire alone**: `scale(ch, 1)` is 0 for any channel < 255.
  ONLINE's flash survives quiet hours only because green is (0, 255, 0); dimming the palette would
  silently kill it — `test_a_lit_frame_never_reaches_the_wire_as_darkness` guards this.
- **Board with no gate**: ceiling 0 only blanks the frame; the WS2812 still draws quiescent current.
- **RMT failure is not retried** (reboot is the recovery); if raising the ceiling fails to recreate
  the strip, power is taken back and colour stays off.
- **Unused today**: nothing pushes `COMFOAIR_LED_OVR_IDENTIFY` and nothing calls
  `comfoair_led_policy_set()`, so devices run at ceiling 51 with quiet off.
- The OTA phase look alone draws dark (PROGRESS at 0 % with no arg); the HTTP OTA handler pushes
  the OTA overlay too, which carries the percentage and appears on the next 25 ms tick.
