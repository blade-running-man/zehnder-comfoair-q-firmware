// The pre-flight on an uploaded image and the answer for every refusal. Pure: see the header for
// why none of this touches flash or HTTP.
#include "comfoair_ota.h"

#include <string.h>

// esp_app_desc_t (esp_app_desc.h): magic_word u32, secure_version u32, reserv1[2] u32, then
// version[32] and project_name[32]. Offsets from the descriptor's start at
// COMFOAIR_OTA_APP_DESC_OFFSET.
#define APP_DESC_VERSION_AT (COMFOAIR_OTA_APP_DESC_OFFSET + 0x10)
#define APP_DESC_PROJECT_AT (COMFOAIR_OTA_APP_DESC_OFFSET + 0x30)
#define APP_DESC_FIELD_BYTES 32
_Static_assert(APP_DESC_PROJECT_AT + APP_DESC_FIELD_BYTES == COMFOAIR_OTA_PREFLIGHT_BYTES,
               "the preflight must end exactly at the end of project_name");

comfoair_ota_fault_t comfoair_ota_check_app_desc(const uint8_t *head, size_t len,
                                                 const char *expected_project, char *version_out,
                                                 size_t version_cap) {
    // Emptied first, so a refusal before the descriptor is trusted never leaves a caller's stale
    // buffer (or a foreign image's garbage) to be logged as a version.
    if (version_out != NULL && version_cap > 0) {
        version_out[0] = '\0';
    }
    if (head == NULL || expected_project == NULL || len < COMFOAIR_OTA_PREFLIGHT_BYTES) {
        return COMFOAIR_OTA_WRONG_PROJECT;
    }
    const uint8_t *m = head + COMFOAIR_OTA_APP_DESC_OFFSET;
    const uint32_t magic =
        (uint32_t)m[0] | ((uint32_t)m[1] << 8) | ((uint32_t)m[2] << 16) | ((uint32_t)m[3] << 24);
    if (magic != COMFOAIR_OTA_APP_DESC_MAGIC) {
        return COMFOAIR_OTA_WRONG_PROJECT;
    }
    // The version is copied even when the project then turns out foreign: the refusal's log line
    // names what was uploaded. Bounded by the field AND the caller: IDF's own build always leaves
    // a NUL (its _Static_assert counts it, esp_app_format/esp_app_desc.c:66), but the bytes here are
    // an uploaded file's, not a promise IDF made.
    if (version_out != NULL && version_cap > 0) {
        const char *v = (const char *)head + APP_DESC_VERSION_AT;
        const char *nul = memchr(v, '\0', APP_DESC_FIELD_BYTES);
        size_t n = nul != NULL ? (size_t)(nul - v) : APP_DESC_FIELD_BYTES;
        if (n > version_cap - 1) {
            n = version_cap - 1;
        }
        memcpy(version_out, v, n);
        version_out[n] = '\0';
    }
    // A name with no NUL inside its 32 bytes is not one IDF wrote (at most 31 chars and the
    // terminator, esp_app_desc.c:70), so it is refused rather than compared. DO NOT strncmp(.., 32):
    // that would accept 32 non-NUL bytes equal to the first 32 of a longer expected name.
    const char *p = (const char *)head + APP_DESC_PROJECT_AT;
    if (memchr(p, '\0', APP_DESC_FIELD_BYTES) == NULL || strcmp(p, expected_project) != 0) {
        return COMFOAIR_OTA_WRONG_PROJECT;
    }
    return COMFOAIR_OTA_OK;
}

comfoair_ota_fault_t comfoair_ota_check_length(bool has_length, size_t content_length,
                                               size_t slot_bytes) {
    // Without Content-Length the slot fit is unknowable until the slot is already being erased --
    // which is exactly what this check exists to prevent.
    if (!has_length) {
        return COMFOAIR_OTA_NO_LENGTH;
    }
    if (content_length < COMFOAIR_OTA_MIN_IMAGE_BYTES) {
        return COMFOAIR_OTA_TOO_SMALL;
    }
    // An image exactly the slot's size fits: the write path refuses only `size > partition->size -
    // dst_offset` (esp_partition/partition_target.c:73), so the last byte of the slot is writable.
    // DO NOT make this >=.
    if (content_length > slot_bytes) {
        return COMFOAIR_OTA_TOO_LARGE;
    }
    return COMFOAIR_OTA_OK;
}

