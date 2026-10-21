// A fake esp_driver_twai that keeps the parts of the real driver's contract a caller can
// get wrong.
//
// It is deliberately NOT a convenient mock. Three behaviours are copied from
// components/esp_driver_twai/esp_twai_onchip.c of framework-espidf 5.5.5, because each
// one is a rule the transport above it has to obey:
//
//   1. The TX queue holds a `const twai_frame_t *`, not a copy. Line 698 creates it with
//      `sizeof(twai_frame_t *)`, and lines 603 (ISR) / 606 (task) enqueue `&frame`, where
//      `frame` is already the pointer parameter. The frame AND its buffer are BORROWED.
//   2. That pointer is dereferenced later, from the TX-done interrupt (line 292 ->
//      _node_start_trans, lines 183-203, whose reads of `frame->header` and `frame->buffer`
//      are at 191-193), and again once a bus-off recovery completes (lines 246-249).
//      "Later" is ~2 ms at 50 kbps, or unbounded across a recovery.
//   3. A node in bus-off refuses to transmit (line 588) and leaves that state only when
//      somebody calls twai_node_recover() (esp_twai.h:36-43). Nothing recovers by itself.
//
// A mock that copied the frame on submission would make a broken caller pass. So this one
// does what the hardware driver does: it remembers a pointer, and it reads through that
// pointer at the moment the hardware would have.

#include "fake_twai.h"

#include <string.h>

#include <stdatomic.h>

#include "esp_err.h"
#include "esp_twai.h"

#define TX_QUEUE_MAX 16

struct twai_node_base {
    int marker;
};

static struct {
    struct twai_node_base     node;
    bool                      created;
    bool                      enabled;
    twai_onchip_node_config_t cfg;

    twai_event_callbacks_t cbs;
    void                  *cb_ctx;

    twai_error_state_t state;
    bool               recovery_requested;
    uint16_t           tec;
    uint16_t           rec;
    uint32_t           bus_err_num;

    // TX. `pending` holds borrowed pointers, exactly as tx_mount_queue does.
    bool                hw_busy;
    const twai_frame_t *curr;
    const twai_frame_t *pending[TX_QUEUE_MAX];
    unsigned            pending_head;
    unsigned            pending_count;

    fake_tx_record_t tx_log[FAKE_TX_LOG_LEN];
    unsigned         tx_count;

    // RX frame staged for twai_node_receive_from_isr.
    bool     rx_staged;
    uint32_t rx_id;
    uint16_t rx_dlc;
    uint8_t  rx_ide;
    uint8_t  rx_rtr;
    uint8_t  rx_data[8];
    size_t   rx_len;

    // Every entry into twai_node_transmit, valid or not. tx_count only counts frames that
    // reached the wire, so the two together say whether an argument check in the transport
    // ran at all or whether the driver did the rejecting.
    unsigned transmit_calls;

    // The controller accepts frames but never reports them complete, which is what a bus
    // with nobody to acknowledge looks like.
    bool stalled;

    // A monotonic tick stamped on the calls whose ORDER is a contract.
    unsigned seq;
    unsigned seq_register;
    unsigned seq_enable;
} g;

// Thread-local, not a global counter. Frames are injected from the test thread while
// comfoair_can.c's comfoair_can_rx_task runs on another one; a shared flag would answer "is this
// interrupt context" for the wrong thread and report a violation that never happened.
// Interrupt context is a property of a thread of execution, so it is modelled as one.
static __thread int t_isr_depth;

// Read from the test thread while a recovery task may be writing them, so these two live
// outside `g` as atomics. Everything else in `g` is touched only by the test thread.
static _Atomic unsigned s_recover_calls;
static _Atomic unsigned s_recover_calls_in_isr;

bool fake_twai_in_isr(void) { return t_isr_depth > 0; }

void fake_twai_reset(void)
{
    memset(&g, 0, sizeof g);
    atomic_store(&s_recover_calls, 0u);
    atomic_store(&s_recover_calls_in_isr, 0u);
    t_isr_depth   = 0;
    g.node.marker = 0x7741;
    fake_log_reset();
}

// --- the deferred dereference ----------------------------------------------------------

