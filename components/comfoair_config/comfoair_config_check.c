// comfoair_config: what a value has to look like to be storable -- one checker per field, the
// Wi-Fi record predicate and the owner-facing error sentences. Read comfoair_config.h first; the
// component's shape is in README.md, Implementation.
#include "comfoair_config.h"
#include "comfoair_config_internal.h"

#include <string.h>

// --- small things ----------------------------------------------------------------------------

// Control characters and DEL. Everything at 0x80 and above is left alone: those bytes are UTF-8
// and a device called "Вентиляция" is a device with a name, not an error. The renderer escapes
// what JSON needs escaped (comfoair_api_escape_json), so this is not the place to police it.
static bool is_control(unsigned char c) { return c < 0x20 || c == 0x7f; }

// NULL is treated as the empty string throughout, one rule for every checker: a value that is
// not there is not a malformed value. Whether empty is ACCEPTABLE is then each field's own
// answer -- it is for a broker host, it is not for a topic prefix.
static const char *or_empty(const char *s) { return s != NULL ? s : ""; }

// --- what is acceptable ------------------------------------------------------------------------

comfoair_config_err_t comfoair_config_check_ssid(const uint8_t *ssid, size_t len)
{
    if (ssid == NULL || len == 0 || len > COMFOAIR_CONFIG_SSID_MAX)
        return COMFOAIR_CONFIG_ERR_SSID;

    // wifi_sta_config_t carries an ssid[32] and no length beside it
    // (esp_wifi_types_generic.h:559-593), so a name shorter than the array is terminated and an
    // embedded NUL cannot be expressed at all: the driver would associate with a shorter name
    // than the one stored, and nothing in the symptom would say so.
    for (size_t i = 0; i < len; i++)
        if (ssid[i] == 0)
            return COMFOAIR_CONFIG_ERR_SSID;
    return COMFOAIR_CONFIG_OK;
}

comfoair_config_err_t comfoair_config_check_psk(const uint8_t *psk, size_t len)
{
    // An open network is a network. Refusing this would make the device impossible to put on
    // one, and a guest network with no key is the normal case in half the houses that have one.
    if (len == 0)
        return COMFOAIR_CONFIG_OK;

    if (psk == NULL || len < COMFOAIR_CONFIG_PSK_MIN || len > COMFOAIR_CONFIG_PSK_MAX)
        return COMFOAIR_CONFIG_ERR_PSK;

    // wpa_supplicant measures the key with strlen() (rsn_supp/wpa.c:2517), so a NUL inside it
    // does not truncate the value -- it changes which BRANCH runs, and a 64-byte key with a NUL
    // at byte ten is run through pbkdf2_sha1 as a ten-character passphrase.
    for (size_t i = 0; i < len; i++)
        if (psk[i] == 0)
            return COMFOAIR_CONFIG_ERR_PSK;

    if (len == COMFOAIR_CONFIG_PSK_MAX) {
        // 64 characters is not a long passphrase, it is the 256-bit key written as hex, and
        // wpa_supplicant decides that by LENGTH ALONE: 64 goes to hexstr2bin, anything else to
        // pbkdf2_sha1 (rsn_supp/wpa.c:2517-2524). When hexstr2bin fails there the function
        // simply returns -- no PMK is computed, no error is raised, and the device silently
        // never associates. Refused here, because on the way out it is not an error, it is
        // silence.
        for (size_t i = 0; i < len; i++)
            if (!is_hex((char)psk[i]))
                return COMFOAIR_CONFIG_ERR_PSK;
    }
    return COMFOAIR_CONFIG_OK;
}

// A hostname, an IPv4 literal or an IPv6 literal, and nothing else. Underscore is in the set
// although RFC 952 does not allow it: real LAN names contain one, resolvers answer them, and a
// store that refuses a broker the owner can ping is a bug they cannot work around. `/` is out,
// which is what makes "mqtt://broker" a refusal the owner can read rather than a device that
// resolves nothing for ever -- the port is its own field, so a URL here can never be honoured.
static bool host_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '-' || c == '_' || c == ':' || c == '[' || c == ']';
}

comfoair_config_err_t comfoair_config_check_host(const char *host)
{
    const char *h = or_empty(host);
    // No broker at all is a supported configuration: the unit ventilates the house whether or
    // not anyone runs one, and AGENTS.md's rule that nothing reboots because a peer is absent
    // starts with being able to say "I do not use MQTT".
    if (h[0] == '\0')
        return COMFOAIR_CONFIG_OK;

    const size_t len = strlen(h);
    // RFC 1035 2.3.4, and deliberately not a rounder smaller number: a cap that refuses a name
    // the owner's resolver answers is a bug report nobody can act on.
    if (len > COMFOAIR_CONFIG_HOST_MAX)
        return COMFOAIR_CONFIG_ERR_HOST;
    for (size_t i = 0; i < len; i++)
        if (!host_char(h[i]))
            return COMFOAIR_CONFIG_ERR_HOST;
    return COMFOAIR_CONFIG_OK;
}

