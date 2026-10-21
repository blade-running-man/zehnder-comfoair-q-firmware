// The one task that knows what the whole device is doing, and the only place in this firmware
// where the network, the broker, the bus and app_main's seven start branches are read in the same
// moment.
//
// WHY A SECOND COMPONENT AND NOT A SECOND FILE. The host environment builds every .c in a
// component directory (platformio.ini, lib_extra_dirs), and its dependency finder works on the
// same granule, so a FreeRTOS task placed beside comfoair_phase.c would drag FreeRTOS, esp-mqtt
// and the TWAI driver into the test_phase binary and end the sweep over the whole product of
// inputs that suite is built on. It is the same pair
// comfoair_provision/comfoair_net and comfoair_mqtt/comfoair_mqtt_inbound already are.
//
// AND WHAT IS ACTUALLY INSIDE IT IS SMALL, deliberately: five calls, a task and a delay.
// Everything that decides anything -- which read fills which of the seventeen phase inputs, the
// broker hold-off, whether this tick is a transition -- lives in comfoair_phase_collect.h
// next door, is pure, and is swept on the host.
//
// NOTHING HERE REBOOTS ANYTHING, EVER (AGENTS.md). The monitor reads five subsystems
// every second and every one of those reads is safe on a subsystem that never came up; a phase of
// FAULT is a colour and a field in a JSON document, and that is the whole of it.
#pragma once

#include "comfoair_phase.h"
#include "comfoair_phase_collect.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Starts the 1 Hz task.
//
// TASK CONTEXT: app_main, once, LAST -- after comfoair_net_start() and comfoair_mqtt_start(), so
// that every one of the seven branches has already had its chance to report. comfoair_prov_init()
// has run inside comfoair_net_start() by then, which is why the collector is handed machine_ready
// true and BOOT is never a live answer here (comfoair_phase.h:44-47).
//
// 1 Hz, and both bounds are argued: twice slower than the rate at which the indication can show a
// change (comfoair_led's render tick is 25 ms) and two orders of magnitude faster than the shortest
// hold-off it applies (30 s). Cheaper is pointless and faster is too.
//
// Errors the caller must handle -- by logging and carrying on, never by returning:
//   ESP_ERR_INVALID_STATE  already started. The second call changes nothing.
//   ESP_ERR_NO_MEM         the task would not start. THE DEVICE THEN SHOWS FAULT AND NOT BOOT:
//                          comfoair_phase_monitor_get() answers FAULT from that moment, because
//                          otherwise one picture -- white, steady -- would mean both "starting up"
//                          and "the thing that reports my state is dead".
esp_err_t comfoair_phase_monitor_start(void);

// The phase as of the last tick, for GET /api/status. TASK CONTEXT: any task; it reads one
// word and takes no lock. A device whose monitor never started answers FAULT, for the reason above.
device_phase_t comfoair_phase_monitor_get(void);

// One of app_main's seven start branches returned an error.
//
// TASK CONTEXT: any task, and BEFORE THE MONITOR EXISTS is the normal case -- app_main reports the
// bus long before the task that reads the report is created. The bit is therefore held in a static
// that outlives the ordering, and calling this after the monitor is running is equally fine.
//
// STICKY, AND IT HAS TO BE: these are one-time verdicts on starting a subsystem, not live states.
// The live state of the CAN driver is a different fact and arrives by a different road
// (comfoair_can_get_stats().started, comfoair_phase.h's paragraph on the two).
void comfoair_phase_monitor_note_fault(comfoair_phase_fault_t which);

// A firmware partition is being written, or has stopped being written. The caller is the POST
// /api/ota handler (comfoair_http_ota.c, indicate()): true just before esp_ota_begin(), false on
// every exit that did not install an image, and left true on success until the restart. The phase
// outranks even the open access point -- it is the one whose owner instruction is "do not cut the
// power". TASK CONTEXT: any task; one atomic store, read on the monitor's next tick.
void comfoair_phase_monitor_note_ota(bool active);

#ifdef __cplusplus
}
#endif
