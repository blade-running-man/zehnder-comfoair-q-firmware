<script setup>
import { data } from '../reference/entities.data'
// Entity facts on this page come from the generated registry (see /reference/entities), never
// from hand-written numbers or labels that would drift from tools/pdo_table.py.
const byKey = Object.fromEntries(Object.values(data.groups).flat().map((e) => [e.key, e]))
const labels = (k) => (byKey[k]?.options ?? []).join(' / ')
const levels = (byKey.ventilation_mode?.options ?? []).filter((o) => o !== 'Auto' && o !== 'Boost').join(', ')
const range = (k) => `${byKey[k]?.min}–${byKey[k]?.max} ${byKey[k]?.unit ?? ''}`.trim()
</script>

# Commands

Everything the firmware writes to the ventilation unit is an RMI message to node `0x01`. A command
starts as an HTTP request or an MQTT message, and travels through four stages that never mix:

```text
REST  POST /api/entities/<key>  ─┐
REST  POST /api/ops/<name>      ─┼─► comfoair_wire / comfoair_mqtt_inbound   parse the body
MQTT  command topics            ─┘          │
                                            ▼
                               comfoair_command        which bytes (pure, host-tested)
                                            │
                                            ▼
                               comfoair_control_submit  queue, never blocks
                                            │
                                            ▼
                               task comfoair_ctrl       when the bytes go out
                                            │
                                            ▼
                               comfoair_can_transmit    the TWAI transport
```

`comfoair_command` has no radio, clock or flash: it turns a request into at most three RMI frames
and hands them back, so "the bytes that reach the unit" is checked by the `test_command` host suite
rather than only on hardware. Every byte comes from the builders in `rmi_frame.h`; nothing builds a
frame by hand. The transports themselves are described under [REST API](/interfaces/rest-api) and
[MQTT](/interfaces/mqtt).

## The RMI frames

