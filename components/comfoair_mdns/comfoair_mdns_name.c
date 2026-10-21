// The pure half of comfoair_mdns: three strings and their arithmetic, decided without a socket,
// a clock or a MAC. Everything that needs the radio is in comfoair_mdns.c, behind
// #ifdef ESP_PLATFORM; everything here is what test/test_mdns_name can actually hold to account.
//
// NO ESP-IDF HEADER MAY APPEAR IN THIS FILE. The host build compiles it, and comfoair_mdns.h is
// reachable from comfoair_button_reset.c, so an include here lands in the button suites too.
#include <string.h>

#include "comfoair_mdns.h"

// Transcribed from comfoair_net_start.c's build_ap_ssid(), which is the ONE thing this string has
// to agree with: `snprintf(g_net_ap_ssid, ..., "comfoair-%02x%02x", mac[4], mac[5])`. Not shared as
// a macro, and that is deliberate -- comfoair_net does not depend on this component and must not
// start to: the dependency runs comfoair_mdns -> comfoair_net, never back. What keeps the two in
// step is that test_mdns_name transcribes the same literal a third time and hardware scenario
// HV-PROV-02 compares the SSID and the name by eye.
static const char PREFIX[] = "comfoair-";

#define PREFIX_LEN (sizeof PREFIX - 1)

#define SHORT_ID_LEN (COMFOAIR_MDNS_HOST_MAX - PREFIX_LEN)

// The five TXT keys, in the order they go out. `path` and `api` are constants rather
// than facts: RFC 6763 §7.1 says an `_http._tcp` record without `path` is one a client cannot turn
// into a URL, and `api` is the entry point a future client would browse for.
static const char KEY_BOARD[] = "board";
static const char KEY_VER[]   = "ver";
static const char KEY_ID[]    = "id";
static const char KEY_PATH[]  = "path";
static const char KEY_API[]   = "api";

static const char VALUE_PATH[] = "/";
static const char VALUE_API[]  = "/api";

// One hex digit, or '0'. Not a validation and not a rejection: this function has no way to report
// one and no caller that could act on it, so it produces the only thing that is always safe -- a
// legal DNS label character. See the header: the single producer of `short_id` can emit nothing but
// lower-case hex, and a contract that rests on that is a contract with a component boundary
// through the middle of it.
static char hex_or_zero(char c)
{
    if (c >= '0' && c <= '9')
        return c;
    if (c >= 'a' && c <= 'f')
        return c;
    // Upper case is legal in DNS and wrong on a sticker: mDNS matches case-insensitively, so
    // folding it here costs nothing and keeps ONE printable spelling of the name.
    if (c >= 'A' && c <= 'F')
        return (char)(c - 'A' + 'a');
    return '0';
}

void comfoair_mdns_hostname(const char *short_id, char out[COMFOAIR_MDNS_HOST_MAX + 1])
{
    if (out == NULL)
        return;

    memcpy(out, PREFIX, PREFIX_LEN);
    for (size_t i = 0; i < SHORT_ID_LEN; i++) {
        // A short input is padded with '0' rather than truncating the name. The alternative is a
        // label of unpredictable length, and the length being fixed at thirteen is half of what
        // makes this string printable on a case.
        const char c = (short_id != NULL && strlen(short_id) > i) ? short_id[i] : '0';
        out[PREFIX_LEN + i] = hex_or_zero(c);
    }
    out[COMFOAIR_MDNS_HOST_MAX] = '\0';
}

// How many octets of `s` may be kept without cutting a UTF-8 sequence in half, given a ceiling.
//
// A continuation octet is 10xxxxxx (RFC 3629 §3). Walking back off them lands on the lead octet of
// the sequence that the ceiling fell inside, and that lead octet goes too -- what is left is
// whole. The loop is bounded by `limit` and cannot run off the front, because a string that is all
// continuation octets is not something this can produce a valid prefix of anyway, and the `i > 0`
// guard stops at zero.
static size_t utf8_prefix(const char *s, size_t limit)
{
    size_t i = limit;
    while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80)
        i--;
    return i;
}

