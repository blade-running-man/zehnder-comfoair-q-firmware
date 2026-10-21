// The PDO request plan: the hourly sweep and the one-PDO read-back after an away command.
//
// The plan is the only place the request task's decisions can be driven without a bus: what goes
// out next, in what order, and how long the task sleeps. A wrong answer here is either bus
// traffic the unit has no reason to expect or a switch that shows a stale state for an hour.
#include <unity.h>

#include <stdint.h>

#include "comfoair_poll_plan.h"

static const uint16_t PDOS[] = {16, 49, 65};
static const uint32_t HOUR = 3600000;
static comfoair_poll_plan_t g_plan;

void setUp(void) { comfoair_poll_plan_init(&g_plan, PDOS, 3, HOUR, 1000); }
void tearDown(void) {}

static void expect_send(uint16_t pdo, uint32_t now)
{
    const comfoair_poll_step_t s = comfoair_poll_plan_next(&g_plan, now);
    TEST_ASSERT_TRUE_MESSAGE(s.send, "expected a request");
    TEST_ASSERT_EQUAL_UINT16(pdo, s.pdo);
}

static void expect_wait(uint32_t wait_ms, uint32_t now)
{
    const comfoair_poll_step_t s = comfoair_poll_plan_next(&g_plan, now);
    TEST_ASSERT_FALSE_MESSAGE(s.send, "expected a wait");
    TEST_ASSERT_EQUAL_UINT32(wait_ms, s.wait_ms);
}

// Runs the boot sweep at 100 ms per frame, as the task does; returns the time after it.
static uint32_t finish_boot_sweep(void)
{
    expect_send(16, 1000);
    expect_send(49, 1100);
    expect_send(65, 1200);
    return 1300;
}

void test_the_boot_sweep_requests_every_pdo_in_order(void)
{
    const uint32_t t = finish_boot_sweep();
    expect_wait(HOUR - 300, t);  // the next sweep counts from the START of this one
}

void test_the_next_sweep_starts_an_interval_after_the_last_one_started(void)
{
    finish_boot_sweep();
    expect_wait(1, 1000 + HOUR - 1);
    expect_send(16, 1000 + HOUR);
}

void test_requested_sweeps_collapse_into_one(void)
{
    const uint32_t t = finish_boot_sweep();
    comfoair_poll_plan_request_sweep(&g_plan);
    comfoair_poll_plan_request_sweep(&g_plan);
    expect_send(16, t);
    expect_send(49, t + 100);
    expect_send(65, t + 200);
    expect_wait(HOUR - 300, t + 300);
}

void test_a_sweep_requested_during_a_sweep_runs_once_more_after_it(void)
{
    expect_send(16, 1000);
    comfoair_poll_plan_request_sweep(&g_plan);
    expect_send(49, 1100);
    expect_send(65, 1200);
    expect_send(16, 1300);
}

void test_a_readback_waits_its_delay_then_sends_once(void)
{
    const uint32_t t = finish_boot_sweep();
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, t, 1500));
    expect_wait(1500, t);
    expect_wait(500, t + 1000);
    expect_send(337, t + 1500);
    expect_wait(HOUR - 300 - 1600, t + 1600);
}

void test_repeated_readbacks_coalesce_into_one_after_the_last(void)
{
    const uint32_t t = finish_boot_sweep();
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, t, 1500));
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, t + 1000, 1500));
    expect_wait(1000, t + 1500);  // not due: the second command moved it
    expect_send(337, t + 2500);
    const comfoair_poll_step_t after = comfoair_poll_plan_next(&g_plan, t + 2600);
    TEST_ASSERT_FALSE_MESSAGE(after.send, "two commands produced two requests");
}

void test_a_readback_of_another_pdo_is_refused_while_one_is_pending(void)
{
    const uint32_t t = finish_boot_sweep();
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, t, 1500));
    TEST_ASSERT_FALSE(comfoair_poll_plan_request_readback(&g_plan, 81, t, 100));
    expect_send(337, t + 1500);
    // The slot is free again.
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 81, t + 1600, 100));
    expect_send(81, t + 1700);
}

void test_a_readback_due_during_a_sweep_slots_between_two_sweep_frames(void)
{
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, 900, 150));
    expect_send(337, 1050);  // due first, ahead of the sweep that is also due
    expect_send(16, 1150);
    expect_send(49, 1250);
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, 1250, 100));
    expect_send(337, 1350);
    expect_send(65, 1450);  // the sweep resumes where it was
}

void test_the_wait_is_the_earlier_of_sweep_and_readback(void)
{
    finish_boot_sweep();
    const uint32_t late = 1000 + HOUR - 200;
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, late, 1500));
    expect_wait(200, late);  // the sweep comes first
    expect_send(16, late + 200);
}

void test_a_readback_across_the_millisecond_wrap(void)
{
    comfoair_poll_plan_init(&g_plan, PDOS, 0, HOUR, UINT32_MAX - 500);
    const comfoair_poll_step_t first = comfoair_poll_plan_next(&g_plan, UINT32_MAX - 500);
    TEST_ASSERT_FALSE(first.send);  // an empty request list sweeps nothing
    TEST_ASSERT_TRUE(comfoair_poll_plan_request_readback(&g_plan, 337, UINT32_MAX - 500, 1500));
    expect_wait(1500, UINT32_MAX - 500);
    expect_wait(1000, UINT32_MAX);
    expect_send(337, 999);  // UINT32_MAX - 500 + 1500 wrapped
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_the_boot_sweep_requests_every_pdo_in_order);
    RUN_TEST(test_the_next_sweep_starts_an_interval_after_the_last_one_started);
    RUN_TEST(test_requested_sweeps_collapse_into_one);
    RUN_TEST(test_a_sweep_requested_during_a_sweep_runs_once_more_after_it);
    RUN_TEST(test_a_readback_waits_its_delay_then_sends_once);
    RUN_TEST(test_repeated_readbacks_coalesce_into_one_after_the_last);
    RUN_TEST(test_a_readback_of_another_pdo_is_refused_while_one_is_pending);
    RUN_TEST(test_a_readback_due_during_a_sweep_slots_between_two_sweep_frames);
    RUN_TEST(test_the_wait_is_the_earlier_of_sweep_and_readback);
    RUN_TEST(test_a_readback_across_the_millisecond_wrap);
    return UNITY_END();
}
