// The short press (docs/device/button.md, The setup press): the owner puts a finger on the button
// for less than a second and the setup window opens, WHATEVER THE MACHINE WAS DOING and WITHOUT
// ERASING ANYTHING.
//
// It lives in a directory of its own rather than in test/test_provision, and not for tidiness:
// that suite is 1039 lines against a ceiling of 600 (AGENTS.md), and test/test_button_gesture --
// the other obvious home for it -- is 597 of the same 600. A case appended to either would have
// been a case appended to a file that is already over, or one line under.
//
// WHAT THIS SUITE IS FOR, in one sentence: the gesture must be able to rescue a device from every
// state this machine has, and it must cost nothing to try. Those are two separate claims and the
// second is the dangerous one -- a "recovery" that quietly re-anchored a clock, dropped a pending
// write or moved the machine sideways would be a gesture the owner is afraid to use, which is the
// same as not having one: the short press ERASES NOTHING.
#include <unity.h>

#include "comfoair_provision.h"

static const uint32_t SECOND = 1000u;
static const uint32_t MINUTE = 60u * 1000u;

static comfoair_prov_t booted(bool has_credentials, bool has_known_good = false,
                              bool window_closed_before = false,
                              bool address_never_arrived = false) {
  comfoair_prov_boot_t boot{};
  boot.has_credentials       = has_credentials;
  boot.has_known_good        = has_known_good;
  boot.window_closed_before  = window_closed_before;
  boot.address_never_arrived = address_never_arrived;

  comfoair_prov_t s{};
  comfoair_prov_init(&s, nullptr, &boot);
  return s;
}

// The caller's loop, as far as this suite needs it: whatever mode() asks for is raised, and the
// machine is told when the access point is actually on the air. The two are separate on purpose --
// the window is anchored on the second one -- which is exactly why the button event has to work
// before the radio has caught up with it.
static void radio_follows(comfoair_prov_t *s, uint32_t now) {
  const comfoair_prov_mode_t mode = comfoair_prov_mode(s);
  const bool wants_ap =
      mode == COMFOAIR_PROV_MODE_ACCESS_POINT || mode == COMFOAIR_PROV_MODE_AP_STA;
  if (wants_ap)
    comfoair_prov_on_ap_started(s, now);
  else
    comfoair_prov_on_ap_stopped(s, now);
}

// Every state the machine has, reached the way the device reaches it. The switch is over the
// enumeration and COMFOAIR_PROV_STATE_COUNT is the loop bound in the sweep below, so an eighth
// state cannot be added without this function refusing to build -- which is the mistake
// test/test_phase/phase_inputs.h made once already ("all seven states", sampling seven eighths).
static comfoair_prov_t in_state(comfoair_prov_state_t want) {
  switch (want) {
    case COMFOAIR_PROV_UNCONFIGURED: {
      comfoair_prov_t s = booted(false);
      comfoair_prov_on_ap_started(&s, 0);
      return s;
    }
    case COMFOAIR_PROV_CONNECTING:
      return booted(true);
    case COMFOAIR_PROV_CONNECTED: {
      comfoair_prov_t s = booted(true, true);
      comfoair_prov_on_connected(&s, 5 * SECOND);
      (void)comfoair_prov_take_action(&s);
      return s;
    }
    case COMFOAIR_PROV_TRIAL: {
      comfoair_prov_t s = booted(false);
      comfoair_prov_on_ap_started(&s, 0);
      comfoair_prov_on_credentials_saved(&s, 10 * SECOND);
      (void)comfoair_prov_take_action(&s);
      return s;
    }
    case COMFOAIR_PROV_RETRYING: {
      comfoair_prov_t s = booted(true, true);
      // 201 is WIFI_REASON_NO_AP_FOUND: the network is simply not on the air here.
      comfoair_prov_on_disconnected(&s, 5 * SECOND, 201);
      (void)comfoair_prov_take_action(&s);
      return s;
    }
    case COMFOAIR_PROV_FALLBACK_AP: {
      // The station of the previous boot that was never given an address; init() offers it the
      // reopen window with the station still trying underneath.
      comfoair_prov_t s = booted(true, true, false, true);
      (void)comfoair_prov_take_action(&s);
      comfoair_prov_on_ap_started(&s, 0);
      return s;
    }
    case COMFOAIR_PROV_WINDOW_CLOSED: {
      comfoair_prov_t s = booted(false);
      comfoair_prov_on_ap_started(&s, 0);
      comfoair_prov_tick(&s, 16 * MINUTE);
      return s;
    }
    case COMFOAIR_PROV_STATE_COUNT:
      break;  // not a state; the sweep never asks for it
  }
  comfoair_prov_t s{};
  comfoair_prov_init(&s, nullptr, nullptr);
  return s;
}

