// comfoair_auth, the half that has no crypto in it. See comfoair_auth.h for what is deliberately
// absent from this component and why.
//
// Pure: no ESP-IDF, no clock, no allocation, no logging. That is what lets test/test_auth send
// this an eight-kilobyte header, a NUL in the middle of a password and a colon in three places,
// which is the input it exists for and which no on-device test could produce conveniently.
#include "comfoair_auth.h"

#include <string.h>

// The one place these two files are allowed to disagree is nowhere. comfoair_config decides how
// long a password may be SET to; if this buffer were the smaller of the two, an owner would set
// the longest password the settings page accepts and then be unable to log in with it -- on a
// device with no way in over the air and the reset button behind the front panel of a running
// ventilation unit. A compile error is the right way to find that out.
#include "comfoair_config.h"
_Static_assert(COMFOAIR_AUTH_PASS_MAX >= COMFOAIR_CONFIG_UI_PASS_MAX,
               "a password comfoair_config will store must fit in the buffer that receives it");

// The longest Authorization token this parser will look at, in base64 characters.
//
// A cap on WORK, not on storage. Nothing here holds the decoded blob any more: the user name is
// dropped as it arrives and the password lands straight in the caller's struct (sink_byte below),
// so an eight-kilobyte header would cost only the walk through it -- but that is still a walk an
// attacker can ask for, and refusing it before the decode is cheaper than during. 512 characters
// is 384 decoded bytes: a 128-character password with 255 characters of user name in front of it,
// which is past anything a browser prompt or a password manager produces, and half of what this
// device's HTTP server will even deliver (sdkconfig.m5stack-nanoc6:1742,
// CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024) -- so it is this file's limit and not a restatement of the
// server's.
//
// DO NOT turn this back into `4 * ((name_max + 1 + pass_max + 2) / 3)` over a buffer that holds
// both halves at once. That arrangement made the SUM the bounded thing, so a 65-character user
// name in front of the CORRECT 128-character password came back TOO_LONG -- and a correct password
// refused is an owner outside a device whose reset button is behind the front panel of a running
// ventilation unit, which is the same loss the _Static_assert above exists to prevent, reached by
// different arithmetic. It also left a gap: sized in encoded characters over a buffer sized in
// decoded bytes, that check admitted a token whose last group landed two bytes past the end -- on
// a stack the device builds without a canary (sdkconfig.m5stack-nanoc6:1212,
// CONFIG_COMPILER_STACK_CHECK_MODE_NONE), so the bound that caught it could be deleted without a
// single assertion in the suite changing its answer.
// test/test_auth pins both halves -- test_a_user_name_never_costs_the_password_its_length and
// test_nothing_a_header_can_carry_writes_past_the_password_buffer.
#define ENCODED_MAX 512

static bool is_space(char c) { return c == ' ' || c == '\t'; }

// Through a volatile pointer, always. A plain memset over a buffer nothing reads again is a dead
// store and the compiler is entitled to delete it -- which is exactly what would happen to every
// call below, and would leave the plaintext password on the HTTP task's stack.
static void wipe(void *p, size_t len)
{
    volatile unsigned char *q = (volatile unsigned char *)p;
    for (size_t i = 0; i < len; i++)
        q[i] = 0;
}

// -1 for anything not in the alphabet, INCLUDING '='. Padding is a structural thing and is
// handled where the structure is checked; letting it decode to a value here is how a decoder
// ends up accepting "YQ==YQ==".
static int b64_value(char c)
{
    if (c >= 'A' && c <= 'Z')
        return c - 'A';
    if (c >= 'a' && c <= 'z')
        return c - 'a' + 26;
    if (c >= '0' && c <= '9')
        return c - '0' + 52;
    if (c == '+')
        return 62;
    if (c == '/')
        return 63;
    return -1;
}

// Where a decoded byte goes: one at a time, and never into a buffer that holds the blob.
//
// That absence is the design. While a `user:password` buffer existed, the user name had to fit in
// it alongside the password, so the two lengths were charged against one budget and a long name
// refused a correct password (see ENCODED_MAX). Counting the name and dropping it costs nothing
// and owes nothing, which is what lets comfoair_auth.h promise the browser's name box is
// discarded without a footnote about how long it may be.
typedef struct {
    comfoair_auth_credentials_t *out;
    size_t                       pass_len;
    bool                         seen_colon;
    // A NUL anywhere in the blob, and more password than this device could have stored. Flags
    // rather than early returns because the decoder still has structure of its own to finish
    // checking; what they MEAN is decided once, in comfoair_auth_parse(), where the order the
    // three refusals are tried in is visible in one place.
    bool                         nul;
    bool                         too_long;
} sink_t;

