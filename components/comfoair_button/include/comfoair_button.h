// The physical gesture, decided by a pure function and nothing else: what a hold MEANS, with no
// pin, no clock and no task (comfoair_button_task.h is the half that has them).
//
// Three tiers and exactly three: a press under a second asks for the setup window and ERASES
// NOTHING, five seconds forgets the network and the owner, fifteen seconds sends the device out of
// this house. The thresholds live here and nowhere else -- comfoair_config.h:196-199 says in prose
// that "the two durations are the gesture's, not this component's", and comfoair_led.h says the
// same from the other side: the LED is handed a tier and a length and never learns what five
// seconds means.
//
// PURE, TOTAL, NO CLOCK, NO GLOBALS, NO PIN. Nothing here includes ESP-IDF, FreeRTOS or board.h:
// the sample arrives as `pressed` and the time as `now_ms`. The active-low inversion of
// board.h:42-44 is applied ONCE, by the polling task (comfoair_button_task.c), at the sample --
// which is why the day a board with the button on another pin and the other polarity arrives, the
// board file changes and neither this decider nor its suite does.
//
// TASK CONTEXT: one state per button, owned by its poller and touched from that task only. There
// is no lock inside, because there is no state shared between two callers to lock.
//
// WHY THIS IS SAFE ON A DOWNLOAD STRAP. GPIO9 on the NanoC6 is the button AND the
// ESP32-C6 download strap, so holding it while power is applied drops the chip into the ROM
// loader and the firmware never runs. Two properties of this decider keep the gesture clear of
// that: no tier is decided by REACHING a threshold -- every outcome is committed by the release
// edge, which only running firmware can observe -- and a button that reads pressed on the very
// first sample is mute until it has been released once. "Held through power-on but the strap
// somehow did not latch" is exactly the strap case, and it stays silent.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_led.h"  // comfoair_gesture_tier_t: declared once, by the component that draws it

