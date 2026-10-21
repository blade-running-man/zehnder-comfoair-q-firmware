// The gesture as a pure function of (pressed, now_ms) -- docs/device/button.md (Structure). The
// decider never reads a pin, never sleeps and never asks the clock: the sample and the time both
// arrive as arguments, which is the only reason "released at 4 999 ms" and "a hold that straddles
// the millisecond wrap" are cases here instead of things somebody once tried with a finger.
//
// THIS FILE INCLUDES NEITHER board.h NOR A SINGLE ESP-IDF HEADER, and the omission is itself an
// assertion: that this suite links in [env:native] is the proof that no pin number and no
// active-low inversion live inside the decider. Inversion is applied once, at the sample, by the
// poller task (comfoair_button_task.c); the day the Olimex board with the button on another pin
// arrives, the board file changes and this file does not.
//
// The windows below are TRANSCRIBED from the gesture table in docs/device/button.md (Gestures and
// timings) and never read back from the implementation -- a table that answers a test with its
// own contents proves nothing (test/test_led_pattern/test_led_pattern.cpp:6-7).
#include <stdio.h>
#include <unity.h>

#include "comfoair_button.h"

// The poll period is 20 ms (50 Hz, COMFOAIR_BUTTON_POLL_MS in docs/device/button.md), so every
// case below feeds samples on that grid and never at the exact instant it is interested in. That
// is the point -- the owner's finger does not land on a sample boundary either.
static const uint32_t POLL_MS = 20;

// The gesture table, transcribed. DEBOUNCE_MS is the bottom of the first window and not a separate
// concept: below it there is no press at all.
static const uint32_t DEBOUNCE_MS  = 60;
static const uint32_t SETUP_END_MS = 1000;
static const uint32_t SOFT_MS      = 5000;
static const uint32_t HARD_MS      = 15000;
static const uint32_t STUCK_MS     = 25000;

// The press starts here, not at zero. Nothing in the decider may measure from boot: a gesture at
// the four-hour mark is the same gesture as one at the fourth second.
static const uint32_t ANCHOR = 1000;

void setUp(void) {}
void tearDown(void) {}

// A machine whose FIRST sample reads released (docs/device/button.md, The GPIO9 boot-strap
// caveat). Every case that is not about the download strap starts here, because a machine whose
// first sample reads pressed is required to stay silent forever after -- which is what the two
// strap cases assert.
static comfoair_button_t idle_machine(void) {
  comfoair_button_t s;
  comfoair_button_init(&s);
  comfoair_button_step(&s, false, 0);
  return s;
}

// Pressed samples on the poll grid over [from, until), i.e. up to but not including the moment the
// finger leaves. Written as a count so the arithmetic is modular and a hold across the 32-bit wrap
// feeds the same number of samples as any other.
static comfoair_button_out_t hold_until(comfoair_button_t *s, uint32_t from, uint32_t until) {
  comfoair_button_out_t out{};
  for (uint32_t n = 0; n * POLL_MS < (uint32_t)(until - from); n++) {
    out = comfoair_button_step(s, true, (uint32_t)(from + n * POLL_MS));
  }
  return out;
}

// Released samples from `at` onward, long past the release debounce, returning the ONE gesture the
// release produced. A second non-NONE return would mean one press decided two things, so it fails
// here rather than being averaged away by the caller.
static comfoair_gesture_tier_t release_at(comfoair_button_t *s, uint32_t at) {
  comfoair_gesture_tier_t seen = COMFOAIR_GESTURE_NONE;
  for (uint32_t n = 0; n * POLL_MS <= 10 * DEBOUNCE_MS; n++) {
    comfoair_button_out_t out = comfoair_button_step(s, false, (uint32_t)(at + n * POLL_MS));
    if (out.gesture != COMFOAIR_GESTURE_NONE) {
      TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, seen, "one press decided twice");
      seen = out.gesture;
    }
  }
  return seen;
}

// A whole gesture from an idle machine: down at ANCHOR, up `held_ms` later.
static comfoair_gesture_tier_t press_for(uint32_t held_ms) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + held_ms));
  return release_at(&s, (uint32_t)(ANCHOR + held_ms));
}

