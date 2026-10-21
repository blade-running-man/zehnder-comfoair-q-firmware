// What goes out on the wire: the topics, the payloads and the two buffers they are built in.
//
// TWO THINGS LEFT THIS FILE ON 2026-09-07, both for the 350-line ceiling and neither with any
// change of behaviour. The RETRACTION -- what the device empties when it stops being the thing it
// was -- went to comfoair_mqtt_retract.c, along the seam comfoair_ha already draws on its own
// side: this file says what the device IS and that one unsays it. DISCOVERY and the three
// subscriptions went to comfoair_mqtt_discovery.c, which is where the discovery gate then put a
// rule this file has no equivalent of: everything below goes out again on every connection, and the
// config documents do not. <prefix>/bus later left for comfoair_mqtt_bus.c, for the same ceiling.
//
// See comfoair_mqtt_internal.h for the shape of this component and for why none of this may run
// anywhere but the publisher task; see comfoair_mqtt.h for the rules that govern all nine files.
#include "comfoair_mqtt_internal.h"

#include <string.h>

#include "comfoair_ha.h"
#include "comfoair_publish_rate.h"
#include "comfoair_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

// Written ONLY by the publisher task. The event handler must never touch them; that was the
// critical defect this component was restructured to remove. Both are declared in
// comfoair_mqtt_internal.h and both are shared, on the one condition that makes sharing sound and
// nothing else: every file that formats into them runs on the publisher task.
//
// THE PAYLOAD BECAME SHARED ON 2026-09-07 and the note it replaces said it was "deliberately NOT
// shared". That was true of its reader at the time and not of the buffer: comfoair_mqtt_retract.c
// renders no payload at all -- the retraction's payload is the empty string -- so there was
// nothing to share with. comfoair_mqtt_discovery.c renders 1 040 bytes of config document, and a
// second BUFFER_SIZE static beside this one would be 1.5 KB of RAM bought to restate a rule the
// task ownership already gives.
char g_topic[192];
char g_payload[BUFFER_SIZE];

// NO LOCK, and that is a consequence of the client now belonging to one task rather than an
// omission. Every caller of this is the publisher task, and that task is also the only creator and
// destroyer of g_client -- so there is no moment at which the handle can be freed underneath the
// call. Until 2026-09-05 there was a lock here, held across esp_mqtt_client_publish, and it was one
// half of a deadlock with esp-mqtt's API lock. DO NOT call it from any other task; that, and not a
// mutex, is what keeps this safe.
//
// IT STOPPED BEING static ON 2026-09-07 and gained a comfoair_mqtt_ prefix with it, when the
// retraction moved to a file of its own for line budget. What used to guard it -- being invisible
// past this translation unit -- is now comfoair_mqtt_internal.h, which is not in include/ and
// cannot be reached from outside this component. The task rule above is unchanged and is the one
// that matters.
bool comfoair_mqtt_publish_one(const char *topic, const char *payload, int qos, bool retain)
{
    if (!g_connected || g_client == NULL)
        return false;
    // Length 0 makes esp-mqtt take it from the string (mqtt_client.c:2202-2204).
    const bool ok = esp_mqtt_client_publish(g_client, topic, payload, 0, qos, retain ? 1 : 0) >= 0;
    if (ok)
        bump(&g_published);
    return ok;
}

// Why a publish produced what it did, because the answers are acted on differently. SENT and SAME
// clear the change mark (the broker holds the current value); HELD keeps it and asks for another
// pass (a rate-limited change, comfoair_publish_rate.h); NOTHING keeps it without asking (nothing
// renders, and the next pass would find the same nothing); FAILED keeps it and asks (a dropped link
// or a timed-out write). The order is load-bearing for publish_fan(), which keeps the worst result
// of its three topics: a dropped link must outrank a value that would not render. DO NOT reorder.
typedef enum {
    PUBLISH_SENT = 0,
    PUBLISH_SAME,
    PUBLISH_HELD,
    PUBLISH_NOTHING,
    PUBLISH_FAILED,
} publish_result_t;

