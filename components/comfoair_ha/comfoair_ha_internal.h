// The two things the halves of this component share: a bounded writer and one predicate.
//
// A HEADER RATHER THAN A THIRD TRANSLATION UNIT, and `static inline` rather than linkage: these
// are five short functions on a struct that lives on the caller's stack, and giving them a .c
// file would put a call through the linker on the inner loop of the discovery burst, one document per entity. Nothing
// here holds state, so two translation units each getting their own copy costs nothing.
//
// INTERNAL. It is not in include/ and nothing outside components/comfoair_ha/ may include it: the
// public contract of this component is comfoair_ha.h, and a writer visible past that boundary is
// an invitation to format a topic somewhere the cleanup cannot see it -- which is the exact
// failure the enumerations in comfoair_ha.h exist to make impossible.
//
// It was inside comfoair_ha.c until 2026-09-07, when that file was split: the topics and the
// payloads are two responsibilities, the file was 563 lines against the 350-line ceiling
// (AGENTS.md), and the seam between "what a topic is called" and "what goes in it" already
// existed as a section comment.
#pragma once

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "comfoair_ha.h"
#include "comfoair_json.h"

// --- a bounded writer, the same shape as everywhere else in this project ---------------------

typedef struct {
    char  *buf;
    size_t cap;
    size_t used;
    bool   overflowed;
} writer_t;

static inline void put(writer_t *w, const char *s)
{
    if (w->buf == NULL || w->cap == 0 || s == NULL) {
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

static inline void put_num(writer_t *w, double value)
{
    char tmp[32];
    // %g so 21.5 stays 21.5 and 3 does not become 3.000000 -- the same choice comfoair_api makes,
    // and the payload is read by a person as often as by Home Assistant.
    snprintf(tmp, sizeof tmp, "%g", value);
    put(w, tmp);
}

static inline void put_string(writer_t *w, const char *s)
{
    char escaped[512];
    comfoair_json_escape(s != NULL ? s : "", escaped, sizeof escaped);
    put(w, "\"");
    put(w, escaped);
    put(w, "\"");
}

// A key and its string value, or NOTHING when the value is absent.
//
// generate_registry.py:58-59: "Absent metadata must be NULL, not "", so that a consumer can tell
// 'no unit' from 'the empty unit'." A discovery payload carrying "unit_of_measurement": "" hands
// Home Assistant an entity whose unit IS the empty string, which is a different entity.
static inline void put_opt(writer_t *w, const char *key, const char *value)
{
    if (value == NULL || value[0] == '\0')
        return;
    put(w, ",\"");
    put(w, key);
    put(w, "\":");
    put_string(w, value);
}

static inline size_t finish(writer_t *w) { return w->overflowed ? 0 : w->used; }

// Whether a context can name anything at all. Every public builder asks this first and answers 0
// rather than dereferencing: comfoair_ha_ctx_t is BORROWED (comfoair_ha.h) and its strings belong
// to whoever built the client, so a partly-filled context reaches here during a reconfigure.
static inline bool usable(const comfoair_ha_ctx_t *ctx)
{
    return ctx != NULL && ctx->topic_prefix != NULL && ctx->device_id != NULL;
}
