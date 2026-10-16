#!/usr/bin/env python3
"""Check of the interaction data tool (interactions.py, stage 2.5a) with made-up sources (no file of the game).

The check makes the placements of a layer, the slices, the option rows of the user interface database, and the lore and
achievement tables in memory, gives them to the entry point of the tool with a source provider that reads no game copy, and
compares the status and the output file of each case:
  - a complete source: the file has the objects, the ids of the client (the actor id of the UUID rule, G, the facet target of
    the XOR rule, the instance id of the option entity), the place of the bound (the rotation of the placement), the option
    rows, and the bit of the lore page; a placement with no slice and a slice with no Interact component are not objects;
  - an incomplete or inconsistent source stops the tool with no file: a range of 0, a capacity of 2 bytes, a transform of 11
    numbers, a position that is not finite, one placement id with two slices, a slice with no source;
  - a name of the data with no row does not stop the tool: an option with no row is in "unknownOptions" (the other options
    stay), a required achievement and a lore id with no row are in "requires"/"unresolved".
The test program of the backend runs it (case IT10).

Usage: check_interactions_tool.py [WORK_FOLDER]
Status 0: each case has the expected result.
"""

from __future__ import annotations

import contextlib
import io
import json
import math
import struct
import sys
import tempfile
import uuid
from pathlib import Path
from types import SimpleNamespace

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import interactions as tool  # noqa: E402
from appearance_tables import field_id  # noqa: E402
from check_combat_tool import sheet  # noqa: E402
from slice_ids import COMPONENT_TYPE, COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, NAME_FIELD, Element  # noqa: E402

LAYER = tool.REGIONS_FOLDER + "r_+00_+00/capitals/test/test.capitals.json"
PAGE_SLICE = "slices/test/page.dynamicslice"
PERSON_SLICE = "slices/test/person.dynamicslice"
PLAIN_SLICE = "slices/test/plain.dynamicslice"
OTHER_SLICE = "slices/test/other.dynamicslice"
UI_DATABASE = tool.UI_DATABASE
TEXT_TYPE = uuid.UUID("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")
# A made-up placement id: its two halves XOR to eight bytes of 0x10, reversed; the low 3 bits of the first byte are 3.
PAGE_ID = "01020304-0506-0708-1112-131415161718"
PAGE_ACTOR = "13101010101010100000000000000000"
PAGE_GROUP = 0x1010101010101013
PERSON_ID = "0a0b0c0d-0000-4000-8000-000000000002"
ROOT, PAGE_ENTITY, INTERACT_COMPONENT = 0x1000, 0x1001, 0x5000
NO_ENTITY = 0xFFFFFFFF


def element(name: str | int | None, value: bytes | None = None, children: list | None = None,
            type_id: uuid.UUID | None = None) -> Element:
    node = Element()
    node.name = field_id(name) if isinstance(name, str) else name
    node.value = value
    node.children = children or []
    if type_id is not None:
        node.type_id = type_id
    return node


def text(name: str, value: str) -> Element:
    return element(name, value.encode(), type_id=TEXT_TYPE)


def entity_ref(identifier: int) -> Element:
    return element(None, children=[element(ID_FIELD, identifier.to_bytes(8, "big"))])


def transform(parent: int, translation: tuple[float, float, float], numbers: int = 12) -> Element:
    # 12 numbers: the three basis vectors (identity), then the translation (the order of the stream: columns).
    values = [1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, *translation][:numbers]
    return element(None, children=[
        element("m_parentId", children=[element(ID_FIELD, parent.to_bytes(8, "big"))]),
        element("m_worldTM", struct.pack(f">{len(values)}f", *values)),
    ])


def interact(options: list[str], range_sq: bytes = struct.pack(">f", 25.0), capacity: bytes = struct.pack(">i", -1),
             achievement: str = "") -> Element:
    base = element(field_id("BaseClass1"), children=[
        element(field_id("BaseClass1"), type_id=COMPONENT_TYPE, children=[element(ID_FIELD, INTERACT_COMPONENT.to_bytes(8, "big"))]),
        element("m_replicationIndex", (2).to_bytes(4, "big")),
    ])
    entries = element("m_interactOptionEntities", children=[element(None, children=[
        element("m_interactEntityRef", children=[entity_ref(PAGE_ENTITY)]),
        element("m_interactOptions", children=[element(None, children=[text("m_interactOption", name)]) for name in options]),
    ])])
    fields = [base, entries, element("m_numberOfInteractorsAllowed", capacity), element("m_interactRangeSq", range_sq),
              element("m_requiresLineOfSight", b"\0"), element("m_requiredInteractArc", b"\0"),
              element("m_interactArc", struct.pack(">f", 180.0))]
    if achievement:
        fields.append(text("m_requiredAchievementOverride", achievement))
    return element(None, type_id=tool.INTERACT_CLASS, children=fields)


