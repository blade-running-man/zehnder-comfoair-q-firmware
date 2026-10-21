#!/usr/bin/env python3
"""Generates the entity registry from pdo_table.py -- the only place entities are declared.

AGENTS.md: one entity list, ever -- never a second, hand-written one.
Everything the firmware exposes -- the C registry that REST, MQTT and HA discovery all read,
and the TypeScript types the web UI is written against -- comes out of here, and so does the
web-only table of the entity names in every UI language. Editing any output by hand recreates
exactly the divergence this file exists to prevent, which is why each carries a do-not-edit
banner and why CI regenerates and diffs them.

    python3 tools/generate_registry.py            # write the outputs
    python3 tools/generate_registry.py --check    # fail if they are stale (for CI)

Widths are absent on purpose: the decoder takes the width from the CAN data-length code at
runtime (pdo_decode.h), so the table declares only signedness and divider. Do not add widths.
"""
from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C_OUT = ROOT / "components" / "comfoair_registry" / "include" / "registry_generated.h"
# Entities only. The device CONFIG types (Wi-Fi, broker, auth) are hand-written in
# web/src/api/client.ts, beside the calls that carry them: pdo_table.py knows nothing about
# configuration, so they must never be generated. DO NOT point this at client.ts or any other
# hand-written module -- the first run would overwrite it.
TS_OUT = ROOT / "web" / "src" / "api" / "entities.ts"
# The entity display names in every UI language -- a web-only overlay. Home Assistant sees the
# English registry name only, through discovery; this table never reaches the firmware.
NAMES_OUT = ROOT / "web" / "src" / "i18n" / "entityNames.generated.ts"

# --- THE ONE ENTITY THIS FILE DECLARES RATHER THAN PROJECTS ----------------------------------
#
# Everything else here is a projection of tools/pdo_table.py. This row is not: nothing on the CAN
# bus reports it, because what it reports is THIS NODE still talking: a heartbeat Home Assistant
# expires on its own, so a node that died without its will firing (the whole house lost power,
# broker included) stops being shown as current. See docs/interfaces/home-assistant-discovery.md.
#
# WHY IT IS DECLARED HERE AND NOT IN pdo_table.py, which would look like the tidier home for it:
# that file is the protocol table -- every row in it is something the unit reports on the bus --
# and a row the bus never carries would break that one property at the single file the whole
# registry is projected from. Here it leaves through the same single output as every other row
# (C_OUT below), so there is still one entity list -- it simply has one row the table did not
# supply. The same construction closed keys_v1.txt.
#
# WHY IT IS NOT A HAND-WRITTEN DISCOVERY RECORD in components/comfoair_ha/, which is the shape a
# draft reached for: an entity Home Assistant knows about and REST, the state model and the web UI
# do not is the second entity list AGENTS.md forbids, and every count in the tree would
# go on agreeing with itself while the four surfaces disagreed.
FRESHNESS_KEY = "data_freshness"
FRESHNESS_NAME = "Data Freshness"
# The publication cadence, and the expiry Home Assistant is told to apply to it. THREE PERIODS,
# and the two numbers are emitted adjacent with a static_assert binding them (render_c.py's
# render_c_header) so that no edit can move one without the other: expire_after's timer is reset by every
# message on the state topic, so an expiry is meaningless except as a multiple of the cadence
# feeding it. Two publications may be lost to a slow broker before the owner's card goes grey.
#
# 300 and not 60 or 180. The need is to notice that a house has no electricity -- an event that
# lasts minutes and that nobody reacts to inside three of them. Five minutes buys the same answer
# for a fifth of the traffic: 288 messages a day against 1440.
#
# DO NOT derive one from the other here (`FRESHNESS_EXPIRE_S = 3 * FRESHNESS_PERIOD_S`). It reads
# tighter and it removes the guard: the emitted assertion would then be true by construction and
# would survive any mutation of either number. Two literals is what makes "they were separated" a
# build failure.
FRESHNESS_PERIOD_S = 300
FRESHNESS_EXPIRE_S = 900

