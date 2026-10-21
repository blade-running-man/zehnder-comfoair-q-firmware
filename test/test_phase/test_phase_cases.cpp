// The named cases: one owner-visible distinction each, spoiling exactly one field of a healthy
// device, plus the single main() this directory is allowed.
//
// SPLIT FROM test_phase_sweeps.cpp: claims checked over the whole product of inputs live there,
// readable witnesses live here, and one file had reached the 600-line ceiling (AGENTS.md).
// PlatformIO links every .cpp in the directory into one binary, so the two are one suite. EVERY
// RUN_TEST IS BELOW, both halves' -- a case registered where it is defined is a case that goes
// missing when a file is split again.
//
// No clock appears in either file and there is nothing to fake: the hold-offs belong to the
// collector the monitor drives, and are tested next door in test_phase_collect.cpp.
#include <unity.h>

#include <cstring>

#include "phase_inputs.h"

// Defined in test_phase_sweeps.cpp.
void test_no_input_combination_falls_through_to_no_phase(void);
void test_every_phase_is_reachable_from_some_input(void);
void test_every_bit_of_the_sweep_moves_a_flag_of_its_own(void);
void test_lower_numbered_phase_wins_when_both_conditions_hold(void);
void test_the_phase_enumeration_has_fourteen_values(void);
void test_derive_takes_no_clock(void);

// Defined in test_phase_collect.cpp -- the collector, which is the half of comfoair_phase_monitor
// that does not need a device.
void test_the_monitor_asks_the_aggregate_and_never_decides_on_its_own(void);
void test_a_bus_that_never_spoke_is_not_a_silent_bus(void);
void test_a_bus_that_stopped_speaking_reaches_the_phase(void);
void test_the_provocation_leaves_the_collector_as_one_bool(void);
void test_a_broker_that_has_just_dropped_is_still_online(void);
void test_a_broker_gone_for_the_hold_off_is_no_broker(void);
void test_a_broker_that_came_back_is_granted_the_hold_off_again(void);
void test_the_hold_off_survives_the_millisecond_wrap(void);
void test_a_refusal_needs_no_hold_off_at_all(void);
void test_a_server_unavailable_connack_is_not_a_refusal_here_either(void);
void test_each_start_branch_lands_in_the_field_that_names_it(void);
void test_a_driver_stopped_after_it_started_is_not_a_startup_fault(void);
void test_a_transition_is_reported_once_and_a_steady_phase_never(void);
void test_a_pass_with_nothing_to_say_is_answered_and_not_crashed(void);

void setUp(void) {}
void tearDown(void) {}

void test_null_inputs_are_boot_not_online(void) {
  // A caller with nothing to say has not yet started the provisioning machine, which is
  // literally BOOT. The alternative -- reporting ONLINE for a null pointer -- would make the
  // quietest possible bug look like the healthiest possible device.
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BOOT, comfoair_phase_derive(nullptr));
}

void test_machine_not_ready_outranks_everything_including_ota(void) {
  comfoair_phase_inputs_t in = online();
  in.machine_ready   = false;
  in.ota_in_progress = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BOOT, comfoair_phase_derive(&in));
}

// --- the three real priority conflicts -------------------------------------------------------

void test_a_refused_connack_outranks_a_silent_bus(void) {
  // Why a fact outranks an inference here: comfoair_phase.h:84-90. This is the case that changed
  // sign (BUS_SILENT once ranked first), so it is spelled out as well as swept.
  comfoair_phase_inputs_t in = online();
  in.broker_rejected  = true;
  in.broker_connected = false;
  in.bus_silent       = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BROKER_REJECTED, comfoair_phase_derive(&in));
}

void test_missing_network_outranks_a_silent_bus(void) {
  // The LED carries units of bits. The network is the only channel over which a bus fault can be
  // explained in words, so spending the slow channel on a fault nobody can be told about is
  // waste.
  comfoair_phase_inputs_t in = online();
  in.prov_state   = COMFOAIR_PROV_RETRYING;
  in.prov_failure = COMFOAIR_PROV_FAIL_NETWORK_ABSENT;
  in.bus_silent   = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NET_LOST, comfoair_phase_derive(&in));
}

