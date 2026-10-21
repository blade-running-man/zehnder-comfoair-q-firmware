// What the device should be doing, given what has happened to it.
//
// Every case below is a failure scenario, not a happy path, because the happy path is the one that
// gets tested by hand anyway. The list is the failure model's (docs/architecture/failure-model.md):
// a typo in a password, a router that went away, a power cut in the middle of a write, two people
// configuring at once.
//
// Time is a parameter here, and that is the only reason these cases can be written at all.
// "The window closed fifteen minutes after the access point came on the air, while the owner
// was still fetching the router's password off a sticker" is not a thing you can assert
// against a real clock -- and it is exactly what the obvious implementation gets wrong: a window
// anchored at boot, while the access point on the `Forget Wi-Fi` path only appears ~90 s
// later, really lasts setup_window minus a minute and a half. Here that is not accepted.
#include <unity.h>

#include "comfoair_provision.h"

static const uint32_t SECOND = 1000u;
static const uint32_t MINUTE = 60u * 1000u;

// The four booleans the caller reads out of NVS before any of this runs. Spelling them at
// each call site would hide which one a test is about.
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

// The caller's loop: raise what mode() asks for, then say it is up. The two are separate on
// purpose -- see the access-point anchoring test.
static void access_point_comes_up(comfoair_prov_t *s, uint32_t now) {
  comfoair_prov_on_ap_started(s, now);
}

void setUp(void) {}
void tearDown(void) {}

// --- an open access point means unclaimed, whatever NVS holds -------------------------------

void test_an_open_access_point_means_unclaimed_whatever_nvs_holds(void) {
  // The single most important rule in this file, and the one the obvious implementation breaks:
  // `provisioned` goes true the instant credentials are stored, while the open access point is
  // still on the air (the condition is "no open access point", not "credentials stored"). In
  // that gap POST /api/config -- the one write an unclaimed device accepts -- sets the FIRST UI
  // password. Two requests from anyone in radio range and the device belongs to a passer-by.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  TEST_ASSERT_FALSE(comfoair_prov_is_provisioned(&s));

  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_is_provisioned(&s),
                            "storing credentials claimed the device while the AP was still up");

  // Only the access point actually leaving the air settles it.
  comfoair_prov_on_connected(&s, 2 * MINUTE);
  comfoair_prov_on_ap_stopped(&s, 2 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_is_provisioned(&s));
}

void test_the_fallback_access_point_unclaims_a_device_that_has_an_owner(void) {
  // The rule has no exception for "but this one has been ours for a year". It cannot: the fallback
  // access point is open too, so the same passer-by is listening. What stops them
  // re-homing it is not this flag but the policy behind it -- comfoair_http_policy.c refuses
  // POST /api/provision to an unauthenticated client once a UI password exists. This flag is
  // the input that makes that branch reachable at all.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  TEST_ASSERT_TRUE(comfoair_prov_is_provisioned(&s));

  comfoair_prov_on_disconnected(&s, 1 * MINUTE, 201 /* NO_AP_FOUND */);
  comfoair_prov_tick(&s, 31 * MINUTE);
  access_point_comes_up(&s, 31 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_is_provisioned(&s));
}

// --- the setup window (docs/device/networking.md) ---------------------------------------------

void test_the_window_is_anchored_on_the_access_point_not_on_boot(void) {
  // Ninety seconds of boot, or thirty minutes of a station retrying a network that is not
  // there, must not come out of the owner's fifteen minutes. Nothing in this machine is
  // anchored on init for exactly this reason.
  comfoair_prov_t s = booted(false);
  comfoair_prov_tick(&s, 10 * MINUTE);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_window_is_open(&s),
                            "a window was running before the access point existed");

  access_point_comes_up(&s, 10 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));

  comfoair_prov_tick(&s, 24 * MINUTE);  // 14 minutes of access point
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s),
                           "the window was measured from boot, not from the access point");

  comfoair_prov_tick(&s, 25 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
}

void test_client_activity_extends_the_window(void) {
  // The most annoying failure in the whole design is not an attack: it is the window closing
  // while the owner is in another room reading the password off the back of the router.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);

  comfoair_prov_on_client_seen(&s, 14 * MINUTE);
  comfoair_prov_tick(&s, 20 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));

  // Fourteen plus fifteen, so 29 minutes is the first tick that finds it expired.
  comfoair_prov_tick(&s, 28 * MINUTE);
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s), "activity did not re-arm the window");
  comfoair_prov_tick(&s, 29 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
}

void test_a_knock_after_the_window_ran_out_does_not_revive_it(void) {
  // on_client_seen() has to ask the live question, not the last tick's answer. A tick period is
  // about a second on the device, and in that second the cheaper test -- "is window_running
  // still set" -- handed a fresh fifteen minutes to a client that knocked after the window had
  // already run out, again and again up to the ceiling. Its own comment says a closed window
  // does not reopen because somebody knocked; this is what makes that true.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_client_seen(&s, 15 * MINUTE + 1);  // expired, and no tick has run yet
  comfoair_prov_tick(&s, 15 * MINUTE + 2);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_window_is_open(&s),
                            "a knock inside the tick period reopened a window that had run out");
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_WINDOW_CLOSED, comfoair_prov_state(&s));
}

void test_the_ceiling_bounds_what_activity_can_extend(void) {
  // The price, named out loud: whoever holds the connection holds the window open. Sixty
  // minutes from the access point coming up is where that stops, and it is measured from the
  // access point, not from the last activity, or it would not bound anything.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  for (uint32_t t = 5 * MINUTE; t <= 90 * MINUTE; t += 5 * MINUTE) {
    comfoair_prov_on_client_seen(&s, t);
    comfoair_prov_tick(&s, t);
    if (t >= 60 * MINUTE)
      TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_window_is_open(&s),
                                "a client held the window past the ceiling");
    else
      TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  }
}

void test_a_window_that_never_closes_is_a_supported_configuration(void) {
  // The never-closing window: an ESP32 behind the front panel of a running ventilation unit has no
  // button and no USB, and for it a lockout is a lost device, not security. So a window that
  // never closes is supported rather than a loophole, and zero is that setting here.
  comfoair_prov_config_t cfg{};
  comfoair_prov_config_defaults(&cfg);
  cfg.setup_window_ms = 0;

  comfoair_prov_boot_t boot{};
  comfoair_prov_t s{};
  comfoair_prov_init(&s, &cfg, &boot);
  access_point_comes_up(&s, 0);

  comfoair_prov_tick(&s, 24u * 60u * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PROV_NEVER,
                           comfoair_prov_window_remaining_ms(&s, 24u * 60u * MINUTE));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_ACCESS_POINT, comfoair_prov_mode(&s));
}

