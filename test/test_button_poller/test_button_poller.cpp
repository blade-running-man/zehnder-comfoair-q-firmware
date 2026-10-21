// The half of the polling task that a host can reach.
//
// comfoair_button_task.c first shipped whole under #ifdef ESP_PLATFORM: a pin, a 20 ms loop, an
// erase and the only esp_restart() in this firmware, compiled to nothing off the device and
// therefore covered by nothing. Its own review found five mutations that survived it, and the
// dangerous one was not the reboot -- it was two separate presses being glued into a factory reset
// by a starved poller, which is the exact defect the decider's own review had already fixed once
// on the other side of the gap.
//
// So the three decisions that can be made without a pin were moved next door into
// comfoair_button.c, where they compile in [env:native], and this suite is what stands over them:
// what a gap means, what the LED is told, and when the reboot is allowed. What is left under the
// #ifdef is gpio_get_level(), vTaskDelay() and esp_restart().
//
// A DIRECTORY OF ITS OWN AND NOT test/test_button_gesture: that suite is 597 lines against a
// ceiling of 600 (AGENTS.md). It is also a different subject -- it pins what a hold MEANS, and this
// pins what the task around it does with the answer.
#include <unity.h>

#include "comfoair_button.h"

static const uint32_t POLL = COMFOAIR_BUTTON_POLL_MS;

// The poller's loop as it actually runs: a sample every 20 ms, through the wrapper the task uses.
// Returns the last gesture seen, because a gesture is at most one per press and dropping it is the
// defect this file exists for.
static comfoair_gesture_tier_t feed(comfoair_button_t *s, bool pressed, uint32_t from_ms,
                                   uint32_t to_ms) {
  comfoair_gesture_tier_t seen = COMFOAIR_GESTURE_NONE;
  for (uint32_t t = from_ms; t <= to_ms; t += POLL) {
    const comfoair_button_out_t out = comfoair_button_sample(s, pressed, t);
    if (out.gesture != COMFOAIR_GESTURE_NONE) seen = out.gesture;
  }
  return seen;
}

// A machine armed the way a real one is: it boots deaf, and a released sample is what wakes it.
static comfoair_button_t armed(void) {
  comfoair_button_t s;
  comfoair_button_init(&s);
  (void)comfoair_button_sample(&s, false, 0);
  return s;
}

void setUp(void) {}
void tearDown(void) {}

// --- a gap is a release, including the gap that lands inside a hold ----------------------------

void test_two_presses_glued_by_a_starved_poller_do_not_erase(void) {
  // THE CASE THIS FILE WAS WRITTEN FOR. Three seconds down, a stall of one second that swallows
  // the release entirely -- no released sample is ever observed -- and three seconds down again.
  // Through comfoair_button_step() that is one six-second hold and a SOFT reset: the owner's
  // network and UI password erased by a scheduler, not by a decision. The gap has to end the press
  // even when both samples either side of it read pressed, and only the caller knows there was one.
  comfoair_button_t s = armed();
  feed(&s, true, 100, 3100);          // three seconds down
                                      // one second of nothing: the release happened in here
  const comfoair_gesture_tier_t g = feed(&s, true, 4100, 7100);  // three seconds down again
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, g);

  // And the release that follows the SECOND press decides on the second press alone.
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, feed(&s, false, 7120, 7300));
}

void test_the_same_two_presses_erase_nothing_when_the_release_is_seen(void) {
  // The control, and the reason the case above is about the GAP and not about two presses: with a
  // single released sample in the middle the decider already got this right (its own suite pins it
  // as test_a_gap_longer_than_the_debounce_ends_the_press). The wrapper must not have changed it.
  comfoair_button_t s = armed();
  feed(&s, true, 100, 3100);
  feed(&s, false, 3120, 3120);
  feed(&s, true, 4100, 7100);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, feed(&s, false, 7120, 7300));
}

