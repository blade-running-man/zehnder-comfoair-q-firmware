// The half of comfoair_mdns that can be decided without a socket: the name the owner types, the
// name a service browser shows, and the five TXT pairs that go out by multicast to everybody on
// the link (docs/device/networking.md, mDNS).
//
// WHAT THIS SUITE DOES NOT CATCH, said first and not in a footnote, because for the first point
// the guarantee is a reviewer's, not a test's:
//
//   1. `short_id` ARRIVES AS AN ARGUMENT. Every case below hands it in. The whole trap is WHICH
//      MAC it was made from -- ESP_MAC_WIFI_SOFTAP (the SSID's, the one printed on the sticker)
//      and not ESP_MAC_WIFI_STA (the MQTT identity's) -- and those two differ by exactly +1 in
//      the last octet (esp_hw_support/mac_addr.c:369-386). Swapping one for the
//      other inside comfoair_net_short_id() changes no format and breaks no case here. The only
//      guard is that there is ONE producer and a comment over it; the observation belongs to the
//      bench: HV-PROV-02 (docs/hardware-verification/05-provisioning-and-recovery.md).
//   2. `build_ap_ssid()` is static in comfoair_net_start.c and does not compile on the host at
//      all, so "the SSID did not move" cannot be asserted here. The format below is TRANSCRIBED
//      from comfoair_net_start.c ("comfoair-%02x%02x"), not imported -- a test that reads the
//      format out of the code it checks proves nothing -- and HV-PROV-18 compares the two strings
//      by eye.
//   3. Probing, conflict resolution, the goodbye packet of mdns_free(), the second A record on the
//      access-point interface, and re-announcement after a DHCP lease change are all packets on a
//      link. None of them is reachable from here.
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <string>

#include <unity.h>

#include "comfoair_config.h"
#include "comfoair_mdns.h"

void setUp(void) {}
void tearDown(void) {}

// TRANSCRIBED from comfoair_net_start.c:58 -- `snprintf(g_net_ap_ssid, ..., "comfoair-%02x%02x",
// mac[4], mac[5])`. The whole point of the .local name is that it and this SSID are one string (the
// table in docs/device/networking.md, Names and identity), so it is spelled out here rather than
// taken from the header under test.
static const char *SSID_PREFIX = "comfoair-";

// RFC 6763 §6.1: one TXT item is ONE length-prefixed string, so `key` + `=` + `value` may not
// exceed 255 octets. Derived here rather than named in the header, because it is a property of the
// KEY: `board` is the longest of the five, so this is the tightest of the five
// value ceilings and the one an over-long board name meets first.
static const size_t BOARD_VALUE_MAX = COMFOAIR_MDNS_ITEM_MAX - (sizeof("board") - 1) - 1;

static const char *txt_value(const comfoair_mdns_txt_t *items, size_t n, const char *key) {
  for (size_t i = 0; i < n; i++)
    if (items[i].key != NULL && strcmp(items[i].key, key) == 0)
      return items[i].value;
  return NULL;
}

static comfoair_mdns_facts_t typical_facts(void) {
  comfoair_mdns_facts_t f{};
  f.board     = "m5stack-nanoc6";
  f.version   = "a837872-dirty";
  f.device_id = "9c139ed48ae4";
  return f;
}

// --- the host name ---------------------------------------------------------------------------

// The host name is "comfoair-XXXX": the same four hex characters the access point is named
// after, in the same format. The device on the bench answers to `comfoair-8ae5` on the air, so
// `8ae5` is what a .local name has to be built from -- byte for byte, or the sticker lies.
static void test_hostname_is_comfoair_plus_four_hex(void) {
  char host[COMFOAIR_MDNS_HOST_MAX + 1] = {0};
  comfoair_mdns_hostname("8ae5", host);
  TEST_ASSERT_EQUAL_STRING("comfoair-8ae5", host);

  // And it is the SSID's prefix followed by exactly the four characters handed in -- which is the
  // assertion that fails if somebody widens the name to the full MAC or drops the prefix the
  // phone's Wi-Fi list shows. Sixteen bits are enough for a person to tell two devices in one
  // house apart; the full MAC stays the identity, and is not a name anybody copies off a sticker.
  TEST_ASSERT_EQUAL_STRING_LEN(SSID_PREFIX, host, strlen(SSID_PREFIX));
  TEST_ASSERT_EQUAL_STRING("8ae5", host + strlen(SSID_PREFIX));
  TEST_ASSERT_EQUAL_size_t(strlen(SSID_PREFIX) + 4, strlen(host));
  TEST_ASSERT_EQUAL_size_t(COMFOAIR_MDNS_HOST_MAX, strlen(host));
}

