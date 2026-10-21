// The bus side of the probe: the node census, the PDO census, the reply queue, the transmit and
// the reply wait.
// See comfoair_probe_task.cpp for the component's contract notes.
//
// Everything here that on_frame() touches is written from the CAN receive task (comfoair_can.c,
// priority 10) for EVERY frame on the bus, so none of it may wait: the census has a spinlock of its
// own, the PDO census another, the listening flag is atomic, and the queue is sent to with a zero
// timeout.
#include <atomic>
#include <cstring>

#include "comfoair_can.h"
#include "comfoair_control.h"
#include "comfoair_pdo_census.h"
#include "comfoair_probe_task.h"
#include "comfoair_probe_task_internal.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "pdo_decode.h"
#include "rmi_read.h"

static const char *TAG = "probe";

namespace rmi = comfoair::rmi;

// The reply window, per request. At 50 kbit/s a 32-byte reply -- the longest documented,
// GETACTIVEERRORS (comfoair-pdo-rmi.md §3) -- is five frames of ~2.5 ms each, so 300 ms is two
// orders of magnitude of slack for the unit's own processing.
#define REPLY_WINDOW_MS 300
// The driver's patience per chunk, the same number comfoair_control uses for the same call.
#define TX_TIMEOUT_MS 200
// Sixteen frames. One request is in flight at a time and only replies addressed to us are queued,
// so this holds three of the longest replies (5 frames) at once. A full queue drops, which the walk
// sees as a timeout and retries -- the cheap outcome, against a CAN receive task that must not wait.
#define QUEUE_LEN 16

typedef struct {
    uint32_t id;
    uint8_t  len;
    uint8_t  data[8];
} raw_frame_t;

static QueueHandle_t s_queue;

// Read by on_frame() without any lock, so atomic. 32-bit atomics are native on the C6 (RV32IMAC).
static std::atomic<bool> s_listening{false};

// Under s_census_mux. A spinlock and not an atomic: the C6 has no lock-free 64-bit atomics, and a
// std::atomic<uint64_t> would quietly become a libatomic call holding a lock of its own.
static uint64_t     s_census;
static portMUX_TYPE s_census_mux = portMUX_INITIALIZER_UNLOCKED;

// Under s_pdos_mux, every field. Static and therefore zero-initialised, which IS the reset state
// (comfoair_pdo_census_reset() is one memset to 0), so no init path has to run before on_frame()
// -- and none could: the bus delivers frames before comfoair_probe_task_init(). ~12 KB, never on a
// stack. A lock of its own, not s_census_mux: the node set is read by start() and /api/system,
// and those must not queue behind a render copying PDO rows.
static comfoair_pdo_census_t s_pdos;
static portMUX_TYPE          s_pdos_mux = portMUX_INITIALIZER_UNLOCKED;

esp_err_t probe_bus_init(void)
{
    if (s_queue == NULL)
        s_queue = xQueueCreate(QUEUE_LEN, sizeof(raw_frame_t));
    return s_queue == NULL ? ESP_ERR_NO_MEM : ESP_OK;
}

void probe_bus_listen(bool on) { s_listening.store(on); }

// Every node heard, ascending. `out` holds 64.
size_t probe_bus_census(uint8_t *out)
{
    taskENTER_CRITICAL(&s_census_mux);
    const uint64_t census = s_census;
    taskEXIT_CRITICAL(&s_census_mux);
    size_t n = 0;
    for (uint8_t node = 0; node < 64; node++)
        if (census & (1ull << node))
            out[n++] = node;
    return n;
}

size_t comfoair_probe_task_nodes(uint8_t out[64]) { return probe_bus_census(out); }

// The clock is read BEFORE the lock: esp_timer_get_time() is cheap, but nothing that can be done
// outside a critical section belongs inside one. record() itself is O(1) amortised and bounded by
// 256 index probes (comfoair_pdo_census.h). DO NOT log, allocate or wait in the critical section:
// this runs on the CAN receive task for every PDO frame, with interrupts masked on this core, and
// any of those either aborts (a blocking call under a spinlock) or stalls the reading of the bus.
static void pdos_record(uint32_t can_id, uint8_t node, const uint8_t *data, size_t len)
{
    const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    taskENTER_CRITICAL(&s_pdos_mux);
    comfoair_pdo_census_record(&s_pdos, comfoair::pdo_id_from_can_id(can_id), node, data, len,
                               now_ms);
    taskEXIT_CRITICAL(&s_pdos_mux);
}

