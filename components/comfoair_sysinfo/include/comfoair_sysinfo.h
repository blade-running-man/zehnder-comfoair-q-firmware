// GET /api/system: who the ventilation unit is, who this device is, and how the links between
// them are doing -- the page an owner opens first when something looks wrong.
//
// PURE. The HTTP handler fills one comfoair_sysinfo_t from the components that own each number
// (the probe task for the unit, IDF for the chip and heap, comfoair_can, comfoair_control,
// comfoair_mqtt, comfoair_net) and this renders it, so the document's shape and every null rule is
// host-tested. It also owns the unit's error-id -> text table: the one place that table lives.
//
// THREAD CONTEXT: none. The caller owns the snapshot for the duration of the call.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// NODE 0x01/01 strings. The live Q350 serial is 31 characters + NUL and its model string 25;
// 48 leaves room for longer models without a second size.
#define COMFOAIR_SYSINFO_TEXT_MAX 48
#define COMFOAIR_SYSINFO_ERRORS_BYTES 32  // GETACTIVEERRORS bitfield (comfoair-pdo-rmi.md §3)
#define COMFOAIR_SYSINFO_MAX_NODES 64

typedef enum {
    COMFOAIR_SYSINFO_READ_UNKNOWN = 0,  // not read yet (boot, or no bus)
    COMFOAIR_SYSINFO_READ_OK,           // the last read answered
    COMFOAIR_SYSINFO_READ_NO_ANSWER,    // the last read went unanswered; older values, if any, kept
} comfoair_sysinfo_read_t;

// The ventilation unit, as the probe task last read it. A string field is "" when that property
// never answered; firmware_raw is meaningful only when has_firmware.
typedef struct {
    comfoair_sysinfo_read_t identity_state;
    uint32_t identity_age_ms;  // since the last successful identity read; ignored when never read
    bool identity_ever_read;
    char model[COMFOAIR_SYSINFO_TEXT_MAX];    // 0x08
    char serial[COMFOAIR_SYSINFO_TEXT_MAX];   // 0x04
    char article[COMFOAIR_SYSINFO_TEXT_MAX];  // 0x0B
    char country[COMFOAIR_SYSINFO_TEXT_MAX];  // 0x0D
    char name[COMFOAIR_SYSINFO_TEXT_MAX];     // 0x14
    bool has_firmware;
    uint32_t firmware_raw;  // 0x06, decoded by comfoair_probe_version_text()

    comfoair_sysinfo_read_t errors_state;
    uint8_t errors[COMFOAIR_SYSINFO_ERRORS_BYTES];  // valid when errors_state == OK
} comfoair_sysinfo_unit_t;

typedef struct {
    const char *firmware;      // esp_app_desc_t.version
    const char *idf;           // esp_app_desc_t.idf_ver
    const char *built;         // "<date> <time>" from esp_app_desc_t
    const char *board;         // board_t.name
    const char *chip;          // e.g. "esp32c6"
    uint16_t chip_revision;    // esp_chip_info revision, major*100 + minor
    uint32_t flash_bytes;
    const char *partition;     // running app partition label, e.g. "ota_0"
    uint64_t uptime_ms;
    const char *reset_reason;  // a fixed lower-case word, see the handler's mapping
    uint32_t heap_free;
    uint32_t heap_min_free;
    uint32_t heap_largest_block;
    const char *device_id;
    const char *mac;
} comfoair_sysinfo_device_t;

typedef struct {
    bool connected;
    const char *ssid;  // NULL or "" when not connected
    const char *ip;
    bool has_rssi;
    int8_t rssi;
    uint8_t channel;  // 0 = unknown
} comfoair_sysinfo_network_t;

typedef struct {
    bool started;
    const char *state;  // "active" | "warning" | "passive" | "off" | "stopped"
    uint32_t rx_frames;
    uint32_t rx_dropped;
    uint32_t bus_errors;
    uint32_t bus_recoveries;
    uint16_t tx_error_count;
    uint16_t rx_error_count;
    const uint8_t *nodes_seen;  // ascending
    size_t nodes_seen_count;
} comfoair_sysinfo_bus_t;

