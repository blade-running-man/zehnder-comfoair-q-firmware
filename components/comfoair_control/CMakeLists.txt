# The device half of phase 7: a queue, a task, a clock and the transmit path. Everything that can
# be decided without any of those is next door in comfoair_command, where a host test reaches it --
# the same split comfoair_net has against comfoair_provision.
#
# REQUIRES comfoair_command, because that is what a queued item IS, and comfoair_can, because this
# is the only component in phase 7 allowed to transmit. comfoair_config for the NVS key names: the
# durations are stored in cfg_app, and every NVS name in this firmware is written down once.
# comfoair_poll for the one read-back request after a command that moved the away timer: the poll
# task owns the RTR schedule and its frame gap, so the request goes there rather than out of here.
idf_component_register(
    SRCS "comfoair_control.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_command comfoair_can comfoair_config comfoair_protocol comfoair_poll nvs_flash
)
