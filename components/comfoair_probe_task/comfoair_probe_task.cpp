// The device half of the RMI probe: a task, a frame queue, a mutex, a node census and the PDO
// census. The full contract is README.md beside this file; the notes below are the load-bearing
// ones:
//
//  * It decides nothing. Which request comes next, when to retry, when a unit is absent and when
//    to give up are comfoair_probe's, host-tested in test_probe_scan. This file only turns a
//    request into bytes through rmi_read.h -- the only builders that cannot produce a write or a
//    bricking >= 0x80 frame (rmi_read.h, comfoair-pdo-rmi.md §0) -- and reports what came back.
//  * ONE LOCK, s_lock, around every comfoair_probe_* call; that module does not lock itself
//    (comfoair_probe.h, THREAD CONTEXT). The walk task holds it for next() and record() only.
//  * The bus side -- both censuses, reply queue, transmit, reply wait -- is
//    comfoair_probe_task_bus.cpp. Each census has its own spinlock there, because it is written
//    from the CAN receive task for every frame and that task must never wait on a mutex the HTTP
//    task holds.
//  * The unit's identity read (comfoair_probe_task_unit.cpp) runs on THIS task, between walks, so
//    the two never share the bus or the reply queue at once.
//  * Nothing here retries for ever, reboots, or blocks the CAN receive path.
#include "comfoair_probe_task.h"

#include <algorithm>

#include "comfoair_control.h"
#include "comfoair_pdo_census.h"
#include "comfoair_probe.h"
#include "comfoair_probe_task_internal.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "registry_generated.h"

static const char *TAG = "probe";

// 4096 bytes. The task does what comfoair_ctrl does in 3072 (log lines, comfoair_can_transmit,
// chunking through std::vector) plus a reassembler and the outcome. The spare kilobyte is cheap
// against a stack overflow that only shows minutes into a walk; the summary line logs the high
// water mark so hardware can say whether it is too much.
#define TASK_STACK 4096
// Priority 3: below can_rx (10) and can_bus (9) in comfoair_can_api.c, below comfoair_poll and
// httpd (5), and below comfoair_ctrl (4) so an owner's command never waits behind a diagnostic
// read. Being starved cannot fake a timeout: the reply wait polls the queue once more at the
// deadline (wait_reply in comfoair_probe_task_bus.cpp), and a reply is in the queue from the moment
// the receive task saw it.
#define TASK_PRIORITY 3

static SemaphoreHandle_t s_lock;
static TaskHandle_t      s_task;

// Under s_lock.
static uint8_t    s_another_counter;
static TickType_t s_start_tick;
static uint32_t   s_elapsed_ms;  // frozen when the walk ends; 0 before the first
static bool       s_ended;       // the running walk has been frozen by end_walk()

void probe_lock(void) { xSemaphoreTake(s_lock, portMAX_DELAY); }
void probe_unlock(void) { xSemaphoreGive(s_lock); }

// The summary line's numbers, copied in the hold that saw the walk end.
typedef struct {
    const char *state;
    uint32_t    elapsed_ms, requests, timeouts;
    unsigned    entries, units;
    bool        truncated;
} summary_t;

static void summarise(summary_t *out)
{
    const comfoair_probe_results_t *r = comfoair_probe_results();
    out->state      = r->state == COMFOAIR_PROBE_DONE                    ? "done"
                      : r->failure == COMFOAIR_PROBE_FAILURE_NO_ANSWER ? "failed/no_answer"
                                                                         : "failed";
    out->elapsed_ms = s_elapsed_ms;
    out->requests   = r->progress.requests;
    out->timeouts   = r->progress.timeouts;
    out->entries    = (unsigned)r->entry_count;
    out->units      = (unsigned)r->unit_count;
    out->truncated  = r->truncated;
}

// Under the lock. Freezes the elapsed time and stops listening IN THE HOLD THAT ENDED THE WALK.
// DO NOT move this back to "when next() returns false": record() is what sets DONE/FAILED, and the
// next hold comes only after the inter-request gap -- a render landing in between saw DONE with
// elapsed_ms still 0, the page stopped polling on it, and the downloaded JSON kept the 0
// (found in a counter-review of the probe). Idempotent per walk; start() re-arms it.
static void end_walk_if_over(void)
{
    if (s_ended || comfoair_probe_results()->state == COMFOAIR_PROBE_RUNNING)
        return;
    s_elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - s_start_tick);
    probe_bus_listen(false);
    s_ended = true;
    // Same hold, so GET /api/system never shows a finished walk's unit older than its report.
    if (comfoair_probe_results()->state == COMFOAIR_PROBE_DONE)
        probe_unit_from_walk_locked();
}

