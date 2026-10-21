# Pure, and for the reason every pure component here is: a discovery payload that can only be
# checked by flashing a board and watching somebody's Home Assistant is a payload nobody checks.
# Every key it emits was verified against HA's own validating schema, and test/test_ha is what
# keeps that true -- including the three rules that are traps rather than preferences (options vs
# device_class enum, the fan's inclusive preset pair, and the characters a discovery topic accepts).
#
# REQUIRES comfoair_state for comfoair_value_t and comfoair_fan_state_t (which carries the registry
# with it), and comfoair_json for the one escaper this firmware has.
#
# NOT listed, deliberately: mqtt, esp-mqtt, comfoair_mqtt. This component turns an entity into a
# topic and a payload; the moment it can publish, what goes on the wire stops being a value a host
# test can inspect. Same rule, and same wording, as comfoair_command against comfoair_can.
# FOUR SOURCES AND ONE INTERNAL HEADER, split along seams that were already section comments:
#   comfoair_ha.c          what a topic is CALLED -- the six builders, the component mapping, the
#                          parser for an arriving topic
#   comfoair_ha_payload.c  what goes IN one -- the discovery document
#   comfoair_ha_state_payload.c  the state scalars and the fan's three leaves
#   comfoair_ha_cleanup.c  the retraction enumerator: no formatting of its own, only the order
#                          and the walk
#   comfoair_ha_internal.h the bounded writer and the one predicate the first two share,
#                          static inline, NOT in include/
#
# The 350-line ceiling (AGENTS.md) is why, and the split happened on 2026-09-07 rather than as a
# drive-by: the fan-leaf enumeration added lines to a file that was already at 532.
idf_component_register(
    SRCS "comfoair_ha.c" "comfoair_ha_payload.c" "comfoair_ha_cleanup.c"
         "comfoair_ha_state_payload.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_state comfoair_json comfoair_registry
)
