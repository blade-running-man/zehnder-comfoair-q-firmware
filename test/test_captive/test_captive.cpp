// The captive portal, in the two halves of it that can be decided without a network stack.
//
// Half one is a table: seven paths that phones fetch to find out whether a network works, and
// the byte-exact answer each of them wants. Every literal asserted below was taken from the
// live original with curl on 2026-09-04 -- the Content-Length of each is in the comment beside
// it -- because "close enough" is not a thing here: Windows compares the body to a string,
// Apple compares the body to a document, and one trailing newline is the difference between a
// setup that works and a phone that leaves for mobile data.
//
// Half two is a DNS answer built from a query sent by ANYONE IN RADIO RANGE. The access point
// is open on purpose, the parser has no authentication in front of it, and it runs before the owner
// has ever touched the device. Most of the cases below are therefore malformed packets, and
// they are the reason this parsing lives in a pure function instead of inside a socket loop
// where it could only be reached with a radio.
//
// EVERY DNS CASE BELOW GOES THROUGH guarded_reply(), which puts the datagram against an unmapped
// page. Read the comment on it before writing another one: a malformed-packet test that only
// looks at the return value is a test that passes against a parser with no bounds checking in it,
// and that was measured here rather than assumed.
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <initializer_list>

#include <unity.h>

#include "comfoair_captive.h"
#include "comfoair_captive_dns.h"

// macOS spells it MAP_ANON; the rest of the world spells it MAP_ANONYMOUS. This file is host-only
// (platformio.ini sets test_ignore = * on the board environment) and already POSIX-bound by
// -pthread, so one alias is the whole of the portability story.
#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

void setUp(void) {}
void tearDown(void) {}

// --- the probe table --------------------------------------------------------------------------

// Each of these is what the real host serves, fetched on 2026-09-04. A test that quoted the
// implementation instead would pass while the firmware told Windows the wrong thing.
static const char *APPLE_SUCCESS =
    "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>\n";  // 69 bytes
static const char *NCSI_TXT        = "Microsoft NCSI";          // 14 bytes, and no newline
static const char *CONNECTTEST_TXT = "Microsoft Connect Test";  // 22 bytes, and no newline
static const char *FIREFOX_SUCCESS = "success\n";               // 8 bytes, and there IS one
static const char *FIREFOX_CANONICAL =
    "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>";

static const comfoair_captive_answer_t *answer_for(const char *path) {
  return comfoair_captive_answer(path);
}

void test_android_is_told_the_network_is_fine_with_the_status_it_asks_for(void) {
  // Android wants 204 and an empty body. Not 200-with-nothing: NetworkMonitor compares the
  // status code, and a 200 -- which is what the SPA shell arrives as today -- is what makes it
  // announce a captive portal and stop counting the network as usable.
  for (const char *path : {"/generate_204", "/gen_204"}) {
    const comfoair_captive_answer_t *a = answer_for(path);
    TEST_ASSERT_NOT_NULL_MESSAGE(a, path);
    TEST_ASSERT_EQUAL_STRING("204 No Content", a->status);
    TEST_ASSERT_NULL(a->body);
    TEST_ASSERT_EQUAL_UINT(0, a->body_len);
  }
}

void test_apple_gets_the_document_apple_serves_byte_for_byte(void) {
  // The Captive Network Assistant opens unless this is EXACTLY the page captive.apple.com
  // returns, trailing newline and all -- Content-Length: 69, not 68.
  const comfoair_captive_answer_t *a = answer_for("/hotspot-detect.html");
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_EQUAL_STRING("200 OK", a->status);
  TEST_ASSERT_EQUAL_STRING("text/html", a->content_type);
  TEST_ASSERT_EQUAL_UINT(69, a->body_len);
  TEST_ASSERT_EQUAL_STRING(APPLE_SUCCESS, a->body);
}

void test_the_two_windows_probes_get_their_two_strings_and_neither_gets_a_newline(void) {
  // Both are text/plain with no line ending, which is the one thing an editor "tidying" this
  // table will add. Windows 10 and later fetch connecttest.txt; ncsi.txt is Windows 8 and
  // earlier and is still probed by enough embedded stacks to be worth its two lines.
  const comfoair_captive_answer_t *ncsi = answer_for("/ncsi.txt");
  TEST_ASSERT_NOT_NULL(ncsi);
  TEST_ASSERT_EQUAL_STRING("text/plain", ncsi->content_type);
  TEST_ASSERT_EQUAL_UINT(14, ncsi->body_len);
  TEST_ASSERT_EQUAL_STRING(NCSI_TXT, ncsi->body);

  const comfoair_captive_answer_t *ct = answer_for("/connecttest.txt");
  TEST_ASSERT_NOT_NULL(ct);
  TEST_ASSERT_EQUAL_UINT(22, ct->body_len);
  TEST_ASSERT_EQUAL_STRING(CONNECTTEST_TXT, ct->body);
}

