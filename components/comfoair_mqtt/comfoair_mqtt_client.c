// The client's own life: the two functions that bring it into existence and take it out again,
// and the configuration buffers it is built from. The event handler moved to
// comfoair_mqtt_event.c on 2026-09-06 when this file reached the 350-line ceiling; the rule it
// exists to keep is unchanged and is stated in comfoair_mqtt_internal.h.
//
// See comfoair_mqtt_internal.h for the shape of this component and for the rule the handler next
// door exists to keep; see comfoair_mqtt.h for the rules that govern all nine files.
#include "comfoair_mqtt_internal.h"

#include <string.h>

#include "comfoair_device_url.h"
#include "comfoair_ha.h"
#include "comfoair_net.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

static const char *TAG = "mqtt";

// Declared in comfoair_mqtt_internal.h; the reasoning for each is there.
esp_mqtt_client_handle_t g_client;
volatile bool            g_connected;
// Defined here, with the rest of the connection's state, rather than beside the handler that
// writes it; comfoair_mqtt_internal.h says who may write it and where it is cleared.
_Atomic uint32_t g_last_failure;
// Set on connect: the next pass publishes the birth message, discovery and every state, because a
// broker that has just accepted us holds nothing of ours and a subscriber cannot ask for what it
// missed. The BIRTH is published from the task and not from the handler -- see the shape of this
// component in comfoair_mqtt_internal.h.
volatile bool g_republish;

comfoair_ha_ctx_t g_ctx;
// THE SIXTH STRING OF g_ctx AND THE ONLY ONE NOT PRIVATE TO THIS FILE -- which is why it wears g_
// and its five neighbours below wear s_ (comfoair_mqtt_internal.h). It is also the one field whose
// value is not known when the client is built: comfoair_mqtt_start_client() runs from app_main
// before the radio has associated. comfoair_mqtt_note_address(), in comfoair_mqtt_retry.c, is the
// second writer, and that is the CURE rather than an erosion of the rule -- both run on the
// publisher task and both lower the same discovery flag, so the broker cannot be left holding
// config documents that place this device at an address it no longer has.
char              g_url[COMFOAIR_DEVICE_URL_MAX];
bool              g_discovery;
// Whether a broker HOST is stored -- not whether a client was built from it. See the assignment in
// comfoair_mqtt_start_client() for why the two are not the same question.
bool              g_configured;
// The strings g_ctx points into. Private to this file because comfoair_mqtt_start_client() is the
// only thing that ever writes them; everybody else reads them through g_ctx. The sixth is g_url,
// declared in comfoair_mqtt_internal.h and defined below, and the prefix is the difference: it has
// a second writer.
static char s_device_id[COMFOAIR_CONFIG_DEVICE_ID_LEN + 1];
static char s_mac[18];
static char s_prefix[COMFOAIR_CONFIG_PREFIX_MAX + 1];
static char s_name[COMFOAIR_CONFIG_NAME_MAX + 1];
static char s_version[32];

// The client's own configuration strings. STILL STATIC, but the arithmetic changed on 2026-09-05
// and the old reason is gone: these used to be written from the HTTP task, on top of the 2 KB
// request body its 4096-byte stack was already carrying, and that was within a few hundred bytes
// of an overflow. Nothing but the publisher task comes here now. They stay static because that
// task's stack is 4096 bytes too and the whole set is 1091 bytes -- a quarter of it, spent on one
// call, where a stack overflow on ESP-IDF is a panic and a reboot.
//
// The figure is measured, not estimated, and re-measurable: sizeof(comfoair_net_broker_t) is 614,
// s_uri is COMFOAIR_CONFIG_HOST_MAX + 32 = 285, s_will_topic is 192. An earlier version of this
// comment said "about 1.35 KB", which was inherited rather than checked.
static comfoair_net_broker_t s_broker;
static char                  s_uri[COMFOAIR_CONFIG_HOST_MAX + 32];
static char                  s_will_topic[192];

