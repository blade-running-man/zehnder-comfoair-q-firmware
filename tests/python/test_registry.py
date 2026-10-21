"""The generator against pdo_table.py.

AGENTS.md: every entity has exactly one place in the API, the MQTT topics and
discovery; no duplicates, none lost. These tests are that rule, made executable. They are
written against the table, never against the generator's output shape -- a test that only
re-reads what the generator wrote proves nothing about whether the table got through.
"""
import re

from conftest import _load

table = _load("pdo_table")
gen = _load("generate_registry")


def all_entity_keys():
    """Every key the firmware is supposed to expose, derived from the table alone."""
    keys = []
    keys += [r.key for r in table.NUMERIC]
    keys += [r.key for r in table.BINARY]
    keys += [r.key for r in table.ENUM]
    keys += [r.key for r in table.UNIT_ERRORS]
    keys += [r.key for r in table.SELECTS]
    keys += [r.key for r in table.NUMBERS]
    keys.append(table.AIRFLOW_CONSTRAINTS.key)
    keys.append(table.AWAY_SWITCH.key)
    keys.append(table.FAN_KEY)
    keys.append(table.SCHEMA_VERSION_KEY)
    return keys


def test_the_table_itself_has_no_duplicate_keys():
    keys = all_entity_keys()
    assert len(keys) == len(set(keys)), "duplicate key in pdo_table.py"


def test_the_contract_counts_hold():
    # AGENTS.md, v1 scope: the ComfoAir Q350/450/600 base unit -- "all 58 values (60 polled PDOs),
    # 13 controllable entities". Accessory, Option-Box and raw schedule rows are not published
    # (docs/reference/comfoair-pdo-rmi.md §1).
    values = len(table.NUMERIC) + len(table.BINARY) + len(table.ENUM)
    controllable = len(table.SELECTS) + len(table.NUMBERS) + 2  # + fan + away switch
    assert values == 58
    assert controllable == 13
    assert len(table.ALL_REQUEST_PDOS) == 60


def test_every_entity_reaches_the_c_registry_exactly_once():
    # Counted in KEY POSITION (`.key = "..."`), not as a bare quoted string. Four entities --
    # the next_*_change_in_raw countdowns -- deliberately have name == key (pdo_table.py,
    # "schedule countdowns"). A bare string count would read those
    # as duplicates and would be measuring the wrong thing: the invariant is one DESCRIPTOR
    # per key, not one occurrence of the characters.
    header = gen.render_c_header()
    for key in all_entity_keys():
        occurrences = len(re.findall(rf'\.key\s*=\s*"{re.escape(key)}"', header))
        assert occurrences == 1, f"{key} has {occurrences} descriptors in the C registry"


def test_every_entity_reaches_the_typescript_types_exactly_once():
    ts = gen.render_ts_types()
    for key in all_entity_keys():
        occurrences = len(re.findall(rf'key:\s*"{re.escape(key)}"', ts))
        assert occurrences == 1, f"{key} has {occurrences} descriptors in types.ts"


def test_the_four_countdown_keys_survive_verbatim():
    # The HA integration resolves these by KEY (docs/development/entity-registry.md), so a
    # rename of the key is a two-repository break. Their names equal their keys on purpose:
    # they are raw second counts the integration turns into timestamps, not labels a person
    # reads -- and the integration never keys on a name.
    frozen = ("next_fan_change_in_raw", "next_bypass_change_in_raw",
              "next_supply_change_in_raw", "next_exhaust_change_in_raw")
    header = gen.render_c_header()
    for key in frozen:
        row = next(r for r in table.NUMERIC if r.key == key)
        assert row.name == key, "the table itself lost the frozen name"
        assert re.search(rf'\.key\s*=\s*"{key}"[^}}]*\.name\s*=\s*"{key}"', header, re.S)


def test_the_registry_declares_the_count_it_actually_emits():
    header = gen.render_c_header()
    declared = int(re.search(r"COMFOAIR_ENTITY_COUNT\s*=\s*(\d+)", header).group(1))
    # The table's rows PLUS the one entity the generator declares itself (see the test below).
    # "+ 1" and not "+ len(something)": exactly one synthetic row exists, and an assertion that
    # tolerated any number of them would tolerate the second entity list AGENTS.md forbids.
    assert declared == len(all_entity_keys()) + 1