comfoair_ota_fault_t comfoair_ota_check_header(const uint8_t *head, size_t len,
                                               uint16_t expected_chip_id) {
    if (head == NULL || len < COMFOAIR_OTA_HEADER_BYTES) {
        return COMFOAIR_OTA_NOT_AN_IMAGE;
    }
    // The magic is judged first so a wrong file gets the actionable answer ("pick firmware.bin"),
    // not a chip complaint about bytes that were never a header. The common wrong file is a
    // .bin.gz, which starts 1F 8B: the Update page's file filter used to offer .bin.gz, and the
    // device cannot gunzip. An .elf starts 7F 'E' 'L' 'F'.
    if (head[0] != COMFOAIR_OTA_IMAGE_MAGIC) {
        return COMFOAIR_OTA_NOT_AN_IMAGE;
    }
    // esp_image_header_t (framework-espidf/components/bootloader_support/include/esp_app_format.h):
    // magic, segment_count, spi_mode, spi_speed:4/spi_size:4, entry_addr (u32, bytes 4-7),
    // wp_pin, spi_pin_drv[3] (bytes 8-11), then chip_id as a packed little-endian u16 at 12-13.
    // Assembled byte by byte so the host's endianness and alignment do not matter.
    const uint16_t chip_id = (uint16_t)(head[12] | ((uint16_t)head[13] << 8));
    if (chip_id != expected_chip_id) {
        return COMFOAIR_OTA_WRONG_CHIP;
    }
    return COMFOAIR_OTA_OK;
}

const char *comfoair_ota_status_line(comfoair_ota_fault_t fault) {
    // Every enumerator is named and there is no `default`, so -Wswitch (on via -Wall in
    // [env:native]) flags a new fault without a status. DO NOT add a default: it would swallow it.
    switch (fault) {
    case COMFOAIR_OTA_OK:
        return "200 OK";
    case COMFOAIR_OTA_NO_LENGTH:
        return "411 Length Required";
    case COMFOAIR_OTA_TOO_SMALL:
    case COMFOAIR_OTA_NOT_AN_IMAGE:
    case COMFOAIR_OTA_WRONG_CHIP:
    case COMFOAIR_OTA_WRONG_PROJECT:
    case COMFOAIR_OTA_VERIFY_FAILED:
        // The request was well-formed; the file in it is the wrong one.
        return "422 Unprocessable Entity";
    case COMFOAIR_OTA_TOO_LARGE:
        // IDF's own server spells it this way (esp_http_server/src/httpd_txrx.c:486,
        // HTTPD_413_CONTENT_TOO_LARGE; RFC 9110 name). One spelling across the device's answers.
        return "413 Content Too Large";
    case COMFOAIR_OTA_TRUNCATED:
        return "400 Bad Request";
    case COMFOAIR_OTA_BUSY:
    case COMFOAIR_OTA_UNCONFIRMED:
        // Both are "not now", not "not this file": the same upload succeeds later.
        return "409 Conflict";
    case COMFOAIR_OTA_FLASH_ERROR:
    case COMFOAIR_OTA_NO_SLOT:
        return "500 Internal Server Error";
    }
    // Reached only by a value outside the enum (the header promises never NULL).
    return "500 Internal Server Error";
}

const char *comfoair_ota_message(comfoair_ota_fault_t fault) {
    // Shown as is on the Update page, so written for the owner, not for a developer. Static
    // strings: the handler sends them without formatting.
    switch (fault) {
    case COMFOAIR_OTA_OK:
        return "";
    case COMFOAIR_OTA_NO_LENGTH:
        return "The upload did not say how large the file is; send it with a Content-Length.";
    case COMFOAIR_OTA_TOO_SMALL:
        return "The file is too small to be a firmware image.";
    case COMFOAIR_OTA_TOO_LARGE:
        return "The file is larger than the update slot.";
    case COMFOAIR_OTA_NOT_AN_IMAGE:
        return "This is not a firmware image; pick firmware.bin from .pio/build/<env>/, "
               "not the .elf or a compressed file.";
    case COMFOAIR_OTA_WRONG_CHIP:
        return "This firmware was built for a different chip.";
    case COMFOAIR_OTA_WRONG_PROJECT:
        // The web's Update page refuses a foreign file with this same sentence before uploading
        // (test_ota pins it word for word). DO NOT reword one without the other.
        return "This file is firmware for a different device, not the ComfoAir Q controller.";
    case COMFOAIR_OTA_TRUNCATED:
        return "The upload stopped before the whole file arrived; try again.";
    case COMFOAIR_OTA_VERIFY_FAILED:
        return "The uploaded image failed verification and was not installed.";
    case COMFOAIR_OTA_BUSY:
        return "Another update is already being uploaded.";
    case COMFOAIR_OTA_FLASH_ERROR:
        return "Writing the update to flash failed; the running firmware is unchanged.";
    case COMFOAIR_OTA_NO_SLOT:
        return "This device has no partition to install an update into.";
    case COMFOAIR_OTA_UNCONFIRMED:
        return "The running firmware was just updated and is still being verified; "
               "try again in a minute.";
    }
    return "The update failed for an unknown reason.";
}
