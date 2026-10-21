// The retry ladder's device half, run once per pass of the publisher task.
//
// EVERYTHING THAT IS A DECISION IS NEXT DOOR, in comfoair_mqtt_backoff: which class a failure is,
// how long its rung is, when the counter resets, what word /api/status gets, how the handler's
// four facts are packed and what the jitter seed of a device id is. What is left here is what
// cannot leave the device: a clock, an address, one call into esp-mqtt and five atomics.
//
// THAT LINE MOVED ONCE ALREADY, AND IT MOVED BECAUSE OF A MUTATION. The FNV of the device id used
// to be a static function in this file; `return 0;` in its place passed the entire host suite and
// built a firmware in which every device in a house retries in lockstep -- the jitter that keeps
// devices waking together after a power cut from dialling in step, deleted in silence. Anything
// here that can be a pure function of its arguments belongs next door, where a test can reach it.
// That line is drawn where it is so the machine can be driven by test/test_mqtt_backoff on the
// host; this file is checked on hardware (hardware-verification/06-mqtt.md) and by nothing else.
//
// ONE TASK OWNS ALL OF IT. s_ladder is a plain static with no atomic and no lock, exactly as g_ctx
// and the two formatting buffers are (comfoair_mqtt_internal.h), and that is sound for one reason
// only: comfoair_mqtt_retry_pass() and comfoair_mqtt_retry_applied() are called from the publisher
// task and from nowhere else. DO NOT call either from httpd, from the event handler or from
// app_main -- post a flag and wake the publisher, which is what comfoair_mqtt_reconfigure() does.
//
// NO NEW LOCK AND NO NEW ORDER. esp_mqtt_client_reconnect() does NOT take MQTT_API_LOCK: its whole
// body is mqtt_client.c:1896-1910 -- a null check, a state check, `wait_timeout_ms = 0` and
// xEventGroupSetBits -- verified by reading the vendored ESP-IDF 5.5.5 tree, and the file's lock is
// taken on eleven other lines, none of them inside it. That is why the ladder may call it from
// here while esp_mqtt_client_stop() may not be called from anywhere but this task: stop() takes
// that lock (:1943) and then waits for STOPPED_BIT with portMAX_DELAY (:1961), which is half of
// the deadlock this component was restructured to remove.
#include "comfoair_mqtt_internal.h"

#include "comfoair_net.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

// The one tag, for the reason comfoair_mqtt_internal.h gives: the owner reading GET /api/log is
// looking at one broker connection, not at this repository's file boundaries.
static const char *TAG = "mqtt";

// The ladder itself. Zero is the correct starting state -- no rung armed, no session timed, no
// address loss recorded, attempt 0 -- and it is deliberately NOT cleared by
// comfoair_mqtt_stop_client(): that function has three callers and two of them run on app_main
// before this task exists, so a reset from there would be a second writer on the one thing whose
// single owner is the whole concurrency argument. The contrast is one file away and worth holding
// in view: g_last_connack IS cleared there, and correctly, because the EVENT HANDLER writes it.
static comfoair_mqtt_ladder_t s_ladder;

// What the ladder tells GET /api/status, in atomics because that reader is the httpd task. Written
// on every pass, so the document is at most one TICK_MS (comfoair_mqtt.c) out of date -- an
// accuracy the document itself already disclaims: "a snapshot, and no reader may treat them as
// simultaneous" (comfoair_wire.h:250-254). Relaxed for the reason given once at the counters in
// comfoair_mqtt.c: no invariant holds between them and nothing may be lost.
static _Atomic uint32_t s_link;
static _Atomic uint32_t s_failure;
static _Atomic uint32_t s_attempts;
static _Atomic uint32_t s_retry_in_ms = COMFOAIR_PROV_NEVER;
static _Atomic uint32_t s_applied_epoch;

