# The pure half of the PDO request task, and its own component for a build reason: the host
# environment's dependency finder works per component directory (platformio.ini), and
# comfoair_poll.cpp beside this file would drag FreeRTOS and the TWAI driver into test_poll_plan.
# Same split as comfoair_phase against comfoair_phase_monitor. REQUIRES nothing.
idf_component_register(
    SRCS "comfoair_poll_plan.c"
    INCLUDE_DIRS "include"
)
