idf_component_register(
    SRCS "comfoair_http.c" "comfoair_http_gate.c" "comfoair_http_ws.c" "comfoair_http_read.c"
         "comfoair_http_config.c" "comfoair_http_command.c" "comfoair_http_assets.c" "comfoair_http_probe.c" "comfoair_http_system.c" "comfoair_http_ota.c" "comfoair_http_ota_preflight.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_wire comfoair_mqtt comfoair_command comfoair_control esp_http_server comfoair_api comfoair_policy comfoair_state comfoair_log
             comfoair_auth comfoair_captive comfoair_config comfoair_mdns comfoair_net comfoair_phase_monitor web_assets
             comfoair_json comfoair_probe_task comfoair_probe_report
             comfoair_sysinfo comfoair_can board esp_app_format app_update spi_flash
             esp_timer comfoair_ota comfoair_ota_runtime comfoair_led
)
