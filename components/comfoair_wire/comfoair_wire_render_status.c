// comfoair_wire: the provisioning/status document (GET /api/provision, GET /api/status, the
// POST /api/provision reply) and its nested mqtt block, the only nested member any document has.
// Read comfoair_wire.h first; the component's shape is in README.md, Implementation.
// The two rules in comfoair_wire.c govern this file: no secret is rendered, and no submitted
// value is quoted in an error.
#include "comfoair_wire.h"

#include "comfoair_wire_internal.h"

// --- GET /api/provision: the status document ----------------------------------------------

// The broker's counters, as the last member of the status document. LAST ON PURPOSE and not for
// tidiness: it is the only nested object the document has, and comfoair_json_check() stops at the
// first one it meets (comfoair_json.cpp:381-383), so every flat field ahead of it stays reachable
// to the reader that walks the document from the front.
//
// DO NOT give this an "omitted means zero" form. The caller passing NULL is saying nothing is known
// about a broker, which is a different fact from every counter being zero -- see the note on
// comfoair_wire_render_provision in the header.
static void put_mqtt(writer_t *w, const comfoair_wire_mqtt_t *mqtt)
{
    put(w, ",");
    put_key(w, "mqtt");
    put(w, "{");
    // Stored, NOT reached. An owner whose broker has never answered sees configured true beside
    // connected false, and that pair is the diagnosis; one flag covering both would erase it.
    put_key(w, "configured");
    put_bool(w, mqtt->configured);
    put(w, ",");
    put_key(w, "connected");
    put_bool(w, mqtt->connected);
    put(w, ",");
    put_key(w, "published");
    put_u32(w, mqtt->published);
    put(w, ",");
    put_key(w, "commands");
    put_u32(w, mqtt->commands);
    put(w, ",");
    put_key(w, "rejected");
    put_u32(w, mqtt->rejected);
    put(w, ",");
    put_key(w, "reconnects");
    put_u32(w, mqtt->reconnects);
    put(w, ",");
    // The number and the sentence together, and the sentence comes from the same function the MQTT
    // log line prints (comfoair_mqtt_client.c:126). That is the whole point of the pair: an owner
    // who mistyped a broker password was told "broker unreachable" every ten seconds, and the
    // refusal that says otherwise was reachable only with a serial cable until this member existed.
    put_key(w, "last_connack");
    put_u32(w, mqtt->last_connack);
    put(w, ",");
    put_key(w, "last_connack_reason");
    put_string(w, comfoair_wire_connack_name(mqtt->last_connack));
    // --- and the ladder's, which are added AFTER the eight above and never among them ---------
    //
    // The order of the eight is a contract with a consumer that walks this block from the front,
    // and it has been on the wire since b068b8c. DO NOT tidy a new member into the group it reads
    // best beside; test_the_new_fields_come_after_the_existing_eight is what makes that a red test
    // instead of a discovery somebody else makes later.
    put(w, ",");
    // "link" and NOT "state": the top-level member of this document called `state` is the
    // provisioning machine's, and two members named the same meaning different things is a
    // prepared confusion. `failure` IS duplicated by name -- the
    // Wi-Fi's above, the broker's here -- and that is why the pair has to be told apart by
    // position, which is the one thing a nested block gives for free.
    put_key(w, "link");
    put_string(w, comfoair_mqtt_link_name(mqtt->link));
    put(w, ",");
    put_key(w, "failure");
    put_string(w, comfoair_mqtt_fail_name(mqtt->failure));
    put(w, ",");
    put_key(w, "attempts");
    put_u32(w, mqtt->attempts);
    put(w, ",");
    // The same sentinel and the same writer `fallback_ap_in_ms` uses: one value, one spelling.
    put_key(w, "retry_in_ms");
    put_duration(w, mqtt->retry_in_ms);
    put(w, ",");
    put_key(w, "subscriptions");
    put_string(w, comfoair_mqtt_subs_name(mqtt->subscriptions));
    put(w, ",");
    put_key(w, "disconnects");
    put_u32(w, mqtt->disconnects);
    put(w, ",");
    put_key(w, "applied_epoch");
    put_u32(w, mqtt->applied_epoch);
    put(w, ",");
    // 0 is "none recorded" and is a NUMBER, never null: a timed-out connect dispatches no error
    // event at all, so the zero is by construction and a reader has to be able to tell it from a
    // firmware that does not report the field (see the header).
    put_key(w, "last_transport_errno");
    put_u32(w, mqtt->last_transport_errno);
    put(w, "}");
}

