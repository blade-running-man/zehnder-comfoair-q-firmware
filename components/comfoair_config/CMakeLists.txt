# Two halves, and the split between them is the design: the pure half is four files, the flash
# half is one. comfoair_config.c, comfoair_config_check.c, comfoair_config_password.c and
# comfoair_config_apply.c are pure -- no ESP-IDF, no flash, no clock -- and test/test_config covers
# them on the host. comfoair_config_nvs.c is the thin layer that opens namespaces and moves bytes;
# it compiles to nothing off the device (it is wrapped in #ifdef ESP_PLATFORM) because the host
# test build compiles every .c in this directory: this component is a PlatformIO library as well
# as an IDF component (platformio.ini, lib_extra_dirs). A new source must be listed in SRCS below
# as well -- the device build does not glob.
#
# REQUIRES, not PRIV_REQUIRES: comfoair_config.h includes comfoair_secrets.h and
# comfoair_config_nvs.h includes esp_err.h, so anything including either needs those directories
# too.
#
# esp_hw_support is listed although it is currently in the IDF's common requirements
# (tools/cmake/build.cmake:282, where the comment says it is there "for backward compatibility").
# esp_random.h lives in it, and a component that names what it uses does not break on the release
# that finally removes it from that list.
idf_component_register(
    SRCS "comfoair_config.c" "comfoair_config_check.c" "comfoair_config_password.c"
         "comfoair_config_apply.c" "comfoair_config_nvs.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_secrets nvs_flash mbedtls esp_hw_support
)
