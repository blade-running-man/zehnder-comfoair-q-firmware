# This file is the single source of truth for everything the firmware publishes -- AGENTS.md
# forbids a second entity list "under any circumstances", and that includes a
# hand-maintained C++ array or a hand-written types.ts. Both are generated from this file
# by tools/generate_registry.py.
#
# Kept as comments rather than a second docstring on purpose: `from __future__ import
# annotations` must follow the FIRST string literal in the file, so prepending one breaks
# the module outright. It did, once.
"""The single source of truth for every entity the comfoair firmware publishes.

This module is pure data. It must never import anything outside the standard library, so that
tests/python can load it on a bare interpreter. tools/generate_registry.py is its only consumer.

Every PDO number, width, sign, divider and value meaning below is copied from
docs/reference/comfoair-pdo-rmi.md, which is authoritative. The keys this table produces
are what the Home Assistant integration builds on; the ones it resolves by key, and the
SCHEMA_VERSION rules, are in docs/development/entity-registry.md.

Widths are deliberately absent: the C++ decoder takes the width from the CAN data-length code at
runtime (see pdo_decode.h). The table declares only signedness and divider.
"""

from __future__ import annotations

from dataclasses import dataclass

#: Lets the integration check the contract at setup instead of failing on a missing key at
#: runtime. Bump on removing or renaming an entity, never on adding one
#: (docs/development/entity-registry.md).
#: 2: the accessory, Option-Box and raw schedule rows went -- the table is the ComfoAir Q350/450/600
#: base unit's. Removing a key also leaves its retained MQTT topics behind; docs/development/
#: entity-registry.md, "Retiring keys", is the cleanup.
SCHEMA_VERSION = 2
SCHEMA_VERSION_KEY = "schema_version"
SCHEMA_VERSION_NAME = "Schema Version"

FAN_KEY = "fan"
FAN_NAME = "Ventilation"
#: Mirrored as string literals in comfoair_fan.h; tests/python asserts they match.
#:
#: !!! DO NOT REORDER. !!! This is also SELECTS["ventilation_mode"].options verbatim (order
#: asserted by tests/python), so a reorder changes what a user sees as the FIRST option in both
#: dropdowns. Concretely: web_server's select_json_ shows a <select>'s first option whenever it has
#: no state yet, so this order is literally what Ventilation Mode displays before the first PDO 49
#: frame lands after boot -- contract-visible, not cosmetic.
FAN_PRESET_MODES = ("Auto", "Away", "Low", "Medium", "High", "Boost")
#: PDO 49 operating mode and PDO 65 fan speed together drive the fan entity.
FAN_PDOS = (49, 65)
#: PDO 54 supply fan mode and PDO 55 exhaust fan mode together drive the balance-mode select.
BALANCE_PDOS = (54, 55)


@dataclass(frozen=True)
class NumericPdo:
    """A PDO published as a `sensor`."""

    pdo: int
    key: str
    name: str
    signed: bool
    divider: int = 1
    unit: str | None = None
    device_class: str | None = None
    state_class: str | None = None
    icon: str | None = None
    entity_category: str | None = None
    #: True for a PDO whose all-ones reading means "nothing pending": the value is then null,
    #: not a huge number.
    none_when_all_ones: bool = False
    #: False: the entity is created disabled -- discovery carries `en:false` and /api/entities
    #: `enabledByDefault:false`; it still exists and can be enabled. Use it for a row that
    #: duplicates another (ventilation_mode is the fan's presets a second time, 54/55 the balance
    #: select's state, 220 equals 276), a raw or unverified diagnostic (225, 219), never to hide a
    #: reading the base unit does not send -- such a row does not belong in this table at all.
    enabled_by_default: bool = True
    #: MQTT ONLY -- REST and the WebSocket serve the model exactly (docs/interfaces/mqtt.md,
    #: "Publication rate"). A change smaller than this, in the row's published unit (after the
    #: divider), is held back until COMFOAIR_PUBLISH_MAX_HOLD_S has passed since the row was last
    #: published. None: every change goes out. Never 0 -- None says that.
    publish_deadband: float | None = None
    #: MQTT ONLY. The shortest gap, in seconds, between two publications of this row; a change
    #: arriving sooner is held until the gap has passed. 0: no gap. Below the max hold.
    publish_min_interval_s: int = 0


@dataclass(frozen=True)
class BinaryPdo:
    """A PDO published as a `binary_sensor`: compared against one value, or one bit tested."""

    pdo: int
    key: str
    name: str
    value: int
    #: "eq", "ne", or "bit" -- then `value` is a bit index and the row is true when it is set
    op: str
    icon: str | None = None
    device_class: str | None = None
    entity_category: str | None = None
    #: False: the entity is created disabled -- discovery carries `en:false` and /api/entities
    #: `enabledByDefault:false`; it still exists and can be enabled. Use it for a row that
    #: duplicates another (ventilation_mode is the fan's presets a second time, 54/55 the balance
    #: select's state, 220 equals 276), a raw or unverified diagnostic (225, 219), never to hide a
    #: reading the base unit does not send -- such a row does not belong in this table at all.
    enabled_by_default: bool = True


