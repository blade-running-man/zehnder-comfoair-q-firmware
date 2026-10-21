#include "comfoair_state.h"

#include <cmath>
#include <cstring>

#include "comfoair_lock.h"
#include "comfoair_state_internal.h"
#include "pdo_decode.h"

// The core of the state model: the slots, cold-start seeding and the public readers and
// setters. The change marks live in comfoair_state_marks.cpp, the per-kind payload decode in
// comfoair_state_decode.cpp and the entities derived from pairs of PDOs in
// comfoair_state_derived.cpp; this is the only file that walks COMFOAIR_ENTITIES
// (comfoair_state_internal.h says why that must stay so).

using comfoair_state_detail::apply_one;
using comfoair_state_detail::mark_changed;
using comfoair_state_detail::same_value;
using comfoair_state_detail::set_text;

namespace {

// One slot per generated entity, indexed the same way, so a lookup is an index and no
// allocation happens anywhere in this file.
comfoair_value_t g_values[COMFOAIR_ENTITY_COUNT];

// The active-errors text, double-buffered so that a pointer comfoair_state_get() handed out stays a
// whole string across the next write: each write goes to the OTHER buffer. One write, not two -- a
// reader that renders after unlocking (the MQTT publisher) copies the text under the lock with
// comfoair_state_get_copy() instead, because a second flip lands back on the half it would be
// reading. DO NOT store the caller's pointer instead: it is a stack buffer.
char     g_error_text[2][COMFOAIR_STATE_TEXT_MAX + 1];
unsigned g_error_text_current;

}  // namespace

namespace comfoair_state_detail {

void set_text(const char *key, const char *label)
{
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) != 0)
            continue;
        if (g_values[i].type != COMFOAIR_VALUE_TEXT || g_values[i].text != label)
            mark_changed(i);
        g_values[i].type = COMFOAIR_VALUE_TEXT;
        g_values[i].text = label;
        return;
    }
}

// The fan is not in g_values -- it has its own struct -- so it needs its own mark.
void mark_fan_changed()
{
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (COMFOAIR_ENTITIES[i].kind == COMFOAIR_KIND_FAN)
            mark_changed(i);
}

// The label must come from the generated options rather than from a literal here, so a
// rename in pdo_table.py cannot leave this file publishing a string the select will reject.
const char *option_of(const char *key, uint8_t index)
{
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) != 0)
            continue;
        if (index >= COMFOAIR_ENTITIES[i].option_count)
            return nullptr;
        return COMFOAIR_ENTITIES[i].options[index];
    }
    return nullptr;
}

}  // namespace comfoair_state_detail

namespace {

// The values this node knows before anything has been heard or restored. Three kinds, and only
// these: the schema version (the entity a client checks the contract against -- null there makes
// the check impossible on exactly the device that needs it), the freshness entity, and the four
// durations, the only rows with a registry default_value. src/main.cpp overwrites the durations
// from NVS right after this runs at boot.
//
// NO SELECT IS SEEDED. temperature_passive, humidity_comfort and humidity_protection are read back
// from the unit over RMI by comfoair_probe_task (COMFOAIR_RMI_READBACK) and stay
// COMFOAIR_VALUE_NONE until that read answers. They once took a Zehnder factory default here, and
// the owner's unit held different values, so the device published settings it had never read.
// DO NOT give a select a default in pdo_table.py to "fill the gap": there is no select branch
// below any more, so the default would land as a NUMBER on a select and publish as a reading.
void seed_defaults()
{
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t &e = COMFOAIR_ENTITIES[i];

        if (e.kind == COMFOAIR_KIND_DIAGNOSTIC_CONST) {
            g_values[i].type   = COMFOAIR_VALUE_NUMBER;
            g_values[i].number = static_cast<float>(COMFOAIR_SCHEMA_VERSION);
            continue;
        }
        // The data-freshness entity: TRUE FOR THE LIFE OF THE DEVICE, and that is deliberate,
        // not a placeholder. What it carries is not a value at all -- it is the
        // ARRIVAL of the message that means something, which is what Home Assistant's
        // expire_after reads and the only reason the entity is a connectivity binary_sensor
        // (comfoair_ha_payload.c). "false" is therefore never published by anyone: an unreachable
        // device cannot say it is unreachable, and the state HA shows in its place is
        // `unavailable`, fifteen minutes after the last publication.
        //
        // Seeded HERE and not left null so that the entity is not permanently blank on the two
        // surfaces expire_after does not reach: GET /api/state and the State page both render
        // whatever this model holds, and a row reading "--" for ever looks like a defect.
        if (e.kind == COMFOAIR_KIND_FRESHNESS) {
            g_values[i].type    = COMFOAIR_VALUE_BOOL;
            g_values[i].boolean = true;
            continue;
        }
        // NaN means the table gives no default; std::isnan, not a comparison, because a
        // NaN compares unequal to everything including itself.
        if (std::isnan(e.default_value))
            continue;
        g_values[i].type   = COMFOAIR_VALUE_NUMBER;
        g_values[i].number = e.default_value;
    }
}

}  // namespace

