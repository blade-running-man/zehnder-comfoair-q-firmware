// comfoair_command: the frame builders comfoair_command.cpp dispatches to. Read comfoair_command.h
// first; the component's shape is in README.md, Implementation.
//
// NOTHING in this file constructs a frame by hand -- every byte comes out of rmi_frame.h, which
// has 48 tests of its own and carries the reasoning for each constant. A literal 0x84 written here
// would be a second copy of the protocol.
#include "comfoair_command_internal.h"

#include <cstring>

#include "rmi_frame.h"

namespace comfoair_command_detail {

namespace {

using comfoair::rmi::Frame;

void append(comfoair_command_t *out, const Frame &frame, uint16_t delay_ms = 0)
{
    // An empty command would become a valid DLC=0 CAN frame: a silent no-op that looks like a
    // successful send (rmi_frame.h:154-155). It cannot happen from the builders below, and it is
    // refused here rather than trusted not to.
    if (frame.empty() || frame.size() > COMFOAIR_COMMAND_MAX_BYTES ||
        out->count >= COMFOAIR_COMMAND_MAX_FRAMES)
        return;
    comfoair_command_frame_t &slot = out->frames[out->count++];
    memcpy(slot.bytes, frame.data(), frame.size());
    slot.len      = static_cast<uint8_t>(frame.size());
    slot.delay_ms = delay_ms;
}

// 0 and every negative value mean "until told otherwise" for bypass, balance and away, which on
// the wire is the indefinite timeout. Boost is the exception and does not come through here.
int32_t timeout_or_indefinite(int32_t seconds)
{
    return seconds > 0 ? seconds : comfoair::rmi::TIMEOUT_INDEFINITE;
}

// The "Auto" preset: release boost, release away, and release manual LAST and LATE.
//
// Both clears are needed because boost and away override the scheduler, and the third frame is
// deferred by 1000 ms. The delay is carried out of here as a number; this component has no clock.
void build_auto(comfoair_command_t *out)
{
    append(out, comfoair::rmi::boost_off());
    append(out, comfoair::rmi::away_off());
    append(out, comfoair::rmi::manual_off(), 1000);
}

}  // namespace

// Minutes to seconds, once. The registry advertises minutes and the wire takes seconds, and getting
// this wrong makes a 60-minute boost last a minute.
int32_t minutes_to_seconds(uint16_t minutes) { return static_cast<int32_t>(minutes) * 60; }

// --- the four operations that also back an entity ---------------------------------------------

void build_boost(comfoair_command_t *out, int32_t seconds)
{
    // 0 CANCELS boost. It is the one place where 0 does not mean "forever", and the asymmetry is
    // deliberate: it is the only way the `boost` operation can end a boost early.
    if (seconds <= 0)
        append(out, comfoair::rmi::boost_off());
    else
        append(out, comfoair::rmi::boost_on(seconds));
}

comfoair_command_err_t build_bypass(comfoair_command_t *out, const char *mode, int32_t seconds)
{
    const int32_t timeout = timeout_or_indefinite(seconds);
    if (mode == nullptr)
        return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
    if (strcmp(mode, "auto") == 0)
        // A release. It takes NO timeout -- clear_timer has no such field -- so the duration is
        // ignored here.
        append(out, comfoair::rmi::bypass_auto());
    else if (strcmp(mode, "open") == 0)
        append(out, comfoair::rmi::bypass_activate(timeout));
    else if (strcmp(mode, "closed") == 0)
        append(out, comfoair::rmi::bypass_deactivate(timeout));
    else
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    return COMFOAIR_COMMAND_OK;
}

comfoair_command_err_t build_balance(comfoair_command_t *out, const char *mode, int32_t seconds)
{
    if (mode == nullptr)
        return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
    const int32_t timeout = timeout_or_indefinite(seconds);

    // BOTH FRAMES, EVERY TIME. Dropping the second frame leaves the other subunit's timer running,
    // and switching extract-only -> supply-only would then leave the unit in an undefined state
    // with both timers set.
    //
    // And the value byte is 01 for BOTH subunits 06 and 07 -- the GETTIMERENTRY read-back reports
    // 01, and 00 is the easy mistake (rmi_frame.h:107-108). It is not written here;
    // supply_only_on/exhaust_only_on carry it.
    if (strcmp(mode, "balanced") == 0) {
        append(out, comfoair::rmi::supply_only_off());
        append(out, comfoair::rmi::exhaust_only_off());
    } else if (strcmp(mode, "supply_only") == 0) {
        append(out, comfoair::rmi::supply_only_on(timeout));
        append(out, comfoair::rmi::exhaust_only_off());
    } else if (strcmp(mode, "exhaust_only") == 0) {
        append(out, comfoair::rmi::exhaust_only_on(timeout));
        append(out, comfoair::rmi::supply_only_off());
    } else {
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    }
    return COMFOAIR_COMMAND_OK;
}

comfoair_command_err_t build_level(comfoair_command_t *out, int32_t level, bool permanent_manual)
{
    if (level < 0 || level > 3)
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    // The manual command FIRST, then the level. A bare level command is the display's *temporary*
    // change: the unit reverts to AUTO at the next scheduler step or after at most two hours. The
    // owner chose whether to pair it; the default is on.
    if (permanent_manual)
        append(out, comfoair::rmi::manual_on());
    append(out, comfoair::rmi::ventilation_level(static_cast<uint8_t>(level)));
    return COMFOAIR_COMMAND_OK;
}

comfoair_command_err_t build_profile(comfoair_command_t *out, int index)
{
    // 0/1/2 is Normal/Cool/Warm in PROTOCOL order. The timeout is hardcoded indefinite inside
    // temperature_profile(); there is no duration parameter and there must not be one.
    if (index < 0 || index > 2)
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    append(out, comfoair::rmi::temperature_profile(static_cast<uint8_t>(index)));
    return COMFOAIR_COMMAND_OK;
}

void build_away(comfoair_command_t *out, bool enabled, int32_t seconds)
{
    // Off ignores the duration -- there is nothing to time. The value byte on the ON path is 0x00,
    // not 0x01 (rmi_frame.h:92-94).
    if (!enabled)
        append(out, comfoair::rmi::away_off());
    else
        append(out, comfoair::rmi::away_on(timeout_or_indefinite(seconds)));
}

// --- the entity table --------------------------------------------------------------------------

bool is_duration_entity(const char *key)
{
    return strcmp(key, "boost_duration") == 0 || strcmp(key, "bypass_duration") == 0 ||
           strcmp(key, "balance_duration") == 0 || strcmp(key, "away_duration") == 0;
}

// The one place an option index becomes an off/auto/on property write. Subunit is 0x01 and the
// unit is TEMPHUMCONTROL; the property distinguishes the three.
comfoair_command_err_t build_off_auto_on(comfoair_command_t *out, uint8_t property, int index)
{
    if (index < 0 || index > 2)
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    append(out, comfoair::rmi::set_property(comfoair::rmi::UNIT_TEMPHUMCONTROL, 0x01, property,
                                            static_cast<uint8_t>(index)));
    return COMFOAIR_COMMAND_OK;
}

// The ventilation preset, by LABEL. This one select does not use its index as a wire byte at all
// -- it dispatches on the string: "Auto" and "Boost" are commands of their own, not levels.
comfoair_command_err_t build_preset(comfoair_command_t *out, const char *label,
                                    const comfoair_command_durations_t *d)
{
    if (label == nullptr)
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    if (strcmp(label, "Auto") == 0) {
        build_auto(out);
        return COMFOAIR_COMMAND_OK;
    }
    if (strcmp(label, "Boost") == 0) {
        build_boost(out, minutes_to_seconds(d->boost_minutes));
        return COMFOAIR_COMMAND_OK;
    }
    // Away / Low / Medium / High are levels 0..3. "Away" here is the fan PRESET -- the lowest
    // level -- and NOT away MODE, which is a different thing with its own command and its own
    // countdown.
    static const char *const levels[] = {"Away", "Low", "Medium", "High"};
    for (int i = 0; i < 4; i++)
        if (strcmp(label, levels[i]) == 0)
            return build_level(out, i, d->permanent_manual);
    return COMFOAIR_COMMAND_ERR_BAD_VALUE;
}

}  // namespace comfoair_command_detail
