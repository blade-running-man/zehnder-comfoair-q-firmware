// WHAT GOES IN A CONFIG TOPIC: the retained discovery document for one entity. The state scalars
// left for comfoair_ha_state_payload.c (line ceiling). The topic builders and the parser stayed in
// comfoair_ha.c when this file was split off -- see comfoair_ha_internal.h for the seam.
//
// Every discovery key below was checked against Home Assistant's own validating schema (a 2026.9
// install vendored in ../zehnder-comfoair-q-ha), and the three that are traps rather than
// preferences carry the file:line of the check that would reject them. An invented key produces
// an entity that silently never appears, which is why none of them was written from memory.
#include "comfoair_ha.h"

#include <math.h>
#include <stdio.h>

#include "comfoair_ha_internal.h"

// --- the discovery payload --------------------------------------------------------------------

// Whether this entity's value comes from the ventilation unit, and therefore whether a silent CAN
// bus turns it into a lie -- these are the entities that list `<prefix>/bus` as a second
// availability topic. Every entity is on this side except six: the four durations,
// `schema_version` and the data-freshness heartbeat.
//
// THE REGISTRY ANSWERS IT, by the one field that means exactly this. `default_value` is NaN unless
// the table gives the entity a cold-start value, and the table gives one to precisely those
// entities nothing on the bus will ever report: "No PDO carries these: a duration is only ever told
// to the unit as part of a command, so the value lives on this node alone" (generate_registry.py
// build_entities()). comfoair_state.cpp seed_defaults() seeds the model from the same field
// before a single frame has arrived, and src/main.cpp restores what the owner last set over the
// top -- which is what makes the four durations true with the loom unplugged, and greying them out
// the second lie of the day.
//
// THE THREE SELECTS (temperature_passive, humidity_comfort, humidity_protection) ARE BUS-BACKED.
// They have no default: the probe task reads their real value from the unit over RMI at boot and
// every six hours, and until it answers they read unknown. A value that comes from the bus must
// go grey with it.
//
// DO NOT write this as `entity->pdo < 0`. FOURTEEN entities carry no PDO of their own and only six
// are exempt here (the four durations, schema_version, data_freshness). The other eight are
// bus-backed: the three selects above are read over RMI, unit_error and active_errors from the
// unit's active-error bitset over RMI, balance_mode is derived from PDOs 54 and 55,
// ventilation_mode from 49 and 65, and the fan from 65 and 49 (comfoair_state_derived.cpp
// recompute_fan()). Those eight would then go on being published as available on a bus that has
// been dead for an hour, which is the whole failure the bus topic exists to end -- and the fan is
// the one the owner looks at.
//
// THE SECOND CLAUSE IS THE OWNER'S DECISION, and a different argument from the first. `schema_version`
// is the one COMFOAIR_KIND_DIAGNOSTIC_CONST in the registry: it carries no default_value, because
// it is not a cold-start guess at something the unit will later report -- it is a constant this
// firmware publishes about ITSELF (comfoair_state.cpp seed_defaults()). It is what a client checks the entity
// contract with, and the contract still holds while the bus is silent. Greying it out would be a
// lie told to the reader who came to find out why the other cards went grey.
//
// THE THIRD CLAUSE IS THE FRESHNESS ENTITY, on a third argument again. It reports that THIS NODE
// published within the last five minutes; gating that on the CAN bus is a category error, and a
// cruel one -- its whole job is to keep answering while everything downstream has stopped. It has
// neither a cold-start default nor the diagnostic kind, so neither clause above reaches it, which
// is why this is a clause and not a widening.
static bool bus_backed(const comfoair_entity_t *entity)
{
    // isnan, not a comparison: away_duration's default IS 0 and has to survive, the same trap
    // render_c.py render_c_header() records for min_value.
    return isnan(entity->default_value) && entity->kind != COMFOAIR_KIND_DIAGNOSTIC_CONST &&
           entity->kind != COMFOAIR_KIND_FRESHNESS;
}

// Repeated in every discovery message, which is why the keys are abbreviated: HA expands them
// itself (mqtt/discovery.py:175-186). `ids` alone satisfies its "at least one identifying value"
// rule (mqtt/schemas.py:121-129); `cns` is added on top because the owner chose to keep this device
// on the card Home Assistant already knows for this unit's MAC.
static void put_device(writer_t *w, const comfoair_ha_ctx_t *ctx)
{
    put(w, ",\"dev\":{\"ids\":[");
    put_string(w, ctx->device_id);
    put(w, "]");
    if (ctx->mac != NULL && ctx->mac[0] != '\0') {
        put(w, ",\"cns\":[[\"mac\",");
        put_string(w, ctx->mac);
        put(w, "]]");
    }
    put_opt(w, "name", ctx->device_name);
    put(w, ",\"mf\":\"Zehnder\",\"mdl\":\"ComfoAir Q\"");
    put_opt(w, "sw", ctx->sw_version);
    // cv.configuration_url is stricter than a string: a value with no scheme is REJECTED, and a
    // rejected device block takes the whole entity with it. Absent beats present-and-wrong.
    put_opt(w, "cu", ctx->url);
    put(w, "}");
}