// Every tier the decider announced while the finger was down, in order. The array is sized well
// above the four values the enumeration has: an implementation that announced a tier twice must
// overflow this and say so, not wrap around and look tidy.
struct Announced {
  comfoair_gesture_tier_t tier[32];
  uint32_t                at_ms[32];
  int                     n;
};

static void hold_recording(comfoair_button_t *s, uint32_t from, uint32_t until, Announced *a) {
  for (uint32_t n = 0; n * POLL_MS < (uint32_t)(until - from); n++) {
    uint32_t              t   = (uint32_t)(from + n * POLL_MS);
    comfoair_button_out_t out = comfoair_button_step(s, true, t);
    if (out.tier_changed) {
      TEST_ASSERT_LESS_THAN_INT_MESSAGE(32, a->n, "more announcements than a gesture has tiers");
      a->tier[a->n]  = out.tier;
      a->at_ms[a->n] = t;
      a->n++;
    }
  }
}

// The short press is the one outcome that erases nothing (docs/device/button.md, The setup press).
// It opens the setup window through comfoair_prov_on_setup_requested(), which touches four fields
// that already exist and no namespace at all -- so "nothing is erased" is asserted here as "the
// two erasing tiers are never what a tap returns", the only form that statement has at this layer.
static void test_a_short_press_asks_for_the_setup_window(void) {
  const uint32_t held[] = {DEBOUNCE_MS + POLL_MS, SETUP_END_MS - POLL_MS};
  for (uint32_t i = 0; i < 2; i++) {
    comfoair_gesture_tier_t g = press_for(held[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_SETUP, g, "a press under a second is SETUP");
    TEST_ASSERT_NOT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, g);
    TEST_ASSERT_NOT_EQUAL_INT(COMFOAIR_GESTURE_HARD, g);
  }
}

static void test_a_tap_opens_the_setup_window(void) {
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SETUP, press_for(200));
}

// The two-level reset (docs/device/button.md): the fifth second stays the erasing one.
// comfoair_config.c:90-95 makes SOFT drop cfg_wifi, cfg_owner and the legacy namespace and keep
// cfg_app -- the broker, the durations and the name. This suite cannot see a namespace; what it can
// pin is that the tier which means all that is the one the fifth second returns.
static void test_five_seconds_is_the_soft_wipe(void) {
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, press_for(SOFT_MS + POLL_MS));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, press_for(8000));
}

static void test_fifteen_seconds_is_the_hard_wipe(void) {
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, press_for(HARD_MS + POLL_MS));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, press_for(20000));
}

// --- the tier, which is what the LED draws ------------------------------------------------

// comfoair_led.h: the tier is what a release RIGHT NOW would decide, and the
// overlay is pushed on the sample that changes it. The flash moved out of the ramp onto this edge
// (comfoair_led_pattern.c:149-153): an edge firing twice is a flash firing twice.
static void test_the_tier_is_announced_exactly_once_per_threshold(void) {
  comfoair_button_t s = idle_machine();
  Announced         a{};
  hold_recording(&s, ANCHOR, (uint32_t)(ANCHOR + STUCK_MS + 5000), &a);

  TEST_ASSERT_EQUAL_INT_MESSAGE(5, a.n, "five edges: SETUP, the dead zone, SOFT, HARD, and giving up");
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SETUP, a.tier[0]);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, a.tier[1]);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, a.tier[2]);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, a.tier[3]);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, a.tier[4]);
}