static void sink_byte(sink_t *s, uint8_t b)
{
    // base64 carries arbitrary bytes and a NUL would end the C string that holds the password
    // early: "hunter2\0anything" would be compared as "hunter2", which is not what was sent.
    // Refused rather than truncated, so that the question answered is the question asked. Checked
    // in the user name too, even though the name is thrown away -- a NUL there says the same thing
    // about the client, and looking only at the half that is kept would make the answer depend on
    // where in the blob the byte fell.
    //
    // ONLY NUL. Other control characters are refused where a password is SET
    // (comfoair_config_check_ui_password), so one arriving here matches nothing that could have
    // been stored; repeating that rule in this file would put one decision in two places and give
    // a future change somewhere to be missed.
    if (b == 0) {
        s->nul = true;
        return;
    }

    // The FIRST colon separates, and everything before it is dropped unread. A colon may not
    // appear in a user name and may appear in a password (RFC 7617 section 2), so taking the last
    // one would read "admin:pa:ss:word" as the password "word" -- a wrong password accepted as
    // right. Everything after this one, colons included, is password.
    if (!s->seen_colon) {
        s->seen_colon = (b == ':');
        return;
    }

    // THE ONLY BOUND between a token chosen by whoever is attacking the device and the caller's
    // memory, and the only place a decoded byte reaches memory at all. DO NOT delete it as
    // redundant to the ENCODED_MAX check in front of the decoder: that one is sized in encoded
    // characters and this one in decoded bytes, four characters carry three bytes, and the
    // rounding between them is what the old arrangement got wrong.
    // test_nothing_a_header_can_carry_writes_past_the_password_buffer puts a canary behind the
    // caller's struct and looks -- which it can only do because this write lands there and not in
    // a stack array of this file's own, where the overrun was silent without a sanitiser.
    if (s->pass_len >= COMFOAIR_AUTH_PASS_MAX) {
        s->too_long = true;
        return;
    }
    s->out->password[s->pass_len++] = (char)b;
}

// Decodes `len` characters, handing each plaintext byte to `sink`. Returns false when the token is
// not base64 at all; everything past that -- colon, NUL, length -- is the sink's to notice.
//
// Not constant time, and it does not need to be: everything it touches was chosen by the sender,
// so its timing tells them only what they already know. DO NOT "harden" it into the shape of
// comfoair_secret_equals -- that function is slow on purpose because it touches the SECRET, and
// copying the pattern to somewhere it buys nothing teaches the next reader the wrong rule.
static bool b64_decode(const char *in, size_t len, sink_t *sink)
{
    // Every encoding is a whole number of four-character groups. Checked before in[len - 1] is
    // read, which is also what makes that read safe.
    if (len == 0 || len % 4 != 0)
        return false;

    size_t pad = 0;
    if (in[len - 1] == '=')
        pad++;
    if (pad == 1 && in[len - 2] == '=')
        pad++;

    // Everything before the trailing padding must be a real alphabet character, which is what
    // refuses '=' anywhere else: b64_value() does not know it. "Y===" and "YQ==YQ==" both die
    // here, and both are encodings a browser cannot produce.
    for (size_t i = 0; i + pad < len; i++)
        if (b64_value(in[i]) < 0)
            return false;

    for (size_t i = 0; i + 4 <= len; i += 4) {
        unsigned v = 0;
        for (size_t k = 0; k < 4; k++) {
            const int d = b64_value(in[i + k]);
            // Only reachable for the trailing '=' characters, which the loop above let past.
            // Their six bits are discarded with the byte they would have landed in.
            v = (v << 6) | (unsigned)(d < 0 ? 0 : d);
        }
        // The leftover bits of a padded group are NOT required to be zero. RFC 4648 section 3.5
        // permits a decoder to reject that and this one does not: the two spellings decode to
        // the same credential, so the check would change no answer, and a rule that changes no
        // answer is one more place to be wrong.
        const size_t emit = (i + 4 == len) ? 3 - pad : 3;
        for (size_t k = 0; k < emit; k++)
            sink_byte(sink, (uint8_t)((v >> (16 - 8 * k)) & 0xffu));
    }
    return true;
}

