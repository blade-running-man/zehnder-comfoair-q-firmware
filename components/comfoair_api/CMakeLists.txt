idf_component_register(
    SRCS "comfoair_api.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_state comfoair_registry comfoair_lock comfoair_json
)
