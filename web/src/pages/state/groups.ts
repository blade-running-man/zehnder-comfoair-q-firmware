// The State page's layout: which group each entity goes in, and its place within that group.
//
// orderedGroups() (order.ts) reads this table: this file holds the rows, order.ts the logic over
// them.
//
// GROUPS below is the layout, with every non-obvious placement carrying the comment that
// justifies it. It is NOT a second entity list of the kind AGENTS.md forbids: pdo_table.py is
// the only place an entity's EXISTENCE, name, unit, class or writability are
// declared, and nothing here restates any of that. GROUPS only says, for a key pdo_table.py may or
// may not currently emit, where it goes if it does. Two things keep that safe rather than a covert
// second list:
//
//   * A key GROUPS does not mention still renders -- orderedGroups() falls back to groupOf() (the
//     old protocol-facts derivation) and appends it after the group's listed keys. An entity
//     nobody placed must never go invisible; see orderedGroups() in order.ts.
//   * A key GROUPS mentions that pdo_table.py no longer emits must fail loudly, not vanish. Two
//     layers catch this, deliberately redundant:
//       - PRIMARY: EntityGroup.keys below is typed against the live key union
//         `(typeof ENTITIES)[number]["key"]`, so a rename or removal in pdo_table.py fails
//         `tsc -b` at the offending string literal, inside the firmware build itself
//         (components/web_assets/CMakeLists.txt runs `tsc -b` on every `pio run`) -- the
//         loudest failure this project has, and it fires before anyone remembers to run a test.
//       - BELT-AND-BRACES: tests/order.test.ts's runtime "every listed key exists in ENTITIES"
//         loop catches the same drift for a bare `node order.test.ts`, which does not typecheck
//         (harness.ts's own header explains why these suites are plain node, not a type-checked
//         runner). Either layer alone would miss one of the two ways this file gets run; keeping
//         both is what makes GROUPS safe to hardcode keys in at all.

import type { ENTITIES } from "../../api/entities.ts";

/** The only keys GROUPS may name -- pdo_table.py's current key set, not a list kept here. */
export type KnownKey = (typeof ENTITIES)[number]["key"];

export interface EntityGroup {
  readonly id: string;
  readonly label: string;
  /**
   * Display order within the group.
   *
   * Typed as `readonly KnownKey[]`, not `readonly string[]`: a key pdo_table.py renames or
   * removes then fails `tsc -b` right here, at the stale literal, rather than only being caught
   * by the runtime suite the next time someone remembers to run it (see the file header).
   */
  readonly keys: readonly KnownKey[];
}