void test_a_window_that_never_closes_outlives_a_window_that_did(void) {
  // The never-closing device is the one that has been sitting in the plant room since before
  // anybody configured it -- so its first fifteen minutes ran out long ago and
  // `window_closed_before` is persisted true. That is the ONLY kind of device the setting is about,
  // and reading the short reopen window for it puts the sealed unit permanently off the air: no
  // button, no USB, front panel of a running ventilation unit. The zero has to outrank the
  // short/long distinction rather than be a case of it.
  comfoair_prov_config_t cfg{};
  comfoair_prov_config_defaults(&cfg);
  cfg.setup_window_ms = 0;

  comfoair_prov_boot_t boot{};
  boot.window_closed_before = true;

  comfoair_prov_t s{};
  comfoair_prov_init(&s, &cfg, &boot);
  access_point_comes_up(&s, 0);

  comfoair_prov_tick(&s, 24u * 60u * MINUTE);
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s),
                           "the never-closing setting was dropped when a window ran out once");
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PROV_NEVER,
                           comfoair_prov_window_remaining_ms(&s, 24u * 60u * MINUTE));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_ACCESS_POINT, comfoair_prov_mode(&s));
}

void test_a_closed_window_on_an_unconfigured_device_takes_the_radio_off_the_air(void) {
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_tick(&s, 15 * MINUTE);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_WINDOW_CLOSED, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_OFF, comfoair_prov_mode(&s));
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_has_closed(&s),
                           "the caller must persist this or the next boot re-opens the long one");
}

void test_a_reboot_after_a_closed_window_still_offers_a_way_back(void) {
  // The invariant that outranks every other rule (docs/architecture/failure-model.md): there is
  // never a state with no network, no access point and no way back. A closed window leaves a
  // device with no network AND no access point, so the way back has to be somewhere -- it is
  // here, and five minutes rather than fifteen so an abandoned device is not an open access
  // point for a quarter of an hour after every power cut.
  comfoair_prov_t s = booted(false, false, /*window_closed_before=*/true);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_ACCESS_POINT, comfoair_prov_mode(&s));

  access_point_comes_up(&s, 0);
  comfoair_prov_tick(&s, 4 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  comfoair_prov_tick(&s, 5 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
}

void test_a_closed_window_does_not_reopen_by_itself(void) {
  // After the window closes the access point comes back at a reboot and NOT on its own, and
  // the fallback's thirty-minute return is for a device that has a network to fail
  // at. A device with nothing stored has neither, so nothing in the machine may raise its
  // access point again -- otherwise the fifteen-minute bound is a fifteen-minute pause and
  // the whole bound buys nothing.
  //
  // The stray disconnect is here on purpose, but NOT because it starts the fallback clock -- it
  // must not, and the assertion after it is what says so. comfoair_prov_on_disconnected() returns
  // at its first branch on a device with nothing stored, and THAT early return is the guard this
  // pins: delete it and the disconnect starts a thirty-minute clock on a device the closed window
  // has taken off the air for good, and the loop below finds RETRYING instead of a closed window.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE, 201);
  comfoair_prov_tick(&s, 15 * MINUTE);
  comfoair_prov_on_ap_stopped(&s, 15 * MINUTE);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(COMFOAIR_PROV_NEVER,
                                   comfoair_prov_fallback_in_ms(&s, 15 * MINUTE),
                                   "a disconnect on a device with nothing stored scheduled an "
                                   "access point the window had closed for good");

  for (uint32_t t = 20 * MINUTE; t <= 6u * 60u * MINUTE; t += 5 * MINUTE)
    comfoair_prov_tick(&s, t);

  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_WINDOW_CLOSED, comfoair_prov_state(&s),
                            "the setup window reopened itself");
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_OFF, comfoair_prov_mode(&s));
}

void test_connecting_takes_the_access_point_off_the_air(void) {
  // The third way a window ends, and the only good one. It matters for the unclaimed rule: until
  // the access point is gone the device is unclaimed, so a successful connection has to end the
  // window rather than leave it running out on its own.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  comfoair_prov_on_connected(&s, 2 * MINUTE);

  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s));
}

void test_the_clock_wrapping_does_not_close_the_window_early(void) {
  // A millisecond counter is 32 bits and wraps after 49.7 days; a ventilation unit runs for
  // years. Every interval here is `(uint32_t)(now - then) >= limit`, which is correct across
  // exactly one wrap. DO NOT rewrite any of them as `now >= deadline`: that form closes the
  // window instantly the first time the clock passes 0xFFFFFFFF, and the device would have
  // been up for seven weeks by the time anyone saw it.
  comfoair_prov_t s = booted(false);
  const uint32_t late = 0xFFFFF000u;
  access_point_comes_up(&s, late);

  comfoair_prov_tick(&s, late + 14 * MINUTE);  // wraps
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s), "the window closed on a clock wrap");
  comfoair_prov_tick(&s, late + 15 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
}

// --- saving credentials is not connecting ----------------------------------------------------

void test_saving_credentials_is_not_connecting(void) {
  // Its own state, because conflating the two is the obvious bug: store the pair, report
  // credentials present, and everything keyed on that behaves as though the device were on the
  // owner's network while it is still sitting on an open access point with a mistyped password.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_TRIAL, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&s),
                            "the access point must stay up or the owner never learns why");
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s));
}