#ifdef __cplusplus
extern "C" {
#endif

// THE THREE NUMBERS OF THE POLLING CONTRACT, HERE BECAUSE TWO COMPONENTS NEED THEM AND ONE OF
// THEM IS A TASK. They were private to comfoair_button.c until the polling task had to answer
// the same two questions this decider answers -- "is this gap a release" and "has the finger been
// off long enough" -- and a second copy of 60 in a second file is exactly the drift every comment
// in this header is written against.
//
// DEBOUNCE_MS is 60 and it is NOT chosen from the mechanics of a tactile dome, whose
// bounce is under 5 ms: it is chosen from induction, because the GPIO9 trace runs beside the WS2812
// data line and the IR LED (esp32-node-hardware.md §4, "On-board peripherals -- the exact pins").
// No data edge on either holds for 60 ms.
// POLL_MS is 20, which is three samples inside that debounce and two FreeRTOS ticks at
// CONFIG_FREERTOS_HZ=100. RELEASE_SAMPLES is the debounce expressed in polls, rounded up -- the
// number of consecutive released readings that count as "the finger is off", derived here rather
// than written as 3 so that changing either number above cannot leave it saying something else.
#define COMFOAIR_BUTTON_DEBOUNCE_MS 60u
#define COMFOAIR_BUTTON_POLL_MS     20u
#define COMFOAIR_BUTTON_RELEASE_SAMPLES \
    ((COMFOAIR_BUTTON_DEBOUNCE_MS + COMFOAIR_BUTTON_POLL_MS - 1u) / COMFOAIR_BUTTON_POLL_MS)

// How long the confirming flash at a tier boundary stays at full brightness. 100 ms because the
// render task ticks every 25 ms: anything shorter than three or four of its frames can be missed
// entirely, and a confirmation the owner did not see is worse than none. It lives here, beside the
// rule that decides WHEN to flash, because comfoair_button_plan_draw() is what applies both.
#define COMFOAIR_BUTTON_FLASH_MS 100u

// Private. Zeroed by comfoair_button_init() and read by nothing outside comfoair_button.c; it is
// in the header only because the caller owns the storage. DO NOT reach into it from a task: the
// whole of what a caller needs comes back from comfoair_button_step() by value, and a second
// reader of these fields is a second interpretation of the thresholds.
typedef struct {
    uint8_t  phase;
    uint32_t press_ms;  // the FIRST pressed sample of this press, not the moment debounce expired:
                        // otherwise the five seconds the owner counts start 60 ms late
    uint32_t edge_ms;   // when the current release candidate began
    uint32_t held_ms;   // the press length, frozen at the first released sample
    uint32_t last_ms;
    bool     seen_sample;
    comfoair_gesture_tier_t tier;
    uint32_t                tier_since_ms;
    uint32_t                tier_ms;
} comfoair_button_t;

// Everything one sample can tell the caller, RETURNED BY VALUE. Not a take_action() callback and
// not a flag to be drained later: a gesture that can be forgotten is an erased NVS with no owner,
// or a factory reset the owner performed and the device ignored.
typedef struct {
    // What the release that just happened decided; COMFOAIR_GESTURE_NONE on every other sample,
    // and NONE also for a release that decided nothing. At most one gesture per call, and each
    // press produces at most one in its life.
    comfoair_gesture_tier_t gesture;
    // What a release RIGHT NOW would decide -- which is exactly what the LED must draw, because
    // the overlay's promise to the owner is "let go here and this happens". NONE while nothing is
    // held, inside the dead zone, and after the button has been declared stuck; comfoair_led.h
    // drops a BUTTON overlay whose tier is NONE, so those three answers all mean "show the phase".
    comfoair_gesture_tier_t tier;
    // When this tier began and how long it lasts, on the caller's own clock. Both go straight into
    // comfoair_led_override_t: since_ms into its field of that name, tier_ms through
    // comfoair_led_button_arg(tier, tier_ms). The ramp means "how much is left until the next
    // tier", so tier_since_ms + tier_ms is the threshold itself and the bar fills exactly onto it.
    uint32_t tier_since_ms;
    uint32_t tier_ms;
    // The tier CHANGED on this sample, and it says nothing about which way. True on the one sample
    // that crossed a threshold, however many samples arrive inside a tier -- and equally true on
    // the three samples where the overlay comes OFF: entering the dead zone at one second, giving
    // the stuck button up at twenty-five, and the sample that commits the gesture after a release
    // (where gesture is SOFT, tier is NONE and this flag is true, all three at once).
    //
    // THE RULE FOR THE CALLER IS THEREFORE TWO RULES, NOT ONE:
    //   tier_changed                          -> repush or clear the BUTTON overlay
    //   tier_changed && tier != GESTURE_NONE   -> AND flash, because a tier was ENTERED
    // A caller that flashed on every change would flash at the moment the owner is being told
    // "nothing will happen now", which is the opposite message; a caller that only acted when the
    // tier is non-NONE would never take the overlay down and would leave the last colour burning
    // after the gesture ended. Both halves are pinned by
    // test_a_tier_that_changes_to_none_is_not_a_flash.
    //
    // The flash exists here at all because it was taken out of the ramp deliberately: a spike of
    // brightness is not a monotonic ramp (the owner's decision,
    // comfoair_led_pattern.c:149-153 -- "DO NOT add it to the ramp"). Confirmation is an EVENT and
    // arrives here as one; a caller that pushed it on every sample would strobe.
    bool tier_changed;
} comfoair_button_out_t;

// Puts the machine in the state where nothing will be decided until the button has been seen
// RELEASED once. That state is not "idle", and the difference is the download strap: whether the
// first sample reads pressed or released is what separates a gesture from a boot with a finger on
// the panel, so it must not be assumed either way.
void comfoair_button_init(comfoair_button_t *s);

// One sample. `pressed` is already in the button's own terms -- true means the owner's finger is
// on it -- and `now_ms` is a monotonic millisecond clock, esp_timer_get_time()/1000 on the device.
//
// The clock is uint32_t and wraps every 49.7 days; every comparison inside is "elapsed since the
// anchor", never "now >= then", the rule comfoair_provision.h:215-216 states for its own
// deadlines, so a hold that straddles the wrap is measured correctly. A sample whose timestamp is
// BELOW the previous one counts as no time passed at all: the state is left exactly as it was and
// the previous tier is returned unchanged, because a clock that goes backwards must not be able to
// erase a device.
//
// SAMPLES MUST ARRIVE MORE OFTEN THAN THE 60 ms DEBOUNCE -- the poller runs at
// 20 ms, three samples inside it. The decider has nothing but the samples it is given, so a gap
// wider than the debounce is indistinguishable from the button being let go, and it is READ as
// being let go: the press ends there and the next press starts a new one. That direction is
// deliberate. A starved sampler then loses a gesture the owner has to repeat, instead of gluing
// two separate presses into a hold long enough to erase the device.
//
// `s` must not be NULL and must have been through comfoair_button_init().
comfoair_button_out_t comfoair_button_step(comfoair_button_t *s, bool pressed, uint32_t now_ms);

// WHAT A POLLER SHOULD CALL INSTEAD OF comfoair_button_step(), and the difference is a defect this
// header used to describe wrongly. The paragraph above promises that a gap wider than the debounce
// is READ as a release. For a gap that arrives while the button is UP that was always true; for a
// gap that arrives while it is HELD it was not, because the decider in PH_DOWN sees pressed
// followed by pressed and goes on counting from the old anchor. Three seconds down, a stall that
// swallows the release whole, three seconds down again -- and the machine hands out SOFT, erasing
// a network nobody asked to lose. A stall that long is real in this firmware: a write to NVS from
// a web handler stops everything that is not in IRAM.
//
// This wrapper closes it in the one place that can: before passing the caller's sample on, it
// feeds the decider a RELEASED sample stamped at the last time the caller could actually vouch
// for. The press then ends there -- the pending outcome is dropped rather than emitted, which is
// a direction chosen deliberately (a lost gesture costs a second attempt, a fired one
// costs the configuration) -- and the sample that follows either starts a new press or completes a
// release that had already happened. A gesture the owner really finished before the stall is still
// delivered: the frozen length is the true one.
//
// The synthetic sample cannot itself decide anything, and that is a property of the decider rather
// than an assumption: a released sample entering PH_DOWN only arms PH_MAYBE_RELEASE, the tier it
// reports is the frozen one it was already reporting, and no other phase reacts to a release with
// an outcome. Its result is therefore dropped.
//
// A caller that can guarantee its own period may still use comfoair_button_step(); nothing in the
// tree can.
comfoair_button_out_t comfoair_button_sample(comfoair_button_t *s, bool pressed, uint32_t now_ms);

// --- the three decisions the polling task used to make in code no host could reach --------------

// The flash's own state, owned by the poller and passed back in on every call. Two fields and no
// clock: whether the confirming flash is up, and when it went up.
typedef struct {
    bool     on;
    uint32_t since_ms;
} comfoair_button_flash_t;

// What to do with the BUTTON overlay for one sample. Both flags false means "leave it exactly as
// it is", which is the answer on most samples of most holds.
typedef struct {
    bool     clear;  // comfoair_led_override_clear(COMFOAIR_LED_OVR_BUTTON)
    bool     push;   // comfoair_led_override_push(COMFOAIR_LED_OVR_BUTTON, arg)
    uint32_t arg;    // already packed by comfoair_led_button_arg(); valid when push
} comfoair_button_draw_t;

// The two rules of `tier_changed` above, applied. THIS IS WHERE THEY LIVE, and it is in the pure
// half on purpose: they were four lines in a FreeRTOS task under #ifdef ESP_PLATFORM, where a
// mutation that flashed on the change to NONE -- telling the owner "confirmed" at the instant they
// are being told "nothing will happen" -- survived every test in the tree.
//
// A tier that is not NONE is pushed on EVERY sample and not only on a change: comfoair_led's
// driver does not restamp a BUTTON push whose arg repeats, so the ramp measures the tier rather
// than the interval between two calls, and a poller that pushed only on changes would draw the
// same picture at more risk. During the flash the packed length is zero, which the RAMP branch of
// comfoair_led_pattern.c draws at full brightness -- deliberately, and this is the caller that
// decision was written for. The ramp for the new tier therefore starts COMFOAIR_BUTTON_FLASH_MS
// late and reaches its ceiling that much after the threshold; 1 % of the ten-second HARD tier, and
// invisible beside the flash that marks the same instant.
comfoair_button_draw_t comfoair_button_plan_draw(const comfoair_button_out_t *out, uint32_t now_ms,
                                                comfoair_button_flash_t *flash);

// "The finger is off the pin", as a predicate over a sequence of samples rather than one reading.
// `consecutive` is the caller's counter, zeroed before the first call; every pressed sample resets
// it. True once COMFOAIR_BUTTON_RELEASE_SAMPLES readings in a row have said released.
//
// THE ONE CALLER IS THE REBOOT GATE, and one undebounced reading is not enough for it. Everything
// else in this system requires 60 ms of agreement, and the 60 ms is justified by induction from
// the WS2812 line -- the very line the confirming animation is driving while this runs. The owner
// is invited by hardware scenario HV-BTN-13 to press the button again during that animation, so a
// single reading landing in the make-bounce of that new press would restart the chip with a finger
// on the download strap, which is the one outcome the whole gate exists to prevent.
bool comfoair_button_release_confirmed(uint8_t *consecutive, bool pressed);

// Which of the two erasing tiers takes everything. A function rather than `tier == HARD` written
// in the task, because the task cannot be reached by a host test and this one line decides whether
// five seconds erases the broker settings or fifteen seconds spares them.
bool comfoair_button_tier_erases_everything(comfoair_gesture_tier_t tier);

#ifdef __cplusplus
}
#endif
