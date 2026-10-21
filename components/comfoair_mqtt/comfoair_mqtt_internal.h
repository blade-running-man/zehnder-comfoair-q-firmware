// The contract between the files of comfoair_mqtt, and it is deliberately NOT in include/: nothing
// outside this component may learn that the component has an inside. comfoair_mqtt.h is the whole
// of what the rest of the firmware may see.
//
// EVERY CALL INTO esp-mqtt AND EVERY BUFFER BELONGS TO THE PUBLISHER TASK; the event handler only
// sets flags and queues bytes. Why (a counter-review finding: esp-mqtt dispatches events from inside
// its own API lock), which file does what, and why the g_ prefix means "shared by at least two of
// them": components/comfoair_mqtt/README.md, "The files and the one-task rule". ONE LOG TAG, "mqtt".
#pragma once

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "comfoair_device_url.h"
#include "comfoair_ha.h"
#include "comfoair_mqtt.h"
#include "comfoair_mqtt_inbound.h"
#include "comfoair_mqtt_ladder.h"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mqtt_client.h"

// Both directions.
//
// 1536 rather than 1024, and the number came from counter-review: the largest payload is the fan's
// and it grows with the topic prefix and the device name. At the prefix's 64-character maximum with
// a 32-character non-ASCII name -- every one of which comfoair_json_escape expands to six bytes --
// it reaches 1040. At 1024 the fan would have silently never appeared, with one log line to say so.
// test_ha now builds that worst case.
//
// Shared because it sizes two unrelated things that must agree: the payload buffer in
// comfoair_mqtt_publish.c and esp-mqtt's own transport buffers in comfoair_mqtt_client.c.
#define BUFFER_SIZE 1536

// QoS 0 for state, and it is a decision rather than a default: every state topic is RETAINED, so a
// subscriber that misses one gets the current value the moment it subscribes -- what QoS 1 would
// buy, without an acknowledgement round trip on every state topic.
#define QOS_STATE 0
// How many messages one burst sends before it yields. A full sweep of the unit's PDOs marks dozens
// of entities at once, so the path is limited to eight per burst, and this task must not stop
// looking at its queue for the length of a full retraction (about two messages per entity) either.
//
// SHARED: the changed-state pass, the discovery burst, the full republish and
// the retraction all pace by it, and the last of those is in a file of its own now. One number,
// because "eight" is one decision -- three copies of it would drift the first time somebody tuned
// the burst they happened to be looking at.
#define PUBLISH_PER_PASS 8

// QoS 1 for the two that retention does not make idempotent: availability, because a missed
// "offline" leaves Home Assistant showing a device that is gone, and discovery, because a missed
// config message is an entity that never appears until the next connect.
//
// The pair lives here and not with the publisher because the second one is also the will's QoS and
// the QoS of the explicit "offline" in comfoair_mqtt_stop_client(), both in comfoair_mqtt_client.c.
// They are one argument and are kept in one place; QOS_STATE alone would read as an unexplained
// default.
#define QOS_META 1

// --- the client, owned by comfoair_mqtt_client.c ----------------------------------------------

// g_client belongs to the publisher task: created, used and destroyed there. The one exception is
// the first comfoair_mqtt_start(), on app_main, BEFORE that task exists -- see the ordering there,
// in comfoair_mqtt.c. Read outside that task in exactly one place, comfoair_mqtt_get_stats(),
// where it is compared against NULL and never dereferenced.
extern esp_mqtt_client_handle_t g_client;

// Both are read everywhere as the answer to "is there any point in sending", but they are written
// by different sets of tasks and the difference matters.
//
// g_connected: written ONLY by the esp-mqtt event handler and by comfoair_mqtt_stop_client(), both
// in comfoair_mqtt_client.c. Nobody else may write it.
extern volatile bool g_connected;
// g_republish: SET only by the esp-mqtt event handler, on connect. CLEARED by two others --
// comfoair_mqtt_stop_client(), because a republish owed to a destroyed client is owed to nobody,
// and mqtt_task in comfoair_mqtt.c at the moment it consumes the request, because a republish is a
// job handed to the publisher and the publisher is what marks it done.
extern volatile bool g_republish;