@dataclass(frozen=True)
class EnumPdo:
    """A PDO published as a `text_sensor` through a value -> label map."""

    pdo: int
    key: str
    name: str
    options: tuple[tuple[int, str], ...]
    icon: str | None = None
    entity_category: str | None = None
    #: False: the entity is created disabled -- discovery carries `en:false` and /api/entities
    #: `enabledByDefault:false`; it still exists and can be enabled. Use it for a row that
    #: duplicates another (ventilation_mode is the fan's presets a second time, 54/55 the balance
    #: select's state, 220 equals 276), a raw or unverified diagnostic (225, 219), never to hide a
    #: reading the base unit does not send -- such a row does not belong in this table at all.
    enabled_by_default: bool = True


@dataclass(frozen=True)
class SpecialPdo:
    """A PDO with bespoke C++ decoding, published as a `text_sensor`."""

    pdo: int
    key: str
    name: str
    icon: str | None = None


@dataclass(frozen=True)
class SelectSpec:
    """A `select` entity. `pdo` is the feedback PDO; `rmi` the read-back property when no PDO
    reports it; neither for a select derived in C++ (balance, ventilation mode)."""

    key: str
    name: str
    kind: str  # the C++ SelectKind enumerator
    options: tuple[str, ...]
    icon: str
    pdo: int | None = None
    #: (unit, subunit, property) of the RMI property this select's value is READ BACK from, for a
    #: select no PDO reports. The probe task (comfoair_probe_task) reads it with RMI GET ONE at
    #: boot and every 6 h; until the first answer the value is unknown, never a guess. A guessed
    #: default (the Zehnder factory one) used to be seeded here instead, and Home Assistant showed
    #: it as the unit's state while the unit held something else. The answer byte is used as the
    #: option index, so generate_registry.py refuses an rmi select with a pdo or with options
    #: other than ("Off", "Auto only", "On").
    rmi: tuple[int, int, int] | None = None
    #: False: the entity is created disabled -- discovery carries `en:false` and /api/entities
    #: `enabledByDefault:false`; it still exists and can be enabled. Use it for a row that
    #: duplicates another (ventilation_mode is the fan's presets a second time, 54/55 the balance
    #: select's state, 220 equals 276), a raw or unverified diagnostic (225, 219), never to hide a
    #: reading the base unit does not send -- such a row does not belong in this table at all.
    enabled_by_default: bool = True


@dataclass(frozen=True)
class SwitchSpec:
    """A `switch`. There is exactly one, and it is state-backed rather than optimistic."""

    key: str
    name: str
    pdo: int
    active_value: int
    icon: str | None = None
    #: "eq", or "bit" -- then `active_value` is a bit index and the switch is on while it is set.
    #: The same comparison a BinaryPdo makes; "ne" is refused, an on/off timer has no use for it.
    op: str = "eq"


@dataclass(frozen=True)
class NumberSpec:
    """A duration the user sets, published as a `number` and kept in a preference.

    Minutes in the UI, seconds on the wire. No PDO carries any of these: the unit is told a
    duration only as part of a command, so the value lives on this node alone.

    These are the intended replacement for the old `boost_duration:` and `bypass_duration:` YAML
    options. Those were compile-time, so changing how long a boost lasts meant a reflash -- and
    they were unreachable from a browser, which is the whole problem this design fixes.
    """

    key: str
    name: str
    #: Mirrored in comfoair.h as a NumberKind enumerator; a test asserts the two stay in step.
    kind: str
    min_value: float
    max_value: float
    default: float
    icon: str | None = None
    #: MINUTES, and it is published rather than implied.
    #:
    #: The trap this closes: on a flat topic tree with no units anywhere, the most obvious
    #: consumer imaginable reads
    #: `.../timers/away_duration/state -> "60"` (minutes) and writes `{"duration": 60}` (seconds),
    #: getting one minute instead of one hour. REST had the same silence. Carrying the unit here
    #: fixes every surface at once -- the C registry, entities.ts, the REST metadata and the HA
    #: discovery payload -- because they are all generated from this row.
    unit: str | None = "min"


