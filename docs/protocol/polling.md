# Polling

The ventilation unit broadcasts most of its PDOs on its own, and those broadcasts are the
firmware's primary source of data: every one is decoded the moment it arrives (see
[PDO and RMI](/protocol/pdo-rmi)). Polling, done by `components/comfoair_poll`, is only a
**refresh** for the values the unit sends rarely or not at all unprompted.

## The request cycle

A PDO is requested by sending a remote-transmission request (RTR) with the PDO's identifier and no
data; the unit answers with the ordinary broadcast, which goes through the same receive path as any
other.

| Parameter | Value | Source |
| --- | --- | --- |
| PDOs per sweep | one request per entry of `COMFOAIR_REQUEST_PDOS` | `COMFOAIR_REQUEST_PDO_COUNT` in the generated registry |
| Interval between sweeps | 3 600 000 ms (one hour) | `COMFOAIR_POLL_DEFAULT_CONFIG().interval_ms` |
| Gap between two requests | 100 ms | `frame_delay_ms` |
| Duration of one sweep | about 6.0 s (60 requests) | `COMFOAIR_REQUEST_PDO_COUNT` × 100 ms nominal, plus up to 100 ms per request the driver is still waiting on |
| Sender node | `0x2A` | `local_node_id`; must be 1–63 |
| Transmit timeout per request | 100 ms | `comfoair_poll.cpp` |
| Task | `comfoair_poll`, priority 5, 3 KiB stack | `comfoair_poll_start()` |

`comfoair_poll.h` asks for a reason before the interval or the gap is shortened, because a sweep
of several dozen back-to-back requests on a 50 kbit/s bus is a burst the unit has no reason to
expect.

- **First sweep at boot.** The task sweeps immediately when it starts, so the first state is
  complete rather than waiting an hour for the rare values.
- **No drift from the gaps.** The next sweep is due one interval after the previous one
  *started*, so the period stays one hour rather than one hour plus a sweep.
- **Failures are skipped, not retried.** A request the driver does not get out within 100 ms is
  logged at DEBUG (`request for PDO … failed: …`) and the sweep moves to the next PDO; the next
  sweep tries again. Nothing reboots because the unit is silent.
- **Extra sweeps.** `comfoair_poll_request_now()` wakes the task for one sweep out of schedule.
  Notifications collapse, so several requests while a sweep is pending still produce one sweep.
  Its one caller is the bus-silence check described below.
