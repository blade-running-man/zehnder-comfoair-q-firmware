// Control surface for the fake TWAI driver. Tests drive the bus through this; the
// production source only ever sees the ESP-IDF prototypes.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_twai_onchip.h"
#include "esp_twai_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FAKE_TX_LOG_LEN 16

// What the controller actually put on the wire, sampled at the moment the driver
// dereferenced the caller's frame -- see fake_twai.c.
typedef struct {
    uint32_t id;
    uint16_t dlc;
    uint8_t  ide;
    uint8_t  rtr;
    size_t   buffer_len;
    uint8_t  data[8];
    bool     buffer_was_null;
} fake_tx_record_t;

void fake_twai_reset(void);

// --- observation ---------------------------------------------------------------------

bool                             fake_twai_node_exists(void);
bool                             fake_twai_enabled(void);
const twai_onchip_node_config_t *fake_twai_config(void);
unsigned                         fake_twai_tx_count(void);
const fake_tx_record_t          *fake_twai_tx(unsigned index);
// Entries into twai_node_transmit, valid or not. `tx_count` counts only what reached the
// wire; the difference is how a test tells "the transport rejected it" from "the driver
// rejected it", which is the difference between testing comfoair_can_api.c and testing this file.
unsigned                         fake_twai_transmit_calls(void);
bool                             fake_twai_callbacks_registered_before_enable(void);
// The frame pointer the driver is still holding for queued entry `index`. The driver reads
// through it later, so where it points is the whole question.
const void                      *fake_twai_queued_frame(unsigned index);
unsigned                         fake_twai_recover_calls(void);
unsigned                         fake_twai_recover_calls_in_isr(void);
bool                             fake_twai_in_isr(void);
twai_error_state_t               fake_twai_state(void);

// --- stimulus ------------------------------------------------------------------------

// Delivers one frame the way the real ISR does: raise the interrupt-context flag, invoke
// the registered on_rx_done, and let it pull the frame with twai_node_receive_from_isr.
// `dlc` is passed through unclamped so a malformed frame can be injected.
void fake_twai_deliver_rx(uint32_t can_id, const uint8_t *data, size_t len, uint16_t dlc);

// A remote frame: a DLC on the wire and no data field at all.
void fake_twai_deliver_rx_remote(uint32_t can_id, uint16_t dlc);

// on_rx_done fires but twai_node_receive_from_isr finds nothing.
void fake_twai_deliver_rx_nothing(void);

// Models the TX-done interrupt (esp_twai_onchip.c:287-300): the finished frame is
// reported, then the NEXT queued frame pointer is dequeued and dereferenced.
void fake_twai_complete_tx(void);

// A controller that accepts frames and never finishes them -- an unpowered unit that never
// acknowledges. twai_node_transmit_wait_all_done then times out with the frames still queued.
void fake_twai_stall_tx(bool stalled);

// The frame the controller is still holding, if any: it will be read again later.
const void *fake_twai_in_flight_frame(void);

// Drives TEC past 255. Fires on_error and on_state_change, in that order, both from
// interrupt context -- matching esp_twai_onchip.c:222-245.
void fake_twai_go_bus_off(void);

// The 128 recessive-bit sequence has elapsed: leaves bus-off for error-active and fires
// on_state_change. Returns false unless twai_node_recover() was called first, which is
// the whole point (esp_twai.h:36-43).
bool fake_twai_finish_recovery(void);

void fake_twai_set_error_counts(uint16_t tec, uint16_t rec);

// --- log observation (implemented in fake_rtos.c) -------------------------------------

unsigned fake_log_calls_in_isr(void);
void     fake_log_reset(void);

// --- FreeRTOS observation (implemented in fake_rtos.c) --------------------------------

// Tasks and queues created and not yet deleted. A stop() that forgets to release either
// leaks per cycle, which otherwise shows up as a hung suite rather than a failure.
unsigned fake_rtos_live_tasks(void);
unsigned fake_rtos_live_queues(void);

// Calls to the BLOCKING xQueueSend made from interrupt context. FreeRTOS would block there;
// the point of xQueueSendFromISR is that it cannot.
unsigned fake_rtos_isr_blocking_calls(void);

// Tasks that vTaskDelete stopped while they were NOT parked in xQueueReceive -- i.e. torn
// down in the middle of doing something, which on FreeRTOS orphans whatever they held.
unsigned fake_rtos_tasks_killed_off_park(void);

#ifdef __cplusplus
}
#endif
