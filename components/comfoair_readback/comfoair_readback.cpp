// See comfoair_readback.h for the contract and for why the guard is not a value compare-and-set.
#include "comfoair_readback.h"

bool comfoair_readback_decode(const comfoair_rmi_readback_t *entry, const uint8_t *data,
                              size_t len, uint8_t *index_out)
{
    if (entry == NULL || data == NULL || index_out == NULL)
        return false;
    if (entry->entity >= COMFOAIR_ENTITY_COUNT ||
        COMFOAIR_ENTITIES[entry->entity].options == NULL)
        return false;
    // Exactly one byte. The selects are U8 on the unit (comfoair-pdo-rmi.md §3, TEMPHUMCONTROL
    // 0x04/0x06/0x07), and a longer answer means the property is not the one we think it is --
    // taking its first byte would show a setting the unit does not hold.
    if (len != 1)
        return false;
    // The bound is the ENTITY's option count, not a literal 3: the wire value is the index into
    // the labels the registry publishes, so the two cannot disagree without this refusing.
    if (data[0] >= COMFOAIR_ENTITIES[entry->entity].option_count)
        return false;
    *index_out = data[0];
    return true;
}

bool comfoair_readback_may_apply(uint32_t queued_before, uint32_t done_before, uint32_t queued_now)
{
    return queued_before == done_before && queued_now == queued_before;
}
