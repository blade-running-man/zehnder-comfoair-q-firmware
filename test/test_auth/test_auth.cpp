// Turning an Authorization header from an open network into a yes or a no.
//
// Every case below is written against attacker-controlled input, because that is what this
// header is: on the setup access point (open, deliberately) anyone in radio range can
// send one, and on a household LAN so can anything that has joined it. The parser therefore
// gets the malformed cases first and the working case last, which is the order they arrive in.
//
// The verifier is injected in every test here. That is not a testing convenience -- it is how
// the component is built (comfoair_auth.h, comfoair_auth_verifier_t), and it is what lets these
// tests state things about the DECISION that no crypto test could state: an always-true verifier
// still cannot unlock a device with no password set.
#include <unity.h>

// <cstdio> for snprintf in verify_recording. It builds without it today only because Unity's
// header pulls <stdio.h> in on its way to putchar (.pio/libdeps/native/Unity/src/
// unity_internals.h:322) -- a transitive include is not a dependency, and the day Unity stops
// needing putchar this file stops compiling for a reason that has nothing to do with it.
// test/test_config/test_config.cpp already includes it for the same call.
#include <cstdio>
#include <cstring>
#include <string>
#include <type_traits>

#include "comfoair_auth.h"
#include "comfoair_config.h"  // COMFOAIR_CONFIG_UI_PASS_MAX -- the length this must survive

// --- the seam no host build compiles ----------------------------------------------------------
//
// comfoair_auth_device.c is the one file that joins this component to comfoair_config, and it is
// behind #ifdef ESP_PLATFORM: [env:native] never compiles it, so a parameter added to or dropped
// from comfoair_config_check_ui_password_against() would be found only by a full device build.
// That is the slow way, and this component prefers the fast one -- comfoair_auth.c already ties
// the two password-length limits together with a _Static_assert rather than trusting them to stay
// equal. These are the same idea for the signature: what device_verify() binds to
// comfoair_auth_verifier_t, checked on the host, where both declarations ARE visible even though
// the file that uses them is not.
static_assert(std::is_same<comfoair_auth_verifier_t, bool (*)(const char *, const char *)>::value,
              "comfoair_auth_verifier_t changed shape and comfoair_auth_device.c binds to it");
static_assert(std::is_same<decltype(&comfoair_config_check_ui_password_against),
                           bool (*)(const char *, const char *, comfoair_config_kdf_t)>::value,
              "comfoair_config_check_ui_password_against drifted from the call "
              "comfoair_auth_device.c's device_verify() makes");

// --- an independent base64 encoder, so the cases read as plaintext -----------------------------
//
// Written here rather than reused from the component under test on purpose: a test that encoded
// with the same table it decodes with would agree with itself about a wrong table. It is pinned
// against RFC 7617's own worked example below before anything else uses it.
static std::string b64(const std::string &in)
{
    static const char *A = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string        out;
    size_t             i = 0;
    for (; i + 2 < in.size(); i += 3) {
        const unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8) |
                           (unsigned char)in[i + 2];
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += A[v & 63];
    }
    if (i + 1 == in.size()) {
        const unsigned v = (unsigned char)in[i] << 16;
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += "==";
    } else if (i + 2 == in.size()) {
        const unsigned v = ((unsigned char)in[i] << 16) | ((unsigned char)in[i + 1] << 8);
        out += A[(v >> 18) & 63];
        out += A[(v >> 12) & 63];
        out += A[(v >> 6) & 63];
        out += '=';
    }
    return out;
}

static std::string basic(const std::string &user_colon_password)
{
    return "Basic " + b64(user_colon_password);
}

// --- injected verifiers ------------------------------------------------------------------------

static char   s_seen_stored[256];
static char   s_seen_candidate[256];
// Every verifier below counts, so that "the verifier was never asked" is assertable whichever
// one a case needed. Cases that turn on NOT reaching the verifier are the ones this component
// exists for.
static size_t s_verify_calls;

// Says yes to everything. Used wherever the point is that something OTHER than the verifier
// refused: an unset password, an empty candidate, a header that never parsed.
static bool verify_always(const char *stored, const char *candidate)
{
    (void)stored;
    (void)candidate;
    s_verify_calls++;
    return true;
}

static bool verify_never(const char *stored, const char *candidate)
{
    (void)stored;
    (void)candidate;
    s_verify_calls++;
    return false;
}

static bool verify_recording(const char *stored, const char *candidate)
{
    s_verify_calls++;
    snprintf(s_seen_stored, sizeof s_seen_stored, "%s", stored ? stored : "(null)");
    snprintf(s_seen_candidate, sizeof s_seen_candidate, "%s", candidate ? candidate : "(null)");
    // "the stored record" in these tests is a marker string, not a real record: what this
    // component is responsible for is handing the two values over unchanged, and what a real
    // record means is comfoair_config's business (comfoair_config_check_ui_password_against).
    return strcmp(candidate, "hunter2hunter2") == 0;
}

// A record-shaped marker. Any non-empty string means "a password is set" as far as this
// component is concerned -- it never looks inside.
static const char *const STORED = "1$10000$0123456789abcdef0123456789abcdef$"
                                  "0000000000000000000000000000000000000000000000000000000000000000";

void setUp(void)
{
    memset(s_seen_stored, 0, sizeof s_seen_stored);
    memset(s_seen_candidate, 0, sizeof s_seen_candidate);
    s_verify_calls = 0;
}
void tearDown(void) {}

// --- the encoder this file leans on ------------------------------------------------------------

void test_the_test_encoder_agrees_with_rfc_7617(void) {
  // RFC 7617 section 2's own example. Pins the helper before any case built with it is trusted;
  // without this every "the password came through" assertion below could be two wrongs agreeing.
  TEST_ASSERT_EQUAL_STRING("QWxhZGRpbjpvcGVuIHNlc2FtZQ==", b64("Aladdin:open sesame").c_str());
}

// --- parsing: the cases that are not a credential ----------------------------------------------

void test_a_missing_header_is_not_a_credential(void) {
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_ABSENT, comfoair_auth_parse(nullptr, &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_ABSENT, comfoair_auth_parse("", &creds));
  // Whitespace only is still nothing said, not something malformed: esp_http_server hands the
  // value on after the colon, and a client that sent "Authorization:" said nothing.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_ABSENT, comfoair_auth_parse("   ", &creds));
}

void test_a_scheme_that_is_not_basic_is_refused(void) {
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_NOT_BASIC, comfoair_auth_parse("Bearer abcdef", &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_NOT_BASIC, comfoair_auth_parse("Digest username=x", &creds));
  // Prefix, not scheme. "Basicx" starts with "Basic" and is a different scheme; matching by
  // strncmp would accept it, which is the usual way a check like this leaks.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_NOT_BASIC, comfoair_auth_parse("Basicx dXNlcjpwdw==", &creds));
}

void test_the_scheme_with_no_credential_after_it_is_malformed_not_a_foreign_scheme(void) {
  // Where the line between the two answers falls, and it is worth stating because both readings
  // are defensible. Once the scheme WORD is Basic, the client asked for Basic and everything
  // wrong after that is a broken Basic credential. NOT_BASIC stays for a scheme this device does
  // not implement -- the two lead to different questions, and a log that confuses "your client
  // is speaking Bearer at me" with "your client sent an empty password" answers neither.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic", &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic   ", &creds));
}

void test_the_scheme_name_is_case_insensitive(void) {
  // RFC 7617 section 2: the scheme name is case-insensitive. Real browsers send "Basic", but
  // curl scripts and HTTP libraries do not all agree, and an owner locked out by the case of a
  // word they never typed has nothing to look at.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(("basic " + b64("u:hunter2hunter2")).c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("hunter2hunter2", creds.password);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(("BASIC " + b64("u:hunter2hunter2")).c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("hunter2hunter2", creds.password);
}

void test_base64_that_is_not_base64_is_refused(void) {
  comfoair_auth_credentials_t creds;
  // Outside the alphabet.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic !!!!", &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic ****", &creds));
  // Not a multiple of four.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic YWJ", &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic YWJjZG", &creds));
  // Padding where padding cannot be. "=" is the end of the encoding and nothing follows it.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic YQ==YQ==", &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic Y=Jj", &creds));
  // Three pad characters encode nothing at all.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic Y===", &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic ====", &creds));
  // The one that is not merely untidy. This is b64("user:hunter2hunter2") with a '=' put over
  // the fourth character, and a decoder that treats '=' as a zero sextet anywhere -- the natural
  // thing to write, because it makes the padding fall out of the same table -- reads it as
  // "us@r:hunter2hunter2" and hands over a password that is exactly right. See
  // test_a_credential_smuggled_through_misplaced_padding_is_refused for why that matters.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED,
                    comfoair_auth_parse("Basic dXN=cjpodW50ZXIyaHVudGVyMg==", &creds));
  // Whitespace inside the token. Header folding is obsolete (RFC 7230 section 3.2.4) and
  // esp_http_server never delivers a folded value, so a space here is a second token and not a
  // wrapped one.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse("Basic YWJj ZGVm", &creds));
}