// The BROKER the live client was built from, kept apart from s_uri although s_uri contains both.
// The comparison in comfoair_mqtt_pending_cleanup() has to be against the fields the owner edits;
// re-deriving them by parsing s_uri back apart would be a second format for one fact, and the
// parser would be the half nobody re-checks the day a scheme or a port default moves.
//
// 256 bytes of static, and no stack at all on the publisher task, which is the resource this file
// is careful with everywhere else. The figure is measured and re-measurable, like the one in the
// block above: s_host is COMFOAIR_CONFIG_HOST_MAX + 1 = 254 (comfoair_config.h:145), s_port is 2,
// and the padding to the next alignment boundary is 0 -- the two sit next to s_prefix and s_name,
// which are char arrays. It said "131 bytes" until 2026-09-07, which was a number carried over
// from a draft in which the host was capped at 128; the block above corrects an inherited figure
// of exactly the same kind, in the same file, and this one had been left standing beside it.
static char     s_host[COMFOAIR_CONFIG_HOST_MAX + 1];
static uint16_t s_port;

// What the settings just saved leave behind on the broker the LIVE client is still talking to, and
// therefore which cleanup has to run before that client is destroyed. Declared in
// comfoair_mqtt_internal.h, which states the task context.
//
// IT LIVES IN THIS FILE BECAUSE THIS FILE IS THE ONLY ONE THAT KNOWS BOTH IDENTITIES. The outgoing
// one is s_host/s_port/s_prefix, written by comfoair_mqtt_start_client() below; the incoming one is
// read here, from the same comfoair_net_broker() this file already calls, into the same static
// buffer, wiped on the same line. Doing it in comfoair_mqtt.c would put a 614-byte
// comfoair_net_broker_t -- password included -- on the publisher's 4096-byte stack for the length
// of the comparison, and would give the password a second home to be forgotten in.
//
// THREE ANSWERS, each a decision, under one rule: never empty a topic a live config still names.
//
//  * the broker host or port MOVED -> WITHDRAW. Everything we ever published stays on a broker we
//    are about to stop being allowed to talk to, and that is the ONE way an owner can produce a
//    genuine phantom device in Home Assistant: a tree of retained config documents that
//    recreate the device every time HA restarts, however often he deletes it. This is the last
//    moment at which anything can be done about it.
//  * only the PREFIX moved -> REKEY. The config topic carries no prefix, so the documents are
//    rewritten in place and the entities move across with their history; only the state tree is
//    orphaned. Erasing the configs here would be the destructive answer, not the thorough one.
//  * only the NAME moved, or nothing at all -> NOTHING, and this is not an optimisation. A blind
//    retraction would empty the config documents too, and Home Assistant removes an entity whose
//    config is emptied -- the owner would watch the device vanish and come back by pressing Save
//    twice, for a broker that keeps the same identity. The name lives
//    only inside the discovery document (comfoair_ha.c:277-289), which is rewritten anyway.
//
// It answers false the moment there is no live connection, so a device whose broker is
// unreachable neither waits nor retries (AGENTS.md). What cannot be cleaned then
// is the honest limit of a broker move: it belongs in the save dialog, not in a retry loop.
bool comfoair_mqtt_pending_cleanup(comfoair_ha_cleanup_t *what)
{
    if (what == NULL || g_client == NULL || !g_connected)
        return false;

    comfoair_net_broker(&s_broker);
    const bool moved   = strcmp(s_broker.host, s_host) != 0 || s_broker.port != s_port;
    const bool rekeyed = strcmp(s_broker.topic_prefix, s_prefix) != 0;
    // Immediately, and before either branch below can return: the struct carries the password in
    // the clear, and this file's rule is that it lives for exactly as long as it is being read.
    explicit_bzero(&s_broker, sizeof s_broker);

    if (moved) {
        *what = COMFOAIR_HA_CLEANUP_WITHDRAW;
        return true;
    }
    if (rekeyed) {
        *what = COMFOAIR_HA_CLEANUP_REKEY;
        return true;
    }
    return false;
}

