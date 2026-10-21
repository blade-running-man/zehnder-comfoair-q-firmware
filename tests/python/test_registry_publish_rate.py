"""The MQTT publication-rate fields (pdo_table.py NumericPdo.publish_deadband and
publish_min_interval_s) against the generator, and the per-row values the Q350 census chose.

The values are pinned on purpose: each is argued from census frame rates and the audit's live
jitter in docs/interfaces/mqtt.md ("Publication rate"), which publishes the same table.
"""
import dataclasses
import re

import pytest

from conftest import _load

table = _load("pdo_table")
gen = _load("generate_registry")

LIMITED = {
    "exhaust_fan_flow": (5, 30), "supply_fan_flow": (5, 30),
    "exhaust_fan_speed": (25, 30), "supply_fan_speed": (25, 30),
    "avoided_heating": (10, 60), "avoided_cooling": (10, 60),
    "intake_air_temp_unverified": (None, 30), "supply_air_temp_post_heater": (None, 30),
    "extract_air_temp": (None, 30), "exhaust_air_temp": (None, 30),
    "outdoor_air_temp": (None, 30), "preheated_outdoor_air_temp": (None, 30),
    "supply_air_temp": (None, 30),
}


def _row_block(header: str, key: str) -> str:
    return re.search(rf'\.key = "{key}",.*?\n    }},', header, re.S).group(0)


def test_exactly_the_jittery_rows_are_rate_limited():
    got = {r.key: (r.publish_deadband, r.publish_min_interval_s) for r in table.NUMERIC
           if r.publish_deadband is not None or r.publish_min_interval_s}
    assert got == LIMITED


def test_no_other_kind_can_carry_a_policy():
    for rows in (table.BINARY, table.ENUM, table.SELECTS, table.NUMBERS, table.UNIT_ERRORS):
        for r in rows:
            assert not hasattr(r, "publish_deadband"), r.key


def test_the_fields_reach_the_c_registry():
    header = gen.render_c_header()
    for key, (band, gap) in LIMITED.items():
        block = _row_block(header, key)
        expected = "NAN" if band is None else f"{float(band)}f"
        assert f".publish_deadband = {expected}," in block, key
        assert f".publish_min_interval_s = {gap}," in block, key
    block = _row_block(header, "operating_mode")
    assert ".publish_deadband = NAN," in block
    assert ".publish_min_interval_s = 0," in block


def test_the_max_hold_is_emitted_and_is_the_freshness_period():
    header = gen.render_c_header()
    assert gen.PUBLISH_MAX_HOLD_S == 300 == gen.FRESHNESS_PERIOD_S
    assert f"#define COMFOAIR_PUBLISH_MAX_HOLD_S  {gen.PUBLISH_MAX_HOLD_S}\n" in header


def test_the_policy_is_mqtt_only_and_never_reaches_rest_types():
    assert "deadband" not in gen.render_ts_types().lower()
    assert "min_interval" not in gen.render_ts_types().lower()


def _refuses(monkeypatch, **fields):
    base = next(r for r in gen.table.NUMERIC if r.key == "supply_fan_speed")
    bad = dataclasses.replace(base, key="bad_rate", **fields)
    monkeypatch.setattr(gen.table, "NUMERIC", gen.table.NUMERIC + (bad,))
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_refuses_a_zero_deadband(monkeypatch):
    _refuses(monkeypatch, publish_deadband=0.0)


def test_refuses_a_negative_deadband(monkeypatch):
    _refuses(monkeypatch, publish_deadband=-1.0)


def test_refuses_a_min_interval_the_max_hold_would_overtake(monkeypatch):
    _refuses(monkeypatch, publish_min_interval_s=gen.PUBLISH_MAX_HOLD_S)


def test_refuses_a_fractional_min_interval(monkeypatch):
    _refuses(monkeypatch, publish_min_interval_s=1.5)
