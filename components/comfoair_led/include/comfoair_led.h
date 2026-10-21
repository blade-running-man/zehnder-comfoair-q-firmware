// What the device looks like, as a decision separate from what the device IS.
//
// device_phase_t answers "what is true"; comfoair_led_look_t answers "what does the owner see",
// and they are DIFFERENT ENUMERATIONS ON PURPOSE. The obvious invariant, "no two phases look
// alike", is arithmetically impossible: seven of the fourteen phases are network phases and
// want the cool colour, while the
// alphabet has four rhythms left after PROGRESS went to OTA, RAMP to the button and SPARK to the
// sign of life. Seven into four is the pigeonhole principle, so a collapse is compulsory -- and the
// only question is whether it is written down. Here it is written down, as a table over looks, and
// the invariant that survives is "no two LOOKS share a colour and a rhythm".
//
// PURE, TOTAL, NO CLOCK AND NO GLOBALS, and no GPIO number ever (AGENTS.md criterion 5). Nothing in this
// component includes ESP-IDF, FreeRTOS or led_strip: time arrives as the now_ms parameter, the
// board arrives as caps, and the owner's settings arrive as policy. That is what lets "OTA at 40 %",
// "the flash at 9 975 ms" and "the ramp on a tier boundary" be host cases in test/test_led_pattern
// instead of observations by eye, the same argument comfoair_phase.h makes for its own purity.
//
// The imperative half -- led_strip, the power gate, the 25 ms render task and the override slots
// that comfoair_led_override_push() fills -- lives in comfoair_led_driver.h beside it, for the
// build reason spelled out in components/comfoair_config/CMakeLists.txt: the host test build
// compiles every .c in this directory, so the device-only half is wrapped in #ifdef ESP_PLATFORM.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_phase.h"

