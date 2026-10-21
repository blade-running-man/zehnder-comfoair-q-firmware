// The paths a phone fetches to find out whether a network works, and what each one is told.
//
// A phone that joins a network probes a fixed URL before it will treat that network as usable.
// Serving the SPA shell with 200 to those probes -- which is what comfoair_http_assets.c's
// asset_get() does today, by falling through to "anything else is a client-side route" -- is not
// one mistake but three, and all three land on the owner in the middle of setup:
//
//  * The OS decides the network is captive and opens its sign-in browser. That browser is not
//    the phone's browser: no WebSocket, so /ws never connects and the state page stays blank,
//    and no Web Crypto -- which is moot on http://192.168.4.1 anyway (not a secure context),
//    but the missing WebSocket is not.
//  * The OS owns that window and closes it when it decides the session is done. On this device
//    that is while somebody is typing the password of their house into it.
//  * Android will not mark a network validated while it believes a portal is unresolved, and it
//    only stops believing that when a probe finally answers 204. There is no sign-in step here
//    that would ever produce one -- setup is not sign-in -- so the phone keeps mobile data as
//    its default route, and POST /api/provision leaves the house instead of arriving at the
//    device three metres away.
//
// So every probe is told the network is fine, in the exact bytes its OS compares against, and
// the owner reaches the setup page through DNS instead (comfoair_captive_dns.h: every name
// resolves to the access point, so anything typed into a real browser arrives here). DO NOT
// "improve" this table by redirecting a probe to the setup page -- that is all three failures
// back, deliberately.
//
// THIS TABLE FIXES THE FIRST TWO. It does not fix the third, and saying that it does was wrong:
// on Android 9 and later, NetworkMonitor runs an HTTPS probe (https://www.google.com/generate_204)
// alongside the HTTP one, and VALID needs the HTTPS half. The DNS half resolves www.google.com to
// the access point, where nothing listens on 443, so that probe fails with connection-refused
// while ours returns 204 -- and success-on-HTTP with failure-on-HTTPS is precisely the input for
// PARTIAL_CONNECTIVITY. The network stays unvalidated, the owner gets "Wi-Fi has limited
// connectivity", and cellular remains the default route until they tap through it.
//
// There is no fix for that from inside a DNS responder and an HTTP table: it would take a TLS
// listener on 443 holding a certificate for www.google.com. The honest statement is two of three,
// and the third is a README sentence -- "if your phone says the Wi-Fi has no internet, tap
// through; that is this device telling the truth about itself".
//
// The cost of the choice, so that it is not discovered later as a bug: no window opens by
// itself. The owner has to open a browser. That is a README sentence and a label on the box;
// the alternative is a form submitted over somebody's mobile data.
//
// Every literal in comfoair_captive_probe.c was fetched from the live original on 2026-09-04
// and its Content-Length recorded next to it. This is a byte-comparison protocol on the other
// end -- Windows compares to a string, Apple to a document, Firefox to both -- so an editor
// that normalises a trailing newline breaks provisioning for one vendor and nothing else.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// One row of the table: everything the HTTP layer has to send, so that it cannot forget half.
typedef struct {
    // Matched WHOLE, never as a prefix, and never against the query string -- the same rule and
    // the same reason as comfoair_http_policy.c's path_is(). The path arrives already stripped
    // of its query, fragment, absolute-form authority and trailing slash by
    // comfoair_http_request_path(); there is deliberately no second stripper here, because
    // two of them are two sets of rules to keep in agreement.
    const char *path;
    // Ready for httpd_resp_set_status(). Always 2xx: see the header comment for why a 3xx here
    // would undo the whole component.
    const char *status;
    // NULL when there is no body. esp_http_server sends a default type in that case and Android
    // does not look at it -- the status code and an empty body are the whole of its test.
    const char *content_type;
    const char *body;      // NULL for the 204s
    size_t      body_len;  // byte-exact, and asserted against strlen() by test/test_captive
    // "no-store", on every row. The phone asks www.msftconnecttest.com/connecttest.txt again in
    // ten minutes, at the same URL, on the owner's real network -- a cached copy of our answer
    // would tell it a genuinely captive hotel network is fine.
    const char *cache_control;
    // Which OS asks. For the log line, so that "a phone joined and was told the network is
    // fine" is visible while somebody is standing next to the unit wondering why nothing opened.
    const char *asked_by;
} comfoair_captive_answer_t;

// The answer for a path, or NULL when the path is not a probe and belongs to the SPA and the API
// as before.
//
// These answers are CONSTANTS, which is what makes it safe to serve them ahead of the access
// policy -- and serving them ahead of it is required, not an optimisation: on the fallback access
// point of a device that has a UI password, comfoair_http_check() answers an unauthenticated GET
// with 401, and 401 is not 204, which is a captive portal, which is the three failures above on
// the device whose owner is already trying to fix something. Nothing here is formatted at send
// time, so no name, address or reading can travel out this way, around the access policy.
const comfoair_captive_answer_t *comfoair_captive_answer(const char *path);

// The whole table, for a caller that wants to register the paths rather than test each request,
// and for the tests.
//
// `count` may be NULL, and then nothing is written to it -- a caller that does that gets a table
// it has no length for, which is useless but not fatal. The header used to say NULL was forbidden
// while the code tolerated it; the code's answer is the one kept, because this runs on a device
// nobody can reach to reset. Pinned by
// test_the_table_survives_a_caller_that_does_not_want_the_count.
const comfoair_captive_answer_t *comfoair_captive_table(size_t *count);

#ifdef __cplusplus
}
#endif
