# The device half of phase 6. Everything that can be decided without a broker is next door: which
# topic and what payload in comfoair_ha, what an arriving command MEANS in comfoair_mqtt_inbound,
# and test/test_ha and test/test_mqtt_inbound reach both -- the same split comfoair_control has
# against comfoair_command and comfoair_net against comfoair_provision.
#
# REQUIRES mqtt for esp-mqtt; comfoair_net for the broker settings, which is the ONE projection of
# the configuration that is not redacted, because MQTT authenticates with the plaintext;
# comfoair_control to submit a command, comfoair_wire to read an operation body, comfoair_state for
# the model it publishes.
#
# REQUIRES comfoair_mqtt_inbound for comfoair_mqtt_decide() and comfoair_mqtt_inbound_t, which is
# both the queue's element type and the decision comfoair_mqtt_handle_inbound() acts on. What is
# left in these files is the ACTING half: submitting, storing, writing the state model, waking the
# browser and moving the counters, in the two orders earlier counter-reviews pinned.
#
# NINE SOURCES, ONE COMPONENT, and comfoair_mqtt_internal.h is what they share. It is NOT in
# include/: the public surface is include/comfoair_mqtt.h alone, and INCLUDE_DIRS below still names
# only that directory, so no other component can reach the internal header even by accident.
# comfoair_mqtt.c is the spine (the publisher task, the statistics, the entry points);
# comfoair_mqtt_command.c is the acting half of an inbound command, what a decision's consequences
# are; comfoair_mqtt_publish.c is what goes out on the wire;
# comfoair_mqtt_client.c builds and destroys the client; comfoair_mqtt_event.c is the esp-mqtt
# event handler, the one file here that does not run on the publisher task; comfoair_mqtt_retry.c
# is the device half of the retry ladder -- the clock, the address and the one call to
# esp_mqtt_client_reconnect(); comfoair_mqtt_retract.c is what the device empties when it stops
# being the thing it was; comfoair_mqtt_discovery.c is what makes Home Assistant know it exists,
# and the only one of the nine whose contents do not go out on every connection;
# comfoair_mqtt_bus.c is <prefix>/bus, the CAN verdict as the second availability topic. Every
# split was line budget (AGENTS.md: 350) and nothing else -- no behaviour moved with any of them.
#
# NOT listed, deliberately: comfoair_can. This component never touches the bus. A command it
# accepts goes into comfoair_control's queue and is sent from there -- publishing or transmitting
# from a broker callback is forbidden outright, because a blocked call
# there stops the task that reads the bus and PDOs are lost.
#
# REQUIRES comfoair_mqtt_backoff for the broker retry ladder: it decides how long to
# wait, what the failure is called and which of the five words /api/status prints, and it is a
# component rather than a file here because PlatformIO's dependency finder works at component
# granularity -- so what is decidable without a broker is host-tested in test/test_mqtt_backoff and
# what needs one is not. SINCE 2026-09-07 IT CARRIES A SECOND LOAD OF THE SAME KIND:
# comfoair_discovery_gate.h, the three conditions under which the discovery
# tree is written again, driven by test/test_discovery_gate. comfoair_wire was already listed and
# now carries a second load: the COMFOAIR_PROV_NEVER sentinel that comfoair_mqtt.h uses for "no
# attempt is scheduled".
#
# REQUIRES comfoair_publish_rate for the per-row deadband / minimum interval rule the publisher
# applies to each state (comfoair_mqtt_publish.c). A component of its own for the same reason as
# comfoair_mqtt_backoff: the rule is host-tested in test/test_publish_rate, this component is not.
idf_component_register(
    SRCS "comfoair_mqtt.c" "comfoair_mqtt_publish.c" "comfoair_mqtt_client.c"
         "comfoair_mqtt_event.c" "comfoair_mqtt_retry.c" "comfoair_mqtt_retract.c"
         "comfoair_mqtt_discovery.c" "comfoair_mqtt_bus.c" "comfoair_mqtt_command.c"
    INCLUDE_DIRS "include"
    REQUIRES mqtt comfoair_ha comfoair_mqtt_inbound comfoair_mqtt_backoff comfoair_net
             comfoair_control comfoair_command comfoair_wire comfoair_state comfoair_publish_rate
             esp_app_format
)