void test_firefox_gets_its_newline_where_windows_does_not(void) {
  // The asymmetry is the point of testing these two together: success.txt ends in \n and
  // connecttest.txt does not. Whoever normalises them will break one of the two.
  const comfoair_captive_answer_t *s = answer_for("/success.txt");
  TEST_ASSERT_NOT_NULL(s);
  TEST_ASSERT_EQUAL_UINT(8, s->body_len);
  TEST_ASSERT_EQUAL_STRING(FIREFOX_SUCCESS, s->body);

  const comfoair_captive_answer_t *c = answer_for("/canonical.html");
  TEST_ASSERT_NOT_NULL(c);
  TEST_ASSERT_EQUAL_STRING("text/html", c->content_type);
  TEST_ASSERT_EQUAL_UINT(90, c->body_len);
  TEST_ASSERT_EQUAL_STRING(FIREFOX_CANONICAL, c->body);
}

void test_no_probe_gets_a_redirect_and_none_gets_the_setup_page(void) {
  // THE decision of this component, and the only test here that is about a choice rather than
  // about a byte. A captive portal that redirects gets the phone to open a sign-in webview --
  // no WebSocket, no Web Crypto, and the OS closes it when it feels like it, which on this
  // device is while somebody is typing their Wi-Fi password into it. Worse on Android: a
  // detected portal stays unvalidated until a probe finally returns 204, and this device has no
  // sign-in step that would ever make that happen, so the phone keeps mobile data as its
  // default route -- and the form POST leaves the house instead of reaching 192.168.4.1.
  //
  // So every probe is told the network is fine, and discovery is the DNS half's job: any name
  // the owner types resolves here. DO NOT "improve" this by redirecting to the setup page.
  size_t count = 0;
  const comfoair_captive_answer_t *table = comfoair_captive_table(&count);
  TEST_ASSERT_NOT_NULL(table);
  for (size_t i = 0; i < count; i++) {
    TEST_ASSERT_TRUE_MESSAGE(table[i].status[0] == '2', table[i].path);
    if (table[i].body != NULL)
      TEST_ASSERT_NULL_MESSAGE(strstr(table[i].body, "<script"), table[i].path);
  }
}

void test_no_answer_is_built_out_of_anything_the_device_knows(void) {
  // These seven are answered before the access policy runs -- a phone probing on the fallback
  // access point of a password-protected device would otherwise get 401, which is not 204, which
  // is a captive portal, which is the three failures above on the device whose owner is already
  // having a bad day. That is only safe while the answers are constants: nothing here is
  // formatted, so nothing here can leak a name, an address or a reading.
  size_t count = 0;
  const comfoair_captive_answer_t *table = comfoair_captive_table(&count);
  for (size_t i = 0; i < count; i++) {
    if (table[i].body == NULL)
      continue;
    TEST_ASSERT_NULL_MESSAGE(strchr(table[i].body, '%'), table[i].path);
    TEST_ASSERT_EQUAL_UINT_MESSAGE(strlen(table[i].body), table[i].body_len, table[i].path);
  }
}

void test_a_probe_answer_is_never_cached(void) {
  // The phone asks www.msftconnecttest.com/connecttest.txt again in ten minutes -- on the
  // owner's real network, at the same URL. A cached copy of our answer would tell it a
  // genuinely captive hotel network is fine. One header, and it is the caller's to send, so it
  // travels in the table rather than in a comment somebody has to read.
  size_t count = 0;
  const comfoair_captive_answer_t *table = comfoair_captive_table(&count);
  for (size_t i = 0; i < count; i++)
    TEST_ASSERT_EQUAL_STRING_MESSAGE("no-store", table[i].cache_control, table[i].path);
}

void test_the_table_is_seven_paths_and_the_lookup_finds_every_one(void) {
  // The count is asserted so that adding a path is a deliberate act with a test edit attached,
  // and so that the lookup and the table can never drift into two lists (AGENTS.md).
  size_t count = 0;
  const comfoair_captive_answer_t *table = comfoair_captive_table(&count);
  TEST_ASSERT_EQUAL_UINT(7, count);
  for (size_t i = 0; i < count; i++)
    TEST_ASSERT_EQUAL_PTR_MESSAGE(&table[i], answer_for(table[i].path), table[i].path);
}

void test_nothing_that_is_not_a_probe_is_answered_as_one(void) {
  // Everything else must fall through to the SPA and the API untouched. A table that matched
  // "/" would serve Apple's success page as the setup page.
  for (const char *path : {"/", "/index.html", "/api/state", "/ws", "/assets/app.js"})
    TEST_ASSERT_NULL_MESSAGE(answer_for(path), path);
  TEST_ASSERT_NULL(answer_for(nullptr));
  TEST_ASSERT_NULL(answer_for(""));
}

