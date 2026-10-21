// WHAT A TOPIC IS CALLED: the six builders, the entity-to-component mapping, and the parser that
// says what an arriving topic addresses. The payloads moved to comfoair_ha_payload.c on
// 2026-09-07 -- see comfoair_ha_internal.h for why the split is here and not elsewhere.
//
// See comfoair_ha.h for the topic tree, for the three enumerations that are the only doors a
// topic of this device can come through, and for which Home Assistant rules govern each payload.
#include "comfoair_ha.h"

#include <stdio.h>
#include <string.h>

#include "comfoair_ha_internal.h"

// --- topics -----------------------------------------------------------------------------------

static size_t topic(char *out, size_t cap, const char *prefix, const char *a, const char *b,
                    const char *c)
{
    writer_t w = {out, cap, 0, false};
    put(&w, prefix);
    if (a != NULL) {
        put(&w, "/");
        put(&w, a);
    }
    if (b != NULL) {
        put(&w, "/");
        put(&w, b);
    }
    if (c != NULL) {
        put(&w, "/");
        put(&w, c);
    }
    // A truncated topic is a DIFFERENT topic: it publishes successfully, to nobody.
    return finish(&w);
}

size_t comfoair_ha_meta_topic(const comfoair_ha_ctx_t *ctx, comfoair_ha_meta_t which, char *out,
                              size_t cap)
{
    if (!usable(ctx))
        return 0;
    // TOTAL, and it has to stay total: [env:native] builds with -Werror=switch, so a value added
    // to comfoair_ha_meta_t and left unnamed here does not compile. That is the half of the rule a
    // test cannot express -- the other half, that the cleanup walks every value, is
    // test_every_meta_topic_is_erased_by_both_cleanups.
    switch (which) {
    case COMFOAIR_HA_META_STATUS:
        return topic(out, cap, ctx->topic_prefix, "status", NULL, NULL);
    case COMFOAIR_HA_META_BUS:
        return topic(out, cap, ctx->topic_prefix, "bus", NULL, NULL);
    case COMFOAIR_HA_META_COUNT:
        break;
    }
    // Including COMFOAIR_HA_META_COUNT itself and anything cast in from outside the enum. NOT a
    // fall-through to the bare prefix: that would empty a topic this device has never written.
    return 0;
}

size_t comfoair_ha_state_topic(const comfoair_ha_ctx_t *ctx, const char *key, char *out, size_t cap)
{
    if (!usable(ctx) || key == NULL)
        return 0;
    // THE REGISTRY IS THE DOOR, exactly as comfoair_ha_meta_t is the door for a top-level topic,
    // and this line is what makes that a construction rather than a sentence. Until 2026-09-07
    // this function would build `<prefix>/<anything>/state` for any string handed to it, so the
    // rule "a retained topic does not exist until the enumeration can name it" could be walked
    // round without writing a single snprintf: one publish under a key the registry does not
    // carry is a retained topic the cleanup walks straight past, and it stays on the owner's
    // broker for ever -- there is no second pass that goes looking for orphans.
    //
    // O(n) strcmp over the registry, on a path that runs at most eight times per publish pass and
    // once per non-fan entity per retraction. DO NOT "optimise" it away by trusting the caller:
    // the caller being trustworthy is precisely what could not be checked before.
    if (comfoair_entity_by_key(key) == NULL)
        return 0;
    return topic(out, cap, ctx->topic_prefix, key, "state", NULL);
}

size_t comfoair_ha_command_topic(const comfoair_ha_ctx_t *ctx, const char *key, char *out,
                                 size_t cap)
{
    if (!usable(ctx) || key == NULL)
        return 0;
    return topic(out, cap, ctx->topic_prefix, key, "command", NULL);
}

