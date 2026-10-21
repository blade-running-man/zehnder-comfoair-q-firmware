# Reference

The reference section holds two kinds of material: a table generated from the firmware itself,
and the source documents the firmware was built from.

## Entities

[Entities](/reference/entities) lists every entity the firmware exposes — key, name, unit, device
class, PDO, Home Assistant component, options and bounds. It is rendered at site build time from
the generated `web/src/api/entities.ts`, so it is always the list the firmware was built with. It
is never edited by hand; see [Entity registry](/development/entity-registry).

## Smart-home standards

[Smart-home standards](/reference/smart-home-standards) records which standards the firmware
speaks (MQTT, Home Assistant discovery, mDNS), why Matter, Thread, Zigbee and the others are not
implemented, the flash and NVS figures behind each verdict, and what an owner without Home
Assistant gets today.

## Source documents

These documents keep their paths because code comments and tests cite them by path and section.
Where one of them and the pages of this site disagree about how this firmware behaves, the site
and the code win.

### Protocol and device

| Document | What it is |
| --- | --- |
| [PDO & RMI reference](/reference/comfoair-pdo-rmi) | The canonical ComfoNet protocol reference: every PDO's type and divider, the RMI frames and command table. The host test vectors come from here. |
| [RMI probe findings (Q350)](/reference/rmi-probe-findings-q350) | First-hand bus evidence from this firmware's read-only RMI probe on a real ComfoAir Q350; it outranks the protocol reference where they disagree. |
| [ComfoAir Q hardware](/reference/comfoair-q-hardware) | The ventilation unit and the ComfoNet bus as the Zehnder manuals describe them, with page citations. |
| [ESP32 nodes and CAN transceivers](/reference/esp32-node-hardware) | The ESP32 boards (including this firmware's M5Stack NanoC6) and CAN transceivers on the other end of the cable, from the M5Stack PDFs, with page citations. |
| [This device: exact figures](/reference/hardware) | Chip, flash, partition table, memory, pins, CAN and logging settings as read from the owner's connected NanoC6 (2026-09-05 snapshot), and how to capture them again. |