void setUp(void) {}
void tearDown(void) {}

// --- the one line of mode() this event needs -----------------------------------------------

void test_a_setup_request_opens_the_window_from_connected(void) {
  // The state the owner is actually in when their Wi-Fi password changes on the router and they
  // want to hand the device the new one before it notices. mode() answered MODE_STATION here
  // unconditionally, so the window could be open and the access point would never go on the air:
  // an event that opens a window nobody can see is not a way back.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s));

  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&s));
  // AP_STA and not ACCESS_POINT: the station stays up. An access point that dropped the owner's
  // network to offer a settings page would take the house's ventilation off Home Assistant to
  // answer a question nobody has asked yet.
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_CONNECTED, comfoair_prov_state(&s));
}

void test_the_window_this_opens_still_closes_by_itself(void) {
  // The event is a way back, not a switch that leaves an open access point on a connected device
  // for ever. The unclaimed rule counts the device as unclaimed for exactly as long as that access
  // point is up.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);
  radio_follows(&s, 1 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));

  comfoair_prov_tick(&s, 1 * MINUTE + 4 * MINUTE + 59 * SECOND);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));

  // Five minutes, not fifteen: reopen_window_ms is the length of every window that comes back,
  // and this one comes back to a device that already has an owner.
  comfoair_prov_tick(&s, 1 * MINUTE + 5 * MINUTE + 1 * SECOND);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s));
}

// --- the terminal state, which is the whole point of the gesture ----------------------------

void test_a_setup_request_revives_a_closed_window(void) {
  // WINDOW_CLOSED is terminal until the next boot, and on a device with nothing stored that
  // means a device with no network, no access point and nothing on the air. Before this event the
  // only way out was a power cut. mode() answered MODE_OFF, which is the radio being told to do
  // nothing at all.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_WINDOW_CLOSED);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_WINDOW_CLOSED, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_OFF, comfoair_prov_mode(&s));

  comfoair_prov_on_setup_requested(&s, 20 * MINUTE);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_UNCONFIGURED, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_ACCESS_POINT, comfoair_prov_mode(&s));

  // And the way back out is the ordinary one: the access point comes up, the window runs, tick()
  // closes it and the machine returns to WINDOW_CLOSED through the branch that was always there.
  // A revival that needed its own closing path would be a second way for a window to end.
  radio_follows(&s, 20 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  comfoair_prov_tick(&s, 26 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_WINDOW_CLOSED, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_OFF, comfoair_prov_mode(&s));
}

// --- it costs nothing to try ------------------------------------------------------------------

