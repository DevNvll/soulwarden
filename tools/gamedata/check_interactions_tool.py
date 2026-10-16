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
    stay), a required achievement and a lore id with no row are in "requires"/"unresolved";
  - stage 2.5b: the station of a settlement (a PrefabSpawner child of a placement with an Assembly and OpenCraftingOption) is in
    "spawned" with its place and rotation (the placement matrix times the spawner entity), its template (the slots, the initial
    state, the Assembly key, the fee flag, "resolved" from the station database, the bound in the frame of the slice, the forge
    child), and the fixture keg (the Trigger option, the permission rule); a station key with no row of the database is
    "resolved": false; a child of a client-authoritative spawner is not in the file; a fixture with no source stops the tool;
  - the cases of the code review of stage 2.5a (--review, case IR5): a layer with no list Capitals, a named slice with an asset id of
    a wrong form, one placement id with two different placements, and two referenced achievements of one bit stop the tool; an
    equal copy of a placement does not.
  - the cases of the code review of stage 2.5b (--review-stations, cases RY6 to RY8): a named placement with no position, with a
    list as its position, or with no z (finding ST-R6), a station or a forge child whose concrete slice asset the catalog does not
    have (ST-R7), and one placement id in two spellings of its UUID at two places (ST-R8) stop the tool and keep the old file.
The test program of the backend runs it (cases IT10, IR5, RY6 to RY8).

Usage: check_interactions_tool.py [WORK_FOLDER] [--review | --review-stations]
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
# Stage 2.5b: a settlement with a station spawner, the station, its forge, and the keg fixture.
SETTLEMENT_ID = "0a0b0c0d-0000-4000-8000-000000000005"
SETTLEMENT_SLICE = "slices/test/settlement.dynamicslice"
STATION_SLICE = "slices/test/station.dynamicslice"
VISUAL_SLICE = "slices/test/forge.dynamicslice"
KEG_SLICE = "slices/test/keg.dynamicslice"
STATION_ASSET = (uuid.UUID("0a0b0c0d-0000-4000-8000-0000000000e1").bytes, 2)
VISUAL_ASSET = (uuid.UUID("0a0b0c0d-0000-4000-8000-0000000000e2").bytes, 2)
KEG_ASSET = (uuid.UUID("0a0b0c0d-0000-4000-8000-0000000000e3").bytes, 2)
SPAWNER_ENTITY, STATION_ROOT, FUNCTIONAL, STATION_BOUND, FORGE_SPAWNER = 0x2001, 0x3000, 0x3001, 0x3002, 0x3003
ASSEMBLY_COMPONENT, STATION_INTERACT = 0x6000, 0x6001
KEG_ROOT, KEG_INTERACT_ENTITY, KEG_INTERACT = 0x4000, 0x4001, 0x7000


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


def base_class(component: int, slot: int) -> Element:
    return element(field_id("BaseClass1"), children=[
        element(field_id("BaseClass1"), type_id=COMPONENT_TYPE, children=[element(ID_FIELD, component.to_bytes(8, "big"))]),
        element("m_replicationIndex", slot.to_bytes(4, "big")),
    ])


def component(class_id: uuid.UUID, identifier: int, slot: int, fields: list[Element]) -> Element:
    return element(None, type_id=class_id, children=[base_class(identifier, slot), *fields])


def spawner(asset: tuple[bytes, int], client: bool, on_init: bool) -> Element:
    return element(None, type_id=getattr(tool, "SPAWNER_CLASS", uuid.UUID(int=6)), children=[
        base_class(0x9000 + asset[0][-1], 0),
        element("m_sliceAsset", asset[0] + asset[1].to_bytes(4, "big")),
        element("m_isClientAuthoratative", b"\x01" if client else b"\x00"),
        element("m_spawnOnInitialize", b"\x01" if on_init else b"\x00"),
    ])


