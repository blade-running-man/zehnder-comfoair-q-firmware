// Confirming a freshly booted image, and restarting into a new one (comfoair_ota_runtime.h).
//
// Board-only: every call here reaches otadata in flash or the restart path, so there is no host
// suite. What it decides is small on purpose -- one state read at boot, one timer, one flag.
//
// WRAPPED IN #ifdef ESP_PLATFORM ENTIRELY, a build fact and not taste: comfoair_button_reset.c
// includes this component's header, and the host build's dependency finder does not evaluate
// that file's own #ifdef -- so every suite that includes comfoair_button.h compiles this .c too,
// and esp_ota_ops.h does not exist there. The same shape as comfoair_button_reset.c itself.
#ifdef ESP_PLATFORM

#include "comfoair_ota_runtime.h"

#include <stdatomic.h>
#include <stdint.h>

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "comfoair_ota";

// Atomic because the writer (the esp_timer task) and the reader (the httpd task) differ.
static atomic_bool s_unconfirmed = false;
static atomic_bool s_restart_armed = false;
static esp_timer_handle_t s_confirm_timer;
// Serialises the two confirmers: the timer (esp_timer task) and comfoair_ota_runtime_confirm_now()
// (the button task). The IDF call is idempotent, but it is a read-modify-erase-write of otadata
// (esp_ota_ops.c:904-923), and a second reader landing inside the first one's sector erase reads a
// blank entry and may pick the OTHER slot as active. DO NOT replace this with esp_timer_stop()'s
// answer alone: ESP_ERR_INVALID_STATE there means "already fired", which includes "still running".
// Static, so it cannot fail to exist; created in start() before s_unconfirmed can become true.
static StaticSemaphore_t s_confirm_lock_mem;
static SemaphoreHandle_t s_confirm_lock;

static const char *state_word(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    case ESP_OTA_IMG_UNDEFINED: return "undefined";
    default: return "unknown";
    }
}

// Runs on the esp_timer task. esp_ota_mark_app_valid_cancel_rollback() rewrites one otadata
// sector (esp_ota_ops.c:944, through esp_ota_current_ota_is_workable), so this callback holds that task for one
// sector erase and write, tens of milliseconds -- once per boot after an update, never again.
// DO NOT move this into a FreeRTOS task "to keep esp_timer free": a task is another stack held
// for the whole uptime to do one write.
//
// `early` NULL is the timer; otherwise it is confirm_now()'s reason. Under s_confirm_lock, and the
// flag is re-read under it: whichever confirmer comes second finds the work done and writes nothing.
static esp_err_t mark_valid(const char *early)
{
    xSemaphoreTake(s_confirm_lock, portMAX_DELAY);
    esp_err_t err = ESP_OK;
    if (atomic_load(&s_unconfirmed)) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err != ESP_OK) {
            // The flag stays set: the image IS still on probation, and esp_ota_begin() would refuse
            // the next upload for that reason anyway (esp_ota_ops.c:164-170). The next reboot rolls
            // back to the previous slot -- which is the safe outcome, not a reason to retry here.
            ESP_LOGE(TAG, "image not confirmed: %s -- the next restart rolls back",
                     esp_err_to_name(err));
        } else {
            atomic_store(&s_unconfirmed, false);
            if (early != NULL)
                ESP_LOGI(TAG, "image confirmed early: %s; rollback cancelled", early);
            else
                ESP_LOGI(TAG, "image confirmed after %u ms; rollback cancelled",
                         (unsigned)COMFOAIR_OTA_CONFIRM_DELAY_MS);
        }
    }
    xSemaphoreGive(s_confirm_lock);
    return err;
}

static void confirm(void *arg)
{
    (void)arg;
    (void)mark_valid(NULL);
}

