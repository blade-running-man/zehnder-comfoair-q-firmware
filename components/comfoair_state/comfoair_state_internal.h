// Private to components/comfoair_state/: the seams between the four files of the state model.
// NOT under include/, so no other component can reach it -- the public contract stays
// comfoair_state.h alone.
//
// Every function here runs with comfoair_lock() ALREADY HELD by its caller; none of them takes it.
//
// ONE TRANSLATION UNIT TOUCHES COMFOAIR_ENTITIES, and that is load-bearing, not tidiness.
// registry_generated.h defines the table and its option arrays `static const`, so every .cpp that
// includes it holds its own copy, and a label taken from one file's copy is a DIFFERENT POINTER
// from the same label in another's. comfoair_state.cpp compares labels by pointer (set_text,
// same_value), so a label minted in comfoair_state_derived.cpp would never equal the one
// comfoair_state_set_option() stored and would mark every frame "changed". Hence: the decode file
// only reads the entity row it is HANDED, and the derived file asks the core for every label
// (option_of) and every write (set_text). DO NOT look a key up in COMFOAIR_ENTITIES from either.
#pragma once

#include <cstddef>
#include <cstdint>

#include "comfoair_state.h"

namespace comfoair_state_detail {

// --- comfoair_state_decode.cpp: one PDO payload into one slot, pure ---------------------------

// Writes the slot for entity `e` from a PDO payload; COMFOAIR_VALUE_NONE when the payload is
// undecodable or names a value the registry does not list. `e` must be the core's own row.
void apply_one(const comfoair_entity_t &e, comfoair_value_t &slot, const uint8_t *data,
               size_t len);
// Value equality as the change marks need it: labels by pointer (see above).
bool same_value(const comfoair_value_t &a, const comfoair_value_t &b);

// --- comfoair_state_derived.cpp: the fan, ventilation_mode and balance_mode -------------------

// Forgets every derived input and the fan, so nothing is published until the PDOs arrive again.
void derived_reset();
// true when `pdo` is one of the four that drive a derived entity; `raw` is the unsigned payload.
bool apply_derived(uint16_t pdo, int64_t raw);

// --- comfoair_state.cpp: what the derived file needs from the core ----------------------------

// Stores a registry label on `key` and marks it changed only when the label differs.
void set_text(const char *key, const char *label);
// The fan has its own struct, not a g_values slot, so it is marked through every KIND_FAN row.
void mark_fan_changed();
// The `index`-th option label of `key`, from the core's copy of the registry; NULL when out of range.
const char *option_of(const char *key, uint8_t index);

// --- comfoair_state_marks.cpp: the change marks ------------------------------------------------

// Marks entity `index` changed for every consumer and raises its generation -- even when the mark
// is already set, because that repeat is the only signal of a second change in flight.
void mark_changed(size_t index);
// Clears every consumer's marks; the generations keep rising (comfoair_state_reset()).
void marks_reset();

}  // namespace comfoair_state_detail
