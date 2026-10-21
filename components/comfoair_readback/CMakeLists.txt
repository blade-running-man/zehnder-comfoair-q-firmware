# Pure: the meaning of an RMI read-back reply and the guard that decides whether it may be applied.
# Apart from comfoair_probe_task so both are host-tested (test/test_readback) against the generated
# table and the write builders, not only through the bus. REQUIRES comfoair_registry for the table
# and comfoair_protocol for nothing at build time but the test's cross-check of rmi_frame.h.
# DO NOT add comfoair_state or comfoair_control: the glue that touches them is the probe task's.
idf_component_register(
    SRCS "comfoair_readback.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_registry comfoair_protocol
)
