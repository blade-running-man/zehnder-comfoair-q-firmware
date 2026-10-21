// The PDO request cycle.
//
// The unit broadcasts most of its values on its own, so this is a refresh for the ones
// that change rarely, not the primary source of data. The defaults have run against the real
// unit for months: one full sweep per hour, 100 ms between frames. Do not shorten either
// without a reason -- a remote-transmission request per polled PDO, back to back on a 50 kbps
// bus, is a burst the unit has no reason to expect.
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // 0x2A is the id this node has always used on the bus. Changing it is safe but
    // pointless; keep it unless a second node appears.
    uint8_t  local_node_id;
    uint32_t interval_ms;       // one full sweep of every request PDO
    uint32_t frame_delay_ms;    // spacing between individual requests
} comfoair_poll_config_t;

#define COMFOAIR_POLL_DEFAULT_CONFIG() \
    ((comfoair_poll_config_t){.local_node_id = 0x2A, .interval_ms = 3600000, .frame_delay_ms = 100})

esp_err_t comfoair_poll_start(const comfoair_poll_config_t *config);

// Sends one sweep now, without waiting for the interval. Used after the bus comes back.
void comfoair_poll_request_now(void);

// Sends ONE request for `pdo`, `delay_ms` from now -- to read back what a command just changed
// when the unit may not broadcast it on its own (comfoair_command_readback_pdo(); today the away
// timer, PDO 337, never heard between sweeps on the Q350 census -- broadcast on change is
// unconfirmed; HV-MQTT-31 records the delay). Returns at once: never sends or waits on
// the caller's task. Calls for the same PDO before it goes out coalesce into one request,
// `delay_ms` after the LAST call. A call for a different PDO while one is pending is dropped with
// a warning (one slot; one caller). A silent no-op before a successful start. Not from an ISR.
void comfoair_poll_request_pdo(uint16_t pdo, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif
