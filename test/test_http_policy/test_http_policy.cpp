// Who may do what, and when.
//
// This is the access rule of docs/architecture/security.md (Who may do what) made executable, and
// it is a pure function on purpose: the rule has to be testable without a network stack, because
// "the device was reachable and unprotected for ninety seconds after a router outage" is exactly
// the class of defect that never shows up in a manual test.
//
// Every case below pins one rule of that section; read its reason there before changing one.
#include <unity.h>

#include "comfoair_http_policy.h"

static comfoair_http_ctx_t ctx(bool provisioned, bool password_set, bool authenticated) {
  comfoair_http_ctx_t c{};
  c.provisioned    = provisioned;
  c.password_set   = password_set;
  c.authenticated  = authenticated;
  return c;
}

void setUp(void) {}
void tearDown(void) {}

// --- reading ---------------------------------------------------------------------------------

void test_reading_is_open_when_no_password_is_set(void) {
  // The LAN is the trust boundary until the owner draws a tighter one. Reading ventilation
  // temperatures is not a capability worth locking a user out of their own device for.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/state", ctx(true, false, false)));
}

void test_reading_requires_the_password_once_one_is_set(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/state", ctx(true, true, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/state", ctx(true, true, true)));
}

// --- writing ---------------------------------------------------------------------------------

void test_writing_is_refused_while_no_password_exists(void) {
  // The hole the counter-review found: an OTA endpoint that answers before anyone has
  // claimed the device is unauthenticated code execution on the LAN, and the image is not
  // signed. Failing closed costs the owner one step and costs an attacker everything.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ota", ctx(true, false, false)));

  // /api/entities/fan USED TO BE ASSERTED HERE and is not any more. The rule changed on
  // 2026-09-04, by the owner's decision, and the reason is worth keeping next to the line that
  // moved: the UI password is optional and off by default, a device without one is a supported
  // configuration, and with only /api/config exempt those two decisions meant every ventilation
  // command answered 403 out of the box -- phase 7 dead on arrival, waiting for a password
  // nothing asked the owner to set. Changing the ventilation is now exempt; changing the DEVICE
  // is not, which is why /api/ota above is unmoved. See the tests further down for both halves.
}

void test_writing_is_allowed_once_authenticated(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ota", ctx(true, true, true)));
}

void test_setting_the_first_password_is_the_one_write_that_gets_through(void) {
  // Otherwise the device could never be secured: every write needs a password and the
  // password itself is a write. The exception is exactly one path and only while no
  // password exists -- afterwards it needs the old one like anything else.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/config",
                                        ctx(true, false, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/config",
                                        ctx(true, true, false)));
}

// --- the access point ------------------------------------------------------------------------

void test_an_unprovisioned_device_refuses_every_write(void) {
  // Radio proximity is not authorisation. On the first-run AP anyone in range can reach the
  // device, so it is read-only there -- including the config write that is otherwise the
  // bootstrap exception, because that is the path that hands the device to a network.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ota", ctx(false, false, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities/fan",
                                        ctx(false, false, false)));
}

void test_provisioning_itself_is_the_exception_on_the_access_point(void) {
  // Wi-Fi credentials have to be accepted somehow, or the device is a brick. This is the
  // single path that works there, and phase 5 puts a clock on the window it lives in.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/provision",
                                        ctx(false, false, false)));
}

void test_reading_still_works_on_the_access_point(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/", ctx(false, false, false)));
}

// --- paths ------------------------------------------------------------------------------------

void test_an_unknown_method_is_refused_rather_than_defaulted(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_OTHER, "/api/state", ctx(true, true, true)));
}

void test_a_path_that_only_starts_like_the_exception_is_not_the_exception(void) {
  // "/api/configuration" must not inherit "/api/config"'s bootstrap exemption, and
  // "/api/provisioning-secrets" must not inherit the AP one. Prefix matching is how this
  // kind of rule is usually got wrong.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/configuration",
                                        ctx(true, false, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/provisioning",
                                        ctx(false, false, false)));
}

void test_a_null_path_is_refused(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_GET, nullptr, ctx(true, false, false)));
}


