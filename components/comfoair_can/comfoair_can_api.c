// comfoair_can: the public API -- start, stop, transmit, stats -- and the transmit-slot claim.
// Read comfoair_can.h first; the component's shape is in README.md, Implementation.
#include "comfoair_can.h"

#include <stdatomic.h>
#include <string.h>

#include "comfoair_can_internal.h"
#include "esp_log.h"

static const char *TAG = "can";

// Deep enough to ride out a burst of PDO broadcasts while the consumer is busy, small
// enough to be honest: if it overflows we count it rather than blocking the ISR.
#define RX_QUEUE_LEN 32

static TaskHandle_t         s_rx_task;
static TaskHandle_t         s_bus_task;

// Claims a slot that outlives the call. Returns NULL when the controller is already holding
// as many frames as it can, which the caller sees as a refused send rather than as a frame
// that quietly went nowhere.
static tx_slot_t *tx_slot_claim(void)
{
    for (size_t i = 0; i < TX_SLOTS; i++) {
        bool expected = false;
        if (atomic_compare_exchange_strong(&g_can_tx[i].busy, &expected, true))
            return &g_can_tx[i];
    }
    return NULL;
}

esp_err_t comfoair_can_start(const board_t *board, comfoair_can_rx_cb_t cb, void *ctx)
{
    if (g_can_node != NULL)
        return ESP_ERR_INVALID_STATE;
    if (board == NULL || cb == NULL)
        return ESP_ERR_INVALID_ARG;

    g_can_rx_queue   = xQueueCreate(RX_QUEUE_LEN, sizeof(rx_item_t));
    g_can_bus_queue  = xQueueCreate(8, sizeof(uint8_t));
    g_can_done_queue = xQueueCreate(2, sizeof(uint8_t));
    if (g_can_rx_queue == NULL || g_can_bus_queue == NULL || g_can_done_queue == NULL) {
        comfoair_can_stop();
        return ESP_ERR_NO_MEM;
    }

    g_can_cb       = cb;
    g_can_ctx      = ctx;
    g_can_stopping = false;

    // Slots are only ever released by the TX-done interrupt, so a session that ended with
    // frames still in the controller would start the next one short of storage.
    for (size_t i = 0; i < TX_SLOTS; i++)
        atomic_store(&g_can_tx[i].busy, false);

    // Statistics count THIS session on the bus, not the lifetime of the process. Without
    // this they survive a stop/start and a consumer cannot tell "20 frames dropped just
    // now" from "20 dropped at some point since boot" -- and the two lead to opposite
    // conclusions when the bus is being diagnosed. Found by the transport's host tests,
    // which pushed 64 frames and were told 71 had been accounted for.
    g_can_rx_frames      = 0;
    g_can_rx_dropped     = 0;
    g_can_bus_errors     = 0;
    g_can_bus_recoveries = 0;

    const twai_onchip_node_config_t cfg = {
        .io_cfg = {
            .tx                = board->can_tx,
            .rx                = board->can_rx,
            .quanta_clk_out    = GPIO_NUM_NC,
            .bus_off_indicator = GPIO_NUM_NC,
        },
        // 50 kbps is the ComfoNet bus rate and is not negotiable; it comes from the board
        // descriptor so that a future board cannot silently disagree with it.
        .bit_timing     = {.bitrate = board->can_bitrate},
        .tx_queue_depth = TX_QUEUE_DEPTH,
        // -1 would retransmit forever. On a bus where the unit may simply not be powered,
        // that is an ISR that never stops retrying; a bounded count fails the send instead
        // and lets the caller decide.
        .fail_retry_cnt = 3,
    };

    esp_err_t err = twai_new_node_onchip(&cfg, &g_can_node);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_new_node_onchip: %s", esp_err_to_name(err));
        goto fail;
    }

    // Registered while the node is still stopped, which esp_twai_onchip.c:362 requires:
    // afterwards it refuses, and the transport would come up unable to hear anything.
    const twai_event_callbacks_t cbs = {
        .on_rx_done      = comfoair_can_on_rx_done,
        .on_tx_done      = comfoair_can_on_tx_done,
        .on_error        = comfoair_can_on_error,
        .on_state_change = comfoair_can_on_state_change,
    };
    err = twai_node_register_event_callbacks(g_can_node, &cbs, NULL);
    if (err != ESP_OK)
        goto fail;

    if (xTaskCreate(comfoair_can_rx_task, "can_rx", 4096, NULL, 10, &s_rx_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }
    if (xTaskCreate(comfoair_can_bus_task, "can_bus", 3072, NULL, 9, &s_bus_task) != pdPASS) {
        err = ESP_ERR_NO_MEM;
        goto fail;
    }

    err = twai_node_enable(g_can_node);
    if (err != ESP_OK)
        goto fail;

    ESP_LOGI(TAG, "up on tx=%d rx=%d at %" PRIu32 " bps", board->can_tx, board->can_rx,
             board->can_bitrate);
    return ESP_OK;

fail:
    comfoair_can_stop();
    return err;
}

