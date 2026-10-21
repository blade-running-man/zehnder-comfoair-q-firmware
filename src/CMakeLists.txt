idf_component_register(
    SRCS "main.cpp" "app_mdns.c"
    INCLUDE_DIRS "."
    REQUIRES board web_assets comfoair_can comfoair_protocol comfoair_state comfoair_poll
             comfoair_net comfoair_http comfoair_log comfoair_control comfoair_mqtt comfoair_led
             comfoair_mdns
             comfoair_button comfoair_phase_monitor
             comfoair_probe_task comfoair_ota_runtime esp_system nvs_flash
)