size_t comfoair_ha_fan_topic(const comfoair_ha_ctx_t *ctx, comfoair_ha_fan_leaf_t leaf, char *out,
                             size_t cap)
{
    if (!usable(ctx))
        return 0;
    // TOTAL, and it has to stay total for the same reason comfoair_ha_meta_topic()'s switch does:
    // [env:native] builds with -Werror=switch, so a leaf added to comfoair_ha_fan_leaf_t and left
    // unnamed here does not compile. The other half -- that the cleanup walks every retained
    // value -- is test_every_retained_fan_leaf_is_erased_by_both_cleanups.
    switch (leaf) {
    case COMFOAIR_HA_FAN_STATE:
        return topic(out, cap, ctx->topic_prefix, "fan", "state", NULL);
    case COMFOAIR_HA_FAN_SPEED_STATE:
        return topic(out, cap, ctx->topic_prefix, "fan", "speed/state", NULL);
    case COMFOAIR_HA_FAN_PRESET_STATE:
        return topic(out, cap, ctx->topic_prefix, "fan", "preset/state", NULL);
    case COMFOAIR_HA_FAN_SPEED_COMMAND:
        return topic(out, cap, ctx->topic_prefix, "fan", "speed/command", NULL);
    case COMFOAIR_HA_FAN_PRESET_COMMAND:
        return topic(out, cap, ctx->topic_prefix, "fan", "preset/command", NULL);
    case COMFOAIR_HA_FAN_ANY_COMMAND:
        return topic(out, cap, ctx->topic_prefix, "fan", "+/command", NULL);
    case COMFOAIR_HA_FAN_LEAF_COUNT:
        break;
    }
    // Including COMFOAIR_HA_FAN_LEAF_COUNT itself and anything cast in from outside the enum. NOT
    // a fall-through to `<prefix>/fan`: that would empty a topic this device has never written.
    return 0;
}

size_t comfoair_ha_op_topic(const comfoair_ha_ctx_t *ctx, const char *name, char *out, size_t cap)
{
    if (!usable(ctx) || name == NULL)
        return 0;
    return topic(out, cap, ctx->topic_prefix, "ops", name, "set");
}

size_t comfoair_ha_discovery_topic(const comfoair_ha_ctx_t *ctx, const comfoair_entity_t *entity,
                                   char *out, size_t cap)
{
    if (!usable(ctx) || entity == NULL)
        return 0;
    const char *component = comfoair_ha_component(entity);
    if (component == NULL)
        return 0;
    // <discovery_prefix>/<component>/<node>/<object>/config. HA's TOPIC_MATCHER accepts only
    // [a-zA-Z0-9_-] in the node and object segments (mqtt/discovery.py:64-67), which is why the
    // node is bare hex and never the colon-separated MAC that the device block carries.
    writer_t w = {out, cap, 0, false};
    put(&w, COMFOAIR_HA_DISCOVERY_PREFIX);
    put(&w, "/");
    put(&w, component);
    put(&w, "/");
    put(&w, ctx->device_id);
    put(&w, "/");
    put(&w, entity->key);
    put(&w, "/config");
    return finish(&w);
}

// --- which component an entity becomes --------------------------------------------------------

const char *comfoair_ha_component(const comfoair_entity_t *entity)
{
    if (entity == NULL)
        return NULL;
    switch (entity->kind) {
    case COMFOAIR_KIND_NUMERIC:
    case COMFOAIR_KIND_DIAGNOSTIC_CONST:
        return "sensor";
    case COMFOAIR_KIND_BINARY:
    case COMFOAIR_KIND_FRESHNESS:
        // binary_sensor AND NOT sensor for the freshness entity, and the choice is forced rather
        // than preferred: expire_after -- the only thing in Home Assistant that turns silence into
        // `unavailable` with no help from the device -- exists on binary_sensor.py:54 and
        // sensor.py:80 and nowhere else in components/mqtt/. Of the two, a sensor would need a
        // VALUE to show, while the fact here has none: what means something is that the message
        // arrived at all, which is what a connectivity binary_sensor reads.
        return "binary_sensor";
    case COMFOAIR_KIND_ENUM:
    case COMFOAIR_KIND_SPECIAL:
        // A PLAIN sensor, deliberately. HA validates `options`, `device_class: enum`,
        // `unit_of_measurement` and `state_class` against one another (mqtt/sensor.py:109-125).
        // An undocumented unit value decodes to no value and publishes `None`, which HA accepts
        // before the options check (sensor.py:317), so options would now be safe. DO NOT add them
        // in passing: they change the entity's class for every existing MQTT user.
        return "sensor";
    case COMFOAIR_KIND_SELECT:
        return "select";
    case COMFOAIR_KIND_NUMBER:
        return "number";
    case COMFOAIR_KIND_SWITCH:
        return "switch";
    case COMFOAIR_KIND_FAN:
        return "fan";
    }
    return NULL;
}