// The rendering context every topic and payload is built from. Written ONLY by
// comfoair_mqtt_start_client(), on the publisher task; read by the publisher, by the decider's
// caller, and by the offline publish in comfoair_mqtt_stop_client(). A reader on another task
// would race a reconfigure.
extern comfoair_ha_ctx_t g_ctx;
// The buffer g_ctx.url points into, and the ONE string of that struct with two writers -- which is
// why it is here and its five neighbours are statics in comfoair_mqtt_client.c. Both writers are
// on the publisher task: comfoair_mqtt_start_client(), with the rest of g_ctx, and
// comfoair_mqtt_note_address() below. The reasoning for the second one is at its definition.
extern char              g_url[COMFOAIR_DEVICE_URL_MAX];
// Whether the owner asked for Home Assistant discovery. Set with g_ctx, read by publish_discovery.
extern bool g_discovery;
// Whether the stored settings name a broker host. Read by comfoair_mqtt_get_stats() from any task
// as a best-effort snapshot; written only by comfoair_mqtt_start_client() on the publisher task.
extern bool g_configured;

// --- the spine, owned by comfoair_mqtt.c ------------------------------------------------------

// The publisher task and its inbound queue. Both are created in comfoair_mqtt.c and both are read
// by the event handler, which may only notify and enqueue -- never wait on either.
extern TaskHandle_t  g_task;
extern QueueHandle_t g_inbox;

// Something in the state model moved and has not been published yet. Set from any task, cleared by
// the publisher before its pass.
extern volatile bool g_dirty;

// The counters GET /api/status reports. The reasoning for atomics rather than a lock is at their
// definition in comfoair_mqtt.c and it is the only statement of it; every place that had to know
// points there.
extern _Atomic uint32_t g_published;
extern _Atomic uint32_t g_rejected;
extern _Atomic uint32_t g_reconnects;
extern _Atomic uint32_t g_last_connack;
// Two of the facts the handler used to lose. g_disconnects is the PAIR of g_reconnects
// and is written on every MQTT_EVENT_DISCONNECTED, a failed dial included; g_last_transport_errno
// is written by the MQTT_EVENT_ERROR branch and cleared with g_last_connack in
// comfoair_mqtt_stop_client(). Both are defined beside g_reconnects in comfoair_mqtt.c, where the
// reasoning for each is written out.
extern _Atomic uint32_t g_disconnects;
extern _Atomic uint32_t g_last_transport_errno;
// Inbound commands accepted: moved by comfoair_mqtt_command.c, reported by
// comfoair_mqtt_get_stats().
extern _Atomic uint32_t g_commands;

// The callback comfoair_mqtt_set_observer() registers. Written once by app_main before the
// publisher task exists; called only by comfoair_mqtt_handle_inbound(), on the publisher task.
extern comfoair_mqtt_observer_t g_observer;

// The last failure the event handler saw, packed by comfoair_mqtt_failword_pack() -- the layout,
// and the argument for one word rather than four atomics, are in comfoair_mqtt_ladder.h, where a
// host test can reach them.
//
// WRITTEN BY THE EVENT HANDLER, and cleared in three places that are all its own side of the
// house: MQTT_EVENT_CONNECTED, MQTT_EVENT_BEFORE_CONNECT and comfoair_mqtt_stop_client(). Read by
// the ladder in comfoair_mqtt_retry.c and by nothing else.
//
// NOT a second spelling of g_last_connack, although both carry the code. g_last_connack is a LATCH
// the owner reads: it survives until a successful connection, so that "the broker refused you"
// does not vanish from /api/status the moment a later attempt fails at the transport instead. This
// word describes the attempt that is happening NOW, is empty while one is in flight, and is what
// the ladder must climb.
extern _Atomic uint32_t g_last_failure;

