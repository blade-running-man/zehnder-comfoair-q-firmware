# comfoair_ota_runtime — confirming a freshly booted OTA image, and restarting into a new one

> This is documentation, not code — re-read the source before relying on a detail.

The upload half of OTA (`comfoair_ota` decides, `comfoair_http` streams the `.bin` into the next
slot) ends with the boot partition pointing at a new image. Two device-side jobs remain, and they
live here: (1) after the reboot, the new image is **on probation** (`ESP_OTA_IMG_PENDING_VERIFY`
under `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`) and must be marked valid once it has proven it is
alive, otherwise the next restart makes the bootloader boot the previous slot; (2) the restart into
the new image must happen from somewhere other than the HTTP handler, which first has to finish
sending its answer. The component is deliberately tiny: one state read at boot, one one-shot timer,
one restart task, two atomic flags and one mutex.

## Responsibility

Owns:
- Reading the running partition's OTA state once at boot and logging it (`running ota_N, state …`).
- The probation clock: a one-shot `esp_timer` that confirms a `PENDING_VERIFY` image after
  `COMFOAIR_OTA_CONFIRM_DELAY_MS` of uptime (counted from the call, which is after HTTP is up).
- The "still on probation" flag the upload handler reads to refuse early.
- Early confirmation for a caller that is about to restart deliberately (`confirm_now`).
- The delayed, once-only restart into a freshly installed image (`restart_later`).

Does NOT do:
- Accept, validate or write an upload — `comfoair_http` (`comfoair_http_ota.c`) streams it;
  `comfoair_ota` holds the pure decisions and the `COMFOAIR_OTA_UNCONFIRMED` outcome / its
  `409` status line.
- Set the boot partition — `comfoair_http_ota.c` calls `esp_ota_end` + `esp_ota_set_boot_partition`.
- The button's own reset restart — `comfoair_button_reset.c` calls `esp_restart()` itself; it only
  calls `confirm_now()` here first.
- Enable rollback — that is `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` in `sdkconfig.defaults`,
  compiled into the bootloader.
- Decide when it is safe to arm the clock — `src/main.cpp` calls `start()` only after
  `comfoair_http_start()` succeeded.

## Public API

Header: `include/comfoair_ota_runtime.h`.

| Symbol | Contract |
| --- | --- |
| `COMFOAIR_OTA_CONFIRM_DELAY_MS` = `30000` | Probation length. 30 s is long enough that an image which crashes or boot-loops never survives it with its web server up, and short enough that the owner's next upload is not blocked for long. It is pure uptime, NOT "CAN/Wi-Fi/MQTT came up" (see Notes). |
| `esp_err_t comfoair_ota_runtime_start(void)` | Call **once** from `app_main`, after the HTTP server started. Not `PENDING_VERIFY` (or state unreadable) → logs, returns `ESP_OK`, nothing armed. `PENDING_VERIFY` → creates the mutex, sets the unconfirmed flag, arms the timer, returns `ESP_OK`. Errors: `ESP_ERR_NOT_FOUND` (no running partition), or the `esp_timer_create`/`esp_timer_start_once` error — the image then stays on probation and the next restart rolls back. The caller (`src/main.cpp`) logs and carries on. Not guarded against a second call. |
| `bool comfoair_ota_runtime_unconfirmed(void)` | True while the running image is on probation. Lock-free atomic read, any task (the httpd task in practice). `esp_ota_begin()` refuses during that window, so the upload handler answers `COMFOAIR_OTA_UNCONFIRMED` (409) before reading the file. |
| `esp_err_t comfoair_ota_runtime_confirm_now(const char *reason)` | Confirm immediately, for a caller about to restart on purpose that has its own proof the image is alive. Idempotent: not on probation → `ESP_OK`, touches nothing. Otherwise stops the timer and marks valid under the mutex; logs `image confirmed early: <reason>` (`NULL` → `"no reason given"`). Any task; blocks for one otadata sector erase+write (tens of ms). Error = whatever `esp_ota_mark_app_valid_cancel_rollback()` returned; the image is still on probation and the next restart rolls back — the caller logs and continues. DO NOT call it from anything a peer can trigger by being absent or by crashing the image. |
| `esp_err_t comfoair_ota_runtime_restart_later(unsigned delay_ms)` | Spawns a short-lived task that sleeps `delay_ms` then `esp_restart()`s. Any task; returns immediately. Second and later calls are ignored (`ESP_OK`, and their `delay_ms` is ignored too). `ESP_ERR_NO_MEM` if the task could not be created — the armed flag is cleared again so a later call can retry; the caller (`comfoair_http_ota.c`) logs and drops the "restarting" LED picture. |

