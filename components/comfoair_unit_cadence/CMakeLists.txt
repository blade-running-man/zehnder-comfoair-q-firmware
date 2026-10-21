# Pure: when the probe task reads the unit again -- the full unit read, or the active errors alone.
# Apart from comfoair_probe_task so the wrap-safe arithmetic is host-tested (test/test_unit_cadence):
# the probe task is FreeRTOS-bound and [env:native] never compiles it. No REQUIRES: ticks in, a
# verdict out.
idf_component_register(
    SRCS "comfoair_unit_cadence.c"
    INCLUDE_DIRS "include"
)