void test_a_decoded_blob_with_no_colon_is_refused(void) {
  // A well-formed base64 string carrying something that is not a credential at all. The colon
  // is what makes it one; without it there is no password to check, and guessing that the whole
  // blob is the password would accept a credential nobody sent.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED,
                    comfoair_auth_parse(("Basic " + b64("noseparatorhere")).c_str(), &creds));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse(("Basic " + b64("")).c_str(), &creds));
  // At any length the header may carry, and the length is what makes this worth stating. While a
  // buffer held the decoded blob, a long enough one filled it before anyone looked for a colon and
  // came back TOO_LONG -- "your password is too big for this device" said to a client that sent no
  // password at all. Same refusal either way, and the same delay, so nothing leaks; the difference
  // is what the log line tells whoever is reading it.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED,
                    comfoair_auth_parse(("Basic " + b64(std::string(300, 'q'))).c_str(), &creds));
}

void test_a_decoded_nul_byte_is_refused(void) {
  // base64 carries arbitrary bytes, and a NUL inside one would silently end the C string that
  // holds the password: "hunter2\0anything" would be compared as "hunter2". Refused rather than
  // truncated, so that what is verified is what was sent.
  //
  // Only NUL. Other control characters are not this component's business -- a password
  // containing one cannot have been STORED (comfoair_config_check_ui_password rejects control
  // characters), so it matches nothing, and duplicating that rule here would put the same
  // decision in two files.
  comfoair_auth_credentials_t creds;
  const std::string with_nul = std::string("user:hun") + '\0' + "ter";
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED,
                    comfoair_auth_parse(("Basic " + b64(with_nul)).c_str(), &creds));
  const std::string nul_in_user = std::string("us") + '\0' + "er:hunter2hunter2";
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED,
                    comfoair_auth_parse(("Basic " + b64(nul_in_user)).c_str(), &creds));
}

// --- parsing: length, which is where a naive parser dies ---------------------------------------

void test_a_header_too_long_to_buffer_is_refused_and_writes_nothing(void) {
  // The case this parser exists to survive. Nothing here is copied before its length is known,
  // and the answer is TOO_LONG rather than MALFORMED because the two mean different things to
  // whoever reads the log: malformed is a broken client, too long is either an attack or an
  // owner whose password no longer fits, and those need different answers.
  comfoair_auth_credentials_t creds;
  memset(&creds, 0xAA, sizeof creds);
  const std::string huge = "Basic " + std::string(8000, 'A');
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_TOO_LONG, comfoair_auth_parse(huge.c_str(), &creds));
  // Zeroed on entry, before any decision: a failed parse must not leave the previous call's
  // password sitting in the caller's struct for a later `if (parse(...) != OK)` to miss.
  for (size_t i = 0; i < sizeof creds.password; i++)
    TEST_ASSERT_EQUAL_MESSAGE(0, creds.password[i], "a refused header left bytes behind");

  // And the refusal that comes AFTER bytes have been written. The decoder has no buffer of its
  // own any more -- the password lands in the caller's struct as it is decoded -- so a credential
  // that turns out to be too long has already put part of itself there before anyone knows.
  // "Writes nothing" therefore has to mean "leaves nothing"; the plaintext of a refused attempt
  // is still plaintext somebody sent.
  memset(&creds, 0xAA, sizeof creds);
  const std::string overlong =
      basic("user:" + std::string(COMFOAIR_CONFIG_UI_PASS_MAX + 1, 'x'));
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_TOO_LONG, comfoair_auth_parse(overlong.c_str(), &creds));
  for (size_t i = 0; i < sizeof creds.password; i++)
    TEST_ASSERT_EQUAL_MESSAGE(0, creds.password[i], "a refused credential was left in the struct");
}

void test_the_longest_password_that_can_be_stored_can_still_be_sent(void) {
  // The boundary on the side that loses a device. comfoair_config accepts a password of
  // COMFOAIR_CONFIG_UI_PASS_MAX characters; if this parser's buffer were smaller, setting one
  // would succeed and logging in with it would fail forever, with no way in over the air.
  const std::string longest(COMFOAIR_CONFIG_UI_PASS_MAX, 'x');
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK,
                    comfoair_auth_parse(basic("user:" + longest).c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING(longest.c_str(), creds.password);
}

void test_a_password_longer_than_anything_storable_is_refused_not_truncated(void) {
  // Truncation would compare a prefix of what was sent against the stored record. That is a
  // different question from the one the client asked, and the honest answer to a credential
  // this device could never have stored is "no".
  const std::string too_long(COMFOAIR_CONFIG_UI_PASS_MAX + 1, 'x');
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_TOO_LONG,
                    comfoair_auth_parse(basic("user:" + too_long).c_str(), &creds));
}