// The flag means "the tier CHANGED", never "flash now", and the difference is a rule the poller
// must not have to derive for itself. Three of the changes are the overlay coming OFF -- the dead
// zone at one second, the stuck button given up at twenty-five, and the sample that commits the
// gesture -- and the caller needs every one of them, or the last colour keeps burning after the
// gesture has ended. What it must not do is flash there: a confirming flash at the moment the
// owner is being told "nothing will happen now" says the opposite of what happened.
static void test_a_tier_that_changes_to_none_is_not_a_flash(void) {
  comfoair_button_t s = idle_machine();
  Announced         a{};
  hold_recording(&s, ANCHOR, (uint32_t)(ANCHOR + STUCK_MS + 1000), &a);

  // Five changes, and only three of them are tiers being ENTERED.
  TEST_ASSERT_EQUAL_INT(5, a.n);
  int flashes = 0;
  int clears  = 0;
  for (int i = 0; i < a.n; i++) {
    if (a.tier[i] == COMFOAIR_GESTURE_NONE) {
      clears++;
    } else {
      flashes++;
    }
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(3, flashes, "a flash for each of the three tiers, and no more");
  TEST_ASSERT_EQUAL_INT_MESSAGE(2, clears, "the dead zone and the stuck button are not flashes");
  TEST_ASSERT_EQUAL_UINT32(ANCHOR + SETUP_END_MS, a.at_ms[1]);
  TEST_ASSERT_EQUAL_UINT32(ANCHOR + STUCK_MS, a.at_ms[4]);

  // And the third of the three: the sample that commits the gesture carries the outcome, the empty
  // tier and the change all at once. Everything before it is the overlay still standing at the
  // frozen tier -- the finger is off, and the amber must not go out until the gesture is decided.
  comfoair_button_t g = idle_machine();
  hold_until(&g, ANCHOR, (uint32_t)(ANCHOR + 8000));
  bool committed = false;
  for (uint32_t n = 0; n * POLL_MS <= 10 * DEBOUNCE_MS; n++) {
    comfoair_button_out_t out = comfoair_button_step(&g, false, (uint32_t)(ANCHOR + 8000 + n * POLL_MS));
    if (out.gesture == COMFOAIR_GESTURE_NONE) {
      TEST_ASSERT_FALSE_MESSAGE(out.tier_changed, "the overlay moved while the outcome was pending");
      continue;
    }
    TEST_ASSERT_FALSE_MESSAGE(committed, "one press committed twice");
    committed = true;
    TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, out.gesture);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, out.tier, "the overlay stayed up");
    TEST_ASSERT_TRUE_MESSAGE(out.tier_changed, "nobody was told to take the overlay down");
  }
  TEST_ASSERT_TRUE(committed);
}

// The LED contract is pinned in the decider, not in the driver. The grid is 20 ms,
// so each edge lands on the first sample at or after its threshold.
static void test_the_tier_steps_at_one_five_and_fifteen_seconds(void) {
  comfoair_button_t s = idle_machine();
  Announced         a{};
  hold_recording(&s, ANCHOR, (uint32_t)(ANCHOR + HARD_MS + 1000), &a);

  TEST_ASSERT_EQUAL_INT(4, a.n);
  TEST_ASSERT_EQUAL_UINT32(ANCHOR + DEBOUNCE_MS, a.at_ms[0]);
  TEST_ASSERT_EQUAL_UINT32(ANCHOR + SETUP_END_MS, a.at_ms[1]);
  TEST_ASSERT_EQUAL_UINT32(ANCHOR + SOFT_MS, a.at_ms[2]);
  TEST_ASSERT_EQUAL_UINT32(ANCHOR + HARD_MS, a.at_ms[3]);
}

// comfoair_led.h: the ramp means "how much is left until the next tier", so it fills over the
// tier's own length and reaches the ceiling exactly at the boundary. Short by one sample and the
// owner sees a full bar with a second still to hold -- the misreading that erases by accident.
static void test_the_tier_length_carries_the_ramp_exactly_to_the_next_threshold(void) {
  comfoair_button_t s      = idle_machine();
  const uint32_t    ends[] = {DEBOUNCE_MS, SETUP_END_MS, SOFT_MS, HARD_MS, STUCK_MS};
  const size_t      rows   = sizeof(ends) / sizeof(ends[0]);
  size_t            next   = 0;

  for (uint32_t n = 0; n * POLL_MS < STUCK_MS; n++) {
    uint32_t              t   = (uint32_t)(ANCHOR + n * POLL_MS);
    comfoair_button_out_t out = comfoair_button_step(&s, true, t);
    while (next < rows - 1 && n * POLL_MS >= ends[next]) next++;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(ANCHOR + ends[next],
                                     out.tier_since_ms + out.tier_ms,
                                     "the ramp must reach the ceiling on the threshold itself");
    TEST_ASSERT_TRUE_MESSAGE(out.tier_since_ms + out.tier_ms > t, "the ramp is already full");
  }
}

