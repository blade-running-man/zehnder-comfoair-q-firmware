// The acting half of an inbound command: what comfoair_mqtt_decide()'s verdict makes happen, on
// the publisher task. Read comfoair_mqtt.h first; the component's shape is in README.md,
// Implementation.
#include "comfoair_command.h"
#include "comfoair_control.h"
#include "comfoair_mqtt_inbound.h"
#include "comfoair_mqtt_internal.h"
#include "comfoair_state.h"
#include "esp_log.h"

static const char *TAG = "mqtt";

// --- commands, all of this on the publisher task ----------------------------------------------

static void reject(const char *why)
{
    bump(&g_rejected);
    // The reason, never the payload: a command payload carries whatever somebody published, and
    // the log ring is served by GET /api/log.
    ESP_LOGW(TAG, "command refused: %s", why);
}

static void accepted(void)
{
    bump(&g_commands);
}

// --- what an arriving command means, and what follows from it ---------------------------------

// The DECIDING half lives in comfoair_mqtt_inbound.c, where test/test_mqtt_inbound reaches it
// without a broker; this is the acting half and nothing else. TASK CONTEXT: the publisher task,
// which is the only task in this component allowed to enter comfoair_control, flash or the state
// model.
//
// THE ORDER OF THE CONSEQUENCES IS NOT A DETAIL. Both orders below were counter-review fixes:
//
//  * SUBMIT FIRST, then publish optimistically -- the order the HTTP handler uses. The reverse
//    showed Home Assistant a mode that was never sent whenever the bus was busy.
//  * STORE, CHECK THE RESULT, and only then set_number. It is the same condition the HTTP twin
//    answers 503 for: ESP_ERR_INVALID_STATE means the sender never started -- which happens on
//    every boot where the CAN bus did not come up -- so nothing was stored and nothing will be
//    used. Reporting it as accepted and publishing the new value to Home Assistant would show the
//    owner a duration the device does not have.
//
// DO NOT move either decision back in here. Everything above the switch is a fact this component
// is the only one that can fetch; everything below it is an effect no host test can observe.
void comfoair_mqtt_handle_inbound(const comfoair_mqtt_inbound_t *in)
{
    comfoair_command_durations_t durations;
    comfoair_control_durations(&durations);
    // NULL rather than a zeroed struct when the model has nothing: that is the same condition the
    // decider's `known && on` rule used to be written as here, and it now belongs to the decider.
    comfoair_fan_state_t        fan = {0};
    const comfoair_fan_state_t *reported = comfoair_state_get_fan(&fan) ? &fan : NULL;

    const comfoair_mqtt_decision_t d = comfoair_mqtt_decide(&g_ctx, in, &durations, reported);

    switch (d.kind) {
    case COMFOAIR_MQTT_IGNORE:
        // Not ours. Silently, and NOT through reject() -- COMFOAIR_MQTT_IGNORE in
        // comfoair_mqtt_inbound.h carries the reason, and it is the counter GET /api/status
        // publishes that is at stake here.
        return;

    case COMFOAIR_MQTT_REJECT:
        reject(d.reason);
        return;

    case COMFOAIR_MQTT_STORE: {
        const esp_err_t stored =
            comfoair_control_store_duration(d.command.duration_key, d.command.duration_minutes);
        if (stored != ESP_OK) {
            reject(stored == ESP_ERR_INVALID_STATE ? "commands are not running"
                                                   : "the duration was not stored");
            return;
        }
        comfoair_state_set_number(d.command.duration_key, (float)d.command.duration_minutes);
        if (g_observer != NULL)
            g_observer();
        accepted();
        g_dirty = true;
        return;
    }

    case COMFOAIR_MQTT_SUBMIT: {
        const esp_err_t err = comfoair_control_submit(&d.command);
        if (err != ESP_OK) {
            reject(err == ESP_ERR_NO_MEM ? "the bus is busy" : "commands are not running");
            return;
        }
        accepted();
        // Optimistic for every select label, as over REST (PDO-backed ones are corrected within
        // seconds); silently declined for a label the registry does not list under that entity.
        if (d.optimistic_text && comfoair_state_set_option(d.key, d.text)) {
            if (g_observer != NULL)
                g_observer();
            g_dirty = true;
        }
        return;
    }
    }
}
