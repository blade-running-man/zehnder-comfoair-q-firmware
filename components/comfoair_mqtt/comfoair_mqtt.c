// The spine of the component: the publisher task, the observer, the statistics and the public
// entry points. What an arriving command's decision makes happen is in comfoair_mqtt_command.c.
//
// See components/comfoair_mqtt/README.md, "The files and the one-task rule", for the shape of this
// component -- which of the nine files owns what, and the finding that put the client's whole
// lifetime on one task; comfoair_mqtt.h has the rules.
#include "comfoair_mqtt.h"

#include "comfoair_ha.h"
#include "comfoair_mqtt_inbound.h"
#include "comfoair_mqtt_internal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "mqtt";

// How often the publisher looks for work when nothing has woken it.
#define TICK_MS 1000

// Inbound commands waiting for the publisher task. Eight is more than a person produces; a burst
// beyond it is a script, and dropping it is the honest answer -- the alternative is a queue that
// applies a command to a ventilation unit a minute after it was sent.
#define INBOX_LEN 8

// Declared in comfoair_mqtt_internal.h, which says who else may read them.
TaskHandle_t  g_task;
QueueHandle_t g_inbox;
volatile bool g_dirty;

// ATOMICS, NOT A STRUCT UNDER THE LOCK, and this is not a micro-optimisation. This is the one
// statement of the reason; the two other places that had to know -- the event handler and
// comfoair_mqtt_stop_client() -- point here. `rejected` and
// `reconnects` are incremented on the esp-mqtt task, which runs the event handler SYNCHRONOUSLY
// FROM INSIDE ITS OWN API LOCK (mqtt_client.c:1661). A lock here would mean the order
// "API lock -> our lock", while publish() in comfoair_mqtt_publish.c took the two in the opposite
// order -- textbook ABBA, and it hung MQTT together with the single web-server task. Past tense:
// that lock was deleted on 2026-09-05 and the client belongs to one task instead. DO NOT put a
// mutex back here -- it would rebuild the inversion these atomics exist to make unnecessary.
// Relaxed ordering is enough: no invariant holds between the counters, and the whole requirement
// is that no increment is lost. A plain `++` is a load, an add and a store that the other task
// can interleave with; tearing is NOT the hazard, because an aligned 32-bit load or store on RV32
// is a single instruction and cannot tear.
//
// All of them are shared. g_commands joined them when the acting half of an inbound command moved
// to comfoair_mqtt_command.c: that file moves it, this one reports it.
_Atomic uint32_t g_published;
_Atomic uint32_t g_rejected;
_Atomic uint32_t g_reconnects;
_Atomic uint32_t g_last_connack;
// THE PAIR IS THE DIAGNOSIS, which is why this is defined on the line under the counter it is read
// against and not beside the handler that moves it: a second device holding our client id is
// observable ONLY as these two climbing together while `connected` flickers.
// It counts a dial that never got through as well -- DISCONNECTED is the one event a timed-out
// esp_mqtt_connect produces, and ten hours of an unreachable broker must move it, or "trying and
// failing" reads the same as "the task is dead".
_Atomic uint32_t g_disconnects;
// The socket errno of the LAST transport failure: ECONNREFUSED, ETIMEDOUT and EHOSTUNREACH ask the
// owner for three different actions and the class `tcp` collapses all three.
// Cleared in comfoair_mqtt_stop_client(), with g_last_connack, for the reason recorded there.
_Atomic uint32_t g_last_transport_errno;
_Atomic uint32_t g_commands;

// A request to rebuild the client, posted by a FOREIGN task. Everything the httpd task does on
// POST /api/config is two writes and a notify; the stop, the re-read of the settings, the rewrite
// of g_ctx and the start all happen on the publisher task.
//
// WHY THIS AND NOT "everything under one lock", which is what stood here until 2026-09-05: that
// version put the httpd task inside esp_mqtt_client_stop() while holding our lock, and the
// esp-mqtt event handler waited for that same lock from inside esp-mqtt's own API lock. Both
// waited for ever, and the single web-server task waited with them -- so the device kept
// ventilating and answered nothing. A flag has no order to invert.
static volatile bool s_reconfigure;
comfoair_mqtt_observer_t g_observer;

// --- the publisher ---------------------------------------------------------------------------

