# Its own component, and not for tidiness: the host test environment builds every component
# it can see, and comfoair_http needs esp_http_server.h, which does not exist on the host.
# Keeping the policy separate is what lets the security rule be tested without a network
# stack -- which was the whole point of making it a pure function.
idf_component_register(
    SRCS "comfoair_http_policy.c"
    INCLUDE_DIRS "include"
)