void test_a_gesture_finished_before_the_stall_is_still_delivered(void) {
  // The other direction, and it is what stops the fix being "throw away everything after a gap".
  // The owner held for eight seconds and let go; the stall happened after the release. The length
  // frozen at the last sample the poller can vouch for is the true one, so the gesture the owner
  // actually made still arrives.
  comfoair_button_t s = armed();
  feed(&s, true, 100, 8100);
  const comfoair_button_out_t out = comfoair_button_sample(&s, false, 9100);  // one second later
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_SOFT, out.gesture);
}

void test_a_hesitation_shorter_than_the_debounce_does_not_end_a_hold(void) {
  // The threshold from the other side. A sample that is merely LATE -- 40 ms instead of 20 -- is
  // still inside the debounce, and the hold it lands in must survive: this task runs at priority 4
  // and a late sample is ordinary, while a hold cut in half is a gesture the owner has to repeat.
  comfoair_button_t s = armed();
  feed(&s, true, 100, 3000);
  (void)comfoair_button_sample(&s, true, 3040);  // 40 ms after the last one, under the 60
  const comfoair_gesture_tier_t g = feed(&s, true, 3060, 5200);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_SOFT, feed(&s, false, 5220, 5400));
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, g);  // and nothing fired while it was still down
}

void test_the_gap_is_measured_against_the_debounce_and_not_a_number_beside_it(void) {
  // Exactly the debounce ends the press; one sample period less does not. Both directions, because
  // a guard that fires early costs gestures and one that fires late costs configurations.
  // One sample period UNDER the debounce: the same press carries on, and the release at 5.12 s is
  // measured from the anchor it started with.
  comfoair_button_t early = armed();
  feed(&early, true, 100, 3000);
  (void)comfoair_button_sample(&early, true, 3000 + COMFOAIR_BUTTON_DEBOUNCE_MS - POLL);
  feed(&early, true, 3060, 5200);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_SOFT, feed(&early, false, 5220, 5400));

  // Exactly the debounce: the press ended in the gap, so what follows is a new one and the release
  // two and a bit seconds later lands in the dead zone -- the same release that was SOFT above.
  comfoair_button_t late = armed();
  feed(&late, true, 100, 3000);
  feed(&late, true, 3000 + COMFOAIR_BUTTON_DEBOUNCE_MS, 5400);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, feed(&late, false, 5420, 5600));
}

void test_the_first_sample_of_all_is_not_a_gap(void) {
  // There is no previous sample to have missed, and treating the boot as a gap would feed a
  // released sample the machine has not earned -- which is the one thing that arms it (the download
  // strap, docs/device/button.md). A button held through power-on must stay mute.
  comfoair_button_t s;
  comfoair_button_init(&s);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, feed(&s, true, 10000, 30000));
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_NONE, feed(&s, false, 30020, 30200));
}

// --- what the LED is told ----------------------------------------------------------------------

static comfoair_button_out_t out_of(comfoair_gesture_tier_t tier, bool changed, uint32_t tier_ms) {
  comfoair_button_out_t out = {COMFOAIR_GESTURE_NONE, tier, 0, tier_ms, changed};
  return out;
}

void test_a_tier_that_changes_to_none_takes_the_overlay_down_and_does_not_flash(void) {
  // The half of the contract that is easy to get backwards, and the mutation that survived the
  // first version of this task: a flash on the change to NONE tells the owner "confirmed" at the
  // exact moment they are being told "let go now and nothing happens" (comfoair_button.h, the two
  // rules).
  comfoair_button_flash_t flash = {false, 0};
  const comfoair_button_out_t out = out_of(COMFOAIR_GESTURE_NONE, true, 0);
  const comfoair_button_draw_t plan = comfoair_button_plan_draw(&out, 1000, &flash);
  TEST_ASSERT_TRUE(plan.clear);
  TEST_ASSERT_FALSE(plan.push);
  TEST_ASSERT_FALSE(flash.on);
}

void test_nothing_is_drawn_while_no_tier_is_held(void) {
  // The dead zone and the idle button: the overlay is already down and must not be taken down
  // again on every sample, or the owner's IDENTIFY and OTA pictures flicker under a resting finger.
  comfoair_button_flash_t flash = {false, 0};
  const comfoair_button_out_t out = out_of(COMFOAIR_GESTURE_NONE, false, 0);
  const comfoair_button_draw_t plan = comfoair_button_plan_draw(&out, 2000, &flash);
  TEST_ASSERT_FALSE(plan.clear);
  TEST_ASSERT_FALSE(plan.push);
}