extern "C" {

void comfoair_state_reset(void)
{
    comfoair_lock();
    std::memset(g_values, 0, sizeof g_values);
    // The marks only, NOT the generations (both live in comfoair_state_marks.cpp; marks_reset()
    // leaves g_generation alone). A generation only ever rises, so one a consumer sampled before a
    // reset can never match one taken after it, and the conditional clear stays refused -- which
    // is the safe answer. Zeroing it would let the counter climb back to a value some publisher is
    // still holding and clear a mark that publisher never sent.
    comfoair_state_detail::marks_reset();
    // g_error_text is NOT cleared either: the slots above are NONE now, and a reader that copied a
    // text pointer under the lock just before this may still be rendering it.
    comfoair_state_detail::derived_reset();
    seed_defaults();
    comfoair_unlock();
}

bool comfoair_state_apply_pdo(uint16_t pdo, const uint8_t *data, size_t len)
{
    if (data == nullptr)
        return false;

    comfoair_lock();
    bool claimed = false;

    // Four PDOs drive entities the registry cannot describe, because those entities are
    // derived from PAIRS of PDOs. They are handled first and in addition to the registry
    // walk below -- PDO 65 feeds both the Fan Level sensor and the fan entity, so an
    // if/else would silently drop one of them.
    int64_t raw = 0;
    if (comfoair::decode_numeric(data, len, false, false, raw) == comfoair::DecodeResult::OK)
        claimed |= comfoair_state_detail::apply_derived(pdo, raw);

    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t &e = COMFOAIR_ENTITIES[i];
        if (e.pdo < 0 || static_cast<uint16_t>(e.pdo) != pdo)
            continue;
        claimed = true;
        const comfoair_value_t before = g_values[i];
        apply_one(e, g_values[i], data, len);
        if (!same_value(before, g_values[i]))
            mark_changed(i);
    }
    comfoair_unlock();
    return claimed;
}

// The registry row for a key, or nullptr. Shared by the two optimistic setters below so they
// cannot disagree about what "writable" means.
static const comfoair_entity_t *writable_entity(const char *key)
{
    if (key == nullptr)
        return nullptr;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
            return COMFOAIR_ENTITIES[i].writable ? &COMFOAIR_ENTITIES[i] : nullptr;
    return nullptr;
}

bool comfoair_state_set_option(const char *key, const char *option_label)
{
    const comfoair_entity_t *e = writable_entity(key);
    if (e == nullptr || e->options == nullptr || option_label == nullptr)
        return false;

    comfoair_lock();
    for (uint8_t o = 0; o < e->option_count; o++) {
        if (std::strcmp(e->options[o], option_label) != 0)
            continue;
        // The REGISTRY's pointer, never the caller's. That is the contract of comfoair_value_t.text
        // and the reason this function takes a label rather than an index: an index arriving from
        // the wire would be a number whose meaning depends on the build.
        set_text(e->key, e->options[o]);
        comfoair_unlock();
        return true;
    }
    comfoair_unlock();
    return false;
}

