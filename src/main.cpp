// Phase 2: bring the CAN bus up and prove PDO decoding works against the real unit.
//
// There is no state model yet -- that is phase 3 -- so decoded values are logged and
// dropped. Wi-Fi, HTTP and MQTT do not exist yet either. AGENTS.md does not let a
// phase be called done because it compiled: this one is verified against the unit.
#include <cinttypes>
#include <cstdio>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "board.h"
#include "comfoair_button_task.h"
#include "comfoair_can.h"
#include "comfoair_control.h"
#include "comfoair_http.h"
#include "comfoair_led_driver.h"
#include "comfoair_log.h"
#include "comfoair_mdns.h"
#include "comfoair_mqtt.h"
#include "comfoair_ota_runtime.h"
#include "comfoair_net.h"
#include "comfoair_phase_monitor.h"
#include "comfoair_poll.h"
#include "comfoair_probe_task.h"
#include "comfoair_state.h"
#include "app_mdns.h"
#include "pdo_decode.h"
#include "rmi_response.h"
#include "web_assets.h"

static const char *TAG = "comfoair";

static void nvs_init()
{
    esp_err_t err = nvs_flash_init();
    // A truncated or version-bumped NVS is recoverable and must be: every runtime setting
    // lives there, and refusing to boot would strand a device that only needs its settings
    // cleared. Nothing is configured at compile time (AGENTS.md), so this costs settings, never
    // function.
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS unusable (%s); erasing", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// The probe task's observer: its settings read-back, or a unit read's active errors (the full
// read or the once-a-minute errors read), moved the model. Both calls only set a flag.
static void on_probe_state_changed(void)
{
    comfoair_http_notify_state_changed();
    comfoair_mqtt_notify_state_changed();
}

static void on_can_frame(uint32_t can_id, bool extended, bool rtr, const uint8_t *data,
                         size_t len, void *)
{
    // The id test alone is not enough and pdo_decode.h says so: a remote-transmission
    // request from another node carries an identical id, and a standard-id frame in the
    // 0x40-0x7f range passes it too. Without this guard an RTR from any other node on the
    // bus overwrites entity values with nothing -- silently, because the frame still counts
    // as claimed.
    if (!extended || rtr)
        return;

    // EVERY frame, before the PDO and RMI branches below return: the probe's node census must see
    // PDO broadcasters and heartbeats too, not only RMI traffic. It never blocks (a full reply
    // queue drops), which is what makes calling it from the bus-reading task allowed at all.
    comfoair_probe_task_on_frame(can_id, data, len);

    if (comfoair::is_pdo_can_id(can_id)) {
        const uint16_t pdo = comfoair::pdo_id_from_can_id(can_id);
        const uint8_t  node = comfoair::node_id_from_can_id(can_id);

        // Signedness, divider and the all-ones sentinel are per-entity properties, and
        // they live in exactly one place: the registry generated from pdo_table.py. One
        // frame can feed several entities -- PDO 337 feeds three -- so this is one-to-many.
        if (comfoair_state_apply_pdo(pdo, data, len)) {
            // Tell both transports. Each coalesces inside, so a full sweep of the unit's PDOs
            // costs one pushed document and one bounded burst of publishes rather than one of
            // each per PDO -- and NEITHER of these blocks. Publishing from here is forbidden outright:
            // a blocked call on this path stops the task that reads the
            // bus, the TWAI receive queue overflows and PDOs are lost.
            comfoair_http_notify_state_changed();
            comfoair_mqtt_notify_state_changed();
        } else {
            // Not an error: the unit broadcasts PDOs this firmware does not publish. Worth
            // seeing at DEBUG because an unclaimed PDO is also how a table gap shows up.
            ESP_LOGD(TAG, "PDO %4u node %2u len %u unclaimed", pdo, node, (unsigned)len);
        }
        return;
    }

    if (comfoair::rmi::is_rmi_can_id(can_id)) {
        // The only consumer of RMI responses is the probe, and only while a walk the owner started
        // is running (comfoair_probe_task_on_frame() above). Commands stay fire-and-forget; this
        // DEBUG line is the one place every other RMI frame is seen.
        ESP_LOGD(TAG, "RMI id=%08" PRIx32 " len=%u %s", can_id, (unsigned)len,
                 comfoair::rmi::is_rmi_response(can_id) ? "response" : "request");
        return;
    }

    ESP_LOGD(TAG, "other id=%08" PRIx32 " len=%u", can_id, (unsigned)len);
}

// Where the device is reachable, said out loud once per transition.
//
// It PUSHES NOTHING into the HTTP layer. The access policy asks comfoair_net_is_provisioned() on
// every request instead: a pushed copy is a tick out of date, and "unclaimed" means an OPEN ACCESS
// POINT BEING ON THE AIR, which changes the instant the station is given an address. The version
// of this function that pushed comfoair_net_has_credentials() keyed it on stored credentials
// instead, which leaves a passer-by who has just supplied credentials able to set the first
// password (docs/device/networking.md).
static void on_net_state(comfoair_net_state_t state, void *)
{
    switch (state) {
    case COMFOAIR_NET_CONNECTED:
        ESP_LOGI(TAG, "on the owner's network");
        break;
    case COMFOAIR_NET_ACCESS_POINT:
        // Worth a line at WARN: an open access point is on the air and the device counts as
        // unclaimed for as long as it is, and that has to be observable.
        ESP_LOGW(TAG, "serving its own open access point; the device counts as unclaimed");
        break;
    case COMFOAIR_NET_DOWN:
        ESP_LOGW(TAG, "no network");
        break;
    }
}

extern "C" void app_main(void)
{
    // Installed first, so the boot lines a user needs when something goes wrong are in the
    // buffer the web UI reads -- not only on a USB cable they may not have attached.
    comfoair_log_install();

    const esp_app_desc_t *app = esp_app_get_description();
    ESP_LOGI(TAG, "%s %s", app->project_name, app->version);

    nvs_init();

    // Not only in a test's setUp. Without this seed_defaults() never runs on the device, so
    // every entity the table gives a default reads null for ever and schema_version -- the
    // one a client checks the contract against -- is null on exactly the device that needs
    // it. The tests passed throughout because Unity calls reset before each case.
    comfoair_state_reset();

    const board_t *b = board_get();
    ESP_LOGI(TAG, "board %s: CAN tx=%d rx=%d @ %" PRIu32 " bps, button=%d%s", b->name,
             b->can_tx, b->can_rx, b->can_bitrate, b->button,
             b->button_is_download_strap ? " (download strap: hold AFTER boot only)" : "");

    // BEFORE the bus and before the network, and the order is the point of the thing: everything
    // started after this line is allowed to fail, and the owner has to be able to SEE that it did.
    // The heartbeat starts here too, so "the firmware is running" stops depending on a USB cable.
    //
    // The picture is COMFOAIR_PHASE_BOOT until the phase monitor starts at the end of this function
    // and sends its first transition -- which is exactly where comfoair_phase.h:44-47 says BOOT is
    // observed, "the initial value comfoair_led holds from init until the first transition reaches
    // it". Everything between this line and that one is genuinely a device that is starting up, so
    // white SOLID there is the honest answer and not a placeholder.
    const esp_err_t led_err = comfoair_led_init(b);
    if (led_err != ESP_OK)
        // Logged and continued, the same rule as the bus below: nothing stops because a peer is
        // absent (AGENTS.md), and a device nobody can see still ventilates a house.
        ESP_LOGE(TAG, "indication did not start: %s -- continuing without it",
                 esp_err_to_name(led_err));

    // THE GESTURE COMES UP HERE, before the bus and before the network, and the position is the
    // argument. Everything below this line may fail; the button is the way back from a device on
    // which something did. The network
    // branch further down RETURNS on failure, so a button started after it would not exist on
    // exactly the device whose owner most needs to hold it for fifteen seconds.
    //
    // The one thing it cannot do this early is open the setup window: comfoair_net's queue does not
    // exist until comfoair_net_start(), and comfoair_net_open_setup_window() drops a press that
    // arrives before it. That costs the owner a second press a few hundred milliseconds later, and
    // it buys the factory reset being armed on a device whose network never came up.
    const esp_err_t button_err = comfoair_button_start(b, comfoair_net_open_setup_window);
    if (button_err != ESP_OK)
        // Logged and continued (AGENTS.md), but the line says what is actually lost
        // rather than naming an error code: with no poller there is no factory reset by button on
        // this boot, and an owner who has forgotten the UI password has to reach the device with a
        // USB cable.
        ESP_LOGE(TAG, "button did not start: %s -- no factory reset by button on this boot; "
                      "the only way back is USB",
                 esp_err_to_name(button_err));

    size_t total = 0;
    for (const web_asset_t *a = web_assets(); a->path; a++)
        total += a->len;
    ESP_LOGI(TAG, "web UI compiled in: %u B", (unsigned)total);

    // A bus that will not come up must not stop the device: without CAN the unit is
    // unreachable, but the web UI and provisioning still have to work so the user can
    // see the fault and reconfigure. Phases 4 and 5 build those; the rule starts here.
    // The bus comes up before the network on purpose: the unit is the reason this device
    // exists, and a router that is slow to answer must not delay reading it.
    const esp_err_t err = comfoair_can_start(b, on_can_frame, nullptr);
    if (err != ESP_OK) {
        // Says "continuing" and means it. An earlier version returned here, which
        // contradicted its own comment and would have stopped Wi-Fi and the web UI from
        // ever starting once phases 4 and 5 add them -- exactly when a user with a dead bus
        // most needs to reach the device to see why.
        ESP_LOGE(TAG, "CAN did not start: %s -- continuing without the bus",
                 esp_err_to_name(err));
        // And now it is more than a log line on a cable nobody attached: the phase aggregate turns
        // it into a colour and a word in /api/status (six of these seven branches were
        // invisible on a device with no USB attached).
        comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_CAN);
    } else {
        const comfoair_poll_config_t poll = COMFOAIR_POLL_DEFAULT_CONFIG();
        const esp_err_t poll_err = comfoair_poll_start(&poll);
        if (poll_err != ESP_OK) {
            ESP_LOGE(TAG, "PDO request cycle did not start: %s -- the unit still broadcasts",
                     esp_err_to_name(poll_err));
            comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_POLL);
        }

        // Commands. Only meaningful with a bus, which is why it is inside this branch: a device
        // whose CAN did not come up can still serve its web interface and say why, and offering
        // to control a unit it cannot reach would be a button that silently does nothing.
        const esp_err_t ctrl_err = comfoair_control_start();
        if (ctrl_err != ESP_OK) {
            ESP_LOGE(TAG, "command sender did not start: %s -- the unit is read-only",
                     esp_err_to_name(ctrl_err));
            comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_CONTROL);
        } else {
            // The four durations come out of NVS and go straight into the state model. No PDO
            // carries them, so this is the only thing that makes GET /api/state agree with what
            // the device will actually put in its next command -- without it the API reports the
            // registry's defaults for values the owner changed and the device is already using.
            comfoair_command_durations_t d;
            comfoair_control_durations(&d);
            comfoair_state_set_number("boost_duration", (float)d.boost_minutes);
            comfoair_state_set_number("bypass_duration", (float)d.bypass_minutes);
            comfoair_state_set_number("balance_duration", (float)d.balance_minutes);
            comfoair_state_set_number("away_duration", (float)d.away_minutes);

            // Before init: the task reads it unlocked. Here, not in the probe: comfoair_http
            // depends on comfoair_probe_task, so a direct call would be a cycle.
            comfoair_probe_task_set_observer(on_probe_state_changed);

            // After the sender, because the probe shares its sequence counter and speaks as its
            // node. A probe that will not start costs the diagnostic read, nothing else -- the
            // device carries on, and POST /api/probe answers that there is no bus to walk.
            const esp_err_t probe_err = comfoair_probe_task_init();
            if (probe_err != ESP_OK)
                ESP_LOGE(TAG, "RMI probe did not start: %s -- everything else is unaffected",
                         esp_err_to_name(probe_err));
        }
    }

    const esp_err_t net_err = comfoair_net_start(b, on_net_state, nullptr);
    if (net_err != ESP_OK) {
        // Same rule as the bus, in the other direction: no network means no web UI and no
        // Home Assistant, but the unit is still being read and the device must stay up so
        // the fault is visible over USB rather than as a silent brick.
        ESP_LOGE(TAG, "network did not start: %s", esp_err_to_name(net_err));
        // THE MONITOR IS STARTED ON THIS PATH TOO, and the return below is why it has to be. This
        // is the one branch of the seven that leaves app_main, so a device whose network never came
        // up would otherwise never reach the start call at the bottom and would sit on BOOT --
        // white, steady, "starting up" -- for as long as it is powered, which is the picture a
        // device gets when it is fine. It is not fine, and FAULT is a red the owner can see from
        // the doorway. Nothing reboots (AGENTS.md): the bus is still being read and
        // the fault is now visible without a cable.
        comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_NET);
        const esp_err_t phase_err = comfoair_phase_monitor_start();
        if (phase_err != ESP_OK)
            ESP_LOGE(TAG, "phase monitor did not start: %s -- the indication stays on BOOT",
                     esp_err_to_name(phase_err));
        return;
    }

    // BEFORE the web server, and the order is the whole reason this call exists separately from
    // comfoair_mqtt_start() below. POST /api/config ends in comfoair_mqtt_reconfigure(), which
    // posts its request into this component's queue -- so a save arriving between httpd coming up
    // and the broker being started needs that queue to already exist. It used to be created lazily
    // inside comfoair_mqtt_start(), several calls further down, and the window was reachable by
    // anyone quick with the settings page.
    const esp_err_t mqtt_init_err = comfoair_mqtt_init();
    if (mqtt_init_err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT did not initialise: %s -- the broker connection will not run",
                 esp_err_to_name(mqtt_init_err));
        // The QUEUE, and it is a different fault from the CLIENT below, on purpose: a device whose
        // queue exists and whose client did not build would otherwise
        // report NO_BROKER, "check your broker", where the true answer is "the firmware could not".
        comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_MQTT_QUEUE);
    }

    comfoair_http_config_t http = COMFOAIR_HTTP_DEFAULT_CONFIG();
    const esp_err_t http_err = comfoair_http_start(&http);
    if (http_err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server did not start: %s", esp_err_to_name(http_err));
        comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_HTTP);
    } else {
        // Only once the server is up: the probation clock confirms an image that can take the
        // NEXT update, and without HTTP this one cannot. An image whose server never starts is
        // therefore never confirmed and the next restart rolls it back -- deliberately.
        // Logged and ignored on failure: the device keeps working, it just stays on probation.
        const esp_err_t ota_err = comfoair_ota_runtime_start();
        if (ota_err != ESP_OK)
            ESP_LOGE(TAG, "OTA confirmation not armed: %s", esp_err_to_name(ota_err));
    }

    // Findable by name. Everything about the ordering, the facts and the failure rule is in
    // src/app_mdns.c, which exists because this function is at its 350-line ceiling and this was
    // the one self-contained seam in it: one set of inputs, one call, no state left behind.
    app_mdns_start(b, app, http_err == ESP_OK ? http.port : 0);

    // The broker, if one is configured. ESP_ERR_NOT_FOUND means none is, which is the state every
    // device ships in and not a fault -- nothing is dialled and nothing is warned about. It starts
    // before the network is necessarily up: esp-mqtt retries on its own, and waiting here would be
    // a boot that a missing router can stall.
    // A command arriving over MQTT changes the state model, and the browser should see it at once
    // rather than on the push channel's fifteen-second keep-alive. Wired here because
    // comfoair_http already depends on comfoair_mqtt, so the reverse call would be a cycle.
    comfoair_mqtt_set_observer(comfoair_http_notify_state_changed);

    const esp_err_t mqtt_err = comfoair_mqtt_start();
    if (mqtt_err != ESP_OK && mqtt_err != ESP_ERR_NOT_FOUND) {
        ESP_LOGE(TAG, "MQTT did not start: %s -- everything else is unaffected",
                 esp_err_to_name(mqtt_err));
        // ESP_ERR_NOT_FOUND IS EXCLUDED AND MUST BE: it means no broker is configured, which is the
        // state every device ships in, and calling it a fault would paint a brand-new unit red for
        // having nothing to connect to.
        comfoair_phase_monitor_note_fault(COMFOAIR_PHASE_FAULT_MQTT_CLIENT);
    }

    // LAST, and after every branch above has had its chance to report: the monitor's first tick
    // reads all seven verdicts at once, and a monitor started earlier would paint a device red for
    // a subsystem that had not been asked to start yet. comfoair_prov_init() has run inside
    // comfoair_net_start() by now, which is what lets the collector call machine_ready true and
    // never answer BOOT.
    //
    // From here the light stops being a placeholder: the phase reaches comfoair_led_set_phase()
    // once a second, the transition reaches the log once per transition, and GET /api/status grows
    // a `phase` field that says the same word.
    const esp_err_t phase_err = comfoair_phase_monitor_start();
    if (phase_err != ESP_OK)
        // Logged and continued like everything else here (AGENTS.md) -- and the device
        // is not left looking healthy for it: comfoair_phase_monitor_get() answers FAULT from a
        // monitor that never started, because one picture meaning both "starting up" and "the thing
        // that reports my state is dead" is a confusion no owner can act on.
        ESP_LOGE(TAG, "phase monitor did not start: %s -- the indication stays on BOOT and "
                      "/api/status reports fault",
                 esp_err_to_name(phase_err));
}
