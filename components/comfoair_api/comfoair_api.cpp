#include "comfoair_api.h"

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>

#include "comfoair_json.h"
#include "comfoair_lock.h"
#include "comfoair_state.h"
#include "registry_generated.h"

namespace {

// A bounded append. Every writer below goes through it, so truncation is handled in one
// place rather than at thirty call sites -- and `used` never exceeds `cap`.
struct Writer {
    char  *buf;
    size_t cap;
    size_t used;
    // Set the moment anything did not fit. A caller that gets a plausible short length back
    // serves valid-looking JSON that stops mid-object with 200 OK, and nothing in the log
    // says so -- the entity list was twenty-three bytes from exactly that.
    bool   overflowed;

    void put(const char *s)
    {
        if (buf == nullptr || cap == 0) {
            overflowed = true;
            return;
        }
        while (*s) {
            if (used + 1 >= cap) {
                overflowed = true;
                break;
            }
            buf[used++] = *s++;
        }
        buf[used] = '\0';
    }

    void putf(const char *fmt, double value)
    {
        char tmp[32];
        // %g drops trailing zeros, so 21.5 stays 21.5 and 3 does not become 3.000000.
        snprintf(tmp, sizeof tmp, fmt, value);
        put(tmp);
    }

    void puti(long value)
    {
        char tmp[24];
        snprintf(tmp, sizeof tmp, "%ld", value);
        put(tmp);
    }

    void put_escaped(const char *s)
    {
        char tmp[256];
        comfoair_api_escape_json(s, tmp, sizeof tmp);
        put(tmp);
    }

    void put_key(const char *key)
    {
        put("\"");
        put_escaped(key);
        put("\":");
    }

    // A number, or null when the table gives none. NaN is the "absent" marker and must be
    // tested with isnan -- a NaN compares unequal to everything, itself included.
    void put_number_or_null(float value)
    {
        if (std::isnan(value)) {
            put("null");
            return;
        }
        putf("%g", value);
    }

    // A string field, or a literal null when the value is absent. Absent must be null and
    // not an empty string: "" is a value, and metadata the table does not carry is not.
    void put_string_or_null(const char *s)
    {
        if (s == nullptr) {
            put("null");
            return;
        }
        put("\"");
        put_escaped(s);
        put("\"");
    }
};

// The names match the `kind` strings the generator writes into entities.ts, so the two
// sides of the API agree on the vocabulary.
const char *kind_name(comfoair_entity_kind_t kind)
{
    switch (kind) {
    case COMFOAIR_KIND_NUMERIC:          return "numeric";
    case COMFOAIR_KIND_BINARY:           return "binary";
    case COMFOAIR_KIND_ENUM:             return "enum";
    case COMFOAIR_KIND_SPECIAL:          return "special";
    case COMFOAIR_KIND_SELECT:           return "select";
    case COMFOAIR_KIND_NUMBER:           return "number";
    case COMFOAIR_KIND_SWITCH:           return "switch";
    case COMFOAIR_KIND_FAN:              return "fan";
    case COMFOAIR_KIND_DIAGNOSTIC_CONST: return "diagnostic_const";
    case COMFOAIR_KIND_FRESHNESS:        return "freshness";
    }
    return nullptr;
}

void render_fan(Writer &w)
{
    comfoair_fan_state_t fan{};
    comfoair_state_get_fan(&fan);
    if (!fan.known) {
        w.put("null");
        return;
    }
    w.put("{\"on\":");
    w.put(fan.on ? "true" : "false");
    w.put(",\"speed\":");
    w.puti(fan.speed);
    w.put(",\"preset\":");
    w.put_string_or_null(fan.preset);
    w.put("}");
}

}  // namespace