void test_nothing_a_header_can_carry_writes_past_the_password_buffer(void) {
  // The one bound between a token chosen by whoever is attacking the device and the caller's
  // memory, pinned somewhere a test can see it.
  //
  // It was not always somewhere a test could see it. The decoder used to fill a `user:password`
  // buffer private to comfoair_auth.c; the check in front of it was sized in ENCODED characters
  // and the buffer in decoded bytes, four characters carry three, and the rounding admitted a
  // token whose last group landed two bytes past the end. Delete the bound that caught that and
  // NO ASSERTION IN THIS SUITE changes -- every case still asks only for TOO_LONG, which the
  // password-length check answers anyway. What actually stopped the mutated build was the host
  // compiler's own stack canary, an abort with nothing behind it, and the device is built without
  // one (sdkconfig.m5stack-nanoc6:1212, CONFIG_COMPILER_STACK_CHECK_MODE_NONE) -- so there it
  // would have been two attacker-chosen bytes onto the HTTP task's stack, in silence.
  //
  // There is no such buffer any more: the decoded password goes straight into the caller's struct,
  // and that is what makes the bound observable at all, because the CALLER can put something
  // behind the struct and look.
  //
  // 0xA5 rather than 0: a zero canary cannot tell "nothing was written" from "a NUL was written".
  struct probe_t {
    comfoair_auth_credentials_t creds;
    unsigned char               canary[64];
  };
  // Both halves of the blob, because a bound that holds for one and not the other is the bug this
  // is here for: a password past what can be stored, and a user name past what the header may
  // carry at all.
  for (size_t extra = 1; extra <= 400; extra++) {
    const std::string headers[] = {
        basic("user:" + std::string(COMFOAIR_CONFIG_UI_PASS_MAX + extra, 'x')),
        basic(std::string(255 + extra, 'n') + ":" + std::string(COMFOAIR_CONFIG_UI_PASS_MAX, 'x')),
    };
    for (const std::string &header : headers) {
      probe_t probe;
      memset(&probe, 0xA5, sizeof probe);
      TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_AUTH_TOO_LONG,
                                comfoair_auth_parse(header.c_str(), &probe.creds),
                                "a credential too big to hold was not refused as too long");
      for (size_t i = 0; i < sizeof probe.canary; i++)
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xA5, probe.canary[i],
                                       "a decoded byte was written past the password buffer");
    }
  }
}

void test_a_user_name_never_costs_the_password_its_length(void) {
  // comfoair_auth.h promises the browser's name box is discarded -- its value AND its length.
  // The length half is the one that was false: one buffer held the decoded `user:password` whole,
  // so the SUM was what was bounded, and a 65-character name in front of the CORRECT 128-character
  // password decoded to 194 bytes, hit the cap and came back TOO_LONG. A password manager that
  // fills that box with an email address reaches it without trying, and a correct password refused
  // is an owner outside a device whose reset button is behind the front panel of a running
  // ventilation unit -- the same loss the _Static_assert in comfoair_auth.c exists to prevent,
  // arrived at by different arithmetic.
  //
  // The sweep is over the NAME at the password length that leaves the least room, because that is
  // the corner the arithmetic hides in. Pinning one name length would restate whatever constant
  // happened to be there -- the old case pinned exactly 64, which was exactly the largest name
  // that could work, so it could not have failed on the boundary it was written to defend.
  const std::string longest(COMFOAIR_CONFIG_UI_PASS_MAX, 'p');
  for (size_t name_len = 0; name_len <= 255; name_len++) {
    comfoair_auth_credentials_t creds;
    const std::string header = basic(std::string(name_len, 'n') + ":" + longest);
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_AUTH_OK, comfoair_auth_parse(header.c_str(), &creds),
                              "a user name refused a password this device can store");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(longest.c_str(), creds.password,
                                     "the user name changed the password that came out");
  }
}

// --- parsing: the credential itself ------------------------------------------------------------

void test_only_the_first_colon_separates(void) {
  // A colon is legal in a password and illegal in a user name (RFC 7617 section 2), which is
  // precisely why the FIRST one separates. Splitting on the last would turn the password
  // "pa:ss:word" into "word" and quietly accept a wrong password as right.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("admin:pa:ss:word").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("pa:ss:word", creds.password);
  // A leading colon in the password survives too: "admin::x" is the password ":x".
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("admin::x").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING(":x", creds.password);
}

void test_an_empty_password_parses_as_an_empty_password(void) {
  // It parses. Whether it is a credential is comfoair_auth_check's answer, and it is no --
  // see test_an_empty_candidate_never_matches. Keeping the two apart matters: a parser that
  // called this malformed would report a broken client where there is a wrong password.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("admin:").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("", creds.password);
}

