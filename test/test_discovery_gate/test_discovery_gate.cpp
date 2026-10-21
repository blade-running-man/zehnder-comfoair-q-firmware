// WHEN THE DISCOVERY TREE IS WRITTEN AGAIN, AS THREE RULES A TEST CAN CALL.
//
// The subject is the discovery gate (docs/interfaces/home-assistant-discovery.md, When discovery is
// published): one bool, not a fingerprint of the tree. The tree is one config document per entity, it
// used to go out on every reconnection, and it now goes out when one of three things has happened:
// nothing has ever published it on this client, the client has been rebuilt from the stored
// settings, or the publisher saw the link drop WHILE THE DEVICE STILL HAD AN ADDRESS.
//
// WHY THE RULES ARE HERE AND NOT WHERE THEY ARE USED. comfoair_mqtt is esp-mqtt, FreeRTOS and a
// client handle, so [env:native] does not build it at all: a condition written inside it is
// checked by nothing, which is exactly how the retraction ceiling came to be deletable with 770
// green cases. What CANNOT come here is the call sites -- the clock, the address and the client
// handle -- and those are hardware scenarios HV-HA-08 to HV-HA-12
// (docs/hardware-verification/06b-home-assistant.md).
//
// THE COST OF THE TWO MISTAKES IS NOT THE SAME, and that is why the cases below are not
// symmetrical. Never clearing means a device that joins a broker with an empty retained store,
// publishes every state and publishes no config document at all: NOTHING APPEARS IN HOME
// ASSISTANT, on a device with nothing wrong with it. Clearing on every drop means the whole tree per
// reconnection -- the cost this exists to remove, and only a cost. Both are pinned, separately, so
// that a mutation in either direction fails with a case that says which direction it was.
#include <unity.h>

#include <cstdio>

#include "comfoair_discovery_gate.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// The state a device is in while it is working: discovery has gone out and the last pass saw the
// session up. Every rule below is asked from here, because this is the only state in which any of
// them can do damage -- from a zeroed gate discovery is due anyway.
constexpr comfoair_discovery_gate_t WORKING = {true, true};

comfoair_discovery_gate_t pass(comfoair_discovery_gate_t gate, bool connected, bool has_address)
{
  return comfoair_discovery_gate(gate, COMFOAIR_DISCOVERY_PASS, connected, has_address);
}

// The two events that are things the caller DID rather than things it saw. Both take the two
// observations too, and both must ignore them -- see the case that pins it.
comfoair_discovery_gate_t sent(comfoair_discovery_gate_t gate) {
  return comfoair_discovery_gate(gate, COMFOAIR_DISCOVERY_SENT, true, true);
}
comfoair_discovery_gate_t rebuilt(comfoair_discovery_gate_t gate) {
  return comfoair_discovery_gate(gate, COMFOAIR_DISCOVERY_REBUILT, false, true);
}

}  // namespace

// --- rule one: it is published once, and then it is not -----------------------------------------

void test_a_device_that_has_published_nothing_owes_discovery(void) {
  const comfoair_discovery_gate_t fresh = {};
  TEST_ASSERT_TRUE(comfoair_discovery_due(fresh));
}

void test_a_sweep_that_completed_is_not_swept_again(void) {
  // THE WHOLE POINT OF THE TASK, in one line: the second connection sends nothing. Without this
  // the 154-message burst is back and nothing else in this suite would notice.
  TEST_ASSERT_FALSE(comfoair_discovery_due(sent(comfoair_discovery_gate_t{})));
}

void test_publishing_twice_over_is_still_published(void) {
  // Idempotent, because the caller is a loop: republish_everything() runs on every connection and
  // asks the gate every time.
  TEST_ASSERT_FALSE(comfoair_discovery_due(sent(sent(comfoair_discovery_gate_t{}))));
}

// --- rule two: a rebuilt client has a new identity ----------------------------------------------

void test_a_rebuilt_client_owes_discovery_again(void) {
  // comfoair_mqtt_start_client() rewrites all six fields of comfoair_ha_ctx_t, so every config
  // document this device has ever published describes something it no longer is. This is the rule
  // that makes a fingerprint unnecessary -- and the one a fingerprint would get WRONG, because the
  // broker's address is not among those six fields.
  TEST_ASSERT_TRUE(comfoair_discovery_due(rebuilt(WORKING)));
}

void test_a_rebuild_forgets_the_session_as_well_as_the_publication(void) {
  // The old client is stopped before the new one is built, so a `link` left true would make the
  // first pass of the new client read as an edge -- a drop of a session that no longer exists.
  // Harmless today, because the flag it would clear is already clear; pinned because the harm
  // arrives silently the day the order of the two rules changes.
  //
  // ASSERTED ON THE REBUILD ITSELF AND NOT ON THE PASS AFTER IT. It was written the other way
  // round first -- `pass(rebuilt(WORKING), false, true)` -- and the mutation that stops the
  // rebuild clearing `link` PASSED all thirteen cases: the pass that followed set `link` from its
  // own argument, so the assertion could not see what the rebuild had left behind. A guard that
  // checks the state after the next event checks the next event.
  const comfoair_discovery_gate_t after = rebuilt(WORKING);
  TEST_ASSERT_TRUE(comfoair_discovery_due(after));
  TEST_ASSERT_FALSE(after.link);
}

// --- rule three: whose fault was the drop -------------------------------------------------------

void test_a_drop_with_an_address_is_the_brokers_and_discovery_is_owed(void) {
  // THE EXPENSIVE MISTAKE IS HERE. mosquitto was restarted; it may have come back with an empty
  // retained store. If this case stops clearing, the owner's device connects, publishes 79
  // messages of state, and Home Assistant shows NOTHING -- with every light on the box green.
  TEST_ASSERT_TRUE(comfoair_discovery_due(pass(WORKING, false, true)));
}

