// What the device should be doing, given what has happened to it: which network mode, whether
// the setup window is open, and whether the credentials it was just handed have to be undone.
//
// Pure logic, no ESP-IDF, and TIME COMES IN AS A PARAMETER on every call. That is not a
// testing convenience, it is the only way the cases this exists for can be written down at
// all: "the window closed while the owner was in the next room reading the password off the
// router" takes fifteen minutes to reproduce against a real clock and one line against this
// one. DO NOT reach for a clock in here -- the moment one call reads esp_timer_get_time(),
// half of test/test_provision stops being writable.
//
// Two mistakes the obvious implementation makes are structural, and are answered by the shape of
// this file rather than by any one function:
//
//  * Treating "credentials are stored" and "the device is on the owner's network" as the same
//    thing. They are not: store the pair, report credentials present, and the device carries on
//    sitting on an open access point with a mistyped password. COMFOAIR_PROV_TRIAL is that gap
//    given a name.
//  * Anchoring the setup window on boot rather than on the access point appearing. On the path
//    back from `Forget Wi-Fi` such a window really lasts setup_window minus the ~90 s the access
//    point takes to come up. Nothing here is anchored on init -- every clock starts on the event
//    it is about.
//
// Rules implemented here, named once for the rest of this component: THE UNCLAIMED RULE (an open
// access point on the air means unclaimed), THE WINDOW RULE (15 minutes, extended by activity,
// ceiling 60, anchored on the access point, its close observable; after a close the access point
// returns only at a reboot, for 5), THE FALLBACK (the access point returns after 30 minutes of
// failure, for the right reasons only), THE SEALED-DEVICE RULE (a never-closing window is valid).
//
// One invariant outranks all four (docs/architecture/failure-model.md): there is never a state with
// no network, no access point and no way back. Every state here has one -- a closed window has the
// next boot, a typo has the rollback, an unlisted disconnect reason has the fallback, and a station
// never given an address has comfoair_prov_address_never_arrived(). That last one reads as a gap in
// the fallback and is not: the fallback governs the access point coming back BY ITSELF, the window
// rule governs it coming back at a REBOOT, and a no-address failure's way back is the second one.
#pragma once

#include <assert.h>   // only for static_assert on comfoair_prov_t; see the struct
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Returned by the duration queries for "this does not have an end". Zero already means
// "nothing is running", and the two answers are not the same: a window with 0 ms left is
// closed, a window that never closes is the sealed-device configuration.
#define COMFOAIR_PROV_NEVER UINT32_MAX

typedef enum {
    // First boot ever, or a factory reset: nothing to connect to, so the access point is the
    // whole of the device's presence. By the unclaimed rule this is also what the device counts as
    // for as long as that access point is on the air, whatever lands in NVS meanwhile.
    COMFOAIR_PROV_UNCONFIGURED,
    // Credentials exist and an attempt is in flight.
    COMFOAIR_PROV_CONNECTING,
    // An address on the owner's network. The only state that proves a pair of credentials.
    COMFOAIR_PROV_CONNECTED,
    // Credentials were saved and the connection is NOT yet confirmed. A state in its own right
    // because conflating it with CONNECTED is the obvious bug -- a mistyped password then looks
    // like success -- and because it is the only state from which a rollback can happen.
    COMFOAIR_PROV_TRIAL,
    // An attempt failed; the next one is waiting out its backoff.
    COMFOAIR_PROV_RETRYING,
    // The device's own access point is back while the station keeps trying underneath it. Two
    // things reach here: the fallback, after sustained failure of the right kind, and init() on a
    // device whose previous run was never given an address (comfoair_prov_address_never_arrived).
    // The state is the same because the situation is -- an open access point on a unit that already
    // holds the owner's data, bounded by a window -- only the clock that reached it differs.
    COMFOAIR_PROV_FALLBACK_AP,
    // The window rule: the window ran out on a device that has nothing to connect to. Terminal
    // until the next boot, which is what makes reopen_window_ms the way back rather than a nicety.
    // A device that HAS credentials never lands here -- see comfoair_prov_state().
    COMFOAIR_PROV_WINDOW_CLOSED,
    // HOW MANY STATES THERE ARE, AND NOT A STATE. It exists so that code which must cover all of
    // them can say so to the compiler -- an exhaustive switch trips -Werror=switch (platformio.ini)
    // when a state is added, and a table meant to hold every state can be measured against this
    // instead of against a number somebody remembered. test/test_phase/phase_inputs.h is the case
    // that made it necessary: an eighth state slipped in there and the sweep that calls itself
    // "all seven provisioning states" went on green, sampling seven eighths of the input space.
    // DO NOT return it, store it, or give it a case in a switch that means to act on a state.
    COMFOAIR_PROV_STATE_COUNT,
} comfoair_prov_state_t;

