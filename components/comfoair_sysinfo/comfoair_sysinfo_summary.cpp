// The unit's active errors for the two Home Assistant entities (comfoair_sysinfo.h): the
// `unit_error` verdict and the `active_errors` line. The table that names and classifies the ids is
// comfoair_sysinfo_errors.cpp's; this file only walks the bitset, joins and bounds.
#include <cstdio>
#include <cstring>

#include "comfoair_sysinfo.h"

namespace {

constexpr size_t MAX      = COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX;
constexpr unsigned IDS    = COMFOAIR_SYSINFO_ERRORS_BYTES * 8u;
constexpr char   SEP[]    = " | ";
// ASCII, like PDO 230's cut (airflow_constraints.h): three bytes whatever reads it.
constexpr char   MORE[]   = "...";
constexpr size_t MORE_LEN = sizeof MORE - 1;

// Byte i, bit b is id i*8 + b -- the order GET /api/system lists them in.
bool active(const uint8_t *errors, unsigned id) { return errors[id / 8] & (1u << (id % 8)); }

bool continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

void put(char *out, size_t *len, const char *s, size_t n)
{
    std::memcpy(out + *len, s, n);
    *len += n;
}

}  // namespace

bool comfoair_sysinfo_errors_any(const uint8_t *errors, bool has_firmware, uint32_t firmware_raw)
{
    if (errors == nullptr)
        return false;
    for (unsigned id = 0; id < IDS; id++)
        if (active(errors, id) &&
            !comfoair_sysinfo_error_informational(static_cast<uint16_t>(id), has_firmware,
                                                  firmware_raw))
            return true;
    return false;
}

size_t comfoair_sysinfo_errors_summary(const uint8_t *errors, bool has_firmware,
                                       uint32_t firmware_raw, char *out, size_t cap)
{
    if (errors == nullptr || out == nullptr || cap <= MAX)
        return 0;
    size_t len = 0;
    for (unsigned id = 0; id < IDS; id++) {
        if (!active(errors, id))
            continue;
        char unknown[24];
        const char *text =
            comfoair_sysinfo_error_text(static_cast<uint16_t>(id), has_firmware, firmware_raw);
        if (text == nullptr) {
            std::snprintf(unknown, sizeof unknown, "Unknown error %u", id);
            text = unknown;
        }
        const size_t sep = len == 0 ? 0 : sizeof SEP - 1;
        const size_t n   = std::strlen(text);
        if (len + sep + n <= MAX) {
            put(out, &len, SEP, sep);
            put(out, &len, text, n);
            continue;
        }
        // The first item that does not fit whole. As much of it as leaves room for MORE, cut where
        // a UTF-8 character starts -- the line goes to Home Assistant and into JSON, and a split
        // character there is a decode error, not a shorter text.
        if (len + sep + MORE_LEN < MAX) {
            put(out, &len, SEP, sep);
            size_t take = MAX - MORE_LEN - len;  // < n, because the whole item did not fit
            while (take > 0 && continuation(text[take]))
                take--;
            put(out, &len, text, take);
        } else {
            // Not even the separator fits: shorten what is there, on a character boundary.
            size_t keep = MAX - MORE_LEN < len ? MAX - MORE_LEN : len;
            while (keep > 0 && keep < len && continuation(out[keep]))
                keep--;
            len = keep;
        }
        put(out, &len, MORE, MORE_LEN);
        break;
    }
    // No bit at all. Notices alone are still listed: the line names what is active, the
    // `unit_error` verdict decides whether it is a problem.
    if (len == 0)
        put(out, &len, COMFOAIR_SYSINFO_NO_ERRORS, sizeof COMFOAIR_SYSINFO_NO_ERRORS - 1);
    out[len] = '\0';
    return len;
}
