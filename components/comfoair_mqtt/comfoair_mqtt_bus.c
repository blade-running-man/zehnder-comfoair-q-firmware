// <prefix>/bus: whether the ventilation unit answers on CAN, as the second availability topic.
// Cut out of comfoair_mqtt_publish.c for the 350-line ceiling; no behaviour moved. The task rule
// is comfoair_mqtt_internal.h's: everything below the note function runs on the publisher task.
#include <stdatomic.h>

#include "comfoair_ha.h"
#include "comfoair_mqtt_internal.h"

// --- the bus, as the second availability topic --------------------------------------------------

// Whether the ventilation unit is answering on CAN, as comfoair_phase_monitor last projected it
// through comfoair_bus_health_available(). ONE VERDICT, TWO READERS: this topic and the
// BUS_SILENT phase are the same fact seen twice, so no threshold, no clock
// and no second opinion lives on this side of the wall -- comfoair_bus_health.h spends a paragraph
// on what two thresholds answering one question look like to an owner, which is one colour on the
// device beside a different card in Home Assistant.
//
// FALSE INITIALLY, and honestly so: before the monitor's first tick nothing has been heard, and
// comfoair_bus_health_available(COMFOAIR_BUS_NEVER_SEEN) is false too. A device that connects to a
// broker before the monitor has run therefore says `offline` and corrects itself a second later,
// which is the right way round -- the other order would publish `online` for a bus nobody has
// looked at yet.
static _Atomic bool s_bus_available;
// What was last PUT ON THE BROKER, so a verdict that has not moved costs no message. -1 is "not
// this connection", which is why a reconnect republishes it: a broker that lost its retained store,
// or a new one entirely, holds nothing of ours. Written only by the publisher task.
static int s_bus_published = -1;

// TASK CONTEXT: any task -- comfoair_phase_monitor's 1 Hz tick is the only caller. It does not
// publish, and must not: every call into esp-mqtt in this component belongs to the publisher task.
// It wakes that task instead, and only when the answer has actually changed, so a bus that is
// simply healthy costs one atomic store per second and nothing else.
//
// SAFE before comfoair_mqtt_init(), with no broker configured and with the client destroyed:
// comfoair_mqtt_notify_state_changed() checks g_task, and the publish below checks g_connected.
// Nothing waits on a peer and nothing fails because one is absent (AGENTS.md).
void comfoair_mqtt_note_bus(bool available)
{
    if (atomic_exchange_explicit(&s_bus_available, available, memory_order_relaxed) != available)
        comfoair_mqtt_notify_state_changed();
}

// BEFORE THE STATES, at every call site, and that is the whole ordering rule.
// Home Assistant applies an availability message to the entity as it arrives; sending yesterday's
// temperature first and only then admitting the bus is dead leaves a window in which the owner is
// shown a fresh-looking reading. The window is small and it is exactly the lie this topic exists
// to close.
//
// RETAINED, QOS_META. Retention does not make an availability message idempotent -- a missed
// `offline` leaves Home Assistant showing a device that is gone until the next change, which on a
// dead bus is never -- so this is the same QoS the birth message and the will already use.
void comfoair_mqtt_publish_bus(bool forget)
{
    if (forget)
        s_bus_published = -1;
    const bool available = atomic_load_explicit(&s_bus_available, memory_order_relaxed);
    if ((int)available == s_bus_published)
        return;
    if (comfoair_ha_meta_topic(&g_ctx, COMFOAIR_HA_META_BUS, g_topic, sizeof g_topic) == 0)
        return;
    // ONLY ON SUCCESS. A publish refused because the link had just dropped must not be remembered
    // as sent, or the verdict would wait for the next CHANGE -- and on a bus that stays dead there
    // is no next change. The reconnect republishes it anyway; this keeps the pass in between right.
    if (comfoair_mqtt_publish_one(g_topic,
                                  available ? COMFOAIR_HA_PAYLOAD_ONLINE
                                            : COMFOAIR_HA_PAYLOAD_OFFLINE,
                                  QOS_META, true))
        s_bus_published = available;
}
