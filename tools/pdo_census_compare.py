#!/usr/bin/env python3
"""pdo_census_compare: a PDO census dump against the entity table, as Markdown.

    curl -s http://<ip>/api/probe > q350-pdos.json
    python3 tools/pdo_census_compare.py q350-pdos.json

The dump is the `GET /api/probe` document; only its "pdos" key is read (docs/protocol/probe.md,
"The PDO census"). Four sections are printed:

  1. heard and in the table -- each PDO with its entity keys, data lengths, senders and frame count;
  2. heard but not in the table -- each PDO with its data lengths, senders and last payload;
  3. in the table but never heard, split into enabled-by-default rows (silent on any unit: a
     finding) and disabled-by-default rows (raw, duplicate or unverified figures);
  4. the summary, with a warning when the census is shorter than one hourly request sweep, when
     PDOs did not fit the census, or when the firmware's `known` flag disagrees with this table.

Exit status: 0 after printing, 2 when the dump cannot be read, is not JSON, or has no "pdos".
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import sys
from dataclasses import dataclass
from pathlib import Path

# The request sweep runs at boot and then hourly (docs/protocol/polling.md). 65 minutes covers the
# first hourly sweep with five minutes of margin, so a PDO the unit sends only when asked has been
# asked twice before its silence is reported as a finding rather than as "not asked yet".
MIN_UPTIME_MS = 65 * 60 * 1000


def _load(name: str):
    """A sibling module of this file, loaded by PATH, exactly as generate_registry.py does.

    DO NOT replace this with sys.path surgery: tests/python loads this script by path from another
    directory, and every module in tools/ would become importable by bare name from wherever this
    happened to be loaded.
    """
    spec = importlib.util.spec_from_file_location(name, Path(__file__).parent / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    # dataclasses resolves string annotations through sys.modules, so register before exec.
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


@dataclass(frozen=True)
class TablePdo:
    keys: tuple[str, ...]   # every registry entity decoded from this PDO, sorted
    enabled: bool           # any of them enabled by default in Home Assistant


def fold(entities, request_pdos) -> dict[int, TablePdo]:
    """The registry entities folded onto the polled PDOs: keys sorted, enabled if any is.

    A PDO counts as enabled when ANY of its entities is: it is then polled for a value the owner
    sees by default, so its silence is a finding even if a sibling key on it is disabled. DO NOT
    turn this into "all enabled": a PDO with one disabled sibling would drop out of the findings.
    Entities without a PDO (commands, derived and synthetic rows) are skipped.
    """
    keys: dict[int, set[str]] = {}
    enabled: dict[int, bool] = {}
    for e in entities:
        if e.pdo is None:
            continue
        keys.setdefault(e.pdo, set()).add(e.key)
        enabled[e.pdo] = enabled.get(e.pdo, False) or bool(e.enabled_by_default)
    return {pdo: TablePdo(tuple(sorted(keys.get(pdo, ()))), enabled.get(pdo, True))
            for pdo in request_pdos}


def table_pdos() -> dict[int, TablePdo]:
    """Every PDO the firmware polls, with the registry entities it feeds.

    The keys come from generate_registry.build_entities() rather than from pdo_table.py's row
    tuples: the generator is the one place that flattens every row kind (numeric, binary, enum,
    select, the special and the switch), so reading the tuples here would be a second flattening
    that a new row kind could slip past. The PDO set itself is ALL_REQUEST_PDOS, the list the
    firmware's COMFOAIR_REQUEST_PDOS is generated from.
    """
    gen = _load("generate_registry")
    return fold(gen.build_entities(), gen.table.ALL_REQUEST_PDOS)


class DumpError(Exception):
    """The dump cannot be compared; the message says why."""


def read_census(path: Path) -> dict:
    """The "pdos" object of a dump, or DumpError naming what is wrong with the file."""
    try:
        text = path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError) as exc:
        raise DumpError(f"{path}: cannot be read ({exc})") from exc
    try:
        doc = json.loads(text)
    except json.JSONDecodeError as exc:
        raise DumpError(f"{path}: not JSON ({exc})") from exc
    census = doc.get("pdos") if isinstance(doc, dict) else None
    if not isinstance(census, dict):
        # The firmware emits "pdos" only when the device supplies a census, so a dump without it
        # is from a build without the census, or a walk report saved by other means.
        raise DumpError(f'{path}: no "pdos" object -- the dump is not from a firmware with '
                        "the PDO census")
    return census


def _nums(values) -> str:
    return ", ".join(str(v) for v in sorted(values)) or "-"


def _last(hexstr: str) -> str:
    return f"`{hexstr}`" if hexstr else "(empty)"


def _table(head: tuple[str, ...], lines: list[tuple]) -> list[str]:
    if not lines:
        return ["None.", ""]
    out = ["| " + " | ".join(head) + " |", "|" + "|".join(" --- " for _ in head) + "|"]
    out += ["| " + " | ".join(str(c) for c in line) + " |" for line in lines]
    return out + [""]


def render(census: dict, table: dict[int, TablePdo], source: str) -> str:
    """The whole Markdown report. Every list is sorted, so two runs over one dump are equal."""
    rows = sorted(census.get("rows", []), key=lambda r: r["pdo"])
    heard = {r["pdo"] for r in rows}

    in_table = [(r["pdo"], ", ".join(table[r["pdo"]].keys), _nums(r["dlc"]), _nums(r["nodes"]),
                 r["count"]) for r in rows if r["pdo"] in table]
    foreign = [(r["pdo"], _nums(r["dlc"]), _nums(r["nodes"]), r["count"], _last(r["last"]))
               for r in rows if r["pdo"] not in table]
    silent = [(pdo, ", ".join(t.keys), t.enabled) for pdo, t in sorted(table.items())
              if pdo not in heard]
    silent_on = [(pdo, keys) for pdo, keys, on in silent if on]
    silent_off = [(pdo, keys) for pdo, keys, on in silent if not on]

    out = [f"# PDO census: {source}", ""]
    out += ["## Heard and in the table", ""]
    out += _table(("PDO", "Keys", "DLC", "Nodes", "Count"), in_table)
    out += ["## Heard but not in the table", ""]
    out += _table(("PDO", "DLC", "Nodes", "Count", "Last"), foreign)
    out += ["## In the table but never heard", ""]
    out += ["### Enabled by default", "",
            "Silent on any unit: each one is a finding (a PDO this unit does not send, or a "
            "table error).", ""]
    out += _table(("PDO", "Keys"), silent_on)
    out += ["### Disabled by default", "",
            "Raw, duplicate or unverified rows the firmware creates disabled.", ""]
    out += _table(("PDO", "Keys"), silent_off)
    out += ["## Summary", ""]
    out += _summary(census, rows, table, len(in_table), len(foreign),
                    len(silent_on), len(silent_off))
    return "\n".join(out).rstrip("\n") + "\n"


def _summary(census: dict, rows: list[dict], table: dict[int, TablePdo], n_in: int,
             n_foreign: int, n_on: int, n_off: int) -> list[str]:
    uptime = int(census.get("uptime_ms", 0))
    overflow = int(census.get("overflow", 0))
    out = [f"- Uptime: {uptime} ms ({uptime / 60000:.1f} min)",
           f"- Frames: {census.get('frames', 0)}",
           f"- Distinct PDOs heard: {census.get('distinct', 0)}; rows recorded: {len(rows)}; "
           f"overflow: {overflow}",
           f"- Heard and in the table: {n_in} of {len(table)}",
           f"- Heard but not in the table: {n_foreign}",
           f"- In the table but never heard: {n_on} enabled by default, {n_off} disabled",
           ""]
    # A stamp past uptime_ms is only possible if the u32 millisecond clock wrapped (>= 49.7 days
    # up), so uptime_ms then reads small while the unit is long past the threshold. Both stamps are
    # checked: a PDO first heard early may have been heard last just before the wrap.
    wrapped = any(int(r.get("first_ms", 0)) > uptime or int(r.get("last_ms", 0)) > uptime
                  for r in rows)
    if uptime < MIN_UPTIME_MS and not wrapped:
        out.append(f"> Warning: uptime {uptime / 60000:.1f} min is shorter than two request "
                   "sweeps (boot + one hourly, 65 min); a never-heard PDO may only have "
                   "missed its one answer, so \"never heard\" is not yet a finding.")
        out.append("")
    if overflow > 0:
        out.append(f"> Warning: overflow {overflow} -- that many heard PDOs did not fit the "
                   "census table, so \"never heard\" may list PDOs that were heard but not "
                   "recorded.")
        out.append("")
    mismatched = [r["pdo"] for r in rows if bool(r.get("known")) != (r["pdo"] in table)]
    if mismatched:
        out.append(f"> Warning: `known` disagrees with the table for PDO {_nums(mismatched)} "
                   "-- the dump comes from a firmware built from another pdo_table.py.")
        out.append("")
    return out


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="pdo_census_compare",
        description="Compare a PDO census dump (GET /api/probe) with the entity table.",
        epilog="Exit status: 0 after printing; 2 when the dump cannot be read, is not JSON, "
               'or has no "pdos".')
    parser.add_argument("dump", type=Path, help="the saved GET /api/probe document")
    args = parser.parse_args(argv)
    try:
        census = read_census(args.dump)
    except DumpError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    sys.stdout.write(render(census, table_pdos(), args.dump.name))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
