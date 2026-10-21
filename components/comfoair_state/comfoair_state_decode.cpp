// One PDO payload into one entity slot, per registry kind. Pure: no state of its own beyond the
// one airflow-constraints text buffer, and no lookup in COMFOAIR_ENTITIES -- it decodes against
// the row it is handed (comfoair_state_internal.h says why that matters).
#include "comfoair_state_internal.h"

#include <cstring>
#include <string>

#include "airflow_constraints.h"
#include "pdo_decode.h"

namespace {

const char *enum_label(const comfoair_entity_t &e, int64_t raw)
{
    for (uint8_t i = 0; i < e.option_count; i++)
        if (e.option_values != nullptr && e.option_values[i] == static_cast<int32_t>(raw))
            return e.options[i];
    // Not guessed. An unlisted value is a reading this firmware does not understand, and
    // inventing a label for it would put a made-up string in front of the user.
    return nullptr;
}

// A select's wire byte is an index into its options. Out of range is no value (`None` on MQTT)
// rather than a clamp: a clamp would show the user a mode the unit is not in.
const char *option_at(const comfoair_entity_t &e, int64_t index)
{
    if (index < 0 || index >= e.option_count)
        return nullptr;
    return e.options[static_cast<size_t>(index)];
}

void apply_numeric(const comfoair_entity_t &e, comfoair_value_t &slot, const uint8_t *data,
                   size_t len)
{
    int64_t raw = 0;
    const auto result = comfoair::decode_numeric(data, len, e.is_signed,
                                                 e.none_when_all_ones, raw);
    if (result != comfoair::DecodeResult::OK) {
        // NO_VALUE is the all-ones sentinel on the countdown rows the table flags:
        // nothing is pending.
        // UNSUPPORTED_LENGTH is a frame this decoder cannot read. Both leave the entity
        // without a value rather than with a wrong one.
        slot.type = COMFOAIR_VALUE_NONE;
        return;
    }
    slot.type = COMFOAIR_VALUE_NUMBER;
    slot.number = e.divider > 1 ? static_cast<float>(raw) / static_cast<float>(e.divider)
                                : static_cast<float>(raw);
}

}  // namespace

namespace comfoair_state_detail {

bool same_value(const comfoair_value_t &a, const comfoair_value_t &b)
{
    if (a.type != b.type)
        return false;
    switch (a.type) {
    case COMFOAIR_VALUE_NUMBER: return a.number == b.number;
    case COMFOAIR_VALUE_BOOL:   return a.boolean == b.boolean;
    // By pointer. A label points into the generated registry, so identity is equality; a
    // rendered text lives in a double buffer that moves to the other half exactly when the text
    // changes (COMFOAIR_KIND_SPECIAL below). DO NOT render a text in place into one buffer: it
    // would compare equal to itself and no change would ever be marked.
    case COMFOAIR_VALUE_TEXT:   return a.text == b.text;
    default:                    return true;
    }
}

void apply_one(const comfoair_entity_t &e, comfoair_value_t &slot, const uint8_t *data,
               size_t len)
{
    switch (e.kind) {
    case COMFOAIR_KIND_NUMERIC:
        apply_numeric(e, slot, data, len);
        break;

    case COMFOAIR_KIND_BINARY:
    case COMFOAIR_KIND_SWITCH: {
        int64_t raw = 0;
        if (comfoair::decode_numeric(data, len, false, false, raw) !=
            comfoair::DecodeResult::OK) {
            slot.type = COMFOAIR_VALUE_NONE;
            break;
        }
        // A bit past the end of a short frame was never sent: a confident false would be a guess.
        if (e.match_bit && e.match_value >= 8 * static_cast<int32_t>(len)) {
            slot.type = COMFOAIR_VALUE_NONE;
            break;
        }
        // A `bit` row (PDO 337's timer bitset) tests one bit; every other row compares the
        // whole value. raw came out of decode_numeric unsigned, so the shift sees no sign bits.
        const bool hit = e.match_bit
                             ? ((static_cast<uint64_t>(raw) >> e.match_value) & 1u) != 0
                             : raw == e.match_value;
        slot.type = COMFOAIR_VALUE_BOOL;
        slot.boolean = e.match_negated ? !hit : hit;
        break;
    }

    case COMFOAIR_KIND_ENUM:
    case COMFOAIR_KIND_SELECT: {
        int64_t raw = 0;
        if (comfoair::decode_numeric(data, len, false, false, raw) !=
            comfoair::DecodeResult::OK) {
            slot.type = COMFOAIR_VALUE_NONE;
            break;
        }
        // An enum maps a WIRE VALUE to a label -- operating_mode's values are 1, 5, 6, 11,
        // 255, so index and value are different things. A select maps an INDEX: PDO 66's
        // byte is 0/1/2 straight into Auto/Open/Closed. Sharing one lookup left both
        // PDO-backed selects permanently without a value, because the generator only emits
        // option_values for enums. Counter-review found it; no test had ever exercised a select.
        const char *label = (e.kind == COMFOAIR_KIND_SELECT)
                                ? option_at(e, raw)
                                : enum_label(e, raw);
        if (label == nullptr) {
            slot.type = COMFOAIR_VALUE_NONE;
            break;
        }
        slot.type = COMFOAIR_VALUE_TEXT;
        slot.text = label;
        break;
    }

    case COMFOAIR_KIND_SPECIAL: {
        // PDO 230's constraint bitset -- the reading that answers "why is the unit not
        // reaching the setpoint". The decoder was ported and tested in phase 2 and then
        // called by nobody, so the entity read null for ever until counter-review said so.
        //
        // decode_u64 rather than decode_numeric: the bitset routinely arrives in 7 bytes
        // (bit 54 lives in byte 6) and decode_numeric only accepts 1, 2, 4 or 8.
        uint64_t bits = 0;
        if (!comfoair::decode_u64(data, len, bits)) {
            slot.type = COMFOAIR_VALUE_NONE;
            break;
        }
        // The formatter returns a std::string; the value model holds a const char *, so the
        // text is copied into storage that outlives the call.
        //
        // TWO buffers, alternated, and a fresh one only when the TEXT moved. same_value() compares
        // TEXT by pointer, and one buffer rewritten in place compared equal to itself: every
        // change after the first was never marked, so MQTT and the WebSocket kept the boot's first
        // constraint text while GET /api/state showed the current one. A changed text is now a
        // new pointer (marked); an unchanged one keeps the old pointer (not marked); and a reader
        // that copied the old pointer under the lock still reads a whole string, because the next
        // write goes to the OTHER buffer. One pair, not one per entity: `special` is also the kind
        // of active_errors, but that row has no PDO and never reaches this decoder -- the probe
        // task sets its text through comfoair_state's setter, which keeps a pair of its own.
        static char text[2][comfoair::AIRFLOW_CONSTRAINTS_MAX_LENGTH + 1];
        static unsigned current;
        const std::string rendered = comfoair::airflow_constraints_to_string(bits);
        if (slot.type == COMFOAIR_VALUE_TEXT && slot.text == text[current] &&
            rendered == text[current])
            break;
        current ^= 1u;
        std::strncpy(text[current], rendered.c_str(), sizeof text[current] - 1);
        text[current][sizeof text[current] - 1] = '\0';
        slot.type = COMFOAIR_VALUE_TEXT;
        slot.text = text[current];
        break;
    }

    default:
        // number, fan, diagnostic_const: no PDO carries them, so nothing to apply.
        break;
    }
}

}  // namespace comfoair_state_detail