void test_a_setup_request_erases_nothing(void) {
  // The claim docs/device/button.md makes for the setup press. This machine holds no credentials,
  // no UI password and no broker settings -- comfoair_prov_t is forbidden to (the static_assert
  // below) -- so what is checked here is everything it DOES hold about them, plus the one-shot
  // the caller owes the world. A request that raised RESTORE_CREDENTIALS would put the previous
  // network back; one that raised COMMIT would promote an untried pair to known-good. Both are a
  // short press destroying something.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  TEST_ASSERT_TRUE(comfoair_prov_has_credentials(&s));
  TEST_ASSERT_TRUE(comfoair_prov_has_known_good(&s));

  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);

  TEST_ASSERT_TRUE(comfoair_prov_has_credentials(&s));
  TEST_ASSERT_TRUE(comfoair_prov_has_known_good(&s));
  TEST_ASSERT_FALSE(comfoair_prov_should_roll_back(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_NONE, comfoair_prov_take_action(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NONE, comfoair_prov_failure(&s));
}

void test_a_setup_request_does_not_displace_a_pending_write(void) {
  // The other half of "erases nothing", and it is not the same case: set_action() lets a nudge be
  // refused but nothing stops an event overwriting a durable write outright. A COMMIT dropped here
  // is a known-good slot that never gets updated, which disarms the rollback silently -- the
  // failure the header warns about at COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS.
  comfoair_prov_t s = booted(true);
  comfoair_prov_on_connected(&s, 5 * SECOND);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS, s.action);

  comfoair_prov_on_setup_requested(&s, 6 * SECOND);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS, comfoair_prov_take_action(&s));
}

void test_a_setup_request_from_retrying_is_not_a_trap(void) {
  // The trap this event was nearly built with: putting the machine into FALLBACK_AP to get an
  // access point. FALLBACK_AP is the fallback's state, and a machine parked there by a button press
  // would have the fallback clock restarted under it at every window close -- an access point on a
  // duty cycle for a device whose owner only wanted to type a password once. The event moves no
  // state at all except out of the terminal one, so RETRYING stays RETRYING and the backoff
  // underneath it is untouched.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_RETRYING);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_RETRYING, comfoair_prov_state(&s));

  comfoair_prov_on_setup_requested(&s, 6 * SECOND);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_RETRYING, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&s));

  // And a tick a moment later does not turn it into one either: the fallback's thirty minutes have
  // not passed, and the window the button opened must not be read as the failure that earns one.
  // The state at this point is CONNECTING, not RETRYING, and that is the machine working rather
  // than the trap: the one-second backoff armed by the disconnect has expired and step 4 of tick()
  // has issued the retry. What the trap would look like is FALLBACK_AP -- the fallback's state,
  // reached without the fallback clock -- and that is what is asserted.
  comfoair_prov_tick(&s, 7 * SECOND);
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&s));
}

// --- from ANY state, which is the claim that makes it a rescue --------------------------------

void test_a_setup_request_asks_for_an_access_point_from_every_state(void) {
  // The sweep, and the reason the cases above are not the whole suite: "opens the window from
  // wherever the machine is" is a claim about all seven states, and four of them are covered by
  // no case of its own anywhere. The bound is the enumeration's own count, so an eighth state
  // joins this sweep by existing.
  for (int i = 0; i < (int)COMFOAIR_PROV_STATE_COUNT; i++) {
    const comfoair_prov_state_t state = (comfoair_prov_state_t)i;
    comfoair_prov_t s = in_state(state);
    TEST_ASSERT_EQUAL_MESSAGE(state, comfoair_prov_state(&s), "the fixture reached another state");

    comfoair_prov_on_setup_requested(&s, 40 * MINUTE);

    const comfoair_prov_mode_t mode = comfoair_prov_mode(&s);
    // OFF is the radio doing nothing and STATION is an access point nobody can join: both are a
    // gesture that did not work. Which of the two AP modes is right is the state's business.
    TEST_ASSERT_TRUE_MESSAGE(mode == COMFOAIR_PROV_MODE_ACCESS_POINT ||
                                 mode == COMFOAIR_PROV_MODE_AP_STA,
                             "a state answered without an access point after a setup request");

    // And once the radio has caught up, the window is genuinely open -- not merely a mode.
    radio_follows(&s, 40 * MINUTE);
    TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s),
                             "the access point came up and the window was not open behind it");
  }
}

