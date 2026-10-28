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
  - stage 3.5b.2 (format 3): the personal loot container has its template (the kind lootDrop, the bound with TakeAllOption and
    OpenStorageOption of the Storage entity) and the values of its slice (AILoot, the LootDrop slot, the lifetime of 180 s at the init,
    the end of 1.25 s after the last item, the hide of 1.5 s, the 5 rarity particles); a loot container with no source, with no delayed
    event at the init, or with no player-unique Storage stops the tool;
  - the cases of the code review of stage 2.5a (--review, case IR5): a layer with no list Capitals, a named slice with an asset id of
    a wrong form, one placement id with two different placements, and two referenced achievements of one bit stop the tool; an
    equal copy of a placement does not.
  - the cases of the code review of stage 2.5b (--review-stations, cases RY6 to RY8): a named placement with no position, with a
    list as its position, or with no z (finding ST-R6), a station or a forge child whose concrete slice asset the catalog does not
    have (ST-R7), and one placement id in two spellings of its UUID at two places (ST-R8) stop the tool and keep the old file.
  - stage 2.5c (--persons, case NC1): the person child of a PrefabSpawner of a placement (m_isClientAuthoratative false,
    m_spawnOnInitialize true) is an object with the client id of its slot (client 0x6160B60: the place, the yaw bucket of 23 degrees of
    a yaw of 0 to 360 degrees, the low bits of the slice asset, the low three bits 7; an independent bit builder of this check), the
    targets of the XOR rule with that id, and its binding (the NPC id of the row of its variant, the presence rule of its slice); a
    yaw at a border of a bucket gives two ids; a negative yaw counts from 360; a variant with no row, a row with no NPC id, and a swap
    slice with no presence rule are bindings with a cause; a client-authoritative spawner and a spawner with m_spawnOnInitialize false
    give no person; a second slot with a taken id is not in the file; a direct person has the NPC id of its NPC component.
    Stage QG4 (also --persons): an empty variant with a slice that has its own NPC component and no variation table gets the NPC of the
    slice; the mode of a swap person is its state child (Healthy hideWhen, Depleted showWhen, another state: a cause); of two
    controllers the one of the NPC entity decides; a person below a nested spawner, of a dungeon level script, of a spawner with no init,
    and of another world is no object and is in personsApart with its cause.
The test program of the backend runs it (cases IT10, IR5, RY6 to RY8, NC1).

Usage: check_interactions_tool.py [WORK_FOLDER] [--review | --review-stations | --persons]
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
# Stage 3.5b.2: the personal loot bag (the parts of slices/instanced_loot_container: research chests-ground-loot.md Q3, K6, K7).
BAG_SLICE = "slices/test/bag.dynamicslice"
BAG_ASSET = (uuid.UUID("0a0b0c0d-0000-4000-8000-0000000000e4").bytes, 2)
BAG_ROOT, BAG_KILL_DELAY, BAG_FAST_DELAY, BAG_HIDE_DELAY, BAG_KILL_SELF, BAG_MESH, BAG_STORAGE, BAG_INTERACT_ENTITY = (
    0x8000, 0x8001, 0x8002, 0x8003, 0x8004, 0x8005, 0x8006, 0x8007)
BAG_INTERACT = 0x8100


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


def event(target: int) -> Element:
    return element(None, children=[element("m_entityRef", children=[entity_ref(target)]), element("m_type", (0).to_bytes(4, "big"))])


def delay(identifier: int, slot: int, seconds: float, on_init: bool, target: int, override: str = "") -> Element:
    return component(getattr(tool, "DELAYED_EVENT_CLASS", uuid.UUID(int=8)), identifier, slot, [
        element("m_startDelayOnInit", b"\x01" if on_init else b"\x00"), element("m_startDelayOnTrigger", b"\x01"),
        element("m_timeDelaySeconds", struct.pack(">f", seconds)), text("m_timeDelaySecondsOverrideConfig", override),
        element("m_eventData", children=[element("m_entityRef", children=[entity_ref(target)]), element("m_type", (0).to_bytes(4, "big"))])])


