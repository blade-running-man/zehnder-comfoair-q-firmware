// comfoair_wire: the bounded writer and the result constructors that the renderers and the
// parsers share. PRIVATE -- it sits beside the sources, not in include/, and nothing outside this
// component includes it. The two rules in comfoair_wire.c govern everything here.
//
// static inline, NOT a comfoair_wire_writer.c. As external functions the writer would be
// link-visible symbols named `put` and `finish` -- the unprefixed export comfoair_http_internal.h
// forbids, a duplicate-symbol error or a silent bind to another component's object -- and
// prefixing them would rename every put_key(&w, ...) in every renderer. As static inline each
// file gets its own copy, the names stay put_*, and nothing new becomes visible to the linker.
// A file that includes this and calls only some of it draws no unused-function warning.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "comfoair_json.h"
#include "comfoair_wire.h"

// --- writing ------------------------------------------------------------------------------

// A bounded append, so truncation is handled once rather than at forty call sites. The same shape
// as comfoair_api.cpp's Writer and for the same reason: a caller that gets a plausible short
// length back serves valid-looking JSON that stops mid-object with a 200 in front of it, and
// nothing in the log says so. Phase 4's counter-review found the entity list twenty-three bytes
// from exactly that.
typedef struct {
    char  *buf;
    size_t cap;
    size_t used;
    bool   overflowed;
} writer_t;

static inline void put(writer_t *w, const char *s)
{
    if (w->buf == NULL || w->cap == 0) {
        w->overflowed = true;
        return;
    }
    while (*s) {
        if (w->used + 1 >= w->cap) {
            w->overflowed = true;
            return;
        }
        w->buf[w->used++] = *s++;
    }
    w->buf[w->used] = '\0';
}

static inline void put_u32(writer_t *w, uint32_t value)
{
    char tmp[12];
    snprintf(tmp, sizeof tmp, "%u", (unsigned)value);
    put(w, tmp);
}

// Sixty-four bits, for `uptime_ms` alone. A separate writer rather than widening put_u32: every
// other number in these documents is a counter or a duration that genuinely fits, and one that
// silently promoted them all would hide the day one of them stopped fitting.
static inline void put_u64(writer_t *w, uint64_t value)
{
    char tmp[21];
    snprintf(tmp, sizeof tmp, "%llu", (unsigned long long)value);
    put(w, tmp);
}

static inline void put_i32(writer_t *w, int32_t value)
{
    char tmp[13];
    snprintf(tmp, sizeof tmp, "%d", (int)value);
    put(w, tmp);
}

static inline void put_bool(writer_t *w, bool value) { put(w, value ? "true" : "false"); }

// Every string a renderer writes goes through the escaper, including the ones that "cannot"
// contain anything interesting. An SSID is chosen by whoever set up the router next door, and a
// device name is chosen by the owner; both reach a browser as JSON.
static inline void put_string(writer_t *w, const char *s)
{
    char escaped[512];
    comfoair_json_escape(s != NULL ? s : "", escaped, sizeof escaped);
    put(w, "\"");
    put(w, escaped);
    put(w, "\"");
}

static inline void put_key(writer_t *w, const char *key)
{
    put(w, "\"");
    put(w, key);
    put(w, "\":");
}

// COMFOAIR_PROV_NEVER is not a duration and must not be rendered as one. "No time left" and "no
// end" are opposite answers: a window with 0 ms left is closed, a window that never closes is
// the device sealed behind the front panel of a running unit, and a page that showed 0 for both
// would count that device down to a deadline it does not have.
static inline void put_duration(writer_t *w, uint32_t ms)
{
    if (ms == COMFOAIR_PROV_NEVER)
        put(w, "null");
    else
        put_u32(w, ms);
}

static inline size_t finish(writer_t *w) { return w->overflowed ? 0 : w->used; }

// --- the result constructors -----------------------------------------------------------------

static inline comfoair_wire_result_t ok(void)
{
    const comfoair_wire_result_t r = {COMFOAIR_WIRE_OK, COMFOAIR_CONFIG_OK, NULL};
    return r;
}

static inline comfoair_wire_result_t bad_body(void)
{
    const comfoair_wire_result_t r = {COMFOAIR_WIRE_BAD_BODY, COMFOAIR_CONFIG_OK, NULL};
    return r;
}

static inline comfoair_wire_result_t bad_field(const char *field)
{
    const comfoair_wire_result_t r = {COMFOAIR_WIRE_BAD_FIELD, COMFOAIR_CONFIG_OK, field};
    return r;
}

static inline comfoair_wire_result_t wrong_route(const char *field)
{
    const comfoair_wire_result_t r = {COMFOAIR_WIRE_WRONG_ROUTE, COMFOAIR_CONFIG_OK, field};
    return r;
}

static inline comfoair_wire_result_t refused(comfoair_config_err_t err, const char *field)
{
    const comfoair_wire_result_t r = {COMFOAIR_WIRE_REFUSED, err, field};
    return r;
}