void test_a_setup_request_before_the_access_point_is_up_still_counts(void) {
  // The window's anchoring rule, applied to this event: the window belongs to the access point, so
  // the press that asks for one must not start the clock. Between the press and the radio there
  // are seconds -- on the `Forget Wi-Fi` path there can be ninety -- and a window anchored on the
  // press is a window the owner is charged for before it exists.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);

  radio_follows(&s, 1 * MINUTE + 90 * SECOND);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  TEST_ASSERT_EQUAL(5 * MINUTE, comfoair_prov_window_remaining_ms(&s, 1 * MINUTE + 90 * SECOND));
}

void test_a_second_press_reopens_a_window_the_ceiling_had_closed(void) {
  // DELIBERATE, and the one place this event overrides a bound. setup_window_ceiling_ms bounds
  // what CLIENT ACTIVITY can extend -- a phone on the access point must not be able to hold
  // it open for ever. A finger on the button is not a client: whoever presses it is standing at
  // the device, and could equally hold it for fifteen seconds and erase the whole thing. Refusing
  // them a fresh window after an hour would mean the answer to "the page timed out while I was
  // fetching the password" is a power cut.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_UNCONFIGURED);
  comfoair_prov_tick(&s, 61 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_WINDOW_CLOSED, comfoair_prov_state(&s));

  comfoair_prov_on_setup_requested(&s, 61 * MINUTE);
  radio_follows(&s, 61 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  TEST_ASSERT_EQUAL(5 * MINUTE, comfoair_prov_window_remaining_ms(&s, 61 * MINUTE));
}


void test_a_press_during_the_first_setup_of_a_new_device_keeps_the_long_window(void) {
  // THE COMMONEST PATH IN THE DEVICE'S LIFE, and the first version of this event got it wrong: it
  // set the short window unconditionally, so an owner unboxing a device and pressing the button
  // once -- to see whether it was awake, or because the page had not loaded -- had their first
  // fifteen-minute setup window cut to five, silently, while they were reading a router sticker.
  // The long window goes to a device that has nothing stored and has never had one close, and
  // that is not changed by how the window was asked for.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_UNCONFIGURED);
  TEST_ASSERT_EQUAL(15 * MINUTE, comfoair_prov_window_remaining_ms(&s, 0));

  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);
  TEST_ASSERT_EQUAL(15 * MINUTE, comfoair_prov_window_remaining_ms(&s, 1 * MINUTE));
  comfoair_prov_tick(&s, 1 * MINUTE + 14 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
}

void test_a_press_on_a_device_that_has_an_owner_gets_the_short_window(void) {
  // The other side of the same predicate, and the reason it is a predicate rather than a constant.
  // This access point is open and sits on a unit that already holds somebody's network, so it is
  // five minutes -- the same answer on_ap_started() gives, from the same call.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);
  radio_follows(&s, 1 * MINUTE);
  TEST_ASSERT_EQUAL(5 * MINUTE, comfoair_prov_window_remaining_ms(&s, 1 * MINUTE));
}

// --- who is holding the window open, and who is not -------------------------------------------

void test_a_client_seen_extends_the_window(void) {
  // THE CONTRACT THE WEBSOCKET SIDE STANDS ON. test_provision already has an extension case, and
  // this is not it: that one holds open the FIFTEEN-minute window of a device straight out of its
  // box, and the window this suite is about is the FIVE-minute one a press reopens on a device
  // that already has an owner. Same person in the next room reading a router sticker, a third of
  // the time to do it in -- which is why the caller side matters here and not there.
  //
  // The caller is comfoair_http_ws.c's push loop: it asks the server which sockets are WebSockets
  // every round, and a phone lying on the settings page holds one open while sending no HTTP
  // request at all. Until that loop reported them, this event had two HTTP callers and the
  // association, so a page being read at that very second counted as nobody. The loop itself cannot
  // be built on the host -- comfoair_http_ws.c needs esp_http_server, and [env:native] builds no
  // such thing -- so what is nailed down here is the MACHINE it calls into, and the loop is a
  // hardware scenario HV-PROV-15 (docs/hardware-verification). Two claims, and the second is the
  // one a plausible fix gets wrong: seeing a client re-anchors the window, and re-anchors it to the
  // length this device is already on rather than promoting it back to the long one.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);
  radio_follows(&s, 1 * MINUTE);
  TEST_ASSERT_EQUAL(5 * MINUTE, comfoair_prov_window_remaining_ms(&s, 1 * MINUTE));

  // Four rounds of the push loop, a minute apart, with not one HTTP request between them.
  for (uint32_t t = 2 * MINUTE; t <= 5 * MINUTE; t += 1 * MINUTE) {
    comfoair_prov_on_client_seen(&s, t);
    comfoair_prov_tick(&s, t);
  }

  // Untouched, this window ended at minute six and the settings page went off the air under a
  // reader. The access point is still up, which is the half the owner can see.
  comfoair_prov_tick(&s, 8 * MINUTE);
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s),
                           "the window closed under a client that was still there");
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&s));

  // Five minutes from the last client seen, not fifteen and not for ever: activity re-anchors the
  // clock, it does not change which of the window's two lengths this device is on, and it stops
  // mattering the moment the last socket goes.
  TEST_ASSERT_EQUAL(2 * MINUTE, comfoair_prov_window_remaining_ms(&s, 8 * MINUTE));
  comfoair_prov_tick(&s, 10 * MINUTE + 1 * SECOND);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_window_is_open(&s),
                            "the last client left and the window stayed open anyway");
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s));
}

