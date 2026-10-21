// The device log the web UI reads, kept in RAM.
//
// A fixed ring, because the alternative is a device that runs out of memory after a week of
// uptime. When it fills, the OLDEST line goes: during an incident the interesting lines are
// always the most recent ones, and a log that stops recording when full is worse than none.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 80 lines of 160 characters is about 13 KB, which buys roughly the last minute of a busy
// boot. Both are deliberate round numbers rather than measurements; raise them only against
// a measured RAM figure, not a feeling.
#define COMFOAIR_LOG_LINES    80
#define COMFOAIR_LOG_LINE_LEN 160

void comfoair_log_reset(void);

// Records one line, truncating anything longer than a slot. Never allocates and never
// blocks: it is called from whatever task happened to log, including at boot.
void comfoair_log_write(const char *line);

// Feeds one fragment of a vprintf-hook call and commits a ring entry (via comfoair_log_write)
// for each complete line found inside it, buffering whatever follows the last newline for the
// next call.
//
// Ordinary ESP-IDF logging arrives here as one complete call per line: this build uses
// CONFIG_LOG_VERSION=1 (sdkconfig.h:768), under which esp_log_va() makes exactly one
// esp_log_vprint_func call carrying the whole formatted line, "\n" included
// (components/log/src/log.c:41-44). The closed WiFi driver blob does not go through that path
// the same way: it calls the same vprint_func three times per logical trace line via
// g_osi_funcs_p->_log_write/_log_writev (esp_wifi/include/esp_private/wifi_os_adapter.h:
// 119-120) -- a prefix call, a message-body call, and a final call carrying only "\n".
// Feeding those three fragments straight to comfoair_log_write, one ring entry per call, is
// what used to turn every WiFi trace line into three ring slots -- the third of them empty,
// since a lone "\n" survives comfoair_log_write's "not empty" check and is then stripped to
// "" by its trailing-newline trim. comfoair_log_feed exists so the ring only ever sees whole
// lines, regardless of how many hook calls a given source splits one across.
//
// Do not call comfoair_log_write directly while a line is still pending here: it commits
// straight to the ring ahead of the fragment comfoair_log_feed is still assembling, putting
// the two out of order.
void comfoair_log_feed(const char *chunk);

// Renders the buffer as a JSON array of strings, oldest first. Returns 0 if it does not fit
// -- the same contract as the other renderers, so a caller cannot serve half a document
// with 200 OK.
size_t comfoair_log_render(char *out, size_t cap);

// Routes ESP-IDF's own logging through this buffer as well as the console. Device only.
void comfoair_log_install(void);

#ifdef __cplusplus
}
#endif