bool comfoair_state_set_number(const char *key, float value)
{
    const comfoair_entity_t *e = writable_entity(key);
    // Only where no PDO carries the entity. A number written over something the unit reports would
    // be a reading it never sent, and the next frame would silently overwrite it anyway.
    if (e == nullptr || e->pdo >= 0)
        return false;

    comfoair_lock();
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        if (&COMFOAIR_ENTITIES[i] != e)
            continue;
        if (g_values[i].type != COMFOAIR_VALUE_NUMBER || g_values[i].number != value)
            mark_changed(i);
        g_values[i].type   = COMFOAIR_VALUE_NUMBER;
        g_values[i].number = value;
        break;
    }
    comfoair_unlock();
    return true;
}

bool comfoair_state_set_unit_errors(bool any, const char *summary)
{
    if (summary == nullptr || std::strlen(summary) > COMFOAIR_STATE_TEXT_MAX)
        return false;
    comfoair_lock();
    bool changed = false;

    comfoair_value_t &flag = g_values[COMFOAIR_UNIT_ERROR_ANY];
    if (flag.type != COMFOAIR_VALUE_BOOL || flag.boolean != any) {
        flag.type    = COMFOAIR_VALUE_BOOL;
        flag.boolean = any;
        mark_changed(COMFOAIR_UNIT_ERROR_ANY);
        changed = true;
    }

    // By TEXT, not by pointer: the caller's buffer is not the registry, and the stored copy is ours.
    // Without this every once-a-minute read would mark the row and republish an unchanged text.
    comfoair_value_t &text = g_values[COMFOAIR_UNIT_ERROR_TEXT];
    if (text.type != COMFOAIR_VALUE_TEXT || std::strcmp(text.text, summary) != 0) {
        g_error_text_current ^= 1u;
        char *slot = g_error_text[g_error_text_current];
        std::memcpy(slot, summary, std::strlen(summary) + 1);
        text.type = COMFOAIR_VALUE_TEXT;
        text.text = slot;
        mark_changed(COMFOAIR_UNIT_ERROR_TEXT);
        changed = true;
    }

    comfoair_unlock();
    return changed;
}

bool comfoair_state_get(const char *key, comfoair_value_t *out)
{
    if (key == nullptr || out == nullptr)
        return false;
    comfoair_lock();
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) == 0) {
            *out = g_values[i];
            comfoair_unlock();
            return true;
        }
    }
    comfoair_unlock();
    return false;
}

bool comfoair_state_get_copy(const char *key, comfoair_value_t *out, char *text, size_t cap)
{
    if (key == nullptr || out == nullptr || text == nullptr || cap < COMFOAIR_STATE_TEXT_MAX + 1)
        return false;
    comfoair_lock();
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) != 0)
            continue;
        *out = g_values[i];
        // Bounded although nothing stored is longer (both writers cap at COMFOAIR_STATE_TEXT_MAX,
        // test_state_copy pins the labels): a cut text is wrong, an overrun is worse.
        if (out->type == COMFOAIR_VALUE_TEXT && out->text != nullptr) {
            std::strncpy(text, out->text, cap - 1);
            text[cap - 1] = '\0';
            out->text     = text;
        }
        comfoair_unlock();
        return true;
    }
    comfoair_unlock();
    return false;
}

const comfoair_entity_t *comfoair_entity_by_key(const char *key)
{
    if (key == nullptr)
        return nullptr;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (std::strcmp(COMFOAIR_ENTITIES[i].key, key) == 0)
            return &COMFOAIR_ENTITIES[i];
    return nullptr;
}

}  // extern "C"