#: Kept in step with comfoair.h NumberKind.
NUMBERS: tuple[NumberSpec, ...] = (
    NumberSpec("boost_duration", "Boost Duration", "NUMBER_BOOST_DURATION",
               1, 1440, 60, "mdi:fan-plus"),
    NumberSpec("bypass_duration", "Bypass Override Duration", "NUMBER_BYPASS_DURATION",
               1, 1440, 60, "mdi:valve"),
    NumberSpec("balance_duration", "Balance Mode Duration", "NUMBER_BALANCE_DURATION",
               1, 1440, 60, "mdi:arrow-split-vertical"),
    # 0 is not "zero minutes". The API (docs/interfaces/rest-api.md, POST /api/ops) defines
    # set_away's duration 0 as indefinite -- away is the only one of the four *numbers* given a
    # zero minimum, not the only service with an indefinite form: set_balance_mode has a
    # comparable 0 = "until balanced is restored" that this UI deliberately does not expose. On
    # the unit AWAY otherwise runs to an explicit end and PDO 81 counts it down. The name says so
    # out loud, because a bare 0 in a minutes field reads as a mistake.
    NumberSpec("away_duration", "Away Duration (0 = indefinite)", "NUMBER_AWAY_DURATION",
               0, 43200, 0, "mdi:home-export-outline"),
)

C = "°C"
PCT = "%"
SECONDS = "s"
WATT = "W"
KWH = "kWh"
RPM = "rpm"
FLOW = "m³/h"
DAYS = "d"

MEASUREMENT = "measurement"
TOTAL_INCREASING = "total_increasing"
DIAGNOSTIC = "diagnostic"