// What the radio should be doing. The caller acts on this and then reports what actually
// happened, because "asked for an access point" and "an access point is on the air" are
// different facts and the window rule anchors its clock on the second one.
typedef enum {
    COMFOAIR_PROV_MODE_OFF,
    COMFOAIR_PROV_MODE_ACCESS_POINT,
    COMFOAIR_PROV_MODE_STATION,
    // Both at once (WIFI_MODE_APSTA). Not an optimisation: the answer to POST /api/provision
    // has to reach the phone that is still on the access point, and the reason a trial failed
    // is only useful if there is somewhere to show it. Expect the phone to drop anyway when
    // the station lands on a different channel and drags the access point with it -- that is
    // the invariant about answering before connecting, seen from the radio's side.
    COMFOAIR_PROV_MODE_AP_STA,
} comfoair_prov_mode_t;

// Four different answers to the owner instead of one "it will not connect". This is an
// invariant, not a nicety: "the router is rebooting" and "you changed the password"
// need different things done about them, and throwing the reason away leaves the owner with
// nothing to act on.
typedef enum {
    COMFOAIR_PROV_FAIL_NONE,
    COMFOAIR_PROV_FAIL_NETWORK_ABSENT,   // nothing answered; the SSID is not on the air here
    COMFOAIR_PROV_FAIL_WRONG_PASSWORD,   // it answered and would not take the key
    COMFOAIR_PROV_FAIL_REFUSED,          // it answered and said no for some other reason
    COMFOAIR_PROV_FAIL_NO_ADDRESS,       // associated, and no address ever arrived
    // How many verdicts there are, and not a verdict. Same rule and same DO NOT as
    // COMFOAIR_PROV_STATE_COUNT above.
    COMFOAIR_PROV_FAIL_COUNT,
} comfoair_prov_failure_t;

// The one-shot things the caller has to do. Drain with comfoair_prov_take_action() after every
// event and every tick: only one is held, and dropping COMMIT means the known-good pair is
// never updated, which quietly disarms the rollback.
typedef enum {
    COMFOAIR_PROV_ACTION_NONE,
    // (Re)start a station attempt with the credentials currently in NVS. Disconnect first if
    // the station is associated -- this is also how a station stuck without an address is made
    // to try again. That disconnect comes back as an event like any other, with reason 8, and
    // comfoair_prov_classify() knows it is ours; see the note there before adding a code to it.
    COMFOAIR_PROV_ACTION_CONNECT,
    // The pair in NVS just produced an address: copy it to the known-good slot. One write, one
    // flash cycle, and only on the transition -- not on every reconnection.
    COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS,
    // The trial failed: put the known-good pair back and connect with it. SSID and PSK are one
    // atomic write; NVS gives no atomicity between keys, and a power cut between two of them
    // leaves a new SSID with an old password, which is a typo nobody made.
    COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS,
} comfoair_prov_action_t;

