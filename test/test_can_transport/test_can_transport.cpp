// The CAN transport, against a fake ESP-IDF that keeps the real driver's contract.
//
// comfoair_can.h says the component "only moves bytes". That is a claim with edges, and
// these are the edges:
//
//   - a frame handed to the driver must still exist when the driver reads it, which is
//     later than the call that submitted it (fake_twai.c cites where);
//   - what reaches the consumer must be bytes that arrived on the wire, never whatever the
//     interrupt stack happened to hold;
//   - the receive path must not block, log, or call back from interrupt context;
//   - a bus that goes off must come back without a reboot, because nothing in this firmware
//     may reboot over an absent peer (AGENTS.md).
//
// Tests are named after the guarantee, never after the defect, so that fixing the code
// turns one green without rewriting it.
//
// To get a second, independent opinion on the lifetime tests, build with
//   -fsanitize=address -fno-omit-frame-pointer   ASAN_OPTIONS=detect_stack_use_after_return=1
// which names the offending variable and line directly. It is not wired into [env:native]
// because it aborts the run at the first report, taking the remaining tests with it.
#include <unity.h>

#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <vector>

#include "board.h"
#include "comfoair_can.h"
#include "fake_twai.h"

// --- harness -------------------------------------------------------------------------------

namespace {

struct Received {
    uint32_t id;
    uint8_t  data[8];
    size_t   len;
    bool     extended;
    bool     rtr;
    bool     seen_in_isr;
};

pthread_mutex_t g_mtx  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  g_cond = PTHREAD_COND_INITIALIZER;

std::vector<Received> g_received;
bool                  g_gate_closed  = false;   // holds the consumer inside the callback
bool                  g_slow_callback = false;  // makes the consumer briefly busy in one

void sleep_ms(unsigned ms)
{
    struct timespec ts = {.tv_sec = ms / 1000u, .tv_nsec = (long)(ms % 1000u) * 1000000L};
    nanosleep(&ts, nullptr);
}

void unlock_g_mtx(void *) { pthread_mutex_unlock(&g_mtx); }

void on_frame(uint32_t can_id, bool extended, bool rtr, const uint8_t *data, size_t len, void *)
{
    Received r{};
    r.id          = can_id;
    r.len         = len;
    r.extended    = extended;
    r.rtr         = rtr;
    r.seen_in_isr = fake_twai_in_isr();
    memcpy(r.data, data, len > sizeof r.data ? sizeof r.data : len);

    if (g_slow_callback)
        sleep_ms(60);   // a consumer doing real work, not one that never returns

    pthread_mutex_lock(&g_mtx);
    // comfoair_can_stop() cancels this thread, and pthread_cond_wait below is a
    // cancellation point that re-acquires the mutex before unwinding. Without this handler
    // a cancelled consumer dies owning g_mtx and the NEXT test blocks forever -- a hang, not
    // a failure. The hazard it papers over is real and is asserted by
    // test_stop_does_not_tear_the_consumer_down_mid_callback, not hidden.
    pthread_cleanup_push(unlock_g_mtx, nullptr);
    while (g_gate_closed)
        pthread_cond_wait(&g_cond, &g_mtx);
    g_received.push_back(r);
    pthread_cond_broadcast(&g_cond);
    pthread_cleanup_pop(0);
    pthread_mutex_unlock(&g_mtx);
}

// The consumer runs on its own thread, so every assertion about what it saw has to wait for
// it rather than assume it already ran.
bool wait_for_frames(size_t n, unsigned timeout_ms)
{
    for (unsigned waited = 0; waited < timeout_ms; waited += 2) {
        pthread_mutex_lock(&g_mtx);
        const size_t have = g_received.size();
        pthread_mutex_unlock(&g_mtx);
        if (have >= n)
            return true;
        sleep_ms(2);
    }
    return false;
}

// Recovery may legitimately be driven from a task rather than from the interrupt, so this
// waits for it instead of demanding it happen synchronously.
bool wait_for_recover(unsigned timeout_ms)
{
    for (unsigned waited = 0; waited < timeout_ms; waited += 2) {
        if (fake_twai_recover_calls() > 0)
            return true;
        sleep_ms(2);
    }
    return false;
}

void close_gate()
{
    pthread_mutex_lock(&g_mtx);
    g_gate_closed = true;
    pthread_mutex_unlock(&g_mtx);
}

void open_gate()
{
    pthread_mutex_lock(&g_mtx);
    g_gate_closed = false;
    pthread_cond_broadcast(&g_cond);
    pthread_mutex_unlock(&g_mtx);
}

void start_transport()
{
    TEST_ASSERT_EQUAL_INT(ESP_OK, comfoair_can_start(board_get(), on_frame, nullptr));
}

// Leaves the controller holding a frame it will never finish, which is what an unpowered
// unit looks like: nothing acknowledges, so nothing completes. Anything sent after this has
// to wait in the driver's queue, and that is the only path on which a frame's lifetime is
// visible at all.
void stall_the_controller()
{
    static const uint8_t filler[1] = {0x00};
    fake_twai_stall_tx(true);
    (void)comfoair_can_transmit(0x1F000001u, filler, 1, false, 1);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, fake_twai_tx_count(),
                                   "the first frame should have reached the controller");
}

}  // namespace

