// One datagram in, one datagram out. Read comfoair_captive_dns.h for why this is a pure function
// over a buffer instead of a loop inside a socket handler; the short version is that the sender
// is anyone within radio range of an open access point, and every shape they can send has to be
// reachable from a test on a laptop.
#include "comfoair_captive_dns.h"

#include <stdbool.h>
#include <string.h>

// RFC 1035 4.1.1: id, flags, and four counts, two bytes each.
#define HEADER_LEN 12

// RFC 1035 2.3.4. The 255 is what bounds every size below it -- with it, the largest reply this
// file can produce is 12 + 259 + 16 = 287 bytes, which is why nothing here needs to think about
// truncation or about the 512-byte limit at all.
#define MAX_NAME 255

// Pointer, type, class, TTL, length, four octets of address.
#define ANSWER_LEN 16

#define TYPE_A   1
#define CLASS_IN 1

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

// How many bytes the question at `p` occupies, or 0 when it is not a question this file will
// read. `len` is what remains of the datagram, and it is carried into every step: the length
// bytes inside a DNS name are written by the sender, and the buffer is not.
static size_t question_len(const uint8_t *p, size_t len, uint16_t *qtype, uint16_t *qclass)
{
    size_t at   = 0;
    size_t name = 0;

    for (;;) {
        if (at >= len)
            return 0;  // the name has no terminating zero inside this datagram
        uint8_t label = p[at];
        // The top two bits are 00 for a label and 11 for a compression pointer; 01 and 10 have
        // never been assigned. All three non-label cases are refused here.
        //
        // The pointer is the one worth naming: following one while parsing is the decompression
        // loop, and 0xC00C -- a pointer at the name it is part of -- is two bytes to send. On a
        // single-core device that loop is not a crash but a ventilation unit that stops
        // answering its owner because a packet arrived. Pointers are legal in an ANSWER (RFC 1035
        // 4.1.4) and one is emitted below; they have no business in a question. DO NOT relax this
        // to be more permissive -- no resolver on any phone sends one.
        if ((label & 0xC0) != 0)
            return 0;
        at++;
        // The length byte counts towards the 255, which is what makes `name` the wire length and
        // not the printed one. No separate label-length check is needed: the two top bits being
        // zero already caps a label at 63.
        name += 1u + label;
        if (name > MAX_NAME)
            return 0;
        if (label == 0)
            break;
        // The label runs off the end of the datagram.
        //
        // DELIBERATELY REDUNDANT, and it is worth knowing that it is: `at` only ever grows, so an
        // overshoot here is caught by `at >= len` at the top of the next iteration, before p[at]
        // is read. Deleting these two lines changes no return value and breaks no test -- that
        // was measured, not assumed. It stays for two reasons. The refusal happens where the
        // reason is legible instead of one iteration later under a comment about missing
        // terminating zeros; and it is the bound that anything ever added here which actually
        // READS the label body (a comparison against a name, a copy into a buffer) would need to
        // already be in place. The loop-top check is the one that keeps the parse inside the
        // datagram, and it is the one the guard page in test/test_captive kills the mutant of.
        if (at + label > len)
            return 0;
        at += label;
    }

    if (at + 4 > len)
        return 0;  // a complete name with no room for the type and class that must follow it
    *qtype  = be16(p + at);
    *qclass = be16(p + at + 2);
    return at + 4;
}