void test_a_probe_path_is_matched_whole_and_never_as_a_prefix(void) {
  // The same rule, and the same reason, as comfoair_http_policy.c's path_is(): a prefix match
  // is how a rule written for one path quietly starts governing another.
  for (const char *path : {"/generate_204/x", "/generate_2040", "/ncsi.txt.bak", "/x/ncsi.txt",
                           "/GENERATE_204"})
    TEST_ASSERT_NULL_MESSAGE(answer_for(path), path);
}

void test_stripping_the_query_string_stays_the_servers_job(void) {
  // "/generate_204?x=1" is deliberately NOT matched here. comfoair_http_gate.c's
  // comfoair_http_request_path() already strips the query, the fragment, an absolute-form URL and a
  // trailing slash before any policy sees a path, and a second stripper in this file would be a
  // second set of rules to keep in agreement with the first. This test exists so that the day a
  // probe arrives with a query and misses, the reader is sent to comfoair_http_request_path()
  // instead of adding a strchr here.
  TEST_ASSERT_NULL(answer_for("/generate_204?x=1"));
  TEST_ASSERT_NOT_NULL(answer_for("/generate_204"));
}

// --- the DNS answer ---------------------------------------------------------------------------

static const uint8_t AP_ADDRESS[4] = {192, 168, 4, 1};

static uint16_t be16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }

// Encodes "example.com" as 7 e x a m p l e 3 c o m 0. Returns the bytes written.
static size_t encode_name(uint8_t *out, const char *dotted) {
  size_t written = 0;
  while (*dotted != '\0') {
    const char *dot = strchr(dotted, '.');
    size_t label    = dot != nullptr ? (size_t)(dot - dotted) : strlen(dotted);
    out[written++]  = (uint8_t)label;
    memcpy(out + written, dotted, label);
    written += label;
    dotted += label;
    if (*dotted == '.')
      dotted++;
  }
  out[written++] = 0;
  return written;
}

// A well-formed query, which every malformed case below then damages in exactly one way.
static size_t make_query(uint8_t *out, const char *name, uint16_t qtype = 1, uint16_t qclass = 1,
                         uint16_t id = 0xBEEF) {
  out[0] = (uint8_t)(id >> 8);
  out[1] = (uint8_t)(id & 0xFF);
  out[2] = 0x01;  // RD, and nothing else: a question, not a response
  out[3] = 0x00;
  out[4] = 0x00;
  out[5] = 0x01;  // one question
  memset(out + 6, 0, 6);
  size_t n = 12 + encode_name(out + 12, name);
  out[n++] = (uint8_t)(qtype >> 8);
  out[n++] = (uint8_t)(qtype & 0xFF);
  out[n++] = (uint8_t)(qclass >> 8);
  out[n++] = (uint8_t)(qclass & 0xFF);
  return n;
}

// comfoair_captive_dns_reply(), with the datagram copied so that its LAST BYTE IS THE LAST MAPPED
// BYTE before an unmapped page. A parser that reads one byte past the length it was handed dies
// here, immediately and visibly, instead of returning a plausible 0.
//
// Without this, every malformed-packet test below is a test of a return value and nothing else,
// and that is not a theory: both bounds checks in question_len() were deleted, one at a time, and
// all thirty tests stayed green. The 512-byte q[] arrays are zeroed, so a read past the length
// finds a legal terminating zero and the parse refuses the packet for a reason that has nothing
// to do with the check that was removed. Comments in this file claiming to defend a read off the
// end were, until this helper existed, defending nothing.
//
// The obvious alternative was -fsanitize=address on [env:native]. DO NOT do that instead: that
// environment is shared by every suite in the repository, and this component has no business
// changing how the others are built or what they must be clean under. A guard page costs one
// mmap per call, is local to this file, and fails harder.
//
// A crash here takes the whole binary with it and the suite reports no further results. That is
// the intended signal: on a parser reachable from the pavement, "read out of bounds" is not a
// failed assertion to be triaged among others.
static size_t guarded_reply(const uint8_t *datagram, size_t len, const uint8_t address[4],
                            uint8_t *out, size_t out_cap) {
  const size_t page = (size_t)sysconf(_SC_PAGESIZE);
  const size_t body = ((len + page) / page) * page;  // >= len, and never zero
  uint8_t *base = (uint8_t *)mmap(nullptr, body + page, PROT_READ | PROT_WRITE,
                                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  TEST_ASSERT_TRUE_MESSAGE(base != MAP_FAILED, "mmap");
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, mprotect(base + body, page, PROT_NONE), "mprotect");

  uint8_t *packet = base + body - len;  // flush against the guard
  memcpy(packet, datagram, len);
  size_t rn = comfoair_captive_dns_reply(packet, len, address, out, out_cap);
  munmap(base, body + page);
  return rn;
}

