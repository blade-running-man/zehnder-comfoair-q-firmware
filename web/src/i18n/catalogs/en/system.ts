// System page (pages/system, route /system): GET /api/system's cards and the copied summary.
//
// THIS object defines the namespace's key set. Every key added here must be added to
// ../de/system.ts, ../nl/system.ts and ../uk/system.ts too: those are typed
// `Record<keyof typeof system, Message>` against this object, so tsc fails the build in each of
// them until it is. Prefix every key "system.". The unit's own strings (model, error texts) and
// the firmware's identifiers (board, partition, chip) come from the device and are NOT translated.
//
// The copied summary ("Copy as text") is ALWAYS rendered from THIS catalog, whatever the UI
// language: it is pasted into a support chat, where the reader is not the owner.
import type { Message } from "../../format.ts";

export const system = {
  "system.heading": "System",
  "system.loading": "Loading…",
  "system.readFailed": "Cannot read the system summary",
  "system.copy": "Copy as text",
  "system.copied": "Copied to the clipboard",
  "system.copyFailed": "The browser refused to copy",
  "system.copy.title": "ComfoAir Q system summary",

  "system.unit.heading": "Ventilation unit",
  "system.unit.model": "Model",
  "system.unit.firmware": "Firmware",
  "system.unit.serial": "Serial number",
  "system.unit.article": "Article number",
  "system.unit.country": "Country",
  "system.unit.name": "Name",
  "system.unit.notRead": "Not read yet",
  "system.unit.noAnswer": "The unit did not answer the last read; values may be old",
  "system.unit.readNow": "read just now",
  "system.unit.readMin": "read {n} min ago",
  "system.unit.readHours": "read {n} h ago",

  "system.errors.heading": "Active errors",
  "system.errors.none": "No active errors",
  "system.errors.notRead": "Not read",
  "system.errors.unknown": "Unknown error",

  "system.device.heading": "This device",
  "system.device.firmware": "Firmware",
  "system.device.idf": "ESP-IDF",
  "system.device.built": "Built",
  "system.device.board": "Board",
  "system.device.chip": "Chip",
  "system.device.chipRev": "{chip} rev {rev}",
  "system.device.flash": "Flash",
  "system.device.partition": "Running partition",
  "system.device.uptime": "Uptime",
  "system.device.reset": "Last reset",
  "system.device.heapFree": "Free heap",
  "system.device.heapMin": "Lowest free heap",
  "system.device.heapLargest": "Largest free block",
  "system.device.id": "Device id",
  "system.device.mac": "MAC address",

  "system.reset.power_on": "Power on",
  "system.reset.software": "Software restart",
  "system.reset.panic": "Crash (panic)",
  "system.reset.watchdog": "Watchdog",
  "system.reset.brownout": "Supply voltage dropped (brownout)",
  "system.reset.deep_sleep": "Wake from deep sleep",
  "system.reset.external": "External reset",
  "system.reset.usb": "Reset over USB",
  "system.reset.jtag": "Reset over JTAG",
  "system.reset.unknown": "Unknown",

  "system.uptime.dhm": "{d} d {h} h {m} min",
  "system.uptime.hm": "{h} h {m} min",
  "system.uptime.m": "{m} min",
  "system.kb": "{n} KB",
  "system.mb": "{n} MB",

  "system.network.heading": "Network",
  "system.network.ssid": "Wi-Fi network",
  "system.network.ip": "IP address",
  "system.network.rssi": "Signal",
  "system.network.rssiValue": "{rssi} dBm ({quality})",
  "system.network.good": "good",
  "system.network.fair": "fair",
  "system.network.weak": "weak",
  "system.network.channel": "Channel",
  "system.network.disconnected": "Not connected to Wi-Fi",

  "system.bus.heading": "CAN bus",
  "system.bus.state": "Controller state",
  "system.bus.state.active": "Active",
  "system.bus.state.warning": "Warning: errors are rising",
  "system.bus.state.passive": "Error passive",
  "system.bus.state.off": "Bus off",
  "system.bus.state.stopped": "Not running",
  "system.bus.rx": "Frames received",
  "system.bus.dropped": "Frames dropped",
  "system.bus.errors": "Bus errors",
  "system.bus.recoveries": "Recoveries after bus-off",
  "system.bus.counters": "Error counters (TEC / REC)",
  "system.bus.nodes": "Nodes on the bus",
  "system.bus.node.unit": "{id} ventilation unit",
  "system.bus.node.self": "{id} this device",
  "system.bus.noNodes": "none heard",

  "system.counters.heading": "Commands and MQTT",
  "system.commands": "Commands",
  "system.commands.value": "{queued} queued · {sent} sent · {failed} failed · {dropped} dropped",
  "system.mqtt": "MQTT",
  "system.mqtt.connected": "connected",
  "system.mqtt.disconnected": "not connected",
  "system.mqtt.unconfigured": "not configured",
  "system.mqtt.value": "{published} published · {commands} commands · {rejected} rejected · {reconnects} reconnects",
} satisfies Record<string, Message>;