// THE DEVICE CARD'S LINK, and it is here rather than in comfoair_mqtt_client.c for the reason that
// file's own g_url comment gives: this is where the address is. Declared in
// comfoair_mqtt_internal.h, which states the task context.
//
// THE DEFECT (a router reboot, a power cut and an owner hunting for the device's address each hit
// it, independently). comfoair_mqtt_start_client() derives the url from comfoair_net_ip_string()
// once, and app_main calls it right after comfoair_http_start() -- before the radio has
// associated, because esp-mqtt dials and retries on its own and nothing waits for a network. So
// after every power cut the device block goes out with `cu` ABSENT, and until this function
// existed nothing ever wrote it again: the card in Home Assistant had no way back to the device's
// own page for the life of the installation.
//
// LOWERING THE DISCOVERY FLAG IS THE HALF THAT MAKES IT VISIBLE. A url in a buffer is not a card;
// the retained config documents on the broker still carry the old device block, and Home
// Assistant reads them from there. COMFOAIR_DISCOVERY_REBUILT is the right event and not an abuse
// of one: it means "every field a config document is made of has just been rewritten", and one of
// them just was.
//
// WHETHER IT MOVED IS comfoair_device_url_refresh()'s ANSWER AND NOT ONE SPELLED HERE, because
// this file is not built by [env:native] and a condition written in it is checked by nothing. Both
// of its edges matter and both are pinned in test/test_device_url: a true on every pass would send
// the whole discovery tree in a circle on a task that wakes several times a second, and a condition that only
// fires on an EMPTY url would freeze the card on the first address the device ever had.
void comfoair_mqtt_note_address(const char *ip)
{
    // NO CLIENT, NO CARD. Before the first comfoair_mqtt_start_client() g_ctx is empty and there is
    // nothing to publish into; on a device that ships with no broker there never will be, and the
    // log line below would be the only thing this function ever did.
    if (g_client == NULL || !comfoair_device_url_refresh(g_url, sizeof g_url, ip))
        return;
    ESP_LOGI(TAG, "device address is %s; Home Assistant is owed its card again", g_url);
    comfoair_mqtt_discovery_rebuilt();
}

