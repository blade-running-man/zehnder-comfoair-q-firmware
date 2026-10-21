# The MQTT retry ladder: how long to wait and what to call the reason, as a pure decider that
# builds on the host (docs/interfaces/mqtt.md, "Reconnect and backoff").
#
# A COMPONENT DIRECTORY AND NOT A FILE INSIDE comfoair_mqtt, for the reason platformio.ini gives:
# PlatformIO's dependency finder works at COMPONENT granularity, so a component either builds on
# the host or it does not. comfoair_mqtt cannot -- it is esp-mqtt, FreeRTOS and a client handle --
# and a half-host-buildable component would make "the board- and IDF-bound components are never
# built here" false. Same argument, and same wording, as comfoair_mqtt_inbound's own CMakeLists.
#
# ONE COMPONENT AND NOT TWO: a separate comfoair_mqtt_verdict component for the link word was
# considered and deliberately not created. Two enumerations over one set of events are two
# spellings of one fact and they diverge on the first edit -- which is also why
# comfoair_mqtt_link_t lives here beside the failure classes instead of in comfoair_wire.
#
# NO REQUIRES, AND THE EMPTINESS IS THE POINT. Not mqtt, not esp_common, not comfoair_mqtt, not
# comfoair_phase -- although comfoair_phase_connack_is_refusal() reads the very same CONNACK code
# space (comfoair_phase.h:186-192). The agreement between the two readers is pinned by
# test_the_refusal_classes_agree_with_the_phase_reader in test/test_mqtt_backoff, which is a test
# dependency and not a build one: a REQUIRES here would drag comfoair_provision behind a header
# whose whole contract is that it needs nothing.
#
# TWO SOURCES, ONE HEADER, and the seam between them is the one the component already had: the
# TABLE of rungs in comfoair_mqtt_backoff.c, the publisher task's PASS over it in
# comfoair_mqtt_ladder.c. The split is the 350-line ceiling (AGENTS.md) and nothing else -- no
# behaviour moved with it, and the 16 the two share is spelled once, in the header, as
# COMFOAIR_MQTT_ATTEMPT_MAX.
#
# THREE LODGERS SINCE 2026-09-07, and they are lodgers rather than further spellings of the ladder.
# Both are here for the only property this directory has that comfoair_mqtt does not -- IT BUILDS
# ON THE HOST -- and both arrived from the same finding, one task apart: a condition written inside
# comfoair_mqtt is checked by nothing at all, so removing it altogether leaves every host case
# green. The second is comfoair_discovery_gate.c -- the three rules (one bool, not a fingerprint;
# its header argues why) under which the discovery tree is published again, driven by
# test/test_discovery_gate. It shares no line with the ladder either, and costs this component
# nothing for the same reason: it needs nothing at all.
#
# The third: comfoair_device_url.c, which decides when the `configuration_url` on the Home
# Assistant device card has stopped describing this device. It is here for the same one reason and
# on the same one finding: the url was derived once, inside comfoair_mqtt_client.c, at a moment
# when the device had no address yet, and no host case in this repository could see that -- or can
# see the condition that replaces it -- from inside that component. Its own header carries the
# argument for why the condition is "the url moved" and not "the url is empty". It needs nothing
# at all either.
#
# The first: comfoair_retract_budget.c has nothing to do with retrying and shares not
# one line with the two above. It is here because of the only property this directory has that
# comfoair_mqtt does not -- it builds on the host. The retraction ceiling lived inside
# comfoair_mqtt_retract.c, which [env:native] never compiles, and REMOVING IT ALTOGETHER LEFT EVERY
# HOST CASE GREEN. Its own header says why it is a loop condition and not a predicate; the arrival
# costs this component nothing, because it needs exactly what the component already requires:
# nothing at all.
idf_component_register(
    SRCS "comfoair_mqtt_backoff.c" "comfoair_mqtt_ladder.c" "comfoair_retract_budget.c"
         "comfoair_discovery_gate.c" "comfoair_device_url.c"
    INCLUDE_DIRS "include"
)
