#include "comfoair_provision.h"

#include "comfoair_provision_internal.h"

#include <stddef.h>

// Every interval in this file is "has `limit` passed since `since`", never "is now past a
// stored deadline". A millisecond counter is 32 bits and wraps every 49.7 days; a ventilation
// unit runs for years. Unsigned subtraction is correct across exactly one wrap, an absolute
// comparison is not -- it would close the setup window the instant the clock passed
// 0xFFFFFFFF, seven weeks into an uptime nobody is watching. DO NOT "simplify" either of
// these into a deadline comparison; test_the_clock_wrapping_does_not_close_the_window_early
// is what stands guard over it.
static bool elapsed(uint32_t now, uint32_t since, uint32_t limit)
{
    return (uint32_t)(now - since) >= limit;
}

uint32_t comfoair_prov_remaining(uint32_t now, uint32_t since, uint32_t limit)
{
    const uint32_t gone = (uint32_t)(now - since);
    return gone >= limit ? 0u : limit - gone;
}

// Zero is a legal millisecond count -- a caller that starts its clock at boot passes it on the
// first tick -- so nothing here uses 0 as "unset". Each timestamp has a bool beside it.

// RESTORE and COMMIT are durable writes to NVS; CONNECT is a nudge. So a pending write is
// never displaced by a nudge. The one thing that DOES displace a pending write is a new pair
// of credentials, which cancels it outright -- see comfoair_prov_on_credentials_saved().
//
// Returns whether it took, and the caller in tick() keeps the retry armed when it did not. A
// dropped CONNECT is a station that never tries again, which on a device with no access point
// is exactly the stranding failure-model.md forbids -- so the arming stays until the
// nudge is actually issued rather than being cleared on the assumption that it was.
static bool set_action(comfoair_prov_t *s, comfoair_prov_action_t action)
{
    if (action == COMFOAIR_PROV_ACTION_CONNECT && s->action != COMFOAIR_PROV_ACTION_NONE)
        return false;
    s->action = action;
    return true;
}

// The window rule: the first window of a device's life is the long one; every window that comes
// back is the short one. The long one is for somebody who has just unboxed a device; the short
// one is for somebody standing in front of a device that already has an owner, and it is short
// because its access point is open and sits on a unit holding that owner's data.
//
// Which one an access point gets is decided when it COMES UP, in on_ap_started(), and read from
// the latch here. DO NOT ask the live question instead: has_credentials goes true the moment the
// owner submits the form, so a live test would cut the owner's own fifteen-minute window down to
// five at the instant they pressed the button --
// test_credentials_saved_mid_window_do_not_shorten_the_window_around_them stands guard over it.
uint32_t comfoair_prov_window_length(const comfoair_prov_t *s)
{
    // The sealed-device rule first, and it OUTRANKS the distinction below rather than being a case
    // of it. Zero is the setting for the ESP32 sealed behind the front panel of a running
    // ventilation unit, and the device that needs it is exactly the device whose first window ran
    // out unattended in a plant room -- so `window_ever_closed` is true on it and reading the short
    // window here would put the one unrecoverable device permanently off the air. The field's
    // comment in the header promises this unconditionally; this is where that promise is kept.
    if (s->cfg.setup_window_ms == 0)
        return 0;

    return s->window_is_the_short_one ? s->cfg.reopen_window_ms : s->cfg.setup_window_ms;
}

// The window rule's two lengths, as ONE predicate, because two events now answer it and a device
// that got the wrong one is either an open access point for fifteen minutes in every forty-five
// or an owner cut down to five minutes on the day they unbox the thing.
//
// has_credentials is in the test as well as window_ever_closed because a device that provisioned
// inside its first window never had one run out, and its fallback access point would otherwise get
// the first-run fifteen minutes -- on a unit that already holds the owner's data.
// Conversely a device with neither is a device straight out of the box, and the first window it is
// ever offered is the long one however that window was asked for. The first version of
// comfoair_prov_on_setup_requested() set this true unconditionally and cut exactly that owner's
// first setup window from fifteen minutes to five -- the commonest path in the device's life, and
// no case anywhere saw it.
static bool window_is_the_short_one(const comfoair_prov_t *s)
{
    return s->window_ever_closed || s->has_credentials;
}

