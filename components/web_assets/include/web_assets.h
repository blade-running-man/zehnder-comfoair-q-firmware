// The compiled-in web UI: three gzipped files, served as they are.
//
// Everything here is already gzip-compressed by Vite at build time, so the
// handler sets Content-Encoding: gzip and never compresses anything at runtime.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
// main.cpp is C++ (the protocol headers are), these components are C. Without this
// the linker looks for mangled names and fails with "undefined reference".
extern "C" {
#endif

typedef struct {
    const char    *path;          // "/", "/app.js", "/style.css"
    const char    *content_type;
    const unsigned char *start;
    size_t         len;
} web_asset_t;

// NULL-terminated; iterate until .path is NULL.
const web_asset_t *web_assets(void);

#ifdef __cplusplus
}
#endif