void test_a_name_lookup_is_answered_with_our_own_access_point_address(void) {
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");

  size_t rn = guarded_reply(q, qn, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_EQUAL_UINT(qn + 16, rn);

  TEST_ASSERT_EQUAL_HEX16(0xBEEF, be16(r));      // the client matches on the id
  TEST_ASSERT_EQUAL_HEX8(0x85, r[2]);            // QR, AA, and the RD it asked with, echoed
  TEST_ASSERT_EQUAL_HEX8(0x80, r[3]);            // RA, RCODE 0
  TEST_ASSERT_EQUAL_UINT16(1, be16(r + 4));      // the question
  TEST_ASSERT_EQUAL_UINT16(1, be16(r + 6));      // one answer
  TEST_ASSERT_EQUAL_UINT16(0, be16(r + 8));      // no authority
  TEST_ASSERT_EQUAL_UINT16(0, be16(r + 10));     // and no additional: the OPT is not echoed

  const uint8_t *rr = r + qn;
  TEST_ASSERT_EQUAL_HEX16(0xC00C, be16(rr));     // the name, as a pointer at the question
  TEST_ASSERT_EQUAL_UINT16(1, be16(rr + 2));     // A
  TEST_ASSERT_EQUAL_UINT16(1, be16(rr + 4));     // IN
  TEST_ASSERT_EQUAL_UINT16(4, be16(rr + 10));    // four bytes of address
  TEST_ASSERT_EQUAL_UINT8_ARRAY(AP_ADDRESS, rr + 12, 4);
}

void test_the_question_comes_back_byte_for_byte_including_its_case(void) {
  // Resolvers randomise the case of the name they ask for and drop an answer that comes back
  // spelled differently -- it is the cheapest defence there is against a forged reply (0x20
  // encoding). DO NOT normalise or re-encode the question on the way out: copy the bytes.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "CoNnEcTiViTyChEcK.gStAtIc.CoM");

  size_t rn = guarded_reply(q, qn, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_GREATER_THAN_UINT(0, rn);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(q + 12, r + 12, qn - 12);
}

void test_the_address_that_goes_out_is_the_one_the_caller_gave(void) {
  // The access point's address is the caller's to know: this component has no esp_netif in it,
  // and hard-coding 192.168.4.1 would make the answer wrong the day the DHCP range moves.
  const uint8_t other[4] = {10, 0, 0, 7};
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "captive.apple.com");
  size_t  rn = guarded_reply(q, qn, other, r, sizeof r);
  TEST_ASSERT_GREATER_THAN_UINT(0, rn);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(other, r + qn + 12, 4);
}

void test_the_answer_expires_immediately_so_the_lie_dies_with_the_access_point(void) {
  // Ten minutes from now the phone is on the owner's real network with our answer for
  // connectivitycheck.gstatic.com still in its cache, pointing at an address that is now a
  // printer. TTL 0 means "use this once" (RFC 2181 section 8). DO NOT raise it to save traffic:
  // the traffic is a handful of packets on a link with one client.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  size_t  rn = guarded_reply(q, qn, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_GREATER_THAN_UINT(0, rn);
  const uint8_t *ttl = r + qn + 6;
  TEST_ASSERT_EQUAL_UINT32(0, ((uint32_t)ttl[0] << 24) | ((uint32_t)ttl[1] << 16) |
                                  ((uint32_t)ttl[2] << 8) | ttl[3]);
}

void test_a_question_we_do_not_serve_is_answered_with_no_records_rather_than_silence(void) {
  // Every phone on this access point asks for the AAAA before the A. Dropping it costs the
  // client its resolver timeout -- seconds, per name, on the one page the owner is waiting for
  // -- so it gets a proper empty answer instead: the name exists, it has nothing of that kind.
  // This is the one malformed-or-unwanted case that gets a reply, and it is not malformed.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com", 28 /* AAAA */);

  size_t rn = guarded_reply(q, qn, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_EQUAL_UINT(qn, rn);             // header and question, and nothing after them
  TEST_ASSERT_EQUAL_UINT16(0, be16(r + 6));   // no answers
  TEST_ASSERT_EQUAL_HEX8(0x80, r[3]);         // and NOERROR: not a failure, just nothing to say
  TEST_ASSERT_EQUAL_UINT8_ARRAY(q + 12, r + 12, qn - 12);
}

void test_a_class_we_do_not_serve_is_answered_the_same_way(void) {
  // CHAOS TXT version.bind is what a scanner asks. An empty NOERROR says nothing about us.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "version.bind", 16 /* TXT */, 3 /* CH */);
  size_t  rn = guarded_reply(q, qn, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_EQUAL_UINT(qn, rn);
  TEST_ASSERT_EQUAL_UINT16(0, be16(r + 6));
}

void test_a_query_carrying_an_edns_record_is_still_answered(void) {
  // glibc, Android and every modern stub resolver append an OPT pseudo-record, so ARCOUNT is 1
  // on most real queries. A parser that insisted on a bare question would answer nothing to
  // almost every phone -- which is the failure this whole component exists to prevent, arrived
  // at from the other side. The OPT is not echoed back; a client that gets no OPT falls back to
  // 512-byte UDP, and 512 bytes is more than any answer here can be.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  q[11]      = 1;  // ARCOUNT
  // OPT: root name, type 41, UDP size 4096, no flags, no data.
  static const uint8_t opt[] = {0x00, 0x00, 0x29, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  memcpy(q + qn, opt, sizeof opt);

  size_t rn = guarded_reply(q, qn + sizeof opt, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_EQUAL_UINT(qn + 16, rn);
  TEST_ASSERT_EQUAL_UINT16(1, be16(r + 6));   // answered
  TEST_ASSERT_EQUAL_UINT16(0, be16(r + 10));  // and the OPT dropped
}

void test_a_packet_too_short_to_be_a_question_is_not_answered(void) {
  // recvfrom hands over whatever arrived. Every length below the smallest legal query has to be
  // survivable, including the empty datagram -- which costs nothing to send and can be sent
  // continuously.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");

  for (size_t len = 0; len < 12; len++)
    TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, len, AP_ADDRESS, r, sizeof r));
  // A header that promises a question, and no question.
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, 12, AP_ADDRESS, r, sizeof r));
  // A complete name and no room for the type and class that must follow it.
  for (size_t missing = 1; missing <= 4; missing++)
    TEST_ASSERT_EQUAL_UINT(
        0, guarded_reply(q, qn - missing, AP_ADDRESS, r, sizeof r));
}

void test_a_name_that_runs_past_the_end_of_the_packet_is_not_answered(void) {
  // The label length byte is attacker-controlled and the buffer is not: 63 bytes of label are
  // claimed and four are present. What is pinned is that the claim is not believed -- the packet
  // is refused, and (through guarded_reply) refused WITHOUT the bytes behind it being read.
  //
  // It used to say "this is the read that walks off the end", and that was wrong twice over: the
  // parse never reaches the label body, and until guarded_reply() existed no test in this file
  // could have told the difference if it had. Which of the two bounds in question_len() catches
  // this one is deliberately not asserted; see the comment on `at + label > len` for why there
  // are two.
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t q[20];
  memset(q, 0, sizeof q);
  q[2]  = 0x01;
  q[5]  = 0x01;
  q[12] = 63;  // a label sixty-three bytes long, with four bytes of packet left
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, 17, AP_ADDRESS, r, sizeof r));
}

