// The two tables, and nothing else: what a phase looks like, and what a look is.
// Separated from the resolver so that "which picture" and "how that picture is drawn this
// millisecond" fail apart -- a wrong row here is a wrong meaning, a wrong number there is a wrong
// animation, and they are found by different cases in test/test_led_pattern.
#include "comfoair_led.h"

// SWITCHES AND NOT ARRAYS, and that is the whole guard. -Werror=switch (platformio.ini) turns a
// fifteenth phase without a row into a build failure; an array indexed by the enumeration would
// have compiled and answered with whatever the initialiser left at that index. For the same reason
// there is no `default:` in either function -- the fallthrough return below is reachable only for a
// number that is not in the enumeration at all, which is a different fact from a missing row.
comfoair_led_look_t comfoair_led_look_of(device_phase_t phase) {
    switch (phase) {
        case COMFOAIR_PHASE_BOOT:            return COMFOAIR_LOOK_BOOT;
        case COMFOAIR_PHASE_OTA:             return COMFOAIR_LOOK_OTA;
        // The three collapses, each one an identical owner action. Fourteen phases
        // onto eleven looks leaves room for exactly three, and the suite MEASURES that rather than
        // taking either the table or the prose at its word. TRIAL and
        // CONNECTING: wait. The difference is real and is reported -- by /api/status, in words --
        // it simply has no owner action of its own, and the LED channel carries nothing but action.
        case COMFOAIR_PHASE_TRIAL:           return COMFOAIR_LOOK_WORKING;
        case COMFOAIR_PHASE_CONNECTING:      return COMFOAIR_LOOK_WORKING;
        // Connect to the access point. Which text greets the owner at 192.168.4.1 differs; what
        // they must DO does not.
        case COMFOAIR_PHASE_SETUP_AP:        return COMFOAIR_LOOK_SETUP;
        case COMFOAIR_PHASE_RECOVERY_AP:     return COMFOAIR_LOOK_SETUP;
        // Go and look at the router. failure and failure_reason keep all four Wi-Fi verdicts by
        // name (comfoair_wire_render_status.c:171-177); only the channel that has nothing but
        // colour maps them by action.
        case COMFOAIR_PHASE_NET_REJECTED:    return COMFOAIR_LOOK_NET_BROKEN;
        case COMFOAIR_PHASE_NET_LOST:        return COMFOAIR_LOOK_NET_BROKEN;
        case COMFOAIR_PHASE_SETUP_EXPIRED:   return COMFOAIR_LOOK_STRANDED;
        case COMFOAIR_PHASE_FAULT:           return COMFOAIR_LOOK_FAULT;
        case COMFOAIR_PHASE_BROKER_REJECTED: return COMFOAIR_LOOK_BROKER_REJECTED;
        case COMFOAIR_PHASE_BUS_SILENT:      return COMFOAIR_LOOK_BUS_SILENT;
        case COMFOAIR_PHASE_NO_BROKER:       return COMFOAIR_LOOK_NO_BROKER;
        case COMFOAIR_PHASE_ONLINE:          return COMFOAIR_LOOK_OK;
        case COMFOAIR_PHASE_COUNT:           break;
    }
    // Not a phase at all: a defect in whoever called, and the owner is told so in red. Darkness --
    // the tempting alternative -- is the one answer indistinguishable from a healthy device.
    return COMFOAIR_LOOK_FAULT;
}

// Colour answers "where": white is transitional, blue is the network, amber is degradation, red is
// what the firmware cannot leave by itself, and green exists only as ONLINE's rare flash -- never
// steady, because to a red-green colour-blind owner a steady green is FAULT's steady red.
#define WHITE 255, 255, 255
#define BLUE  0, 80, 255
#define AMBER 255, 120, 0
#define RED   255, 0, 0
#define GREEN 0, 255, 0

comfoair_led_style_t comfoair_led_style_of(comfoair_led_look_t look) {
    switch (look) {
        case COMFOAIR_LOOK_BOOT:    return (comfoair_led_style_t){WHITE, COMFOAIR_LED_SOLID};
        case COMFOAIR_LOOK_OTA:     return (comfoair_led_style_t){WHITE, COMFOAIR_LED_PROGRESS};
        // The fastest rhythm in the alphabet goes to the one look whose message is "you are
        // needed", because it is the only message with the right to hurry anybody.
        case COMFOAIR_LOOK_SETUP:   return (comfoair_led_style_t){BLUE, COMFOAIR_LED_BLINK};
        case COMFOAIR_LOOK_WORKING: return (comfoair_led_style_t){BLUE, COMFOAIR_LED_BREATHE};
        case COMFOAIR_LOOK_NET_BROKEN: return (comfoair_led_style_t){BLUE, COMFOAIR_LED_DOUBLE};
        // "Nothing is happening and nothing will" is the literal reading of WINDOW_CLOSED: no
        // network, no access point on the air, and no return scheduled. The button is the way out.
        case COMFOAIR_LOOK_STRANDED: return (comfoair_led_style_t){BLUE, COMFOAIR_LED_SOLID};
        case COMFOAIR_LOOK_NO_BROKER: return (comfoair_led_style_t){AMBER, COMFOAIR_LED_BREATHE};
        case COMFOAIR_LOOK_BROKER_REJECTED:
            return (comfoair_led_style_t){AMBER, COMFOAIR_LED_BLINK};
        // Amber and not red: the network and the web UI still work, so the device is serviceable
        // (src/main.cpp:141-143).
        case COMFOAIR_LOOK_BUS_SILENT: return (comfoair_led_style_t){AMBER, COMFOAIR_LED_DOUBLE};
        case COMFOAIR_LOOK_FAULT:      return (comfoair_led_style_t){RED, COMFOAIR_LED_SOLID};
        case COMFOAIR_LOOK_OK:         return (comfoair_led_style_t){GREEN, COMFOAIR_LED_SPARK};
        case COMFOAIR_LOOK_COUNT:      break;
    }
    return (comfoair_led_style_t){RED, COMFOAIR_LED_SOLID};
}
