// Between the two halves of comfoair_probe_task: the walk (comfoair_probe_task.cpp -- the task,
// the lock, start/render) and the bus side (comfoair_probe_task_bus.cpp -- the census, the reply
// queue, the transmit and the reply wait) -- plus the unit's identity read
// (comfoair_probe_task_unit.cpp) and its settings read-back (comfoair_probe_task_settings.cpp),
// which run on the same task. Not a public header; nothing outside
// this component includes it.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "comfoair_probe.h"
#include "comfoair_probe_report.h"
#include "comfoair_sysinfo.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "rmi_response.h"

// The pause between requests, for the walk and the identity read alike. With
// CONFIG_FREERTOS_HZ=100 this is two ticks, so the real gap is 10-20 ms; it exists so a walk of
// ~3200 reads leaves the bus room for PDOs, not to time anything, and a shorter gap is harmless.
#define PROBE_GAP_MS 20

// The one mutex around every comfoair_probe_* call and the unit snapshot. Defined in
// comfoair_probe_task.cpp; exists once init has run.
void probe_lock(void);
void probe_unlock(void);

// Creates the reply queue. ESP_ERR_NO_MEM on failure; ESP_OK if it already exists.
esp_err_t probe_bus_init(void);

// Whether on_frame() queues replies. On from start() until the walk ends.
void probe_bus_listen(bool on);

// Every node heard on the bus since boot, ascending. `out` holds 64. Any task; never blocks.
size_t probe_bus_census(uint8_t *out);

// The PDO census (comfoair_probe_task_bus.cpp), fed by on_frame() on the CAN receive task. Any
// task; neither blocks beyond a spinlock held for a few dozen bytes.
//
// Fills `summary` (uptime_ms is now) and writes up to `cap` record indices in ascending PDO order
// into `order`; returns how many. `count` is snapshotted under the census lock and the sort runs
// outside it, so rows appended after this call are simply not in this render.
size_t probe_pdos_snapshot(comfoair_probe_pdo_summary_t *summary, uint16_t *order, size_t cap);

// Copies record `index` (an index from `order`) under the census lock into `out`, with `known`
// false. False for an index past the table.
bool probe_pdos_row(uint16_t index, comfoair_probe_pdo_row_t *out);

// Puts one request on the bus and waits up to the reply window. `out->data` borrows `reasm`'s
// buffer until the next call. Runs on the walk task, WITHOUT the walk's lock held.
void probe_bus_ask(const comfoair_probe_request_t *req, uint8_t another_counter,
                   comfoair::rmi::ResponseReassembler &reasm, comfoair_probe_outcome_t *out);

// --- the unit's identity read (comfoair_probe_task_unit.cpp) ----------------------------------
// Everything below runs on the probe task unless it says otherwise.

// Arms the first unit read, COMFOAIR_UNIT_FIRST_S from now. Once, at the top of the task.
void probe_unit_arm(void);

// Ticks until a unit read (full or errors-only) is due; 0 when one is. For ulTaskNotifyTake's
// timeout.
TickType_t probe_unit_wait_ticks(void);

// Runs whichever unit read is due -- the full one (the six GET ONE reads, GETACTIVEERRORS and the
// settings read-back) or the active errors alone (comfoair_unit_cadence) -- on the probe task,
// between walks, through probe_bus_ask(). Takes the probe lock itself, never across a round trip;
// writes the two unit-error entities (comfoair_state, comfoair_lock()) after releasing it.
void probe_unit_run_due(void);

// Wakes the WebSocket and MQTT publishers after a read changed the state model. Defined in
// comfoair_probe_task_settings.cpp beside the observer it calls. Takes no lock (the observer only
// raises the publishers' flags); a no-op before comfoair_probe_task_set_observer().
void probe_notify_state_changed(void);

// UNDER THE LOCK, in the hold that saw a walk end DONE: refreshes the unit from the walk's results.
void probe_unit_from_walk_locked(void);

// UNDER THE LOCK, any task: the snapshot with identity_age_ms filled in.
void probe_unit_copy_locked(comfoair_sysinfo_unit_t *out);

// --- the settings read-back (comfoair_probe_task_settings.cpp) --------------------------------

// GET ONE of every COMFOAIR_RMI_READBACK row, applied to the state model behind the command-path
// guard (comfoair_readback_may_apply). Called by the full unit read with listening ON, WITHOUT the
// probe lock held: it takes comfoair_lock() itself for each apply. Calls the observer
// (probe_notify_state_changed) once if any value changed. True when every row is final -- a
// value applied, an error answer, or a malformed answer; false when the sender was busy, a row went
// unanswered, or a command was submitted during the read, which the caller turns into a 60 s retry.
bool probe_settings_read(comfoair::rmi::ResponseReassembler &reasm);
