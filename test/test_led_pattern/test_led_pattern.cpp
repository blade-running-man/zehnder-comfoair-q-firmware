// The palette as an enumeration of its own, and "all is well" that does not look like a dead
// channel (docs/device/indication.md, LED looks).
//
// The suite is written against that page's two tables -- the eleven looks and the mapping from
// the fourteen phases onto them -- and NOT against the switch that implements them. That is why
// the colours below are spelled out as literals transcribed from the page rather than read back
// from comfoair_led_style_of(): a table that answers a test with its own contents proves nothing.
//
// Time is a parameter of comfoair_led_resolve(), the same shape as comfoair_prov_* ("Time is a
// parameter here, and that is the only reason these cases can be written at all",
// test/test_provision/test_provision.cpp:8-9). "OTA at 40 %", "the flash at 9 975 ms" and "the
// ramp on a tier boundary" are therefore host cases and not observations by eye.
#include <unity.h>

#include "comfoair_led.h"

// The colour axis of the look table. Transcribed, not imported: see the file header.
static const uint8_t WHITE[3] = {255, 255, 255};
static const uint8_t BLUE[3]  = {0, 80, 255};
static const uint8_t AMBER[3] = {255, 120, 0};
static const uint8_t RED[3]   = {255, 0, 0};
static const uint8_t GREEN[3] = {0, 255, 0};

// NanoC6 carries both LEDs, and the default ceiling is 20 % of 255 (docs/device/indication.md,
// Brightness).
static const comfoair_led_caps_t   NANOC6 = {true, true};
static const comfoair_led_policy_t NORMAL = {51, false};

// The start-up grace window is the first five minutes of uptime. 100 000 and 400 000 are
// congruent modulo every period in the alphabet, so a case that compares one to the other is
// comparing colours and not phases of a rhythm.
static const uint32_t IN_GRACE    = 100000;
static const uint32_t AFTER_GRACE = 400000;

void setUp(void) {}
void tearDown(void) {}

static bool colour_is(comfoair_led_frame_t f, const uint8_t rgb[3]) {
  return f.r == rgb[0] && f.g == rgb[1] && f.b == rgb[2];
}

static bool same_light(comfoair_led_frame_t a, comfoair_led_frame_t b) {
  return a.r == b.r && a.g == b.g && a.b == b.b && a.brightness == b.brightness;
}

static comfoair_led_frame_t phase_frame(device_phase_t phase, uint32_t now_ms) {
  return comfoair_led_resolve(phase, NULL, NANOC6, NORMAL, now_ms);
}

// tier_ms is how long THIS tier lasts, which the button decides and the LED only draws. The
// thresholds behind it (comfoair_config.h:196-199) appear nowhere in this file: every case below
// picks a length because it needs two different ones, not because five or fifteen seconds mean
// anything here.
static comfoair_led_override_t button(comfoair_gesture_tier_t tier, uint32_t since_ms,
                                      uint32_t tier_ms) {
  comfoair_led_override_t o{};
  o.kind     = COMFOAIR_LED_OVR_BUTTON;
  o.arg      = comfoair_led_button_arg(tier, tier_ms);  // never packed by hand: see the case below
  o.since_ms = since_ms;
  return o;
}

// Deuteranopia collapses amber, red and green into one colour: the group, not the hue, is what a
// colour-blind owner actually discriminates by.
enum colour_group { GROUP_COOL, GROUP_WARM, GROUP_WHITE };
static colour_group group_of(comfoair_led_style_t s) {
  if (s.r == 255 && s.g == 255 && s.b == 255) return GROUP_WHITE;
  if (s.b > s.r) return GROUP_COOL;
  return GROUP_WARM;
}

// --- the look table --------------------------------------------------------------------------

// The invariant cannot be kept on phases: seven cool phases against four cool rhythms is a
// pigeonhole. On looks it is arithmetic that closes.
static void test_no_two_looks_share_a_colour_and_a_rhythm(void) {
  for (int i = 0; i < COMFOAIR_LOOK_COUNT; i++) {
    for (int j = 0; j < i; j++) {
      comfoair_led_style_t a = comfoair_led_style_of((comfoair_led_look_t)i);
      comfoair_led_style_t b = comfoair_led_style_of((comfoair_led_look_t)j);
      bool same = a.r == b.r && a.g == b.g && a.b == b.b && a.rhythm == b.rhythm;
      TEST_ASSERT_FALSE_MESSAGE(same, "two looks are one picture");
    }
  }
}

// Colour blindness as an assertion rather than a comment: cool 4/4, warm 5/5, white 2/2.
static void test_within_a_colour_group_every_rhythm_is_distinct(void) {
  for (int i = 0; i < COMFOAIR_LOOK_COUNT; i++) {
    for (int j = 0; j < i; j++) {
      comfoair_led_style_t a = comfoair_led_style_of((comfoair_led_look_t)i);
      comfoair_led_style_t b = comfoair_led_style_of((comfoair_led_look_t)j);
      if (group_of(a) != group_of(b)) continue;
      TEST_ASSERT_NOT_EQUAL_MESSAGE(a.rhythm, b.rhythm, "one group, one rhythm twice");
    }
  }
}