void test_saving_credentials_extends_the_window(void) {
  // Submitting the form is the strongest possible evidence that a human is present. If the
  // window ran out mid-trial the owner would lose the page that tells them what went wrong.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 14 * MINUTE);
  comfoair_prov_tick(&s, 20 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
}

// --- credential rollback ---------------------------------------------------------------------

void test_a_typo_from_the_lan_puts_the_old_network_back(void) {
  // The likeliest catastrophe in the whole project (docs/architecture/failure-model.md):
  // not an attack, a typo. The owner changes Wi-Fi from the settings page, mistypes one
  // character, the old pair is already overwritten, and the device cannot raise an access
  // point because credentials exist -- it simply disappears, and recovery is physical.
  comfoair_prov_t s = booted(true, /*has_known_good=*/true);
  comfoair_prov_on_connected(&s, 0);
  (void)comfoair_prov_take_action(&s);

  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE + 8 * SECOND, 15 /* 4WAY_HANDSHAKE_TIMEOUT */);

  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_TRIAL, comfoair_prov_state(&s),
                            "one bad handshake is not proof; a crowded band produces those too");
  TEST_ASSERT_FALSE(comfoair_prov_should_roll_back(&s));

  comfoair_prov_tick(&s, 1 * MINUTE + 90 * SECOND);
  TEST_ASSERT_TRUE(comfoair_prov_should_roll_back(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS, comfoair_prov_take_action(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_CONNECTING, comfoair_prov_state(&s));

  // And what the owner is told afterwards is the whole point of classifying the reason.
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, comfoair_prov_failure(&s));
  TEST_ASSERT_EQUAL_UINT8(15, comfoair_prov_failure_reason(&s));
}

void test_a_rollback_does_not_happen_twice(void) {
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_tick(&s, 3 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS, comfoair_prov_take_action(&s));

  comfoair_prov_tick(&s, 10 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_should_roll_back(&s));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS, comfoair_prov_take_action(&s));
}

void test_a_first_provisioning_with_no_known_good_pair_keeps_what_the_owner_typed(void) {
  // There is nothing to roll back to on a device that has never been on a network, and
  // erasing the SSID they picked out of the scan list would only make them do it all again.
  // The way back here is the access point that is still on the air, and after that the fallback's.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE + 5 * SECOND, 202 /* AUTH_FAIL */);
  comfoair_prov_tick(&s, 5 * MINUTE);

  TEST_ASSERT_FALSE(comfoair_prov_should_roll_back(&s));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS, comfoair_prov_take_action(&s));
  TEST_ASSERT_TRUE(comfoair_prov_has_credentials(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, comfoair_prov_failure(&s));
}

void test_a_confirmed_connection_is_what_makes_a_pair_known_good(void) {
  // Nothing else may. A pair that has been stored, retried and given up on is not evidence of
  // anything; only an address on the owner's network is.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  (void)comfoair_prov_take_action(&s);
  TEST_ASSERT_FALSE(comfoair_prov_has_known_good(&s));

  comfoair_prov_on_connected(&s, 2 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_COMMIT_CREDENTIALS, comfoair_prov_take_action(&s));
  TEST_ASSERT_TRUE(comfoair_prov_has_known_good(&s));

  // Once, not on every reconnection: the write costs a flash cycle and the pair has not changed.
  comfoair_prov_on_disconnected(&s, 3 * MINUTE, 200 /* BEACON_TIMEOUT */);
  comfoair_prov_on_connected(&s, 4 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_NONE, comfoair_prov_take_action(&s));
}

// --- disconnect reasons are classified, not discarded ----------------------------------------
//
// The numbers are wifi_err_reason_t from ESP-IDF 5.5.5,
// components/esp_wifi/include/esp_wifi_types_generic.h:113-178, which is what the pinned
// platform ships. They arrive as wifi_event_sta_disconnected_t.reason, a uint8_t (:1167) --
// which is why this takes a uint8_t and why no code above 255 needs handling.

void test_a_network_that_is_not_there_is_told_apart_from_one_that_refuses_us(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NETWORK_ABSENT, comfoair_prov_classify(201));  // NO_AP_FOUND
  // 200 BEACON_TIMEOUT, then NO_AP_FOUND_W_COMPATIBLE_SECURITY, _IN_AUTHMODE_THRESHOLD and
  // _IN_RSSI_THRESHOLD -- all four are "we looked and there was nothing here we can use".
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NETWORK_ABSENT, comfoair_prov_classify(200));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NETWORK_ABSENT, comfoair_prov_classify(210));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NETWORK_ABSENT, comfoair_prov_classify(211));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NETWORK_ABSENT, comfoair_prov_classify(212));
}

void test_a_mistyped_password_is_told_apart_from_a_router_that_says_no(void) {
  // The four-way handshake is where a wrong PSK actually fails: the MIC on message 2 does not
  // verify and the access point simply stops answering, so the station reports a timeout
  // rather than anything that says "password".
  // 15 is 4WAY_HANDSHAKE_TIMEOUT and 204 is esp_wifi's own HANDSHAKE_TIMEOUT for the same event.
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, comfoair_prov_classify(15));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, comfoair_prov_classify(204));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, comfoair_prov_classify(14));   // MIC_FAILURE
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_WRONG_PASSWORD, comfoair_prov_classify(202));  // AUTH_FAIL

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_REFUSED, comfoair_prov_classify(5));    // ASSOC_TOOMANY
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_REFUSED, comfoair_prov_classify(203));  // ASSOC_FAIL
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_REFUSED, comfoair_prov_classify(23));   // 802_1X_AUTH_FAILED
}

void test_a_disconnect_this_machine_asked_for_is_not_a_verdict_on_anything(void) {
  // Reason 8 is WIFI_REASON_ASSOC_LEAVE (:124) -- what esp_wifi_disconnect() produces, and the
  // header's contract for COMFOAIR_PROV_ACTION_CONNECT orders the caller to call it before
  // reconnecting an associated station. So this number is the machine's OWN recovery nudge
  // coming back at it, and there is no fifth answer to give the owner about it: it is not a
  // refusal, and it must not be one of the three that count toward the fallback clock.
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_FAIL_NONE, comfoair_prov_classify(8),
                            "our own disconnect was read as a router refusing us");
}

void test_an_unclassified_reason_reads_as_refused_so_a_way_back_still_exists(void) {
  // Sixty-odd codes exist and this maps a dozen. The default has to be one of the three that count
  // toward the fallback clock, or a device failing for an unlisted reason would never raise its
  // access point again -- which is the state the invariants forbid outright. "The router will not
  // have us" is also the honest thing to tell the owner when we have nothing better.
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_REFUSED, comfoair_prov_classify(1));   // UNSPECIFIED
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_REFUSED, comfoair_prov_classify(39));  // TIMEOUT
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_REFUSED, comfoair_prov_classify(99));  // not a code at all
}

