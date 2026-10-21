idf_component_register(
    SRCS "comfoair_state.cpp" "comfoair_state_decode.cpp" "comfoair_state_derived.cpp"
         "comfoair_state_marks.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_registry comfoair_protocol comfoair_lock
)
