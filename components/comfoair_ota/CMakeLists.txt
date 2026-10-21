# Pure: the pre-flight checks on an uploaded image and the answer for each refusal. No flash and no
# HTTP here, so a host test decides what a wrong file is told.
idf_component_register(
    SRCS "comfoair_ota.c"
    INCLUDE_DIRS "include"
)