// --- a device that already belongs to someone -------------------------------------------------
//
// The case with no test at all until now: NOT provisioned, but a password IS set. That is a
// device whose owner has claimed it and whose router has since died, sitting on its own
// access point with the owner's data still in it. The whole "radio proximity authorises
// nothing but handing it a network" argument was written for a device nobody owns yet, and
// it must not survive into this state -- otherwise a neighbour hands the device to their
// network and takes it out of the owner's house.

void test_provisioning_needs_the_password_once_one_exists(void) {
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_HTTP_UNAUTHORIZED,
                            comfoair_http_check(COMFOAIR_HTTP_POST, "/api/provision",
                                                ctx(false, true, false)),
                            "an owned device let a stranger re-home it");
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/provision",
                                        ctx(false, true, true)));
}

void test_an_unclaimed_device_still_accepts_a_network(void) {
  // The existing rule, restated so the fix above cannot quietly take it away: a device
  // nobody has claimed must accept credentials, or it is a brick.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/provision",
                                        ctx(false, false, false)));
}

void test_an_owned_device_on_its_own_access_point_still_refuses_everything_else(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ota", ctx(false, true, true)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/config", ctx(false, true, true)));
}

void test_reading_an_owned_device_on_its_access_point_needs_the_password(void) {
  // Otherwise the whole neighbourhood reads the house's ventilation, occupancy included,
  // for as long as the router is down.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/state", ctx(false, true, false)));
}


// --- controlling the ventilation on a device with no password ----------------------------------

void test_a_claimed_device_with_no_password_still_accepts_ventilation_commands(void) {
  // The UI password is OPTIONAL and off by default, and a device with no password is a
  // SUPPORTED configuration -- the unit sealed behind a front panel of a running ventilation unit.
  // Before this rule those two decisions made phase 7 dead on arrival: every command answered 403
  // until the owner set a password nothing told them to set.
  //
  // The trade is named rather than hidden: anybody on the household LAN can change the
  // ventilation. That is the same thing anybody who can walk up to the unit can already do, and it
  // is a different question from OTA -- which stays closed, because unauthenticated code execution
  // is not "changing the fan speed".
  const comfoair_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities/fan", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ops/boost", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/config", claimed));
}

void test_everything_else_still_fails_closed_without_a_password(void) {
  // The exemption is for the two command routes and nothing else. An OTA endpoint answering before
  // anyone has claimed the device is unauthenticated code execution on the LAN, and this firmware
  // does not verify image signatures.
  const comfoair_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ota", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/provision", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/", claimed));
}

void test_the_command_prefixes_are_matched_as_prefixes_and_not_as_the_word_before_them(void) {
  // A prefix rule is how an exemption leaks, so the bare stems must NOT inherit it, and neither
  // must a path that merely starts with the same letters.
  const comfoair_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities/", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ops", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities-secret", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/opsimulator", claimed));
}

void test_an_unclaimed_device_does_not_accept_commands_even_now(void) {
  // An open access point means unclaimed (docs/architecture/security.md): while one is up,
  // anybody in radio range is "the client". The one thing such a device accepts is a network to
  // join. Handing the ventilation of a house to whoever is outside it is not a trade anybody made.
  const comfoair_http_ctx_t unclaimed = {.provisioned = false, .password_set = false,
                                         .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities/fan", unclaimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ops/boost", unclaimed));
}

void test_once_a_password_exists_commands_need_it_like_everything_else(void) {
  // The exemption is for a device with NO password, not for the command routes. An owner who has
  // drawn a boundary gets it drawn around the ventilation too.
  const comfoair_http_ctx_t locked = {.provisioned = true, .password_set = true,
                                      .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/entities/fan", locked));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ops/boost", locked));

  const comfoair_http_ctx_t open = {.provisioned = true, .password_set = true,
                                    .authenticated = true};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/ops/boost", open));
}

// --- starting the RMI probe ---------------------------------------------------------------------
//
// POST /api/probe starts a READ-ONLY walk of the unit's settings. It shares the command routes'
// no-password exemption because reading the unit's configuration is no more than changing its fan
// speed, which that exemption already allows. It does not share anything else.

void test_starting_the_probe_needs_no_password_on_a_claimed_device_without_one(void) {
  const comfoair_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/probe", claimed));
}

