// The ladder's own behaviour: which rung, when it is armed, when the counter resets and when it
// stands still while the device has no address.
//
// SPLIT FROM test_mqtt_wiring.cpp, which holds what the PUBLISHER must carry into the machine --
// the failure word, the jitter seed, and the protocol that keeps a rung off a live attempt. Both
// halves were one file until it passed the 600-line ceiling (AGENTS.md); they share
// ladder_harness.h and one main(), which lives in test_mqtt_backoff.cpp with every RUN_TEST, so
// that a case cannot be lost by forgetting to register it. Same shape as test/test_phase.
//
// The rung arithmetic is proved next door in test_mqtt_backoff.cpp and is not re-proved here: the
// bands below appear only where the point is WHICH row was taken.
#include "ladder_harness.h"

// ---------------------------------------------------------------------------------------------

// THE ONLY CASE THAT CATCHES THIS. A pass that builds a
// signal out of the four ERROR fields for comfoair_mqtt_classify() and hands that same half-built
// struct to comfoair_mqtt_link_of() answers "off" -- for ever, on a broker that is publishing
// right now. Every other assertion in this suite stays green while it does: the value is inside
// the enumeration, the delays are right, the document renders. Only this asks the machine what it
// says about a device that is CONNECTED.
void test_a_live_broker_is_online_and_not_off(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  in.connected                 = true;

  const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_ONLINE, out.link,
                                "a connected broker is not reported online");
  TEST_ASSERT_EQUAL_STRING("online", comfoair_mqtt_link_name(out.link));
  // The three fields link_of reads and classify does not. If the builder drops any one of them the
  // line above turns "off" or "connecting", which is why they are named here rather than trusted.
  TEST_ASSERT_FALSE_MESSAGE(out.reconnect, "a connected device dialled its broker again");
  TEST_ASSERT_FALSE_MESSAGE(out.retry_known, "a connected device has a rung armed");
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_NONE, out.failure,
                                "a device that is publishing reports a failure class");

  // And the pair that tells an attempt from a wait, from the same machine: an attempt in flight is
  // CONNECTING with no time to show, a rung being waited out is BACKOFF with one.
  comfoair_mqtt_ladder_t    st2{};
  comfoair_mqtt_ladder_in_t dead = healthy(1000u);
  transport_failure(&dead, false);
  const comfoair_mqtt_ladder_out_t armed = tick(&st2, &dead);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_BACKOFF, armed.link,
                                "a rung being waited out is backoff");
  TEST_ASSERT_TRUE(armed.retry_known);
  const uint32_t at = run_until_dial(&st2, &dead, 8u, "a transport failure");
  (void)at;
  const comfoair_mqtt_ladder_out_t flying = tick(&st2, &dead);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_BACKOFF, flying.link,
                                "the next rung is armed on the pass after the dial");
  TEST_ASSERT_EQUAL_STRING("tcp", comfoair_mqtt_fail_name(flying.failure));
}