extern "C" {

size_t comfoair_api_escape_json(const char *in, char *out, size_t cap)
{
    // Forwarded, not reimplemented. The rules for \u, for surrogate pairs and for a byte that is
    // not part of a well-formed UTF-8 sequence now live in components/comfoair_json, next to the
    // READER that has to undo them -- a device name rendered here comes back through
    // POST /api/config, and two implementations of the same encoding are two chances for that
    // round trip to change the name every time the page is saved. The body moved verbatim; this
    // name stays because it is what the rest of the firmware calls (AGENTS.md: one list, ever --
    // the same rule applied to an encoding).
    return comfoair_json_escape(in, out, cap);
}

// The body of both the full document and the delta. `changed_only` is the difference; the
// two must share this or they will drift, and a client would see one shape on connect and
// another afterwards.
//
// `consume` says whether this rendering is going to the push clients, and only then are the PUSH
// marks cleared. The marks record what the WebSocket clients have not yet been sent; a rendering
// that reaches somebody else has delivered nothing to them. DO NOT clear on every full render:
// GET /api/state renders the same document for a REST reader, and a poll landing in the gap
// before the next push round would take the change from every open socket, which would then not
// see it until the value moved again.
static size_t render_object(char *out, size_t cap, bool changed_only, bool consume)
{
    // For the whole document, not per entity. comfoair_state_get() locks on its own, but
    // one take per entity would let a CAN frame land between two of them and
    // produce one response mixing values from before and after -- a snapshot of a moment
    // that never existed. The lock is recursive precisely so this can nest.
    comfoair_lock();
    Writer w{out, cap, 0, false};
    w.put("{");
    bool first = true;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t &e = COMFOAIR_ENTITIES[i];
        if (changed_only && !comfoair_state_changed(COMFOAIR_STATE_CONSUMER_PUSH, i))
            continue;
        if (!first)
            w.put(",");
        first = false;
        w.put_key(e.key);

        if (e.kind == COMFOAIR_KIND_FAN) {
            render_fan(w);
            continue;
        }

        comfoair_value_t v{};
        comfoair_state_get(e.key, &v);
        switch (v.type) {
        case COMFOAIR_VALUE_NUMBER: w.putf("%g", v.number); break;
        case COMFOAIR_VALUE_BOOL:   w.put(v.boolean ? "true" : "false"); break;
        case COMFOAIR_VALUE_TEXT:   w.put_string_or_null(v.text); break;
        case COMFOAIR_VALUE_NONE:
        default:
            // null, never omitted: a missing key says "no such entity", a null says "no
            // reading yet", and a client must be able to tell a dead bus from a typo.
            w.put("null");
            break;
        }
    }
    w.put("}");
    // A push of either kind clears: a client that has just been sent the whole state must not
    // immediately receive a delta repeating part of it. The PUSH consumer's marks, and only
    // those -- MQTT keeps its own set, and clearing a shared one would let whichever transport
    // rendered first take the other's updates.
    //
    // Inside the lock, and DO NOT move it below the unlock: the CAN task would apply a PDO in the
    // gap, and its mark would be cleared without the value ever having been rendered. The lock is
    // recursive, so comfoair_state_clear_changes() taking it again nests.
    if (consume)
        comfoair_state_clear_changes(COMFOAIR_STATE_CONSUMER_PUSH);
    comfoair_unlock();
    return w.overflowed ? 0 : w.used;
}

size_t comfoair_api_render_value(const char *key, char *out, size_t cap)
{
    if (key == nullptr)
        return 0;

    comfoair_lock();
    const comfoair_entity_t *e = comfoair_entity_by_key(key);
    if (e == nullptr) {
        comfoair_unlock();
        return 0;
    }

    Writer w{out, cap, 0, false};
    w.put("{\"key\":");
    w.put_string_or_null(e->key);
    w.put(",\"name\":");
    w.put_string_or_null(e->name);
    w.put(",\"unit\":");
    w.put_string_or_null(e->unit);
    w.put(",\"value\":");

    if (e->kind == COMFOAIR_KIND_FAN) {
        render_fan(w);
    } else {
        comfoair_value_t v{};
        comfoair_state_get(key, &v);
        switch (v.type) {
        case COMFOAIR_VALUE_NUMBER: w.putf("%g", v.number); break;
        case COMFOAIR_VALUE_BOOL:   w.put(v.boolean ? "true" : "false"); break;
        case COMFOAIR_VALUE_TEXT:   w.put_string_or_null(v.text); break;
        default:                    w.put("null"); break;
        }
    }
    w.put("}");
    comfoair_unlock();
    return w.overflowed ? 0 : w.used;
}