All timer commands go to unit SCHEDULE (`0x15`); the layout is on
[PDO and RMI](/protocol/pdo-rmi#rmi-request-payloads). `T` is the 4-byte little-endian signed
timeout in seconds; `FF FF FF FF` is −1, no expiry.

| Builder | Bytes |
| --- | --- |
| ventilation level `L` (0–3) | `84 15 01 01 00 00 00 00 01 00 00 00 L` (timeout 1 s) |
| manual mode on | `84 15 08 01 00 00 00 00 01 00 00 00 01` (timeout 1 s) |
| manual mode off | `85 15 08 01` |
| boost on | `84 15 01 06 00 00 00 00 T 03` |
| boost off | `85 15 01 06` |
| away on | `84 15 01 0B 00 00 00 00 T 00` |
| away off | `85 15 01 0B` |
| bypass open | `84 15 02 01 00 00 00 00 T 01` |
| bypass closed | `84 15 02 01 00 00 00 00 T 02` |
| bypass auto | `85 15 02 01` |
| supply only on / off | `84 15 06 01 00 00 00 00 T 01` / `85 15 06 01` |
| extract only on / off | `84 15 07 01 00 00 00 00 T 01` / `85 15 07 01` |
| temperature profile `P` (0 Normal, 1 Cool, 2 Warm) | `84 15 03 01 00 00 00 00 FF FF FF FF P` |
| sensor ventilation select (0 Off, 1 Auto only, 2 On) | `03 1D 01 <prop> <index>`, `<prop>` = `04` temperature passive, `06` humidity comfort, `07` humidity protection |

Details that look like mistakes and are not:

- Level and manual-mode changes carry a **1-second** timeout, not −1 (`TIMEOUT_STICKY` in
  `rmi_frame.h`). Why: the [PDO/RMI reference](/reference/comfoair-pdo-rmi) records this as what the
  official app sends.
- The value byte is `01` for **both** supply-only (subunit `06`) and extract-only (subunit `07`).
- The away-on value byte is `00`.
- The temperature profile has no duration; it is always sent with no expiry. There is no "auto"
  profile: the firmware has no builder for a release `85 15 03 01`, because that frame has never
  been observed or verified — by analogy with the other subunits it should be the release, but it
  is a hypothesis, not a documented command (see the [PDO/RMI reference](/reference/comfoair-pdo-rmi)).

## The six operations

`POST /api/ops/<name>` and the matching MQTT operation topics take a JSON body. **Every parameter
is required** — none has a default — and a missing one is refused with
`This operation has no optional parameters; one is missing.` An operation carries its own
duration; it never reads the stored duration settings, so a mode and its duration arrive and are
applied as one request.

| Operation | Parameters | Frames |
| --- | --- | --- |
| `boost` | `duration` | `duration` > 0: boost on for that many seconds. `duration` ≤ 0: boost off |
| `set_bypass` | `mode` (`auto`, `open`, `closed`), `duration` | open/closed with the timeout; `auto` is a release and ignores the duration (which must still be present) |
| `set_balance_mode` | `mode` (`balanced`, `supply_only`, `exhaust_only`), `duration` | **two** frames, always (below) |
| `set_ventilation_level` | `level` (0–3) | manual mode on (if permanent manual is on), then the level |
| `set_temperature_profile` | `profile` (`normal`, `cool`, `warm`) | one profile frame, no expiry |
| `set_away` | `enabled`, `duration` | `enabled: true`: away on; `false`: away off, duration ignored |

`duration` is in **seconds** and signed. For `set_bypass`, `set_balance_mode` and `set_away`, 0 or
any negative number means **indefinite** (−1 on the wire); for `boost`, 0 or negative means
**cancel**. A fractional duration such as `18.5` is refused as a bad field rather than truncated,
because reading it as −1 would mean "forever". Level 0 is the Away *level* — the lowest fan
setting — not away *mode* and not "off".

```json
POST /api/ops/set_bypass
{"mode": "open", "duration": 3600}
```

Balance mode always sends both subunits, because leaving the other one's timer running would let
"extract only" followed by "supply only" leave both timers set:

| `mode` | Frame 1 | Frame 2 |
| --- | --- | --- |
| `balanced` | supply only off | extract only off |
| `supply_only` | supply only on, `duration` | extract only off |
| `exhaust_only` | extract only on, `duration` | supply only off |

## Writing an entity

`POST /api/entities/<key>` with `{"value": …}` (and the equivalent MQTT command topic) writes one
of the {{ data.writable }} writable entities listed in the [entity reference](/reference/entities). Selects are
written by their option **label**, matched exactly including case — never by index — because the
option order is protocol order and the nearest label is a different command. Writing an entity
uses the stored durations, in minutes, converted to seconds once where the frame is built.

| Entity | Value | What is sent |
| --- | --- | --- |
| `fan` | `false` | level 0 (an HRV never stops; "off" is the Away level) |
| `fan` | `true` | the level the unit was last seen running at (PDO 65), or level 1 if none is known or the last level seen was 0 |
| `fan` | 0–3, or a preset label | that level, or the preset as for `ventilation_mode` |
| `ventilation_mode` | a level preset ({{ levels }}) | level 0–3 in that order (with manual mode on first, if enabled) |
| `ventilation_mode` | `Boost` | boost on for `boost_duration` |
| `ventilation_mode` | `Auto` | boost off, away off, and — **1000 ms later** — manual mode off |
| `bypass_mode` | an option label ({{ labels('bypass_mode') }}) | as `set_bypass`, with `bypass_duration` |
| `balance_mode` | an option label ({{ labels('balance_mode') }}) | as `set_balance_mode`, with `balance_duration` |
| `away_mode` | `true` / `false` | away on for `away_duration` (0 = indefinite) / away off |
| `temperature_profile` | an option label ({{ labels('temperature_profile') }}) | the profile frame |
| the three sensor ventilation selects, e.g. `humidity_comfort` | an option label ({{ labels('humidity_comfort') }}) | one SET PROPERTY on unit `0x1D` |
| `boost_duration`, `bypass_duration`, `balance_duration`, `away_duration` | minutes | **no frame** (below) |

### The durations

The unit is told a duration only as part of a command, so writing one of the four duration entities
sends nothing. The value is stored in NVS by `comfoair_control_store_duration()`, takes effect for
the next command at once, and is written into the state model so every projection shows it. The
range check is the registry's (for example `boost_duration` {{ range('boost_duration') }}); the
ranges and defaults live in `tools/pdo_table.py`, and the ranges are listed in
[Entities](/reference/entities). The four values are loaded from NVS at boot when the command sender
starts, falling back to the registry defaults.

If the NVS write fails, the new value is already in effect in the sender. Over REST the write is
still answered as accepted, written into the state model, and a warning is logged. Over MQTT it is
counted as rejected (`the duration was not stored`) and the state model is **not** updated. With no
command sender running, nothing is stored: REST answers `503 commands are not running` and MQTT
rejects with the same reason.

### Permanent manual mode

With **permanent manual** on — the default (`comfoair_command_durations_defaults()`) — every level
change is preceded by "manual mode on" (`84 15 08 01 …`). With it off, a level command is sent on
its own. What the unit does with each, according to the [PDO/RMI reference](/reference/comfoair-pdo-rmi):
a bare level command is a *temporary* change that reverts to the schedule at the next scheduler step
or after at most two hours, and in manual mode the unit's display warns that demand-controlled
ventilation is ignored. The setting is the
`permanent_manual` field of `POST /api/config` (see [Configuration](/guide/configuration)) and is
stored in NVS. The "Auto" preset releases it again.

## The command task

`comfoair_control` owns everything that cannot be host-tested: the queue, the clock and the
transmit path. A request handler never touches the bus — `comfoair_can_transmit()` waits for the
driver, and the HTTP server serves every socket from one task.

| Property | Value |
| --- | --- |
| Queue | 8 commands; `comfoair_control_submit()` never waits |
| Task | `comfoair_ctrl`, priority 4 (above the probe, below polling and HTTP), 3 KiB stack |
| Per-frame delay | honoured before the frame; only "Auto"'s third frame has one (1000 ms) |
| Sequence number | fresh for each RMI frame, from the counter shared with the probe |
| Transmit timeout | 200 ms per CAN frame |
| Started | only when the CAN controller came up |

For each RMI frame the task builds the identifier (source `0x2A`, destination `0x01`,
AnotherCounter 0, is-request), splits the frame into CAN frames and sends **every** chunk, even
after one fails — abandoning the rest would leave a truncated multi-frame message on the bus. The
remaining frames of a command are also sent after one fails: a balance-mode command's second frame
is what releases the other subunit's timer.

There are **no retries** above the driver. The TWAI driver retransmits each CAN frame up to three
times; after that the command is counted as failed, `frame N of M was not acknowledged` is logged at
WARN, and the command is finished. Nothing retries forever and nothing reboots: a unit that does not
acknowledge is a unit that is switched off.

## How results are reported

A command is **accepted, not confirmed.** The firmware does not read the unit's RMI response, so a
refused command looks exactly like an accepted one at the moment of sending. The confirmation is the
unit's own PDO broadcast arriving afterwards and reaching clients like any other change.

| Outcome | REST | MQTT |
| --- | --- | --- |
| Queued (or a duration stored) | `200 {"accepted":true}` | counted in the `mqtt` block's `commands` |
| Body is not JSON | `400` | rejected |
| Field of the wrong type | `422` | rejected |
| Unknown entity or operation (including read-only entities) | `404 No such writable entity.` / `No such operation.` | rejected |
| Value or label not accepted, or a parameter missing | `422` | rejected |
| Queue full | `503 the bus is busy` | rejected: `the bus is busy` |
| Command sender not running (no CAN bus) | `503 commands are not running` | rejected: `commands are not running` |

On MQTT a rejection is counted in `rejected` (the `mqtt` block of `GET /api/status` and
`GET /api/system`) and logged as
`command refused: <reason>` — the reason, never the payload.

After a command is queued, every select written by its label — over REST or MQTT — is set in the
state model at once. For the three sensor ventilation selects the next unit read (every 6 h, or
sooner after a skipped read) confirms it; the PDO-backed selects (`bypass_mode`, `temperature_profile`) and the derived ones (`balance_mode`,
`ventilation_mode`) are overwritten by the unit's own PDOs when they next arrive. The four durations
show the stored value at once (see above). `away_mode` is deliberately **not** optimistic: its state is PDO 337
bit 11, the away timer the command starts and cancels. The Q350 census never heard PDO 337 between
sweeps; whether the unit broadcasts it on change is unconfirmed, so the device requests it about 1.5 s after every command that starts or cancels that timer — the
switch, `set_away`, and the `Auto` preset's away release (`comfoair_command_readback_pdo()`, sent
through `comfoair_poll_request_pdo()`) — and that answer confirms it ([HV-MQTT-31](/hardware-verification/06-mqtt) records the delay).
Showing away for a command
the unit refused would be worse than showing it a second and a half late.

To see why the unit is not running at the requested airflow after a command, read the
`airflow_constraints` sensor (decoded from PDO 230, see
[PDO and RMI](/protocol/pdo-rmi#airflow-constraints-pdo-230)).

The sender's counters appear in the `commands` block of `GET /api/system`:

| Field | Meaning |
| --- | --- |
| `queued` | commands accepted into the queue |
| `sent` | commands whose every CAN frame was acknowledged |
| `failed` | commands with at least one unacknowledged frame |
| `dropped` | commands refused because the queue was full |