void setUp(void)
{
    fake_twai_reset();
    pthread_mutex_lock(&g_mtx);
    g_received.clear();
    g_gate_closed   = false;
    g_slow_callback = false;
    pthread_mutex_unlock(&g_mtx);
}

void tearDown(void)
{
    open_gate();
    comfoair_can_stop();
}

// --- bring-up ------------------------------------------------------------------------------

void test_the_controller_takes_its_pins_and_bitrate_from_the_board(void)
{
    start_transport();

    const board_t                   *b   = board_get();
    const twai_onchip_node_config_t *cfg = fake_twai_config();
    TEST_ASSERT_EQUAL_INT_MESSAGE(b->can_tx, cfg->io_cfg.tx, "tx pin");
    TEST_ASSERT_EQUAL_INT_MESSAGE(b->can_rx, cfg->io_cfg.rx, "rx pin");
    // 50 kbps is the ComfoNet rate. A future board that disagrees has to be able to say so,
    // which is only true while the value travels from the descriptor, not a literal.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(b->can_bitrate, cfg->bit_timing.bitrate, "bitrate");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(50000, cfg->bit_timing.bitrate, "ComfoNet bitrate");
}

void test_retransmission_is_bounded(void)
{
    start_transport();
    // -1 means "retry forever", which on a bus whose unit may simply be unpowered is an
    // interrupt that never stops. The source comment promises this is bounded; assert it.
    TEST_ASSERT_GREATER_OR_EQUAL_INT_MESSAGE(0, fake_twai_config()->fail_retry_cnt,
                                             "fail_retry_cnt must not be -1");
}

void test_callbacks_are_registered_before_the_node_is_enabled(void)
{
    // Asserted on the recorded ORDER of the two calls, not on the node ending up enabled --
    // it ends up enabled either way, so the obvious assertion would have checked nothing.
    // esp_twai_onchip.c:362 rejects registration on a running node, so getting this wrong
    // silently produces a transport that never receives a frame.
    start_transport();
    TEST_ASSERT_TRUE_MESSAGE(fake_twai_callbacks_registered_before_enable(),
                             "callbacks must be registered while the node is still stopped");
}

void test_starting_twice_is_refused(void)
{
    start_transport();
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE,
                          comfoair_can_start(board_get(), on_frame, nullptr));
}

void test_starting_without_a_callback_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG, comfoair_can_start(board_get(), nullptr, nullptr));
    TEST_ASSERT_FALSE_MESSAGE(fake_twai_node_exists(), "a refused start must leave no node");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_live_queues(),
                                   "a refused start must allocate nothing");
}

void test_stop_releases_the_controller_the_task_and_the_queue(void)
{
    start_transport();
    TEST_ASSERT_EQUAL_INT(ESP_OK, comfoair_can_stop());
    TEST_ASSERT_FALSE(fake_twai_node_exists());
    // Without these two the leak is invisible: a stop that only NULLs its handles simply
    // strands a thread and a queue per cycle, and the suite hangs instead of failing.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_live_tasks(), "rx task was not deleted");
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_live_queues(), "rx queue was not deleted");
    start_transport();
}

// --- receive -------------------------------------------------------------------------------