// ON THE PUBLISHER TASK, so nobody can be inside publish() -- in comfoair_mqtt_publish.c -- at
// this moment; both belong to the one task, which is what the lock that used to be here attempted
// and could not deliver. It could not, because a lock held around esp_mqtt_client_stop() waits for
// STOPPED_BIT with portMAX_DELAY (mqtt_client.c:1961), and that bit never rises while the esp-mqtt
// task is itself waiting for our lock from inside its event dispatch. The other two callers are
// the failure path of comfoair_mqtt_start_client() below and comfoair_mqtt_start()'s, in
// comfoair_mqtt.c, when the publisher task could not be created at all; both of those run on
// app_main, before the publisher task exists, so the one-task property holds on all three.
void comfoair_mqtt_stop_client(void)
{
    if (g_client == NULL)
        return;
    // OFFLINE, EXPLICITLY, before a clean stop. esp_mqtt_client_stop() sends a proper DISCONNECT
    // (mqtt_client.c:1953-1954), and a broker does NOT publish the will for one -- so without this
    // a device whose broker was cleared, or whose prefix changed, leaves `online` retained for ever
    // and Home Assistant shows a live device that is gone.
    if (g_connected
        && comfoair_ha_meta_topic(&g_ctx, COMFOAIR_HA_META_STATUS, s_will_topic, sizeof s_will_topic)
               > 0)
        esp_mqtt_client_publish(g_client, s_will_topic, COMFOAIR_HA_PAYLOAD_OFFLINE, 0, QOS_META, 1);

    esp_mqtt_client_stop(g_client);
    esp_mqtt_client_destroy(g_client);
    g_client = NULL;
    // The refusal belonged to THIS client, so it dies with it. Otherwise an owner who mistyped a
    // password (code 5), then pointed the device at a broker that is merely switched off, keeps
    // being told "not authorized" about a broker that was never contacted -- the same wrong-cause
    // defect the code was added to remove, displaced by one step.
    //
    // AFTER esp_mqtt_client_stop() RETURNS, AND THE ORDER IS THE WHOLE POINT. DO NOT move these
    // three clears up above the stop -- they were there and they were wrong. The handler stopped
    // taking this component's mutex on 2026-09-05 and the mutex itself is now gone, so nothing
    // stops the esp-mqtt task writing g_last_connack concurrently: stop() blocks on the API lock
    // at entry (mqtt_client.c:1943) while the task holds that same lock across its dispatch
    // (mqtt_client.c:1661-1662), and even after `run = false` (:1958) the task finishes its
    // iteration before raising STOPPED_BIT, which is what stop() waits for (:1961). So a refusal
    // event can be dispatched after an earlier clear and outlive the client -- and during a
    // refusal-retry storm, which is exactly when an owner reconfigures, it does so by construction
    // rather than by luck. Only once stop() has returned is the task gone and this store final.
    atomic_store_explicit(&g_last_connack, 0, memory_order_relaxed);
    // The same clear and the same argument for the socket errno of the last transport failure: it
    // described a client that no longer exists, and left standing it would sit in /api/status
    // beside a healthy successor for ever -- an owner who mistyped a host (ECONNREFUSED) and then
    // corrected it would be told 111 about a broker he is talking to right now. Hardware scenario
    // HV-MQTT-24 (docs/hardware-verification/06-mqtt.md) reads 0 after a save; this makes it so.
    atomic_store_explicit(&g_last_transport_errno, 0, memory_order_relaxed);
    // The same clear, for the same reason, on the word the ladder reads: the event handler writes
    // it too, and a failure belonging to a destroyed client would choose its successor's first
    // rung. THIS IS THE HALF THAT BELONGS HERE -- the ladder's OWN state does not, because it is
    // the publisher task's and two of this function's three callers run on app_main before that
    // task exists.
    atomic_store_explicit(&g_last_failure, 0, memory_order_relaxed);
    // The same reasoning, for the two flags the handler also writes: a CONNECTED event dispatched
    // inside the stop window sets both for a client that is already being destroyed, and the next
    // client would then inherit a "connected" it never earned and a republish that belongs to its
    // predecessor. After stop() has returned there is no esp-mqtt task left to contradict this.
    g_connected = false;
    g_republish = false;
}

