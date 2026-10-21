# Pure, and the whole reason it is its own component rather than three files inside
# comfoair_http: what a body MEANS can be decided without a socket, and a decision that needs a
# socket can only be exercised by flashing a board. The bodies this reads are written by whoever
# is in radio range of an OPEN access point, so "only testable on hardware" was not an
# option. Same argument, and same shape, as components/comfoair_policy.
#
# REQUIRES, not PRIV_REQUIRES, for all five: comfoair_wire.h exposes comfoair_wifi_t,
# comfoair_config_patch_t, comfoair_config_err_t, the comfoair_prov_* enums and device_phase_t in
# its own interface, and COMFOAIR_SECRET_SENTINEL travels with them.
#
# comfoair_phase is the fifth and it stays PURE both ways: the status document names the phase, and
# comfoair_phase names nothing but comfoair_provision. It is NOT the monitor -- that component owns
# a FreeRTOS task, and a renderer that could reach it could not be host-tested.
#
# NOT listed, deliberately: esp_http_server, comfoair_http, comfoair_net. This component turns
# bytes into a decision and a decision into bytes; the moment it can send a response it can answer
# a request, and then the access policy in comfoair_policy has a second door. Same rule, and same
# wording, as components/comfoair_captive and components/comfoair_auth.
# comfoair_mqtt_backoff is the sixth, and it arrives with the same property as the fifth: it is
# PURE both ways. Its CMakeLists has no REQUIRES at all -- not mqtt, not esp_common -- so naming it
# here cannot pull an IDF header into a component whose whole contract is that it builds on the
# host. It is NOT comfoair_mqtt, which is esp-mqtt and FreeRTOS and must never appear here.
#
# WHAT IT IS FOR: the broker block of GET /api/status names its link, its failure class and its
# subscription state, and those three words are spelled ONCE, over there, beside the decisions that
# produce them. A copy of the tables in this file would be a second spelling that the publisher
# task's log line and this document drift apart on -- the exact defect comfoair_wire.h records the
# price of for comfoair_prov_failure_name().
#
# FIVE SOURCES: comfoair_wire.c holds the two rules and strerror; the parsers, the renderers, the
# status document and the command parsers each have a file. They share the writer and the result
# constructors through the private comfoair_wire_internal.h (static inline, so nothing new is
# link-visible). Listed one by one: IDF compiles only what SRCS names, while the host build
# compiles every source in this directory, so a file missing here builds green on the host and
# fails to link on the board.
idf_component_register(
    SRCS "comfoair_wire.c"
         "comfoair_wire_parse.c"
         "comfoair_wire_render.c"
         "comfoair_wire_render_status.c"
         "comfoair_wire_command.c"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_json comfoair_config comfoair_provision comfoair_secrets comfoair_command
             comfoair_phase comfoair_mqtt_backoff
)