void test_access_point_on_air_outranks_a_startup_fault(void) {
  // Why an instruction outranks information: comfoair_phase.h:57-61.
  comfoair_phase_inputs_t in = online();
  in.ap_on_air   = true;
  in.prov_state  = COMFOAIR_PROV_UNCONFIGURED;
  in.can_started = false;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_SETUP_AP, comfoair_phase_derive(&in));
}

void test_trial_outranks_the_access_point_it_runs_underneath(void) {
  // Why this order is necessity rather than taste: comfoair_phase.h:52-56.
  comfoair_phase_inputs_t in = online();
  in.prov_state = COMFOAIR_PROV_TRIAL;
  in.ap_on_air  = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_TRIAL, comfoair_phase_derive(&in));
}

void test_ota_outranks_the_open_access_point(void) {
  // The only phase whose owner action is "do not cut the power".
  comfoair_phase_inputs_t in = online();
  in.ota_in_progress = true;
  in.ap_on_air       = true;
  in.prov_state      = COMFOAIR_PROV_UNCONFIGURED;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_OTA, comfoair_phase_derive(&in));
}

// --- the splits the phases exist for ------------------------------------------------------------

void test_unconfigured_access_point_is_setup_not_recovery(void) {
  comfoair_phase_inputs_t in = online();
  in.ap_on_air  = true;
  in.prov_state = COMFOAIR_PROV_UNCONFIGURED;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_SETUP_AP, comfoair_phase_derive(&in));
}

void test_fallback_access_point_is_recovery_not_setup(void) {
  // Same open access point, different meaning to the owner, and why that earns a second phase:
  // comfoair_phase.h:62-65.
  comfoair_phase_inputs_t in = online();
  in.ap_on_air  = true;
  in.prov_state = COMFOAIR_PROV_FALLBACK_AP;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_RECOVERY_AP, comfoair_phase_derive(&in));
}

void test_wrong_password_and_refused_are_net_rejected(void) {
  // "The network is there and will not let us in" -- fix the password or the router's settings.
  const comfoair_prov_failure_t rejected[] = {COMFOAIR_PROV_FAIL_WRONG_PASSWORD,
                                              COMFOAIR_PROV_FAIL_REFUSED};
  for (comfoair_prov_failure_t failure : rejected) {
    comfoair_phase_inputs_t in = online();
    in.prov_state   = COMFOAIR_PROV_RETRYING;
    in.prov_failure = failure;
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NET_REJECTED, comfoair_phase_derive(&in));
  }
}

void test_network_absent_and_no_address_are_net_lost(void) {
  // "No working connectivity to be had" -- check the router and its DHCP. The four Wi-Fi verdicts
  // stay four wherever there are words for them: failure and failure_reason travel by name in
  // /api/status. Only the channel that has nothing but colour maps them by owner action.
  const comfoair_prov_failure_t lost[] = {COMFOAIR_PROV_FAIL_NETWORK_ABSENT,
                                          COMFOAIR_PROV_FAIL_NO_ADDRESS};
  for (comfoair_prov_failure_t failure : lost) {
    comfoair_phase_inputs_t in = online();
    in.prov_state   = COMFOAIR_PROV_RETRYING;
    in.prov_failure = failure;
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NET_LOST, comfoair_phase_derive(&in));
  }
}

void test_window_closed_is_setup_expired_not_net_lost(void) {
  // The only state with no network, no access point and no scheduled return: the way out is the
  // button's short press (docs/device/button.md, The setup press), which is why it may not be
  // swallowed by a network phase.
  comfoair_phase_inputs_t in = online();
  in.prov_state   = COMFOAIR_PROV_WINDOW_CLOSED;
  in.prov_failure = COMFOAIR_PROV_FAIL_NONE;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_SETUP_EXPIRED, comfoair_phase_derive(&in));
}

void test_retrying_and_connecting_are_one_phase(void) {
  // Why the two states are one phase: comfoair_phase.h:80-83.
  const comfoair_prov_state_t attempting[] = {COMFOAIR_PROV_CONNECTING, COMFOAIR_PROV_RETRYING};
  for (comfoair_prov_state_t state : attempting) {
    comfoair_phase_inputs_t in = online();
    in.prov_state = state;
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_CONNECTING, comfoair_phase_derive(&in));
  }
}

void test_unconfigured_broker_is_online_not_no_broker(void) {
  // Why "no broker" is not a degradation: comfoair_phase.h:93-95.
  comfoair_phase_inputs_t in = online();
  in.broker_configured = false;
  in.broker_connected  = false;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, comfoair_phase_derive(&in));
}