static void walk(void)
{
    comfoair::rmi::ResponseReassembler reasm;
    summary_t                          sum    = {};
    uint32_t                           handed = 0;
    for (;;) {
        comfoair_probe_request_t req;
        probe_lock();
        const bool    more            = comfoair_probe_next(&req);
        const uint8_t another_counter = s_another_counter;
        if (!more) {
            // Summarised in the same hold that saw the walk end, so a start() arriving right
            // after this unlock cannot have its fresh walk reported as this one's result.
            end_walk_if_over();
            summarise(&sum);
        }
        probe_unlock();
        if (!more)
            break;
        handed++;

        // Outside the lock: the bus round trip is up to 300 ms, and the HTTP task renders under it.
        comfoair_probe_outcome_t outcome;
        probe_bus_ask(&req, another_counter, reasm, &outcome);

        probe_lock();
        comfoair_probe_record(&outcome);
        end_walk_if_over();
        probe_unlock();
        vTaskDelay(pdMS_TO_TICKS(PROBE_GAP_MS));
    }

    // A start() that landed between the walk ending and its summary was walked by this pass and
    // left a notification behind; the pass that notification wakes hands out nothing and must not
    // log the same walk twice.
    if (handed == 0)
        return;
    ESP_LOGI(TAG, "walk %s in %lu ms: %lu requests, %lu timeouts, %u entries, %u units%s; stack "
                  "free %u B",
             sum.state, (unsigned long)sum.elapsed_ms, (unsigned long)sum.requests,
             (unsigned long)sum.timeouts, sum.entries, sum.units,
             sum.truncated ? " (truncated)" : "", (unsigned)uxTaskGetStackHighWaterMark(NULL));
}

static void probe_task(void *arg)
{
    (void)arg;
    probe_unit_arm();
    for (;;) {
        // A notification is start(); a timeout is a unit read falling due (the full one or the
        // active errors alone -- comfoair_unit_cadence picks). pdTRUE clears the count: a start()
        // during the summary line above was already begun under the lock and is walked by the next
        // pass, once. ONE task runs the walk and both reads, so they never overlap on the bus.
        //
        // A start() that lands DURING a unit read is not lost: start() has begun the walk
        // under the lock and left its notification pending, so this take returns at once and the
        // walk runs right after the read. The read leaves listening on for it (either unit read).
        // A walk postpones a due read the same way: after it, the wait is 0 (a walk never moves the
        // due time, not even one that refreshed the unit) -- DO NOT swap the order of the branches.
        if (ulTaskNotifyTake(pdTRUE, probe_unit_wait_ticks()) != 0)
            walk();
        else
            probe_unit_run_due();
    }
}

