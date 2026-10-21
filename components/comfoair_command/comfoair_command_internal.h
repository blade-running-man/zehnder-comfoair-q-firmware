// Private to components/comfoair_command/, C++ only: the frame builders comfoair_command.cpp
// dispatches to, defined in comfoair_command_build.cpp. NOT under include/, so no other component
// can reach them -- the public contract stays comfoair_command.h alone.
//
// A NAMED namespace, because an anonymous namespace is private to one translation unit and cannot
// span two. comfoair_command.cpp pulls each name in with a `using` declaration, so its call sites
// use the bare names.
//
// Every builder appends to `out` through rmi_frame.h and nothing else. Those that can refuse
// return COMFOAIR_COMMAND_OK or the refusal the caller hands back unchanged. None of them zeroes
// `out`; the public entry points do that before dispatching.
#pragma once

#include <cstdint>

#include "comfoair_command.h"

namespace comfoair_command_detail {

// Minutes (what the registry advertises) to seconds (what the wire takes).
int32_t minutes_to_seconds(uint16_t minutes);

// --- the four operations that also back an entity ---------------------------------------------

// `seconds <= 0` CANCELS the boost.
void build_boost(comfoair_command_t *out, int32_t seconds);
// "auto" / "open" / "closed"; `seconds <= 0` is indefinite, and "auto" ignores it.
comfoair_command_err_t build_bypass(comfoair_command_t *out, const char *mode, int32_t seconds);
// "balanced" / "supply_only" / "exhaust_only"; always two frames.
comfoair_command_err_t build_balance(comfoair_command_t *out, const char *mode, int32_t seconds);
// Level 0..3, preceded by manual_on when `permanent_manual`.
comfoair_command_err_t build_level(comfoair_command_t *out, int32_t level, bool permanent_manual);
// Index 0..2 = Normal/Cool/Warm, protocol order.
comfoair_command_err_t build_profile(comfoair_command_t *out, int index);
// Off ignores `seconds`; on with `seconds <= 0` is indefinite.
void build_away(comfoair_command_t *out, bool enabled, int32_t seconds);

// --- the entity table --------------------------------------------------------------------------

// true for the four *_duration entities, which store a number and produce no frame.
bool is_duration_entity(const char *key);
// One TEMPHUMCONTROL property write; `index` 0..2 = off/auto/on.
comfoair_command_err_t build_off_auto_on(comfoair_command_t *out, uint8_t property, int index);
// The ventilation preset by LABEL: Auto / Boost / Away / Low / Medium / High.
comfoair_command_err_t build_preset(comfoair_command_t *out, const char *label,
                                    const comfoair_command_durations_t *d);

}  // namespace comfoair_command_detail
