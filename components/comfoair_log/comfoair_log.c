#include "comfoair_log.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static char   s_lines[COMFOAIR_LOG_LINES][COMFOAIR_LOG_LINE_LEN];
static size_t s_next;   // where the next line goes
static size_t s_count;  // how many slots are in use, up to COMFOAIR_LOG_LINES

// Assembly state for comfoair_log_feed, kept separate from s_lines/s_next/s_count above.
//
// Thread safety: this module has never taken a lock (see comfoair_log_write's "never blocks"
// contract in the header), and nothing serializes concurrent calls into the vprintf hook
// itself. CONFIG_LOG_MODE_BINARY_EN is unset in this build (absent from sdkconfig.h), so
// log_format_binary.c -- the one place in ESP-IDF's log component that holds
// esp_log_impl_lock() across a log call (log_format_binary.c:264) -- is not even compiled in.
// The per-tag level lookup this build does use (CONFIG_LOG_TAG_LEVEL_IMPL_CACHE_AND_LINKED_
// LIST=1, sdkconfig.h:774) takes that same lock, but releases it before the vprintf hook is
// ever reached: esp_log_is_tag_loggable() resolves through log_level_get()
// (log_level/tag_log_level/tag_log_level.c:58-82), which locks at line 65, unlocks at line
// 80, and returns at line 81 -- all before esp_log_va() calls esp_log_vprint_func() at
// log.c:43. So two tasks logging at the same time can already interleave their writes
// through s_next/s_count, and can now equally interleave through s_pending/s_pending_len -- a
// torn pending line (mangled text, or an extra/missing ring entry at the seam) is the same
// accepted exposure this component has always had, not a new one. Adding a lock here, on
// this module alone, would be inventing a scheme the rest of the codebase has deliberately
// not adopted.
static char   s_pending[COMFOAIR_LOG_LINE_LEN];
static size_t s_pending_len;

void comfoair_log_reset(void)
{
    s_next  = 0;
    s_count = 0;
    s_pending_len = 0;  // otherwise a fragment pending from one caller leaks into the next
}

void comfoair_log_write(const char *line)
{
    if (line == NULL || line[0] == '\0')
        return;

    char *slot = s_lines[s_next];
    strncpy(slot, line, COMFOAIR_LOG_LINE_LEN - 1);
    slot[COMFOAIR_LOG_LINE_LEN - 1] = '\0';

    // Trailing newlines are the logger's framing, not content, and would otherwise be
    // escaped into every line of the rendered array.
    size_t n = strlen(slot);
    while (n > 0 && (slot[n - 1] == '\n' || slot[n - 1] == '\r'))
        slot[--n] = '\0';

    s_next = (s_next + 1) % COMFOAIR_LOG_LINES;
    if (s_count < COMFOAIR_LOG_LINES)
        s_count++;
}

void comfoair_log_feed(const char *chunk)
{
    if (chunk == NULL)
        return;

    for (const char *p = chunk; *p != '\0'; p++) {
        if (*p == '\n') {
            // The WiFi blob's third vprint_func call per logical trace line carries only
            // "\n" (see comfoair_log_feed's comment in the header). Committing that when
            // nothing is pending is exactly the 26-empty-slots bug this function exists to
            // fix, so a bare newline with no pending text commits nothing.
            if (s_pending_len > 0) {
                s_pending[s_pending_len] = '\0';
                comfoair_log_write(s_pending);
                s_pending_len = 0;
            }
            continue;
        }

        if (*p == '\r')
            // Framing, not content, same as comfoair_log_write's own trailing-newline trim
            // above -- but that trim only runs on a whole line handed to it in one call. Here
            // a line arrives one fragment at a time, so a "\r" immediately before a later
            // "\n" would otherwise get buffered as pending content, survive as the one-byte
            // pending line "\r", and be committed and then trimmed by comfoair_log_write down
            // to "": a "\r\n" line ending would resurrect the very empty-slot bug this
            // function exists to fix. Dropping it here, unbuffered, is cheaper than teaching
            // comfoair_log_write to reject an all-framing line after the fact.
            continue;

        s_pending[s_pending_len++] = *p;

        // Commit as soon as the slot is full rather than waiting for a newline that may never
        // reach this function at all: log_hook's own vsnprintf (below) already truncates a
        // single over-long hook call to fit COMFOAIR_LOG_LINE_LEN before comfoair_log_feed
        // ever sees it, which silently discards that call's own trailing newline along with
        // the rest of the overflow. Waiting for a newline to flush a full pending buffer used
        // to mean the NEXT, unrelated call's characters kept accumulating into "still waiting
        // for the newline that already got cut," so one oversized line silently ate the
        // entire line after it. Chopping at the cap instead means a too-long line lands in as
        // many consecutive whole ring slots as it takes, and normal recovery for the
        // following call requires nothing special: it starts filling pending from an
        // already-empty buffer.
        //
        // The cap can fire mid-chunk, not only on a chunk's own last character: the WiFi
        // blob's short prefix call (see the header) is often still pending when the body
        // call's characters start filling the rest of the slot, so the cap is reached
        // partway through that second call. That is fine -- a too-long line still lands
        // truncated in as many consecutive whole ring slots as it takes, nothing is lost,
        // and nothing merges into whatever line follows. A caller that hands feed() a single
        // chunk many times the slot length still commits cleanly at every cap boundary, but
        // whatever is left over after the last full slot stays pending and merges with the
        // start of the following call's content, same as any other unterminated fragment.
        // log_hook never does this: it hands feed() at most COMFOAIR_LOG_LINE_LEN - 1
        // characters per call, by vsnprintf-ing into a buffer of exactly that size (below).
        //
        // s_pending_len is at most sizeof(s_pending) - 2 going into this line (it is reset to
        // 0 the moment it would otherwise reach sizeof(s_pending) - 1, below, and by
        // comfoair_log_reset), so the append above never writes past the buffer.
        if (s_pending_len == sizeof(s_pending) - 1) {
            s_pending[s_pending_len] = '\0';
            comfoair_log_write(s_pending);
            s_pending_len = 0;
        }
    }
}