// Relaxed, for the reason given at those definitions: no invariant holds between the counters and
// the whole requirement is that no increment is lost.
static inline void bump(_Atomic uint32_t *counter)
{
    atomic_fetch_add_explicit(counter, 1, memory_order_relaxed);
}

// The two buffers every outgoing message is formatted in. g_topic is 192 bytes because that is
// what test_no_retraction_topic_truncates_at_the_longest_prefix_the_config_allows measures the
// worst case against; g_payload is BUFFER_SIZE for the reason given at that macro above.
//
// BOTH ARE DEFINED IN comfoair_mqtt_publish.c and shared with comfoair_mqtt_retract.c and
// comfoair_mqtt_discovery.c, which is safe for exactly one reason and would be a data race without
// it: all three files run only on the publisher task, and neither the retraction nor the discovery
// sweep is nested inside a publish pass -- the retraction runs in the reconfigure branch and the
// sweep runs from comfoair_mqtt_republish_everything(), which is the whole of its pass. DO NOT
// format into either from the event handler or from any other task: a buffer two tasks share is
// precisely the defect this component was restructured to remove, and it cost a temperature
// reading published, retained, to <prefix>/status.
extern char g_topic[192];
extern char g_payload[BUFFER_SIZE];

// --- what one file may ask another to do ------------------------------------------------------
//
// EVERY ONE OF THEM IS THE PUBLISHER TASK'S -- and the count this sentence used to open with is
// gone rather than corrected, because it said FOUR while the list below held twelve and a number
// in prose beside a list is a number that will be wrong again. That is the whole invariant this
// component was restructured to establish: the client has exactly one owner, so no call into
// esp-mqtt can ever be made from a task that another one is waiting for. This header is the only
// place they are visible, and it is not reachable from outside the component -- but a file inside
// it could still call one from the wrong context, so each says where it may be called from. DO NOT
// widen this list to make some other task's life easier; post a flag and wake the publisher, which
// is what comfoair_mqtt_reconfigure() does and why it exists.

// One message, and the ONLY door to esp_mqtt_client_publish() in this component. False when there
// is no connection and false when the client refused it -- and a true is the result of QUEUEING,
// never an acknowledgement, so it does not mean the broker has the message. TASK CONTEXT: the
// publisher task, only; the reasoning, and what would break if that stopped being true, is at the
// definition in comfoair_mqtt_publish.c.
bool comfoair_mqtt_publish_one(const char *topic, const char *payload, int qos, bool retain);

// Publishes the entities whose state has changed, PUBLISH_PER_PASS of them at most, and leaves
// g_dirty set if it stopped early. It sends <prefix>/bus first when that verdict has moved, for
// the reason comfoair_mqtt_note_bus() gives. TASK CONTEXT: the publisher task, only.
void comfoair_mqtt_publish_changed(void);

// <prefix>/bus, owned by comfoair_mqtt_bus.c: sends the CAN verdict when it differs from what this
// connection last put on the broker. `forget` first drops that memory -- the connect sweep needs
// it, because a new broker (or one that lost its retained store) holds nothing of ours.
// TASK CONTEXT: the publisher task, only.
void comfoair_mqtt_publish_bus(bool forget);