// Hardware scenario HV-MQTT-19 (docs/hardware-verification/06-mqtt.md) exercises this on the
// device. The defect was found in an earlier version of the pass: `s_retry_at = 0` meant both "no
// address" and "nothing scheduled", so an address coming back cost a whole rung AND an attempt. The
// ladder must stand while the address is gone and dial on the very pass the address returns.
void test_an_address_returning_does_not_burn_an_attempt(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  transport_failure(&in, false);

  // Two rungs climbed against an unreachable broker, so there is something to lose.
  run_until_dial(&st, &in, 8u, "the first rung");
  run_until_dial(&st, &in, 12u, "the second rung");
  comfoair_mqtt_ladder_out_t out = tick(&st, &in);
  const uint32_t attempts_before = out.attempts;
  TEST_ASSERT_TRUE_MESSAGE(attempts_before >= 2u, "two rungs did not count two attempts");

  // Three minutes with no address. Not one attempt is made and not one is counted.
  in.has_address = false;
  for (uint32_t n = 0; n < 180u; n++) {
    out = tick(&st, &in);
    TEST_ASSERT_FALSE_MESSAGE(out.reconnect, "the ladder dialled a broker with no address");
    TEST_ASSERT_FALSE_MESSAGE(out.retry_known, "a rung is armed while there is no address");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(attempts_before, out.attempts,
                                     "an attempt was counted while there was no address");
  }

  // The address is back. THE VERY NEXT PASS dials, and the counter is where the broker left it --
  // a broker that is down must not be forgiven because our Wi-Fi blinked.
  in.has_address = true;
  out            = tick(&st, &in);
  TEST_ASSERT_TRUE_MESSAGE(out.reconnect,
                           "the pass after an address returned did not attempt at once");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(attempts_before, out.attempts,
                                   "the returning address burned an attempt");

  // AND IT IS SPENT ONCE, which needs the case the pass clears the flag for: the address comes
  // back and the broker answers ON THE SAME PASS, so the returning-address branch never runs. If
  // the connected branch did not clear the flag there, the NEXT failure -- an hour later, nothing
  // to do with Wi-Fi -- would collect the immediate attempt owed to a returning network, once per
  // outage for the rest of the device's life.
  comfoair_mqtt_ladder_t    back{};
  comfoair_mqtt_ladder_in_t home = healthy(1000u);
  transport_failure(&home, false);
  home.has_address = false;
  for (uint32_t n = 0; n < 10u; n++) {
    tick(&back, &home);
  }
  home.has_address  = true;
  home.connected    = true;
  home.was_connected = false;
  home.failed       = false;
  home.kind         = COMFOAIR_MQTT_ERR_NONE;
  for (uint32_t n = 0; n < 90u; n++) {
    tick(&back, &home);
  }
  transport_failure(&home, false);
  home.connected = false;
  const comfoair_mqtt_ladder_out_t later = tick(&back, &home);
  TEST_ASSERT_FALSE_MESSAGE(later.reconnect,
                            "a failure after a settled session took the returning address's free "
                            "attempt");
  TEST_ASSERT_TRUE_MESSAGE(later.retry_in_ms >= FAST_FIRST_MS * 80u / 100u,
                           "that failure was not made to wait its rung");
}

// The settled-session reset, at the level the pass runs it: the reset is a rule evaluated on EVERY
// pass while connected, not an edge at the moment of connecting. A broker that accepts and drops
// 200 ms later -- a duplicate client id, an exhausted max_connections, an ACL that cuts after
// CONNACK -- must climb, not cycle for ever on the first rung, republishing every entity each time.
void test_only_a_settled_session_resets_the_ladder(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  transport_failure(&in, false);
  run_until_dial(&st, &in, 8u, "the first rung");
  run_until_dial(&st, &in, 12u, "the second rung");
  const uint32_t climbed = tick(&st, &in).attempts;
  TEST_ASSERT_TRUE(climbed >= 2u);

  // A session that lasts 59 seconds and dies. Nothing is forgiven.
  in.connected = true;
  for (uint32_t ms = 0; ms < SETTLED_MS - 1000u; ms += 1000u) {
    const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(climbed, out.attempts,
                                     "a session shorter than a minute reset the ladder");
  }
  dropped_session(&in);
  const comfoair_mqtt_ladder_out_t after = tick(&st, &in);
  // It ARMS the next rung here, so the counter moves up by one; what must not happen is a reset.
  TEST_ASSERT_TRUE_MESSAGE(after.attempts >= climbed,
                           "a flapping broker reset the ladder on disconnect");

  // The same broker, a minute of session this time.
  in.connected     = true;
  in.was_connected = false;
  in.failed        = false;
  uint32_t attempts_now = climbed;
  for (uint32_t ms = 0; ms <= SETTLED_MS; ms += 1000u) {
    attempts_now = tick(&st, &in).attempts;
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, attempts_now,
                                   "a settled session did not reset the ladder");
}