size_t comfoair_api_render_state(char *out, size_t cap) { return render_object(out, cap, false, false); }

size_t comfoair_api_render_delta(char *out, size_t cap) { return render_object(out, cap, true, true); }

size_t comfoair_api_render_push(char *out, size_t cap, bool full)
{
    static const char prefix_state[] = "{\"type\":\"state\",\"values\":";
    static const char prefix_delta[] = "{\"type\":\"delta\",\"values\":";
    const char *prefix = full ? prefix_state : prefix_delta;
    const size_t plen  = std::strlen(prefix);

    // Leave room for the prefix and the closing brace before the body is rendered, rather
    // than discovering afterwards that it does not fit.
    if (out == nullptr || cap < plen + 4)
        return 0;

    std::memcpy(out, prefix, plen);
    const size_t body = render_object(out + plen, cap - plen - 2, !full, true);
    if (body == 0)
        return 0;
    // An empty delta is not worth a frame. A full state is always worth one: a client that
    // has just connected needs it whether or not anything moved.
    if (!full && body == 2)
        return 0;

    size_t used = plen + body;
    out[used++] = '}';
    out[used]   = '\0';
    return used;
}

size_t comfoair_api_render_entities(char *out, size_t cap)
{
    Writer w{out, cap, 0, false};
    w.put("[");
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t &e = COMFOAIR_ENTITIES[i];
        if (i > 0)
            w.put(",");
        w.put("{\"key\":");
        w.put_string_or_null(e.key);
        w.put(",\"name\":");
        w.put_string_or_null(e.name);
        // kind, pdo, min and max are declared by the generated web/src/api/entities.ts and
        // were missing here, so a client typed against those types read undefined for a
        // number's range and could not tell a select from an enum.
        w.put(",\"kind\":");
        w.put_string_or_null(kind_name(e.kind));
        w.put(",\"pdo\":");
        if (e.pdo < 0)
            w.put("null");   // -1 is the C sentinel; a client must not see a PDO numbered -1
        else
            w.puti(e.pdo);
        w.put(",\"min\":");
        w.put_number_or_null(e.min_value);
        w.put(",\"max\":");
        w.put_number_or_null(e.max_value);
        w.put(",\"unit\":");
        w.put_string_or_null(e.unit);
        w.put(",\"deviceClass\":");
        w.put_string_or_null(e.device_class);
        // comfoair_ha_discovery_payload() already publishes this as stat_cla over MQTT discovery,
        // from the same registry field (the `state_class` field of comfoair_entity_t in
        // registry_generated.h). It was missing here only because
        // this renderer was written before that field existed -- REST and MQTT must agree
        // per AGENTS.md criterion 3, and a client with no state_class gets no Home Assistant statistics.
        w.put(",\"stateClass\":");
        w.put_string_or_null(e.state_class);
        w.put(",\"icon\":");
        w.put_string_or_null(e.icon);
        w.put(",\"entityCategory\":");
        w.put_string_or_null(e.entity_category);
        w.put(",\"writable\":");
        w.put(e.writable ? "true" : "false");
        // Mirrors discovery's `en`. The HA integration builds its entities from this document,
        // not from discovery, so without it accessory rows would arrive enabled there.
        w.put(",\"enabledByDefault\":");
        w.put(e.enabled_by_default ? "true" : "false");
        w.put(",\"options\":");
        if (e.option_count == 0) {
            w.put("null");
        } else {
            w.put("[");
            for (uint8_t o = 0; o < e.option_count; o++) {
                if (o > 0)
                    w.put(",");
                w.put_string_or_null(e.options[o]);
            }
            w.put("]");
        }
        w.put("}");
    }
    w.put("]");
    return w.overflowed ? 0 : w.used;
}

}  // extern "C"
