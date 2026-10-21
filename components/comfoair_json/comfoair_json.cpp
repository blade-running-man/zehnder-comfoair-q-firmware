// See comfoair_json.h for what this reads and why it is not cJSON.
//
// THE ONE RULE THAT GOVERNS EVERY FUNCTION HERE: nothing reads past the terminator, whatever the
// bytes inside the document claim. The input is chosen by whoever is talking to the device, the
// setup access point is open by design, and this is the first code the bytes reach.
// test_nothing_is_read_past_the_terminator asserts it over every prefix of a realistic body.
#include "comfoair_json.h"

#include <cstdio>
#include <cstring>

namespace {

// A cursor over the document. `p` never advances past the NUL, so every helper below can look at
// *p without checking the length first -- the terminator IS the check.
struct Scan {
    const char *p;

    void spaces()
    {
        // RFC 8259 section 2: space, tab, newline, carriage return. Nothing else, and in
        // particular not a vertical tab or a form feed -- accepting those would make this reader
        // take documents no other reader does.
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
    }
};

bool is_digit(char c) { return c >= '0' && c <= '9'; }

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

// Where a decoded string byte goes. The same walk serves three callers -- checking a document,
// comparing a key, and copying a value -- and one walk is the point: three walks would be three
// sets of rules about what a legal escape is.
struct Sink {
    char  *out;      // may be null: then nothing is written and only `used` grows
    size_t cap;      // includes the terminator
    size_t used;
    bool   overflow;