void test_a_client_seen_does_not_bring_a_closed_window_back(void) {
  // The asymmetry between the two events, and the reason the push loop is allowed to call this one
  // every round without thinking. A FINGER on the button reopens a window from anywhere, including
  // past the ceiling (the case above says so): whoever pressed it is standing at the device. A
  // CLIENT is a radio in range, and the window rule bounds what it can do -- so the tab left open
  // on a phone in a coat pocket must not put the access point of a provisioned unit back on the
  // air, once or a thousand times. Unguarded, a loop that notes a client every round is exactly the
  // machine that would never let this window end.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_CONNECTED);
  comfoair_prov_on_setup_requested(&s, 1 * MINUTE);
  radio_follows(&s, 1 * MINUTE);
  comfoair_prov_tick(&s, 7 * MINUTE);
  radio_follows(&s, 7 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s));

  for (uint32_t t = 8 * MINUTE; t <= 30 * MINUTE; t += 1 * MINUTE) {
    comfoair_prov_on_client_seen(&s, t);
    comfoair_prov_tick(&s, t);
    TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_window_is_open(&s),
                              "a client knocked and a closed setup window came back");
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s),
                              "a client knocked and the access point came back with it");
  }
}

// Every field of comfoair_prov_t that this event is FORBIDDEN to touch, checked one by one. The
// list is the struct minus the five the event owns (state, and the four window fields), and it is
// spelled out rather than memcmp'd so that a failure names the field.
//
// If a field is added to comfoair_prov_t, add it here: the sizeof case below is what fires to say
// so, since a new field cannot pass 88 bytes unnoticed.
static void assert_only_the_window_moved(const comfoair_prov_t &before,
                                        const comfoair_prov_t &after) {
  TEST_ASSERT_EQUAL_MESSAGE(before.action, after.action, "action");
  TEST_ASSERT_EQUAL_MESSAGE(before.failure, after.failure, "failure");
  TEST_ASSERT_EQUAL_MESSAGE(before.failure_reason, after.failure_reason, "failure_reason");
  TEST_ASSERT_EQUAL_MESSAGE(before.has_credentials, after.has_credentials, "has_credentials");
  TEST_ASSERT_EQUAL_MESSAGE(before.has_known_good, after.has_known_good, "has_known_good");
  TEST_ASSERT_EQUAL_MESSAGE(before.credentials_committed, after.credentials_committed,
                            "credentials_committed");
  TEST_ASSERT_EQUAL_MESSAGE(before.ap_on_air, after.ap_on_air, "ap_on_air");
  TEST_ASSERT_EQUAL_MESSAGE(before.window_ever_closed, after.window_ever_closed,
                            "window_ever_closed");
  TEST_ASSERT_EQUAL_MESSAGE(before.trialling, after.trialling, "trialling");
  TEST_ASSERT_EQUAL_MESSAGE(before.associated, after.associated, "associated");
  TEST_ASSERT_EQUAL_MESSAGE(before.address_never_arrived, after.address_never_arrived,
                            "address_never_arrived");
  TEST_ASSERT_EQUAL_MESSAGE(before.failing, after.failing, "failing");
  TEST_ASSERT_EQUAL_MESSAGE(before.retry_armed, after.retry_armed, "retry_armed");
  TEST_ASSERT_EQUAL_MESSAGE(before.attempts, after.attempts, "attempts");
  TEST_ASSERT_EQUAL_MESSAGE(before.retry_delay_ms, after.retry_delay_ms, "retry_delay_ms");
  TEST_ASSERT_EQUAL_MESSAGE(before.trial_at, after.trial_at, "trial_at");
  TEST_ASSERT_EQUAL_MESSAGE(before.associated_at, after.associated_at, "associated_at");
  TEST_ASSERT_EQUAL_MESSAGE(before.failing_at, after.failing_at, "failing_at");
  TEST_ASSERT_EQUAL_MESSAGE(before.retry_at, after.retry_at, "retry_at");
  TEST_ASSERT_EQUAL_MEMORY_MESSAGE(&before.cfg, &after.cfg, sizeof before.cfg, "cfg");
}