// `count` is read under s_pdos_mux, the lock record() appends under: on the RISC-V C6 that
// exit/enter pair is the memory barrier that makes the `pdo` fields of records [0, count) visible
// here, which is what lets the sort below run OUTSIDE the lock while the receive task keeps
// appending past `count` (comfoair_pdo_census.h, THREAD CONTEXT). DO NOT read s_pdos.count without
// the lock, and DO NOT sort under it -- 256 rows of insertion sort with interrupts masked.
size_t probe_pdos_snapshot(comfoair_probe_pdo_summary_t *summary, uint16_t *order, size_t cap)
{
    const uint32_t uptime_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    taskENTER_CRITICAL(&s_pdos_mux);
    const uint32_t count    = s_pdos.count;
    const uint32_t frames   = s_pdos.frames;
    const uint32_t distinct = s_pdos.distinct;
    taskEXIT_CRITICAL(&s_pdos_mux);
    summary->uptime_ms = uptime_ms;
    summary->frames    = frames;
    summary->distinct  = distinct;
    // Never negative: a PDO gets a record only on its first sighting, which also sets its bit.
    summary->overflow = distinct - count;
    return comfoair_pdo_census_order(&s_pdos, count, order, cap);
}

// One record, copied whole under the lock: every field but `pdo` changes on each frame of that PDO,
// so formatting from the live record could pair one frame's `last_len` with another's bytes.
bool probe_pdos_row(uint16_t index, comfoair_probe_pdo_row_t *out)
{
    if (index >= COMFOAIR_PDO_CENSUS_MAX)
        return false;
    taskENTER_CRITICAL(&s_pdos_mux);
    const comfoair_pdo_census_record_t rec = s_pdos.rec[index];
    taskEXIT_CRITICAL(&s_pdos_mux);
    out->pdo       = rec.pdo;
    out->known     = false;  // the caller's to decide; the census does not know the entity table
    out->count     = rec.count;
    out->first_ms  = rec.first_ms;
    out->last_ms   = rec.last_ms;
    out->dlc_mask  = rec.dlc_mask;
    out->node_mask = rec.node_mask;
    out->last_len  = rec.last_len;
    memcpy(out->last, rec.last, sizeof out->last);
    return true;
}

void comfoair_probe_task_on_frame(uint32_t can_id, const uint8_t *data, size_t len)
{
    // Neither census needs init: both are static and so are their spinlocks, and the bus is up
    // (and calling this) before comfoair_probe_task_init() runs.
    bool    known = true;
    uint8_t node  = 0;
    if (rmi::is_rmi_can_id(can_id))
        node = rmi::rmi_src_node(can_id);
    else if (comfoair::is_pdo_can_id(can_id)) {
        node = comfoair::node_id_from_can_id(can_id);
        pdos_record(can_id, node, data, len);
    }
    else if ((can_id & ~0x3fu) == 0x10000000u)  // node heartbeat; not documented by the vendor
        node = static_cast<uint8_t>(can_id & 0x3f);
    else
        known = false;
    if (known) {
        taskENTER_CRITICAL(&s_census_mux);
        s_census |= 1ull << node;
        taskEXIT_CRITICAL(&s_census_mux);
    }

    if (!s_listening.load() || s_queue == NULL)
        return;
    if (!rmi::is_rmi_response(can_id) || rmi::rmi_dst_node(can_id) != COMFOAIR_CONTROL_LOCAL_NODE)
        return;
    // Classic CAN carries at most 8; anything else is not a frame this protocol produced, and
    // truncating it would hand the reassembler bytes that never arrived.
    if (data == NULL || len > 8)
        return;
    raw_frame_t f;
    f.id  = can_id;
    f.len = static_cast<uint8_t>(len);
    memcpy(f.data, data, len);
    // DO NOT give this a timeout. This runs on the CAN receive task; a wait here stops the reading
    // of the bus and loses PDOs. A dropped reply costs one retry.
    (void)xQueueSend(s_queue, &f, 0);
}