comfoair_config_err_t comfoair_config_check_port(uint32_t port)
{
    // uint32_t, not uint16_t, and this is the whole reason: 70000 arrives from a JSON body and
    // has to be REFUSED. Taken as a uint16_t it becomes 4464, and the device then talks to a
    // port the owner never typed -- "validated" quietly turned into "accepted something else".
    if (port == 0 || port > 65535)
        return COMFOAIR_CONFIG_ERR_PORT;
    return COMFOAIR_CONFIG_OK;
}

// Length, and nothing else, for both broker credentials. MQTT 3.1.1 3.1.3 makes the user name a
// UTF-8 string and the password binary data; a store that decides which byte sequences a broker
// is allowed to accept refuses somebody's real credential, and there is nothing on the other
// side to protect -- a credential the broker rejects is a connection error the owner can read.
comfoair_config_err_t comfoair_config_check_user(const char *user)
{
    return strlen(or_empty(user)) <= COMFOAIR_CONFIG_USER_MAX ? COMFOAIR_CONFIG_OK
                                                             : COMFOAIR_CONFIG_ERR_USER;
}

comfoair_config_err_t comfoair_config_check_broker_password(const char *password)
{
    return strlen(or_empty(password)) <= COMFOAIR_CONFIG_BROKER_PASS_MAX
               ? COMFOAIR_CONFIG_OK
               : COMFOAIR_CONFIG_ERR_BROKER_PASSWORD;
}

comfoair_config_err_t comfoair_config_check_prefix(const char *prefix)
{
    const char *p = or_empty(prefix);
    // Empty is refused rather than defaulted because this function does not know the device id
    // and must not invent one; comfoair_config_sanitize() does know it and puts the default
    // back. An empty prefix would publish to topics beginning with a slash.
    if (p[0] == '\0')
        return COMFOAIR_CONFIG_ERR_PREFIX;

    const size_t len = strlen(p);
    if (len > COMFOAIR_CONFIG_PREFIX_MAX)
        return COMFOAIR_CONFIG_ERR_PREFIX;
    // $SYS and its neighbours are the broker's own tree. Publishing into it is refused by every
    // broker worth using and accepted silently by the rest, which is worse.
    if (p[0] == '$')
        return COMFOAIR_CONFIG_ERR_PREFIX;
    // Topics are built as prefix + "/" + entity, so a leading or trailing slash makes an empty
    // level -- `comfoair//state`. That is legal MQTT and the worst kind of wrong: it works, it
    // is subscribable, and it does not match the topic anybody types by hand.
    if (p[0] == '/' || p[len - 1] == '/')
        return COMFOAIR_CONFIG_ERR_PREFIX;

    for (size_t i = 0; i < len; i++) {
        // MQTT 3.1.1 4.7.1: `+` and `#` are subscription syntax and are not legal in a topic
        // being published to. A broker's answer to one is to drop the connection, which reads
        // from here as a broker that keeps disconnecting for no reason anybody can see.
        if (p[i] == '+' || p[i] == '#')
            return COMFOAIR_CONFIG_ERR_PREFIX;
        if (p[i] == ' ' || is_control((unsigned char)p[i]))
            return COMFOAIR_CONFIG_ERR_PREFIX;
        if (p[i] == '/' && p[i + 1] == '/')
            return COMFOAIR_CONFIG_ERR_PREFIX;
    }
    return COMFOAIR_CONFIG_OK;
}

comfoair_config_err_t comfoair_config_check_name(const char *name)
{
    const char *n = or_empty(name);
    // Same reason as the prefix: a nameless device in Home Assistant is not a configuration
    // anyone wants, and the default that replaces it needs a device id this function has not
    // got.
    if (n[0] == '\0')
        return COMFOAIR_CONFIG_ERR_NAME;

    const size_t len = strlen(n);
    if (len > COMFOAIR_CONFIG_NAME_MAX)
        return COMFOAIR_CONFIG_ERR_NAME;
    // A pasted newline is the realistic case and it ends up in a log line and an MQTT payload.
    for (size_t i = 0; i < len; i++)
        if (is_control((unsigned char)n[i]))
            return COMFOAIR_CONFIG_ERR_NAME;
    return COMFOAIR_CONFIG_OK;
}

comfoair_config_err_t comfoair_config_check_ui_password(const char *password)
{
    const char *p = or_empty(password);
    const size_t len = strlen(p);
    if (len < COMFOAIR_CONFIG_UI_PASS_MIN || len > COMFOAIR_CONFIG_UI_PASS_MAX)
        return COMFOAIR_CONFIG_ERR_UI_PASSWORD;
    for (size_t i = 0; i < len; i++)
        if (is_control((unsigned char)p[i]))
            return COMFOAIR_CONFIG_ERR_UI_PASSWORD;
    return COMFOAIR_CONFIG_OK;
}