    void put(char c)
    {
        if (out == nullptr) {
            used++;
            return;
        }
        if (used + 1 >= cap) {
            overflow = true;
            return;
        }
        out[used++] = c;
    }
};

void put_utf8(Sink &sink, uint32_t cp)
{
    if (cp < 0x80) {
        sink.put((char)cp);
    } else if (cp < 0x800) {
        sink.put((char)(0xc0 | (cp >> 6)));
        sink.put((char)(0x80 | (cp & 0x3f)));
    } else if (cp < 0x10000) {
        sink.put((char)(0xe0 | (cp >> 12)));
        sink.put((char)(0x80 | ((cp >> 6) & 0x3f)));
        sink.put((char)(0x80 | (cp & 0x3f)));
    } else {
        sink.put((char)(0xf0 | (cp >> 18)));
        sink.put((char)(0x80 | ((cp >> 12) & 0x3f)));
        sink.put((char)(0x80 | ((cp >> 6) & 0x3f)));
        sink.put((char)(0x80 | (cp & 0x3f)));
    }
}

enum class StrResult { Ok, Malformed, TooLong };

// Reads one JSON string, starting AT its opening quote, and leaves the cursor just past the
// closing one. Writes the decoded bytes into `sink` when it has somewhere to put them.
StrResult read_string(Scan &s, Sink &sink)
{
    if (*s.p != '"')
        return StrResult::Malformed;
    s.p++;

    for (;;) {
        const unsigned char c = (unsigned char)*s.p;
        if (c == '\0')
            return StrResult::Malformed;  // unterminated: the document ended inside it
        if (c == '"') {
            s.p++;
            break;
        }
        if (c < 0x20) {
            // RFC 8259 section 7: everything below 0x20 must be escaped. A raw newline in a
            // string is a broken client, and guessing what it meant is how a parser starts
            // accepting documents no other parser does.
            return StrResult::Malformed;
        }
        if (c != '\\') {
            // Raw UTF-8 passes through byte for byte. An SSID is an OCTET string -- 802.11 does
            // not say what encoding it is in -- and whatever arrives has to reach the radio
            // unchanged, or a household whose router is named in Cyrillic cannot be joined.
            sink.put((char)c);
            s.p++;
            continue;
        }

        s.p++;
        const char esc = *s.p;
        switch (esc) {
        case '"':  sink.put('"');  s.p++; continue;
        case '\\': sink.put('\\'); s.p++; continue;
        case '/':  sink.put('/');  s.p++; continue;
        case 'b':  sink.put('\b'); s.p++; continue;
        case 'n':  sink.put('\n'); s.p++; continue;
        case 'f':  sink.put('\f'); s.p++; continue;
        case 'r':  sink.put('\r'); s.p++; continue;
        case 't':  sink.put('\t'); s.p++; continue;
        case 'u':  break;
        default:   return StrResult::Malformed;  // including a backslash at the very end
        }

        s.p++;  // past the 'u'
        uint32_t cp = 0;
        for (int i = 0; i < 4; i++) {
            const int v = hex_value(s.p[i]);
            if (v < 0)
                return StrResult::Malformed;  // also catches the document ending mid-escape
            cp = (cp << 4) | (uint32_t)v;
        }
        s.p += 4;

        if (cp == 0) {
            // Legal JSON, and this function hands back a C string. Decoding it would truncate
            // silently -- a password shorter than the owner set, an SSID that is not the one
            // typed. comfoair_config_check_ssid refuses an embedded NUL for the same reason;
            // this is that rule one layer earlier, where it is still a bad request.
            return StrResult::Malformed;
        }

        if (cp >= 0xd800 && cp <= 0xdbff) {
            // A high surrogate must be followed by its low half. Anything else is not valid
            // UTF-16 and the bytes a lenient decoder emits for it are not what any other reader
            // would emit.
            if (s.p[0] != '\\' || s.p[1] != 'u')
                return StrResult::Malformed;
            uint32_t low = 0;
            for (int i = 0; i < 4; i++) {
                const int v = hex_value(s.p[2 + i]);
                if (v < 0)
                    return StrResult::Malformed;
                low = (low << 4) | (uint32_t)v;
            }
            if (low < 0xdc00 || low > 0xdfff)
                return StrResult::Malformed;
            s.p += 6;
            cp = 0x10000 + ((cp - 0xd800) << 10) + (low - 0xdc00);
        } else if (cp >= 0xdc00 && cp <= 0xdfff) {
            return StrResult::Malformed;  // a low surrogate on its own
        }

        put_utf8(sink, cp);
    }

    return sink.overflow ? StrResult::TooLong : StrResult::Ok;
}

// JSON's number grammar, exactly: an optional minus, an integer part with no leading zero, an
// optional fraction, an optional exponent. `whole` says whether what was read is an integer, and
// `value` carries it when it is one and fits.
struct Number {
    bool     ok;
    bool     whole;
    bool     negative;
    bool     overflow;
    uint64_t value;
};

Number read_number(Scan &s)
{
    Number n = {false, true, false, false, 0};

    if (*s.p == '-') {
        n.negative = true;
        s.p++;
    }
    if (!is_digit(*s.p))
        return n;  // "-" alone, or ".5", or "+1"

    if (*s.p == '0') {
        // No leading zeros: "01" is not a number, it is two tokens, and a reader that took it
        // would accept documents no other reader does.
        s.p++;
    } else {
        while (is_digit(*s.p)) {
            const uint64_t digit = (uint64_t)(*s.p - '0');
            if (n.value > (UINT64_MAX - digit) / 10u)
                n.overflow = true;
            else
                n.value = n.value * 10u + digit;
            s.p++;
        }
    }

    if (*s.p == '.') {
        s.p++;
        if (!is_digit(*s.p))
            return n;  // "1." is not a number
        n.whole = false;
        while (is_digit(*s.p))
            s.p++;
    }
    if (*s.p == 'e' || *s.p == 'E') {
        s.p++;
        if (*s.p == '+' || *s.p == '-')
            s.p++;
        if (!is_digit(*s.p))
            return n;
        // "1e3" is a whole number mathematically and is not one anybody typed into a port box.
        // Refused rather than evaluated: there is nothing to gain from supporting it and a
        // conversion to lose precision in.
        n.whole = false;
        while (is_digit(*s.p))
            s.p++;
    }

    n.ok = true;
    return n;
}

bool read_literal(Scan &s, const char *word)
{
    const size_t n = strlen(word);
    if (strncmp(s.p, word, n) != 0)
        return false;
    // The byte AFTER it has to end the token, or "truthy" would read as "true" with rubbish
    // behind it that the value loop then tries to parse as a separator.
    const char next = s.p[n];
    if (next != '\0' && next != ',' && next != '}' && next != ' ' && next != '\t' &&
        next != '\n' && next != '\r')
        return false;
    s.p += n;
    return true;
}

enum class ValueKind { String, Number, Bool, Null };

// Reads one value and leaves the cursor just past it. `sink` receives a string's decoded bytes.
comfoair_json_read_t read_value(Scan &s, ValueKind *kind, Sink *sink, Number *number, bool *flag)
{
    Sink discard = {nullptr, 0, 0, false};

    switch (*s.p) {
    case '"': {
        *kind                = ValueKind::String;
        const StrResult r    = read_string(s, sink != nullptr ? *sink : discard);
        if (r == StrResult::Malformed)
            return COMFOAIR_JSON_MALFORMED;
        if (r == StrResult::TooLong)
            return COMFOAIR_JSON_TOO_LONG;
        return COMFOAIR_JSON_FOUND;
    }
    case 't':
        *kind = ValueKind::Bool;
        if (!read_literal(s, "true"))
            return COMFOAIR_JSON_MALFORMED;
        if (flag != nullptr)
            *flag = true;
        return COMFOAIR_JSON_FOUND;
    case 'f':
        *kind = ValueKind::Bool;
        if (!read_literal(s, "false"))
            return COMFOAIR_JSON_MALFORMED;
        if (flag != nullptr)
            *flag = false;
        return COMFOAIR_JSON_FOUND;
    case 'n':
        *kind = ValueKind::Null;
        return read_literal(s, "null") ? COMFOAIR_JSON_FOUND : COMFOAIR_JSON_MALFORMED;
    case '{':
    case '[':
        // Nested. Reported by the caller as COMFOAIR_JSON_DOC_NESTED; nothing here tries to skip
        // over it. See the header for why the narrowness is deliberate.
        *kind = ValueKind::Null;
        return COMFOAIR_JSON_WRONG_TYPE;
    default: {
        *kind          = ValueKind::Number;
        const Number n = read_number(s);
        if (!n.ok)
            return COMFOAIR_JSON_MALFORMED;
        if (number != nullptr)
            *number = n;
        return COMFOAIR_JSON_FOUND;
    }
    }
}

// True when the decoded key at the cursor equals `want`. Compared BYTE FOR BYTE against what the
// escape sequences decode to, so `{"a":1}` answers to "a" -- a client that escapes a key
// would otherwise have its field silently ignored, which for a config patch is a save that
// dropped a setting.
bool key_matches(Scan &s, const char *want, bool *malformed)
{
    char      buf[64];
    Sink      sink = {buf, sizeof buf, 0, false};
    Scan      copy = s;
    const StrResult r = read_string(copy, sink);
    if (r == StrResult::Malformed) {
        *malformed = true;
        return false;
    }
    s = copy;
    if (r == StrResult::TooLong)
        return false;  // longer than any key this firmware has; it is simply not the one asked for
    buf[sink.used] = '\0';
    return strcmp(buf, want) == 0;
}

}  // namespace