// Nothing new happens inside HARD -- the frame at 24.9 s equals the frame at 15.0 s, because there
// is no fourth tier and an owner who overheld must not be left guessing what they did. The case
// stops at 25 s and not later: that is where the stuck-button row of the gesture table gives the
// press up, and test_the_tier_returns_to_none_after_twenty_five_seconds pins what follows.
static void test_holding_past_hard_stays_hard(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + HARD_MS));
  comfoair_button_out_t at_fifteen = comfoair_button_step(&s, true, (uint32_t)(ANCHOR + HARD_MS));

  for (uint32_t t = HARD_MS; t < STUCK_MS; t += POLL_MS) {
    comfoair_button_out_t out = comfoair_button_step(&s, true, (uint32_t)(ANCHOR + t));
    TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, out.tier);
    TEST_ASSERT_EQUAL_UINT32(at_fifteen.tier_since_ms, out.tier_since_ms);
    TEST_ASSERT_EQUAL_UINT32(at_fifteen.tier_ms, out.tier_ms);
    TEST_ASSERT_FALSE_MESSAGE(out.tier_changed, "a second announcement inside HARD is a second flash");
  }
}

// What the owner sees once the gesture is already impossible. The overlay comes off and the phase
// colour returns -- the honest picture, and a known weakness: "you held too long" is told only by
// a signal disappearing, on a channel an owner on a ladder may not be watching. The non-optical
// check is the setup SSID on the air (or not) once the button is released.
static void test_the_tier_returns_to_none_after_twenty_five_seconds(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + STUCK_MS));
  for (uint32_t t = STUCK_MS; t < 60000; t += 500) {
    comfoair_button_out_t out = comfoair_button_step(&s, true, (uint32_t)(ANCHOR + t));
    TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, out.tier);
    // NONE alone does not prove the machine gave the press up: the last row of the table answers
    // NONE for any hold past 25 s whether or not the state ever changed. An empty ramp does prove
    // it -- a machine still counting hands out the window it is inside.
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, out.tier_since_ms, "still counting a press it gave up on");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, out.tier_ms, "still counting a press it gave up on");
  }
}

// --- release, which is the whole safety argument ------------------------------------------

// Reaching a threshold arms and nothing else; the gesture is committed by the RELEASE -- and that
// is also why esp_restart() after an erase is safe. By the time the reset path reboots, the button
// has been up for the whole confirming animation, so "button held while power is applied" -- the
// download strap (docs/device/button.md, The GPIO9 boot-strap caveat) -- is a state the gesture
// cannot produce. The loop below lands on 5 000, 15 000 and 24 900 ms among every other sample, so
// a separate "no tier fires while the button is still down" case would be redundant: it could not
// fail unless this one failed first.
static void test_the_gesture_completes_on_release_and_never_on_the_threshold(void) {
  comfoair_button_t s = idle_machine();
  for (uint32_t n = 0; n * POLL_MS <= STUCK_MS + 5000; n++) {
    comfoair_button_out_t out = comfoair_button_step(&s, true, (uint32_t)(ANCHOR + n * POLL_MS));
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, out.gesture,
                                  "a threshold decided a gesture while the finger was still down");
  }

  // Not even the release EDGE decides: the outcome waits out the release debounce, so the finger
  // has been off the button for at least that long before the poller has anything to act on --
  // and for the whole confirming animation before it reboots.
  comfoair_button_t g = idle_machine();
  hold_until(&g, ANCHOR, (uint32_t)(ANCHOR + 20000));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE,
                        comfoair_button_step(&g, false, (uint32_t)(ANCHOR + 20000)).gesture);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE,
                        comfoair_button_step(&g, false, (uint32_t)(ANCHOR + 20020)).gesture);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, release_at(&g, (uint32_t)(ANCHOR + 20040)));
}

// The outcome is the tier at the FIRST released sample, so a release inside a window yields that
// window's tier and never the one the finger did not reach. 4.9 s is NONE, not SETUP: 1...5 s is
// a dead zone, and the dead zone is load-bearing -- it is what keeps "the finger slipped" and "a
// shoulder leant on the panel" from opening an access point or erasing anything.
static void test_releasing_before_a_threshold_yields_the_tier_below(void) {
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SETUP, press_for(SETUP_END_MS - 100));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, press_for(SOFT_MS - 100));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, press_for(HARD_MS - 100));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, press_for(STUCK_MS - 100));
}

