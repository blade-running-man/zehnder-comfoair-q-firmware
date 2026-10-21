// Turning an HTTP Authorization header into a yes or a no.
//
// This is the one component whose entire input is chosen by whoever is attacking the device.
// The setup access point is OPEN on purpose (docs/architecture/security.md, "Trust model"), so
// on it "the client" is anyone in radio range; afterwards the device sits on a household LAN,
// and treating the LAN as a trust boundary is an assumption this firmware explicitly refuses to
// make. Everything below is written for that reader: nothing is copied before its
// length is known, no answer depends on how nearly right a guess was, and no path can turn a
// device with no password into one that accepts a password.
//
// THREE THINGS ARE NOT HERE, and each absence is the design:
//
//  * The stored form of the password. That is comfoair_config's record -- `1$<iterations>$<salt
//    hex>$<digest hex>`, PBKDF2-HMAC-SHA256 over mbedtls with a per-password salt carried inside
//    the record (comfoair_config.h, COMFOAIR_CONFIG_KDF_ITERATIONS). A SECOND stored format here
//    would not merely duplicate it, it would be destroyed: comfoair_config_sanitize() ERASES a
//    ui_pw_hash its own parser cannot read, deliberately, so that an unreadable record leaves
//    the device open rather than sealed. A password stored in any other shape would therefore
//    vanish at the next boot. DO NOT add a hash to this component.
//  * Anything that hashes. The verifier arrives as a function pointer, the way comfoair_config
//    takes its KDF and for the same reason: mbedtls is on the device and not on the host, and a
//    component that cannot build on the host cannot have its parser tested against the input it
//    exists for. comfoair_auth_device_verifier() is the device's answer and is the only place
//    the two halves are joined.
//  * A lockout. A fixed delay per failed attempt, applied by the caller. Counting failures and
//    refusing afterwards is a denial of service against the owner -- who is the person most
//    likely to mistype -- and the whole recovery design turns on the owner still being able to
//    get in: the reset button and the USB port are behind the front panel of a running unit.
//
// NOTHING HERE LOGS, and nothing here may. The whole log ring is served by GET /api/log
// (comfoair_log.h), which on the open access point is world-readable (docs/architecture/
// security.md, "Write-only secrets"). That is also why comfoair_auth_check() does not return the
// parsed credential: there is no password in the outcome for a caller to put in a format string
// by accident, only the shape of the failure, which is safe to publish.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The longest password this parser will carry. It has to be at least what comfoair_config will
// STORE, or an owner sets the longest password the settings page accepts and can then never log
// in -- with no way in over the air and the front panel in the way of the button. The two
// constants are tied together by a _Static_assert in comfoair_auth.c so they cannot drift.
#define COMFOAIR_AUTH_PASS_MAX 128

// Milliseconds the caller must wait before answering a failed attempt.
//
// A speed bump, and ONE fact decides both what it buys and what it costs: esp_http_server serves
// all of its sockets from a single task (comfoair_http_start(), max_open_sockets = 7). A caller
// that sleeps here stops that task, so the delays do not run side by side -- seven open
// connections do not get seven guesses a second, they queue behind one another and an attacker
// grinding a word list gets one guess per second in total however many sockets they open. The
// same sentence is the price: every OTHER request waits out that second too, which is a lever an
// attacker can pull on purpose, and it is the reason this is one second and not five.
//
// DO NOT reason about this number as though the sockets ran in parallel -- this comment used to,
// and said in one breath that the delay is paid "per CONNECTION" and that one task serves them
// all. The two readings differ by a factor of seven in the attacker's favour, which is enough to
// size the constant against the wrong threat.
//
// One second is chosen against the owner rather than against the attacker: long enough that a
// script grinding through a word list is not worth writing, short enough that a person who
// mistyped their password does not conclude the device has crashed.
#define COMFOAIR_AUTH_FAIL_DELAY_MS 1000u

// What the header was, as distinct from whether the credential was right. The two are separate
// answers on purpose: "no header" is what every browser sends before it has been challenged and
// is not a wrong guess, while "malformed" is a broken client and "too long" is either an attack
// or an owner whose password no longer fits. One value for all of them would put those three in
// the same log line.
typedef enum {
    COMFOAIR_AUTH_OK,
    // No Authorization header, or one that is empty or all whitespace. Nothing was claimed.
    COMFOAIR_AUTH_ABSENT,
    // A header naming some other scheme -- Bearer, Digest, or a word that merely starts with
    // "Basic". Only Basic exists on this device.
    COMFOAIR_AUTH_NOT_BASIC,
    // Basic, but the token is not base64, or what it decodes to is not `user:password`.
    COMFOAIR_AUTH_MALFORMED,
    // Either a password longer than this device could ever have stored, or a header too big to be
    // carrying one at all. Refused, never truncated: a truncated credential asks a different
    // question from the one the client asked.
    COMFOAIR_AUTH_TOO_LONG,
} comfoair_auth_parse_t;