void test_an_empty_user_name_is_accepted(void) {
  // The realistic case, not a corner one: this device has no accounts, so an owner who has been
  // told "leave the user blank" sends exactly this.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic(":hunter2hunter2").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("hunter2hunter2", creds.password);
}

void test_the_whole_base64_alphabet_decodes(void) {
  // All sixty-four characters, by construction rather than by hoping a literal somewhere happens
  // to contain one. This case used to be named for '+' while sending none: every literal in the
  // suite encoded to letters and digits alone, so b64_value's `if (c == '+') return 62;` could be
  // changed to `return -1;` with all thirty-three cases still green -- and an owner whose password
  // ends in '~' would have been refused by a decoder this suite called covered.
  //
  // The construction: the LAST character of a four-character group is the low six bits of that
  // group's third byte, so a plaintext byte of (v + 0x40) puts sextet v there, and '?' does it for
  // v = 63. "ab:" is three bytes, so the password's own three bytes are a group of their own and
  // its third byte is the one that moves. v = 62 is therefore '+' and v = 63 is '/', sent for
  // real, and the assertion is that the byte comes back unchanged.
  comfoair_auth_credentials_t creds;
  for (unsigned v = 0; v < 64; v++) {
    const std::string pass = std::string("xx") + (char)(v == 63 ? '?' : (char)(v + 0x40));
    const std::string tok  = b64("ab:" + pass);
    TEST_ASSERT_EQUAL_size_t(8, tok.size());
    TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_AUTH_OK,
                              comfoair_auth_parse(("Basic " + tok).c_str(), &creds),
                              "a character of the base64 alphabet was refused");
    TEST_ASSERT_EQUAL_STRING_MESSAGE(pass.c_str(), creds.password,
                                     "a character of the base64 alphabet decoded to the wrong byte");
  }

  // The same two characters in the shape they actually arrive in, because a sweep says nothing
  // about whether anyone ever sends one. A '~' in a password produces '+' and a '?' produces '/',
  // and the strchr assertions are what stop an innocent edit to either literal from quietly
  // taking the character out again -- which is exactly how this case came to be named for a branch
  // it never reached.
  const std::string plus = b64("user:correcthorse~");
  TEST_ASSERT_NOT_NULL_MESSAGE(strchr(plus.c_str(), '+'), "this case no longer sends a '+'");
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(("Basic " + plus).c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("correcthorse~", creds.password);
  const std::string slash = b64("u:\xfb\xff\xbf");
  TEST_ASSERT_NOT_NULL_MESSAGE(strchr(slash.c_str(), '/'), "this case no longer sends a '/'");
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(("Basic " + slash).c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("\xfb\xff\xbf", creds.password);

  // Padded and unpadded lengths, because the last group is the other half of the same mistake.
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("u:ab").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("ab", creds.password);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("u:abc").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("abc", creds.password);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("u:abcd").c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("abcd", creds.password);
}

void test_surrounding_whitespace_is_tolerated(void) {
  // RFC 7230 section 3.2.3 allows optional whitespace around a field value, and the space
  // between the scheme and the token is "1*SP" -- more than one is legal.
  comfoair_auth_credentials_t creds;
  const std::string padded = "  Basic   " + b64("u:hunter2hunter2") + "  ";
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(padded.c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("hunter2hunter2", creds.password);
  const std::string tabbed = "Basic\t" + b64("u:hunter2hunter2");
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(tabbed.c_str(), &creds));
  TEST_ASSERT_EQUAL_STRING("hunter2hunter2", creds.password);
}

void test_a_null_destination_is_refused_rather_than_dereferenced(void) {
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, comfoair_auth_parse(basic("u:p").c_str(), nullptr));
}

// --- forgetting --------------------------------------------------------------------------------

void test_forgetting_a_credential_clears_every_byte(void) {
  // The caller holds the plaintext on the HTTP task's stack. Wiping it is the caller's job and
  // this is the call that does it, so that nobody writes their own memset and has it optimised
  // away as a dead store on a buffer that is never read again.
  comfoair_auth_credentials_t creds;
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK, comfoair_auth_parse(basic("u:hunter2hunter2").c_str(), &creds));
  comfoair_auth_forget(&creds);
  for (size_t i = 0; i < sizeof creds.password; i++)
    TEST_ASSERT_EQUAL_MESSAGE(0, creds.password[i], "the password survived being forgotten");
  comfoair_auth_forget(nullptr);  // must not crash
}

// --- the decision ------------------------------------------------------------------------------

