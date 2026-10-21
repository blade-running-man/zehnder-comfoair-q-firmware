"""The unit's active errors (pdo_table.py UNIT_ERRORS): two entities no PDO carries, read over
RMI by the probe task and written into the state model by two generated indices."""
import re

import pytest

from conftest import _load

table = _load("pdo_table")
gen = _load("generate_registry")


def test_two_rows_one_of_each_form():
    assert [(r.key, r.form) for r in table.UNIT_ERRORS] == [
        ("unit_error", "any"), ("active_errors", "text")]


def test_a_problem_binary_and_a_diagnostic_text_with_no_pdo_and_no_default():
    es = {e.key: e for e in gen.build_entities()}
    any_, text = es["unit_error"], es["active_errors"]
    assert (any_.kind, any_.pdo, any_.device_class) == ("binary", None, "problem")
    assert (text.kind, text.pdo, text.entity_category) == ("special", None, "diagnostic")
    # No default: both are bus-backed (comfoair_ha_payload.c bus_backed()), unknown until read.
    assert any_.default is None and text.default is None
    assert not any_.writable and not text.writable


def test_they_are_not_polled():
    assert len(table.ALL_REQUEST_PDOS) == 60


def test_the_generated_indices_point_at_the_rows():
    header = gen.render_c_header()
    keys = re.findall(r'\.key\s*=\s*"([^"]+)"', header)
    m = re.search(r"enum \{ COMFOAIR_UNIT_ERROR_ANY = (\d+), COMFOAIR_UNIT_ERROR_TEXT = (\d+) \};",
                  header)
    assert m, "the unit-error indices are not emitted"
    assert keys[int(m.group(1))] == "unit_error"
    assert keys[int(m.group(2))] == "active_errors"


def test_the_registry_is_76_entities():
    assert len(gen.build_entities()) == 76


def test_refuses_two_rows_of_one_form(monkeypatch):
    extra = table.UnitErrorSpec("second_text", "Second", "text")
    monkeypatch.setattr(gen.table, "UNIT_ERRORS", gen.table.UNIT_ERRORS + (extra,))
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_refuses_an_unknown_form(monkeypatch):
    bad = (table.UnitErrorSpec("unit_error", "Unit Error", "count"), gen.table.UNIT_ERRORS[1])
    monkeypatch.setattr(gen.table, "UNIT_ERRORS", bad)
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_no_label_anywhere_is_spelled_none():
    for e in gen.build_entities():
        for label in e.options or ():
            assert label.strip().lower() != "none", (e.key, label)


def test_refuses_an_option_spelled_none(monkeypatch):
    bad = table.SelectSpec("bad_none", "Bad", "SELECT_BAD", ("Auto", "none"), "mdi:alert", pdo=66)
    monkeypatch.setattr(gen.table, "SELECTS", gen.table.SELECTS + (bad,))
    with pytest.raises(SystemExit):
        gen.build_entities()
