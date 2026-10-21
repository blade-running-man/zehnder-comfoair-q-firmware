// What this device empties when it stops being the thing it was.
//
// See comfoair_ha.h for the contract -- the two operations, why their orders differ, and why the
// meta enum is the only door through which a top-level retained topic can come into existence.
//
// A SECOND FILE RATHER THAN NINETY MORE LINES IN comfoair_ha.c, and not for tidiness: that file is
// already 530 lines against the 350-line ceiling (AGENTS.md), and "split along a seam that already
// exists" is exactly what this is -- the cleanup is one responsibility, the public header did not
// change shape, and three other tasks in this wave are editing comfoair_ha.c at the same time.
//
// THE SEQUENCE IS WALKED, NOT TABULATED. There is no array of topics anywhere below: every name
// comes out of one of the four builders in comfoair_ha.c, and the membership of the sequence is
// decided by the same two questions the publisher asks -- does this entity have a Home Assistant
// component (comfoair_mqtt_publish.c:116-119, :218-220), and is it the fan. Resolving an index by
// walking is O(n) per call and a withdrawal is comfoair_ha_cleanup_count() entries, so
// it costs some tens of thousands of pointer comparisons on a task about to wait 2 s on a socket. A
// lookup table would be that second list, and it would be the one that goes stale.
//
// AND UNTIL 2026-09-07 THAT CLAIM WAS FALSE BY EIGHT LINES. There was a `FAN_LEAVES[]` array
// right here -- a second copy of it in comfoair_mqtt_publish.c and a third in the suite -- so a
// fourth leaf added to publish_fan() was erased by nothing and caught by nothing. It is now
// comfoair_ha_fan_leaf_t (comfoair_ha.h) and this file walks it, exactly as it already walked
// comfoair_ha_meta_t. There is no array in this file any more, and that is now checkable rather
// than asserted.
#include "comfoair_ha.h"

// Whether this device publishes anything at all for an entity. The one filter, asked of the same
// function the publisher asks: an entity comfoair_ha_component() does not map has no discovery
// document and no state topic, so there is nothing of ours to erase.
static bool published(const comfoair_entity_t *entity)
{
    return comfoair_ha_component(entity) != NULL;
}

static size_t discovery_count(void)
{
    size_t n = 0;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (published(&COMFOAIR_ENTITIES[i]))
            n++;
    return n;
}

static size_t state_count(void)
{
    // Every meta topic, then one per published entity -- except the fan, which contributes three
    // and no <prefix>/<key>/state of its own, because publish_entity() hands it to publish_fan()
    // before a state topic is ever built (comfoair_mqtt_publish.c:94-95).
    size_t n = COMFOAIR_HA_META_COUNT;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
        if (!published(e))
            continue;
        n += e->kind == COMFOAIR_KIND_FAN ? (size_t)COMFOAIR_HA_FAN_RETAINED_COUNT : 1;
    }
    return n;
}

static size_t discovery_at(const comfoair_ha_ctx_t *ctx, size_t index, char *out, size_t cap)
{
    size_t seen = 0;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
        if (!published(e))
            continue;
        if (seen++ == index)
            return comfoair_ha_discovery_topic(ctx, e, out, cap);
    }
    return 0;
}

static size_t state_at(const comfoair_ha_ctx_t *ctx, size_t index, char *out, size_t cap)
{
    // THE META TOPICS FIRST, and the order inside the state side is otherwise free: nothing reads
    // these while they are being emptied. First, because both of them are entries in every entity's
    // `avty` list (comfoair_ha_payload.c), and an empty payload on either matches neither "online"
    // nor "offline", so HA keeps the previous value and no availability flap comes out of the
    // cleanup at all (entity.py:689-694 -- two branches, if/elif, and no else).
    if (index < COMFOAIR_HA_META_COUNT)
        return comfoair_ha_meta_topic(ctx, (comfoair_ha_meta_t)index, out, cap);

    size_t seen = COMFOAIR_HA_META_COUNT;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++) {
        const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
        if (!published(e))
            continue;
        if (e->kind == COMFOAIR_KIND_FAN) {
            // 0 .. COMFOAIR_HA_FAN_RETAINED_COUNT, which is where the `/state` leaves stop and the
            // `/command` ones begin (comfoair_ha.h). The command leaves are values of the same
            // enum and are deliberately NOT reached from here: Home Assistant writes them, and
            // emptying one is emptying somebody else's retained message.
            for (size_t leaf = 0; leaf < (size_t)COMFOAIR_HA_FAN_RETAINED_COUNT; leaf++)
                if (seen++ == index)
                    return comfoair_ha_fan_topic(ctx, (comfoair_ha_fan_leaf_t)leaf, out, cap);
            continue;
        }
        if (seen++ == index)
            return comfoair_ha_state_topic(ctx, e->key, out, cap);
    }
    return 0;
}

size_t comfoair_ha_cleanup_count(comfoair_ha_cleanup_t what)
{
    switch (what) {
    case COMFOAIR_HA_CLEANUP_REKEY:
        return state_count();
    case COMFOAIR_HA_CLEANUP_WITHDRAW:
        return discovery_count() + state_count();
    }
    return 0;
}

size_t comfoair_ha_cleanup_topic(const comfoair_ha_ctx_t *ctx, comfoair_ha_cleanup_t what,
                                 size_t index, char *out, size_t cap)
{
    switch (what) {
    case COMFOAIR_HA_CLEANUP_REKEY:
        // NO DISCOVERY TOPIC IS REACHABLE FROM HERE, and that is the whole of the operation. The
        // config topic does not carry the prefix, so it is rewritten in place by the new client;
        // emptying it would delete every entity with its history instead. comfoair_ha.h carries
        // the argument in full.
        return state_at(ctx, index, out, cap);
    case COMFOAIR_HA_CLEANUP_WITHDRAW: {
        // DISCOVERY FIRST. Not a preference: with the configs gone Home Assistant has removed the
        // entities, so nothing is left pointing at the state topics that are emptied next. The
        // other order leaves a window in which live entities read a topic that has just been
        // cleared. DO NOT swap these two lines to "clean the small side first".
        const size_t configs = discovery_count();
        return index < configs ? discovery_at(ctx, index, out, cap)
                               : state_at(ctx, index - configs, out, cap);
    }
    }
    return 0;
}
