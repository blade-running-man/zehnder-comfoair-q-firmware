// What a property's bytes mean -- by the catalogue when it knows the property, by shape when not --
// and how bytes are spelled in JSON (Emitter: escaping, hex, numbers, the piece-wise output).
//
// Every answer is also rendered as raw hex beside whatever this decides, so a wrong guess here
// costs a misleading number, never lost data.
#include <cstdio>
#include <cstring>

#include "comfoair_probe_report_internal.h"

namespace probe_report {

namespace {

bool printable(uint8_t c) { return c >= 0x20 && c < 0x7F; }

// The uncatalogued guess. Text needs a terminating NUL and at least TWO printable ASCII bytes
// before it: the device terminates every documented string (`34 32 31 30 00`, `ComfoAirQ\00`),
// and any little-endian INT16 below 0x7F ends in 00 -- live Q350 R1.11.0: POSTHEATER
// 0x27/01/0x03 `6400` (range 50..150) came out as the text "d". DO NOT lower the bound back to 2: one character and a NUL is indistinguishable from a small number, and the
// number is the likelier reading. A value cut short by COMFOAIR_PROBE_VALUE_MAX has lost its NUL
// and is left as hex -- the length says why.
// Widths 1/2/4 follow the documented formats: every UINT8/INT16/UINT32 row has that size, and the
// 2-byte rows are overwhelmingly INT16 (VENTILATIONCONFIG), so a 2-byte unknown is read signed
// until its range says otherwise (settle_sign).
Format guess(const uint8_t *bytes, size_t stored, size_t received)
{
    if (received != stored)
        return Format::None;
    if (stored >= 3 && bytes[stored - 1] == 0x00) {
        bool text = true;
        for (size_t i = 0; i + 1 < stored; i++)
            text = text && printable(bytes[i]);
        if (text)
            return Format::String;
    }
    switch (stored) {
    case 1: return Format::U8;
    case 2: return Format::I16;
    case 4: return Format::U32;
    default: return Format::None;
    }
}

}  // namespace

bool decode_int(Format f, const uint8_t *bytes, size_t len, int64_t *out)
{
    const size_t width = format_width(f);
    if (width == 0 || len != width)
        return false;
    uint32_t v = 0;
    for (size_t i = width; i-- > 0;)  // little-endian on the wire (unbalance `9dff` = -99)
        v = (v << 8) | bytes[i];
    *out = f == Format::I16 ? (int64_t)(int16_t)(uint16_t)v : (int64_t)v;
    return true;
}

Decoded decode_value(const Property *p, const uint8_t *bytes, size_t stored, size_t received)
{
    Decoded d{};
    d.number.divisor = (p != nullptr && p->divisor > 1) ? p->divisor : 1;
    d.format = p != nullptr ? p->format : guess(bytes, stored, received);

    if (d.format == Format::String) {
        // Up to the first NUL of what was STORED: a catalogued string that was truncated still
        // shows its head, and `len` beside it tells the reader there was more.
        const void *nul = std::memchr(bytes, 0, stored);
        d.text = Text::Bytes;
        d.text_len = nul != nullptr ? (size_t)((const uint8_t *)nul - bytes) : stored;
        return d;
    }
    // A catalogued numeric of the wrong width gets no number: decoding the first two of three
    // bytes would print a value the unit never meant. DO NOT relax to `>=`.
    if (received != stored || !decode_int(d.format, bytes, stored, &d.number.raw))
        return d;
    d.number.valid = true;
    if (d.format == Format::Version)
        d.text = Text::Version;
    if (d.format == Format::Enum && d.number.raw < p->label_count)
        d.label = p->labels[d.number.raw];
    return d;
}

void settle_sign(Decoded &d, const uint8_t *value, size_t value_len, const uint8_t *range,
                 size_t range_stored, size_t range_received)
{
    if (d.format != Format::I16 || range_stored != range_received || range_stored != 4)
        return;
    int64_t lo = 0, hi = 0;
    if (!decode_int(Format::I16, range, 2, &lo) || !decode_int(Format::I16, range + 2, 2, &hi) ||
        lo <= hi)
        return;
    d.format = Format::U16;
    // Only a value decode_value() accepted is re-read: a truncated one stays without a number.
    if (d.number.valid)
        d.number.valid = decode_int(Format::U16, value, value_len, &d.number.raw);
}

size_t version_text(uint32_t version, char *out, size_t cap)
{
    // Two bits of release kind (U/D/P/R), then three 10-bit fields: major, minor, patch.
    static const char KIND[] = {'U', 'D', 'P', 'R'};
    const int n = std::snprintf(out, cap, "%c%u.%u.%u", KIND[(version >> 30) & 3],
                                (unsigned)((version >> 20) & 1023),
                                (unsigned)((version >> 10) & 1023), (unsigned)(version & 1023));
    return (n < 0 || (size_t)n >= cap) ? 0 : (size_t)n;
}

size_t format_number(const Number &n, char *out, size_t cap)
{
    if (!n.valid)
        return 0;
    int len;
    if (n.divisor <= 1) {
        len = std::snprintf(out, cap, "%lld", (long long)n.raw);
    } else {
        // Integer arithmetic, rounded half away from zero, so -99/10 is exactly "-9.9" and never
        // "-9.900000001". The sign is printed separately: -5/10 must be "-0.5", and "%lld" of the
        // integer part (0) would drop it.
        const int64_t scaled = n.raw * 10;
        int64_t tenths = scaled / n.divisor;
        const int64_t rest = scaled % n.divisor;
        if (2 * (rest < 0 ? -rest : rest) >= n.divisor)
            tenths += scaled < 0 ? -1 : 1;
        const int64_t mag = tenths < 0 ? -tenths : tenths;
        len = std::snprintf(out, cap, "%s%lld.%lld", tenths < 0 ? "-" : "", (long long)(mag / 10),
                            (long long)(mag % 10));
    }
    return (len < 0 || (size_t)len >= cap) ? 0 : (size_t)len;
}

// --- the JSON spelling ----------------------------------------------------------------------

void Emitter::put(const char *s, size_t n)
{
    while (ok_ && n > 0) {
        if (used_ == EMIT_PIECE && !flush())
            return;
        size_t take = EMIT_PIECE - used_;
        if (take > n)
            take = n;
        std::memcpy(buf_ + used_, s, take);
        used_ += take;
        s += take;
        n -= take;
    }
}

void Emitter::put(const char *s) { put(s, std::strlen(s)); }

void Emitter::put_uint(uint64_t v)
{
    char tmp[24];
    size_t i = sizeof tmp;
    do {
        tmp[--i] = (char)('0' + v % 10);
        v /= 10;
    } while (v != 0);
    put(tmp + i, sizeof tmp - i);
}

void Emitter::put_number(const Number &n)
{
    char tmp[32];
    const size_t len = format_number(n, tmp, sizeof tmp);
    if (len == 0)
        put("null");
    else
        put(tmp, len);
}

void Emitter::put_string(const char *s, size_t n, bool device)
{
    static const char HEX[] = "0123456789abcdef";
    if (s == nullptr) {
        put("null");
        return;
    }
    put("\"");
    for (size_t i = 0; i < n && ok_; i++) {
        const uint8_t c = (uint8_t)s[i];
        if (c == '"' || c == '\\') {
            const char esc[2] = {'\\', (char)c};
            put(esc, 2);
        } else if (c < 0x20 || (device && c >= 0x7F)) {
            const char esc[6] = {'\\', 'u', '0', '0', HEX[c >> 4], HEX[c & 15]};
            put(esc, 6);
        } else {
            put((const char *)&c, 1);
        }
    }
    put("\"");
}

void Emitter::put_string(const char *s) { put_string(s, s != nullptr ? std::strlen(s) : 0, false); }

void Emitter::put_hex(const uint8_t *b, size_t n)
{
    static const char HEX[] = "0123456789abcdef";
    put("\"");
    for (size_t i = 0; i < n && ok_; i++) {
        const char pair[2] = {HEX[b[i] >> 4], HEX[b[i] & 15]};
        put(pair, 2);
    }
    put("\"");
}

bool Emitter::flush()
{
    if (ok_ && used_ > 0)
        ok_ = write_(ctx_, buf_, used_);
    used_ = 0;
    return ok_;
}

}  // namespace probe_report

// The public door to version_text, for GET /api/system. DO NOT copy the decode into
// comfoair_sysinfo: the probe report and the System page must never print one firmware two ways.
size_t comfoair_probe_version_text(uint32_t raw, char *out, size_t cap)
{
    return probe_report::version_text(raw, out, cap);
}
