// Host stand-in for esp_log.h.
//
// It does not print: a passing suite must have pristine output. What it DOES do is
// record every call, and whether the call happened while the fake driver was inside
// an interrupt callback. comfoair_can.h promises the ISR "must not log", and this is
// what makes that promise testable rather than aspirational.
#pragma once
#include <inttypes.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void fake_log_record(char level, const char *tag);

// The arguments are type-checked for -Wformat's benefit and then discarded, so a broken
// format string in the production source still fails the build on the host.
void fake_log_sink(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef __cplusplus
}
#endif

#define ESP_LOGE(tag, fmt, ...) do { fake_log_record('E', tag); if (0) fake_log_sink(fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGW(tag, fmt, ...) do { fake_log_record('W', tag); if (0) fake_log_sink(fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGI(tag, fmt, ...) do { fake_log_record('I', tag); if (0) fake_log_sink(fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGD(tag, fmt, ...) do { fake_log_record('D', tag); if (0) fake_log_sink(fmt, ##__VA_ARGS__); } while (0)
#define ESP_LOGV(tag, fmt, ...) do { fake_log_record('V', tag); if (0) fake_log_sink(fmt, ##__VA_ARGS__); } while (0)