size_t comfoair_wire_render_provision(const comfoair_wire_provision_t *status,
                                      const comfoair_wire_mqtt_t *mqtt,
                                      const comfoair_wire_device_t *device, char *out, size_t cap)
{
    if (status == NULL)
        return 0;
    writer_t w = {out, cap, 0, false};

    put(&w, "{");
    put_key(&w, "state");
    put_string(&w, comfoair_wire_state_name(status->state));
    put(&w, ",");
    put_key(&w, "mode");
    put_string(&w, comfoair_wire_mode_name(status->mode));
    put(&w, ",");
    // The aggregate, beside the machine state rather than instead of it: `state` answers "what is
    // the provisioning machine doing", `phase` answers "what is this device doing", and a device
    // that is `connected` and `bus-silent` at once is why both are here. FLAT AND AHEAD OF THE
    // mqtt BLOCK, like every other field: comfoair_json_check() stops at the first nested value it
    // meets, so a member behind the block is a member that reader cannot reach.
    //
    // The word is comfoair_phase_name()'s and is never invented here -- the same rule as `failure`
    // below and `mode` above, and the reason a colour on the device and a word in this document
    // cannot come to disagree.
    put_key(&w, "phase");
    put_string(&w, comfoair_phase_name(status->phase));
    put(&w, ",");
    // Claimed or not: the answer the access policy acts on. Published so that "why will it not let
    // me save anything" has an answer that does not need a serial cable.
    put_key(&w, "provisioned");
    put_bool(&w, status->provisioned);
    put(&w, ",");
    put_key(&w, "has_credentials");
    put_bool(&w, status->has_credentials);
    put(&w, ",");
    put_key(&w, "wifi_known_good");
    put_bool(&w, status->has_known_good);
    put(&w, ",");
    put_key(&w, "connected");
    put_bool(&w, status->connected);
    put(&w, ",");
    put_key(&w, "wifi_ssid");
    put_string(&w, status->ssid);
    put(&w, ",");
    put_key(&w, "ip");
    put_string(&w, status->ip);
    put(&w, ",");
    put_key(&w, "ap_ssid");
    put_string(&w, status->ap_ssid);
    put(&w, ",");
    put_key(&w, "ap_on_air");
    put_bool(&w, status->ap_on_air);
    put(&w, ",");
    // A window closing has to be OBSERVABLE, or a network that silently
    // vanished is indistinguishable from a device that broke.
    put_key(&w, "setup_window_open");
    put_bool(&w, status->window_open);
    put(&w, ",");
    put_key(&w, "setup_window_remaining_ms");
    put_duration(&w, status->window_remaining_ms);
    put(&w, ",");
    put_key(&w, "fallback_ap_in_ms");
    put_duration(&w, status->fallback_in_ms);
    put(&w, ",");
    // Beside the other two clocks, because it is read from the same one in the same snapshot.
    // Without it "the house is still coming up" reads exactly like "it broke an hour ago" -- two
    // states that ask opposite things of the owner and that nothing else in this document
    // separates. Sixty-four bits: see the header on why 32 would report a fresh boot every 49.7
    // days.
    put_key(&w, "uptime_ms");
    put_u64(&w, status->uptime_ms);
    put(&w, ",");
    // The invariant, in the API where the owner can see it: four different answers instead of one
    // "it will not connect". The word is comfoair_provision's, so the REST
    // projection and the MQTT one cannot invent a second spelling.
    put_key(&w, "failure");
    put_string(&w, comfoair_prov_failure_name(status->failure));
    put(&w, ",");
    // The raw wifi_err_reason_t, for an owner looking one up. 0 when there is none -- the
    // no-address answer is the ABSENCE of an event rather than an event.
    put_key(&w, "failure_reason");
    put_u32(&w, status->failure_reason);
    put(&w, ",");
    // Beside the raw reason ON PURPOSE: the two are read together. 201 (no AP found) with a strong
    // signal is a different fault from 201 with none, and 0 with -85 dBm is the DHCP question. One
    // member closes both support requests, and no fifth Wi-Fi verdict is added for either.
    //
    // null, never 0, when the station is not associated -- 0 dBm is a legal reading and would
    // report a perfect signal on a device that is on no network at all.
    put_key(&w, "rssi");
    if (status->has_rssi)
        put_i32(&w, status->rssi);
    else
        put(&w, "null");
    // Who answered. Absent rather than empty when the caller has nothing to say, because a
    // consumer keys its record of this device on device_id and an empty one is not an identity.
    if (device != NULL) {
        put(&w, ",");
        put_key(&w, "device_id");
        put_string(&w, device->device_id);
        put(&w, ",");
        put_key(&w, "mac");
        put_string(&w, device->mac);
        put(&w, ",");
        put_key(&w, "sw_version");
        put_string(&w, device->sw_version);
    }
    // Absent rather than zeroed when the caller has nothing to say about a broker.
    if (mqtt != NULL)
        put_mqtt(&w, mqtt);
    put(&w, "}");

    return finish(&w);
}