void test_a_received_frame_reaches_the_callback_intact(void)
{
    start_transport();

    const uint8_t payload[6] = {0x01, 0x02, 0x03, 0xFD, 0xFE, 0xFF};
    fake_twai_deliver_rx(0x10FF0042u, payload, sizeof payload, sizeof payload);

    TEST_ASSERT_TRUE_MESSAGE(wait_for_frames(1, 500), "frame never reached the callback");
    TEST_ASSERT_EQUAL_HEX32(0x10FF0042u, g_received[0].id);
    TEST_ASSERT_EQUAL_size_t(sizeof payload, g_received[0].len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, g_received[0].data, sizeof payload);
}

// A remote frame has a DLC on the wire and no data field at all: twai_hal_parse_frame
// (hal/twai_hal_v1.c:382-389, and v1 is what the C6 builds) copies the payload only
// `if (!header->rtr)`, leaving the receiver's buffer exactly as it found it.
//
// This matters far beyond a wrong length. pdo_decode.h:19-20 records that "a remote
// transmission request from another node has an identical id" to the broadcast it asks for,
// and requires the caller to reject rtr for that reason -- an obligation the caller cannot
// meet, because comfoair_can_rx_cb_t carries no rtr. So a poll from a second node on the
// bus (another controller, a ComfoConnect LAN C) is decoded as a reading.
void test_a_remote_frame_delivers_no_payload(void)
{
    start_transport();

    fake_twai_deliver_rx_remote(0x10FF0800u, 8);

    TEST_ASSERT_TRUE_MESSAGE(wait_for_frames(1, 500), "frame never reached the callback");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(
        0, g_received[0].len,
        "a remote frame carries no data: its dlc must not become a payload length");
    // Length alone is not enough. pdo_decode.h requires the consumer to reject rtr, and it
    // can only do that if the transport says which frames were remote.
    TEST_ASSERT_TRUE_MESSAGE(g_received[0].rtr, "the rtr flag was swallowed by the transport");
    TEST_ASSERT_TRUE_MESSAGE(g_received[0].extended, "the ide flag was swallowed by the transport");
}

void test_the_callback_never_runs_in_interrupt_context(void)
{
    start_transport();

    const uint8_t payload[2] = {0xAA, 0xBB};
    fake_twai_deliver_rx(0x10FF0001u, payload, sizeof payload, sizeof payload);

    TEST_ASSERT_TRUE(wait_for_frames(1, 500));
    // comfoair_can.h: "Called from a driver task -- NOT from the ISR". A consumer that takes
    // a mutex or logs is a latent crash the moment that stops holding.
    TEST_ASSERT_FALSE_MESSAGE(g_received[0].seen_in_isr,
                              "rx callback ran inside the interrupt callback");
}

void test_nothing_is_logged_from_interrupt_context(void)
{
    start_transport();

    const uint8_t payload[1] = {0x7F};
    for (int i = 0; i < 4; i++)
        fake_twai_deliver_rx(0x10FF0000u + (uint32_t)i, payload, 1, 1);
    // on_error is an interrupt callback too, and is the one most likely to grow a log line
    // the day somebody debugs the bus.
    fake_twai_go_bus_off();

    TEST_ASSERT_TRUE(wait_for_frames(4, 500));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_log_calls_in_isr(),
                                   "the ISR path must not log -- esp_log is not ISR-safe");
}

void test_a_dlc_above_eight_is_clamped(void)
{
    start_transport();

    // Classic CAN cannot carry more than 8 bytes, but the driver accepts a DLC of 9..15 on
    // the wire (esp_twai_onchip.c:584), so a malformed frame reaches the transport and must
    // not become an overrun. NOTE: deleting the clamp is caught as a memory error, not as
    // this assertion -- run the suite under ASan to see it named.
    const uint8_t payload[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    fake_twai_deliver_rx(0x10FF0002u, payload, sizeof payload, 15);

    TEST_ASSERT_TRUE(wait_for_frames(1, 500));
    TEST_ASSERT_EQUAL_size_t_MESSAGE(8, g_received[0].len, "dlc 15 must be clamped to 8");
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, g_received[0].data, 8);
}

// twai_node_receive_from_isr can fail (esp_twai_onchip.c:654 rejects a call with nothing
// pending). The frame is then lost, and the claim asserted just below -- that every frame is
// either delivered or counted -- has to survive that path too.
void test_a_receive_that_yields_nothing_is_accounted_for(void)
{
    start_transport();

    comfoair_can_stats_t before{};
    comfoair_can_get_stats(&before);

    fake_twai_deliver_rx_nothing();

    comfoair_can_stats_t after{};
    comfoair_can_get_stats(&after);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.rx_frames, after.rx_frames,
                                     "nothing was received, so nothing may be counted as a frame");
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(before.rx_dropped, after.rx_dropped,
                                            "a lost frame must leave a trace somewhere");
}

