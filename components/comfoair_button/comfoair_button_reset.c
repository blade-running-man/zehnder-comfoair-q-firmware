// The half of the gesture that destroys something: the erase, the three confirming flashes, and
// the restart. One of the two esp_restart() calls in this firmware; the other is OTA's
// (comfoair_ota_runtime_restart_later(), into a freshly installed image).
//
// A FILE OF ITS OWN, and the split is by responsibility rather than by line count (though the
// ceiling is what forced the question): comfoair_button_task.c owns the pin and the loop and must
// stay readable as a loop, while everything here runs exactly once in the life of a gesture and
// ends the process. See comfoair_button_internal.h for the two calls that cross between them.
//
// WRAPPED IN #ifdef ESP_PLATFORM for the same reason as its neighbour: the host build compiles
// every .c in this directory the moment a suite includes comfoair_button.h. What could be decided
// without a pin was pushed further out still, into comfoair_button.c, where test/test_button_poller
// reaches it -- the tier that erases everything and the release gate are both host cases now.
#ifdef ESP_PLATFORM

#include "comfoair_button_internal.h"

#include "comfoair_config.h"
#include "comfoair_config_nvs.h"
#include "comfoair_led.h"
#include "comfoair_led_driver.h"
#include "comfoair_log.h"
#include "comfoair_mdns.h"
#include "comfoair_ota_runtime.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "comfoair_btn";

// The animation between the release and the reboot, hardware scenarios HV-BTN-07/HV-BTN-10:
// three flashes, 1,5 s in total. The number matters twice. It is what the owner sees INSTEAD of a
// device that goes dark and comes back -- "it did what I asked" rather than "it crashed" -- and it
// is the window inside which they may press again, which is what the gate below is for.
#define CONFIRM_FLASHES 3u
#define CONFIRM_ON_MS   250u
#define CONFIRM_OFF_MS  250u

// The confirming animation, ONE STEP AT A TIME so that the same picture can be driven from two
// places: the three flashes that follow the release, and the wait loop below, which must not leave
// the owner looking at a device that did nothing for as long as their finger stays on the button.
//
// NOT green: green is LOOK_OK's colour and the only overlay this component can raise is the
// BUTTON one, whose colour IS the tier (comfoair_led_pattern.c, button_style) -- amber for the
// soft reset, red for the factory reset, which is what hardware scenarios HV-BTN-07 and HV-BTN-10
// (docs/hardware-verification/07-indication-and-button.md) expect. A fourth overlay kind to say
// "done" in another hue is comfoair_led's decision and comfoair_led's suite, not a poller's.
//
// A tier length of zero is what draws it at full brightness (comfoair_button.h, the flash).
static void flash_step(comfoair_gesture_tier_t tier, bool on)
{
    if (on)
        comfoair_led_override_push(COMFOAIR_LED_OVR_BUTTON, comfoair_led_button_arg(tier, 0u));
    else
        comfoair_led_override_clear(COMFOAIR_LED_OVR_BUTTON);
}

// Three flashes, 1,5 s in total (HV-BTN-07/HV-BTN-10). The number matters twice: it is
// what the owner sees INSTEAD of a device that goes dark and comes back -- "it did what I asked"
// rather than "it crashed" -- and it is the window during which they may press again, which is
// what the gate below is for.
//
// SAMPLES NOTHING AND DECIDES NOTHING. The gesture is over and the decider is deliberately not fed
// during it: a second gesture cannot change what has already been erased.
static void confirm(comfoair_gesture_tier_t tier)
{
    for (uint32_t i = 0; i < CONFIRM_FLASHES; i++) {
        flash_step(tier, true);
        vTaskDelay(pdMS_TO_TICKS(CONFIRM_ON_MS));
        flash_step(tier, false);
        vTaskDelay(pdMS_TO_TICKS(CONFIRM_OFF_MS));
    }
}

// --- the reboot ---------------------------------------------------------------------------