esp_err_t comfoair_probe_task_init(void)
{
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;
    if (s_lock == NULL)
        s_lock = xSemaphoreCreateMutex();
    if (s_lock == NULL || probe_bus_init() != ESP_OK) {
        ESP_LOGE(TAG, "out of memory starting the probe");
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(probe_task, "comfoair_probe", TASK_STACK, NULL, TASK_PRIORITY, &s_task) !=
        pdPASS) {
        s_task = NULL;
        ESP_LOGE(TAG, "probe task did not start");
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t comfoair_probe_task_start(const comfoair_probe_start_t *start)
{
    if (start == NULL)
        return ESP_ERR_INVALID_ARG;
    // NOT_FOUND, not INVALID_STATE: the HTTP layer answers "no bus" (503) for this and "already
    // running" (409) for the other, and must be able to tell them apart (comfoair_probe_task.h).
    // s_task, not s_lock: init can fail after the lock exists, and then nothing would walk.
    if (s_task == NULL)
        return ESP_ERR_NOT_FOUND;
    if (start->node < 1 || start->node > 63 || start->node == COMFOAIR_CONTROL_LOCAL_NODE ||
        start->another_counter > 3)
        return ESP_ERR_INVALID_ARG;

    uint8_t      seen[64];
    const size_t seen_count = probe_bus_census(seen);

    // An unbounded take, and still "never blocks" in the sense the header means: the walk task
    // holds this for next()/record() and the identity read's copy-in only, and the other holders
    // -- render() and comfoair_probe_task_unit() -- run on the HTTP task, which is the task
    // calling this, so they cannot be holding it now. DO NOT render from another task without
    // revisiting this: start() would then wait out a slow client.
    probe_lock();
    if (comfoair_probe_results()->state == COMFOAIR_PROBE_RUNNING) {
        probe_unlock();
        return ESP_ERR_INVALID_STATE;
    }
    comfoair_probe_plan_t plan = COMFOAIR_PROBE_DEFAULT_PLAN();
    plan.target_node           = start->node;
    for (size_t i = 0; i < seen_count && plan.extra_node_count < COMFOAIR_PROBE_MAX_EXTRA_NODES;
         i++) {
        const uint8_t node = seen[i];
        // Node 0 is heard (a census records what was on the wire) but is not a node id anyone
        // can be addressed as -- the same 1..63 rule as the target.
        if (node == 0 || node == start->node || node == COMFOAIR_CONTROL_LOCAL_NODE)
            continue;
        plan.extra_nodes[plan.extra_node_count++] = node;
    }
    comfoair_probe_begin(&plan);
    s_another_counter = start->another_counter;
    s_start_tick      = xTaskGetTickCount();
    s_elapsed_ms      = 0;
    s_ended           = false;
    probe_bus_listen(true);
    probe_unlock();

    xTaskNotifyGive(s_task);
    ESP_LOGI(TAG, "walk of node %u started (another_counter %u, %u other nodes)",
             (unsigned)plan.target_node, (unsigned)start->another_counter,
             (unsigned)plan.extra_node_count);
    return ESP_OK;
}

// Under the probe lock, which makes this one buffer safe: every render holds it for its whole
// duration, so two renders never share it. Static, not on the stack: 512 B of the HTTP task's
// stack for a debugging section is the wrong place to spend it.
static uint16_t s_pdo_order[COMFOAIR_PDO_CENSUS_MAX];

// The renderer's row callback: row i is record s_pdo_order[i], copied under the census spinlock.
// `known` by binary search, because COMFOAIR_REQUEST_PDOS is generated sorted ascending
// (render_c.py sorts it; tests/python/test_registry.py pins the emitted order).
static bool pdo_row(void *ctx, size_t i, comfoair_probe_pdo_row_t *out)
{
    const uint16_t *order = static_cast<const uint16_t *>(ctx);
    if (!probe_pdos_row(order[i], out))
        return false;
    out->known = std::binary_search(COMFOAIR_REQUEST_PDOS,
                                    COMFOAIR_REQUEST_PDOS + COMFOAIR_REQUEST_PDO_COUNT, out->pdo);
    return true;
}

esp_err_t comfoair_probe_task_render(comfoair_probe_write_fn write, void *ctx)
{
    if (s_lock == NULL)
        return ESP_ERR_INVALID_STATE;
    if (write == NULL)
        return ESP_ERR_INVALID_ARG;

    uint8_t seen[64];
    // HELD FOR THE WHOLE STREAMED RENDER, on the HTTP task, so a slow client holds it for as long
    // as it reads. That delays the walk -- the walk task waits for the lock between requests --
    // but cannot fake a timeout: the walk never holds the lock across a bus round trip, and reply
    // timing is measured on the frame queue, which the CAN receive task fills without this lock.
    probe_lock();
    const comfoair_probe_results_t *r = comfoair_probe_results();
    comfoair_probe_meta_t           meta;
    meta.elapsed_ms       = r->state == COMFOAIR_PROBE_RUNNING
                                ? pdTICKS_TO_MS(xTaskGetTickCount() - s_start_tick)
                                : s_elapsed_ms;
    meta.another_counter  = s_another_counter;
    meta.nodes_seen       = seen;
    meta.nodes_seen_count = probe_bus_census(seen);
    comfoair_probe_pdo_summary_t pdos;
    meta.pdo_rows    = probe_pdos_snapshot(&pdos, s_pdo_order, COMFOAIR_PDO_CENSUS_MAX);
    meta.pdo_summary = &pdos;
    meta.pdo_row     = pdo_row;
    meta.pdo_ctx     = s_pdo_order;
    const bool ok         = comfoair_probe_render(r, &meta, write, ctx);
    probe_unlock();
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t comfoair_probe_task_unit(comfoair_sysinfo_unit_t *out)
{
    if (out == NULL)
        return ESP_ERR_INVALID_ARG;
    // s_task, not s_lock, for the same reason as start(): without the task nothing ever reads.
    if (s_task == NULL) {
        *out = comfoair_sysinfo_unit_t{};
        return ESP_ERR_INVALID_STATE;
    }
    probe_lock();
    probe_unit_copy_locked(out);
    probe_unlock();
    return ESP_OK;
}