bool comfoair_prov_window_open_at(const comfoair_prov_t *s, uint32_t now)
{
    if (!s->ap_on_air || !s->window_running)
        return false;

    // The sealed-device zero, and the ORDER is the point rather than the zero itself
    // (comfoair_prov_window_length() above is where that is decided). DO NOT apply the ceiling
    // before this test: the ceiling exists to bound what client ACTIVITY can extend, and there is
    // nothing to extend on a window that never closes -- checking it first would put a silent
    // 60-minute lockout on the one
    // device that cannot recover from one.
    const uint32_t length = comfoair_prov_window_length(s);
    if (length == 0)
        return true;

    if (s->cfg.setup_window_ceiling_ms != 0 &&
        elapsed(now, s->window_opened_at, s->cfg.setup_window_ceiling_ms))
        return false;

    return !elapsed(now, s->window_extended_at, length);
}

// Doubling, with a floor and a ceiling. The ceiling is what keeps "retry for ever" from
// meaning "and it will notice the router came back an hour later"; there is no attempt limit
// at all, because the alternative to retrying is rebooting and nothing here reboots because a
// peer is absent (AGENTS.md).
static uint32_t backoff_ms(const comfoair_prov_t *s, uint8_t attempt)
{
    uint32_t delay = s->cfg.retry_backoff_min_ms;
    for (uint8_t i = 0; i < attempt; i++) {
        // The second test is not redundant with the first. `delay * 2` above 2^31 wraps to a
        // SMALLER number, so with a ceiling near UINT32_MAX the first test never fires and the
        // thirty-third attempt computes 1000 << 32 -- which is zero, and elapsed(now, retry_at, 0)
        // is true on every call. The backoff would collapse into a station hammering a network
        // that has been refusing it for weeks, which is the opposite of what arm_retry() promises.
        if (delay >= s->cfg.retry_backoff_max_ms || delay > UINT32_MAX / 2u)
            break;
        delay *= 2u;
    }
    return delay > s->cfg.retry_backoff_max_ms ? s->cfg.retry_backoff_max_ms : delay;
}

// The fallback names three of the four answers and excludes the fourth by name: the AP comes
// back "only if the attempts fail because the network was not found or authentication was
// refused -- not because there is no IP". Four places read that line -- both clocks in tick(),
// the countdown the API publishes, and the disconnect path -- so it is drawn once. Two spellings
// of it is how "no access point for a no-address failure" quietly becomes "usually".
bool comfoair_prov_failure_earns_the_access_point(comfoair_prov_failure_t failure)
{
    return failure == COMFOAIR_PROV_FAIL_NETWORK_ABSENT ||
           failure == COMFOAIR_PROV_FAIL_WRONG_PASSWORD ||
           failure == COMFOAIR_PROV_FAIL_REFUSED;
}

// The one place a failure becomes the standing answer, because the disconnect path and tick()'s
// address timeout both reach it and both feed the same clock.
static void note_failure(comfoair_prov_t *s, comfoair_prov_failure_t failure, uint8_t reason,
                         uint32_t now)
{
    // The fallback measures a run of failures on ONE side of its line, not "any failure for
    // 30 minutes". Twenty-nine minutes of "the network is not there" followed by the station
    // associating and waiting on DHCP is not thirty minutes of no-address, and carrying the older
    // timestamp across the change of answer would spend the escape below on a router whose DHCP
    // server merely took a moment to wake up after a power cut. Repeated failures of the SAME
    // kind must not re-anchor it either, or a clock that measures "sustained" would restart every
    // thirty seconds and never reach anything.
    if (!s->failing ||
        comfoair_prov_failure_earns_the_access_point(s->failure) !=
            comfoair_prov_failure_earns_the_access_point(failure)) {
        s->failing    = true;
        s->failing_at = now;
    }

    s->failure        = failure;
    s->failure_reason = reason;
}

static void arm_retry(comfoair_prov_t *s, uint32_t now)
{
    s->retry_delay_ms = backoff_ms(s, s->attempts);
    s->retry_at       = now;
    s->retry_armed    = true;
    // Saturate rather than wrap: an attempt count that rolls over to 0 would silently reset
    // the backoff to a second and hammer a network that has been refusing us for weeks.
    if (s->attempts < 32u)
        s->attempts++;
}