export const GROUPS: readonly EntityGroup[] = [
  {
    id: "ventilation",
    label: "Ventilation",
    keys: [
      "ventilation_mode",
      "fan",
      "fan_level",
      "operating_mode",
      "device_state",
      // The unit's own active errors, read over RMI every minute: beside the device state they
      // qualify. The System page lists the same ids with their numbers.
      "unit_error",
      "active_errors",
      // Last in this group, not third: ventilation_mode's own options include "Away" (PRESET A),
      // and this switch is TASK MENU > AWAY, which ignores every scheduler -- different things.
      "away_mode",
    ],
  },
  {
    id: "temperature",
    label: "Temperatures",
    keys: [
      // Zehnder's own airflow order: what comes in, what it becomes, what goes out.
      "outdoor_air_temp",
      "preheated_outdoor_air_temp",
      "supply_air_temp",
      "supply_air_temp_post_heater",
      "extract_air_temp",
      "exhaust_air_temp",
      "profile_target_temp",
      "running_mean_outdoor_temp",
    ],
  },
  {
    id: "humidity",
    label: "Humidity",
    keys: [
      "outdoor_air_humidity",
      "preheated_outdoor_air_humidity",
      "supply_air_humidity",
      "extract_air_humidity",
      "exhaust_air_humidity",
    ],
  },
  {
    id: "airflow",
    label: "Airflow and fans",
    keys: [
      // Supply before extract, and flow before the duty and speed that produce it. The flows are
      // COMPUTED, not measured -- see the README's "Airflow is not measured".
      "supply_fan_flow",
      "exhaust_fan_flow",
      "supply_fan_duty",
      "exhaust_fan_duty",
      "supply_fan_speed",
      "exhaust_fan_speed",
      // The unit's own single modulated-speed figure, beside the two measured per-fan speeds it
      // summarises rather than replaces.
      "fan_speed_modulated",
      // "Why is it not reaching the setpoint" belongs beside the numbers it explains, not in the
      // "Other" bucket where the old protocol-facts derivation put it: entities.ts gives this
      // key entityCategory: null, deviceClass: null and unit: null with kind "special", which
      // matches none of groupOf()'s branches (order.ts) and falls through to "other" --
      // not, as an earlier draft of this comment claimed, to Diagnostics.
      "airflow_constraints",
    ],
  },
  {
    id: "bypass",
    label: "Bypass and season",
    keys: [
      "bypass_mode",
      "bypass_position",
      // Whether a bypass override is in force: PDO 338's override-timer bit, a second,
      // independently encoded confirmation beside bypass_position -- not beside "Bypass Override
      // Duration" in Timers, which sets a timer rather than reporting one.
      "bypass_override",
      "heating_season",
      "cooling_season",
      // The % the supply fan is cut back while frost protection runs: besides the bypass, the
      // other thing the season does to the air path. The base unit's own figure (pdo_table.py).
      "frost_protection_unbalance",
    ],
  },
  {
    id: "energy",
    label: "Energy",
    keys: [
      // The ventilation figures are FANS ONLY; the avoided pairs are the heat exchanger's saving.
      "power",
      "energy_ytd",
      "energy_total",
      "preheater_power",
      // The pre-heater's draw while it is doing frost protection: the pre-heater is part of the
      // Q itself, so this row belongs beside its other figures, always shown.
      "frost_protection_preheater_power",
      "preheater_energy_ytd",
      "preheater_energy_total",
      "avoided_heating",
      "avoided_heating_ytd",
      "avoided_heating_total",
      "avoided_cooling",
      "avoided_cooling_ytd",
      "avoided_cooling_total",
    ],
  },
  {
    id: "filter",
    label: "Filter",
    keys: [
      // The one number on the page that ever asks the owner to do something. "DAYS LEFT -
      // INDICATIVE", and it can move backwards faster than a day per day.
      // The old derivation had no Filter group at all and buried this in "Other".
      "filter_replacement_remaining_days",
      // Beside the countdown it qualifies -- "12 days left" reads differently once this says
      // Changing Filter instead of Normal.
      "changing_filters",
    ],
  },
  {
    id: "timers",
    label: "Timers",
    keys: [
      // The four settable durations, then whether the boost and away timers they start are
      // running (PDO 337). The next_*_change_in_raw countdowns are in Diagnostics -- see below.
      "boost_duration",
      "bypass_duration",
      "balance_duration",
      "away_duration",
      "boost_timer_running",
      "away_timer_running",
    ],
  },
  {
    id: "settings",
    label: "Settings",
    keys: [
      "temperature_profile",
      "balance_mode",
      // The three sensor-ventilation controls. No PDO carries them: the firmware reads them from
      // the unit over RMI at start-up and every 6 hours (unknown until the first read), so a
      // value set here shows the optimistic echo until the next read confirms it.
      "temperature_passive",
      "humidity_comfort",
      "humidity_protection",
    ],
  },
  {
    id: "diagnostic",
    label: "Diagnostics",
    keys: [
      // Every temperature on this page is published as deg C and every flow as m3/h
      // unconditionally (see pdo_table.py's comment on these two rows); they are the ONLY
      // indication if the unit is actually configured otherwise, which is why they head the
      // group.
      "temperature_unit",
      "airflow_unit",
      // This unit's own mode flags.
      "manual_mode",
      "supply_only_active",
      "exhaust_only_active",
      "supply_fan_mode",
      "exhaust_fan_mode",
      // Moved out of Timers deliberately: these four keys are resolved by name by the HA
      // integration (docs/development/entity-registry.md) as machine tokens carrying a raw second count, not something a person reads
      // as a label, and pdo_table.py already marks all four DIAGNOSTIC. Under the friendly
      // "Timers" heading they made that group read as two unrelated halves; here, they join this
      // unit's other mode flags -- what a fan or bypass is doing now, and how much longer, are
      // the same kind of fact.
      "next_fan_change_in_raw",
      "next_bypass_change_in_raw",
      "next_supply_change_in_raw",
      "next_exhaust_change_in_raw",
      // The "(unverified)" readings sit here and nowhere else. Their labels are uncorroborated by
      // any manual or the protocol reference; they must not sit among readings a user would
      // trust.
      "intake_air_temp_unverified",
      "sensor_ventilation_mode",
      // Placed rather than left to groupOf(). The freshness entity (data_freshness) exists
      // for Home Assistant's `expire_after` and says nothing on THIS page: if the page loaded,
      // the device answered. So it belongs at the bottom, just above schema_version, which stays
      // last because it is at least a fact about this firmware rather than about a mechanism in
      // somebody else's process. Without this line the entityCategory fallback would append it
      // AFTER schema_version by accident.
      "data_freshness",
      // A machine-facing diagnostic row, not something a person
      // reads by choice -- the true bottom of the group, mirroring temperature_unit/airflow_unit
      // at its head.
      "schema_version",
    ],
  },
];