void test_an_overrun_is_counted_and_never_blocks_the_interrupt(void)
{
    start_transport();

    // A delta, not a total: the counters are reset by start(), but this test's own frames
    // are what it must measure.
    comfoair_can_stats_t before{};
    comfoair_can_get_stats(&before);

    // Hold the consumer inside its callback, then push more than the queue holds. The ISR
    // must keep returning; the loss must show up in the statistics, not in a stall.
    close_gate();

    const unsigned pushed     = 64;
    const uint8_t  payload[1] = {0x11};
    for (unsigned i = 0; i < pushed; i++)
        fake_twai_deliver_rx(0x10FF0100u + i, payload, 1, 1);

    comfoair_can_stats_t after{};
    comfoair_can_get_stats(&after);
    TEST_ASSERT_GREATER_THAN_UINT32_MESSAGE(before.rx_dropped, after.rx_dropped,
                                            "overrun was not counted");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(pushed,
                                     (after.rx_frames - before.rx_frames) +
                                         (after.rx_dropped - before.rx_dropped),
                                     "every frame is either accepted or counted as dropped");
    // The other half of the name. Swapping xQueueSendFromISR for the blocking xQueueSend --
    // exactly the bug this test forbids -- is otherwise invisible on the host, because a
    // shim queue never actually parks anybody.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_isr_blocking_calls(),
                                   "the ISR used a blocking queue send");

    open_gate();
}

// vTaskDelete stops a FreeRTOS task at whatever instruction it is executing. If stop() can
// do that while the consumer is inside the user's callback, whatever that callback holds --
// a mutex, a half-written state update -- is orphaned with no way to recover it.
void test_stop_does_not_tear_the_consumer_down_mid_callback(void)
{
    start_transport();

    g_slow_callback = true;
    const uint8_t payload[1] = {0x55};
    fake_twai_deliver_rx(0x10FF0200u, payload, 1, 1);
    sleep_ms(15);   // the consumer is now inside the callback and will be for a while yet

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK, comfoair_can_stop(),
                                  "stop() must wait the consumer out, not give up on it");

    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_rtos_tasks_killed_off_park(),
                                   "the rx task was killed while it was inside the callback; "
                                   "stop() must let it finish and return");
    // And the frame it was in the middle of delivering still arrived.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(1, (unsigned)g_received.size(),
                                   "the in-flight callback was lost");
}

// --- transmit ------------------------------------------------------------------------------

void test_transmitting_before_start_is_refused(void)
{
    const uint8_t payload[1] = {0x01};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_STATE,
                          comfoair_can_transmit(0x10000001u, payload, 1, false, 10));
}

void test_transmitting_more_than_eight_bytes_is_refused(void)
{
    start_transport();
    const uint8_t payload[9] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG,
                          comfoair_can_transmit(0x10000001u, payload, sizeof payload, false, 10));
    // Not tx_count: the driver refuses an oversized frame too (esp_twai_onchip.c:584-585), so
    // "nothing reached the wire" would have passed with the transport's own guard deleted.
    // What is under test is that comfoair_can_api.c never handed it over.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_twai_transmit_calls(),
                                   "the frame was passed to the driver instead of being refused");
}

void test_transmitting_a_null_buffer_with_a_length_is_refused(void)
{
    start_transport();
    // On the device this reaches the TX interrupt as a NULL source for the payload copy.
    TEST_ASSERT_EQUAL_INT(ESP_ERR_INVALID_ARG,
                          comfoair_can_transmit(0x10000001u, nullptr, 3, false, 10));
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_twai_transmit_calls(),
                                   "a NULL payload was passed to the driver");
}

void test_a_data_frame_goes_out_as_an_extended_frame(void)
{
    start_transport();

    const uint8_t payload[3] = {0xC0, 0xFF, 0xEE};
    TEST_ASSERT_EQUAL_INT(ESP_OK,
                          comfoair_can_transmit(0x1F0123ABu, payload, sizeof payload, false, 10));

    const fake_tx_record_t *tx = fake_twai_tx(0);
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_EQUAL_HEX32(0x1F0123ABu, tx->id);
    TEST_ASSERT_EQUAL_UINT16(3, tx->dlc);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, tx->ide, "ComfoNet is 29-bit throughout");
    TEST_ASSERT_EQUAL_UINT8(0, tx->rtr);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(payload, tx->data, sizeof payload);
}

