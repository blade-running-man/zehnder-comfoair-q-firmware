// Host stand-in for esp_twai_onchip.h (ESP-IDF 5.5.5), config struct transcribed
// field-for-field from esp_twai_onchip.h:19-40.
#pragma once

#include "esp_err.h"
#include "esp_twai_types.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    struct {
        gpio_num_t tx;
        gpio_num_t rx;
        gpio_num_t quanta_clk_out;
        gpio_num_t bus_off_indicator;
    } io_cfg;
    twai_clock_source_t        clk_src;
    twai_timing_basic_config_t bit_timing;
    twai_timing_basic_config_t data_timing;
    uint32_t                   timestamp_resolution_hz;
    int8_t                     fail_retry_cnt;
    uint32_t                   tx_queue_depth;
    int                        intr_priority;
    struct {
        uint32_t enable_self_test: 1;
        uint32_t enable_loopback: 1;
        uint32_t enable_listen_only: 1;
        uint32_t no_receive_rtr: 1;
        uint32_t sleep_allow_pd: 1;
    } flags;
} twai_onchip_node_config_t;

esp_err_t twai_new_node_onchip(const twai_onchip_node_config_t *node_config,
                               twai_node_handle_t *node_ret);

#ifdef __cplusplus
}
#endif