// The second reset path, saving new broker settings, and the only one that is not a level rule: the
// owner saved his settings, so he has corrected something. This IS the exit from a five-minute
// BROKER_REJECTED rung inside a second -- scenario HV-MQTT-11 -- and without it an owner who fixes
// a mistyped password waits out the credential refusal's 300 seconds for no reason.
void test_a_reconfigure_leaves_the_rejection_rung_at_once(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  in.kind                      = COMFOAIR_MQTT_ERR_REFUSED;
  in.connack                   = 5u;
  in.failed                    = true;

  comfoair_mqtt_ladder_out_t out = tick(&st, &in);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_REJECTED, out.link,
                                "a refused password is not the generic backoff colour");
  TEST_ASSERT_EQUAL_STRING("bad-credentials", comfoair_mqtt_fail_name(out.failure));
  TEST_ASSERT_TRUE_MESSAGE(out.retry_in_ms >= FLAT_MS * 80u / 100u,
                           "a credential refusal waits five minutes");

  const uint32_t reset_at = in.now_ms;
  // The owner saves. HV-MQTT-11: SECONDS, not the five minutes he is sitting on. The next
  // pass arms rung 0 of the fast row rather than dialling on the spot, and that is the whole of
  // the difference -- the client the reconfigure branch has just rebuilt dials by itself anyway
  // (mqtt_client.c: a fresh client starts in MQTT_STATE_INIT), so what this rule owes the owner is
  // not to hold the NEXT attempt for five minutes.
  comfoair_mqtt_ladder_reset(&st);
  in.kind    = COMFOAIR_MQTT_ERR_NONE;
  in.connack = 0u;
  out        = tick(&st, &in);
  // One and not zero: this very pass arms rung 0 and counting a rung is what the counter is for.
  // Zero was reached and is visible in the rung it produced -- the band below is rung 0's, and the
  // sixteenth rung's band does not overlap it.
  TEST_ASSERT_TRUE_MESSAGE(out.attempts <= 1u,
                           "a reconfigure did not put the attempt counter back at the bottom");
  TEST_ASSERT_TRUE_MESSAGE(out.retry_in_ms <= FAST_FIRST_MS * 120u / 100u,
                           "a reconfigure left the owner on the five-minute rung");
  const uint32_t at = run_until_dial(&st, &in, 5u, "the rung after a reconfigure");
  TEST_ASSERT_TRUE_MESSAGE(at - reset_at <= 3000u,
                           "a reconfigure did not produce an attempt within seconds");
}

// This one is about the DOCUMENT rather than the delay: the
// table saturates at 16 by itself, so an unclamped counter would keep an unchanged retry_in_ms
// beside "4000 attempts" in /api/status -- a number that says the device is doing something it is
// not.
void test_the_counter_saturates_where_the_table_does(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  transport_failure(&in, false);

  comfoair_mqtt_ladder_out_t out{};
  // Far more dials than rungs: at the ceiling each one costs a minute of simulated time.
  for (uint32_t n = 0; n < 200u; n++) {
    run_until_dial(&st, &in, 120u, "a rung at the ceiling");
    out = tick(&st, &in);
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(SATURATES_AT, out.attempts,
                                   "the attempt counter did not saturate at sixteen");
  TEST_ASSERT_TRUE_MESSAGE(out.retry_in_ms <= 72000u,
                           "the delay left the 60 s ceiling's jitter band");
}

// The second reason `retry_scheduled` is a bool of its own and not a zero `retry_at_ms` sentinel,
// and the one no bench test would ever reach: pdTICKS_TO_MS(xTaskGetTickCount()) is zero on the
// first ticks after boot and passes through zero again every ~49.7 days. The rung must come due on
// time across the wrap.
void test_the_ladder_survives_the_millisecond_wrap(void)
{
  comfoair_mqtt_ladder_t st{};
  // One second before the wrap, so the first rung's 1.6-2.4 s comes due on the FAR side of it and
  // retry_at_ms is a SMALLER number than the clock that armed it. `now >= retry_at` reads that as
  // "long overdue" and dials at once; the signed difference reads it as "1.6 s to go".
  comfoair_mqtt_ladder_in_t in = healthy(0xffffffffu - 1000u);
  transport_failure(&in, false);

  const uint32_t armed_at = in.now_ms;
  const uint32_t dialled  = run_until_dial(&st, &in, 8u, "a rung armed before the wrap");
  const uint32_t waited   = dialled - armed_at;   // unsigned, so the wrap cancels out
  TEST_ASSERT_TRUE_MESSAGE(waited >= FAST_FIRST_MS * 80u / 100u,
                           "the rung came due the moment the clock wrapped round");
  TEST_ASSERT_TRUE_MESSAGE(waited <= FAST_FIRST_MS * 120u / 100u + 1000u,
                           "the rung did not come due until after the clock wrapped round");

  // And at now == 0 itself, where a zero sentinel reads as "nothing is scheduled".
  comfoair_mqtt_ladder_t    boot{};
  comfoair_mqtt_ladder_in_t fresh = healthy(0u);
  transport_failure(&fresh, false);
  const comfoair_mqtt_ladder_out_t out = tick(&boot, &fresh);
  TEST_ASSERT_TRUE_MESSAGE(out.retry_known, "a rung armed at time zero is reported as unarmed");
  TEST_ASSERT_TRUE_MESSAGE(out.retry_in_ms >= FAST_FIRST_MS * 80u / 100u,
                           "a rung armed at time zero came due immediately");
}

// Through the pass rather than through classify() alone: the
// handler's was_connected must reach the row that is chosen. A dropped session starts one rung
// lower than a broker that was never reached, so an empty was_connected
// doubles the first wait after every ordinary broker restart (scenario HV-MQTT-04).
void test_a_dropped_session_takes_the_dropped_row(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  in.connected                 = true;
  tick(&st, &in);

  dropped_session(&in);
  const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
  TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_FAIL_DROPPED, out.failure,
                                "a session that dropped is not classified as dropped");
  TEST_ASSERT_TRUE_MESSAGE(out.retry_in_ms >= DROP_FIRST_MS * 80u / 100u &&
                               out.retry_in_ms <= DROP_FIRST_MS * 120u / 100u,
                           "a dropped session did not take the one-second row");
  // The row it must NOT be on: the bands are a factor of two apart, so this says which mutation
  // the case is for.
  TEST_ASSERT_TRUE_MESSAGE(out.retry_in_ms < FAST_FIRST_MS * 80u / 100u,
                           "a dropped session took the unreachable-broker row");
}

