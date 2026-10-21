// comfoair_command: the public entry points and the registry lookups that decide WHICH command a
// request is. Read comfoair_command.h first; the bytes are built in comfoair_command_build.cpp.
//
// This file uses rmi_frame.h only for the PROP_* constants it hands to a builder; it builds no
// frame itself.
#include "comfoair_command.h"

#include <cstring>

#include "comfoair_command_internal.h"
#include "registry_generated.h"
#include "rmi_frame.h"

using comfoair_command_detail::minutes_to_seconds;
using comfoair_command_detail::build_boost;
using comfoair_command_detail::build_bypass;
using comfoair_command_detail::build_balance;
using comfoair_command_detail::build_level;
using comfoair_command_detail::build_profile;
using comfoair_command_detail::build_away;
using comfoair_command_detail::is_duration_entity;
using comfoair_command_detail::build_off_auto_on;
using comfoair_command_detail::build_preset;

namespace {

// The registry is the source of truth for what an entity IS. This file only decides how to write
// one, so every lookup goes through the generated table rather than a list here.
const comfoair_entity_t *entity(const char *key)
{
    if (key == nullptr)
        return nullptr;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
            return &COMFOAIR_ENTITIES[i];
    return nullptr;
}

// The index of an option label in an entity's list, or -1. Matched EXACTLY, including case: these
// are the table's own labels, the order is protocol order, and the nearest label is a different
// command (pdo_table.py, SELECTS row temperature_profile -- "fixing" the order to match the unit's display would silently
// send Warm when the user picks Normal).
int option_index(const comfoair_entity_t *e, const char *text)
{
    if (e == nullptr || e->options == nullptr || text == nullptr)
        return -1;
    for (uint8_t i = 0; i < e->option_count; i++)
        if (strcmp(e->options[i], text) == 0)
            return static_cast<int>(i);
    return -1;
}

}  // namespace

// --- the public surface --------------------------------------------------------------------