// The fan is three topics, and it reports the worst of them. Its change mark used to be cleared
// unconditionally, so a link dropping mid-sweep lost the fan's state until its level or preset
// next moved -- every other entity already cleared only on success.
static publish_result_t publish_fan(void)
{
    comfoair_fan_state_t fan = {0};
    // No early return for a fan PDO 65 has not reported: each renderer answers `None` for it, so a
    // fan state retained before a reboot does not survive the first connection.
    comfoair_state_get_fan(&fan);

    // ONE RENDERER PER RETAINED LEAF, INDEXED BY THE LEAF ITSELF. This used to be an array of
    // {"state", "speed/state", "preset/state"} -- a second copy of the list in
    // comfoair_ha_cleanup.c, and the reason a fourth leaf added here would have been erased by
    // nothing and caught by nothing. The names now come from comfoair_ha_fan_topic(); what is
    // left here is the mapping leaf -> payload, which is genuinely this file's. The
    // _Static_assert is the join: a retained leaf added to the enum stops this compiling until a
    // renderer exists, rather than being published with whatever payload sat at that index.
    static size_t (*const render[])(const comfoair_fan_state_t *, char *, size_t) = {
        [COMFOAIR_HA_FAN_STATE]        = comfoair_ha_fan_state_payload,
        [COMFOAIR_HA_FAN_SPEED_STATE]  = comfoair_ha_fan_speed_payload,
        [COMFOAIR_HA_FAN_PRESET_STATE] = comfoair_ha_fan_preset_payload,
    };
    _Static_assert(sizeof render / sizeof render[0] == COMFOAIR_HA_FAN_RETAINED_COUNT,
                   "a retained fan leaf has no payload renderer, or a renderer has no leaf");
    publish_result_t worst = PUBLISH_SENT;
    for (size_t i = 0; i < (size_t)COMFOAIR_HA_FAN_RETAINED_COUNT; i++) {
        if (comfoair_ha_fan_topic(&g_ctx, (comfoair_ha_fan_leaf_t)i, g_topic, sizeof g_topic) == 0
            || render[i](&fan, g_payload, sizeof g_payload) == 0) {
            if (worst < PUBLISH_NOTHING)
                worst = PUBLISH_NOTHING;
            continue;
        }
        if (!comfoair_mqtt_publish_one(g_topic, g_payload, QOS_STATE, true))
            worst = PUBLISH_FAILED;
    }
    return worst;
}

// RETAINED FOR EVERY ENTITY BUT ONE, `data_freshness`, and the exception is forced
// rather than preferred: `expire_after` and a retained state topic CANCEL EACH OTHER OUT. Home
// Assistant restarts the expiry timer on every message the state topic delivers and never asks
// whether the message was retained -- binary_sensor.py:171-183 of the 2026.9 install vendored in
// ../zehnder-comfoair-q-ha resets `_expiration_trigger` unconditionally, and `msg.retain` is read
// nowhere in binary_sensor.py, entity.py or models.py. A retained `ON` therefore buys a DEAD
// device fifteen fresh minutes of "Connected" every time Home Assistant resubscribes -- its own
// restart, a reload of the MQTT integration, a broker link that blinked -- and a link that blinks
// oftener than once a quarter of an hour buys them for ever. That is the exact failure this entity
// was raised against, produced by the entity itself.
//
// WHAT IT COSTS, and it is more than "a fresh install waits five minutes". HA does restore an
// expire_after entity across a restart (RestoreEntity, binary_sensor.py:94-117), but it measures
// the remainder from `last_state.last_changed` -- when the state STRING last moved, not when the
// last message arrived, because core.py:2457 keeps the old `last_changed` for an unchanged state.
// A healthy device holds this entity at `ON` for weeks, so the remainder is negative, the recovery
// is skipped, and the entity comes back UNAVAILABLE until the next tick: up to
// COMFOAIR_FRESHNESS_PERIOD_S after EVERY restart of Home Assistant, not only the first. Accepted:
// "nobody has said anything yet" is the honest answer, and it is the answer this entity exists to
// be able to give.
//
// DO NOT widen this to the other entities: their retained payload is what a reconnecting Home
// Assistant is handed instead of an empty device, and the availability topics are retained for a
// reason of their own. This is the ONE divergence in the tree, and test_ha_availability reads this
// file to keep it one.
static bool state_retained(const comfoair_entity_t *entity)
{
    return entity->kind != COMFOAIR_KIND_FRESHNESS;
}