void test_entering_a_tier_flashes_at_full_brightness_and_then_ramps(void) {
  // The confirming flash, drawn with a tier length of zero because that is what the RAMP branch
  // renders at full brightness. The ramp itself comes back the moment the flash is over, with the
  // tier's real length so the fill reaches the ceiling on the next threshold.
  comfoair_button_flash_t flash = {false, 0};
  comfoair_button_out_t out = out_of(COMFOAIR_GESTURE_SOFT, true, 10000);

  comfoair_button_draw_t plan = comfoair_button_plan_draw(&out, 5000, &flash);
  TEST_ASSERT_TRUE(plan.push);
  TEST_ASSERT_FALSE(plan.clear);
  TEST_ASSERT_EQUAL_UINT32(comfoair_led_button_arg(COMFOAIR_GESTURE_SOFT, 0), plan.arg);

  out = out_of(COMFOAIR_GESTURE_SOFT, false, 10000);
  plan = comfoair_button_plan_draw(&out, 5000 + COMFOAIR_BUTTON_FLASH_MS - 1, &flash);
  TEST_ASSERT_EQUAL_UINT32(comfoair_led_button_arg(COMFOAIR_GESTURE_SOFT, 0), plan.arg);

  plan = comfoair_button_plan_draw(&out, 5000 + COMFOAIR_BUTTON_FLASH_MS, &flash);
  TEST_ASSERT_EQUAL_UINT32(comfoair_led_button_arg(COMFOAIR_GESTURE_SOFT, 10000), plan.arg);
  TEST_ASSERT_FALSE(flash.on);
}

void test_the_same_tier_is_pushed_with_the_same_arg_on_every_sample(void) {
  // Not an optimisation and not laziness: comfoair_led's driver refuses to restamp a BUTTON push
  // whose arg repeats, which is what lets a poller push on every one of its own samples without
  // resetting the ramp to zero fifty times a second. An arg that wobbled between samples would
  // restamp, the fill would sit near nought for the whole hold, and no host case in comfoair_led
  // could see it.
  comfoair_button_flash_t flash = {false, 0};
  const comfoair_button_out_t out = out_of(COMFOAIR_GESTURE_HARD, false, 10000);
  const comfoair_button_draw_t a = comfoair_button_plan_draw(&out, 20000, &flash);
  const comfoair_button_draw_t b = comfoair_button_plan_draw(&out, 20020, &flash);
  const comfoair_button_draw_t c = comfoair_button_plan_draw(&out, 20040, &flash);
  TEST_ASSERT_EQUAL_UINT32(a.arg, b.arg);
  TEST_ASSERT_EQUAL_UINT32(a.arg, c.arg);
  TEST_ASSERT_EQUAL_UINT32(comfoair_led_button_arg(COMFOAIR_GESTURE_HARD, 10000), a.arg);
}

void test_every_tier_boundary_of_one_hold_is_drawn_in_order(void) {
  // The whole hold, through the real decider and the real plan: white appears, the dead zone takes
  // it away, amber arrives with a flash, red arrives with a flash, and twenty-five seconds takes
  // the overlay down without one. Four pushes-after-a-change and two clears, and no fifth picture.
  comfoair_button_t s = armed();
  comfoair_button_flash_t flash = {false, 0};
  unsigned flashes = 0, clears = 0;
  comfoair_gesture_tier_t flashed[8] = {COMFOAIR_GESTURE_NONE};

  for (uint32_t t = 100; t <= 26000; t += POLL) {
    const comfoair_button_out_t out = comfoair_button_sample(&s, true, t);
    const comfoair_button_draw_t plan = comfoair_button_plan_draw(&out, t, &flash);
    if (plan.clear) clears++;
    if (plan.push && out.tier_changed && flashes < 8) flashed[flashes++] = out.tier;
  }
  TEST_ASSERT_EQUAL_UINT32(3u, flashes);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_SETUP, flashed[0]);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_SOFT, flashed[1]);
  TEST_ASSERT_EQUAL(COMFOAIR_GESTURE_HARD, flashed[2]);
  // One for the dead zone at a second, one for the stuck button at twenty-five.
  TEST_ASSERT_EQUAL_UINT32(2u, clears);
}