// The whole gesture table as one sweep, so that a boundary moved by a refactor cannot hide in the
// gap between two named cases. It steps 10 ms across every window, so it passes through 3 000,
// 4 900, 5 100, 14 900, 15 100 and 20 000 -- release times that deliberately have no case of their
// own: six cases that cannot fail unless this one fails first are six chances to believe the
// suite is bigger than it is. The step is half the poll period and the range is the table's own
// last row plus a window -- both derived from the table above, so widening a window here cannot
// silently shrink the sweep into a sample of it.
static void test_every_release_time_agrees_with_the_table_of_section_seven_two(void) {
  struct Window {
    uint32_t                until;
    comfoair_gesture_tier_t tier;
  };
  // The first row ends just PAST the debounce rather than on it, and that is the sampling grid
  // speaking, not the table: a release at exactly 60 ms leaves the last pressed sample sitting at
  // 40 ms, so 60 ms of continuous contact were never observed. From 61 ms upward there is a sample
  // at 60 and the press is real. A gesture that hangs on a 20 ms grid phase at its very shortest
  // window is the sampling's own arithmetic (a 20 ms poll is +/-20 ms on every threshold), not a
  // defect to paper over.
  const Window table[] = {
      {DEBOUNCE_MS + 1, COMFOAIR_GESTURE_NONE}, {SETUP_END_MS, COMFOAIR_GESTURE_SETUP},
      {SOFT_MS, COMFOAIR_GESTURE_NONE},         {HARD_MS, COMFOAIR_GESTURE_SOFT},
      {STUCK_MS, COMFOAIR_GESTURE_HARD},        {STUCK_MS + 5000, COMFOAIR_GESTURE_NONE},
  };
  const size_t   rows = sizeof(table) / sizeof(table[0]);
  const uint32_t last = table[rows - 1].until;

  size_t row = 0;
  for (uint32_t held = 0; held < last; held += POLL_MS / 2) {
    while (row < rows - 1 && held >= table[row].until) row++;
    char msg[64];
    snprintf(msg, sizeof(msg), "release at %u ms", (unsigned)held);
    TEST_ASSERT_EQUAL_INT_MESSAGE(table[row].tier, press_for(held), msg);
  }
}

// --- the download strap, which is why the gesture is a hold after boot --------------------

// A button already down on the first sample is the download-strap case, and it must be mute at
// every length. Anything else is a device that erases itself because somebody powered it up with a
// finger on the panel.
static void test_a_button_already_down_at_the_first_sample_can_never_produce_a_gesture(void) {
  const uint32_t lengths[] = {200, 3000, SOFT_MS + 1000, HARD_MS + 1000, 60000};
  for (uint32_t i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
    comfoair_button_t s;
    comfoair_button_init(&s);
    comfoair_button_step(&s, true, 0);  // the very first sample, and it reads pressed
    hold_until(&s, 0, lengths[i]);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, release_at(&s, lengths[i]),
                                  "a hold that began before the firmware did decided something");
  }
}

static void test_a_button_held_since_boot_works_normally_after_it_is_released(void) {
  comfoair_button_t s;
  comfoair_button_init(&s);
  comfoair_button_step(&s, true, 0);
  hold_until(&s, 0, 4000);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, release_at(&s, 4000));

  hold_until(&s, 10000, (uint32_t)(10000 + SOFT_MS + 1000));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, release_at(&s, (uint32_t)(10000 + SOFT_MS + 1000)));
}

// 60 ms of continuous contact, three consecutive samples of it, and no majority vote.
// Chatter alternating faster than that must not accumulate towards a press -- if it did, a burst
// of interference from the WS2812 line next to GPIO9 would spell a tap.
static void test_bounce_does_not_start_a_gesture(void) {
  comfoair_button_t s = idle_machine();
  for (uint32_t n = 0; n * POLL_MS < 30000; n++) {
    comfoair_button_out_t out = comfoair_button_step(&s, (n % 2) == 0, (uint32_t)(ANCHOR + n * POLL_MS));
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, out.gesture, "chatter decided a gesture");
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, out.tier, "chatter lit the overlay");
    TEST_ASSERT_FALSE_MESSAGE(out.tier_changed, "chatter announced a tier");
  }
}