// _node_start_trans (esp_twai_onchip.c:183-203): reads the header and the buffer THROUGH
// the stored pointer, at whatever moment the hardware became free. Recording what it finds
// is what turns a lifetime bug into a failing assertion instead of a heisenbug.
static void start_trans(void)
{
    const twai_frame_t *frame = g.curr;
    if (frame == NULL)
        return;

    fake_tx_record_t rec = {0};
    rec.id              = frame->header.id;
    rec.dlc             = frame->header.dlc;
    rec.ide             = (uint8_t)frame->header.ide;
    rec.rtr             = (uint8_t)frame->header.rtr;
    rec.buffer_len      = frame->buffer_len;
    rec.buffer_was_null = frame->buffer == NULL;

    if (frame->buffer != NULL) {
        const size_t n = frame->buffer_len > sizeof rec.data ? sizeof rec.data : frame->buffer_len;
        memcpy(rec.data, frame->buffer, n);
    }

    if (g.tx_count < FAKE_TX_LOG_LEN)
        g.tx_log[g.tx_count] = rec;
    g.tx_count++;
}

// --- ESP-IDF surface ---------------------------------------------------------------------

esp_err_t twai_new_node_onchip(const twai_onchip_node_config_t *node_config,
                               twai_node_handle_t *node_ret)
{
    if (node_config == NULL || node_ret == NULL)
        return ESP_ERR_INVALID_ARG;
    // :679 -- a zero-depth TX queue is refused by the driver. Without this the fake was
    // LOOSER than the device and a regression to depth 0 would have passed the host suite.
    if (node_config->tx_queue_depth == 0)
        return ESP_ERR_INVALID_ARG;
    if (g.created)
        return ESP_ERR_INVALID_STATE;

    g.cfg     = *node_config;
    g.created = true;
    // A freshly created node is "stopped", which this driver spells TWAI_ERROR_BUS_OFF --
    // see the state assertions at esp_twai_onchip.c:362, :455 and :525.
    g.state   = TWAI_ERROR_BUS_OFF;
    *node_ret = &g.node;
    return ESP_OK;
}

esp_err_t twai_node_register_event_callbacks(twai_node_handle_t node,
                                             const twai_event_callbacks_t *cbs, void *user_data)
{
    if (node != &g.node || cbs == NULL)
        return ESP_ERR_INVALID_ARG;
    // esp_twai_onchip.c:362 -- "register callback must when node stopped".
    if (g.state != TWAI_ERROR_BUS_OFF)
        return ESP_ERR_INVALID_STATE;
    g.cbs         = *cbs;
    g.cb_ctx      = user_data;
    g.seq_register = ++g.seq;
    return ESP_OK;
}

esp_err_t twai_node_enable(twai_node_handle_t node)
{
    if (node != &g.node)
        return ESP_ERR_INVALID_ARG;
    if (g.state != TWAI_ERROR_BUS_OFF)   // :455 -- "node already enabled"
        return ESP_ERR_INVALID_STATE;
    g.enabled    = true;
    g.state      = TWAI_ERROR_ACTIVE;
    g.seq_enable = ++g.seq;
    return ESP_OK;
}

esp_err_t twai_node_disable(twai_node_handle_t node)
{
    if (node != &g.node)
        return ESP_ERR_INVALID_ARG;
    if (g.state == TWAI_ERROR_BUS_OFF)   // :483 -- "node already disabled"
        return ESP_ERR_INVALID_STATE;
    g.enabled = false;
    g.state   = TWAI_ERROR_BUS_OFF;      // :492
    return ESP_OK;
}

esp_err_t twai_node_delete(twai_node_handle_t node)
{
    if (node != &g.node)
        return ESP_ERR_INVALID_ARG;
    if (g.state != TWAI_ERROR_BUS_OFF)   // :348 -- "delete node must when node stopped"
        return ESP_ERR_INVALID_STATE;
    g.created = false;
    g.enabled = false;
    memset(&g.cbs, 0, sizeof g.cbs);
    g.curr          = NULL;
    g.hw_busy       = false;
    g.pending_count = 0;
    return ESP_OK;
}