// --- when the reboot is allowed ----------------------------------------------------------------

void test_the_reboot_gate_needs_the_debounce_in_released_samples(void) {
  // One reading is not an answer. The confirming animation is driving the WS2812 line beside this
  // pin while the gate runs, and hardware scenario HV-BTN-07 has the owner press the button
  // again during it -- so the gate asks for the same 60 ms of agreement everything else does.
  uint8_t n = 0;
  for (unsigned i = 1; i < COMFOAIR_BUTTON_RELEASE_SAMPLES; i++)
    TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, false));
  TEST_ASSERT_TRUE(comfoair_button_release_confirmed(&n, false));
}

void test_a_finger_back_on_the_pin_starts_the_gate_again(void) {
  // The download strap, and the only reason this predicate is not `!pressed`. The owner who
  // presses again inside the confirmation must not be able to reach a restart by having been
  // released for two samples out of three.
  uint8_t n = 0;
  TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, false));
  TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, false));
  TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, true));
  TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, false));
  TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, false));
  TEST_ASSERT_TRUE(comfoair_button_release_confirmed(&n, false));
}

void test_the_gate_never_answers_yes_on_a_pressed_sample(void) {
  // However long the button has been up, the sample that reads pressed is a no. Anything else is
  // esp_restart() with a finger on the pin the ROM reads at reset.
  uint8_t n = 0;
  for (unsigned i = 0; i < 50; i++) (void)comfoair_button_release_confirmed(&n, false);
  TEST_ASSERT_FALSE(comfoair_button_release_confirmed(&n, true));
}

void test_only_the_fifteen_second_tier_erases_everything(void) {
  // Five seconds takes the network and the owner's password; fifteen takes the broker settings too
  // and sends the device out of the house. Swapping them is one character in the task and destroys
  // either the wrong thing or not enough of it.
  TEST_ASSERT_TRUE(comfoair_button_tier_erases_everything(COMFOAIR_GESTURE_HARD));
  TEST_ASSERT_FALSE(comfoair_button_tier_erases_everything(COMFOAIR_GESTURE_SOFT));
  TEST_ASSERT_FALSE(comfoair_button_tier_erases_everything(COMFOAIR_GESTURE_SETUP));
  TEST_ASSERT_FALSE(comfoair_button_tier_erases_everything(COMFOAIR_GESTURE_NONE));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_two_presses_glued_by_a_starved_poller_do_not_erase);
  RUN_TEST(test_the_same_two_presses_erase_nothing_when_the_release_is_seen);
  RUN_TEST(test_a_gesture_finished_before_the_stall_is_still_delivered);
  RUN_TEST(test_a_hesitation_shorter_than_the_debounce_does_not_end_a_hold);
  RUN_TEST(test_the_gap_is_measured_against_the_debounce_and_not_a_number_beside_it);
  RUN_TEST(test_the_first_sample_of_all_is_not_a_gap);
  RUN_TEST(test_a_tier_that_changes_to_none_takes_the_overlay_down_and_does_not_flash);
  RUN_TEST(test_nothing_is_drawn_while_no_tier_is_held);
  RUN_TEST(test_entering_a_tier_flashes_at_full_brightness_and_then_ramps);
  RUN_TEST(test_the_same_tier_is_pushed_with_the_same_arg_on_every_sample);
  RUN_TEST(test_every_tier_boundary_of_one_hold_is_drawn_in_order);
  RUN_TEST(test_the_reboot_gate_needs_the_debounce_in_released_samples);
  RUN_TEST(test_a_finger_back_on_the_pin_starts_the_gate_again);
  RUN_TEST(test_the_gate_never_answers_yes_on_a_pressed_sample);
  RUN_TEST(test_only_the_fifteen_second_tier_erases_everything);
  return UNITY_END();
}