## Implementation

Files:
- `comfoair_ota_runtime.c` — everything; the whole file sits inside `#ifdef ESP_PLATFORM`.
- `include/comfoair_ota_runtime.h` — the contract above.
- `CMakeLists.txt` — `REQUIRES app_update esp_timer`, no `PRIV_REQUIRES`.

State (all file-static):
- `s_unconfirmed` (`atomic_bool`) — written by the boot path / esp_timer task / caller of
  `confirm_now`, read by the httpd task.
- `s_restart_armed` (`atomic_bool`) — once-only guard for `restart_later`, taken with
  `atomic_exchange` so two concurrent callers cannot both see "not armed".
- `s_confirm_timer` — the one-shot timer, `ESP_TIMER_TASK` dispatch, name `ota_confirm`.
- `s_confirm_lock` — a mutex in static storage (`xSemaphoreCreateMutexStatic`), so it cannot fail.

Flow:
1. `start()`: `esp_ota_get_running_partition()` → `esp_ota_get_state_partition()`. A read error
   (typically `ESP_ERR_NOT_FOUND`: no CRC-valid otadata entry for this slot) is logged as
   `state none (…)` and treated like VALID. After a USB flash the expected log is
   `running ota_0, state valid`, because the bootloader writes `otadata[0]` = VALID before the app runs.
2. On `PENDING_VERIFY`: create the mutex, **then** set `s_unconfirmed`, **then** create and start the
   timer. Logs `image on probation; confirming in 30000 ms`.
3. Timer fires → `confirm()` → `mark_valid(NULL)`; `confirm_now()` → stop timer → `mark_valid(reason)`.
4. `mark_valid()`: take mutex, re-read the flag, call `esp_ota_mark_app_valid_cancel_rollback()` only
   if still set; clear the flag only on success; release.
5. `restart_later()`: `xTaskCreate(restart_task, "ota_restart", 4096 bytes, delay, prio 1)`;
   the task logs `restarting into the new image` and calls `esp_restart()`.

Invariants:
- The flag is set before the timer exists, so there is no instant where an upload is told "confirmed"
  while `esp_ota_begin()` would still refuse.
- The mutex is created before the flag can become true; `confirm_now()` returns on a false flag
  before touching the mutex, which is why the mutex may legitimately be `NULL` on a non-probation boot.
- The flag is cleared only after a successful mark-valid; on failure it stays set (truthfully).

DO NOT:
- Confirm on the spot when the timer fails to arm. Confirming at boot is exactly what rollback
  exists to prevent: an image that crashes at second 10 would be kept forever. A missing timer only
  means the next restart rolls back and the owner uploads again.
- Retry a failed `mark_valid`. The next reboot rolling back to the previous slot is the safe outcome.
- Replace the mutex with `esp_timer_stop()`'s return value. `ESP_ERR_INVALID_STATE` means "already
  fired", which includes "callback still running". Mark-valid is a read-modify-erase-write of
  otadata; a second confirmer landing inside the first one's sector erase can read a blank entry
  and pick the other slot as active.
- Move the timer callback into its own FreeRTOS task "to keep esp_timer free". It holds the esp_timer
  task for one sector write, once per boot after an update; a task would hold a stack for the whole
  uptime to do that.
