// The cascade and the table of names. See comfoair_phase.h for what each phase means and why the
// order is what it is; the argument is not repeated here.
#include "comfoair_phase.h"

#include <stddef.h>   // NULL: the contract answers a null argument with BOOT

// The seven start branches of app_main as one question. Kept apart from the cascade because it is
// the part that grows: a subsystem added to app_main adds a field and a term here, and nothing
// else in this file moves.
static bool a_startup_branch_failed(const comfoair_phase_inputs_t *in)
{
    return !in->can_started || !in->poll_started || !in->control_started || !in->net_started ||
           !in->mqtt_ready || !in->mqtt_client_started || !in->http_started;
}

device_phase_t comfoair_phase_derive(const comfoair_phase_inputs_t *in)
{
    // A straight cascade in the order of the enumeration, because that order IS the rule -- see
    // comfoair_phase.h:32-35 and the note on COMFOAIR_PHASE_BROKER_REJECTED (:84-90). DO NOT
    // reorder to group the provisioning states together: 2, 3, 4, 6, 7, 8 and 9 are mutually
    // exclusive by construction, but 5, 10, 11 and 12 are not, and their order is a decision.
    if (in == NULL || !in->machine_ready) {
        return COMFOAIR_PHASE_BOOT;
    }
    if (in->ota_in_progress) {
        return COMFOAIR_PHASE_OTA;
    }
    if (in->prov_state == COMFOAIR_PROV_TRIAL) {
        return COMFOAIR_PHASE_TRIAL;
    }
    if (in->ap_on_air && in->prov_state == COMFOAIR_PROV_UNCONFIGURED) {
        return COMFOAIR_PHASE_SETUP_AP;
    }
    if (in->ap_on_air && in->prov_state == COMFOAIR_PROV_FALLBACK_AP) {
        return COMFOAIR_PHASE_RECOVERY_AP;
    }
    if (a_startup_branch_failed(in)) {
        return COMFOAIR_PHASE_FAULT;
    }
    if (in->prov_failure == COMFOAIR_PROV_FAIL_WRONG_PASSWORD ||
        in->prov_failure == COMFOAIR_PROV_FAIL_REFUSED) {
        return COMFOAIR_PHASE_NET_REJECTED;
    }
    if (in->prov_failure == COMFOAIR_PROV_FAIL_NETWORK_ABSENT ||
        in->prov_failure == COMFOAIR_PROV_FAIL_NO_ADDRESS) {
        return COMFOAIR_PHASE_NET_LOST;
    }
    // The two lines above ask about the failure and NOT about the state, which is safe for a
    // connected device only because comfoair_provision clears the failure the moment an address
    // arrives -- one address ends the run, both for the verdict and for the access-point
    // fallback's sustained-failure clock. DO NOT add "and the state is not CONNECTED" as a belt: it
    // would hide a machine that stopped clearing the failure, which is a defect this phase should
    // show, not paper over.
    if (in->prov_state == COMFOAIR_PROV_WINDOW_CLOSED) {
        return COMFOAIR_PHASE_SETUP_EXPIRED;
    }
    if (in->prov_state == COMFOAIR_PROV_CONNECTING || in->prov_state == COMFOAIR_PROV_RETRYING) {
        return COMFOAIR_PHASE_CONNECTING;
    }
    if (in->broker_rejected) {
        return COMFOAIR_PHASE_BROKER_REJECTED;
    }
    if (in->bus_started && in->bus_silent) {
        return COMFOAIR_PHASE_BUS_SILENT;
    }
    if (in->broker_configured && !in->broker_connected) {
        return COMFOAIR_PHASE_NO_BROKER;
    }
    // Unconditional, and that is a property the suite leans on: it is what makes "there is no
    // phase" unreachable for every combination of inputs.
    return COMFOAIR_PHASE_ONLINE;
}

bool comfoair_phase_connack_is_refusal(uint32_t connack)
{
    // Stated as the two codes that are NOT a refusal, because the interesting half of the rule is
    // the open end: everything outside 0..5 is a refusal too. Why 3 is excluded and 1 and 2 are
    // not: comfoair_phase.h:172-181.
    return connack != 0u && connack != 3u;
}

const char *comfoair_phase_name(device_phase_t phase)
{
    switch (phase) {
    case COMFOAIR_PHASE_BOOT:            return "boot";
    case COMFOAIR_PHASE_OTA:             return "ota";
    case COMFOAIR_PHASE_TRIAL:           return "trial";
    case COMFOAIR_PHASE_SETUP_AP:        return "setup-ap";
    case COMFOAIR_PHASE_RECOVERY_AP:     return "recovery-ap";
    case COMFOAIR_PHASE_FAULT:           return "fault";
    case COMFOAIR_PHASE_NET_REJECTED:    return "net-rejected";
    case COMFOAIR_PHASE_NET_LOST:        return "net-lost";
    case COMFOAIR_PHASE_SETUP_EXPIRED:   return "setup-expired";
    case COMFOAIR_PHASE_CONNECTING:      return "connecting";
    case COMFOAIR_PHASE_BROKER_REJECTED: return "broker-rejected";
    case COMFOAIR_PHASE_BUS_SILENT:      return "bus-silent";
    case COMFOAIR_PHASE_NO_BROKER:       return "no-broker";
    case COMFOAIR_PHASE_ONLINE:          return "online";
    case COMFOAIR_PHASE_COUNT:           break;
    }
    // Never NULL and never a fabricated meaning, the same rule as comfoair_wire_connack_name: a
    // number that is not a phase is reported as one, not guessed at.
    return "unknown";
}
