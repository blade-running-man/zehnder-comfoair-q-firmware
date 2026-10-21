# REQUIRES, not PRIV_REQUIRES, for the first three: comfoair_net.h exposes board_t,
# comfoair_wifi_t and comfoair_prov_state_t in its own interface, so anything including it needs
# those directories too.
#
# comfoair_captive is here because the DNS responder lives and dies with the access point, and the
# only code that knows when the access point is on the air is this one. Note which half is used:
# comfoair_captive_dns_server.h, the socket. The TABLE of probe paths is comfoair_http's business
# and is deliberately not touched here -- this component has no business answering HTTP.
#
# esp_timer is named although it is easy to get away without: it is NOT one of the IDF's common
# requirements (tools/cmake/build.cmake:282 lists exactly `cxx newlib freertos esp_hw_support heap
# log soc hal esp_rom esp_common esp_system`), and esp_timer_get_time() is the clock every deadline
# in comfoair_provision is measured against. A component that names what it uses does not break on
# the release that stops pulling it in by accident.
idf_component_register(
    SRCS "comfoair_net.c" "comfoair_net_radio.c" "comfoair_net_start.c" "comfoair_net_query.c"
         "comfoair_net_scan.c" "comfoair_net_settings.c"
    INCLUDE_DIRS "include"
    REQUIRES board comfoair_config comfoair_provision comfoair_captive comfoair_wire
             esp_wifi esp_netif esp_event esp_timer nvs_flash
)
