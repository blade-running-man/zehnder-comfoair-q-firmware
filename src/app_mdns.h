// Bringing comfoair_mdns up, out of app_main().
//
// A FILE OF ITS OWN BECAUSE main.cpp IS AT ITS CEILING (AGENTS.md: 350 lines) and this was the
// only self-contained seam in it -- one set of inputs, one call, nothing left behind. The split is
// by size, not by responsibility, and it is honest to say so: everything here would read perfectly
// well inline, and the day app_main() has room again it could go back.
//
// It gathers what comfoair_mdns may not fetch for itself. That component includes no other
// component's header at all (comfoair_mdns/CMakeLists.txt says why -- an #include of
// comfoair_net.h there reaches the HOST build and drags the radio into three test suites), so
// somebody has to read comfoair_net and board on its behalf, and this is that somebody.
#pragma once

#include "board.h"
#include "esp_app_desc.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start the responder. `http_port` is the port the web server actually came up on, or 0 when it
// did not start -- 0 advertises the host name and no service, since a name that resolves to a
// refused connection reads as a broken device.
//
// RETURNS NOTHING AND CANNOT FAIL THE BOOT. It logs what was lost and comes back, the same rule as
// every other start in app_main (AGENTS.md).
void app_mdns_start(const board_t *board, const esp_app_desc_t *app, uint16_t http_port);

#ifdef __cplusplus
}
#endif
