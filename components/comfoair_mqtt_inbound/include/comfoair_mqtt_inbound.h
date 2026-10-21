// What an arriving MQTT message MEANS, with none of what follows from it.
//
// PURE, and its OWN COMPONENT rather than a second source file inside comfoair_mqtt -- the fourth
// member of the same family as comfoair_command against comfoair_control, comfoair_ha against the
// publisher and comfoair_wire against comfoair_http. The deciding half needs no broker, no
// FreeRTOS task, no network and no flash, so it can be exercised by a host test, and until it left
// what is now comfoair_mqtt_handle_inbound() it was not exercised at all. The branching below is
// exactly the logic where a regression is silent: a command quietly stops being applied, and nobody
// learns until the owner presses a button that does nothing.
//
// THE DIRECTORY IS PART OF THAT, not a filing preference. PlatformIO's dependency finder works at
// component granularity (platformio.ini, [env:native]), so a component is host-buildable or it is
// not; leaving this file inside comfoair_mqtt made that component half of each and needed a
// per-source filter nothing pointed at. DO NOT move it back and reach for one again.
//
// NOTHING HERE HAS AN EFFECT. No comfoair_control_*, no comfoair_state_*, no logging, no counters
// and no flags. The caller -- comfoair_mqtt_handle_inbound() in comfoair_mqtt_command.c, on the
// publisher task -- reads the decision and performs it, in the order that file states and pins.
#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "comfoair_command.h"
#include "comfoair_ha.h"
#include "comfoair_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// A command body. The six operations carry JSON; an entity command is a scalar.
#define COMFOAIR_MQTT_PAYLOAD_MAX 512
#define COMFOAIR_MQTT_TOPIC_MAX   192

// An entity key or an operation name. 64 because the longest key the generated registry carries is
// 33 bytes ("filter_replacement_remaining_days") and the longest operation name 21
// ("set_ventilation_level"); it is also the cap handed to comfoair_ha_parse_topic, which REFUSES a
// longer name rather than shortening one, so this number decides which topics exist.
#define COMFOAIR_MQTT_KEY_MAX 64

// An option LABEL from the registry -- the only part of a payload that has to outlive the call.
//
// DERIVED, NOT CHOSEN: the longest option label in registry_generated.h is 18 bytes
// ("Manual (permanent)", operating_mode_options). 32 leaves room for a
// table that grows, and test_mqtt_inbound sweeps every label in the registry and fails if one ever
// does not fit -- so this stays provable rather than remembered. Re-derive it the same way if the
// sweep ever fails; do NOT raise it back to the payload size, which is what cost this struct 512
// bytes of a 4 KB task's stack.
#define COMFOAIR_MQTT_LABEL_MAX 32

// One incoming message, copied out of esp-mqtt's receive buffer by the event handler.
//
// `topic` is NOT NUL-terminated by the broker -- esp-mqtt hands over a pointer and a length into
// its own buffer -- so the length travels beside it and comfoair_ha_parse_topic reads by length.
// `payload` IS terminated, because the event handler copies it into a zeroed struct and refuses
// anything that would fill the buffer.
typedef struct {
    char   topic[COMFOAIR_MQTT_TOPIC_MAX];
    size_t topic_len;
    char   payload[COMFOAIR_MQTT_PAYLOAD_MAX];
    bool   retained;
} comfoair_mqtt_inbound_t;

typedef enum {
    // Not our tree. SILENTLY, and NOT a refusal: a broker is shared, and counting somebody else's
    // topic against this device's rejection counter turns a healthy household into a fault report.
    COMFOAIR_MQTT_IGNORE,
    // Ours, and refused. `reason` says why and is safe to log.
    COMFOAIR_MQTT_REJECT,
    // Submit `command`; then, if optimistic_text is set, apply the label. THAT ORDER -- see below.
    COMFOAIR_MQTT_SUBMIT,
    // Store the duration, CHECK THE RESULT, and only then write it to the state model.
    COMFOAIR_MQTT_STORE,
} comfoair_mqtt_decision_kind_t;

typedef struct {
    comfoair_mqtt_decision_kind_t kind;
    // REJECT only, NULL otherwise. Always a string literal and NEVER the payload: a command
    // payload carries whatever anybody on the LAN published, and the log ring is served by
    // GET /api/log.
    const char *reason;
    // SUBMIT and STORE. On STORE it carries no frame at all -- the unit is told a duration only as
    // part of a command -- and `duration_key` and `duration_minutes` are what the caller stores.
    comfoair_command_t command;
    // SUBMIT only: apply comfoair_state_set_option(key, text) AFTER the submit succeeded.
    // Optimistic only where nothing will ever confirm the change; comfoair_state_set_option
    // declines a label the registry does not list under that entity, which is the intended answer.
    bool               optimistic_text;
    // The entity this addresses. Set on SUBMIT and on STORE, empty otherwise. On STORE it repeats
    // command.duration_key, which is the one the wrapper actually stores by -- they are the same
    // registry key, and a caller should prefer duration_key there because it is the string the
    // command was built from.
    char               key[COMFOAIR_MQTT_KEY_MAX];
    // The option label, as a COPY, when optimistic_text is set; empty otherwise. `in` belongs to
    // the caller and need not outlive the decision.
    // DO NOT turn this back into a `const char *` into the inbound buffer: that aliasing was safe
    // only while deciding and acting happened inside one function, which is no longer true. It is
    // a label and not a payload -- see COMFOAIR_MQTT_LABEL_MAX for why that is a bound and not a
    // hope.
    char               text[COMFOAIR_MQTT_LABEL_MAX];
} comfoair_mqtt_decision_t;

// Decides what `in` means. No argument is retained and nothing is written outside `out`.
//
// RETURNED BY VALUE, and the caller must be able to afford it. MEASURED, not estimated: the struct
// is 168 bytes on riscv32 and this function's own frame 352 (riscv32-esp-elf-gcc -Os, nm -S and
// -fstack-usage). comfoair_mqtt's publisher task has a 4 KB stack that also carries a 1536-byte
// publish buffer, so this is not free. It was 648 bytes while `text` held a whole payload instead
// of a label -- anything that makes this struct grow again is a stack question on that task first
// and a style question second.
//
// `durations` and `fan` are the caller's snapshot of state this function must not fetch itself.
// The rule that combines them lives HERE and not in the caller, because it is a decision and the
// test has to be able to exercise it: turning the fan on resumes the level the unit is actually
// running at, and only a fan that is `known` and `on` may override the stored default. A NULL
// `fan` means nothing has been reported.
//
// A NULL `ctx` or `in` yields IGNORE rather than a dereference: this runs on a message from
// anything that can reach the broker.
comfoair_mqtt_decision_t comfoair_mqtt_decide(const comfoair_ha_ctx_t *ctx,
                                              const comfoair_mqtt_inbound_t *in,
                                              const comfoair_command_durations_t *durations,
                                              const comfoair_fan_state_t *fan);

// A scalar that has to BE a number. Exposed so the test can pin it directly: strtod("hello") is 0,
// and 0 on the fan is the AWAY level, so a garbage payload would otherwise put the house into Away.
// Returns false for an empty string, for text, and for a number with anything after it.
bool comfoair_mqtt_parse_number(const char *text, double *out);

#ifdef __cplusplus
}
#endif