#: PUBLICATION RATE (publish_deadband / publish_min_interval_s) is set only on the rows the Q350
#: census shows the unit broadcasting fast with jitter (docs/unit/data/q350-pdos.json, 67 min):
#: 121/122 rpm every ~3 s, 119/120 flow every 6-8 s, 216 avoided cooling every ~4 s (213, its
#: heating twin, takes the same policy by symmetry -- the census ran outside the heating season);
#: and a 30 s floor on the temperatures, which the census has at 46-225 s but the audit saw at
#: 6 per 30 s live. State rows (enums, binaries, controls) are never held. The values and the
#: budget they buy (about 0.3 msg/s worst case, from 1.4 msg/s average) are argued in
#: docs/interfaces/mqtt.md.
NUMERIC: tuple[NumericPdo, ...] = (
    # --- schedule countdowns (UINT32, 0xFFFFFFFF = nothing pending) ---------------------------
    # These four KEYS are frozen: the Home Assistant integration resolves them by key and turns
    # them into timestamp sensors. Their names equal their keys because they are raw second
    # counts, not labels a person reads.
    NumericPdo(81, "next_fan_change_in_raw", "next_fan_change_in_raw", False, 1, SECONDS,
               "duration", None, "mdi:timer-outline", DIAGNOSTIC, none_when_all_ones=True),
    NumericPdo(82, "next_bypass_change_in_raw", "next_bypass_change_in_raw", False, 1, SECONDS,
               "duration", None, "mdi:timer-outline", DIAGNOSTIC, none_when_all_ones=True),
    NumericPdo(86, "next_supply_change_in_raw", "next_supply_change_in_raw", False, 1, SECONDS,
               "duration", None, "mdi:timer-outline", DIAGNOSTIC, none_when_all_ones=True),
    NumericPdo(87, "next_exhaust_change_in_raw", "next_exhaust_change_in_raw", False, 1, SECONDS,
               "duration", None, "mdi:timer-outline", DIAGNOSTIC, none_when_all_ones=True),
    # --- fans -------------------------------------------------------------------------------
    # Naming: the unit's own display calls the ETA-side fan the EXTRACT fan --
    # STATUS > FANS > EXTRACT FAN and TASK MENU > VENTILATION > EXTRACT ONLY (hardware reference
    # 13.1; User pp. 13, 20) -- so every user-visible `name:` below says "Extract". The config
    # KEYS stay `exhaust_*`: they are machine tokens frozen against the protocol, where 117/119/121
    # are the exhaust half of the FAN block, and the Service manual's own fault code for this fan
    # is FAN_EHA ERROR = "the exhaust air fan" (hardware reference 16; Service p. 33). Do not
    # "fix" one to match the other; the two manuals genuinely differ and both spellings are kept
    # deliberately, each on the side its audience reads.
    NumericPdo(117, "exhaust_fan_duty", "Extract Fan Duty", False, 1, PCT, None, MEASUREMENT,
               "mdi:fan"),
    NumericPdo(118, "supply_fan_duty", "Supply Fan Duty", False, 1, PCT, None, MEASUREMENT,
               "mdi:fan"),
    # Reference doc: 119 is EXHAUST and 120 is SUPPLY in protocol terms; labelling them the other
    # way round is an easy mistake. Only the display name says "Extract"; the PDO binding is
    # unchanged.
    # Neither figure is measured: both are computed from the fan characteristic plus the two
    # static-pressure sensors, corrected by the installer's ALTITUDE setting, and UNBALANCE
    # (default 0 %) deliberately makes them differ (hardware reference 10; Service pp. 22-23).
    NumericPdo(119, "exhaust_fan_flow", "Extract Fan Flow", False, 1, FLOW, "volume_flow_rate",
               MEASUREMENT, "mdi:gauge", publish_deadband=5, publish_min_interval_s=30),
    NumericPdo(120, "supply_fan_flow", "Supply Fan Flow", False, 1, FLOW, "volume_flow_rate",
               MEASUREMENT, "mdi:gauge", publish_deadband=5, publish_min_interval_s=30),
    NumericPdo(121, "exhaust_fan_speed", "Extract Fan Speed", False, 1, RPM, None, MEASUREMENT,
               "mdi:speedometer", publish_deadband=25, publish_min_interval_s=30),
    NumericPdo(122, "supply_fan_speed", "Supply Fan Speed", False, 1, RPM, None, MEASUREMENT,
               "mdi:speedometer", publish_deadband=25, publish_min_interval_s=30),
    # --- power ------------------------------------------------------------------------------
    # STATUS > ENERGY > POWER CONSUMPTION > VENTILATION is defined as "the current electrical
    # consumption of the fans", with PREHEATER a separate item in the same menu (User p. 20;
    # hardware reference 13, and the PDO reference's row notes). So 128/129/130 are FAN power and
    # fan energy, never whole-unit, and the pre-heater is 146. Hence the "Ventilation " prefix.
    # Ceiling for 128, from the technical table's "max power excl. pre-heater" column: 180 W
    # (Q350) / 250 W (Q450) / 350 W (Q600) (hardware reference 3; Installer p. 6 = Service p. 6).
    # A reading far above that means the pre-heater is running, which is PDO 146, not this.
    # 129 resets at the calendar year; 130 counts from commissioning.
    NumericPdo(128, "power", "Ventilation Power", False, 1, WATT, "power", MEASUREMENT),
    NumericPdo(129, "energy_ytd", "Ventilation Energy YTD", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    NumericPdo(130, "energy_total", "Ventilation Energy Total", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    # --- pre-heater ----------------------------------------------------------------------------
    NumericPdo(144, "preheater_energy_ytd", "Pre-heater Energy YTD", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    NumericPdo(145, "preheater_energy_total", "Pre-heater Energy Total", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    NumericPdo(146, "preheater_power", "Pre-heater Power", False, 1, WATT, "power", MEASUREMENT),
    # --- filter -----------------------------------------------------------------------------
    # Name frozen: the integration derives filter_replacement_at and the problem sensor from it.
    NumericPdo(192, "filter_replacement_remaining_days", "Filter Replacement Remaining Days",
               False, 1, DAYS, "duration", None, "mdi:air-filter"),
    # --- temperature control ------------------------------------------------------------------
    NumericPdo(209, "running_mean_outdoor_temp", "Running Mean Outdoor Temperature", True, 10, C,
               "temperature", MEASUREMENT),
    # Sometimes given as UINT8, but the value is INT16/10: 238 -> 23.8 C (reference doc §1).
    NumericPdo(212, "profile_target_temp", "Profile Target Temperature", True, 10, C,
               "temperature", MEASUREMENT),
    # An annotation of 441 -> 4.41 W (implying /100) exists, but a Q350/450 avoids hundreds of
    # watts, so raw watts is right and that annotation is wrong (reference doc §1).
    NumericPdo(213, "avoided_heating", "Avoided Heating", False, 1, WATT, "power", MEASUREMENT,
               publish_deadband=10, publish_min_interval_s=60),
    NumericPdo(214, "avoided_heating_ytd", "Avoided Heating YTD", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    NumericPdo(215, "avoided_heating_total", "Avoided Heating Total", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    NumericPdo(216, "avoided_cooling", "Avoided Cooling", False, 1, WATT, "power", MEASUREMENT,
               publish_deadband=10, publish_min_interval_s=60),
    NumericPdo(217, "avoided_cooling_ytd", "Avoided Cooling YTD", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    NumericPdo(218, "avoided_cooling_total", "Avoided Cooling Total", False, 1, KWH, "energy",
               TOTAL_INCREASING),
    # Reference doc verdict: no label for 220 is confirmed; its name is documented as unknown,
    # with no unit. It sits in the TEMPHUMCONTROL block, so it is a
    # controller-side intake temperature paired with 221. Ship a neutral, explicitly unverified
    # label -- never pre_heater_temp_before. Disabled by default: on the Q350 census it equals
    # PDO 276 in value and in its 46 s cadence, so enabled it is a second outdoor temperature.
    NumericPdo(220, "intake_air_temp_unverified", "Intake Air Temperature (unverified)", True, 10,
               C, "temperature", MEASUREMENT, None, DIAGNOSTIC, enabled_by_default=False,
               publish_min_interval_s=30),
    # TEMPHUMCONTROL's own supply-air figure: after the post-heater on a unit that has one; on a
    # unit without one (the Q350 census) it equals PDO 278 in every sample. A base-unit PDO, so it
    # stays -- named "(controller)": it is the controller-side figure (TEMPHUMCONTROL). The KEY
    # keeps "post_heater": renaming it would break every consumer for a word.
    NumericPdo(221, "supply_air_temp_post_heater", "Supply Air Temperature (controller)",
               True, 10, C, "temperature", MEASUREMENT, publish_min_interval_s=30),
    NumericPdo(227, "bypass_position", "Bypass Position", False, 1, PCT, None, MEASUREMENT,
               "mdi:valve"),
    # PDO 226 steps 0/100/200/300 with the four presets and goes continuous over 0-300 while a
    # sensor-ventilation control is overruling. Not a percentage and not m³/h: it is the unit's
    # own internal airflow demand, so it carries no unit. It is the only PDO evidence that
    # Temperature Passive / Humidity Comfort / Humidity Protection did anything -- those three
    # selects have no PDO of their own, and their settings are read back over RMI
    # (SelectSpec.rmi), which says what is configured, not whether it is acting. DO NOT cite
    # PDO 225 as a second witness: on the Q350 it follows the fan level (see its row below).
    # Name matches the protocol's own wording for this PDO and this table's parenthetical house
    # style (PDO 221's "Supply Air Temperature (controller)"), not the inverted
    # "Modulated Fan Speed" this row first shipped with.
    NumericPdo(226, "fan_speed_modulated", "Fan Speed (modulated)", False, 1, None, None,
               MEASUREMENT, "mdi:fan-chevron-up", DIAGNOSTIC),
    # --- temperature sensors ------------------------------------------------------------------
    NumericPdo(274, "extract_air_temp", "Extract Air Temperature", True, 10, C, "temperature",
               MEASUREMENT, publish_min_interval_s=30),
    NumericPdo(275, "exhaust_air_temp", "Exhaust Air Temperature", True, 10, C, "temperature",
               MEASUREMENT, publish_min_interval_s=30),
    NumericPdo(276, "outdoor_air_temp", "Outdoor Air Temperature", True, 10, C, "temperature",
               MEASUREMENT, publish_min_interval_s=30),
    # 277 is after the PRE-HEATER, not after a ground heat exchanger: that accessory has PDOs of
    # its own (416/417), which this table does not publish.
    NumericPdo(277, "preheated_outdoor_air_temp", "Outdoor Air Temperature (after pre-heater)",
               True, 10, C, "temperature", MEASUREMENT, publish_min_interval_s=30),
    # Reference doc verdict: 278 is the unit's own SUP sensor, before any post-heater (§2).
    NumericPdo(278, "supply_air_temp", "Supply Air Temperature", True, 10, C, "temperature",
               MEASUREMENT, publish_min_interval_s=30),
    # --- humidity sensors ---------------------------------------------------------------------
    NumericPdo(290, "extract_air_humidity", "Extract Air Humidity", False, 1, PCT, "humidity",
               MEASUREMENT),
    NumericPdo(291, "exhaust_air_humidity", "Exhaust Air Humidity", False, 1, PCT, "humidity",
               MEASUREMENT),
    NumericPdo(292, "outdoor_air_humidity", "Outdoor Air Humidity", False, 1, PCT, "humidity",
               MEASUREMENT),
    NumericPdo(293, "preheated_outdoor_air_humidity", "Outdoor Air Humidity (after pre-heater)",
               False, 1, PCT, "humidity", MEASUREMENT),
    NumericPdo(294, "supply_air_humidity", "Supply Air Humidity", False, 1, PCT, "humidity",
               MEASUREMENT),
    # --- frost protection and the raw sensor-ventilation state --------------------------------
    # 219 and 228 come from the reference doc's §1 "Accessory and schedule PDOs" table
    # (aiocomfoconnect, not first-hand). Of that table, these two and PDO 337 are all this
    # firmware publishes: the rest are accessories (ComfoCool, ComfoFond, CO2, ComfoClime,
    # post-heater), the Option Box or raw schedule words, outside its ComfoAir Q350/450/600 scope.
    # 219 is seen only in the official app (†), so its NAME says "(unverified)" and it is disabled.
    NumericPdo(219, "frost_protection_preheater_power",
               "Frost Protection Pre-heater Power (unverified)", False, 1, WATT, "power",
               MEASUREMENT, None, DIAGNOSTIC, enabled_by_default=False),
    # The unit's own: the % the supply fan is cut back while frost protection runs.
    NumericPdo(228, "frost_protection_unbalance", "Frost Protection Unbalance", False, 1, PCT,
               None, MEASUREMENT, "mdi:snowflake-alert", DIAGNOSTIC),
    # PDO 225 is published RAW. aiocomfoconnect labels it "sensor based ventilation mode"
    # 0 disabled / 1 active / 2 overruling, but no Zehnder manual names its values, and the Q350
    # census shows it following the fan level instead (0 at the Away preset, 1 at Low, broadcast
    # with PDO 65 on every level change). So the number, disabled, with "(raw, unverified)" in the
    # NAME; labels return only once a unit pins them. The KEY stays: a rename is a schema break
    # that buys nothing. It lives in VENTILATIONCONFIG (0x1E), not TEMPHUMCONTROL (0x1D): do not
    # map it onto the OFF / AUTO ONLY / ON of Temperature Passive and the humidity controls.
    NumericPdo(225, "sensor_ventilation_mode", "Sensor Ventilation Mode (raw, unverified)",
               False, icon="mdi:motion-sensor", entity_category=DIAGNOSTIC,
               enabled_by_default=False),
)

BINARY: tuple[BinaryPdo, ...] = (
    BinaryPdo(56, "manual_mode", "Manual Mode", 1, "eq", "mdi:hand-back-right"),
    # TASK MENU > VENTILATION > SUPPLY ONLY / EXTRACT ONLY (User p. 13; hardware reference 13.1).
    # SUPPLY ONLY stops extracting, EXTRACT ONLY stops supplying. Display name follows the unit;
    # the key stays `exhaust_only_active`, which is the frozen machine token.
    BinaryPdo(70, "supply_only_active", "Supply Only", 0, "ne", "mdi:home-import-outline"),
    BinaryPdo(71, "exhaust_only_active", "Extract Only", 0, "ne", "mdi:home-export-outline"),
    # PDO 337 is the SCHEDULE preset unit's running-timer bitset (UINT32): bit 6 = timer 6, the
    # boost timer; bit 11 = timer 11, the away timer -- the timers `84 15 01 06` and
    # `84 15 01 0B` start (aiocomfoconnect docs/PROTOCOL-PDO.md; docs/reference/comfoair-pdo-rmi.md
    # §1 "Accessory and schedule PDOs"). Bits, not a raw number: the state store holds a float,
    # exact only below 2^24.
    BinaryPdo(337, "boost_timer_running", "Boost Timer Running", 6, "bit", "mdi:fan-plus"),
    BinaryPdo(337, "away_timer_running", "Away Timer Running", 11, "bit",
              "mdi:home-export-outline"),
    # PDO 338 is the SCHEDULE bypass instance's running-timer bitset (UINT32), laid out like 337:
    # bit 1 = timer 1, the bypass override timer `84 15 02 01 … T 01|02` starts (aiocomfoconnect
    # docs/PROTOCOL-PDO.md, "0=auto, 2=overriden (timer 1)"). A bit test, not an enum over {0, 2}:
    # that enum turned every other value into null. Independent of PDO 66, so a second,
    # differently encoded confirmation that an override is in force. Named "...Active", not a
    # bare "Bypass Override", because NUMBERS already ships "Bypass Override Duration".
    BinaryPdo(338, "bypass_override", "Bypass Override Active", 1, "bit", "mdi:valve", None,
              DIAGNOSTIC),
    BinaryPdo(210, "heating_season", "Heating Season", 1, "eq", "mdi:snowflake-thermometer"),
    BinaryPdo(211, "cooling_season", "Cooling Season", 1, "eq", "mdi:sun-thermometer"),
)

#: AWAY *mode* -- the unit's TASK MENU > AWAY, which "ignores all schedulers" for its duration
#: (User p. 14) and is counted down by PDO 81. The switch's command starts and cancels SCHEDULE
#: timer 11 (`84 15 01 0B ... 00` / `85 15 01 0B`), so its state is that same timer running:
#: PDO 337 bit 11, the bit `away_timer_running` reads (aiocomfoconnect docs/PROTOCOL-PDO.md: 337
#: is the preset unit's running-timer bitset, bit 11 = timer 11, away).
#:
#: DO NOT derive the switch from PDO 16 == 7. On the owner's Q350 PDO 16 read 7 whenever the
#: fan's level was the Away PRESET: switching the level Away -> Low on the display moved PDO 16
#: from 7 to 1 while PDO 337 bit 11 stayed clear and PDO 81 counted nothing down. A switch on
#: PDO 16 showed ON for the preset, and its OFF -- a cancel of timer 11 -- could never clear it.
#: A switch must read back the thing its command drives.
AWAY_SWITCH = SwitchSpec(
    "away_mode",
    "Away Mode",
    337,
    11,
    "mdi:home-export-outline",
    "bit",
)

ENUM: tuple[EnumPdo, ...] = (
    # 7 is the FAN's Away preset (PDO 65 = 0), not TASK MENU > AWAY: the Q350 census read 7 while
    # the level was Away and 1 the moment it went to Low. "(preset)" says which away it is; the
    # away_mode switch reads PDO 337 bit 11. Not 8: 8 is DFC.
    EnumPdo(16, "device_state", "Device State",
            ((0, "Initialising"), (1, "Normal"), (2, "Filter Wizard"), (3, "Commissioning"),
             (4, "Supplier Factory"), (5, "Zehnder Factory"), (6, "Standby"), (7, "Away (preset)"),
             (8, "DFC")),
            "mdi:state-machine", DIAGNOSTIC),
    # 1 is the normal application mode (aiocomfoconnect: "1=normal (seen as active)"), 2 the
    # filter wizard. Value 0 has no documented meaning, so it stays unmapped and publishes null.
    EnumPdo(18, "changing_filters", "Filter Status",
            ((1, "Normal"), (2, "Changing Filter")), "mdi:air-filter"),
    # The SCHEDULE preset instance's active timer. 1 is what the Q350 (unit firmware R1.11.0)
    # reports in manual mode with NO countdown (PDO 81 all-ones), so it is plain "Manual" -- the
    # display's temporary airflow change that reverts to AUTO is not what that unit showed. 5
    # ("unlimited manual" in aiocomfoconnect) has NOT been seen on a unit: UNVERIFIED. Its label
    # stays "Manual (permanent)" instead of gaining "(unverified)", because a label is the HA
    # integration's state key (option_keys.py) and confirming the value must not rename it.
    # 6 boost, 11 away (TASK MENU > AWAY), 255 auto. Anything else publishes null.
    EnumPdo(49, "operating_mode", "Operating Mode",
            ((1, "Manual"), (5, "Manual (permanent)"), (6, "Boost"), (11, "Away"),
             (255, "Auto")),
            "mdi:tune"),
    # Disabled by default, both: balance_mode (a select) and 70/71 already say the same thing.
    EnumPdo(54, "supply_fan_mode", "Supply Fan Mode",
            ((1, "Supply Only"), (255, "Balanced")), "mdi:home-import-outline", DIAGNOSTIC,
            enabled_by_default=False),
    # Display name and option follow TASK MENU > VENTILATION > EXTRACT ONLY (User p. 13); the key
    # stays `exhaust_fan_mode` and the protocol is unchanged.
    EnumPdo(55, "exhaust_fan_mode", "Extract Fan Mode",
            ((1, "Extract Only"), (255, "Balanced")), "mdi:home-export-outline", DIAGNOSTIC,
            enabled_by_default=False),
    EnumPdo(65, "fan_level", "Fan Level",
            ((0, "Away"), (1, "Low"), (2, "Medium"), (3, "High")), "mdi:fan"),
    # The unit's own display settings. They are diagnostics, not controls -- but they say what the
    # numbers above actually mean. The nine temperature sensors are published as °C and PDO 119/120
    # as m³/h unconditionally; if the unit is configured otherwise, these two rows are the only
    # warning.
    EnumPdo(208, "temperature_unit", "Device Temperature Unit",
            ((0, "Celsius"), (1, "Fahrenheit")), "mdi:thermometer", DIAGNOSTIC),
    EnumPdo(224, "airflow_unit", "Device Airflow Unit",
            ((1, "kg/h"), (2, "l/s"), (3, "m³/h")), "mdi:weather-windy", DIAGNOSTIC),
)

#: PDO 230 answers "why is it not reaching the setpoint".
AIRFLOW_CONSTRAINTS = SpecialPdo(230, "airflow_constraints", "Airflow Constraints",
                                 "mdi:alert-circle-outline")


@dataclass(frozen=True)
class UnitErrorSpec:
    """One face of the unit's ACTIVE ERRORS. No PDO carries them: the probe task reads the ERROR
    unit's 32-byte bitset over RMI (`80 03 01`, GETACTIVEERRORS; aiocomfoconnect
    docs/PROTOCOL-RMI.md "ERROR Unit commands") every minute, and comfoair_sysinfo names the ids
    with the firmware-selected table. Until the first answer both rows are unknown, never a guess.

    `form` "any": a binary_sensor, on while at least one error that is not merely informational
    is active (a manual bypass, service mode or a filter reminder is the owner's own doing or a
    routine notice, never a Home Assistant problem). "text": a sensor listing every active id,
    informational ones included, in one line of at most 255 bytes (Home Assistant's state limit).
    Exactly one of each: the probe task writes them by two generated indices, never by key.
    """

    key: str
    name: str
    form: str
    icon: str | None = None
    device_class: str | None = None
    entity_category: str | None = None


UNIT_ERRORS: tuple[UnitErrorSpec, ...] = (
    # `problem`: Home Assistant shows OK / Problem and offers it to alert automations directly.
    UnitErrorSpec("unit_error", "Unit Error", "any", "mdi:alert-circle", "problem"),
    # The words, for the owner who got the alert. The ids are in GET /api/system unit.errors.
    UnitErrorSpec("active_errors", "Active Errors", "text", "mdi:alert-circle-outline",
                  None, DIAGNOSTIC),
)

SELECTS: tuple[SelectSpec, ...] = (
    # Bypass: options match the frozen set_bypass vocabulary (docs/interfaces/rest-api.md, POST /api/ops).
    SelectSpec("bypass_mode", "Bypass Mode", "SELECT_BYPASS_MODE",
               ("Auto", "Open", "Closed"), "mdi:valve", pdo=66),
    # PDO 67 value 0 is Normal, not "auto".
    #
    # !!! DO NOT REORDER TO MATCH THE MANUAL. !!!
    # This tuple is in PROTOCOL order -- index 0/1/2 IS the byte sent, and build_profile() in
    # comfoair_command_build.cpp casts the index straight to the value. The unit's own menu lists
    # these the other way round, WARM / NORMAL / COOL (hardware reference 13.3, User p. 15), so
    # "fixing" the order to match the display would silently send Warm when the user picks Normal.
    #
    # What PDO 212 reports behind this select, so its movement is not mistaken for a bug
    # (hardware reference 13.3, User p. 24): in FIXED mode WARM 24 / NORMAL 20 / COOL 18 °C; in
    # ADAPTIVE mode NORMAL is 21.5 °C at 0 °C RMOT and rises +0.11 °C per °C of RMOT, with
    # WARM = NORMAL + 1.5 °C and COOL = NORMAL - 1.5 °C. So under ADAPTIVE the target drifts every
    # day with the five-day mean outdoor temperature, by design.
    SelectSpec("temperature_profile", "Temperature Profile", "SELECT_TEMPERATURE_PROFILE",
               ("Normal", "Cool", "Warm"), "mdi:thermometer", pdo=67),
    # Derived in C++ from PDO 54 and 55; there is no single PDO for it.
    # Option follows the display: TASK MENU > VENTILATION > EXTRACT ONLY (User p. 13). The service
    # token stays `exhaust_only` -- machine token of the API (docs/interfaces/rest-api.md, POST /api/ops).
    SelectSpec("balance_mode", "Balance Mode", "SELECT_BALANCE_MODE",
               ("Balanced", "Supply Only", "Extract Only"), "mdi:arrow-split-vertical", pdo=None),
    # RMI properties of TEMPHUMCONTROL (unit 0x1D, subunit 0x01) that no PDO reports. Written by
    # RMI on control and READ BACK by RMI GET ONE in the probe task (comfoair_probe_task) at boot
    # and every 6 h, so the state is what the unit holds -- including what an installer set on
    # its own display. Until the first read answers the value is unknown, not a default.
    #
    # !!! DO NOT REORDER THESE THREE EITHER. !!!
    # ("Off", "Auto only", "On") is PROTOCOL order 0/1/2: the write's value byte is
    # 00 = off / 01 = auto / 02 = on, and the read-back decodes the answer byte as the option
    # index the same way -- so a reorder would invert both what is sent AND what is read. The
    # Zehnder manual *prints* them the other way round, ON / AUTO ONLY / OFF (hardware reference
    # 13.5, User p. 23); reordering to match would invert On and Off.
    SelectSpec("temperature_passive", "Temperature Passive", "SELECT_TEMPERATURE_PASSIVE",
               ("Off", "Auto only", "On"), "mdi:thermometer-minus", pdo=None,
               rmi=(0x1D, 0x01, 0x04)),
    SelectSpec("humidity_comfort", "Humidity Comfort", "SELECT_HUMIDITY_COMFORT",
               ("Off", "Auto only", "On"), "mdi:water-percent", pdo=None,
               rmi=(0x1D, 0x01, 0x06)),
    SelectSpec("humidity_protection", "Humidity Protection", "SELECT_HUMIDITY_PROTECTION",
               ("Off", "Auto only", "On"), "mdi:water-alert", pdo=None,
               rmi=(0x1D, 0x01, 0x07)),
    # Why this select exists: the six presets ARE this device's control surface, and a select
    # reaches them from automations and clients that do not handle a fan entity's preset modes.
    # This select is the second face on the same C++ derivation, not a second implementation.
    #
    # Options must equal FAN_PRESET_MODES -- see that tuple's own definition for why the order
    # matters too.
    #
    # Disabled by default (owner's decision): the select and the fan are the SAME control, and
    # two visible entities that move each other read as a defect. It stays available for
    # automations and clients that do not handle a fan's preset modes.
    SelectSpec("ventilation_mode", "Ventilation Mode", "SELECT_VENTILATION_MODE",
               FAN_PRESET_MODES, "mdi:fan", pdo=None, enabled_by_default=False),
)


def _all_request_pdos() -> tuple[int, ...]:
    pdos = {row.pdo for row in NUMERIC}
    pdos |= {row.pdo for row in BINARY}
    pdos |= {row.pdo for row in ENUM}
    pdos.add(AIRFLOW_CONSTRAINTS.pdo)
    # The switch's own PDO, not left to a binary row happening to share it: a switch with no
    # feedback would sit at "unknown" while its commands still went out.
    pdos.add(AWAY_SWITCH.pdo)
    pdos |= set(FAN_PDOS)
    pdos |= set(BALANCE_PDOS)
    pdos |= {spec.pdo for spec in SELECTS if spec.pdo is not None}
    return tuple(sorted(pdos))


#: Every PDO the component polls on its request cycle.
ALL_REQUEST_PDOS: tuple[int, ...] = _all_request_pdos()