void comfoair_prov_config_defaults(comfoair_prov_config_t *cfg)
{
    if (cfg == NULL)
        return;

    cfg->setup_window_ms         = 15u * 60u * 1000u;
    cfg->setup_window_ceiling_ms = 60u * 60u * 1000u;
    cfg->reopen_window_ms        = 5u * 60u * 1000u;
    cfg->fallback_after_ms       = 30u * 60u * 1000u;
    cfg->trial_timeout_ms        = 90u * 1000u;
    cfg->address_timeout_ms      = 30u * 1000u;
    cfg->retry_backoff_min_ms    = 1000u;
    cfg->retry_backoff_max_ms    = 30u * 1000u;
}

void comfoair_prov_init(comfoair_prov_t *s, const comfoair_prov_config_t *cfg,
                        const comfoair_prov_boot_t *boot)
{
    if (s == NULL)
        return;

    const comfoair_prov_t empty = {0};
    *s = empty;

    if (cfg != NULL)
        s->cfg = *cfg;
    else
        comfoair_prov_config_defaults(&s->cfg);

    if (boot != NULL) {
        s->has_credentials       = boot->has_credentials;
        s->has_known_good        = boot->has_known_good;
        s->window_ever_closed    = boot->window_closed_before;
        s->address_never_arrived = boot->address_never_arrived;
    }

    // Deliberately NOT credentials_committed = has_known_good. Whether the pair in NVS is the
    // known-good one is unknowable from here: a power cut in the middle of a trial leaves an
    // untried pair stored and the old one in the known-good slot, and nothing in NVS says so.
    // Assuming the worst costs one redundant write on the first successful connection after a
    // boot, and makes that connection re-confirm whatever actually works. Assuming the best
    // would leave a device whose known-good pair is a network it has never reached.
    s->credentials_committed = false;

    s->state  = s->has_credentials ? COMFOAIR_PROV_CONNECTING : COMFOAIR_PROV_UNCONFIGURED;
    s->action = s->has_credentials ? COMFOAIR_PROV_ACTION_CONNECT : COMFOAIR_PROV_ACTION_NONE;

    // The invariant that outranks every rule here (failure-model.md): never a state with no
    // network, no access point and no way back. A station that associates and is never given an
    // address had all three -- it is unreachable, the fallback excludes it BY NAME, and this line
    // is where the previous boot's answer stopped it being reproduced for ever. The fallback is
    // not touched by it: it decides when the access point comes back BY ITSELF, and the window
    // rule has already decided that after a window closes it comes back at each REBOOT. Those are
    // different mechanisms, and this is the second one.
    //
    // FALLBACK_AP rather than a state of its own because it is exactly that state: the access
    // point is up (mode() answers AP_STA) and the station keeps trying underneath it -- an access
    // point that stopped trying the network would be a trap rather than a rescue. tick() closes
    // the window after reopen_window_ms and does NOT start the fallback's clock for it, so the
    // exposure is five minutes per boot and not an open access point for ever.
    if (s->has_credentials && s->address_never_arrived)
        s->state = COMFOAIR_PROV_FALLBACK_AP;
}

// --- events ------------------------------------------------------------------------------------

void comfoair_prov_on_ap_started(comfoair_prov_t *s, uint32_t now_ms)
{
    if (s == NULL || s->ap_on_air)
        return;  // a repeated event must not hand out a second window

    // The window rule's anchor, and the whole reason this is an event rather than a field set at
    // init: the clock starts when the access point is ON THE AIR. Starting it at boot is the
    // obvious implementation, and on the path back from `Forget Wi-Fi` -- where the access point
    // only appears ~90 s in -- it hands the owner setup_window minus a minute and a half. That is
    // not an acceptable price.
    s->ap_on_air           = true;
    s->window_running      = true;
    s->window_opened_at    = now_ms;
    s->window_extended_at  = now_ms;

    // Which of the two lengths this access point gets, decided here and not read live -- see
    // comfoair_prov_window_length().
    s->window_is_the_short_one = window_is_the_short_one(s);
}

