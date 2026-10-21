# Communication and network

This area is what the ventilation unit is connected to. The unit is a node on the ComfoNet CAN
bus, the same bus the firmware joins, and it keeps three RMI units about its connections: COMFOBUS
(`0x02`) for the wired ComfoNet bus, RFCOMMUNICATION (`0x1B`) for wireless ancillaries paired
through a ComfoSense C, and PERIPHERALS (`0x24`) for registered accessories. The firmware takes
part in the bus as a node of its own and keeps its own record of which nodes it hears; it does not
use the unit's three communication units.

<UnitAreaSummary area="communication-and-network" />

## Firmware reads

The firmware reads no PDO and no RMI property in this area. What it knows about the bus it learns
from the frames themselves:

- **Node census.** Every PDO broadcast, RMI frame and heartbeat (`0x10000000 | node`) the bus
  delivers records its source node in a 64-bit set, from boot onwards
  (`components/comfoair_probe_task/comfoair_probe_task_bus.cpp:92`). The hook runs on every
  received extended-id data frame — remote-transmission requests and standard-id frames are
  dropped first (`src/main.cpp:58`) — before the PDO and RMI handling (`src/main.cpp:64`), and
  needs no RMI exchange,
  so it is filled even on a boot where the probe task did not start. Details:
  [Probe — the node census](/protocol/probe#the-node-census).
- **Bus liveness.** Whether the unit is talking at all is decided from the received-frame counter,
  for the bus as a whole; any frame from any node counts
  ([Polling — when the unit stops answering](/protocol/polling#when-the-unit-stops-answering)).

## Firmware writes

The firmware writes nothing in this area. It speaks on the bus as node `0x2A`
(`components/comfoair_control/include/comfoair_control.h:33`) for its PDO requests, its commands
and its RMI reads, and sends its commands and its timed identity read only to node `0x01`
(`components/comfoair_control/include/comfoair_control.h:40`); only a probe walk addresses other
nodes (below).

## Unit settings

The three units are the unit's interface for its connections; all of their properties are
undocumented or inferred:

- **COMFOBUS** answers three u8 bytes ([2.1.2](/unit/properties#prop-2-1-2)–[2.1.4](/unit/properties#prop-2-1-4))
  and a read-only u32 ([2.1.5](/unit/properties#prop-2-1-5)) shaped like a version word.
- **RFCOMMUNICATION** answers two read-only u8 bytes ([27.1.2](/unit/properties#prop-27-1-2),
  [27.1.3](/unit/properties#prop-27-1-3)) and three bounded u8 values: a three-way value
  ([27.1.4](/unit/properties#prop-27-1-4), range 0–2) and a two-way value
  ([27.1.5](/unit/properties#prop-27-1-5), range 0–1), inferred from their domains to be the
  display's RF SENSOR PRIORITY (off / auto only / on) and RF SENSOR FUNCTION (flow proportional /
  flow preset) settings, and [27.1.6](/unit/properties#prop-27-1-6), range 0–100, undocumented.
  These settings act only with an RF ancillary paired through a ComfoSense C.
- **PERIPHERALS** answers ten u8 slots, [36.1.2](/unit/properties#prop-36-1-2)–[36.1.11](/unit/properties#prop-36-1-11),
  all 0 on the reference unit, which has no accessory on its bus.

The firmware does not read or write these settings.

<UnitPropertyTable area="communication-and-network" />

## Behaviour

- **REST.** The census is `bus.nodes_seen`, an ascending array of node ids, in `GET /api/system`
  (`components/comfoair_http/comfoair_http_system.c:167`;
  [REST API](/interfaces/rest-api#get-api-system)), beside the CAN counters of the same `bus`
  block. A probe report carries the same list as `nodes_seen`. The web UI's System page shows it
  (`web/src/pages/system/sections.ts:128`).
- **Probe walks.** A walk also reads the NODE identity of up to eight other nodes from the census,
  skipping node 0, the walk's target and the firmware's own node
  (`components/comfoair_probe_task/comfoair_probe_task.cpp:204`). This is the only use of the
  census beyond reporting it.
- **MQTT and Home Assistant.** The census is not published on MQTT. Bus liveness is: the retained
  `<prefix>/bus` availability topic goes `offline` after 90 s without a frame
  (`components/comfoair_phase/include/comfoair_bus_health.h:59`), and Home Assistant
  greys out the entities the bus feeds ([MQTT](/interfaces/mqtt)).
- **No other node is addressed.** A ComfoSense, LAN C or Option Box on the same bus is recorded
  in the census; the firmware sends commands only to node `0x01`. A received PDO is decoded by its
  PDO number alone, whichever node sent it (`src/main.cpp:73`).

::: tip Two different things are numbered 0x2A
The firmware's CAN node id is `0x2A`, and so is the number of an undocumented RMI unit inside node
`0x01`, described under [Other units](/unit/areas/other). They are addresses in different fields of
different frames and have nothing to do with each other.
:::