// Every duration in milliseconds. Zero is a SETTING on three of these and a hazard on the rest,
// so each field says which it is -- there is no blanket rule, and writing one down was how this
// comment used to be wrong. Nothing clamps: a value the owner asked for is not silently rewritten
// here, and the field comments are where the price of each zero is named instead.
typedef struct {
    // The window rule. 15 minutes is long enough to finish setup and is the value with field
    // evidence behind it from shipped consumer devices: a setup access point left open
    // indefinitely after joining a network is a publicly disclosed attack class (unauthenticated
    // control within radio range, a pivot onto the home LAN). 0 = the window never closes, which
    // is the sealed device and a supported configuration, not an escape hatch.
    uint32_t setup_window_ms;
    // The window rule. The bound on what client activity can extend, measured from the access point
    // coming up rather than from the last activity, or it would bound nothing. 0 = no ceiling.
    uint32_t setup_window_ceiling_ms;
    // The window rule. Every window that comes back: after one has run out, after a reboot, and the
    // one init() offers a station that is never given an address. 0 = those windows never close
    // either, which is the same sealed device seen from its second boot.
    uint32_t reopen_window_ms;
    // The fallback. Sustained failure before the device raises its own access point again. 30
    // minutes, chosen so a router rebooting at night does not put an open access point on the air,
    // and so an owner does not spend a week without the device. 0 = the access point never comes
    // back by itself, and it also disables the no-address escape that shares this clock (see
    // comfoair_prov_address_never_arrived) -- so 0 here is a device with no way back at all.
    uint32_t fallback_after_ms;
    // How long a newly saved pair gets to produce an address before the known-good pair is put
    // back. 90 s: a working network associates and hands out an address well inside that, so a
    // pair that has produced none by then is treated as "this network is not working, go back".
    // DO NOT set this to 0: every pair the owner saves is then rolled back on the next tick, so
    // a device that has ever been on a network can never be moved to another one.
    uint32_t trial_timeout_ms;
    // How long an associated station gets to be given an address before that counts as the
    // fourth kind of failure. Nothing sends an event for this; its absence is the signal.
    // DO NOT set this to 0: an associated station is then declared address-less on the tick it
    // associates, which reconnects it, for ever.
    uint32_t address_timeout_ms;
    // Retry backoff, doubling from the first to the second. Never a reboot: a router that is
    // off for a weekend must not produce a device that reboots all weekend (AGENTS.md).
    // DO NOT set the minimum to 0: the delay stays 0 through every doubling, "has 0 ms passed"
    // is true on every call, and the retry becomes a station that reconnects on every tick.
    uint32_t retry_backoff_min_ms;
    uint32_t retry_backoff_max_ms;
} comfoair_prov_config_t;

// What the caller read out of NVS before any of this ran. Three facts, no values.
typedef struct {
    // An SSID worth trying is stored. NOT "the key exists", and the emptiness check has to live
    // in whatever fills this field, because the storage layer will hand back a zeroed blob as a
    // present one: a saved-but-zeroed blob is still a saved blob. A test that asks only whether
    // the key exists is therefore true after `Forget Wi-Fi`, and the device spends a boot
    // connecting to "". The right question is "stored AND the SSID is non-empty", and this field
    // is that question asked once, by the caller, before any of this runs.
    bool has_credentials;
    // A pair that has been seen to work is retained separately. False on a device that has
    // never been on a network, which is why a first provisioning cannot roll back.
    bool has_known_good;
    // A setup window has run out at some point in this device's life. The window rule: after that
    // the access point comes back for reopen_window_ms at each boot, and not by itself.
    bool window_closed_before;
    // The last run of this device ended with a station that had joined the owner's network and
    // was never given an address -- see comfoair_prov_address_never_arrived() for why this one
    // fact has to survive a reboot.
    bool address_never_arrived;
} comfoair_prov_boot_t;

