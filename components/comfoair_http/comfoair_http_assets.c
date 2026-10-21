// comfoair_http: the catch-all routes -- the write gate, the captive-portal probe answers and the
// embedded SPA. Read comfoair_http.h first; the component's shape is in README.md, Implementation.
#include "comfoair_http_internal.h"

#include <string.h>

#include "comfoair_captive.h"
#include "comfoair_net.h"
#include "esp_http_server.h"
#include "web_assets.h"

// Every write, whatever the path. Registering this is what makes the access policy real:
// without it a POST simply found no handler and the server answered 404, so "writing is
// refused until a password exists" was true by accident rather than by rule -- and it would
// have stopped being true the moment the first write handler appeared.
esp_err_t comfoair_http_write_gate(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    // The policy let it through; there is simply nothing behind this path. Every real write
    // (/api/config, /api/provision, /api/entities/*, /api/ops/*, /api/probe, /api/ota) has its
    // own route registered before this one. 501 says that honestly instead of pretending the
    // request was applied.
    httpd_resp_set_status(req, "501 Not Implemented");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"error\":\"not implemented yet\"}");
    return ESP_OK;
}

static esp_err_t asset_get(httpd_req_t *req)
{
    if (!comfoair_http_allowed(req))
        return ESP_OK;

    char path[128];
    comfoair_http_request_path(req, path, sizeof path);

    for (const web_asset_t *a = web_assets(); a->path; a++) {
        if (strcmp(path, a->path) != 0)
            continue;
        httpd_resp_set_type(req, a->content_type);
        // Vite gzipped these at build time; nothing is compressed at runtime.
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
        return httpd_resp_send(req, (const char *)a->start, a->len);
    }

    // An unknown /api/ path is a missing endpoint, not a client-side route. Returning the
    // SPA shell with 200 turns "no such endpoint" into "the endpoint returned HTML", which
    // is a great deal harder to diagnose than a 404.
    if (strncmp(path, "/api/", 5) == 0) {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"error\":\"no such endpoint\"}");
        return ESP_OK;
    }

    // Anything else is a client-side route: hand back the shell and let the SPA resolve it.
    // Without this, a reload on /settings is a 404.
    const web_asset_t *index = web_assets();
    httpd_resp_set_type(req, index->content_type);
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    return httpd_resp_send(req, (const char *)index->start, index->len);
}

// The paths a phone probes to decide whether a network has internet. Answering the SPA shell
// with 200 to these is what makes an Android decide the network is useless and switch back to
// mobile data -- taking the form submission with it.
esp_err_t comfoair_http_captive_probe_get(httpd_req_t *req)
{
    char path[128];
    comfoair_http_request_path(req, path, sizeof path);

    const comfoair_captive_answer_t *answer = comfoair_captive_answer(path);
    if (answer == NULL)
        return asset_get(req);

    comfoair_net_note_client();
    httpd_resp_set_status(req, answer->status);
    httpd_resp_set_type(req, answer->content_type);
    if (answer->cache_control != NULL)
        httpd_resp_set_hdr(req, "Cache-Control", answer->cache_control);
    // body is NULL for the 204s, and httpd_resp_send takes that as "no body" only when the
    // length is zero too -- which comfoair_captive guarantees and its tests assert.
    return httpd_resp_send(req, answer->body, answer->body_len);
}