bool comfoair_config_wifi_usable(const comfoair_wifi_t *wifi)
{
    if (wifi == NULL)
        return false;
    // BOTH checkers, and the same two comfoair_config_sanitize() runs. DO NOT reduce this to a
    // length test on the SSID "because that is what asking whether a network is stored means": that
    // is what it did, and it made the access point decision disagree with the document. One flipped
    // bit in the PSK length byte -- 0x08 to 0x88 -- was a credential the provisioning state machine
    // believed in and a record sanitize() cleared, which is a device with no network, no access
    // point and no way back (docs/architecture/failure-model.md). check_ssid() already refuses a
    // zero length, so the emptiness test is inside this and not beside it.
    return comfoair_config_check_ssid(wifi->ssid, wifi->ssid_len) == COMFOAIR_CONFIG_OK &&
           comfoair_config_check_psk(wifi->psk, wifi->psk_len) == COMFOAIR_CONFIG_OK;
}

bool comfoair_config_wifi_from_legacy(comfoair_wifi_t *out, const char *ssid, const char *psk)
{
    if (out == NULL)
        return false;
    // Zeroed before anything is decided, so that every `return false` below leaves a record that
    // stores nothing rather than half a pair -- the caller is a migration whose next statement
    // writes this to flash.
    memset(out, 0, sizeof *out);

    const char  *s   = or_empty(ssid);
    const char  *p   = or_empty(psk);
    const size_t s_n = strlen(s);
    const size_t p_n = strlen(p);
    // Bounded before the copy and validated after it, in that order: the lengths are what makes
    // the memcpy safe, and comfoair_config_wifi_usable() is what makes the RECORD acceptable.
    if (s_n > COMFOAIR_CONFIG_SSID_MAX || p_n > COMFOAIR_CONFIG_PSK_MAX)
        return false;
    memcpy(out->ssid, s, s_n);
    memcpy(out->psk, p, p_n);
    out->ssid_len = (uint8_t)s_n;
    out->psk_len  = (uint8_t)p_n;

    // The same predicate everything else asks, because a migration is a point of acceptance like
    // any other and this component has exactly one idea of what a usable pair is. What it refuses
    // here is a pair that WAS stored by an older build and cannot be used by this one: a
    // 64-character key that is not hex, a passphrase below the WPA2 floor. Those are not moved,
    // and the caller must not erase the source it read them from.
    if (!comfoair_config_wifi_usable(out)) {
        memset(out, 0, sizeof *out);
        return false;
    }
    return true;
}

const char *comfoair_config_strerror(comfoair_config_err_t err)
{
    // Not one of these quotes the value it rejected. They travel in an HTTP body AND through the
    // log ring, and /api/log is readable by whoever can reach the device
    // (docs/architecture/security.md) -- so a message that echoed the input would publish a
    // password the first time somebody typed one into the wrong box.
    switch (err) {
    case COMFOAIR_CONFIG_OK:
        return "accepted";
    case COMFOAIR_CONFIG_ERR_SSID:
        return "the network name must be 1 to 32 characters";
    case COMFOAIR_CONFIG_ERR_PSK:
        return "the network password must be 8 to 63 characters, or exactly 64 hexadecimal "
               "digits, or empty for an open network";
    case COMFOAIR_CONFIG_ERR_WIFI_PAIR:
        return "the network name and its password are stored as one record: send both, and "
               "re-enter the password whenever the name changes";
    case COMFOAIR_CONFIG_ERR_HOST:
        return "the broker address must be a host name or an IP address, with no scheme and no "
               "port";
    case COMFOAIR_CONFIG_ERR_PORT:
        return "the broker port must be between 1 and 65535";
    case COMFOAIR_CONFIG_ERR_USER:
        return "the broker user name is longer than this device can store";
    case COMFOAIR_CONFIG_ERR_BROKER_PASSWORD:
        return "the broker password is longer than this device can store";
    case COMFOAIR_CONFIG_ERR_PREFIX:
        return "the topic prefix must be 1 to 64 characters, without spaces, + or #, and without "
               "an empty level";
    case COMFOAIR_CONFIG_ERR_NAME:
        return "the device name must be 1 to 32 characters and contain no control characters";
    case COMFOAIR_CONFIG_ERR_UI_PASSWORD:
        return "the web password must be 8 to 128 characters and contain no control characters";
    case COMFOAIR_CONFIG_ERR_NO_HASH:
        return "this device could not secure the password just now; nothing was changed";
    case COMFOAIR_CONFIG_ERR_READ_ONLY:
        return "these settings were written by a newer firmware and cannot be changed until the "
               "device is reset";
    case COMFOAIR_CONFIG_ERR_NO_DOCUMENT:
        return "the request carried no settings, so nothing was saved";
    }
    // A code from a build that is not this one. Saying something is better than a NULL a
    // renderer will dereference.
    return "the settings were refused";
}
