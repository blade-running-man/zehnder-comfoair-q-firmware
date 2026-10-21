#include "comfoair_poll.h"

#include <cinttypes>

#include "comfoair_can.h"
#include "comfoair_poll_plan.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "pdo_decode.h"
#include "registry_generated.h"

static const char *TAG = "poll";

static comfoair_poll_config_t s_config;
static TaskHandle_t           s_task;
// What goes out next is decided by the plan (comfoair_poll_plan, host-tested); this file only
// reads the clock, sends and sleeps. The plan is touched by this task and by the callers of the
// two request functions, so every access is under one short critical section -- each call is a
// handful of comparisons, never a send or a wait.
static comfoair_poll_plan_t s_plan;
static portMUX_TYPE         s_plan_mux = portMUX_INITIALIZER_UNLOCKED;

// esp_timer's microseconds, cut to 32-bit milliseconds. The plan compares by signed difference,
// so the wrap after 49 days is harmless; the tick count would also wrap, but at a point that is
// not a multiple of 2^32 ms, which would make one comparison wrong.
static uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// The id is built by pdo_request_can_id() in the protocol layer, not here. Writing the
// shift and mask out a second time would be a second implementation of the same rule --
// and the one in pdo_decode.h is the one with tests behind it.
static void request(uint16_t pdo)
{
    const esp_err_t err = comfoair_can_transmit(
        comfoair::pdo_request_can_id(pdo, s_config.local_node_id), nullptr, 0, true, 100);
    if (err != ESP_OK) {
        // One failed request is not worth anything more: the unit may simply be off, and the
        // next sweep will try again. Nothing here may reboot the device: nothing reboots because
        // a peer is absent (AGENTS.md).
        ESP_LOGD(TAG, "request for PDO %u failed: %s", pdo, esp_err_to_name(err));
    }
}

static void poll_task(void *arg)
{
    (void)arg;
    for (;;) {
        taskENTER_CRITICAL(&s_plan_mux);
        const comfoair_poll_step_t step = comfoair_poll_plan_next(&s_plan, now_ms());
        taskEXIT_CRITICAL(&s_plan_mux);

        if (step.send) {
            request(step.pdo);
            // The gap follows EVERY request, a read-back's included: the bus never sees two of
            // ours closer than frame_delay_ms.
            vTaskDelay(pdMS_TO_TICKS(s_config.frame_delay_ms));
        } else {
            // A request (sweep or read-back) notifies and ends this early; pdTRUE clears the
            // count, and the plan, not the count, says what is owed.
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(step.wait_ms));
        }
    }
}

esp_err_t comfoair_poll_start(const comfoair_poll_config_t *config)
{
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;
    if (config == NULL || config->local_node_id < 0x01 || config->local_node_id > 0x3f)
        return ESP_ERR_INVALID_ARG;

    s_config = *config;
    // Due at once: the unit is broadcasting already, and a sweep at boot fills in whatever it
    // does not volunteer, so the first state is complete rather than partial.
    comfoair_poll_plan_init(&s_plan, COMFOAIR_REQUEST_PDOS, COMFOAIR_REQUEST_PDO_COUNT,
                            s_config.interval_ms, now_ms());
    if (xTaskCreate(poll_task, "comfoair_poll", 3072, NULL, 5, &s_task) != pdPASS)
        return ESP_ERR_NO_MEM;

    ESP_LOGI(TAG, "cycle: %u PDOs every %" PRIu32 " ms, %" PRIu32 " ms apart, node 0x%02x",
             (unsigned)COMFOAIR_REQUEST_PDO_COUNT, s_config.interval_ms,
             s_config.frame_delay_ms, s_config.local_node_id);
    return ESP_OK;
}

void comfoair_poll_request_now(void)
{
    if (s_task == NULL)
        return;
    taskENTER_CRITICAL(&s_plan_mux);
    comfoair_poll_plan_request_sweep(&s_plan);
    taskEXIT_CRITICAL(&s_plan_mux);
    xTaskNotifyGive(s_task);
}

void comfoair_poll_request_pdo(uint16_t pdo, uint32_t delay_ms)
{
    if (s_task == NULL)
        return;
    taskENTER_CRITICAL(&s_plan_mux);
    const bool armed = comfoair_poll_plan_request_readback(&s_plan, pdo, now_ms(), delay_ms);
    taskEXIT_CRITICAL(&s_plan_mux);
    if (!armed) {
        ESP_LOGW(TAG, "read-back of PDO %u dropped: another PDO's is pending", pdo);
        return;
    }
    // HV-MQTT-31 times the unit's answer against this line.
    ESP_LOGD(TAG, "read-back of PDO %u in %" PRIu32 " ms", pdo, delay_ms);
    // Wakes the task so it re-plans its wait around the new deadline; it does not send yet.
    xTaskNotifyGive(s_task);
}
