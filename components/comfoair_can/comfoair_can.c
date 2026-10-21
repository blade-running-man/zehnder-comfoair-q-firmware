// comfoair_can: the interrupt side -- the four ISR callbacks, the receive and bus-off tasks,
// and the definitions of the state they share with comfoair_can_api.c. Read comfoair_can.h
// first; the component's shape is in README.md, Implementation.
#include "comfoair_can.h"

#include <stdatomic.h>
#include <string.h>

#include "comfoair_can_internal.h"
#include "esp_log.h"

static const char *TAG = "can";

// Bus-off recovery back-off. The first attempt is immediate -- a single glitch should not
// cost a delay -- and a bus that keeps collapsing is retried ever more slowly instead of
// being hammered. It never gives up: a unit that is switched on tomorrow must still be found.
#define RECOVER_BACKOFF_FIRST_MS 250
#define RECOVER_BACKOFF_MAX_MS   5000

twai_node_handle_t   g_can_node;
QueueHandle_t        g_can_rx_queue;
QueueHandle_t        g_can_bus_queue;
QueueHandle_t        g_can_done_queue;
comfoair_can_rx_cb_t g_can_cb;
void                *g_can_ctx;
tx_slot_t            g_can_tx[TX_SLOTS];
volatile uint32_t    g_can_rx_frames;
volatile uint32_t    g_can_rx_dropped;
volatile uint32_t    g_can_bus_errors;
volatile uint32_t    g_can_bus_recoveries;
// Raised before anything is torn down. The sentinel comfoair_can_stop() posts is only a way to
// WAKE a task that is blocked; this flag is what actually retires it, because a sentinel cannot
// be enqueued onto a queue that is already full -- and a full receive queue is exactly the state
// a consumer that has fallen behind leaves it in.
volatile bool        g_can_stopping;

// Runs in ISR context. It must not call the user callback, must not log, and must not
// block: it copies the frame into the queue and leaves.
bool IRAM_ATTR comfoair_can_on_rx_done(twai_node_handle_t handle,
                                       const twai_rx_done_event_data_t *edata, void *user_ctx)
{
    (void)edata;
    (void)user_ctx;

    rx_item_t item = {0};
    uint8_t   buf[CAN_MAX_DLC] = {0};
    twai_frame_t frame = {.buffer = buf, .buffer_len = sizeof buf};

    if (twai_node_receive_from_isr(handle, &frame) != ESP_OK) {
        // The interrupt fired and there was nothing to take. Counting it matters because the
        // diagnostics promise that every frame is either delivered or accounted for; a
        // silent return here made a lost frame leave no trace at all.
        g_can_rx_dropped++;
        return false;
    }

    item.id       = frame.header.id;
    item.extended = frame.header.ide != 0;
    item.rtr      = frame.header.rtr != 0;

    // A remote-transmission request carries NO data: twai_hal_parse_frame only fills the
    // buffer when the frame is not an RTR (twai_hal_v1.c:386-388), while its DLC is whatever
    // the requester chose. Trusting the DLC here copied uninitialised stack into the state
    // model and, on a bus with a second polling node, fed random bytes to the fan level.
    item.len = item.rtr ? 0
                        : (frame.header.dlc > CAN_MAX_DLC ? CAN_MAX_DLC
                                                          : (uint8_t)frame.header.dlc);
    memcpy(item.data, buf, item.len);

    BaseType_t higher_priority_woken = pdFALSE;
    if (xQueueSendFromISR(g_can_rx_queue, &item, &higher_priority_woken) != pdTRUE)
        g_can_rx_dropped++;
    else
        g_can_rx_frames++;

    return higher_priority_woken == pdTRUE;
}

// Releases the slot the finished frame occupied. The driver hands back the very pointer it
// was given, so the slot is found by identity rather than by searching for a match.
bool IRAM_ATTR comfoair_can_on_tx_done(twai_node_handle_t handle,
                                       const twai_tx_done_event_data_t *edata, void *user_ctx)
{
    (void)handle;
    (void)user_ctx;
    if (edata == NULL || edata->done_tx_frame == NULL)
        return false;

    for (size_t i = 0; i < TX_SLOTS; i++) {
        if (&g_can_tx[i].frame == edata->done_tx_frame) {
            atomic_store(&g_can_tx[i].busy, false);
            break;
        }
    }
    return false;
}

bool IRAM_ATTR comfoair_can_on_error(twai_node_handle_t handle, const twai_error_event_data_t *edata,
                                     void *user_ctx)
{
    (void)handle;
    (void)edata;
    (void)user_ctx;
    g_can_bus_errors++;
    return false;
}