// What THIS CONNECTION last put on the broker for each entity, for the rate rule; indexed like
// COMFOAIR_ENTITIES. ZEROED ON EVERY CONNECT (comfoair_mqtt_republish_everything()): a new session
// may meet a broker whose retained copy is stale or gone, so its first publication of every row is
// a SEND whatever the last session sent. Publisher task only, like every buffer in this file.
static comfoair_publish_rate_slot_t s_rate[COMFOAIR_ENTITY_COUNT];

// The text of the value being published, copied under the state lock (comfoair_state_get_copy()).
// DO NOT render from the model's own text pointer: the payload is built after the lock is released,
// and two writes to a double-buffered text (PDO 230, the active errors) inside that window would
// hand the renderer a string being rewritten. Publisher task only.
static char s_text[COMFOAIR_STATE_TEXT_MAX + 1];

static uint32_t now_ms(void)
{
    return (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
}

// One entity's state, under the rate rule. A reading the model does not have renders `None`
// (comfoair_ha_state_payload()), so PUBLISH_NOTHING is left for a render or a topic that does not
// fit -- a defect, and not worth a retry. The deadband is compared on the model's number, which is
// already the PUBLISHED value (the raw reading over the row's divider), the unit the table states.
//
// RECORDED ONLY AFTER THE CLIENT TOOK THE MESSAGE. A slot recording what the broker never got would
// answer SAME to that value for ever after. QOS_STATE is 0, so "took" is the socket write; a message
// lost after it is lost to a dead link, and a dead link ends in a new connection, which zeroes the
// slots and republishes everything -- so no slot outlives a message the broker did not get.
static publish_result_t publish_entity(size_t index)
{
    const comfoair_entity_t *entity = &COMFOAIR_ENTITIES[index];
    if (entity->kind == COMFOAIR_KIND_FAN)
        return publish_fan();

    comfoair_value_t value = {0};
    if (!comfoair_state_get_copy(entity->key, &value, s_text, sizeof s_text))
        return PUBLISH_NOTHING;
    const uint32_t now = now_ms();
    switch (comfoair_publish_rate_decide(entity, &value, &s_rate[index], now)) {
    case COMFOAIR_PUBLISH_HOLD:
        return PUBLISH_HELD;
    case COMFOAIR_PUBLISH_SAME:
        return PUBLISH_SAME;
    case COMFOAIR_PUBLISH_SEND:
        break;
    }
    bool skip = true;
    if (comfoair_ha_state_payload(entity, &value, &skip, g_payload, sizeof g_payload) == 0 || skip)
        return PUBLISH_NOTHING;
    if (comfoair_ha_state_topic(&g_ctx, entity->key, g_topic, sizeof g_topic) == 0)
        return PUBLISH_NOTHING;
    if (!comfoair_mqtt_publish_one(g_topic, g_payload, QOS_STATE, state_retained(entity)))
        return PUBLISH_FAILED;
    comfoair_publish_rate_record(&s_rate[index], &value, now);
    return PUBLISH_SENT;
}

// THE ONE PUBLICATION ON A CLOCK IN THIS FIRMWARE -- the data-freshness entity. Its contract, its
// cost and what this entity does NOT do are on the declaration in comfoair_mqtt_internal.h;
// everything else here goes out because something MOVED, and this goes out because time passed.
// THROUGH publish_entity(), NOT BESIDE IT: this is an ordinary registry row, so its topic and its
// payload are every other entity's and there is ONE renderer -- rendering "ON" here to save the
// lookup would be the second copy. Two things about it are unusual and both live elsewhere: the
// schedule is below, and the RETAINED FLAG IS NOT SET -- state_retained() owns that and says why.
// The comparison is a WRAP-SAFE SUBTRACTION: the counter wraps after 49.7 days, and
// `now < deadline` would then stop publishing for the rest of that device's life.
static uint32_t s_freshness_ms;
static bool     s_freshness_sent;

void comfoair_mqtt_publish_freshness(void)
{
    const uint32_t now = now_ms();
    if (s_freshness_sent && (now - s_freshness_ms) < COMFOAIR_FRESHNESS_PERIOD_S * 1000U)
        return;
    // ONLY ON SUCCESS, for the reason comfoair_mqtt_bus.c gives: a publish refused by a link that
    // just dropped must not restart the five minutes, or HA expires a live device's entity.
    // The INDEX in this file's own copy of the registry. DO NOT subtract the pointer
    // comfoair_entity_by_key() returns: registry_generated.h defines the table `static const`, so
    // that pointer is into comfoair_state.cpp's copy (comfoair_state_internal.h), not this one. The
    // row carries no rate policy, so the rule always answers SEND; the schedule above is its own.
    size_t index = COMFOAIR_ENTITY_COUNT;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT; i++)
        if (strcmp(COMFOAIR_ENTITIES[i].key, COMFOAIR_FRESHNESS_KEY) == 0)
            index = i;
    if (index == COMFOAIR_ENTITY_COUNT || publish_entity(index) != PUBLISH_SENT)
        return;
    s_freshness_ms   = now;
    s_freshness_sent = true;
}

