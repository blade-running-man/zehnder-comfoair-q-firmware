"""tools/pdo_census_compare.py: a PDO census dump (`GET /api/probe`, "pdos") against the table.

The dump is synthetic but the table is the real one, loaded through the generator, so a test here
fails when the script stops reflecting pdo_table.py -- which is the whole job of the script.
"""
import json
from types import SimpleNamespace

import pytest

from conftest import _load

cmp = _load("pdo_census_compare")
table = _load("pdo_table")

HOUR_MS = 60 * 60 * 1000


def row(pdo, known, count=10, dlc=(1,), nodes=(1,), last="01"):
    return {"pdo": pdo, "known": known, "count": count, "first_ms": 1000, "last_ms": 2000,
            "dlc": list(dlc), "nodes": list(nodes), "last": last}


def dump(rows, uptime_ms=2 * HOUR_MS, overflow=0):
    return {"state": "idle", "pdos": {"uptime_ms": uptime_ms, "frames": 4242,
                                      "distinct": len(rows) + overflow, "overflow": overflow,
                                      "rows": rows}, "truncated": False}


ROWS = [row(16, True, count=412, last="01"),
        row(7000, False, count=3, dlc=(2, 4), nodes=(1, 55), last="a1b2c3d4"),
        row(275, True, count=60, dlc=(2,), last="e600"),
        row(8, False, dlc=(0,), last="")]


def run(tmp_path, capsys, doc, raw=None):
    path = tmp_path / "census.json"
    path.write_text(raw if raw is not None else json.dumps(doc), encoding="utf-8")
    code = cmp.main([str(path)])
    out = capsys.readouterr()
    return code, out.out, out.err


def section(text, heading):
    """The body of one `## ` section, up to the next one."""
    start = text.index(f"## {heading}\n")
    end = text.find("\n## ", start + 1)
    return text[start:] if end < 0 else text[start:end]


def test_the_table_is_every_polled_pdo_with_its_keys():
    pdos = cmp.table_pdos()
    assert set(pdos) == set(table.ALL_REQUEST_PDOS)
    assert pdos[16].keys == ("device_state",)
    assert pdos[337].keys == ("away_mode", "away_timer_running", "boost_timer_running")
    assert pdos[65].enabled and not pdos[219].enabled


def entity(key, pdo, enabled):
    return SimpleNamespace(key=key, pdo=pdo, enabled_by_default=enabled)


def test_one_enabled_entity_makes_its_pdo_enabled():
    # The real table has no PDO mixing enabled and disabled entities, so the fold is pinned on
    # made-up rows: an enabled sibling means the owner sees a value from this PDO by default, and
    # its silence is a finding.
    folded = cmp.fold([entity("b_off", 900, False), entity("a_on", 900, True),
                       entity("x_off", 901, False), entity("y_off", 901, False),
                       entity("no_pdo", None, True)], (900, 901))
    assert folded == {900: cmp.TablePdo(("a_on", "b_off"), True),
                      901: cmp.TablePdo(("x_off", "y_off"), False)}


def test_every_section_in_order(tmp_path, capsys):
    code, out, _ = run(tmp_path, capsys, dump(ROWS))
    assert code == 0
    headings = [line for line in out.splitlines() if line.startswith("## ")]
    assert headings == ["## Heard and in the table", "## Heard but not in the table",
                        "## In the table but never heard", "## Summary"]

    heard = section(out, "Heard and in the table")
    assert "| 16 | device_state | 1 | 1 | 412 |" in heard
    assert heard.index("| 16 |") < heard.index("| 275 |")

    foreign = section(out, "Heard but not in the table")
    assert "| 7000 | 2, 4 | 1, 55 | 3 | `a1b2c3d4` |" in foreign
    assert "| 8 | 0 | 1 | 10 | (empty) |" in foreign

    never = section(out, "In the table but never heard")
    enabled, disabled = never.split("### Disabled by default")
    assert "| 65 | fan_level |" in enabled and "| 219 |" not in enabled
    assert "| 219 | frost_protection_preheater_power |" in disabled and "| 65 |" not in disabled
    assert "| 16 |" not in never and "| 275 |" not in never

    summary = section(out, "Summary")
    assert "Warning" not in summary


def test_output_does_not_depend_on_row_order(tmp_path, capsys):
    _, first, _ = run(tmp_path, capsys, dump(ROWS))
    _, second, _ = run(tmp_path, capsys, dump(list(reversed(ROWS))))
    assert first == second


@pytest.mark.parametrize("minutes, warned", [(64, True), (65, False), (120, False)])
def test_a_census_shorter_than_two_request_sweeps_is_warned(tmp_path, capsys, minutes, warned):
    _, out, _ = run(tmp_path, capsys, dump(ROWS, uptime_ms=minutes * 60 * 1000))
    assert ("Warning: uptime" in section(out, "Summary")) is warned


@pytest.mark.parametrize("stamp", ["first_ms", "last_ms"])
def test_a_wrapped_clock_skips_the_uptime_warning(tmp_path, capsys, stamp):
    rows = [dict(r) for r in ROWS]
    rows[0][stamp] = 10 * 60 * 1000 + 1
    _, out, _ = run(tmp_path, capsys, dump(rows, uptime_ms=10 * 60 * 1000))
    assert "Warning: uptime" not in section(out, "Summary")


def test_overflow_is_warned(tmp_path, capsys):
    _, out, _ = run(tmp_path, capsys, dump(ROWS, overflow=3))
    assert "Warning: overflow 3" in section(out, "Summary")


def test_a_known_flag_that_disagrees_with_the_table_is_warned(tmp_path, capsys):
    # The firmware marks `known` from its own compiled table; a disagreement means the dump came
    # from a build of another table than the one in this checkout.
    rows = [row(16, False), row(7000, True)]
    _, out, _ = run(tmp_path, capsys, dump(rows))
    assert "Warning: `known` disagrees with the table for PDO 16, 7000" in section(out, "Summary")


def test_a_dump_without_pdos_exits_2(tmp_path, capsys):
    code, out, err = run(tmp_path, capsys, {"state": "idle", "truncated": False})
    assert code == 2 and out == ""
    assert '"pdos"' in err


def test_a_dump_that_is_not_json_exits_2(tmp_path, capsys):
    code, out, err = run(tmp_path, capsys, None, raw="{not json")
    assert code == 2 and out == ""
    assert "not JSON" in err


def test_help_exits_0(capsys):
    with pytest.raises(SystemExit) as exc:
        cmp.main(["--help"])
    assert exc.value.code == 0
    assert "pdo_census_compare" in capsys.readouterr().out