// Waits for the reply to `req` sent with `sequence`, feeding matching frames to `reasm`. True with `reasm.message()`
// complete; false on the deadline.
static bool wait_reply(const comfoair_probe_request_t *req, uint8_t sequence,
                       rmi::ResponseReassembler &reasm, uint32_t *reply_id)
{
    const TickType_t start  = xTaskGetTickCount();
    const TickType_t window = pdMS_TO_TICKS(REPLY_WINDOW_MS);
    reasm.reset();
    for (;;) {
        const TickType_t waited = xTaskGetTickCount() - start;
        // At or past the deadline this is a ZERO wait, not a return: a reply that arrived while
        // this task was not running is already in the queue and still counts.
        const TickType_t left = waited >= window ? 0 : window - waited;
        raw_frame_t      f;
        if (xQueueReceive(s_queue, &f, left) != pdTRUE)
            return false;
        // Source, destination AND sequence: a reply to an owner's write to the same unit differs
        // from ours only in the sequence (comfoair_control_next_sequence()).
        if (!rmi::response_matches(f.id, req->node, COMFOAIR_CONTROL_LOCAL_NODE, sequence))
            continue;
        // PROTOCOL_ERROR resets the reassembler and the wait goes on: a retransmission starts at
        // index 0 and can still complete inside the window.
        if (reasm.feed(f.id, f.data, f.len) == rmi::ResponseReassembler::Result::COMPLETE) {
            *reply_id = f.id;
            return true;
        }
    }
}

// Puts one request on the bus and fills `out`. `out->data` borrows `reasm`'s buffer.
void probe_bus_ask(const comfoair_probe_request_t *req, uint8_t another_counter,
                   rmi::ResponseReassembler &reasm, comfoair_probe_outcome_t *out)
{
    *out = comfoair_probe_outcome_t{COMFOAIR_PROBE_ANSWER_TIMEOUT, 0, NULL, 0};

    const rmi::Frame frame = req->kind == COMFOAIR_PROBE_REQ_ACTIVE_ERRORS
                                 ? rmi::get_active_errors()
                                 : rmi::get_one(req->unit, req->subunit, req->type, req->property);
    if (frame.empty()) {
        // get_one() refuses a type outside the three reads; the walk asked for one, which is a bug
        // in comfoair_probe. Recorded as a timeout so the walk moves on instead of asking forever.
        ESP_LOGE(TAG, "walk asked for type 0x%02x on %u/%u/%u; not sent", req->type,
                 (unsigned)req->unit, (unsigned)req->subunit, (unsigned)req->property);
        return;
    }

    const uint8_t  sequence = comfoair_control_next_sequence();
    const uint32_t id = rmi::read_request_can_id(COMFOAIR_CONTROL_LOCAL_NODE, req->node,
                                                 another_counter, frame, sequence);

    // Late answers to the previous request are drained BEFORE sending, never after: the reply to
    // this one can land while transmit is still waiting for the driver, and draining after the
    // send would throw it away with them.
    raw_frame_t stale;
    while (xQueueReceive(s_queue, &stale, 0) == pdTRUE) {
    }

    // Every chunk is attempted, and a refused one still waits the window: ESP_ERR_TIMEOUT means
    // the driver may still send it (comfoair_can.h), and an answer then still counts.
    for (const rmi::Frame &chunk : rmi::split_into_can_frames(frame)) {
        const esp_err_t err = comfoair_can_transmit(id, chunk.data(), chunk.size(), false,
                                                    TX_TIMEOUT_MS);
        if (err != ESP_OK)
            ESP_LOGD(TAG, "transmit %08lx: %s", (unsigned long)id, esp_err_to_name(err));
    }

    uint32_t reply_id = 0;
    if (!wait_reply(req, sequence, reasm, &reply_id))
        return;

    const std::vector<uint8_t> &msg = reasm.message();
    const rmi::ReadAnswer       answer = rmi::classify_response(reply_id, msg.data(), msg.size());
    out->kind  = answer.is_error ? COMFOAIR_PROBE_ANSWER_ERROR : COMFOAIR_PROBE_ANSWER_DATA;
    out->error = answer.error;
    // ALL the bytes, error or not: classify_response()'s "the code is the first byte" is an
    // unverified assumption (rmi_read.h), and the raw payload is how hardware shows it wrong.
    out->data = msg.data();
    out->len  = msg.size();
}
