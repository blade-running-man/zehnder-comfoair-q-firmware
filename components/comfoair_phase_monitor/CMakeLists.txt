# The imperative half of the phase aggregate, and its own component for a build reason rather than
# a stylistic one -- the same argument comfoair_phase/CMakeLists.txt already
# makes from the other side. The host environment's dependency finder works on the component
# directory (platformio.ini, lib_extra_dirs), so this task living beside comfoair_phase.c would drag
# FreeRTOS, esp-mqtt and the TWAI driver into the test_phase binary and end the sweep over the whole
# product of inputs that suite is built on. It is the same pair as comfoair_provision against
# comfoair_net, and comfoair_mqtt against comfoair_mqtt_inbound.
#
# NO HOST TEST EVER INCLUDES comfoair_phase_monitor.h, which is why this directory may name the
# whole framework: nothing under test/ can reach it, and comfoair_phase_monitor.c is wrapped in
# #ifdef ESP_PLATFORM in case something one day does.
#
# REQUIRES rather than PRIV_REQUIRES for comfoair_phase and comfoair_phase_collect's home:
# comfoair_phase_monitor.h names device_phase_t and comfoair_phase_fault_t in its own interface, so
# src/main.cpp and comfoair_http need those directories too. Everything else is PRIVATE -- the five
# subsystems this task READS appear nowhere in the header, and listing them publicly would put the
# TWAI driver and esp-mqtt into the REQUIRES of every consumer of a phase.
idf_component_register(
    SRCS "comfoair_phase_monitor.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_phase
    PRIV_REQUIRES comfoair_can comfoair_led comfoair_mqtt comfoair_net comfoair_poll esp_timer
)