void comfoair_mdns_instance(const char *device_name, const char *hostname,
                            char out[COMFOAIR_MDNS_INSTANCE_MAX + 1])
{
    if (out == NULL)
        return;
    out[0] = '\0';

    // Empty is the state of the stored document before the first save, and a service record with
    // no instance name is invalid -- so the host name stands in. NOT the other way round: the
    // owner's name never becomes the host name: renaming would re-key bookmarks and kill the open
    // tab, and a free-form name (spaces, Cyrillic, emoji) has no lossless DNS label.
    const char *src = (device_name != NULL && device_name[0] != '\0') ? device_name : hostname;
    if (src == NULL)
        return;

    size_t len = strlen(src);
    if (len > COMFOAIR_MDNS_INSTANCE_MAX)
        // 63 OCTETS, not 63 characters (RFC 6763 §4.1.1). Unreachable from the settings page --
        // COMFOAIR_CONFIG_NAME_MAX is 32 -- and written anyway, because "no caller can reach this"
        // is the reasoning that makes a branch wrong the first time somebody else calls it.
        len = utf8_prefix(src, COMFOAIR_MDNS_INSTANCE_MAX);

    memcpy(out, src, len);
    out[len] = '\0';
}

// Whether a pair can go on the wire at all. RFC 6763 §6.1: one item is one length-prefixed string,
// so key + '=' + value must fit in 255 octets. An empty value is refused too, and that is the case
// that actually happens: esp_app_desc_t.version is a 32-byte FIELD, not a string, and an image
// built without a version reads back as a leading NUL.
//
// DROPPED, NEVER TRUNCATED. Half a version string answers "which firmware is this" with something
// that looks like an answer, and the whole reason `ver` is in TXT is to be trusted after an OTA.
static bool pair_ok(const char *key, const char *value)
{
    if (key == NULL || value == NULL || value[0] == '\0')
        return false;
    return strlen(key) + 1 + strlen(value) <= COMFOAIR_MDNS_ITEM_MAX;
}

static size_t put(comfoair_mdns_txt_t *out, size_t cap, size_t n, const char *key,
                  const char *value)
{
    if (n >= cap || !pair_ok(key, value))
        return n;
    out[n].key   = key;
    out[n].value = value;
    return n + 1;
}

size_t comfoair_mdns_txt(const comfoair_mdns_facts_t *facts, comfoair_mdns_txt_t *out, size_t cap)
{
    if (facts == NULL || out == NULL)
        return 0;

    size_t n = 0;
    n        = put(out, cap, n, KEY_BOARD, facts->board);
    n        = put(out, cap, n, KEY_VER, facts->version);
    n        = put(out, cap, n, KEY_ID, facts->device_id);
    n        = put(out, cap, n, KEY_PATH, VALUE_PATH);
    n        = put(out, cap, n, KEY_API, VALUE_API);
    return n;
}

size_t comfoair_mdns_copy_field(char *out, size_t cap, const char *src)
{
    if (out == NULL || cap == 0)
        return 0;
    out[0] = '\0';
    if (src == NULL)
        return 0;

    // NOT strncpy, NOT snprintf("%s"): both are told to read until a terminator, and the source may
    // not have one. The loop's bound is the DESTINATION's, so nothing past `cap - 1` octets of the
    // source is ever touched -- which is what makes a 32-byte esp_app_desc_t.version safe to pass
    // when `cap` is 33.
    size_t i = 0;
    for (; i + 1 < cap && src[i] != '\0'; i++)
        out[i] = src[i];
    out[i] = '\0';
    return i;
}

size_t comfoair_mdns_txt_bytes(const comfoair_mdns_txt_t *items, size_t n)
{
    if (items == NULL)
        return 0;

    size_t total = 0;
    for (size_t i = 0; i < n; i++) {
        if (items[i].key == NULL || items[i].value == NULL)
            continue;
        // One length octet per item, then `key`, `=`, `value` -- the shape RFC 6763 §6.1 puts on
        // the wire, which is what the 200-octet budget of §6.2 is measured against. NOT sizeof of
        // anything in this process: the C strings and the pointers around them are not what
        // travels.
        total += 1 + strlen(items[i].key) + 1 + strlen(items[i].value);
    }
    return total;
}