void test_configured_but_unreachable_broker_is_no_broker(void) {
  comfoair_phase_inputs_t in = online();
  in.broker_connected = false;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NO_BROKER, comfoair_phase_derive(&in));
}

void test_a_protocol_refusal_shares_the_phase_but_not_the_class(void) {
  // Why all four are one phase, and why the failure class still separates 1/2 from 4/5:
  // comfoair_phase.h:172-181. That split belongs to comfoair_mqtt's classifier and is asserted in
  // its own suite -- this component decides only the look, and a look is not a state.
  const uint32_t refusals[] = {1, 2, 4, 5};
  for (uint32_t code : refusals) {
    comfoair_phase_inputs_t in = online();
    in.broker_rejected  = comfoair_phase_connack_is_refusal(code);
    in.broker_connected = false;
    TEST_ASSERT_TRUE_MESSAGE(in.broker_rejected, "a CONNACK refusal was not read as one");
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BROKER_REJECTED, comfoair_phase_derive(&in));
  }
}

void test_connack_three_is_not_rejected(void) {
  // Why 3 is not a refusal: comfoair_phase.h:172-181. Code 0 is here from the other side of the
  // same rule -- no refusal was recorded -- and both must leave the phase on the fast ladder.
  const uint32_t not_refusals[] = {0, 3};
  for (uint32_t code : not_refusals) {
    comfoair_phase_inputs_t in = online();
    in.broker_rejected  = comfoair_phase_connack_is_refusal(code);
    in.broker_connected = false;
    TEST_ASSERT_FALSE_MESSAGE(in.broker_rejected, "a temporary answer was read as a refusal");
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_NO_BROKER, comfoair_phase_derive(&in));
  }
}

void test_an_unknown_connack_code_is_a_refusal(void) {
  // The open end of the rule (comfoair_phase.h:172-181): a broker that answered with a code
  // outside the specification answered no, and reading it as "not a refusal" would leave the
  // device waiting in NO_BROKER for ever.
  const uint32_t unknown[] = {6, 7, 42, 255, 0xFFFFFFFFu};
  for (uint32_t code : unknown) {
    comfoair_phase_inputs_t in = online();
    in.broker_rejected  = comfoair_phase_connack_is_refusal(code);
    in.broker_connected = false;
    TEST_ASSERT_TRUE_MESSAGE(in.broker_rejected, "an out-of-range CONNACK was not a refusal");
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BROKER_REJECTED, comfoair_phase_derive(&in));
  }
}

void test_broker_rejected_outranks_no_broker_while_disconnected(void) {
  // After a refusal the retry is 300 s (docs/interfaces/mqtt.md, Reconnect and backoff), so
  // connected is false for five minutes at a stretch.
  // Without the latch outranking NO_BROKER the phase would slide into "check your broker" while
  // the broker has already said what is wrong.
  comfoair_phase_inputs_t in = online();
  in.broker_rejected  = true;
  in.broker_connected = false;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BROKER_REJECTED, comfoair_phase_derive(&in));
}

void test_can_driver_that_did_not_start_is_fault_not_bus_silent(void) {
  // Why FAULT and BUS_SILENT are disjoint: comfoair_phase.h:66-69.
  comfoair_phase_inputs_t in = online();
  in.can_started = false;
  in.bus_started = false;
  in.bus_silent  = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_FAULT, comfoair_phase_derive(&in));
}

void test_a_stopped_driver_is_not_a_silent_bus(void) {
  // can_started is app_main's one-time verdict and bus_started is the driver's state now
  // (comfoair_phase.h:135-147). They diverge only after comfoair_can_stop() on a running device,
  // and this case is that divergence: the start branch succeeded, so FAULT is false, and the
  // driver is down, so a stale bus_silent must NOT become "check the loom" for a fault of ours.
  //
  // A WITNESS, NOT THE ONLY GUARD: deleting the `bus_started &&` term also fails the priority
  // sweep, which mirrors it. This case is the one that says out loud which situation that is.
  comfoair_phase_inputs_t in = online();
  in.can_started = true;
  in.bus_started = false;
  in.bus_silent  = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_ONLINE, comfoair_phase_derive(&in));
}

