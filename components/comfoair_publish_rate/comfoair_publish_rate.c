// See comfoair_publish_rate.h for the contract and why REST and the WebSocket are not limited.
#include "comfoair_publish_rate.h"

#include <math.h>
#include <stddef.h>

bool comfoair_publish_rate_limited(const comfoair_entity_t *entity)
{
    return entity != NULL &&
           (!isnan(entity->publish_deadband) || entity->publish_min_interval_s > 0);
}

comfoair_publish_verdict_t comfoair_publish_rate_decide(const comfoair_entity_t *entity,
                                                        const comfoair_value_t *value,
                                                        const comfoair_publish_rate_slot_t *slot,
                                                        uint32_t now_ms)
{
    if (entity == NULL || value == NULL || slot == NULL || !comfoair_publish_rate_limited(entity) ||
        !slot->sent)
        return COMFOAIR_PUBLISH_SEND;

    // A reading appearing or going away is a STATE change, never jitter: immediate.
    const bool is_number = value->type == COMFOAIR_VALUE_NUMBER;
    if (is_number != slot->was_number)
        return COMFOAIR_PUBLISH_SEND;
    // Exact float equality is the right "same": both sides come out of the one decode (raw integer
    // over the row's divider), so an unchanged frame yields the identical float -- and the state
    // model's own change test (comfoair_state_decode.cpp, same_value) is this same equality.
    if (!is_number || value->number == slot->number)
        return COMFOAIR_PUBLISH_SAME;

    // Unsigned difference: the millisecond clock wraps after 49.7 days, and `now < deadline`
    // would then hold every limited row for the rest of that device's life.
    const uint32_t elapsed = now_ms - slot->at_ms;
    // A held DIFFERENCE catches up here. Never a repeat: the equal case returned above, and the
    // one publication on a clock stays the freshness entity's (comfoair_mqtt_internal.h).
    // DO NOT move this below the deadband test: a drift that stays inside one band would then be
    // held for good, and the "exact within the max hold" guarantee would be false.
    if (elapsed >= (uint32_t)COMFOAIR_PUBLISH_MAX_HOLD_S * 1000u)
        return COMFOAIR_PUBLISH_SEND;

    const float band = isnan(entity->publish_deadband) ? 0.0f : entity->publish_deadband;
    if (fabsf(value->number - slot->number) < band)
        return COMFOAIR_PUBLISH_HOLD;
    if (elapsed < (uint32_t)entity->publish_min_interval_s * 1000u)
        return COMFOAIR_PUBLISH_HOLD;
    return COMFOAIR_PUBLISH_SEND;
}

void comfoair_publish_rate_record(comfoair_publish_rate_slot_t *slot,
                                  const comfoair_value_t *value, uint32_t now_ms)
{
    if (slot == NULL || value == NULL)
        return;
    slot->sent       = true;
    slot->was_number = value->type == COMFOAIR_VALUE_NUMBER;
    slot->number     = slot->was_number ? value->number : 0.0f;
    slot->at_ms      = now_ms;
}
