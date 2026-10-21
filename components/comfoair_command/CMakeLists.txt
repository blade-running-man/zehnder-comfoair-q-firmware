# Pure, and its own component for the reason comfoair_policy is: a builder living inside a request
# handler could only ever be exercised by flashing a board, and "which bytes reach the ventilation
# unit" is the last thing in this firmware that should be checkable only on hardware.
#
# REQUIRES comfoair_protocol, because every byte comes out of rmi_frame.h -- which was ported
# verbatim and has 48 tests carrying the reasoning for each constant. A literal 0x84 written in
# this component would be a second copy of the protocol.
#
# REQUIRES comfoair_registry, because writability, option order and the duration ranges are the
# GENERATED table's answers. Restating any of them here would be the second entity list AGENTS.md
# forbids outright.
#
# NOT listed, deliberately: comfoair_can, esp_driver_twai, anything that can transmit. This
# component turns a request into bytes and hands them back; the moment it can send, "what is on
# the wire" stops being a value a host test can inspect.
idf_component_register(
    SRCS "comfoair_command.cpp" "comfoair_command_build.cpp" "comfoair_command_readback.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_protocol comfoair_registry
)