void test_an_unset_password_grants_nothing_even_to_a_verifier_that_says_yes(void) {
  // The invariant, stated where it cannot be argued with: "unprotected" must never become
  // "protected by the empty string". comfoair_secrets pins the same thing one layer down
  // (comfoair_secret_equals with an empty stored value), and this pins that no path here can
  // route around it. It is also the distinction the CALLER must not collapse: comfoair_http_check()
  // decides what an unclaimed device allows from its own `password_set`, and an
  // `authenticated = (result == GRANTED)` bridge told GRANTED here would hand the stranger on the
  // open access point the device.
  //
  // Three assertions per case, and only the first is about the enum. `s_verify_calls` is what makes
  // "the verifier is never reached" a fact rather than a comment -- an always-yes verifier that
  // returned the right answer for the wrong reason would pass on the enum alone. `o.header` is the
  // promise comfoair_auth.h:149-151 makes and nothing pinned: ABSENT whatever the header said,
  // because with nothing stored the header is not decoded at all, which is what makes that branch
  // structurally unable to accept a credential rather than merely careful not to. A refactor that
  // parsed first and checked `stored` afterwards would keep the enum right and lose that.
  const char *const stored_nothing[] = {nullptr, ""};
  // One header per answer comfoair_auth_parse() can give, because "whatever the header said" is
  // the claim: if any of these came back as anything but ABSENT, the header had been decoded.
  const std::string headers[] = {
      basic("u:anything"),                 // would parse OK
      "Bearer x",                          // would parse NOT_BASIC
      "Basic !!!!",                        // would parse MALFORMED
      "Basic " + std::string(8000, 'A'),   // would parse TOO_LONG
  };
  for (size_t si = 0; si < sizeof stored_nothing / sizeof stored_nothing[0]; si++) {
    for (size_t hi = 0; hi < sizeof headers / sizeof headers[0]; hi++) {
      s_verify_calls = 0;
      const comfoair_auth_outcome_t o =
          comfoair_auth_check(headers[hi].c_str(), stored_nothing[si], verify_always);
      TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_AUTH_NO_PASSWORD, o.result,
                                "an unset password answered something other than NO_PASSWORD");
      TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_AUTH_ABSENT, o.header,
                                "an unset password decoded the header after all");
      TEST_ASSERT_EQUAL_size_t_MESSAGE(0, s_verify_calls,
                                       "an unset password reached the verifier");
    }
    // And with no header at all, which is what a browser sends to an unprotected device.
    s_verify_calls = 0;
    const comfoair_auth_outcome_t o = comfoair_auth_check(nullptr, stored_nothing[si], verify_always);
    TEST_ASSERT_EQUAL(COMFOAIR_AUTH_NO_PASSWORD, o.result);
    TEST_ASSERT_EQUAL_size_t(0, s_verify_calls);
  }
}

void test_an_unset_password_costs_no_delay(void) {
  // There is nothing to guess, so there is nothing to slow down -- and this is the path every
  // request takes on a device whose owner has not set a password, which is the default.
  const comfoair_auth_outcome_t o = comfoair_auth_check(nullptr, nullptr, verify_always);
  TEST_ASSERT_EQUAL_UINT32(0, o.delay_ms);
}

void test_the_right_password_is_granted_at_once(void) {
  const comfoair_auth_outcome_t o =
      comfoair_auth_check(basic("u:hunter2hunter2").c_str(), STORED, verify_recording);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_GRANTED, o.result);
  TEST_ASSERT_EQUAL_UINT32(0, o.delay_ms);
}

void test_the_verifier_is_handed_the_record_and_the_candidate_unchanged(void) {
  // Pins the seam. This component decides nothing about what a stored password looks like --
  // that is comfoair_config's record format, and there must not be a second one (a record this
  // build writes and comfoair_config cannot parse is ERASED at the next boot,
  // comfoair_config.c comfoair_config_sanitize). All that is owed here is the two strings, whole.
  comfoair_auth_check(basic("admin:pa:ss:word").c_str(), STORED, verify_recording);
  TEST_ASSERT_EQUAL_size_t(1, s_verify_calls);
  TEST_ASSERT_EQUAL_STRING(STORED, s_seen_stored);
  TEST_ASSERT_EQUAL_STRING("pa:ss:word", s_seen_candidate);
}

void test_a_wrong_password_is_denied_and_costs_the_delay(void) {
  const comfoair_auth_outcome_t o =
      comfoair_auth_check(basic("u:wrongwrongwrong").c_str(), STORED, verify_recording);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
  TEST_ASSERT_EQUAL_UINT32(COMFOAIR_AUTH_FAIL_DELAY_MS, o.delay_ms);
}