// esp_restart() ONLY WITH THE BUTTON RELEASED -- the pin is read here and nowhere earlier, three
// readings in a row, and the last of them is what lets this loop out two statements before the
// restart.
//
// The pin on this board is also the download strap (board.h, the `button` field, and
// board_m5stack_nanoc6.c:16-18): holding it while power is applied drops the chip into the ROM
// loader, and the strap is latched on
// the release of reset -- a software restart included. Outwardly that is a device that died: no
// CAN, no Wi-Fi, no light, and an owner who was told "hold the button" repeating it. The gesture
// itself cannot walk into that, because every outcome is committed by the RELEASE edge and the
// confirmation above has run for a second and a half since -- unless the owner pressed again inside
// those one and a half seconds. That one case is exactly what this loop checks.
//
// THE NO-REBOOT RULE IS NOT BROKEN (AGENTS.md). It is
// conditional -- "nothing reboots BECAUSE A PEER IS ABSENT" -- and a
// button released after fifteen seconds of being held is not a peer. Nothing in this file watches a
// broker, a router or a bus.
//
// THIS WAS THE FIRST esp_restart() IN THE TREE (OTA's restart into a new image is the second):
// `grep -rn "esp_restart" components src` returned zero before it, and that zero was explained
// rather than accidental -- comfoair_net.c:20-23
// (a stored value that survives a reboot makes a panic a loop with no way out; nothing may reboot
// because a peer is absent) and comfoair_net_radio.c:107-109 (a captive portal that will not start
// is a device the owner reaches by typing an address; one that reboots is a device the owner does
// not reach at all). Both still hold: they forbid rebooting at a peer's absence, not rebooting at
// all. The consequence is for later readers -- the OTA code can no longer treat the absence of
// esp_restart as a marker of anything.
static void restart_when_the_finger_is_off(comfoair_gesture_tier_t tier)
{
    uint8_t  released_in_a_row = 0;
    uint32_t waited_ms         = 0;
    bool     lit               = false;

    // THREE CONSECUTIVE RELEASED SAMPLES, not one reading. The predicate is
    // comfoair_button_release_confirmed() and it lives in the pure half so that
    // test/test_button_poller can hold it to the same 60 ms of agreement everything else in this
    // system requires -- 60 ms justified by induction from the WS2812 line, which the animation
    // above has been driving for the last second and a half. A single reading landing in the make
    // bounce of a NEW press would restart the chip with a finger on the download strap, and
    // hardware scenario HV-BTN-13 asks the owner to make exactly that press.
    while (!comfoair_button_release_confirmed(&released_in_a_row, comfoair_button_pin_pressed())) {
        // The owner is not left looking at nothing while they hold. Same colour as the
        // confirmation, so "still going" and "done" are one language.
        lit = !lit;
        flash_step(tier, lit);

        // No bound on this wait, and that is the safe direction. A button held for ever -- a carton
        // leaning on the panel -- leaves a device that keeps ventilating the house with its
        // settings already erased on flash, which the next power cycle resolves. Restarting anyway
        // would hand that same owner a chip in the ROM loader and nothing to read.
        comfoair_button_wait_a_poll();
        waited_ms += COMFOAIR_BUTTON_POLL_MS;
        if (waited_ms % 5000u == 0)
            ESP_LOGW(TAG, "reset done; waiting for the button to be released before restarting");
    }

    comfoair_led_override_clear(COMFOAIR_LED_OVR_BUTTON);
    // The one measurement of this task's stack, taken where the deepest call has already returned
    // and one line before the process ends. TASK_STACK is otherwise a guess (HV-BTN-07 records it).
    ESP_LOGW(TAG, "restarting; stack high water mark %u bytes",
             (unsigned)(uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t)));

    // AFTER the release gate and BEFORE the restart, and that order is the whole of the placement.
    // The gate above -- three consecutive released samples -- is what keeps a finger off the
    // download strap through the reboot, and nothing may be inserted in front of it. These three
    // lines come after the loop has already let go, so they delay no sample.
    //
    // mdns_free() announces TTL 0 for the host name and the service, so the neighbours drop
    // `comfoair-XXXX.local` instead of caching an address that leads nowhere while the device
    // reboots.
    //
    // BUT THE WAIT IS FIXED AND THE RESTART IS UNCONDITIONAL, and that is the point of writing it
    // as three statements rather than one. mdns_free() has two waits with no timeout inside it --
    // a portMAX_DELAY on the service semaphore (mdns_service.c:43) and
    // `while (s_service_task_handle) vTaskDelay(10 ms)` (mdns_service.c:360-363) -- so calling it
    // straight from here would put an unbounded wait between an ERASED NVS and the reboot that is
    // supposed to bring the device up on it. The owner would be left with a device that has lost
    // its settings, is not restarting, and (the override is already cleared) is showing nothing.
    // comfoair_mdns_goodbye() therefore hands mdns_free() to a task and returns at once; we give it
    // COMFOAIR_MDNS_GOODBYE_MS and then reset the chip whatever it managed.
    //
    // DO NOT collapse this into a plain comfoair_mdns_goodbye() that waits. A stale record in a
    // neighbour's cache expires in minutes; a factory reset that does not reboot does not.
    comfoair_mdns_goodbye();
    vTaskDelay(pdMS_TO_TICKS(COMFOAIR_MDNS_GOODBYE_MS));
    esp_restart();
}

// --- what a release decided ---------------------------------------------------------------

// What the tier asked for, one line each, so a log read after a failure says WHICH settings were
// meant to go. The store walks this same enumeration and returns only the FIRST error, so this is
// as close as the device can get to naming what went: it names what was asked for, and the error
// says at least one did not. The previous version named nothing, which left an owner with a
// half-reset device and a line that said only "FAILED".
static void log_namespaces(comfoair_config_reset_t reset)
{
    for (int i = 0; i < COMFOAIR_CONFIG_NS_COUNT; i++) {
        const comfoair_config_ns_t ns = (comfoair_config_ns_t)i;
        if (comfoair_config_reset_erases(reset, ns))
            ESP_LOGW(TAG, "  namespace %s", comfoair_config_ns_name(ns));
    }
}

