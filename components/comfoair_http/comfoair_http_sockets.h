#pragma once

#include "sdkconfig.h"  // CONFIG_LWIP_MAX_SOCKETS, the table this file budgets

// The lwIP socket budget. lwIP has ONE descriptor table for the whole firmware,
// CONFIG_LWIP_MAX_SOCKETS long (sdkconfig.defaults), and esp_http_server's start-up check
// (max_open_sockets + 3 <= LWIP_MAX_SOCKETS, httpd_main.c:502) only proves that httpd fits on its
// own. It reserves nothing. Every other socket the firmware holds comes out of the same table, and
// when the table is full lwIP refuses the accept() with ENFILE (23) BEFORE httpd's LRU purge can
// help: httpd only purges once all max_open_sockets of its own sessions are taken, and an accept
// that fails one short of that never gets there. That is how the device stopped answering at the
// seventh browser connection with "httpd_accept_conn: error in accept (23)" filling the log --
// the budget was written as 7 + 3 = 10 and forgot the two lines below.
//
// The sum of the four terms below is checked by a _Static_assert beside cfg.max_open_sockets in
// comfoair_http.c, and tests/python/test_socket_budget.py fails when a source file opens a socket
// without being named here. A NEW SOCKET USER IS ADDED HERE, with its file, or the guard is lying.

// esp_http_server's client sockets, cfg.max_open_sockets -- HTTP and WebSocket together. Named
// once, here, so the server's setting, this budget and the MAX_WS_CLIENTS check in comfoair_http.c
// cannot disagree. A browser tab holding a WebSocket plus its own fetches is already two.
#define COMFOAIR_HTTP_CLIENT_SOCKETS 7

// esp_http_server's own: the listening TCP socket, the UDP control socket bound to ctrl_port and
// the UDP socket it sends control messages from (httpd_main.c, httpd_server_init). All three are
// held for the life of the server.
#define COMFOAIR_HTTPD_INTERNAL_SOCKETS 3

// Everything else that can be open at the same moment as a full house of HTTP clients:
//   comfoair_mqtt_client.c        1 TCP -- esp-mqtt's broker connection. One at a time: the old
//                                         client is stopped and destroyed before a new one is
//                                         initialised, and esp-mqtt closes its transport before
//                                         it redials.
//   comfoair_captive_dns_server.c 1 UDP -- the captive DNS responder on port 53. Runs whenever
//                                         the access point is on the air, and in AP+STA (the
//                                         setup window on a provisioned device) that is WHILE
//                                         MQTT is connected, so it is counted, not hoped away.
// Deliberately NOT counted, because they use lwIP raw pcbs and never take a descriptor: mDNS
// (espressif/mdns in its default LWIP networking mode, CONFIG_MDNS_NETWORKING_SOCKET unset), the
// DNS resolver behind getaddrinfo(), the DHCP client and server. The firmware runs no SNTP and no
// HTTP or WebSocket client.
#define COMFOAIR_OTHER_LWIP_SOCKETS 2

// One more than the count, so a socket that is closing while its replacement opens -- or a socket
// user nobody has counted yet -- costs a log line somewhere rather than the web UI.
#define COMFOAIR_LWIP_SOCKET_SPARE 1
