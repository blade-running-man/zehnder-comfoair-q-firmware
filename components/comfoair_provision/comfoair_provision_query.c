// The reading half of comfoair_provision: every question the API, the LED, the log and the radio
// ask, and NOT ONE DECISION. Nothing here has a clock of its own and nothing here moves a state --
// comfoair_prov_take_action() is the single writer, and all it does is hand over the one-shot the
// machine already decided on and clear it.
//
// SPLIT OUT OF comfoair_provision.c 2026-09-06, along the line that file had already drawn with a
// `// --- questions ---` comment: 810 lines against a ceiling of 350 (AGENTS.md), and the rule is
// that an oversized file is cut when somebody next works inside it. The two halves share five
// helpers through comfoair_provision_internal.h and nothing else; every rule that had to be asked
// identically on both sides is one of those five, so there is no second spelling of any of them.
#include "comfoair_provision.h"

#include <stddef.h>

#include "comfoair_provision_internal.h"


comfoair_prov_state_t comfoair_prov_state(const comfoair_prov_t *s)
{
    // Every question below is reachable from an HTTP handler on a device whose provisioning
    // task has not started yet, so none of them may fall over. WINDOW_CLOSED rather than the
    // zero value: it is the answer that asks for nothing to be raised, and a machine that does
    // not exist should not be telling the radio to do anything. It is the right answer for the
    // radio and the wrong one for a status page -- see the DO NOT on this function in the header.
    return s == NULL ? COMFOAIR_PROV_WINDOW_CLOSED : s->state;
}

comfoair_prov_mode_t comfoair_prov_mode(const comfoair_prov_t *s)
{
    if (s == NULL)
        return COMFOAIR_PROV_MODE_OFF;

    switch (s->state) {
    case COMFOAIR_PROV_UNCONFIGURED:
        return COMFOAIR_PROV_MODE_ACCESS_POINT;
    case COMFOAIR_PROV_WINDOW_CLOSED:
        return COMFOAIR_PROV_MODE_OFF;
    case COMFOAIR_PROV_CONNECTED:
        // The same shape as the default: branch below, and for the same reason. A connected device
        // has no window of its own -- on_connected() takes the access point down, because the
        // unclaimed rule counts the device as unclaimed for as long as an open one is up -- so
        // window_running is false here in every ordinary life. The one thing that sets it is the
        // short press of the button, and this line is what makes that press visible: an owner whose
        // router password has just changed presses the button, the access point joins the station
        // that is still up, and they hand the device the new password without it having to fail
        // first. Answering MODE_STATION unconditionally opened a window nobody could reach.
        return s->window_running ? COMFOAIR_PROV_MODE_AP_STA : COMFOAIR_PROV_MODE_STATION;
    case COMFOAIR_PROV_FALLBACK_AP:
        return COMFOAIR_PROV_MODE_AP_STA;
    default:
        // CONNECTING, TRIAL, RETRYING. The access point stays up for as long as the window
        // does, and not one moment longer: the answer to POST /api/provision has to reach a
        // phone that is still on it, and a trial that fails is only useful if there is
        // somewhere to say why.
        return s->window_running ? COMFOAIR_PROV_MODE_AP_STA : COMFOAIR_PROV_MODE_STATION;
    }
}

bool comfoair_prov_is_provisioned(const comfoair_prov_t *s)
{
    if (s == NULL)
        return false;

    // The unclaimed rule, and the single most important line in this component. Not "are there
    // credentials" and not "is there an address": the question is whether anybody in radio range is
    // being listened to right now. comfoair_http_policy.c decides who may write from this flag, and
    // the moment it goes true while the access point is still up is the moment a passer-by can set
    // the first UI password and own the device.
    return s->has_credentials && !s->ap_on_air;
}

bool comfoair_prov_has_credentials(const comfoair_prov_t *s)
{
    return s != NULL && s->has_credentials;
}

