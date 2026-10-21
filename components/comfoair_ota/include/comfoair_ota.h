// What an uploaded firmware image must look like before a byte of it reaches flash, and what the
// client is told when it does not.
//
// PURE. No flash, no HTTP, no esp_err_t: the HTTP handler (comfoair_http_ota.c) asks these
// questions and maps IDF's answers onto comfoair_ota_fault_t, so every refusal and its wording is
// host-tested (test_ota). The checks are cheap pre-flight only -- esp_ota_end() still runs IDF's
// full image verification (segments, checksum, appended SHA-256) and that is what decides.
//
// THERE IS NO SIGNATURE CHECK, deliberately not hidden here: secure boot is off, so anyone who can
// POST /api/ota can run code. That is why comfoair_policy refuses the path outright on a device
// without a password -- these checks protect against the WRONG file, not
// against a hostile one.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// esp_image_header_t (esp_app_format.h): magic at byte 0, chip_id (LE u16) at bytes 12-13. The
// first 24 bytes are the whole header; the pre-flight needs only these.
#define COMFOAIR_OTA_HEADER_BYTES 24
#define COMFOAIR_OTA_IMAGE_MAGIC 0xE9
// The smallest image worth flashing. Anything shorter is not an app image of this firmware
// (~1.3 MB); refusing it early keeps a stray text file from erasing a slot.
#define COMFOAIR_OTA_MIN_IMAGE_BYTES (64u * 1024u)

typedef enum {
    COMFOAIR_OTA_OK = 0,
    COMFOAIR_OTA_NO_LENGTH,       // no Content-Length (chunked or absent): the slot fit cannot be checked
    COMFOAIR_OTA_TOO_SMALL,       // < COMFOAIR_OTA_MIN_IMAGE_BYTES
    COMFOAIR_OTA_TOO_LARGE,       // > the update slot
    COMFOAIR_OTA_NOT_AN_IMAGE,    // first byte is not 0xE9 (a .bin.gz, an ELF, a web page...)
    COMFOAIR_OTA_WRONG_CHIP,      // built for another chip (an ESP32-C3 .bin on this C6)
    COMFOAIR_OTA_TRUNCATED,       // the client stopped sending before Content-Length
    COMFOAIR_OTA_VERIFY_FAILED,   // esp_ota_end / set_boot_partition rejected the image
    COMFOAIR_OTA_BUSY,            // another upload is in progress
    COMFOAIR_OTA_FLASH_ERROR,     // begin/write failed for a reason that is not the file's fault
    COMFOAIR_OTA_NO_SLOT,         // no partition to update into
    COMFOAIR_OTA_UNCONFIRMED,     // the running app is still on probation (rollback pending)
    COMFOAIR_OTA_WRONG_PROJECT,   // a valid ESP image of ANOTHER firmware (no app descriptor, or
                                  // project_name != this one) -- e.g. the OpenTherm thermostat's C6 build
} comfoair_ota_fault_t;

// esp_app_desc_t sits right after the image header (24 B) and the first segment header (8 B):
// magic 0xABCD5432 (LE) at 0x20, version[32] at 0x30, project_name[32] at 0x50 (esp_app_desc.h;
// verified on this firmware's own firmware.bin). The upload handler buffers this many bytes before
// esp_ota_begin(), so a foreign image is refused before anything is erased.
#define COMFOAIR_OTA_APP_DESC_OFFSET 0x20
#define COMFOAIR_OTA_PREFLIGHT_BYTES 0x70  // through the end of project_name
#define COMFOAIR_OTA_APP_DESC_MAGIC 0xABCD5432u

// On the first COMFOAIR_OTA_PREFLIGHT_BYTES of the body: the app descriptor's magic, and
// project_name equal to `expected_project` (esp_app_get_description()->project_name of the running
// firmware). Fewer bytes, wrong magic or another name -> WRONG_PROJECT. Copies the image's version
// string (NUL-terminated, at most `version_cap`-1 chars) into `version_out` when non-NULL, for the
// log line and the 200 answer.
comfoair_ota_fault_t comfoair_ota_check_app_desc(const uint8_t *head, size_t len,
                                                 const char *expected_project, char *version_out,
                                                 size_t version_cap);

// Before reading the body. `slot_bytes` is the update partition's size.
comfoair_ota_fault_t comfoair_ota_check_length(bool has_length, size_t content_length,
                                               size_t slot_bytes);

// On the first COMFOAIR_OTA_HEADER_BYTES of the body, before esp_ota_begin(): nothing is erased for
// a file that fails this. `expected_chip_id` is the esp_chip_id_t of the running chip (0x000D on
// the C6). Fewer than COMFOAIR_OTA_HEADER_BYTES bytes -> NOT_AN_IMAGE.
comfoair_ota_fault_t comfoair_ota_check_header(const uint8_t *head, size_t len,
                                               uint16_t expected_chip_id);

// The HTTP answer for a fault: status line ("422 Unprocessable Entity") and an English sentence for
// the {"error": ...} body, which the web UI shows as is. Never NULL; OK maps to "200 OK" / "".
const char *comfoair_ota_status_line(comfoair_ota_fault_t fault);
const char *comfoair_ota_message(comfoair_ota_fault_t fault);

#ifdef __cplusplus
}
#endif