typedef struct {
    uint32_t queued, sent, failed, dropped;
} comfoair_sysinfo_commands_t;

typedef struct {
    bool configured, connected;
    uint32_t published, commands, rejected, reconnects;
} comfoair_sysinfo_mqtt_t;

typedef struct {
    comfoair_sysinfo_unit_t unit;
    comfoair_sysinfo_device_t device;
    comfoair_sysinfo_network_t network;
    comfoair_sysinfo_bus_t bus;
    comfoair_sysinfo_commands_t commands;
    comfoair_sysinfo_mqtt_t mqtt;
} comfoair_sysinfo_t;

// The English text for a unit error id, or NULL for an id the table does not know. The table is
// chosen by firmware: one table for firmware above R1.4.0, another at or below (the selector is
// raw <= 3222278144 = 0xC0101000, comfoair-pdo-rmi.md §4). Without a firmware version the newer
// table is used -- every unit this firmware has met runs R1.11.0.
const char *comfoair_sysinfo_error_text(uint16_t id, bool has_firmware, uint32_t firmware_raw);

// True when the id, in the firmware-selected table, is a NOTICE rather than a fault: the owner's
// own doing (a manual bypass, service mode, a cooker hood switched on) or a routine reminder (order
// filters). Such an id never turns `unit_error` on. False for an unknown id -- an error nobody
// can name is never assumed harmless -- and false for EVERY id without a firmware read: the
// numbering is then unknown, and old 79/80 are faults where new 79/80 are notices. The per-id
// evidence is in comfoair_sysinfo_errors.cpp.
bool comfoair_sysinfo_error_informational(uint16_t id, bool has_firmware, uint32_t firmware_raw);

// The active errors as ONE LINE, for the `active_errors` entity (tools/pdo_table.py UNIT_ERRORS).
// At most this many bytes, because Home Assistant refuses a longer state: homeassistant/const.py:61
// MAX_LENGTH_STATE_STATE = 255, enforced by core.py:203 and, for an MQTT sensor, by
// check_state_too_long() (components/mqtt/util.py:377, called at sensor.py:340) -- which turns the
// whole state unknown rather than cutting it.
#define COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX 255
// The line while no error is active. NOT "None": Home Assistant reads that literal as "no value"
// (components/mqtt/sensor.py:317), so a healthy unit would show unknown.
#define COMFOAIR_SYSINFO_NO_ERRORS "No errors"

// The `unit_error` verdict: true when any bit of a COMFOAIR_SYSINFO_ERRORS_BYTES bitfield names an
// id that is not comfoair_sysinfo_error_informational() in the firmware-selected table (an unknown
// id counts). False for NULL. Takes the firmware because the numbering does: 81 is the manual
// bypass at R1.4.0 and a preheater fault above it. DO NOT reduce this to "any bit set": a user's
// own bypass override would then raise a Home Assistant problem.
bool comfoair_sysinfo_errors_any(const uint8_t *errors, bool has_firmware, uint32_t firmware_raw);

// The errors as one line: ascending id, " | " between them, each comfoair_sysinfo_error_text()'s
// text (the firmware-selected table), informational ones included, "Unknown error <id>" for an id
// it does not know, COMFOAIR_SYSINFO_NO_ERRORS for no bit at all. Longer than
// COMFOAIR_SYSINFO_ERRORS_SUMMARY_MAX bytes it is cut where a UTF-8 character starts and ends in
// "..."; every id is still in GET /api/system. Returns the length (NUL-terminated), or 0 for a
// NULL argument or cap <= the limit.
size_t comfoair_sysinfo_errors_summary(const uint8_t *errors, bool has_firmware,
                                       uint32_t firmware_raw, char *out, size_t cap);

// Renders the document (schema: docs/device/ota-and-system.md) into
// `out`. Returns the bytes written, or 0 if it did not fit -- a defect, the buffer is sized for the
// worst case. Never writes past `cap`; NUL-terminates on success.
size_t comfoair_sysinfo_render(const comfoair_sysinfo_t *info, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
