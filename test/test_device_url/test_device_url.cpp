// `configuration_url` on the Home Assistant device card: the one field of comfoair_ha_ctx_t whose
// value is not known when the client is built.
//
// THE DEFECT THIS SUITE EXISTS FOR, reached independently from three ordinary situations: the
// router rebooted, the owner does not know the device's address, and the power to the house went
// out and came back. comfoair_mqtt_start_client() derives the url from
// comfoair_net_ip_string() ONCE, and it is called from app_main at a moment when there is no
// address yet: src/main.cpp starts the HTTP server and MQTT back to back, and esp-mqtt retries on
// its own without waiting for the network. So the device block is published with no `cu` member,
// and nothing ever writes it again -- the card in Home Assistant has no link to the device's own
// page after every power cut, for the life of the installation.
//
// A NEW DIRECTORY, AND FOR THE REASON test_wire_epoch GIVES ABOUT ITS OWN: the natural home,
// test/test_ha_cleanup/test_ha_cleanup.cpp, stands at 595 of the 600 lines AGENTS.md allows
// a Unity suite, and PlatformIO links every .cpp in a directory into one binary, so a second file
// beside it would collide on main().
//
// WHY THERE IS A PURE FUNCTION HERE AT ALL, rather than four lines inside comfoair_mqtt_client.c
// where the buffer is: [env:native] does not build comfoair_mqtt -- it is esp-mqtt, FreeRTOS and a
// client handle -- so a condition written in there is checked by NOTHING. That is not a
// hypothetical in this repository; it is the finding comfoair_retract_budget.c and
// comfoair_discovery_gate.c were both extracted for, and in both cases deleting the condition
// altogether left every host case green.
//
// WHAT THIS SUITE CANNOT SEE, stated here because the report says it too: it checks what the
// firmware would put in the buffer and when it would say "the card moved". It sees nothing of the
// call site -- that comfoair_mqtt_retry.c passes the address it has just read, that
// comfoair_mqtt_client.c lowers the discovery flag on a true, or that g_ctx.url points into the
// buffer that was rewritten. Those three are hardware scenario HV-HA-13
// (docs/hardware-verification/06b-home-assistant.md).
#include <unity.h>

#include <cstring>
#include <string>

#include "comfoair_device_url.h"

void setUp(void) {}
void tearDown(void) {}

namespace {

// The buffer the client actually carries, so that a case which passes here cannot fail on the
// device for a size this file made up.
struct card {
  char url[COMFOAIR_DEVICE_URL_MAX];
};

card empty_card(void) {
  card c = {};
  c.url[0] = '\0';
  return c;
}

}  // namespace

// THE CENTRAL CASE. An address that arrives after the client was built must reach the card,
// and "reach" means two things at once: the buffer now holds a usable url, and the caller is told
// so -- a true is what lowers the discovery flag, and without it the new url sits in a buffer
// nobody republishes from.
void test_an_address_arriving_after_the_client_was_built_reaches_the_device_card(void) {
  card c = empty_card();

  TEST_ASSERT_TRUE_MESSAGE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.50"),
                           "the address arrived and the card was not told to move");
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/", c.url);
}

// THE SCHEME AND THE TRAILING SLASH ARE NOT DECORATION. cv.configuration_url REJECTS a value with
// no scheme (comfoair_ha.h:76-79), and Home Assistant refuses the whole device block when one
// member of it fails validation -- which takes every entity with it. A "simplification" to the
// bare address is therefore not a cosmetic regression but a device that vanishes.
void test_the_url_carries_the_scheme_home_assistant_refuses_to_do_without(void) {
  card c = empty_card();
  TEST_ASSERT_TRUE(comfoair_device_url_refresh(c.url, sizeof c.url, "10.0.0.7"));

  const std::string url(c.url);
  TEST_ASSERT_EQUAL_STRING_MESSAGE("http://", url.substr(0, 7).c_str(),
                                   "a url with no scheme is refused by cv.configuration_url");
  TEST_ASSERT_TRUE_MESSAGE(url.find("10.0.0.7") != std::string::npos,
                           "the url does not contain the address it was built from");
  TEST_ASSERT_EQUAL_MESSAGE('/', url[url.size() - 1],
                            "the url must end at the root of the device's own web server");
}

// THE ONCE-ONLY PROPERTY, AND IT IS THE HALF THAT IS EASY TO GET WRONG. The caller is the retry
// pass, which runs on every wake of the publisher task -- several times a second on a busy CAN
// bus. Written as a level ("there is an address, so refresh"), the fix republishes the whole
// discovery tree on every one of those passes: one config document per entity, in a circle, for
// ever. So an address that has not moved must answer false, and it must go on answering false.
void test_a_second_pass_with_the_same_address_publishes_nothing_again(void) {
  card c = empty_card();
  TEST_ASSERT_TRUE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.50"));

  // A thousand passes is about six minutes of a busy publisher. Not one of them may move the card.
  for (int i = 0; i < 1000; i++) {
    char msg[96];
    snprintf(msg, sizeof msg, "pass %d republished the whole discovery tree for an address that "
                              "had not moved", i);
    TEST_ASSERT_FALSE_MESSAGE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.50"),
                              msg);
  }
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/", c.url);
}

// THE OTHER EDGE, and it is the one that decides the shape of the condition. "Rebuild when the url
// is EMPTY" answers this case wrong: a router rebooted, the lease came back on a different
// address, and the card keeps a link to a device that is no longer there -- for ever, because the
// url is not empty any more. The condition is therefore "the url MOVED", which covers the empty
// case as one of its instances and cannot freeze on the first address.
void test_a_router_that_hands_out_a_different_address_moves_the_card(void) {
  card c = empty_card();
  TEST_ASSERT_TRUE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.50"));

  TEST_ASSERT_TRUE_MESSAGE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.51"),
                           "the lease moved and the card was left pointing at the old address");
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.51/", c.url);
  // And the new address settles in exactly the same way the first one did: once, not for ever.
  TEST_ASSERT_FALSE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.51"));
}