static void test_a_pulse_shorter_than_the_debounce_is_not_a_press(void) {
  comfoair_button_t s = idle_machine();
  comfoair_button_out_t out = comfoair_button_step(&s, true, ANCHOR);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, out.tier);
  TEST_ASSERT_FALSE(out.tier_changed);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, release_at(&s, ANCHOR + POLL_MS));
}

// The other half of that rule, and the half nothing guarded: a gap WIDER than the debounce is a
// release, and what follows it is a NEW press. The poller runs at priority 4, below everything
// that may not miss a frame (docs/device/button.md), so a starved sampler is designed for and not
// hypothetical -- and glued presses mean three seconds, a pause and three more erase the owner's
// network.
static void test_a_gap_longer_than_the_debounce_ends_the_press(void) {
  // Three seconds down, one released sample seen, two seconds of nothing, three seconds down.
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + 3000));
  comfoair_button_step(&s, false, (uint32_t)(ANCHOR + 3000));
  hold_until(&s, (uint32_t)(ANCHOR + 5000), (uint32_t)(ANCHOR + 8000));
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_NONE, release_at(&s, (uint32_t)(ANCHOR + 8000)),
                                "two presses either side of a two-second gap erased the network");

  // And the anchor really restarted, rather than the outcome merely landing in another window: a
  // four-second hold, a gap, and a tap is a tap.
  comfoair_button_t g = idle_machine();
  hold_until(&g, ANCHOR, (uint32_t)(ANCHOR + 4000));
  comfoair_button_step(&g, false, (uint32_t)(ANCHOR + 4000));
  hold_until(&g, (uint32_t)(ANCHOR + 6000), (uint32_t)(ANCHOR + 6500));
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_SETUP, release_at(&g, (uint32_t)(ANCHOR + 6500)),
                                "a tap after a pause was counted as the tail of an older press");
}

// The finger is off, so the ramp stands still: what is shown between release and outcome is the
// tier now FIXED, not the one the clock is still walking towards. A release at 14.98 s that let it
// run would flash red -- the flash rule fires on a tier ENTERED -- and then do a soft reset, the
// confirmation and the act disagreeing, which is what the overlay exists to prevent.
static void test_the_ramp_does_not_grow_while_the_release_is_being_debounced(void) {
  const struct {
    uint32_t                held;
    comfoair_gesture_tier_t frozen;
  } cases[] = {
      {SOFT_MS - POLL_MS, COMFOAIR_GESTURE_NONE},  // 4 980 ms: the dead zone, and it stays dead
      {HARD_MS - POLL_MS, COMFOAIR_GESTURE_SOFT},  // 14 980 ms: soft, and it must not turn red
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    comfoair_button_t s = idle_machine();
    hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + cases[i].held));
    const uint32_t edge      = (uint32_t)(ANCHOR + cases[i].held);
    bool           committed = false;
    for (uint32_t n = 0; n * POLL_MS <= 5 * DEBOUNCE_MS; n++) {
      comfoair_button_out_t out = comfoair_button_step(&s, false, (uint32_t)(edge + n * POLL_MS));
      if (out.gesture != COMFOAIR_GESTURE_NONE) {
        TEST_ASSERT_EQUAL_INT(cases[i].frozen, out.gesture);
        TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, out.tier);
        committed = true;
        continue;
      }
      if (committed) continue;
      TEST_ASSERT_EQUAL_INT_MESSAGE(cases[i].frozen, out.tier, "the ramp walked on without a finger");
      TEST_ASSERT_FALSE_MESSAGE(out.tier_changed, "a threshold was announced after the finger left");
    }
    TEST_ASSERT_TRUE(committed || cases[i].frozen == COMFOAIR_GESTURE_NONE);
  }
}