void comfoair_prov_on_ap_stopped(comfoair_prov_t *s, uint32_t now_ms)
{
    (void)now_ms;
    if (s == NULL)
        return;

    // The access point leaving the air is not the window running out, and only the second one
    // shortens the next window. The usual reason to be here is a successful connection.
    s->ap_on_air      = false;
    s->window_running = false;
}

void comfoair_prov_on_associated(comfoair_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    // Associated is not connected. The fourth answer to the owner -- "it joined the network and
    // was never given an address" -- has no disconnect code behind it; it is the absence of the
    // next event, which is why this timestamp exists.
    //
    // DO NOT clear s->failure here on the reasoning that a fresh attempt is in flight. Associating
    // is not evidence that the previous answer was wrong: a wrong PSK associates perfectly well
    // and fails afterwards, at the four-way handshake (WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT,
    // esp_wifi_types_generic.h:131). Clearing it would blank the status page to "no failure" on
    // every retry of a network that is not going to work, which is how an owner ends up with a
    // device that says nothing is wrong and never connects. The answer is replaced when the next
    // outcome is known, and by nothing else.
    s->associated    = true;
    s->associated_at = now_ms;
}

void comfoair_prov_on_connected(comfoair_prov_t *s, uint32_t now_ms)
{
    (void)now_ms;
    if (s == NULL)
        return;

    s->state          = COMFOAIR_PROV_CONNECTED;
    s->associated     = false;
    s->trialling      = false;
    s->retry_armed    = false;
    s->attempts       = 0;
    // The fallback measures SUSTAINED failure; one address ends the run, both for the thirty-minute
    // return in tick() step 5 and for the no-address escape in step 6.
    // test_an_address_ends_the_run_of_failures_that_preceded_it stands guard over this line
    // -- and note that the countdown alone does not, because the failure is cleared below
    // and a cleared failure answers NEVER whatever this flag says.
    s->failing        = false;
    s->failure        = COMFOAIR_PROV_FAIL_NONE;
    s->failure_reason = 0;
    s->has_credentials = true;
    // An address is the one thing that disproves "this network never gives us one". Without this
    // the caller would keep the fact in NVS and every boot for the rest of the device's life
    // would put an open access point on the air for five minutes.
    s->address_never_arrived = false;

    // The access point comes down. That is not tidiness: by the unclaimed rule the device
    // is unclaimed for as long as an open access point is on the air, so a connection that left one
    // up would leave the device writable by the neighbourhood for the rest of the window.
    s->window_running = false;

    // An address on the owner's network is the ONLY thing that makes a pair known-good. Once
    // per pair, not once per reconnection -- a flapping router would otherwise spend a flash
    // cycle every few minutes rewriting the same two strings.
    if (!s->credentials_committed) {
        s->credentials_committed = true;
        s->has_known_good        = true;
        set_action(s, COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS);
    }
}

void comfoair_prov_on_disconnected(comfoair_prov_t *s, uint32_t now_ms, uint8_t reason)
{
    if (s == NULL)
        return;

    // Nothing stored, nothing to fail at: the radio raises this event in situations that have
    // no stored network behind them, a mode change among them, and such an event carries no
    // answer for the owner. Ignoring it whole is load-bearing rather than tidy -- classifying
    // it would start the fallback's thirty-minute clock, and the fallback would then raise an
    // access point on a device whose setup window had deliberately closed for good.
    if (!s->has_credentials)
        return;

    s->associated = false;

    // A disconnect this machine asked for is not a verdict on anything, and
    // comfoair_prov_classify() answers COMFOAIR_PROV_FAIL_NONE for those. The header's contract for
    // COMFOAIR_PROV_ACTION_CONNECT orders the caller to disconnect an associated station before
    // reconnecting it, so the machine's own recovery nudge comes straight back here as reason 8 --
    // and recording it would answer "the router will not have us" for a dead DHCP server, overwrite
    // the NO_ADDRESS answer tick() had just reached, and start the fallback's clock for the one
    // failure it excludes by name. The retry below is still armed for it: a disconnect that arms
    // nothing is a station that never tries again, which is the stranding the invariants forbid.
    const comfoair_prov_failure_t verdict = comfoair_prov_classify(reason);
    if (verdict != COMFOAIR_PROV_FAIL_NONE)
        note_failure(s, verdict, reason, now_ms);  // and this is where the fallback's clock starts

    // A trial survives its own disconnections: the rollback is on a clock, not on the first
    // discouraging event. One 4-way-handshake timeout is also what a crowded 2.4 GHz band
    // produces on a first attempt, and undoing the owner's correct new password over that is
    // worse than waiting out trial_timeout_ms.
    if (!s->trialling)
        s->state = COMFOAIR_PROV_RETRYING;

    arm_retry(s, now_ms);
}

