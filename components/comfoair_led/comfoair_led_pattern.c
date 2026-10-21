// One render tick, as a pure function of (phase, overlay, board, policy, time).
//
// Every number in this file is a duration in milliseconds and every one of them is argued beside
// its definition -- WITH ONE CLASS OF EXCEPTION that is the point of the component: the gesture's
// thresholds. Five and fifteen seconds do not appear here. They belong to comfoair_config.h,
// comfoair_config_reset_t ("the two durations are the gesture's, not this component's"), the button
// hands over a tier it has already decided, and this file only draws it. A second copy of those two
// numbers here would be a second source of truth that drifts on the first change.
#include "comfoair_led.h"

#define BREATHE_PERIOD_MS  4000  // 0.25 Hz: movement the periphery catches without being alarmed
#define BLINK_PERIOD_MS    1000  // 1 Hz reads as deliberate; below ~300 ms it reads as a flicker
#define DOUBLE_PERIOD_MS   2000  // 120/180/120 in 2000 ms: a texture, recognisable without counting
#define DOUBLE_FIRST_MS     120
#define DOUBLE_SECOND_MS    300
#define DOUBLE_END_MS       420
#define PROGRESS_PERIOD_MS 1000
#define SPARK_PERIOD_MS   10000  // ONLINE's flash: rarer than anything else in the alphabet by 5x
// MEASURED ON THE BOARD, 2026-09-07, AND BOTH NUMBERS ARE A REVERSAL. They were 50 ms at level 64,
// and the arithmetic that follows from that pair is: gamma(64) = 12, ceiling 51, so
// 12 * 51 / 255 = 2. A green flash of brightness TWO out of 255, for 50 ms, once every ten
// seconds. The owner watched the assembled board and reported seeing nothing but the blue mono
// heartbeat -- and he was right: at that arithmetic there is nothing to see. The old comment here
// ("a confirmation, not a night light... about a twentieth of what a steady look draws") described
// the intent faithfully and the intent was the error. A signal below the threshold of noticing
// does not distinguish "all is well" from "the RGB path never came up", which is the ONE job
// the flash has.
//
// DO NOT lower either number back on the grounds that the flash is "meant to be discreet".
// Discretion is bought by RARITY here -- once in ten seconds, five times rarer than anything else
// in the alphabet -- and it has already been bought. Buying it a second time with brightness
// spends the signal itself. The quiet hour still dims this like everything else: policy.quiet
// drops the ceiling to QUIET_CEILING, and 255 * 8 / 255 is 8, so the night is still a night.
#define SPARK_ON_MS         250  // ten render ticks; 50 ms read as an artefact even when lit
// Full envelope, so the flash lands at exactly the ceiling -- the same brightness a steady look
// draws. That equality is the point and it is what the suite pins.
#define SPARK_LEVEL         255
// THERE IS NO RAMP FILL CONSTANT, and its absence is the owner's decision of 2026-09-06.
// The first implementation filled the ramp over a fixed 2 000 ms and then held a flat colour for
// the remaining eight seconds of a tier -- so "how much longer do I have to hold this", the one
// question the ramp exists to answer, was answered for the first fifth of the tier and abandoned
// after it. The tier's own length arrives from the button instead, in the high 24 bits of arg.
#define HEARTBEAT_PERIOD_MS 1000
#define HEARTBEAT_ON_MS      100
// Both of the durations that are argued AS A COUNT OF RENDER TICKS, checked here rather than
// trusted: the pip is four ticks and the flash is ten. 40 ms -- the pip
// this file first carried -- is 1.6 ticks, so it could not be drawn at all and would have floated
// between 25 and 50 ms on a grid nothing in the suite could see.
_Static_assert(HEARTBEAT_ON_MS % COMFOAIR_LED_TICK_MS == 0, "the pip is not a whole tick count");
_Static_assert(SPARK_ON_MS % COMFOAIR_LED_TICK_MS == 0, "the flash is not a whole tick count");
// And at least two of them, which is the claim comfoair_led.h:54-57 makes by name. A single tick
// would be a frame the eye can miss entirely if the render task is late once. NOTHING GUARDS THE
// TASK'S ACTUAL PERIOD on the host; only an analyser on the board sees it (hardware scenario
// HV-LED-06).
_Static_assert(SPARK_ON_MS >= 2 * COMFOAIR_LED_TICK_MS, "the flash is under two render ticks");
#define QUIET_CEILING          8  // 3 % of 255, inside the quiet window
#define GRACE_MS          300000  // the first five minutes of uptime, see below