// Opaque by convention, in the header because the caller allocates it. DO NOT add an SSID or a
// PSK field: this struct is what the provisioning status projection gets built from, and
// comfoair_secret_key() can only redact a named key in a document -- it cannot help a string
// that a handler printed because it was in the struct it was handed. The caller owns NVS; this
// knows only whether something is in it. The static_assert below is the tripwire: an ssid[32]
// would not fit under it, so the rule fails the build rather than the review.
typedef struct {
    comfoair_prov_config_t  cfg;
    comfoair_prov_state_t   state;
    comfoair_prov_action_t  action;
    comfoair_prov_failure_t failure;
    uint8_t                 failure_reason;  // the raw wifi_err_reason_t, for the owner's benefit
    bool has_credentials;
    bool has_known_good;
    bool credentials_committed;  // the pair in NVS is the known-good one
    bool ap_on_air;              // reported by the caller, not inferred from mode()
    bool window_running;
    bool window_ever_closed;
    bool window_is_the_short_one;  // which window length the access point on the air got
    bool trialling;
    bool associated;
    bool address_never_arrived;
    bool failing;
    bool retry_armed;
    uint8_t attempts;              // failed attempts since the last success or new pair
    uint32_t retry_delay_ms;       // how long the armed retry waits, kept so the deadline
                                   // arithmetic below stays "elapsed since", never "now >= then"
    uint32_t window_opened_at;    // when the access point came up (the window rule's anchor)
    uint32_t window_extended_at;  // the last activity that re-armed it
    uint32_t trial_at;
    uint32_t associated_at;
    uint32_t failing_at;
    uint32_t retry_at;
} comfoair_prov_t;

// Bytes, not fields, because what must not get in here is a string. 96 is comfortably above what
// the flags and timestamps need and comfortably below what one SSID would cost.
// test_the_machine_holds_no_credentials_only_the_fact_of_them says the same thing for a reader
// who is looking at the tests rather than at this file.
static_assert(sizeof(comfoair_prov_t) <= 96,
              "comfoair_prov_t grew enough to be holding something it must not");

// The numbers above, each with its reason. Fill a config, change what you need, pass it in.
void comfoair_prov_config_defaults(comfoair_prov_config_t *cfg);

// `cfg` NULL means the defaults; `boot` NULL means a device with nothing stored. Takes no
// timestamp on purpose: nothing in this machine is anchored on boot, which is the whole of
// the window rule's anchor.
void comfoair_prov_init(comfoair_prov_t *s, const comfoair_prov_config_t *cfg,
                        const comfoair_prov_boot_t *boot);

// --- events ------------------------------------------------------------------------------------

// The clock. Everything with a deadline is decided here, so a caller that stops ticking stops
// the machine -- deliberately, because that is easier to see than a machine that half runs.
void comfoair_prov_tick(comfoair_prov_t *s, uint32_t now_ms);

// The access point is ON THE AIR. The window rule anchors the window here and not on the decision
// to raise one; the unclaimed rule keys on this and not on what NVS holds. Every access point this
// firmware raises is open, so on-the-air and open are the same question here -- if one of them
// ever gains a password, the unclaimed rule keys on OPEN and this flag has to grow a second bit.
void comfoair_prov_on_ap_started(comfoair_prov_t *s, uint32_t now_ms);
void comfoair_prov_on_ap_stopped(comfoair_prov_t *s, uint32_t now_ms);

// Associated with the access point, no address yet (WIFI_EVENT_STA_CONNECTED). Not a success:
// the fourth kind of failure is the absence of the next event.
void comfoair_prov_on_associated(comfoair_prov_t *s, uint32_t now_ms);

// An address on the owner's network (IP_EVENT_STA_GOT_IP). The only thing that proves a pair.
void comfoair_prov_on_connected(comfoair_prov_t *s, uint32_t now_ms);

// `reason` is wifi_event_sta_disconnected_t.reason -- a uint8_t, ESP-IDF 5.5.5
// components/esp_wifi/include/esp_wifi_types_generic.h:1167, which is why no code above 255
// needs handling here.
void comfoair_prov_on_disconnected(comfoair_prov_t *s, uint32_t now_ms, uint8_t reason);

