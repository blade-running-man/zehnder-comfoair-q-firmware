// See comfoair_mqtt_inbound.h for what this file is and why it is its own component.
//
// EVERY BRANCH BELOW IS A DECISION AND NOTHING ELSE. It was lifted out of what is now
// comfoair_mqtt_handle_inbound(), which decided and acted in one pass -- submitting, writing flash,
// writing the state model, waking the browser and moving counters as it went. None of that could be
// reached from a host test, so the question "does this payload still mean what it meant" had no
// answer short of flashing a board. It has one now, in test/test_mqtt_inbound.
#include "comfoair_mqtt_inbound.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "comfoair_wire.h"

static comfoair_mqtt_decision_t blank(comfoair_mqtt_decision_kind_t kind)
{
    comfoair_mqtt_decision_t out;
    memset(&out, 0, sizeof out);
    out.kind = kind;
    return out;
}

// A LITERAL, never the payload. A command payload carries whatever anybody with the broker's
// address published, and the caller logs this reason into a ring that GET /api/log serves.
//
// Built from a ZEROED struct rather than by amending one in progress, so a refusal can never hand
// back a half-filled key or label that a caller might read.
static comfoair_mqtt_decision_t refuse(const char *why)
{
    comfoair_mqtt_decision_t out = blank(COMFOAIR_MQTT_REJECT);
    out.reason                   = why;
    return out;
}

// The key travels as a COPY: it is read out of a local buffer that does not outlive this call. It
// cannot truncate -- comfoair_ha_parse_topic was handed this same buffer size as its cap and
// refuses a longer key outright rather than shortening one.
static void set_key(comfoair_mqtt_decision_t *out, const char *key)
{
    strncpy(out->key, key, sizeof out->key - 1);
    out->key[sizeof out->key - 1] = '\0';
}

// A scalar that has to BE a number, and the guard is wider than it looks because strtod accepts a
// great deal that is not one. Every spelling below reads as one thing and means another:
//
//   "hello"    -> 0, and 0 on the fan is the AWAY level, so a typo empties the house of ventilation
//   "2x"       -> 2, so a typo is applied as if it had been typed correctly
//   "nan"      -> NaN, which passes EVERY downstream range check, because all comparisons against
//                 NaN are false. The cast that follows is undefined: on the host (int32_t)NaN is 0
//                 -- the Away level again -- and on riscv32 (uint16_t)NaN is 65535, which stores a
//                 65535-minute duration the registry's own range (1..1440) exists to forbid and
//                 then publishes it to Home Assistant as truth. Anybody who can reach the broker
//                 can send it.
//   "inf"      -> the same, by the same route
//   "1e400"    -> overflows to infinity out of characters that are ALL decimal, which is why
//                 isfinite() is here and not merely implied by the character sweep
//   "0x10"     -> 16, a number that does not look like the one it is
//   " 2"       -> 2, because strtod skips leading whitespace
//
// So the string is required to be spelled out of decimal characters BEFORE strtod sees it, and the
// result is required to be finite after. DO NOT drop either half: the sweep alone misses "1e400",
// and isfinite alone misses "0x10". Exponents ("1e2") still pass, which is what keeps this a
// number parser rather than a digit parser.
bool comfoair_mqtt_parse_number(const char *text, double *out)
{
    if (text == NULL || out == NULL || text[0] == '\0')
        return false;
    for (const char *c = text; *c != '\0'; c++)
        if (strchr("0123456789+-.eE", *c) == NULL)
            return false;
    char        *end = NULL;
    const double v   = strtod(text, &end);
    if (end == text || *end != '\0' || !isfinite(v))
        return false;
    *out = v;
    return true;
}

