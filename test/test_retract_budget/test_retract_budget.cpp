// THE CEILING ON A RETRACTION, AS A CONDITION A TEST CAN CALL.
//
// The subject is four lines of comfoair_mqtt_retract.c and the reason they are worth a suite of
// their own is that they were, until 2026-09-07, unreachable from any of them: comfoair_mqtt is
// esp-mqtt, FreeRTOS and a client handle, so it is not built in [env:native] at all, and REMOVING
// THE CEILING ENTIRELY LEFT EVERY HOST CASE GREEN. What the ceiling is for is in
// comfoair_mqtt_retract.c; what it costs when it is missing is an absent peer stalling the device
// (AGENTS.md) -- a publisher task that answers nothing for twelve
// seconds because a broker in a black hole is swallowing QoS 1 writes one ten-second socket timeout
// at a time.
//
// A LOOP CONDITION AND NOT A PREDICATE, which is the whole design of the thing. `budget_spent()`
// beside a `for` is two statements, and a mutation deletes the `if` and leaves the loop; this is
// one statement, and the loop cannot be written without it. That property is not testable from
// here -- it is a property of the call site -- so it is stated in the header and read in review.
//
// NO CLOCK, NO TickType_t, NO FreeRTOS. The caller does the subtraction and passes the DIFFERENCE,
// which is what makes the wrap case below expressible: at CONFIG_FREERTOS_HZ = 100 a 32-bit tick
// counter turns over every 497 days, and a device meant to run for months will do it.
#include <unity.h>

#include <cstddef>
#include <cstdint>

#include "comfoair_retract_budget.h"

void setUp(void) {}
void tearDown(void) {}

namespace {
// The two numbers the call site passes, named once so the cases below read as the loop does. 154 is
// a full WITHDRAW (the discovery tree plus the state side, measured); 200 ticks is
// RETRACT_BUDGET_MS at 100 Hz.
constexpr size_t   TOTAL  = 154;
constexpr uint32_t BUDGET = 200;
}  // namespace

void test_the_budget_is_spent_the_tick_it_is_reached_and_not_the_tick_after(void) {
  // THE BOUNDARY IS THE CASE. `>` instead of `>=` gives the retraction one extra tick, which is
  // nothing -- and is exactly why nobody would notice the same slip written the other way, as a
  // ceiling that never stops. Both directions are pinned here.
  TEST_ASSERT_TRUE(comfoair_retract_continue(0, TOTAL, BUDGET - 1, BUDGET));
  TEST_ASSERT_FALSE(comfoair_retract_continue(0, TOTAL, BUDGET, BUDGET));
  TEST_ASSERT_FALSE(comfoair_retract_continue(0, TOTAL, BUDGET + 1, BUDGET));

  // Nothing elapsed yet: the first publish always gets to happen, or a device whose tick counter
  // sat still for the length of the call would retract nothing and log a clean sweep.
  TEST_ASSERT_TRUE(comfoair_retract_continue(0, TOTAL, 0, BUDGET));
}

void test_a_budget_of_no_ticks_at_all_publishes_nothing(void) {
  // Not reachable from RETRACT_BUDGET_MS today, and pinned anyway: it is the answer that says the
  // comparison is against the budget rather than against a constant that happens to equal it.
  TEST_ASSERT_FALSE(comfoair_retract_continue(0, TOTAL, 0, 0));
}

void test_the_sweep_ends_when_the_last_topic_has_been_sent(void) {
  // The other half of the condition, and the half a budget test would silently drop: an index
  // that has reached the count must stop even with the whole budget left. Without it the loop
  // asks comfoair_ha_cleanup_topic() for topics past the end for two seconds.
  TEST_ASSERT_TRUE(comfoair_retract_continue(TOTAL - 1, TOTAL, 0, BUDGET));
  TEST_ASSERT_FALSE(comfoair_retract_continue(TOTAL, TOTAL, 0, BUDGET));
  TEST_ASSERT_FALSE(comfoair_retract_continue(TOTAL + 1, TOTAL, 0, BUDGET));

  // Nothing to retract: a REKEY on a device that has published nothing yet. It must not enter the
  // loop once, or the first thing an empty sweep does is publish an empty payload to a topic that
  // does not exist.
  TEST_ASSERT_FALSE(comfoair_retract_continue(0, 0, 0, BUDGET));
}

void test_a_tick_counter_that_wrapped_is_still_measured_by_the_difference(void) {
  // TickType_t is uint32_t and CONFIG_FREERTOS_HZ is 100 (sdkconfig.m5stack-nanoc6:2497), so it
  // turns over every 2^32 / 100 seconds -- about 497 days. The call site passes
  // `(uint32_t)(xTaskGetTickCount() - started)`, and the unsigned subtraction is what survives
  // that: a signed one, or a `now >= started + budget` written out in full, is false for the next
  // 497 days after the turn -- a retraction with no ceiling at all, on a device meant to run for
  // months. Reproduced here rather than described.
  const uint32_t started = 0xFFFFFF00u;

  const uint32_t just_before = (uint32_t)((started + BUDGET - 1u) - started);
  TEST_ASSERT_EQUAL_UINT32(BUDGET - 1, just_before);
  TEST_ASSERT_TRUE(comfoair_retract_continue(0, TOTAL, just_before, BUDGET));

  const uint32_t just_after = (uint32_t)((started + BUDGET) - started);
  TEST_ASSERT_EQUAL_UINT32(BUDGET, just_after);
  TEST_ASSERT_FALSE(comfoair_retract_continue(0, TOTAL, just_after, BUDGET));

  // And the same arithmetic taken to the far side: a counter that has come all the way round to
  // one tick BEFORE where it started is 0xFFFFFFFF ticks of elapsed time, not -1.
  TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, (uint32_t)((started - 1u) - started));
  TEST_ASSERT_FALSE(comfoair_retract_continue(0, TOTAL, (uint32_t)((started - 1u) - started), BUDGET));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_the_budget_is_spent_the_tick_it_is_reached_and_not_the_tick_after);
  RUN_TEST(test_a_budget_of_no_ticks_at_all_publishes_nothing);
  RUN_TEST(test_the_sweep_ends_when_the_last_topic_has_been_sent);
  RUN_TEST(test_a_tick_counter_that_wrapped_is_still_measured_by_the_difference);
  return UNITY_END();
}