def test_the_freshness_entity_is_declared_by_the_generator_not_by_hand():
    """The freshness entity (`data_freshness`), and the two places it must NOT be written down.

    The entity is synthetic: nothing on the CAN bus reports it, so pdo_table.py cannot carry
    it -- and must not be made to. That file is the protocol table, every row of it something
    the unit reports, and a row the bus never carries would break that at the one file the
    whole registry is projected from. So it is declared here, in the generator, and it leaves
    through the same single output as every other row (generate_registry.py:24,
    registry_generated.h).

    The other forbidden place is a hand-written discovery record in components/comfoair_ha/.
    That would be the second entity list outright: one entity known to Home Assistant and to
    nothing else -- not to REST, not to the state model, not to the web UI -- and the count
    tests would go on agreeing with themselves while the surfaces disagreed. This test is
    what fails when somebody takes that shortcut; a count of 76 would not.
    """
    key = gen.FRESHNESS_KEY
    assert key not in all_entity_keys(), "the synthetic entity leaked into pdo_table.py"

    table_source = (gen.ROOT / "tools" / "pdo_table.py").read_text()
    assert key not in table_source, "pdo_table.py carries only what the bus reports"

    header = gen.render_c_header()
    assert len(re.findall(rf'\.key\s*=\s*"{re.escape(key)}"', header)) == 1
    assert len(re.findall(rf'key:\s*"{re.escape(key)}"', gen.render_ts_types())) == 1

    # And nowhere in the hand-written firmware: the key reaches C as COMFOAIR_FRESHNESS_KEY,
    # which the generator emits beside the row. A source file spelling the key out is either a
    # second declaration of the entity or a topic built without asking the registry -- the two
    # failures comfoair_ha.h's enumerations exist to make impossible.
    quoted = f'"{key}"'
    for path in sorted((gen.ROOT / "components").rglob("*.[ch]")) + \
            sorted((gen.ROOT / "components").rglob("*.cpp")):
        if path.name == "registry_generated.h":
            continue
        assert quoted not in path.read_text(), f"{path} names the freshness key by hand"


def test_the_freshness_period_and_expiry_are_emitted_as_a_pair():
    """The freshness entity's two numbers, and the shape that stops them being separated.

    They are emitted as two adjacent literals followed by a static_assert binding them, and
    the assert is why the generator carries two constants rather than deriving one from the
    other: `#define EXPIRE (3 * PERIOD)` would make the check true by construction and would
    survive any mutation of either number. Written this way, moving one is a BUILD failure in
    every translation unit that includes the registry.
    """
    assert gen.FRESHNESS_EXPIRE_S == 3 * gen.FRESHNESS_PERIOD_S
    header = gen.render_c_header()
    period = header.index("#define COMFOAIR_FRESHNESS_PERIOD_S")
    expire = header.index("#define COMFOAIR_FRESHNESS_EXPIRE_S")
    assert 0 < expire - period < 120, "the period and the expiry stopped being adjacent"
    assert re.search(r"static_assert\(\s*COMFOAIR_FRESHNESS_EXPIRE_S\s*==\s*"
                     r"3\s*\*\s*COMFOAIR_FRESHNESS_PERIOD_S", header)
    assert f"#define COMFOAIR_FRESHNESS_PERIOD_S  {gen.FRESHNESS_PERIOD_S}\n" in header
    assert f"#define COMFOAIR_FRESHNESS_EXPIRE_S  {gen.FRESHNESS_EXPIRE_S}\n" in header


def test_every_polled_pdo_is_in_the_generated_request_list():
    header = gen.render_c_header()
    block = re.search(r"COMFOAIR_REQUEST_PDOS\[\][^{]*\{([^}]*)\}", header, re.S).group(1)
    emitted = sorted(int(n) for n in re.findall(r"\d+", block))
    assert emitted == sorted(table.ALL_REQUEST_PDOS)


def test_enum_options_keep_their_order():
    # Order is contract-visible: web_server shows a select's first option before any state
    # arrives, so a reorder changes what the user sees on a cold boot.
    header = gen.render_c_header()
    first = table.ENUM[0]
    labels = re.search(
        rf'{first.key}_options\[\][^{{]*\{{(.*?)\}}', header, re.S).group(1)
    emitted = re.findall(r'"([^"]*)"', labels)
    assert emitted == [label for _, label in first.options]


