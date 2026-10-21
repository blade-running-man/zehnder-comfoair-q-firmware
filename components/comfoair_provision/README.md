# comfoair_provision — the pure Wi-Fi provisioning state machine

> This is documentation, not code — re-read the source before relying on a detail.

Decides what the device's radio should be doing given what has happened to it: station,
access point, both or off; whether the setup window is open; whether a newly saved pair of
credentials must be rolled back; and which of four failure answers to show the owner. It has
no ESP-IDF, no clock, no NVS and no radio. Time arrives as a parameter on every call, which is
what lets "the window closed while the owner was fetching the router password" be one line of
a host test instead of fifteen minutes in front of a device. The impure half that owns
`esp_wifi`, NVS and the event queue is `comfoair_net`.

## Responsibility

Owns:
- The provisioning state (`comfoair_prov_state_t`) and the radio mode it implies (`comfoair_prov_mode()`).
- **The unclaimed rule:** the device counts as provisioned only when it has credentials AND no
  open access point is on the air (`comfoair_prov_is_provisioned()`).
- **The window rule:** setup window 15 min, extended by client activity, ceiling 60 min,
  anchored on the access point coming *up* (not on boot); after a window has ever closed,
  every window that comes back is the short one (5 min).
- **The fallback:** the access point returns by itself after 30 min of sustained failure of the
  right kind (network absent / wrong password / refused — never no-address).
- **The sealed-device rule:** `setup_window_ms == 0` means a window that never closes.
- The trial/rollback of a newly saved pair, retry backoff, and the reason-code classifier.
- The single spelling of each failure answer (`comfoair_prov_failure_name()`).

Does NOT do:
- Touch the radio, NVS or a clock — `comfoair_net` (`comfoair_net.c`) feeds events, ticks it
  every 250 ms (`TICK_MS`), drains actions and performs them (`comfoair_net_radio.c`).
- Store SSIDs/PSKs, or the persisted flags — `comfoair_config` (`comfoair_config_nvs_*`,
  including `comfoair_config_nvs_save_prov_flags()`); the machine only knows *whether*.
- Decide who may write over HTTP — `comfoair_policy` (`comfoair_http_check()`), which consumes
  `is_provisioned` via `comfoair_net_is_provisioned()`.
- Map state to an LED/phase — `comfoair_phase` / `comfoair_phase_monitor`.
- Project status JSON — `comfoair_wire` (uses `comfoair_prov_failure_name()` and the enums).

## Public API

All in `include/comfoair_provision.h`. Every function is NULL-safe (`s == NULL` returns early or
returns a benign answer). No `esp_err_t` anywhere; nothing can fail. Not thread-safe: the
caller serialises all access (see Concurrency).

