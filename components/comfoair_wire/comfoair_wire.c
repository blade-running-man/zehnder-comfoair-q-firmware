// See comfoair_wire.h for what these five documents are and why they are not in comfoair_http.
//
// TWO RULES GOVERN EVERY FUNCTION OF THIS COMPONENT, and both are the same fact seen from two
// sides: this runs on a device whose setup access point is OPEN, and everything it logs is served
// to whoever can reach it by GET /api/log. The component's files are listed in README.md,
// Implementation.
//
//  * NO SECRET IS RENDERED. The projection is what turns a stored password into a sentinel, and
//    comfoair_config_public_t is deliberately the only thing the config renderer accepts -- a
//    renderer handed the raw document would be one decision away from publishing one.
//  * NO SUBMITTED VALUE IS QUOTED IN AN ERROR. A message names a FIELD, never its contents, and
//    comfoair_wire_result_t carries a pointer to a literal for exactly that reason.
#include "comfoair_wire.h"

// --- how a submission failed ------------------------------------------------------------------

const char *comfoair_wire_strerror(comfoair_wire_result_t result)
{
    switch (result.status) {
    case COMFOAIR_WIRE_OK:
        return "Accepted.";
    case COMFOAIR_WIRE_BAD_BODY:
        return "The request body is not a JSON object this device can read.";
    case COMFOAIR_WIRE_BAD_FIELD:
        // The FIELD is carried beside this sentence rather than formatted into it: this string is
        // a literal precisely so that nothing submitted can end up inside it, and a handler that
        // wants to name the field renders result.field as its own JSON member.
        return "A field is missing, of the wrong type, or longer than this device can store.";
    case COMFOAIR_WIRE_REFUSED:
        return comfoair_config_strerror(result.err);
    case COMFOAIR_WIRE_READ_ONLY:
        return comfoair_config_strerror(COMFOAIR_CONFIG_ERR_READ_ONLY);
    case COMFOAIR_WIRE_WRONG_ROUTE:
        // Names the route that works. The whole GET /api/config document carries wifi_ssid --
        // the page needs it to show which network is configured -- so a client that echoes the
        // document back arrives here without having done anything unreasonable, and a message
        // saying only "bad field" would send it looking at the value.
        return "The network is set through POST /api/provision, not here.";
    }
    return "The request could not be applied.";
}
