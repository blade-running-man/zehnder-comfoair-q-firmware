<script setup>
import { data } from '../reference/entities.data'
// The duration rows come from the generated registry (see /reference/entities), not from a
// hand-written table that would drift from tools/pdo_table.py.
const durations = Object.values(data.groups).flat().filter((e) => e.kind === 'number')
</script>

# Configuration

Everything the owner can change is changed at runtime, in the web interface's **Settings** page or
through the same REST endpoints it uses. Settings are stored in the device's NVS flash and survive
power cuts and firmware updates. Nothing is configured at build time.

## The settings

The rules below are the device's own (`components/comfoair_config/comfoair_config_check.c`); the
page does not add rules of its own, so the device's error sentence is what you see when a value is
refused.

### Wi-Fi

Set through the **Wi-Fi** card, which sends `POST /api/provision` — never together with the other
settings, so saving the broker can never disturb the network.

| Setting | Default | Accepted |
| --- | --- | --- |
| Network name (SSID) | none | 1 to 32 bytes |
| Wi-Fi password | none | 8 to 63 characters, exactly 64 hexadecimal digits, or empty for an open network |

The name and the password are stored as one record, so a power cut can never leave a new name
beside an old password. Saving starts a connection attempt; see
[First setup](/guide/first-setup#_3-choose-your-wi-fi) for what happens next and how a wrong password
is rolled back.

### MQTT broker

| Setting | Default | Accepted |
| --- | --- | --- |
| Broker host | empty — **MQTT off** | A host name or IP address, up to 253 characters of letters, digits, `.`, `-`, `_`, `:`, `[` and `]`. A scheme is refused (`mqtt://broker`); the port goes in its own field. |
| Broker port | `1883` | 1 to 65535 |
| Broker username | empty | Up to 128 characters |
| Broker password | empty | Up to 128 characters |
| Topic prefix | `comfoair/<device id>` | 1 to 64 characters; no spaces, `+` or `#`; must not start with `$` or `/`, end with `/`, or contain `//` |
| Publish Home Assistant discovery | **on** | on / off |

- An empty host is a supported configuration: the device ventilates and serves its web interface
  and REST API without any broker.
- The `<device id>` in the default prefix is the full station MAC as 12 lower-case hex digits,
  shown as **Device id** on the System page. Identity comes from the MAC, never from the name.
- There is **no TLS** in this version. The broker password crosses your network in the clear on
  every connection; do not reuse a password you use anywhere else.
- Changing the **host or port** while connected makes the device withdraw its Home Assistant
  entities from the old broker before it moves. Changing the **prefix** moves the state topics; the
  entities in Home Assistant follow and keep their history. Turning **discovery off** stops
  publishing discovery but does not remove what was published. Details:
  [Home Assistant](/guide/home-assistant#changing-things-later).

### Device

| Setting | Default | Accepted |
| --- | --- | --- |
| Device name | `ComfoAir Q xxxx` (last four hex digits of the station MAC) | 1 to 32 bytes (a non-ASCII letter takes two or more), no control characters |
| Keep the ventilation level | **on** | on / off |
| Web interface password | **not set** | 8 to 128 characters, no control characters; empty removes a stored password |

- **Device name** is a display name: the Home Assistant device name and the mDNS service name. It
  is not an identity, so renaming never creates a second device in Home Assistant.
- **Keep the ventilation level** (`permanent_manual`): on, a level chosen in the web interface or
  in Home Assistant is sent together with the unit's manual-mode command, so it sticks
  (`components/comfoair_command/comfoair_command_build.cpp`, `build_level`). Off, only the level is
  sent: the unit returns to AUTO by itself within two hours, and its humidity and temperature
  controls stay active — manual mode switches them off ([ComfoAir Q
  hardware](/reference/comfoair-q-hardware)). The switch is stored on the device and saved with the
  other settings.
- **Web interface password** is stored as a salted PBKDF2-HMAC-SHA256 record, never in plain text.
  There is no lockout and no recovery: a forgotten password is cleared with the
  [5-second button hold](/guide/troubleshooting#the-button), which also clears the Wi-Fi.

### Durations

Four durations are not on the Settings page: they are ordinary writable entities, set on the
**Diagnostics** page (all four), on the home page's **Timers** card (boost, bypass and away), from
Home Assistant, or through `POST /api/entities/<key>`. They are stored on the device because the
unit takes a duration only as part of a command.

<table>
<thead><tr><th>Entity</th><th>Range</th></tr></thead>
<tbody>
<tr v-for="e in durations" :key="e.key"><td>{{ e.name }} (<code>{{ e.key }}</code>)</td><td>{{ e.min }} to {{ e.max }} {{ e.unit }}</td></tr>
</tbody>
</table>

The defaults — 60 minutes for boost, bypass and balance, 0 (indefinite) for away — are declared
with the entities in `tools/pdo_table.py` and repeated in `comfoair_command_durations_defaults()`
(`components/comfoair_command/comfoair_command.cpp`), which is what a device with nothing stored
uses. Every entity is listed in [Entities](/reference/entities).

## Saving

**Save settings** sends the broker and device settings together as one document
(`POST /api/config`). The device checks every field before it writes any: a document with one bad
field changes nothing, and the page shows the device's own sentence saying which rule failed.

Saved settings take effect at once — the MQTT client restarts with the new values and the mDNS
service name follows the device name, while the host name `comfoair-xxxx.local` stays as it is.
**Nothing reboots.**

### Passwords are write-only

Neither the broker password nor the web interface password is ever sent back to a browser or any
API client. `GET /api/config` returns the sentinel `__UNCHANGED__` for a stored secret and an empty
string for an absent one. On the page that shows as an empty box with a note:

| What you do with the box | What saving does |
| --- | --- |
| Leave it untouched | Keeps the stored value |
| Type a new value | Replaces it |
| Clear it when something is stored | Deletes the stored value |

## Who may change what

Reading is open until you set a password. Writing depends on where the device is and whether a
password exists (`components/comfoair_policy/comfoair_http_policy.c`):

| | No password set | Password set |
| --- | --- | --- |
| Read anything (`GET`) | Anyone | Password required |
| Save settings (`/api/config`) | Anyone on your network | Password required |
| Control the ventilation, run the unit probe | Anyone on your network | Password required |
| Change Wi-Fi from your network (`/api/provision`) | **Refused** | Password required |
| Upload firmware (`/api/ota`) | **Refused** | Password required |
| Any write while the setup network is on the air | Only `/api/provision` | Only `/api/provision`, with the password |

So without a password, Wi-Fi is changed by pressing the button briefly to bring up the setup
network, and firmware updates need a password first.

The browser asks for the password with its own login prompt (HTTP Basic authentication). The user
name is ignored; only the password is checked. A wrong password is answered after a one-second
delay.

## Settings that cannot be changed

These are fixed in this version and have no setting:

- the setup window lengths (15 minutes first, 5 minutes later, 60-minute ceiling);
- the LED brightness, capped at 20 % (`COMFOAIR_LED_CEILING_DEFAULT`, 51 of 255);
- the Home Assistant discovery prefix, `homeassistant`;
- MQTT over TLS, which is not implemented.

## When settings are read-only

If the stored settings were written by a newer firmware than the one running — for example after
installing an older version — the device keeps using what it can read but **refuses to save the
broker and device settings** (`POST /api/config` answers `409`), and Settings says so. The Wi-Fi
card keeps working, so the device can always be put on a network. Reinstall the newer firmware, or
clear the settings with the [15-second button hold](/guide/troubleshooting#the-button).

How the settings are laid out in flash, and which namespace each reset tier erases, is in
[Configuration storage](/device/config-storage).