# The longest an MQTT publication may be held back by a row's publish_deadband or
# publish_min_interval_s (pdo_table.py): after this many seconds any DIFFERENCE goes out. It
# repeats nothing -- the one publication on a clock stays the freshness entity's -- and it equals
# FRESHNESS_PERIOD_S so that one number describes both: nothing on the broker is more than five
# minutes behind the unit. Literal, not derived, for the reason given for the pair above.
PUBLISH_MAX_HOLD_S = 300


def _load(name: str):
    """A sibling module of this file, loaded by PATH rather than by `import`.

    tests/python loads this generator by path too, from a directory that is not tools/, so a
    plain `import render_c` would only resolve when the generator is run as a script. DO NOT
    replace this with sys.path surgery: it would make every module in tools/ importable by bare
    name from wherever the generator happens to be loaded, and hide exactly that difference.
    """
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parent / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolves string annotations through sys.modules, so register before exec.
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


table = _load("pdo_table")
_render_c = _load("render_c")
_render_ts = _load("render_ts")
_render_names = _load("render_entity_names_ts")
_checks = _load("registry_checks")


class Entity:
    """One row of the registry, flattened from whichever table shape produced it.

    Flattening happens HERE and only here: the C and TypeScript renderers both walk this
    list, so the two outputs cannot drift apart by one renderer learning about a table shape
    the other does not.
    """

    def __init__(self, key, name, kind, *, pdo=None, signed=False, divider=1,
                 none_when_all_ones=False, unit=None, device_class=None, state_class=None,
                 icon=None, entity_category=None, options=None, option_values=None,
                 match_value=None, match_negated=False, min_value=None, max_value=None,
                 default=None, writable=False, rmi=None, match_bit=False,
                 enabled_by_default=True, publish_deadband=None, publish_min_interval_s=0,
                 unit_error=None):
        self.key = key
        self.name = name
        self.kind = kind
        self.pdo = pdo
        self.signed = signed
        self.divider = divider
        self.none_when_all_ones = none_when_all_ones
        self.unit = unit
        self.device_class = device_class
        self.state_class = state_class
        self.icon = icon
        self.entity_category = entity_category
        self.options = options
        self.option_values = option_values
        self.match_value = match_value
        self.match_negated = match_negated
        self.match_bit = match_bit
        self.min_value = min_value
        self.max_value = max_value
        self.default = default
        self.writable = writable
        self.rmi = rmi   # (unit, subunit, property) read back by the probe task, or None
        self.enabled_by_default = enabled_by_default
        self.publish_deadband = publish_deadband             # MQTT only; None = no deadband
        self.publish_min_interval_s = publish_min_interval_s  # MQTT only; 0 = no gap
        self.unit_error = unit_error   # "any" / "text" for the UNIT_ERRORS rows, else None


