#include "comfoair_secrets.h"

#include <string.h>

static size_t s_compared;

static bool contains_ci(const char *haystack, const char *needle)
{
    const size_t n = strlen(needle);
    for (const char *p = haystack; *p; p++) {
        size_t i = 0;
        while (i < n && p[i] && (p[i] | 0x20) == (needle[i] | 0x20))
            i++;
        if (i == n)
            return true;
    }
    return false;
}

bool comfoair_secret_key(const char *key)
{
    if (key == NULL)
        return false;
    // Words, not key names. "psk" is here because Wi-Fi calls it that and nothing else in
    // this configuration contains those three letters.
    static const char *const words[] = {"password", "passwd", "psk", "secret", "token", "key"};
    for (size_t i = 0; i < sizeof words / sizeof words[0]; i++)
        if (contains_ci(key, words[i]))
            return true;
    return false;
}

const char *comfoair_secret_redact(const char *stored)
{
    if (stored == NULL || stored[0] == '\0')
        return "";
    return COMFOAIR_SECRET_SENTINEL;
}

comfoair_secret_action_t comfoair_secret_decide(bool has_stored, const char *submitted)
{
    // The key was absent from the submission: nothing was said about it, so nothing changes.
    if (submitted == NULL)
        return COMFOAIR_SECRET_KEEP;

    if (strcmp(submitted, COMFOAIR_SECRET_SENTINEL) == 0) {
        // Untouched field: the page rendered the sentinel and handed it straight back.
        if (has_stored)
            return COMFOAIR_SECRET_KEEP;
        // Nothing is stored, so this cannot be an untouched field -- it is the sentinel
        // being submitted as a value. Storing it would make a PUBLISHED string the password
        // of every device that has never been configured. Refuse and say so.
        return COMFOAIR_SECRET_REJECT;
    }

    if (submitted[0] == '\0')
        return has_stored ? COMFOAIR_SECRET_CLEAR : COMFOAIR_SECRET_KEEP;

    return COMFOAIR_SECRET_STORE;
}

bool comfoair_secret_equals(const char *stored, const char *candidate)
{
    s_compared = 0;
    // A device with no password set must not accept an empty guess as correct: that is the
    // difference between "unprotected" and "protected by the empty string".
    if (stored == NULL || stored[0] == '\0' || candidate == NULL)
        return false;

    const size_t stored_len = strlen(stored);
    const size_t cand_len   = strlen(candidate);
    const size_t span       = stored_len > cand_len ? stored_len : cand_len;

    // Every byte of the longer string is read whatever happens, so the time taken does not
    // depend on where the first difference is. The length difference is folded into the
    // accumulator rather than returned early for the same reason.
    unsigned diff = (unsigned)(stored_len ^ cand_len);
    for (size_t i = 0; i < span; i++) {
        const unsigned a = i < stored_len ? (unsigned char)stored[i] : 0u;
        const unsigned b = i < cand_len ? (unsigned char)candidate[i] : 0u;
        diff |= a ^ b;
        s_compared++;
    }
    return diff == 0;
}

size_t comfoair_secret_compared_bytes(void) { return s_compared; }
