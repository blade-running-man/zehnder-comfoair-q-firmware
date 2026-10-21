// The socket that carries comfoair_captive_dns_reply() to a phone. Nothing decides anything
// here; read comfoair_captive_dns.h for what is actually being answered and why.
//
// Separate header, and separate translation unit, for the reason comfoair_config_nvs.h is:
// test/test_captive builds every source in this component the moment it includes one of its
// headers (platformio.ini, lib_extra_dirs), and a socket does not exist on the host. What can be
// decided without lwip is decided next door, where the suite reaches it for real.
#pragma once

#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts answering name lookups addressed to `address` on port 53.
//
// `address` is the access point's own address, four octets in wire order --
// esp_netif_get_ip_info() on the AP netif, then memcpy of ip.addr, which lwip already stores
// that way round. The socket BINDS to it rather than to INADDR_ANY, and that is a rule and not
// a detail: the provisioning trial and the access-point fallback (docs/device/networking.md) both
// run the radio in AP_STA, so a socket on 0.0.0.0 would also answer every name asked on the
// OWNER'S network -- one misconfigured resolver in the house and this device is hijacking the
// household's DNS from inside the wall.
// The lie belongs on the access point and nowhere else.
//
// FOR ANY OF THIS TO BE ASKED A SINGLE QUESTION, the access point's DHCP server has to hand out
// this address as the client's resolver. On this build it already does, and by accident rather
// than by configuration: `dhcps->dhcps_dns = 0x00` (dhcpserver.c:176) leaves the explicit DNS
// offer off, and the `#ifdef CONFIG_LWIP_DHCPS_ADD_DNS` branch at dhcpserver.c:486-489 then
// offers the server's own address instead -- and sdkconfig.m5stack-nanoc6:2855 has that option
// on. So: turning CONFIG_LWIP_DHCPS_ADD_DNS off kills the captive portal silently, with nothing
// in this component to show for it. If it ever has to be set deliberately, it is the three calls
// from the IDF's own softap_sta example (softap_sta.c:171-177): dhcps_stop, dhcps_option with
// ESP_NETIF_DOMAIN_NAME_SERVER = DHCPS_OFFER_DNS, esp_netif_set_dns_info, dhcps_start.
//
// Returns ESP_ERR_INVALID_STATE if it is already running, ESP_ERR_INVALID_ARG on a NULL address,
// ESP_FAIL if the socket cannot be opened or bound, ESP_ERR_NO_MEM if the task cannot be
// created. It never aborts and never reboots: a captive portal that cannot start is a device the
// owner reaches by typing an address, and a device that reboots is a device the owner does not
// reach at all (AGENTS.md).
esp_err_t comfoair_captive_dns_start(const uint8_t address[4]);

// Asks the task to finish and waits, briefly, for it to. Safe to call when nothing is running.
// Call it when the access point goes off the air -- an answer for every name in existence is
// correct only while the network it is served on has nothing else in it.
void comfoair_captive_dns_stop(void);

#ifdef __cplusplus
}
#endif
