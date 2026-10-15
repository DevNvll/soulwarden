#!/usr/bin/env python3
"""Write the spell entity data of the game as one JSON file (stage 3.3b-S2a).

The backend reads this file at its start (protocol notes 4.25). For each spell row of the spell tables of the game with
a prefab slice, the file has what the backend needs to make the spell as an entity of the world and to send its record:
the slice of the spell (the asset id and the sub id of the asset catalog), the root entity and the faceted components
of the slice (the facet targets), the slot of each replicated component (the Spell state, type 2912; Ownership, 3217;
TemporaryAffiliation, 3563; each DelayedEvent, 3139), the interest radius of the slice (m_sliceSpawnRadius), and the
delayed events of the slice: for each one its slot, its delay, its event type, its target entity, the damage row of a
MomentaryOffenseComponent of the target (the burst of Gravity Well), and whether the target destroys the entity group
(DestroyGDEComponent: the end of the spell).

The chain of one spell:

  1. A row of a spell table (SpellData) has the column SpellPrefabPath ("Items/Weapons/GreatAxe_GravityWell_Vortex").
  2. The slice is slices/<path in lower case>.dynamicslice. The asset catalog (assetcatalog.catalog) gives its asset id;
     a slice has the sub id 2.
  3. The slice has the entities and the components. A component with a replicated state has the field
     m_replicationIndex: its slot in a state record. A DelayedEventComponent has m_timeDelaySeconds and m_eventData
     (the target entity, m_entityRef, and the event type, m_type).

The event types (m_type of m_eventData; the meaning comes from the targets of the vortex of Gravity Well and from the
live check L4 of stage 3.3b-S2a): 0 trigger (the Destroy entity), 2 activate (the Burst entity, inactive at the start),
3 deactivate (the effects of the vortex, the root, the detection volume). The tool writes the number.

A spell row whose prefab names no slice of the game copy (a path that the catalog or the pak files do not have) is left
out, with a count. The tool stops with status 1 and writes no file when a file that it needs has no source, when a
spell table has no column SpellID or SpellPrefabPath, and when a slice of a spell has no root entity, no Spell
component, two components with one slot, or a delayed event with no delay or no target.

A prefab slice with no Spell component (a waypoint or a rally point of an encounter) is not a spell entity: the tool
leaves it out with a count.

Output: {"format": 1, "counts": {"spells": N, "slices": N, "spellsWithNoSlice": N, "slicesWithNoSpellComponent": N},
"slices": [{"path": text, "asset": "UUID", "sub": N, "root": "16 hex digits", "interestRadius": meters,
            "spellSlot": N, "slots": [[slot, class name, state type], ...],
            "events": [[slot, delay seconds, event type, target name, offense row, destroys], ...],
            "components": [["entity", "component", "name/class"], ...]}, ...],
"spells": [[id, slice index], ...]}

The tool needs only the standard library of Python and the modules of this folder (creature_data.py, slice_ids.py,
appearance_tables.py, item_catalog.py, abilities.py).

Usage: spells.py [OUTPUT.json]
       spells.py --find TEXT...     print the spells whose id has a text, with their slice (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/spell-data.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see creature_data.py)
"""

from __future__ import annotations

import json
import math
import os
import struct
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import CATALOG_PATH, TABLES_FOLDER, Catalog, Sources, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import (COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, LOCAL_TRANSFORM_FIELD, NAME_FIELD, NO_ENTITY,  # noqa: E402
                       PARENT_FIELD, component_id, find_field, id_value)

OUTPUT_NAME = "spell-data.json"
SPELL_TYPE = "SpellData"
WANTED = (TABLES_FOLDER, "slices/", CATALOG_PATH)
SUB_ID = 2
TEXT_TYPE = uuid.UUID("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")
SLICE_COMPONENT_TYPE = uuid.UUID("afd304e4-1773-47c8-855a-8b622398934f")