void test_an_empty_candidate_never_matches(void) {
  // Same rule as comfoair_secret_equals, applied before the verifier rather than inside it, so
  // that no injected verifier can be the thing that gets this wrong. "admin:" is one keystroke
  // away from a real attempt and a browser will send it.
  const comfoair_auth_outcome_t o = comfoair_auth_check(basic("admin:").c_str(), STORED, verify_always);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, s_verify_calls, "an empty password reached the verifier");
}

void test_a_credential_smuggled_through_misplaced_padding_is_refused(void) {
  // The reason the base64 decoder is strict about where '=' may appear, stated as the thing that
  // goes wrong rather than as a rule. The token below is b64("user:hunter2hunter2") with a '='
  // written over its fourth character; a decoder that gives '=' a value in the ordinary table --
  // which is the shorter and more obvious way to write one -- decodes it to "us@r:hunter2hunter2"
  // and grants, because the password half survives the substitution untouched.
  //
  // Nothing is stolen by that on its own; the sender already knew the password. What it means is
  // that two spellings of one credential exist, which is how a device and whatever sits in front
  // of it come to disagree about what was presented. This one is caught by structure, before any
  // of it is decoded.
  const comfoair_auth_outcome_t o =
      comfoair_auth_check("Basic dXN=cjpodW50ZXIyaHVudGVyMg==", STORED, verify_recording);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED, o.header);
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, s_verify_calls, "a misplaced pad character reached the verifier");
}

void test_a_missing_verifier_denies(void) {
  // Fails closed. A caller that forgot to wire the verifier gets a device nobody can log into,
  // which is visible and fixable; the other way round is a device anybody can log into, which
  // is neither.
  const comfoair_auth_outcome_t o = comfoair_auth_check(basic("u:hunter2hunter2").c_str(), STORED, nullptr);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
}

void test_an_absent_header_is_denied_without_the_delay(void) {
  // The 401 handshake is not a guess. Every page load on a protected device starts with a
  // request carrying no Authorization header at all -- the browser only sends credentials after
  // it has been challenged -- so charging that request the failure delay would tax every normal
  // load, on a single-task HTTP server, and teach nothing about anybody's password.
  //
  // All three spellings comfoair_auth_parse() calls ABSENT, because only the first of them was
  // ever checked through comfoair_auth_check(). "" is what esp_http_server hands over for a bare
  // `Authorization:` -- it delivers the value after the colon, so an empty field arrives as an
  // empty string and not as a missing one -- and "   " is that field with the optional whitespace
  // RFC 7230 section 3.2.3 allows. A rule that held for nullptr and not for those two would charge
  // the delay to a client that claimed nothing.
  const char *const nothing_claimed[] = {nullptr, "", "   "};
  for (size_t i = 0; i < sizeof nothing_claimed / sizeof nothing_claimed[0]; i++) {
    const comfoair_auth_outcome_t o = comfoair_auth_check(nothing_claimed[i], STORED, verify_always);
    TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(0, o.delay_ms, "a header claiming nothing was charged the delay");
    TEST_ASSERT_EQUAL(COMFOAIR_AUTH_ABSENT, o.header);
  }
  TEST_ASSERT_EQUAL_size_t_MESSAGE(0, s_verify_calls, "a header claiming nothing reached the verifier");
}

void test_every_presented_credential_that_fails_costs_the_same(void) {
  // A delay that varied by failure kind would be a side channel: an attacker who can tell
  // "wrong password" from "malformed" learns which of their guesses were even considered. One
  // number, and it does not depend on how the attempt was wrong or how long the guess was.
  const char *const attempts[] = {
      "Bearer abcdef",  // not Basic
      "Basic !!!!",     // not base64
  };
  for (size_t i = 0; i < sizeof attempts / sizeof attempts[0]; i++) {
    const comfoair_auth_outcome_t o = comfoair_auth_check(attempts[i], STORED, verify_never);
    TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_AUTH_FAIL_DELAY_MS, o.delay_ms);
  }
  const std::string built[] = {
      basic("noseparator"),                                     // no colon
      basic("u:"),                                              // empty password
      basic("u:x"),                                             // a one-character guess
      basic("u:" + std::string(COMFOAIR_CONFIG_UI_PASS_MAX, 'x')),  // the longest legal guess
      basic("u:" + std::string(COMFOAIR_CONFIG_UI_PASS_MAX + 1, 'x')),  // longer than storable
      "Basic " + std::string(8000, 'A'),                        // too long to buffer
  };
  for (size_t i = 0; i < sizeof built / sizeof built[0]; i++) {
    const comfoair_auth_outcome_t o = comfoair_auth_check(built[i].c_str(), STORED, verify_never);
    TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, o.result);
    TEST_ASSERT_EQUAL_UINT32(COMFOAIR_AUTH_FAIL_DELAY_MS, o.delay_ms);
  }
}