// Log lines are the only arbitrary text on this device, so this is where invalid JSON would
// come from. Escaping is local rather than shared with comfoair_api so that this component
// stays independent of the entity model -- the log has to work when nothing else does.
static bool append_escaped(char *out, size_t cap, size_t *used, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        char esc[8];
        const char *piece = esc;
        switch (*p) {
        case '"':  piece = "\\\""; break;
        case '\\': piece = "\\\\"; break;
        case '\n': piece = "\\n";  break;
        case '\r': piece = "\\r";  break;
        case '\t': piece = "\\t";  break;
        default:
            if (*p < 0x20 || *p > 0x7e)
                snprintf(esc, sizeof esc, "\\u%04x", (unsigned)*p);
            else {
                esc[0] = (char)*p;
                esc[1] = '\0';
            }
            break;
        }
        const size_t need = strlen(piece);
        if (*used + need + 1 > cap)
            return false;
        memcpy(out + *used, piece, need);
        *used += need;
    }
    return true;
}

size_t comfoair_log_render(char *out, size_t cap)
{
    if (out == NULL || cap < 3)
        return 0;

    size_t used = 0;
    out[used++] = '[';

    const size_t first = s_count == COMFOAIR_LOG_LINES ? s_next : 0;
    for (size_t i = 0; i < s_count; i++) {
        if (i > 0) {
            if (used + 1 >= cap)
                return 0;
            out[used++] = ',';
        }
        if (used + 1 >= cap)
            return 0;
        out[used++] = '"';
        if (!append_escaped(out, cap - 1, &used, s_lines[(first + i) % COMFOAIR_LOG_LINES]))
            return 0;
        if (used + 1 >= cap)
            return 0;
        out[used++] = '"';
    }

    if (used + 2 > cap)
        return 0;
    out[used++] = ']';
    out[used]   = '\0';
    return used;
}

#ifdef ESP_PLATFORM
#include "esp_log.h"

static vprintf_like_t s_previous;

static int log_hook(const char *format, va_list args)
{
    char line[COMFOAIR_LOG_LINE_LEN];
    // vsnprintf on a copy: the original list is consumed by the console writer below, and
    // reusing a va_list after it has been walked is undefined.
    va_list copy;
    va_copy(copy, args);
    vsnprintf(line, sizeof line, format, copy);
    va_end(copy);

    // Most callers reach this hook once per logical log line, but the WiFi driver blob calls
    // it three times per line (prefix, body, trailing "\n" -- see comfoair_log_feed's comment
    // in the header); feed reassembles whichever pattern arrives before anything commits.
    comfoair_log_feed(line);
    return s_previous != NULL ? s_previous(format, args) : 0;
}

void comfoair_log_install(void)
{
    // The console keeps working: USB is the only channel when the network is the thing that
    // is broken, and this buffer is an addition to it, never a replacement.
    s_previous = esp_log_set_vprintf(log_hook);
}
#else
void comfoair_log_install(void) {}
#endif
