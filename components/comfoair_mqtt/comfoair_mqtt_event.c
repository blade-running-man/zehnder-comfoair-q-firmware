// The esp-mqtt event handler, and nothing else.
//
// A FILE OF ITS OWN AS OF 2026-09-06, split off comfoair_mqtt_client.c along the seam that file's
// own header comment already drew -- the handler on one side, building and destroying the client
// on the other -- because it reached the 350-line ceiling (AGENTS.md). Every split in this
// component has had that one cause and no behaviour has ever moved with one.
//
// It is also the seam with the sharpest rule, which is the second reason to give it a file: what
// is below runs on somebody else's task, and what is next door runs on the publisher's.
#include "comfoair_mqtt_internal.h"

#include <string.h>

#include "comfoair_mqtt_inbound.h"
#include "comfoair_wire.h"
#include "esp_log.h"
// For ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME alone, which reaches a plain-TCP client through
// esp_tls_last_esp_err. Already on the path: mqtt requires tcp_transport, which requires esp-tls.
#include "esp_tls_errors.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"

// The one tag for all of comfoair_mqtt; the reasoning is in comfoair_mqtt_internal.h.
static const char *TAG = "mqtt";

// SETS FLAGS AND COPIES BYTES. Nothing else, ever. An atomic is not an exception to that rule but
// the way to keep it: the 2026-09-05 counter-review found a `lock()` here taken for a single
// `reconnects++`, one side of the deadlock described where the counters are defined, in
// comfoair_mqtt.c.
//
// It runs on the esp-mqtt task, synchronously inside that client's API lock, while the publisher
// task may be blocked in esp_mqtt_client_publish. Anything it touches that the publisher also
// touches is mutated under a caller that is waiting -- which is how a temperature reading came to
// be published, retained, to <prefix>/status.
void comfoair_mqtt_on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    const esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)data;

    switch ((esp_mqtt_event_id_t)id) {
    // A DIAL IS STARTING. Dispatched at mqtt_client.c:1702-1703, immediately before
    // esp_transport_connect(), which is what makes it the right place to say "nothing has failed
    // in the attempt that is now running".
    //
    // WITHOUT THIS BRANCH THE LADDER ARMS A RUNG OVER A LIVE ATTEMPT, and only the FIRST dial of a
    // boot is safe: the word survives from the previous failure, so from the second attempt
    // onwards the publisher sees "something failed" throughout the dial, arms a rung, and reports
    // link "backoff" with a countdown while esp-mqtt is still inside a connect that can last two
    // network timeouts -- the opposite of the status contract, under which a dial in progress
    // reads link "connecting" with retry_in_ms null. It also costs the log: the expiring rung
    // calls esp_mqtt_client_reconnect() once a second, and that function logs "Client force
    // reconnect requested" BEFORE it checks the state (mqtt_client.c:1901), so a twenty-second
    // dial wrote twenty lines into an eighty-line ring.
    //
    // The rung is now measured from the FAILURE, which is what the ladder's rows mean by a
    // delay between attempts.
    case MQTT_EVENT_BEFORE_CONNECT:
        atomic_store_explicit(&g_last_failure, 0, memory_order_relaxed);
        break;

    case MQTT_EVENT_CONNECTED:
        g_connected = true;
        g_republish = true;
        bump(&g_reconnects);
        atomic_store_explicit(&g_last_connack, 0, memory_order_relaxed);
        // Nothing has failed, and saying so is not cosmetic: comfoair_mqtt_classify() answers
        // DROPPED for any signal whose was_connected is set, so a word left over from the last
        // outage would make a device that is publishing right now report a failure class.
        atomic_store_explicit(&g_last_failure, 0, memory_order_relaxed);
        break;

    case MQTT_EVENT_DISCONNECTED:
        // FAILED unconditionally: this event is the ONLY one a timed-out esp_mqtt_connect
        // produces, and the ladder must climb for it too. was_connected is g_connected AS IT IS
        // AT THIS INSTANT, which is the whole definition -- "the connection that just failed had
        // been established" -- and it is the only thing telling the ladder a dropped session from
        // a dial that never got through: a broker that has accepted us is retried after one
        // second, one we never reached after two. The rest of the word is left alone: an error
        // event arrives BEFORE this one and has already described the failure. The
        // read-modify-write needs no atomicity, and NOT because this task is the only writer --
        // comfoair_mqtt_stop_client() writes it too. It is safe for that function's own reason,
        // recorded beside its clear of g_last_connack: the store there happens after
        // esp_mqtt_client_stop() has returned, when this task no longer exists.
        atomic_store_explicit(&g_last_failure,
                              atomic_load_explicit(&g_last_failure, memory_order_relaxed) |
                                  COMFOAIR_MQTT_FAILWORD_FAILED |
                                  (g_connected ? COMFOAIR_MQTT_FAILWORD_WAS_CONNECTED : 0u),
                              memory_order_relaxed);
        g_connected = false;
        // THE THIRD FACT THIS HANDLER USED TO LOSE, COUNTED HERE UNCONDITIONALLY. Until 2026-09-07
        // the only counter this handler moved was g_reconnects, on MQTT_EVENT_CONNECTED, so ten
        // hours of hammering an unreachable broker changed GET /api/status by NOTHING AT ALL and
        // an owner who read the document twice half a day apart could not tell "we are trying and
        // failing" from "the task is dead". The bump goes on the failed dial as well as on the
        // dropped session, because this event is the only one a timed-out esp_mqtt_connect
        // produces, and ten hours of an unreachable broker must move exactly that number.
        //
        // THE PAIR WITH g_reconnects IS THE POINT, not the count on its own: the two climbing
        // together while `connected` flickers is the ONLY observation of "a second device is using
        // our client id" that reaches this device, because the broker sends an ordinary
        // disconnect and says nothing else. DO NOT make this conditional on
        // g_connected to count "real" drops only -- that is the half that would delete the ten
        // hours again, and the pair would stop being comparable.
        bump(&g_disconnects);
        // A log line and nothing else. No reboot, no escalation: a household whose broker is off
        // for a weekend keeps its ventilation, its web interface and its CAN polling (AGENTS.md).
        ESP_LOGW(TAG, "broker unreachable; retrying, and nothing else changes");
        break;

    case MQTT_EVENT_DATA: {
        if (g_inbox == NULL)
            break;
        // A fragmented payload arrives as several events and this handles none of them. Refused
        // rather than applied in halves: a truncated command is a different command.
        if (event->total_data_len != event->data_len || (size_t)event->data_len >= COMFOAIR_MQTT_PAYLOAD_MAX ||
            (size_t)event->topic_len >= sizeof ((comfoair_mqtt_inbound_t *)0)->topic) {
            bump(&g_rejected);
            break;
        }
        comfoair_mqtt_inbound_t in = {0};
        memcpy(in.topic, event->topic, (size_t)event->topic_len);
        in.topic_len = (size_t)event->topic_len;
        memcpy(in.payload, event->data, (size_t)event->data_len);
        in.retained = event->retain != 0;
        // NEVER BLOCKS: a wait here stalls the client's own receive loop. A full inbox drops the
        // command, which is the honest answer -- applying it a minute late is worse.
        if (xQueueSend(g_inbox, &in, 0) != pdTRUE)
            bump(&g_rejected);
        else if (g_task != NULL)
            xTaskNotifyGive(g_task);
        break;
    }

    case MQTT_EVENT_ERROR:
        if (event->error_handle == NULL)
            break;
        // A refusal in CONNACK is NOT an unreachable broker, and the owner has to read something
        // different. esp-mqtt dispatches DISCONNECTED after a refusal anyway, and that branch's
        // line above says "retrying" -- so without this branch a mistyped password looks exactly
        // like a switched-off broker, forever.
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            const uint32_t code = (uint32_t)event->error_handle->connect_return_code;
            atomic_store_explicit(&g_last_connack, code, memory_order_relaxed);
            atomic_store_explicit(&g_last_failure,
                                  comfoair_mqtt_failword_pack(COMFOAIR_MQTT_ERR_REFUSED, code,
                                                              false, g_connected),
                                  memory_order_relaxed);
            // The WORDS come from comfoair_wire, which owns one spelling of each code, so this
            // line and the status document cannot disagree. Naming the code matters: only 4 and 5
            // are about credentials, and a single "bad credentials?" for all of them would send
            // an owner hunting a password over a broker that is merely unavailable.
            ESP_LOGE(TAG, "broker refused the connection: %s (code %d)",
                     comfoair_wire_connack_name(code), (int)code);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            // UNTIL 2026-09-06 THIS WAS THROWN AWAY, and the cost was this:
            // esp_mqtt_client_dispatch_transport_error() (mqtt_client.c:2359-2375) has filled
            // esp_tls_last_esp_err by the time we get here, and dropping it left
            // comfoair_mqtt_classify() reading a signal that says nothing ever failed -- so a
            // broker that cannot be reached would climb no ladder at all.
            //
            // The DNS split buys one word in /api/status and nothing else -- both classes take the
            // same row -- so if the flag never arrives on the plain-TCP path (it sits behind
            // MQTT_SUPPORTED_FEATURE_TRANSPORT_ERR_REPORTING, unverified) the text is less precise
            // and the timing identical. No log line: the DISCONNECTED branch already writes one.
            const bool unresolved = event->error_handle->esp_tls_last_esp_err ==
                                    ESP_ERR_ESP_TLS_CANNOT_RESOLVE_HOSTNAME;
            // THE SOCKET ERRNO, WHICH WENT THE SAME WAY AS THE CLASS DID UNTIL 2026-09-07. It is
            // already filled by the time we get here (mqtt_client.c:2373, under
            // MQTT_SUPPORTED_FEATURE_TRANSPORT_SOCK_ERRNO_REPORTING, which is defined for every
            // IDF >= 4.3 and so unconditionally on the 5.5.5 tree this project pins), and it is
            // the ONE thing that separates the three failures the class `tcp` collapses:
            // ECONNREFUSED is a broker that is switched off, ETIMEDOUT is the wrong address or a
            // firewall, EHOSTUNREACH is no route. Three different things for the owner to go and
            // do.
            //
            // NOT A CAST: comfoair_mqtt_transport_errno() is next door because esp_transport_get_errno()
            // answers -1 when there is no error handle at all, and a cast would print
            // 4294967295 in the field HV-MQTT-24 tells the owner to read as 111 or 118.
            atomic_store_explicit(
                &g_last_transport_errno,
                comfoair_mqtt_transport_errno(event->error_handle->esp_transport_sock_errno),
                memory_order_relaxed);
            atomic_store_explicit(&g_last_failure,
                                  comfoair_mqtt_failword_pack(COMFOAIR_MQTT_ERR_TRANSPORT, 0,
                                                              unresolved, g_connected),
                                  memory_order_relaxed);
        }
        // MQTT_ERROR_TYPE_SUBSCRIBE_FAILED falls through untouched ON PURPOSE: a refused
        // subscription is not a failure to connect -- the session is up and publishing works -- so
        // writing it here would put a live broker on the retry ladder.
        break;

    default:
        break;
    }
}