void test_the_delay_is_a_delay_and_not_a_lockout(void) {
  // The two-level reset and the never-closing setup window both rest on the owner getting back
  // in: the button and the USB port are behind the front panel of a running ventilation unit. A
  // lockout after N failures is a denial of service against the person most likely to mistype --
  // so a correct password works on the attempt straight after a wrong one, with no counter.
  for (int i = 0; i < 25; i++) {
    const comfoair_auth_outcome_t bad =
        comfoair_auth_check(basic("u:wrongwrongwrong").c_str(), STORED, verify_recording);
    TEST_ASSERT_EQUAL(COMFOAIR_AUTH_DENIED, bad.result);
  }
  const comfoair_auth_outcome_t good =
      comfoair_auth_check(basic("u:hunter2hunter2").c_str(), STORED, verify_recording);
  TEST_ASSERT_EQUAL_MESSAGE(COMFOAIR_AUTH_GRANTED, good.result,
                            "the right password was refused after wrong ones -- that is a lockout");
  TEST_ASSERT_EQUAL_UINT32(0, good.delay_ms);
}

void test_the_outcome_says_how_the_header_failed(void) {
  // So a log line can say why without saying what. The kind of failure is safe to publish; the
  // credential is not, and nothing in the outcome carries it (comfoair_auth.h says why).
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_NOT_BASIC,
                    comfoair_auth_check("Bearer x", STORED, verify_never).header);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_MALFORMED,
                    comfoair_auth_check("Basic !!!!", STORED, verify_never).header);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_TOO_LONG,
                    comfoair_auth_check(("Basic " + std::string(8000, 'A')).c_str(), STORED,
                                        verify_never).header);
  TEST_ASSERT_EQUAL(COMFOAIR_AUTH_OK,
                    comfoair_auth_check(basic("u:hunter2hunter2").c_str(), STORED,
                                        verify_recording).header);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_the_test_encoder_agrees_with_rfc_7617);
  RUN_TEST(test_a_missing_header_is_not_a_credential);
  RUN_TEST(test_a_scheme_that_is_not_basic_is_refused);
  RUN_TEST(test_the_scheme_with_no_credential_after_it_is_malformed_not_a_foreign_scheme);
  RUN_TEST(test_the_scheme_name_is_case_insensitive);
  RUN_TEST(test_base64_that_is_not_base64_is_refused);
  RUN_TEST(test_a_decoded_blob_with_no_colon_is_refused);
  RUN_TEST(test_a_decoded_nul_byte_is_refused);
  RUN_TEST(test_a_header_too_long_to_buffer_is_refused_and_writes_nothing);
  RUN_TEST(test_the_longest_password_that_can_be_stored_can_still_be_sent);
  RUN_TEST(test_a_password_longer_than_anything_storable_is_refused_not_truncated);
  RUN_TEST(test_nothing_a_header_can_carry_writes_past_the_password_buffer);
  RUN_TEST(test_a_user_name_never_costs_the_password_its_length);
  RUN_TEST(test_only_the_first_colon_separates);
  RUN_TEST(test_an_empty_password_parses_as_an_empty_password);
  RUN_TEST(test_an_empty_user_name_is_accepted);
  RUN_TEST(test_the_whole_base64_alphabet_decodes);
  RUN_TEST(test_surrounding_whitespace_is_tolerated);
  RUN_TEST(test_a_null_destination_is_refused_rather_than_dereferenced);
  RUN_TEST(test_forgetting_a_credential_clears_every_byte);
  RUN_TEST(test_an_unset_password_grants_nothing_even_to_a_verifier_that_says_yes);
  RUN_TEST(test_an_unset_password_costs_no_delay);
  RUN_TEST(test_the_right_password_is_granted_at_once);
  RUN_TEST(test_the_verifier_is_handed_the_record_and_the_candidate_unchanged);
  RUN_TEST(test_a_wrong_password_is_denied_and_costs_the_delay);
  RUN_TEST(test_an_empty_candidate_never_matches);
  RUN_TEST(test_a_credential_smuggled_through_misplaced_padding_is_refused);
  RUN_TEST(test_a_missing_verifier_denies);
  RUN_TEST(test_an_absent_header_is_denied_without_the_delay);
  RUN_TEST(test_every_presented_credential_that_fails_costs_the_same);
  RUN_TEST(test_the_delay_is_a_delay_and_not_a_lockout);
  RUN_TEST(test_the_outcome_says_how_the_header_failed);
  return UNITY_END();
}
