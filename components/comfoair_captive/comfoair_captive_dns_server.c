// One socket, one task, and no decisions. See comfoair_captive_dns_server.h for the contract and
// comfoair_captive_dns.h for what the bytes mean.
//
// THE GUARD BELOW IS NOT DECORATION. This component is an ESP-IDF component AND a PlatformIO
// library at once (platformio.ini, lib_extra_dirs), so the host test build compiles every .c in
// this directory the moment test/test_captive includes comfoair_captive.h -- and this one cannot
// compile without lwip. The same split, and the same reason, as comfoair_config_nvs.c: what can
// be tested on a laptop lives next door, and what is left here is too dull to hide a bug in.
#ifdef ESP_PLATFORM

#include "comfoair_captive_dns_server.h"

#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>

#include "comfoair_captive_dns.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"

static const char *TAG = "captive";

// Static rather than on the task's stack, and static rather than heap: two 512-byte buffers are
// a line in the map file this way, and cannot fail to be there at the moment a packet arrives.
// One task owns both, which is what makes it safe -- comfoair_http_internal.h reasons the same way
// about g_http_scratch. DO NOT let a second caller reach these.
static uint8_t s_rx[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];
static uint8_t s_tx[COMFOAIR_CAPTIVE_DNS_MAX_MESSAGE];

static uint8_t       s_address[4];
static int           s_sock = -1;
static TaskHandle_t  s_task;
static volatile bool s_running;  // the task should keep going
static volatile bool s_alive;    // the task has not reached vTaskDelete yet

// Consecutive non-timeout failures of recvfrom(). It exists so that a wedged socket costs the log
// TWO lines rather than one per second: see the loop.
static unsigned s_recv_errors;

// How long recvfrom() waits before returning so the loop can look at s_running.
//
// This is how the task is stopped, and the alternative is the bug: closing a socket from another
// task while this one is blocked in recvfrom() on it is a race whose outcome depends on how lwip
// was configured, and the failure it produces -- the stack freeing something this task is still
// inside -- is not one that shows up in testing. One wakeup a second on an idle device costs
// nothing measurable. DO NOT replace this with a close() from comfoair_captive_dns_stop().
//
// struct timeval is the right type here and not a millisecond int: LWIP_SO_SNDRCVTIMEO_NONSTANDARD
// defaults to 0 (lwip/opt.h:2098-2099), and the IDF's own udp_server example passes one
// (examples/protocols/sockets/udp_server/main/udp_server.c:75-78).
#define RECV_TIMEOUT_MS 1000

// A failing socket must not become a spin. This device has ONE core and the HTTP server the
// owner is typing into is on it; a tight loop on a socket that returns an error every time is
// that owner watching a page never load. Nothing here reboots, whatever happens (AGENTS.md).
#define ERROR_BACKOFF_MS 1000

static void dns_task(void *arg)
{
    (void)arg;

    while (s_running) {
        struct sockaddr_storage from;
        socklen_t               from_len = sizeof from;

        int n = recvfrom(s_sock, s_rx, sizeof s_rx, 0, (struct sockaddr *)&from, &from_len);
        if (n < 0) {
            // The timeout expiring is the normal case, and it is the only reason this loop ever
            // gets to look at s_running.
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
                continue;
            // ONE line per outage, not one per second. The backoff below sets that rate, and the
            // log this writes into is a finite ring that /api/log publishes -- so a socket wedged
            // for an hour would evict 3600 lines of real diagnostics, including the provisioning
            // failure the owner is standing in front of the unit trying to read. The reason is in
            // the first line and the duration is in the recovery line; nothing is lost by keeping
            // quiet in between, because every one of the suppressed lines is the same errno.
            if (s_recv_errors++ == 0)
                ESP_LOGW(TAG, "dns socket read failed (errno %d); staying quiet until it recovers",
                         errno);
            vTaskDelay(pdMS_TO_TICKS(ERROR_BACKOFF_MS));
            continue;
        }
        if (s_recv_errors != 0) {
            // The other end of the pair, so an outage is bounded in the log rather than trailing
            // off. At one failure per ERROR_BACKOFF_MS this count is also roughly its length.
            //
            // This counts OUR OWN socket failures, not queries, which is why it is not the packet
            // counter the note below refuses: it says nothing about who was on the access point
            // or what they asked for, and it only appears when something was already wrong.
            ESP_LOGW(TAG, "dns socket recovered after %u failed reads", s_recv_errors);
            s_recv_errors = 0;
        }

        // DO NOT log the name that was asked for, at any level. Logging is captured whole and
        // served by /api/log to whoever can reach the device, and on this access point that is
        // anyone in radio range -- so a debug line here publishes every host the owner's phone
        // looked up while they stood next to the unit. A packet counter is not interesting
        // enough to be worth the same risk.
        size_t reply = comfoair_captive_dns_reply(s_rx, (size_t)n, s_address, s_tx, sizeof s_tx);
        if (reply == 0)
            continue;  // saying nothing is an answer here; comfoair_captive_dns.h lists to what

        // Best effort, and deliberately unchecked: a datagram that does not leave is a phone that
        // asks again a second later, and there is nothing this task could usefully do about it.
        (void)sendto(s_sock, s_tx, reply, 0, (struct sockaddr *)&from, from_len);
    }

    // The task closes its own socket, so no other task ever touches a descriptor this one might
    // still be blocked inside.
    close(s_sock);
    s_sock  = -1;
    s_alive = false;
    vTaskDelete(NULL);
}

