// The table. Read comfoair_captive.h first -- the reasoning for answering these paths at all,
// and for answering them with success rather than with a redirect, lives there next to the
// declarations. What is repeated here is only what an editor of this file needs in front of them.
#include "comfoair_captive.h"

#include <string.h>

// EVERY BYTE BELOW WAS FETCHED FROM THE LIVE ORIGINAL ON 2026-09-04, and the Content-Length it
// arrived with is in the comment beside it. That is the whole of the specification: these are
// compared as bytes on the other end, not parsed, so a body that is nearly right is a body that
// is wrong. Do not retype them from memory -- curl the URL in the comment.

// http://captive.apple.com/hotspot-detect.html -- Content-Length: 69, and the 69th byte is the
// newline. iOS and macOS open the Captive Network Assistant on anything else, which is the
// cut-down browser this component exists to keep the setup form out of.
static const char APPLE_SUCCESS[] =
    "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>\n";

// http://www.msftncsi.com/ncsi.txt -- Content-Length: 14. No newline. Windows 8 and earlier, and
// a long tail of embedded stacks that copied it.
static const char NCSI_TXT[] = "Microsoft NCSI";

// http://www.msftconnecttest.com/connecttest.txt -- Content-Length: 22. No newline either.
// Windows 10 and later. Anything but 200 with exactly this body raises the sign-in flyout.
static const char CONNECTTEST_TXT[] = "Microsoft Connect Test";

// http://detectportal.firefox.com/success.txt -- Content-Length: 8, and this one DOES end in a
// newline. The asymmetry with the two Windows strings above is not a typo in either place; it is
// why test/test_captive pins all three lengths rather than one rule about newlines.
static const char FIREFOX_SUCCESS[] = "success\n";

// http://detectportal.firefox.com/canonical.html -- Content-Length: 90. What Firefox serves when
// there is no portal is a redirect to its own help page; a browser that gets this concludes the
// network is clear and closes the portal notification bar.
static const char FIREFOX_CANONICAL[] =
    "<meta http-equiv=\"refresh\" content=\"0;url=https://support.mozilla.org/kb/captive-portal\"/>";

// One string, on every row, because there is no row it should be missing from. See the header:
// the phone asks these same URLs again on the owner's real network within the hour.
#define NO_STORE "no-store"

// sizeof - 1 rather than a written-out number: the literal and its length cannot drift apart,
// and the length is what actually goes on the wire as Content-Length.
static const comfoair_captive_answer_t TABLE[] = {
    // Android probes http://connectivitycheck.gstatic.com/generate_204 and a per-vendor list of
    // mirrors of it; every one of them uses this path, which is why the path is what is matched
    // and the Host header is ignored. /gen_204 is Google's older spelling and still in use.
    {"/generate_204", "204 No Content", NULL, NULL, 0, NO_STORE, "Android"},
    {"/gen_204", "204 No Content", NULL, NULL, 0, NO_STORE, "Android"},

    {"/hotspot-detect.html", "200 OK", "text/html", APPLE_SUCCESS, sizeof APPLE_SUCCESS - 1,
     NO_STORE, "iOS and macOS"},

    {"/ncsi.txt", "200 OK", "text/plain", NCSI_TXT, sizeof NCSI_TXT - 1, NO_STORE,
     "Windows 8 and earlier"},
    {"/connecttest.txt", "200 OK", "text/plain", CONNECTTEST_TXT, sizeof CONNECTTEST_TXT - 1,
     NO_STORE, "Windows 10 and later"},

    {"/success.txt", "200 OK", "text/plain", FIREFOX_SUCCESS, sizeof FIREFOX_SUCCESS - 1, NO_STORE,
     "Firefox"},
    {"/canonical.html", "200 OK", "text/html", FIREFOX_CANONICAL, sizeof FIREFOX_CANONICAL - 1,
     NO_STORE, "Firefox"},
};

// Deliberately absent, so that the next reader knows they were considered rather than forgotten:
//
//  * /redirect -- Windows fetches it only after connecttest.txt has already failed, and it exists
//    to be redirected to a portal. Serving it would mean answering the branch above correctly and
//    then contradicting it.
//  * /library/test/success.html -- an older Apple path. Harmless to add, and no device that
//    reaches this firmware still asks for it.
//  * /kindle-wifi/wifistub.html, and the rest of the per-vendor list. Each is one row, and each
//    is one more byte-exact body that nobody here can verify against a device they own.
//  * The OTHER HALF OF THE WINDOWS PROBE, which is not a path at all. NCSI is two questions, not
//    one: the fetch above, and a DNS lookup of dns.msftncsi.com that must come back as
//    131.107.255.255. comfoair_captive_dns.c answers every name with the access point's own
//    address, so Windows sees its web probe agree and its DNS probe disagree -- one of the shapes
//    that produces the yellow warning triangle rather than "connected". Fixing it means a
//    special-cased name in the DNS responder: a case-insensitive comparison against one string,
//    on the one code path in this firmware that a stranger on the pavement can reach, in exchange
//    for the least likely client here. Considered and declined; if it is ever wanted, it belongs
//    beside the qtype test in comfoair_captive_dns_reply() and it needs the malformed-name tests
//    of test/test_captive pointed at it first.
//
// Adding a row costs a test edit, because test_the_table_is_seven_paths_and_the_lookup_finds_
// every_one pins the count. That is on purpose: this table is a list of things the firmware
// lies to, and it should not grow by accident.

const comfoair_captive_answer_t *comfoair_captive_answer(const char *path)
{
    if (path == NULL)
        return NULL;
    // Linear and exact. Seven rows on a path that arrives a handful of times per join, so the
    // cost of the loop is nothing and the cost of a prefix match -- "/generate_204/x" inheriting
    // the answer written for "/generate_204" -- is the same class of leak comfoair_http_policy.c
    // avoids with the same shape.
    for (size_t i = 0; i < sizeof TABLE / sizeof TABLE[0]; i++)
        if (strcmp(path, TABLE[i].path) == 0)
            return &TABLE[i];
    return NULL;
}

const comfoair_captive_answer_t *comfoair_captive_table(size_t *count)
{
    if (count != NULL)
        *count = sizeof TABLE / sizeof TABLE[0];
    return TABLE;
}
