// PRIVATE to comfoair_probe: the report's storage, beside the walk that fills it.
//
// The walk (comfoair_probe_scan.cpp) decides WHAT is asked and what an answer means; this file
// only puts answers where the report keeps them. Not in include/: nothing outside this component
// may write the report -- comfoair_probe_results() is the one, read-only, way in.
#pragma once

#include "comfoair_probe.h"

namespace comfoair_probe_store {

// The live report, the singleton comfoair_probe_results() returns.
extern comfoair_probe_results_t g_results;

// A new unit row, or a scratch row once the table is full (and `truncated` is set). Never NULL:
// see the sink in comfoair_probe_store.cpp for why the walk must not branch on room.
comfoair_probe_unit_t *open_row(uint8_t node, uint8_t unit, comfoair_probe_unit_status_t status);
// The scratch row a unit writes to before its discovery read has said what it is.
comfoair_probe_unit_t *row_sink();

// A new entry holding `o` as its value, or a scratch entry once the table is full.
comfoair_probe_entry_t *store_value(uint8_t node, uint8_t unit, uint8_t subunit, uint8_t property,
                                    const comfoair_probe_outcome_t &o);
comfoair_probe_entry_t *entry_sink();

// A range or step answer into its entry, and the GETACTIVEERRORS answer into the report.
void keep_aux(comfoair_probe_aux_t *dst, const comfoair_probe_outcome_t &o);
void keep_errors(const comfoair_probe_outcome_t &o);

}  // namespace comfoair_probe_store
