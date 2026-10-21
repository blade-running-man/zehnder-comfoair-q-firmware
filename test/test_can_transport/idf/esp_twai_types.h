// Host stand-in for esp_twai_types.h + hal/twai_types.h (ESP-IDF 5.5.5).
//
// Every declaration below is transcribed field-for-field from
//   components/esp_driver_twai/include/esp_twai_types.h
//   components/hal/include/hal/twai_types.h
// of the framework-espidf package this project builds against. DO NOT "tidy" a field
// name or reorder a struct: comfoair_can.c and comfoair_can_api.c use designated initialisers, so a
// wrong name here would silently zero a field the real build sets.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_attr.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TWAI_STD_ID_MASK    0x000007FFU
#define TWAI_EXT_ID_MASK    0x1FFFFFFFU
#define TWAI_FRAME_MAX_DLC  8
#define TWAI_FRAME_MAX_LEN  8
// Present even on a controller without FD: a classic frame may carry a DLC of 9..15 on the
// wire, and the driver accepts it (esp_twai_onchip.c:584). Leaving these out of the fake
// made it reject frames the device accepts.
#define TWAIFD_FRAME_MAX_DLC 15
#define TWAIFD_FRAME_MAX_LEN 64

typedef struct twai_node_base *twai_node_handle_t;

// hal/twai_types.h:34-39.
typedef enum {
    TWAI_ERROR_ACTIVE,      // TEC/REC < 96
    TWAI_ERROR_WARNING,     // TEC/REC >= 96 and < 128
    TWAI_ERROR_PASSIVE,     // TEC/REC >= 128 and < 256
    TWAI_ERROR_BUS_OFF,     // TEC >= 256 -- node offline
} twai_error_state_t;

// hal/twai_types.h:44-53. A union, NOT a plain uint32_t: `edata->err_flags.ack_err` is the
// obvious next line anyone writes when the bus-off diagnostics grow, and with a scalar here
// that line would compile on the device and fail on the host -- or worse, the other way.
typedef union {
    struct {
        uint32_t arb_lost: 1;
        uint32_t bit_err: 1;
        uint32_t form_err: 1;
        uint32_t stuff_err: 1;
        uint32_t ack_err: 1;
    };
    uint32_t val;
} twai_error_flags_t;

typedef int twai_clock_source_t;

// hal/twai_types.h:135-156. esp_twai.h:142 tells callers to take a received frame's length
// from twaifd_dlc2len(header.dlc). Without these on the host, the correct fix for the RX
// length would not even compile here -- which is how a fake quietly steers a fix wrong.
static inline uint16_t twaifd_dlc2len(uint16_t dlc)
{
    return (dlc <= 8)    ? dlc
           : (dlc <= 12) ? (uint16_t)((dlc - 8) * 4 + 8)
           : (dlc <= 13) ? (uint16_t)((dlc - 12) * 8 + 24)
                         : (uint16_t)((dlc - 13) * 16 + 32);
}

static inline uint16_t twaifd_len2dlc(uint16_t byte_len)
{
    return (byte_len <= 8)    ? byte_len
           : (byte_len <= 24) ? (uint16_t)((byte_len - 8 + 3) / 4 + 8)
           : (byte_len <= 32) ? (uint16_t)((byte_len - 24 + 7) / 8 + 12)
                              : (uint16_t)((byte_len - 32 + 15) / 16 + 13);
}

typedef struct {
    uint32_t bitrate;
    uint16_t sp_permill;
    uint16_t ssp_permill;
} twai_timing_basic_config_t;

// hal/twai_types.h:113-127. The bit-field members are anonymous in the real header;
// comfoair_can_api.c initialises `.ide` and `.rtr` through that anonymity.
typedef struct {
    uint32_t id;
    uint16_t dlc;
    struct {
        uint32_t ide: 1;
        uint32_t rtr: 1;
        uint32_t fdf: 1;
        uint32_t brs: 1;
        uint32_t esi: 1;
    };
    union {
        uint64_t timestamp;
        uint64_t trigger_time;
    };
} twai_frame_header_t;

// esp_twai_types.h:32-36. `buffer` is a BORROWED pointer -- see fake_twai.c for the
// lifetime rule the real driver imposes on it.
typedef struct {
    twai_frame_header_t header;
    uint8_t            *buffer;
    size_t              buffer_len;
} twai_frame_t;

typedef struct {
    twai_error_state_t state;
    uint16_t           tx_error_count;
    uint16_t           rx_error_count;
    uint32_t           tx_queue_remaining;
} twai_node_status_t;

typedef struct {
    uint32_t bus_err_num;
} twai_node_record_t;

typedef struct {
    bool                is_tx_success;
    const twai_frame_t *done_tx_frame;
} twai_tx_done_event_data_t;

// Empty in the real header too (esp_twai_types.h:68-69). It only ever crosses the
// language boundary by pointer, so the C/C++ size disagreement -Wextern-c-compat warns
// about cannot reach anything.
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wextern-c-compat"
#endif
typedef struct {
} twai_rx_done_event_data_t;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

typedef struct {
    twai_error_state_t old_sta;
    twai_error_state_t new_sta;
} twai_state_change_event_data_t;

typedef struct {
    twai_error_flags_t err_flags;
} twai_error_event_data_t;

typedef struct {
    bool (*on_tx_done)(twai_node_handle_t handle, const twai_tx_done_event_data_t *edata, void *user_ctx);
    bool (*on_rx_done)(twai_node_handle_t handle, const twai_rx_done_event_data_t *edata, void *user_ctx);
    bool (*on_state_change)(twai_node_handle_t handle, const twai_state_change_event_data_t *edata, void *user_ctx);
    bool (*on_error)(twai_node_handle_t handle, const twai_error_event_data_t *edata, void *user_ctx);
} twai_event_callbacks_t;

#ifdef __cplusplus
}
#endif
