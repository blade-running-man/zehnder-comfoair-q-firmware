// When one entity's state may go out over MQTT: the per-row deadband and minimum interval
// tools/pdo_table.py declares (NumericPdo.publish_deadband / publish_min_interval_s), and the one
// bound every held change obeys, COMFOAIR_PUBLISH_MAX_HOLD_S.
//
// PURE. No clock, no task, no lock: the MQTT publisher owns one slot per entity, the millisecond
// clock and the publish (comfoair_mqtt_publish.c), and calls these from its own task only.
//
// MQTT ONLY, deliberately. GET /api/state is the read-back authority -- a client re-reads after a
// write and must get what the unit said, not something within 25 rpm of it -- and the WebSocket is
// a LAN delta with no broker, no retained store and no third-party subscribers. Criterion 3 asks
// both transports to cover the same entities, which they still do; MQTT's value is a declared
// projection of REST's: within one deadband, or one min interval old, and exact within
// COMFOAIR_PUBLISH_MAX_HOLD_S of the last change.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_state.h"
#include "registry_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

// What THIS CONNECTION last put on the broker for one entity. Zeroed = nothing yet. The owner zeroes
// every slot on a new connection: a new session may meet a broker whose retained copy is stale or
// gone, so its first publication of every row must go out whatever the last session sent.
typedef struct {
    bool     sent;
    bool     was_number;
    float    number;  // meaningful when was_number
    uint32_t at_ms;   // the caller's wrapping millisecond clock
} comfoair_publish_rate_slot_t;

typedef enum {
    COMFOAIR_PUBLISH_SEND = 0,  // publish now; record it with comfoair_publish_rate_record()
    COMFOAIR_PUBLISH_HOLD,      // not yet: keep the change mark and ask again on a later pass
    COMFOAIR_PUBLISH_SAME,      // the broker holds exactly this already: clear the mark, send nothing
} comfoair_publish_verdict_t;

// True when the row declares a deadband or a minimum interval. Only numeric rows can: the fields
// exist on pdo_table.py's NumericPdo alone, because an enum, a binary, a select or a switch moving
// IS the state change and must never wait.
bool comfoair_publish_rate_limited(const comfoair_entity_t *entity);

// The verdict for `value` against `slot` at `now_ms`, in this order:
//   1. no policy on the row, or nothing sent on this connection yet  -> SEND
//   2. a reading appearing or going away (number <-> none)           -> SEND
//   3. exactly the value already sent                                 -> SAME
//   4. >= COMFOAIR_PUBLISH_MAX_HOLD_S since the last publication      -> SEND
//   5. |delta| < deadband, or < min interval since the last one       -> HOLD
//   6. otherwise                                                      -> SEND
// A NULL argument answers SEND: holding back is the optimisation, publishing is the contract.
comfoair_publish_verdict_t comfoair_publish_rate_decide(const comfoair_entity_t *entity,
                                                        const comfoair_value_t *value,
                                                        const comfoair_publish_rate_slot_t *slot,
                                                        uint32_t now_ms);

// Records a publication that succeeded. Call it only after the broker accepted the message: a
// slot recording a value the broker never got would answer SAME to it forever after.
void comfoair_publish_rate_record(comfoair_publish_rate_slot_t *slot,
                                  const comfoair_value_t *value, uint32_t now_ms);

#ifdef __cplusplus
}
#endif