// Totality of the mapping table, swept rather than read. A fifteenth phase without a row of its
// own does not reach this assertion at all -- it fails to compile under -Werror=switch
// (platformio.ini) -- and what remains for the sweep is the other hole: a phase mapped to the one
// look that means "the firmware cannot fix this itself" when it is not COMFOAIR_PHASE_FAULT.
static void test_every_phase_maps_to_a_look(void) {
  bool reached[COMFOAIR_LOOK_COUNT] = {};
  for (int p = 0; p < COMFOAIR_PHASE_COUNT; p++) {
    comfoair_led_look_t look = comfoair_led_look_of((device_phase_t)p);
    TEST_ASSERT_TRUE(look >= 0 && look < COMFOAIR_LOOK_COUNT);
    if (p != COMFOAIR_PHASE_FAULT) {
      TEST_ASSERT_NOT_EQUAL_MESSAGE(COMFOAIR_LOOK_FAULT, look, "phase fell through the table");
    }
    reached[look] = true;
  }
  // Fourteen phases onto eleven looks is exactly three merges, each named in
  // docs/device/indication.md and each because the owner's action is the same. Counting them here
  // is what keeps this case from passing against a table that answers every phase with the same
  // picture.
  int distinct = 0;
  for (bool r : reached) distinct += r ? 1 : 0;
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_LOOK_COUNT, distinct, "the table lost or gained a merge");
}

// A number that is not a phase is a defect in the caller, and the owner is told so in red. The
// alternative -- darkness -- is the one answer that is indistinguishable from a healthy device.
static void test_an_unknown_phase_is_a_fault_not_darkness(void) {
  TEST_ASSERT_EQUAL_INT(COMFOAIR_LOOK_FAULT, comfoair_led_look_of((device_phase_t)99));
  TEST_ASSERT_EQUAL_INT(COMFOAIR_LOOK_FAULT, comfoair_led_look_of(COMFOAIR_PHASE_COUNT));
  comfoair_led_frame_t f = phase_frame((device_phase_t)99, 1234);
  TEST_ASSERT_TRUE(colour_is(f, RED));
  TEST_ASSERT_EQUAL_UINT8(NORMAL.ceiling, f.brightness);
}

// --- "all is well" is a visible spark, not a dead channel -------------------------------------

// Darkness alone collapses ONLINE onto three real failures of the RGB path, none of which a host
// test can see: a led_strip/RMT error at run time, a wrong power-gate settling time, and a
// brightness ceiling of 0. The flash is what separates them, and it is separated from the
// alphabet by being RARER than anything in it -- once in ten seconds -- and by nothing else.
//
// THIS CASE WAS REVERSED ON 2026-09-07, and the reversal is the point of it. It used to assert
// the flash was DIMMER than a steady look. That assertion passed for weeks and was wrong on the
// board: at level 64 and ceiling 51 the flash computed to brightness 2 of 255 for 50 ms, and the
// owner, watching the assembled device, reported seeing nothing at all. A confirmation nobody can
// perceive does not separate "all is well" from "the RGB path never came up" -- the one job
// it exists for. The flash now lands at the ceiling exactly.
static void test_online_is_dark_with_a_confirming_flash_every_ten_seconds(void) {
  const uint32_t inside[]  = {0, 25, 225, 10000, 20225, 59975 + 25};
  const uint32_t outside[] = {250, 275, 5000, 9975, 10250};
  for (uint32_t t : inside) {
    comfoair_led_frame_t f = phase_frame(COMFOAIR_PHASE_ONLINE, t);
    TEST_ASSERT_TRUE(colour_is(f, GREEN));
    // The literal, so that a change to SPARK_LEVEL or to the gamma table has to come here and be
    // argued. 51 is COMFOAIR_LED_CEILING_DEFAULT, and the flash reaching it exactly is what
    // "visible" was reduced to after the measurement.
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(51, f.brightness, "the flash no longer reaches the ceiling");
    // And the same brightness a steady look draws -- two different envelope paths, one result.
    TEST_ASSERT_EQUAL_UINT8(phase_frame(COMFOAIR_PHASE_FAULT, t).brightness, f.brightness);
  }
  for (uint32_t t : outside) {
    TEST_ASSERT_EQUAL_UINT8(0, phase_frame(COMFOAIR_PHASE_ONLINE, t).brightness);
  }
  // The edge, by literals, because the window length is the half of this decision that arithmetic
  // cannot recover: 225 ms is the last lit tick and 250 ms is the first dark one. Shortening the
  // flash back to two ticks fails here and not in the loops above.
  TEST_ASSERT_GREATER_THAN_UINT8(0, phase_frame(COMFOAIR_PHASE_ONLINE, 225).brightness);
  TEST_ASSERT_EQUAL_UINT8(0, phase_frame(COMFOAIR_PHASE_ONLINE, 250).brightness);
}

