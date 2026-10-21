// The contract between the two files of comfoair_can, and deliberately NOT in include/: nothing
// outside this component may learn that it has an inside. The interrupt side (comfoair_can.c)
// and the API side (comfoair_can_api.c) share the node, the queues, the transmit slots and the
// counters, all defined in comfoair_can.c. What may run in ISR context, and what must not, is
// stated beside each function, not here.
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "comfoair_can.h"
#include "esp_twai.h"
#include "esp_twai_onchip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// A ComfoNet frame is classic CAN: at most 8 bytes.
#define CAN_MAX_DLC 8
// The driver's queue is 8 deep and one more frame can be in the shift register, so nine is
// the most the controller can be holding at once. One slot per frame it can hold; fewer
// would refuse a send the hardware would have accepted.
#define TX_QUEUE_DEPTH 8
#define TX_SLOTS       (TX_QUEUE_DEPTH + 1)

typedef struct {
    uint32_t id;
    uint8_t  len;
    bool     extended;
    bool     rtr;
    bool     stop;   // the sentinel comfoair_can_stop() uses to retire the task
    uint8_t  data[CAN_MAX_DLC];
} rx_item_t;

// What the transport hands the driver, and keeps.
//
// DO NOT go back to building the frame on the caller's stack. twai_node_transmit stores a
// POINTER (esp_twai_onchip.c:698 sizes the queue with `sizeof(twai_frame_t *)`, and :603/:606
// enqueue the caller's pointer), and the TX-done interrupt dereferences it later at :292 ->
// :191-193, as does :246-249 once a bus-off recovery completes. Waiting for the driver inside
// the send is not enough on its own: the wait can time out (esp_twai.h:127-134) with the
// frame still queued, and returning that error unwinds the stack the driver is pointing at --
// which is the ordinary case on a bus whose unit is not powered and acknowledges nothing.
typedef struct {
    twai_frame_t frame;
    uint8_t      data[CAN_MAX_DLC];
    // Claimed from a task, released from the TX-done interrupt, and there may be more than
    // one sender (the hourly poll, and RMI writes later), so the claim has to be atomic
    // rather than a plain flag with a read-modify-write in the middle.
    atomic_bool  busy;
} tx_slot_t;

typedef enum {
    BUS_EVT_OFF = 1,
    BUS_EVT_ACTIVE,
    BUS_EVT_STOP,
} bus_evt_t;

// Defined in comfoair_can.c. The counters are written from ISR context and read unlocked by
// comfoair_can_get_stats(); g_can_stopping is what retires the two tasks (see its definition).
extern twai_node_handle_t   g_can_node;
extern QueueHandle_t        g_can_rx_queue;
extern QueueHandle_t        g_can_bus_queue;
extern QueueHandle_t        g_can_done_queue;
extern comfoair_can_rx_cb_t g_can_cb;
extern void                *g_can_ctx;
extern tx_slot_t            g_can_tx[TX_SLOTS];
extern volatile uint32_t    g_can_rx_frames;
extern volatile uint32_t    g_can_rx_dropped;
extern volatile uint32_t    g_can_bus_errors;
extern volatile uint32_t    g_can_bus_recoveries;
extern volatile bool        g_can_stopping;

// The driver's event callbacks, registered by comfoair_can_start() while the node is stopped.
// They run in ISR context from IRAM.
//
// DO NOT put IRAM_ATTR on these prototypes. It expands to a section name numbered by __COUNTER__,
// so a prototype and its definition name two different .iram1.N sections: GCC ignores the
// definition's (-Wattributes) and the function lands in whichever the prototype named. The
// attribute belongs on the definitions in comfoair_can.c only.
bool comfoair_can_on_rx_done(twai_node_handle_t handle,
                             const twai_rx_done_event_data_t *edata, void *user_ctx);
bool comfoair_can_on_tx_done(twai_node_handle_t handle,
                             const twai_tx_done_event_data_t *edata, void *user_ctx);
bool comfoair_can_on_error(twai_node_handle_t handle,
                           const twai_error_event_data_t *edata, void *user_ctx);
bool comfoair_can_on_state_change(twai_node_handle_t handle,
                                  const twai_state_change_event_data_t *edata, void *user_ctx);

// The two tasks comfoair_can_start() creates ("can_rx", "can_bus"). Each leaves only through
// its stop sentinel or g_can_stopping, posting one token to g_can_done_queue on the way out.
void comfoair_can_rx_task(void *arg);
void comfoair_can_bus_task(void *arg);
