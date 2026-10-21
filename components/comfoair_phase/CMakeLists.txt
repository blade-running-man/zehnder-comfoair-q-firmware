# The pure half of the phase aggregate, and its own component for a build reason rather than a
# stylistic one. The host environment's dependency finder pulls in exactly what a test includes
# (platformio.ini), and the granule it works on is the component directory: putting the 1 Hz
# FreeRTOS task beside comfoair_phase.c would drag FreeRTOS, esp-mqtt and the TWAI driver into the
# test_phase binary and end the sweep over the whole product of inputs that the suite is built on.
# The imperative half lives in comfoair_phase_monitor, the same shape as comfoair_provision against
# comfoair_net.
#
# REQUIRES comfoair_provision and nothing else: comfoair_phase.h names comfoair_prov_state_t and
# comfoair_prov_failure_t in its own interface. NOT listed, deliberately: comfoair_mqtt and
# comfoair_can, whose facts arrive here as plain booleans that the collector has already debounced.
# Depending on either would put an IDF-bound component behind a header that must build on the host.
#
# comfoair_bus_health.c is the second pure verdict of the same shape: whether the ventilation unit
# is answering on CAN, from rx_frames and a millisecond count. comfoair_poll is NOT listed either,
# and that is the one a reader will want to add -- the resolver's whole point is to ask for a sweep
# when the bus goes quiet. It asks by RETURNING a bool; the caller (comfoair_phase_monitor) makes
# the call. An include of comfoair_poll.h here would reach FreeRTOS and take test_bus_health and
# test_phase down with it, for exactly the reason written above.
idf_component_register(
    SRCS "comfoair_phase.c" "comfoair_bus_health.c" "comfoair_phase_collect.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_provision
)