// Six flashes in a minute, and between them a frame that never changes. The second half is what
// makes the case bite: a "flash" implemented as a rhythm would move between the flashes too.
static void test_the_confirming_flash_is_the_only_thing_that_moves_in_online(void) {
  int flashes = 0;
  bool was_lit = false;
  comfoair_led_frame_t dark = phase_frame(COMFOAIR_PHASE_ONLINE, 5000);
  for (uint32_t t = 0; t < 60000; t += 25) {  // the 25 ms render tick
    comfoair_led_frame_t f = phase_frame(COMFOAIR_PHASE_ONLINE, t);
    bool lit = f.brightness > 0;
    if (lit && !was_lit) flashes++;
    if (!lit) TEST_ASSERT_TRUE_MESSAGE(same_light(f, dark), "online moves between flashes");
    was_lit = lit;
  }
  TEST_ASSERT_EQUAL_INT(6, flashes);
}

// --- brightness, ceiling and the quiet hour ---------------------------------------------------

static void test_brightness_never_exceeds_the_ceiling(void) {
  for (int p = 0; p < COMFOAIR_PHASE_COUNT; p++) {
    for (int ceiling = 0; ceiling <= 255; ceiling++) {
      comfoair_led_policy_t policy = {(uint8_t)ceiling, false};
      for (uint32_t t = 0; t < 10000; t += 125) {
        comfoair_led_frame_t f =
            comfoair_led_resolve((device_phase_t)p, NULL, NANOC6, policy, t);
        TEST_ASSERT_LESS_OR_EQUAL_UINT8(ceiling, f.brightness);
      }
    }
  }
}

// The owner who switched the RGB off gets darkness, not "nearly nothing",
// and the heartbeat on the mono LED is what keeps that from meaning "brick".
static void test_a_ceiling_of_zero_darkens_even_fault_and_ota(void) {
  comfoair_led_policy_t off = {0, false};
  comfoair_led_override_t ota{};
  ota.kind = COMFOAIR_LED_OVR_OTA;
  ota.arg  = 40;
  for (uint32_t t = 0; t < 4000; t += 25) {
    TEST_ASSERT_EQUAL_UINT8(
        0, comfoair_led_resolve(COMFOAIR_PHASE_FAULT, NULL, NANOC6, off, t).brightness);
    TEST_ASSERT_EQUAL_UINT8(
        0, comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &ota, NANOC6, off, t).brightness);
    TEST_ASSERT_TRUE(comfoair_led_resolve(COMFOAIR_PHASE_FAULT, NULL, NANOC6, off, t).mono_on ||
                     (t % 1000) >= 100);
  }
}

// The quiet hour dims FAULT too, and this case is here because that is what a reviewer will want to
// "fix": nothing this device reports is a safety question, and a LED that wakes the owner at three
// in the morning over a broker they cannot repair until daylight is a defect.
static void test_the_quiet_hour_dims_even_fault(void) {
  comfoair_led_policy_t quiet = {51, true};
  comfoair_led_frame_t loud   = phase_frame(COMFOAIR_PHASE_FAULT, 0);
  comfoair_led_frame_t hushed = comfoair_led_resolve(COMFOAIR_PHASE_FAULT, NULL, NANOC6, quiet, 0);
  TEST_ASSERT_LESS_THAN_UINT8(loud.brightness, hushed.brightness);
  TEST_ASSERT_GREATER_THAN_UINT8(0, hushed.brightness);
  TEST_ASSERT_TRUE(colour_is(hushed, RED));
  // Three per cent of 255, and asserted as the number because it is a decision -- the quiet hour's
  // ceiling -- rather than a consequence: a steady look at any ceiling above it lands exactly
  // there.
  comfoair_led_policy_t wide_open = {255, true};
  TEST_ASSERT_EQUAL_UINT8(
      8, comfoair_led_resolve(COMFOAIR_PHASE_FAULT, NULL, NANOC6, wide_open, 0).brightness);
}

// --- overlays (docs/device/indication.md, Overlays and priority) ------------------------------

// The one moment the owner's fingers are on the device, and the feedback has to be immediate and
// true even when the phase underneath is the quietest one there is.
static void test_a_button_overlay_outranks_the_phase(void) {
  comfoair_led_override_t held = button(COMFOAIR_GESTURE_HARD, 0, 10000);
  comfoair_led_frame_t bare    = phase_frame(COMFOAIR_PHASE_ONLINE, 5000);
  comfoair_led_frame_t over =
      comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL, 5000);
  TEST_ASSERT_FALSE(same_light(bare, over));
  TEST_ASSERT_TRUE(colour_is(over, RED));
  TEST_ASSERT_GREATER_THAN_UINT8(0, over.brightness);
}