void test_a_bus_that_is_up_and_quiet_is_bus_silent(void) {
  comfoair_phase_inputs_t in = online();
  in.bus_silent = true;
  TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_BUS_SILENT, comfoair_phase_derive(&in));
}

void test_each_startup_failure_reaches_fault(void) {
  // One case per start branch of app_main (the seven behind `fault` in docs/device/indication.md),
  // INCLUDING comfoair_mqtt_start(): the queue and the client are different failures, and a device
  // whose queue exists but whose client was never built would otherwise report NO_BROKER -- "check
  // your broker" -- where the true answer is "the firmware could not". That is the very lie
  // last_connack was introduced to end.
  bool comfoair_phase_inputs_t::*branches[] = {
      &comfoair_phase_inputs_t::can_started,   &comfoair_phase_inputs_t::poll_started,
      &comfoair_phase_inputs_t::control_started, &comfoair_phase_inputs_t::net_started,
      &comfoair_phase_inputs_t::mqtt_ready,    &comfoair_phase_inputs_t::mqtt_client_started,
      &comfoair_phase_inputs_t::http_started,
  };
  TEST_ASSERT_EQUAL_INT(7, (int)(sizeof(branches) / sizeof(branches[0])));
  for (bool comfoair_phase_inputs_t::*branch : branches) {
    comfoair_phase_inputs_t in = online();
    in.*branch                 = false;
    TEST_ASSERT_EQUAL_INT(COMFOAIR_PHASE_FAULT, comfoair_phase_derive(&in));
  }
}

// --- projections --------------------------------------------------------------------------------

void test_every_phase_has_a_name(void) {
  // The name contract of comfoair_phase.h:193-200, checked: never NULL, never empty, never the
  // same word twice, and never "unknown" -- that spelling belongs to numbers that are not phases,
  // and a phase that falls through to it is one somebody added without a line in the table.
  for (int p = 0; p < (int)COMFOAIR_PHASE_COUNT; ++p) {
    const char *name = comfoair_phase_name((device_phase_t)p);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_TRUE(name[0] != '\0');
    TEST_ASSERT_TRUE_MESSAGE(strcmp(name, "unknown") != 0,
                             "a phase reached the enumeration without a line in comfoair_phase_name");
    for (int earlier = 0; earlier < p; ++earlier) {
      TEST_ASSERT_TRUE_MESSAGE(
          strcmp(name, comfoair_phase_name((device_phase_t)earlier)) != 0,
          "two phases share a spelling, so the document cannot tell them apart");
    }
  }
}