def bag_slice(mode: str) -> list[Element]:
    """The personal loot bag (stage 3.5b.2): InstancedLoot (AILoot, the looted and the completely looted events, 5 particles), Position
    slot 1, the delayed events of slots 2 to 4 (180 s at the init, 1.25 s and 1.5 s at a trigger), the Interact bound (slot 5) with
    TakeAllOption and OpenStorageOption of the Storage entity, LootDrop slot 6, the player-unique Storage."""
    instanced = component(getattr(tool, "INSTANCED_LOOT_CLASS", uuid.UUID(int=9)), 0x8200, 0, [
        element("m_instancedLootType", (1).to_bytes(4, "big")),
        element("m_clientFacetPtr", children=[element("m_lootedEvents", children=[event(BAG_STORAGE), event(BAG_HIDE_DELAY)]),
                                              element("m_rarityParticleEntityRef", children=[entity_ref(BAG_MESH) for _ in range(5)])]),
        element("m_serverFacetPtr", children=[element("m_completelyLootedEvents", children=[event(BAG_FAST_DELAY)])])])
    loot_drop = component(getattr(tool, "LOOT_DROP_CLASS", uuid.UUID(int=10)), 0x8201, 6, [element("m_isInstancedLoot", b"\x01")])
    storage = component(getattr(tool, "STORAGE_CLASS", uuid.UUID(int=11)), 0x8202, 0,
                        [element("m_isPlayerUniqueStorage", b"\x00" if mode == "bag-shared" else b"\x01")])
    return [element(None, children=[
        entity(BAG_ROOT, "Instanced_Loot_Container", [transform(NO_ENTITY, (0.0, 0.0, 0.0)), instanced, loot_drop,
                                                      component(getattr(tool, "POSITION_CLASS", uuid.UUID(int=3)), 0x8203, 1, [])]),
        entity(BAG_KILL_DELAY, "KillSelfDelay", [transform(BAG_ROOT, (0.0, 0.0, 0.0)),
                                                 delay(0x8300, 2, 180.0, mode != "bag-no-lifetime", BAG_KILL_SELF)]),
        entity(BAG_FAST_DELAY, "FastKillSelfDelay", [transform(BAG_ROOT, (0.0, 0.0, 0.0)), delay(0x8301, 3, 1.25, False, BAG_KILL_SELF)]),
        entity(BAG_HIDE_DELAY, "HideMeshDelay", [transform(BAG_ROOT, (0.0, 0.0, 0.0)), delay(0x8302, 4, 1.5, False, BAG_MESH)]),
        entity(BAG_KILL_SELF, "KillSelf", [transform(BAG_ROOT, (0.0, 0.0, 0.0))]),
        entity(BAG_MESH, "Mesh", [transform(BAG_ROOT, (0.0, 0.0, 0.0))]),
        entity(BAG_STORAGE, "Storage", [transform(BAG_ROOT, (0.0, 0.0, 0.0)), storage]),
        entity(BAG_INTERACT_ENTITY, "Interaction", [transform(BAG_STORAGE, (0.0, 0.0, 0.0)),
                                                    interact(["TakeAllOption", "OpenStorageOption"], slot=5, identifier=BAG_INTERACT,
                                                             option_entity=BAG_STORAGE)]),
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
            BAG_SLICE: bag_slice(mode),
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
            if not (mode == "slice-missing" and path == PAGE_SLICE) and not (mode == "fixture-missing" and path == KEG_SLICE) \
                    and not (mode == "bag-missing" and path == BAG_SLICE):
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
                                            option_row("Trigger", 33, True, 5.0), option_row("TakeAllOption", 28, True),
                                            option_row("OpenStorageOption", 26, True)])]
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
    ("bag-missing", False, "a loot container slice with no source (stage 3.5b.2)"),
    ("bag-no-lifetime", False, "a loot container with no delayed event that starts at the init (stage 3.5b.2)"),
    ("bag-shared", False, "a loot container with no player-unique Storage: not a personal bag (stage 3.5b.2)"),
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
            and any("PLOT" in cause for cause in document["excluded"]) and not document["unresolved"] and check_station(document)
            and check_bag(document))


def check_bag(document: dict) -> bool:
    """@return True when the personal loot container has its template and the values of its slice (stage 3.5b.2, format 3)."""
    container = document.get("lootContainers", {}).get("personal")
    if document.get("format") != tool.FORMAT or container is None or document["counts"].get("lootContainers") != 1:
        return False
    template = document["templates"][container["template"]]
    names = [document["options"][index]["name"] for index, _ in template["bounds"][0]["options"]] if template["bounds"] else []
    return (template["slice"] == BAG_SLICE and template["kind"] == "lootDrop" and template["slots"]["position"] == 1
            and template["root"] == f"{BAG_ROOT:016x}" and template["bounds"][0]["slot"] == 5
            and template["bounds"][0]["options"][0][1] == f"{BAG_STORAGE:016x}" and names == ["TakeAllOption", "OpenStorageOption"]
            and container["kind"] == 1 and container["lootDropSlot"] == 6 and container["lifetime"] == 180.0
            and container["emptyLifetime"] == 1.25 and container["hideDelay"] == 1.5 and container["rarityParticles"] == 5
            and [delay["slot"] for delay in container["delays"]] == [2, 3, 4])