comfoair_auth_parse_t comfoair_auth_parse(const char *header, comfoair_auth_credentials_t *out)
{
    if (out == NULL)
        return COMFOAIR_AUTH_MALFORMED;
    // Before anything is decided, so that no path out of this function can leave the previous
    // call's password in the caller's struct for an unchecked return value to miss.
    memset(out, 0, sizeof *out);

    if (header == NULL)
        return COMFOAIR_AUTH_ABSENT;

    const char *p = header;
    while (is_space(*p))
        p++;
    // Nothing was claimed. esp_http_server hands over the value after the colon, so a client
    // that sent a bare `Authorization:` arrives here as an empty string, and that is not a
    // malformed credential -- it is no credential, which is what every browser sends before it
    // has been challenged.
    if (*p == '\0')
        return COMFOAIR_AUTH_ABSENT;

    // Whole word, case-insensitively (RFC 7617 section 2). The length check on the terminator is
    // what stops "Basicx" from matching -- a bare strncasecmp of five characters would take it.
    static const char SCHEME[] = "Basic";
    for (size_t i = 0; i < sizeof SCHEME - 1; i++, p++)
        if ((*p | 0x20) != (SCHEME[i] | 0x20))
            return COMFOAIR_AUTH_NOT_BASIC;
    if (*p != '\0' && !is_space(*p))
        return COMFOAIR_AUTH_NOT_BASIC;

    // From here on the client asked for Basic, so every remaining failure is a broken Basic
    // credential rather than a scheme this device does not implement.
    while (is_space(*p))
        p++;
    const char *tok = p;
    while (*p != '\0' && !is_space(*p))
        p++;
    const size_t tok_len = (size_t)(p - tok);
    // Anything after the token that is not whitespace is a second token. Basic has one, and
    // header folding -- the only thing that could legitimately put a space in the middle of a
    // long value -- is obsolete (RFC 7230 section 3.2.4) and never reaches a handler.
    while (is_space(*p))
        p++;
    if (tok_len == 0 || *p != '\0')
        return COMFOAIR_AUTH_MALFORMED;

    // The early exit. A header of eight kilobytes is refused here, having been walked once and
    // copied nowhere.
    if (tok_len > ENCODED_MAX)
        return COMFOAIR_AUTH_TOO_LONG;

    sink_t sink = {out, 0, false, false, false};

    comfoair_auth_parse_t status;
    if (!b64_decode(tok, tok_len, &sink))
        status = COMFOAIR_AUTH_MALFORMED;
    else if (sink.nul)
        status = COMFOAIR_AUTH_MALFORMED;
    else if (!sink.seen_colon)
        // No colon is not a credential at all. Treating the whole blob as the password would
        // accept something nobody sent.
        status = COMFOAIR_AUTH_MALFORMED;
    else if (sink.too_long)
        // Longer than this device could ever have stored, so there is no candidate here to check.
        // Refused, not truncated: a truncated credential asks a different question from the one
        // the client asked.
        status = COMFOAIR_AUTH_TOO_LONG;
    else {
        out->password[sink.pass_len] = '\0';
        status = COMFOAIR_AUTH_OK;
    }

    // The plaintext arrived in the CALLER's struct byte by byte, so a refusal has to leave nothing
    // rather than merely never having finished: part of a rejected credential is still a password
    // somebody sent, on the stack of whatever task took the request. One exit and one wipe, for
    // the reason the decode buffer used to have one -- there are four refusals above and the wipe
    // would be missed on whichever one was added last.
    if (status != COMFOAIR_AUTH_OK)
        wipe(out->password, sizeof out->password);
    return status;
}

void comfoair_auth_forget(comfoair_auth_credentials_t *creds)
{
    if (creds == NULL)
        return;
    wipe(creds->password, sizeof creds->password);
}

comfoair_auth_outcome_t comfoair_auth_check(const char *header, const char *stored,
                                            comfoair_auth_verifier_t verify)
{
    comfoair_auth_outcome_t o = {COMFOAIR_AUTH_DENIED, 0, COMFOAIR_AUTH_ABSENT};

    // FIRST, and the position is the point. With nothing stored there is no question to answer,
    // and no credential is decoded at all -- so no bug in anything below can turn "this device
    // has no password" into "this device accepts one". comfoair_secrets pins the same rule one
    // layer down for the same reason (comfoair_secret_equals with an empty stored value).
    //
    // NO_PASSWORD, never GRANTED. comfoair_http_check() decides what an unclaimed device allows
    // from its own `password_set`, and a caller bridging this into that policy's `authenticated`
    // field must map only GRANTED to true: told a stranger on the open access point had
    // authenticated, that policy hands them the device.
    if (stored == NULL || stored[0] == '\0') {
        o.result = COMFOAIR_AUTH_NO_PASSWORD;
        return o;
    }

    comfoair_auth_credentials_t creds;
    o.header = comfoair_auth_parse(header, &creds);

    // No header is not a guess. A browser sends none until it has been challenged, so this is
    // the FIRST request of every page load on a protected device; charging it the failure delay
    // would tax every normal load on a server that answers all of its sockets from one task
    // (comfoair_http.c, max_open_sockets = 7) and would teach an attacker nothing they did not
    // already know about their own request.
    if (o.header == COMFOAIR_AUTH_ABSENT) {
        comfoair_auth_forget(&creds);
        return o;
    }

    if (o.header == COMFOAIR_AUTH_OK &&
        // An empty candidate matches nothing, checked here rather than left to the verifier so
        // that no injected verifier can be the thing that gets it wrong. `admin:` with the
        // password box empty is one keystroke away from a real attempt.
        creds.password[0] != '\0' &&
        // Fails closed. A caller that forgot to wire the verifier gets a device nobody can log
        // into -- visible, and fixable by the reset gesture; the other way round is a device
        // anybody can log into, which is neither.
        verify != NULL && verify(stored, creds.password))
        o.result = COMFOAIR_AUTH_GRANTED;
    else
        // One number for every way of being wrong. A delay that varied by failure kind would
        // tell an attacker which of their guesses was even considered, and the value is
        // deliberately not a function of how nearly right the guess was -- see
        // COMFOAIR_AUTH_FAIL_DELAY_MS for what this does and does not buy.
        o.delay_ms = COMFOAIR_AUTH_FAIL_DELAY_MS;

    // The caller gets no credential back (comfoair_auth.h), so this local is the last copy and
    // it goes now rather than at the next stack frame that happens to reuse the bytes.
    comfoair_auth_forget(&creds);
    return o;
}
