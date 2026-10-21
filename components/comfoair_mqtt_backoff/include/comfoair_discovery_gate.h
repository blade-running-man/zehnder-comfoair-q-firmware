// WHEN THE RETAINED DISCOVERY TREE HAS TO BE WRITTEN AGAIN. The as-built rules:
// docs/interfaces/home-assistant-discovery.md, "When discovery is published".
//
// The tree is one config document per entity -- the largest burst this device sends -- and until
// 2026-09-07 every reconnection sent all of it. It does not have to: Home Assistant reads the
// config documents from the broker's RETAINED store, so a broker that still holds them needs
// nothing from us. What this file decides is the one question that follows -- whether the store
// can still be trusted.
//
// ONE FLAG AND NOT A FINGERPRINT, AND THE FINGERPRINT IS THE THING TO ARGUE WITH, because it is
// the idea a later reader will want to bring back: hash the six fields discovery is built from,
// republish when the hash moves. DO NOT. Two reasons, and the second is a defect rather than a
// preference:
//
//  * IT WOULD BE THE SAME ANSWER, SPELLED LONGER. A discovery document is built from
//    comfoair_ha_ctx_t and a registry row and from nothing else (comfoair_ha.h, and the payload
//    builder in comfoair_ha_payload.c), and comfoair_ha_ctx_t is filled in exactly one place --
//    comfoair_mqtt_start_client(), the six assignments around comfoair_mqtt_client.c:226-231.
//    So "the client was rebuilt" is both necessary and sufficient for "the content changed", and
//    a hash over six fields does the work of a bool cleared by the very function that rewrites
//    them.
//  * AND THE FINGERPRINT LEAKS. The broker's ADDRESS is not one of the six: neither
//    s_broker.host nor s_port reaches comfoair_ha_ctx_t. An owner moving to a different broker
//    with the same prefix would produce a MATCHING fingerprint and an EMPTY new broker -- the
//    device connects, publishes every state, and publishes not one config document. Nothing
//    appears in Home Assistant, on a device with nothing wrong with it, and the only way out is
//    a reboot. The bool has no such hole because it is cleared by the rebuild itself, and
//    changing the host is a rebuild.
//
// THE THIRD RULE IS THE ONE WITH REAL CONTENT, and it is a discriminator: WE fell over, or the
// BROKER did. If the link dropped while this device still had an IP address, the drop happened at
// the far end -- the broker may have been restarted, and a broker that has been restarted may
// have lost its retained store, so discovery is owed again. If the address had already gone, the
// drop is ours: the broker never noticed, and its retained store is intact.
//
// NO SUBSCRIPTION TO homeassistant/status IS TAKEN, and this is why we can do without one. The
// contract of subscribe_all() -- "Three patterns, and no more: this device hears only its own
// tree" -- does not change. A fourth pattern reaching into somebody else's tree would need a
// second rule in the broker's ACL, and a device whose ACL refused exactly that one would report
// its subscriptions as refused and tell the owner "commands will not arrive" while every command
// worked perfectly.
//
// THE COST OF BEING WRONG IS NOT SYMMETRIC, which is what decides every boundary below. Clearing
// too rarely means a device that connects to a broker with an empty retained store and publishes
// no discovery: NOTHING APPEARS IN HOME ASSISTANT while the device is perfectly healthy. Clearing
// too often means the whole tree on a reconnection -- the very cost this exists to remove, but only a cost.
// So where a case is genuinely ambiguous the answer here is "clear".
//
// PURE, AND A FILE OF ITS OWN IN THIS COMPONENT for the reason comfoair_retract_budget.h gives
// about itself: comfoair_mqtt is esp-mqtt, FreeRTOS and a client handle, so [env:native] does not
// build it and anything decided in there is checked by NOTHING. The three rules are here, where
// test/test_discovery_gate calls them; what is left at the call sites is a clock, an address and
// a client handle, and those are hardware scenarios HV-HA-08..12 (06b-home-assistant.md).
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The whole of what this device remembers about its own discovery tree.
 *
 * `published` is the single bool argued for above: discovery has gone out, and nothing since has
 * invalidated it. `link` is NOT a second flag about discovery -- it is what the previous
 * publisher pass saw on the connection, and the third rule cannot be written without it, because
 * "the link dropped" is an EDGE and a flag can only be read as a level. Keeping the two in one
 * struct is what stops the edge being computed against a `link` some other line has already
 * updated.
 *
 * Zero is the correct initial value in both fields: nothing has been published and no session has
 * been seen.
 */
typedef struct {
    bool published;
    bool link;
} comfoair_discovery_gate_t;

/** What the publisher has just done or just seen. */
typedef enum {
    /** An ordinary pass of the publisher task: nothing happened but the link being looked at. */
    COMFOAIR_DISCOVERY_PASS = 0,
    /** publish_discovery() has just completed a full sweep with the link still up. */
    COMFOAIR_DISCOVERY_SENT,
    /** comfoair_mqtt_start_client() has rewritten comfoair_ha_ctx_t from the stored settings. */
    COMFOAIR_DISCOVERY_REBUILT,
} comfoair_discovery_event_t;

/**
 * All three rules, and there is no fourth anywhere else.
 *
 * `connected` and `has_address` are read only for COMFOAIR_DISCOVERY_PASS; the other two events
 * are things the caller did rather than things it saw. `has_address` is what
 * comfoair_net_ip_string() answered on this pass -- the publisher task already takes that reading
 * for the retry ladder, so the third rule costs nothing new.
 *
 * BY VALUE, IN AND OUT. A `comfoair_discovery_gate_t *` would let a caller update one field and
 * not the other; this shape makes the state after an event a function of the state before it, and
 * that is the property the suite next door exercises.
 *
 * PURE. No clock, no globals, no allocation; safe from any task.
 */
comfoair_discovery_gate_t comfoair_discovery_gate(comfoair_discovery_gate_t gate,
                                                  comfoair_discovery_event_t event, bool connected,
                                                  bool has_address);

/**
 * Whether publish_discovery() has anything to do.
 *
 * Inline and trivial ON PURPOSE: it is the NAME of `!published`, so that no call site spells the
 * negation itself and no call site can drift from this one's meaning.
 */
static inline bool comfoair_discovery_due(comfoair_discovery_gate_t gate)
{
    return !gate.published;
}

#ifdef __cplusplus
}
#endif
