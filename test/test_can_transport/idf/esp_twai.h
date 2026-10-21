// Host stand-in for esp_twai.h (ESP-IDF 5.5.5). Same prototypes, same order.
//
// Note what is NOT here: there is no task-context receive. `twai_node_receive_from_isr`
// is the only way in (esp_twai.h:149), which is why comfoair_can.c must own an ISR, a
// queue and a task -- that shape is the driver's, not an invention.
#pragma once

#include "esp_err.h"
#include "esp_twai_types.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t twai_node_enable(twai_node_handle_t node);
esp_err_t twai_node_disable(twai_node_handle_t node);
esp_err_t twai_node_recover(twai_node_handle_t node);
esp_err_t twai_node_delete(twai_node_handle_t node);
esp_err_t twai_node_register_event_callbacks(twai_node_handle_t node,
                                             const twai_event_callbacks_t *cbs, void *user_data);
esp_err_t twai_node_get_info(twai_node_handle_t node, twai_node_status_t *status_ret,
                             twai_node_record_t *statistics_ret);
esp_err_t twai_node_transmit(twai_node_handle_t node, const twai_frame_t *frame, int timeout_ms);
esp_err_t twai_node_transmit_wait_all_done(twai_node_handle_t node, int timeout_ms);
esp_err_t twai_node_receive_from_isr(twai_node_handle_t node, twai_frame_t *rx_frame);

#ifdef __cplusplus
}
#endif