void test_a_name_that_never_ends_is_not_answered(void) {
  // Labels all the way to the last byte, no terminating zero. A loop that stops only on that
  // zero reads for ever.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  memset(q, 0, sizeof q);
  q[2] = 0x01;
  q[5] = 0x01;
  for (size_t i = 12; i < 60; i += 4)
    q[i] = 3;  // three bytes of label, then the next length byte, to the end
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, 60, AP_ADDRESS, r, sizeof r));
}

// Writes a question whose NAME is exactly `wire` bytes on the wire -- length bytes and the root
// byte included, which is the number RFC 1035 2.3.4 bounds at 255 and the number MAX_NAME counts.
// Returns the whole datagram length. `wire` must be at least 1 and leave room for whole labels.
static size_t make_query_with_name_of(uint8_t *out, size_t wire) {
  memset(out, 0, 12);
  out[2]      = 0x01;  // RD
  out[5]      = 0x01;  // one question
  size_t n    = 12;
  size_t left = wire - 1;  // everything but the root byte
  while (left > 0) {
    size_t label = left > 16 ? 15 : left - 1;  // 1 length byte + `label` bytes of data
    // A zero here would be a root byte in the middle of the name: the packet would still be
    // legal, the name would end early, and the test would be measuring a different number than
    // the one in its own name. Refuse to build it rather than pass quietly.
    TEST_ASSERT_GREATER_THAN_UINT(0, label);
    out[n] = (uint8_t)label;
    memset(out + n + 1, 'a', label);
    n += 1 + label;
    left -= 1 + label;
  }
  out[n++] = 0;  // root
  out[n++] = 0;
  out[n++] = 1;  // type A
  out[n++] = 0;
  out[n++] = 1;  // class IN
  return n;
}