// The release debounce is a LENGTH, not "eventually": release_at() feeds samples for 600 ms and
// waits, so on its own it passes a debounce of half a second just as happily -- half a second in
// which the owner is looking at the phase colour and nothing has been decided. The SAMPLE, not
// the fact.
static void test_the_outcome_lands_on_the_sample_that_completes_the_release_debounce(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + 8000));
  const uint32_t edge = (uint32_t)(ANCHOR + 8000);

  for (uint32_t n = 0; n * POLL_MS < DEBOUNCE_MS; n++) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(
        COMFOAIR_GESTURE_NONE, comfoair_button_step(&s, false, (uint32_t)(edge + n * POLL_MS)).gesture,
        "the outcome landed before the release was debounced");
  }
  TEST_ASSERT_EQUAL_INT_MESSAGE(
      COMFOAIR_GESTURE_SOFT, comfoair_button_step(&s, false, (uint32_t)(edge + DEBOUNCE_MS)).gesture,
      "the outcome missed the very sample that completed the debounce");
}

// Two samples carrying the same millisecond are two samples: time did not advance but the LEVEL
// did, and the level carries the edge -- only a timestamp going BACKWARDS is refused.
// Dropping an equal one loses the first pressed sample of a press that began in the millisecond of
// the reading before it, the anchor lands a poll late, and five seconds measures 4 980.
static void test_a_repeated_timestamp_is_still_a_new_sample(void) {
  comfoair_button_t s;
  comfoair_button_init(&s);
  comfoair_button_step(&s, false, ANCHOR);  // the first look: up
  comfoair_button_step(&s, true, ANCHOR);   // and the finger lands within the same millisecond
  hold_until(&s, (uint32_t)(ANCHOR + POLL_MS), (uint32_t)(ANCHOR + SOFT_MS));
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_SOFT, release_at(&s, (uint32_t)(ANCHOR + SOFT_MS)),
                                "a sample sharing a millisecond with the one before it was dropped");
}

// Coming back down before the release debounce expires returns the machine to DOWN
// with the anchor UNMOVED. Otherwise one eight-second hold with a scratchy contact reads as two
// short ones, and the owner who counted to eight gets nothing at all.
static void test_bounce_on_release_does_not_split_one_press_into_two(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + 5100));
  // Up for two samples -- inside the 60 ms -- then down again.
  comfoair_button_step(&s, false, (uint32_t)(ANCHOR + 5100));
  comfoair_button_step(&s, false, (uint32_t)(ANCHOR + 5120));
  hold_until(&s, (uint32_t)(ANCHOR + 5140), (uint32_t)(ANCHOR + 8000));
  // The anchor did not move: 8 s of hold is 8 s, so this is still SOFT and not a fresh 2.9 s press.
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, release_at(&s, (uint32_t)(ANCHOR + 8000)));
}

// A bag pressing the button against the wall of a cabinet erases nothing, not after a
// minute and not after an hour.
static void test_a_sixty_second_hold_produces_nothing(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + 60000));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, release_at(&s, (uint32_t)(ANCHOR + 60000)));
}

static void test_a_sixty_second_hold_leaves_the_machine_able_to_see_the_next_gesture(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + 60000));
  release_at(&s, (uint32_t)(ANCHOR + 60000));

  const uint32_t again = ANCHOR + 70000;
  hold_until(&s, again, (uint32_t)(again + HARD_MS + 1000));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_HARD, release_at(&s, (uint32_t)(again + HARD_MS + 1000)));
}

// --- the clock ----------------------------------------------------------------------------

// Every comparison is "elapsed since", never "now >= then", the rule
// comfoair_provision.h:215-216 states for its own deadlines. 0xFFFFF000 is 4 096 ms before the
// wrap, so a five-second hold anchored there ends on the far side of it.
static void test_a_press_that_straddles_the_millisecond_wrap_is_measured_correctly(void) {
  const uint32_t    near_wrap = 0xFFFFF000u;
  comfoair_button_t s;
  comfoair_button_init(&s);
  comfoair_button_step(&s, false, (uint32_t)(near_wrap - POLL_MS));

  hold_until(&s, near_wrap, (uint32_t)(near_wrap + 8000));

  // The OVERLAY is measured across the wrap too, and not only the outcome. A tier that fell back
  // to NONE here would take the amber off the LED halfway through the hold and put the phase
  // colour back -- the owner would read that as "let go, nothing is happening" and let go.
  comfoair_button_out_t out = comfoair_button_step(&s, true, (uint32_t)(near_wrap + 8000));
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_SOFT, out.tier, "the wrap dropped the overlay");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE((uint32_t)(near_wrap + HARD_MS), out.tier_since_ms + out.tier_ms,
                                   "the ramp lost its far end at the wrap");

  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_SOFT, release_at(&s, (uint32_t)(near_wrap + 8000)),
                                "the wrap turned eight seconds into nothing or into everything");
}