comfoair_mqtt_decision_t comfoair_mqtt_decide(const comfoair_ha_ctx_t *ctx,
                                              const comfoair_mqtt_inbound_t *in,
                                              const comfoair_command_durations_t *durations,
                                              const comfoair_fan_state_t *fan)
{
    if (ctx == NULL || in == NULL || durations == NULL)
        return blank(COMFOAIR_MQTT_IGNORE);

    comfoair_mqtt_decision_t out = blank(COMFOAIR_MQTT_IGNORE);

    // Zeroed, because comfoair_ha_parse_topic leaves `name` UNTOUCHED for the three fan targets:
    // they name themselves, and reading an uninitialised buffer here would be reading whatever the
    // stack held.
    char                       name[COMFOAIR_MQTT_KEY_MAX] = {0};
    const comfoair_ha_target_t target =
        comfoair_ha_parse_topic(ctx, in->topic, in->topic_len, name, sizeof name);

    // FIRST, and BEFORE the retained check below -- that order is a fix, not a formatting choice.
    // See COMFOAIR_MQTT_IGNORE in the header for why a foreign topic is not a refusal. With the
    // two the other way round, a retained message on the neighbour's tree -- which is most of what
    // a shared broker holds -- was refused and moved this device's rejection counter.
    if (target == COMFOAIR_HA_TARGET_NONE)
        return out;

    // A RETAINED COMMAND IS NEVER APPLIED. Left alone it is re-delivered on every reconnect and
    // every boot for as long as the broker keeps it: one `mosquitto_pub -r` during testing and the
    // bypass reopens for ever. NOTE what this does and does not cover -- MQTT 3.1.1 §3.3.1.3 makes
    // a broker deliver to an ALREADY-ESTABLISHED subscription with the retain flag CLEARED, so a
    // retained publish that arrives while the device is connected is applied once. What is fixed
    // is the replay, which is the half that never stops.
    if (in->retained)
        return refuse("retained commands are not applied on delivery to a new subscription");

    // The one rule in this file that reads the unit's state, and it lives HERE rather than in the
    // caller because it is a decision: turning the fan on resumes the level it is actually running
    // at instead of dropping the house to Low.
    // `known && on` is both halves of the guard -- an unreported fan has nothing to say, and a fan
    // that is OFF still reports speed 1, because a fan cannot show speed 0. Taking that 1 would be
    // the downgrade this rule exists to prevent.
    comfoair_command_durations_t effective = *durations;
    if (fan != NULL && fan->known && fan->on)
        effective.fan_resume_level = fan->speed;

    // READ FROM in->payload, and copy nothing yet. `in` is the caller's and is valid for the whole
    // of this call; only the option label has to outlive it, and that copy happens once, at the
    // end, where the label is already known to be one the registry lists. A blanket 512-byte copy
    // stood here and cost every caller of this function 512 bytes of stack on a 4 KB task.
    const char *payload = in->payload;

    if (target == COMFOAIR_HA_TARGET_OP) {
        comfoair_command_params_t  params  = {0};
        // `storage` backs the two string parameters params points at, so it must outlive the
        // build below and not one line longer -- comfoair_command_t carries no pointer into it.
        comfoair_wire_op_storage_t storage = {0};
        // No copy: comfoair_wire_parse_operation takes a `const char *`, reads it during the call
        // and keeps nothing, and `in` outlives the call by construction.
        if (comfoair_wire_parse_operation(payload, &params, &storage).status != COMFOAIR_WIRE_OK)
            return refuse("the operation body could not be read");
        if (comfoair_command_from_op(name, &params, &effective, &out.command) !=
            COMFOAIR_COMMAND_OK)
            return refuse("the operation was refused");
        // No optimistic write: an operation is not an entity and has nothing in the state model to
        // show early.
        out.kind = COMFOAIR_MQTT_SUBMIT;
        return out;
    }

    comfoair_command_value_t value  = {0};
    double                   number = 0;

    switch (target) {
    case COMFOAIR_HA_TARGET_FAN_ON:
        set_key(&out, "fan");
        if (strcmp(payload, "ON") != 0 && strcmp(payload, "OFF") != 0)
            return refuse("a fan takes ON or OFF");
        value.kind    = COMFOAIR_COMMAND_VALUE_BOOL;
        value.boolean = strcmp(payload, "ON") == 0;
        break;
    case COMFOAIR_HA_TARGET_FAN_SPEED:
        set_key(&out, "fan");
        if (!comfoair_mqtt_parse_number(payload, &number))
            return refuse("a fan speed must be a number");
        value.kind   = COMFOAIR_COMMAND_VALUE_NUMBER;
        value.number = number;
        break;
    case COMFOAIR_HA_TARGET_FAN_PRESET:
        // The fan's preset topic writes the ventilation_mode select: Home Assistant reaches a
        // fan's preset on its own topic, while the six labels belong to an entity of that name.
        set_key(&out, "ventilation_mode");
        value.kind = COMFOAIR_COMMAND_VALUE_TEXT;
        value.text = payload;
        break;
    default: {
        // Only here, because comfoair_ha_parse_topic leaves `name` untouched for the three targets
        // above and each of them names itself.
        set_key(&out, name);
        // Which shape the payload is depends on the entity, and the REGISTRY says which: a select
        // takes its option label, a switch ON/OFF, a number a number.
        const comfoair_entity_t *e = comfoair_entity_by_key(out.key);
        if (e == NULL)
            return refuse("no such entity");
        if (e->kind == COMFOAIR_KIND_SWITCH) {
            if (strcmp(payload, "ON") != 0 && strcmp(payload, "OFF") != 0)
                return refuse("a switch takes ON or OFF");
            value.kind    = COMFOAIR_COMMAND_VALUE_BOOL;
            value.boolean = strcmp(payload, "ON") == 0;
        } else if (e->kind == COMFOAIR_KIND_NUMBER) {
            if (!comfoair_mqtt_parse_number(payload, &number))
                return refuse("that entity takes a number");
            value.kind   = COMFOAIR_COMMAND_VALUE_NUMBER;
            value.number = number;
        } else {
            value.kind = COMFOAIR_COMMAND_VALUE_TEXT;
            value.text = payload;
        }
        break;
    }
    }

    // A read-only entity ends here too: comfoair_command_from_entity answers UNKNOWN_ENTITY for
    // anything the registry does not mark writable, so there is no second writability table in
    // this file -- and there must not be one.
    if (comfoair_command_from_entity(out.key, &value, &effective, &out.command) !=
        COMFOAIR_COMMAND_OK)
        return refuse("the value was refused");

    if (out.command.stores_duration) {
        out.kind = COMFOAIR_MQTT_STORE;
        return out;
    }

    if (value.kind == COMFOAIR_COMMAND_VALUE_TEXT) {
        // THE ONLY COPY THIS FUNCTION MAKES, and it is bounded by what got here rather than by
        // what arrived: comfoair_command_from_entity has just matched this payload against the
        // registry's option list for this entity, so it IS one of those labels, and the longest
        // in the generated table is 18 bytes. test_mqtt_inbound sweeps the table and fails if a
        // regenerated registry ever outgrows COMFOAIR_MQTT_LABEL_MAX, so the refusal below is
        // unreachable today -- it is there because truncating a label silently would turn a valid
        // command into one the state model then declines, with nothing saying why.
        const size_t n = strlen(payload);
        if (n >= sizeof out.text)
            return refuse("that option label is longer than this device can carry");
        memcpy(out.text, payload, n + 1);
        // Optimistic for EVERY label, as over REST: three selects only read back at the next RMI
        // settings read (up to 6 h), so without the echo HA shows the pre-write value until then;
        // the other four are overwritten by their own PDOs within seconds. Applied AFTER submit.
        out.optimistic_text = true;
    }

    out.kind = COMFOAIR_MQTT_SUBMIT;
    return out;
}