void test_associated_with_no_address_is_its_own_answer(void) {
  // The fourth answer, and the only one that is not a disconnect reason: the station stays
  // associated and no address ever arrives. Telling this apart matters because it is the one
  // failure where the network is demonstrably present, which is why the fallback refuses to raise
  // an access point for it.
  comfoair_prov_t s = booted(true);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 1 * MINUTE);
  comfoair_prov_tick(&s, 1 * MINUTE + 29 * SECOND);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NONE, comfoair_prov_failure(&s));

  comfoair_prov_tick(&s, 1 * MINUTE + 30 * SECOND);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s),
                            "a station stuck without an address has to be made to try again");
}

void test_our_own_recovery_nudge_does_not_become_the_owners_answer(void) {
  // The whole chain, because each link is separately wrong. tick() reaches NO_ADDRESS and issues
  // CONNECT; the caller does what the header told it to and calls esp_wifi_disconnect(); the event
  // comes back with reason 8. Reading that as a refusal told the owner "the router will not have
  // us" about a DHCP fault -- collapsing the four answers onto the wrong one of the four -- and
  // started the thirty-minute fallback clock for the one failure it excludes by name, so an OPEN
  // access point went on the air half an hour into a dead DHCP server.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 1 * MINUTE);
  comfoair_prov_tick(&s, 1 * MINUTE + 30 * SECOND);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s));

  comfoair_prov_on_disconnected(&s, 1 * MINUTE + 30 * SECOND, 8 /* ASSOC_LEAVE, ours */);

  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s),
                            "the owner is told 'refused' for a DHCP server that never answered");
  TEST_ASSERT_EQUAL_UINT8_MESSAGE(0, comfoair_prov_failure_reason(&s),
                                  "reason 8 is ours; publishing it as the router's answer is a lie");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      COMFOAIR_PROV_NEVER, comfoair_prov_fallback_in_ms(&s, 2 * MINUTE),
      "the fallback clock was started by our own disconnect, for a no-address failure");

  // It is still a disconnect: a station that stops trying is the stranding the invariants forbid.
  comfoair_prov_tick(&s, 2 * MINUTE + 30 * SECOND);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s),
                            "nothing re-armed after a disconnect that carried no verdict");
}

void test_an_address_arriving_in_time_is_not_a_failure(void) {
  comfoair_prov_t s = booted(true);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 1 * MINUTE);
  comfoair_prov_on_connected(&s, 1 * MINUTE + 2 * SECOND);
  comfoair_prov_tick(&s, 10 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NONE, comfoair_prov_failure(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_CONNECTED, comfoair_prov_state(&s));
}

void test_each_failure_has_a_name_the_api_can_publish(void) {
  // One spelling, decided here. Four different answers to the owner is an invariant; two
  // different spellings of them in the REST projection and the MQTT one is how that invariant
  // gets lost.
  TEST_ASSERT_EQUAL_STRING("network-not-found",
                           comfoair_prov_failure_name(COMFOAIR_PROV_FAIL_NETWORK_ABSENT));
  TEST_ASSERT_EQUAL_STRING("wrong-password",
                           comfoair_prov_failure_name(COMFOAIR_PROV_FAIL_WRONG_PASSWORD));
  TEST_ASSERT_EQUAL_STRING("refused", comfoair_prov_failure_name(COMFOAIR_PROV_FAIL_REFUSED));
  TEST_ASSERT_EQUAL_STRING("no-address", comfoair_prov_failure_name(COMFOAIR_PROV_FAIL_NO_ADDRESS));
  TEST_ASSERT_EQUAL_STRING("none", comfoair_prov_failure_name(COMFOAIR_PROV_FAIL_NONE));
}

// --- when the access point comes back by itself (docs/device/networking.md) --------------------

void test_the_access_point_returns_after_thirty_minutes_of_a_network_that_is_not_there(void) {
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE, 201 /* NO_AP_FOUND */);

  // Not FALLBACK_AP rather than any one state: which of CONNECTING and RETRYING it is at this
  // instant depends on where in the backoff the tick landed, and that is beside the point.
  comfoair_prov_tick(&s, 30 * MINUTE);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s),
                                "too early: this is every night's router reboot");
  TEST_ASSERT_EQUAL_UINT32(1 * MINUTE, comfoair_prov_fallback_in_ms(&s, 30 * MINUTE));

  comfoair_prov_tick(&s, 31 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&s),
                            "an access point that stops trying the network is a trap");
}

void test_no_address_does_not_bring_the_access_point_back(void) {
  // The fallback draws the line here: "the network is not there" and "the network is there and the
  // DHCP server is not" are different, and only the first one earns an open access point.
  //
  // KNOWN GAP, and it is a real one rather than an oversight -- see the header. A station that
  // stays associated with no address is unreachable, has no access point, and by this rule
  // never gets one. The way back is physical. It is left as decided because that is the
  // owner's call, and it is reported through comfoair_prov_fallback_in_ms() answering NEVER so that
  // nothing about it is silent.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  comfoair_prov_on_associated(&s, 1 * MINUTE);
  for (uint32_t t = 2 * MINUTE; t <= 120 * MINUTE; t += 1 * MINUTE) {
    comfoair_prov_tick(&s, t);
    if (comfoair_prov_take_action(&s) == COMFOAIR_PROV_ACTION_CONNECT)
      comfoair_prov_on_associated(&s, t);  // associates again, and again gets no address
  }
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PROV_NEVER, comfoair_prov_fallback_in_ms(&s, 120 * MINUTE));
}