// --- checking ---------------------------------------------------------------------------------

comfoair_json_doc_t comfoair_json_check(const char *doc)
{
    if (doc == nullptr)
        return COMFOAIR_JSON_DOC_NOT_AN_OBJECT;

    Scan s = {doc};
    s.spaces();
    if (*s.p != '{')
        return COMFOAIR_JSON_DOC_NOT_AN_OBJECT;
    s.p++;
    s.spaces();

    // Every key seen so far, so the same one twice is refused. Bounded: the largest document this
    // firmware accepts has ten fields, and a body with more keys than this is not one of ours.
    char   seen[16][64];
    size_t seen_count = 0;

    if (*s.p == '}') {
        s.p++;
        s.spaces();
        return *s.p == '\0' ? COMFOAIR_JSON_DOC_OK : COMFOAIR_JSON_DOC_MALFORMED;
    }

    for (;;) {
        s.spaces();
        if (*s.p != '"')
            return COMFOAIR_JSON_DOC_MALFORMED;  // a bare word as a key, or the document ended

        char      key[64];
        Sink      key_sink = {key, sizeof key, 0, false};
        const StrResult r  = read_string(s, key_sink);
        if (r == StrResult::Malformed)
            return COMFOAIR_JSON_DOC_MALFORMED;
        if (r != StrResult::TooLong) {
            key[key_sink.used] = '\0';
            for (size_t i = 0; i < seen_count; i++)
                if (strcmp(seen[i], key) == 0)
                    return COMFOAIR_JSON_DOC_DUPLICATE;
            if (seen_count < sizeof seen / sizeof seen[0])
                memcpy(seen[seen_count++], key, key_sink.used + 1);
        }

        s.spaces();
        if (*s.p != ':')
            return COMFOAIR_JSON_DOC_MALFORMED;
        s.p++;
        s.spaces();

        ValueKind                  kind   = ValueKind::Null;
        const comfoair_json_read_t status = read_value(s, &kind, nullptr, nullptr, nullptr);
        if (status == COMFOAIR_JSON_WRONG_TYPE)
            return COMFOAIR_JSON_DOC_NESTED;
        if (status != COMFOAIR_JSON_FOUND)
            return COMFOAIR_JSON_DOC_MALFORMED;

        s.spaces();
        if (*s.p == ',') {
            s.p++;
            s.spaces();
            // A trailing comma. Refused, because the alternative is a grammar of our own.
            if (*s.p == '}')
                return COMFOAIR_JSON_DOC_MALFORMED;
            continue;
        }
        if (*s.p == '}') {
            s.p++;
            break;
        }
        return COMFOAIR_JSON_DOC_MALFORMED;
    }

    s.spaces();
    // Trailing rubbish. "{}{}"" is two documents and this reads one, so accepting it would mean
    // the second was silently ignored -- and the obvious use of that is to put the fields you
    // want ignored in the first.
    return *s.p == '\0' ? COMFOAIR_JSON_DOC_OK : COMFOAIR_JSON_DOC_MALFORMED;
}

