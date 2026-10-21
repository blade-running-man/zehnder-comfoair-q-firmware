# Confirming a freshly booted image and restarting into a new one: app_update for the OTA states,
# esp_timer for the confirmation delay.
idf_component_register(
    SRCS "comfoair_ota_runtime.c"
    INCLUDE_DIRS "include"
    REQUIRES app_update esp_timer
)