void test_a_station_that_is_never_given_an_address_gets_a_window_at_the_next_boot(void) {
  // The invariant that outranks every other rule (failure-model.md): there is never a state with no
  // network, no access point and no way back. A station that associates and is never given an
  // address -- a dead DHCP server, an exhausted lease pool -- had all three. It is unreachable,
  // the fallback excludes it from the thirty-minute return BY NAME ("not on no-IP"), and a reboot
  // reproduced it exactly, so recovery was opening the ventilation unit's front panel.
  //
  // The way back is the setup window's, which the fallback does not govern: the fallback decides
  // when the access point comes back BY ITSELF, the window rule that after a window closes it
  // comes back at each REBOOT. Different mechanisms, so this costs the fallback nothing.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 1 * MINUTE);
  for (uint32_t t = 2 * MINUTE; t <= 120 * MINUTE; t += 1 * MINUTE) {
    comfoair_prov_tick(&s, t);
    if (comfoair_prov_take_action(&s) == COMFOAIR_PROV_ACTION_CONNECT)
      comfoair_prov_on_associated(&s, t);  // associates again, and again gets no address
  }
  // The fallback rule holds: nothing came back by itself, and the countdown says so.
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_PROV_NEVER, comfoair_prov_fallback_in_ms(&s, 120 * MINUTE));
  // And the invariant is honoured: the machine has recorded that this device owes itself a way
  // back, in the one place that survives the reboot -- the caller's NVS.
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_address_never_arrived(&s),
                           "two hours of a station with no address left nothing for the next boot");

  // The reboot. Power to a ventilation unit is the one physical act still available to the owner
  // of a device sealed behind its front panel.
  comfoair_prov_t next = booted(true, true, false, /*address_never_arrived=*/true);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_MODE_AP_STA, comfoair_prov_mode(&next),
                            "the reboot reproduced the stranding exactly");
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&next),
                            "an access point that stops trying the network is a trap, not a rescue");

  access_point_comes_up(&next, 0);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&next));
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_is_provisioned(&next),
                            "an open access point means unclaimed, whatever NVS holds");

  // It is a window and not a permanently open access point: five minutes, and then the station
  // carries on alone until the next boot. The fallback still does not get to bring this one back.
  comfoair_prov_tick(&next, 5 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&next));
  comfoair_prov_on_ap_stopped(&next, 5 * MINUTE);
  comfoair_prov_tick(&next, 60 * MINUTE);
  TEST_ASSERT_NOT_EQUAL_MESSAGE(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&next),
                                "closing the window started the fallback clock for no-address");
}

void test_an_address_ends_the_no_address_escape_so_it_is_not_offered_for_ever(void) {
  // The fact the caller persists has to be cleared by the thing that disproves it, or a device
  // that was once starved of an address raises an open access point at every boot for the rest
  // of its life -- and the open access point was priced as one of those, not an unbounded series.
  comfoair_prov_t s = booted(true, true, false, /*address_never_arrived=*/true);
  comfoair_prov_on_connected(&s, 1 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_address_never_arrived(&s));
}

void test_a_dhcp_pause_after_a_router_returns_is_not_a_starved_station(void) {
  // The false positive this has to avoid. The router is off for twenty-nine minutes (network
  // absent, fallback clock running); it comes back, the station associates, and DHCP takes a
  // moment. Carrying the older timestamp across the change of answer would declare the device
  // starved seconds later and put an open access point on the air at the next boot of a perfectly
  // healthy unit. A run of failures is counted on one side of the absent/no-address line at a time.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE, 201 /* NO_AP_FOUND */);
  comfoair_prov_tick(&s, 29 * MINUTE);
  comfoair_prov_on_associated(&s, 29 * MINUTE + 30 * SECOND);

  comfoair_prov_tick(&s, 30 * MINUTE + 5 * SECOND);  // the address timeout expires
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(
      COMFOAIR_PROV_NEVER, comfoair_prov_fallback_in_ms(&s, 30 * MINUTE + 5 * SECOND),
      "the answer became no-address and the fallback clock kept counting toward an access point");

  // Two minutes of no DHCP, not thirty-one minutes of it. The escape is not owed yet.
  comfoair_prov_tick(&s, 32 * MINUTE);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_address_never_arrived(&s),
                            "thirty minutes of a missing router was counted as thirty of no DHCP");
}

void test_one_success_resets_the_sustained_failure_clock(void) {
  // "Sustained" is the whole of the fallback rule. A device that gets on the network for a second
  // every ten minutes is annoying; it is not a device that has lost its home, and raising an open
  // access point at it would be worse than the flapping.
  //
  // What this pins is one line -- `s->failing = false` in comfoair_prov_on_connected(). Asserting
  // the state after the loop pins nothing at all, because on_connected() sets CONNECTED
  // unconditionally: the assertion would hold with the clock still running underneath it. So the
  // countdown is read on BOTH sides of every success. Delete that line and the ninth minute of
  // the fourth outage is thirty minutes after the first one, and this fails there.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  for (uint32_t t = 10 * MINUTE; t <= 120 * MINUTE; t += 10 * MINUTE) {
    comfoair_prov_on_disconnected(&s, t, 201);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(21 * MINUTE, comfoair_prov_fallback_in_ms(&s, t + 9 * MINUTE),
                                     "the clock did not restart at this outage");
    comfoair_prov_tick(&s, t + 9 * MINUTE);
    TEST_ASSERT_NOT_EQUAL_MESSAGE(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s),
                                  "nine minutes of outage raised an open access point");

    comfoair_prov_on_connected(&s, t + 9 * MINUTE);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(
        COMFOAIR_PROV_NEVER, comfoair_prov_fallback_in_ms(&s, t + 9 * MINUTE),
        "an address arrived, the fallback clock kept running: this device flaps, it is not lost");
  }
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_CONNECTED, comfoair_prov_state(&s));
}

void test_an_address_ends_the_run_of_failures_that_preceded_it(void) {
  // The other half of "sustained", and the half with no countdown to read: step 6's clock, the
  // one that decides whether the next boot owes this device a window. `s->failing = false` in
  // comfoair_prov_on_connected() is the whole of it. Reading comfoair_prov_fallback_in_ms() does
  // NOT pin that line -- on_connected() clears the failure as well, and a cleared failure answers
  // NEVER on its own -- so this is where it is pinned instead.
  //
  // The device: starved of an address in the morning, given one at lunchtime, and waiting thirty
  // seconds for DHCP in the afternoon. Without the reset, the afternoon's single timeout inherits
  // the morning's timestamp, the run reads as forty minutes long, and every boot from then on
  // puts an open access point on the air for a device that is working.
  comfoair_prov_t s = booted(true, true);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 0);
  comfoair_prov_tick(&s, 1 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));
  (void)comfoair_prov_take_action(&s);

  comfoair_prov_on_connected(&s, 40 * MINUTE);

  comfoair_prov_on_disconnected(&s, 41 * MINUTE, 8 /* ours: carries no verdict, clears nothing */);
  comfoair_prov_on_associated(&s, 41 * MINUTE);
  comfoair_prov_tick(&s, 42 * MINUTE);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_address_never_arrived(&s),
                            "one address in the middle did not end the run of failures before it");
}

