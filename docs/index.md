---
layout: home

hero:
  name: ComfoAir Q Firmware
  tagline: Own ESP-IDF firmware for the Zehnder ComfoAir Q — MQTT, REST and a web UI over one model
  actions:
    - theme: brand
      text: Guide
      link: /guide/introduction
    - theme: alt
      text: Architecture
      link: /architecture/overview

features:
  - title: Guide
    details: Flash one universal binary from the browser, then enter Wi-Fi, the MQTT broker and the device name in the web UI. Nothing is set at compile time.
    link: /guide/introduction
    linkText: Get started
  - title: Architecture
    details: One state and command model, generated from a single entity list, with HTTP, MQTT and the WebSocket push channel as transports over it.
    link: /architecture/overview
    linkText: Read the architecture
  - title: Protocol
    details: The ComfoAir Q CAN bus — PDO parsing for every published value, RMI reads and commands, and the polling that ties them together.
    link: /protocol/can-bus
    linkText: Read the protocol
  - title: Interfaces
    details: Every entity write and operation is accepted over both REST and MQTT. Home Assistant finds the unit through MQTT discovery; the web UI uses only the public API.
    link: /interfaces/rest-api
    linkText: Read the interfaces
  - title: Device
    details: The board boundary, status indication, the button, networking and provisioning, OTA by .bin upload, and what lives in NVS.
    link: /device/board
    linkText: Read about the device
  - title: Development
    details: Building with PlatformIO, host test suites that run without hardware, the entity registry generator and the project's conventions.
    link: /development/building
    linkText: Start developing
---