| Symbol | Contract |
| --- | --- |
| `comfoair_prov_config_defaults(cfg)` | Fills the defaults below. |
| `comfoair_prov_init(s, cfg, boot)` | `cfg` NULL = defaults, `boot` NULL = nothing stored. Takes no timestamp on purpose — nothing is anchored on boot. Arms `ACTION_CONNECT` if credentials exist; enters `FALLBACK_AP` if `boot->address_never_arrived` and credentials exist. |
| `comfoair_prov_tick(s, now)` | The clock. Every deadline is decided here, in fixed order (window close → trial timeout → address timeout → retry → fallback → no-address escape). A caller that stops ticking stops the machine. |
| `comfoair_prov_on_ap_started/_stopped` | The AP is actually on/off the air (not "asked for"). `_started` anchors the window and latches which length it gets; a repeated start is ignored. |
| `comfoair_prov_on_associated` | `WIFI_EVENT_STA_CONNECTED`. Not success; starts the address timeout. Does not clear the standing failure. |
| `comfoair_prov_on_connected` | `IP_EVENT_STA_GOT_IP`. The only proof of a pair: clears failures, ends the window, emits `COMMIT` once per pair. |
| `comfoair_prov_on_disconnected(s, now, reason)` | `reason` is the raw `uint8_t` `wifi_err_reason_t`. Ignored entirely when no credentials are stored. Classifies, arms a retry. |
| `comfoair_prov_on_credentials_saved` | A pair was written to NVS. Starts `TRIAL`, resets backoff/failures/no-address fact, *replaces* any pending action with `CONNECT`. |
| `comfoair_prov_on_setup_requested` | Button short press. Opens a window from any state (re-arms the ceiling too), moves only `WINDOW_CLOSED → UNCONFIGURED`, erases nothing. |
| `comfoair_prov_on_client_seen` | Extends the window only if it is *live* at `now` (not merely not-yet-ticked-closed). |
| `comfoair_prov_state` / `_mode` | NULL answers `WINDOW_CLOSED` / `MODE_OFF` (safe for the radio, wrong for a status page — do not project `state()` before init). |
| `comfoair_prov_is_provisioned` | `has_credentials && !ap_on_air`. The security gate. |
| `comfoair_prov_has_credentials` / `_has_known_good` | Facts, no values. |
| `comfoair_prov_window_is_open` | What the last tick decided (`ap_on_air && window_running`), so API, log and LED see the close at the same instant. Do not drive the radio from it — use `mode()`. |
| `comfoair_prov_window_remaining_ms(s, now)` | min(window, ceiling) remaining; `COMFOAIR_PROV_NEVER` for the sealed device; 0 when closed. |
| `comfoair_prov_window_has_closed` | Caller persists it; returned via `boot.window_closed_before`. |
| `comfoair_prov_address_never_arrived` | Caller persists it; returned via `boot.address_never_arrived`. |
| `comfoair_prov_should_roll_back` | True while `RESTORE_CREDENTIALS` is pending. |
| `comfoair_prov_failure` / `_failure_reason` / `_failure_name` | The answer, the raw reason byte, and its one spelling: `network-not-found`, `wrong-password`, `refused`, `no-address`, `none`. |
| `comfoair_prov_fallback_in_ms(s, now)` | Countdown to the fallback AP, or `NEVER` (always `NEVER` for no-address, when an AP is already up, or `fallback_after_ms == 0`). |
| `comfoair_prov_take_action(s)` | Returns and clears the single held one-shot. Call after every event and tick. |
| `comfoair_prov_classify(reason)` | Pure reason-code table (exposed so it can be pinned by number). |

Key types:
- `comfoair_prov_state_t`: `UNCONFIGURED, CONNECTING, CONNECTED, TRIAL, RETRYING, FALLBACK_AP,
  WINDOW_CLOSED`, then `COMFOAIR_PROV_STATE_COUNT` — a count, not a state; exists so tables and
  exhaustive switches can be checked against it (`test/test_phase/phase_inputs.h` static-asserts
  on it). Never return/store it.
- `comfoair_prov_failure_t`: `NONE, NETWORK_ABSENT, WRONG_PASSWORD, REFUSED, NO_ADDRESS`,
  `COMFOAIR_PROV_FAIL_COUNT` (same rule).
- `comfoair_prov_mode_t`: `OFF, ACCESS_POINT, STATION, AP_STA`. AP_STA so the answer to
  `POST /api/provision` still reaches the phone on the AP and a failed trial has somewhere to be shown.
- `comfoair_prov_action_t`: `NONE, CONNECT` (disconnect first if associated), `COMMIT_CREDENTIALS`
  (copy NVS pair to known-good slot), `RESTORE_CREDENTIALS` (put known-good back, one atomic write).
- `comfoair_prov_boot_t`: four bools the caller reads from NVS before init. `has_credentials`
  must mean "stored AND SSID non-empty" — a zeroed blob after `Forget Wi-Fi` is still a present key.
- `COMFOAIR_PROV_NEVER = UINT32_MAX`: "no end", distinct from 0 = "nothing running / closed".
- `comfoair_prov_t`: caller-allocated, opaque by convention, `static_assert(sizeof <= 96)`.

Defaults (`comfoair_prov_config_defaults`, all ms; nothing is clamped):