def test_fan_preset_modes_survive_in_order():
    # !!! DO NOT REORDER !!! in the table; the generator must not reorder them either.
    ts = gen.render_ts_types()
    block = re.search(r"FAN_PRESET_MODES[^=]*=\s*\[(.*?)\]", ts, re.S).group(1)
    assert re.findall(r'"([^"]*)"', block) == list(table.FAN_PRESET_MODES)


def test_a_write_only_secret_never_appears_in_the_entity_registry():
    # Passwords are write-only and must not be readable through any projection of the API
    # (docs/architecture/security.md, Write-only secrets). The registry is one of those projections.
    header = gen.render_c_header() + gen.render_ts_types()
    for forbidden in ("mqtt_password", "web_password", "wifi_password"):
        assert forbidden not in header


# --- selects read back over RMI ----------------------------------------------------------------
#
# Three selects have no PDO. Before they were read back each was seeded with an invented factory
# default at every boot, so Home Assistant showed a value the unit did not hold. The table now
# says where each is read from, and the probe task reads it.

RMI_OPTIONS = ("Off", "Auto only", "On")
RMI_EXPECTED = {
    "temperature_passive": (0x1D, 0x01, 0x04),
    "humidity_comfort": (0x1D, 0x01, 0x06),
    "humidity_protection": (0x1D, 0x01, 0x07),
}


def _select(key):
    return next(s for s in table.SELECTS if s.key == key)


def test_the_three_selects_name_their_rmi_property():
    for key, rmi in RMI_EXPECTED.items():
        assert _select(key).rmi == rmi, key
    assert {s.key for s in table.SELECTS if s.rmi is not None} == set(RMI_EXPECTED)


def test_a_read_back_select_has_no_pdo_the_protocol_options_and_no_default():
    # The byte read IS the option index, so any other option set would decode to a wrong label;
    # a default would be the invented value this read-back exists to replace.
    entities = {e.key: e for e in gen.build_entities()}
    for spec in table.SELECTS:
        if spec.rmi is None:
            continue
        assert spec.pdo is None, spec.key
        assert spec.options == RMI_OPTIONS, spec.key
        assert entities[spec.key].default is None, spec.key
        assert not hasattr(spec, "default_index"), "default_index outlived its last reader"


def _rejects(monkeypatch, bad):
    import pytest
    monkeypatch.setattr(gen.table, "SELECTS", table.SELECTS + (bad,))
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_the_generator_refuses_a_read_back_with_other_options(monkeypatch):
    bad = table.SelectSpec("bad_rmi", "Bad", "SELECT_BAD", ("On", "Auto only", "Off"),
                           "mdi:alert", rmi=(0x1D, 0x01, 0x08))
    _rejects(monkeypatch, bad)


def test_the_generator_refuses_a_read_back_that_also_has_a_pdo(monkeypatch):
    bad = table.SelectSpec("bad_rmi", "Bad", "SELECT_BAD", RMI_OPTIONS, "mdi:alert", pdo=66,
                           rmi=(0x1D, 0x01, 0x08))
    _rejects(monkeypatch, bad)


def test_the_rendered_read_back_table_points_at_the_right_entities():
    header = gen.render_c_header()
    assert "COMFOAIR_RMI_READBACK_COUNT = 3" in header
    keys = re.findall(r'\.key\s*=\s*"([^"]+)"', header)
    block = re.search(r"COMFOAIR_RMI_READBACK\[[^\]]*\]\s*=\s*\{(.*?)\n\};", header,
                      re.S).group(1)
    rows = re.findall(r"\{\s*(\d+),\s*0x([0-9a-f]+),\s*0x([0-9a-f]+),\s*0x([0-9a-f]+)\s*\}",
                      block)
    got = {keys[int(i)]: (int(u, 16), int(s, 16), int(p, 16)) for i, u, s, p in rows}
    assert len(rows) == 3
    assert got == RMI_EXPECTED


def test_every_pdo_fits_the_can_id():
    # pdo_decode.h: the PDO number is bits 14..26 of the CAN id. pdo_request_can_id() does not
    # clamp, so a larger number would poll some other PDO; this is its PRECONDITION, enforced.
    assert max(table.ALL_REQUEST_PDOS) <= 0x1FFF