void test_the_fallback_window_closing_leaves_the_station_running(void) {
  // A configured device never lands in WINDOW_CLOSED. Its window closing means one thing only:
  // the access point goes off the air and the station carries on -- which is also what makes
  // the exposure a duty cycle instead of an open access point for ever. Thirty minutes later
  // The fallback raises it again, and that is the way back the invariant demands.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_connected(&s, 0);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE, 201);
  comfoair_prov_tick(&s, 31 * MINUTE);
  access_point_comes_up(&s, 31 * MINUTE);

  comfoair_prov_tick(&s, 46 * MINUTE);
  comfoair_prov_on_ap_stopped(&s, 46 * MINUTE);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_RETRYING, comfoair_prov_state(&s),
                            "a device with credentials was taken off the air");
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_STATION, comfoair_prov_mode(&s));
  TEST_ASSERT_TRUE(comfoair_prov_is_provisioned(&s));

  comfoair_prov_on_disconnected(&s, 47 * MINUTE, 201);
  comfoair_prov_tick(&s, 78 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
}

void test_the_access_point_that_comes_back_gets_the_short_window(void) {
  // Which of the window's two lengths an access point gets is a property of THAT access point,
  // decided when it comes up. A device that provisioned inside its first window never had a window
  // run out, so keying the length on "has a window ever closed" alone gave its fallback access
  // point the first-run fifteen minutes -- and the fallback access point is open and sits on a
  // device that already holds the owner's data. Fifteen minutes of that in every forty-five is
  // three times the exposure the window rule priced.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_disconnected(&s, 0, 201);
  comfoair_prov_tick(&s, 31 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
  access_point_comes_up(&s, 31 * MINUTE);

  comfoair_prov_tick(&s, 35 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  comfoair_prov_tick(&s, 36 * MINUTE);
  TEST_ASSERT_FALSE_MESSAGE(comfoair_prov_window_is_open(&s),
                            "the access point that came back got the first-run fifteen minutes");
}

void test_credentials_saved_mid_window_do_not_shorten_the_window_around_them(void) {
  // The other side of the same rule. If the length were read live rather than latched when the
  // access point came up, the owner submitting the form would shorten their own window from
  // fifteen minutes to five at the instant they pressed the button -- because saving a pair is
  // what makes has_credentials true.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_credentials_saved(&s, 1 * MINUTE);
  comfoair_prov_tick(&s, 10 * MINUTE);
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_window_is_open(&s),
                           "the owner's own submission cut their window short");
}

void test_the_duty_cycle_is_measured_from_the_access_point_going_away(void) {
  // What keeps the fallback's return from being an open access point for ever is that the next one
  // is thirty minutes after this one CAME DOWN, not thirty minutes after a disconnect that is
  // already history. Measured from the disconnect, the second access point would be due the moment
  // the first one closed and the two would run back to back.
  comfoair_prov_t s = booted(true, true);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_disconnected(&s, 0, 201);
  comfoair_prov_tick(&s, 31 * MINUTE);
  (void)comfoair_prov_take_action(&s);
  access_point_comes_up(&s, 31 * MINUTE);

  comfoair_prov_tick(&s, 36 * MINUTE);  // the five minutes run out
  comfoair_prov_on_ap_stopped(&s, 36 * MINUTE);
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(30 * MINUTE, comfoair_prov_fallback_in_ms(&s, 36 * MINUTE),
                                   "the next access point was measured from a disconnect long past");

  comfoair_prov_tick(&s, 65 * MINUTE);
  TEST_ASSERT_NOT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
  comfoair_prov_tick(&s, 66 * MINUTE);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FALLBACK_AP, comfoair_prov_state(&s));
}

void test_a_fallback_window_closing_does_not_restart_the_clock_that_counts_no_address(void) {
  // The same restart, on the other side of the fallback's line, where it must not happen. The
  // router comes back while the fallback access point is up, the station associates, and DHCP is
  // dead -- so the run of failures being counted is now a no-address run, and step 6 is what will
  // offer this device a window at its next boot. Restarting the clock when the access point comes
  // down would hand that run five extra minutes it has already served, every cycle, and a device
  // whose access point cycles faster than thirty minutes would never reach the escape at all.
  comfoair_prov_t s = booted(true, true);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_disconnected(&s, 0, 201 /* NO_AP_FOUND: the fallback clock starts */);
  comfoair_prov_tick(&s, 31 * MINUTE);
  (void)comfoair_prov_take_action(&s);
  access_point_comes_up(&s, 31 * MINUTE);

  comfoair_prov_on_associated(&s, 32 * MINUTE);
  comfoair_prov_tick(&s, 33 * MINUTE);  // the answer becomes no-address; the run starts here
  (void)comfoair_prov_take_action(&s);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NO_ADDRESS, comfoair_prov_failure(&s));

  comfoair_prov_tick(&s, 36 * MINUTE);  // the fallback window runs out
  comfoair_prov_on_ap_stopped(&s, 36 * MINUTE);

  comfoair_prov_tick(&s, 63 * MINUTE + 30 * SECOND);  // thirty minutes and a half of no address
  TEST_ASSERT_TRUE_MESSAGE(comfoair_prov_address_never_arrived(&s),
                           "the access point going down restarted a clock besides the fallback's");
}

void test_a_second_setup_window_is_the_short_one(void) {
  // Once a window has run out, every window after it is the five-minute one -- across the
  // reboot and within a boot too. The device has already been sitting
  // unclaimed for the full fifteen; the second helping is for someone who is standing in front
  // of it right now.
  comfoair_prov_t s = booted(true, true);
  comfoair_prov_on_disconnected(&s, 0, 201);
  comfoair_prov_tick(&s, 31 * MINUTE);
  access_point_comes_up(&s, 31 * MINUTE);
  comfoair_prov_tick(&s, 46 * MINUTE);
  comfoair_prov_on_ap_stopped(&s, 46 * MINUTE);

  comfoair_prov_on_disconnected(&s, 47 * MINUTE, 201);
  comfoair_prov_tick(&s, 78 * MINUTE);
  access_point_comes_up(&s, 78 * MINUTE);
  comfoair_prov_tick(&s, 82 * MINUTE);
  TEST_ASSERT_TRUE(comfoair_prov_window_is_open(&s));
  comfoair_prov_tick(&s, 83 * MINUTE);
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(&s));
}

// --- retrying ----------------------------------------------------------------------------------