// RFC 6762 §3: a .local host name is ONE DNS label. RFC 1035 §2.3.1 and RFC 1123 §2.1 give the
// alphabet -- letters, digits, hyphen -- and forbid a leading or trailing hyphen; 63 octets is the
// label ceiling. An underscore is legal in a DNS-SD SERVICE type ("_http") and illegal in a host
// name, which is the confusion this case exists to stop.
//
// Fed garbage on purpose. comfoair_net_short_id() can only produce hex, but a contract that holds
// only because of its single caller is not a contract -- and the caller is in another component.
static void test_hostname_is_a_legal_dns_label(void) {
  const char *inputs[] = {"8ae5", "0000", "ffff", "-a-b", "AB_c", "zz!\x7f", "", "toolong"};

  for (size_t i = 0; i < sizeof inputs / sizeof inputs[0]; i++) {
    char host[COMFOAIR_MDNS_HOST_MAX + 1] = {0};
    comfoair_mdns_hostname(inputs[i], host);

    const size_t len = strlen(host);
    TEST_ASSERT_TRUE_MESSAGE(len > 0, inputs[i]);
    TEST_ASSERT_TRUE_MESSAGE(len <= 63, inputs[i]);
    TEST_ASSERT_TRUE_MESSAGE(len == COMFOAIR_MDNS_HOST_MAX, inputs[i]);
    TEST_ASSERT_TRUE_MESSAGE(host[0] != '-', inputs[i]);
    TEST_ASSERT_TRUE_MESSAGE(host[len - 1] != '-', inputs[i]);

    for (size_t c = 0; c < len; c++) {
      const char ch = host[c];
      const bool legal =
          (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-';
      TEST_ASSERT_TRUE_MESSAGE(legal, inputs[i]);
      // Named separately from the alphabet above, because "_" is the one character a reader is
      // most likely to think is allowed here: DNS-SD service types are full of them.
      TEST_ASSERT_TRUE_MESSAGE(ch != '_', inputs[i]);
      // Upper case is legal in DNS and WRONG here: mDNS compares case-insensitively but the owner
      // reads this off a sticker, and one string is easier to print than two.
      TEST_ASSERT_TRUE_MESSAGE(!(ch >= 'A' && ch <= 'Z'), inputs[i]);
    }
  }
}

// One MAC, one name, for the life of the device: the name is on a sticker, in a bookmark, in a
// browser history, and in somebody's /etc/hosts. It must not depend on when it was asked for,
// nor on what was asked for before it.
static void test_the_name_is_deterministic_for_a_mac(void) {
  char first[COMFOAIR_MDNS_HOST_MAX + 1]  = {0};
  char second[COMFOAIR_MDNS_HOST_MAX + 1] = {0};
  char other[COMFOAIR_MDNS_HOST_MAX + 1]  = {0};

  comfoair_mdns_hostname("8ae5", first);
  comfoair_mdns_hostname("1234", other);
  comfoair_mdns_hostname("8ae5", second);

  TEST_ASSERT_EQUAL_STRING(first, second);
  // Two devices in one house are distinguishable -- the reason sixteen bits are carried at all
  // (comfoair_net_start.c, the comment over build_ap_ssid()).
  TEST_ASSERT_EQUAL_STRING("comfoair-1234", other);

  // Both octets reach the name. `mac[4]` only would collide across 255 of every 256 devices, and
  // the SSID it has to match uses both.
  char a[COMFOAIR_MDNS_HOST_MAX + 1] = {0};
  char b[COMFOAIR_MDNS_HOST_MAX + 1] = {0};
  comfoair_mdns_hostname("8ae5", a);
  comfoair_mdns_hostname("8af5", b);
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
  comfoair_mdns_hostname("9ae5", b);
  TEST_ASSERT_TRUE(strcmp(a, b) != 0);
}

// --- the instance name -----------------------------------------------------------------------

// The owner's name for the device is the DNS-SD instance name and NOTHING ELSE -- the host name
// does not follow it. A record with no instance name is invalid on the wire, so this function
// must not be able to produce one.
//
// THE FALLBACK IS DEFENSIVE, NOT THE NORMAL CASE, and the case below this one is the normal case.
// An earlier version of this comment said the stored document carries an empty device_name until
// the first save. It does not: comfoair_config_sanitize() refills the field from default_name()
// whenever it is empty or invalid (comfoair_config.c:308-310), so a booted device always has one.
// The branch is kept and tested because "no caller can reach this" is what makes a branch wrong
// the first time somebody else calls it -- and because an unterminated field read out of NVS is
// exactly the kind of thing that would.
static void test_instance_name_falls_back_to_the_hostname_when_the_name_is_empty(void) {
  char out[COMFOAIR_MDNS_INSTANCE_MAX + 1] = {0};

  comfoair_mdns_instance("", "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_STRING("comfoair-8ae5", out);

  comfoair_mdns_instance(NULL, "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_STRING("comfoair-8ae5", out);
}

// COMFOAIR_CONFIG_NAME_MAX is 32 (comfoair_config.h:149) and RFC 6763 §4.1.1 allows 63 octets, so
// the truncating branch below is DEAD in production. This case is what makes that statement
// checkable: if NAME_MAX ever grows past the instance ceiling, this fails on the host instead of
// the device shipping a silently cut name.
static void test_instance_name_passes_a_thirty_two_byte_name_through_unchanged(void) {
  char name[COMFOAIR_CONFIG_NAME_MAX + 1];
  memset(name, 'n', COMFOAIR_CONFIG_NAME_MAX);
  name[COMFOAIR_CONFIG_NAME_MAX] = '\0';
  TEST_ASSERT_TRUE(COMFOAIR_CONFIG_NAME_MAX <= COMFOAIR_MDNS_INSTANCE_MAX);

  char out[COMFOAIR_MDNS_INSTANCE_MAX + 1] = {0};
  comfoair_mdns_instance(name, "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_STRING(name, out);

  // Spaces, slashes and anything else non-control are legal here and must survive: this is the
  // "user-friendly" string of RFC 6763 §4.1.1, not a DNS label. comfoair_config_check_name()
  // passes them (comfoair_config_check.c:175-178), which is why they may not be transliterated into
  // the host name: the mapping into one DNS label loses information, collides (`Vent 1` and
  // `Vent-1` give one label), and would make the device unreachable by name over an emoji.
  comfoair_mdns_instance("Вентиляция в подвале", "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_STRING("Вентиляция в подвале", out);
}

// THE BRANCH THAT ACTUALLY FIRES ON EVERY DEVICE, and until this case was added nothing covered
// it. comfoair_config_sanitize() has always put default_name()'s string in device_name
// (comfoair_config.c:145-164, :308-310), so what a browser shows is `ComfoAir Q 8ae4` -- and 8ae4
// is the tail of the STA MAC, while the host name is built from the ACCESS POINT's, one greater.
//
// The two are supposed to differ (docs/device/networking.md, Names and identity: the three names
// cannot all be reconciled, and the pair worth protecting is the sticker and the address bar).
// This case exists so that the difference is a decision written down in a test rather than a
// surprise on a bench, and so that hardware scenarios HV-PROV-18 and HV-PROV-23 are written against
// the string the device actually broadcasts.
static void test_the_default_device_name_is_what_a_service_browser_shows(void) {
  char out[COMFOAIR_MDNS_INSTANCE_MAX + 1] = {0};

  // Exactly what default_name() writes for the bench unit, transcribed rather than imported.
  comfoair_mdns_instance("ComfoAir Q 8ae4", "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_STRING("ComfoAir Q 8ae4", out);

  // And the host name is untouched by it -- the whole point of keeping two strings.
  char host[COMFOAIR_MDNS_HOST_MAX + 1] = {0};
  comfoair_mdns_hostname("8ae5", host);
  TEST_ASSERT_EQUAL_STRING("comfoair-8ae5", host);
  TEST_ASSERT_TRUE(strcmp(host, out) != 0);

  // The degenerate default, for a device whose id would not read: default_name() writes this one
  // (comfoair_config.c:163) and it is still not empty, so the fallback above still does not fire.
  comfoair_mdns_instance("ComfoAir Q", "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_STRING("ComfoAir Q", out);
}

// The bound is the DESTINATION's, so a source that is a fixed-size FIELD rather than a string is
// safe -- esp_app_get_description()->version is char[32] and an image that fills it carries no
// terminator (esp_app_format.h). This is the one place that hazard is checkable at all: the caller
// that meets it is the driver, which no test can reach.
//
// A GUARD PAGE, NOT A SENTINEL BYTE, and the first version of this case got that wrong. It put an
// 'X' just past a 32-byte field and asserted the 'X' did not come out -- which a strncpy-shaped
// implementation passes, because the DESTINATION bound stops the copy at 32 either way. The
// overrun is a READ, and a read leaves no trace in the output. Mutation M13 (take the bound from
// strlen(src) instead of from cap) was green against that case. It is not green against this one:
// the field ends flush against a PROT_NONE page, so anything that reads one octet further dies.
static void test_a_fact_is_copied_without_reading_past_the_field(void) {
  const size_t page = (size_t)sysconf(_SC_PAGESIZE);

  // Two pages; the second is then made unreadable. The 32-byte field sits at the very end of the
  // first, so src[32] is the first octet of the guard.
  char *base = (char *)mmap(nullptr, page * 2, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  TEST_ASSERT_TRUE(base != MAP_FAILED);
  TEST_ASSERT_EQUAL_INT(0, mprotect(base + page, page, PROT_NONE));

  char *field = base + page - 32;
  memset(field, 'v', 32);  // NO terminator: exactly what an image with a full version reads as

  char out[33] = {0};
  TEST_ASSERT_EQUAL_size_t(32, comfoair_mdns_copy_field(out, sizeof out, field));
  TEST_ASSERT_EQUAL_size_t(32, strlen(out));
  for (size_t i = 0; i < 32; i++)
    TEST_ASSERT_EQUAL_INT('v', out[i]);

  // A destination SHORTER than the field must stop earlier still, and must also not run on.
  char small[8] = {0};
  TEST_ASSERT_EQUAL_size_t(7, comfoair_mdns_copy_field(small, sizeof small, field));
  TEST_ASSERT_EQUAL_STRING("vvvvvvv", small);

  munmap(base, page * 2);

  // Terminated sources are unremarkable, and both degenerate arguments answer rather than crash.
  TEST_ASSERT_EQUAL_size_t(13, comfoair_mdns_copy_field(out, sizeof out, "a837872-dirty"));
  TEST_ASSERT_EQUAL_STRING("a837872-dirty", out);
  TEST_ASSERT_EQUAL_size_t(0, comfoair_mdns_copy_field(out, sizeof out, NULL));
  TEST_ASSERT_EQUAL_STRING("", out);
  TEST_ASSERT_EQUAL_size_t(0, comfoair_mdns_copy_field(NULL, sizeof out, "x"));
  TEST_ASSERT_EQUAL_size_t(0, comfoair_mdns_copy_field(out, 0, "x"));
}

// 63 OCTETS, not 63 characters, and a name is UTF-8. Cutting at octet 63 in the middle of a
// two-byte sequence produces a string no browser will render and some resolvers reject outright.
// The input here is longer than anything comfoair_config_check_name() will store -- see the case
// above -- and it is written anyway, because "the caller cannot reach this" is the reasoning that
// makes a branch wrong the first time somebody else calls it.
static void test_instance_name_never_splits_a_utf8_sequence(void) {
  // "я" is two octets (D1 8F). Thirty-two of them is 64 octets: the cut lands between the two
  // halves of the thirty-second letter.
  std::string cyrillic;
  for (int i = 0; i < 32; i++)
    cyrillic += "я";
  TEST_ASSERT_EQUAL_size_t(64, cyrillic.size());

  char out[COMFOAIR_MDNS_INSTANCE_MAX + 1] = {0};
  comfoair_mdns_instance(cyrillic.c_str(), "comfoair-8ae5", out);

  const size_t len = strlen(out);
  TEST_ASSERT_TRUE(len <= COMFOAIR_MDNS_INSTANCE_MAX);
  // 62 octets, not 63: the thirty-first letter ends there and the thirty-second may not be halved.
  TEST_ASSERT_EQUAL_size_t(62, len);
  TEST_ASSERT_EQUAL_STRING_LEN(cyrillic.c_str(), out, len);
  // No continuation octet may be left dangling at the end.
  TEST_ASSERT_TRUE(((unsigned char)out[len - 1] & 0xC0) != 0xC0);

  // A four-octet sequence (an emoji) on the boundary is the other half of the same rule.
  std::string emoji(60, 'x');
  emoji += "\xF0\x9F\x8C\x80";  // U+1F300, four octets: 60 + 4 = 64
  memset(out, 0, sizeof out);
  comfoair_mdns_instance(emoji.c_str(), "comfoair-8ae5", out);
  TEST_ASSERT_EQUAL_size_t(60, strlen(out));
  TEST_ASSERT_EQUAL_STRING_LEN(emoji.c_str(), out, 60);
}

// --- TXT -------------------------------------------------------------------------------------

// The TXT table of docs/device/networking.md (mDNS), and the list is closed. A sixth key is not a
// code review comment, it is this failure -- and the way to add one is to answer the question
// every field has to pass, because TXT goes by multicast to everyone on the link, the open access
// point included: "would I print this on the outside of the case?"
static void test_txt_keys_are_exactly_the_allowlist(void) {
  const comfoair_mdns_facts_t facts = typical_facts();
  comfoair_mdns_txt_t         items[COMFOAIR_MDNS_TXT_MAX];
  const size_t                n = comfoair_mdns_txt(&facts, items, COMFOAIR_MDNS_TXT_MAX);

  TEST_ASSERT_EQUAL_size_t(5, n);
  const char *expected[] = {"board", "ver", "id", "path", "api"};
  for (size_t i = 0; i < n; i++)
    TEST_ASSERT_EQUAL_STRING(expected[i], items[i].key);

  TEST_ASSERT_EQUAL_STRING("m5stack-nanoc6", txt_value(items, n, "board"));
  TEST_ASSERT_EQUAL_STRING("a837872-dirty", txt_value(items, n, "ver"));
  TEST_ASSERT_EQUAL_STRING("9c139ed48ae4", txt_value(items, n, "id"));
  // RFC 6763 §7.1: an _http._tcp record without `path` is one a client cannot turn into a URL.
  TEST_ASSERT_EQUAL_STRING("/", txt_value(items, n, "path"));
  TEST_ASSERT_EQUAL_STRING("/api", txt_value(items, n, "api"));

  // The cap is honoured rather than trusted: the driver passes a fixed array and a short one must
  // truncate, not write past the end.
  comfoair_mdns_txt_t few[2] = {};
  TEST_ASSERT_EQUAL_size_t(2, comfoair_mdns_txt(&facts, few, 2));
  TEST_ASSERT_EQUAL_STRING("board", few[0].key);
  TEST_ASSERT_EQUAL_size_t(0, comfoair_mdns_txt(&facts, items, 0));
}

// mDNS goes to 224.0.0.251 on every raised interface, and for this device "everyone on the link"
// includes the guest VLAN and everyone in radio range of the OPEN access point
// (comfoair_net_radio.c, ap.ap.authmode = WIFI_AUTH_OPEN). A field that asserts something about how
// protected the device is right now is the one class forbidden by construction, not discipline.
static void test_txt_never_contains_the_provisioned_flag(void) {
  const comfoair_mdns_facts_t facts = typical_facts();
  comfoair_mdns_txt_t         items[COMFOAIR_MDNS_TXT_MAX];
  const size_t                n = comfoair_mdns_txt(&facts, items, COMFOAIR_MDNS_TXT_MAX);

  // FIRST, or this case passes on an empty record and proves nothing -- which is exactly what it
  // did against the stub it was written against.
  TEST_ASSERT_EQUAL_size_t(COMFOAIR_MDNS_TXT_MAX, n);

  const char *forbidden[] = {"prov", "provisioned", "claimed", "ssid", "wifi", "psk",
                             "broker", "host", "user", "pass", "password", "topic",
                             "prefix", "auth", "secured", "pw", "salt", "hash"};
  for (size_t f = 0; f < sizeof forbidden / sizeof forbidden[0]; f++)
    TEST_ASSERT_NULL_MESSAGE(txt_value(items, n, forbidden[f]), forbidden[f]);
}

// Construction, not discipline: what the struct has no field for cannot reach the wire, however
// carelessly the driver is later edited. Three pointers, and a fourth one breaks this line --
// which sends whoever added it back to the "print it on the case?" question of the allowlist.
static void test_the_facts_struct_has_no_field_for_a_secret(void) {
  TEST_ASSERT_EQUAL_size_t(3 * sizeof(const char *), sizeof(comfoair_mdns_facts_t));

  // And the three are the board, the version and the device id. Setting every one of them by
  // name means a renamed or retyped field fails to compile here rather than silently going out
  // empty.
  comfoair_mdns_facts_t f{};
  f.board     = "b";
  f.version   = "v";
  f.device_id = "d";
  TEST_ASSERT_EQUAL_STRING("b", f.board);
  TEST_ASSERT_EQUAL_STRING("v", f.version);
  TEST_ASSERT_EQUAL_STRING("d", f.device_id);
}

// RFC 6763 §6.2 wants the TXT small enough that it, the SRV and the A record travel in one packet;
// its 200 octets are the figure budgeted against here. Measured on the LONGEST LEGAL inputs, not
// on today's: esp_app_desc_t.version is a 32-byte field (esp_app_format.h) and a board name is
// whatever the next board file calls itself.
static void test_txt_fits_the_single_packet_budget(void) {
  const comfoair_mdns_facts_t today = typical_facts();
  comfoair_mdns_txt_t         items[COMFOAIR_MDNS_TXT_MAX];
  size_t                      n = comfoair_mdns_txt(&today, items, COMFOAIR_MDNS_TXT_MAX);

  // 71 octets, counted by hand: five length prefixes plus 66 bytes of key=value.
  TEST_ASSERT_EQUAL_size_t(71, comfoair_mdns_txt_bytes(items, n));

  // The longest inputs this firmware can actually produce, each bounded by its own source and not
  // by the RFC: `ver` by esp_app_desc_t.version, a 32-byte field including its terminator; `id` by
  // COMFOAIR_CONFIG_DEVICE_ID_LEN; `board` by nothing at all -- it is a string literal in a board
  // file -- so it is held here to one DNS label, which is the longest name anybody would write for
  // a board and still be able to read it.
  //
  // DO NOT relax this into "whatever fits an item". A 249-octet board name is a legal TXT item
  // (the case below proves it) and would blow the 200-octet packet budget on its own; the ceiling
  // that keeps the record in one packet is this one, and the only thing holding it is that board
  // names are compile-time constants in this repository.
  std::string board(63, 'b');
  std::string ver(31, 'v');
  char        id[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1];
  memset(id, 'f', COMFOAIR_CONFIG_DEVICE_ID_LEN);
  id[COMFOAIR_CONFIG_DEVICE_ID_LEN] = '\0';

  const comfoair_mdns_facts_t worst = {board.c_str(), ver.c_str(), id};
  n                                 = comfoair_mdns_txt(&worst, items, COMFOAIR_MDNS_TXT_MAX);
  TEST_ASSERT_EQUAL_size_t(5, n);
  TEST_ASSERT_TRUE(comfoair_mdns_txt_bytes(items, n) <= COMFOAIR_MDNS_TXT_BUDGET);
  // Named, so that a change in any of the five shows up as a number and not as "still under 200".
  TEST_ASSERT_EQUAL_size_t(138, comfoair_mdns_txt_bytes(items, n));

  // An empty set is zero octets and not one: nothing to prefix.
  TEST_ASSERT_EQUAL_size_t(0, comfoair_mdns_txt_bytes(items, 0));
}

// The two ways a TXT pair goes onto the wire malformed. RFC 6763 §6.1: one item is one length-
// prefixed string, so 255 octets of key + '=' + value is the hard ceiling; §6.4: a key with no
// value at all is a different record from one with an empty value, and neither is what a caller
// that handed in "" meant.
//
// An embedded NUL is NOT representable through this API -- every value is a C string -- and saying
// so is the point: the reachable version of that bug is a value whose FIRST octet is the NUL, which
// is exactly what esp_app_get_description()->version reads as when the header is blank.
static void test_txt_rejects_a_value_with_a_nul_or_over_255_octets(void) {
  comfoair_mdns_txt_t items[COMFOAIR_MDNS_TXT_MAX];

  const comfoair_mdns_facts_t blank = {"m5stack-nanoc6", "", "9c139ed48ae4"};
  size_t                      n     = comfoair_mdns_txt(&blank, items, COMFOAIR_MDNS_TXT_MAX);
  TEST_ASSERT_EQUAL_size_t(4, n);
  TEST_ASSERT_NULL(txt_value(items, n, "ver"));
  // The other four are unaffected: one unusable fact drops one pair, not the record.
  TEST_ASSERT_EQUAL_STRING("m5stack-nanoc6", txt_value(items, n, "board"));
  TEST_ASSERT_EQUAL_STRING("9c139ed48ae4", txt_value(items, n, "id"));

  const comfoair_mdns_facts_t missing = {NULL, "a837872", NULL};
  n                                   = comfoair_mdns_txt(&missing, items, COMFOAIR_MDNS_TXT_MAX);
  TEST_ASSERT_EQUAL_size_t(3, n);
  TEST_ASSERT_NULL(txt_value(items, n, "board"));
  TEST_ASSERT_NULL(txt_value(items, n, "id"));
  TEST_ASSERT_EQUAL_STRING("a837872", txt_value(items, n, "ver"));

  // One octet past what "board=" leaves of 255.
  std::string huge(BOARD_VALUE_MAX + 1, 'b');
  const comfoair_mdns_facts_t big = {huge.c_str(), "a837872", "9c139ed48ae4"};
  n                               = comfoair_mdns_txt(&big, items, COMFOAIR_MDNS_TXT_MAX);
  TEST_ASSERT_NULL(txt_value(items, n, "board"));
  TEST_ASSERT_EQUAL_size_t(4, n);
  // And the last legal length is still accepted, or the ceiling is off by one.
  std::string edge(BOARD_VALUE_MAX, 'b');
  const comfoair_mdns_facts_t fits = {edge.c_str(), "a837872", "9c139ed48ae4"};
  n                                = comfoair_mdns_txt(&fits, items, COMFOAIR_MDNS_TXT_MAX);
  TEST_ASSERT_EQUAL_size_t(5, n);
  TEST_ASSERT_EQUAL_STRING(edge.c_str(), txt_value(items, n, "board"));

  TEST_ASSERT_EQUAL_size_t(0, comfoair_mdns_txt(NULL, items, COMFOAIR_MDNS_TXT_MAX));
}

// The image on the bench reports `a837872-dirty`, and the suffix is TRUE: the tree had
// uncommitted changes when it was built. `ver` exists so that "which firmware is on the device I
// cannot open" has a cheap answer and so that an OTA can be checked with `dns-sd -L` -- both are
// worth less if the string is tidied on the way out.
static void test_version_with_a_dirty_suffix_survives_unchanged(void) {
  const comfoair_mdns_facts_t facts = typical_facts();
  comfoair_mdns_txt_t         items[COMFOAIR_MDNS_TXT_MAX];
  const size_t                n = comfoair_mdns_txt(&facts, items, COMFOAIR_MDNS_TXT_MAX);

  TEST_ASSERT_EQUAL_STRING("a837872-dirty", txt_value(items, n, "ver"));

  const comfoair_mdns_facts_t tagged = {"m5stack-nanoc6", "v1.2.3-rc1+g1234567",
                                        "9c139ed48ae4"};
  const size_t m = comfoair_mdns_txt(&tagged, items, COMFOAIR_MDNS_TXT_MAX);
  TEST_ASSERT_EQUAL_STRING("v1.2.3-rc1+g1234567", txt_value(items, m, "ver"));
}

int main(int, char **) {
  UNITY_BEGIN();
  RUN_TEST(test_hostname_is_comfoair_plus_four_hex);
  RUN_TEST(test_hostname_is_a_legal_dns_label);
  RUN_TEST(test_the_name_is_deterministic_for_a_mac);
  RUN_TEST(test_instance_name_falls_back_to_the_hostname_when_the_name_is_empty);
  RUN_TEST(test_instance_name_passes_a_thirty_two_byte_name_through_unchanged);
  RUN_TEST(test_the_default_device_name_is_what_a_service_browser_shows);
  RUN_TEST(test_a_fact_is_copied_without_reading_past_the_field);
  RUN_TEST(test_instance_name_never_splits_a_utf8_sequence);
  RUN_TEST(test_txt_keys_are_exactly_the_allowlist);
  RUN_TEST(test_txt_never_contains_the_provisioned_flag);
  RUN_TEST(test_the_facts_struct_has_no_field_for_a_secret);
  RUN_TEST(test_txt_fits_the_single_packet_budget);
  RUN_TEST(test_txt_rejects_a_value_with_a_nul_or_over_255_octets);
  RUN_TEST(test_version_with_a_dirty_suffix_survives_unchanged);
  return UNITY_END();
}
