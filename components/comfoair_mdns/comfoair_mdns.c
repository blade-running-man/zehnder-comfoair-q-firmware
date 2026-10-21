// The half of comfoair_mdns that touches the network: espressif/mdns, and nothing that could not
// be decided in comfoair_mdns_name.c.
//
// WRAPPED IN #ifdef ESP_PLATFORM, and it has to be: the host test build compiles every .c in this
// directory the moment a suite includes comfoair_mdns.h, and comfoair_button_reset.c includes it
// for the goodbye below -- so the button suites compile this file too. Same arrangement, and same
// reason, as comfoair_led_driver.c and comfoair_captive_dns_server.c.
//
// NO ESP_ERROR_CHECK ANYWHERE IN HERE. Same rule as comfoair_net_radio_try(): every call is
// tried, its failure is named, and the device carries on (nothing reboots because a peer is
// absent). The worst outcome this file may produce is a device that is reachable only by address --
// which is the device the owner has today.
#ifdef ESP_PLATFORM

#include "comfoair_mdns.h"

#include <stddef.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mdns.h"

static const char *TAG = "comfoair_mdns";

// The one service. `_comfoair._tcp` is NOT registered in v1 and its slot is the spare half of
// CONFIG_MDNS_MAX_SERVICES=2: nothing in this firmware's world browses for it (Home Assistant
// comes in over MQTT), and a second TXT would be a second list that eventually disagrees.
#define SERVICE_TYPE  "_http"
#define SERVICE_PROTO "_tcp"

// COPIED, NOT BORROWED. mdns_service_add() does strdup the TXT it is given
// (mdns_responder.c:1202), so borrowing would survive that one call -- but comfoair_mdns_rename()
// runs minutes later against s_host and s_instance, and `version` is the harder case: the field
// esp_app_get_description() returns is a 32-BYTE ARRAY, not a string, and an image whose version
// fills it carries no terminator (esp_app_format.h). Reading it as a C string is a walk off the
// end of the descriptor, so the bound is applied here, once.
//
// 63 for a board name is one DNS label, which is the ceiling test_mdns_name measures the packet
// budget against. Longer than that is a board file nobody would type, and it is truncated rather
// than dropped: `board` is a diagnostic, not an identity.
static char s_host[COMFOAIR_MDNS_HOST_MAX + 1];
static char s_instance[COMFOAIR_MDNS_INSTANCE_MAX + 1];
static char s_board[64];
static char s_version[33];
static char s_device_id[64];

// Whether mdns_init() succeeded, and the only thing the two entry points below key on. NOT a
// second idea of "is mDNS up": the component owns that and re-raises its own per-interface PCBs on
// address changes without being asked.
static bool s_up;
static bool s_service_up;

// comfoair_mdns_txt_t is declared in our own header so that it compiles on the host, where mdns.h
// does not exist. These two lines are what stop the two definitions drifting: the cast below is
// only sound while the layouts agree.
_Static_assert(sizeof(comfoair_mdns_txt_t) == sizeof(mdns_txt_item_t),
               "comfoair_mdns_txt_t must stay layout-compatible with mdns_txt_item_t");
_Static_assert(offsetof(comfoair_mdns_txt_t, value) == offsetof(mdns_txt_item_t, value),
               "comfoair_mdns_txt_t must stay layout-compatible with mdns_txt_item_t");

