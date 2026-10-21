// WHAT A STATE TOPIC CARRIES: the bare scalar for one entity and the fan's three leaves. Cut out
// of comfoair_ha_payload.c (the discovery documents) for the 350-line ceiling; no behaviour moved.
#include "comfoair_ha.h"

#include "comfoair_ha_internal.h"

// --- state payloads ---------------------------------------------------------------------------

size_t comfoair_ha_state_payload(const comfoair_entity_t *entity, const comfoair_value_t *value,
                                 bool *skip, char *out, size_t cap)
{
    if (skip != NULL)
        *skip = true;
    if (entity == NULL || value == NULL)
        return 0;

    writer_t w = {out, cap, 0, false};
    switch (value->type) {
    case COMFOAIR_VALUE_NONE:
        // HA's "no value", never nothing: publishing nothing left the last retained reading on the
        // broker as if current -- for ever, after a countdown ended or a value went unlisted.
        put(&w, COMFOAIR_HA_PAYLOAD_NONE);
        break;
    case COMFOAIR_VALUE_NUMBER:
        put_num(&w, value->number);
        break;
    case COMFOAIR_VALUE_BOOL:
        // HA's defaults for payload_on / payload_off, so neither has to be published.
        put(&w, value->boolean ? "ON" : "OFF");
        break;
    case COMFOAIR_VALUE_TEXT:
        // BARE, not quoted. An empty text is no value too: an empty RETAINED payload would delete
        // the broker's copy rather than say anything.
        put(&w, value->text != NULL && value->text[0] != '\0' ? value->text
                                                               : COMFOAIR_HA_PAYLOAD_NONE);
        break;
    }

    const size_t used = finish(&w);
    if (used > 0 && skip != NULL)
        *skip = false;
    return used;
}

// The fan's three leaves. Before PDO 65 has been seen every leaf is `None` (fan.py:347 for the
// state; the percentage and preset resets default to it, fan.py:158/:161), so a fan state retained
// before a reboot does not survive the first connection.
size_t comfoair_ha_fan_state_payload(const comfoair_fan_state_t *fan, char *out, size_t cap)
{
    if (fan == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};
    // OFF here is the AWAY level, not a stopped fan: an HRV never stops ventilating entirely.
    put(&w, !fan->known ? COMFOAIR_HA_PAYLOAD_NONE : fan->on ? "ON" : "OFF");
    return finish(&w);
}

size_t comfoair_ha_fan_speed_payload(const comfoair_fan_state_t *fan, char *out, size_t cap)
{
    if (fan == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};
    if (!fan->known)
        put(&w, COMFOAIR_HA_PAYLOAD_NONE);
    else
        put_num(&w, fan->speed);
    return finish(&w);
}

size_t comfoair_ha_fan_preset_payload(const comfoair_fan_state_t *fan, char *out, size_t cap)
{
    if (fan == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};
    // A null preset is a real state -- the operating mode maps to none -- and HA clears a preset
    // with this exact literal.
    put(&w, fan->known && fan->preset != NULL ? fan->preset : COMFOAIR_HA_PAYLOAD_NONE);
    return finish(&w);
}
