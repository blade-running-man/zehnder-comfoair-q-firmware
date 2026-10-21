"""The entity table is the ComfoAir Q350/450/600 base unit's, with the audit's corrections.

Pinned against pdo_table.py and the generator's flattening, like test_registry.py. The removed
PDOs are named here on purpose: removing them was a SCHEMA_VERSION change that left their retained
MQTT topics on every broker, to be cleaned up by hand (docs/development/entity-registry.md,
"Retiring keys"). Re-adding one is a deliberate scope change, never a quiet tidy-up.
"""
from conftest import _load

table = _load("pdo_table")
gen = _load("generate_registry")

#: Accessory, Option-Box and raw schedule PDOs the firmware neither decodes nor polls.
REMOVED_PDOS = frozenset({
    33, 37, 53, 85, 176, 321, 325, 341, 342, 343, 369, 370, 371, 372, 400, 401, 402,
    416, 417, 418, 419, 784, 785, 802, *range(1024, 1032),
    4145, 4148, 4149, 4151, 4154, *range(4193, 4199), 4201,
})


def _rows():
    return (*table.NUMERIC, *table.BINARY, *table.ENUM)


def _row(key):
    return next(r for r in _rows() if r.key == key)


def _entity(key):
    return next(e for e in gen.build_entities() if e.key == key)


def test_no_accessory_option_box_or_schedule_word_pdo_is_decoded_or_polled():
    assert not REMOVED_PDOS & {r.pdo for r in _rows()}
    assert not REMOVED_PDOS & set(table.ALL_REQUEST_PDOS)


def test_away_indicator_is_gone_and_pdo_16_feeds_device_state_alone():
    entities = gen.build_entities()
    assert "away_indicator" not in {e.key for e in entities}
    assert [e.key for e in entities if e.pdo == 16] == ["device_state"]


def test_removing_entities_bumped_the_schema_to_2():
    assert table.SCHEMA_VERSION == 2


def test_the_q350_labels():
    assert dict(_row("operating_mode").options) == {
        1: "Manual", 5: "Manual (permanent)", 6: "Boost", 11: "Away", 255: "Auto"}
    assert dict(_row("changing_filters").options) == {1: "Normal", 2: "Changing Filter"}
    assert dict(_row("device_state").options)[7] == "Away (preset)"


def test_pdo_225_is_a_raw_disabled_diagnostic_under_its_old_key():
    e = _entity("sensor_ventilation_mode")
    assert (e.kind, e.pdo, e.options, e.enabled_by_default) == ("numeric", 225, None, False)
    assert e.entity_category == "diagnostic"
    assert e.name == "Sensor Ventilation Mode (raw, unverified)"


def test_pdo_338_is_a_bit_test_on_the_override_timer_under_its_old_key():
    r = _row("bypass_override")
    assert isinstance(r, table.BinaryPdo)
    assert (r.pdo, r.op, r.value, r.name) == (338, "bit", 1, "Bypass Override Active")


def test_exactly_these_rows_are_created_disabled():
    disabled = {e.key for e in gen.build_entities() if not e.enabled_by_default}
    assert disabled == {"ventilation_mode", "supply_fan_mode", "exhaust_fan_mode",
                        "intake_air_temp_unverified", "sensor_ventilation_mode",
                        "frost_protection_preheater_power"}


def test_flows_are_volume_flow_rates_and_speeds_are_rpm():
    for key in ("exhaust_fan_flow", "supply_fan_flow"):
        assert (_row(key).unit, _row(key).device_class) == ("m³/h", "volume_flow_rate")
    for key in ("exhaust_fan_speed", "supply_fan_speed"):
        assert _row(key).unit == "rpm"


def test_pdo_221_keeps_its_key_under_its_new_name():
    r = _row("supply_air_temp_post_heater")
    assert (r.pdo, r.name) == (221, "Supply Air Temperature (controller)")


def test_the_registry_is_76_entities():
    assert len(gen.build_entities()) == 76