void comfoair_prov_on_credentials_saved(comfoair_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    s->has_credentials       = true;
    s->credentials_committed = false;
    s->trialling             = true;
    s->trial_at              = now_ms;
    s->state                 = COMFOAIR_PROV_TRIAL;

    // Whoever submitted the form is standing there watching. Making them wait out a
    // half-minute backoff earned by the previous network is how a working device looks broken.
    s->attempts    = 0;
    s->retry_armed = false;

    // A fresh pair gets a fresh 30 minutes before the fallback raises it, and no stale
    // reason on the status page while a new attempt is in flight. The no-address escape goes with
    // them: whether a network hands out addresses is a property of the network, and the owner has
    // just named a different one.
    s->failing               = false;
    s->failure               = COMFOAIR_PROV_FAIL_NONE;
    s->failure_reason        = 0;
    s->address_never_arrived = false;

    // DO NOT let a pending action survive this. Both of them name "the pair in NVS", and the
    // pair in NVS has just changed underneath them: an un-drained COMMIT would promote the
    // untried new pair to known-good and destroy the very thing the rollback restores, and an
    // un-drained RESTORE would throw away what the owner just typed.
    s->action = COMFOAIR_PROV_ACTION_CONNECT;

    // Submitting the form is the strongest evidence a human is present. If the window ran out
    // mid-trial the owner would lose the page that tells them what went wrong.
    if (s->window_running)
        s->window_extended_at = now_ms;
}

void comfoair_prov_on_setup_requested(comfoair_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    // The ONLY state this event moves, and it moves it because WINDOW_CLOSED is the one state
    // whose mode() is MODE_OFF -- the radio told to do nothing at all. Everything else already
    // answers with an access point once window_running is set, so touching their state would be
    // inventing a transition to solve a problem that does not exist. That restraint is the whole
    // of why the "RETRYING for ever" trap cannot appear here: the trap was FALLBACK_AP being set
    // from a state that had a working station underneath it, and nothing below sets a state at all.
    //
    // UNCONFIGURED and not FALLBACK_AP even though credentials may exist: a device that reached
    // WINDOW_CLOSED has none (tick() step 1 only sends UNCONFIGURED there), so the two would
    // behave identically today -- and UNCONFIGURED is the one that stays right if that ever
    // changes, because it is the state the closing branch will send it back to.
    if (s->state == COMFOAIR_PROV_WINDOW_CLOSED)
        s->state = COMFOAIR_PROV_UNCONFIGURED;

    // Four fields that already existed, and NOT ONE NEW ONE: the static_assert on
    // comfoair_prov_t is a tripwire against a string getting in here, and every field added for a
    // good reason spends the slack it runs on.
    s->window_running = true;

    // Both anchors, and window_opened_at deliberately among them even though it re-arms the
    // sixty-minute ceiling. The ceiling bounds what CLIENT ACTIVITY can extend -- a phone on
    // the open access point must not be able to hold it up for ever -- and a finger on the button
    // is not a client: whoever pressed it is standing at the device and could as easily hold it
    // for fifteen seconds and erase everything. Refusing them a window because the
    // last one started an hour ago would make the answer to "the page timed out while I was
    // fetching the router password" a power cut.
    //
    // These are the anchors for the window that is running RIGHT NOW, and on a device whose access
    // point is not yet on the air they are provisional: on_ap_started() re-anchors both when it
    // actually comes up, which is the window rule's anchor and the reason the press does not start
    // the clock the owner is charged for.
    s->window_opened_at   = now_ms;
    s->window_extended_at = now_ms;

    // Which length, decided by the SAME predicate on_ap_started() uses and not by a `true` written
    // here. Every window that COMES BACK is the short one -- reopen_window_ms, not a new constant
    // -- but a device that has never had a window close and holds no credentials is
    // not coming back to anything: it is a new device whose owner has just pressed the button
    // during their first setup, and cutting them from fifteen minutes to five is the reverse of
    // what this event is for. This line exists at all because the access point may ALREADY be on
    // the air, in which case on_ap_started() will never fire to decide it.
    s->window_is_the_short_one = window_is_the_short_one(s);

    // AND NOTHING ELSE. No action, no failure, no credential fact, no clock belonging to the
    // retry or the fallback. "Erases nothing" is the promise of the short press, and the way an
    // event breaks it is not by erasing -- it is by displacing the pending COMMIT that keeps the
    // rollback armed, or by clearing a failure the status page is showing the owner.
}