// The mark for one entity, taken only if nothing changed it while it was being published. The
// generation is sampled BEFORE publish_entity, which is where the value is read: a PDO applied by
// the CAN task in between moves it, the conditional clear is then refused, the mark survives and
// the newer value goes out on the next pass. The clear used to be unconditional and erased that
// mark; for the durations, which nothing rebroadcasts, the retained payload on the broker then
// stayed wrong until the owner moved the value again (the RMI-read selects until the next 6-hourly
// read). comfoair_state.h carries the full argument.
//
// DO NOT sample the generation inside publish_entity: the read of the value has to be INSIDE the
// window this measures, and moving the sample after it would leave exactly the gap it closes.
static publish_result_t publish_and_clear(size_t index)
{
    const uint32_t       seen   = comfoair_state_change_generation(COMFOAIR_STATE_CONSUMER_MQTT, index);
    const publish_result_t result = publish_entity(index);
    // SAME clears too: the broker already holds exactly this value, so the mark has nothing left
    // to say. HELD must NOT clear -- the held change is still owed, and the mark is what owes it.
    if (result == PUBLISH_SENT || result == PUBLISH_SAME)
        comfoair_state_clear_change_if(COMFOAIR_STATE_CONSUMER_MQTT, index, seen);
    return result;
}

// Declared in comfoair_mqtt_internal.h, which documents the task context both of these demand.
void comfoair_mqtt_publish_changed(void)
{
    // Cleared BEFORE the pass. Clearing it afterwards discarded a mark another task set while the
    // pass was running -- the entity then waited for the next change or the next tick.
    g_dirty        = false;
    // FIRST, and it is why comfoair_mqtt_note_bus() wakes this task at all: a verdict that arrived
    // while the pass was being scheduled has to reach the broker before the readings it makes
    // stale. It sends nothing when the verdict has not moved.
    comfoair_mqtt_publish_bus(false);
    size_t sent    = 0;
    bool   again   = false;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT && g_connected; i++) {
        if (!comfoair_state_changed(COMFOAIR_STATE_CONSUMER_MQTT, i))
            continue;
        if (comfoair_ha_component(&COMFOAIR_ENTITIES[i]) == NULL) {
            // Unconditional, and correct here: nothing was read and nothing will be published, so
            // there is no window in which a change could be lost.
            comfoair_state_clear_change(COMFOAIR_STATE_CONSUMER_MQTT, i);
            continue;
        }
        // Per entity, and only after the publish. MQTT sends one at a time, so a link that drops
        // after three of them must not cost the rest their marks.
        const publish_result_t result = publish_and_clear(i);
        if (result == PUBLISH_FAILED || result == PUBLISH_HELD)
            again = true;
        // Only what reached the client counts toward the burst bound: a SAME or a HELD sent nothing.
        if (result != PUBLISH_SENT && result != PUBLISH_FAILED)
            continue;
        if (++sent >= PUBLISH_PER_PASS) {
            g_dirty = true;  // the rest next pass
            return;
        }
    }
    // A publish can fail without the connection dropping -- a socket write that times out. The
    // mark is still set, but g_dirty was cleared at the top of this pass, so without this the
    // entity waited for the next state event; on a silent CAN bus that is unbounded. HELD asks for
    // the next pass for the same reason: a held change must be re-asked when its interval or the
    // max hold runs out, and with no new frame nothing else would wake us -- the cost is one walk of
    // the marks per TICK_MS while anything is held, and no message. PUBLISH_NOTHING does not ask: it
    // would ask on every pass forever. DO NOT retry inside the loop -- this task must get back to
    // its queue.
    if (again)
        g_dirty = true;
}

