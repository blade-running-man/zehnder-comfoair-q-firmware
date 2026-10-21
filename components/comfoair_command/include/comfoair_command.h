// What bytes go into somebody's ventilation unit.
//
// Everything before phase 7 read. This is the first component that WRITES, and the stake is not
// a wrong reading: the reference document records that a single wrong unit byte turns a
// diagnostic read into a bricking write -- "Never send any >= 0x80 command to unit 0x01 (NODE)"
// (comfoair-pdo-rmi.md §0, "Commands that can brick the unit"). The legitimate >= 0x80 set is
// 0x83/0x84/0x85 on SCHEDULE (0x15), and test_command pins the unit byte of every frame this file
// can produce, by number, over a sweep of the whole registry.
//
// PURE, AND THAT IS THE POINT. No radio, no clock, no flash: it turns a request into up to three
// frames and hands them back. A builder living inside a request handler could only ever be
// exercised by flashing a board, and "the bytes that reach the unit" is the last thing in this
// firmware that should be checkable only on hardware. Same argument as comfoair_policy and
// comfoair_wire, with more at stake.
//
// THE COMPONENT'S FIRST TEST IS NOT ABOUT BYTES. It walks COMFOAIR_ENTITIES and demands a builder
// for every entity the registry marks writable. An early build shipped two of thirteen writable
// entities dead -- a select's wire byte is its option INDEX while an enum's is a VALUE from the
// table, the two shared one lookup, and no test touched a select. That is what the sweep exists to
// stop happening twice.
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ENABLETIMERENTRY is exactly 13 bytes and DISABLETIMERENTRY exactly 4. Trailing padding is NOT
// part of the protocol (rmi_frame.h:11): the unit accepts boost frames with three extra zero bytes
// and without them alike, and this firmware emits none.
#define COMFOAIR_COMMAND_MAX_BYTES 13

// Three, because "Auto" is three: release boost, release away, and release manual last. Nothing
// in the protocol needs a fourth.
#define COMFOAIR_COMMAND_MAX_FRAMES 3

typedef struct {
    uint8_t bytes[COMFOAIR_COMMAND_MAX_BYTES];
    uint8_t len;
    // How long to wait BEFORE sending this frame. Non-zero on exactly one frame today: the
    // manual-mode release that ends the "Auto" preset, 1000 ms after the two releases in front of
    // it. It is carried as a number rather than slept on here because this component has no
    // clock -- comfoair_control owns the waiting.
    uint16_t delay_ms;
} comfoair_command_frame_t;

typedef struct {
    comfoair_command_frame_t frames[COMFOAIR_COMMAND_MAX_FRAMES];
    uint8_t count;
    // True for the four duration entities, which produce NO frame at all. The unit is told a
    // duration only as part of a command (pdo_table.py, NumberSpec), so writing one is a local state
    // change and nothing else. A caller that ignored this and looked only at `count` would treat
    // a successful write as a no-op and report nothing happened.
    bool     stores_duration;
    // Which duration, when stores_duration is true. The key is the registry's.
    const char *duration_key;
    uint16_t    duration_minutes;
} comfoair_command_t;

typedef enum {
    COMFOAIR_COMMAND_OK,
    COMFOAIR_COMMAND_ERR_UNKNOWN_ENTITY,
    COMFOAIR_COMMAND_ERR_UNKNOWN_OP,
    // A value outside what the registry advertises, or an option label that is not in the list.
    // Refused rather than mapped to the nearest one: the option order is PROTOCOL order and the
    // nearest label is a different command.
    COMFOAIR_COMMAND_ERR_BAD_VALUE,
    // An operation parameter was absent. NONE of the six has a default -- docs/interfaces/rest-api.md
    // lists the parameters -- and inventing one would act on a number
    // the caller never chose. For three of the six the invented number would mean "forever".
    COMFOAIR_COMMAND_ERR_MISSING_PARAM,
    COMFOAIR_COMMAND_ERR_ARG,
} comfoair_command_err_t;

// --- what the caller is holding ----------------------------------------------------------------

typedef enum {
    COMFOAIR_COMMAND_VALUE_NUMBER,
    COMFOAIR_COMMAND_VALUE_BOOL,
    // An option LABEL from the registry, matched exactly. Not an index: an index arriving over
    // the wire would be a number whose meaning depends on the firmware's build.
    COMFOAIR_COMMAND_VALUE_TEXT,
} comfoair_command_value_kind_t;