// --- reading one key --------------------------------------------------------------------------

namespace {

// Walks to the value of `key`, leaving the cursor on its first byte. Does its OWN scanning rather
// than trusting a prior comfoair_json_check(): handlers are supposed to check first, and one day
// one will not -- a getter that assumed a checked document would run off the end of an unchecked
// one.
comfoair_json_read_t seek(const char *doc, const char *key, Scan &s)
{
    if (doc == nullptr || key == nullptr)
        return COMFOAIR_JSON_MISSING;

    s = Scan{doc};
    s.spaces();
    if (*s.p != '{')
        return COMFOAIR_JSON_MISSING;
    s.p++;

    for (;;) {
        s.spaces();
        if (*s.p == '}')
            return COMFOAIR_JSON_MISSING;
        if (*s.p != '"')
            return COMFOAIR_JSON_MALFORMED;

        bool       malformed = false;
        const bool hit       = key_matches(s, key, &malformed);
        if (malformed)
            return COMFOAIR_JSON_MALFORMED;

        s.spaces();
        if (*s.p != ':')
            return COMFOAIR_JSON_MALFORMED;
        s.p++;
        s.spaces();
        if (hit)
            return COMFOAIR_JSON_FOUND;

        // Step over the value we are not interested in. A nested one stops the walk: this reader
        // does not know how long it is, and guessing is how it would read past its own document.
        ValueKind                  kind   = ValueKind::Null;
        const comfoair_json_read_t status = read_value(s, &kind, nullptr, nullptr, nullptr);
        if (status != COMFOAIR_JSON_FOUND)
            return COMFOAIR_JSON_MALFORMED;

        s.spaces();
        if (*s.p == ',') {
            s.p++;
            continue;
        }
        return COMFOAIR_JSON_MISSING;  // '}' or rubbish: either way the key is not here
    }
}

// What has to follow a value for it to have been a value at all.
//
// Without this a getter answers 12 for `{"a":12` -- the number parses, the document does not, and
// nothing said so. It never reads past the terminator either way, so this is not a memory rule;
// it is the same rule as comfoair_json_check()'s trailing-rubbish test, applied per key so that a
// handler which forgot to check the body cannot be handed half a document's worth of answers.
comfoair_json_read_t value_is_closed(Scan &s)
{
    s.spaces();
    return (*s.p == ',' || *s.p == '}') ? COMFOAIR_JSON_FOUND : COMFOAIR_JSON_MALFORMED;
}

}  // namespace

