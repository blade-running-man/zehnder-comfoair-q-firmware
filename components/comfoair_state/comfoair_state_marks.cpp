// The change marks: one row per consumer, and the generation that makes a conditional clear safe.
// Cut out of comfoair_state.cpp for the 350-line ceiling along the seam comfoair_state.h already
// draws ("Who is asking"); no behaviour moved. It touches COMFOAIR_ENTITY_COUNT only, never
// COMFOAIR_ENTITIES, so comfoair_state_internal.h's one-translation-unit rule still holds.
#include "comfoair_state.h"

#include <cstring>

#include "comfoair_lock.h"
#include "comfoair_state_internal.h"

namespace {

// One bit per consumer, not one flag: see comfoair_state_consumer_t.
bool g_changed[COMFOAIR_STATE_CONSUMER_COUNT][COMFOAIR_ENTITY_COUNT];
// How many times each slot has been marked, per consumer. A consumer samples this before it reads
// the value and hands it back to comfoair_state_clear_change_if, which is what stops the clear
// from erasing a mark set while the value was in flight -- comfoair_state.h has the full argument.
// A bool cannot carry that: "still set" and "set again" look identical. 4 bytes per entity per
// consumer.
uint32_t g_generation[COMFOAIR_STATE_CONSUMER_COUNT][COMFOAIR_ENTITY_COUNT];

}  // namespace

namespace comfoair_state_detail {

void mark_changed(size_t index)
{
    for (int who = 0; who < COMFOAIR_STATE_CONSUMER_COUNT; who++) {
        g_changed[who][index] = true;
        // Even when the flag was already set. That repeat is the ONLY signal that a second change
        // arrived while a consumer was publishing the first, so DO NOT skip it when the flag is
        // already true. Wrapping after 2^32 markings is a match with a generation sampled 4.29
        // billion changes earlier, which no publish outlives.
        g_generation[who][index]++;
    }
}

// The marks only. DO NOT clear g_generation alongside them: the counter has to keep rising for
// comfoair_state_clear_change_if to mean anything (comfoair_state_reset()).
void marks_reset()
{
    std::memset(g_changed, 0, sizeof g_changed);
}

}  // namespace comfoair_state_detail

extern "C" {

bool comfoair_state_changed(comfoair_state_consumer_t who, size_t index)
{
    if (who >= COMFOAIR_STATE_CONSUMER_COUNT || index >= COMFOAIR_ENTITY_COUNT)
        return false;
    // Under the lock, like every other reader. mark_changed() runs from the CAN task and these two
    // run from the publisher and the renderer; without it a PDO arriving between a reader's test
    // and its clear loses its mark, and the value is not published until it next changes.
    comfoair_lock();
    const bool changed = g_changed[who][index];
    comfoair_unlock();
    return changed;
}

void comfoair_state_clear_change(comfoair_state_consumer_t who, size_t index)
{
    if (who >= COMFOAIR_STATE_CONSUMER_COUNT || index >= COMFOAIR_ENTITY_COUNT)
        return;
    comfoair_lock();
    g_changed[who][index] = false;
    comfoair_unlock();
}

uint32_t comfoair_state_change_generation(comfoair_state_consumer_t who, size_t index)
{
    if (who >= COMFOAIR_STATE_CONSUMER_COUNT || index >= COMFOAIR_ENTITY_COUNT)
        return 0;
    // Under the lock for the reason comfoair_state_changed states above: mark_changed() runs on
    // the CAN task, and a torn read here would be compared for equality later.
    comfoair_lock();
    const uint32_t generation = g_generation[who][index];
    comfoair_unlock();
    return generation;
}

bool comfoair_state_clear_change_if(comfoair_state_consumer_t who, size_t index, uint32_t seen)
{
    if (who >= COMFOAIR_STATE_CONSUMER_COUNT || index >= COMFOAIR_ENTITY_COUNT)
        return false;
    comfoair_lock();
    // ONE critical section for the compare AND the clear. DO NOT read the generation through
    // comfoair_state_change_generation() and clear afterwards: the CAN task would mark the slot in
    // the gap between the two calls, and this would then be an unconditional clear with extra
    // steps -- the exact defect it exists to close.
    const bool matched = g_generation[who][index] == seen;
    if (matched)
        g_changed[who][index] = false;
    comfoair_unlock();
    return matched;
}

void comfoair_state_clear_changes(comfoair_state_consumer_t who)
{
    if (who >= COMFOAIR_STATE_CONSUMER_COUNT)
        return;
    comfoair_lock();
    // ONE ROW. This used to memset the whole array, which is what made the two consumers share
    // one set of marks -- the signature changed and the body did not, and the test above is what
    // noticed. sizeof g_changed[who] is the row, not the table.
    //
    // The marks only. DO NOT clear g_generation[who] alongside them: the counter has to keep
    // rising for comfoair_state_clear_change_if to mean anything, and a rewind here would let a
    // generation sampled before this sweep match a slot marked after it.
    std::memset(g_changed[who], 0, sizeof g_changed[who]);
    comfoair_unlock();
}

}  // extern "C"
