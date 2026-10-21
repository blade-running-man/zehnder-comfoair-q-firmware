// HOW HOME ASSISTANT COMES TO KNOW THIS DEVICE EXISTS, and how often it has to be told.
//
// Two things happen once per connection and nothing else in this component does: the retained
// config documents go out, and the three command patterns are subscribed to. They are here
// together because the rule below is stated in terms of both -- one of them is now sent once per
// IDENTITY rather than once per connection, and the other must stay once per connection, and a
// reader who cannot see both at once cannot check that.
//
// A FILE OF ITS OWN (one of the nine CMakeLists.txt lists), for the reason every split in this
// component has had and no other: the 350-line ceiling (AGENTS.md). comfoair_mqtt_publish.c was at
// 348 when the discovery gate asked it for a flag, a discriminator and two outcomes. No behaviour moved with the split; what CHANGED is
// stated below.
//
// THE RULE. The discovery tree is COMFOAIR_ENTITY_COUNT config documents -- the largest burst this
// device sends; HV-HA-03 measures it on a broker -- once republished on every reconnection. No longer:
// Home Assistant reads config documents from the RETAINED store, so a broker still holding them is
// owed nothing. THE THREE CONDITIONS THAT SAY OTHERWISE, AND WHY THEY ARE NOT AN FNV FINGERPRINT
// OF THE SIX FIELDS, are in comfoair_discovery_gate.h -- in comfoair_mqtt_backoff, because this
// file is not built by [env:native] and a condition living here is checked by NOTHING. That is
// not a hypothetical: the retraction ceiling lived in this component until 2026-09-07 and deleting
// it altogether left every host case green.
//
// DO NOT PUT subscribe_all() UNDER THE SAME GATE. It is the obvious next economy and it would
// take the owner's commands away: a subscription lives in the broker's SESSION, this client is
// built with a clean one (comfoair_mqtt_client.c sets no disable_clean_session), so the broker
// forgets all three patterns on every disconnect. A gated subscribe is a device that publishes
// perfectly and answers nothing from the second connection onwards -- with no error anywhere,
// because SUBSCRIBE is the message that would not have been sent.
//
// ONE TASK, AS EVERYWHERE ELSE HERE: everything below runs on the publisher task, including
// comfoair_mqtt_note_link(), which is called from the ladder's pass in comfoair_mqtt_retry.c. That
// is what makes s_gate a plain static with no atomic and no lock.
#include "comfoair_mqtt_internal.h"

#include "comfoair_discovery_gate.h"
#include "comfoair_ha.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

// The gate's single bool, plus the one bit its third rule cannot be written without; the header
// next door says why they are one struct. s_ AND NOT g_: only this file reads or writes it, and in
// this codebase the prefix is what tells a reader how far a write can be felt
// (comfoair_mqtt_internal.h). It was first sketched as `g_discovery_published`, a bare bool in
// the internal header; it is reached through the three functions below instead, so
// the extern would have been an extern for nobody.
static comfoair_discovery_gate_t s_gate;

// RULE TWO OF THREE. Declared in comfoair_mqtt_internal.h, which states the task context.
void comfoair_mqtt_discovery_rebuilt(void)
{
    s_gate = comfoair_discovery_gate(s_gate, COMFOAIR_DISCOVERY_REBUILT, false, false);
}

// RULE THREE OF THREE, one call per pass of the publisher task.
//
// IT IS CALLED FROM comfoair_mqtt_retry.c AND THAT IS WHERE THE ADDRESS IS, which is the whole
// reason for the shape: comfoair_net_ip_string() is read there once per pass already, and reading
// it a second time here would be a second read of two statics the Wi-Fi event task writes with no
// synchronisation of any kind. The ladder also answers `has_address` as true whenever the link is
// up -- correct for this rule as well, because a pass that sees a connection is not an edge and
// the address cannot change its answer.
void comfoair_mqtt_note_link(bool connected, bool has_address)
{
    s_gate = comfoair_discovery_gate(s_gate, COMFOAIR_DISCOVERY_PASS, connected, has_address);
}

