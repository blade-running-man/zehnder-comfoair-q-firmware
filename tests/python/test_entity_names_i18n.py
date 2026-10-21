"""The DE/NL/UK entity-name overlay against pdo_table.py.

entity_names_i18n.py is a second table keyed by entity key, which is exactly the shape that
drifts. render_entity_names_ts.py refuses to emit a mismatched table; these tests pin that the
overlay matches today AND that the refusal actually fires, so a later "make the build green"
edit cannot quietly turn the guard into a warning. The web side keeps the same guard on the
emitted file (web/src/i18n/tests/entityNames.test.ts).
"""
import pytest

from conftest import _load

gen = _load("generate_registry")
# The renderer the generator itself loaded, not a second copy: a fresh _load() would execute
# the module again and monkeypatching its table would not touch the one the generator renders.
names = gen._render_names


def test_overlay_keys_match_the_registry_exactly():
    registry = {e.key for e in gen.build_entities()}
    translated = set(names.ENTITY_NAMES_I18N)
    assert translated == registry, (
        f"missing: {sorted(registry - translated)}; orphan: {sorted(translated - registry)}"
    )


def test_every_row_has_every_language_and_no_english_column():
    # An "en" column would be a second English name beside pdo_table.py's -- a second list.
    for key, row in names.ENTITY_NAMES_I18N.items():
        assert set(row) == set(names.LOCALES), f"{key} has locales {sorted(row)}"
        for loc in names.LOCALES:
            assert row[loc].strip(), f'{key} has no "{loc}" name'


def test_the_render_refuses_a_missing_translation(monkeypatch):
    monkeypatch.delitem(names.ENTITY_NAMES_I18N, "fan")
    with pytest.raises(ValueError, match="missing translations"):
        gen.render_entity_names_ts()


def test_the_render_refuses_an_orphan_translation(monkeypatch):
    monkeypatch.setitem(names.ENTITY_NAMES_I18N, "no_such_entity",
                        {"de": "x", "nl": "x", "uk": "x"})
    with pytest.raises(ValueError, match="not entities"):
        gen.render_entity_names_ts()


def test_the_render_refuses_an_empty_name(monkeypatch):
    monkeypatch.setitem(names.ENTITY_NAMES_I18N, "fan", {"de": "Lüftung", "nl": " ", "uk": "x"})
    with pytest.raises(ValueError, match='"fan" has no "nl" name'):
        gen.render_entity_names_ts()


def test_the_english_column_is_the_registry_name():
    ts = gen.render_entity_names_ts()
    for e in gen.build_entities():
        assert f'"{e.key}": {{ en: "{e.name}",' in ts, f"{e.key} lost its registry name"
