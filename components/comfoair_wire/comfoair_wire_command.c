// comfoair_wire: the command parsers, POST /api/entities/<key> and POST /api/ops/<name>
// (the MQTT operation payload too).
// Read comfoair_wire.h first; the component's shape is in README.md, Implementation.
// The two rules in comfoair_wire.c govern this file: no secret is rendered, and no submitted
// value is quoted in an error.
#include "comfoair_wire.h"

#include "comfoair_json.h"
#include "comfoair_wire_internal.h"

// --- POST /api/entities/<key> ------------------------------------------------------------

comfoair_wire_result_t comfoair_wire_parse_entity_write(const char *body,
                                                        comfoair_command_value_t *out, char *text,
                                                        size_t cap)
{
    if (out == NULL || text == NULL || cap == 0)
        return bad_body();
    if (comfoair_json_check(body) != COMFOAIR_JSON_DOC_OK)
        return bad_body();

    const comfoair_command_value_t empty = {0};
    *out                                 = empty;

    // Asked as a string first, because that is the only shape with a length that can fail. The
    // reader answers WRONG_TYPE for a number or a boolean, which is how the kind is discovered
    // without a separate type query.
    const comfoair_json_read_t as_text = comfoair_json_string(body, "value", text, cap);
    if (as_text == COMFOAIR_JSON_FOUND) {
        out->kind = COMFOAIR_COMMAND_VALUE_TEXT;
        out->text = text;
        return ok();
    }
    if (as_text == COMFOAIR_JSON_TOO_LONG)
        // Longer than any option label this device has. Refused, never truncated.
        return bad_field("value");
    if (as_text == COMFOAIR_JSON_MISSING)
        return bad_field("value");
    if (as_text == COMFOAIR_JSON_MALFORMED)
        return bad_body();

    bool flag = false;
    if (comfoair_json_bool(body, "value", &flag) == COMFOAIR_JSON_FOUND) {
        out->kind    = COMFOAIR_COMMAND_VALUE_BOOL;
        out->boolean = flag;
        return ok();
    }

    uint32_t number = 0;
    if (comfoair_json_u32(body, "value", &number) == COMFOAIR_JSON_FOUND) {
        out->kind   = COMFOAIR_COMMAND_VALUE_NUMBER;
        out->number = (double)number;
        return ok();
    }

    // Present and none of the three: `null`, a fraction, a negative. Nothing writable on this
    // device takes any of them, and guessing which one was meant is how a select gets the wrong
    // option.
    return bad_field("value");
}

// --- POST /api/ops/<name> ----------------------------------------------------------------

// One signed parameter, through comfoair_json_i32.
//
// SIGNED IS LOAD-BEARING. A negative duration is a legal request meaning "until told otherwise",
// and it folds into the same branch as zero downstream. What must NOT happen is a fraction
// becoming one: {"duration": 18.5} is a typo, and reading it as -1 would mean "forever" on three
// of the four timed operations. comfoair_json_i32 keeps a minus sign and a fraction apart, which
// is the whole reason it exists rather than a flag on the unsigned reader.
static comfoair_wire_result_t read_signed(const char *body, const char *key, bool *has,
                                          int32_t *out)
{
    int32_t                    value = 0;
    const comfoair_json_read_t got   = comfoair_json_i32(body, key, &value);
    if (got == COMFOAIR_JSON_MISSING)
        return ok();
    if (got != COMFOAIR_JSON_FOUND)
        return bad_field(key);
    *has = true;
    *out = value;
    return ok();
}

static comfoair_wire_result_t read_op_string(const char *body, const char *key, char *store,
                                             size_t cap, const char **target)
{
    const comfoair_json_read_t got = comfoair_json_string(body, key, store, cap);
    if (got == COMFOAIR_JSON_MISSING) {
        *target = NULL;
        return ok();
    }
    if (got != COMFOAIR_JSON_FOUND)
        return bad_field(key);
    *target = store;
    return ok();
}

comfoair_wire_result_t comfoair_wire_parse_operation(const char *body,
                                                     comfoair_command_params_t *out,
                                                     comfoair_wire_op_storage_t *storage)
{
    if (out == NULL || storage == NULL)
        return bad_body();
    if (comfoair_json_check(body) != COMFOAIR_JSON_DOC_OK)
        return bad_body();

    // Built locally and copied out at the end, so a refused body leaves no half-decoded call
    // behind for a handler that checked the status too late.
    comfoair_command_params_t params = {0};
    comfoair_wire_result_t    fail;

    fail = read_op_string(body, "mode", storage->mode, sizeof storage->mode, &params.mode);
    if (fail.status != COMFOAIR_WIRE_OK)
        return fail;
    fail = read_op_string(body, "profile", storage->profile, sizeof storage->profile,
                          &params.profile);
    if (fail.status != COMFOAIR_WIRE_OK)
        return fail;

    fail = read_signed(body, "duration", &params.has_duration, &params.duration_seconds);
    if (fail.status != COMFOAIR_WIRE_OK)
        return fail;

    int32_t level = 0;
    fail          = read_signed(body, "level", &params.has_level, &level);
    if (fail.status != COMFOAIR_WIRE_OK)
        return fail;
    params.level = level;

    bool                       enabled = false;
    const comfoair_json_read_t flag    = comfoair_json_bool(body, "enabled", &enabled);
    if (flag == COMFOAIR_JSON_FOUND) {
        params.has_enabled = true;
        params.enabled     = enabled;
    } else if (flag != COMFOAIR_JSON_MISSING) {
        return bad_field("enabled");
    }

    *out = params;
    return ok();
}