// A gesture that has not reached a tier yet is not an overlay: releasing there does nothing, so
// showing something would promise an outcome that will not happen.
static void test_a_gesture_below_the_first_tier_shows_the_phase(void) {
  comfoair_led_override_t idle = button(COMFOAIR_GESTURE_NONE, 0, 10000);
  TEST_ASSERT_TRUE(same_light(phase_frame(COMFOAIR_PHASE_ONLINE, 3000),
                              comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &idle, NANOC6, NORMAL,
                                                   3000)));
}

static void test_ota_outranks_every_phase(void) {
  comfoair_led_override_t ota{};
  ota.kind = COMFOAIR_LED_OVR_OTA;
  ota.arg  = 40;
  for (int p = 0; p < COMFOAIR_PHASE_COUNT; p++) {
    comfoair_led_frame_t f = comfoair_led_resolve((device_phase_t)p, &ota, NANOC6, NORMAL, 100);
    TEST_ASSERT_TRUE(colour_is(f, WHITE));
    TEST_ASSERT_GREATER_THAN_UINT8(0, f.brightness);
    // Duty is the percentage: 40 % of the 1 000 ms period lit, the rest dark.
    TEST_ASSERT_EQUAL_UINT8(
        0, comfoair_led_resolve((device_phase_t)p, &ota, NANOC6, NORMAL, 500).brightness);
  }
}

// An update that looks stuck is exactly when a finger arrives, and a countdown to a
// factory reset hidden behind a progress bar erases the device by feel.
static void test_a_button_overlay_outranks_ota(void) {
  comfoair_led_override_t slots[2];
  slots[0].kind = COMFOAIR_LED_OVR_OTA;
  slots[0].arg  = 40;
  slots[0].since_ms = 0;
  slots[1] = button(COMFOAIR_GESTURE_SOFT, 0, 10000);
  const comfoair_led_override_t *won = comfoair_led_override_strongest(slots, 2);
  TEST_ASSERT_EQUAL_INT(COMFOAIR_LED_OVR_BUTTON, won->kind);
  TEST_ASSERT_TRUE(colour_is(comfoair_led_resolve(COMFOAIR_PHASE_OTA, won, NANOC6, NORMAL, 3000),
                             AMBER));
}

// Last-writer-wins would make the picture a function of the FreeRTOS scheduler, reproducible once
// a month and pinnable by nothing. The fixed total order is pinned here instead.
static void test_priority_does_not_depend_on_the_order_of_pushes(void) {
  comfoair_led_override_t a[3], b[3];
  a[0].kind = COMFOAIR_LED_OVR_IDENTIFY; a[0].arg = 0; a[0].since_ms = 0;
  a[1].kind = COMFOAIR_LED_OVR_OTA;      a[1].arg = 40; a[1].since_ms = 0;
  a[2] = button(COMFOAIR_GESTURE_HARD, 0, 10000);
  b[0] = a[2]; b[1] = a[0]; b[2] = a[1];
  for (uint32_t t = 0; t < 4000; t += 125) {
    comfoair_led_frame_t fa =
        comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, comfoair_led_override_strongest(a, 3), NANOC6,
                             NORMAL, t);
    comfoair_led_frame_t fb =
        comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, comfoair_led_override_strongest(b, 3), NANOC6,
                             NORMAL, t);
    TEST_ASSERT_TRUE(same_light(fa, fb));
  }
}