// --- what an arriving topic means -------------------------------------------------------------

// The remainder of `topic` after `prefix/`, or NULL. Length-bounded throughout: esp-mqtt hands
// over a pointer and a length into its receive buffer, with no terminator anywhere.
static const char *after_prefix(const comfoair_ha_ctx_t *ctx, const char *topic, size_t len,
                                size_t *rest_len)
{
    const size_t n = strlen(ctx->topic_prefix);
    if (len <= n + 1 || strncmp(topic, ctx->topic_prefix, n) != 0 || topic[n] != '/')
        return NULL;
    *rest_len = len - n - 1;
    return topic + n + 1;
}

// True when `rest` is exactly `<name>/<suffix>`, and copies <name> out. The suffix is matched at
// the END, so a key containing a slash cannot be smuggled past it.
static bool split_tail(const char *rest, size_t len, const char *suffix, char *name, size_t cap)
{
    const size_t s = strlen(suffix);
    if (len <= s + 1 || strncmp(rest + len - s, suffix, s) != 0 || rest[len - s - 1] != '/')
        return false;
    const size_t n = len - s - 1;
    if (n == 0 || n + 1 > cap)
        return false;
    // A key is one segment. Anything with a slash inside it is not one of ours, and accepting it
    // would let `<prefix>/a/b/command` be read as the key "a/b".
    for (size_t i = 0; i < n; i++)
        if (rest[i] == '/')
            return false;
    memcpy(name, rest, n);
    name[n] = '\0';
    return true;
}

comfoair_ha_target_t comfoair_ha_parse_topic(const comfoair_ha_ctx_t *ctx, const char *topic,
                                             size_t topic_len, char *name, size_t cap)
{
    if (!usable(ctx) || topic == NULL || name == NULL || cap == 0)
        return COMFOAIR_HA_TARGET_NONE;

    size_t      rest_len = 0;
    const char *rest     = after_prefix(ctx, topic, topic_len, &rest_len);
    if (rest == NULL)
        return COMFOAIR_HA_TARGET_NONE;

    // The fan's three, matched before the generic entity form: "fan/speed/command" would otherwise
    // be read as an entity named "fan/speed", which split_tail refuses -- but naming them first
    // makes the intent legible rather than relying on that refusal.
    //
    // THESE STRINGS ARE THE READER'S COPY OF WHAT comfoair_ha_fan_topic() WRITES, and until
    // 2026-09-07 nothing tied the two together: a mutation of the builder ("speed/command" ->
    // "pct/command") sat in this tree with every host case green, because every case that named a
    // fan command topic typed it out. On the device that is silent and total -- HA is told
    // `pct_cmd_t`, the `fan/+/command` subscription still matches, and this function answers
    // TARGET_NONE. test_every_command_leaf_the_builder_names_is_understood_by_the_parser
    // (test/test_ha) closes the circle: it BUILDS each command topic and requires the target back.
    // DO NOT edit a string here without running it.
    struct {
        const char          *tail;
        comfoair_ha_target_t target;
    } const fan[] = {
        {"fan/speed/command", COMFOAIR_HA_TARGET_FAN_SPEED},
        {"fan/preset/command", COMFOAIR_HA_TARGET_FAN_PRESET},
        {"fan/command", COMFOAIR_HA_TARGET_FAN_ON},
    };
    for (size_t i = 0; i < sizeof fan / sizeof fan[0]; i++) {
        const size_t n = strlen(fan[i].tail);
        if (rest_len == n && strncmp(rest, fan[i].tail, n) == 0)
            return fan[i].target;
    }

    // <prefix>/ops/<name>/set
    if (rest_len > 4 && strncmp(rest, "ops/", 4) == 0 &&
        split_tail(rest + 4, rest_len - 4, "set", name, cap))
        return COMFOAIR_HA_TARGET_OP;

    if (split_tail(rest, rest_len, "command", name, cap))
        return COMFOAIR_HA_TARGET_ENTITY;

    return COMFOAIR_HA_TARGET_NONE;
}

