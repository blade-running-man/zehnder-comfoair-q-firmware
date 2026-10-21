// WHEN THE LINK ON THE HOME ASSISTANT DEVICE CARD HAS TO BE WRITTEN AGAIN. Three cases arrive at
// it independently: a router that reboots, an owner who does not know the address, a power cut.
//
// `configuration_url` is the one member of comfoair_ha_ctx_t whose value is not known at the
// moment the client is built. comfoair_mqtt_start_client() reads comfoair_net_ip_string() once and
// derives the url from it; app_main calls that function immediately after comfoair_http_start(),
// before the radio has associated -- and esp-mqtt does not wait for a network, it dials and
// retries on its own. So on every power cut the device block is published with the member ABSENT,
// and the old code never wrote it again: the card in Home Assistant has no way back to the
// device's own page, for the life of the installation.
//
// A THIRD LODGER IN comfoair_mqtt_backoff, and for the only property this directory has that
// comfoair_mqtt does not: IT BUILDS ON THE HOST. The same finding put comfoair_retract_budget.c
// and comfoair_discovery_gate.c here, one task apart -- a condition written inside comfoair_mqtt
// is checked by nothing at all, and in both of those cases deleting it altogether left every host
// case green. It shares no line with the ladder and costs this component nothing, because like the
// other two it needs nothing at all.
//
// THE CONDITION IS "THE URL MOVED" AND NOT "THE URL IS EMPTY", and that is the whole content of
// this file. Both answer the boot case; only the first answers the second one, which is a router
// that reboots and hands out a different lease. "Empty" freezes the card on the first address the
// device ever had and leaves the owner with a link to somebody else's kettle. And both must be
// false on a pass that changed nothing: the caller is comfoair_mqtt_retry_pass(), which runs on
// every wake of the publisher task -- several times a second on a busy CAN bus -- and a true costs
// one retained config document per entity -- the whole discovery tree, the largest burst this
// device sends (HV-HA-03 measures it on a broker).
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The buffer a caller must give comfoair_device_url_refresh(), terminator included.
 *
 * EXACTLY THE LONGEST URL THIS CAN PRODUCE and not a round number: "http://" is 7,
 * "255.255.255.255" is 15, the trailing slash is 1 and the terminator is 1. One byte less and the
 * whole 255.255.255.255 half of the address space would be refused, silently and only there --
 * which is why the suite next door asserts this against `sizeof "http://255.255.255.255/"` rather
 * than against 24.
 *
 * IPv4 ONLY, deliberately: comfoair_net_ip_string() writes a `char[16]` dotted quad
 * (comfoair_net_query.c) and nothing else in this firmware has an IPv6 address to offer.
 */
#define COMFOAIR_DEVICE_URL_MAX 24

/**
 * Bring `url` up to date with the address the publisher pass has just read, and say whether the
 * device card now has to be published again.
 *
 * `url` is read AND written: it is the buffer comfoair_ha_ctx_t::url points into, so a rewrite
 * here is what the next discovery document carries. `ip` is what comfoair_net_ip_string()
 * answered -- a dotted quad, or an empty string when the device has no address.
 *
 * Returns true ONLY when the content of `url` changed, which is exactly when the retained config
 * documents on the broker have stopped describing this device. Every other answer is false:
 *
 *  * THE ADDRESS IS GONE (`ip` empty or NULL) -> the url is LEFT ALONE. A device with no address
 *    cannot publish anything at that moment anyway, so blanking would buy two full discovery
 *    sweeps for one Wi-Fi blip -- one for the card with no link, one for the card when the lease
 *    comes back -- and in between it would take the link away at exactly the moment the owner is
 *    trying to reach the device to find out what is wrong. The url is the LAST KNOWN address, and
 *    a household router almost always hands back the same one.
 *  * THE URL DOES NOT FIT -> the url is LEFT ALONE. snprintf's own answer to a short buffer is a
 *    truncation, and "http://192.168.1" is a link Home Assistant accepts, renders, and sends the
 *    owner nowhere. An ABSENT `cu` is the documented harmless state (comfoair_ha_payload.c omits
 *    the member for an empty url); a wrong one is a support call.
 *  * NOTHING MOVED -> false, and this is the property that keeps the fix from becoming a defect of
 *    its own. See the header comment above for what a true costs.
 *
 * PURE. No clock, no globals, no allocation; safe from any task. A caller with no buffer, or a
 * buffer of zero, is answered rather than dereferenced -- the rule comfoair_mqtt_backoff_seed()
 * already carries in this component.
 */
bool comfoair_device_url_refresh(char *url, size_t size, const char *ip);

#ifdef __cplusplus
}
#endif