void test_a_remote_frame_carries_no_data(void)
{
    start_transport();

    const uint8_t ignored[4] = {1, 2, 3, 4};
    TEST_ASSERT_EQUAL_INT(ESP_OK,
                          comfoair_can_transmit(0x1F0123ACu, ignored, sizeof ignored, true, 10));

    const fake_tx_record_t *tx = fake_twai_tx(0);
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(1, tx->rtr, "a poll is an RTR frame");
    TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, tx->dlc, "an RTR frame carries no data");
    TEST_ASSERT_EQUAL_size_t_MESSAGE(0, tx->buffer_len, "an RTR frame carries no data");
}

// The transport now waits for the driver before returning, so on the happy path the frame
// outlives the call. The hole is the unhappy path: twai_node_transmit_wait_all_done returns
// ESP_ERR_TIMEOUT with the frame STILL held by the controller (esp_twai.h:127-134), and
// returning that error unwinds the very stack the driver is pointing at. An unpowered unit
// acknowledges nothing, so this is the ordinary case on a bus that is not there.
//
// Asserted on WHERE the retained pointer points, not on what is readable through it: reading
// a dead frame is undefined, so an assertion on the bytes would be reporting a coincidence of
// stack layout. This is decided at submission time and cannot flip.
void test_a_transmit_that_times_out_leaves_no_dangling_frame(void)
{
    start_transport();

    // A real local of THIS function: comfoair_can_transmit's own frame sits below it and
    // ceases to exist the moment that call returns.
    volatile uint8_t marker = 0;
    const uintptr_t  here   = (uintptr_t)&marker;

    fake_twai_stall_tx(true);
    static const uint8_t payload[2] = {0xAB, 0xCD};
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_TIMEOUT,
                                  comfoair_can_transmit(0x10000005u, payload, 2, false, 5),
                                  "a send the controller never completes must report a timeout");

    const uintptr_t retained = (uintptr_t)fake_twai_in_flight_frame();
    TEST_ASSERT_FALSE_MESSAGE(retained != 0 && retained < here && here - retained < 65536u,
                              "the send returned while the controller still points into the "
                              "dead stack of comfoair_can_transmit");
}

// The second frame goes to the driver's queue, where it waits behind the first --
// esp_twai_onchip.c:292 dereferences it from the TX-done interrupt, and :246-249 does it
// again after a bus-off recovery, which is unbounded in time.
void test_a_queued_frame_survives_the_return_of_its_sender(void)
{
    start_transport();
    stall_the_controller();

    volatile uint8_t marker = 0;
    const uintptr_t  here   = (uintptr_t)&marker;

    // Static, so that only the FRAME's lifetime is under test here and not the payload's.
    static const uint8_t payload[4] = {0xDE, 0xAD, 0xBE, 0xEF};
    (void)comfoair_can_transmit(0x10000002u, payload, sizeof payload, false, 1);

    const uintptr_t retained = (uintptr_t)fake_twai_queued_frame(0);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(0, retained, "the second frame never reached the queue");
    TEST_ASSERT_FALSE_MESSAGE(retained < here && here - retained < 65536u,
                              "the driver is left pointing into the dead stack of "
                              "comfoair_can_transmit");
}

// The header states no lifetime rule for `data`, so a caller is entitled to reuse its buffer
// the instant the call returns. Either the transport copies, or that contract has to be
// written down as "until the transmission completes" -- which across a bus-off recovery is
// unbounded, and which every future caller then has to honour forever.
void test_a_queued_payload_is_not_read_back_from_the_callers_buffer(void)
{
    start_transport();
    stall_the_controller();

    uint8_t payload[4] = {0x11, 0x22, 0x33, 0x44};
    (void)comfoair_can_transmit(0x10000003u, payload, sizeof payload, false, 1);

    const uint8_t expected[4] = {0x11, 0x22, 0x33, 0x44};
    memset(payload, 0xFF, sizeof payload);   // the caller reuses its own buffer

    fake_twai_stall_tx(false);
    fake_twai_complete_tx();   // the first frame finishes and the queued one is picked up

    const fake_tx_record_t *tx = fake_twai_tx(1);
    TEST_ASSERT_NOT_NULL(tx);
    TEST_ASSERT_EQUAL_UINT8_ARRAY_MESSAGE(expected, tx->data, sizeof expected,
                                          "payload was read from the caller's buffer, late");
}