// A sample whose timestamp is below the previous one counts as no time passed. The
// anchor is not moved and no gesture is produced -- a harness that hands out a clock going
// backwards must not be able to erase a device.
static void test_a_timestamp_that_goes_backwards_produces_no_gesture(void) {
  comfoair_button_t s = idle_machine();
  hold_until(&s, ANCHOR, (uint32_t)(ANCHOR + 6000));
  const comfoair_button_out_t before = comfoair_button_step(&s, true, (uint32_t)(ANCHOR + 6000));
  // The clock falls back inside the SOFT window; the release that follows is still one press.
  for (uint32_t n = 0; n < 10; n++) {
    comfoair_button_out_t out = comfoair_button_step(&s, true, (uint32_t)(ANCHOR + 100));
    TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_NONE, out.gesture);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_GESTURE_SOFT, out.tier,
                                  "a backwards timestamp rewound the tier");
    // Both halves of the ramp come back untouched as well. They go straight into
    // comfoair_led_override_t, so zeroes here are a tier of no length: full brightness on the
    // instant a bad reading arrives, which reads as "the boundary is here" (comfoair_led.h).
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.tier_since_ms, out.tier_since_ms, "the ramp lost its start");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(before.tier_ms, out.tier_ms, "the ramp lost its length");
    TEST_ASSERT_FALSE(out.tier_changed);
  }
  TEST_ASSERT_EQUAL_INT(COMFOAIR_GESTURE_SOFT, release_at(&s, (uint32_t)(ANCHOR + 6000)));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_short_press_asks_for_the_setup_window);
  RUN_TEST(test_a_tap_opens_the_setup_window);
  RUN_TEST(test_five_seconds_is_the_soft_wipe);
  RUN_TEST(test_fifteen_seconds_is_the_hard_wipe);
  RUN_TEST(test_the_tier_is_announced_exactly_once_per_threshold);
  RUN_TEST(test_a_tier_that_changes_to_none_is_not_a_flash);
  RUN_TEST(test_the_tier_steps_at_one_five_and_fifteen_seconds);
  RUN_TEST(test_the_tier_length_carries_the_ramp_exactly_to_the_next_threshold);
  RUN_TEST(test_holding_past_hard_stays_hard);
  RUN_TEST(test_the_tier_returns_to_none_after_twenty_five_seconds);
  RUN_TEST(test_the_gesture_completes_on_release_and_never_on_the_threshold);
  RUN_TEST(test_releasing_before_a_threshold_yields_the_tier_below);
  RUN_TEST(test_every_release_time_agrees_with_the_table_of_section_seven_two);
  RUN_TEST(test_a_button_already_down_at_the_first_sample_can_never_produce_a_gesture);
  RUN_TEST(test_a_button_held_since_boot_works_normally_after_it_is_released);
  RUN_TEST(test_bounce_does_not_start_a_gesture);
  RUN_TEST(test_a_pulse_shorter_than_the_debounce_is_not_a_press);
  RUN_TEST(test_bounce_on_release_does_not_split_one_press_into_two);
  RUN_TEST(test_a_gap_longer_than_the_debounce_ends_the_press);
  RUN_TEST(test_the_ramp_does_not_grow_while_the_release_is_being_debounced);
  RUN_TEST(test_the_outcome_lands_on_the_sample_that_completes_the_release_debounce);
  RUN_TEST(test_a_repeated_timestamp_is_still_a_new_sample);
  RUN_TEST(test_a_sixty_second_hold_produces_nothing);
  RUN_TEST(test_a_sixty_second_hold_leaves_the_machine_able_to_see_the_next_gesture);
  RUN_TEST(test_a_press_that_straddles_the_millisecond_wrap_is_measured_correctly);
  RUN_TEST(test_a_timestamp_that_goes_backwards_produces_no_gesture);
  return UNITY_END();
}
