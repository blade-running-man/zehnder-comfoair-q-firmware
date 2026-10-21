// TWAI transport for the ComfoNet bus.
//
// Deliberately thin. Every rule about what a CAN id or a payload MEANS lives in
// components/comfoair_protocol/, which is host-tested; this file only moves bytes.
// If you find yourself decoding something here, it belongs in a tested header instead.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "esp_err.h"

#ifdef __cplusplus
// main.cpp is C++ (the protocol headers are), these components are C. Without this
// the linker looks for mangled names and fails with "undefined reference".
extern "C" {
#endif

// Called from a driver task -- NOT from the ISR -- once per received frame.
// `data` is valid only for the duration of the call.
//
// `extended` and `rtr` are part of the frame and must be passed on, not swallowed:
// pdo_decode.h states outright that its id test is not sufficient on its own -- "the caller
// must also require an extended id and !rtr, a remote transmission request from another node
// has an identical id". Without these two flags a consumer physically cannot honour that.
typedef void (*comfoair_can_rx_cb_t)(uint32_t can_id, bool extended, bool rtr,
                                     const uint8_t *data, size_t len, void *ctx);

// Brings the controller up on the pins and bitrate the board describes and starts the
// receive task. Returns ESP_ERR_INVALID_STATE if already started.
esp_err_t comfoair_can_start(const board_t *board, comfoair_can_rx_cb_t cb, void *ctx);

esp_err_t comfoair_can_stop(void);

// Sends one extended-id frame and waits for the driver to finish with it. `len` must be
// 0..8; a remote-transmission request (`rtr`) carries no data and is how a single PDO is
// polled. `data` is copied, so the caller may reuse its buffer the moment this returns.
//
// The copy is not a convenience. twai_node_transmit stores a POINTER to the frame
// (esp_twai_onchip.c:698 sizes the queue with `sizeof(twai_frame_t *)`; :603/:606 enqueue the
// caller's pointer) and dereferences it later from the TX-done interrupt (:292 -> :191-193),
// and again after a bus-off recovery (:246-249). Waiting is not enough on its own: the wait
// itself can time out with the frame still queued (esp_twai.h:127-134), and on a bus whose
// unit is unpowered nothing is ever acknowledged, so that is the ordinary path rather than
// the exotic one. The transport therefore owns the storage until the driver gives it back.
//
// Returns ESP_ERR_NO_MEM when the controller is already holding every frame it can, and
// ESP_ERR_TIMEOUT when the send did not complete within `timeout_ms`.
esp_err_t comfoair_can_transmit(uint32_t can_id, const uint8_t *data, size_t len, bool rtr,
                                int timeout_ms);

// Mirrors the controller's error state, spelled without the driver's types on purpose: this
// header is included by layers that are host-tested and must not have to drag esp_driver_twai
// in behind it. Note that the driver reports a node that is merely stopped as bus-off too, so
// read this together with `started`.
typedef enum {
    COMFOAIR_CAN_BUS_ACTIVE = 0,   // healthy
    COMFOAIR_CAN_BUS_WARNING,      // error counters climbing
    COMFOAIR_CAN_BUS_PASSIVE,      // no longer allowed to flag errors on the bus
    COMFOAIR_CAN_BUS_OFF,          // offline until recovered
} comfoair_can_bus_state_t;

// Bus health, for the diagnostics the firmware publishes. Counters are zeroed by
// comfoair_can_start(), so they describe the current session on the bus.
typedef struct {
    bool     started;
    uint32_t rx_frames;
    // Frames that did not reach the consumer: the queue was full, or the interrupt fired
    // with nothing to collect. Either way the promise is that no frame vanishes unrecorded.
    uint32_t rx_dropped;
    uint32_t bus_errors;
    // Times the bus went off and was asked back. A number that keeps climbing is a wiring
    // fault, and it is the one symptom that otherwise looks exactly like a healthy quiet bus.
    uint32_t bus_recoveries;
    // The controller's own error state. Without it the diagnostics can show mounting errors
    // but never that the bus is actually offline, which are different problems with different
    // answers.
    comfoair_can_bus_state_t state;
    uint16_t tx_error_count;
    uint16_t rx_error_count;
} comfoair_can_stats_t;

void comfoair_can_get_stats(comfoair_can_stats_t *out);

#ifdef __cplusplus
}
#endif