esp_err_t twai_node_recover(twai_node_handle_t node)
{
    if (node != &g.node)
        return ESP_ERR_INVALID_ARG;
    // :548 -- rejected unless the node really is in bus-off.
    if (g.state != TWAI_ERROR_BUS_OFF)
        return ESP_ERR_INVALID_STATE;

    atomic_fetch_add(&s_recover_calls, 1u);
    if (fake_twai_in_isr())
        atomic_fetch_add(&s_recover_calls_in_isr, 1u);
    g.recovery_requested = true;
    g.bus_err_num        = 0;   // _node_recover clears history.bus_err_num
    return ESP_OK;
}

esp_err_t twai_node_transmit(twai_node_handle_t node, const twai_frame_t *frame, int timeout_ms)
{
    (void)timeout_ms;
    g.transmit_calls++;
    if (node != &g.node || frame == NULL)
        return ESP_ERR_INVALID_ARG;
    // :584 accepts a DLC up to TWAIFD_FRAME_MAX_DLC even on a controller without FD.
    // Rejecting 9..15 here would have been STRICTER than the device -- a fake that invents
    // an error invents a passing test for handling it.
    if (frame->header.dlc > TWAIFD_FRAME_MAX_DLC || frame->buffer_len > TWAI_FRAME_MAX_LEN)
        return ESP_ERR_INVALID_ARG;
    // :578-579 -- dlc and buffer_len must agree; below 9 bytes the DLC is the length.
    if (frame->header.dlc && frame->buffer_len && frame->header.dlc != frame->buffer_len)
        return ESP_ERR_INVALID_ARG;
    if (g.state == TWAI_ERROR_BUS_OFF)   // :588
        return ESP_ERR_INVALID_STATE;

    if (!g.hw_busy) {
        g.hw_busy = true;
        g.curr    = frame;
        start_trans();   // formatted now, while the caller's frame is still alive
        return ESP_OK;
    }

    if (g.pending_count >= g.cfg.tx_queue_depth || g.pending_count >= TX_QUEUE_MAX)
        return ESP_ERR_TIMEOUT;   // :603/:606 -- "tx queue full"

    // The borrowed pointer, and nothing else. This is the line the transport has to
    // survive.
    g.pending[(g.pending_head + g.pending_count) % TX_QUEUE_MAX] = frame;
    g.pending_count++;
    return ESP_OK;
}

// esp_twai.h:127-134: "if bus-off happens during waiting, wait until node recovered and tx
// is finished, OR TIMEOUT". The timeout is the whole point of modelling this call -- on that
// path it returns while the frames are still queued, so a caller that treats the wait as a
// guarantee of completion is wrong exactly when the bus is in trouble.
esp_err_t twai_node_transmit_wait_all_done(twai_node_handle_t node, int timeout_ms)
{
    (void)timeout_ms;
    if (node != &g.node)
        return ESP_ERR_INVALID_ARG;
    if (g.stalled)
        return g.hw_busy ? ESP_ERR_TIMEOUT : ESP_OK;
    while (g.hw_busy)
        fake_twai_complete_tx();
    return ESP_OK;
}

esp_err_t twai_node_receive_from_isr(twai_node_handle_t node, twai_frame_t *rx_frame)
{
    if (node != &g.node || rx_frame == NULL)
        return ESP_ERR_INVALID_ARG;
    if (!g.rx_staged)
        return ESP_ERR_INVALID_STATE;

    rx_frame->header.id  = g.rx_id;
    rx_frame->header.dlc = g.rx_dlc;
    rx_frame->header.ide = g.rx_ide;
    rx_frame->header.rtr = g.rx_rtr;

    // twai_hal_parse_frame (hal/twai_hal_v1.c:382-389 -- v1 is what the C6 builds, it has
    // no SOC_TWAI_SUPPORT_FD) copies the payload ONLY for a data frame:
    //     if (!header->rtr) { twai_ll_parse_frame_data(...); }
    // A remote frame therefore leaves the caller's buffer exactly as it found it.
    if (!g.rx_rtr && rx_frame->buffer != NULL) {
        const size_t cap = rx_frame->buffer_len;
        memcpy(rx_frame->buffer, g.rx_data, g.rx_len > cap ? cap : g.rx_len);
    }

    // DO NOT write rx_frame->buffer_len. The real driver never does -- _node_parse_rx
    // (:657) passes it in BY VALUE as a capacity, and neither twai_hal_parse_frame nor
    // twai_ll_parse_frame_data writes it back. On the device it stays at `sizeof buf`
    // forever. A fake that updated it would go green on a fix that reads the length from
    // buffer_len, and that fix reports 8 bytes for every frame on real hardware.

    g.rx_staged = false;
    return ESP_OK;
}