bool comfoair_prov_has_known_good(const comfoair_prov_t *s)
{
    return s != NULL && s->has_known_good;
}

bool comfoair_prov_window_is_open(const comfoair_prov_t *s)
{
    // Reports what the last tick decided rather than re-deciding it here, so that every reader
    // of the status -- the API, the log line, the LED -- sees the same window closing at the
    // same instant. A caller that stops ticking stops the machine, which is deliberate: that
    // is far easier to notice than a machine that half runs.
    return s != NULL && s->ap_on_air && s->window_running;
}

bool comfoair_prov_window_has_closed(const comfoair_prov_t *s)
{
    return s != NULL && s->window_ever_closed;
}

bool comfoair_prov_address_never_arrived(const comfoair_prov_t *s)
{
    return s != NULL && s->address_never_arrived;
}

uint32_t comfoair_prov_window_remaining_ms(const comfoair_prov_t *s, uint32_t now_ms)
{
    if (s == NULL || !comfoair_prov_window_open_at(s, now_ms))
        return 0;

    const uint32_t length = comfoair_prov_window_length(s);
    if (length == 0)
        return COMFOAIR_PROV_NEVER;  // the sealed-device rule

    const uint32_t by_window  = comfoair_prov_remaining(now_ms, s->window_extended_at, length);
    const uint32_t by_ceiling = s->cfg.setup_window_ceiling_ms == 0
                                    ? COMFOAIR_PROV_NEVER
                                    : comfoair_prov_remaining(now_ms, s->window_opened_at,
                                                s->cfg.setup_window_ceiling_ms);
    return by_window < by_ceiling ? by_window : by_ceiling;
}

bool comfoair_prov_should_roll_back(const comfoair_prov_t *s)
{
    // Latched with the action, so it answers false again the moment the caller has done it.
    return s != NULL && s->action == COMFOAIR_PROV_ACTION_RESTORE_CREDENTIALS;
}

comfoair_prov_failure_t comfoair_prov_failure(const comfoair_prov_t *s)
{
    return s == NULL ? COMFOAIR_PROV_FAIL_NONE : s->failure;
}

uint8_t comfoair_prov_failure_reason(const comfoair_prov_t *s)
{
    return s == NULL ? 0 : s->failure_reason;
}

const char *comfoair_prov_failure_name(comfoair_prov_failure_t failure)
{
    switch (failure) {
    case COMFOAIR_PROV_FAIL_NETWORK_ABSENT: return "network-not-found";
    case COMFOAIR_PROV_FAIL_WRONG_PASSWORD: return "wrong-password";
    case COMFOAIR_PROV_FAIL_REFUSED:        return "refused";
    case COMFOAIR_PROV_FAIL_NO_ADDRESS:     return "no-address";
    // COUNT is a counter and not a verdict; it lands here because the tail is the honest answer
    // for a number that names nothing, and "none" is what a device with no failure reports.
    case COMFOAIR_PROV_FAIL_NONE:
    case COMFOAIR_PROV_FAIL_COUNT:          break;
    }
    return "none";
}

uint32_t comfoair_prov_fallback_in_ms(const comfoair_prov_t *s, uint32_t now_ms)
{
    // ap_on_air as well as the state: counting down to an access point while one is already
    // up would be a status line that contradicts the radio, and tick() will not raise a second
    // one anyway.
    if (s == NULL || s->state == COMFOAIR_PROV_FALLBACK_AP || s->ap_on_air || !s->failing ||
        !comfoair_prov_failure_earns_the_access_point(s->failure) || s->cfg.fallback_after_ms == 0)
        return COMFOAIR_PROV_NEVER;

    return comfoair_prov_remaining(now_ms, s->failing_at, s->cfg.fallback_after_ms);
}

comfoair_prov_action_t comfoair_prov_take_action(comfoair_prov_t *s)
{
    if (s == NULL)
        return COMFOAIR_PROV_ACTION_NONE;

    const comfoair_prov_action_t action = s->action;
    s->action = COMFOAIR_PROV_ACTION_NONE;
    return action;
}