def entity(identifier: int, name: str, components: list[Element]) -> Element:
    return element(None, type_id=ENTITY_TYPE, children=[
        element(ID_FIELD, children=[element(None, identifier.to_bytes(8, "big"))]),
        element(NAME_FIELD, name.encode()),
        element(COMPONENTS_FIELD, children=components),
    ])


def page_slice(mode: str, options: list[str], lore: str = "00_Test_Title_01", achievement: str = "") -> list[Element]:
    range_sq = struct.pack(">f", 0.0) if mode == "range-zero" else struct.pack(">f", 25.0)
    capacity = b"\xff\xff" if mode == "capacity-short" else struct.pack(">i", -1)
    numbers = 11 if mode == "transform-short" else 12
    lore_component = element(None, children=[text("m_loreId", lore)]) if lore else element(None)
    return [element(None, children=[
        entity(ROOT, "Root", [transform(NO_ENTITY, (0.0, 0.0, 0.0))]),
        entity(PAGE_ENTITY, "Interactable", [transform(ROOT, (0.0, 1.0, 0.5), numbers), lore_component,
                                             interact(options, range_sq, capacity, achievement)]),
    ])]


def option_row(name: str, action: int, committed: bool) -> Element:
    return element(None, type_id=tool.OPTION_ROW_CLASS, children=[
        text("Name", name),
        element("Ui Interact Action", children=[element("Interact Action Type", bytes([action]))]),
        element("Delayed Interaction Data", children=[element("Delay Time", struct.pack(">f", 0.0))]),
        element("Is Committed Interaction", b"\x01" if committed else b"\x00"),
        element("Hold action press time", struct.pack(">f", 0.5)),
        element("Required Level", (0).to_bytes(4, "big")),
    ])


class MadeUpSources:
    """The source provider of the tool, with made-up files. `mode` selects the fault of one source."""

    mode = "valid"

    def __init__(self, assets, wanted=None) -> None:
        self.used = {"made-up files": 6}
        mode = self.mode
        page_position = {"x": 1000.0, "y": 2000.0, "z": float("nan") if mode == "position-not-finite" else 50.0}
        placements = [
            # A page turned by 90 degrees about z: the entity at (0, 1, 0.5) of the slice is at (-1, 0, 0.5) from the placement.
            {"id": PAGE_ID, "worldPosition": page_position, "rotation": {"x": 0.0, "y": 0.0, "z": math.sqrt(0.5), "w": math.sqrt(0.5)},
             "sliceName": "Slices/Test/Page.slice", "sliceAssetId": "{0A0B0C0D-0000-4000-8000-0000000000AA}:2"},
            {"id": PERSON_ID, "worldPosition": {"x": 1010.0, "y": 2000.0, "z": 50.0}, "rotation": {"w": 1.0},
             "sliceName": "Slices/Test/Person.slice", "sliceAssetId": "{0A0B0C0D-0000-4000-8000-0000000000BB}:2"},
            {"id": "0a0b0c0d-0000-4000-8000-000000000003", "worldPosition": {"x": 1020.0, "y": 2000.0, "z": 50.0},
             "rotation": {"w": 1.0}, "sliceName": "Slices/Test/Plain.slice", "sliceAssetId": "{0A0B0C0D-0000-4000-8000-0000000000CC}:2"},
            {"id": "0a0b0c0d-0000-4000-8000-000000000004", "worldPosition": {"x": 1030.0, "y": 2000.0, "z": 50.0},
             "sliceName": "<PLOT>", "sliceAssetId": ""},
        ]
        if mode == "two-slices":
            placements.append(dict(placements[0], sliceName="Slices/Test/Other.slice",
                                   sliceAssetId="{0A0B0C0D-0000-4000-8000-0000000000DD}:2"))
        page_options = ["ReadLore", "AddLoreToJournal"] + (["NoSuchOption"] if mode == "unknown-option" else [])
        self.slices = {
            PAGE_SLICE: page_slice(mode, page_options, lore="00_No_Row" if mode == "unknown-lore" else "00_Test_Title_01"),
            PERSON_SLICE: page_slice("valid", ["OpenConversation"], lore="",
                                     achievement="Test_No_Row_Key" if mode == "unknown-achievement" else ""),
            PLAIN_SLICE: [element(None, children=[entity(ROOT, "Root", [transform(NO_ENTITY, (0.0, 0.0, 0.0))])])],
            OTHER_SLICE: page_slice("valid", ["ReadLore"]),
        }
        self.files = {
            LAYER: json.dumps({"Capitals": placements}).encode(),
            tool.TABLES_FOLDER + "javelindata_loreitems.datasheet": sheet("LoreItems", tool.LORE_TABLE_TYPE, [
                {"LoreID": "00_Test_Title_01", "AchievementId": "Lore_unlock_00_Test_Title_01"}]),
            tool.TABLES_FOLDER + "javelindata_achievements.datasheet": sheet("Achievements", tool.ACHIEVEMENT_TABLE_TYPE, [
                {"AchievementID": "Lore_unlock_00_Test_Title_01", "AchievementIndex": 2032}]),
            tool.CATALOG_PATH: b"made-up",
            UI_DATABASE: b"made-up",
        }
        for path in self.slices:
            if not (mode == "slice-missing" and path == PAGE_SLICE):
                self.files[path] = tool.INTERACT_BYTES + b"made-up"
        self.absent: list[str] = []

    def paths(self, prefix, suffix):
        return sorted(path for path in self.files if path.startswith(prefix) and path.endswith(suffix))

    def has(self, path):
        return path in self.files

    def read(self, path):
        if path not in self.files:
            self.absent.append(path)
            return None
        return self.files[path]

    def stream(self, path):
        if path == UI_DATABASE:
            return [element(None, children=[option_row("ReadLore", 47, True), option_row("AddLoreToJournal", 50, False),
                                            option_row("OpenConversation", 63, True)])]
        return self.slices.get(path)

    def check_complete(self) -> None:
        if self.absent:
            raise tool.Failure(f"{len(self.absent)} file(s) of the game copy have no source (first: {self.absent[0]})")


