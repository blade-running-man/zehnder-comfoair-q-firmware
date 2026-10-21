# Three files and one line between them: what a hold MEANS is pure, what a pin reads is not.
#
# comfoair_button.c is the decider AND the three decisions the polling task used to make inside
# itself -- the gap that is a release, the overlay plan, the reboot gate. They live here because
# here they are reachable from [env:native]: test/test_button_gesture covers the gesture,
# test/test_button_poller covers the other three. No ESP-IDF, no clock, no pin.
#
# comfoair_button_task.c (the pin, the 20 ms poll) and comfoair_button_reset.c (the erase, the
# confirming flashes and one of the two esp_restart() calls in this firmware -- OTA has the other)
# are both wrapped in
# #ifdef ESP_PLATFORM, and they have to be: the host environment's dependency finder works on the
# component directory (platformio.ini:44-47) and builds every .c in it the moment a suite includes
# comfoair_button.h. comfoair_led/CMakeLists.txt:1-6 says the same of comfoair_led_driver.c. They
# are two files rather than one because they are two responsibilities -- a loop that must read as a
# loop, and a path that runs once and ends the process -- and comfoair_button_internal.h is the
# whole of what crosses between them.
#
# REQUIRES comfoair_led and nothing else: comfoair_gesture_tier_t is named in this component's own
# interface and is DECLARED THERE, by the component that draws it (comfoair_led.h:70-76). A second
# copy of that enumeration here would be a second source of truth for the thing the two components
# exist to agree on.
#
# EVERYTHING THE TASK NEEDS IS PRIVATE, and board is the one to look at twice: it appears in
# comfoair_button_task.h, so a caller that includes that header needs `board` in its own REQUIRES --
# src/CMakeLists.txt already has it, exactly as comfoair_led_driver.h required. Listing board
# publicly here would put it into the REQUIRES of every host suite that includes the decider, which
# is the dependency this component was split to avoid.
#
# comfoair_ota_runtime IS PRIVATE, for comfoair_button_reset.c alone: a reset restarts the chip,
# and within 30 s of an update that restart would roll the update back unless the image is
# confirmed first. No cycle: comfoair_ota_runtime requires app_update and esp_timer only.
#
# comfoair_net IS NOT LISTED AT ALL, and that is not an omission. The short press reaches the
# provisioning machine through a function pointer handed in by src/main.cpp, because an #include of
# comfoair_net.h here would put the radio into the host build of every suite that includes the
# decider -- PlatformIO's dependency finder does not evaluate #ifdef ESP_PLATFORM. See the comment
# above comfoair_button_start().
idf_component_register(
    SRCS "comfoair_button.c" "comfoair_button_task.c" "comfoair_button_reset.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_led
    PRIV_REQUIRES board comfoair_config comfoair_log comfoair_mdns comfoair_ota_runtime driver
                  esp_timer esp_system
)