// esp_mqtt_client_reconnect() answers ESP_FAIL while the client has not parked yet, and the pass
// repeats on the next tick. Without that a
// dial that takes the full network timeout -- up to 2 x 10 s, mqtt_client.c:1705-1708 and :1715 --
// lets the ladder arm, expire and count SEVERAL rungs while one attempt is still in flight, and
// /api/status reports attempts nobody made.
void test_a_client_that_has_not_parked_yet_costs_no_attempt(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  transport_failure(&in, false);
  run_until_dial(&st, &in, 8u, "the first rung");
  const uint32_t attempts = tick(&st, &in).attempts;

  // Twenty seconds of a client that is still dialling: every pass asks again, none counts.
  for (uint32_t n = 0; n < 20u; n++) {
    comfoair_mqtt_ladder_out_t out{};
    comfoair_mqtt_ladder_pass(&st, &in, &out);
    if (out.reconnect) {
      comfoair_mqtt_ladder_not_yet(&st, in.now_ms);
    }
    in.now_ms += 1000u;
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(attempts, out.attempts,
                                     "a refused reconnect counted as an attempt");
  }
  // And the pass after it parks dials for real.
  TEST_ASSERT_TRUE_MESSAGE(tick(&st, &in).reconnect,
                           "the ladder stopped asking after the client refused once");
}