esp_err_t comfoair_captive_dns_start(const uint8_t address[4])
{
    if (s_task != NULL)
        return ESP_ERR_INVALID_STATE;
    if (address == NULL)
        return ESP_ERR_INVALID_ARG;

    memcpy(s_address, address, sizeof s_address);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "no socket for dns (errno %d)", errno);
        return ESP_FAIL;
    }

    struct timeval timeout = {.tv_sec = RECV_TIMEOUT_MS / 1000, .tv_usec = 0};
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout) < 0) {
        // Fatal rather than tolerated: without the timeout the stop path becomes the race
        // described above, and a portal that cannot be stopped cleanly is worse than one that
        // never started -- the second failure is visible and this one is not.
        ESP_LOGE(TAG, "no receive timeout on the dns socket (errno %d)", errno);
        close(sock);
        return ESP_FAIL;
    }

    // Bound to the access point's own address, never to INADDR_ANY. The header says why; it is
    // the one line in this file that is a security decision rather than plumbing.
    struct sockaddr_in bind_to = {
        .sin_family = AF_INET,
        .sin_port   = htons(COMFOAIR_CAPTIVE_DNS_PORT),
    };
    memcpy(&bind_to.sin_addr.s_addr, s_address, sizeof s_address);

    if (bind(sock, (struct sockaddr *)&bind_to, sizeof bind_to) < 0) {
        ESP_LOGE(TAG, "cannot bind port %d (errno %d)", COMFOAIR_CAPTIVE_DNS_PORT, errno);
        close(sock);
        return ESP_FAIL;
    }

    s_sock    = sock;
    s_running = true;
    s_alive   = true;
    // Cleared here and not only at load: after a stop and a restart a stale count would make the
    // first successful read announce a recovery from an outage that ended with the last portal.
    s_recv_errors = 0;
    // Priority 3: below the CAN receive task at 10 and the push channel at 4. A flood of queries
    // from the pavement must not be able to delay a PDO off the bus -- the TWAI receive queue
    // overflowing is the one thing in this firmware that loses data silently, and it is the same
    // argument that keeps MQTT publishing out of the CAN callback: a publish blocked on a silent
    // broker would stop the bus being read.
    //
    // 3 KB of stack because both buffers are static; what is left on it is lwip's own frames.
    if (xTaskCreate(dns_task, "comfoair_dns", 3072, NULL, 3, &s_task) != pdPASS) {
        s_running = false;
        s_alive   = false;
        s_sock    = -1;
        close(sock);
        ESP_LOGE(TAG, "no task for dns");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "captive dns on %u.%u.%u.%u:%d", (unsigned)s_address[0], (unsigned)s_address[1],
             (unsigned)s_address[2], (unsigned)s_address[3], COMFOAIR_CAPTIVE_DNS_PORT);
    return ESP_OK;
}

void comfoair_captive_dns_stop(void)
{
    if (s_task == NULL)
        return;

    s_running = false;
    // One receive timeout plus a margin. Bounded, and it gives up rather than blocking for ever:
    // this is called from whatever noticed the access point going away, and that path has its own
    // work to do.
    for (int waited = 0; waited < RECV_TIMEOUT_MS + 500 && s_alive; waited += 20)
        vTaskDelay(pdMS_TO_TICKS(20));

    if (s_alive) {
        // s_task is deliberately left set, so a later start() refuses instead of putting a second
        // task on the same buffers. A portal that does not come back is a device the owner still
        // reaches by typing an address; two tasks sharing s_rx is a device that answers nonsense.
        ESP_LOGW(TAG, "dns task did not stop; not starting another");
        return;
    }
    s_task = NULL;
}

#endif  // ESP_PLATFORM
