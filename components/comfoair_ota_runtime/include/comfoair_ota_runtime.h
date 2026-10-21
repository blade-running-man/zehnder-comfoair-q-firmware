// The device half of OTA that is not the upload itself: confirming a freshly booted image, and
// restarting into a new one.
//
// ROLLBACK (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE): an image booted for the first time after an OTA
// is PENDING_VERIFY. If it reboots before comfoair_ota_runtime confirms it, the bootloader boots
// the previous slot instead. The flag lives in the BOOTLOADER, which an OTA never replaces, so the
// first build with it must be flashed over USB; until then nothing here can roll anything back.
#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call once from app_main after the HTTP server has started (the one path that can deliver the
// NEXT update). If the running image is PENDING_VERIFY, arms a one-shot timer that confirms it
// after COMFOAIR_OTA_CONFIRM_DELAY_MS of uptime; otherwise only logs the partition and its state.
//
// DO NOT make confirmation wait for the CAN unit, Wi-Fi association or the MQTT broker: a peer
// that is absent must never decide that this firmware is broken ("nothing reboots because a peer is
// absent"). What rollback protects against is an image that crashes or boot-loops -- which never
// survives 30 s of uptime with its web server up.
#define COMFOAIR_OTA_CONFIRM_DELAY_MS 30000
esp_err_t comfoair_ota_runtime_start(void);

// True while the running image is PENDING_VERIFY. esp_ota_begin() refuses during that window, so the
// upload handler answers COMFOAIR_OTA_UNCONFIRMED instead of a flash error.
bool comfoair_ota_runtime_unconfirmed(void);

// Confirms the running image NOW instead of at COMFOAIR_OTA_CONFIRM_DELAY_MS, for a caller that is
// about to restart on purpose and has its own proof that this image is alive. `reason` is logged
// ("confirmed early: <reason>") so the log says who cut probation short.
//
// WHY IT EXISTS: a restart during probation makes the bootloader boot the PREVIOUS slot. Any
// deliberate restart inside the first 30 s after an update -- the button's reset tiers today --
// would therefore silently become a downgrade. The caller's proof stands in for the 30 s: the
// image has already run long enough to do whatever led to that restart.
//
// DO NOT call it from anything a peer can trigger by being absent or by crashing the image: the
// timer is what catches a boot loop, and this skips it.
//
// Idempotent: on an image that is not on probation it does nothing and answers ESP_OK. It stops
// the pending confirmation timer. TASK CONTEXT: any task; it rewrites one otadata sector (tens
// of ms). Errors: whatever esp_ota_mark_app_valid_cancel_rollback() returns -- the image is then
// still on probation and the next restart rolls back; the caller logs it and carries on.
esp_err_t comfoair_ota_runtime_confirm_now(const char *reason);

// Restarts after `delay_ms`, from a short-lived task -- never inside the HTTP handler, which must
// first finish sending its answer. Safe to call twice (the second is ignored).
esp_err_t comfoair_ota_runtime_restart_later(unsigned delay_ms);

#ifdef __cplusplus
}
#endif
