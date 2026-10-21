// comfoair_config: what the pure files of this component share and nobody else does. Private --
// it sits beside the sources, not in include/, so no other component can reach it. Read
// comfoair_config.h first; the component's shape is in README.md, Implementation.
//
// A name that leaves one file gets the component prefix: once it is not `static` it is visible to
// the linker, and a bare name collides with another component's. The helper below stays
// `static inline` under its own name, a private copy in each file that includes this;
// comfoair_config.c, _check.c and _password.c all ask it.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "comfoair_config.h"

static inline bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

typedef struct {
    uint32_t    iterations;
    uint8_t     salt[COMFOAIR_CONFIG_SALT_LEN];
    const char *digest_hex;  // borrowed from the record; exactly DIGEST_LEN*2 characters
} password_record_t;

// Parses a stored UI password record (`1$<iterations>$<salt hex>$<digest hex>`); false for
// anything this build would not have written. A NULL `out` validates only, which is how
// comfoair_config_sanitize() asks. Pure, no allocation; `out->digest_hex` borrows from `record`.
bool comfoair_config_parse_record(const char *record, password_record_t *out);