typedef struct {
    // Everything after the FIRST colon, which is what RFC 7617 section 2 means: a colon may not
    // appear in a user name and may appear in a password, so splitting anywhere else would turn
    // the password "pa:ss:word" into "word" and accept a wrong password as right.
    //
    // There is no user name in this struct because this device has no accounts: one password, and
    // whatever the owner types into the browser's name box is discarded. Checking a fixed name as
    // well would add a second thing to mistype, with nothing to show for it -- the password is the
    // whole of the secret either way.
    //
    // DISCARDED MEANS ITS LENGTH TOO, and that half is not free: it holds because the decoder
    // drops the name as it arrives (comfoair_auth.c, sink_byte) instead of buffering
    // `user:password` whole. While it buffered both, the SUM was what was bounded and the name was
    // charged against the password's room -- a 65-character name in front of the CORRECT
    // 128-character password came back TOO_LONG. A password manager that fills the name box with
    // an email address gets there without trying, and the answer to a correct password must never
    // be no: the reset button is behind the front panel of a running ventilation unit and there is
    // no way in over the air. What remains bounded is the header itself (comfoair_auth.c,
    // ENCODED_MAX), which is a limit on the request and not on either half of the credential.
    char password[COMFOAIR_AUTH_PASS_MAX + 1];
} comfoair_auth_credentials_t;

// Parses `header` -- the raw value of the Authorization field, with no field name and no colon.
// `out` is zeroed before anything is decided, so a refused header never leaves the previous
// call's password in the caller's struct.
comfoair_auth_parse_t comfoair_auth_parse(const char *header, comfoair_auth_credentials_t *out);

// Overwrites a credential the caller is done with.
//
// Exists as a call rather than as advice to memset because a memset over a buffer that is never
// read again is a dead store, and a compiler is entitled to delete it. The password otherwise
// stays on the HTTP task's stack until something else happens to use those bytes.
void comfoair_auth_forget(comfoair_auth_credentials_t *creds);

// Answers "does this candidate match the stored record". `stored` is comfoair_config's record;
// this signature is what comfoair_config_check_ui_password_against() becomes once its KDF is
// bound, which is all comfoair_auth_device_verifier() does.
typedef bool (*comfoair_auth_verifier_t)(const char *stored, const char *candidate);

typedef enum {
    COMFOAIR_AUTH_GRANTED,
    COMFOAIR_AUTH_DENIED,
    // Nothing is stored, so there is no question to answer. NOT granted, and the difference is
    // load-bearing: comfoair_http_check() decides what an unclaimed device allows from
    // `password_set`, and a caller bridging this into its `authenticated` field must map only
    // GRANTED to true. Told that a stranger on the open access point had authenticated, that
    // policy would hand them the device.
    COMFOAIR_AUTH_NO_PASSWORD,
} comfoair_auth_result_t;

typedef struct {
    comfoair_auth_result_t result;
    // COMFOAIR_AUTH_FAIL_DELAY_MS after an attempt that was made and failed, 0 otherwise. The
    // caller applies it; nothing in here sleeps, because a pure function that sleeps cannot be
    // tested and would put the wait on whichever task happened to call it.
    uint32_t delay_ms;
    // Why, in a form that is safe to log. Never the credential itself.
    //
    // COMFOAIR_AUTH_ABSENT when `result` is NO_PASSWORD, whatever the header said: with nothing
    // stored the header is not decoded at all, which is what makes that branch structurally
    // unable to accept a credential rather than merely careful not to.
    comfoair_auth_parse_t header;
} comfoair_auth_outcome_t;

comfoair_auth_outcome_t comfoair_auth_check(const char *header, const char *stored,
                                            comfoair_auth_verifier_t verify);

// The device's verifier: comfoair_config's record parser with the mbedtls PBKDF2-HMAC-SHA256 KDF
// bound to it, and nothing else.
//
// Device only, and it does not exist off the device -- the same as comfoair_config_device_kdf(),
// which it calls. A host test injects its own verifier instead, and host-side code that calls
// this gets a link error, which is the informative failure rather than a stub quietly answering
// "no" to every password.
comfoair_auth_verifier_t comfoair_auth_device_verifier(void);

#ifdef __cplusplus
}
#endif