comfoair_json_read_t comfoair_json_string(const char *doc, const char *key, char *out, size_t cap)
{
    if (out == nullptr || cap == 0)
        return COMFOAIR_JSON_TOO_LONG;

    Scan                 s      = {nullptr};
    const comfoair_json_read_t found = seek(doc, key, s);
    if (found != COMFOAIR_JSON_FOUND)
        return found;

    if (*s.p != '"')
        return COMFOAIR_JSON_WRONG_TYPE;

    // Written into the caller's buffer only on success. A refused value must not leave half of
    // itself where the caller will read it as the whole.
    Sink                       sink   = {out, cap, 0, false};
    ValueKind                  kind   = ValueKind::String;
    const comfoair_json_read_t status = read_value(s, &kind, &sink, nullptr, nullptr);
    if (status != COMFOAIR_JSON_FOUND)
        return status;
    const comfoair_json_read_t closed = value_is_closed(s);
    if (closed != COMFOAIR_JSON_FOUND)
        return closed;
    out[sink.used] = '\0';
    return COMFOAIR_JSON_FOUND;
}

comfoair_json_read_t comfoair_json_u32(const char *doc, const char *key, uint32_t *out)
{
    if (out == nullptr)
        return COMFOAIR_JSON_MISSING;

    Scan                       s     = {nullptr};
    const comfoair_json_read_t found = seek(doc, key, s);
    if (found != COMFOAIR_JSON_FOUND)
        return found;

    ValueKind kind   = ValueKind::Null;
    Number    number = {false, false, false, false, 0};
    const comfoair_json_read_t status = read_value(s, &kind, nullptr, &number, nullptr);
    if (status == COMFOAIR_JSON_WRONG_TYPE)
        return COMFOAIR_JSON_WRONG_TYPE;  // an object or an array
    if (status != COMFOAIR_JSON_FOUND)
        return status;
    if (kind != ValueKind::Number)
        return COMFOAIR_JSON_WRONG_TYPE;
    const comfoair_json_read_t closed = value_is_closed(s);
    if (closed != COMFOAIR_JSON_FOUND)
        return closed;
    if (!number.whole || number.negative || number.overflow || number.value > UINT32_MAX)
        return COMFOAIR_JSON_OUT_OF_RANGE;

    *out = (uint32_t)number.value;
    return COMFOAIR_JSON_FOUND;
}

comfoair_json_read_t comfoair_json_i32(const char *doc, const char *key, int32_t *out)
{
    if (out == nullptr)
        return COMFOAIR_JSON_MISSING;

    Scan                       s     = {nullptr};
    const comfoair_json_read_t found = seek(doc, key, s);
    if (found != COMFOAIR_JSON_FOUND)
        return found;

    ValueKind kind   = ValueKind::Null;
    Number    number = {false, false, false, false, 0};
    const comfoair_json_read_t status = read_value(s, &kind, nullptr, &number, nullptr);
    if (status == COMFOAIR_JSON_WRONG_TYPE)
        return COMFOAIR_JSON_WRONG_TYPE;
    if (status != COMFOAIR_JSON_FOUND)
        return status;
    if (kind != ValueKind::Number)
        return COMFOAIR_JSON_WRONG_TYPE;
    const comfoair_json_read_t closed = value_is_closed(s);
    if (closed != COMFOAIR_JSON_FOUND)
        return closed;
    // A fraction and an exponent are refused here exactly as they are in _u32: 18.5 is a typo,
    // not a number to round, and the caller cannot tell the difference afterwards.
    if (!number.whole || number.overflow)
        return COMFOAIR_JSON_OUT_OF_RANGE;
    // The magnitude is bounded against the SIGNED limits, and the negative side is one larger.
    if (number.negative) {
        if (number.value > (uint64_t)INT32_MAX + 1u)
            return COMFOAIR_JSON_OUT_OF_RANGE;
        *out = (int32_t)(-(int64_t)number.value);
    } else {
        if (number.value > (uint64_t)INT32_MAX)
            return COMFOAIR_JSON_OUT_OF_RANGE;
        *out = (int32_t)number.value;
    }
    return COMFOAIR_JSON_FOUND;
}