void test_a_name_longer_than_dns_allows_is_not_answered(void) {
  // 255 bytes is the limit (RFC 1035 section 2.3.4) and the reason the reply buffer can be
  // sized at all. A 300-byte name inside a legal 512-byte datagram is well formed everywhere
  // except in the one field that bounds our arithmetic.
  //
  // 256 IS THE CASE THAT MATTERS, and 320 is only company. With 320 alone, MAX_NAME can be
  // widened from 255 to 300 and this suite stays green -- measured -- which means the constant
  // that the whole reply-size argument rests on was not pinned at all, only bracketed loosely.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  for (size_t wire : {(size_t)256, (size_t)320}) {
    size_t n = make_query_with_name_of(q, wire);
    TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, n, AP_ADDRESS, r, sizeof r));
  }
}

void test_the_longest_name_dns_does_allow_is_answered(void) {
  // The other side of the same boundary, one byte away from the test above, because a bound that
  // also rejects legal names is a captive portal that silently fails on somebody's very long
  // hostname -- and nothing in the field would say why.
  //
  // The 287 is the arithmetic comfoair_captive_dns.c's MAX_NAME comment claims: 12 + 259 + 16,
  // the largest datagram this file can ever emit, and the reason nothing here has to think about
  // truncation or about the 512. It is asserted rather than described, because a description is
  // what it was. (The line this replaces asserted rn <= 512 immediately after asserting
  // rn == n + 16 with n a compile-time constant of 271: it could only fire if the line above it
  // already had.)
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  n = make_query_with_name_of(q, 255);
  TEST_ASSERT_EQUAL_UINT(271, n);  // 12 header + 255 name + 4 type and class
  size_t rn = guarded_reply(q, n, AP_ADDRESS, r, sizeof r);
  TEST_ASSERT_EQUAL_UINT(n + 16, rn);
  TEST_ASSERT_EQUAL_UINT(287, rn);
}

void test_a_compression_pointer_in_a_question_is_not_followed(void) {
  // 0xC00C points at itself. Following pointers while parsing is the classic decompression
  // loop, and on a single-core device the loop is not a crash -- it is a ventilation unit that
  // stops answering because a packet arrived. Pointers are legal in an ANSWER (RFC 1035 4.1.4)
  // and this file emits one; they have no business in a question, so they are refused rather
  // than resolved. DO NOT add pointer following here to be more permissive.
  //
  // THE PACKET IS 210 BYTES FOR A REASON, and the reason is the same trap
  // test_a_reserved_label_type_is_not_answered describes. Written the obvious way -- 0xC0 0x0C
  // at the front of an 18-byte datagram -- this test passed against a parser with the pointer
  // check DELETED, and did so for six months' worth of readers who would have believed it: a
  // pointer-blind parser reads 0xC0 as a 192-byte label, and 192 bytes do not fit in what is
  // left of eighteen, so the bounds check refuses the packet for a reason that has nothing to
  // do with pointers. Measured, not reasoned about: the mutant passed all thirty tests.
  //
  // So the fake 192-byte label is made to FIT. A pointer-blind parser walks it, finds the root
  // byte at 205 and the type and class after it, and answers 226 bytes; the real parser refuses
  // at the first byte. The two now disagree, which is the whole job of this test.
  //
  // A parser that instead FOLLOWS the pointer never returns, and this file is where that shows
  // up -- as a suite that hangs rather than one that fails. On a decompression loop that is the
  // honest signal.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  memset(q, 0, sizeof q);
  q[2]   = 0x01;
  q[5]   = 0x01;
  q[12]  = 0xC0;  // a pointer to offset 12 -- which is this byte
  q[13]  = 0x0C;
  q[205] = 0;     // where the root byte lands if 0xC0 is read as a length instead
  q[207] = 1;     // type A
  q[209] = 1;     // class IN
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, 210, AP_ADDRESS, r, sizeof r));

  // And the two-byte version an attacker actually sends, kept because it is the cheap one: a
  // datagram with nothing in it but the loop.
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, 18, AP_ADDRESS, r, sizeof r));
}

void test_a_reserved_label_type_is_not_answered(void) {
  // The top two bits of a length byte are 00 for a label and 11 for a pointer. 01 and 10 have
  // never been assigned, so a packet carrying one is not a query we can read -- and guessing at
  // it is how a parser grows a branch nobody has ever tested.
  //
  // The packet is built so that the guess in question -- masking the reserved bits off and
  // carrying on, which is what "be liberal in what you accept" turns into here -- would produce a
  // PERFECTLY VALID question and a reply. Written the obvious way, with the reserved byte simply
  // dropped in front of a short packet, this test passes against a parser that has no reserved
  // check at all, because the length that comes out of the mask runs off the end and the bounds
  // check refuses it for another reason entirely. Caught by mutating the parser; do not simplify
  // the packet back.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  for (uint8_t top : {0x40, 0x80}) {
    memset(q, 0, sizeof q);
    q[2]  = 0x01;
    q[5]  = 0x01;
    q[12] = (uint8_t)(top | 3);  // masked, this reads as a three-byte label...
    memset(q + 13, 'a', 3);
    q[16] = 0;  // ...ending a name that is complete and legal,
    q[18] = 1;  // followed by a type
    q[20] = 1;  // and a class that are both exactly what we serve.
    TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, 21, AP_ADDRESS, r, sizeof r));
  }
}

