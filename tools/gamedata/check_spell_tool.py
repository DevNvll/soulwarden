#!/usr/bin/env python3
"""Check of the spell data tool with made-up slice nodes (no file of the game).

The spell data tool (spells.py) must write no file from a source that is not complete. Code review of stage 3.3b-S2a,
finding F5: a delay of a delayed event with a value of one byte (not a 32-bit float) gave the delay 0, and the burst of
the vortex then came at the start of the spell. This check gives the entry point of the tool one spell table and one
slice (a spell component, a delayed event of slot 7 to a child entity with the damage row of the burst) with a source
provider that reads no game copy, and compares the status and the output file of each case. The test program of the
backend runs it (case RG5).

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


def slice_nodes(delay: bytes) -> list[Element]:
    """@return The nodes of a slice: a root entity with a Spell component (slot 1) and a delayed event (slot 7) of `delay`."""
    spell_kind = uuid.UUID(next(key for key, value in tool.COMPONENT_CLASSES.items() if value[0] == "Spell"))
    delay_kind = uuid.UUID(next(key for key, value in tool.COMPONENT_CLASSES.items() if value[0] == "DelayedEvent"))
    transform = element(children=[element(tool.LOCAL_TRANSFORM_FIELD), identifier(tool.PARENT_FIELD, tool.NO_ENTITY)])
    spell = component(spell_kind, 11, [element(tool.REPLICATION_FIELD, b"\x01")])
    event = [element(tool.ENTITY_REF_FIELD, children=[identifier(tool.ID_FIELD, 200)]), element(tool.EVENT_TYPE_FIELD, b"\x02")]
    delayed = component(delay_kind, 12, [element(tool.REPLICATION_FIELD, b"\x07"), element(tool.DELAY_FIELD, delay),
                                         element(tool.EVENT_FIELD, children=event)])
    root = element(kind=tool.ENTITY_TYPE, children=[identifier(tool.ID_FIELD, 100), element(tool.NAME_FIELD, b"Vortex", tool.TEXT_TYPE),
                                                    element(tool.COMPONENTS_FIELD, children=[transform, spell, delayed]),
                                                    element(tool.SPAWN_RADIUS_FIELD, struct.pack(">f", 50.0))])
    offense = element(kind=tool.OFFENSE_CLASS, children=[element(tool.DAMAGE_DATA_FIELD, children=[
        element(tool.DAMAGE_ENTRY_FIELD, b"GreatAxe_GravityWell_Burst")])])
    child = element(kind=tool.ENTITY_TYPE, children=[identifier(tool.ID_FIELD, 200), element(tool.NAME_FIELD, b"Burst", tool.TEXT_TYPE),
                                                     element(tool.COMPONENTS_FIELD, children=[offense])])
    return [root, child]


def run(work: Path, case: str, delay: bytes) -> tuple[int, str]:
    sheet = FakeSheet("Spells", [{"SpellID": "Test_Spell", "SpellPrefabPath": PREFAB}])
    path = tool.slice_path(PREFAB)
    roots = slice_nodes(delay)

    class Sources:
        used: dict = {}

        def __init__(self, *arguments) -> None:
            pass

        def paths(self, *arguments):
            return ["table"]

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
    passed = status == 0 and events == [[7, 3.0, 2, "Burst", "GreatAxe_GravityWell_Burst", False]]
    print(f"{'ok  ' if passed else 'FAIL'}  valid: status {status}, events {events}")
    failures += 0 if passed else 1
    for case, delay in (("bad-delay-width", b"\x00"), ("delay-not-finite", struct.pack(">f", float("nan")))):
        status, text = run(work, case, delay)
        passed = status == 1 and text == MARK
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, file {'kept' if text == MARK else 'replaced'}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