void test_starting_the_probe_needs_the_password_once_one_exists(void) {
  const comfoair_http_ctx_t locked = {.provisioned = true, .password_set = true,
                                      .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/probe", locked));
  const comfoair_http_ctx_t open = {.provisioned = true, .password_set = true,
                                    .authenticated = true};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/probe", open));
}

void test_an_unclaimed_device_does_not_start_the_probe(void) {
  // The open access point accepts a network and nothing else -- the same rule as the commands.
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/probe",
                                        ctx(false, false, false)));
}

void test_the_probe_exemption_is_an_exact_path_and_not_a_prefix(void) {
  const comfoair_http_ctx_t claimed = {.provisioned = true, .password_set = false,
                                       .authenticated = false};
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/probe/x", claimed));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN,
                    comfoair_http_check(COMFOAIR_HTTP_POST, "/api/probes", claimed));
}

void test_reading_the_probe_report_follows_the_get_rule(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/probe", ctx(true, false, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/probe", ctx(true, true, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW,
                    comfoair_http_check(COMFOAIR_HTTP_GET, "/api/probe", ctx(true, true, true)));
}

// --- uploading firmware -------------------------------------------------------------------------

void test_firmware_upload_is_code_execution_and_is_gated_as_such(void) {
  // Images are unsigned (secure boot is off), so POST /api/ota runs whatever is sent. The four
  // states in one place, because the OTA handler relies on the policy for all of them.
  const comfoair_http_method_t P = COMFOAIR_HTTP_POST;
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN, comfoair_http_check(P, "/api/ota", ctx(true, false, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_UNAUTHORIZED, comfoair_http_check(P, "/api/ota", ctx(true, true, false)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_ALLOW, comfoair_http_check(P, "/api/ota", ctx(true, true, true)));
  TEST_ASSERT_EQUAL(COMFOAIR_HTTP_FORBIDDEN, comfoair_http_check(P, "/api/ota", ctx(false, false, false)));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_firmware_upload_is_code_execution_and_is_gated_as_such);
  RUN_TEST(test_reading_is_open_when_no_password_is_set);
  RUN_TEST(test_reading_requires_the_password_once_one_is_set);
  RUN_TEST(test_writing_is_refused_while_no_password_exists);
  RUN_TEST(test_writing_is_allowed_once_authenticated);
  RUN_TEST(test_setting_the_first_password_is_the_one_write_that_gets_through);
  RUN_TEST(test_an_unprovisioned_device_refuses_every_write);
  RUN_TEST(test_provisioning_itself_is_the_exception_on_the_access_point);
  RUN_TEST(test_reading_still_works_on_the_access_point);
  RUN_TEST(test_an_unknown_method_is_refused_rather_than_defaulted);
  RUN_TEST(test_a_path_that_only_starts_like_the_exception_is_not_the_exception);
  RUN_TEST(test_a_null_path_is_refused);
  RUN_TEST(test_provisioning_needs_the_password_once_one_exists);
  RUN_TEST(test_an_unclaimed_device_still_accepts_a_network);
  RUN_TEST(test_an_owned_device_on_its_own_access_point_still_refuses_everything_else);
  RUN_TEST(test_reading_an_owned_device_on_its_access_point_needs_the_password);
  RUN_TEST(test_a_claimed_device_with_no_password_still_accepts_ventilation_commands);
  RUN_TEST(test_everything_else_still_fails_closed_without_a_password);
  RUN_TEST(test_the_command_prefixes_are_matched_as_prefixes_and_not_as_the_word_before_them);
  RUN_TEST(test_an_unclaimed_device_does_not_accept_commands_even_now);
  RUN_TEST(test_once_a_password_exists_commands_need_it_like_everything_else);
  RUN_TEST(test_starting_the_probe_needs_no_password_on_a_claimed_device_without_one);
  RUN_TEST(test_starting_the_probe_needs_the_password_once_one_exists);
  RUN_TEST(test_an_unclaimed_device_does_not_start_the_probe);
  RUN_TEST(test_the_probe_exemption_is_an_exact_path_and_not_a_prefix);
  RUN_TEST(test_reading_the_probe_report_follows_the_get_rule);
  return UNITY_END();
}