// THE ONLY PUBLICATION ON A CLOCK IN THIS FIRMWARE, and it sends nothing until its period is up.
// The data-freshness entity's heartbeat. TASK CONTEXT: the publisher task, only -- it goes
// through the same publish path every entity's state does.
//
// WHAT IT BUYS. The house loses power entirely. The broker dies with the device, so the will is
// delivered to nobody; whatever comes back up first still holds the retained `online` written
// before the cut, and Home Assistant goes on showing yesterday's numbers as current ones FOR EVER.
// Availability topics in HA have no TTL. The only mechanism that turns silence into `unavailable`
// with no help from the device is `expire_after`, which exists on binary_sensor.py:54 and
// sensor.py:80 and nowhere else in components/mqtt/ -- so it is one binary_sensor, published every
// COMFOAIR_FRESHNESS_PERIOD_S, that HA expires COMFOAIR_FRESHNESS_EXPIRE_S after the last one.
//
// WHAT IT COSTS, because it is a real departure and not a free win. The whole publication model of
// this firmware is DELTAS -- comfoair_state.h refuses to count a repeated frame as a change, and
// every other topic in the tree moves only when its value does. This one message a period is the
// single exception, narrowed as far as it can be narrowed: 288 messages a day at 300 s, against
// 1440 at 60 s. DO NOT add a second timed publication beside it; if something else needs one, the
// argument has to be made again from the beginning.
//
// WHAT IT DOES NOT DO, stated here because the overstatement is what would mislead the owner. It
// DOES NOT grey out the 67 bus-backed entities -- that is <prefix>/bus -- and it is
// NOT a second level of availability. `expire_after` reaches neither the 13 writable entities nor
// the fan, so a device that burned down still shows an available fan at yesterday's level, under
// any choice that could have been made here; MQTT v1 has no complete answer to that. Calling this a
// second availability level would promise the owner more than the mechanism delivers, which is why
// the entity is called data freshness and nothing else.
void comfoair_mqtt_publish_freshness(void);

// --- discovery, owned by comfoair_mqtt_discovery.c ----------------------------------------------
//
// THE THREE RULES OF THE DISCOVERY GATE REACH THIS COMPONENT AS THREE CALLS, one
// per rule, and the flag they move is a static in that file rather than a g_ here: nothing else
// reads it, and the prefix is what tells a reader how far a write can be felt. The conditions
// themselves are comfoair_discovery_gate.h's, in comfoair_mqtt_backoff, because [env:native] does
// not build this component and anything decided in here is checked by nothing.

// The retained config documents, once per identity rather than once per connection: one per entity
// (the largest burst this device sends; HV-HA-03 measures it) that a broker which still holds them is not owed again.
// TASK CONTEXT: the publisher task, only -- it sleeps between batches.
void comfoair_mqtt_publish_discovery(void);

// The three command patterns, once per CONNECTION and never under the gate above: they live in the
// broker's session and this client's session is a clean one. TASK CONTEXT: the publisher task.
void comfoair_mqtt_subscribe_all(void);

// RULE TWO: the client has been rebuilt, so every field a config document is made of has just been
// rewritten and what is on the broker describes a device that no longer exists. TASK CONTEXT: the
// publisher task, or app_main before it exists -- it is called from comfoair_mqtt_start_client()
// beside the assignments it is about, and takes no locks and no clock.
void comfoair_mqtt_discovery_rebuilt(void);

// RULE THREE: what this pass saw of the link, and whether the device had an address while it saw
// it. CALLED FROM comfoair_mqtt_retry_pass(), which is where comfoair_net_ip_string() is already
// read once per pass; a second reader here would be a second unsynchronised read of two statics
// the Wi-Fi event task writes. TASK CONTEXT: the publisher task, once per pass, connected or not.
void comfoair_mqtt_note_link(bool connected, bool has_address);

// The birth message, the subscriptions, discovery and every entity's state, in that order, because
// a broker that has just accepted us holds nothing of ours. TASK CONTEXT: the publisher task,
// only. It sleeps between batches, so it must never be called from anything that is holding
// something up.
void comfoair_mqtt_republish_everything(void);

// The acting half of one inbound command, in comfoair_mqtt_command.c: carries out what
// comfoair_mqtt_decide() returns. TASK CONTEXT: the publisher task, only; the order of its
// consequences is pinned by the comment above its definition.
void comfoair_mqtt_handle_inbound(const comfoair_mqtt_inbound_t *in);

// The esp-mqtt event handler, in comfoair_mqtt_event.c, registered by
// comfoair_mqtt_start_client(). TASK CONTEXT: THE esp-mqtt TASK, synchronously inside that
// client's API lock (mqtt_client.c:1661-1662), while the publisher may be blocked in
// esp_mqtt_client_publish. Declared here for ONE caller and ONE line -- the registration -- and
// not so that anything else may call it.
void comfoair_mqtt_on_event(void *arg, esp_event_base_t base, int32_t id, void *data);