// The ramp answers one question -- "how much longer do I have to hold this" -- so it fills over the
// TIER'S OWN length and reaches the ceiling exactly at the boundary (since 2026-09-06).
// Five and fifteen seconds appear neither here nor in the component: the length arrives from the
// button as a number, in the high 24 bits of arg.
//
// TWO LENGTHS, and that is what makes the case bite. A ramp with a fixed fill time passes
// "monotonic, and full at the boundary" trivially -- it is full long before the boundary, and it
// gives an owner on a stepladder feedback for the first fifth of a tier and a flat colour for the
// rest. It cannot pass the two assertions at the end, which read the same instant inside two
// tiers of different lengths and demand two different pictures.
static void test_the_ramp_is_monotonic_and_reaches_the_ceiling_at_the_tier_boundary(void) {
  const uint32_t began = 1000;
  const uint32_t lengths[] = {4000, 10000};
  uint8_t halfway = 0;
  for (uint32_t tier_ms : lengths) {
    comfoair_led_override_t held = button(COMFOAIR_GESTURE_SOFT, began, tier_ms);
    uint8_t previous = 0;
    for (uint32_t t = began; t <= began + tier_ms; t += 25) {
      comfoair_led_frame_t f = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL, t);
      TEST_ASSERT_GREATER_OR_EQUAL_UINT8_MESSAGE(previous, f.brightness, "the ramp went backwards");
      TEST_ASSERT_TRUE(colour_is(f, AMBER));
      previous = f.brightness;
    }
    comfoair_led_frame_t start = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL,
                                                      began);
    comfoair_led_frame_t just_before = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6,
                                                            NORMAL, began + tier_ms - 25);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, start.brightness, "a new tier starts lit");
    TEST_ASSERT_LESS_THAN_UINT8_MESSAGE(NORMAL.ceiling, just_before.brightness,
                                        "full before the boundary is full too early");
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(NORMAL.ceiling, previous, "not full at the boundary");
    // Half of a tier looks like half of a tier whatever the tier is worth: the ramp is a fraction
    // of the way through, not a count of milliseconds.
    uint8_t half = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL,
                                        began + tier_ms / 2).brightness;
    TEST_ASSERT_GREATER_THAN_UINT8(0, half);
    TEST_ASSERT_LESS_THAN_UINT8(NORMAL.ceiling, half);
    if (halfway == 0) halfway = half;
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(halfway, half, "half a tier is not half a tier");
  }
  comfoair_led_override_t brief = button(COMFOAIR_GESTURE_SOFT, began, lengths[0]);
  comfoair_led_override_t lasting = button(COMFOAIR_GESTURE_SOFT, began, lengths[1]);
  uint8_t in_brief = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &brief, NANOC6, NORMAL, 3000)
                         .brightness;
  uint8_t in_lasting = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &lasting, NANOC6, NORMAL, 3000)
                           .brightness;
  TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(in_lasting, in_brief,
                                         "two seconds into two different tiers is one picture");
  // A tier pushed without a length is drawn FULL, and the number is asserted rather than merely
  // being "different": darkness satisfies "different" just as well, and darkness is precisely what
  // the resolver's DO NOT forbids -- it cannot be told apart from a firmware that never noticed
  // the finger, which is the one thing this feedback exists to rule out.
  comfoair_led_override_t lengthless = button(COMFOAIR_GESTURE_SOFT, began, 0);
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(
      NORMAL.ceiling,
      comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &lengthless, NANOC6, NORMAL, 3000).brightness,
      "a tier with no length went dark");
}

// Three tiers and no fourth: an owner who held the button too long must not be left guessing what
// the new picture meant, because there is no new picture.
static void test_holding_past_hard_does_not_produce_a_third_look(void) {
  comfoair_led_override_t held = button(COMFOAIR_GESTURE_HARD, 0, 10000);
  comfoair_led_frame_t at15 = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL,
                                                   15000);
  comfoair_led_frame_t at60 = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL,
                                                   60000);
  TEST_ASSERT_TRUE(same_light(at15, at60));
  TEST_ASSERT_EQUAL(at15.mono_on, at60.mono_on);
  for (uint32_t t = 15000; t <= 60000; t += 25) {
    TEST_ASSERT_TRUE(colour_is(comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &held, NANOC6, NORMAL,
                                                    t), RED));
  }
}

// --- rhythms, gamma, uptime ---------------------------------------------------------------------

// A discontinuity at the period boundary reads to the eye as a jerk, and 4 000 ms is a multiple of
// the heartbeat period, so the whole frame -- not only its colour -- has to repeat.
static void test_breathing_is_continuous_across_the_period_boundary(void) {
  uint8_t peak = 0;
  for (uint32_t t = 0; t < 4000; t += 25) {
    comfoair_led_frame_t a = phase_frame(COMFOAIR_PHASE_CONNECTING, t);
    comfoair_led_frame_t b = phase_frame(COMFOAIR_PHASE_CONNECTING, t + 4000);
    TEST_ASSERT_TRUE(same_light(a, b));
    TEST_ASSERT_EQUAL(a.mono_on, b.mono_on);
    if (a.brightness > peak) peak = a.brightness;
  }
  TEST_ASSERT_EQUAL_UINT8(peak, phase_frame(COMFOAIR_PHASE_CONNECTING, 2000).brightness);
}

// WS2812 is linear in PWM and the eye is not; without this table BREATHE looks like "snapped on,
// faded off".
static void test_gamma_is_monotonic_and_keeps_its_ends(void) {
  TEST_ASSERT_EQUAL_UINT8(0, comfoair_led_gamma(0));
  TEST_ASSERT_EQUAL_UINT8(255, comfoair_led_gamma(255));
  for (int i = 1; i <= 255; i++) {
    TEST_ASSERT_GREATER_OR_EQUAL_UINT8(comfoair_led_gamma((uint8_t)(i - 1)),
                                       comfoair_led_gamma((uint8_t)i));
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(i, comfoair_led_gamma((uint8_t)i));
  }
}