extern "C" {

void comfoair_command_durations_defaults(comfoair_command_durations_t *out)
{
    if (out == nullptr)
        return;
    // The registry's own defaults, not a second set of numbers. away_duration's default is 0 and
    // that is a SETTING there -- 0 means indefinite -- while it is out of range for the other
    // three, whose minimum is 1.
    out->boost_minutes    = 60;
    out->bypass_minutes   = 60;
    out->balance_minutes  = 60;
    out->away_minutes     = 0;
    out->permanent_manual = true;
    // 0 means "nothing reported yet", which build_level falls back to Low for.
    out->fan_resume_level = 0;
}

bool comfoair_command_knows_entity(const char *key)
{
    const comfoair_entity_t *e = entity(key);
    // Writability is the REGISTRY's answer, asked here rather than restated. An entity this file
    // could write but the table calls read-only would be a privileged path; one the table calls
    // writable and this file cannot write is the phase 3 failure.
    if (e == nullptr || !e->writable)
        return false;
    static const char *const known[] = {
        "bypass_mode",         "temperature_profile", "balance_mode",     "temperature_passive",
        "humidity_comfort",    "humidity_protection", "ventilation_mode", "away_mode",
        "fan",                 "boost_duration",      "bypass_duration",  "balance_duration",
        "away_duration",
    };
    for (size_t i = 0; i < sizeof known / sizeof known[0]; i++)
        if (strcmp(known[i], key) == 0)
            return true;
    return false;
}

comfoair_command_err_t comfoair_command_from_entity(const char *key,
                                                    const comfoair_command_value_t *value,
                                                    const comfoair_command_durations_t *durations,
                                                    comfoair_command_t *out)
{
    if (key == nullptr || value == nullptr || durations == nullptr || out == nullptr)
        return COMFOAIR_COMMAND_ERR_ARG;

    const comfoair_command_t empty = {};
    *out                           = empty;

    const comfoair_entity_t *e = entity(key);
    if (e == nullptr || !e->writable || !comfoair_command_knows_entity(key))
        return COMFOAIR_COMMAND_ERR_UNKNOWN_ENTITY;

    // The four durations produce no frame at all: the unit is told a duration only as part of a
    // command. The RANGE is the registry's, asked here rather than copied -- a second copy would
    // refuse a value the API advertises, or accept one it does not.
    if (is_duration_entity(key)) {
        if (value->kind != COMFOAIR_COMMAND_VALUE_NUMBER)
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        if (value->number < e->min_value || value->number > e->max_value)
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        out->stores_duration  = true;
        out->duration_key     = e->key;
        out->duration_minutes = static_cast<uint16_t>(value->number);
        return COMFOAIR_COMMAND_OK;
    }

    if (strcmp(key, "away_mode") == 0) {
        if (value->kind != COMFOAIR_COMMAND_VALUE_BOOL)
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        build_away(out, value->boolean, minutes_to_seconds(durations->away_minutes));
        return COMFOAIR_COMMAND_OK;
    }

    if (strcmp(key, "fan") == 0) {
        // Off is the AWAY LEVEL, not a stop: an HRV never stops ventilating entirely. Speed is
        // 1..3 and 0 arrives as "off".
        if (value->kind == COMFOAIR_COMMAND_VALUE_BOOL) {
            if (!value->boolean)
                // Off is the AWAY LEVEL. An HRV never stops ventilating entirely.
                return build_level(out, 0, durations->permanent_manual);
            // On resumes what the unit was last seen running at, falling back to Low only when
            // nothing has been reported; always sending Low would turn "on" into a downgrade for
            // a house that was on High.
            const uint8_t resume =
                (durations->fan_resume_level >= 1 && durations->fan_resume_level <= 3)
                    ? durations->fan_resume_level
                    : 1;
            return build_level(out, resume, durations->permanent_manual);
        }
        // The fan's own metadata lists six preset labels, so a write carrying one of them has to
        // work: an entity that advertises an option and then refuses it is a 422 the API asked
        // for. They mean exactly what they mean on ventilation_mode and go through one builder.
        if (value->kind == COMFOAIR_COMMAND_VALUE_TEXT)
            return build_preset(out, value->text, durations);
        if (value->kind != COMFOAIR_COMMAND_VALUE_NUMBER)
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        if (value->number < 0 || value->number > 3)
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        return build_level(out, static_cast<int32_t>(value->number), durations->permanent_manual);
    }

    // Everything left is a select, written by its option LABEL. An index arriving over the wire
    // would be a number whose meaning depends on the firmware's build.
    if (value->kind != COMFOAIR_COMMAND_VALUE_TEXT)
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    const int index = option_index(e, value->text);
    if (index < 0)
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;

    if (strcmp(key, "bypass_mode") == 0) {
        // Bounded by the LITERAL table's own size, not by the registry's option count. The index
        // comes from the registry, and a fourth option added to pdo_table.py would otherwise read
        // past this array -- in the one file whose premise is that the table is the source of
        // truth. The registry sweep cannot catch it: it asserts only that a builder exists.
        static const char *const modes[] = {"auto", "open", "closed"};
        if ((size_t)index >= sizeof modes / sizeof modes[0])
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        return build_bypass(out, modes[index], minutes_to_seconds(durations->bypass_minutes));
    }
    if (strcmp(key, "temperature_profile") == 0)
        return build_profile(out, index);
    if (strcmp(key, "balance_mode") == 0) {
        // The index chooses a PAIR OF FRAMES, not a wire byte. This is one of the two selects for
        // which "the index is the byte" is false, and conflating them is the phase 3 failure.
        static const char *const modes[] = {"balanced", "supply_only", "exhaust_only"};
        if ((size_t)index >= sizeof modes / sizeof modes[0])
            return COMFOAIR_COMMAND_ERR_BAD_VALUE;
        return build_balance(out, modes[index], minutes_to_seconds(durations->balance_minutes));
    }
    if (strcmp(key, "temperature_passive") == 0)
        return build_off_auto_on(out, comfoair::rmi::PROP_TEMPERATURE_PASSIVE, index);
    if (strcmp(key, "humidity_comfort") == 0)
        return build_off_auto_on(out, comfoair::rmi::PROP_HUMIDITY_COMFORT, index);
    if (strcmp(key, "humidity_protection") == 0)
        return build_off_auto_on(out, comfoair::rmi::PROP_HUMIDITY_PROTECTION, index);
    if (strcmp(key, "ventilation_mode") == 0)
        return build_preset(out, value->text, durations);

    return COMFOAIR_COMMAND_ERR_UNKNOWN_ENTITY;
}

comfoair_command_err_t comfoair_command_from_op(const char *name,
                                                const comfoair_command_params_t *params,
                                                const comfoair_command_durations_t *durations,
                                                comfoair_command_t *out)
{
    if (name == nullptr || params == nullptr || durations == nullptr || out == nullptr)
        return COMFOAIR_COMMAND_ERR_ARG;

    const comfoair_command_t empty = {};
    *out                           = empty;

    // Every parameter is required: docs/interfaces/rest-api.md lists them and none has a
    // default, and for three of these operations an invented duration would mean "forever".
    if (strcmp(name, "boost") == 0) {
        if (!params->has_duration)
            return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
        build_boost(out, params->duration_seconds);
        return COMFOAIR_COMMAND_OK;
    }
    if (strcmp(name, "set_bypass") == 0) {
        if (params->mode == nullptr || !params->has_duration)
            return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
        return build_bypass(out, params->mode, params->duration_seconds);
    }
    if (strcmp(name, "set_balance_mode") == 0) {
        if (params->mode == nullptr || !params->has_duration)
            return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
        return build_balance(out, params->mode, params->duration_seconds);
    }
    if (strcmp(name, "set_ventilation_level") == 0) {
        if (!params->has_level)
            return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
        return build_level(out, params->level, durations->permanent_manual);
    }
    if (strcmp(name, "set_temperature_profile") == 0) {
        if (params->profile == nullptr)
            return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
        // Names, not indices, on this surface -- and "auto" is deliberately absent. By analogy
        // with every other subunit the release should be 85 15 03 01, but it has never been
        // observed being sent: it is a hypothesis, not a documented command (comfoair-pdo-rmi.md
        // §3, "There is no profile back to auto"). Verify on hardware before adding it.
        static const char *const profiles[] = {"normal", "cool", "warm"};
        for (int i = 0; i < 3; i++)
            if (strcmp(params->profile, profiles[i]) == 0)
                return build_profile(out, i);
        return COMFOAIR_COMMAND_ERR_BAD_VALUE;
    }
    if (strcmp(name, "set_away") == 0) {
        if (!params->has_enabled || !params->has_duration)
            return COMFOAIR_COMMAND_ERR_MISSING_PARAM;
        build_away(out, params->enabled, params->duration_seconds);
        return COMFOAIR_COMMAND_OK;
    }

    return COMFOAIR_COMMAND_ERR_UNKNOWN_OP;
}

const char *comfoair_command_strerror(comfoair_command_err_t err)
{
    switch (err) {
    case COMFOAIR_COMMAND_OK:                  return "Accepted.";
    case COMFOAIR_COMMAND_ERR_UNKNOWN_ENTITY:  return "No such writable entity.";
    case COMFOAIR_COMMAND_ERR_UNKNOWN_OP:      return "No such operation.";
    case COMFOAIR_COMMAND_ERR_BAD_VALUE:       return "That value is not one this entity accepts.";
    case COMFOAIR_COMMAND_ERR_MISSING_PARAM:
        return "This operation has no optional parameters; one is missing.";
    case COMFOAIR_COMMAND_ERR_ARG:             return "The request could not be read.";
    }
    return "The command could not be built.";
}

}  // extern "C"