// WS2812 is linear in PWM and the eye is not. Generated as
// round(255 * (i/255)^2.2); it is non-decreasing, keeps both ends, and never exceeds its argument,
// which is why applying the ceiling afterwards still bounds the result by the ceiling exactly.
static const uint8_t k_gamma[256] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   1,
      1,   1,   1,   1,   1,   1,   1,   1,   1,   2,   2,   2,   2,   2,   2,   2,
      3,   3,   3,   3,   3,   4,   4,   4,   4,   5,   5,   5,   5,   6,   6,   6,
      6,   7,   7,   7,   8,   8,   8,   9,   9,   9,  10,  10,  11,  11,  11,  12,
     12,  13,  13,  13,  14,  14,  15,  15,  16,  16,  17,  17,  18,  18,  19,  19,
     20,  20,  21,  22,  22,  23,  23,  24,  25,  25,  26,  26,  27,  28,  28,  29,
     30,  30,  31,  32,  33,  33,  34,  35,  35,  36,  37,  38,  39,  39,  40,  41,
     42,  43,  43,  44,  45,  46,  47,  48,  49,  49,  50,  51,  52,  53,  54,  55,
     56,  57,  58,  59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,
     73,  74,  75,  76,  77,  78,  79,  81,  82,  83,  84,  85,  87,  88,  89,  90,
     91,  93,  94,  95,  97,  98,  99, 100, 102, 103, 105, 106, 107, 109, 110, 111,
    113, 114, 116, 117, 119, 120, 121, 123, 124, 126, 127, 129, 130, 132, 133, 135,
    137, 138, 140, 141, 143, 145, 146, 148, 149, 151, 153, 154, 156, 158, 159, 161,
    163, 165, 166, 168, 170, 172, 173, 175, 177, 179, 181, 182, 184, 186, 188, 190,
    192, 194, 196, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
    223, 225, 227, 229, 231, 234, 236, 238, 240, 242, 244, 246, 248, 251, 253, 255,
};

uint8_t comfoair_led_gamma(uint8_t linear) { return k_gamma[linear]; }

// Here rather than in the driver, because the driver is where no host case can reach it and this
// is where ONLINE's flash either survives the arithmetic or quietly does not (comfoair_led.h).
uint8_t comfoair_led_scale(uint8_t channel, uint8_t brightness) {
    return (uint8_t)((channel * (uint32_t)brightness) / 255u);
}

// A raised cosine, computed as a smoothstep rather than with cosf(): the two differ by ~2 of 255
// at their worst, they are indistinguishable after the gamma table, and this way the pure half of
// the indication does not drag libm into a host suite or a device image for one curve.
static uint8_t breathe_envelope(uint32_t phase_ms) {
    uint32_t half = BREATHE_PERIOD_MS / 2;
    uint32_t up   = phase_ms < half ? phase_ms : BREATHE_PERIOD_MS - phase_ms;
    uint32_t x    = (up * 1024u) / half;                     // 0..1024
    uint32_t s    = (3u * x * x - (2u * x * x * x) / 1024u); // 1024^2 * (3x^2 - 2x^3)
    return (uint8_t)((s * 255u) / (1024u * 1024u));
}

