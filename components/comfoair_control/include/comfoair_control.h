// The task that puts a command on the bus, and the four durations that live on this node.
//
// comfoair_command decides WHICH bytes; this decides WHEN they go and owns everything that cannot
// be host-tested: the radio's transmit path, the sequence counter, a FreeRTOS queue and a clock.
// The split is the same one comfoair_net has against comfoair_provision, and for the same reason.
//
// WHY A QUEUE AND NOT A CALL FROM THE HANDLER. Two facts, and either one alone would be enough:
//
//  * comfoair_can_transmit() WAITS for the driver, and on a ventilation unit that is unpowered
//    nothing on the bus is ever acknowledged -- comfoair_can.h calls that "the ordinary path
//    rather than the exotic one". esp_http_server serves every socket from ONE task, so a send
//    from a request handler stops the whole web interface for the length of the timeout.
//  * The "Auto" preset's third frame is sent 1000 ms after the first two. A handler cannot wait
//    for that; it has already answered.
//
// NOTHING HERE RETRIES FOR EVER AND NOTHING REBOOTS. A unit that does not acknowledge is a unit
// that is switched off, and the answer to that is a log line -- AGENTS.md: nothing reboots because
// a peer is absent.
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "comfoair_command.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// The node this firmware speaks as. 0x2A is what comfoair_poll already uses for its PDO requests,
// and the two must agree: they are the same node on the same bus.
#define COMFOAIR_CONTROL_LOCAL_NODE 0x2A

// The ComfoAir unit. Commands are addressed to it and to nothing else.
//
// DO NOT make this configurable to "support other nodes". The unit byte inside the RMI frame is
// what decides whether a command is a write or a bricking write, and comfoair_command pins those
// by test; a configurable DESTINATION would add a second address nobody tests.
#define COMFOAIR_CONTROL_UNIT_NODE 0x01

// Brings the sender up. Safe to call once; ESP_ERR_INVALID_STATE afterwards.
//
// Reads the four durations and the manual-mode setting out of NVS. A store that will not answer is
// reported and the table's defaults stand -- a device that cannot read a preference is not a
// device that should refuse to ventilate.
esp_err_t comfoair_control_start(void);

// Queues one built command. Returns as soon as it is queued: the frames go out on the sender's
// task, which is what lets a request handler answer before the bus has been touched.
//
// ESP_ERR_NO_MEM when the queue is full. That is not a case to paper over with a bigger queue: it
// means commands are arriving faster than a 50 kbit/s bus can carry them, and the honest answer to
// the client is "busy", not a command applied minutes later.
esp_err_t comfoair_control_submit(const comfoair_command_t *command);

// The durations and the manual-mode setting, as comfoair_command wants them. Copied under a lock.
void comfoair_control_durations(comfoair_command_durations_t *out);

// Stores one duration, by the registry key comfoair_command_t carries. Persists it, because a
// reboot must not reset the owner's choice. A failed write is reported and the value still takes
// effect in memory -- the change happened, it merely will not survive.
esp_err_t comfoair_control_store_duration(const char *key, uint16_t minutes);

// The owner's manual-mode choice. Persisted the same way.
esp_err_t comfoair_control_set_permanent_manual(bool permanent);

// How many commands have been queued, sent and dropped since boot. Diagnostics: a `dropped` that
// climbs is a bus nobody is listening on, which otherwise looks exactly like a quiet one.
typedef struct {
    uint32_t queued;
    uint32_t sent;
    uint32_t failed;
    uint32_t dropped;
} comfoair_control_stats_t;

void comfoair_control_get_stats(comfoair_control_stats_t *out);

// The next 2-bit RMI SeqNr (CAN-id bits 17-18, rmi_frame.h command_can_id) for a frame this node
// sends. ONE counter for every RMI sender on this node -- the command sender and the probe
// (comfoair_probe_task) both take theirs here. They speak as the same node to the same unit, and a
// reply is matched to its request by source, destination and this number alone: rmi_response.h
// says outright that matching is the caller's job, and the reassembler never looks at it. Two
// private counters would hand both senders the same number, and the unit's reply to an owner's
// write arriving during a probe read would be taken for the probe's answer.
//
// Two bits only promise so much: four frames sent while the probe waits on one reply bring the
// number round again. A probe read waits at most 300 ms and a command is at most three frames, so
// that takes two commands inside one wait -- possible, and visible as a wrong-looking answer in
// the report rather than as anything sent.
//
// Thread-safe, never blocks, and callable before comfoair_control_start(): it takes a statically
// initialised spinlock, so there is no lock that has to exist first.
uint8_t comfoair_control_next_sequence(void);

#ifdef __cplusplus
}
#endif