// "The house is still coming up": for the first five minutes of uptime, an absent network or an
// absent broker is normal rather than broken. It softens the COLOUR only -- the
// rhythm, and the phase the rest of the firmware reports, are untouched.
static void test_the_first_five_minutes_after_boot_are_never_alarming(void) {
  const device_phase_t inferred[] = {COMFOAIR_PHASE_NO_BROKER, COMFOAIR_PHASE_BUS_SILENT};
  for (device_phase_t p : inferred) {
    comfoair_led_look_t look = comfoair_led_look_of(p);
    comfoair_led_frame_t young = phase_frame(p, IN_GRACE);
    comfoair_led_frame_t grown = phase_frame(p, AFTER_GRACE);
    TEST_ASSERT_TRUE_MESSAGE(colour_is(young, BLUE), "still alarming inside the grace window");
    TEST_ASSERT_TRUE(colour_is(grown, AMBER));
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(grown.brightness, young.brightness, "grace changed a rhythm");
    TEST_ASSERT_EQUAL_INT_MESSAGE(look, comfoair_led_look_of(p), "grace changed the look");
  }
}

// The other side of the same window, and the reason it is explicit: a refusal the broker actually
// sent is a fact, and a grace period that hides a fact is a lie with a timer on it.
//
// NAMED "not softened" and not "red": an earlier list of cases called it "a connack refusal is red
// at any uptime", while the look table paints BROKER_REJECTED AMBER and keeps red for the one look
// that means "I cannot fix this myself". The invariant both readings agree on is the one asserted
// here -- the grace window does not touch a stated fact -- and red is asserted where the table
// actually puts it, on FAULT.
static void test_a_connack_refusal_is_not_softened_at_any_uptime(void) {
  comfoair_led_frame_t young = phase_frame(COMFOAIR_PHASE_BROKER_REJECTED, IN_GRACE);
  comfoair_led_frame_t grown = phase_frame(COMFOAIR_PHASE_BROKER_REJECTED, AFTER_GRACE);
  TEST_ASSERT_TRUE_MESSAGE(same_light(young, grown), "the grace window softened a stated fact");
  TEST_ASSERT_TRUE(colour_is(young, AMBER));
  // FAULT is the other stated fact: a start branch that returned an error said so, and said so at
  // a moment which is always inside the first five minutes.
  TEST_ASSERT_TRUE(colour_is(phase_frame(COMFOAIR_PHASE_FAULT, IN_GRACE), RED));
}

// --- carriers: the heartbeat pip and the boards without one -----------------------------------

// The pip encodes nothing, always: 100 ms in every second, through faults, through OTA and through
// the whole fifteen seconds of a gesture. "Alive unless a gesture is running" is not "alive".
static void test_the_heartbeat_never_encodes_anything(void) {
  comfoair_led_override_t held = button(COMFOAIR_GESTURE_HARD, 0, 10000);
  const device_phase_t phases[] = {COMFOAIR_PHASE_ONLINE, COMFOAIR_PHASE_FAULT,
                                   COMFOAIR_PHASE_OTA};
  for (device_phase_t p : phases) {
    for (uint32_t t = 0; t < 3000; t += 25) {
      TEST_ASSERT_EQUAL(t % 1000 < 100, phase_frame(p, t).mono_on);
      TEST_ASSERT_EQUAL(t % 1000 < 100,
                        comfoair_led_resolve(p, &held, NANOC6, NORMAL, t).mono_on);
    }
  }
}

// AtomS3 has no free mono LED, so the one carrier has to say "alive" as well as
// "what". The degradation is checked here, years before anyone assembles that board.
static void test_without_a_mono_led_the_sign_of_life_mixes_into_every_look(void) {
  comfoair_led_caps_t rgb_only = {false, true};
  for (int p = 0; p < COMFOAIR_PHASE_COUNT; p++) {
    comfoair_led_frame_t f = comfoair_led_resolve((device_phase_t)p, NULL, rgb_only, NORMAL, 0);
    TEST_ASSERT_FALSE(f.mono_on);
    TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, f.brightness, "no carrier says 'alive' at all");
  }
  // And it stays a sign of life, not a second rhythm: outside the flash the look is untouched.
  for (uint32_t t = 100; t < 10000; t += 25) {
    TEST_ASSERT_TRUE(same_light(comfoair_led_resolve(COMFOAIR_PHASE_SETUP_AP, NULL, rgb_only,
                                                     NORMAL, t),
                                phase_frame(COMFOAIR_PHASE_SETUP_AP, t)));
  }
}

// The symmetric hole to test_an_unknown_phase_is_a_fault_not_darkness, and it was open: an overlay
// kind from outside the enumeration falls through a switch that is exhaustive BY TYPE, where no
// compiler warning can reach it. Both tables in this component answer a non-value with the FAULT
// row, and so does the resolver.
static void test_an_unknown_override_kind_is_a_fault_not_a_leftover_frame(void) {
  comfoair_led_override_t nonsense{};
  nonsense.kind = (comfoair_led_override_kind_t)99;
  comfoair_led_frame_t f =
      comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, &nonsense, NANOC6, NORMAL, 5000);
  TEST_ASSERT_TRUE_MESSAGE(colour_is(f, RED), "an unknown overlay drew something undecided");
  TEST_ASSERT_EQUAL_UINT8(NORMAL.ceiling, f.brightness);
}