static void mqtt_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(TICK_MS));

        // Commands first, and drained whether or not the link is up: a command that arrived just
        // before a drop is still a command, and comfoair_control queues it independently.
        comfoair_mqtt_inbound_t in;
        while (g_inbox != NULL && xQueueReceive(g_inbox, &in, 0) == pdTRUE)
            comfoair_mqtt_handle_inbound(&in);

        // Rebuilding the client happens HERE and only here: between two passes, on its own task,
        // without a single lock, because the stop, the start and the rewrite of g_ctx all belong
        // to this body. AFTER the drain above, and that is deliberate -- every message queued
        // before this pass was received by the outgoing client, so draining first parses each one
        // against the g_ctx it arrived under. Rebuilding first would parse them against the new
        // prefix, where they resolve to no target and vanish without a word: the same shape of
        // defect as the one this ownership change removes. The drain above does not cover
        // everything, which is why there is a second one below, after the client is destroyed.
        //
        // THE PRICE OF THAT ORDER, so that whoever considers swapping these two blocks knows what
        // they would be trading: a command from the OUTGOING broker is applied after the owner has
        // already pointed the device at a different one. That is the better half -- the command was
        // accepted by the client the owner had authorised at the moment it arrived -- but it is a
        // trade, not a free win.
        if (s_reconfigure) {
            s_reconfigure = false;
            // HERE AND NOWHERE ELSE: one line down the client is destroyed and g_ctx rewritten
            // from the new settings, so nothing is left to publish into the OLD identity with.
            comfoair_ha_cleanup_t cleanup;
            if (comfoair_mqtt_pending_cleanup(&cleanup))
                comfoair_mqtt_retract(cleanup);
            comfoair_mqtt_stop_client();
            // AND AGAIN, because the drain above is not the whole of it. Between that loop ending
            // and this branch running, the outgoing client is still live and can enqueue one more
            // message; parsing it after the rewrite below would hit the new prefix, resolve to no
            // target, and drop it in silence. Here the client is destroyed -- so no further message
            // can arrive -- and g_ctx is still the one those messages were addressed to. Only after
            // this loop is the queue truly empty of the old broker's traffic.
            while (g_inbox != NULL && xQueueReceive(g_inbox, &in, 0) == pdTRUE)
                comfoair_mqtt_handle_inbound(&in);
            const esp_err_t err = comfoair_mqtt_start_client();
            // "No broker configured" is the ordinary outcome of clearing the host field, not a
            // failure.
            if (err != ESP_OK && err != ESP_ERR_NOT_FOUND)
                ESP_LOGE(TAG, "reconfigure failed: %s", esp_err_to_name(err));
            // UNCONDITIONALLY, including after that error and after ESP_ERR_NOT_FOUND: the owner
            // who cleared the host field is owed the same answer as the one who fixed it, and a
            // browser that polls applied_epoch would otherwise wait for ever. The ladder reset it
            // carries is deliberate -- he corrected something, so he must not sit out a
            // five-minute rung.
            comfoair_mqtt_retry_applied();
        }

        // THE LADDER, and it belongs to this task in its entirety. After the
        // reconfigure branch, so a pass never dials a client that is about to be destroyed, and
        // BEFORE the `!g_connected` early-out below, which is the whole reason it is here: every
        // line after that one is skipped exactly when there is a broker to reconnect to.
        comfoair_mqtt_retry_pass();

        if (!g_connected)
            continue;
        if (g_republish) {
            g_republish = false;
            comfoair_mqtt_republish_everything();
            continue;
        }
        comfoair_mqtt_publish_freshness();  // the one on a clock; contract in the internal header
        if (g_dirty)
            comfoair_mqtt_publish_changed();
    }
}

void comfoair_mqtt_set_observer(comfoair_mqtt_observer_t observer) { g_observer = observer; }

void comfoair_mqtt_notify_state_changed(void)
{
    g_dirty = true;
    if (g_task != NULL)
        xTaskNotifyGive(g_task);
}

// Read from ANY task and takes no locks: every counter is an atomic, and nobody ever promised --
// or needs -- exact consistency between them. The "the mutex does not exist yet" early-out this
// replaces is gone rather than lost: before comfoair_mqtt_start() every atomic, g_connected and
// g_client are still at their static zero, so a caller that arrives early reads exactly the zeroed
// struct that guard used to hand back. `connected` and `configured` are a best-effort snapshot and
// nothing more: both are read once and neither is dereferenced, so the worst a race costs is a
// flag one reconfigure out of date in a document that was only ever a report.
void comfoair_mqtt_get_stats(comfoair_mqtt_stats_t *out)
{
    if (out == NULL)
        return;
    // CLEARED FIRST, and not for tidiness: the caller declares this on its stack
    // (comfoair_http_status_get), and ONE member still has no producer -- the subscription counters
    // that would feed `subscriptions` have not been built -- so without this a stack remnant is
    // rendered into GET /api/status as a plausible number. Zero is what it MEANS while nothing
    // observes it. `disconnects` and `last_transport_errno` left that list on 2026-09-07 and are
    // read below.
    *out              = (comfoair_mqtt_stats_t){0};
    out->published    = atomic_load_explicit(&g_published, memory_order_relaxed);
    out->commands     = atomic_load_explicit(&g_commands, memory_order_relaxed);
    out->rejected     = atomic_load_explicit(&g_rejected, memory_order_relaxed);
    out->reconnects   = atomic_load_explicit(&g_reconnects, memory_order_relaxed);
    out->last_connack = atomic_load_explicit(&g_last_connack, memory_order_relaxed);
    // Read HERE and not from the ladder's snapshot below: both belong to the connection rather
    // than to the retry machine, which must not grow a second set of facts to carry.
    out->disconnects  = atomic_load_explicit(&g_disconnects, memory_order_relaxed);
    out->last_transport_errno =
        atomic_load_explicit(&g_last_transport_errno, memory_order_relaxed);
    out->connected    = g_connected;
    out->configured   = g_configured;
    // The ladder's five, from its own atomics and by the same rule: read once, never dereferenced.
    comfoair_mqtt_retry_stats(out);
}