comfoair_json_read_t comfoair_json_bool(const char *doc, const char *key, bool *out)
{
    if (out == nullptr)
        return COMFOAIR_JSON_MISSING;

    Scan                       s     = {nullptr};
    const comfoair_json_read_t found = seek(doc, key, s);
    if (found != COMFOAIR_JSON_FOUND)
        return found;

    ValueKind kind = ValueKind::Null;
    bool      flag = false;
    const comfoair_json_read_t status = read_value(s, &kind, nullptr, nullptr, &flag);
    if (status == COMFOAIR_JSON_WRONG_TYPE)
        return COMFOAIR_JSON_WRONG_TYPE;
    if (status != COMFOAIR_JSON_FOUND)
        return status;
    if (kind != ValueKind::Bool)
        return COMFOAIR_JSON_WRONG_TYPE;
    const comfoair_json_read_t closed = value_is_closed(s);
    if (closed != COMFOAIR_JSON_FOUND)
        return closed;

    *out = flag;
    return COMFOAIR_JSON_FOUND;
}

// --- writing ----------------------------------------------------------------------------------

// Moved verbatim from comfoair_api.cpp, which now forwards to it. One set of rules for \u, for
// surrogate pairs and for control characters -- a second implementation would be a second set.
size_t comfoair_json_escape(const char *in, char *out, size_t cap)
{
    if (out == nullptr || cap == 0)
        return 0;
    size_t used = 0;
    // All or nothing. Copying "as much as fits" is what let a \uXXXX escape be cut in half,
    // and half an escape does not make the document shorter, it makes it unparseable.
    const auto emit = [&](const char *s) {
        size_t need = 0;
        while (s[need])
            need++;
        if (used + need + 1 > cap)
            return;
        for (size_t i = 0; i < need; i++)
            out[used++] = s[i];
    };
    const auto emit_codepoint = [&](uint32_t cp) {
        // Neither of these is representable in JSON, and emitting them anyway produced
        // output that parsed as text but was not valid: a surrogate pair whose high half
        // came from the low range, or a lone surrogate. Dropping is the honest answer --
        // the input was not valid UTF-8 to begin with.
        if (cp > 0x10ffff)
            return;
        if (cp >= 0xd800 && cp <= 0xdfff)
            return;

        char esc[16];
        if (cp < 0x10000) {
            snprintf(esc, sizeof esc, "\\u%04x", (unsigned)cp);
        } else {
            // Outside the BMP needs a surrogate pair. Nothing in pdo_table reaches here,
            // but a truncated pair is invalid JSON and the failure would be silent.
            cp -= 0x10000;
            snprintf(esc, sizeof esc, "\\u%04x\\u%04x", (unsigned)(0xd800 + (cp >> 10)),
                     (unsigned)(0xdc00 + (cp & 0x3ff)));
        }
        emit(esc);
    };

    for (const unsigned char *p = (const unsigned char *)in; in && *p;) {
        switch (*p) {
        case '"':  emit("\\\""); p++; continue;
        case '\\': emit("\\\\"); p++; continue;
        case '\b': emit("\\b");  p++; continue;
        case '\f': emit("\\f");  p++; continue;
        case '\n': emit("\\n");  p++; continue;
        case '\r': emit("\\r");  p++; continue;
        case '\t': emit("\\t");  p++; continue;
        default:   break;
        }

        if (*p < 0x20) {
            emit_codepoint(*p);
            p++;
            continue;
        }
        if (*p < 0x80) {
            if (used + 2 > cap)
                break;
            out[used++] = (char)*p++;
            continue;
        }

        // Multi-byte UTF-8, decoded so it can be re-emitted as \uXXXX. A byte that is not part
        // of a well-formed sequence is DROPPED rather than passed through: passing it through
        // is how invalid UTF-8 gets into a document that claims to be JSON.
        uint32_t cp    = 0;
        int      extra = 0;
        if ((*p & 0xe0) == 0xc0) {
            cp    = (uint32_t)(*p & 0x1f);
            extra = 1;
        } else if ((*p & 0xf0) == 0xe0) {
            cp    = (uint32_t)(*p & 0x0f);
            extra = 2;
        } else if ((*p & 0xf8) == 0xf0) {
            cp    = (uint32_t)(*p & 0x07);
            extra = 3;
        } else {
            p++;
            continue;
        }
        p++;
        bool good = true;
        for (int i = 0; i < extra; i++) {
            if ((p[i] & 0xc0) != 0x80) {
                good = false;
                break;
            }
            cp = (cp << 6) | (uint32_t)(p[i] & 0x3f);
        }
        if (!good)
            continue;
        p += extra;
        emit_codepoint(cp);
    }

    out[used < cap ? used : cap - 1] = '\0';
    return used;
}