size_t comfoair_captive_dns_reply(const uint8_t *query, size_t query_len, const uint8_t address[4],
                                  uint8_t *out, size_t out_cap)
{
    if (query == NULL || address == NULL || out == NULL)
        return 0;
    if (query_len < HEADER_LEN)
        return 0;

    // QR is set: this is already somebody's answer. Replying to an answer is how two of these
    // start trading packets -- our own replies leave from port 53, so a single datagram with a
    // spoofed source port 53 is enough to start it, and the access point becomes unusable while
    // both ends are behaving exactly as written.
    if ((query[2] & 0x80) != 0)
        return 0;
    // Opcode must be QUERY. NOTIFY and UPDATE are not name lookups, and a captive portal that
    // answers them has opinions about a protocol it does not speak.
    if ((query[2] & 0x78) != 0)
        return 0;
    // Exactly one question. Zero is a probe; more than one would have to be echoed in full and
    // answered in part, which is a shape no stub resolver sends and every hand-written parser
    // gets wrong.
    if (be16(query + 4) != 1)
        return 0;

    uint16_t qtype  = 0;
    uint16_t qclass = 0;
    size_t   qlen   = question_len(query + HEADER_LEN, query_len - HEADER_LEN, &qtype, &qclass);
    if (qlen == 0)
        return 0;

    // Everything else -- AAAA above all, which every phone asks for before the A -- gets a
    // well-formed NOERROR with no records: the name is here, it has nothing of that kind. It
    // would be less code to say nothing, and it would cost the client its resolver timeout on
    // the one page the owner is waiting for. Anything the additional section carried (an EDNS
    // OPT, on most real queries) is not echoed; a client that gets no OPT back falls back to
    // 512-byte UDP, which is already more than this file can produce.
    bool   answering  = qtype == TYPE_A && qclass == CLASS_IN;
    size_t reply_len  = HEADER_LEN + qlen + (answering ? ANSWER_LEN : 0);
    // Decided before the first byte is stored: a truncated DNS response is worse than none,
    // because the client accepts it and reads whatever the missing bytes used to be.
    if (reply_len > out_cap)
        return 0;

    out[0] = query[0];  // the id, echoed: it is what the client matches the reply against
    out[1] = query[1];
    // QR, AA, and the RD the client asked with. AA because this device is authoritative for the
    // whole namespace as far as anyone on this access point is concerned -- there is nobody else
    // to ask, and a resolver told otherwise goes looking for a second server that does not exist.
    out[2] = (uint8_t)(0x84 | (query[2] & 0x01));
    out[3] = 0x80;  // RA, and RCODE 0
    out[4] = 0;
    out[5] = 1;
    out[6] = 0;
    out[7] = answering ? 1 : 0;
    memset(out + 8, 0, 4);  // no authority, and no additional: the OPT is dropped

    // memmove, not memcpy: `out` is allowed to be the query's own buffer, and with out == query
    // memcpy would be a copy onto itself -- which the standard leaves undefined however harmless
    // it looks. The socket loop uses two separate buffers today, so this contract has no caller
    // in this repository; it has a test instead
    // (test_the_reply_may_be_built_on_top_of_the_query_it_answers), because a documented contract
    // with nothing exercising it is a comment, not a promise. The trap it comes with -- out_cap
    // must be the buffer's size and never query_len -- is on the declaration in the header.
    //
    // The question goes back BYTE FOR BYTE, including the case it arrived in. Resolvers randomise
    // the case of the name they ask for and drop a reply that comes back spelled differently --
    // it is the cheapest defence there is against a forged answer. DO NOT normalise it.
    memmove(out + HEADER_LEN, query + HEADER_LEN, qlen);
    if (!answering)
        return reply_len;

    uint8_t *rr = out + HEADER_LEN + qlen;
    rr[0]       = 0xC0;         // the name, as a compression pointer...
    rr[1]       = HEADER_LEN;   // ...at the question, which is always at offset 12
    rr[2]       = 0;
    rr[3]       = TYPE_A;
    rr[4]       = 0;
    rr[5]       = CLASS_IN;
    // TTL 0: use it once, do not cache it (RFC 2181 section 8). Ten minutes from now this phone
    // is on the owner's real network with our answer for connectivitycheck.gstatic.com still in
    // its resolver, pointing at an address that now belongs to a printer. DO NOT raise it to save
    // traffic -- the traffic is a handful of packets on a link with one client on it.
    memset(rr + 6, 0, 4);
    rr[10] = 0;
    rr[11] = 4;
    memcpy(rr + 12, address, 4);
    return reply_len;
}
