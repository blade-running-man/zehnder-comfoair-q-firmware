// The RMI read-back of the selects the unit reports through no PDO: what a reply means, and whether
// it may be applied over the state model.
//
// PURE. No FreeRTOS, no state, no lock: the probe task (comfoair_probe_task_settings.cpp) owns the
// bus round trip, the lock, the command counters and the apply. Both calls are safe from any task.
//
// The rows read back are COMFOAIR_RMI_READBACK in registry_generated.h, generated from
// tools/pdo_table.py -- this component adds no list of its own.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "registry_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

// One GET ONE reply (`01 <unit> <subunit> 10 <property>`) into an index into the entity's own
// options. True only for exactly one byte below the entity's option_count; false for a NULL
// argument, an entry outside COMFOAIR_ENTITIES, any other length or an out-of-range value. On
// false *index_out is untouched.
bool comfoair_readback_decode(const comfoair_rmi_readback_t *entry, const uint8_t *data,
                              size_t len, uint8_t *index_out);

// The command-path guard. `queued_before` and `done_before` (sent + failed) are
// comfoair_control_get_stats() sampled BEFORE the reads; `queued_now` is `queued` sampled at apply
// time, under the same hold as the apply. True only when the sender was idle before the reads
// (nothing queued that had not gone out) AND nothing was submitted since.
//
// DO NOT replace this with a value compare-and-set ("apply only if the state still holds what it
// held before the read"). HTTP and MQTT call comfoair_control_submit() BEFORE their optimistic
// comfoair_state_set_option(), and the sender sleeps between frames with up to eight commands
// queued: a GET can reach the wire ahead of a queued SET, come back with the OLD value while the
// state still shows the old value too, and an unchanged value proves nothing. Only the command
// counters see a write that has not reached the bus yet.
//
// Equality only, never `<`: the counters are uint32_t and wrap, and a submit that was dropped
// decrements `queued` again (comfoair_control_submit), so "unchanged" is the one safe question.
bool comfoair_readback_may_apply(uint32_t queued_before, uint32_t done_before, uint32_t queued_now);

#ifdef __cplusplus
}
#endif
