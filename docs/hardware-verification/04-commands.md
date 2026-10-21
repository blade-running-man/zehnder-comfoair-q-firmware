# 04 · Commands

The firmware **writes** to the unit here. Read [HV-SAFE-03](./00-setup-and-safety) first and send
the first command watching the log, on a unit you can afford to stop. The items are failure
scenarios more than the happy path: everything after the first group is a way in which the unit
could end up doing something other than what the interface shows.

Every write is fire-and-forget: `{"accepted":true}` means **queued**, and the only confirmation is
the unit's own PDO arriving afterwards. A refused frame looks exactly like an accepted one at the
moment of sending, so **the unit's display is the only independent witness** in this scenario.
Items marked "analyser" can be confirmed on the wire only with a CAN analyser; without one they are
checked on the display and recorded as such.

As-built: [Commands](/protocol/commands), [REST API](/interfaces/rest-api#post-api-ops-name).

## The happy path

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CMD-01 | Boot | `ctrl: commands ready: boost 60 min, bypass 60 min, balance 60 min, away 0 min, permanent manual on` (the stored durations, or these defaults) | serial log | unit, USB |
| HV-CMD-02 | `curl -X POST http://<ip>/api/ops/set_ventilation_level -d '{"level":2}'` | `{"accepted":true}` **at once**, not after seconds; the unit's display shows the middle level in permanent manual mode; after 5 s `GET /api/state` shows `fan_level` 2 and an `operating_mode` meaning manual. If the display did not change although the answer was `accepted`, that is the fire-and-forget contract — record it as a unit refusal, not a firmware defect | `curl`; unit display | unit |

## Failure scenarios

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CMD-03 | **The most important item here.** Power the unit off (or disconnect the bus). `curl -X POST http://<ip>/api/ops/boost -d '{"duration":600}'`, and while it is pending, `GET /api/state` from a second window | The command answers **immediately** with `{"accepted":true}`, and the parallel `GET` answers immediately too — a send that blocked the web server would mean a dead unit hangs the interface. The log shows `ctrl: frame 1 of 1 was not acknowledged`; `commands.failed` in `GET /api/system` grows by one | `curl`; serial log | unit |
| HV-CMD-04 | Unit still off. Send twenty commands back to back | Some answer `503 {"error":"the bus is busy"}` (the queue holds eight). **No reboot at any point** — a reboot here breaks the rule that nothing reboots because a peer is absent | `curl` in a loop; `uptime_ms` | unit |
| HV-CMD-05 | Balance mode, two frames or none: `curl -X POST http://<ip>/api/ops/set_balance_mode -d '{"mode":"supply_only","duration":600}'`, then, without cancelling, the same with `exhaust_only`; then `balanced` | Display: SUPPLY ONLY with a countdown; then EXTRACT ONLY **and SUPPLY ONLY gone** — both timers running means the second frame did not go and the unit is in an undefined state; `balanced` clears both. Analyser: exactly two frames per call, `84 15 07 01 …` and `85 15 06 01` for `exhaust_only`. If the installer set FIRE PLACE PRESENT, `exhaust_only` is silently refused by the unit — check that `supply_only_active` and `exhaust_only_active` (PDO 70 and 71) did not move before calling it a defect | unit display; `GET /api/state`; analyser | unit |
| HV-CMD-06 | The Auto preset's third frame comes later: start a boost, wait for BOOST on the display, then `curl -X POST http://<ip>/api/entities/ventilation_mode -d '{"value":"Auto"}'` | Display back on AUTO with the schedule in control. Analyser: `85 15 01 06` and `85 15 01 0B` at once, `85 15 08 01` **about one second later** — three frames, not two. If the third goes with the first two the unit stays manual while the interface shows Auto, and only the display reveals it | unit display; analyser | unit |
| HV-CMD-07 | Minutes in the interface, seconds on the wire: `curl -X POST http://<ip>/api/entities/boost_duration -d '{"value":5}'`, then `GET /api/state`; then `curl -X POST http://<ip>/api/entities/ventilation_mode -d '{"value":"Boost"}'`; 5 s later read `next_fan_change_in_raw`; then reboot the device | `boost_duration` = 5 at once and **no frame on the bus** for the duration write (it lives on the device and travels only inside a command). `next_fan_change_in_raw` ≈ 300 — not 5 and not 18 000. After the reboot `boost_duration` is still 5 | `curl`; analyser | unit |
| HV-CMD-08 | Permanent manual mode both ways: with `permanent_manual` on (the default) set a level and wait — it holds. Then `curl -X POST http://<ip>/api/config -d '{"permanent_manual":false}'` and set a level again; wait two hours or until a schedule step | Off: analyser shows **one** frame `84 15 01 01 …` with no `84 15 08 01` before it; the level reverts to AUTO by itself at the next schedule step or within two hours — that is what switching the setting off buys, not a fault; the unit's three touch controls are active again | unit display; analyser | unit |

## Values that must be refused

None of these may reach the bus: the display does not change and no frame is sent.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CMD-09 | `POST /api/ops/set_ventilation_level` `{"level":4}` | `422` | `curl -si` | unit |
| HV-CMD-10 | `POST /api/ops/set_ventilation_level` `{}` | `422` `This operation has no optional parameters; one is missing.` — **not** a default level | `curl -si` | unit |
| HV-CMD-11 | `POST /api/ops/boost` `{"duration":18.5}` | `422` — not "forever" | `curl -si` | unit |
| HV-CMD-12 | `POST /api/ops/set_bypass` `{"mode":"opened"}` | `422` | `curl -si` | unit |
| HV-CMD-13 | `POST /api/entities/temperature_passive` `{"value":"on"}` | `422` — labels are matched with their case | `curl -si` | unit |
| HV-CMD-14 | `POST /api/entities/supply_air_temp` `{"value":20}` | `404 {"error":"No such writable entity."}` — a sensor is not written | `curl -si` | unit |
| HV-CMD-15 | `POST /api/ops/set_temperature_profile` `{"profile":"auto"}` | `422` — there is no command that releases the profile | `curl -si` | unit |
| HV-CMD-16 | `POST /api/entities/boost_duration` `{"value":0}` | `422` — the minimum is 1 | `curl -si` | unit |
| HV-CMD-17 | `POST /api/entities/away_duration` `{"value":0}` | **`200`** — here 0 is legal and means "indefinite" | `curl -si` | unit |

## Zero means opposite things

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CMD-18 | `POST /api/ops/boost` `{"duration":0}` during a boost | Boost is **cancelled**; analyser `85 15 01 06` | unit display; analyser | unit |
| HV-CMD-19 | `POST /api/ops/set_away` `{"enabled":true,"duration":0}` | AWAY **indefinitely**; analyser shows `FF FF FF FF` as the timeout. The same zero means the opposite in HV-CMD-18; a boost that starts on zero means the branches are swapped | unit display; analyser | unit |

## Selects read back from the unit, and concurrency

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CMD-20 | `curl -X POST http://<ip>/api/entities/humidity_comfort -d '{"value":"On"}'`; read `GET /api/state` at once; check the display (ADVANCED SETTINGS › SENSOR VENTILATION); reboot the device | `humidity_comfort` = `On` immediately (set optimistically; no PDO reports it, the unit read confirms it); the display shows the same value. If the display differs, the interface is wrong, and this is the one place it can be. Indirect confirmation: `airflow_constraints` (PDO 230) reacts to the enabled control. After the reboot the value is unknown at first and shows the unit's own value about 10–15 s after boot (the unit read; nothing is stored in NVS) | `GET /api/state`; unit display | unit |
| HV-CMD-21 | From two windows at once: `set_ventilation_level` level 1 and level 3 | Both answer `accepted`; the unit ends in one of the two levels (last writer wins, which one is not defined), **never between them** — for example manual with no level | unit display | unit |
| HV-CMD-22 | From two windows at once: `set_balance_mode` `supply_only` and `exhaust_only` | Both answer `accepted`; never both timers running at once | unit display | unit |

## The permanent temperature profile — last, and deliberately

There is no command to release a temperature profile: one set from the API overrides the unit's
schedule until it is changed **on the unit's own display**.

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-CMD-23 | Note the current profile. `curl -X POST http://<ip>/api/ops/set_temperature_profile -d '{"profile":"warm"}'` | The display shows WARM, and the schedule no longer changes the profile | unit display | unit |
| HV-CMD-24 | Restore the original profile **from the unit's display** | The schedule is back in control. This cannot be done through the API | unit display | unit |
| HV-CMD-25 | (a) Set `humidity_comfort` to a value on the unit's display (ADVANCED SETTINGS › SENSOR VENTILATION) and reboot the ESP. (b) Change it on the display again. (c) With the log open, write one of the three selects from HA while a multi-frame command (for example `set_balance_mode`) is still queued | (a) HA shows the unit's value, not a default, within ~15 s of boot; before that the entity is unknown. (b) The new value appears at the next read (or after a reboot), at most 6 h later. (c) The log shows, once, the INFO line `probe: unit identity read (0 of 6 silent), errors read, settings owed; next in 60 s` (the per-row "skipped"/"dropped" lines are DEBUG and compiled out; repeats of this line while it stays owed are demoted to DEBUG), and a later `... settings read; next in 21600 s`; the written value stays and is not replaced by the unit's pre-write value | HA; log; unit display | unit, HA |