esp_err_t twai_node_get_info(twai_node_handle_t node, twai_node_status_t *status_ret,
                             twai_node_record_t *statistics_ret)
{
    if (node != &g.node)
        return ESP_ERR_INVALID_ARG;
    if (status_ret) {
        status_ret->state              = g.state;
        status_ret->tx_error_count     = g.tec;
        status_ret->rx_error_count     = g.rec;
        status_ret->tx_queue_remaining = g.cfg.tx_queue_depth - g.pending_count;
    }
    if (statistics_ret)
        statistics_ret->bus_err_num = g.bus_err_num;
    return ESP_OK;
}

// --- stimulus ------------------------------------------------------------------------------

static void fire_rx_done(void)
{
    // The real event struct is empty (esp_twai_types.h:68-69); a file-scope const is the
    // only way to hand out a zeroed one without an excess-initialiser warning.
    static const twai_rx_done_event_data_t edata;
    t_isr_depth++;
    g.cbs.on_rx_done(&g.node, &edata, g.cb_ctx);
    t_isr_depth--;
    g.rx_staged = false;
}

void fake_twai_deliver_rx(uint32_t can_id, const uint8_t *data, size_t len, uint16_t dlc)
{
    if (g.cbs.on_rx_done == NULL)
        return;

    g.rx_id  = can_id;
    g.rx_dlc = dlc;
    g.rx_ide = 1;
    g.rx_rtr = 0;
    g.rx_len = len > sizeof g.rx_data ? sizeof g.rx_data : len;
    memset(g.rx_data, 0, sizeof g.rx_data);
    if (data != NULL)
        memcpy(g.rx_data, data, g.rx_len);
    g.rx_staged = true;

    fire_rx_done();
}

// A remote frame. The wire carries a DLC but no data field, so the driver leaves the
// caller's buffer untouched -- see twai_node_receive_from_isr above. Nothing else about the
// frame distinguishes it: pdo_decode.h:19-20 notes that a request from another node has an
// identical id to the broadcast it asks for.
void fake_twai_deliver_rx_remote(uint32_t can_id, uint16_t dlc)
{
    if (g.cbs.on_rx_done == NULL)
        return;

    g.rx_id  = can_id;
    g.rx_dlc = dlc;
    g.rx_ide = 1;
    g.rx_rtr = 1;
    g.rx_len = 0;
    memset(g.rx_data, 0, sizeof g.rx_data);
    g.rx_staged = true;

    fire_rx_done();
}

// The interrupt fired but the frame is already gone -- twai_node_receive_from_isr returns
// ESP_ERR_INVALID_STATE, which is the one RX path the transport can take without producing
// a frame OR a statistic.
void fake_twai_deliver_rx_nothing(void)
{
    if (g.cbs.on_rx_done == NULL)
        return;
    g.rx_staged = false;
    fire_rx_done();
}

void fake_twai_stall_tx(bool stalled) { g.stalled = stalled; }

const void *fake_twai_in_flight_frame(void) { return g.curr; }

void fake_twai_complete_tx(void)
{
    if (!g.hw_busy)
        return;

    t_isr_depth++;

    const twai_tx_done_event_data_t edata = {.is_tx_success = true, .done_tx_frame = g.curr};
    if (g.cbs.on_tx_done)
        g.cbs.on_tx_done(&g.node, &edata, g.cb_ctx);

    // esp_twai_onchip.c:292 -- the next borrowed pointer comes off the queue and is
    // dereferenced right here, long after its owner returned.
    if (g.state != TWAI_ERROR_BUS_OFF && g.pending_count > 0) {
        g.curr         = g.pending[g.pending_head];
        g.pending_head = (g.pending_head + 1) % TX_QUEUE_MAX;
        g.pending_count--;
        start_trans();
    } else {
        g.curr    = NULL;
        g.hw_busy = false;
    }

    t_isr_depth--;
}