// AN ADDRESS THAT GOES AWAY IS NOT A CARD THAT CHANGES, and this is a decision rather than an
// omission. A device whose Wi-Fi drops has no way to publish anything at that moment anyway, so
// blanking the url would cost two full discovery sweeps for one blip -- one for the card with no
// link, one for the card when the address returns -- and in between it would take away the link at
// exactly the moment the owner is trying to reach the device to find out what is wrong. The url is
// the LAST KNOWN address, and on a household router the returning lease is almost always the same
// one, in which case the second sweep would have been paid for nothing at all.
void test_an_address_that_goes_away_leaves_the_card_alone(void) {
  card c = empty_card();
  TEST_ASSERT_TRUE(comfoair_device_url_refresh(c.url, sizeof c.url, "192.168.1.50"));

  TEST_ASSERT_FALSE_MESSAGE(comfoair_device_url_refresh(c.url, sizeof c.url, ""),
                            "a Wi-Fi blip republished the discovery tree");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("http://192.168.1.50/", c.url,
                                   "the last known address was erased while the link was down");

  // comfoair_net_ip_string() writes an empty string and never a NULL, but a caller that has
  // nothing to say is answered rather than dereferenced -- the rule comfoair_mqtt_backoff_seed()
  // already carries in this component.
  TEST_ASSERT_FALSE(comfoair_device_url_refresh(c.url, sizeof c.url, nullptr));
  TEST_ASSERT_EQUAL_STRING("http://192.168.1.50/", c.url);
}

// AND WITH NO ADDRESS THERE WAS NEVER A CARD TO MOVE. The state every device boots in: the client
// is built before the network is up, so the first passes see an empty buffer and an empty address.
// Answering true there would republish the whole tree into a broker we are not even connected to, on every
// pass, for as long as the Wi-Fi takes to come up.
void test_no_address_and_no_url_is_not_a_change(void) {
  card c = empty_card();
  TEST_ASSERT_FALSE(comfoair_device_url_refresh(c.url, sizeof c.url, ""));
  TEST_ASSERT_EQUAL_STRING("", c.url);
}

// THE BUFFER IS EXACTLY THE LONGEST URL THIS CAN PRODUCE, and the case exists because it is one
// byte from being wrong. "http://" is 7, the longest IPv4 text is 15, the trailing slash is 1 and
// the terminator is 1: 24. A buffer sized 23 by somebody counting the slash twice would silently
// refuse the whole 255.255.255.255 half of the address space, and only there.
void test_the_buffer_holds_the_longest_address_a_router_can_hand_out(void) {
  TEST_ASSERT_EQUAL_size_t_MESSAGE(sizeof "http://255.255.255.255/", COMFOAIR_DEVICE_URL_MAX,
                                   "COMFOAIR_DEVICE_URL_MAX no longer holds the longest IPv4 url");

  card c = empty_card();
  TEST_ASSERT_TRUE(comfoair_device_url_refresh(c.url, sizeof c.url, "255.255.255.255"));
  TEST_ASSERT_EQUAL_STRING("http://255.255.255.255/", c.url);
}

// A URL THAT DOES NOT FIT IS ABSENT, NEVER HALF-WRITTEN. snprintf's own answer to a short buffer
// is a truncation, and "http://192.168.1" is a link Home Assistant accepts, renders and sends the
// owner to nowhere. An ABSENT `cu` is the documented, harmless state (comfoair_ha_payload.c:81-83
// omits the member); a wrong one is a support call. The caller is told false as well, because a
// card that did not change must not be republished.
void test_a_url_that_would_not_fit_is_absent_rather_than_truncated(void) {
  char small[12] = {};
  TEST_ASSERT_FALSE_MESSAGE(comfoair_device_url_refresh(small, sizeof small, "192.168.1.50"),
                            "a truncated url was reported as a card worth republishing");
  TEST_ASSERT_EQUAL_STRING_MESSAGE("", small, "a url was written in half");

  // And the previous, working url survives a buffer that cannot hold the new one, for the reason
  // the blip case above gives: the last known address beats a broken link.
  char nine[9] = "http://a";
  TEST_ASSERT_FALSE(comfoair_device_url_refresh(nine, sizeof nine, "192.168.1.50"));
  TEST_ASSERT_EQUAL_STRING("http://a", nine);

  // A caller with no buffer at all is answered, not dereferenced.
  TEST_ASSERT_FALSE(comfoair_device_url_refresh(nullptr, 24, "192.168.1.50"));
  TEST_ASSERT_FALSE(comfoair_device_url_refresh(small, 0, "192.168.1.50"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_an_address_arriving_after_the_client_was_built_reaches_the_device_card);
  RUN_TEST(test_the_url_carries_the_scheme_home_assistant_refuses_to_do_without);
  RUN_TEST(test_a_second_pass_with_the_same_address_publishes_nothing_again);
  RUN_TEST(test_a_router_that_hands_out_a_different_address_moves_the_card);
  RUN_TEST(test_an_address_that_goes_away_leaves_the_card_alone);
  RUN_TEST(test_no_address_and_no_url_is_not_a_change);
  RUN_TEST(test_the_buffer_holds_the_longest_address_a_router_can_hand_out);
  RUN_TEST(test_a_url_that_would_not_fit_is_absent_rather_than_truncated);
  return UNITY_END();
}