esp_err_t comfoair_ota_runtime_start(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    if (running == NULL) {
        ESP_LOGE(TAG, "no running partition");
        return ESP_ERR_NOT_FOUND;
    }

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    const esp_err_t      read  = esp_ota_get_state_partition(running, &state);
    // After a USB flash otadata is blank, but the app never sees it so: the bootloader, finding
    // both entries blank and no factory slot, boots ota_0 and writes otadata[0] = seq 1, VALID
    // before the app runs (bootloader_utility.c:420-424, 491-498). So the expected log then is
    // "running ota_0, state valid". NOT_FOUND (no entry with a valid CRC for this slot,
    // esp_ota_ops.c:1024-1035) is the exception -- e.g. a bootloader without that write -- and
    // means the same as VALID here: nothing to confirm.
    if (read != ESP_OK) {
        ESP_LOGI(TAG, "running %s, state none (%s)", running->label, esp_err_to_name(read));
        return ESP_OK;
    }
    ESP_LOGI(TAG, "running %s, state %s", running->label, state_word(state));
    if (state != ESP_OTA_IMG_PENDING_VERIFY)
        return ESP_OK;

    // Set BEFORE the timer exists, so there is no instant in which an upload could be told the
    // image is confirmed while esp_ota_begin() would still refuse it. The lock before the flag:
    // confirm_now() takes it only once the flag reads true.
    s_confirm_lock = xSemaphoreCreateMutexStatic(&s_confirm_lock_mem);
    atomic_store(&s_unconfirmed, true);
    const esp_timer_create_args_t args = {
        .callback        = confirm,
        .dispatch_method = ESP_TIMER_TASK,
        .name            = "ota_confirm",
    };
    esp_err_t err = esp_timer_create(&args, &s_confirm_timer);
    if (err == ESP_OK)
        err = esp_timer_start_once(s_confirm_timer, (uint64_t)COMFOAIR_OTA_CONFIRM_DELAY_MS * 1000u);
    if (err != ESP_OK) {
        // DO NOT confirm on the spot instead. Confirming at boot is exactly what rollback exists
        // to prevent -- an image that crashes at second 10 would then be kept for ever. A missing
        // timer means the next restart rolls back, and the owner can simply upload again.
        ESP_LOGE(TAG, "confirmation timer did not start: %s -- the next restart rolls back",
                 esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "image on probation; confirming in %u ms", (unsigned)COMFOAIR_OTA_CONFIRM_DELAY_MS);
    return ESP_OK;
}

bool comfoair_ota_runtime_unconfirmed(void)
{
    return atomic_load(&s_unconfirmed);
}

esp_err_t comfoair_ota_runtime_confirm_now(const char *reason)
{
    // The common case -- no update in the last 30 s -- returns here and touches nothing, which is
    // also why s_confirm_lock may still be NULL on this path.
    if (!atomic_load(&s_unconfirmed))
        return ESP_OK;
    // Stopped first so the timer does not fire into a second, empty pass. Its answer is not
    // trusted for anything more (see s_confirm_lock); INVALID_STATE -- already fired, or never
    // started because start() failed to arm it -- is normal here.
    if (s_confirm_timer != NULL)
        (void)esp_timer_stop(s_confirm_timer);
    return mark_valid(reason != NULL ? reason : "no reason given");
}

static void restart_task(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS((uint32_t)(uintptr_t)arg));
    ESP_LOGI(TAG, "restarting into the new image");
    esp_restart();
}

esp_err_t comfoair_ota_runtime_restart_later(unsigned delay_ms)
{
    // exchange, not load-then-store: two callers must not both see "not armed".
    if (atomic_exchange(&s_restart_armed, true))
        return ESP_OK;
    // 4096 BYTES (IDF's xTaskCreate counts bytes). esp_restart() runs the shutdown handlers ON THIS
    // STACK -- esp_wifi_stop among them, with its own ESP_LOGI -- after this task's log line.
    // DO NOT shrink it to "one delay, one log, one call": an overflow would still reboot, but the
    // reset reason on /api/system would read "panic" instead of a software restart, after every
    // update. Priority 1, just above idle: the restart is not urgent, finishing the answer is.
    if (xTaskCreate(restart_task, "ota_restart", 4096, (void *)(uintptr_t)delay_ms, 1, NULL) !=
        pdPASS) {
        atomic_store(&s_restart_armed, false);
        ESP_LOGE(TAG, "restart task did not start");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

#endif  // ESP_PLATFORM
