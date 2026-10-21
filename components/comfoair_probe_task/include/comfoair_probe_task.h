// The device half of comfoair_probe: one task that puts the walk's requests on the bus and hands
// the answers back. It decides nothing -- every "what next" is comfoair_probe's.
//
// It is the ONLY reader of RMI responses in this firmware, and it listens only while a walk or the
// unit read is running and only for frames addressed to COMFOAIR_CONTROL_LOCAL_NODE from the node
// being asked. The unit read also reads back the selects no PDO reports and applies them to the
// state model (comfoair_probe_task_settings.cpp) -- the one place RMI feeds an entity.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "comfoair_probe_report.h"
#include "comfoair_sysinfo.h"

#ifdef __cplusplus
extern "C" {
#endif

// Who to tell when a unit read changed the state model: the settings read-back
// (comfoair_probe_task_settings.cpp: temperature_passive, humidity_comfort, humidity_protection)
// and the active errors (unit_error, active_errors -- from the full read or the once-a-minute
// errors read). Called ON THE PROBE TASK, at most once per settings read and once per errors
// answer, with no lock held; it MUST NOT block -- the
// probe task shares the bus with nothing, but a stall here postpones every later read. NULL (the
// default) tells nobody. Set it BEFORE comfoair_probe_task_init(): the pointer is read by the task
// without a lock, and only the task's creation orders the write before the reads.
//
// An observer rather than a direct call because the publishers depend on this component
// (comfoair_http includes comfoair_probe_task.h for /api/probe and /api/system), so calling them
// from here would be a dependency cycle; src/main.cpp wires the two together.
void comfoair_probe_task_set_observer(void (*on_state_changed)(void));

// Creates the lock, the frame queue and the (idle) task. Call once, after comfoair_can_start()
// and comfoair_control_start() have succeeded. ESP_ERR_INVALID_STATE on a second call,
// ESP_ERR_NO_MEM if the task or queue cannot be made.
esp_err_t comfoair_probe_task_init(void);

typedef struct {
    uint8_t node;             // the node to walk, default 0x01
    uint8_t another_counter;  // CAN-id bits 12-13, 0 or 1; default 0 (comfoair-pdo-rmi.md §0)
} comfoair_probe_start_t;

// Starts a walk and returns at once. ESP_ERR_INVALID_STATE if one is already running;
// ESP_ERR_NOT_FOUND if init has not run (no CAN bus on this device -- the HTTP layer answers 503,
// not 409, and needs the two apart); ESP_ERR_INVALID_ARG for a node outside 1..63, our own node,
// or another_counter > 3. Never blocks: it is called from the HTTP task.
esp_err_t comfoair_probe_task_start(const comfoair_probe_start_t *start);

// Every received extended CAN frame, from the CAN receive callback. Records the source node in the
// census and, while a walk is running, queues RMI responses addressed to us. NEVER BLOCKS: a full
// queue drops the frame (the walk then sees a timeout and retries) -- a stall here would stop the
// task that reads the bus and lose PDOs.
void comfoair_probe_task_on_frame(uint32_t can_id, const uint8_t *data, size_t len);

// The report, rendered under the lock. ESP_ERR_INVALID_STATE before init; ESP_FAIL if `write`
// returned false.
esp_err_t comfoair_probe_task_render(comfoair_probe_write_fn write, void *ctx);

// The unit as last read: identity (NODE 0x01/01 0x04 0x06 0x08 0x0B 0x0D 0x14) and the active-error
// bitfield. The task reads it by itself 10 s after init and every 6 h after a complete read (60 s
// after an incomplete one -- complete also needs the settings read-back and the firmware word),
// reads the errors alone every 60 s in between, and refreshes it from a finished walk; a running
// walk postpones it. Copied under the
// lock; never blocks for longer than a render holds that lock. Before init *out is zeroed
// (states UNKNOWN) and ESP_ERR_INVALID_STATE is returned.
esp_err_t comfoair_probe_task_unit(comfoair_sysinfo_unit_t *out);

// Every node id heard on the bus since boot, ascending, into `out` (64 entries); returns how many.
// Any task, never blocks (a spinlock held for one 64-bit copy), needs no init: the census is filled
// from on_frame() whether or not init ever ran -- which is how GET /api/system shows a bus with
// nodes on it even on a boot where the probe task did not start.
size_t comfoair_probe_task_nodes(uint8_t out[64]);

#ifdef __cplusplus
}
#endif
