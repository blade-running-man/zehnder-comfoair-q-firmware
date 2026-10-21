// Three rules and no clock, and the header carries the whole argument for why they are a file
// rather than three lines inside comfoair_mqtt: that component is not built by [env:native], so a
// condition written there is checked by nothing at all. The same lesson comfoair_retract_budget.c
// was extracted for, one finding later.
#include "comfoair_discovery_gate.h"

comfoair_discovery_gate_t comfoair_discovery_gate(comfoair_discovery_gate_t gate,
                                                  comfoair_discovery_event_t event, bool connected,
                                                  bool has_address)
{
    // A SWITCH AND NOT AN if/else CHAIN, because [env:native] builds with -Werror=switch
    // (platformio.ini): a fourth event added to the enum stops this compiling until somebody has
    // said what it does to the flag, rather than falling into the pass below and being answered
    // "an ordinary pass" by silence.
    switch (event) {
    case COMFOAIR_DISCOVERY_REBUILT:
        // RULE TWO. comfoair_mqtt_start_client() has just rewritten every field a discovery
        // document is built from, so everything published under the old ones describes a device
        // that no longer exists at that address, under that prefix or by that name. `link` goes
        // with it: the session those six fields belonged to was stopped before they were
        // rewritten, and an edge measured against it would be an edge of a dead client.
        gate.published = false;
        gate.link      = false;
        return gate;
    case COMFOAIR_DISCOVERY_SENT:
        // RULE ONE, and `link` is deliberately NOT touched here. The pass is the only thing that
        // looks at the connection, so it is the only thing that may record what it saw; writing
        // `link` from this event's arguments would date the edge from the moment discovery went
        // out rather than from the moment the publisher last looked.
        gate.published = true;
        return gate;
    case COMFOAIR_DISCOVERY_PASS:
        break;
    }

    // RULE THREE, AND ALL THREE TERMS CARRY WEIGHT.
    //
    //  * `gate.link` makes this an EDGE. Drop it and the rule becomes "disconnected, with an
    //    address", which is true of every pass between the moment a rebooting router hands the
    //    address back and the moment the broker is reachable again -- so the one failure this
    //    whole mechanism exists to make cheap would republish the whole tree, and would do it on a broker
    //    whose retained store never went anywhere.
    //  * `!connected` is the drop itself.
    //  * `has_address` is the discriminator, and it is the only thing on the device that can tell
    //    "the broker went away" from "we went away" without subscribing to somebody else's tree.
    //    An address at the moment of the drop means the far end let go: it may have been
    //    restarted, and a restarted broker may have come back with an empty retained store. No
    //    address means the drop is ours and the broker still holds everything.
    //
    // A BROKER THAT RESTARTS WITH ITS PERSISTENCE FILE INTACT IS REPUBLISHED TO ANYWAY, and that
    // is accepted rather than overlooked: nothing on this side can tell the two restarts apart,
    // and the header says which way an ambiguous case has to fall. A whole tree resent is a cost; an owner
    // whose Home Assistant shows nothing is a fault.
    if (gate.link && !connected && has_address)
        gate.published = false;
    // AFTER the comparison, never before -- this is the value the NEXT pass measures its edge
    // against, and moving it up turns the condition into `connected && !connected`, which is
    // false for ever and clears nothing at all.
    gate.link = connected;
    return gate;
}