def build_entities() -> list[Entity]:
    out: list[Entity] = []
    _checks.check_table(table)
    _checks.check_publish_rate(table, PUBLISH_MAX_HOLD_S)
    _checks.check_unit_errors(table)

    for r in table.NUMERIC:
        out.append(Entity(r.key, r.name, "numeric", pdo=r.pdo, signed=r.signed,
                          divider=r.divider, none_when_all_ones=r.none_when_all_ones,
                          unit=r.unit, device_class=r.device_class,
                          state_class=r.state_class, icon=r.icon,
                          entity_category=r.entity_category,
                          enabled_by_default=r.enabled_by_default,
                          publish_deadband=r.publish_deadband,
                          publish_min_interval_s=r.publish_min_interval_s))

    for r in table.BINARY:
        out.append(Entity(r.key, r.name, "binary", pdo=r.pdo, icon=r.icon,
                          device_class=r.device_class, entity_category=r.entity_category,
                          match_value=r.value, match_negated=(r.op == "ne"),
                          match_bit=(r.op == "bit"),
                          enabled_by_default=r.enabled_by_default))

    for r in table.ENUM:
        out.append(Entity(r.key, r.name, "enum", pdo=r.pdo, icon=r.icon,
                          entity_category=r.entity_category,
                          options=[label for _, label in r.options],
                          option_values=[value for value, _ in r.options],
                          enabled_by_default=r.enabled_by_default))

    special = table.AIRFLOW_CONSTRAINTS
    out.append(Entity(special.key, special.name, "special", pdo=special.pdo, icon=special.icon))

    # The unit's active errors: no PDO, read over RMI by the probe task. Existing kinds on purpose
    # -- a binary and a special text -- so every surface that switches on the kind (discovery,
    # REST, the integration's platform map) already places them; `unit_error` tells render_c.py
    # which generated index to emit.
    for r in table.UNIT_ERRORS:
        out.append(Entity(r.key, r.name, "binary" if r.form == "any" else "special",
                          icon=r.icon, device_class=r.device_class,
                          entity_category=r.entity_category, unit_error=r.form))

    for r in table.SELECTS:
        _checks.check_rmi_select(r)
        # No default for any select: one with no PDO is either read back over RMI or derived in
        # C++, and until either has a value "unknown" is the truth (pdo_table.py SelectSpec.rmi).
        out.append(Entity(r.key, r.name, "select", pdo=r.pdo, icon=r.icon,
                          options=list(r.options), writable=True, rmi=r.rmi,
                          enabled_by_default=r.enabled_by_default))

    for r in table.NUMBERS:
        # No PDO carries these: a duration is only ever told to the unit as part of a
        # command, so the value lives on this node alone.
        out.append(Entity(r.key, r.name, "number", icon=r.icon, writable=True, unit=r.unit,
                          min_value=r.min_value, max_value=r.max_value, default=r.default))

    sw = table.AWAY_SWITCH
    out.append(Entity(sw.key, sw.name, "switch", pdo=sw.pdo, icon=sw.icon,
                      match_value=sw.active_value, match_bit=(sw.op == "bit"), writable=True))

    out.append(Entity(table.FAN_KEY, table.FAN_NAME, "fan", writable=True,
                      options=list(table.FAN_PRESET_MODES)))

    out.append(Entity(table.SCHEMA_VERSION_KEY, table.SCHEMA_VERSION_NAME, "diagnostic_const",
                      entity_category="diagnostic"))

    # The synthetic row (the freshness entity, see the constants at the top of this file).
    # `connectivity` on a binary_sensor is the only pair Home Assistant will attach expire_after to
    # -- the option exists on binary_sensor.py:54 and sensor.py:80 and nowhere else in
    # components/mqtt/ -- and its ARRIVAL, not its value, is the fact being published. It carries no
    # PDO and no default: its payload is a constant, and the state model seeds it by kind
    # (comfoair_state.cpp).
    out.append(Entity(FRESHNESS_KEY, FRESHNESS_NAME, "freshness",
                      device_class="connectivity", entity_category="diagnostic"))

    _checks.check_no_none_label(out)
    keys = [e.key for e in out]
    duplicates = {k for k in keys if keys.count(k) > 1}
    if duplicates:
        raise SystemExit(f"duplicate entity keys in pdo_table.py: {sorted(duplicates)}")
    return out


# --- C and the name overlay: bodies in their own modules -----------------------------------

def render_c_header() -> str:
    # The freshness constants are handed over rather than read back from this module by
    # render_c.py: the renderer takes everything as arguments so it never imports its importer.
    return _render_c.render_c_header(build_entities(), table,
                                     freshness_key=FRESHNESS_KEY,
                                     freshness_period_s=FRESHNESS_PERIOD_S,
                                     freshness_expire_s=FRESHNESS_EXPIRE_S,
                                     publish_max_hold_s=PUBLISH_MAX_HOLD_S)


def render_entity_names_ts() -> str:
    return _render_names.render_entity_names_ts(build_entities())


# --- TypeScript: body in its own module -----------------------------------------------------

def render_ts_types() -> str:
    return _render_ts.render_ts_types(build_entities(), table)


# --- entry point -----------------------------------------------------------------------------

def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true",
                        help="exit non-zero if the generated files are stale")
    args = parser.parse_args()

    outputs = {C_OUT: render_c_header(), TS_OUT: render_ts_types(),
               NAMES_OUT: render_entity_names_ts()}

    if args.check:
        stale = [p for p, content in outputs.items()
                 if not p.exists() or p.read_text(encoding="utf-8") != content]
        for path in stale:
            print(f"stale: {path.relative_to(ROOT)}", file=sys.stderr)
        if stale:
            print("run: python3 tools/generate_registry.py", file=sys.stderr)
            return 1
        return 0

    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(content, encoding="utf-8")
        print(f"wrote {path.relative_to(ROOT)} ({len(content.splitlines())} lines)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