- **One read-back after an away command.** `comfoair_poll_request_pdo(pdo, delay_ms)` sends ONE
  request for one PDO after a delay. `comfoair_control` calls it about 1.5 s after sending any
  command that starts or cancels the away timer (SCHEDULE subunit 1, timer 11 — the `away_mode`
  switch, `set_away`, and the `Auto` preset's away release), for the switch's own PDO from the
  registry (`comfoair_command_readback_pdo()`). The Q350 census never heard PDO 337 between sweeps;
  whether the unit broadcasts it on change is unconfirmed, so without the request the switch could
  show the old state until the next hourly sweep ([HV-MQTT-31](/hardware-verification/06-mqtt)
  records the delay). Calls before it goes out
  coalesce into one request, 1.5 s after the last command. The read-back keeps the 100 ms gap: if
  it falls due during a sweep it goes out between two sweep frames.
- **The decisions are host-tested.** What goes out next and how long the task sleeps are decided
  by the pure `comfoair_poll_plan` (suite `test/test_poll_plan`); the task only reads the clock,
  sends and sleeps.

The cycle starts only when the CAN controller came up. If it fails to start, `app_main` logs
`PDO request cycle did not start: … -- the unit still broadcasts` and records a fault; the
broadcasts are still decoded.

### Which PDOs are requested

The list is generated from `tools/pdo_table.py` (`ALL_REQUEST_PDOS`) into
`components/comfoair_registry/include/registry_generated.h` as `COMFOAIR_REQUEST_PDOS`, together
with the entities; it is never written by hand, and it is not copied here so that it cannot drift.
It is every PDO an entity is read from — the **PDO** column of the generated
[entity reference](/reference/entities) — plus the PDOs the fan and the balance mode are derived
from.

## How fresh a value is

The state model keeps **one current value per entity and no timestamp**. A value stays what the
last frame said until a new frame replaces it; there is no per-entity age and no per-entity expiry.
A frame carrying the unit's "nothing pending" sentinel, or one that cannot be decoded, clears the
value to *none*. A repeated broadcast of an unchanged value is not reported as a change, so REST,
WebSocket and MQTT updates follow changes, not traffic.

Some writable entities are never reported back by any PDO — the sensor ventilation selects and the
durations. The durations hold what was last written to them and are loaded from NVS at boot. The
three sensor ventilation selects are not persisted: they are unknown until the probe task's
[unit read](/protocol/probe#the-unit-read) answers (about 10 s after boot, every 60 s until it is complete, then every 6 h), and are
then replaced by what the unit holds. The same task reads the unit's active errors every 60 s.

Staleness is decided once, for the bus as a whole, as described next.

## When the unit stops answering

A pure state machine, `comfoair_bus_health_pass()` in `components/comfoair_phase`, is run once a
second by the phase monitor task. Its only input from the bus is the transport's `rx_frames`
counter — **any** received frame counts, including PDOs no entity uses and frames from other nodes.
What matters is whether the counter *moved* since the last pass, which keeps the check correct when
the 32-bit counter wraps.

| State | Condition | Effect |
| --- | --- | --- |
| `never-seen` | no frame since the controller started | MQTT bus availability `offline`; **not** reported as a silent bus |
| `fresh` | a frame within the last 60 s | bus availability `online` |
| `probing` | no frame for 60 s | **one** extra sweep is requested; still `online` |
| `silent` | no frame for 90 s | bus availability `offline`; device phase `bus-silent` |

The thresholds are 2 × and 3 × one constant, `COMFOAIR_BUS_QUIET_MS` = 30 000 ms, so they cannot
drift apart. The sweep is asked for only on the pass that *enters* `probing` — a sweep every minute
would multiply the bus traffic sixty-fold. The first frame to arrive returns the state to `fresh`
immediately, with no debounce, and a new silence later gets its own single extra sweep.

The verdict reaches the owner in two places, and they are the same fact:

- the retained MQTT availability topic `<prefix>/bus` (`online` / `offline`), which Home Assistant
  uses to grey out the entities the bus feeds — see [MQTT](/interfaces/mqtt);
- the device phase `bus-silent`, shown by the LED and in `GET /api/status` — see
  [Indication](/device/indication) and [Failure model](/architecture/failure-model).

What does **not** happen when the unit stops answering:

- REST and the WebSocket keep serving the last values; they carry no per-value staleness.
- Nothing reboots, and the hourly sweep continues. Requests to an unpowered unit are simply not
  acknowledged: the driver retransmits each frame up to three times and the send fails.
- The CAN controller is not restarted. Only a real bus-off triggers recovery
  (see [CAN bus](/protocol/can-bus#errors-and-bus-off)).

::: tip Not the same as `data_freshness`
The `data_freshness` binary sensor is a different mechanism: the device publishes it to MQTT every
300 s and Home Assistant expires it 900 s after the last one. It says whether the *device* is still
publishing, not whether the *unit* is still talking. See [MQTT](/interfaces/mqtt).
:::

::: warning Thresholds not yet measured
How often the unit broadcasts its rarest PDO on its own has not been measured. The 60 s and 90 s
thresholds are argued from the length of one sweep, not from a measured broadcast period. If a healthy unit
ever goes quiet for more than 60 s, the firmware will send an extra sweep — which, being answered,
returns the state to `fresh` without the bus ever being reported offline.
:::