// The alphabet, and the only place any of it is drawn. arg and since_ms are meaningful for exactly
// two rhythms -- PROGRESS reads a percentage, RAMP reads the moment its tier began -- and the
// others ignore them, which is what keeps the overlay encoding out of every other case.
static uint8_t envelope_of(comfoair_led_rhythm_t rhythm, uint32_t now_ms, uint32_t arg,
                           uint32_t since_ms) {
    switch (rhythm) {
        case COMFOAIR_LED_SOLID:
            return 255;
        case COMFOAIR_LED_BREATHE:
            return breathe_envelope(now_ms % BREATHE_PERIOD_MS);
        case COMFOAIR_LED_BLINK:
            return (now_ms % BLINK_PERIOD_MS) < BLINK_PERIOD_MS / 2 ? 255 : 0;
        case COMFOAIR_LED_DOUBLE: {
            uint32_t p = now_ms % DOUBLE_PERIOD_MS;
            return (p < DOUBLE_FIRST_MS || (p >= DOUBLE_SECOND_MS && p < DOUBLE_END_MS)) ? 255 : 0;
        }
        case COMFOAIR_LED_PROGRESS: {
            uint32_t pct = arg > 100 ? 100 : arg;
            // Multiplied before dividing: pct * PROGRESS_PERIOD_MS is at most 100 000, so it
            // cannot overflow, and the duty stays exact for a period that is not a multiple of 100.
            return (now_ms % PROGRESS_PERIOD_MS) < (pct * PROGRESS_PERIOD_MS) / 100 ? 255 : 0;
        }
        case COMFOAIR_LED_RAMP: {
            // Modular subtraction, and DO NOT "fix" it with a signed comparison: the millisecond
            // counter wraps every 49.7 days, and unsigned arithmetic is what measures a hold that
            // straddles the wrap correctly -- the same reason the button suite anchors a case at
            // 0xFFFFF000.
            uint32_t held = now_ms - since_ms;
            // How long THIS tier lasts, decided by the gesture and sent here as a number. Five and
            // fifteen seconds still do not appear in this component: it is told the length of the
            // stretch it is drawing and never what the stretch means.
            uint32_t tier_ms = arg >> 8;
            // A tier with no length is a caller that pushed a tier and forgot the rest of arg. It
            // is drawn at full brightness rather than dark: the owner then loses "how much longer"
            // but still sees which tier they are in. DO NOT make this branch dark to be safe --
            // dark is indistinguishable from "the firmware never noticed the finger", which is the
            // one thing the gesture's feedback exists to rule out.
            // The first term is REDUNDANT ARITHMETICALLY -- for unsigned values held >= 0 is
            // always true, so a zero length already lands here -- and it is kept as the statement
            // of intent that the second term cannot make: "no length given" is a case with a
            // decided answer, not an accident of the comparison. DO NOT simplify it away; the
            // sentence above it is what would be lost, and the case that pins full brightness at
            // a zero length would then be guarding a branch nobody could find.
            if (tier_ms == 0 || held >= tier_ms) return 255;
            // No overflow: tier_ms is 24 bits and held is below it, so the product stays under 2^32.
            return (uint8_t)((held * 255u) / tier_ms);
        }
        case COMFOAIR_LED_SPARK:
            return (now_ms % SPARK_PERIOD_MS) < SPARK_ON_MS ? SPARK_LEVEL : 0;
    }
    return 0;
}

// Which looks are an INFERENCE FROM ABSENCE rather than a statement by the far end. Only these are
// softened during the grace window: "the house is still coming up" is a reasonable reading of a
// missing broker or a quiet bus in the first minutes after a power cut, and an unreasonable
// reading of a CONNACK the broker actually sent or of a start branch that returned an error.
static bool look_is_inferred_from_absence(comfoair_led_look_t look) {
    switch (look) {
        case COMFOAIR_LOOK_NO_BROKER:
        case COMFOAIR_LOOK_BUS_SILENT:
            return true;
        case COMFOAIR_LOOK_BOOT:
        case COMFOAIR_LOOK_OTA:
        case COMFOAIR_LOOK_SETUP:
        case COMFOAIR_LOOK_WORKING:
        case COMFOAIR_LOOK_NET_BROKEN:
        case COMFOAIR_LOOK_STRANDED:
        case COMFOAIR_LOOK_BROKER_REJECTED:
        case COMFOAIR_LOOK_FAULT:
        case COMFOAIR_LOOK_OK:
        case COMFOAIR_LOOK_COUNT:
            break;
    }
    return false;
}