void test_names_are_the_spelling_the_spa_expects(void) {
  // Named one by one and not generated: a table that generated the expected strings from the same
  // source as the implementation would rename a phase in the API without anybody noticing.
  TEST_ASSERT_EQUAL_STRING("boot", comfoair_phase_name(COMFOAIR_PHASE_BOOT));
  TEST_ASSERT_EQUAL_STRING("ota", comfoair_phase_name(COMFOAIR_PHASE_OTA));
  TEST_ASSERT_EQUAL_STRING("trial", comfoair_phase_name(COMFOAIR_PHASE_TRIAL));
  TEST_ASSERT_EQUAL_STRING("setup-ap", comfoair_phase_name(COMFOAIR_PHASE_SETUP_AP));
  TEST_ASSERT_EQUAL_STRING("recovery-ap", comfoair_phase_name(COMFOAIR_PHASE_RECOVERY_AP));
  TEST_ASSERT_EQUAL_STRING("fault", comfoair_phase_name(COMFOAIR_PHASE_FAULT));
  TEST_ASSERT_EQUAL_STRING("net-rejected", comfoair_phase_name(COMFOAIR_PHASE_NET_REJECTED));
  TEST_ASSERT_EQUAL_STRING("net-lost", comfoair_phase_name(COMFOAIR_PHASE_NET_LOST));
  TEST_ASSERT_EQUAL_STRING("setup-expired", comfoair_phase_name(COMFOAIR_PHASE_SETUP_EXPIRED));
  TEST_ASSERT_EQUAL_STRING("connecting", comfoair_phase_name(COMFOAIR_PHASE_CONNECTING));
  TEST_ASSERT_EQUAL_STRING("broker-rejected", comfoair_phase_name(COMFOAIR_PHASE_BROKER_REJECTED));
  TEST_ASSERT_EQUAL_STRING("bus-silent", comfoair_phase_name(COMFOAIR_PHASE_BUS_SILENT));
  TEST_ASSERT_EQUAL_STRING("no-broker", comfoair_phase_name(COMFOAIR_PHASE_NO_BROKER));
  TEST_ASSERT_EQUAL_STRING("online", comfoair_phase_name(COMFOAIR_PHASE_ONLINE));
  // Out of range is answered, not crashed: the number reaches here from a JSON document.
  TEST_ASSERT_NOT_NULL(comfoair_phase_name((device_phase_t)COMFOAIR_PHASE_COUNT));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_no_input_combination_falls_through_to_no_phase);
  RUN_TEST(test_every_bit_of_the_sweep_moves_a_flag_of_its_own);
  RUN_TEST(test_every_phase_is_reachable_from_some_input);
  RUN_TEST(test_lower_numbered_phase_wins_when_both_conditions_hold);
  RUN_TEST(test_the_phase_enumeration_has_fourteen_values);
  RUN_TEST(test_null_inputs_are_boot_not_online);
  RUN_TEST(test_machine_not_ready_outranks_everything_including_ota);
  RUN_TEST(test_a_refused_connack_outranks_a_silent_bus);
  RUN_TEST(test_missing_network_outranks_a_silent_bus);
  RUN_TEST(test_access_point_on_air_outranks_a_startup_fault);
  RUN_TEST(test_trial_outranks_the_access_point_it_runs_underneath);
  RUN_TEST(test_ota_outranks_the_open_access_point);
  RUN_TEST(test_unconfigured_access_point_is_setup_not_recovery);
  RUN_TEST(test_fallback_access_point_is_recovery_not_setup);
  RUN_TEST(test_wrong_password_and_refused_are_net_rejected);
  RUN_TEST(test_network_absent_and_no_address_are_net_lost);
  RUN_TEST(test_window_closed_is_setup_expired_not_net_lost);
  RUN_TEST(test_retrying_and_connecting_are_one_phase);
  RUN_TEST(test_unconfigured_broker_is_online_not_no_broker);
  RUN_TEST(test_configured_but_unreachable_broker_is_no_broker);
  RUN_TEST(test_a_protocol_refusal_shares_the_phase_but_not_the_class);
  RUN_TEST(test_connack_three_is_not_rejected);
  RUN_TEST(test_an_unknown_connack_code_is_a_refusal);
  RUN_TEST(test_broker_rejected_outranks_no_broker_while_disconnected);
  RUN_TEST(test_can_driver_that_did_not_start_is_fault_not_bus_silent);
  RUN_TEST(test_a_stopped_driver_is_not_a_silent_bus);
  RUN_TEST(test_a_bus_that_is_up_and_quiet_is_bus_silent);
  RUN_TEST(test_each_startup_failure_reaches_fault);
  RUN_TEST(test_every_phase_has_a_name);
  RUN_TEST(test_names_are_the_spelling_the_spa_expects);
  RUN_TEST(test_derive_takes_no_clock);
  RUN_TEST(test_the_monitor_asks_the_aggregate_and_never_decides_on_its_own);
  RUN_TEST(test_a_bus_that_never_spoke_is_not_a_silent_bus);
  RUN_TEST(test_a_bus_that_stopped_speaking_reaches_the_phase);
  RUN_TEST(test_the_provocation_leaves_the_collector_as_one_bool);
  RUN_TEST(test_a_broker_that_has_just_dropped_is_still_online);
  RUN_TEST(test_a_broker_gone_for_the_hold_off_is_no_broker);
  RUN_TEST(test_a_broker_that_came_back_is_granted_the_hold_off_again);
  RUN_TEST(test_the_hold_off_survives_the_millisecond_wrap);
  RUN_TEST(test_a_refusal_needs_no_hold_off_at_all);
  RUN_TEST(test_a_server_unavailable_connack_is_not_a_refusal_here_either);
  RUN_TEST(test_each_start_branch_lands_in_the_field_that_names_it);
  RUN_TEST(test_a_driver_stopped_after_it_started_is_not_a_startup_fault);
  RUN_TEST(test_a_transition_is_reported_once_and_a_steady_phase_never);
  RUN_TEST(test_a_pass_with_nothing_to_say_is_answered_and_not_crashed);
  return UNITY_END();
}