void test_a_setup_request_touches_nothing_but_the_window(void) {
  // The case the first version of this suite did not have, and the reason it mattered: the fixture
  // was a machine with nothing in it, so seven different one-line mutations -- clearing the
  // failure the status page is showing, clearing `trialling` and disarming the rollback, zeroing
  // the retry ladder, forgetting that a window had ever closed -- all passed a suite that claimed
  // "erases nothing" in its name.
  //
  // The machine here has every one of those facts SET. A trial is in flight (so the ninety-second
  // rollback is armed), a verdict is standing, the backoff has a rung on it, and the previous boot
  // recorded a station that was never given an address.
  comfoair_prov_t s = booted(true, true, true, true);
  comfoair_prov_on_ap_started(&s, 1 * SECOND);
  comfoair_prov_on_associated(&s, 2 * SECOND);
  comfoair_prov_on_credentials_saved(&s, 3 * SECOND);
  // 15 is WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT: the one a mistyped password produces, and the verdict
  // an owner is looking at on the status page while they reach for the button.
  comfoair_prov_on_disconnected(&s, 4 * SECOND, 15);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 5 * SECOND);
  // The two facts the events above cannot leave set, stamped by hand. They are not reachable
  // together -- on_credentials_saved() clears address_never_arrived, and rightly, because the owner
  // has just named a different network; credentials_committed only goes true on an address, which
  // ends the trial that arms the rollback. The claim under test is per-field, "this event does not
  // touch it", so the fixture is the union rather than any one reachable machine. Without these two
  // lines the mutation that clears either of them passes a suite whose fixture had them false
  // already -- which is exactly how the first version of this case let one through.
  s.address_never_arrived  = true;
  s.credentials_committed  = true;

  TEST_ASSERT_TRUE(s.trialling);
  TEST_ASSERT_TRUE(s.failing);
  TEST_ASSERT_TRUE(s.retry_armed);
  TEST_ASSERT_TRUE(s.associated);
  TEST_ASSERT_TRUE(s.window_ever_closed);
  TEST_ASSERT_GREATER_THAN_UINT32(0u, s.attempts);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, s.failure);

  const comfoair_prov_t before = s;
  comfoair_prov_on_setup_requested(&s, 6 * SECOND);

  assert_only_the_window_moved(before, s);
  // And the state is untouched too: this machine is not in WINDOW_CLOSED, so the event has no
  // business moving it. (The one state it does move has its own case above.)
  TEST_ASSERT_EQUAL(before.state, s.state);
  // The window did open, or the case would pass on a function that returned immediately.
  TEST_ASSERT_TRUE(s.window_running);
  TEST_ASSERT_EQUAL(6 * SECOND, s.window_extended_at);
}

