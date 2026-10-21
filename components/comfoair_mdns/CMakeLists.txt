# Two halves in one component, and the split between them is the design. comfoair_mdns_name.c is
# pure -- no ESP-IDF, no socket, no MAC -- and test/test_mdns_name covers it on the host.
# comfoair_mdns.c is espressif/mdns; it compiles to nothing off the device because it is wrapped in
# #ifdef ESP_PLATFORM, and it HAS to be, because the host test build compiles every .c in this
# directory the moment a suite includes comfoair_mdns.h (platformio.ini, lib_extra_dirs) -- and
# comfoair_button_reset.c includes it, so the two button suites compile this file too.
# comfoair_led/CMakeLists.txt:1-6 says the same of comfoair_led_driver.c.
#
# NO COMFOAIR COMPONENT IS LISTED, AND NONE MAY BE. The dependency runs comfoair_mdns ->
# comfoair_net, never back, and this component genuinely needs what comfoair_net knows -- the
# four characters of comfoair_net_short_id(), the device id, the owner's name. It takes them AS
# ARGUMENTS instead, through comfoair_mdns_config_t, because an #include of comfoair_net.h in
# comfoair_mdns.c reaches the host build: PlatformIO's dependency finder does not evaluate
# #ifdef ESP_PLATFORM, so it followed that include and tried to compile comfoair_net.c natively.
# That was measured, not feared -- `pio test -e native -f test_mdns_name` died on esp_event.h. The
# same trap, and the same escape through a caller-supplied value, is written out in
# comfoair_button/CMakeLists.txt for comfoair_net and in comfoair_led/CMakeLists.txt for board.
#
# The consequence: src/main.cpp assembles the facts. That is also where the ORDER lives -- after
# comfoair_net_start() and after comfoair_http_start() -- so both halves of the wiring read in one
# place.
#
# comfoair_mdns.h therefore names esp_err_t and its own three structs and nothing else: no board_t,
# no esp_netif_t, no mdns_txt_item_t. A consumer that includes it (src/main.cpp, comfoair_http,
# comfoair_button) needs nothing added to its own REQUIRES.
#
# espressif__mdns, NOT `mdns`: that is the name the component manager gives a managed dependency,
# and the short form does not resolve -- the same lesson comfoair_led learned with
# espressif__led_strip. The manifest that fetches it is src/idf_component.yml, NOT the project
# root, where it builds green and is silently ignored.
idf_component_register(
    SRCS "comfoair_mdns_name.c" "comfoair_mdns.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES espressif__mdns
)