void test_retries_back_off_and_nothing_reboots(void) {
  // AGENTS.md: nothing reboots because a peer is absent. A router that is off for a weekend must
  // not produce a device that reboots all weekend, which is what a reboot timeout does. So the
  // answer to a failed attempt is a longer wait, for ever, and the ceiling is what keeps "for ever"
  // from meaning "and it will find the router an hour after it comes back".
  comfoair_prov_t s = booted(true);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s));

  const uint32_t expected[] = {1 * SECOND, 2 * SECOND, 4 * SECOND, 8 * SECOND, 16 * SECOND,
                               30 * SECOND, 30 * SECOND, 30 * SECOND};
  uint32_t now = 0;
  for (unsigned i = 0; i < sizeof expected / sizeof expected[0]; i++) {
    comfoair_prov_on_disconnected(&s, now, 201);
    TEST_ASSERT_EQUAL(COMFOAIR_PROV_RETRYING, comfoair_prov_state(&s));

    comfoair_prov_tick(&s, now + expected[i] - 1);
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_ACTION_NONE, comfoair_prov_take_action(&s),
                              "retried before the backoff was up");
    now += expected[i];
    comfoair_prov_tick(&s, now);
    TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s));
    TEST_ASSERT_EQUAL(COMFOAIR_PROV_CONNECTING, comfoair_prov_state(&s));
  }
}

void test_a_huge_backoff_ceiling_saturates_instead_of_wrapping(void) {
  // `delay *= 2` past 2^31 wraps to a SMALLER number, and the attempt counter saturates at 32 --
  // so with a ceiling near UINT32_MAX the thirty-third attempt used to compute 1000 << 32, which
  // is zero, and `elapsed(now, retry_at, 0)` is true on every call. The backoff collapsed into a
  // station hammering a network that had been refusing it for weeks: the exact opposite of the
  // saturation arm_retry() promises, and it is the ceiling that is meant to be the safe end.
  comfoair_prov_config_t cfg{};
  comfoair_prov_config_defaults(&cfg);
  cfg.retry_backoff_min_ms = 1 * SECOND;
  cfg.retry_backoff_max_ms = UINT32_MAX;

  comfoair_prov_boot_t boot{};
  boot.has_credentials = true;
  comfoair_prov_t s{};
  comfoair_prov_init(&s, &cfg, &boot);
  (void)comfoair_prov_take_action(&s);

  uint32_t now = 0;
  for (int i = 0; i < 40; i++) {
    comfoair_prov_on_disconnected(&s, now, 201);
    now += 1 * MINUTE;
    comfoair_prov_tick(&s, now);
    (void)comfoair_prov_take_action(&s);
  }

  comfoair_prov_on_disconnected(&s, now, 201);
  comfoair_prov_tick(&s, now + 1 * SECOND);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_ACTION_NONE, comfoair_prov_take_action(&s),
                            "the backoff wrapped to nothing and the station retries every tick");
}

void test_the_nudge_for_a_station_with_no_address_is_not_dropped_by_a_pending_write(void) {
  // set_action() refuses a CONNECT while a durable write is waiting to be drained, because a
  // nudge must never displace a flash write. Step 4 of tick() knows that and keeps its retry
  // armed until the nudge is actually issued; step 3 cleared `associated` first and then dropped
  // the refused nudge on the floor, leaving a machine that has forgotten it was associated and
  // has no timer left -- a station that never tries again, on a device with no access point.
  comfoair_prov_t s = booted(true, /*has_known_good=*/true);
  comfoair_prov_on_credentials_saved(&s, 0);
  (void)comfoair_prov_take_action(&s);
  comfoair_prov_on_associated(&s, 0);

  // One tick in which the trial runs out (RESTORE) and the address timeout expires (CONNECT).
  comfoair_prov_tick(&s, 90 * SECOND);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS, comfoair_prov_take_action(&s));

  comfoair_prov_tick(&s, 91 * SECOND);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s),
                            "the nudge was refused by a pending write and nothing reissued it");
}

void test_the_backoff_resets_when_the_owner_hands_us_a_new_network(void) {
  // Whoever just submitted the form is standing there watching. Making them wait out a
  // half-minute backoff earned by the previous network is how a working device looks broken.
  comfoair_prov_t s = booted(true);
  (void)comfoair_prov_take_action(&s);
  uint32_t now = 0;
  for (int i = 0; i < 6; i++) {
    comfoair_prov_on_disconnected(&s, now, 201);
    now += 30 * SECOND;
    comfoair_prov_tick(&s, now);
    (void)comfoair_prov_take_action(&s);
  }

  comfoair_prov_on_credentials_saved(&s, now);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s));
  comfoair_prov_on_disconnected(&s, now, 201);
  comfoair_prov_tick(&s, now + 1 * SECOND);
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_CONNECT, comfoair_prov_take_action(&s));
}

void test_a_stray_disconnect_leaves_an_unconfigured_device_on_its_access_point(void) {
  // The radio raises WIFI_EVENT_STA_DISCONNECTED in situations that have nothing to do with a
  // stored network -- a mode change among them. A device with nothing in NVS must not be moved
  // into a retry state by one: mode() would stop asking for the access point that is the whole
  // of such a device's presence, and there is nothing else for it to be reachable on.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  comfoair_prov_on_disconnected(&s, 1 * MINUTE, 201);
  comfoair_prov_tick(&s, 2 * MINUTE);

  TEST_ASSERT_EQUAL(COMFOAIR_PROV_UNCONFIGURED, comfoair_prov_state(&s));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_ACCESS_POINT, comfoair_prov_mode(&s));
}

