// The harness both halves of this suite drive the ladder with.
//
// A HEADER IN THE SUITE DIRECTORY, the same shape as test/test_phase/phase_inputs.h and for the
// same reason: PlatformIO links every .cpp in a directory into one binary, so the cases can be
// split across files for the 600-line ceiling (AGENTS.md) without the helpers being written twice.
// A second copy of `healthy()` is a second definition of what a working device looks like, and
// those drift.
//
// NOT ONE ESP-IDF HEADER, here or in either half: the pass owns no clock, no socket and no client
// handle, which is why "an address came back after two minutes" is a case at all.
#pragma once

#include <unity.h>

#include <stdio.h>

#include "comfoair_mqtt_ladder.h"

// The table of docs/interfaces/mqtt.md (Reconnect and backoff), transcribed and never read back
// from the implementation.
static const uint32_t FAST_FIRST_MS = 2000u;
static const uint32_t DROP_FIRST_MS = 1000u;
static const uint32_t FLAT_MS       = 300000u;
// The ladder resets after sixty seconds of SESSION, not on a connection that happened.
static const uint32_t SETTLED_MS    = 60000u;
// The table's saturation point, and the clamp the publisher's side must apply to its counter.
static const uint32_t SATURATES_AT  = 16u;
// One "MAC address"; the spread across seeds is proved next door.
static const uint32_t SEED          = 0x24a160ffu;

// A device that is doing well: a client exists, a host is stored, an address is up, and the event
// handler has recorded nothing. Every case below starts here and spoils exactly what it is about.
static inline comfoair_mqtt_ladder_in_t healthy(uint32_t now_ms)
{
  comfoair_mqtt_ladder_in_t in{};
  in.now_ms      = now_ms;
  in.client      = true;
  in.configured  = true;
  in.connected   = false;
  in.has_address = true;
  in.seed        = SEED;
  in.kind        = COMFOAIR_MQTT_ERR_NONE;
  // Nothing has failed yet, so this is also a device whose client is still dialling by itself.
  in.failed = false;
  return in;
}

// One second of the publisher task, TICK_MS apart, which is the resolution the real one runs at
// (comfoair_mqtt.c:20). Returns the pass's output so a case can look at any single tick.
static inline comfoair_mqtt_ladder_out_t tick(comfoair_mqtt_ladder_t *st, comfoair_mqtt_ladder_in_t *in)
{
  comfoair_mqtt_ladder_out_t out{};
  comfoair_mqtt_ladder_pass(st, in, &out);
  in->now_ms += 1000u;
  return out;
}

// Runs passes until one asks for a reconnect, and answers with the millisecond it happened on.
// Fails rather than hangs: a machine that never dials again is exactly the defect the never-ending
// retry forbids (AGENTS.md), so it must be a red test and not a stuck one.
static inline uint32_t run_until_dial(comfoair_mqtt_ladder_t *st, comfoair_mqtt_ladder_in_t *in,
                               uint32_t budget_ticks, const char *what)
{
  for (uint32_t n = 0; n < budget_ticks; n++) {
    const uint32_t at = in->now_ms;
    if (tick(st, in).reconnect) {
      return at;
    }
  }
  char msg[160];
  snprintf(msg, sizeof msg, "%s: no attempt was made in %u passes", what, (unsigned)budget_ticks);
  TEST_FAIL_MESSAGE(msg);
  return 0;
}

// The failure the event handler would have recorded for a broker that cannot be reached.
static inline void transport_failure(comfoair_mqtt_ladder_in_t *in, bool unresolved)
{
  in->kind            = COMFOAIR_MQTT_ERR_TRANSPORT;
  in->name_unresolved = unresolved;
  in->connack         = 0u;
  in->was_connected   = false;
  in->failed          = true;
}

// The failure a dropped session leaves: the row one rung lower, whether or not an error event came
// with it -- a keepalive timeout produces none.
static inline void dropped_session(comfoair_mqtt_ladder_in_t *in)
{
  in->connected     = false;
  in->was_connected = true;
  in->failed        = true;
}
