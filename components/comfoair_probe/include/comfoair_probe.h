// What the ventilation unit says it can do: a read-only walk of its RMI tree, and the report.
//
// PURE. No bus, no clock, no task: the walk is a state machine that hands out one request at a
// time and is told what came back, so every decision it makes -- when a unit is absent, when the
// subunits have run out, when to retry, when to give up on a unit that will not answer -- is
// exercised on the host against a fake unit. comfoair_probe_task is the device half that puts the
// requests on the bus; it decides nothing.
//
// THE WALK ONLY READS. A request is either GET ONE (`01 unit subunit type property`) or
// GETACTIVEERRORS (`80 03 01`), and the device half turns it into bytes through rmi_read.h, whose
// builders cannot produce anything else (test_rmi_read pins that). A command >= 0x80 on unit 0x01
// can brick the unit (comfoair-pdo-rmi.md §0); this header deliberately has no field that could
// carry a command byte at all.
//
// THREAD CONTEXT: none of this locks. The module state is a singleton (like comfoair_state), and
// the caller serialises every call -- comfoair_probe_task holds one mutex around all of them.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Room for every property that answered. ~110 properties are documented on node 0x01 and the
// walk covers units nobody has documented, so this is generous; a walk that fills it keeps going,
// counts what it dropped and reports `truncated`. 400 x ~70 B is ~28 KB of static RAM, which is
// the price of a report that does not allocate.
#define COMFOAIR_PROBE_MAX_ENTRIES 400

// One summary row per (node, unit) probed: 0x2F units on the target plus the identity reads of
// up to COMFOAIR_PROBE_MAX_EXTRA_NODES other nodes.
#define COMFOAIR_PROBE_MAX_UNITS 64
#define COMFOAIR_PROBE_MAX_EXTRA_NODES 8

// The longest documented value is the model string, "ComfoAir Q450 B R RF ST Quality" plus its
// NUL = 32 bytes. Anything longer is stored truncated with `len` saying so (len > stored bytes).
#define COMFOAIR_PROBE_VALUE_MAX 40
// A range is min + max of the value's width (8 bytes covers UINT32); a step is one value.
#define COMFOAIR_PROBE_AUX_MAX 8
// GETACTIVEERRORS returns a 32-byte bitfield (comfoair-pdo-rmi.md §3).
#define COMFOAIR_PROBE_ERRORS_MAX 32

// RMI GET ONE `type` byte (comfoair-pdo-rmi.md §3).
#define COMFOAIR_PROBE_TYPE_VALUE 0x10
#define COMFOAIR_PROBE_TYPE_RANGE 0x20
#define COMFOAIR_PROBE_TYPE_STEP 0x40

// RMI error codes the walk acts on (comfoair-pdo-rmi.md §3). Every other code is stored as-is.
#define COMFOAIR_PROBE_ERR_UNKNOWN_COMMAND 11
#define COMFOAIR_PROBE_ERR_UNKNOWN_UNIT 12
#define COMFOAIR_PROBE_ERR_UNKNOWN_SUBUNIT 13
#define COMFOAIR_PROBE_ERR_UNKNOWN_PROPERTY 14
#define COMFOAIR_PROBE_ERR_NO_RANGE 15
#define COMFOAIR_PROBE_ERR_NOT_GETTABLE 32

typedef enum {
    COMFOAIR_PROBE_IDLE = 0,  // never started since boot
    COMFOAIR_PROBE_RUNNING,
    COMFOAIR_PROBE_DONE,
    COMFOAIR_PROBE_FAILED,    // see comfoair_probe_failure_t
} comfoair_probe_state_t;

typedef enum {
    COMFOAIR_PROBE_FAILURE_NONE = 0,
    // The first `abort_after_silent` requests ALL went unanswered (retries included). Not a
    // conclusion about the unit's tree: it means the unit does not answer RMI from this node (for
    // example, it answers only its own gateway's node id). Every later request would be a timeout too.
    COMFOAIR_PROBE_FAILURE_NO_ANSWER,
} comfoair_probe_failure_t;

// --- the walk ----------------------------------------------------------------------------------

typedef struct {
    uint8_t target_node;      // the ventilation unit, default 0x01
    uint8_t max_unit;         // last unit id tried, default 0x2F
    uint8_t max_subunit;      // last subunit tried, default 16
    uint8_t max_property;     // last property tried, default 0x3F
    uint8_t retries;          // re-asks after a timeout before the answer stands, default 1
    uint8_t abort_after_silent;  // see COMFOAIR_PROBE_FAILURE_NO_ANSWER, default 5
    uint8_t extra_nodes[COMFOAIR_PROBE_MAX_EXTRA_NODES];  // other nodes seen on the bus
    uint8_t extra_node_count;
} comfoair_probe_plan_t;

#define COMFOAIR_PROBE_DEFAULT_PLAN()                                                          \
    {                                                                                          \
        .target_node = 0x01, .max_unit = 0x2F, .max_subunit = 16, .max_property = 0x3F,        \
        .retries = 1, .abort_after_silent = 5, .extra_nodes = {0}, .extra_node_count = 0,      \
    }

