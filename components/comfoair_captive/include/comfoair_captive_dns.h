// The DNS half of the captive portal: every name asked for on our access point resolves to us.
//
// Without it, a phone on comfoair-XXXX has a network with no resolver: every name times out, the
// browser shows a dead page, and the only way to the setup form is an address the owner has to
// have been told. With it, anything typed into a real browser arrives at the setup page -- which
// is the ENTIRE discovery mechanism of this firmware, because comfoair_captive.h deliberately
// stops the OS from opening its own sign-in window (read that file first; the two halves are one
// decision).
//
// This function parses a packet from anyone in radio range. The access point is open on purpose
// (docs/architecture/security.md), so there is no authentication in front of it and no policy
// above it; it runs before the owner has ever touched the device, and it is reachable from the
// pavement. That is why the parse is a pure function over a buffer and a length instead of a loop
// inside a socket handler -- every malformed shape in test/test_captive is a packet somebody can
// actually send, and none of them needs a radio to reproduce.
//
// What it will not do, each for a reason recorded in test/test_captive:
//  * follow a compression pointer in a question (the decompression loop; a single-core device
//    that loops here stops answering the owner),
//  * reply to anything already carrying QR (two of these on one channel would trade packets for
//    ever, and a spoofed source port 53 is enough to start it),
//  * emit a partially written datagram,
//  * or read one byte past `query_len`, whatever the length bytes inside the packet claim -- and
//    that last one is checked rather than asserted: the suite puts every datagram against an
//    unmapped page, so a read past the length faults instead of returning a plausible zero.
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define COMFOAIR_CAPTIVE_DNS_PORT 53

// Classic DNS over UDP, before EDNS negotiates anything larger (RFC 1035 section 4.2.1). It is
// the size of the buffer the socket loop receives into (comfoair_captive_dns_server.c), and with
// a name bounded at 255 bytes the largest answer this file can build is 12 + 259 + 16 = 287, so
// a reply always fits in it. A client that offered EDNS gets no OPT back and falls back to this
// number, which is why not echoing the OPT costs nothing.
//
// IT IS NOT A CHECK ON THE QUERY, and this used to claim it was. comfoair_captive_dns_reply()
// never looks at query_len against this number: a 900-byte datagram whose question sits in the
// first thirty bytes is parsed and answered normally, and the only reason nothing that long
// arrives is that lwip drops what does not fit in the socket loop's buffer. DO NOT add the check
// to make the code match the old sentence -- the function is pure over a buffer and a length, the
// buffer belongs to the caller, and the next caller may have a bigger one. Pinned by
// test_the_512_is_the_socket_loops_buffer_and_not_a_check_in_here.
#define COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE 512

// Builds the reply to one datagram. Returns the number of bytes to send, or 0 for "say nothing
// at all" -- which is the answer to every packet that is not a question we can read.
//
// `address` is the four octets of the access point's own address, in the order they go on the
// wire. The caller owns it: this component holds no esp_netif and no hard-coded 192.168.4.1, so
// moving the DHCP range cannot leave a resolver pointing at the wrong host. lwip already stores
// ip4_addr_t.addr in network order, so `memcpy(octets, &info.ip.addr, 4)` is the whole bridge.
//
// `out` may BE the query's own buffer, not merely the same size as it -- which is why the
// question is moved with memmove and not memcpy. `out_cap` is then the size of that buffer and
// NOT `query_len`: the reply to an A question is exactly sixteen bytes longer than the question,
// so out_cap = query_len returns 0 for every lookup and the portal answers nothing, silently,
// because 0 is also what a malformed packet returns. Pinned by
// test_the_reply_may_be_built_on_top_of_the_query_it_answers.
//
// The reply is never larger than the query plus sixteen bytes, and never larger than
// COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE.
size_t comfoair_captive_dns_reply(const uint8_t *query, size_t query_len, const uint8_t address[4],
                                  uint8_t *out, size_t out_cap);

#ifdef __cplusplus
}
#endif