def test_a_bit_binary_names_a_bit_that_exists():
    for r in table.BINARY:
        assert r.op in ("eq", "ne", "bit"), f"{r.key}: unknown op {r.op}"
        if r.op == "bit":
            assert 0 <= r.value <= 63, f"{r.key}: bit {r.value} is outside a 64-bit PDO"


def test_the_preset_timer_bits_are_boost_and_away():
    # aiocomfoconnect docs/PROTOCOL-PDO.md, PDO 337: bit 6 is timer 6 (boost), bit 11 timer 11
    # (away) -- the same timer numbers the SCHEDULE commands use (84 15 01 06 / 84 15 01 0B).
    rows = {r.key: r for r in table.BINARY if r.pdo == 337}
    assert (rows["boost_timer_running"].op, rows["boost_timer_running"].value) == ("bit", 6)
    assert (rows["away_timer_running"].op, rows["away_timer_running"].value) == ("bit", 11)


def test_enabled_by_default_reaches_both_outputs():
    header = gen.render_c_header()
    ts = gen.render_ts_types()
    assert re.search(r'\.key = "ventilation_mode",[^}]*\.enabled_by_default = false', header, re.S)
    assert re.search(r'\.key = "fan",[^}]*\.enabled_by_default = true', header, re.S)
    assert re.search(r'key: "ventilation_mode",[^}]*enabledByDefault: false', ts)


def _refuses_binary(monkeypatch, bad):
    import pytest
    monkeypatch.setattr(gen.table, "BINARY", table.BINARY + (bad,))
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_the_generator_refuses_an_unknown_binary_op(monkeypatch):
    # The C decoder reads anything that is not "ne" or "bit" as "eq": a typo must not build.
    _refuses_binary(monkeypatch, table.BinaryPdo(337, "bad_op", "Bad", 6, "bitt", "mdi:alert"))


def test_the_generator_refuses_a_bit_outside_a_64_bit_pdo(monkeypatch):
    _refuses_binary(monkeypatch, table.BinaryPdo(337, "bad_bit", "Bad", 64, "bit", "mdi:alert"))


def test_the_generator_refuses_a_pdo_the_can_id_cannot_carry(monkeypatch):
    import pytest
    monkeypatch.setattr(gen.table, "ALL_REQUEST_PDOS", table.ALL_REQUEST_PDOS + (0x2000,))
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_the_away_switch_reads_the_away_timer_its_command_drives():
    # The switch starts and cancels SCHEDULE timer 11 (84 15 01 0B / 85 15 01 0B), so its state
    # is timer 11 running: PDO 337 bit 11. PDO 16 == 7 follows the fan's Away PRESET as well
    # (owner's Q350), and an OFF could never clear an ON that came from the preset.
    sw = table.AWAY_SWITCH
    assert (sw.pdo, sw.op, sw.active_value) == (337, "bit", 11)
    header = gen.render_c_header()
    row = re.search(r'\.key = "away_mode",.*?\n    \}', header, re.S).group(0)
    assert ".pdo = 337," in row
    assert ".match_value = 11," in row
    assert ".match_bit = true," in row
    assert ".match_negated = false," in row


def test_the_away_switch_is_not_derived_from_pdo_16():
    entity = next(e for e in gen.build_entities() if e.key == "away_mode")
    assert entity.pdo != 16


def _refuses_switch(monkeypatch, bad):
    import pytest
    monkeypatch.setattr(gen.table, "AWAY_SWITCH", bad)
    with pytest.raises(SystemExit):
        gen.build_entities()


def test_the_generator_refuses_a_switch_op_the_decoder_does_not_take(monkeypatch):
    # The switch's command is an on/off timer, so "ne" has no meaning for it; a typo must not
    # build as a silent "eq" either.
    for op in ("ne", "bitt"):
        _refuses_switch(monkeypatch, table.SwitchSpec("away_mode", "Away Mode", 337, 11,
                                                      "mdi:home-export-outline", op))


def test_the_generator_refuses_a_switch_bit_outside_a_64_bit_pdo(monkeypatch):
    _refuses_switch(monkeypatch, table.SwitchSpec("away_mode", "Away Mode", 337, 64,
                                                  "mdi:home-export-outline", "bit"))
