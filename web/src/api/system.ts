// GET /api/system: who the ventilation unit is, who this device is, and how the links between them
// are doing (docs/device/ota-and-system.md, "GET /api/system"). These types are a transcription
// of the schema documented there; comfoair_sysinfo renders it, this file only names its shape.
// A key the firmware adds later is simply not shown.
//
// Same terms as every other call in client.ts: no header, cookie or route curl cannot use.

import { ApiError, send } from "./client.ts";

/** unknown = not read yet (boot, or no bus); no_answer = the last read went unanswered and any
 *  older values are kept. */
export type ReadState = "unknown" | "ok" | "no_answer";

export interface UnitErrorItem {
  id: number;
  /** null: the firmware's table does not know this id. */
  text: string | null;
}

export interface UnitErrors {
  state: ReadState;
  /** [] whenever state != ok. */
  ids: number[];
  items: UnitErrorItem[];
}

export interface SystemUnit {
  state: ReadState;
  /** Since the last successful identity read; null until one has happened. */
  age_ms: number | null;
  // Each string is null when that property never answered.
  model: string | null;
  serial: string | null;
  /** "R1.11.0"; null (with firmware_raw) without an answer to property 0x06. */
  firmware: string | null;
  firmware_raw: number | null;
  article: string | null;
  country: string | null;
  name: string | null;
  errors: UnitErrors;
}

/** The fixed words comfoair_http_system.c maps esp_reset_reason() onto. */
export type ResetReason =
  | "power_on" | "software" | "panic" | "watchdog" | "brownout"
  | "deep_sleep" | "external" | "usb" | "jtag" | "unknown";

export interface SystemDevice {
  firmware: string;
  idf: string;
  built: string;
  board: string;
  chip: string;
  /** major * 100 + minor: 2 is "0.2", 101 is "1.1". */
  chip_revision: number;
  flash_bytes: number;
  partition: string;
  uptime_ms: number;
  /** A ResetReason; typed as string so a word a newer firmware adds still renders. */
  reset_reason: string;
  heap_free: number;
  heap_min_free: number;
  heap_largest_block: number;
  device_id: string;
  mac: string;
}

export interface SystemNetwork {
  connected: boolean;
  /** null without a network. */
  ssid: string | null;
  ip: string | null;
  /** dBm; null when unknown. */
  rssi: number | null;
  channel: number | null;
}

/** "active" | "warning" | "passive" | "off" | "stopped" (comfoair_sysinfo_bus_t). */
export type BusState = string;

export interface SystemBus {
  started: boolean;
  state: BusState;
  rx_frames: number;
  rx_dropped: number;
  bus_errors: number;
  bus_recoveries: number;
  tx_error_count: number;
  rx_error_count: number;
  /** Node ids heard, ascending. TWAI does not receive its own frames, so 0x2A rarely appears. */
  nodes_seen: number[];
}

export interface SystemCommands {
  queued: number;
  sent: number;
  failed: number;
  dropped: number;
}

export interface SystemMqtt {
  configured: boolean;
  connected: boolean;
  published: number;
  commands: number;
  rejected: number;
  reconnects: number;
}

export interface SystemInfo {
  unit: SystemUnit;
  device: SystemDevice;
  network: SystemNetwork;
  bus: SystemBus;
  commands: SystemCommands;
  mqtt: SystemMqtt;
}

/** The document, parsed. The body IS the answer, so an unreadable one is reported as an
 *  ApiError carrying the status the device gave -- the same rule client.ts's request() keeps. */
export async function getSystem(): Promise<SystemInfo> {
  const response = await send("/api/system");
  const raw = await response.text();
  try {
    return JSON.parse(raw) as SystemInfo;
  } catch {
    const what = raw === "" ? "an empty body" : "a body this page could not read";
    throw new ApiError(response.status, `The device answered ${response.status} with ${what}.`);
  }
}
