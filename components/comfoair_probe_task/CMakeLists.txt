# The device half of comfoair_probe: a task, a queue, a mutex and the transmit path.
#
# REQUIRES comfoair_control for the shared RMI sequence counter and the node ids: the probe and the
# command sender speak as the same node to the same unit, and two counters would let a reply to an
# owner's write be taken for the probe's answer. comfoair_protocol for rmi_read.h (the only
# builders the probe may use) and rmi_response.h (reassembly).
#
# REQUIRES comfoair_readback comfoair_state comfoair_lock comfoair_registry for the settings
# read-back (comfoair_probe_task_settings.cpp): the pure decode and command-path guard, the model it
# applies to, the lock the guard and the apply share, and the generated table of rows. None of the
# four depends on anything probe-side, so this adds no cycle; the publishers that DO depend on this
# component are reached through comfoair_probe_task_set_observer(), wired in src/main.cpp.
#
# REQUIRES comfoair_pdo_census for the PDO census the bus side feeds from the receive hook and the
# render streams into GET /api/probe; it is pure and requires nothing, so no cycle. esp_timer for
# the census's millisecond clock: it is NOT among the IDF's common requirements.
#
# REQUIRES comfoair_unit_cadence for WHEN the unit is read (the full read, or the active errors
# alone every minute); pure, requires nothing, so no cycle.
idf_component_register(
    SRCS "comfoair_probe_task.cpp" "comfoair_probe_task_bus.cpp" "comfoair_probe_task_unit.cpp"
         "comfoair_probe_task_settings.cpp"
    INCLUDE_DIRS "include"
    REQUIRES comfoair_probe comfoair_probe_report comfoair_sysinfo comfoair_protocol comfoair_control comfoair_can
             comfoair_readback comfoair_state comfoair_lock comfoair_registry comfoair_pdo_census
             comfoair_unit_cadence esp_timer
)