esp_err_t comfoair_can_stop(void)
{
    if (g_can_node) {
        twai_node_disable(g_can_node);
        twai_node_delete(g_can_node);
        g_can_node = NULL;
    }

    // Ask each task to leave, then wait for it. DO NOT go back to vTaskDelete() on the
    // handles: that stops a task wherever it happens to be, and comfoair_can_rx_task spends its
    // life inside the consumer's callback.
    g_can_stopping       = true;
    unsigned pending = 0;
    if (s_rx_task && g_can_rx_queue) {
        const rx_item_t sentinel = {.stop = true};
        // Timeout 0 on purpose. A full queue means the task is not blocked, so it will reach
        // the sentinel that is already there; blocking here would deadlock against a consumer
        // that is itself waiting on us.
        (void)xQueueSend(g_can_rx_queue, &sentinel, 0);
        pending++;
    }
    if (s_bus_task && g_can_bus_queue) {
        const uint8_t evt = BUS_EVT_STOP;
        (void)xQueueSend(g_can_bus_queue, &evt, 0);
        pending++;
    }
    bool all_retired = true;
    for (unsigned i = 0; i < pending; i++) {
        uint8_t done;
        if (g_can_done_queue == NULL || xQueueReceive(g_can_done_queue, &done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            all_retired = false;
            ESP_LOGE(TAG, "a task did not retire in time");
            break;
        }
    }
    s_rx_task  = NULL;
    s_bus_task = NULL;

    // A task that is still running is still reading these handles. Leaking three queues is
    // an ugly outcome; freeing them under a live task is a crash, and the leak is bounded by
    // the fact that this path means something is already badly wrong.
    if (!all_retired) {
        g_can_cb       = NULL;
        g_can_ctx      = NULL;
        g_can_stopping = false;
        return ESP_ERR_TIMEOUT;
    }

    if (g_can_rx_queue) {
        vQueueDelete(g_can_rx_queue);
        g_can_rx_queue = NULL;
    }
    if (g_can_bus_queue) {
        vQueueDelete(g_can_bus_queue);
        g_can_bus_queue = NULL;
    }
    if (g_can_done_queue) {
        vQueueDelete(g_can_done_queue);
        g_can_done_queue = NULL;
    }

    g_can_cb       = NULL;
    g_can_ctx      = NULL;
    g_can_stopping = false;
    return ESP_OK;
}

esp_err_t comfoair_can_transmit(uint32_t can_id, const uint8_t *data, size_t len, bool rtr,
                                int timeout_ms)
{
    if (g_can_node == NULL)
        return ESP_ERR_INVALID_STATE;
    if (len > CAN_MAX_DLC || (len > 0 && data == NULL))
        return ESP_ERR_INVALID_ARG;

    tx_slot_t *slot = tx_slot_claim();
    if (slot == NULL)
        return ESP_ERR_NO_MEM;

    const size_t payload_len = rtr ? 0 : len;
    if (payload_len > 0)
        memcpy(slot->data, data, payload_len);

    slot->frame = (twai_frame_t){
        .header = {
            .id  = can_id,
            .dlc = (uint16_t)payload_len,
            .ide = 1,   // ComfoNet is 29-bit throughout
            .rtr = rtr ? 1 : 0,
        },
        .buffer     = slot->data,
        .buffer_len = payload_len,
    };

    const esp_err_t err = twai_node_transmit(g_can_node, &slot->frame, timeout_ms);
    if (err != ESP_OK) {
        // Refused outright, so the driver never took the pointer and the slot is ours again.
        atomic_store(&slot->busy, false);
        return err;
    }

    // The wait is what turns "the controller accepted it" into "it went out", which is the
    // only thing a caller can act on -- comfoair_poll logs a failed request and moves to the
    // next PDO. It is no longer what keeps the frame alive: the slot does that, and it stays
    // claimed until the TX-done interrupt hands it back, however long the wait took.
    return twai_node_transmit_wait_all_done(g_can_node, timeout_ms);
}

void comfoair_can_get_stats(comfoair_can_stats_t *out)
{
    if (out == NULL)
        return;
    memset(out, 0, sizeof *out);
    out->started        = g_can_node != NULL;
    out->rx_frames      = g_can_rx_frames;
    out->rx_dropped     = g_can_rx_dropped;
    out->bus_errors     = g_can_bus_errors;
    out->bus_recoveries = g_can_bus_recoveries;

    twai_node_status_t status = {0};
    if (g_can_node && twai_node_get_info(g_can_node, &status, NULL) == ESP_OK) {
        switch (status.state) {
        case TWAI_ERROR_WARNING: out->state = COMFOAIR_CAN_BUS_WARNING; break;
        case TWAI_ERROR_PASSIVE: out->state = COMFOAIR_CAN_BUS_PASSIVE; break;
        case TWAI_ERROR_BUS_OFF: out->state = COMFOAIR_CAN_BUS_OFF;     break;
        default:                 out->state = COMFOAIR_CAN_BUS_ACTIVE;  break;
        }
        out->tx_error_count = status.tx_error_count;
        out->rx_error_count = status.rx_error_count;
    }
}