# Each case: the mode, True when the tool must write its file, and the text of the line.
CASES = [
    ("valid", True, "a complete source: the page and the person; no object for a plot and for a slice with no Interact"),
    ("range-zero", False, "a range of 0"),
    ("capacity-short", False, "a capacity field of 2 bytes"),
    ("transform-short", False, "a transform of 11 numbers"),
    ("position-not-finite", False, "a placement position that is not finite"),
    ("two-slices", False, "one placement id with two different slices"),
    ("slice-missing", False, "a slice of a placement with no source"),
    ("unknown-option", True, "an option name with no row: in unknownOptions, and the other options stay"),
    ("unknown-achievement", True, "a required achievement with no row: unresolved (the backend refuses the bound)"),
    ("unknown-lore", True, "a lore id with no row: unresolved, and no lore bit"),
]


def check_valid(document: dict) -> bool:
    objects = {entry["key"]: entry for entry in document["objects"]}
    page = objects.get(PAGE_ID)
    person = objects.get(PERSON_ID)
    if page is None or person is None or len(objects) != 2:
        return False
    bound = page["bounds"][0]
    k = ROOT ^ PAGE_GROUP
    target = ((PAGE_ENTITY ^ k) + INTERACT_COMPONENT) & 0xFFFFFFFFFFFFFFFF
    names = [document["options"][index]["name"] for index, _ in bound["options"]]
    entities = {entity_id for _, entity_id in bound["options"]}
    x, y, z = bound["position"]
    return (page["actor"] == PAGE_ACTOR and page["kind"] == "lorePage" and person["kind"] == "npc"
            and bound["target"] == f"{target:016x}" and entities == {f"{PAGE_ENTITY ^ k:016x}"}
            and names == ["ReadLore", "AddLoreToJournal"] and bound["slot"] == 2 and bound["capacity"] == -1
            and bound["rangeSq"] == 25.0 and abs(x - 999.0) < 1e-3 and abs(y - 2000.0) < 1e-3 and abs(z - 50.5) < 1e-3
            and document["achievements"][bound["lore"]] == ["Lore_unlock_00_Test_Title_01", 2032]
            and document["options"][names.index("AddLoreToJournal")]["committed"] is False
            and document["counts"]["placements"] == 4 and document["counts"]["objects"] == 2
            and any("PLOT" in cause for cause in document["excluded"]) and not document["unresolved"])


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="soulwarden-interactions-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    tool.Sources = MadeUpSources
    tool.Catalog = lambda data: SimpleNamespace(path=lambda asset: None)
    failures = 0
    for mode, writes, what in CASES:
        MadeUpSources.mode = mode
        target = work / f"interactions-tool-check-{mode}.json"
        target.unlink(missing_ok=True)
        sys.argv = ["interactions.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        exists = target.exists()
        document = json.loads(target.read_text(encoding="utf-8")) if exists else {}
        correct = (status == 0 and exists) if writes else (status == 1 and not exists)
        if correct and mode == "valid":
            correct = check_valid(document)
        elif correct and mode == "unknown-option":
            bound = next(entry for entry in document["objects"] if entry["key"] == PAGE_ID)["bounds"][0]
            correct = bound.get("unknownOptions") == ["NoSuchOption"] and len(bound["options"]) == 2 and "option NoSuchOption" in document["unresolved"]
        elif correct and mode == "unknown-achievement":
            bound = next(entry for entry in document["objects"] if entry["key"] == PERSON_ID)["bounds"][0]
            correct = bound["requires"].get("unresolved") == ["Test_No_Row_Key"] and "achievement" not in bound["requires"]
        elif correct and mode == "unknown-lore":
            bound = next(entry for entry in document["objects"] if entry["key"] == PAGE_ID)["bounds"][0]
            correct = bound["lore"] == -1 and bound["requires"].get("unresolved") == ["lore 00_No_Row"]
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, " + ("a file" if exists else "no file")
              + (f" ({message[:120]})" if not writes and message else ""))
        failures += 0 if correct else 1
        target.unlink(missing_ok=True)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