// A pair was written to NVS. Starts the trial and the rollback timer; does NOT mean connected,
// and the whole file exists to keep those two apart.
void comfoair_prov_on_credentials_saved(comfoair_prov_t *s, uint32_t now_ms);

// The owner pressed the button for less than a second. Opens the setup window FROM WHATEVER
// STATE THE MACHINE IS IN, for reopen_window_ms, and ERASES NOTHING -- not the credentials, not
// the known-good pair, not a pending action, not the failure the status page is showing.
//
// This is the only way out of COMFOAIR_PROV_WINDOW_CLOSED short of a reboot, and the only quick
// one for an owner whose Wi-Fi password has just changed: without it that device waits out the
// fallback's thirty minutes. It is also the one event whose source is a person standing at the
// device rather than a peer on the network, which is why it is allowed to re-arm a ceiling that
// bounds what a client on the access point may extend.
//
// THE EXPOSURE DOES NOT GROW, and it is worth saying rather than assuming: while the access point
// is up, POST /api/provision is open to everyone in radio range, exactly as it is for the fallback
// access point the fallback raises by itself. The difference is who can cause it -- and whoever can
// reach the button can also hold it for fifteen seconds and erase the device outright.
//
// IT IS NOT A WAY TO CHANGE THE BROKER. comfoair_prov_is_provisioned() is false while the access
// point is on the air, and the policy answers FORBIDDEN to every POST but /api/provision
// (comfoair_http_policy.c:59-67). For the five minutes of this window, saving broker settings gets
// a 403 -- from the LAN and from the access point alike.
void comfoair_prov_on_setup_requested(comfoair_prov_t *s, uint32_t now_ms);

// Somebody is talking to us on the access point. The window rule: this extends the window, because
// the window closing while the owner is fetching their router password is the most annoying failure
// in the setup flow and it is not an attack.
void comfoair_prov_on_client_seen(comfoair_prov_t *s, uint32_t now_ms);

// --- questions -----------------------------------------------------------------------------------

// DO NOT project state() before comfoair_prov_init() has run. A NULL machine answers
// WINDOW_CLOSED, which is the right answer for the radio -- it asks for nothing to be raised --
// but it is a real state with a meaning, and the window rule's demand for an observable close wants
// the setup window closing to be OBSERVABLE. A status endpoint that reads this on a device whose
// provisioning task has not started yet announces a transition that never happened. A caller that
// can be asked this early reports "starting up" from its own knowledge, not from here.
comfoair_prov_state_t comfoair_prov_state(const comfoair_prov_t *s);
comfoair_prov_mode_t  comfoair_prov_mode(const comfoair_prov_t *s);

// The unclaimed rule, and the single most important rule in this component. False whenever an open
// access point is on the air, whatever NVS holds -- because in that moment anyone in radio range is
// listening. Storing credentials must not flip it: that gap is where a passer-by sets the first UI
// password and takes the device.
//
// comfoair_net_is_provisioned() answers with this, and comfoair_http_check()'s `ctx.provisioned`
// asks that on every request. DO NOT feed it comfoair_net_has_credentials() instead: "credentials
// are stored" is the variant this rule rejects, because it leaves exactly the gap above open.
bool comfoair_prov_is_provisioned(const comfoair_prov_t *s);

bool comfoair_prov_has_credentials(const comfoair_prov_t *s);
bool comfoair_prov_has_known_good(const comfoair_prov_t *s);

// Open means: an access point is on the air AND its clock had not run out as of the last tick
// -- tick() is what closes it, so that the API, the log line and the UI all see it close at the
// same instant. False before the access point is up, even when mode() is already asking for
// one, and false for ever after it closes: nothing reopens a window (the window rule). DO NOT drive
// the radio from this -- mode() is what says whether the access point should exist; this says
// whether the setup flow behind it is still live, and it is what the API and the log report so that
// a window closing is observable rather than a network that silently vanished (the window rule).
bool     comfoair_prov_window_is_open(const comfoair_prov_t *s);
uint32_t comfoair_prov_window_remaining_ms(const comfoair_prov_t *s, uint32_t now_ms);

