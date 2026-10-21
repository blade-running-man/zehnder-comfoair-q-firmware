# Pure, and its own component for the reason comfoair_command is: "which requests reach the unit"
# must be checkable on the host, not only by flashing a board.
#
# NOT listed, deliberately: comfoair_can, comfoair_protocol's builders, anything that can transmit.
# The walk hands out abstract requests; comfoair_probe_task turns them into bytes through
# rmi_read.h. The moment this component can send, "what goes on the wire" stops being a value a
# host test can inspect.
idf_component_register(
    SRCS "comfoair_probe_scan.cpp" "comfoair_probe_store.cpp"
    INCLUDE_DIRS "include"
)
