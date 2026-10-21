// Who may do what, and when.
//
// Deliberately a pure function with no HTTP types in it, so the rule can be tested without
// a network stack: a rule that can only be exercised on a device is a rule whose holes show
// up in the field, not in a test.
//
// The shape of the answer, in one sentence: reading is open until the owner sets a
// password, writing is refused until they do, and radio proximity to an UNCLAIMED device
// authorises nothing but handing it a network.
//
// Unclaimed, not merely unprovisioned. A device whose router has died looks identical from
// the outside -- an access point and no connection -- but it holds somebody's data, and
// handing it to another network is a decision only its owner may make.
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    COMFOAIR_HTTP_GET,
    COMFOAIR_HTTP_POST,
    // Anything else -- PUT, DELETE, PATCH, an unparsed method. Refused rather than mapped
    // onto the closest known verb.
    COMFOAIR_HTTP_OTHER,
} comfoair_http_method_t;

typedef struct {
    // The device has Wi-Fi credentials and is on the owner's network. False means it is
    // serving its own access point, where anyone in radio range is "the client".
    bool provisioned;
    bool password_set;
    bool authenticated;
} comfoair_http_ctx_t;

typedef enum {
    COMFOAIR_HTTP_ALLOW,
    // 401: a password exists and this request did not present it.
    COMFOAIR_HTTP_UNAUTHORIZED,
    // 403: no credential could make this request acceptable in this state. Distinct from
    // 401 on purpose -- telling a client "authenticate" when no authentication exists yet
    // sends it round a loop it cannot leave.
    COMFOAIR_HTTP_FORBIDDEN,
} comfoair_http_decision_t;

comfoair_http_decision_t comfoair_http_check(comfoair_http_method_t method, const char *path,
                                             comfoair_http_ctx_t ctx);

#ifdef __cplusplus
}
#endif