// Sections 5.5 and 6.2: with no client there is nothing to dial and nothing to say. `off` is how
// this firmware SHIPS -- no broker configured -- and not a degradation, so it must not be reported
// as an attempt in flight; and the pass must not invent a ladder for a client that does not exist.
void test_a_device_with_no_broker_is_off_and_dials_nothing(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);
  in.client                    = false;
  in.configured                = false;

  for (uint32_t n = 0; n < 600u; n++) {
    const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_OFF, out.link,
                                  "a device with no broker is not off");
    TEST_ASSERT_FALSE_MESSAGE(out.reconnect, "a device with no broker dialled one");
    TEST_ASSERT_FALSE_MESSAGE(out.retry_known, "a device with no broker armed a rung");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, out.attempts, "a device with no broker counted attempts");
  }

  // And the harder half of the same rule: a host IS stored and the client still does not exist,
  // which is what esp_mqtt_client_init() or _start() failing leaves behind, with a failure recorded
  // beside it. There is nothing to reconnect -- a handle that was never built is not repaired by
  // asking it to dial -- so the ladder must not climb here either. The rebuild is the owner's save.
  //
  // NAMED RATHER THAN ASSERTED: `link` reads "connecting" in this state, which is not true -- no
  // attempt is in flight -- and the five words of `mqtt.link` have none for "the client could not
  // be built". It cannot be reproduced on hardware either short of exhausting the heap.
  comfoair_mqtt_ladder_t    broken{};
  comfoair_mqtt_ladder_in_t half = healthy(1000u);
  half.client                    = false;
  transport_failure(&half, false);
  for (uint32_t n = 0; n < 600u; n++) {
    const comfoair_mqtt_ladder_out_t out = tick(&broken, &half);
    TEST_ASSERT_FALSE_MESSAGE(out.reconnect, "the ladder dialled a client that does not exist");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, out.attempts,
                                     "a client that was never built counted attempts");
  }

  // And the rung a live client had armed is DISARMED when that client goes away, which is what a
  // reconfigure that clears the broker host does. Left armed, the document reports "backoff" with
  // a countdown that never moves and an attempt that will never be made.
  comfoair_mqtt_ladder_t    armed{};
  comfoair_mqtt_ladder_in_t was_live = healthy(1000u);
  transport_failure(&was_live, false);
  TEST_ASSERT_TRUE_MESSAGE(tick(&armed, &was_live).retry_known, "the rung under test was not armed");
  was_live.client = false;
  const comfoair_mqtt_ladder_out_t gone = tick(&armed, &was_live);
  TEST_ASSERT_FALSE_MESSAGE(gone.retry_known,
                            "a rung stayed armed for a client that no longer exists");
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(COMFOAIR_MQTT_LINK_CONNECTING, gone.link,
                                   "a destroyed client still reported a rung being waited out");
}

// The never-ending retry (AGENTS.md), stated as the property rather than a promise: no
// input sequence leaves the machine unable to dial again. The sweep is over the whole product of
// the flags the pass reads, and the assertion is that after any of them a healthy device attempts
// within one ceiling. NULLs are here too -- a total function has no crash to report.
void test_no_sequence_of_inputs_can_park_the_ladder_for_ever(void)
{
  for (uint32_t bits = 0; bits < 128u; bits++) {
    comfoair_mqtt_ladder_t    st{};
    comfoair_mqtt_ladder_in_t in = healthy(1000u);
    in.client                    = (bits & 1u) != 0u;
    in.configured                = (bits & 2u) != 0u;
    in.connected                 = (bits & 4u) != 0u;
    in.has_address               = (bits & 8u) != 0u;
    in.was_connected             = (bits & 16u) != 0u;
    in.failed                    = (bits & 32u) != 0u;
    in.kind = (bits & 64u) != 0u ? COMFOAIR_MQTT_ERR_REFUSED : COMFOAIR_MQTT_ERR_TRANSPORT;
    in.connack = 5u;
    for (uint32_t n = 0; n < 30u; n++) {
      tick(&st, &in);
    }

    // Whatever happened, a device that is now well dials inside one ceiling of the slowest row.
    comfoair_mqtt_ladder_in_t well = healthy(in.now_ms);
    transport_failure(&well, false);
    char msg[128];
    snprintf(msg, sizeof msg, "flags %02x left the ladder unable to dial", (unsigned)bits);
    bool dialled = false;
    for (uint32_t n = 0; n < 400u && !dialled; n++) {
      dialled = tick(&st, &well).reconnect;
    }
    TEST_ASSERT_TRUE_MESSAGE(dialled, msg);
  }

  comfoair_mqtt_ladder_t     st{};
  comfoair_mqtt_ladder_in_t  in = healthy(1000u);
  comfoair_mqtt_ladder_out_t out{};
  comfoair_mqtt_ladder_pass(NULL, &in, &out);
  comfoair_mqtt_ladder_pass(&st, NULL, &out);
  comfoair_mqtt_ladder_pass(&st, &in, NULL);
  comfoair_mqtt_ladder_not_yet(NULL, 0u);
  comfoair_mqtt_ladder_reset(NULL);
}