// The only thing the interrupt does about a dead bus is say so. twai_node_recover() is not
// ISR-safe -- neither it nor twai_hal_start_bus_recovery appears in esp_driver_twai's
// linker.lf, so under CONFIG_TWAI_ISR_CACHE_SAFE calling it from an IRAM interrupt would be a
// flash access with the cache disabled -- and there is nowhere in an ISR to wait out a
// back-off. So the event goes to a task.
bool IRAM_ATTR comfoair_can_on_state_change(twai_node_handle_t handle,
                                            const twai_state_change_event_data_t *edata,
                                            void *user_ctx)
{
    (void)handle;
    (void)user_ctx;
    if (edata == NULL || g_can_bus_queue == NULL)
        return false;

    uint8_t evt;
    if (edata->new_sta == TWAI_ERROR_BUS_OFF)
        evt = BUS_EVT_OFF;
    else if (edata->old_sta == TWAI_ERROR_BUS_OFF)
        evt = BUS_EVT_ACTIVE;
    else
        return false;   // error-active/warning/passive churn is not our business

    BaseType_t higher_priority_woken = pdFALSE;
    (void)xQueueSendFromISR(g_can_bus_queue, &evt, &higher_priority_woken);
    return higher_priority_woken == pdTRUE;
}

static void signal_done(void)
{
    const uint8_t done = 1;
    if (g_can_done_queue)
        (void)xQueueSend(g_can_done_queue, &done, 0);
}

void comfoair_can_rx_task(void *arg)
{
    (void)arg;
    rx_item_t item;
    for (;;) {
        if (xQueueReceive(g_can_rx_queue, &item, portMAX_DELAY) != pdTRUE)
            continue;
        if (g_can_stopping || item.stop)
            break;
        if (g_can_cb)
            g_can_cb(item.id, item.extended, item.rtr, item.data, item.len, g_can_ctx);
    }
    // Reached only through the sentinel, which is to say only from between two callbacks.
    // That is the point: vTaskDelete() from comfoair_can_stop() would have stopped this task
    // at whatever instruction it happened to be on, including the middle of the consumer's
    // callback, orphaning whatever that callback held.
    signal_done();
    vTaskDelete(NULL);
}

// A bus-off controller is offline until somebody asks it back. Nothing recovers by itself:
// twai_hal_start_bus_recovery is reachable from twai_node_recover() and from nowhere else in
// the tree, and esp_driver_twai's Kconfig has no automatic-recovery option. Without this task
// a single fault -- the unit unpowered while its transceiver is still biased, a missing
// terminator, a shorted line -- freezes every entity at its last value until the device is
// power-cycled. That breaks the rule that nothing reboots because a peer is absent (AGENTS.md)
// just the same, as a silent hang rather than a reboot, which is why it is not left to the user.
void comfoair_can_bus_task(void *arg)
{
    (void)arg;
    uint32_t backoff_ms = 0;
    uint8_t  evt;

    for (;;) {
        if (xQueueReceive(g_can_bus_queue, &evt, portMAX_DELAY) != pdTRUE)
            continue;
        if (g_can_stopping || evt == BUS_EVT_STOP)
            break;
        if (evt == BUS_EVT_ACTIVE) {
            // The bus came back. Forget how hard it was to get here, so that a fault next
            // year is not answered with last year's five-second delay.
            backoff_ms = 0;
            ESP_LOGI(TAG, "bus recovered");
            continue;
        }

        if (backoff_ms > 0)
            vTaskDelay(pdMS_TO_TICKS(backoff_ms));
        if (g_can_node == NULL)
            continue;

        const esp_err_t err = twai_node_recover(g_can_node);
        if (err == ESP_OK) {
            g_can_bus_recoveries++;
            ESP_LOGW(TAG, "bus off -- recovery requested");
        } else {
            // ESP_ERR_INVALID_STATE here means the node left bus-off between the event and
            // this call, which is not a problem and must not be retried.
            ESP_LOGD(TAG, "recover: %s", esp_err_to_name(err));
        }

        backoff_ms = backoff_ms == 0 ? RECOVER_BACKOFF_FIRST_MS
                                     : (backoff_ms * 2 > RECOVER_BACKOFF_MAX_MS
                                            ? RECOVER_BACKOFF_MAX_MS
                                            : backoff_ms * 2);
    }
    signal_done();
    vTaskDelete(NULL);
}