void comfoair_prov_on_client_seen(comfoair_prov_t *s, uint32_t now_ms)
{
    // The live question, not the last tick's answer. window_running is only cleared by tick(), so
    // asking it here handed a whole fresh window to a client that knocked in the second between a
    // window running out and the tick that closes it -- and again at the next expiry, up to the
    // ceiling. A closed window does not reopen because somebody knocked, and this is the line that
    // makes that sentence true rather than nearly true.
    if (s == NULL || !comfoair_prov_window_open_at(s, now_ms))
        return;

    s->window_extended_at = now_ms;
}

// --- the clock ----------------------------------------------------------------------------------

void comfoair_prov_tick(comfoair_prov_t *s, uint32_t now_ms)
{
    if (s == NULL)
        return;

    // 1. The setup window ran out.
    if (s->window_running && s->ap_on_air && !comfoair_prov_window_open_at(s, now_ms)) {
        s->window_running     = false;
        s->window_ever_closed = true;

        if (s->state == COMFOAIR_PROV_UNCONFIGURED) {
            // Nothing to connect to, so the radio has nothing left to do. This is the one
            // state with no network and no access point, and the window rule pays for it with the
            // short window every reboot gives -- which is the "way back" the invariants demand.
            s->state = COMFOAIR_PROV_WINDOW_CLOSED;
        } else if (s->state == COMFOAIR_PROV_FALLBACK_AP) {
            // A device with credentials never lands in WINDOW_CLOSED. Its window closing means
            // one thing: the access point goes off the air and the station carries on. The fallback
            // brings it back thirty minutes later, so the exposure is a duty cycle
            // rather than an open access point for ever -- and the clock for that restarts HERE,
            // when the access point actually goes away, rather than at the disconnect thirty
            // minutes ago, or the two access points would run back to back.
            //
            // The test around it is not tidiness. This branch is also reached by the access point
            // that init() raises for a station that is never given an address, and there the
            // restart would do two wrong things at once: bring that access point back by itself
            // thirty minutes later, which the fallback forbids for a no-address failure, and push
            // back the escape clock in step 6 by the length of every window it serves.
            s->state = COMFOAIR_PROV_RETRYING;
            if (comfoair_prov_failure_earns_the_access_point(s->failure)) {
                s->failing    = true;
                s->failing_at = now_ms;
            }
        }
    }

    // 2. The trial ran out: the pair the owner just saved has not produced an address.
    if (s->trialling && elapsed(now_ms, s->trial_at, s->cfg.trial_timeout_ms)) {
        s->trialling = false;
        if (s->has_known_good) {
            // The likeliest catastrophe in this whole project is not an attack, it is a typo:
            // the owner changes networks from the settings page, mistypes one character, the
            // old pair is already overwritten, no access point comes up because credentials
            // exist -- and the device simply disappears.
            s->state       = COMFOAIR_PROV_CONNECTING;
            s->retry_armed = false;
            s->attempts    = 0;
            s->credentials_committed = true;  // what is about to be in NVS is the known-good pair
            set_action(s, COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS);
        } else {
            // A first provisioning has nothing to roll back to, and erasing the SSID they
            // picked out of the scan list would only make them do it all again. The way back
            // here is the access point still on the air, and after that the fallback's.
            s->state = COMFOAIR_PROV_RETRYING;
        }
    }

    // 3. Associated, and no address ever arrived.
    if (s->associated && elapsed(now_ms, s->associated_at, s->cfg.address_timeout_ms)) {
        // Starts a clock, but not the fallback's: step 5 below refuses this answer by name, and
        // step 6 is the one it feeds. `0` because this is the one answer with no reason code behind
        // it -- it is the absence of the next event, not an event.
        note_failure(s, COMFOAIR_PROV_FAIL_NO_ADDRESS, 0, now_ms);
        if (s->state != COMFOAIR_PROV_FALLBACK_AP)
            s->state = s->trialling ? COMFOAIR_PROV_TRIAL : COMFOAIR_PROV_CONNECTING;

        // Clear `associated` only once the nudge has actually been issued, exactly as step 4
        // keeps its retry armed. set_action() refuses a CONNECT while a durable write waits to be
        // drained -- and clearing the flag first threw the refused nudge away, leaving a machine
        // that has forgotten it was associated and has no timer left. That is a station that
        // never tries again, on a device with no access point.
        if (set_action(s, COMFOAIR_PROV_ACTION_CONNECT))
            s->associated = false;
    }

    // 4. The backoff expired: try again.
    if (s->retry_armed && elapsed(now_ms, s->retry_at, s->retry_delay_ms) &&
        set_action(s, COMFOAIR_PROV_ACTION_CONNECT)) {
        s->retry_armed = false;
        if (s->state == COMFOAIR_PROV_RETRYING)
            s->state = COMFOAIR_PROV_CONNECTING;
    }

    // 5. The fallback: sustained failure of the right kind brings the access point back by itself.
    //
    // "Of the right kind" is comfoair_prov_failure_earns_the_access_point(), and it is the term
    // that keeps the fallback's exclusion. Every disconnect used to reach this, because the
    // classifier could only answer one of the three that count -- so the fourth answer had no way
    // in and the exclusion held by accident. It no longer does: the address timeout in step 3
    // feeds this same clock now, and reason 8 no longer answers anything at all.
    //
    // has_credentials is the line between the fallback and the window rule. The fallback is for a
    // device that HAS a network and cannot reach it; a device with nothing stored has no network
    // to fail at, and the window rule brings its window back at a reboot and not on its own --
    // without which a closed window would be a fifteen-minute pause rather than a bound. Today it
    // is a belt: comfoair_prov_on_disconnected() returns before setting `failing` on such a
    // device, so nothing can reach here with credentials missing and a failure standing. DO NOT
    // drop it on that reasoning -- the 5 s button hold (device/button.md) forgets Wi-Fi, and the
    // event that implements it is exactly what makes the two facts come apart.
    if (s->has_credentials && s->failing &&
        comfoair_prov_failure_earns_the_access_point(s->failure) && !s->ap_on_air &&
        s->state != COMFOAIR_PROV_FALLBACK_AP && s->cfg.fallback_after_ms != 0 &&
        elapsed(now_ms, s->failing_at, s->cfg.fallback_after_ms)) {
        // Thirty minutes, because a router rebooting at night must not put an open access
        // point on the air, and because a week without the device is the other failure. The
        // station keeps trying underneath -- an access point that stopped trying the network
        // would be a trap rather than a rescue.
        s->state = COMFOAIR_PROV_FALLBACK_AP;
    }

    // 6. The way back for the failure the fallback excludes from step 5, and the reason step 5
    // can afford to exclude it at all.
    //
    // "Not on no-address" is the fallback's own wording, kept: nothing here raises an access point.
    // What it does is record, for the caller to put in NVS, that this device has associated with
    // the owner's network and been refused an address for as long as the fallback calls sustained.
    // init() reads it back at the next boot and offers the reopen window then -- the window rule's
    // mechanism ("back at each reboot, not by itself"), not the fallback's. Without it the state is
    // no network, no access point and no way back, reproduced by every reboot, with recovery
    // through the front panel of a running ventilation unit.
    if (s->failing && s->failure == COMFOAIR_PROV_FAIL_NO_ADDRESS &&
        s->cfg.fallback_after_ms != 0 && elapsed(now_ms, s->failing_at, s->cfg.fallback_after_ms))
        s->address_never_arrived = true;
}
