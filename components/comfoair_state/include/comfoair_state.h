// The state model: one value per entity, fed by PDO frames.
//
// This is the single model AGENTS.md puts underneath all three transports.
// REST, MQTT and the WebSocket read from here; none of them keeps a copy, and none of them
// knows anything about CAN.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "registry_generated.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    // "Not reported yet" and "reported as nothing pending" are the same to a consumer and
    // deliberately share one type: both mean there is no value to show. What they must NOT
    // be confused with is "no such entity", which is a failed lookup, not a value.
    COMFOAIR_VALUE_NONE = 0,
    COMFOAIR_VALUE_NUMBER,
    COMFOAIR_VALUE_BOOL,
    COMFOAIR_VALUE_TEXT,
} comfoair_value_type_t;

typedef struct {
    comfoair_value_type_t type;
    float       number;
    bool        boolean;
    // Points into the generated registry (a label) or into a buffer this component owns; never
    // freed, never the caller's -- except from comfoair_state_get_copy(), where it is the caller's
    // own buffer. A rendered text (PDO 230, the active errors) survives ONE later write only.
    const char *text;
} comfoair_value_t;

// The fan is not a scalar and must not be flattened into one: Home Assistant needs
// on/off, a speed and a preset, and a preset can legitimately be absent while the other
// two are known. Squeezing it into comfoair_value_t would have to drop one of them.
typedef struct {
    bool        known;   // false until PDO 65 has been seen at least once
    bool        on;
    uint8_t     speed;   // 1..3; stays 1 when off, because a fan cannot show speed 0
    const char *preset;  // NULL when the operating mode maps to no preset
} comfoair_fan_state_t;

void comfoair_state_reset(void);

// Always returns true; `known` says whether anything has been reported yet.
bool comfoair_state_get_fan(comfoair_fan_state_t *out);

// Applies one PDO frame to EVERY entity that claims that PDO -- there is at least one PDO
// (16) behind three entities at once, so this is one-to-many by construction.
// Returns false when no entity claims the PDO, which is a fact worth logging, not an error.
bool comfoair_state_apply_pdo(uint16_t pdo, const uint8_t *data, size_t len);

// false means "no entity with this key"; true with type == COMFOAIR_VALUE_NONE means
// "known entity, no value yet".
bool comfoair_state_get(const char *key, comfoair_value_t *out);

// comfoair_state_get() for a reader that uses a TEXT after it has unlocked: the text is copied into
// `text` (the caller's, at least COMFOAIR_STATE_TEXT_MAX + 1 bytes) INSIDE the same hold of the
// lock, and out->text then points at `text` -- for every text, registry labels included, so the
// caller never has to know which kind it was handed. Non-text values read exactly as from
// comfoair_state_get(). False for an unknown key, a NULL argument or a `cap` below
// COMFOAIR_STATE_TEXT_MAX + 1 -- refused by SIZE, never by the text stored today, so a buffer that
// fits a short text cannot fail on a live unit the day the text grows.
//
// WHY IT EXISTS: the model's rendered texts (PDO 230, the active errors) live in double buffers,
// and a pointer copied by comfoair_state_get() stays readable only while at most ONE further write
// lands; the MQTT publisher's render can be pre-empted for longer than that. DO NOT switch a reader
// that renders after unlocking back to comfoair_state_get(). REST and the WebSocket render under
// comfoair_lock() and need no copy. Any task; takes comfoair_lock().
bool comfoair_state_get_copy(const char *key, comfoair_value_t *out, char *text, size_t cap);

// --- entities no PDO ever reports back -----------------------------------------------------
//
// FOUR writable entities have no feedback of any kind and carry a cold-start default: the four
// durations, which the unit is never told except as part of a command. comfoair_state_reset()
// seeds the default, src/main.cpp restores the owner's value from NVS at boot, and
// comfoair_state_set_number() records every accepted write -- without it a duration reads null
// while the device is about to use it.
//
// THREE writable selects have no PDO but DO have feedback: temperature_passive, humidity_comfort
// and humidity_protection are read back from the unit over RMI by comfoair_probe_task
// (COMFOAIR_RMI_READBACK, at boot and every six hours) and land through
// comfoair_state_set_option(), as does each accepted write. They have NO default: they read
// COMFOAIR_VALUE_NONE until the read-back answers. A seeded factory default was dropped because the
// owner's unit held different values and the device published settings it had never read.
//
// THE COUNT OF DEFAULTED ENTITIES IS LOAD-BEARING. comfoair_ha_payload.c gives the <prefix>/bus
// availability topic to every entity WITHOUT a cold-start default and not of the
// DIAGNOSTIC_CONST or FRESHNESS kind: every entity except the 4 durations, schema_version and
// data_freshness is bus-backed. A select given a default would drop out of what Home Assistant
// greys out when the CAN bus dies -- an off-by-one here is an off-by-one there.
//
// "NO PDO" IS NOT "NO FEEDBACK". Five selects have no PDO of their own, but balance_mode is derived
// from PDOs 54 and 55 and ventilation_mode from 49 and 65 (comfoair_state_derived.cpp), so the unit
// reports those two back, and the three above come back over RMI. Only the durations are truly
// optimistic.
//
// OPTIMISTIC, and only where nothing can confirm. DO NOT reach for these to make a change appear
// instantly on an entity a PDO does carry: away_mode is PDO 337 bit 11 (the away timer), never heard
// between sweeps on the Q350 census -- whether it is broadcast on change is unconfirmed -- so the
// device requests PDO 337 about 1.5 s after every away command
// (comfoair_control, comfoair_command_readback_pdo()) and the answer confirms it. Showing AWAY for
// a command the unit refused is worse than showing it a second and a half late.
//
// Both refuse an entity the registry does not mark writable, and _set_option refuses a label the
// registry does not list -- what is stored is the REGISTRY's own string, because comfoair_value_t
// carries a pointer that "points into the generated registry; never freed, never copied", and a
// caller's buffer reaching it would be a dangling read at the next render.
bool comfoair_state_set_option(const char *key, const char *option_label);
bool comfoair_state_set_number(const char *key, float value);

