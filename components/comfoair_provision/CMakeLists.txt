# Its own component, and for the same reason comfoair_policy is: this is the one part of
# provisioning that can be decided without a radio, and keeping it out of the component that
# owns esp_wifi is what lets every case in test/test_provision be a line rather than a
# fifteen-minute wait in front of a device.
# TWO SOURCES SINCE 2026-09-06, and the split is the one the file had already drawn for itself with
# a `// --- questions ---` comment: comfoair_provision.c decides (init, the events, the clock) and
# comfoair_provision_query.c answers. 810 lines against a 350-line ceiling, cut when somebody next
# worked inside it, which is what AGENTS.md asks for. The four rules both halves must ask the same
# way cross through comfoair_provision_internal.h -- private, and deliberately not in include/.
idf_component_register(
    SRCS "comfoair_provision.c" "comfoair_provision_query.c"
    INCLUDE_DIRS "include"
)