typedef struct {
    comfoair_command_value_kind_t kind;
    double      number;
    bool        boolean;
    const char *text;
} comfoair_command_value_t;

// The four durations that live on this node, plus one behavioural setting.
//
// Minutes, because that is what the registry advertises and what the owner types. The x60 into
// seconds happens where the frame is built, once -- getting it wrong makes a 60-minute boost last
// a minute.
typedef struct {
    uint16_t boost_minutes;
    uint16_t bypass_minutes;
    uint16_t balance_minutes;
    uint16_t away_minutes;   // 0 is legal here and means indefinite
    // D-decision of 2026-09-04, and the owner's: pair a level change with the permanent-manual
    // command on subunit 0x08. Without it the unit reverts to AUTO within two hours or at the next
    // scheduler step, and Home Assistant reads that as a firmware that did not hold the command.
    // With it the unit's own screen says "demand control ventilation will be ignored or partially
    // ignored in manual mode" -- the three sensor-ventilation controls deactivate themselves.
    // Default true, so a level set from Home Assistant holds; the owner can turn it off and get
    // the scheduler back.
    bool permanent_manual;
    // The level the fan is running at now, as the state model last saw it, or 0 when nothing has
    // been reported. Used for ONE thing: turning the fan back ON resumes it rather than dropping
    // the house to Low, which is the fallback only when no level is known; a firmware that always
    // sends Low turns "on" into a downgrade for anybody whose unit was on High.
    uint8_t fan_resume_level;
} comfoair_command_durations_t;

void comfoair_command_durations_defaults(comfoair_command_durations_t *out);

// --- writing one entity ------------------------------------------------------------------------

// True when this component can write `key`. The registry sweep in test_command asserts this is
// true for every writable entity and false for everything else.
bool comfoair_command_knows_entity(const char *key);

comfoair_command_err_t comfoair_command_from_entity(const char *key,
                                                    const comfoair_command_value_t *value,
                                                    const comfoair_command_durations_t *durations,
                                                    comfoair_command_t *out);

// --- running one of the six operations ---------------------------------------------------------

// A decoded operation body. Every `has_*` is false until the caller sets it, and an operation
// whose parameter is missing is REFUSED -- see COMFOAIR_COMMAND_ERR_MISSING_PARAM.
//
// `duration_seconds` is SECONDS and signed. Seconds because that is the frozen contract with Home
// Assistant (services.yml), and signed because 0 and every negative value fold into one branch --
// which for `boost` means CANCEL and for the other three means FOREVER. That asymmetry is
// deliberate -- it is the only way `boost` can end a boost early -- and a consumer using -1 as its
// "unset" sentinel gets "forever" on three of the four.
typedef struct {
    const char *mode;     // set_bypass, set_balance_mode
    const char *profile;  // set_temperature_profile
    bool     has_level;
    int32_t  level;       // set_ventilation_level, 0..3 -- and 0 is Away, not "off"
    bool     has_duration;
    int32_t  duration_seconds;
    bool     has_enabled;
    bool     enabled;     // set_away
} comfoair_command_params_t;

comfoair_command_err_t comfoair_command_from_op(const char *name,
                                                const comfoair_command_params_t *params,
                                                const comfoair_command_durations_t *durations,
                                                comfoair_command_t *out);

// --- what to read back after a command ---------------------------------------------------------

// The PDO to request shortly after `command` is sent, or -1 for none.
//
// WHY IT EXISTS. The away_mode switch's state is PDO 337 bit 11 (the away timer its command starts
// and cancels). The owner's Q350 census never heard PDO 337 between sweeps -- only at the boot
// sweep and the hourly one; whether the unit broadcasts it on change is unconfirmed. Without a
// request the switch could show the old state for up to an hour after an away command, so the
// device asks for it (HV-MQTT-31 records the delay). Every command with a frame that starts or
// cancels SCHEDULE subunit 1 timer 11 -- the switch, set_away, and the "Auto" preset's away
// release -- names the switch's PDO, taken from the registry row, never a number written here.
//
// Pure, like everything else in this component: the caller (comfoair_control, after sending)
// turns the answer into one request through comfoair_poll. Decided from the frames, not from the
// entity or operation name, so a new builder that releases away is covered without being listed.
int32_t comfoair_command_readback_pdo(const comfoair_command_t *command);

// One sentence per failure, safe to log and safe to send. Never contains a value.
const char *comfoair_command_strerror(comfoair_command_err_t err);

#ifdef __cplusplus
}
#endif