void test_something_that_is_already_an_answer_is_never_answered(void) {
  // QR set means a response arrived at port 53. Replying to it makes a loop: our own reply
  // leaves from port 53, so a datagram with a spoofed source port 53 gets two servers -- or
  // this one and itself -- trading packets until somebody notices the access point is unusable.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  q[2] |= 0x80;
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, qn, AP_ADDRESS, r, sizeof r));
}

void test_a_message_that_is_not_a_question_is_not_answered(void) {
  // Opcode 4 is NOTIFY and 5 is UPDATE. Neither is a name lookup, and a captive portal that
  // answers them is a captive portal that has opinions about a protocol it does not speak.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  for (uint8_t opcode : {4, 5}) {
    q[2] = (uint8_t)(0x01 | (opcode << 3));
    TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, qn, AP_ADDRESS, r, sizeof r));
  }
}

void test_a_packet_without_exactly_one_question_is_not_answered(void) {
  // Zero questions is a probe, and more than one is a packet whose second question we would
  // have to echo without answering -- a shape no client sends and every parser gets wrong.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  q[5]       = 0;
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, qn, AP_ADDRESS, r, sizeof r));
  q[5] = 2;
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, qn, AP_ADDRESS, r, sizeof r));
}

void test_an_answer_that_would_not_fit_is_not_half_written(void) {
  // The size is decided before the first byte is stored, so a caller with a small buffer gets
  // nothing rather than a truncated datagram -- and a truncated DNS response is worse than no
  // response, because the client accepts it and reads garbage out of it.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  memset(r, 0xAA, sizeof r);
  TEST_ASSERT_EQUAL_UINT(0, guarded_reply(q, qn, AP_ADDRESS, r, qn + 15));
  for (size_t i = 0; i < sizeof r; i++)
    TEST_ASSERT_EQUAL_HEX8(0xAA, r[i]);
  TEST_ASSERT_EQUAL_UINT(qn + 16, guarded_reply(q, qn, AP_ADDRESS, r, qn + 16));
}