// A window has run out at some point. The caller persists this: the window rule's "the access point
// comes back for five minutes at each reboot" needs it to survive one.
bool comfoair_prov_window_has_closed(const comfoair_prov_t *s);

// A station that joined the owner's network and was never given an address, for as long as the
// fallback calls sustained. The caller persists this exactly as it persists the window flag above
// and hands it back through comfoair_prov_boot_t at the next boot.
//
// This exists because of the invariant that outranks every rule here: there is never a state with
// no network, no access point and no way back. A dead DHCP server or an exhausted lease pool
// leaves a station associated, unreachable, with no access point -- and the fallback excludes that
// failure from its thirty-minute return BY NAME ("not on no-address"), so nothing was ever going
// to raise one. A reboot reproduced it exactly, and recovery meant the front panel of a running
// ventilation unit.
//
// The way back is the window rule's rather than the fallback's, and that distinction is the whole
// of why this is not a re-decision of the fallback: it decides when the access point returns BY
// ITSELF, the window rule decides that after a window closes it returns at each REBOOT.
// init() offers the reopen window to a device that boots with this true, with the station still
// trying underneath it. An address clears it, and so does a new pair of credentials -- whether a
// network hands out addresses is a property of that network. DO NOT infer it from
// comfoair_prov_failure() == COMFOAIR_PROV_FAIL_NO_ADDRESS: that answer is true thirty seconds into
// a router that is still waking up, and acting on it would put an open access point on the air at
// the next boot of a perfectly healthy device.
bool comfoair_prov_address_never_arrived(const comfoair_prov_t *s);

// Whether the pair currently in NVS should be undone. Latched with the RESTORE action so it
// answers false again once the caller has done it.
bool comfoair_prov_should_roll_back(const comfoair_prov_t *s);

comfoair_prov_failure_t comfoair_prov_failure(const comfoair_prov_t *s);
uint8_t                 comfoair_prov_failure_reason(const comfoair_prov_t *s);

// One spelling of each answer, decided here. Four different answers to the owner is the
// invariant; the REST projection and the MQTT one inventing two spellings of them is how that
// invariant gets lost (AGENTS.md: one entity list, ever).
const char *comfoair_prov_failure_name(comfoair_prov_failure_t failure);

// How long until the fallback brings the access point back, or COMFOAIR_PROV_NEVER when nothing is
// scheduled to. NEVER while the failure is COMFOAIR_PROV_FAIL_NO_ADDRESS: the fallback says the
// access point returns only when the network is absent or refuses us, "not when we simply have no
// address yet", and that exclusion is kept literally -- nothing here raises an access point for a
// no-address failure, ever.
//
// The invariant that the exclusion used to collide with is answered somewhere else on purpose:
// comfoair_prov_address_never_arrived() offers that device a window at its next BOOT, which is
// the window rule's mechanism and not the fallback's. A caller showing "no access point is
// scheduled" should show that alongside it, or the owner reads "nothing will happen" for a device
// that has a way back.
uint32_t comfoair_prov_fallback_in_ms(const comfoair_prov_t *s, uint32_t now_ms);

// The one-shot the caller owes the world. Returns it and clears it; call after every event.
comfoair_prov_action_t comfoair_prov_take_action(comfoair_prov_t *s);

// The number -> answer table, exposed so the mapping can be pinned by number rather than by
// scenario. ESP-IDF 5.5.5, components/esp_wifi/include/esp_wifi_types_generic.h:113-178.
// COMFOAIR_PROV_FAIL_NONE means "this disconnect is not a verdict on anything" -- reason 8 is
// what esp_wifi_disconnect() raises, and COMFOAIR_PROV_ACTION_CONNECT is what tells the caller to
// call it, so that number is this machine's own nudge arriving back at it. Everything else is one
// of the owner's four answers.
comfoair_prov_failure_t comfoair_prov_classify(uint8_t reason);

#ifdef __cplusplus
}
#endif
