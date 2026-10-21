# 01 · Boot and resilience

What a boot prints, and the one rule that holds through every other scenario: **nothing reboots
because a peer is absent** — not the unit, not the broker, not a browser. The device has exactly
two deliberate restarts: into a new image after an upload, and after an erasing button release.

As-built: [Runtime](/architecture/runtime), [Failure model](/architecture/failure-model),
[CAN bus](/protocol/can-bus).

| ID | Step | Expected | Observe | Needs |
| --- | --- | --- | --- | --- |
| HV-BOOT-01 | Boot the device **without** the CAN loom attached, watch the serial log | In order: `comfoair-q <version>`; `board m5stack-nanoc6: CAN tx=2 rx=1 @ 50000 bps, button=9 (download strap: hold AFTER boot only)`; `indication up: heartbeat yes, colour yes`; `web UI compiled in: N B`, where N equals the sum of the sizes of `web/dist/index.html.gz`, `app.js.gz` and `style.css.gz` from the same build; `can: up on tx=2 rx=1 at 50000 bps`; `commands ready: …`; `http: documents: state N B, entities N B, buffer 49152 B`; `http: listening on :80 (…)`. Record the first ~30 lines | serial log; `GET /api/log` holds the same lines | USB |
| HV-BOOT-02 | Same boot. If instead of `can: up` the log says `CAN did not start: <err> -- continuing without the bus` | The device **continues**: Wi-Fi, web UI and the setup network come up; `GET /api/status` reports `phase: "fault"` and the LED is solid red. Record the error code — it means the driver could not take the controller. Either outcome passes; a stop or a reboot fails | serial log; `curl -s http://<ip>/api/status` | USB |
| HV-BOOT-03 | **The no-reboot soak — one item, run once under each condition:** (a) unit not connected or unpowered, (b) broker switched off, (c) no browser or client connected. Leave the device 30 minutes in each | No `rst:` line, no panic, no second boot banner. `uptime_ms` in `GET /api/status` grows by the elapsed time across the whole soak; `device.reset_reason` in `GET /api/system` is unchanged; the web UI answers at once throughout; with the unit connected, the ventilation keeps running. 30 minutes because the replaced firmware rebooted every 15 minutes with an unreachable broker | `curl -s http://<ip>/api/status \| grep -o '"uptime_ms":[0-9]*'` at start and end; serial log | USB, broker |
| HV-BOOT-04 | Device running and reading the unit. Unplug the CAN loom for 60 s, plug it back | While unplugged: the web UI stays reachable, `GET /api/state` answers, the values freeze. After replugging: values move again and `bus.rx_frames` in `GET /api/system` grows, **without a reboot** | dashboard; `curl -s http://<ip>/api/system`; serial log | unit, USB |
| HV-BOOT-05 | During any of the above, watch for a start branch that fails: `indication did not start`, `button did not start`, `PDO request cycle did not start`, `RMI probe did not start`, `push task did not start` | Each is logged and the device carries on with what remains; none of them restarts the device | serial log | USB |
