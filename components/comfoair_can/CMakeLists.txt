idf_component_register(
    SRCS "comfoair_can.c" "comfoair_can_api.c"
    INCLUDE_DIRS "include"
    REQUIRES board esp_driver_twai
)