void comfoair_mqtt_republish_everything(void)
{
    // The birth message, from HERE and not from the event handler in comfoair_mqtt_client.c. Every
    // buffer in this file belongs to the publisher task.
    if (comfoair_ha_meta_topic(&g_ctx, COMFOAIR_HA_META_STATUS, g_topic, sizeof g_topic) > 0)
        comfoair_mqtt_publish_one(g_topic, COMFOAIR_HA_PAYLOAD_ONLINE, QOS_META, true);
    // THE BUS VERDICT NEXT, unconditionally -- the forget is what makes it unconditional. This
    // broker holds nothing of ours: it may be a different broker, or the same one having lost its
    // retained store, and either way the last thing we told the previous connection means nothing
    // here. AND BEFORE publish_discovery(), because the config documents are what make Home
    // Assistant subscribe to this topic, and a retained message is delivered on subscription --
    // so by the time HA reads a stat_t it already has the availability that governs it. DO NOT
    // move it after the states: that reopens the window in which a dead bus reads as live.
    comfoair_mqtt_publish_bus(true);
    // EVERY RATE SLOT FORGOTTEN, for the reason the bus verdict above is: what the last connection
    // sent says nothing about this broker. With the slots empty the rule answers SEND for every row,
    // so the sweep below is the full republication it has always been. DO NOT keep the slots "to
    // save the burst": a held reading would then answer SAME against a retained copy that is gone.
    memset(s_rate, 0, sizeof s_rate);
    comfoair_mqtt_subscribe_all();
    comfoair_mqtt_publish_discovery();
    size_t sent = 0;
    for (size_t i = 0; i < COMFOAIR_ENTITY_COUNT && g_connected; i++) {
        if (comfoair_ha_component(&COMFOAIR_ENTITIES[i]) == NULL)
            continue;
        if (publish_and_clear(i) != PUBLISH_SENT)
            continue;
        // Paced by what was SENT, exactly as publish_discovery above paces itself. Pacing by the
        // loop index counted entities with no component and publishes that never happened, so the
        // pauses landed wherever those happened to fall rather than after every eighth message.
        if ((++sent % PUBLISH_PER_PASS) == 0)
            vTaskDelay(pdMS_TO_TICKS(20));
    }
    // The sweep above sent the freshness entity too, so its clock restarts here rather than
    // repeating the message a second after every connect.
    s_freshness_ms   = now_ms();
    s_freshness_sent = true;
    ESP_LOGI(TAG, "broker connected; state republished");
}