// A client that has just been built dials the broker by itself -- it starts in MQTT_STATE_INIT and
// reaches the broker without being asked -- so until something has failed there is nothing to
// retry. Reported otherwise, every boot spends its first seconds saying "backoff, one attempt"
// about an attempt that is in flight and was never ours, and /api/status shows a device retrying
// a broker it is in the middle of reaching.
//
// THE OTHER HALF IS THE NEVER-ENDING RETRY, and it is the reason this case exists in one piece: the
// ladder must not read "nothing failed" as "wait for ever". A timed-out esp_mqtt_connect dispatches
// only MQTT_EVENT_DISCONNECTED and no error at all, so a real failure can arrive with a failure
// CLASS of none -- and it must still climb.
void test_a_fresh_client_is_left_to_dial_by_itself(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);

  for (uint32_t n = 0; n < 60u; n++) {
    const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
    TEST_ASSERT_FALSE_MESSAGE(out.reconnect, "the ladder dialled over the client's own attempt");
    TEST_ASSERT_FALSE_MESSAGE(out.retry_known, "a rung was armed before anything had failed");
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0u, out.attempts, "an attempt was counted before any failure");
    TEST_ASSERT_EQUAL_INT_MESSAGE(COMFOAIR_MQTT_LINK_CONNECTING, out.link,
                                  "an attempt in flight is connecting, not backoff");
  }

  // The dial times out. No error event, so the class is none -- and the ladder climbs anyway.
  in.failed = true;
  const comfoair_mqtt_ladder_out_t armed = tick(&st, &in);
  TEST_ASSERT_TRUE_MESSAGE(armed.retry_known, "a failure with no error event armed no rung");
  TEST_ASSERT_TRUE_MESSAGE(armed.retry_in_ms >= FAST_FIRST_MS * 80u / 100u,
                           "a failure with no error event was answered faster than the fast row");
  run_until_dial(&st, &in, 5u, "a failure that carried no error event");
}

// The mutation behind this case survived the whole suite of be70cbd:
// `st->session_timed = false` in the branch where the link is down is the only line that makes
// comfoair_mqtt_backoff_settled() measure the CURRENT session rather than the time since the first
// connection this device ever made. Delete it and a broker that accepts and drops every ten
// seconds is forgiven at the sixth cycle -- the ladder falls back to rung 0 and stays there, an
// endless one-second reconnect loop with every entity republished each time, which is the exact
// outcome the settled-session rule exists to prevent.
//
// The case that catches it has to run the cycle long enough for the WALL CLOCK to pass sixty
// seconds while no single session does. No earlier case did: they all measured one session.
void test_a_flapping_broker_is_never_forgiven(void)
{
  comfoair_mqtt_ladder_t    st{};
  comfoair_mqtt_ladder_in_t in = healthy(1000u);

  uint32_t attempts = 0;
  uint32_t previous = 0;
  for (uint32_t cycle = 0; cycle < 10u; cycle++) {
    // Ten seconds of session -- a sixth of the minute that counts as settled -- and then a drop.
    in.connected     = true;
    in.was_connected = false;
    in.failed        = false;
    for (uint32_t n = 0; n < 10u; n++) {
      tick(&st, &in);
    }
    dropped_session(&in);
    const comfoair_mqtt_ladder_out_t out = tick(&st, &in);
    attempts = out.attempts;

    char msg[160];
    snprintf(msg, sizeof msg, "cycle %u: attempts went %u -> %u, so a flapping broker was forgiven",
             (unsigned)cycle, (unsigned)previous, (unsigned)attempts);
    TEST_ASSERT_TRUE_MESSAGE(attempts > previous, msg);
    previous = attempts;

    // And the delay is the DROPPED row, climbing: this is where those rungs are actually used,
    // which is the answer to "the row has seven numbers and only the first is reachable". A broker
    // that keeps accepting and dropping climbs all of them.
    const uint32_t rung = DROP_FIRST_MS << (attempts - 1u > 5u ? 5u : attempts - 1u);
    in.now_ms += rung + 2000u;   // wait the rung out; the next cycle reconnects
  }
  TEST_ASSERT_EQUAL_UINT32_MESSAGE(10u, attempts,
                                   "ten drops did not count ten attempts");
}
