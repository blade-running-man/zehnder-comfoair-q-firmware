// The JSON projection of the state model.
//
// One renderer, three consumers: REST serves it whole, the WebSocket pushes it, and MQTT
// will publish its fields. AGENTS.md criterion 3 requires REST and MQTT to cover exactly the same
// ground; they can only do that by rendering from here rather than each formatting its own.
//
// Everything writes into a caller-supplied buffer and never allocates: this runs on a task
// with a fixed stack, and a state document that grows with the entity table must fail by
// truncating, not by exhausting the heap.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Renders the whole state as one JSON object, for GET /api/state. Returns the number of bytes
// written, excluding the terminator, or 0 when the document did not fit (the buffer is still
// NUL-terminated and holds a truncated prefix that must not be sent).
//
// Leaves every change mark alone. A REST reader is not a push client, so this rendering has
// delivered nothing the WebSocket clients are owed; clearing the PUSH marks here would let any
// poll take a change from every open socket before the next push round sent it.
size_t comfoair_api_render_state(char *out, size_t cap);

// Renders only what has changed since the last push rendering (this, or comfoair_api_render_push
// of either kind), as the same shape as the state document. Returns "{}" when nothing moved.
//
// Rendering CLEARS the change marks OF THE PUSH CONSUMER, and of nothing else. This used to clear
// a single shared array, which meant a second reader -- MQTT, when phase 6 arrived -- would take
// updates this one never saw, silently and in whichever direction happened to render first.
// comfoair_state now keeps one mark per consumer (comfoair_state_consumer_t); there is still one
// push channel by design, but the model no longer assumes it.
size_t comfoair_api_render_delta(char *out, size_t cap);

// Renders one message for the push socket: {"type":"state"|"delta","values":{...}}.
//
// The envelope is not decoration. The socket carries a full state, a delta and a keep-alive,
// and a client has to tell them apart without inferring it from size or content -- the kind
// of guess that works until the day it does not. Returns 0 when a delta has nothing in it,
// so the caller knows there is nothing worth sending; a full state always renders.
//
// Both kinds CLEAR the PUSH consumer's marks: a client just handed the whole state must not
// get a delta repeating it next round. The caller is the one push task; this rendering is what
// it sends to every WebSocket client.
size_t comfoair_api_render_push(char *out, size_t cap, bool full);

// Renders ONE entity -- its value and the metadata a client needs to display it -- for
// GET /api/entities/{key}. Returns 0 and writes nothing when no such entity exists, which
// is how the caller tells "unknown entity" (404) from "known, no reading yet" (200 with a
// null value). Those are different answers and conflating them hides a dead bus.
size_t comfoair_api_render_value(const char *key, char *out, size_t cap);

// Renders the entity registry as a JSON array -- what a client needs to display or drive
// an entity it has never heard of.
size_t comfoair_api_render_entities(char *out, size_t cap);

// The size of the one buffer every JSON document is rendered into (comfoair_http.c g_http_scratch).
// Sized for the largest document, GET /api/entities, which grows by about 275 B per registry
// row. test_api pins "at least a quarter spare"
// against the generated registry, so a table that outgrows it fails on the host, not at boot.
// DO NOT size a caller's buffer with a literal: the 32768s this replaced drifted from it.
#define COMFOAIR_API_DOC_BUFFER_SIZE 49152

// Escapes one string into JSON. Bytes above ASCII are emitted as \uXXXX rather than passed
// through, so the output is valid regardless of what the consumer assumes about encoding.
size_t comfoair_api_escape_json(const char *in, char *out, size_t cap);

#ifdef __cplusplus
}
#endif