// The wire format between the button and this resolver, and the ONE place a literal is the right
// oracle: it is the layout itself, not a table that would then be checked against itself. A driver
// that packs the length four bits low fills every ramp sixteen times too fast and passes every
// other case in this file, because every other case gets its arg from the same helper.
static void test_the_button_argument_packs_the_tier_low_and_its_length_above(void) {
  TEST_ASSERT_EQUAL_HEX32(0x00271002u, comfoair_led_button_arg(COMFOAIR_GESTURE_SOFT, 10000));
  TEST_ASSERT_EQUAL_HEX32(0x00000003u, comfoair_led_button_arg(COMFOAIR_GESTURE_HARD, 0));
  // A length too large to fit saturates instead of overwriting the tier: a clipped ramp is a wrong
  // picture, a clobbered tier is the wrong thing about to be destroyed.
  TEST_ASSERT_EQUAL_HEX32(0xFFFFFF01u, comfoair_led_button_arg(COMFOAIR_GESTURE_SETUP, 0x1000000u));
}

// A board with no RGB shows "alive" and nothing else (since 2026-09-06). The
// meaning is NOT moved onto the mono LED -- eleven looks do not separate into seven rhythms on one
// channel -- so the colour frame is empty and the heartbeat carries on being a heartbeat.
static void test_a_board_without_rgb_gets_an_empty_colour_frame(void) {
  comfoair_led_caps_t mono_only = {true, false};
  comfoair_led_caps_t neither   = {false, false};
  for (int p = 0; p < COMFOAIR_PHASE_COUNT; p++) {
    for (uint32_t t = 0; t < 2000; t += 25) {
      comfoair_led_frame_t f =
          comfoair_led_resolve((device_phase_t)p, NULL, mono_only, NORMAL, t);
      TEST_ASSERT_EQUAL_UINT8(0, f.r);
      TEST_ASSERT_EQUAL_UINT8(0, f.g);
      TEST_ASSERT_EQUAL_UINT8(0, f.b);
      TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, f.brightness, "a board with no RGB lit a colour");
      TEST_ASSERT_EQUAL_MESSAGE(t % 1000 < 100, f.mono_on, "the heartbeat stopped being one");
      TEST_ASSERT_FALSE(comfoair_led_resolve((device_phase_t)p, NULL, neither, NORMAL, t).mono_on);
    }
  }
}

// The visible ONLINE flash has to survive the arithmetic that renders it, not only the table that
// declares it. gamma(64) is 12, so before the floor the flash rounded to nothing at every ceiling
// below 22 of 255 -- and the quiet hour's ceiling is 8, which made ONLINE identically dark inside
// and outside the flash window: the exact picture of an RGB path that never came up.
static void test_the_confirming_flash_survives_the_lowest_ceilings(void) {
  for (uint8_t ceiling = 1; ceiling < 22; ceiling++) {
    comfoair_led_policy_t dim = {ceiling, false};
    comfoair_led_frame_t lit = comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, NULL, NANOC6, dim, 0);
    TEST_ASSERT_GREATER_THAN_UINT8_MESSAGE(0, lit.brightness, "the flash rounded away to nothing");
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(ceiling, lit.brightness);
    TEST_ASSERT_EQUAL_UINT8_MESSAGE(
        0, comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, NULL, NANOC6, dim, 5000).brightness,
        "the floor lit the dark half of ONLINE");
  }
  // Including the quiet hour, which is where an owner is least likely to look twice.
  comfoair_led_policy_t quiet = {51, true};
  TEST_ASSERT_GREATER_THAN_UINT8(
      0, comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, NULL, NANOC6, quiet, 0).brightness);
  // And the off switch stays an off switch: a floor is not an exception to a ceiling of zero.
  comfoair_led_policy_t off = {0, false};
  TEST_ASSERT_EQUAL_UINT8(
      0, comfoair_led_resolve(COMFOAIR_PHASE_ONLINE, NULL, NANOC6, off, 0).brightness);
}

