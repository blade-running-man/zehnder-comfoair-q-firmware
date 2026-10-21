# Pure: when one entity's state may go out over MQTT, from the per-row policy tools/pdo_table.py
# declares. Apart from comfoair_mqtt so the rule is host-tested (test/test_publish_rate); the clock,
# the slots and the publish are the publisher task's (comfoair_mqtt_publish.c).
# REQUIRES comfoair_state for comfoair_value_t only. DO NOT add comfoair_mqtt or esp-mqtt: PlatformIO's
# dependency finder works per component, and one esp-mqtt include would stop this building on the host.
idf_component_register(
    SRCS "comfoair_publish_rate.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_registry comfoair_state
)