// The retained config documents. Published only when the owner asked for discovery -- a device
// nobody has adopted should not leave a retained document per entity under homeassistant/# on
// somebody's broker -- and only when the gate says this broker has not been told already.
void comfoair_mqtt_publish_discovery(void)
{
    if (!g_discovery || !comfoair_discovery_due(s_gate))
        return;
    size_t sent = 0, i = 0;
    // A PUBLISH THAT WAS REFUSED IS NOT A SWEEP THAT HAPPENED. esp-mqtt takes a QoS 1 publish even
    // with the session gone, so this is false only for a dropped link or a socket write that timed
    // out -- and half a discovery tree left in place for ever is the one outcome worse than
    // sending the whole of it twice.
    bool refused = false;
    for (; i < COMFOAIR_ENTITY_COUNT && g_connected; i++) {
        const comfoair_entity_t *e = &COMFOAIR_ENTITIES[i];
        if (comfoair_ha_component(e) == NULL)
            continue;
        if (comfoair_ha_discovery_topic(&g_ctx, e, g_topic, sizeof g_topic) == 0 ||
            comfoair_ha_discovery_payload(&g_ctx, e, g_payload, sizeof g_payload) == 0) {
            // The KEY, never the payload. test_ha pins the worst case below BUFFER_SIZE, so this
            // is a defect to see rather than a condition to expect.
            //
            // AND IT DOES NOT HOLD THE FLAG DOWN, which is the same distinction publish_changed()
            // draws between PUBLISH_NOTHING and PUBLISH_FAILED: a document that does not fit will
            // not fit on the next connection either, so counting it as an unfinished sweep would
            // resend every other document for the life of the device.
            ESP_LOGE(TAG, "discovery for '%s' did not fit; it will not appear", e->key);
            continue;
        }
        if (!comfoair_mqtt_publish_one(g_topic, g_payload, QOS_META, true)) {
            refused = true;
            continue;
        }
        // Paced by what was SENT, the rule the whole component pages by: a tree of retained
        // publishes back to back is a burst, and this task must not stop looking at anything else
        // for its duration.
        if ((++sent % PUBLISH_PER_PASS) == 0)
            vTaskDelay(pdMS_TO_TICKS(20));
    }

    // "AFTER A SUCCESSFUL PASS", spelled out, because the flag is what stops the next connection
    // trying again: the loop reached the end of the registry, the link was still up when it got
    // there, and nothing was refused on the way. A sweep cut short at entity thirty leaves Home
    // Assistant holding a device with a third of its entities, and remembering that as done would
    // leave it that way until the owner next pressed Save.
    if (i < COMFOAIR_ENTITY_COUNT || !g_connected || refused) {
        ESP_LOGW(TAG, "discovery stopped after %u entities; it will be published again",
                 (unsigned)sent);
        return;
    }
    s_gate = comfoair_discovery_gate(s_gate, COMFOAIR_DISCOVERY_SENT, true, true);
    // THE LINE HARDWARE SCENARIOS HV-HA-08..12 READ (hardware-verification/06b-home-assistant.md).
    // Its absence after a router reboot and its presence after Save are the only observation of
    // these three rules that exists, because nothing on this side of the component is reachable
    // from a host suite. DO NOT reword it.
    ESP_LOGI(TAG, "published discovery for %u entities", (unsigned)sent);
}

void comfoair_mqtt_subscribe_all(void)
{
    // Three patterns, and no more: this device hears only its own tree. comfoair_config_check_prefix
    // already refuses `+`, `#` and `$` in a stored prefix, so a wildcard cannot reach here.
    //
    // A FOURTH PATTERN IS WHAT THE DISCOVERY GATE AVOIDS. Subscribing to homeassistant/status
    // would tell us when Home Assistant restarted and is the answer other projects take; it also
    // reaches into somebody else's tree, which needs a second rule in the broker's ACL -- and a
    // device whose ACL refused exactly that one would report its subscriptions as refused and
    // warn the owner that commands will not arrive, while every command worked. The third rule of
    // comfoair_discovery_gate.h covers the case this would have covered, from inside our own tree.
    //
    // No lock, for the reason comfoair_mqtt_publish_one() states: this runs on the publisher task,
    // which is the only task that creates or destroys g_client.
    char pattern[192];
    if (g_client == NULL)
        return;
    if (comfoair_ha_command_topic(&g_ctx, "+", pattern, sizeof pattern) > 0)
        esp_mqtt_client_subscribe(g_client, pattern, QOS_META);
    if (comfoair_ha_fan_topic(&g_ctx, COMFOAIR_HA_FAN_ANY_COMMAND, pattern, sizeof pattern) > 0)
        esp_mqtt_client_subscribe(g_client, pattern, QOS_META);
    if (comfoair_ha_op_topic(&g_ctx, "+", pattern, sizeof pattern) > 0)
        esp_mqtt_client_subscribe(g_client, pattern, QOS_META);
}