typedef enum {
    COMFOAIR_PROBE_REQ_GET = 0,        // 01 unit subunit type property
    COMFOAIR_PROBE_REQ_ACTIVE_ERRORS,  // 80 03 01 -- unit/subunit/type/property are unused
} comfoair_probe_req_kind_t;

typedef struct {
    comfoair_probe_req_kind_t kind;
    uint8_t node;
    uint8_t unit;
    uint8_t subunit;
    uint8_t type;  // COMFOAIR_PROBE_TYPE_*
    uint8_t property;
} comfoair_probe_request_t;

typedef enum {
    COMFOAIR_PROBE_ANSWER_NONE = 0,  // not asked
    COMFOAIR_PROBE_ANSWER_DATA,      // `bytes` is the reassembled payload
    COMFOAIR_PROBE_ANSWER_ERROR,     // `error` is the code; `bytes` the raw error payload
    COMFOAIR_PROBE_ANSWER_TIMEOUT,   // nothing came back, retries included
} comfoair_probe_answer_kind_t;

// What came back for the last request handed out. `data` is borrowed for the call only.
typedef struct {
    comfoair_probe_answer_kind_t kind;  // never NONE
    uint8_t error;
    const uint8_t *data;
    size_t len;
} comfoair_probe_outcome_t;

// Starts a walk, discarding the previous report. A NULL plan is the default plan.
void comfoair_probe_begin(const comfoair_probe_plan_t *plan);

// The next request to put on the bus, or false when the walk is over (DONE or FAILED). After a
// TIMEOUT outcome with retries left, the SAME request comes back -- the caller does not retry on
// its own. Calling it twice without an outcome in between returns the same request again.
bool comfoair_probe_next(comfoair_probe_request_t *out);

// The answer to the request the last comfoair_probe_next() returned. Ignored if no request is
// outstanding (a late duplicate cannot advance the walk twice).
void comfoair_probe_record(const comfoair_probe_outcome_t *outcome);

// --- the report --------------------------------------------------------------------------------

typedef struct {
    comfoair_probe_answer_kind_t kind;
    uint8_t error;
    uint8_t len;  // as received; may exceed the stored bytes, see COMFOAIR_PROBE_VALUE_MAX
    uint8_t bytes[COMFOAIR_PROBE_VALUE_MAX];
} comfoair_probe_value_t;

typedef struct {
    comfoair_probe_answer_kind_t kind;
    uint8_t error;
    uint8_t len;
    uint8_t bytes[COMFOAIR_PROBE_AUX_MAX];
} comfoair_probe_aux_t;

// One property that exists or might: it answered with data, with an error other than
// 12/13/14, or not at all. A plain "unknown property" (14) is only counted on its unit row.
typedef struct {
    uint8_t node;
    uint8_t unit;
    uint8_t subunit;
    uint8_t property;
    comfoair_probe_value_t value;
    comfoair_probe_aux_t range;  // NONE unless value is DATA
    comfoair_probe_aux_t step;   // NONE unless range is DATA
} comfoair_probe_entry_t;

typedef enum {
    COMFOAIR_PROBE_UNIT_PRESENT = 0,
    COMFOAIR_PROBE_UNIT_ABSENT,   // answered 12
    COMFOAIR_PROBE_UNIT_TIMEOUT,  // the discovery read went unanswered
} comfoair_probe_unit_status_t;

typedef struct {
    uint8_t node;
    uint8_t unit;
    comfoair_probe_unit_status_t status;
    uint8_t subunits;            // how many answered (0 unless PRESENT)
    uint16_t absent_properties;  // 14s counted across all its subunits
} comfoair_probe_unit_t;

typedef struct {
    uint32_t requests;  // frames handed out, retries included
    uint32_t timeouts;  // individual silences, retries included
    uint8_t node, unit, subunit, property;  // where the walk is now
} comfoair_probe_progress_t;

typedef struct {
    comfoair_probe_state_t state;
    comfoair_probe_failure_t failure;
    comfoair_probe_plan_t plan;
    comfoair_probe_progress_t progress;
    comfoair_probe_unit_t units[COMFOAIR_PROBE_MAX_UNITS];
    size_t unit_count;
    comfoair_probe_entry_t entries[COMFOAIR_PROBE_MAX_ENTRIES];
    size_t entry_count;
    bool truncated;  // an entry or unit row did not fit
    comfoair_probe_answer_kind_t errors_kind;
    uint8_t errors_code;
    uint8_t errors_len;
    uint8_t errors[COMFOAIR_PROBE_ERRORS_MAX];
} comfoair_probe_results_t;

// The live results. Valid until the next comfoair_probe_begin(). Read under the caller's lock.
const comfoair_probe_results_t *comfoair_probe_results(void);

#ifdef __cplusplus
}
#endif