// Builds the client and rewrites g_ctx from the stored settings. Called from the publisher task,
// and once from app_main before that task exists -- never from anywhere else, because it writes
// every buffer publish() in comfoair_mqtt_publish.c and comfoair_mqtt_handle_inbound() read.
esp_err_t comfoair_mqtt_start_client(void)
{
    if (g_client != NULL)
        return ESP_ERR_INVALID_STATE;

    // The one projection that is not redacted; nothing in it reaches a log line (comfoair_net.h).
    // Static rather than a local: see the declaration above for the stack arithmetic.
    comfoair_net_broker(&s_broker);
    // RECORDED SEPARATELY FROM g_client, and the difference is the whole point of the field. "A
    // host is stored" and "a client exists" diverge exactly when esp_mqtt_client_init() or
    // _start() fails below: the owner HAS configured a broker, and reporting configured=false
    // there tells them the opposite -- the same wrong-cause reporting that finding B4 exists to
    // remove. DO NOT go back to deriving this from g_client.
    g_configured = s_broker.host[0] != '\0';
    if (!g_configured) {
        // Not an error and not a fault: it is the state every device ships in.
        explicit_bzero(&s_broker, sizeof s_broker);
        ESP_LOGI(TAG, "no broker configured; MQTT stays off");
        return ESP_ERR_NOT_FOUND;
    }

    comfoair_net_device_id(s_device_id);
    comfoair_net_mac_string(s_mac);
    char ip[16];
    comfoair_net_ip_string(ip);
    // CLEARED FIRST, AND THE REFRESH CANNOT DO IT FOR US: it never blanks a url (its header says
    // why), and a REBUILD is the other case -- an address left over from a previous client must not
    // survive into this one. Together the two lines do what the single snprintf here used to,
    // ABSENT url included: an unknown address must not become a `cu` with no scheme, which
    // cv.configuration_url rejects, taking the device block and every entity with it.
    g_url[0] = '\0';
    comfoair_device_url_refresh(g_url, sizeof g_url, ip);
    snprintf(s_prefix, sizeof s_prefix, "%s", s_broker.topic_prefix);
    snprintf(s_name, sizeof s_name, "%s", s_broker.device_name);
    // The version the bootloader carries, so there is no second place for it to drift from. It
    // used to be a hardcoded literal here and the comment claimed otherwise.
    const esp_app_desc_t *app = esp_app_get_description();
    snprintf(s_version, sizeof s_version, "%s", app != NULL ? app->version : "unknown");
    g_discovery = s_broker.ha_discovery;

    g_ctx.device_id    = s_device_id;
    g_ctx.device_name  = s_name;
    g_ctx.topic_prefix = s_prefix;
    g_ctx.sw_version   = s_version;
    g_ctx.mac          = s_mac;
    g_ctx.url          = g_url;
    // RULE TWO OF THE DISCOVERY GATE, AND IT BELONGS ON THIS LINE RATHER THAN
    // ANYWHERE ELSE IN THIS FUNCTION. The six assignments above are the ONLY place in the firmware
    // that fills comfoair_ha_ctx_t, and a discovery document is built from that struct and a
    // registry row and from nothing else -- so those six lines are exactly the event "the config
    // documents on the broker no longer describe this device", and the forget sits with them.
    //
    // THIS IS WHY THERE IS NO FNV FINGERPRINT OF THE SIX FIELDS, which is the smarter-looking
    // design and the one to argue with: a hash here would compute what the assignment above
    // already announces. WORSE, IT WOULD BE WRONG: the broker's ADDRESS is not one of the six --
    // s_broker.host and s_port go to s_host/s_port below and never into g_ctx -- so an owner
    // moving to a different broker with the same prefix would get a MATCHING hash and an EMPTY new
    // broker. States published, not one config document, nothing in Home Assistant, and no way out
    // but a reboot. comfoair_discovery_gate.h carries the full argument. DO NOT replace this with
    // a hash.
    //
    // The no-broker path above returns before this and needs nothing: it writes no g_ctx and
    // builds no client, and the next start does both.
    comfoair_mqtt_discovery_rebuilt();

    snprintf(s_uri, sizeof s_uri, "mqtt://%s:%u", s_broker.host, (unsigned)s_broker.port);
    // The identity of the broker itself, for the cleanup decision above. Written HERE and nowhere
    // else, with the rest of g_ctx, so "what the live client was built from" has exactly one
    // definition.
    snprintf(s_host, sizeof s_host, "%s", s_broker.host);
    s_port = s_broker.port;
    if (comfoair_ha_meta_topic(&g_ctx, COMFOAIR_HA_META_STATUS, s_will_topic, sizeof s_will_topic)
        == 0) {
        explicit_bzero(&s_broker, sizeof s_broker);
        return ESP_ERR_INVALID_ARG;
    }

    esp_mqtt_client_config_t cfg = {0};
    cfg.broker.address.uri       = s_uri;
    // The client id is the FULL MAC. Two devices sharing one would throw each other off the
    // broker for ever, and the access point's name carries only sixteen bits of it.
    cfg.credentials.client_id = s_device_id;
    if (s_broker.user[0] != '\0') {
        cfg.credentials.username                = s_broker.user;
        cfg.credentials.authentication.password = s_broker.password;
    }
    cfg.session.last_will.topic  = s_will_topic;
    cfg.session.last_will.msg    = COMFOAIR_HA_PAYLOAD_OFFLINE;
    cfg.session.last_will.qos    = QOS_META;
    cfg.session.last_will.retain = 1;
    cfg.buffer.size              = BUFFER_SIZE;
    cfg.buffer.out_size          = BUFFER_SIZE;
    // SECTION 5.1: the timing stops being esp-mqtt's and becomes the publisher task's. Switched
    // off here is a fixed ten seconds for ever, with no ceiling, no jitter and the same ten seconds
    // for "the password is wrong" as for "the broker is off for the weekend"; comfoair_mqtt_retry.c
    // replaces it. Left true, both would run and the two timers would race.
    //
    // DO NOT also set network.reconnect_timeout_ms. It changes NOTHING about our timing: with
    // auto-reconnect off the client parks in MQTT_STATE_WAIT_RECONNECT and waits on RECONNECT_BIT
    // itself (mqtt_client.c:1830-1831), returning the instant the bit is set, and the timeout only
    // sets how often it wakes to find nothing. Its default is written back over any value we set,
    // on every application-initiated reconnect (:1819), so the number would not even survive.
    cfg.network.disable_auto_reconnect = true;
    // EXPLICIT, and the reason is the RETRACTION and not the dial. esp-mqtt uses this one number
    // for three things: the socket connect, the wait for CONNACK, and -- the one that matters --
    // every socket WRITE, because a QoS 1 esp_mqtt_client_publish() writes synchronously
    // (esp_mqtt_write -> esp_transport_write(..., network_timeout_ms), mqtt_client.c:730-733).
    // Left unset it defaults to ten seconds (:536-537), so a single publish into a broker that
    // has stopped reading its socket parked the publisher task for ten seconds and the two-second
    // retraction ceiling in comfoair_mqtt_publish.c could not be honoured at all. No peer may stall
    // the device (AGENTS.md), so that is unacceptable rather than merely slow: for
    // those ten seconds no command from the broker is handled.
    //
    // 2000 AND NOT 1000. It also bounds name resolution plus the TCP connect, and an owner whose
    // broker is `homeassistant.local` resolves it by mDNS -- a query that routinely takes several
    // hundred milliseconds on a busy 2.4 GHz network. One second would start refusing brokers
    // that work. A dial therefore costs at most 2 x 2 s instead of 2 x 10 s, which is a change
    // the ladder absorbs (it measures from the end of an attempt) and which hardware scenario
    // HV-MQTT-18 (docs/hardware-verification/06-mqtt.md) checks.
    cfg.network.timeout_ms = 2000;

    g_client = esp_mqtt_client_init(&cfg);
    // esp_mqtt_set_config strdup()s the uri, username, password, client id and will topic
    // (mqtt_client.c:463-490), so nothing above has to outlive this call -- and the copy of the
    // password here can go now.
    explicit_bzero(&s_broker, sizeof s_broker);
    if (g_client == NULL) {
        ESP_LOGE(TAG, "client did not initialise");
        return ESP_ERR_NO_MEM;
    }
    esp_mqtt_client_register_event(g_client, ESP_EVENT_ANY_ID, comfoair_mqtt_on_event, NULL);

    const esp_err_t err = esp_mqtt_client_start(g_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "client did not start: %s", esp_err_to_name(err));
        // The line below this one in the log will be esp-mqtt's own "Client asked to stop, but was
        // not started" (mqtt_client.c:1964). It is expected, not a second fault:
        // comfoair_mqtt_stop_client() is how the half-built client is destroyed and its buffers
        // released, and there is no separate teardown that skips the stop.
        comfoair_mqtt_stop_client();
        return err;
    }

    // The HOST, never the credentials.
    ESP_LOGI(TAG, "broker %s, prefix '%s', discovery %s", s_uri, s_prefix,
             g_discovery ? "on" : "off");
    return ESP_OK;
}
