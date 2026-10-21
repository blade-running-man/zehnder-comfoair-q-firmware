idf_component_register(
    SRCS "comfoair_poll.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_can comfoair_registry comfoair_protocol comfoair_poll_plan esp_timer
)