// Numbers, not scenarios. These are wifi_err_reason_t from ESP-IDF 5.5.5,
// components/esp_wifi/include/esp_wifi_types_generic.h:113-178, and they arrive as
// wifi_event_sta_disconnected_t.reason -- a uint8_t (:1167), which is why nothing above 255
// needs a branch here. Throwing this byte away in the event handler is the easy option, and it
// is how "the router is rebooting" and "you changed the password" become one message saying
// neither.
comfoair_prov_failure_t comfoair_prov_classify(uint8_t reason)
{
    switch (reason) {
    // Nothing answered. BEACON_TIMEOUT belongs here rather than with the refusals: it means
    // the access point we were talking to stopped being heard, which is what a router reboot
    // and a router that has been unplugged both look like from a station.
    case 200:  // WIFI_REASON_BEACON_TIMEOUT (:165)
    case 201:  // WIFI_REASON_NO_AP_FOUND (:166)
    case 210:  // WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY (:175)
    case 211:  // WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD (:176)
    case 212:  // WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD (:177)
        return COMFOAIR_PROV_FAIL_NETWORK_ABSENT;

    // It answered and would not take the key. There is no reason code that says "password":
    // a wrong PSK fails the MIC check on message 2 of the four-way handshake and the access
    // point simply stops replying, so what a station reports is a timeout.
    case 14:   // WIFI_REASON_MIC_FAILURE (:130)
    case 15:   // WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT (:131)
    case 202:  // WIFI_REASON_AUTH_FAIL (:167) -- WPA3-SAE rejects the passphrase at auth
    case 204:  // WIFI_REASON_HANDSHAKE_TIMEOUT (:169)
        return COMFOAIR_PROV_FAIL_WRONG_PASSWORD;

    // Ours, and therefore not an answer to anybody. esp_wifi_disconnect() raises
    // WIFI_EVENT_STA_DISCONNECTED with this code, and the header's contract for
    // COMFOAIR_PROV_ACTION_CONNECT is what makes the caller call it: "disconnect first if the
    // station is associated". Sending it to the default arm below made the machine's own recovery
    // nudge come back as a router refusal -- the wrong one of the owner's four answers, and worse,
    // one of the three that count toward the fallback's clock, so a no-address fault raised an open
    // access point half an hour later. DO NOT add the other locally-flavoured codes here on the
    // same reasoning: 3 (AUTH_LEAVE, :116) and 47 (AP_INITIATED, :157) can equally be a router
    // kicking us for good, and a code that stops counting toward the fallback's clock is a device
    // that never raises its access point again. Reason 8 earns the exception because THIS FILE is
    // what causes it.
    case 8:    // WIFI_REASON_ASSOC_LEAVE (:124)
        return COMFOAIR_PROV_FAIL_NONE;

    // Everything else. Named ones, so the reasoning is visible: the access point is full (5),
    // this is an enterprise network our provisioning form cannot express (23) -- telling the
    // owner "wrong password" there would send them round in circles retyping a PSK that was
    // never going to be asked for -- association was rejected (203), or the attempt simply
    // failed (205).
    //
    // The DEFAULT matters more than the list. Sixty-odd codes exist and this maps a dozen; an
    // unlisted one has to land in a class that counts toward the fallback's clock, or a device
    // failing for a reason nobody enumerated would never raise its access point again -- which is
    // the state failure-model.md forbids outright. "The router will not have us" is also the
    // honest thing to say when there is nothing better to say.
    case 5:    // WIFI_REASON_ASSOC_TOOMANY (:119)
    case 23:   // WIFI_REASON_802_1X_AUTH_FAILED (:139)
    case 203:  // WIFI_REASON_ASSOC_FAIL (:168)
    case 205:  // WIFI_REASON_CONNECTION_FAIL (:170)
    default:
        return COMFOAIR_PROV_FAIL_REFUSED;
    }
}