// Since 2026-09-06 the pip is 100 ms on and 900 off, and not the earlier 40/960, because 40 ms is
// 1.6 render ticks and would have floated between 25 and 50 while 100 ms is four ticks exactly. THE
// TICK IS A CONSTANT OF THE CONTRACT (comfoair_led.h) and the driver's loop period is that same
// macro, which is the only reason this is a host case and not a stopwatch held to a board. That a
// pin moved, and the right way up, stays hardware scenario HV-LED-06 (docs/hardware-verification).
static void test_the_render_tick_divides_the_heartbeat_evenly(void) {
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, 1000u % COMFOAIR_LED_TICK_MS,
                                   "a second is not a whole number of render ticks");
  for (uint32_t second = 0; second < 3; second++) {
    unsigned on = 0;
    for (uint32_t t = second * 1000; t < (second + 1) * 1000; t += COMFOAIR_LED_TICK_MS) {
      if (!phase_frame(COMFOAIR_PHASE_ONLINE, t).mono_on) continue;
      // Contiguous and starting at the top of the second: a pip delivered in pieces is a flicker,
      // and a pip that starts mid-second drifts against every other second's.
      TEST_ASSERT_EQUAL_UINT32_MESSAGE(second * 1000 + on * COMFOAIR_LED_TICK_MS, t,
                                       "the pip is not one unbroken run of ticks");
      on++;
    }
    TEST_ASSERT_EQUAL_UINT_MESSAGE(4, on, "the pip is not four whole render ticks long");
  }
}

// The visible ONLINE flash survives resolve(), and resolve() is not where it ends: the driver
// sends channel * brightness / 255 and skips a frame byte-identical to the last, so a lit frame
// that SCALES to (0,0,0) is never transmitted and the pixel stops being rewritten at all -- both
// jobs of the flash dead (a visible "all is well", and a pixel rewritten every ten seconds), with
// brightness still reading 1 and every case above still green. Dimming the
// palette's green to (0,200,0) inside the quiet hour is enough: 200 * 1 / 255 == 0.
static void test_a_lit_frame_never_reaches_the_wire_as_darkness(void) {
  for (int p = 0; p < COMFOAIR_PHASE_COUNT; p++)
    for (unsigned ceiling = 1; ceiling <= 255; ceiling++) {
      const comfoair_led_policy_t cap = {(uint8_t)ceiling, false};
      for (uint32_t t = 0; t < 10000; t += COMFOAIR_LED_TICK_MS) {
        const comfoair_led_frame_t f =
            comfoair_led_resolve((device_phase_t)p, NULL, NANOC6, cap, t);
        if (f.brightness == 0) continue;  // dark on purpose; nothing to carry to the wire
        const unsigned wire = comfoair_led_scale(f.r, f.brightness) +
                              comfoair_led_scale(f.g, f.brightness) +
                              comfoair_led_scale(f.b, f.brightness);
        TEST_ASSERT_GREATER_THAN_UINT_MESSAGE(0, wire, "a lit frame scaled to darkness");
      }
    }
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_no_two_looks_share_a_colour_and_a_rhythm);
  RUN_TEST(test_within_a_colour_group_every_rhythm_is_distinct);
  RUN_TEST(test_every_phase_maps_to_a_look);
  RUN_TEST(test_an_unknown_phase_is_a_fault_not_darkness);
  RUN_TEST(test_an_unknown_override_kind_is_a_fault_not_a_leftover_frame);
  RUN_TEST(test_the_button_argument_packs_the_tier_low_and_its_length_above);
  RUN_TEST(test_online_is_dark_with_a_confirming_flash_every_ten_seconds);
  RUN_TEST(test_the_confirming_flash_is_the_only_thing_that_moves_in_online);
  RUN_TEST(test_brightness_never_exceeds_the_ceiling);
  RUN_TEST(test_a_ceiling_of_zero_darkens_even_fault_and_ota);
  RUN_TEST(test_the_quiet_hour_dims_even_fault);
  RUN_TEST(test_a_button_overlay_outranks_the_phase);
  RUN_TEST(test_a_gesture_below_the_first_tier_shows_the_phase);
  RUN_TEST(test_ota_outranks_every_phase);
  RUN_TEST(test_a_button_overlay_outranks_ota);
  RUN_TEST(test_priority_does_not_depend_on_the_order_of_pushes);
  RUN_TEST(test_the_ramp_is_monotonic_and_reaches_the_ceiling_at_the_tier_boundary);
  RUN_TEST(test_holding_past_hard_does_not_produce_a_third_look);
  RUN_TEST(test_breathing_is_continuous_across_the_period_boundary);
  RUN_TEST(test_gamma_is_monotonic_and_keeps_its_ends);
  RUN_TEST(test_the_first_five_minutes_after_boot_are_never_alarming);
  RUN_TEST(test_a_connack_refusal_is_not_softened_at_any_uptime);
  RUN_TEST(test_the_heartbeat_never_encodes_anything);
  RUN_TEST(test_the_render_tick_divides_the_heartbeat_evenly);
  RUN_TEST(test_without_a_mono_led_the_sign_of_life_mixes_into_every_look);
  RUN_TEST(test_a_board_without_rgb_gets_an_empty_colour_frame);
  RUN_TEST(test_the_confirming_flash_survives_the_lowest_ceilings);
  RUN_TEST(test_a_lit_frame_never_reaches_the_wire_as_darkness);
  return UNITY_END();
}
