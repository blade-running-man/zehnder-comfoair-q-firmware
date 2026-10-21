// The HTTP surface: the same API for everyone, and the SPA served from flash.
//
// AGENTS.md criterion 4 -- the web UI has no privileged endpoint. Every path below is reachable by
// curl exactly as it is by the browser, and the browser gets no header, cookie or route the
// API does not give anyone else. If a screen ever needs something the API does not offer,
// the API is wrong.
#pragma once

#include "esp_err.h"
#include "comfoair_http_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t port;
} comfoair_http_config_t;

#define COMFOAIR_HTTP_DEFAULT_CONFIG() ((comfoair_http_config_t){.port = 80})

esp_err_t comfoair_http_start(const comfoair_http_config_t *config);
esp_err_t comfoair_http_stop(void);

// Tell connected WebSocket clients the state moved. Safe to call from any task and from
// the CAN receive path -- it only sets a flag and wakes the pusher, because a burst of PDO
// frames must not turn into a burst of frames on every open socket.
void comfoair_http_notify_state_changed(void);

// There is deliberately NO comfoair_http_set_provisioned(). Whether the device counts as claimed
// is asked of comfoair_net on every request, not pushed here and cached.
//
// A cached copy is a tick out of date, and the tick it is out of date by is exactly the window
// the "unclaimed while an open access point is on the air" rule exists to close
// (docs/architecture/security.md, "Trust model"): the access point leaves the air the instant the
// station is given an address. Worse, the thing that used to be pushed was
// comfoair_net_has_credentials() -- "claimed means credentials are stored", the reading that rule
// rejects BY NAME, because storing credentials would then make the device writable while its open
// access point is still up and the passer-by who supplied them is still on it.

#ifdef __cplusplus
}
#endif
