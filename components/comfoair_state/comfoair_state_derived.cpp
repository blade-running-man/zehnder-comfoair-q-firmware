// The entities no single PDO carries: the fan and ventilation_mode (PDOs 49 + 65) and
// balance_mode (PDOs 54 + 55). Every label and every write goes through the core
// (option_of / set_text) -- comfoair_state_internal.h says why this file must not mint its own.
#include "comfoair_state_internal.h"

#include "comfoair_lock.h"

using comfoair_state_detail::mark_fan_changed;
using comfoair_state_detail::option_of;
using comfoair_state_detail::set_text;

namespace {

// The sentinel -1000 for "not seen yet" is chosen because 0 and 0xff are both legitimate readings.
constexpr int16_t NOT_SEEN = -1000;

int16_t g_operating_mode = NOT_SEEN;  // PDO 49
int16_t g_fan_level      = NOT_SEEN;  // PDO 65
int16_t g_supply_mode    = NOT_SEEN;  // PDO 54
int16_t g_exhaust_mode   = NOT_SEEN;  // PDO 55

comfoair_fan_state_t g_fan{};

void recompute_fan()
{
    // The level alone is enough to publish; the operating mode only refines the preset.
    if (g_fan_level == NOT_SEEN)
        return;

    // Snapshot BEFORE anything is written, or the comparison at the end is against values
    // this function has already overwritten and no change is ever detected.
    const comfoair_fan_state_t before = g_fan;

    g_fan.known = true;
    g_fan.on    = g_fan_level > 0;
    g_fan.speed = static_cast<uint8_t>(g_fan_level > 0 ? g_fan_level : 1);

    // PDO 49: 0xff auto, 11 away, 6 boost, 1 limited manual, 5 unlimited manual.
    const char *preset = nullptr;
    switch (g_operating_mode) {
    case 0xff: preset = option_of("ventilation_mode", 0); break;  // Auto
    case 11:   preset = option_of("ventilation_mode", 1); break;  // Away
    case 6:    preset = option_of("ventilation_mode", 5); break;  // Boost
    case 1:
    case 5:
        // Under either manual mode the preset follows the level.
        switch (g_fan_level) {
        case 0: preset = option_of("ventilation_mode", 1); break;  // Away
        case 1: preset = option_of("ventilation_mode", 2); break;  // Low
        case 2: preset = option_of("ventilation_mode", 3); break;  // Medium
        case 3: preset = option_of("ventilation_mode", 4); break;  // High
        default: break;
        }
        break;
    default:
        break;
    }
    g_fan.preset = preset;
    if (before.known != g_fan.known || before.on != g_fan.on ||
        before.speed != g_fan.speed || before.preset != g_fan.preset)
        mark_fan_changed();

    // A null preset is the fan's "no preset" state, which a select cannot express: leave
    // the select on its last value rather than inventing one.
    if (preset != nullptr)
        set_text("ventilation_mode", preset);
}

void recompute_balance()
{
    // Wait until both halves are known: publishing from one PDO alone flip-flops it.
    if (g_supply_mode == NOT_SEEN || g_exhaust_mode == NOT_SEEN)
        return;
    // PDO 54 / 55: 1 = supply-only / extract-only, 0xff = balanced.
    uint8_t index = 0;  // Balanced
    if (g_supply_mode == 1)
        index = 1;      // Supply Only
    else if (g_exhaust_mode == 1)
        index = 2;      // Extract Only
    const char *label = option_of("balance_mode", index);
    if (label != nullptr)
        set_text("balance_mode", label);
}

}  // namespace

namespace comfoair_state_detail {

void derived_reset()
{
    g_operating_mode = NOT_SEEN;
    g_fan_level      = NOT_SEEN;
    g_supply_mode    = NOT_SEEN;
    g_exhaust_mode   = NOT_SEEN;
    g_fan            = comfoair_fan_state_t{};
}

bool apply_derived(uint16_t pdo, int64_t raw)
{
    switch (pdo) {
    case 49: g_operating_mode = static_cast<int16_t>(raw & 0xff); recompute_fan();     return true;
    case 65: g_fan_level      = static_cast<int16_t>(raw & 0xff); recompute_fan();     return true;
    case 54: g_supply_mode    = static_cast<int16_t>(raw & 0xff); recompute_balance(); return true;
    case 55: g_exhaust_mode   = static_cast<int16_t>(raw & 0xff); recompute_balance(); return true;
    default: return false;
    }
}

}  // namespace comfoair_state_detail

extern "C" bool comfoair_state_get_fan(comfoair_fan_state_t *out)
{
    if (out == nullptr)
        return false;
    comfoair_lock();
    *out = g_fan;
    comfoair_unlock();
    return true;
}