// White while the firmware merely sees a finger, amber for the tier that forgets the
// network, red for the one that erases everything. The colours are the palette's own, so the
// gesture says "which domain am I about to damage" in the same language as every phase.
//
// NO CONFIRMING FLASH AT A TIER BOUNDARY LIVES HERE, and its absence is deliberate. A spike of
// brightness at the moment of the change is not a monotonic ramp, and both requirements are
// legitimate because they belong to different layers: monotonicity is a property of the picture,
// confirmation is an event. The button therefore pushes the flash as an overlay of its own when the
// tier changes. DO NOT add it to the ramp.
static comfoair_led_style_t button_style(comfoair_gesture_tier_t tier) {
    comfoair_led_style_t s = comfoair_led_style_of(COMFOAIR_LOOK_FAULT);
    switch (tier) {
        case COMFOAIR_GESTURE_SETUP: s = comfoair_led_style_of(COMFOAIR_LOOK_BOOT); break;
        case COMFOAIR_GESTURE_SOFT:  s = comfoair_led_style_of(COMFOAIR_LOOK_NO_BROKER); break;
        case COMFOAIR_GESTURE_HARD:  s = comfoair_led_style_of(COMFOAIR_LOOK_FAULT); break;
        case COMFOAIR_GESTURE_NONE:  break;  // never reached: resolve() drops the overlay first
        case COMFOAIR_GESTURE_COUNT: break;  // not a tier; FAULT stands, the same answer both
                                             // tables give a value that is not one of theirs
    }
    s.rhythm = COMFOAIR_LED_RAMP;
    return s;
}

const comfoair_led_override_t *comfoair_led_override_strongest(const comfoair_led_override_t *slots,
                                                               uint8_t count) {
    const comfoair_led_override_t *best = 0;
    if (!slots) return 0;
    for (uint8_t i = 0; i < count; i++) {
        if (slots[i].kind == COMFOAIR_LED_OVR_NONE) continue;
        // The enumeration's order IS the priority (comfoair_led.h), so the winner is a maximum and
        // the push order cannot enter into it.
        if (!best || slots[i].kind > best->kind) best = &slots[i];
    }
    return best;
}