// --- lifecycle -------------------------------------------------------------------------------

esp_err_t comfoair_mqtt_init(void)
{
    if (g_inbox != NULL)
        return ESP_OK;
    g_inbox = xQueueCreate(INBOX_LEN, sizeof(comfoair_mqtt_inbound_t));
    return g_inbox != NULL ? ESP_OK : ESP_ERR_NO_MEM;
}

// app_main, once. The first start is synchronous because there is no publisher task yet to contend
// with -- and THE ORDER BELOW IS THE POINT: the task is created only after the client's start has
// returned. Created first, it could be woken by a reconfigure and run comfoair_mqtt_stop_client()
// while this call was still inside esp_mqtt_client_init(), which is two tasks on one handle -- the
// very property this component was restructured to establish. It is created UNCONDITIONALLY,
// including after ESP_ERR_NOT_FOUND: a device that ships with no broker must still have somebody
// to serve the reconfigure that follows the owner's first save.
esp_err_t comfoair_mqtt_start(void)
{
    const esp_err_t init_err = comfoair_mqtt_init();
    if (init_err != ESP_OK)
        return init_err;

    // CLEARED BEFORE the start, not after, and the order is the whole of the reasoning. The web
    // server is up before this function runs (src/main.cpp: comfoair_http_start precedes
    // comfoair_mqtt_start), so a POST /api/config can already have set the flag -- and the start
    // below reads the very settings that save stored, which satisfies the request. Clearing it
    // AFTER the start would drop a save that landed while the client was being built: the flag
    // would be set for settings we never picked up, and we would erase it. Clearing it first
    // cannot lose anything -- a save arriving from here on re-sets the flag and the publisher's
    // first pass serves it -- and costs at worst one redundant restart.
    //
    // Leaving it set instead is not free: the first pass would stop and rebuild a client that
    // already has these settings, and if it had connected in that window the stop publishes a
    // retained `offline`, so Home Assistant sees the device flap on a boot where nothing was wrong.
    s_reconfigure = false;

    const esp_err_t err = comfoair_mqtt_start_client();

    if (g_task == NULL &&
        // 4096: it formats topics and payloads; the big buffers are static.
        xTaskCreate(mqtt_task, "comfoair_mqtt", 4096, NULL, 4, &g_task) != pdPASS) {
        g_task = NULL;
        ESP_LOGE(TAG, "publisher did not start");
        // Nothing in this component runs without it, so nothing in it keeps memory either: the
        // queue is 5.7 KB that no one would ever drain, on a device that has just failed to find
        // 4 KB for a stack. Releasing it also makes comfoair_mqtt_reconfigure() tell the truth --
        // but only because g_inbox is cleared BEFORE the queue is freed. The other order leaves a
        // window where httpd reads a non-NULL g_inbox, returns ESP_OK and records a request that
        // nobody will ever serve, which is the lie this is meant to prevent.
        comfoair_mqtt_stop_client();
        QueueHandle_t doomed = g_inbox;
        g_inbox              = NULL;
        vQueueDelete(doomed);
        return ESP_ERR_NO_MEM;
    }
    return err;
}

// THE HTTPD TASK, and it neither blocks nor enters esp-mqtt: it posts a request and wakes the
// publisher, which does stop -> re-read the settings -> rewrite g_ctx -> start between two of its
// passes. The HTTP response has already gone out by then (comfoair_http_config.c:226-227), so
// nothing changes for the browser, while the single web-server task stops stalling for seconds
// inside esp_mqtt_client_stop() on a socket that is timing out.
//
// IT DELIBERATELY DOES NOT COPY THE SETTINGS ACROSS THE TASK BOUNDARY. A slot holding a
// comfoair_net_broker_t would carry the password in the clear through one more static variable for
// as long as it sat there; the publisher task reads it itself through comfoair_net_broker() and
// wipes it itself, so there is no second copy to forget.
//
// g_task == NULL is NOT an error: the flag is level-triggered and read on every pass, and in the
// one window where the task legitimately does not exist yet -- between comfoair_http_start() and
// comfoair_mqtt_start() in src/main.cpp -- that start reads the settings this handler has already
// saved, so the request is honoured either way. Only a component that never initialised has nobody
// to serve it.
esp_err_t comfoair_mqtt_reconfigure(void)
{
    if (g_inbox == NULL)
        return ESP_ERR_INVALID_STATE;
    s_reconfigure = true;
    if (g_task != NULL)
        xTaskNotifyGive(g_task);
    return ESP_OK;
}
