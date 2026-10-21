# Two halves in one component, and the split between them is the design. comfoair_led_pattern.c and
# comfoair_led_map.c are pure -- no ESP-IDF, no clock, no pin -- and test/test_led_pattern covers
# them on the host. comfoair_led_driver.c is the led_strip channel, the power gate, the heartbeat
# pin and the 25 ms task; it compiles to nothing off the device because it is wrapped in
# #ifdef ESP_PLATFORM, and it has to be, because the host test build compiles every .c in this
# directory (components/comfoair_config/CMakeLists.txt:1-8 says the same of comfoair_config_nvs.c).
#
# REQUIRES lists comfoair_phase ALONE, and everything else is PRIVATE ON PURPOSE. comfoair_led.h --
# the header a consumer includes -- names device_phase_t and nothing else; the board, the GPIO
# driver and the led_strip component appear only in comfoair_led_driver.h and .c. Listing them
# publicly would put esp_driver_gpio into the REQUIRES of comfoair_button, which includes
# comfoair_led.h purely for comfoair_gesture_tier_t and must stay free of the framework.
#
# A caller that DOES include comfoair_led_driver.h -- src/main.cpp is the only one -- needs `board`
# in its own REQUIRES, which it already has (src/CMakeLists.txt).
#
# espressif__led_strip is the name the component manager gives a managed dependency: the short
# `led_strip` is not a component in this build and does not resolve. The manifest that fetches it
# is src/idf_component.yml -- NOT the project root, where it builds green and is silently ignored.
idf_component_register(
    SRCS "comfoair_led_pattern.c" "comfoair_led_map.c" "comfoair_led_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_phase
    PRIV_REQUIRES board esp_driver_gpio esp_timer espressif__led_strip
)
