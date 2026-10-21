# MQTT

MQTT is the main control path. The device publishes every entity's state to a broker, listens for
commands on its own topic tree and, by default, announces itself to Home Assistant through MQTT
discovery (see [Home Assistant discovery](/interfaces/home-assistant-discovery)).

The topic shapes and payloads are decided in `components/comfoair_ha` (pure, host-tested); the
meaning of an incoming message in `components/comfoair_mqtt_inbound`; the retry timing in
`components/comfoair_mqtt_backoff`; and everything that needs a socket — the esp-mqtt client, its
one publisher task and the event handler — in `components/comfoair_mqtt`.

## Connection

| Setting | Value |
| --- | --- |
| Broker URI | `mqtt://host:port` — plain TCP, no TLS |
| Port | `mqtt_port`, default 1883 |
| Credentials | `mqtt_user` / `mqtt_password`, sent only when a user is set |
| Client id | `device_id` — the twelve lower-case hex digits of the STA MAC |
| Session | clean session |
| Last will | `<prefix>/status` = `offline`, QoS 1, retained |
| Socket timeout | 2 s (`network.timeout_ms`), which esp-mqtt uses for the socket connect, the CONNACK wait and each write |
| Auto-reconnect | off (`network.disable_auto_reconnect`); the device runs its own ladder, see below |
| Buffers | 1536 bytes in and out (`BUFFER_SIZE`) |

The client is built in `comfoair_mqtt_start_client()` (`comfoair_mqtt_client.c`). All of it is
configured at runtime with `POST /api/config` (`mqtt_host`, `mqtt_port`, `mqtt_user`,
`mqtt_password`, `topic_prefix`, `ha_discovery`) and takes effect without a reboot. An empty
`mqtt_host` means "no broker", which is the state every device ships in — nothing is dialled and
nothing is reported as a fault. See [Configuration](/guide/configuration).

### Why the socket timeout is 2 s