# The component classes of a spell slice (the class id of the slice, the name, and the state type of the supported client
# build: research abilities-shared.md Q2 and K5; constructors 0x67185C0, 0x360B3C0, 0x405C370, 0x3066370). A class that
# is not here has the type 0.
COMPONENT_CLASSES = {
    "2412ac34-f256-4124-9035-6c73124dcfb6": ("Spell", 2912),
    "2a333c23-a549-bc11-1a9a-ae905170f2f6": ("Ownership", 3217),
    "171fa882-c8af-41df-bd19-09cbbfeb1c1c": ("TemporaryAffiliation", 3563),
    "d353f7e8-4266-f749-19a1-aa2c3841fbca": ("DelayedEvent", 3139),
    "7c84dee6-f1df-4b85-8cd5-6bfb970b70d0": ("DetectionVolumeEvent", 0),
    "0c848755-1f41-4408-b2be-0a3627674f50": ("Vitals", 15),
    "1bd9d601-9de8-4a8b-b115-bb534eb6020c": ("DamageReceiver", 1528),
    "5c822d39-e3ca-73f0-7ab5-922457e9cc9c": ("StatusEffects", 4236),
    "98021a39-d35a-423d-bdcd-86aac61c6207": ("Marker", 5399),
}
DESTROY_CLASS = uuid.UUID("28e4c1a4-b9b4-8675-59e8-a21527d9da65")
OFFENSE_CLASS = uuid.UUID("7c7256b3-cad2-4782-a5fd-7edfe8d9e311")
REPLICATION_FIELD = field_id("m_replicationIndex")
FACET_FIELDS = (field_id("m_clientFacetPtr"), field_id("m_serverFacetPtr"))
SPAWN_RADIUS_FIELD = field_id("m_sliceSpawnRadius")
DELAY_FIELD = field_id("m_timeDelaySeconds")
EVENT_FIELD = field_id("m_eventData")
ENTITY_REF_FIELD = field_id("m_entityRef")
EVENT_TYPE_FIELD = field_id("m_type")
DAMAGE_DATA_FIELD = field_id("m_damageData")
DAMAGE_ENTRY_FIELD = field_id("m_damageDataEntry")


def text_of(element) -> str:
    return element.value.decode("utf-8", "replace") if element is not None and element.value is not None else ""


def number_of(element) -> int:
    return int.from_bytes(element.value, "big") if element is not None and element.value else 0


def real_of(element) -> float:
    if element is None or element.value is None or len(element.value) != 4:
        return 0.0
    return struct.unpack(">f", element.value)[0]


def required_real(element, what: str) -> float:
    """@return The 32-bit float of a field that the tool needs (the delay of an event). A field with a different size, or
    a value that is not finite, stops the tool (code review of stage 3.3b-S2a, finding F5): a delay of 0 would move the
    burst of a spell to its start."""
    if element is None or element.value is None or len(element.value) != 4:
        size = "no value" if element is None or element.value is None else f"{len(element.value)} bytes"
        raise Failure(f"{what} is not a 32-bit float ({size})")
    value = struct.unpack(">f", element.value)[0]
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return value


def deep(element, wanted: int):
    """@return The first element below `element` (not itself) with this field name."""
    for child in walk(element):
        if child is not element and child.name == wanted:
            return child
    return None


def read_spell_slice(path: str, asset: tuple[bytes, int], roots: list) -> dict:
    """@return The data of one spell slice: its root, its slots, its delayed events, and its faceted components."""
    entities: dict[int, dict] = {}
    slots: list[list] = []
    components: list[list] = []
    events: list[list] = []
    radius = 0.0
    for root in roots:
        for element in walk(root):
            if element.name == SPAWN_RADIUS_FIELD and radius == 0.0:
                radius = real_of(element)
            if element.type_id != ENTITY_TYPE:
                continue
            listed = next((child for child in element.children if child.name == COMPONENTS_FIELD), None)
            entity_id = next((id_value(child) for child in element.children if child.name == ID_FIELD), None)
            name = next((text_of(child) for child in element.children if child.name == NAME_FIELD and child.type_id == TEXT_TYPE), "")
            # The outer entity of the file has the slice component: it is not an entity of an instance.
            if listed is None or entity_id is None or any(c.type_id == SLICE_COMPONENT_TYPE for c in listed.children):
                continue
            entry = {"name": name, "parent": None, "destroys": False, "offense": ""}
            for component in listed.children:
                if find_field(component, LOCAL_TRANSFORM_FIELD) is not None:
                    entry["parent"] = id_value(find_field(component, PARENT_FIELD))
                class_name, state_type = COMPONENT_CLASSES.get(str(component.type_id), ("", 0))
                if component.type_id == DESTROY_CLASS:
                    entry["destroys"] = True
                if component.type_id == OFFENSE_CLASS:
                    data = find_field(component, DAMAGE_DATA_FIELD)
                    entry["offense"] = entry["offense"] or text_of(deep(data, DAMAGE_ENTRY_FIELD) if data is not None else None)
                identifier = component_id(component)
                slot = number_of(find_field(component, REPLICATION_FIELD))
                faceted = any(find_field(component, facet) is not None for facet in FACET_FIELDS)
                if identifier is None or (slot == 0 and not faceted):
                    continue
                if slot != 0:
                    slots.append([slot, class_name or str(component.type_id), state_type])
                if faceted:
                    components.append([f"{entity_id:016X}", f"{identifier:016X}", f"{name}/{class_name or component.type_id}"])
                if class_name == "DelayedEvent":
                    delay = find_field(component, DELAY_FIELD)
                    event = find_field(component, EVENT_FIELD)
                    reference = deep(event, ENTITY_REF_FIELD) if event is not None else None
                    target = id_value(deep(reference, ID_FIELD)) if reference is not None else None
                    if target is None and reference is not None:
                        target = next((id_value(child) for child in walk(reference) if child is not reference and id_value(child) is not None), None)
                    if delay is None or event is None or target is None:
                        raise Failure(f"{path}: a delayed event of slot {slot} has no delay or no target")
                    seconds = required_real(delay, f"{path}: the delay of the delayed event of slot {slot}")
                    events.append([slot, round(seconds, 4), number_of(deep(event, EVENT_TYPE_FIELD)), target])
            entities[entity_id] = entry
    roots_found = [identifier for identifier, entry in entities.items() if entry["parent"] == NO_ENTITY]
    if not roots_found:
        raise Failure(f"{path}: the slice has no root entity")
    slots.sort()
    numbers = [slot[0] for slot in slots]
    if len(set(numbers)) != len(numbers):
        raise Failure(f"{path}: two components of the slice have the same slot")
    spell_slots = [slot[0] for slot in slots if slot[1] == "Spell"]
    if not spell_slots:
        return {}  # Not a spell entity (a waypoint, a rally point of an encounter): the caller leaves it out.
    resolved = []
    for slot, delay, kind, target in sorted(events):
        entry = entities.get(target)
        resolved.append([slot, delay, kind, entry["name"] if entry else f"{target:016X}", entry["offense"] if entry else "",
                         bool(entry and entry["destroys"])])
    return {"path": path, "asset": str(uuid.UUID(bytes=asset[0])).upper(), "sub": asset[1], "root": f"{roots_found[-1]:016X}",
            "interestRadius": round(radius, 3), "spellSlot": spell_slots[0], "slots": slots, "events": resolved,
            "components": components}


