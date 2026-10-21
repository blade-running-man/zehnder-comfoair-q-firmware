// The decider of the button gesture (docs/device/button.md), and nothing else:
// no pin, no clock, no task, no log. Everything it knows arrives in the two arguments of
// comfoair_button_step(), which is what makes "released at 4 999 ms" and "a hold across the 32-bit
// millisecond wrap" cases in test/test_button_gesture instead of things somebody tried by hand.
#include "comfoair_button.h"

// The thresholds: THIS IS THE ONLY COPY. comfoair_config.h:196-199 describes the two durations in
// prose and calls them the gesture's; comfoair_led.h is handed a tier and a length and never a
// threshold. A second set of numbers anywhere else is the drift both of those comments exist to
// prevent.
//
// The dead zone between the first and the second window is load-bearing, not slack: four seconds
// in which any release is an unambiguous cancel is what keeps "the finger slipped" and "somebody
// leant on the panel" from opening an access point or erasing a network.
#define DEBOUNCE_MS  COMFOAIR_BUTTON_DEBOUNCE_MS  // in the header: the poller needs it too
#define SETUP_END_MS 1000u
#define SOFT_MS      5000u
#define HARD_MS      15000u
#define STUCK_MS     25000u

// PH_UNTIL_RELEASED is the state the machine BOOTS IN, and that is the download strap
// argument turned into structure rather than into a comment: before a sample has been seen, "the
// button is up" is an assumption, and the one boot where it is wrong is the boot where the owner
// powered the device up with a finger on the button -- GPIO9 is the ESP32-C6 download strap, and
// such a boot must stay mute. The machine therefore starts deaf and is armed only by a sample that
// reads released.
//
// The stuck button (held past the last window) lands in the same state, and it is the SAME state
// rather than a second one that behaves identically: "no sample yet" and "seen and disqualified"
// ask for exactly one behaviour -- say nothing until this button has been let go -- and two names
// for one behaviour is a distinction no test can reach.
enum button_phase {
    PH_UNTIL_RELEASED = 0,
    PH_IDLE,
    PH_MAYBE_PRESS,
    PH_DOWN,
    PH_MAYBE_RELEASE,
};

typedef struct {
    comfoair_gesture_tier_t tier;
    uint32_t                from;       // ms since the anchor at which this window opens
    uint32_t                len;        // and how long it lasts, i.e. where the next threshold is
    bool                    past_last;  // the hold ran off the end of the table: the stuck button
} window_t;

// The threshold table, walked rather than branched, so that the tier a release yields and the
// tier the LED is showing cannot be computed two different ways. They are the same question asked
// at two times -- "what would letting go right now do" -- and one answer serves both.
static window_t window_of(uint32_t held_ms) {
    static const struct {
        uint32_t                until;
        comfoair_gesture_tier_t tier;
    } k_windows[] = {
        {DEBOUNCE_MS, COMFOAIR_GESTURE_NONE},   // not a press at all yet
        {SETUP_END_MS, COMFOAIR_GESTURE_SETUP}, // open the setup window, erase nothing
        {SOFT_MS, COMFOAIR_GESTURE_NONE},       // the dead zone
        {HARD_MS, COMFOAIR_GESTURE_SOFT},       // cfg_wifi + cfg_owner, cfg_app survives
        {STUCK_MS, COMFOAIR_GESTURE_HARD},      // every namespace: the device leaves this house
    };
    // A fifth tier must not be able to land without a window to live in: the enumeration is the
    // LED's (comfoair_led.h) and the windows are the gesture's, and this is the one line where the
    // two counts meet.
    _Static_assert(COMFOAIR_GESTURE_COUNT == 4,
                   "a tier was added to comfoair_gesture_tier_t: give it a window in k_windows, or "
                   "say here why it is one the gesture never produces");

    uint32_t from = 0;
    for (unsigned i = 0; i < sizeof(k_windows) / sizeof(k_windows[0]); i++) {
        if (held_ms < k_windows[i].until) {
            window_t w = {k_windows[i].tier, from, k_windows[i].until - from, false};
            return w;
        }
        from = k_windows[i].until;
    }
    // Past the last threshold the button is declared stuck: a bag pressing it against the wall of
    // a cabinet must erase nothing, not after a minute and not after an hour. Length
    // zero because there is no next threshold to ramp towards -- and the tier is NONE, so
    // comfoair_led_resolve() drops the overlay and the phase colour comes back.
    // past_last, and not "held_ms >= STUCK_MS" spelled out a second time in the state machine: the
    // threshold has ONE home, which is the last row of the table above. Two homes is how a guard
    // moves and the picture does not, or the picture moves and the guard does not -- and neither
    // half of that pair is visible in the other's tests.
    window_t stuck = {COMFOAIR_GESTURE_NONE, from, 0, true};
    return stuck;
}

void comfoair_button_init(comfoair_button_t *s) {
    comfoair_button_t zero = {0};
    zero.phase             = PH_UNTIL_RELEASED;
    zero.tier              = COMFOAIR_GESTURE_NONE;
    *s                     = zero;
}

