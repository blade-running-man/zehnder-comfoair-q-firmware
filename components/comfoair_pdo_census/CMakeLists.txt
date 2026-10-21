# Pure: the record of every PDO heard on the bus. Apart from comfoair_probe_task so that first
# sight, the row ceiling and the ignored frames are pinned on the host, and apart from
# comfoair_probe_report so that the JSON renderer does not depend on how the rows are kept.
#
# NOT listed, deliberately: comfoair_can, freertos, log. record() runs under the receive task's
# spinlock, and a dependency that could block or log has no business being reachable from it.
idf_component_register(
    SRCS "comfoair_pdo_census.cpp"
    INCLUDE_DIRS "include"
)