def check_station(document: dict, resolved: bool = True) -> bool:
    """@return True when the station of the settlement, its template, the forge child, and the keg fixture are correct (stage 2.5b)."""
    if document.get("format") != tool.FORMAT or len(document.get("spawned", [])) != 1:
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


# Stage 2.5c (--persons, case NC1): the persons. The child slice of a person spawner has the path of the swap slice that is shown by
# default (its presence rule is in tool.PERSON_PRESENCE); a second child slice with a swap controller has no rule.
PERSON_CHILD_SLICE = "slices/characters/mission/sliceswaps/masternpc_swap_defaultshown.dynamicslice"
PERSON_UNKNOWN_SLICE = "slices/test/unknown_swap.dynamicslice"
PERSON_SPAWNER_SLICE = "slices/test/person_spawner.dynamicslice"
PERSON_DIRECT_SLICE = "slices/test/direct_person.dynamicslice"
PERSON_CHILD_ASSET = (uuid.UUID("2b0c0d0e-0000-4000-8000-0000000000f1").bytes, 2)
PERSON_UNKNOWN_ASSET = (uuid.UUID("2b0c0d0e-0000-4000-8000-0000000000f2").bytes, 2)
PERSON_ROOT = 0x0000071100000AB0
PERSON_INTERACT_ENTITY = 0x0000071300000AB0
PERSON_INTERACT = 0x3518710D72990001
PERSON_SPAWNER_ENTITY = 0x5000000000000A01
PERSON_STATE_ENTITY = 0x0000071200000AB0
PERSON_PLACE = (2000.3, 3000.6, 40.27)
# Stage QG4: a direct NPC template (an NPC component, no variation table), a swap slice with the state 8 (shown with the bit), a slice with
# two swap controllers (the cage on the root, the person on the NPC entity), a holder slice with a nested person spawner, a level script,
# and a placement of another world.
PERSON_TEMPLATE_SLICE = "slices/test/npc_template.dynamicslice"
PERSON_HIDDEN_SLICE = "slices/test/swap_hidden.dynamicslice"
PERSON_CAGE_SLICE = "slices/test/person_cage.dynamicslice"
PERSON_HOLDER_SLICE = "slices/test/person_holder.dynamicslice"
PERSON_SCRIPT_SLICE = "slices/dungeon/levelscripts/test_script.dynamicslice"
PERSON_TEMPLATE_ASSET = (uuid.UUID("2b0c0d0e-0000-4000-8000-0000000000f3").bytes, 2)
PERSON_HIDDEN_ASSET = (uuid.UUID("2b0c0d0e-0000-4000-8000-0000000000f4").bytes, 2)
PERSON_CAGE_ASSET = (uuid.UUID("2b0c0d0e-0000-4000-8000-0000000000f5").bytes, 2)
PERSON_HOLDER_ASSET = (uuid.UUID("2b0c0d0e-0000-4000-8000-0000000000f6").bytes, 2)
OTHER_WORLD_LAYER = tool.WORLDS_FOLDER + "test_dungeon/regions/r_+00_+00/capitals/test/test.capitals.json"


