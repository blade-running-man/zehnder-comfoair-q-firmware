// The probe's report as JSON: the catalogue of known RMI properties, the decoders, the renderer.
//
// PURE, and a component of its own beside comfoair_probe so that "what the bytes mean" is tested
// apart from "which bytes were asked for". The catalogue here is the ONE list of known RMI
// properties (names, formats, scales, enumerations); the web UI renders whatever names this emits
// and keeps no copy. It is not a second entity list: registry entities are PDOs and commands, and
// RMI properties are not in the registry.
//
// THREAD CONTEXT: none of this locks. The caller holds whatever lock guards `results`.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "comfoair_probe.h"

#ifdef __cplusplus
extern "C" {
#endif

// One PDO of the census: every PDO number heard on the bus since boot, not only the ones the
// firmware knows. Declared HERE, not in the census component, so the renderer stays pure and
// depends on no store: the device copies its records into this shape one row at a time.
typedef struct {
    uint16_t pdo;
    bool known;          // the PDO is in the firmware's entity table (COMFOAIR_REQUEST_PDOS)
    uint32_t count;      // frames of this PDO
    uint32_t first_ms;   // on the uptime clock of comfoair_probe_pdo_summary_t.uptime_ms
    uint32_t last_ms;
    uint16_t dlc_mask;   // bit n set: a frame of length n (0..8) was seen; higher bits ignored
    uint64_t node_mask;  // bit n set: node n (0..63) sent it
    uint8_t last_len;    // 0..8; a larger value renders as 8 bytes, never a read past `last`
    uint8_t last[8];
} comfoair_probe_pdo_row_t;

typedef struct {
    uint32_t uptime_ms;  // ms since boot at render; a u32, it wraps after 49.7 days
    uint32_t frames;     // every PDO frame recorded, including PDOs that did not fit a row
    uint32_t distinct;   // distinct PDO numbers heard
    uint32_t overflow;   // distinct minus the rows recorded
} comfoair_probe_pdo_summary_t;

// Fills row `i` (0 <= i < rows) in ascending PDO order. Returns false if it could not; the
// renderer then skips that row and goes on with the next -- one record the device cannot copy
// must not cost the rest of the census. Called on the render's task, under the caller's lock.
typedef bool (*comfoair_probe_pdo_row_fn)(void *ctx, size_t i, comfoair_probe_pdo_row_t *out);

// What the device half knows and the pure walk does not.
typedef struct {
    uint32_t elapsed_ms;      // since the walk began, or of the whole walk once it ended
    uint8_t another_counter;  // the CAN-id bits 12-13 the walk is being sent with
    const uint8_t *nodes_seen;  // every node id heard on the bus, ascending
    size_t nodes_seen_count;
    // The PDO census, appended LAST so every existing initialiser stays valid (the new members
    // zero-initialise). NULL summary: the report has no "pdos" key at all. A non-NULL summary
    // with a NULL `pdo_row` renders an empty `rows`.
    const comfoair_probe_pdo_summary_t *pdo_summary;
    size_t pdo_rows;
    comfoair_probe_pdo_row_fn pdo_row;
    void *pdo_ctx;
} comfoair_probe_meta_t;

// Receives the JSON in pieces. Returning false stops the render (the client went away).
typedef bool (*comfoair_probe_write_fn)(void *ctx, const char *text, size_t len);

// Renders the report (schema: docs/protocol/probe.md, "The report") as a
// sequence of writes, each at most a few hundred bytes, so the HTTP layer can stream it without a
// buffer the size of the report. Returns false if a write did; the output is then incomplete.
bool comfoair_probe_render(const comfoair_probe_results_t *results,
                           const comfoair_probe_meta_t *meta, comfoair_probe_write_fn write,
                           void *ctx);

// The unit's firmware version word as text: "R1.11.0" for 0xC0102C00. Writes at most `cap`
// bytes including the NUL; returns the length written (0 if `cap` is too small). The one
// implementation -- GET /api/system and the probe report both print firmware through it.
size_t comfoair_probe_version_text(uint32_t raw, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