void test_a_drop_with_no_address_is_ours_and_the_retained_store_is_intact(void) {
  // The router went away. The broker never noticed us leave, its retained store still holds every
  // config document, and republishing them would be the burst this task exists to remove.
  TEST_ASSERT_FALSE(comfoair_discovery_due(pass(WORKING, false, false)));
}

void test_the_address_coming_back_after_our_own_outage_publishes_nothing(void) {
  // THE ROUTER REBOOT, WHICH IS THE CASE THE WHOLE THING IS FOR, and the reason the rule is an
  // EDGE and not a level. The address goes first (lwip aborts the TCP connections when the
  // interface loses its address), so the pass that sees the drop sees no address; the passes after
  // it see the address return while the broker is still not back. A rule written as
  // "not connected and an address" would clear on every one of those and put the burst back on
  // exactly the failure it was written to make cheap.
  comfoair_discovery_gate_t gate = pass(WORKING, false, false);
  for (int i = 0; i < 5; i++)
    gate = pass(gate, false, true);
  TEST_ASSERT_FALSE(comfoair_discovery_due(gate));
  // And it survives the reconnection itself, which is the pass the owner sees in the log.
  TEST_ASSERT_FALSE(comfoair_discovery_due(pass(gate, true, true)));
}

void test_an_outage_that_outlives_the_address_still_owes_nothing_when_it_ends(void) {
  // The same shape, with the address flapping the other way round: nothing after the first pass of
  // an outage may reopen the question, because the verdict was taken at the drop.
  comfoair_discovery_gate_t gate = pass(WORKING, false, false);
  gate                           = pass(gate, false, true);
  gate                           = pass(gate, false, false);
  gate                           = pass(gate, false, true);
  TEST_ASSERT_FALSE(comfoair_discovery_due(gate));
}

void test_a_healthy_connection_is_never_asked_to_publish_again(void) {
  // A thousand ordinary passes with the link up. If the drop test lost its `!connected` term this
  // is what would fail, and it would fail as a whole-tree burst every second on a device that is fine.
  comfoair_discovery_gate_t gate = WORKING;
  for (int i = 0; i < 1000; i++)
    gate = pass(gate, true, true);
  TEST_ASSERT_FALSE(comfoair_discovery_due(gate));
}

void test_two_broker_restarts_are_answered_twice(void) {
  // The flag is not a one-shot: it goes back up when discovery is published again, and a second
  // broker restart owes a second sweep.
  comfoair_discovery_gate_t gate = pass(WORKING, false, true);
  gate                           = sent(pass(gate, true, true));
  TEST_ASSERT_FALSE(comfoair_discovery_due(gate));
  TEST_ASSERT_TRUE(comfoair_discovery_due(pass(gate, false, true)));
}

// --- the three conditions, all eight ways -------------------------------------------------------

void test_every_one_of_the_three_conditions_is_load_bearing(void) {
  // THE TRUTH TABLE, because a scenario test passes for the right reason and for two wrong ones.
  // Exactly ONE of the eight combinations may clear the flag: a session the previous pass saw, no
  // session now, and an address. Any term dropped from the condition turns a different row green
  // here, and the row names which term it was.
  for (int bits = 0; bits < 8; bits++) {
    const bool link      = (bits & 4) != 0;
    const bool connected = (bits & 2) != 0;
    const bool address   = (bits & 1) != 0;

    const comfoair_discovery_gate_t before = {true, link};
    const bool expected = link && !connected && address;
    char       what[64];
    snprintf(what, sizeof what, "link=%d connected=%d address=%d", link, connected, address);
    TEST_ASSERT_EQUAL_MESSAGE(expected, comfoair_discovery_due(pass(before, connected, address)),
                              what);
  }
}

void test_a_pass_records_the_session_it_saw_and_nothing_else_does(void) {
  // `link` is the memory the edge is measured against, so the pass is its only writer -- and SENT
  // must not become a second one. A SENT that wrote `link` from its own arguments would record a
  // session at the moment discovery went out rather than at the moment the pass looked, and the
  // next drop would be judged against the wrong instant.
  TEST_ASSERT_TRUE(pass(comfoair_discovery_gate_t{}, true, false).link);
  TEST_ASSERT_FALSE(pass(WORKING, false, true).link);
  const comfoair_discovery_gate_t idle = {false, false};
  TEST_ASSERT_FALSE(sent(idle).link);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_a_device_that_has_published_nothing_owes_discovery);
  RUN_TEST(test_a_sweep_that_completed_is_not_swept_again);
  RUN_TEST(test_publishing_twice_over_is_still_published);

  RUN_TEST(test_a_rebuilt_client_owes_discovery_again);
  RUN_TEST(test_a_rebuild_forgets_the_session_as_well_as_the_publication);

  RUN_TEST(test_a_drop_with_an_address_is_the_brokers_and_discovery_is_owed);
  RUN_TEST(test_a_drop_with_no_address_is_ours_and_the_retained_store_is_intact);
  RUN_TEST(test_the_address_coming_back_after_our_own_outage_publishes_nothing);
  RUN_TEST(test_an_outage_that_outlives_the_address_still_owes_nothing_when_it_ends);
  RUN_TEST(test_a_healthy_connection_is_never_asked_to_publish_again);
  RUN_TEST(test_two_broker_restarts_are_answered_twice);

  RUN_TEST(test_every_one_of_the_three_conditions_is_load_bearing);
  RUN_TEST(test_a_pass_records_the_session_it_saw_and_nothing_else_does);
  return UNITY_END();
}