size_t comfoair_ha_discovery_payload(const comfoair_ha_ctx_t *ctx, const comfoair_entity_t *entity,
                                     char *out, size_t cap)
{
    if (!usable(ctx) || entity == NULL)
        return 0;
    const char *component = comfoair_ha_component(entity);
    if (component == NULL)
        return 0;

    char state[160];
    char command[160];
    char status[160];
    char bus[160];
    if (comfoair_ha_state_topic(ctx, entity->key, state, sizeof state) == 0 ||
        comfoair_ha_meta_topic(ctx, COMFOAIR_HA_META_STATUS, status, sizeof status) == 0 ||
        comfoair_ha_meta_topic(ctx, COMFOAIR_HA_META_BUS, bus, sizeof bus) == 0)
        return 0;
    const bool writable = entity->writable;
    if (writable && comfoair_ha_command_topic(ctx, entity->key, command, sizeof command) == 0)
        return 0;

    writer_t w = {out, cap, 0, false};
    put(&w, "{\"name\":");
    put_string(&w, entity->name);

    // The unique id is the MAC plus the key, never the display name. Renaming must not re-key
    // anything, or Home Assistant holds two devices and the dead one cannot be removed.
    put(&w, ",\"uniq_id\":\"");
    put(&w, ctx->device_id);
    put(&w, "_");
    put(&w, entity->key);
    put(&w, "\"");

    // Availability: A LIST, ALWAYS, EVEN WHEN IT HOLDS ONE TOPIC. payload_available /
    // payload_not_available are HA's defaults, so only the topics have to travel.
    //
    // `avty` REPLACES `avty_t` AND MUST NEVER JOIN IT. The two are vol.Exclusive in the SAME group
    // "availability" -- mqtt/schemas.py:81 and :97, joined by .extend(), which preserves the
    // markers -- so a document carrying both raises vol.MultipleInvalid, and an entity whose
    // config was rejected does not break visibly: it SILENTLY NEVER APPEARS. DO NOT keep `avty_t`
    // for the six that have one topic on the grounds that it is shorter; that is the branch the
    // exclusivity punishes, and the whole cost of the list form here is twenty bytes.
    //
    // `all` and not `any` or `latest` (entity.py:722-729). `any` fails fatally: the will is one
    // per connection (cfg.session.last_will) and `status` has it, so after a hard power cut the
    // broker still holds a retained `bus = online` and every entity of a dead device would stay
    // available. `latest` fails more quietly -- its answer is whichever message arrived last, and
    // after a reconnect the delivery order of retained messages is the broker's to choose.
    put(&w, ",\"avty\":[{\"t\":");
    put_string(&w, status);
    if (bus_backed(entity)) {
        put(&w, "},{\"t\":");
        put_string(&w, bus);
    }
    put(&w, "}],\"avty_mode\":\"all\"");

    // The fan is the one entity whose state does not live on a single topic.
    if (entity->kind != COMFOAIR_KIND_FAN) {
        put(&w, ",\"stat_t\":");
        put_string(&w, state);
    }
    if (writable && entity->kind != COMFOAIR_KIND_FAN) {
        put(&w, ",\"cmd_t\":");
        put_string(&w, command);
    }

    put_opt(&w, "icon", entity->icon);
    put_opt(&w, "ent_cat", entity->entity_category);
    // One registry field for every kind. The ventilation_mode select used to be disabled here
    // by key; a key test in C is the second entity list AGENTS.md forbids.
    if (!entity->enabled_by_default)
        put(&w, ",\"en\":false");

    switch (entity->kind) {
    case COMFOAIR_KIND_NUMERIC:
        put_opt(&w, "dev_cla", entity->device_class);
        put_opt(&w, "stat_cla", entity->state_class);
        put_opt(&w, "unit_of_meas", entity->unit);
        break;

    case COMFOAIR_KIND_BINARY:
        put_opt(&w, "dev_cla", entity->device_class);
        break;

    case COMFOAIR_KIND_FRESHNESS:
        // `connectivity`, from the registry row like every other device class -- HA renders it as
        // Connected / Disconnected, and Disconnected is never published: the entity goes
        // UNAVAILABLE instead, which is the only state that means "nobody is talking".
        put_opt(&w, "dev_cla", entity->device_class);
        // THE ONE expire_after IN THE TREE, and THE MACRO rather than a literal beside it:
        // registry_generated.h emits it next to the publication period with a static_assert
        // binding the two, because the timer is reset by every message and an expiry that stops
        // being a multiple of its cadence greys out a healthy device. DO NOT put this key on a
        // data entity -- that was weighed and rejected, and test_ha_freshness counts the carriers:
        // delta publication plus expire_after means republishing every slow reading on a clock
        // for ever, the whole publication model traded away for one property.
        put(&w, ",\"exp_aft\":");
        put_num(&w, (double)COMFOAIR_FRESHNESS_EXPIRE_S);
        break;

    case COMFOAIR_KIND_ENUM:
    case COMFOAIR_KIND_SPECIAL:
    case COMFOAIR_KIND_DIAGNOSTIC_CONST:
        // Nothing. No options, no device class, no unit -- see comfoair_ha_component().
        break;

    case COMFOAIR_KIND_SELECT:
        put(&w, ",\"ops\":[");
        for (uint8_t i = 0; i < entity->option_count; i++) {
            if (i > 0)
                put(&w, ",");
            put_string(&w, entity->options[i]);
        }
        put(&w, "]");
        break;

    case COMFOAIR_KIND_NUMBER:
        // The range is the registry's. NaN means "no range", and a literal zero here would be the
        // bug render_c.py render_c_header() records: away_duration's real minimum IS 0 and has to
        // survive, so the test is "is it a number", never a comparison against zero.
        if (!isnan(entity->min_value)) {
            put(&w, ",\"min\":");
            put_num(&w, entity->min_value);
        }
        if (!isnan(entity->max_value)) {
            put(&w, ",\"max\":");
            put_num(&w, entity->max_value);
        }
        // The half of the minutes/seconds trap discovery can fix: the topic tree carries no units
        // anywhere, so without this a consumer reads 60 (minutes) and writes 60 (seconds).
        put_opt(&w, "unit_of_meas", entity->unit);
        break;

    case COMFOAIR_KIND_SWITCH:
        break;

    case COMFOAIR_KIND_FAN: {
        // Six topics, not one. HA reads a fan's on/off, its percentage and its preset from three
        // separate places, and each FEATURE is switched on by its COMMAND topic, not its state
        // topic (mqtt/fan.py:265-270) -- publishing only the state topic gives a fan with no
        // presets and no error at all.
        char leaf[160];
        put(&w, ",\"stat_t\":");
        if (comfoair_ha_fan_topic(ctx, COMFOAIR_HA_FAN_STATE, leaf, sizeof leaf) == 0)
            return 0;
        put_string(&w, leaf);
        put(&w, ",\"cmd_t\":");
        if (comfoair_ha_command_topic(ctx, entity->key, leaf, sizeof leaf) == 0)
            return 0;
        put_string(&w, leaf);

        put(&w, ",\"pct_stat_t\":");
        if (comfoair_ha_fan_topic(ctx, COMFOAIR_HA_FAN_SPEED_STATE, leaf, sizeof leaf) == 0)
            return 0;
        put_string(&w, leaf);
        put(&w, ",\"pct_cmd_t\":");
        if (comfoair_ha_fan_topic(ctx, COMFOAIR_HA_FAN_SPEED_COMMAND, leaf, sizeof leaf) == 0)
            return 0;
        put_string(&w, leaf);
        // 1..3, so HA's percentage maps onto three steps rather than a hundred
        // (mqtt/fan.py:272-276, _attr_speed_count).
        put(&w, ",\"spd_rng_min\":1,\"spd_rng_max\":3");

        // preset_modes and preset_mode_command_topic are an INCLUSIVE pair (mqtt/fan.py:140-147):
        // the list alone fails validation, the topic alone yields zero presets silently.
        put(&w, ",\"pr_mode_stat_t\":");
        if (comfoair_ha_fan_topic(ctx, COMFOAIR_HA_FAN_PRESET_STATE, leaf, sizeof leaf) == 0)
            return 0;
        put_string(&w, leaf);
        put(&w, ",\"pr_mode_cmd_t\":");
        if (comfoair_ha_fan_topic(ctx, COMFOAIR_HA_FAN_PRESET_COMMAND, leaf, sizeof leaf) == 0)
            return 0;
        put_string(&w, leaf);
        put(&w, ",\"pr_modes\":[");
        for (uint8_t i = 0; i < entity->option_count; i++) {
            if (i > 0)
                put(&w, ",");
            put_string(&w, entity->options[i]);
        }
        put(&w, "]");
        break;
    }
    }

    put_device(&w, ctx);
    put(&w, "}");
    return finish(&w);
}