void test_the_window_says_how_long_is_left_so_that_closing_is_observable(void) {
  // A network that silently disappears is indistinguishable from a
  // broken device, so the closing has to be something the API, the log and the UI can see
  // coming. Which of the two clocks is nearer is the whole content of the answer -- late in a
  // window held open by activity it is the ceiling, and reporting the fifteen minutes that
  // activity keeps re-arming would promise time the device is not going to give.
  comfoair_prov_t s = booted(false);
  access_point_comes_up(&s, 0);
  TEST_ASSERT_EQUAL_UINT32(15 * MINUTE, comfoair_prov_window_remaining_ms(&s, 0));
  TEST_ASSERT_EQUAL_UINT32(5 * MINUTE, comfoair_prov_window_remaining_ms(&s, 10 * MINUTE));

  // Held open by a client that keeps knocking, which is the only honest way to still be inside a
  // fifteen-minute window fifty minutes after it opened.
  for (uint32_t t = 5 * MINUTE; t <= 50 * MINUTE; t += 5 * MINUTE) {
    comfoair_prov_on_client_seen(&s, t);
    comfoair_prov_tick(&s, t);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(10 * MINUTE, comfoair_prov_window_remaining_ms(&s, 50 * MINUTE),
                                   "the ceiling was nearer and the window promised past it");

  comfoair_prov_tick(&s, 60 * MINUTE);
  TEST_ASSERT_EQUAL_UINT32(0, comfoair_prov_window_remaining_ms(&s, 60 * MINUTE));
}

// --- the shape of the thing ---------------------------------------------------------------------

void test_a_null_machine_answers_without_falling_over(void) {
  // Every one of these is reachable from an HTTP handler on a device whose provisioning task
  // has not started yet. The answers are the safe ones: not provisioned, no window, do nothing.
  TEST_ASSERT_FALSE(comfoair_prov_is_provisioned(nullptr));
  TEST_ASSERT_FALSE(comfoair_prov_window_is_open(nullptr));
  TEST_ASSERT_FALSE(comfoair_prov_should_roll_back(nullptr));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_MODE_OFF, comfoair_prov_mode(nullptr));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_ACTION_NONE, comfoair_prov_take_action(nullptr));
  TEST_ASSERT_EQUAL(COMFOAIR_PROV_FAIL_NONE, comfoair_prov_failure(nullptr));
  comfoair_prov_tick(nullptr, 0);
}

void test_the_machine_holds_no_credentials_only_the_fact_of_them(void) {
  // DO NOT add an ssid or psk field here. This struct is what the status projection is built
  // from, and comfoair_secret_key() can only redact a named key in a document -- it cannot
  // help a string that a status handler prints because it was in the struct it was handed.
  // The caller owns NVS; this machine only ever knows whether something is in it.
  TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(96u, (unsigned)sizeof(comfoair_prov_t),
                                    "this struct grew enough to be holding something it must not");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_an_open_access_point_means_unclaimed_whatever_nvs_holds);
  RUN_TEST(test_the_fallback_access_point_unclaims_a_device_that_has_an_owner);
  RUN_TEST(test_the_window_is_anchored_on_the_access_point_not_on_boot);
  RUN_TEST(test_client_activity_extends_the_window);
  RUN_TEST(test_a_knock_after_the_window_ran_out_does_not_revive_it);
  RUN_TEST(test_the_ceiling_bounds_what_activity_can_extend);
  RUN_TEST(test_a_window_that_never_closes_is_a_supported_configuration);
  RUN_TEST(test_a_window_that_never_closes_outlives_a_window_that_did);
  RUN_TEST(test_a_closed_window_on_an_unconfigured_device_takes_the_radio_off_the_air);
  RUN_TEST(test_a_reboot_after_a_closed_window_still_offers_a_way_back);
  RUN_TEST(test_a_closed_window_does_not_reopen_by_itself);
  RUN_TEST(test_connecting_takes_the_access_point_off_the_air);
  RUN_TEST(test_the_clock_wrapping_does_not_close_the_window_early);
  RUN_TEST(test_saving_credentials_is_not_connecting);
  RUN_TEST(test_saving_credentials_extends_the_window);
  RUN_TEST(test_a_typo_from_the_lan_puts_the_old_network_back);
  RUN_TEST(test_a_rollback_does_not_happen_twice);
  RUN_TEST(test_a_first_provisioning_with_no_known_good_pair_keeps_what_the_owner_typed);
  RUN_TEST(test_a_confirmed_connection_is_what_makes_a_pair_known_good);
  RUN_TEST(test_a_network_that_is_not_there_is_told_apart_from_one_that_refuses_us);
  RUN_TEST(test_a_mistyped_password_is_told_apart_from_a_router_that_says_no);
  RUN_TEST(test_a_disconnect_this_machine_asked_for_is_not_a_verdict_on_anything);
  RUN_TEST(test_an_unclassified_reason_reads_as_refused_so_a_way_back_still_exists);
  RUN_TEST(test_associated_with_no_address_is_its_own_answer);
  RUN_TEST(test_our_own_recovery_nudge_does_not_become_the_owners_answer);
  RUN_TEST(test_an_address_arriving_in_time_is_not_a_failure);
  RUN_TEST(test_each_failure_has_a_name_the_api_can_publish);
  RUN_TEST(test_the_access_point_returns_after_thirty_minutes_of_a_network_that_is_not_there);
  RUN_TEST(test_no_address_does_not_bring_the_access_point_back);
  RUN_TEST(test_a_station_that_is_never_given_an_address_gets_a_window_at_the_next_boot);
  RUN_TEST(test_an_address_ends_the_no_address_escape_so_it_is_not_offered_for_ever);
  RUN_TEST(test_a_dhcp_pause_after_a_router_returns_is_not_a_starved_station);
  RUN_TEST(test_one_success_resets_the_sustained_failure_clock);
  RUN_TEST(test_an_address_ends_the_run_of_failures_that_preceded_it);
  RUN_TEST(test_the_fallback_window_closing_leaves_the_station_running);
  RUN_TEST(test_the_duty_cycle_is_measured_from_the_access_point_going_away);
  RUN_TEST(test_a_fallback_window_closing_does_not_restart_the_clock_that_counts_no_address);
  RUN_TEST(test_the_access_point_that_comes_back_gets_the_short_window);
  RUN_TEST(test_credentials_saved_mid_window_do_not_shorten_the_window_around_them);
  RUN_TEST(test_a_second_setup_window_is_the_short_one);
  RUN_TEST(test_retries_back_off_and_nothing_reboots);
  RUN_TEST(test_a_huge_backoff_ceiling_saturates_instead_of_wrapping);
  RUN_TEST(test_the_nudge_for_a_station_with_no_address_is_not_dropped_by_a_pending_write);
  RUN_TEST(test_the_backoff_resets_when_the_owner_hands_us_a_new_network);
  RUN_TEST(test_a_stray_disconnect_leaves_an_unconfigured_device_on_its_access_point);
  RUN_TEST(test_the_window_says_how_long_is_left_so_that_closing_is_observable);
  RUN_TEST(test_a_null_machine_answers_without_falling_over);
  RUN_TEST(test_the_machine_holds_no_credentials_only_the_fact_of_them);
  return UNITY_END();
}