void test_nothing_here_falls_over_on_a_null(void) {
  // The socket loop is the only caller today and it never passes one. The next caller is a
  // test, a fuzzer, or an integration written at midnight.
  //
  // The one group that calls the parser directly rather than through guarded_reply(): a NULL
  // query has no buffer to put behind a guard page.
  uint8_t q[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  size_t  qn = make_query(q, "example.com");
  TEST_ASSERT_EQUAL_UINT(0, comfoair_captive_dns_reply(nullptr, qn, AP_ADDRESS, r, sizeof r));
  TEST_ASSERT_EQUAL_UINT(0, comfoair_captive_dns_reply(q, qn, nullptr, r, sizeof r));
  TEST_ASSERT_EQUAL_UINT(0, comfoair_captive_dns_reply(q, qn, AP_ADDRESS, nullptr, sizeof r));
  TEST_ASSERT_EQUAL_UINT(0, comfoair_captive_dns_reply(q, qn, AP_ADDRESS, r, 0));
}

void test_the_reply_may_be_built_on_top_of_the_query_it_answers(void) {
  // comfoair_captive_dns.c uses memmove rather than memcpy for the question, and the reason it
  // gives is that `out` is allowed to be the query's own buffer. That was a claim about a
  // contract with no caller and no test behind it -- so here is the test, and the header now
  // states the contract in the same words.
  //
  // WHAT AN ALIASING CALLER MUST GET RIGHT, because getting it wrong fails silently: `out_cap`
  // is the size of the BUFFER, never the length of the query in it. The reply to an A question
  // is exactly sixteen bytes longer than the question, so out_cap = query_len makes every A
  // lookup return 0 and the portal answers nothing at all -- with no error anywhere, because 0
  // is also what a malformed packet returns.
  //
  // Not routed through guarded_reply(): the point of the test is one buffer, and the reply needs
  // sixteen bytes past the query's end, which is where the guard page is.
  uint8_t shared[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t apart[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  uint8_t reference[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];

  size_t qn = make_query(apart, "connectivitycheck.gstatic.com");
  size_t expected = comfoair_captive_dns_reply(apart, qn, AP_ADDRESS, reference, sizeof reference);
  TEST_ASSERT_EQUAL_UINT(qn + 16, expected);

  memcpy(shared, apart, qn);
  size_t rn = comfoair_captive_dns_reply(shared, qn, AP_ADDRESS, shared, sizeof shared);
  TEST_ASSERT_EQUAL_UINT(expected, rn);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(reference, shared, rn);
}

void test_the_512_is_the_socket_loops_buffer_and_not_a_check_in_here(void) {
  // comfoair_captive_dns.h used to say a query above 512 bytes "is refused". It is not: nothing
  // in comfoair_captive_dns_reply() looks at query_len except to require twelve bytes of header
  // and to bound the question inside it. The 512 is where it actually lives --
  // comfoair_captive_dns_server.c hands recvfrom a 512-byte buffer and lwip drops the rest -- and
  // the header now says so.
  //
  // This is pinned rather than left implied because the sentence invited exactly one edit: add
  // `if (query_len > COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE) return 0;` to make the code match the
  // comment. That check belongs to nobody. The function is pure over a buffer and a length, the
  // buffer size is the caller's to choose, and a fuzzer or a future caller with a jumbo buffer
  // would silently get nothing.
  uint8_t q[900];
  uint8_t r[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
  memset(q, 0xEE, sizeof q);  // trailing junk, which is what an EDNS OPT looks like from here
  size_t qn = make_query(q, "example.com");
  TEST_ASSERT_EQUAL_UINT(qn + 16, guarded_reply(q, sizeof q, AP_ADDRESS, r, sizeof r));
}

void test_the_table_survives_a_caller_that_does_not_want_the_count(void) {
  // comfoair_captive.h said "`count` may not be NULL" while comfoair_captive_probe.c checked for
  // NULL and carried on. Two statements of one contract, neither tested, is how the next reader
  // ends up trusting whichever they read first. The code's answer is the one kept -- a firmware
  // that faults on a caller's mistake is worse than one that returns something useless -- and the
  // header now says what it does.
  TEST_ASSERT_EQUAL_PTR(comfoair_captive_table(nullptr), comfoair_captive_answer("/generate_204"));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_android_is_told_the_network_is_fine_with_the_status_it_asks_for);
  RUN_TEST(test_apple_gets_the_document_apple_serves_byte_for_byte);
  RUN_TEST(test_the_two_windows_probes_get_their_two_strings_and_neither_gets_a_newline);
  RUN_TEST(test_firefox_gets_its_newline_where_windows_does_not);
  RUN_TEST(test_no_probe_gets_a_redirect_and_none_gets_the_setup_page);
  RUN_TEST(test_no_answer_is_built_out_of_anything_the_device_knows);
  RUN_TEST(test_a_probe_answer_is_never_cached);
  RUN_TEST(test_the_table_is_seven_paths_and_the_lookup_finds_every_one);
  RUN_TEST(test_nothing_that_is_not_a_probe_is_answered_as_one);
  RUN_TEST(test_a_probe_path_is_matched_whole_and_never_as_a_prefix);
  RUN_TEST(test_stripping_the_query_string_stays_the_servers_job);
  RUN_TEST(test_a_name_lookup_is_answered_with_our_own_access_point_address);
  RUN_TEST(test_the_question_comes_back_byte_for_byte_including_its_case);
  RUN_TEST(test_the_address_that_goes_out_is_the_one_the_caller_gave);
  RUN_TEST(test_the_answer_expires_immediately_so_the_lie_dies_with_the_access_point);
  RUN_TEST(test_a_question_we_do_not_serve_is_answered_with_no_records_rather_than_silence);
  RUN_TEST(test_a_class_we_do_not_serve_is_answered_the_same_way);
  RUN_TEST(test_a_query_carrying_an_edns_record_is_still_answered);
  RUN_TEST(test_a_packet_too_short_to_be_a_question_is_not_answered);
  RUN_TEST(test_a_name_that_runs_past_the_end_of_the_packet_is_not_answered);
  RUN_TEST(test_a_name_that_never_ends_is_not_answered);
  RUN_TEST(test_a_name_longer_than_dns_allows_is_not_answered);
  RUN_TEST(test_the_longest_name_dns_does_allow_is_answered);
  RUN_TEST(test_a_compression_pointer_in_a_question_is_not_followed);
  RUN_TEST(test_a_reserved_label_type_is_not_answered);
  RUN_TEST(test_something_that_is_already_an_answer_is_never_answered);
  RUN_TEST(test_a_message_that_is_not_a_question_is_not_answered);
  RUN_TEST(test_a_packet_without_exactly_one_question_is_not_answered);
  RUN_TEST(test_an_answer_that_would_not_fit_is_not_half_written);
  RUN_TEST(test_nothing_here_falls_over_on_a_null);
  RUN_TEST(test_the_reply_may_be_built_on_top_of_the_query_it_answers);
  RUN_TEST(test_the_512_is_the_socket_loops_buffer_and_not_a_check_in_here);
  RUN_TEST(test_the_table_survives_a_caller_that_does_not_want_the_count);
  return UNITY_END();
}
