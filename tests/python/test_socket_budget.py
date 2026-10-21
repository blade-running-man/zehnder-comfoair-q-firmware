"""The lwIP socket budget: every socket the firmware can hold at once fits CONFIG_LWIP_MAX_SOCKETS.

Why this exists: the budget was once written as "7 clients + 3 httpd-internal = 10", which forgot
the MQTT client's TCP socket and the captive DNS responder's UDP socket. lwIP then ran out at the
seventh browser connection, before esp_http_server's LRU purge could run (it only purges when all
max_open_sockets sessions are taken), and every new connection died in accept() with ENFILE (23).

comfoair_http_sockets.h holds the count and a _Static_assert in comfoair_http.c checks it on the
board build. This suite checks the same sum WITHOUT a board build, and -- the part a compile-time
check cannot do -- that every source file opening a socket is named in that header, so a new
socket user cannot land without being counted.

What the scan sees: every .c and .cpp under components/ and src/ (test directories and
comfoair_http itself excluded -- httpd's sockets are the clients and internal terms), comments
stripped, searched for the calls in SOCKET_CALLS. What it does NOT see, so a reviewer still has to:
- managed_components/ and the IDF itself -- a library that opens a socket inside a call not listed
  here (esp-mqtt is caught only because esp_mqtt_client_init is listed);
- a call reached through a function pointer, a macro of our own, or a name spelled differently;
- how MANY sockets a file holds: it checks each file is named in the header and that the "other"
  count is at least the number of files, not that the count per file is right.
"""
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / "components" / "comfoair_http" / "comfoair_http_sockets.h"
HTTP_C = ROOT / "components" / "comfoair_http" / "comfoair_http.c"
DEFAULTS = ROOT / "sdkconfig.defaults"

# Calls that leave a descriptor open in lwIP's socket table for longer than the call itself.
# lwIP raw-pcb users (mDNS in its default LWIP networking mode, DHCP, the DNS resolver behind
# getaddrinfo, SNTP) do not take a socket and are deliberately absent.
SOCKET_CALLS = re.compile(
    r"\b("
    r"socket|lwip_socket|netconn_new\w*"                      # BSD and lwIP's own socket APIs
    r"|esp_tls_conn_new\w*|esp_transport_connect\w*"          # TLS and transport-layer opens
    r"|esp_mqtt_client_init|esp_http_client_init|esp_websocket_client_init"  # IDF clients
    r"|httpd_start|httpd_ssl_start"                          # a second server: 3 + its clients
    r")\s*\("
)


def _strip_comments(src: str) -> str:
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def _define(text: str, name: str) -> int:
    m = re.search(rf"^#define\s+{name}\s+(\d+)\b", text, flags=re.M)
    assert m, f"{name} is not #defined in {HEADER.name}"
    return int(m.group(1))


def _lwip_max_sockets() -> int:
    m = re.search(r"^CONFIG_LWIP_MAX_SOCKETS=(\d+)\s*$", DEFAULTS.read_text(), flags=re.M)
    assert m, "sdkconfig.defaults must set CONFIG_LWIP_MAX_SOCKETS -- the IDF default of 10 is too few"
    return int(m.group(1))


def _socket_users() -> set:
    users = set()
    sources = [p for top in ("components", "src") for ext in ("*.c", "*.cpp")
               for p in (ROOT / top).rglob(ext)]
    for path in sources:
        rel = path.relative_to(ROOT)
        # esp_http_server's own sockets are the clients + internal terms, not "other" users.
        if rel.parts[1] == "comfoair_http" or "test" in rel.parts:
            continue
        if SOCKET_CALLS.search(_strip_comments(path.read_text(errors="replace"))):
            users.add(path.name)
    return users


def test_budget_fits_lwip():
    header = HEADER.read_text()
    total = (
        _define(header, "COMFOAIR_HTTP_CLIENT_SOCKETS")
        + _define(header, "COMFOAIR_HTTPD_INTERNAL_SOCKETS")
        + _define(header, "COMFOAIR_OTHER_LWIP_SOCKETS")
        + _define(header, "COMFOAIR_LWIP_SOCKET_SPARE")
    )
    assert total <= _lwip_max_sockets()


def test_server_uses_the_named_client_count():
    # The budget counts COMFOAIR_HTTP_CLIENT_SOCKETS; a literal here would let the two drift.
    assert re.search(r"cfg\.max_open_sockets\s*=\s*COMFOAIR_HTTP_CLIENT_SOCKETS\s*;", HTTP_C.read_text())


def test_httpd_internal_is_three():
    # esp_http_server: listen + ctrl + msg sockets (httpd_main.c, httpd_server_init).
    assert _define(HEADER.read_text(), "COMFOAIR_HTTPD_INTERNAL_SOCKETS") == 3


def test_spare_is_at_least_one():
    assert _define(HEADER.read_text(), "COMFOAIR_LWIP_SOCKET_SPARE") >= 1


def test_every_socket_user_is_counted():
    header = HEADER.read_text()
    users = _socket_users()
    assert users, "the scan found no socket user at all -- the scan is broken, not the firmware"
    missing = sorted(u for u in users if u not in header)
    assert not missing, f"opens a socket but is not counted in {HEADER.name}: {missing}"
    assert _define(header, "COMFOAIR_OTHER_LWIP_SOCKETS") >= len(users)