comfoair_button_out_t comfoair_button_step(comfoair_button_t *s, bool pressed, uint32_t now_ms) {
    // Named, not positional: gesture and tier are the SAME TYPE, and so are tier_since_ms and
    // tier_ms, so a field reordered in the header would compile in silence and hand the caller the
    // tier as the outcome. That is the class of defect this project has already paid for twice.
    comfoair_button_out_t out = {
        .gesture       = COMFOAIR_GESTURE_NONE,
        .tier          = s->tier,
        .tier_since_ms = s->tier_since_ms,
        .tier_ms       = s->tier_ms,
        .tier_changed  = false,
    };

    // A timestamp below the previous one is "no time passed". The state is left
    // untouched and the tier already announced is returned as it stands. DO NOT rebase last_ms on
    // the lower value to "resynchronise": that turns one bad reading into a rewound anchor, and a
    // rewound anchor inside a hold is a fifteen-second press that reports five.
    if (s->seen_sample && (int32_t)(now_ms - s->last_ms) < 0) return out;
    s->last_ms     = now_ms;
    s->seen_sample = true;

    switch ((enum button_phase)s->phase) {
        case PH_UNTIL_RELEASED:
            // The download strap and the stuck button, in the same branch: a
            // button that is already down when the firmware takes its first look, and one that has
            // been down past the last window, are both mute until let go. A clean release is
            // enough to rearm -- a machine that stayed deaf would turn one bag against a cabinet
            // wall into a device with no factory reset until it is power-cycled.
            if (!pressed) s->phase = PH_IDLE;
            break;

        case PH_IDLE:
            if (pressed) {
                s->phase = PH_MAYBE_PRESS;
                // The anchor is the FIRST pressed sample and not the moment the
                // debounce expires. Anchoring 60 ms later would start the five seconds the owner
                // is counting 60 ms after their finger landed.
                s->press_ms = now_ms;
            }
            break;

        case PH_MAYBE_PRESS:
            // 60 ms of CONTINUOUS contact, and there is no majority vote: chatter
            // that alternates faster than the debounce must not accumulate towards a press, or a
            // burst of interference from the WS2812 line beside GPIO9 spells a tap.
            if (!pressed) {
                s->phase = PH_IDLE;
            } else if ((uint32_t)(now_ms - s->press_ms) >= DEBOUNCE_MS) {
                s->phase = PH_DOWN;
            }
            break;

        case PH_DOWN:
            // The release is tested BEFORE the stuck threshold, and the order is the decision: a
            // sample that arrives late and reads released cannot prove the finger was still there
            // at the twenty-fifth second, so the length is whatever the clock says and the table
            // answers NONE for it. Erasing on an ambiguity is the one outcome worth avoiding here.
            if (!pressed) {
                s->phase = PH_MAYBE_RELEASE;
                s->edge_ms = now_ms;
                // The outcome is fixed by the FIRST released sample. A release at
                // 4.99 s confirmed at 5.05 s is a 4.99 s press and no reset.
                s->held_ms = (uint32_t)(now_ms - s->press_ms);
            } else if (window_of((uint32_t)(now_ms - s->press_ms)).past_last) {
                s->phase = PH_UNTIL_RELEASED;
            }
            break;

        case PH_MAYBE_RELEASE:
            if (pressed && (uint32_t)(now_ms - s->edge_ms) < DEBOUNCE_MS) {
                // Coming back down INSIDE THE DEBOUNCE returns the machine to DOWN
                // with the ANCHOR UNMOVED. Otherwise one scratchy eight-second hold reads as two
                // short ones and the owner who counted to eight gets nothing.
                s->phase = PH_DOWN;
            } else if (pressed) {
                // And outside it the button was genuinely up, so this is a NEW press and the old
                // anchor is dead. DO NOT drop the guard: without it a sampler that starved -- and
                // the poller runs at priority 4, below everything that may not miss a
                // frame -- glues two separate presses into one, and three seconds plus a gap plus
                // three seconds erases the network the owner never asked to lose.
                //
                // The pending outcome is dropped rather than emitted, and that direction is the
                // decision: emitting it here would fire a reset WHILE THE BUTTON IS DOWN, which is
                // precisely the state a reboot must never see (strap held = bootloader). A lost
                // gesture costs the owner a second attempt; a fired one their configuration.
                s->phase = PH_IDLE;
            } else if ((uint32_t)(now_ms - s->edge_ms) >= DEBOUNCE_MS) {
                // The reason the esp_restart() that follows a reset is safe: reaching a
                // threshold arms and nothing more; the release commits. By the time anything acts
                // on this the button has been up for at least the debounce, so the gesture cannot
                // put a finger on the strap pin and a power-on in the same window.
                out.gesture = window_of(s->held_ms).tier;
                s->phase    = PH_IDLE;
            }
            break;
    }

    // What a release RIGHT NOW would decide -- which is exactly what the LED must draw, because
    // the overlay's promise to the owner is "let go here and this happens". While
    // the release is being debounced the answer is the frozen length, not the growing one: the
    // finger is already off, and a ramp that kept climbing would promise a tier that can no longer
    // be reached.
    comfoair_gesture_tier_t tier  = COMFOAIR_GESTURE_NONE;
    uint32_t                since = 0;
    uint32_t                len   = 0;
    if (s->phase == PH_MAYBE_PRESS || s->phase == PH_DOWN || s->phase == PH_MAYBE_RELEASE) {
        const uint32_t held =
            (s->phase == PH_MAYBE_RELEASE) ? s->held_ms : (uint32_t)(now_ms - s->press_ms);
        const window_t w = window_of(held);
        tier             = w.tier;
        since            = (uint32_t)(s->press_ms + w.from);
        len              = w.len;
    }

    // The confirming flash on entering a tier is an EVENT: true on the one sample that crossed
    // a threshold, however many samples arrive inside the tier. It was taken out of the ramp
    // deliberately -- a spike of brightness is not a monotonic ramp -- and
    // comfoair_led_pattern.c:149-153 says "DO NOT add it to the ramp" from the other side.
    out.tier_changed = (tier != s->tier);
    s->tier          = tier;
    s->tier_since_ms = since;
    s->tier_ms       = len;
    out.tier         = tier;
    out.tier_since_ms = since;
    out.tier_ms       = len;
    return out;
}