#ifdef __cplusplus
extern "C" {
#endif

// The render task's period, and A CONSTANT OF THE CONTRACT rather than a literal inside the
// driver. Every duration in the alphabet is argued against it -- SPARK is ten ticks, the
// heartbeat's pip is four -- so a driver that ticked at some other
// rate would draw a picture this component's suite has never checked. comfoair_led_driver.c's
// loop period IS this macro, which is what lets test_the_render_tick_divides_the_heartbeat_evenly
// be a host case instead of a stopwatch held to a board.
//
// 25 ms is 40 Hz: BREATHE gets 160 steps across its 4000 ms and shows no staircase, and a tick
// this coarse is one a dropped instance of cannot be seen.
#define COMFOAIR_LED_TICK_MS 25

// The alphabet, and it is closed on purpose: seven rhythms, each with an argued period. Peripheral
// vision barely resolves hue and resolves movement well, so the rhythm -- not the colour -- is
// what an owner walking past at three metres actually reads.
typedef enum {
    COMFOAIR_LED_SOLID,     // nothing is happening and nothing will
    COMFOAIR_LED_BREATHE,   // a process is running, wait -- sine, 4000 ms
    COMFOAIR_LED_BLINK,     // you are needed -- square, 1000 ms
    COMFOAIR_LED_DOUBLE,    // a subsystem fell off; act, but not now -- 120/180/120 in 2000 ms
    COMFOAIR_LED_PROGRESS,  // duty is the percentage -- 1000 ms
    COMFOAIR_LED_RAMP,      // brightness rises from 0 to full across the current button tier
    // Confirmation of life rather than a state: 250 ms every 10 000 ms. A whole number of render
    // ticks (ten, so at least two), so it is representable on the render grid instead of floating
    // between ticks; and five times rarer than the rarest rhythm (DOUBLE's 2000 ms), which is what
    // makes it recognisable at all and lets it stay discreet at full ceiling brightness.
    COMFOAIR_LED_SPARK,
} comfoair_led_rhythm_t;

// Eleven pictures, each admitted only by naming what the owner does differently.
// The alphabet has no free (colour, rhythm) pair left in the cool or the warm group, and that is
// the correct state: a fifteenth phase must either collapse into a look that exists or say what
// makes it different for the owner.
typedef enum {
    COMFOAIR_LOOK_BOOT,             // white SOLID -- and it proves all three channels are alive
    COMFOAIR_LOOK_OTA,              // white PROGRESS
    COMFOAIR_LOOK_SETUP,            // blue BLINK -- the only message allowed to hurry the owner
    COMFOAIR_LOOK_WORKING,          // blue BREATHE
    COMFOAIR_LOOK_NET_BROKEN,       // blue DOUBLE
    COMFOAIR_LOOK_STRANDED,         // blue SOLID -- no network, no access point, no return planned
    COMFOAIR_LOOK_NO_BROKER,        // amber BREATHE -- the firmware retries by itself, forever
    COMFOAIR_LOOK_BROKER_REJECTED,  // amber BLINK -- credentials are a person's job
    COMFOAIR_LOOK_BUS_SILENT,       // amber DOUBLE -- check the loom; the web UI still works
    COMFOAIR_LOOK_FAULT,            // red SOLID -- the only look that claims "I cannot fix this"
    COMFOAIR_LOOK_OK,               // green SPARK -- not darkness, which a dead RGB path shows too
    COMFOAIR_LOOK_COUNT,
} comfoair_led_look_t;

// The tier the BUTTON has ALREADY decided. The thresholds behind these names (comfoair_config.h:
// 196-199) appear nowhere in this component: the gesture owns them, and a second copy here would
// be a second source of truth that drifts.
typedef enum {
    COMFOAIR_GESTURE_NONE,
    COMFOAIR_GESTURE_SETUP,
    COMFOAIR_GESTURE_SOFT,
    COMFOAIR_GESTURE_HARD,
    // Not a tier. It exists so that a fifth one cannot be added in silence: comfoair_button.c
    // static-asserts against this count beside the table of windows that decides the tiers, and
    // -Werror=switch catches the switches. Without it a new tier is a value the gesture never
    // produces and the LED never draws, and nothing in either build says so (added 2026-09-06,
    // Task 3 quality review; the same argument that promoted -Werror=switch, platformio.ini:60-66).
    COMFOAIR_GESTURE_COUNT,
} comfoair_gesture_tier_t;

// DECLARED IN PRIORITY ORDER, LOW TO HIGH: the numeric order IS the rule, BUTTON > OTA > IDENTIFY >
// the phase, and comfoair_led_override_strongest() is nothing but a maximum over it. Fixed and
// total rather than a stack or last-writer-wins, because last-writer-wins makes the picture a
// function of the FreeRTOS scheduler -- unpinnable by any test and reproducible once a month.
// BUTTON is above OTA deliberately: it is the one moment the owner's fingers are on
// the device, and a factory-reset countdown hidden behind a progress bar erases a device by feel.
typedef enum {
    COMFOAIR_LED_OVR_NONE,
    COMFOAIR_LED_OVR_IDENTIFY,  // "which of these boxes is mine" -- white SOLID, self-clearing
    COMFOAIR_LED_OVR_OTA,       // arg is the percentage 0..100
    COMFOAIR_LED_OVR_BUTTON,    // arg is a comfoair_gesture_tier_t
} comfoair_led_override_kind_t;

typedef struct {
    comfoair_led_override_kind_t kind;
    // OTA: percent complete, 0..100.
    //
    // BUTTON: comfoair_led_override_push(kind, tier | (tier_ms << 8)) -- the tier in the low 8
    // bits, and HOW LONG THAT TIER LASTS in the high 24. Both halves are read, because the owner
    // chose what the ramp means: "how much is left until the next tier", so it fills
    // over the tier's own length and reaches the ceiling exactly at the boundary. The length is a
    // number the gesture hands over, never a threshold this component knows -- five and fifteen
    // seconds stay in comfoair_config.h:196-199, which owns them. A tier of zero length is drawn
    // at full brightness; see the branch in comfoair_led_pattern.c for why it is not drawn dark.
    //
    // DO NOT declare a field here that resolve() does not read. The high bits spent one review
    // cycle documented and ignored, and that is exactly the seam along which a driver written from
    // the header alone would have diverged from the resolver without a single test noticing.
    uint32_t arg;
    // BUTTON: when the current TIER began, on the same clock as now_ms. Not when the button went
    // down: every tier restarts the ramp, and together with the length in arg that is the whole of
    // what this component needs to draw "how much is left" without knowing where any boundary is.
    // IGNORED for OTA and IDENTIFY, whose rhythms are periodic in now_ms alone -- a driver has
    // nothing meaningful to put here for them and may leave it zero. IDENTIFY ignores arg too.
    uint32_t since_ms;
} comfoair_led_override_t;

// The BUTTON overlay's arg, packed. THE WIRE FORMAT LIVES HERE AND NOWHERE ELSE: prose in three
// places and code in none is how the driver of Task 5 ends up shifting by four, filling every ramp
// sixteen times too fast, and passing every host case in this suite -- a defect visible only to a
// finger on the button. Both the pusher and the tests call this.
//
// tier_ms saturates at 0xFFFFFF (about 4 h 39 min). Silently overwriting the tier bits is the one
// outcome that must not happen: a clipped ramp is a wrong picture, a clobbered tier is the wrong
// tier, and the tier is what tells the owner which thing they are about to destroy.
static inline uint32_t comfoair_led_button_arg(comfoair_gesture_tier_t tier, uint32_t tier_ms) {
    if (tier_ms > 0xFFFFFFu) tier_ms = 0xFFFFFFu;
    return ((uint32_t)tier & 0xFFu) | (tier_ms << 8);
}

// What the board carries. A parameter and not a compile-time constant so that the degradation of
// a board with no free mono LED (the AtomS3), where the sign of life has to ride on the colour
// channel, is a host case years before such a board is assembled.
typedef struct {
    bool has_mono;  // a plain status LED exists, and it carries the heartbeat and nothing else
    bool has_rgb;
} comfoair_led_caps_t;

// The brightness cap a device starts with, 20 % of 255: one WS2812
// at full white is more than half the board's whole measured budget of 106.2 mA
// (docs/reference/esp32-node-hardware.md §4, "Power and flashing"), and 20 % is a clearly
// visible point in a dark room rather than a floodlight. A MACRO AND NOT A LITERAL IN THE
// DRIVER: the owner's stored value arrives later through comfoair_led_policy_set(), and the
// value it starts from is the same number the settings entity will offer as its default.
#define COMFOAIR_LED_CEILING_DEFAULT 51

// The owner's settings. NOTHING FEEDS THEM TODAY: no entity or setting calls
// comfoair_led_policy_set(), so every device runs at the default ceiling with quiet off.
typedef struct {
    // The brightness cap, 0..255, default COMFOAIR_LED_CEILING_DEFAULT. Zero is a guaranteed dark
    // RGB channel and not "almost nothing" -- and it stays legitimate only because the mono
    // heartbeat keeps running, so "I turned the light off" never becomes "I cannot tell it from
    // a brick".
    uint8_t ceiling;
    // Inside the owner's quiet window. It dims FAULT and BROKER_REJECTED TOO, and that is a
    // decision, not an oversight: nothing this device reports is a safety
    // question, and a LED that wakes the owner over a broker they cannot repair before morning is
    // a defect. DO NOT add an exception here without changing the test that guards it.
    bool quiet;
} comfoair_led_policy_t;

// One row of the look table: what a look IS, before time, ceiling or overlay touch it.
typedef struct {
    uint8_t               r, g, b;
    comfoair_led_rhythm_t rhythm;
} comfoair_led_style_t;

typedef struct {
    uint8_t r, g, b;      // LOGICAL channels; the physical order is the driver's business
    uint8_t brightness;   // after gamma and after the ceiling; the driver only scales and sends
    bool    mono_on;      // the heartbeat level this tick, before any board inversion
} comfoair_led_frame_t;

// The phase-to-look table, total over the fourteen phases and collapsing three pairs, each
// collapse argued by name in comfoair_led_map.c. A number that is NOT a phase answers
// COMFOAIR_LOOK_FAULT: the alternative -- darkness -- is the one answer indistinguishable from a
// healthy device.
//
// TASK CONTEXT: any task, any core; no locks, no state, no clock.
comfoair_led_look_t comfoair_led_look_of(device_phase_t phase);

// The look table itself, exposed because the invariant "no two looks share a colour and a rhythm"
// has to be checkable from outside. A look outside the enumeration answers the FAULT row.
comfoair_led_style_t comfoair_led_style_of(comfoair_led_look_t look);

// The whole picture for one render tick. Pure: the same arguments always give the same frame.
//
// An overlay whose kind is not one of the four is drawn as FAULT, the same answer both tables give
// a number that is not one of their values -- never as a leftover or undecided frame.
//
// ovr may be NULL, and an override of kind NONE -- or a BUTTON override whose tier is still NONE --
// falls through to the phase. A gesture below its first tier deliberately shows nothing: releasing
// there does nothing, and showing something would promise an outcome that will not happen.
//
// now_ms is uptime, esp_timer_get_time()/1000 as everywhere else in this tree (comfoair_net.c:104).
// It carries a second meaning beyond the rhythms: inside the first five minutes the colour of a
// phase INFERRED FROM ABSENCE is softened to the cool one, because "the house is still coming up"
// after a power cut is normal rather than broken. It softens the colour only -- never
// the phase, never the rhythm -- and never a fact the far end actually stated.
comfoair_led_frame_t comfoair_led_resolve(device_phase_t phase, const comfoair_led_override_t *ovr,
                                          comfoair_led_caps_t caps, comfoair_led_policy_t policy,
                                          uint32_t now_ms);

// The winner of the fixed total order above, or NULL when nothing is pushed. Pure, and public for
// exactly one reason: the driver holds the slots, so if the priority lived there it would live
// where no host test can reach it -- and an untested priority is one a scheduler decides.
// The returned pointer aliases slots and is valid as long as slots is.
const comfoair_led_override_t *comfoair_led_override_strongest(const comfoair_led_override_t *slots,
                                                               uint8_t count);

// The byte that actually reaches the wire for ONE channel: the look's own hue, scaled by the
// envelope resolve() has already put through the gamma table and the ceiling. The multiplication
// is SECOND, and that ordering is the whole reason this is public rather than four characters
// inside the driver.
//
// THE FLOOR OF ONE IN resolve() DOES NOT REACH THE WIRE BY ITSELF. `brightness` of 1 becomes
// `channel / 255`, which is zero for every channel below 255 -- so the OK look's confirming flash
// survives today only because GREEN is (0, 255, 0). Dim the palette's green to (0, 200, 0), an
// entirely plausible change, and inside the quiet hour the flash scales to (0, 0, 0): byte-identical
// to the darkness around it, therefore skipped by the driver's "do not resend an identical frame",
// therefore gone -- with frame.brightness still reading 1 and every case that checks brightness
// still green. test_a_lit_frame_never_reaches_the_wire_as_darkness is what stands in that gap.
uint8_t comfoair_led_scale(uint8_t channel, uint8_t brightness);

// WS2812 is linear in PWM, the eye is not; without this BREATHE reads as "snapped on, faded off"
// gamma(0) == 0, gamma(255) == 255, non-decreasing, and never above its
// argument -- which is also why the ceiling is applied after it and still bounds the result.
uint8_t comfoair_led_gamma(uint8_t linear);

#ifdef __cplusplus
}
#endif