void comfoair_button_erase_and_restart(comfoair_gesture_tier_t tier)
{
    // The tier -> what-is-destroyed mapping is a host case (test_button_poller), not a comparison
    // written here: `tier == SOFT` in this one line makes five seconds erase the broker settings
    // and fifteen spare them, and nothing under #ifdef ESP_PLATFORM could ever have caught it.
    const bool hard = comfoair_button_tier_erases_everything(tier);
    const comfoair_config_reset_t reset =
        hard ? COMFOAIR_CONFIG_RESET_HARD : COMFOAIR_CONFIG_RESET_SOFT;

    // THE RETAINED HOME ASSISTANT TOPICS ARE NOT CLEANED UP HERE, AND THAT IS A KNOWN HOLE, not an
    // oversight: a hard reset today leaves the previous owner's broker holding a full set of
    // retained discovery configs for a device that has left the house. A retraction, if one is
    // ever added, goes exactly HERE, before the erase, bounded by its own wait -- a cleanup that
    // fails must never become a reset that fails: if the broker is unreachable the reset still
    // happens and the owner is told what could not be tidied.

    esp_err_t err = comfoair_config_nvs_reset(reset);
    if (err != ESP_OK) {
        // ONE RETRY, and only one. The failures NVS can have here are transient (a busy flash, a
        // page being garbage-collected) or structural (a corrupt partition); the first is worth a
        // second attempt costing tens of milliseconds, and the second will fail identically for
        // ever, so a loop would only spin under a finger that has already let go.
        ESP_LOGW(TAG, "factory reset did not take (%s); trying once more", esp_err_to_name(err));
        err = comfoair_config_nvs_reset(reset);
    }

    if (err != ESP_OK) {
        // NO RESTART ON A FAILED ERASE, and the reason is the log ring. The reboot's whole purpose
        // is to bring the device up on the erased configuration, and there is not one; meanwhile
        // the ring is RAM and does not survive a restart, so rebooting would destroy the only
        // channel that can tell the owner what happened (GET /api/log is one of the confirmations
        // that does not need the LED). The device stays up, reachable, and says so.
        //
        // A PARTIAL ERASE IS THE UGLY OUTCOME AND IT IS NAMED: the store carries on past a bad
        // namespace, so some may be gone while the running device still serves what it read at
        // boot. It is consistent again at its next power cycle and inconsistent until then, and
        // the only warning is a line that dies with the mains. No bench scenario can provoke it.
        ESP_LOGE(TAG, "factory reset FAILED (%s); nothing was restarted and some of these may "
                      "already be gone. Read GET /api/log NOW -- the ring dies with the mains",
                 esp_err_to_name(err));
        log_namespaces(reset);
        comfoair_led_override_clear(COMFOAIR_LED_OVR_BUTTON);
        return;
    }

    ESP_LOGW(TAG, "%s reset done by button; erased:", hard ? "hard" : "soft");
    log_namespaces(reset);

    if (hard) {
        // Not hygiene. The ring survives the erase of NVS and holds SSIDs, device names and
        // whatever else was logged, while a hard reset is the tier that means "this device is
        // leaving the house" -- and after it no UI password is set, so GET /api/log is open
        // to anyone. Only after the erase actually took: on the failure path above the same
        // ring is the only evidence left.
        comfoair_log_reset();
    }

    // BEFORE THE RESTART, AND ONLY ON THE PATH THAT RESTARTS. Within 30 s of an OTA reboot the
    // running image is still on probation (comfoair_ota_runtime.h), and a restart then makes the
    // bootloader boot the PREVIOUS slot -- so without this line the owner's reset would silently
    // become a downgrade, with the credentials already erased. An image that has run this task
    // through a five-second hold and a successful NVS erase is alive, which is all the 30 s timer
    // waits to learn. Here and not after the release gate: nothing may delay its samples, and the
    // tens of ms of otadata write hide inside the flashes' fixed 1,5 s.
    //
    // A FAILURE IS LOGGED AND THE RESET GOES ON. The erase has already happened; refusing to
    // restart would leave a device running on settings it no longer has, and the rollback that
    // follows is the old, working image -- worse than intended, not dangerous.
    const esp_err_t ota = comfoair_ota_runtime_confirm_now(hard ? "hard reset by button"
                                                                : "soft reset by button");
    if (ota != ESP_OK)
        ESP_LOGE(TAG, "new image not confirmed (%s); this restart boots the previous one",
                 esp_err_to_name(ota));

    confirm(tier);
    restart_when_the_finger_is_off(tier);
}

#endif  // ESP_PLATFORM