// Builds the client from the stored settings and rewrites g_ctx. TASK CONTEXT: the publisher task,
// or app_main BEFORE that task is created -- never anywhere else, because it writes every buffer
// the publisher reads. ESP_ERR_NOT_FOUND when no broker host is stored, which is not a failure and
// is the state every device ships in; ESP_ERR_INVALID_STATE when a client already exists;
// ESP_ERR_NO_MEM or a transport error otherwise, with the half-built client already destroyed.
esp_err_t comfoair_mqtt_start_client(void);

// Which cleanup the settings just saved call for, judged against the identity the LIVE client was
// built from -- see the reasoning at the definition in comfoair_mqtt_client.c, which is the only
// file that knows both identities. False when the answer is "none", and false whenever there is no
// live connection, so a device with an unreachable broker neither waits nor retries
// (AGENTS.md). TASK CONTEXT: the publisher task, in the reconfigure branch, BEFORE
// comfoair_mqtt_stop_client() -- after it, the client is gone and g_ctx has been rewritten, so
// there is nothing left to publish into the old identity with.
bool comfoair_mqtt_pending_cleanup(comfoair_ha_cleanup_t *what);

// Empties every topic of that cleanup with a zero-length retained payload, then waits up to two
// seconds for esp-mqtt's outbox to drain. TASK CONTEXT: the publisher task, while the OUTGOING
// client is still connected and g_ctx still describes the outgoing identity. Returns having done
// nothing when there is no connection.
//
// IT PUBLISHES NO "offline" OF ITS OWN. comfoair_mqtt_stop_client() writes that on the very next
// line of the caller, and one fact with two publishers is one fact that will disagree with itself.
void comfoair_mqtt_retract(comfoair_ha_cleanup_t what);

// Publishes "offline", stops the client, destroys it and clears the connection's flags. TASK
// CONTEXT: the publisher task, or app_main before it exists. Safe with no client. A caller on
// another task would deadlock against the esp-mqtt task -- the reasoning is at the definition.
void comfoair_mqtt_stop_client(void);

// One pass of the retry ladder: reads the clock, the address and the handler's atomics, and makes
// at most one call to esp_mqtt_client_reconnect(). TASK CONTEXT: the publisher task, only -- the
// ladder's state is a plain static with no atomic and no lock, and that is sound for exactly one
// reason, which is that this is called from one task. Cheap enough for every pass: sixteen bytes
// of FNV, one comfoair_net_ip_string() and no allocation.
void comfoair_mqtt_retry_pass(void);

// The address this pass read, and what it means for the Home Assistant device card. Rewrites g_url
// and, when the url MOVED, lowers the discovery flag so the card is published again with a link
// that works. TASK CONTEXT: the publisher task, from inside comfoair_mqtt_retry_pass(), which is
// where comfoair_net_ip_string() is already read -- a second reader here would be a second
// unsynchronised read of two statics the Wi-Fi event task writes, the same cost
// comfoair_mqtt_note_link() is shaped to avoid.
void comfoair_mqtt_note_address(const char *ip);

// The client has just been rebuilt from the stored settings. Resets the ladder -- its one reset
// besides a settled session: the owner corrected something, so he must not wait out a five-minute
// rung -- and then moves applied_epoch, which tells the browser that the verdict it now reads
// belongs to the settings it just saved. TASK CONTEXT: the publisher task, in the reconfigure
// branch, AFTER comfoair_mqtt_start_client() has returned.
//
// AFTER, and this is the whole of why: an epoch moved by the httpd task when the
// REQUEST arrives is an epoch that changes while the previous client is still connected, so the
// page reads "connected" from the connection it asked to replace and stops polling.
void comfoair_mqtt_retry_applied(void);

// The ladder's five values for GET /api/status. TASK CONTEXT: any task; every one is an atomic and
// none is dereferenced, the same best-effort snapshot rule the counters carry.
void comfoair_mqtt_retry_stats(comfoair_mqtt_stats_t *out);
