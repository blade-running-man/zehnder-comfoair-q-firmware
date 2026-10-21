# What an arriving MQTT command MEANS, with none of what follows from it.
#
# Pure, and its own component for the reason comfoair_command is one against comfoair_control and
# comfoair_ha is one against the publisher: the deciding half needs no broker, no FreeRTOS task and
# no network, and until it was lifted out of comfoair_mqtt's inbound handler (today
# comfoair_mqtt_handle_inbound() in comfoair_mqtt_command.c) it could only be exercised by flashing
# a board and pressing a button in Home Assistant. That branching is where a regression is SILENT --
# a command quietly stops being applied and nothing anywhere says so -- which is exactly the kind of
# logic AGENTS.md puts on the host. test/test_mqtt_inbound is what reaches it.
#
# A COMPONENT DIRECTORY AND NOT A FILTERED SOURCE INSIDE comfoair_mqtt, and the reason is
# platformio.ini's: PlatformIO's dependency finder works at COMPONENT granularity, which is the
# whole basis of "the board- and IDF-bound components are never built here". A component that is
# half host-buildable makes that sentence false and leaves the next person adding a pure file here
# with a filter to edit that nothing points at.
#
# REQUIRES comfoair_ha for the topic parser and comfoair_ha_ctx_t, which are the two things it
# needs to know a message is ours; comfoair_command for comfoair_command_t and the durations, both
# of which are in this component's own interface; comfoair_state for comfoair_fan_state_t and the
# registry lookup comfoair_entity_by_key, which is a strcmp over the generated table and takes no
# lock; and comfoair_registry, whose entity kinds it branches on by name.
#
# PRIV_REQUIRES comfoair_wire, and the distinction is the one comfoair_wire's own CMakeLists draws
# in the other direction: comfoair_wire.h is included from the .c to read an operation body and
# appears nowhere in comfoair_mqtt_inbound.h, so a consumer of this component does not inherit it.
#
# NOT listed, deliberately: mqtt, comfoair_mqtt, comfoair_control, esp_log. This component decides
# and returns; the moment it can submit a command, write the state model or log, the decision stops
# being a value a host test can inspect and the counters start moving from inside a parser. Same
# rule, and same wording, as comfoair_command against comfoair_can.
idf_component_register(
    SRCS "comfoair_mqtt_inbound.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_ha comfoair_command comfoair_state comfoair_registry
    PRIV_REQUIRES comfoair_wire
)