| Field | Value | Why / what 0 means |
| --- | --- | --- |
| `setup_window_ms` | 15 min | Enough to finish setup; an indefinitely open setup AP is a known attack class. 0 = sealed device, never closes (supported). |
| `setup_window_ceiling_ms` | 60 min | Bounds client-activity extension, measured from AP up. 0 = no ceiling. |
| `reopen_window_ms` | 5 min | Every window that comes back (after a close, at reboot, the no-address boot window). Short because the AP is open on a unit with the owner's data. 0 = those never close. |
| `fallback_after_ms` | 30 min | A router rebooting overnight must not raise an open AP; a week without the device is the other failure. 0 = AP never returns by itself AND disables the no-address escape — no way back. |
| `trial_timeout_ms` | 90 s | A working network gives an address well inside it. DO NOT 0 (every new pair rolled back next tick). |
| `address_timeout_ms` | 30 s | Associated-without-address becomes `NO_ADDRESS`. DO NOT 0 (reconnect loop). |
| `retry_backoff_min_ms` / `_max_ms` | 1 s / 30 s | Doubling backoff, no attempt limit, never a reboot. DO NOT min = 0 (reconnects every tick). |

## Implementation

Files:
- `comfoair_provision.c` — the deciding half: `init`, the events, `tick`, backoff, `note_failure`.
- `comfoair_provision_query.c` — the reading half: every question, `take_action`, `classify`,
  `failure_name`. Moves no state except clearing the action in `take_action`.
- `comfoair_provision_internal.h` — private (not in `include/`): the four rules both halves must
  ask identically: `comfoair_prov_remaining()`, `comfoair_prov_window_length()`,
  `comfoair_prov_window_open_at()`, `comfoair_prov_failure_earns_the_access_point()`.
- `CMakeLists.txt` — `idf_component_register(SRCS both .c, INCLUDE_DIRS include)`, no REQUIRES.

Flow: `init` → caller drains `CONNECT` (if credentials) → events and 250 ms ticks → after each,
caller reads `mode()`, drains `take_action()`, persists `window_has_closed()` and
`address_never_arrived()`.

Internals worth knowing:
- `elapsed()` / `comfoair_prov_remaining()` are unsigned `now - since` against a limit, never a
  stored deadline — correct across one 32-bit wrap (49.7 days).
- Every timestamp has a bool beside it; 0 is a legal time, never "unset".
- `set_action()`: only one action is held. `CONNECT` never displaces a pending `COMMIT`/`RESTORE`
  (durable writes outrank a nudge) and returns false; tick() then keeps the retry armed / keeps
  `associated` set so the nudge is retried, never lost. Only `on_credentials_saved` overwrites a
  pending write, deliberately (both durable actions refer to the pair that just changed).
- `window_is_the_short_one` is latched when the AP comes up (or on a setup press), from
  `window_ever_closed || has_credentials` — not read live, or saving credentials mid-window would
  cut the owner's 15 minutes to 5.
- `window_length()` checks the sealed zero first, and `window_open_at()` checks it before the
  ceiling — otherwise a sealed device whose first window once ran out would get the short window
  or a silent 60-minute lockout.
- `note_failure()` re-anchors `failing_at` only when the failure crosses the earns-the-AP line, so
  "sustained" is measured on one side of it and repeated same-kind failures do not restart it.
- `backoff_ms()` stops doubling above `UINT32_MAX/2` (overflow would wrap to 0); `attempts`
  saturates at 32.
- `init` sets `credentials_committed = false` regardless of `has_known_good`: after a power cut
  mid-trial the stored pair may be untried; one redundant COMMIT is the price.
- Window closing in `FALLBACK_AP` → `RETRYING`, restarting the fallback clock only for failures
  that earn the AP (not for the no-address boot window).
- `classify()`: 200/201/210/211/212 → NETWORK_ABSENT; 14/15/202/204 → WRONG_PASSWORD;
  8 (`ASSOC_LEAVE`, raised by our own `esp_wifi_disconnect()`) → NONE; everything else → REFUSED.

Concurrency: none inside. `comfoair_net` holds one `comfoair_prov_t` (`g_net_prov`) under a
recursive mutex (`g_net_machine`); all events go through its queue to one task (single writer — the
button task posts `EV_SETUP_REQUESTED` instead of calling in). The machine is read and released
before any radio/NVS work.

DO NOT:
- Read a clock in this component (breaks every host test).
- Add an SSID/PSK/string field to `comfoair_prov_t` (the 96-byte `static_assert` is the tripwire;
  the status projection is built from this struct).