- Shrink the restart task's 4096-byte stack (IDF counts bytes). `esp_restart()` runs the shutdown
  handlers (e.g. `esp_wifi_stop`, which logs) on that stack; an overflow still reboots, but the reset
  reason on `/api/system` would read "panic" after every update.
- Restart inside the HTTP handler — the answer must go out first (`comfoair_http_ota.c` uses
  `OTA_RESTART_DELAY_MS` = 1000).
- Remove the `#ifdef ESP_PLATFORM` wrapper (see Notes).

## Dependencies

- Requires: `app_update` (`esp_ota_ops.h`), `esp_timer`; uses FreeRTOS, `esp_log`, `esp_system`
  from the IDF core.
- Depended on by:
  - `src/main.cpp` — `comfoair_ota_runtime_start()` after a successful `comfoair_http_start()`.
  - `components/comfoair_http/comfoair_http_ota.c` (`REQUIRES` in its `CMakeLists.txt`) —
    `comfoair_ota_runtime_unconfirmed()` before the slot lookup, `comfoair_ota_runtime_restart_later()`
    after a successful install.
  - `components/comfoair_button/comfoair_button_reset.c` (`PRIV_REQUIRES`) —
    `comfoair_ota_runtime_confirm_now("hard|soft reset by button")` just before its own `esp_restart()`.
- No cycle: this component requires nothing of the project's.

## Tests

- **No host suite.** Every call reaches otadata in flash, `esp_timer` or the restart path.
- `test/test_button_gesture` and `test/test_button_poller` compile `comfoair_ota_runtime.c` on the
  host (pulled in through `comfoair_button`), but it compiles to nothing there; they exercise none of it.
- `test/test_ota` covers the neighbour `comfoair_ota`'s `COMFOAIR_OTA_UNCONFIRMED` → `409 Conflict`
  mapping, not this component.
- On hardware: `docs/hardware-verification/08-ota-and-system.md` — HV-OTA-01 (`state valid` after
  USB flash), HV-OTA-08 (`restarting into the new image` ~1 s after the answer), HV-OTA-10/11
  (probation log and confirmation after 30000 ms), HV-OTA-13 (409 on an upload inside the window).
- Not covered anywhere automated: the timer/`confirm_now` race, the failure paths of mark-valid and
  timer creation, actual rollback after a crash during probation.

## Notes

- **Why confirmation waits only for uptime with HTTP up.** Rollback protects against an image that
  crashes or boot-loops. Tying confirmation to the CAN unit, Wi-Fi association or the broker would let
  an absent peer declare good firmware broken and roll it back ("nothing reboots because a peer is
  absent"). HTTP is the one requirement because it is the only path that can deliver the *next*
  update; an image whose server never starts is never confirmed, and is rolled back on the next
  restart — deliberately.
- **Why `confirm_now` exists.** Any deliberate restart inside the 30 s window would make the
  bootloader boot the previous slot, silently turning e.g. a button reset into a downgrade (with the
  credentials already erased). The button reset path has run a five-second hold and an NVS erase, which
  is the proof of life the timer waits for.
- **Why the whole `.c` is under `#ifdef ESP_PLATFORM`.** `comfoair_button_reset.c` includes this
  header, and PlatformIO's host dependency finder does not evaluate that file's own `#ifdef`, so every
  host suite that includes `comfoair_button.h` also compiles this `.c` — where `esp_ota_ops.h` does not
  exist. Same shape as `comfoair_button_reset.c`.
- **Rollback needs a USB-flashed bootloader.** The rollback flag is compiled into the bootloader,
  which OTA never replaces. Until a build with `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` has been
  flashed over USB, images never enter `PENDING_VERIFY` and this component only logs.
  `CONFIG_BOOTLOADER_APP_ANTI_ROLLBACK` is intentionally not set (it burns eFuses; manual downgrade is
  a supported path).
- **The two flags differ in direction.** `s_unconfirmed` may go true→false once; `s_restart_armed`
  goes false→true once and only falls back on task-creation failure. There is no way to cancel an
  armed restart.
