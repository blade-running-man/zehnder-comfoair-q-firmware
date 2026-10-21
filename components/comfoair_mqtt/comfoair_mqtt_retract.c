// WHAT THIS DEVICE EMPTIES WHEN IT STOPS BEING THE THING IT WAS: one zero-length retained publish
// per topic of comfoair_ha_cleanup_topic(), under a budget, and nothing else.
//
// The two operations, the order inside each and why the orders differ are in comfoair_ha.h; the
// enumerator itself is comfoair_ha_cleanup.c, where a host test can reach it. What is left here
// is the half that needs a broker.
//
// A SIXTH FILE RATHER THAN A HUNDRED MORE LINES IN comfoair_mqtt_publish.c, and the reason is the
// one every split in this component has had: the 350-line ceiling (AGENTS.md). That file was at
// 349 when it was asked for a second availability topic, <prefix>/bus. The seam was already
// there as a section comment and is the same one comfoair_ha draws on its own side -- the
// publisher sends what the device IS, the retraction unsays it -- so nothing moved but text, and
// the public surface did not change at all.
//
// ONE TASK, AS EVERYWHERE ELSE HERE. comfoair_mqtt_retract() runs on the publisher task, in the
// reconfigure branch of mqtt_task, while the OUTGOING client is still connected and g_ctx still
// describes the outgoing identity. It borrows g_topic from that task's own set of buffers rather
// than growing a second one; comfoair_mqtt_internal.h carries the argument for why that is safe
// and what would make it unsafe.
#include "comfoair_mqtt_internal.h"

#include "comfoair_ha.h"
#include "comfoair_retract_budget.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

// How long the WHOLE retraction may take, and in what slices it checks.
//
// A BUDGET OVER THE WHOLE OPERATION, NOT OVER THE WAIT AT THE END. Until 2026-09-07 it bounded
// only the outbox drain, assuming the publish loop ahead of it was instantaneous. It is not: a
// QoS 1 esp_mqtt_client_publish() writes to the socket SYNCHRONOUSLY, through esp_mqtt_write() ->
// esp_transport_write(..., network_timeout_ms) (mqtt_client.c:730-733, :2237), whose default is
// ten seconds (:536-537). Against a broker in a black hole -- session established, then every
// packet dropped -- the first publish that did not fit the socket buffer blocked this task for
// those ten seconds BEFORE the drain was reached: about twelve seconds answering no command,
// which breaks the no-stall rule from inside the code that exists to honour it. So the deadline
// is taken before the first publish, both loops respect it, and cfg.network.timeout_ms is 2 s in
// comfoair_mqtt_start_client(); worst case is this budget plus one write already started, about
// 4.2 s, and hardware scenario HV-HA-18 (06b-home-assistant.md) measures it.
//
// THE NUMBER IS DERIVED, NOT CHOSEN. A full withdrawal is comfoair_ha_cleanup_count() messages
// (about two per entity); on a household-LAN broker that is tens of milliseconds, so two
// seconds is two orders of magnitude of slack. IT BLOCKS NOBODY: it runs on the publisher task
// and the HTTP response went out before the reconfigure was requested (comfoair_http_config.c:227).
//
// NOTHING DEGRADES BECAUSE A PEER IS ABSENT (AGENTS.md). A CEILING, never a
// condition to satisfy: with no broker the function returns before the first publish, and with
// one that stops acknowledging it gives up and lets the client be destroyed, messages unsent.
#define RETRACT_BUDGET_MS 2000
#define RETRACT_SLICE_MS  100

// How much of it has gone. UNSIGNED SUBTRACTION, and that is not style: TickType_t is uint32_t and
// CONFIG_FREERTOS_HZ is 100 (sdkconfig.m5stack-nanoc6:2497), so the counter turns over about every
// 497 days -- not the 49 this comment claimed until 2026-09-07, which was the 1 kHz default
// transcribed from somebody else's project. The number is wrong by a factor of ten and the
// argument is unchanged: `now >= started + budget` is false for the whole turn after it, which is
// a retraction with no ceiling at all on a device meant to run for months.
//
// THE COMPARISON ITSELF IS NOT HERE. It is comfoair_retract_continue()
// (components/comfoair_mqtt_backoff), because this file is not built by [env:native] and the
// ceiling that lived here was therefore checked by nothing: removing it altogether left every host
// case green. What is left in this macro is the reading of the clock, which no host suite can do.
#define RETRACT_ELAPSED(started) ((uint32_t)(xTaskGetTickCount() - (started)))