// A send the controller never completes must not look like a success. This is the failure a
// caller actually meets on a bus with nothing on it: comfoair_poll.cpp:27-33 logs it and
// carries on, which is only correct if the error arrives.
void test_a_stalled_controller_is_reported_not_swallowed(void)
{
    start_transport();
    fake_twai_stall_tx(true);

    static const uint8_t payload[1] = {0x01};
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_TIMEOUT,
                                  comfoair_can_transmit(0x10000010u, payload, 1, false, 5),
                                  "an unfinished send must be reported to the caller");
}

// --- bus health ------------------------------------------------------------------------------

void test_bus_errors_are_counted(void)
{
    start_transport();

    comfoair_can_stats_t before{};
    comfoair_can_get_stats(&before);
    fake_twai_go_bus_off();
    comfoair_can_stats_t after{};
    comfoair_can_get_stats(&after);

    TEST_ASSERT_GREATER_THAN_UINT32(before.bus_errors, after.bus_errors);
}

void test_the_controller_error_counters_are_published(void)
{
    start_transport();
    fake_twai_set_error_counts(97, 42);

    comfoair_can_stats_t s{};
    comfoair_can_get_stats(&s);
    TEST_ASSERT_TRUE(s.started);
    TEST_ASSERT_EQUAL_UINT16(97, s.tx_error_count);
    TEST_ASSERT_EQUAL_UINT16(42, s.rx_error_count);
}

// Counting errors is not the same as saying the bus is down, and only one of the two tells a
// reader whether to go and look at the wiring. This was the field the diagnostics had no way
// to express at all.
void test_the_bus_state_is_published(void)
{
    start_transport();

    comfoair_can_stats_t up{};
    comfoair_can_get_stats(&up);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_CAN_BUS_ACTIVE, up.state, "a healthy bus reads active");

    fake_twai_go_bus_off();

    comfoair_can_stats_t down{};
    comfoair_can_get_stats(&down);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_CAN_BUS_OFF, down.state, "an offline bus must say so");
    TEST_ASSERT_TRUE_MESSAGE(wait_for_recover(500), "recovery was never requested");
    TEST_ASSERT_TRUE(fake_twai_finish_recovery());

    comfoair_can_stats_t back{};
    comfoair_can_get_stats(&back);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_CAN_BUS_ACTIVE, back.state, "a recovered bus reads active");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(1, back.bus_recoveries, "the recovery was not counted");
}

// A restart is a new session on the bus. Counters that survive it make the diagnostics lie
// about the session being looked at, and a reader has no way to subtract the previous one.
void test_statistics_start_from_zero_on_a_fresh_start(void)
{
    start_transport();
    const uint8_t payload[1] = {0x42};
    fake_twai_deliver_rx(0x10FF0300u, payload, 1, 1);
    TEST_ASSERT_TRUE(wait_for_frames(1, 500));

    comfoair_can_stop();
    start_transport();

    comfoair_can_stats_t s{};
    comfoair_can_get_stats(&s);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.rx_frames, "rx_frames carried over from the last run");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.rx_dropped, "rx_dropped carried over from the last run");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, s.bus_errors, "bus_errors carried over from the last run");
}

// Nothing in this firmware reboots because a peer is absent (AGENTS.md).
// That rule only holds if the transport heals itself: bus-off is where an unpowered unit, a
// missing terminator or a shorted TX line puts the controller, and on the C6 the only path out
// is twai_node_recover() -- twai_hal_start_bus_recovery is called from nowhere else in
// the tree, and esp_driver_twai's Kconfig has no automatic-recovery option.
void test_a_bus_that_goes_off_is_brought_back(void)
{
    start_transport();
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_twai_recover_calls(),
                                   "a healthy bus must not be recovered");

    fake_twai_go_bus_off();
    TEST_ASSERT_EQUAL_INT(TWAI_ERROR_BUS_OFF, fake_twai_state());

    TEST_ASSERT_TRUE_MESSAGE(wait_for_recover(500),
                             "twai_node_recover() was never called: the bus stays dead until "
                             "the device is rebooted");
    // Neither _node_recover nor twai_hal_start_bus_recovery appears in esp_driver_twai's
    // linker.lf, so neither can be IRAM-resident; calling them from an IRAM interrupt would
    // be a flash access with the cache disabled. A task or a timer callback both satisfy this.
    TEST_ASSERT_EQUAL_UINT_MESSAGE(0, fake_twai_recover_calls_in_isr(),
                                   "recovery must not be requested from interrupt context");
}