comfoair_led_frame_t comfoair_led_resolve(device_phase_t phase, const comfoair_led_override_t *ovr,
                                          comfoair_led_caps_t caps, comfoair_led_policy_t policy,
                                          uint32_t now_ms) {
    comfoair_led_frame_t frame = {0, 0, 0, 0, false};
    // Initialised, and initialised to the SAME answer both tables give a number that is not one of
    // their values (comfoair_led_map.c). The switch below is exhaustive over the enumeration, so a
    // kind from outside it falls through every case -- and -Wmaybe-uninitialized cannot see that,
    // because by type the switch is total. DO NOT drop this initialiser on the strength of the
    // switch being exhaustive: the hole is a value outside the type, not a missing case.
    comfoair_led_style_t style = comfoair_led_style_of(COMFOAIR_LOOK_FAULT);
    uint32_t             arg = 0, since_ms = 0;
    // The low byte of arg, and only the BUTTON branch below reads it as a tier: OTA's arg is a
    // percentage, and casting that to comfoair_gesture_tier_t would be a number pretending to be a
    // name in a component that never checks it again.
    uint32_t tier_bits = ovr ? (ovr->arg & 0xFFu) : (uint32_t)COMFOAIR_GESTURE_NONE;

    // A gesture that has not reached a tier is not an overlay at all: releasing there does nothing,
    // and a picture would promise an outcome that will not happen.
    bool overlaid =
        ovr && ovr->kind != COMFOAIR_LED_OVR_NONE &&
        !(ovr->kind == COMFOAIR_LED_OVR_BUTTON && tier_bits == (uint32_t)COMFOAIR_GESTURE_NONE);

    if (overlaid) {
        arg      = ovr->arg;
        since_ms = ovr->since_ms;
        switch (ovr->kind) {
            case COMFOAIR_LED_OVR_BUTTON:
                style = button_style((comfoair_gesture_tier_t)tier_bits);
                break;
            case COMFOAIR_LED_OVR_OTA:      style = comfoair_led_style_of(COMFOAIR_LOOK_OTA); break;
            // "Which of these boxes is mine": the transitional colour, standing still, for the five
            // seconds the caller keeps it pushed.
            case COMFOAIR_LED_OVR_IDENTIFY: style = comfoair_led_style_of(COMFOAIR_LOOK_BOOT); break;
            case COMFOAIR_LED_OVR_NONE:     style = comfoair_led_style_of(COMFOAIR_LOOK_FAULT); break;
        }
    } else {
        comfoair_led_look_t look = comfoair_led_look_of(phase);
        style                    = comfoair_led_style_of(look);
        if (now_ms < GRACE_MS && look_is_inferred_from_absence(look)) {
            // COLOUR ONLY, and taken from the look table rather than written again here: the
            // rhythm still says what fell off, /api/status still says it in words, and the phase
            // itself is untouched -- the document does not lie for the sake of a LED.
            comfoair_led_style_t calm = comfoair_led_style_of(COMFOAIR_LOOK_WORKING);
            style.r = calm.r;
            style.g = calm.g;
            style.b = calm.b;
        }
    }

    uint8_t envelope = envelope_of(style.rhythm, now_ms, arg, since_ms);
    // The AtomS3 case: with no second LED the colour channel has to carry the sign of life as
    // well as the meaning, so the flash is mixed into every look instead of being LOOK_OK's alone.
    if (!caps.has_mono && (now_ms % SPARK_PERIOD_MS) < SPARK_ON_MS && envelope < SPARK_LEVEL) {
        envelope = SPARK_LEVEL;
    }

    uint8_t ceiling = policy.ceiling;
    if (policy.quiet && ceiling > QUIET_CEILING) ceiling = QUIET_CEILING;

    frame.r          = style.r;
    frame.g          = style.g;
    frame.b          = style.b;
    frame.brightness = (uint8_t)((comfoair_led_gamma(envelope) * (uint32_t)ceiling) / 255u);
    // A FLOOR OF ONE, and it is the flash surviving arithmetic. It was written when SPARK_LEVEL
    // was 64: gamma(64) is 12, so the flash rounded to nothing at every ceiling below 22 of 255,
    // and inside the quiet hour -- ceiling 8 -- ONLINE was dark in the flash window and out of it
    // alike. THE FLOOR NO LONGER CARRIES THE FLASH: at level 255 the flash is the ceiling itself,
    // and the ceiling would have to be zero for it to round away. The floor stays because the
    // OTHER job of the flash needs it -- a frame identical to the last one is never sent, so
    // without a non-zero lit frame a latched corruption would have nothing to correct it -- and
    // because the same rounding still hides the first sixth of every ramp.
    // A ceiling of zero is the one exception and keeps its darkness: the owner was promised an off
    // switch, not "almost off".
    if (ceiling > 0 && envelope > 0 && frame.brightness == 0) frame.brightness = 1;
    // The pip encodes nothing, ever -- not the phase, not a fault, not the fifteen seconds of a
    // gesture. "Alive unless a gesture is running" is not "alive", and an owner
    // looking at a steady LED could not tell which of the two they were seeing.
    frame.mono_on = caps.has_mono && (now_ms % HEARTBEAT_PERIOD_MS) < HEARTBEAT_ON_MS;

    if (!caps.has_rgb) {
        // A board with no RGB channel has no colour to give. Carrying the eleven looks on a lone
        // mono LED is NOT designed: seven rhythms cannot separate eleven looks, which is the same
        // pigeonhole the look table resolves for colours and nothing resolves here. The driver
        // must not read this frame as "the look was expressed elsewhere".
        frame.r = frame.g = frame.b = frame.brightness = 0;
    }
    return frame;
}