- Turn interval checks into `now >= deadline` comparisons.
- Feed `is_provisioned` from "credentials stored" — an open AP on the air means unclaimed.
- Add reason 3 (`AUTH_LEAVE`) or 47 (`AP_INITIATED`) to the `FAIL_NONE` arm: they can be a router
  kicking us for good; not counting them toward the fallback can strand the device. Reason 8 is the
  only exception because this machine causes it.
- Change `classify()`'s default from REFUSED — an unlisted code must still count toward the fallback.
- Let `on_associated` clear the failure (a wrong PSK associates, then fails the handshake).
- Infer `address_never_arrived` from `failure() == NO_ADDRESS` (true 30 s into a router waking up).
- Drop the `has_credentials` guard in tick() step 5 because it is currently redundant — `Forget
  Wi-Fi` is exactly what makes the facts diverge.
- Give `*_COUNT` a case in a switch that acts on a value.

## Dependencies

Requires nothing (C headers only: `assert.h`, `stdbool.h`, `stdint.h`, `stddef.h`).

Depended on by: `comfoair_net` (REQUIRES; the only driver — events, tick, actions, status),
`comfoair_wire` (REQUIRES; status projection, `comfoair_prov_failure_name()`), `comfoair_phase`
(REQUIRES; `comfoair_prov_state_t`/`_failure_t` in its inputs), `comfoair_config`
(`comfoair_prov_boot_t` in `comfoair_config_nvs.h`), plus header-level references from
`comfoair_http` (client-seen path via `comfoair_net`), `comfoair_button`, `comfoair_mqtt`,
`comfoair_control`, `comfoair_phase_monitor` and `src/main.cpp` (comments/ordering only).

## Tests

- `test/test_provision/test_provision.cpp` — the main suite: unclaimed rule, window anchoring,
  extension, ceiling, sealed device, clock wrap, trial/rollback, commit-once, classifier by
  number, reason 8, no-address escape across a simulated reboot, fallback timing and duty cycle,
  backoff and saturation, dropped-nudge protection, NULL safety, struct size.
- `test/test_provision_setup_window/test_provision_setup_window.cpp` — the button short press
  (`on_setup_requested`) from every state (swept with `COMFOAIR_PROV_STATE_COUNT`), erases-nothing,
  long vs short window, ceiling re-arm, client-seen interplay, struct did not grow.
- Indirect: `test/test_phase` (state/failure sweeps), `test/test_wire` and
  `test/test_wire_status` (`failure_name` and enums), `test/test_config` (`comfoair_prov_boot_t`),
  `test/test_button_gesture` (setup request).

Not covered: the `comfoair_net` glue (event translation, 250 ms tick, action execution, flag
persistence) has no host suite; real radio behaviour lives in `docs/hardware-verification/`.

## Notes

- **Why TRIAL is its own state:** "credentials stored" is not "on the owner's network"; merging
  them makes a mistyped password look like success, and TRIAL is the only state a rollback leaves.
- **Why a first provisioning does not roll back:** there is no known-good pair; erasing what the
  owner picked would only make them redo it. The way back is the AP still up, then the fallback.
- **Why the no-address escape is a boot-time window, not the fallback:** the fallback excludes
  no-address by name (a slow DHCP after a router reboot must not raise an open AP), but a dead DHCP
  server would otherwise leave no network, no AP and no way back. So after 30 min of sustained
  no-address the machine sets a persisted flag; the next `init` enters `FALLBACK_AP` for the short
  window with the station still trying. An address or a new pair clears it.
- **Why a setup press may re-arm the ceiling:** the ceiling bounds what a *client on the open AP*
  can extend; a person at the button could equally hold it to erase the device, so refusing them
  a window gains nothing. The press does not loosen HTTP: while the AP is up `is_provisioned` is
  false, so the policy refuses every POST except `/api/provision`.
- **Why the window closes only in tick():** so the API, log and LED observe the close at one
  instant — a closing window must be observable, not a network that silently vanished.
- **Why `WINDOW_CLOSED` is only reachable without credentials:** a device with credentials keeps
  its station running; only an unconfigured device can go fully silent, and its way back is the
  reopen window at the next boot (or a button press).