def slice_path(prefab: str) -> str:
    return "slices/" + prefab.strip().replace("\\", "/").lower().lstrip("/") + ".dynamicslice"


def build_document(sources: Sources) -> dict:
    tables = []
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is not None and sheet_type(data) == SPELL_TYPE:
            tables.append(Sheet(data))
    catalog_data = sources.read(CATALOG_PATH)
    sources.check_complete()
    if not tables:
        raise Failure("the game copy has no spell table")
    if catalog_data is None:
        raise Failure("the game copy has no asset catalog")
    catalog = Catalog(catalog_data)
    by_path = {path: asset for asset, path in catalog.paths.items() if asset[1] == SUB_ID}
    slices: list[dict] = []
    slice_index: dict[str, int] = {}
    spells: dict[str, list] = {}
    no_slice = 0
    no_spell_component = 0
    not_spells: set[str] = set()
    for sheet in tables:
        for column in ("SpellID", "SpellPrefabPath"):
            if column not in sheet.columns:
                raise Failure(f"the table {sheet.name} has no column {column}")
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "SpellID")).strip()
            prefab = str(sheet.source(row, "SpellPrefabPath")).strip()
            if not identifier or not prefab or identifier.lower() in spells:
                continue
            path = slice_path(prefab)
            if path not in slice_index and path not in not_spells:
                asset = by_path.get(path)
                roots = sources.stream(path) if asset is not None and sources.has(path) else None
                if roots is None:
                    no_slice += 1
                    continue
                entry = read_spell_slice(path, asset, roots)
                if not entry:
                    no_spell_component += 1
                    not_spells.add(path)
                    continue
                slice_index[path] = len(slices)
                slices.append(entry)
            if path in not_spells:
                continue
            spells[identifier.lower()] = [identifier, slice_index[path]]
    sources.check_complete()
    return {"format": 1, "counts": {"spells": len(spells), "slices": len(slices), "spellsWithNoSlice": no_slice,
                                    "slicesWithNoSpellComponent": no_spell_component},
            "slices": slices, "spells": sorted(spells.values())}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    arguments = sys.argv[1:]
    find = arguments[1:] if arguments[:1] == ["--find"] else None
    target = Path(arguments[0]) if arguments and find is None else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets", WANTED)
        document = build_document(sources)
        if find is not None:
            wanted = [text.lower() for text in find]
            for identifier, index in document["spells"]:
                if any(text in identifier.lower() for text in wanted):
                    entry = document["slices"][index]
                    print(f"{identifier}: {entry['path']} asset {entry['asset']}:{entry['sub']} root {entry['root']} spell slot "
                          f"{entry['spellSlot']} radius {entry['interestRadius']}\n  slots {entry['slots']}\n  events {entry['events']}")
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"spells: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"spells: {target}: {error}", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['spells']} spells with a slice, {counts['slices']} slices, {counts['spellsWithNoSlice']} spells whose prefab "
          f"has no slice in the game copy, {counts['slicesWithNoSpellComponent']} prefab slices with no Spell component (source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