void fake_twai_go_bus_off(void)
{
    if (!g.created || g.state == TWAI_ERROR_BUS_OFF)
        return;

    const twai_error_state_t old = g.state;
    g.tec = 256;
    g.bus_err_num += 1;
    g.state = TWAI_ERROR_BUS_OFF;
    // The hardware aborts whatever was in flight (twai_hal_v1.c:211 clears TX_BUFF_OCCUPIED)
    // and the ISR's else-branch clears the busy flag (:296-297). Leaving it set would make
    // every later test on this node diverge from the device.
    g.hw_busy = false;
    g.curr    = NULL;

    t_isr_depth++;
    if (g.cbs.on_error) {
        // ack_err, not arb_lost: the way this bus reaches bus-off is an unpowered unit that
        // never acknowledges. err_flags is a union (hal/twai_types.h:44-53).
        const twai_error_event_data_t e = {.err_flags = {.ack_err = 1}};
        g.cbs.on_error(&g.node, &e, g.cb_ctx);
    }
    if (g.cbs.on_state_change) {
        const twai_state_change_event_data_t e = {.old_sta = old, .new_sta = TWAI_ERROR_BUS_OFF};
        g.cbs.on_state_change(&g.node, &e, g.cb_ctx);
    }
    t_isr_depth--;
}

bool fake_twai_finish_recovery(void)
{
    // The hardware needs 128 consecutive recessive bits AFTER the recover command. No
    // command, no recovery -- that is the whole reason bus-off is not self-healing.
    if (!g.recovery_requested || g.state != TWAI_ERROR_BUS_OFF)
        return false;

    g.recovery_requested = false;
    g.state              = TWAI_ERROR_ACTIVE;
    g.tec                = 0;

    t_isr_depth++;
    if (g.cbs.on_state_change) {
        const twai_state_change_event_data_t e = {.old_sta = TWAI_ERROR_BUS_OFF,
                                                  .new_sta = TWAI_ERROR_ACTIVE};
        g.cbs.on_state_change(&g.node, &e, g.cb_ctx);
    }
    // esp_twai_onchip.c:246-249 -- frames parked during bus-off resume here, which is a
    // second and much later dereference of the borrowed pointers.
    if (g.pending_count > 0) {
        g.curr         = g.pending[g.pending_head];
        g.pending_head = (g.pending_head + 1) % TX_QUEUE_MAX;
        g.pending_count--;
        g.hw_busy = true;
        start_trans();
    } else {
        g.hw_busy = false;   // :250-251 -- nothing parked, the controller goes idle
        g.curr    = NULL;
    }
    t_isr_depth--;
    return true;
}

void fake_twai_set_error_counts(uint16_t tec, uint16_t rec)
{
    g.tec = tec;
    g.rec = rec;
}

// --- observation ---------------------------------------------------------------------------

bool                             fake_twai_node_exists(void) { return g.created; }
bool                             fake_twai_enabled(void) { return g.enabled; }
const twai_onchip_node_config_t *fake_twai_config(void) { return &g.cfg; }
unsigned                         fake_twai_tx_count(void) { return g.tx_count; }
unsigned fake_twai_transmit_calls(void) { return g.transmit_calls; }
unsigned fake_twai_recover_calls(void) { return atomic_load(&s_recover_calls); }
unsigned fake_twai_recover_calls_in_isr(void) { return atomic_load(&s_recover_calls_in_isr); }
twai_error_state_t fake_twai_state(void) { return g.state; }

bool fake_twai_callbacks_registered_before_enable(void)
{
    return g.seq_register != 0 && g.seq_enable != 0 && g.seq_register < g.seq_enable;
}

const void *fake_twai_queued_frame(unsigned index)
{
    if (index >= g.pending_count)
        return NULL;
    return g.pending[(g.pending_head + index) % TX_QUEUE_MAX];
}

const fake_tx_record_t *fake_twai_tx(unsigned index)
{
    if (index >= g.tx_count || index >= FAKE_TX_LOG_LEN)
        return NULL;
    return &g.tx_log[index];
}
