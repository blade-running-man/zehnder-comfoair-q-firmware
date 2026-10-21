// POST /api/ota's pre-flight: what the running firmware expects of an uploaded image, asked of
// comfoair_ota's pure checks (host-tested in test_ota). Its own file only because
// comfoair_http_ota.c would otherwise pass the 350-line ceiling; this is the one place that knows
// the RUNNING chip and project, the handler only moves bytes and answers.
#include "comfoair_http_internal.h"

#include <stdint.h>

#include "comfoair_ota.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "comfoair_ota";

// The chip the BOOTLOADER will accept: the same constant bootloader_common_check_chip_validity()
// compares the header's chip_id against (bootloader_common_loader.c:123). Asking esp_chip_info()
// and mapping its model would be a second table that could disagree with the one that decides.
_Static_assert(CONFIG_IDF_FIRMWARE_CHIP_ID <= UINT16_MAX, "chip_id is a u16 in the image header");

comfoair_ota_fault_t comfoair_http_ota_preflight(const uint8_t *head, size_t len, const char *slot,
                                                 char *version)
{
    // Header first: a .bin.gz or a C3 image gets the actionable answer, not "different device".
    // DO NOT swap the order: check_app_desc() reads a gzip's bytes as a descriptor and calls every
    // non-image a WRONG_PROJECT, which sends the owner looking for the wrong mistake.
    comfoair_ota_fault_t fault =
        comfoair_ota_check_header(head, len, (uint16_t)CONFIG_IDF_FIRMWARE_CHIP_ID);
    if (fault != COMFOAIR_OTA_OK)
        return fault;
    // The descriptor then catches the owner's OTHER C6 firmware (the OpenTherm thermostat), which
    // passes the chip check. The expected name is the RUNNING image's, not a literal, so it cannot
    // drift from project() in the top-level CMakeLists.txt.
    const char *project = esp_app_get_description()->project_name;
    fault = comfoair_ota_check_app_desc(head, len, project, version, COMFOAIR_HTTP_OTA_VERSION_CAP);
    if (fault == COMFOAIR_OTA_OK)
        ESP_LOGI(TAG, "image %s (%s) -> %s", version, project, slot);
    else
        ESP_LOGW(TAG, "image %s is not %s firmware", version[0] != '\0' ? version : "?", project);
    return fault;
}