def single(value: float) -> float:
    """@return The value as an IEEE 32-bit float (the client keeps the place and the yaw in floats)."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def independent_group(x: float, y: float, z: float, yaw: float, asset: bytes) -> int:
    """The client id of a person child, from its fields at their bit places (low to high): 3 bits 7, 18 bits x * 8, 18 bits y * 8, 12
    bits z * 4, 4 bits of the yaw bucket, 9 bits of the slice asset. Each value is a 32-bit float of the client before its product and
    roundf (code review of stage 2.5c, finding PR5C-R6)."""
    def nearest(value: float) -> int:
        return int(math.copysign(math.floor(abs(value) + 0.5), value))
    turn = single(yaw) + 360.0 if single(yaw) < 0 else single(yaw)
    fields = [(7, 3), (nearest(single(x) * 8) % (1 << 18), 18), (nearest(single(y) * 8) % (1 << 18), 18),
              (nearest(single(z) * 4) % (1 << 12), 12), ((int(turn) % 360) // 23, 4), (int.from_bytes(asset[:8], "little") % (1 << 9), 9)]
    value = 0
    offset = 0
    for field, width in fields:
        value |= field << offset
        offset += width
    return value


def swap_controller(swap: bool, state: int | None, achievement: str = "DevWorld_Default_Swap") -> Element:
    """A GatherableController with the achievement swap (stage QG4: m_achievementServerState in its server facet)."""
    children = [element("m_enableUseAchievementForClientState", b"\x01" if swap else b"\x00"), text("m_achievementName", achievement)]
    if state is not None:
        children.insert(0, element("BaseClass1", children=[element("m_serverFacetPtr", children=[
            element("m_achievementServerState", bytes([state]))])]))
    return element(None, children=children)


def person_child_slice(swap: bool = True, npc_key: str = "0317_Generic", state: int | None = 4, variation_table: bool = True,
                       npc_controller: int | None = None, child: str | None = None) -> list[Element]:
    """A person child (stage 2.5c): the root with the NPC component (a generic key), the variation component of the table NPC with the
    columns NPCId and SwapAchievementId, the GatherableController with the achievement swap, and the Interact entity (slot 5). Stage QG4:
    `state` the m_achievementServerState of the controller (4 in masternpc_swap_defaultshown), no variation table (a direct NPC
    template), a second controller on the NPC entity with the state `npc_controller` (a cage and its person), and the state child above the
    person entity (`child`; default: the state of the byte, Healthy for 4 and Depleted for 8; no child with no swap)."""
    variation = element(None, children=[text("m_variationTableUniqueName", "NPC"),
                                        element("m_variantData", children=[element(None, children=[text("m_colName", "NPCId")]),
                                                                           element(None, children=[text("m_colName", "SwapAchievementId")])])])
    root = [transform(NO_ENTITY, (0.0, 0.0, 0.0))] + ([variation] if variation_table else []) + ([swap_controller(swap, state)] if swap else [])
    state_child = child if child is not None else {4: "Healthy", 8: "Depleted", 6: "Locked"}.get(state or 0, "") if swap else ""
    person = [transform(PERSON_STATE_ENTITY if state_child else PERSON_ROOT, (0.0, 0.0, 1.0)),
              element(None, type_id=tool.NPC_CLASS, children=[text("m_npcKey", npc_key)]),
              interact(["OpenConversation"], slot=5, identifier=PERSON_INTERACT, option_entity=PERSON_INTERACT_ENTITY)]
    if npc_controller is not None:
        person.append(swap_controller(True, npc_controller, "Test_Person_Controller"))
    entities = [entity(PERSON_ROOT, "MasterNpc", root)]
    if state_child:
        # The state child of the controller (the client shows the entities below the child of the state: static read Q1).
        entities.append(entity(PERSON_STATE_ENTITY, state_child, [transform(PERSON_ROOT, (0.0, 0.0, 0.0))]))
    entities.append(entity(PERSON_INTERACT_ENTITY, "Interact", person))
    return [element(None, children=entities)]


def person_spawner_slice(asset: tuple[bytes, int], variant: str, client: bool, on_init: bool) -> list[Element]:
    component = spawner(asset, client, on_init)
    component.children.append(text("m_sliceVariant", variant))
    return [element(None, children=[entity(PERSON_SPAWNER_ENTITY, "NPC_Test_Person", [transform(NO_ENTITY, (0.0, 0.0, 0.0)), component])])]


class PersonSources(MadeUpSources):
    """The made-up sources of the person cases: the sources of the valid case with the person placements, slices, and variation table."""

    person_mode = "person-valid"

    def __init__(self, assets, wanted=None) -> None:
        MadeUpSources.mode = "valid"
        super().__init__(assets, wanted)
        mode = self.person_mode
        yaw = {"person-yaw-negative": -107.43, "person-yaw-border": 0.0}.get(mode, 140.0)
        half = math.radians(yaw) / 2
        rotation = {"x": 0.0, "y": 0.0, "z": math.sin(half), "w": math.cos(half)}
        place = dict(zip("xyz", PERSON_PLACE))
        placements = [{"id": "2b0c0d0e-0000-4000-8000-000000000001", "worldPosition": place, "rotation": rotation,
                       "sliceName": "Slices/Test/Person_Spawner.slice", "sliceAssetId": "{2B0C0D0E-0000-4000-8000-0000000000A1}:2"},
                      {"id": "2b0c0d0e-0000-4000-8000-000000000002", "worldPosition": {"x": 2010.0, "y": 3000.0, "z": 40.0},
                       "rotation": {"w": 1.0}, "sliceName": "Slices/Test/Direct_Person.slice",
                       "sliceAssetId": "{2B0C0D0E-0000-4000-8000-0000000000A2}:2"}]
        if mode == "person-taken-id":
            placements.append(dict(placements[0], id="2b0c0d0e-0000-4000-8000-000000000003"))
        variant = {"person-no-row": "Test_No_Row", "person-no-npc": "Test_No_Npc", "person-template": ""}.get(mode, "Test_Person")
        child = {"person-swap-unknown": PERSON_UNKNOWN_ASSET, "person-template": PERSON_TEMPLATE_ASSET, "person-swap-hidden": PERSON_HIDDEN_ASSET,
                 "person-two-controllers": PERSON_CAGE_ASSET, "person-nested": PERSON_HOLDER_ASSET}.get(mode, PERSON_CHILD_ASSET)
        self.slices[PERSON_SPAWNER_SLICE] = person_spawner_slice(child, variant, mode == "person-client", mode != "person-not-init")
        self.slices[PERSON_CHILD_SLICE] = person_child_slice()
        self.slices[PERSON_UNKNOWN_SLICE] = person_child_slice(state=6)
        self.slices[PERSON_DIRECT_SLICE] = person_child_slice(swap=False, npc_key="Test_Direct_Npc")
        self.slices[PERSON_TEMPLATE_SLICE] = person_child_slice(swap=False, npc_key="Test_Template_Npc", variation_table=False)
        self.slices[PERSON_HIDDEN_SLICE] = person_child_slice(state=8)
        self.slices[PERSON_CAGE_SLICE] = person_child_slice(state=4, npc_controller=8, child="Depleted")
        # A holder slice with no Interact: its spawner (at the init) makes the person child below it (a nested PrefabSpawner).
        holder = spawner(PERSON_CHILD_ASSET, False, True)
        holder.children.append(text("m_sliceVariant", "Test_Person"))
        self.slices[PERSON_HOLDER_SLICE] = [element(None, children=[entity(0x5000000000000B01, "Holder",
                                                                           [transform(NO_ENTITY, (0.0, 0.0, 0.0)), holder])])]
        self.slices[PERSON_SCRIPT_SLICE] = person_spawner_slice(PERSON_CHILD_ASSET, "Test_Person", False, False)
        for path in (PERSON_SPAWNER_SLICE, PERSON_CHILD_SLICE, PERSON_UNKNOWN_SLICE, PERSON_DIRECT_SLICE, PERSON_TEMPLATE_SLICE,
                     PERSON_HIDDEN_SLICE, PERSON_CAGE_SLICE, PERSON_HOLDER_SLICE, PERSON_SCRIPT_SLICE):
            self.files[path] = tool.INTERACT_BYTES + tool.SPAWNER_CLASS.bytes + b"made-up"
        if mode == "person-level-script":
            placements.append({"id": "2b0c0d0e-0000-4000-8000-000000000004", "worldPosition": {"x": 2020.0, "y": 3000.0, "z": 40.0},
                               "rotation": {"w": 1.0}, "sliceName": "Slices/Dungeon/LevelScripts/Test_Script.slice",
                               "sliceAssetId": "{2B0C0D0E-0000-4000-8000-0000000000A3}:2"})
        if mode == "person-other-world":
            self.files[OTHER_WORLD_LAYER] = json.dumps({"Capitals": [{
                "id": "2b0c0d0e-0000-4000-8000-000000000005", "worldPosition": {"x": 10.0, "y": 20.0, "z": 30.0}, "rotation": {"w": 1.0},
                "sliceName": "Slices/Test/Direct_Person.slice", "sliceAssetId": "{2B0C0D0E-0000-4000-8000-0000000000A2}:2"}]}).encode()
        self.files[LAYER] = json.dumps({"Capitals": placements}).encode()
        self.files[tool.TABLES_FOLDER + "javelindata_variations_npcs.datasheet"] = sheet("NPC", tool.VARIATION_TABLE_TYPE, [
            {"VariantID": "Test_Person", "NPCId": "Test_Person_Npc", "SwapAchievementId": "Test_Swap"},
            {"VariantID": "Test_No_Npc", "NPCId": "", "SwapAchievementId": "Test_Swap"}])


# Each case: the mode, and the text of the line. Each case writes a file.
PERSON_CASES = [
    ("person-valid", "a person child: the client id of its slot, the XOR targets, the NPC id of its variant row, hideWhen Test_Swap"),
    ("person-yaw-negative", "a yaw of -107.43 degrees counts from 360 (252: bucket 10; live check L1)"),
    ("person-yaw-border", "a yaw of 0 degrees (a border of a bucket): the slot has two ids (two objects)"),
    ("person-no-row", "a variant with no row: a binding with a cause (the backend refuses its use)"),
    ("person-no-npc", "a row with no NPCId: a binding with a cause"),
    ("person-swap-unknown", "a child slice with an achievement swap and no presence rule: a binding with a cause"),
    ("person-client", "a client-authoritative spawner: no person"),
    ("person-not-init", "a spawner with m_spawnOnInitialize false: no person (the client does not make it)"),
    ("person-taken-id", "two slots with one client id: the second slot is not in the file"),
    ("person-template", "stage QG4: an empty variant and a slice with its own NPC component and no variation table: the NPC of the slice"),
    ("person-swap-hidden", "stage QG4: a person below the state child Depleted (shown with a true conditional): showWhen with the row value"),
    ("person-two-controllers", "stage QG4: two swap controllers: the controller of the NPC entity and its state child Depleted decide"),
    ("person-nested", "stage QG4: a person below a nested PrefabSpawner: no object, in personsApart (nested)"),
    ("person-level-script", "stage QG4: a person of a dungeon level script: in personsApart (levelScript)"),
    ("person-other-world", "stage QG4: a person of a placement of another world: in personsApart (otherWorld, its world)"),
]


def check_person(mode: str, document: dict) -> bool:
    objects = [entry for entry in document["objects"] if entry.get("person", {}).get("clientChild")]
    direct = [entry for entry in document["objects"] if entry.get("person", {}).get("direct")]
    if len(direct) != 1 or direct[0]["person"].get("npc") != "Test_Direct_Npc" or document.get("format") != tool.FORMAT:
        return False
    apart = document.get("personsApart", [])
    if mode in ("person-client", "person-not-init"):
        cause = tool.CAUSE_PERSON_NO_INIT if mode == "person-not-init" else tool.CAUSE_SPAWNER
        events = [entry for entry in apart if entry["cause"] == "event" and entry["npc"] == "Test_Person_Npc"]
        return not objects and document["excluded"].get(cause, 0) >= 1 and len(events) == (1 if mode == "person-not-init" else 0)
    if mode == "person-nested":
        return (not objects and [(entry["npc"], entry["cause"], entry["slice"]) for entry in apart]
                == [("Test_Person_Npc", "nested", PERSON_CHILD_SLICE)] and apart[0]["via"] == [PERSON_SPAWNER_SLICE, PERSON_HOLDER_SLICE])
    if mode == "person-level-script":
        return [(entry["npc"], entry["cause"]) for entry in apart] == [("Test_Person_Npc", "levelScript")] and len(objects) == 1
    if mode == "person-other-world":
        return ([(entry["npc"], entry["cause"], entry["world"]) for entry in apart] == [("Test_Direct_Npc", "otherWorld", "test_dungeon")]
                and len(objects) == 1 and document["counts"]["otherWorlds"] == 1)
    if mode in ("person-template", "person-swap-hidden", "person-two-controllers"):
        binding = objects[0]["person"] if len(objects) == 1 else {}
        expected = {"person-template": {"npc": "Test_Template_Npc", "variant": "", "table": "", "template": True},
                    "person-swap-hidden": {"npc": "Test_Person_Npc", "variant": "Test_Person", "table": "NPC", "row": 0,
                                           "presence": {"mode": "showWhen", "condition": "Test_Swap"}},
                    "person-two-controllers": {"npc": "Test_Person_Npc", "variant": "Test_Person", "table": "NPC", "row": 0,
                                               "presence": {"mode": "showWhen", "condition": "Test_Swap"}}}[mode]
        expected.update({"clientChild": True, "spawner": "2b0c0d0e-0000-4000-8000-000000000001"})
        return binding == expected and document["counts"]["personsFromTemplate"] == (1 if mode == "person-template" else 0) and not apart
    if mode == "person-taken-id":
        return len(objects) == 1 and document["excluded"].get(tool.CAUSE_PERSON_ID_TAKEN) == 1 and document["counts"]["persons"] == 2
    asset = (PERSON_UNKNOWN_ASSET if mode == "person-swap-unknown" else PERSON_CHILD_ASSET)[0]
    yaw = {"person-yaw-negative": -107.43, "person-yaw-border": 0.0}.get(mode, 140.0)
    expected = independent_group(*PERSON_PLACE, yaw, asset)
    if mode == "person-yaw-border":
        other = independent_group(*PERSON_PLACE, -0.01, asset)
        return (len(objects) == 2 and {entry["actor"][:16] for entry in objects} == {expected.to_bytes(8, "little").hex(),
                                                                                     other.to_bytes(8, "little").hex()}
                and objects[1]["key"].endswith("~1") and document["counts"]["personSlotsWithTwoIds"] == 1)
    if len(objects) != 1:
        return False
    person = objects[0]
    binding = person["person"]
    if mode == "person-yaw-negative":
        return person["actor"] == expected.to_bytes(8, "little").hex() + "00" * 8 and (expected >> 51) & 0xF == 10
    if mode in ("person-no-row", "person-no-npc", "person-swap-unknown"):
        return bool(binding.get("unresolved")) and document["counts"]["personsUnresolved"] == 1
    bound = person["bounds"][0]
    k = PERSON_ROOT ^ expected
    target = ((PERSON_INTERACT_ENTITY ^ k) + PERSON_INTERACT) & 0xFFFFFFFFFFFFFFFF
    return (person["key"] == f"2b0c0d0e-0000-4000-8000-000000000001/{PERSON_SPAWNER_ENTITY:016x}" and person["kind"] == "npc"
            and person["slice"] == PERSON_CHILD_SLICE and person["asset"] == PERSON_CHILD_ASSET[0].hex() and person["sub"] == 2
            and person["actor"] == expected.to_bytes(8, "little").hex() + "00" * 8 and bound["target"] == f"{target:016x}"
            and bound["slot"] == 5 and bound["options"][0][1] == f"{PERSON_INTERACT_ENTITY ^ k:016x}"
            and binding == {"npc": "Test_Person_Npc", "variant": "Test_Person", "table": "NPC", "row": 0,
                            "presence": {"mode": "hideWhen", "condition": "Test_Swap"}, "clientChild": True,
                            "spawner": "2b0c0d0e-0000-4000-8000-000000000001"}
            and document["counts"]["persons"] == 1 and document["counts"]["personsDirect"] == 1)


def run_person_cases(work: Path) -> int:
    tool.Sources = PersonSources
    assets = {STATION_ASSET: STATION_SLICE, VISUAL_ASSET: VISUAL_SLICE, KEG_ASSET: KEG_SLICE, BAG_ASSET: BAG_SLICE,
              PERSON_CHILD_ASSET: PERSON_CHILD_SLICE, PERSON_UNKNOWN_ASSET: PERSON_UNKNOWN_SLICE, PERSON_TEMPLATE_ASSET: PERSON_TEMPLATE_SLICE,
              PERSON_HIDDEN_ASSET: PERSON_HIDDEN_SLICE, PERSON_CAGE_ASSET: PERSON_CAGE_SLICE, PERSON_HOLDER_ASSET: PERSON_HOLDER_SLICE}
    tool.Catalog = lambda data: SimpleNamespace(path=lambda asset: assets.get(asset), paths=assets)
    failures = 0
    # The rule itself: the tool function against the independent bit builder.
    vectors = [(2000.3, 3000.6, 40.27, 140.0), (100.06, 7.94, 0.13, -107.43), (16383.9, 16383.9, 999.9, 359.9), (0.0, 0.0, 0.0, 0.0)]
    rule = all(tool.child_group(*vector, PERSON_CHILD_ASSET[0]) == independent_group(*vector, PERSON_CHILD_ASSET[0]) for vector in vectors)
    print(f"{'ok  ' if rule else 'FAIL'} the client id rule of the tool (child_group) equals the independent bit builder ({len(vectors)} "
          f"vectors: a positive and a negative yaw, the upper ends, 0)")
    failures += 0 if rule else 1
    # Code review of stage 2.5c, finding PR5C-R6 (the probes of the review, made-up places): two axes at their roundf borders at one time
    # (each combination of the roundings), and a place whose float is above the border while its double is below it (10000.0623 is the
    # float 10000.0625: 80000.5 rounds to 80001).
    asset = PERSON_CHILD_ASSET[0]
    two = (100.0624999, 200.0624999, 30.0, 50.0)
    spacing = (10000.0623, 2000.0, 30.0, 50.0)
    border = (independent_group(*two, asset) in tool.child_groups(*two, asset) and len(tool.child_groups(*two, asset)) == 4
              and tool.child_groups(*spacing, asset)[0] == independent_group(*spacing, asset)
              and tool.child_groups(*PERSON_PLACE, 140.0, asset) == [independent_group(*PERSON_PLACE, 140.0, asset)])
    print(f"{'ok  ' if border else 'FAIL'} the ids of a slot at the borders of the rule: two axes at their borders give 4 ids with the id of the "
          f"client; the float of a place above the border gives the id of the client first; a place far from a border has one id")
    failures += 0 if border else 1
    for mode, what in PERSON_CASES:
        PersonSources.person_mode = mode
        target = work / f"interactions-tool-check-{mode}.json"
        target.unlink(missing_ok=True)
        sys.argv = ["interactions.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        document = json.loads(target.read_text(encoding="utf-8")) if target.exists() else {}
        correct = status == 0 and bool(document) and check_person(mode, document)
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, " + ("a file" if document else f"no file ({message[:120]})"))
        failures += 0 if correct else 1
        target.unlink(missing_ok=True)
    return failures

def main() -> int:
    review = "--review" in sys.argv[1:]
    stations = "--review-stations" in sys.argv[1:]
    persons = "--persons" in sys.argv[1:]
    folders = [argument for argument in sys.argv[1:] if not argument.startswith("--")]
    work = Path(folders[0]) if folders else Path(tempfile.mkdtemp(prefix="soulwarden-interactions-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    if persons:
        tool.FIXTURES = {"powderKeg": KEG_SLICE}
        tool.LOOT_CONTAINERS = {"personal": BAG_SLICE}
        return 0 if run_person_cases(work) == 0 else 1
    tool.Sources = MadeUpSources
    every = {STATION_ASSET: STATION_SLICE, VISUAL_ASSET: VISUAL_SLICE, KEG_ASSET: KEG_SLICE, BAG_ASSET: BAG_SLICE}
    assets = dict(every)
    tool.Catalog = lambda data: SimpleNamespace(path=lambda asset: assets.get(asset), paths=assets)
    tool.FIXTURES = {"powderKeg": KEG_SLICE}
    tool.LOOT_CONTAINERS = {"personal": BAG_SLICE}
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
    if not review and not stations:
        failures += 0 if check_station_overrides() else 1
    return 0 if failures == 0 else 1


def check_station_overrides() -> bool:
    """Decision D1 of stage 3.7a (tool.station_overrides): a master station slice with a key with no row gets its _t2 variant (key ending
    in 3), a different slice with a key with no row keeps its template, and a variant with a different key stops the tool."""
    def templates() -> list[dict]:
        return [{"slice": f"{tool.STATION_FOLDER}{tool.STATION_PREFIX}carpentry_master.dynamicslice",
                 "assembly": {"key": "Carpentry2", "resolved": False}},
                {"slice": STATION_SLICE, "assembly": {"key": "Carpentry2", "resolved": False}},
                {"slice": f"{tool.STATION_FOLDER}{tool.STATION_PREFIX}engineering_master.dynamicslice",
                 "assembly": {"key": "Engineering2", "resolved": True}}]
    variant = f"{tool.STATION_FOLDER}{tool.STATION_PREFIX}carpentry_t2.dynamicslice"

    def run(variant_key: str):
        listed = templates()
        spawned = [{"key": "a", "template": 0}, {"key": "b", "template": 1}, {"key": "c", "template": 2}]

        def load(path: str) -> int:
            listed.append({"slice": path, "assembly": {"key": variant_key, "resolved": True}})
            return len(listed) - 1
        counts = tool.station_overrides(spawned, listed, lambda path: path == variant, load)
        return counts, spawned
    (overrides, missing), spawned = run("Carpentry3")
    correct = (overrides == 1 and missing == 1 and spawned[0]["template"] == 3 and spawned[0]["authoredTemplate"] == 0
               and spawned[0]["override"] == {"rule": "D1", "from": "Carpentry2", "to": "Carpentry3", "slice": variant}
               and spawned[1] == {"key": "b", "template": 1} and spawned[2] == {"key": "c", "template": 2})
    try:
        run("Carpentry4")
        stopped = False
    except tool.Failure:
        stopped = True
    ok = correct and stopped
    print(f"{'ok  ' if ok else 'FAIL'} decision D1 (stage 3.7a): the master Carpentry2 gets its _t2 variant Carpentry3 (authoredTemplate, "
          f"override), a different slice with a key with no row keeps its template, a resolved key stays; a variant Carpentry4 stops the tool")
    return ok


if __name__ == "__main__":
    sys.exit(main())