void test_a_second_press_inside_an_open_window_still_extends_it(void) {
  // The mutation `if (s->window_running) return;` at the top -- an entirely reasonable-looking
  // guard against "reopening what is already open" -- leaves the owner who presses the button at
  // minute four of a five-minute window with sixty seconds. The press is the strongest evidence a
  // human is standing there, and it re-anchors the window whether or not one was running.
  comfoair_prov_t s = in_state(COMFOAIR_PROV_UNCONFIGURED);
  comfoair_prov_tick(&s, 14 * MINUTE);
  TEST_ASSERT_EQUAL(1 * MINUTE, comfoair_prov_window_remaining_ms(&s, 14 * MINUTE));

  comfoair_prov_on_setup_requested(&s, 14 * MINUTE);
  TEST_ASSERT_EQUAL(15 * MINUTE, comfoair_prov_window_remaining_ms(&s, 14 * MINUTE));
}

// --- the tripwire ------------------------------------------------------------------------------

void test_the_structure_did_not_grow(void) {
  // The event reuses four fields that already existed (window_running, window_opened_at,
  // window_extended_at, window_is_the_short_one) and adds none. That is not thrift: the
  // static_assert in the header is a TRIPWIRE AGAINST A STRING -- an ssid[32] added to this struct
  // costs 120 bytes (measured by compiling it) and would fail the build rather than the review.
  // Every field that goes in for a good reason spends the slack that tripwire runs on.
  //
  // The static_assert fires at compile time in every translation unit that includes the header;
  // this case is the same statement in a place a reader looking at the tests will find, which is
  // what test_the_machine_holds_no_credentials_only_the_fact_of_them does next door.
  static_assert(sizeof(comfoair_prov_t) <= 96, "comfoair_prov_t grew past its tripwire");
  TEST_ASSERT_LESS_OR_EQUAL(96u, (uint32_t)sizeof(comfoair_prov_t));
  // 88 as measured on 2026-09-06, and this event left it there. Asserting the EXACT number is
  // what makes "no new fields" a test rather than a sentence: eight bytes of slack is room for two
  // fields to be added in silence and still pass the tripwire above.
  TEST_ASSERT_EQUAL_UINT32(88u, (uint32_t)sizeof(comfoair_prov_t));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_a_setup_request_opens_the_window_from_connected);
  RUN_TEST(test_the_window_this_opens_still_closes_by_itself);
  RUN_TEST(test_a_setup_request_revives_a_closed_window);
  RUN_TEST(test_a_setup_request_erases_nothing);
  RUN_TEST(test_a_setup_request_does_not_displace_a_pending_write);
  RUN_TEST(test_a_setup_request_from_retrying_is_not_a_trap);
  RUN_TEST(test_a_setup_request_asks_for_an_access_point_from_every_state);
  RUN_TEST(test_a_setup_request_before_the_access_point_is_up_still_counts);
  RUN_TEST(test_a_second_press_reopens_a_window_the_ceiling_had_closed);
  RUN_TEST(test_a_press_during_the_first_setup_of_a_new_device_keeps_the_long_window);
  RUN_TEST(test_a_press_on_a_device_that_has_an_owner_gets_the_short_window);
  RUN_TEST(test_a_client_seen_extends_the_window);
  RUN_TEST(test_a_client_seen_does_not_bring_a_closed_window_back);
  RUN_TEST(test_a_setup_request_touches_nothing_but_the_window);
  RUN_TEST(test_a_second_press_inside_an_open_window_still_extends_it);
  RUN_TEST(test_the_structure_did_not_grow);
  return UNITY_END();
}