esp-mqtt uses the one `network.timeout_ms` for the socket connect, the CONNACK wait and every
socket write, and a QoS 1 `esp_mqtt_client_publish()` writes synchronously. Left at its default of
10 s, a single publish into a broker that has stopped reading its socket parks the publisher task
for ten seconds — no command is handled meanwhile, and the 2 s ceiling on a
[retraction](/interfaces/home-assistant-discovery#cleanup-and-retraction) cannot be honoured. 1 s
is too short the other way: the same timeout bounds name resolution plus the TCP connect, and a
broker named `homeassistant.local` is resolved by mDNS, which routinely takes several hundred
milliseconds on a busy 2.4 GHz network. A dial therefore costs at most 2 × 2 s
(`cfg.network.timeout_ms = 2000` in `comfoair_mqtt_start_client()`).

`CONFIG_MQTT_SKIP_PUBLISH_IF_DISCONNECTED` is not enabled. The firmware gates itself instead:
`comfoair_mqtt_publish_one()` returns false without calling esp-mqtt while the link is down, so no
publish is queued into esp-mqtt's outbox for a connection that does not exist.

::: tip Two devices, one broker
The client id is the full MAC, so two units never knock each other off the broker. If
`mqtt.disconnects` and `mqtt.reconnects` in `GET /api/status` climb together while `connected`
flickers, something else is using the same client id.
:::

## Topic tree

`<prefix>` is the stored `topic_prefix`, by default `comfoair/<device_id>`, e.g.
`comfoair/aabbccddeeff`. The tree is flat: the entity key is the topic segment, the same string
that names the entity in REST and in discovery.

| Topic | Direction | Payload | Retained |
| --- | --- | --- | --- |
| `<prefix>/status` | device → broker | `online` / `offline` | yes, QoS 1 |
| `<prefix>/bus` | device → broker | `online` / `offline` | yes, QoS 1 |
| `<prefix>/<key>/state` | device → broker | bare scalar | yes, QoS 0 (one exception) |
| `<prefix>/<key>/command` | broker → device | bare scalar | must not be |
| `<prefix>/fan/state` | device → broker | `ON` / `OFF` | yes |
| `<prefix>/fan/speed/state` | device → broker | `1`..`3` | yes |
| `<prefix>/fan/preset/state` | device → broker | a preset label, or `None` | yes |
| `<prefix>/fan/command` | broker → device | `ON` / `OFF` | must not be |
| `<prefix>/fan/speed/command` | broker → device | a number | must not be |
| `<prefix>/fan/preset/command` | broker → device | a preset label | must not be |
| `<prefix>/ops/<name>/set` | broker → device | JSON object | must not be |

There is no other topic under `<prefix>` (discovery documents live under `homeassistant/`, see
[Home Assistant discovery](/interfaces/home-assistant-discovery)). Each topic is built by one
function in `comfoair_ha` —
`comfoair_ha_meta_topic()`, `comfoair_ha_state_topic()`, `comfoair_ha_command_topic()`,
`comfoair_ha_fan_topic()`, `comfoair_ha_op_topic()` — and the state builder refuses a key the
registry does not carry, so no retained topic can appear that the [cleanup](/interfaces/home-assistant-discovery#cleanup-and-retraction)
cannot see.

The device subscribes to exactly three patterns, at QoS 1, on every connection:

```text
<prefix>/+/command
<prefix>/fan/+/command
<prefix>/ops/+/set
```

It does not subscribe to `homeassistant/status` or anything outside its own tree.

## Availability: status and bus

Two retained availability topics answer two different questions:

- **`<prefix>/status`** — is this device connected to the broker. The birth message `online` is
  published on every connect; the will publishes `offline` if the connection is lost; and an
  explicit `offline` is published before every deliberate disconnect — every accepted
  `POST /api/config` rebuilds the client — because a broker does not fire the will for a clean
  `DISCONNECT` (`comfoair_mqtt_stop_client()`).
- **`<prefix>/bus`** — is the ventilation unit answering on CAN. It is published from the same
  verdict that drives the `bus-silent` phase (`comfoair_bus_health_available()`), only when the
  verdict changes, and unconditionally on every connect. A device that has never heard the unit
  publishes `offline`. This is what greys out unit readings in Home Assistant when the unit's power
  is cut while Wi-Fi and the broker keep working.

Which entities list `<prefix>/bus` is decided by the registry's cold-start default, not by whether
the entity has a PDO of its own (`bus_backed()` in `components/comfoair_ha/comfoair_ha_payload.c`;
see [Availability](/interfaces/home-assistant-discovery#availability)). "No PDO" would be the wrong
test: `balance_mode`, `ventilation_mode` and the fan carry no PDO of their own but are derived from
the unit's PDOs, and they would stay "available" on a bus that has been dead for an hour.

On connect the order is fixed: `status`, then `bus`, then the subscriptions, then discovery, then
every state. Availability always goes out before the readings it governs. Discovery goes out only
when it is owed — one flag, not a fingerprint of the configuration; see
[When discovery is published](/interfaces/home-assistant-discovery#when-discovery-is-published).

## State topics

`<prefix>/<key>/state` carries a **bare scalar**, not JSON, so a Home Assistant sensor reads it
without a template (`comfoair_ha_state_payload()`):

| Value | Payload |
| --- | --- |
| number | `21.5`, `34` |
| boolean | `ON` / `OFF` |
| text (selects, enums) | the label, unquoted: `Auto`, `Open` |
| no reading | `None` |

`None` is Home Assistant's own "no value" literal, and every component this device publishes reads
it as unknown: sensors (numeric and text) at `mqtt/sensor.py:317`, binary sensors at
`binary_sensor.py:203`, the switch at `switch.py:111`, selects at `select.py:124` (case-insensitive),
numbers through `payload_reset`'s default (`number.py:95`, `const.py:308`) and the fan's three leaves
at `fan.py:347`, `:359` and `:394` (Home Assistant 2026.9). It is the MQTT face of REST's `null`. A
reading that goes away (a countdown that ends, an unlisted enum value, a short frame) therefore
replaces the retained value instead of leaving it on the broker as if current, and the connect sweep
publishes `None` for anything this boot has not read, so no retained value survives a reboot. An
empty payload is never sent: retained, it deletes the broker's copy. No label is ever spelled
`None` (the generator refuses it). The `fan` publishes three retained leaves — `fan/state`,
`fan/speed/state` and `fan/preset/state` — and `OFF` on the fan is the Away level — an HRV never
stops ventilating.

### Retain and QoS rules

| Message | QoS | Retained | Why |
| --- | --- | --- | --- |
| State topics | 0 | yes | a subscriber that missed one gets the current value on subscribe |
| `data_freshness` state | 0 | **no** | Home Assistant's `expire_after` would otherwise be renewed by the retained copy on every resubscribe |
| `status`, `bus`, will | 1 | yes | a missed `offline` leaves a dead device looking alive |
| Discovery documents | 1 | yes | a missed config message is an entity that never appears |

States are published on change, subject to the [publication rate](#publication-rate) below, at
most eight per pass (`PUBLISH_PER_PASS`), from one publisher task. The one publication on a clock is
`data_freshness` (`ON` every 300 s, `COMFOAIR_FRESHNESS_PERIOD_S`), which tells Home Assistant this
node is still talking. On every (re)connect everything is republished, `None` included, with no rate
limit, because a broker that restarted may have lost its retained store.

### Publication rate

States go out on change, but a few numeric rows jitter: the Q350 broadcasts fan speed every ~3 s,
fan flow every 6–8 s and avoided cooling every ~4 s, with ±1 rpm, ±3 m³/h and ±3 W of noise. Those
rows carry a **deadband** and a **minimum interval** in `tools/pdo_table.py`
(`NumericPdo.publish_deadband`, `publish_min_interval_s`), generated into the registry:

| Rows | Deadband | Minimum interval |
| --- | --- | --- |
| `supply_fan_speed`, `exhaust_fan_speed` | 25 rpm | 30 s |
| `supply_fan_flow`, `exhaust_fan_flow` | 5 m³/h | 30 s |
| `avoided_heating`, `avoided_cooling` | 10 W | 60 s |
| the seven air temperatures (220, 221, 274–278) | none | 30 s |

A change smaller than the deadband, or sooner than the interval after the row's last publication, is
held; after `COMFOAIR_PUBLISH_MAX_HOLD_S` (300 s) any **difference** goes out. An unchanged value is
never repeated — the one publication on a clock stays `data_freshness`. A reading appearing or going
away (`None`) is never held, and enums, binaries, selects, the switch, the fan and numbers are never
held at all. So the broker's value is within one deadband of the unit's, or at most one interval
old, and exact within five minutes of the last change. The declared intervals bound the limited
rows at about 0.4 msg/s (four rows at most every 30 s, two every 60 s, seven temperatures every
30 s); the Q350 census (`docs/unit/data/q350-pdos.json`) expects about 0.3 msg/s, against about
1.4 msg/s if every change went out. HV-MQTT-47 measures it.

REST and the WebSocket are **not** limited: `GET /api/state` is the read-back authority and must
return what the unit said, and the WebSocket is a LAN delta with no broker behind it. Both
transports still carry the same entities (criterion 3); MQTT's value is a declared projection of
REST's.

## Commands

Commands arrive on the three subscribed patterns and are decided by `comfoair_mqtt_decide()`
(`components/comfoair_mqtt_inbound/comfoair_mqtt_inbound.c`). The decision is pure; the publisher
task performs it.

### Entity commands

`<prefix>/<key>/command` with a bare payload:

| Entity kind | Payload |
| --- | --- |
| `switch` (`away_mode`) | `ON` or `OFF`, exactly |
| `number` (the `*_duration` entities) | a number of minutes, e.g. `30` — text such as `hello` is refused, not read as 0; a fraction such as `30.7` is accepted and truncated to `30` |
| `select` | the option label, exactly as in the registry, e.g. `Open` |
| `fan` (`<prefix>/fan/command`) | `ON` (resume the last level, or Low) or `OFF` (the Away level) |
| fan speed (`<prefix>/fan/speed/command`) | a number `0`..`3`; 0 is Away (a fraction is truncated) |
| fan preset (`<prefix>/fan/preset/command`) | `Auto`, `Away`, `Low`, `Medium`, `High` or `Boost` — routed to `ventilation_mode` |

```sh
mosquitto_pub -h broker -t comfoair/aabbccddeeff/bypass_mode/command -m Open
mosquitto_pub -h broker -t comfoair/aabbccddeeff/boost_duration/command -m 30
mosquitto_pub -h broker -t comfoair/aabbccddeeff/fan/preset/command -m High
```

### The six operations

`<prefix>/ops/<name>/set` with a JSON object — **the same names and the same body as
`POST /api/ops/<name>`**, parsed by the same function (`comfoair_wire_parse_operation()`) and built
by the same one (`comfoair_command_from_op()`):

| Name | Body |
| --- | --- |
| `boost` | `{"duration": 1800}` — seconds; `0` cancels |
| `set_bypass` | `{"mode": "open", "duration": 3600}` — `auto` \| `open` \| `closed` |
| `set_balance_mode` | `{"mode": "supply_only", "duration": 900}` — `balanced` \| `supply_only` \| `exhaust_only` |
| `set_ventilation_level` | `{"level": 2}` — 0..3, 0 is Away |
| `set_temperature_profile` | `{"profile": "warm"}` — `normal` \| `cool` \| `warm` |
| `set_away` | `{"enabled": true, "duration": 0}` — 0 means indefinite |

Every parameter is required. Durations are seconds.

```sh
mosquitto_pub -h broker -t comfoair/aabbccddeeff/ops/set_bypass/set \
              -m '{"mode":"open","duration":3600}'
```

From a Home Assistant automation the operations are reached with the `mqtt.publish` action; they
are deliberately not discovery entities (see
[Home Assistant discovery](/interfaces/home-assistant-discovery#what-is-not-discovered)).

### What is refused

There is no reply topic. An accepted command increments `mqtt.commands` in `GET /api/status`; a
refused one increments `mqtt.rejected` and logs `command refused: <reason>` to the device log
(`GET /api/log`). The reason is always a fixed sentence, never the payload. Messages dropped by the
event handler before they are decided — fragmented, oversized, or arriving at a full inbox — are
counted in `mqtt.rejected` without a log line.

- **Retained commands are never applied.** A command delivered with the retain flag — typically a
  stale `mosquitto_pub -r` replayed on every reconnect — is refused.
- A topic outside this device's tree is ignored silently and not counted, because a broker is
  shared.
- A fragmented payload, a payload of 512 bytes or more, or a topic of 192 bytes or more is refused.
- At most eight commands wait in the inbox (`INBOX_LEN`); a command arriving while it is full is
  dropped and counted as rejected rather than applied late.
- Values go through the same builders as over REST (`comfoair_command_from_entity()`,
  `comfoair_command_from_op()`): unknown or read-only key, wrong kind, label not in the list, out
  of range, missing operation parameter. One difference: a bare MQTT number is parsed with
  `strtod()` (`comfoair_mqtt_parse_number()`), so a fraction or an exponent is accepted and then
  truncated by the builder, where REST refuses a fraction.
- A number payload must be spelled from `0`–`9`, `+`, `-`, `.`, `e` and `E` only, and must be
  finite after parsing (`comfoair_mqtt_parse_number()` in
  `components/comfoair_mqtt_inbound/comfoair_mqtt_inbound.c`). `strtod()` alone accepts far more:
  `hello` reads as 0 (the Away level on the fan), `2x` as 2, `0x10` as 16, a leading space is skipped, and `nan`,
  `inf` or `1e400` as non-finite values that pass every range check, because every comparison
  against NaN is false — the integer cast that follows is then undefined, and on the device turns
  `nan` into a 65535-minute duration. Both halves are needed: the character filter alone misses
  `1e400`, the finiteness check alone misses `0x10`.
- A duration whose store fails is refused (`the duration was not stored`); REST answers the same
  case with `accepted` and logs a warning.
- `the bus is busy` / `commands are not running` when the command queue is full or the CAN bus never
  started.

Accepted means **queued**, as over REST. The unit's own PDO arriving on the state topic is the
confirmation. Duration entities produce no frame and are stored and republished immediately;
every select written by label is set optimistically, as over REST.

## REST and MQTT cover the same operations

Every write is available on both transports and goes through the same builders, so neither has an
operation the other lacks:

| Operation | REST | MQTT |
| --- | --- | --- |
| Write one entity | `POST /api/entities/{key}` `{"value": ...}` | `<prefix>/<key>/command` |
| Fan on/off, level, preset | `POST /api/entities/fan` with a bool, a number or a label | `<prefix>/fan/command`, `.../fan/speed/command`, `.../fan/preset/command` |
| One of the six operations | `POST /api/ops/{name}` | `<prefix>/ops/<name>/set` |
| Read every value | `GET /api/state`, `/ws` | `<prefix>/<key>/state` |

The paths differ in shape — REST takes typed JSON, MQTT takes bare scalars — but both end in
`comfoair_command_from_entity()` or `comfoair_command_from_op()`. Configuration, provisioning, the
probe and OTA are device management, not ventilation operations, and are REST only.

## Reconnect and backoff

esp-mqtt's own auto-reconnect is switched off. The publisher task runs its own ladder, computed by
the pure `comfoair_mqtt_backoff_ms()`:

| Failure class (`mqtt.failure`) | Waits |
| --- | --- |
| `dns`, `tcp`, `unavailable` | 2 s, 4 s, 8 s, 16 s, 32 s, then 60 s |
| `dropped` (a session that was up) | 1 s, 2 s, 4 s … up to 60 s |
| `protocol` (CONNACK 1, 2), `bad-credentials` (CONNACK 4, 5 or any unknown code) | 300 s, flat |

`unavailable` is CONNACK 3; `dns` is a transport error whose host name did not resolve
(`comfoair_mqtt_classify()`). Every wait is jittered to 80–120 % by a hash of the `device_id` (the
MAC) and the attempt number, so units in one house spread out but each unit follows the same
ladder from boot to boot. The attempt counter saturates at 16 and resets only after
a session has stayed up for 60 s; saving new broker settings also resets it. The ladder never ends
and never reboots the device — a broker that is off for a weekend costs nothing but log lines.

`GET /api/status` shows the ladder in its `mqtt` block:

| Field | Meaning |
| --- | --- |
| `link` | `off` (no broker), `connecting`, `online`, `backoff` (waiting out a rung), `rejected` |
| `failure` | the class above, or `none` |
| `attempts` | failed attempts since the last settled session |
| `retry_in_ms` | time to the next attempt, `null` when none is scheduled |
| `last_connack`, `last_connack_reason` | the broker's refusal code (MQTT 3.1.1 §3.2.2.3) and its spelling from `comfoair_wire_connack_name()` (`0` is `no refusal`, `4` is `bad username or password`) |
| `last_transport_errno` | raw socket errno of the last transport failure, in newlib's numbering (`111` ECONNREFUSED, `113` ECONNABORTED, `116` ETIMEDOUT, `118` EHOSTUNREACH); 0 when none was recorded, which is normal for a connect timeout |
| `applied_epoch` | increments each time the client is rebuilt after a save — poll until it moves and `link` leaves `connecting` |
| `subscriptions` | no producer yet; always `none` |

See [Failure model](/architecture/failure-model) for how a missing broker fits into the device's
overall behaviour.