// The longest text the model stores: Home Assistant's state limit (MAX_LENGTH_STATE_STATE, 255, in
// homeassistant/const.py). A longer state is refused by HA, so the model never holds one.
#define COMFOAIR_STATE_TEXT_MAX 255

// The unit's active errors, as the probe task last READ them over RMI (GETACTIVEERRORS): `any`
// drives COMFOAIR_UNIT_ERROR_ANY and `summary` COMFOAIR_UNIT_ERROR_TEXT (indices generated from
// tools/pdo_table.py UNIT_ERRORS). Stored exactly as handed: deciding which ids raise `any` is the
// caller's (comfoair_sysinfo's summary leaves informational ids such as 89 "Bypass is in manual
// mode" out of it, while the text still lists them) -- DO NOT classify here, or the rule lives in
// two places. Each row is marked only when its value changed (the text compared by content, not by
// pointer); returns true when either did. Refuses NULL and a summary longer than
// COMFOAIR_STATE_TEXT_MAX (false, nothing stored). Call it only for an ANSWERED read: an unanswered
// one keeps the last value, and before the first read both rows are COMFOAIR_VALUE_NONE.
// Any task; takes comfoair_lock().
bool comfoair_state_set_unit_errors(bool any, const char *summary);

const comfoair_entity_t *comfoair_entity_by_key(const char *key);

// Who is asking. ONE MARK PER CONSUMER, and that is not a generalisation for its own sake: the
// WebSocket delta renderer clears the marks it reads, so a second reader sharing them would take
// updates the first never saw. With MQTT arriving in phase 6 that is two consumers of one model,
// and the failure is silent and asymmetric -- a value reaches Home Assistant and never reaches the
// browser, or the reverse, depending on which task rendered first. comfoair_api.h used to say
// "two consumers cannot both be served from one buffer" and left it at that; this is the answer.
typedef enum {
    COMFOAIR_STATE_CONSUMER_PUSH,  // the WebSocket delta
    COMFOAIR_STATE_CONSUMER_MQTT,
    COMFOAIR_STATE_CONSUMER_COUNT,
} comfoair_state_consumer_t;

// True when entity `index` has changed since that CONSUMER last cleared its marks.
// Indices match COMFOAIR_ENTITIES, so a caller walks the registry once and asks per entity.
//
// "Changed" means the VALUE differs, not that a frame arrived: the unit rebroadcasts
// constantly, and treating every frame as a change would make a delta the size of the full
// document and defeat its purpose.
bool comfoair_state_changed(comfoair_state_consumer_t who, size_t index);
void comfoair_state_clear_changes(comfoair_state_consumer_t who);

// Clears ONE entity's mark for one consumer. MQTT publishes entity by entity rather than as one
// document, so it clears what it actually sent -- a broker that accepts three publishes and then
// drops the connection must not lose the marks of the rest.
void comfoair_state_clear_change(comfoair_state_consumer_t who, size_t index);

// --- clearing a mark without losing the change that arrived while publishing ------------------
//
// The publisher reads a value, sends it, and clears the mark; the CAN task can apply a PDO to that
// same slot in between. An unconditional clear then erases that PDO's mark and the newer value is
// never published -- and for the four durations, which nothing ever rebroadcasts, the retained
// payload left on the broker stays wrong indefinitely rather than healing on the next frame (the
// three RMI-read selects heal only at the next settings read, up to 6 h later). So the publisher samples the generation BEFORE it reads the value and
// clears with it afterwards.
//
// The generation of this CONSUMER's mark on entity `index`: it rises once per marking, never
// falls, and the two consumers count separately, exactly as their marks are separate. An
// out-of-range consumer or index reads 0, which is the same answer every time and therefore no
// basis for clearing anything -- the call below refuses those arguments outright.
//
// Called from the MQTT publisher task. Safe from any task: it takes the state lock like every
// other reader in this header, and owns nothing the caller must free.
uint32_t comfoair_state_change_generation(comfoair_state_consumer_t who, size_t index);

// Clears one consumer's mark on `index` ONLY if the generation is still `seen`. The compare and
// the clear happen inside ONE hold of the state lock -- split them and this is no better than
// clearing unconditionally, because the PDO lands in the gap between them.
//
// Returns true when the generation still matched -- the mark is clear either way afterwards, so a
// repeat call at the same generation returns true having cleared nothing, which is the honest
// answer to "is what I published still the current value". FALSE OBLIGES THE CALLER TO NOTHING
// but leaving the mark alone: it
// means a change arrived after `seen` was sampled, the mark is still set, and the next pass reads
// it and publishes the newer value. DO NOT retry the publish in place -- the value the caller
// holds is the stale one, and re-sending it is the very update this call exists to preserve.
// Called from the MQTT publisher task.
bool comfoair_state_clear_change_if(comfoair_state_consumer_t who, size_t index, uint32_t seen);

#ifdef __cplusplus
}
#endif
