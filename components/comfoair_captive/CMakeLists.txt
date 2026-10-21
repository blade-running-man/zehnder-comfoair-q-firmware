# Three sources, and the split between them is the design. comfoair_captive_probe.c and
# comfoair_captive_dns.c are pure -- no ESP-IDF, no socket, no clock -- and test/test_captive
# covers both on the host, which is the only way the malformed-packet cases can be written at all:
# every one of them would otherwise need a radio. comfoair_captive_dns_server.c is the socket and
# the task, and it compiles to nothing off the device (it is wrapped in #ifdef ESP_PLATFORM)
# because the host test build compiles every .c in this directory: this component is a PlatformIO
# library as well as an IDF component (platformio.ini, lib_extra_dirs). Same arrangement, and same
# reason, as components/comfoair_config.
#
# REQUIRES lwip IS LOAD-BEARING. DO NOT delete it as redundant: lwip is NOT one of the IDF's
# common requirements. tools/cmake/build.cmake:282 sets those to exactly `cxx newlib freertos
# esp_hw_support heap log soc hal esp_rom esp_common esp_system` (IDF 5.5.5), and the "# -
# esp_hw_support is here for backward compatibility" note on the line above it is about
# esp_hw_support, not about lwip. Without this line lwip's include directory never reaches this
# component and comfoair_captive_dns_server.c's `#include "lwip/sockets.h"` does not resolve.
#
# DO NOT restate this by copying components/comfoair_config/CMakeLists.txt:14-17. That paragraph
# is correct where it stands -- esp_hw_support really is in the common list, for the reason the
# note there gives -- and the sentence that used to be here was that paragraph with `lwip` put in
# its place, which is exactly the substitution that turns it false. It told the reader this line
# was belt and braces, and cited the line that disproves it.
#
# NOT listed, deliberately: comfoair_http. This component must never depend on the HTTP server --
# it exports a table and an answer, and registering them is the caller's job. The moment it can
# see esp_http_server it can register a handler, and then the access policy has a second door.
idf_component_register(
    SRCS "comfoair_captive_probe.c" "comfoair_captive_dns.c" "comfoair_captive_dns_server.c"
    INCLUDE_DIRS "include"
    REQUIRES lwip
)
