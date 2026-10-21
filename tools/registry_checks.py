"""The table's preconditions, refused at generation time.

Split out of generate_registry.py for the 350-line ceiling. Every check takes the table (or one
row, or the flattened entities) as an ARGUMENT and imports nothing from tools/, the same
no-back-import rule render_c.py follows: the generator imports this module, never the reverse.
"""
from __future__ import annotations

# The only option set a read-back select may carry: the probe task stores the answer byte AS the
# option index, and the unit's TEMPHUMCONTROL properties encode 00 = off / 01 = auto / 02 = on.
RMI_SELECT_OPTIONS = ("Off", "Auto only", "On")


def check_rmi_select(spec) -> None:
    """Refuse a read-back select whose answer could not be decoded or would have two sources.

    DO NOT relax this to "same length": a reordered tuple of the same three labels decodes the
    unit's On as Off, which is exactly the inversion the table's DO NOT REORDER warns against.
    A pdo beside an rmi would be two writers of one value racing each other in the state model.
    """
    if spec.rmi is None:
        return
    if spec.pdo is not None:
        raise SystemExit(f"select {spec.key}: rmi read-back and pdo {spec.pdo} are exclusive")
    if tuple(spec.options) != RMI_SELECT_OPTIONS:
        raise SystemExit(f"select {spec.key}: rmi read-back needs options "
                         f"{RMI_SELECT_OPTIONS}, got {tuple(spec.options)}")
    if len(spec.rmi) != 3 or not all(0 <= b <= 0xFF for b in spec.rmi):
        raise SystemExit(f"select {spec.key}: rmi must be three bytes, got {spec.rmi}")


def check_match(what: str, key: str, op: str, value: int, ops: tuple[str, ...]) -> None:
    """Refuse a comparison the C decoder does not make, and a bit a PDO cannot hold.

    The decoder reads any op that is not "ne" or "bit" as "eq", so a typo would build and quietly
    compare. Binary rows and the switch go through the same decoder branch, so the same check.
    """
    if op not in ops:
        raise SystemExit(f"{what} {key}: op must be {', '.join(ops)}, got {op!r}")
    if op == "bit" and not 0 <= value <= 63:
        raise SystemExit(f"{what} {key}: bit {value} is outside a 64-bit PDO")


def check_table(table) -> None:
    """Refuse a bad binary or switch comparison and a PDO the CAN id cannot carry.

    pdo_request_can_id() does not clamp the PDO number (bits 14..26 of the id), so one above 8191
    would poll some other PDO; this is that function's precondition, enforced here. The switch
    takes no "ne": its command turns a timer on or off, and on-while-not-X has no such command.
    """
    for r in table.BINARY:
        check_match("binary", r.key, r.op, r.value, ("eq", "ne", "bit"))
    sw = table.AWAY_SWITCH
    check_match("switch", sw.key, sw.op, sw.active_value, ("eq", "bit"))
    if max(table.ALL_REQUEST_PDOS) > 0x1FFF:
        raise SystemExit("a PDO above 8191 does not fit the CAN id (pdo_decode.h PDO_ID_MAX)")


def check_publish_rate(table, max_hold_s: int) -> None:
    """Refuse a publication-rate field the MQTT publisher could not honour.

    A deadband of 0 would read as a policy while holding nothing back -- None says "no deadband".
    A minimum interval at or past the max hold would never be reached: the max hold sends first.
    """
    for r in table.NUMERIC:
        band, gap = r.publish_deadband, r.publish_min_interval_s
        if band is not None and not band > 0:
            raise SystemExit(f"numeric {r.key}: publish_deadband must be > 0 or None, got {band!r}")
        if not isinstance(gap, int) or isinstance(gap, bool) or not 0 <= gap < max_hold_s:
            raise SystemExit(f"numeric {r.key}: publish_min_interval_s must be an int in "
                             f"0..{max_hold_s - 1}, got {gap!r}")


def check_unit_errors(table) -> None:
    """Exactly one row of each form: the probe task writes them by the two generated indices."""
    forms = sorted(r.form for r in table.UNIT_ERRORS)
    if forms != ["any", "text"]:
        raise SystemExit(f"UNIT_ERRORS needs exactly one 'any' and one 'text' row, got {forms}")


def check_no_none_label(entities) -> None:
    """Refuse a label Home Assistant would read as "no value".

    MQTT publishes the literal `None` for an entity with no reading, and HA's handlers compare
    against it before anything else -- mqtt/sensor.py:317, and mqtt/select.py:124 does so
    case-insensitively. A select option or enum label spelled "None" would therefore show as
    unknown whenever it is the real state.
    """
    for e in entities:
        for label in e.options or ():
            if label.strip().lower() == "none":
                raise SystemExit(f"{e.key}: option {label!r} is Home Assistant's 'no value' payload")