esp_err_t comfoair_mdns_start(const comfoair_mdns_config_t *config)
{
    if (config == NULL)
        return ESP_ERR_INVALID_ARG;

    // The four characters that name the access point, from the ONE producer -- passed in, because
    // this component must not include comfoair_net.h (see the header). Not read from a MAC here:
    // choosing which MAC is comfoair_net's decision, and the trap (SoftAP, not STA) lives there.
    comfoair_mdns_hostname(config->short_id, s_host);
    comfoair_mdns_instance(config->device_name, s_host, s_instance);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mDNS did not start (%s); the device is reachable by address only",
                 esp_err_to_name(err));
        return err;
    }
    s_up = true;

    err = mdns_hostname_set(s_host);
    if (err != ESP_OK) {
        // The host name is the point of the whole component -- without it the responder is up and
        // answers to nothing anybody would type. Said at ERROR, and still not fatal.
        ESP_LOGE(TAG, "mDNS host name '%s' was refused: %s", s_host, esp_err_to_name(err));
        return err;
    }

    const esp_err_t inst_err = mdns_instance_name_set(s_instance);
    if (inst_err != ESP_OK)
        ESP_LOGW(TAG, "mDNS instance name was refused: %s", esp_err_to_name(inst_err));

    // NO SERVICE WHEN THE WEB SERVER DID NOT COME UP. A name that resolves to a
    // refused connection is worse than no name: the owner reads it as "the device is broken" and
    // goes looking in the wrong place. The host name stays -- a ping and the router's client list
    // still work -- and that is the honest half.
    if (config->http_port == 0) {
        ESP_LOGW(TAG, "http://%s.local/ -- name only; the web server did not start, so no "
                      "_http._tcp service is advertised",
                 s_host);
        return ESP_OK;
    }

    comfoair_mdns_copy_field(s_board, sizeof s_board, config->facts.board);
    comfoair_mdns_copy_field(s_version, sizeof s_version, config->facts.version);
    comfoair_mdns_copy_field(s_device_id, sizeof s_device_id, config->facts.device_id);
    const comfoair_mdns_facts_t facts = {s_board, s_version, s_device_id};

    comfoair_mdns_txt_t txt[COMFOAIR_MDNS_TXT_MAX];
    const size_t        n = comfoair_mdns_txt(&facts, txt, COMFOAIR_MDNS_TXT_MAX);

    // The port comes from the caller and is never a constant here: comfoair_http is parameterised
    // by it (comfoair_http.h, COMFOAIR_HTTP_DEFAULT_CONFIG), and an SRV record disagreeing with the
    // listener is a name that resolves into a refused connection -- the failure the branch above
    // exists to avoid, arrived at by a different route.
    err = mdns_service_add(s_instance, SERVICE_TYPE, SERVICE_PROTO, config->http_port,
                           (mdns_txt_item_t *)txt, n);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "http://%s.local/ resolves, but the _http._tcp record was refused: %s",
                 s_host, esp_err_to_name(err));
        return ESP_OK;
    }
    s_service_up = true;

    // The line the owner needs in the log, and the one that makes hardware scenarios HV-PROV-02 and
    // HV-PROV-19 (docs/hardware-verification/05-provisioning-and-recovery.md) checkable at all: it
    // names the string to type. The octet count is measured against the 200 of RFC 6763 §6.2 rather
    // than asserted -- the RAM and flash cost of espressif/mdns was only ever estimated, and a run
    // on a board is what replaces the estimate.
    ESP_LOGI(TAG, "findable at http://%s.local:%u/ as '%s' (%u TXT pairs, %u octets)", s_host,
             (unsigned)config->http_port, s_instance, (unsigned)n,
             (unsigned)comfoair_mdns_txt_bytes(txt, n));
    return ESP_OK;
}

void comfoair_mdns_rename(const char *device_name)
{
    // The HOST NAME DOES NOT MOVE, and that is the decision this function implements rather than a
    // limitation of it. A bookmark, an /etc/hosts entry and the tab the owner is
    // renaming from all survive; what changes is the string a service browser shows.
    if (!s_up || !s_service_up)
        return;

    char next[COMFOAIR_MDNS_INSTANCE_MAX + 1];
    comfoair_mdns_instance(device_name, s_host, next);
    if (strcmp(next, s_instance) == 0)
        return;

    const esp_err_t err =
        mdns_service_instance_name_set(SERVICE_TYPE, SERVICE_PROTO, next);
    if (err != ESP_OK) {
        // WARN and no further action: the save has already succeeded and been answered. A rename
        // the responder would not take is a cosmetic difference in a service browser, and turning
        // it into a failed POST /api/config would be a settings page that refuses a legal name
        // because of mDNS.
        ESP_LOGW(TAG, "the device was renamed but the mDNS record was not: %s",
                 esp_err_to_name(err));
        return;
    }
    // strlen + 1, NOT sizeof: everything in `next` past its terminator is whatever the stack
    // held, and copying it is undefined behaviour by the letter even though nothing reads it.
    memcpy(s_instance, next, strlen(next) + 1);
    ESP_LOGI(TAG, "mDNS instance name is now '%s'; http://%s.local/ is unchanged", s_instance,
             s_host);
}

// The task that actually calls mdns_free(). ITS WHOLE PURPOSE IS TO BE THE THING THAT HANGS if
// mdns_free() hangs -- see the header for the two unbounded waits inside it -- so that the caller,
// which is one statement from esp_restart() on a device whose settings are already erased, is not.
//
// Deletes itself. If mdns_free() never returns it never gets there, and that is fine: the chip is
// about to be reset out from under it.
static void goodbye_task(void *arg)
{
    (void)arg;
    mdns_free();
    vTaskDelete(NULL);
}

void comfoair_mdns_goodbye(void)
{
    if (!s_up)
        return;
    // Cleared BEFORE the task starts, so a second call cannot hand mdns_free() to two tasks.
    s_up         = false;
    s_service_up = false;

    // Priority above the caller's (the button poller runs at 4, comfoair_button_task.c:45) so the
    // goodbye is issued inside the caller's wait rather than after it. 3072 bytes: mdns_free()
    // walks the service list, drains a queue and frees, with no recursion and no printf.
    if (xTaskCreate(goodbye_task, "comfoair_bye", 3072, NULL, 5, NULL) != pdPASS)
        // Not fatal and not retried. The caller restarts regardless; the neighbours keep a stale
        // record for a couple of minutes, which is the same outcome as a power cut.
        ESP_LOGW(TAG, "no room to say goodbye over mDNS; the name will time out of caches instead");
}

#endif  // ESP_PLATFORM