def interact(options: list[str], range_sq: bytes = struct.pack(">f", 25.0), capacity: bytes = struct.pack(">i", -1),
             achievement: str = "", slot: int = 2, identifier: int = INTERACT_COMPONENT, option_entity: int = PAGE_ENTITY) -> Element:
    base = base_class(identifier, slot)
    entries = element("m_interactOptionEntities", children=[element(None, children=[
        element("m_interactEntityRef", children=[entity_ref(option_entity)]),
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


def station_slice(key: str) -> list[Element]:
    """A station slice (stage 2.5b): Buildable slot 1 (Complete), Position slot 2, the Assembly, a bound (slot 4), a forge spawner."""
    return [element(None, children=[
        entity(STATION_ROOT, "Station", [transform(NO_ENTITY, (0.0, 0.0, 0.0)),
                                         component(getattr(tool, "BUILDABLE_CLASS", uuid.UUID(int=2)), 0x6100, 1, [element("m_initialState", (5).to_bytes(4, "big"))]),
                                         component(getattr(tool, "POSITION_CLASS", uuid.UUID(int=3)), 0x6101, 2, [])]),
        entity(FUNCTIONAL, "Functional", [transform(STATION_ROOT, (0.0, 0.0, 0.0)), component(getattr(tool, "ASSEMBLY_CLASS", uuid.UUID(int=4)), ASSEMBLY_COMPONENT, 0, [
            element("m_craftingStationReference", children=[text("m_craftingStationEntry", key)]),
            element("m_chargeCraftingFee", b"\x01")])]),
        entity(STATION_BOUND, "InteractBound", [transform(FUNCTIONAL, (1.0, 0.0, 0.25)),
                                                interact(["OpenCraftingOption"], slot=4, identifier=STATION_INTERACT, option_entity=FUNCTIONAL)]),
        entity(FORGE_SPAWNER, "Forge", [transform(STATION_ROOT, (0.0, 0.0, 0.0)), spawner(VISUAL_ASSET, False, True)]),
    ])]


def keg_slice() -> list[Element]:
    """The keg fixture (stage 2.5b): Ownership slot 3 (rule 4), Position slot 6, the Trigger bound (slot 10)."""
    return [element(None, children=[
        entity(KEG_ROOT, "Keg", [transform(NO_ENTITY, (0.0, 0.0, 0.0)),
                                 component(getattr(tool, "OWNERSHIP_CLASS", uuid.UUID(int=5)), 0x7100, 3, [element("m_initialPermissionRule", (4).to_bytes(4, "big"))]),
                                 component(getattr(tool, "POSITION_CLASS", uuid.UUID(int=3)), 0x7101, 6, [])]),
        entity(KEG_INTERACT_ENTITY, "Interact", [transform(KEG_ROOT, (0.0, 0.0, 0.0)),
                                                 interact(["Trigger"], capacity=struct.pack(">i", 1), slot=10, identifier=KEG_INTERACT,
                                                          option_entity=KEG_INTERACT_ENTITY)]),
    ])]


def option_row(name: str, action: int, committed: bool, delay: float = 0.0) -> Element:
    return element(None, type_id=tool.OPTION_ROW_CLASS, children=[
        text("Name", name),
        element("Ui Interact Action", children=[element("Interact Action Type", bytes([action]))]),
        element("Delayed Interaction Data", children=[element("Delay Time", struct.pack(">f", delay))]),
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
            # A settlement turned by 90 degrees about z: its spawner entity at (5, 0, 0) puts the station at (1040, 2005, 50).
            {"id": SETTLEMENT_ID, "worldPosition": {"x": 1040.0, "y": 2000.0, "z": 50.0},
             "rotation": {"x": 0.0, "y": 0.0, "z": math.sqrt(0.5), "w": math.sqrt(0.5)},
             "sliceName": "Slices/Test/Settlement.slice", "sliceAssetId": "{0A0B0C0D-0000-4000-8000-0000000000EE}:2"},
        ]
        if mode == "two-slices":
            placements.append(dict(placements[0], sliceName="Slices/Test/Other.slice",
                                   sliceAssetId="{0A0B0C0D-0000-4000-8000-0000000000DD}:2"))
        if mode == "invalid-page-asset":
            placements[0]["sliceAssetId"] = "not-an-asset-id"
        if mode == "conflicting-placement":
            placements.append(dict(placements[0], worldPosition={"x": 1500.0, "y": 2000.0, "z": 50.0}))
        if mode == "equal-placement":
            placements.append(dict(placements[0]))
        # The cases of the code review of stage 2.5b (--review-stations): the settlement is the last placement.
        if mode == "missing-placement-position":
            del placements[-1]["worldPosition"]
        if mode == "placement-position-list":
            placements[-1]["worldPosition"] = []
        if mode == "missing-placement-axis":
            placements[-1]["worldPosition"] = {"x": 1040.0, "y": 2000.0}
        if mode == "case-variant-station":
            placements.append(dict(placements[-1], id=SETTLEMENT_ID.upper(), worldPosition={"x": 1540.0, "y": 2000.0, "z": 50.0}))
        page_options = ["ReadLore", "AddLoreToJournal"] + (["NoSuchOption"] if mode == "unknown-option" else [])
        self.slices = {
            PAGE_SLICE: page_slice(mode, page_options, lore="00_No_Row" if mode == "unknown-lore" else "00_Test_Title_01"),
            PERSON_SLICE: page_slice("valid", ["OpenConversation"], lore="",
                                     achievement="Test_No_Row_Key" if mode == "unknown-achievement" else ""),
            PLAIN_SLICE: [element(None, children=[entity(ROOT, "Root", [transform(NO_ENTITY, (0.0, 0.0, 0.0))])])],
            OTHER_SLICE: page_slice("valid", ["ReadLore"]),
            SETTLEMENT_SLICE: [element(None, children=[
                entity(ROOT, "Settlement", [transform(NO_ENTITY, (0.0, 0.0, 0.0))]),
                entity(SPAWNER_ENTITY, "Public_Blacksmith", [transform(ROOT, (5.0, 0.0, 0.0)),
                                                              spawner(STATION_ASSET, mode == "spawner-client", False)]),
            ])],
            STATION_SLICE: station_slice("Carpentry2" if mode == "station-no-row" else "Blacksmith5"),
            VISUAL_SLICE: [element(None, children=[entity(0x5000, "Forge", [transform(NO_ENTITY, (0.0, 0.0, 0.0)),
                                                                           component(getattr(tool, "POSITION_CLASS", uuid.UUID(int=3)), 0x5100, 1, [])])])],
            KEG_SLICE: keg_slice(),
        }
        layer = {"Other": []} if mode == "missing-capitals" else {"Capitals": placements}
        person_achievement = "Lore_unlock_00_Test_Title_02" if mode == "duplicate-bit" else ""
        if person_achievement:
            self.slices[PERSON_SLICE] = page_slice("valid", ["OpenConversation"], lore="", achievement=person_achievement)
        self.files = {
            LAYER: json.dumps(layer).encode(),
            tool.TABLES_FOLDER + "javelindata_loreitems.datasheet": sheet("LoreItems", tool.LORE_TABLE_TYPE, [
                {"LoreID": "00_Test_Title_01", "AchievementId": "Lore_unlock_00_Test_Title_01"}]),
            tool.TABLES_FOLDER + "javelindata_achievements.datasheet": sheet("Achievements", tool.ACHIEVEMENT_TABLE_TYPE, [
                {"AchievementID": "Lore_unlock_00_Test_Title_01", "AchievementIndex": 2032},
                {"AchievementID": "Lore_unlock_00_Test_Title_02", "AchievementIndex": 2032 if mode == "duplicate-bit" else 2033}]),
            tool.CATALOG_PATH: b"made-up",
            UI_DATABASE: b"made-up",
            getattr(tool, "CRAFTING_STATIONS", "made-up/stations"): b"made-up",
        }
        for path in self.slices:
            if not (mode == "slice-missing" and path == PAGE_SLICE) and not (mode == "fixture-missing" and path == KEG_SLICE):
                self.files[path] = tool.INTERACT_BYTES + getattr(tool, "SPAWNER_CLASS", uuid.UUID(int=1)).bytes + b"made-up"
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
                                            option_row("OpenConversation", 63, True), option_row("OpenCraftingOption", 20, True),
                                            option_row("Trigger", 33, True, 5.0)])]
        if path == getattr(tool, "CRAFTING_STATIONS", None):
            # The crafting station database: one row (its name and its crafting types).
            return [element(None, children=[element(None, children=[text("Name", "Blacksmith5"),
                                                                    element("CraftingTypes", children=[text(None, "blacksmith1")])])])]
        return self.slices.get(path)

    def check_complete(self) -> None:
        if self.absent:
            raise tool.Failure(f"{len(self.absent)} file(s) of the game copy have no source (first: {self.absent[0]})")


# The cases of the code review of stage 2.5b (--review-stations, cases RY6 to RY8).
STATION_REVIEW_CASES = [
    ("valid", True, "control: a complete source with the station of the settlement"),
    ("missing-placement-position", False, "a named placement with no position (finding ST-R6)"),
    ("placement-position-list", False, "a named placement with a list as its position (finding ST-R6)"),
    ("missing-placement-axis", False, "a named placement with no z in its position (finding ST-R6)"),
    ("missing-station-asset-join", False, "a station child whose concrete slice asset the catalog does not have (finding ST-R7)"),
    ("missing-forge-asset-join", False, "a forge child whose concrete slice asset the catalog does not have (finding ST-R7)"),
    ("case-variant-station", False, "one settlement id in two spellings of its UUID at two places (finding ST-R8)"),
]
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
    ("station-no-row", True, "a station key with no row of the crafting station database: resolved false (stage 2.5b)"),
    ("spawner-client", True, "a child of a client-authoritative spawner: not a server child (stage 2.5b)"),
    ("fixture-missing", False, "a fixture slice with no source (stage 2.5b)"),
]
# The cases of the code review of stage 2.5a (--review, case IR5).
REVIEW_CASES = [
    ("valid", True, "control: a complete source"),
    ("missing-capitals", False, "a layer with no list Capitals (finding INT-R5)"),
    ("invalid-page-asset", False, "a named slice with an asset id of a wrong form (finding INT-R5)"),
    ("conflicting-placement", False, "one placement id with two placements at different places (finding INT-R6)"),
    ("equal-placement", True, "control: one placement id with two equal placements"),
    ("duplicate-bit", False, "two referenced achievements with one AchievementIndex (finding INT-R4)"),
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
            and document["counts"]["placements"] == 5 and document["counts"]["objects"] == 2
            and any("PLOT" in cause for cause in document["excluded"]) and not document["unresolved"] and check_station(document))


def check_station(document: dict, resolved: bool = True) -> bool:
    """@return True when the station of the settlement, its template, the forge child, and the keg fixture are correct (stage 2.5b)."""
    if document.get("format") != 2 or len(document.get("spawned", [])) != 1:
        return False
    child = document["spawned"][0]
    station = document["templates"][child["template"]]
    x, y, z = child["position"]
    qx, qy, qz, qw = child["rotation"]
    names = [document["options"][index]["name"] for index, _ in station["bounds"][0]["options"]]
    forge = document["templates"][station["children"][0]["template"]] if len(station["children"]) == 1 else {}
    keg = document["templates"][document["fixtures"].get("powderKeg", 0)]
    keg_names = [document["options"][index]["name"] for index, _ in keg["bounds"][0]["options"]] if keg["bounds"] else []
    trigger = next((row for row in document["options"] if row["name"] == "Trigger"), {})
    bound = station["bounds"][0]
    return (child["key"] == f"{SETTLEMENT_ID}/{SPAWNER_ENTITY:016x}" and child["parent"] == SETTLEMENT_ID
            and child["parentActor"] == tool.actor_of(SETTLEMENT_ID).hex() and child["spawner"] == "Public_Blacksmith"
            and abs(x - 1040.0) < 1e-3 and abs(y - 2005.0) < 1e-3 and abs(z - 50.0) < 1e-3
            and abs(qz - math.sqrt(0.5)) < 1e-4 and abs(qw - math.sqrt(0.5)) < 1e-4 and abs(qx) < 1e-6 and abs(qy) < 1e-6
            and station["slice"] == STATION_SLICE and station["asset"] == STATION_ASSET[0].hex() and station["kind"] == "craftingStation"
            and station["root"] == f"{STATION_ROOT:016x}" and station["slots"] == {"position": 2, "buildable": 1, "ownership": 0, "vitals": 0}
            and station["buildableState"] == 5 and station["permissionRule"] == -1
            and station["assembly"] == {"key": "Blacksmith5" if resolved else "Carpentry2", "fee": True, "entity": f"{FUNCTIONAL:016x}",
                                        "component": f"{ASSEMBLY_COMPONENT:016x}", "resolved": resolved}
            and bound["slot"] == 4 and bound["entity"] == f"{STATION_BOUND:016x}" and bound["component"] == f"{STATION_INTERACT:016x}"
            and bound["position"] == [1.0, 0.0, 0.25] and names == ["OpenCraftingOption"] and bound["options"][0][1] == f"{FUNCTIONAL:016x}"
            and "target" not in bound and forge.get("slice") == VISUAL_SLICE and forge.get("slots", {}).get("position") == 1
            and not forge.get("bounds") and station["children"][0]["position"] == [0.0, 0.0, 0.0]
            and keg["slice"] == KEG_SLICE and keg["slots"]["position"] == 6 and keg["slots"]["ownership"] == 3 and keg["permissionRule"] == 4
            and keg["bounds"][0]["slot"] == 10 and keg["bounds"][0]["capacity"] == 1 and keg_names == ["Trigger"]
            and trigger.get("delay") == 5.0 and trigger.get("committed") is True and trigger.get("action") == 33)


def main() -> int:
    review = "--review" in sys.argv[1:]
    stations = "--review-stations" in sys.argv[1:]
    folders = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
    work = Path(folders[0]) if folders else Path(tempfile.mkdtemp(prefix="soulwarden-interactions-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    tool.Sources = MadeUpSources
    every = {STATION_ASSET: STATION_SLICE, VISUAL_ASSET: VISUAL_SLICE, KEG_ASSET: KEG_SLICE}
    assets = dict(every)
    tool.Catalog = lambda data: SimpleNamespace(path=lambda asset: assets.get(asset), paths=assets)
    tool.FIXTURES = {"powderKeg": KEG_SLICE}
    failures = 0
    for mode, writes, what in STATION_REVIEW_CASES if stations else REVIEW_CASES if review else CASES:
        MadeUpSources.mode = mode
        assets.clear()
        assets.update(every)
        if mode == "missing-station-asset-join":
            del assets[STATION_ASSET]
        if mode == "missing-forge-asset-join":
            del assets[VISUAL_ASSET]
        target = work / f"interactions-tool-check-{mode}.json"
        # An old complete file at the place of the output: a refused source must keep it (finding INT-R5).
        old = '{"oldExport": true}\n'
        target.write_text(old, encoding="utf-8")
        sys.argv = ["interactions.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        written = target.exists() and target.read_text(encoding="utf-8") != old
        exists = written
        document = json.loads(target.read_text(encoding="utf-8")) if written else {}
        correct = (status == 0 and written) if writes else (status == 1 and not written)
        if correct and mode == "valid":
            # The control of --review needs only the objects of stage 2.5a: the same check runs on the tree of stage 2.5a.
            correct = len(document["objects"]) == 2 if review else check_station(document) if stations else check_valid(document)
        elif correct and mode == "unknown-option":
            bound = next(entry for entry in document["objects"] if entry["key"] == PAGE_ID)["bounds"][0]
            correct = bound.get("unknownOptions") == ["NoSuchOption"] and len(bound["options"]) == 2 and "option NoSuchOption" in document["unresolved"]
        elif correct and mode == "unknown-achievement":
            bound = next(entry for entry in document["objects"] if entry["key"] == PERSON_ID)["bounds"][0]
            correct = bound["requires"].get("unresolved") == ["Test_No_Row_Key"] and "achievement" not in bound["requires"]
        elif correct and mode == "unknown-lore":
            bound = next(entry for entry in document["objects"] if entry["key"] == PAGE_ID)["bounds"][0]
            correct = bound["lore"] == -1 and bound["requires"].get("unresolved") == ["lore 00_No_Row"]
        elif correct and mode == "station-no-row":
            correct = check_station(document, resolved=False)
        elif correct and mode == "spawner-client":
            correct = document.get("spawned") == [] and len(document["fixtures"]) == 1
        elif correct and mode == "equal-placement":
            correct = len(document["objects"]) == 2 and document["counts"]["placements"] == 6
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, " + ("a file" if exists else "no file")
              + (f" ({message[:120]})" if not writes and message else ""))
        failures += 0 if correct else 1
        target.unlink(missing_ok=True)
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