void comfoair_mqtt_retry_pass(void)
{
    // The FreeRTOS tick, in milliseconds, and it wraps every ~49.7 days. That is a fact the ladder
    // is built for -- it compares signed differences and keeps no zero sentinel -- and not a
    // hazard to guard here.
    const uint32_t now = (uint32_t)pdTICKS_TO_MS(xTaskGetTickCount());
    // An ADDRESS, not an association: without one the ladder stands, neither dialling nor
    // counting. The whole of comfoair_net_status() for one bool would be a hundred-odd bytes of
    // struct on a 4096-byte stack, and this call is already made by this task in
    // comfoair_mqtt_start_client().
    //
    // ASKED ONLY WHEN THE LINK IS DOWN. NOT because the call is expensive -- it takes no lock at
    // all, it is a snprintf over two variables (comfoair_net_query.c:128-133) -- but because those
    // two are written by the Wi-Fi event task with no synchronisation of any kind, and this
    // pass runs on every wake of the publisher, which a busy CAN bus makes several a second. A
    // device that is CONNECTED TO A BROKER has an address by construction, so the unsynchronised
    // pair is not read at all in the state the device spends its life in.
    bool has_address = true;
    if (!g_connected) {
        char ip[16];
        comfoair_net_ip_string(ip);
        has_address = ip[0] != '\0';
        // THE ONE READING, SPENT TWICE, and the second spending is why this line is inside the
        // `if` rather than after it. A device that is CONNECTED has an address by construction and
        // its url cannot have moved without the broker's socket dying with it -- the connection was
        // opened from the address that changed -- so the pass that notices the drop is the pass
        // that fixes the card, one TICK_MS later at worst.
        comfoair_mqtt_note_address(ip);
    }
    // RULE THREE OF THE DISCOVERY GATE, AND IT IS HERE FOR THE ADDRESS AND NOT FOR
    // THE LADDER. The discriminator "did the BROKER drop us, or did WE drop off the network" needs
    // exactly the reading taken three lines up, and this pass is the only thing in the component
    // that runs on a disconnected tick -- mqtt_task's `if (!g_connected) continue;` is the very
    // next thing after the call to this function. Reading the address a second time in
    // comfoair_mqtt_discovery.c would be a second unsynchronised read of two statics the Wi-Fi
    // event task writes, which is the cost the comment above exists to avoid.
    //
    // ONCE PER PASS, connected or not, because the rule is an EDGE: the flag is cleared on the
    // pass that first sees the link down, and never afterwards. A rule read as a level would clear
    // on every pass between a rebooting router handing the address back and the broker becoming
    // reachable again -- which is the failure the gate exists to make cheap.
    comfoair_mqtt_note_link(g_connected, has_address);
    // ONE load of the failure word, so its four fields cannot disagree with each other; the
    // packing exists for that reason and comfoair_mqtt_internal.h states it.
    const uint32_t word = atomic_load_explicit(&g_last_failure, memory_order_relaxed);

    comfoair_mqtt_ladder_in_t in = {
        .now_ms      = now,
        .client      = g_client != NULL,
        .configured  = g_configured,
        .connected   = g_connected,
        .has_address = has_address,
        // Recomputed each pass rather than cached: twelve bytes of FNV against a static that every
        // reconfigure would have to invalidate, which is a state to get wrong for no measurable
        // saving. NULL until the first comfoair_mqtt_start_client(), and the ladder is standing
        // still then anyway because there is no client.
        .seed        = comfoair_mqtt_backoff_seed(g_ctx.device_id),
    };
    comfoair_mqtt_failword_unpack(word, &in);
    comfoair_mqtt_ladder_out_t out;
    comfoair_mqtt_ladder_pass(&s_ladder, &in, &out);

    if (out.reconnect) {
        // ESP_FAIL here means the client has not parked in MQTT_STATE_WAIT_RECONNECT yet
        // (mqtt_client.c:1903), i.e. an attempt is STILL IN FLIGHT and nothing was asked of the
        // broker. Telling the ladder so is what keeps `attempts` a count of attempts: a dial can
        // occupy up to two network timeouts (:1705-1708 and :1715), and without this the rung
        // would expire, arm, expire and count several times over one of them.
        if (esp_mqtt_client_reconnect(g_client) != ESP_OK) {
            comfoair_mqtt_ladder_not_yet(&s_ladder, now);
        } else {
            // The one line the owner sees per attempt, and it says WHY, because "broker
            // unreachable; retrying" on the disconnect side does not: a mistyped password and a
            // switched-off broker produce the same sentence there.
            ESP_LOGI(TAG, "attempt %u at the broker (%s)", (unsigned)out.attempts,
                     comfoair_mqtt_fail_name(out.failure));
        }
    }

    atomic_store_explicit(&s_link, (uint32_t)out.link, memory_order_relaxed);
    atomic_store_explicit(&s_failure, (uint32_t)out.failure, memory_order_relaxed);
    atomic_store_explicit(&s_attempts, out.attempts, memory_order_relaxed);
    // The sentinel and not zero: "no attempt is scheduled" and "an attempt in 0 ms" are opposite
    // answers, and the document renders this one as null (comfoair_mqtt.h,
    // comfoair_wire_internal.h:97-107).
    atomic_store_explicit(&s_retry_in_ms, out.retry_known ? out.retry_in_ms : COMFOAIR_PROV_NEVER,
                          memory_order_relaxed);
}

void comfoair_mqtt_retry_applied(void)
{
    comfoair_mqtt_ladder_reset(&s_ladder);
    // A PASS BEFORE THE EPOCH MOVES, and the order is the whole contract of the field. A browser
    // that saved a broker waits for this number to change and then reads the rest of the block; if
    // it moved first, `link`, `failure`, `attempts` and `retry_in_ms` would for one pass still
    // describe the client that was just destroyed -- which is precisely the stale verdict the
    // field is an epoch of APPLICATION to avoid. The pass is safe here: the word was
    // cleared by comfoair_mqtt_stop_client(), so nothing is armed and nothing is dialled.
    comfoair_mqtt_retry_pass();
    atomic_fetch_add_explicit(&s_applied_epoch, 1u, memory_order_relaxed);
}

void comfoair_mqtt_retry_stats(comfoair_mqtt_stats_t *out)
{
    if (out == NULL) {
        return;
    }
    out->link          = (uint8_t)atomic_load_explicit(&s_link, memory_order_relaxed);
    out->failure       = (uint8_t)atomic_load_explicit(&s_failure, memory_order_relaxed);
    out->attempts      = atomic_load_explicit(&s_attempts, memory_order_relaxed);
    out->retry_in_ms   = atomic_load_explicit(&s_retry_in_ms, memory_order_relaxed);
    out->applied_epoch = atomic_load_explicit(&s_applied_epoch, memory_order_relaxed);
}
