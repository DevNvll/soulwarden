#!/usr/bin/env python3
"""Check of the spell data tool with made-up slice nodes (no file of the game).

The spell data tool (spells.py) must write no file from a source that is not complete. Code review of stage 3.3b-S2a,
finding F5: a delay of a delayed event with a value of one byte (not a 32-bit float) gave the delay 0, and the burst of
the vortex then came at the start of the spell. This check gives the entry point of the tool one spell table and one
slice (a spell component, a delayed event of slot 7 to a child entity with the damage row of the burst) with a source
provider that reads no game copy, and compares the status and the output file of each case. The test program of the
backend runs it (case RG5; the case trap-slice: case SB1 of stage 3.3b-S2b). Code review of stage 3.3b-S2b, finding F12: an
AutoSpell (the rule of a summon) with a lifetime of one byte gave the lifetime 0; the cases autospell and
autospell-lifetime-width check it (case RK12). Code review of stage 1.1b, finding F2: a Vitals component with no row
reference gave the member vitals null and a paid Pylon with no damage path; the case vitals-no-row checks that the tool keeps
the old file (case RD2).

Usage: check_spell_tool.py WORK_FOLDER
Status 0: each case has the expected result.
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import uuid
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import spells as tool  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402
from slice_ids import COMPONENT_TYPE, Element  # noqa: E402

MARK = "earlier output\n"
ID_TYPE = uuid.UUID(int=0)
ASSET = (bytes.fromhex("5150e110000040008000000000000001"), 2)
PREFAB = "Items/Weapons/Test_Vortex"


def element(name=None, value=None, kind=None, children=()) -> Element:
    node = Element()
    node.name, node.value, node.type_id, node.children = name, value, kind or ID_TYPE, list(children)
    return node


def identifier(name: int, number: int) -> Element:
    return element(name, number.to_bytes(8, "big"), ID_TYPE)


def component(kind, serial: int, fields: list) -> Element:
    base = element(tool.field_id("BaseClass1"), kind=COMPONENT_TYPE, children=[element(tool.field_id("Id"), serial.to_bytes(8, "big"))])
    return element(kind=kind, children=[base, *fields])


def slice_nodes(delay: bytes, trap: bool = False, lifetime: bytes | None = None, vitals: bool = False) -> list[Element]:
    """@return The nodes of a slice: a root entity with a Spell component (slot 1) and a delayed event (slot 7) of `delay`.
    `trap`: a PositionInTheWorld component (slot 1) in place of the Spell component, and an event that starts at a trigger
    (format 2, stage 3.3b-S2b). `lifetime`: an AutoSpell component with this m_lifetime value (finding F12 of the code review
    of stage 3.3b-S2b)."""
    spell_kind = uuid.UUID(next(key for key, value in tool.COMPONENT_CLASSES.items() if value[0] == ("PositionInTheWorld" if trap else "Spell")))
    delay_kind = uuid.UUID(next(key for key, value in tool.COMPONENT_CLASSES.items() if value[0] == "DelayedEvent"))
    transform = element(children=[element(tool.LOCAL_TRANSFORM_FIELD), identifier(tool.PARENT_FIELD, tool.NO_ENTITY)])
    spell = component(spell_kind, 11, [element(tool.REPLICATION_FIELD, b"\x01")])
    event = [element(tool.ENTITY_REF_FIELD, children=[identifier(tool.ID_FIELD, 200)]), element(tool.EVENT_TYPE_FIELD, b"\x02")]
    flags = [element(tool.START_ON_INIT_FIELD, b"\x00"), element(tool.START_ON_TRIGGER_FIELD, b"\x01")] if trap else []
    delayed = component(delay_kind, 12, [element(tool.REPLICATION_FIELD, b"\x07"), element(tool.DELAY_FIELD, delay),
                                         element(tool.EVENT_FIELD, children=event), *flags])
    components = [transform, spell, delayed]
    if vitals:
        # A Vitals component (slot 21) with no m_rowReference (finding F2 of the code review of stage 1.1b).
        vitals_kind = uuid.UUID(next(key for key, value in tool.COMPONENT_CLASSES.items() if value[0] == "Vitals"))
        components.append(component(vitals_kind, 21, [element(tool.REPLICATION_FIELD, b"\x15")]))
    if lifetime is not None:
        components.append(component(tool.AUTO_SPELL_CLASS, 20, [
            element(tool.SPELL_NAME_FIELD, b"Test_Spell_Projectile"), element(tool.LIFETIME_FIELD, lifetime),
            element(tool.LIFETIME_ON_HIT_FIELD, struct.pack(">f", 0.0)),
            element(tool.DEFAULT_RATE_FIELD, children=[element(tool.INITIAL_DELAY_FIELD, struct.pack(">f", 1.0)),
                                                       element(tool.COOLDOWN_FIELD, struct.pack(">f", 2.0))])]))
    root = element(kind=tool.ENTITY_TYPE, children=[identifier(tool.ID_FIELD, 100), element(tool.NAME_FIELD, b"Vortex", tool.TEXT_TYPE),
                                                    element(tool.COMPONENTS_FIELD, children=components),
                                                    element(tool.SPAWN_RADIUS_FIELD, struct.pack(">f", 50.0))])
    offense = element(kind=tool.OFFENSE_CLASS, children=[element(tool.DAMAGE_DATA_FIELD, children=[
        element(tool.DAMAGE_ENTRY_FIELD, b"GreatAxe_GravityWell_Burst")])])
    child = element(kind=tool.ENTITY_TYPE, children=[identifier(tool.ID_FIELD, 200), element(tool.NAME_FIELD, b"Burst", tool.TEXT_TYPE),
                                                     element(tool.COMPONENTS_FIELD, children=[offense])])
    return [root, child]


def run(work: Path, case: str, delay: bytes, lifetime: bytes | None = None) -> tuple[int, str]:
    sheet = FakeSheet("Spells", [{"SpellID": "Test_Spell", "SpellPrefabPath": PREFAB}])
    path = tool.slice_path(PREFAB)
    roots = slice_nodes(delay, case == "trap-slice", lifetime, case == "vitals-no-row")

    class Sources:
        used: dict = {}

        def __init__(self, *arguments) -> None:
            pass

        def paths(self, prefix: str, suffix: str):
            return ["table"] if suffix == ".datasheet" else []

        def read(self, *arguments):
            return b"made-up"

        def check_complete(self) -> None:
            pass

        def has(self, wanted: str) -> bool:
            return wanted == path

        def stream(self, wanted: str):
            return roots

    target = work / f"spell-tool-check-{case}.json"
    target.write_text(MARK, encoding="utf-8")
    with patch.object(tool, "Sources", Sources), patch.object(tool, "sheet_type", lambda _: tool.SPELL_TYPE), \
            patch.object(tool, "Sheet", lambda _: sheet), \
            patch.object(tool, "Catalog", lambda _: type("C", (), {"paths": {ASSET: path}})()), \
            patch.object(sys, "argv", ["spells.py", str(target)]), \
            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        status = tool.main()
    text = target.read_text(encoding="utf-8")
    target.unlink(missing_ok=True)
    return status, text


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__, file=sys.stderr)
        return 2
    work = Path(sys.argv[1])
    work.mkdir(parents=True, exist_ok=True)
    failures = 0
    status, text = run(work, "valid", struct.pack(">f", 3.0))
    events = json.loads(text)["slices"][0]["events"] if status == 0 and text != MARK else []
    passed = status == 0 and events == [[7, 3.0, 2, "Burst", "GreatAxe_GravityWell_Burst", False, "Vortex", True, False]]
    print(f"{'ok  ' if passed else 'FAIL'}  valid: status {status}, events {events}")
    failures += 0 if passed else 1
    # Format 2 (stage 3.3b-S2b): a slice with a PositionInTheWorld component and no Spell component is a spell entity (the
    # trap), and an event keeps its start flags (an event that starts at a trigger).
    status, text = run(work, "trap-slice", struct.pack(">f", 0.5))
    entry = json.loads(text)["slices"][0] if status == 0 and text != MARK else {}
    passed = (status == 0 and entry.get("spellSlot") == 0 and entry.get("positionSlot") == 1
              and entry.get("events") == [[7, 0.5, 2, "Burst", "GreatAxe_GravityWell_Burst", False, "Vortex", False, True]])
    print(f"{'ok  ' if passed else 'FAIL'}  trap-slice: status {status}, slots {entry.get('spellSlot')} {entry.get('positionSlot')}, "
          f"events {entry.get('events')}")
    failures += 0 if passed else 1
    for case, delay in (("bad-delay-width", b"\x00"), ("delay-not-finite", struct.pack(">f", float("nan")))):
        status, text = run(work, case, delay)
        passed = status == 1 and text == MARK
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if text == MARK else 'replaced'}")
        failures += 0 if passed else 1
    # Finding F12 of the code review of stage 3.3b-S2b: the times of an AutoSpell are required 32-bit floats.
    status, text = run(work, "autospell", struct.pack(">f", 3.0), struct.pack(">f", 15.0))
    entry = json.loads(text)["slices"][0] if status == 0 and text != MARK else {}
    passed = status == 0 and (entry.get("autoSpell") or [None, None])[1] == 15.0
    print(f"{'ok  ' if passed else 'FAIL'}  autospell: status {status}, AutoSpell {entry.get('autoSpell')}")
    failures += 0 if passed else 1
    status, text = run(work, "autospell-lifetime-width", struct.pack(">f", 3.0), b"\x01")
    passed = status == 1 and text == MARK
    print(f"{'ok  ' if passed else 'FAIL'}  autospell-lifetime-width: status {status}, file {'kept' if text == MARK else 'replaced'}")
    failures += 0 if passed else 1
    # Finding F2 of the code review of stage 1.1b: a Vitals component needs its row reference.
    status, text = run(work, "vitals-no-row", struct.pack(">f", 3.0))
    passed = status == 1 and text == MARK
    print(f"{'ok  ' if passed else 'FAIL'}  vitals-no-row: status {status}, file {'kept' if text == MARK else 'replaced'}")
    failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