// --- what the polling task of Task 6 would otherwise decide where nothing can test it -----------

comfoair_button_out_t comfoair_button_sample(comfoair_button_t *s, bool pressed, uint32_t now_ms) {
    // The whole of the fix, and it is three lines because the decider already knows what to do
    // with a release -- it just never saw one. See the header for why PH_DOWN could not close this
    // itself: pressed followed by pressed is not a release to anything that only has the samples.
    //
    // The stamp is s->last_ms and not now_ms: the release is placed at the last instant this poller
    // could actually vouch for the button, which is what makes the length frozen here the true one
    // and what puts the following sample OUTSIDE the debounce, so a press that survived the stall
    // starts a new anchor instead of returning to the old one.
    //
    // Its result is dropped. That is safe rather than convenient: a released sample can only arm
    // PH_MAYBE_RELEASE, it reports the frozen tier the machine was already reporting, and no phase
    // answers a release with an outcome -- so there is nothing in it to lose. DO NOT act on it
    // instead "to be safe": acting would let a synthetic sample fire a factory reset.
    if (s->seen_sample && (uint32_t)(now_ms - s->last_ms) >= DEBOUNCE_MS)
        (void)comfoair_button_step(s, false, s->last_ms);

    return comfoair_button_step(s, pressed, now_ms);
}

comfoair_button_draw_t comfoair_button_plan_draw(const comfoair_button_out_t *out, uint32_t now_ms,
                                                comfoair_button_flash_t *flash) {
    comfoair_button_draw_t plan = {false, false, 0};

    // Rule one of the two: a change -- ANY change, including the change to nothing -- repushes or
    // clears. Rule two: only a change INTO a tier is a flash, which is why this branch takes the
    // overlay down and sets nothing alight. The dead zone at one second and the stuck button at
    // twenty-five both arrive here.
    if (out->tier == COMFOAIR_GESTURE_NONE) {
        flash->on = false;
        plan.clear = out->tier_changed;
        return plan;
    }

    if (out->tier_changed) {
        flash->on       = true;
        flash->since_ms = now_ms;
    } else if (flash->on && (uint32_t)(now_ms - flash->since_ms) >= COMFOAIR_BUTTON_FLASH_MS) {
        flash->on = false;
    }

    plan.push = true;
    // A length of zero IS the flash (see the header). Everything else about the wire format is
    // comfoair_led's, and this is the only place in the gesture that packs one -- by calling the
    // helper that owns the format rather than shifting by hand.
    plan.arg = comfoair_led_button_arg(out->tier, flash->on ? 0u : out->tier_ms);
    return plan;
}

bool comfoair_button_release_confirmed(uint8_t *consecutive, bool pressed) {
    if (pressed) {
        *consecutive = 0;
        return false;
    }
    // Saturating, because this counter is polled for as long as a button stays stuck -- possibly
    // for days -- and a counter that wrapped would answer "not yet" once every five seconds for
    // ever after.
    if (*consecutive < COMFOAIR_BUTTON_RELEASE_SAMPLES)
        (*consecutive)++;
    return *consecutive >= COMFOAIR_BUTTON_RELEASE_SAMPLES;
}

bool comfoair_button_tier_erases_everything(comfoair_gesture_tier_t tier) {
    // Exhaustive rather than `tier == HARD`, so that -Werror=switch in [env:native] stops a fifth
    // tier being added without an answer here. The default for anything that is not the last
    // window is NO: the tier that erases the broker settings is named, never assumed.
    switch (tier) {
        case COMFOAIR_GESTURE_HARD:
            return true;
        case COMFOAIR_GESTURE_NONE:
        case COMFOAIR_GESTURE_SETUP:
        case COMFOAIR_GESTURE_SOFT:
        case COMFOAIR_GESTURE_COUNT:
            break;
    }
    return false;
}