void comfoair_mqtt_retract(comfoair_ha_cleanup_t what)
{
    // The retraction is worth nothing to a broker we are not talking to, and the whole point of
    // checking here rather than inside the loop is that a disconnected device must not spend two
    // seconds finding that out.
    if (!g_connected || g_client == NULL)
        return;

    // BEFORE THE FIRST PUBLISH, because the first publish is what can block. See the budget's
    // comment: a QoS 1 publish writes to the socket synchronously, so the expensive part of a
    // retraction into a black hole is the loop below and not the drain after it.
    const TickType_t started = xTaskGetTickCount();

    const size_t total = comfoair_ha_cleanup_count(what);
    size_t       sent = 0, unfit = 0;
    // THE CEILING IS THE LOOP'S CONDITION AND NOT AN `if` INSIDE IT, which is what makes it
    // impossible to remove without removing the loop -- see comfoair_retract_budget.h for why that
    // shape was chosen over the `if (budget_spent()) break;` this held until 2026-09-07. It is
    // read BEFORE each publish rather than after, so at most ONE socket write can have started
    // past the deadline. Stopping leaves the rest of the tree on a broker we are being taken away
    // from: worse than a clean sweep, better than a publisher that answers nothing for twelve
    // seconds. The no-stall rule decides that trade, not a preference.
    size_t i = 0;
    for (; comfoair_retract_continue(i, total, RETRACT_ELAPSED(started),
                                     pdMS_TO_TICKS(RETRACT_BUDGET_MS)) &&
           g_connected;
         i++) {
        if (comfoair_ha_cleanup_topic(&g_ctx, what, i, g_topic, sizeof g_topic) == 0) {
            // Carry on rather than stop. A truncated topic is a different topic, so it is refused
            // -- but ending the sweep here would leave the whole rest of the tree on the broker for
            // ever, and there is no second pass that goes looking for orphans.
            unfit++;
            continue;
        }
        // AN EMPTY PAYLOAD, RETAINED, is the erasure (MQTT 3.1.1 section 3.3.1.3): the broker drops
        // the retained message and current subscribers get a zero-length one, which is how Home
        // Assistant removes the entity. QOS_META because retention makes nothing idempotent here --
        // a lost erasure is a topic that stays for ever, and there is no next connection to this
        // identity that would try again.
        //
        // NO `offline` IS PUBLISHED FROM HERE. comfoair_mqtt_stop_client() already writes it, on
        // the line after this call returns, and a second publisher of one fact is a second place
        // for it to be wrong.
        if (!comfoair_mqtt_publish_one(g_topic, "", QOS_META, true))
            continue;
        // Paced by what was SENT, the same rule publish_discovery() and republish_everything()
        // use: this task must not stop looking at anything else for the length of a full-withdrawal
        // burst.
        if ((++sent % PUBLISH_PER_PASS) == 0)
            vTaskDelay(pdMS_TO_TICKS(20));
    }

    // Then wait for esp-mqtt's outbox, because comfoair_mqtt_publish_one() returns the result of
    // QUEUEING and never an acknowledgement (see its comment in comfoair_mqtt_publish.c). The
    // client is about to be destroyed by the caller and esp_mqtt_client_destroy() takes the outbox
    // with it, so without this most of a retraction would be freed rather than sent. Checked BEFORE
    // each sleep, so an already-empty outbox costs nothing -- and ON WHAT IS LEFT OF THE SAME
    // BUDGET, not on a fresh two seconds of its own.
    //
    // -1 AND NOT 0, because 0 is an answer and this is the absence of one. If the link drops during
    // the drain the loop below never runs and the final re-read is skipped, and a 0 left here would
    // print as "the outbox emptied" over a retraction of which nothing left the device -- a false
    // NEGATIVE for HV-HA-18, which reads exactly this line to decide whether the ceiling
    // worked. Unknown is printed as unknown.
    //
    // `comfoair_retract_continue(0, 1, ...)` is the same ceiling asked with the work half held
    // open: index 0 of 1 is "there is still something to wait for", so the answer is the budget
    // alone. Written inline for the reason the loop above is -- a `budget_spent()` helper here is a
    // second statement, and a second statement can be deleted on its own.
    int outbox = -1;
    while (g_connected && g_client != NULL &&
           comfoair_retract_continue(0, 1, RETRACT_ELAPSED(started),
                                     pdMS_TO_TICKS(RETRACT_BUDGET_MS))) {
        outbox = esp_mqtt_client_get_outbox_size(g_client);
        if (outbox <= 0)
            break;
        vTaskDelay(pdMS_TO_TICKS(RETRACT_SLICE_MS));
    }
    if (g_connected && g_client != NULL)
        outbox = esp_mqtt_client_get_outbox_size(g_client);

    // QUEUED, NOT ACKNOWLEDGED, AND THE LINE NOW SAYS SO. `sent` counts what
    // esp_mqtt_client_publish() accepted, and at QoS 1 it accepts even with the session gone: it
    // enqueues and returns the message id (mqtt_client.c:2220-2229). So "N of N" was read as
    // "the broker has them" when it only meant "the client took them"; the elapsed time and the
    // outbox remainder carry the real answer, and HV-HA-18 reads those. Prefix, never creds.
    //
    // TWO REASONS TO STOP SHORT AND THEY ARE NOT THE SAME FINDING. `i < total` with the link still
    // up is the budget doing its job, which is what HV-HA-18 provokes; `i < total` with the link
    // gone is a broker that went away mid-sweep, which proves nothing about the ceiling. Naming
    // them apart is the difference between reading HV-HA-18 and guessing at it.
    const bool stopped_short = i < total;
    ESP_LOGI(TAG, "retraction: queued %u of %u under '%s' in %u ms%s%s%s%s", (unsigned)sent,
             (unsigned)total, g_ctx.topic_prefix != NULL ? g_ctx.topic_prefix : "",
             (unsigned)pdTICKS_TO_MS(RETRACT_ELAPSED(started)),
             unfit > 0 ? "; some did not fit" : "",
             stopped_short && g_connected ? "; GAVE UP on the budget" : "",
             stopped_short && !g_connected ? "; the link went before the sweep did" : "",
             outbox < 0 ? "; outbox unknown" : outbox > 0 ? "; outbox not drained" : "");
}