void test_the_bus_carries_traffic_again_after_a_recovery(void)
{
    start_transport();

    static const uint8_t payload[1] = {0x01};
    fake_twai_go_bus_off();
    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_ERR_INVALID_STATE,
                                  comfoair_can_transmit(0x10000004u, payload, 1, false, 10),
                                  "a bus-off controller refuses to transmit");

    TEST_ASSERT_TRUE_MESSAGE(wait_for_recover(500), "recovery was never requested");
    TEST_ASSERT_TRUE(fake_twai_finish_recovery());

    TEST_ASSERT_EQUAL_INT_MESSAGE(ESP_OK,
                                  comfoair_can_transmit(0x10000004u, payload, 1, false, 10),
                                  "the bus must carry traffic again after recovery");
}

// A recovered bus must be left alone. Without this, a task that simply calls
// twai_node_recover() on a timer satisfies the test above while hammering a persistently
// faulty bus with no back-off -- and on a node that was merely stopped rather than bus-off,
// exiting reset mode is not even a legal thing to do.
void test_a_recovered_bus_is_not_recovered_again(void)
{
    start_transport();
    fake_twai_go_bus_off();
    TEST_ASSERT_TRUE_MESSAGE(wait_for_recover(500), "recovery was never requested");
    TEST_ASSERT_TRUE(fake_twai_finish_recovery());

    const unsigned settled = fake_twai_recover_calls();
    sleep_ms(100);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(settled, fake_twai_recover_calls(),
                                   "recovery kept firing on a healthy bus");
}

// ----------------------------------------------------------------------------------------------

int main(int, char **)
{
    UNITY_BEGIN();

    RUN_TEST(test_the_controller_takes_its_pins_and_bitrate_from_the_board);
    RUN_TEST(test_retransmission_is_bounded);
    RUN_TEST(test_callbacks_are_registered_before_the_node_is_enabled);
    RUN_TEST(test_starting_twice_is_refused);
    RUN_TEST(test_starting_without_a_callback_is_refused);
    RUN_TEST(test_stop_releases_the_controller_the_task_and_the_queue);

    RUN_TEST(test_a_received_frame_reaches_the_callback_intact);
    RUN_TEST(test_a_remote_frame_delivers_no_payload);
    RUN_TEST(test_the_callback_never_runs_in_interrupt_context);
    RUN_TEST(test_nothing_is_logged_from_interrupt_context);
    RUN_TEST(test_a_dlc_above_eight_is_clamped);
    RUN_TEST(test_a_receive_that_yields_nothing_is_accounted_for);
    RUN_TEST(test_an_overrun_is_counted_and_never_blocks_the_interrupt);
    RUN_TEST(test_stop_does_not_tear_the_consumer_down_mid_callback);

    RUN_TEST(test_transmitting_before_start_is_refused);
    RUN_TEST(test_transmitting_more_than_eight_bytes_is_refused);
    RUN_TEST(test_transmitting_a_null_buffer_with_a_length_is_refused);
    RUN_TEST(test_a_data_frame_goes_out_as_an_extended_frame);
    RUN_TEST(test_a_remote_frame_carries_no_data);
    RUN_TEST(test_a_transmit_that_times_out_leaves_no_dangling_frame);
    RUN_TEST(test_a_queued_frame_survives_the_return_of_its_sender);
    RUN_TEST(test_a_queued_payload_is_not_read_back_from_the_callers_buffer);
    RUN_TEST(test_a_stalled_controller_is_reported_not_swallowed);

    RUN_TEST(test_bus_errors_are_counted);
    RUN_TEST(test_the_controller_error_counters_are_published);
    RUN_TEST(test_the_bus_state_is_published);
    RUN_TEST(test_statistics_start_from_zero_on_a_fresh_start);
    RUN_TEST(test_a_bus_that_goes_off_is_brought_back);
    RUN_TEST(test_the_bus_carries_traffic_again_after_a_recovery);
    RUN_TEST(test_a_recovered_bus_is_not_recovered_again);

    return UNITY_END();
}
