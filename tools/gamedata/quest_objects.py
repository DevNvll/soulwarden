#!/usr/bin/env python3
"""Write the destructible and the proximity quest objects of the world (stage 1.8e and stage QG5a, docs/systems/quests.md): one JSON file.

The backend reads GAMEDATA_DIR/quest-objects.json at its start. Without the file no quest object can be destroyed (a strike changes
nothing), no proximity object gives its event, and the rest of the backend works.

A destructible quest object is a placement of a layer whose slice has the variation table Gatherable_Quest_Damageable (the corruption
nodules of 06_survivors: IO_Custom_MSQ_CorruptedNodule_A to _D; the monolith of 06_demonstorm2: Master_Quest_Damageable_Instanced with the
variant MSQ_Beach_Damned_Monolith). The client makes it itself from its level data (a managed actor: the UUID rule of interactions.py). It
has a DamageReceiver, a GatherableController (m_isDamageableGatherable), and no HitVolume: the client reports no melee contact on it (the
static read of stage 1.8e, research quests-18e/destructible). The sources (all in the pak files of the game copy):

  1. The placements: each placement of the layers (regions/REGION/capitals/LAYER/LAYER.capitals.json) whose slice (sliceAssetId and the
     asset catalog, else sliceName) has the variation table Gatherable_Quest_Damageable and one GatherableController: its UUID, its
     managed actor, its position (worldPosition), its slice asset and sub id, the slot of the GatherableController (m_replicationIndex),
     and its variant (variantName; stage QG5a: else the m_selectedVariant of its slice).
  2. The variation row of the variant (VariantID): its gatherable row (GatherableEntryID), InteractTag, and InteractOnlyOnQuest.
  3. The gatherable row (the GatherableData tables, the rules of gathering.py): BaseGatherTime (the work of the destruction), the respawn
     bounds, the loot root, and GameEventID (the game event of the quests).

Stage QG5a adds (research quest-gaps.md section 4; the census of evidence/quests/qg5a/implement):
  4. The proximity quest objects ("proximity"): a placement whose slice has the variation table Gatherable_Quest_Proximity (the trails of
     04A_Side01_01, the torches of 99C_MSQ2 and 17_MSQ: master_quest_proximity_instanced) or, with no variation table, a GatherableController
     with m_isProximityGatherable (a server facet field) and a gatherable row. The slice has a DetectionVolumeComponent: its sphere has the
     radius DetectionVolumeSphereRadius of the row ("Change Sphere Shape Radius"; with no row the m_Radius of the sphere of the slice).
     The server finds a character in the sphere (the client sends nothing: its handler of 5078 only sets a value of the HUD; static read
     ANSWERS.md B3).
  5. The direct destructible objects: a GatherableController with m_isDamageableGatherable, no variation table, and a gatherable row
     (quest_instanced_damageable_monarchs_crate of 12A_MSQ_13_StormBeach). Stage QG5b: the row of a direct destructible object has the
     form of a destructible quest row (the loot table Empty or blank and a GameEventID that a quest task names; no IsLootContainer: the
     crate row is Kicking), and
     the object has the m_interactTagString and the m_interactOnlyOnQuest of the one Interact component of its slice.
  6. The children: a PrefabSpawner of a placed slice that the client makes at the init (m_isClientAuthoratative false, m_spawnOnInitialize
     true) with a slice of 1, 4, or 5: its key "UUID/ENTITY" of the spawner, its place (the placement times the transform of the spawner
     entity), its variant (m_sliceVariant, else the slice), and the client id of a child of a level placement (interactions.py
     child_groups, client 0x6160B60; the first id of a slot near a border of the rule) as its actor.

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when a
variation row names a gatherable row that no table has, and when a value of a row has a wrong form (the rules of gathering.py). A placement
whose variant has no variation row is counted (unknownVariants) and not written.

Usage: quest_objects.py [OUTPUT.json]      (default: GAMEDATA_DIR/quest-objects.json; a test: a path outside the common folder)

The document (format 1):
  {"format": 1,
   "rows": [{"id", "table", "time", "respawn": [MIN, MAX], "loot", "event"}, ...],
   "objects": [{"key": PLACEMENT_UUID or UUID/ENTITY, "actor": HEX32, "asset": HEX32, "sub": N, "slot": N, "variant": TEXT, "row": INDEX,
                "tag": TEXT, "onlyOnQuest": BOOL, "position": [X, Y, Z], "slice": PATH}, ...],
   "proximity" (stage QG5a): [{the members of an object, and "radius": METERS}, ...],
   "counts": {...}}
  A backend of before stage QG5a reads "objects" and does not know "proximity".
"""

from __future__ import annotations

import json
import os
import re
import struct
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import CATALOG_PATH, REGIONS_FOLDER, Catalog, Sources  # noqa: E402
from gathering import (CONTROLLER_CLASS, DAMAGEABLE_TABLES, ENTRY_FIELD, PROXIMITY_TABLES, SELECTED_FIELD, SLOT_FIELD,  # noqa: E402
                       TABLE_FIELD, WANTED, Tables, quest_form, row_entry, slice_path_of)
from interactions import (IDENTITY, INTERACT_CLASS, ONLY_ON_QUEST, SPAWNER_CLASS, TAG, Template, actor_of, child_groups,  # noqa: E402
                          multiply, placement_matrix, walk, yaw_of)
from slice_ids import find_field  # noqa: E402

OUTPUT_NAME = "quest-objects.json"
FORMAT = 1
# Stage QG5a: the flags of the server facet of a GatherableController and the sphere of the DetectionVolumeComponent of a proximity slice.
PROXIMITY_FIELD = field_id("m_isProximityGatherable")
DAMAGEABLE_FIELD = field_id("m_isDamageableGatherable")
SHAPE_FIELD = field_id("m_shape")
SPHERE_RADIUS_FIELD = field_id("m_Radius")


def flag_in(element, name: int) -> bool:
    """@return True when a field of the name in the element (any depth: a field of the server facet) has a value that is not 0."""
    return any(node.name == name and node.value is not None and any(node.value) for node in walk(element))


def destructible_form(cells: dict, task_events: set[str]) -> bool:
    """@return True for the row of a direct destructible quest object (stage QG5b): the loot table Empty or blank (no roll) and a GameEventID
    that a quest task names (gathering.Tables.task_events; the gongs of a world event are no quest objects). A destructible row is no loot
    container (the crate MB_MSQ2_Crate_interact is Kicking with WaitForImpact)."""
    return (cells.get("FinalLootTable", "").strip() in ("", "Empty")
            and cells.get("GameEventID", "").strip().lower() in task_events)


def slice_facts(sources: Sources, path: str, cache: dict) -> dict | None:
    """@return The facts of a quest object slice (one GatherableController: its slot, its gatherable row, its flags; the variation table
    and the selected variant; the sphere of a detection volume), or no value for another slice."""
    if path in cache:
        return cache[path]
    cache[path] = None
    raw = sources.read(path)
    if raw is None:
        sources.check_complete()
        return None
    if CONTROLLER_CLASS.bytes not in raw:
        return None
    controllers = []
    tables = []
    selected = []
    interacts = []
    sphere = None
    for root in sources.stream(path) or []:
        for element in walk(root):
            if element.type_id == INTERACT_CLASS:
                # Stage QG5b: the tag and the flag of the Interact component (the quest gate of a direct object).
                tag = find_field(element, TAG)
                interacts.append({"tag": tag.value.decode("utf-8", "replace").rstrip("\0").strip() if tag is not None and tag.value else "",
                                  "onlyOnQuest": flag_in(element, ONLY_ON_QUEST)})
            if element.type_id == CONTROLLER_CLASS:
                slot = find_field(element, SLOT_FIELD)
                entry = find_field(element, ENTRY_FIELD)
                controllers.append({"slot": int.from_bytes(slot.value, "big") if slot is not None and slot.value else 0,
                                    "entry": entry.value.decode("utf-8", "replace").rstrip("\0").strip() if entry is not None and entry.value else "",
                                    "proximity": flag_in(element, PROXIMITY_FIELD), "damageable": flag_in(element, DAMAGEABLE_FIELD)})
            if element.name == TABLE_FIELD and element.value:
                tables.append(element.value.decode("utf-8", "replace"))
            if element.name == SELECTED_FIELD and element.value:
                selected.append(element.value.decode("utf-8", "replace").rstrip("\0").strip())
            if sphere is None and element.name == SHAPE_FIELD:
                radius = next((node for node in walk(element) if node.name == SPHERE_RADIUS_FIELD and node.value is not None
                               and len(node.value) == 4), None)
                if radius is not None:
                    sphere = struct.unpack(">f", radius.value)[0]
    if len(controllers) != 1:
        return None
    table = next((name for name in tables if name in DAMAGEABLE_TABLES or name in PROXIMITY_TABLES), "")
    controller = controllers[0]
    if table in DAMAGEABLE_TABLES or (not tables and controller["damageable"] and controller["entry"]):
        kind = "destructible"
    elif table in PROXIMITY_TABLES or (not tables and controller["proximity"] and controller["entry"]):
        kind = "proximity"
    else:
        return None
    interact = interacts[0] if len(interacts) == 1 else {"tag": "", "onlyOnQuest": False}
    cache[path] = {"slot": controller["slot"], "table": table, "kind": kind, "entry": controller["entry"],
                   "selected": selected[0] if selected else "", "sphere": sphere, "tag": interact["tag"],
                   "onlyOnQuest": interact["onlyOnQuest"]}
    return cache[path]


def build(sources: Sources) -> dict:
    tables = Tables(sources)
    catalog_data = sources.read(CATALOG_PATH)
    if catalog_data is None:
        sources.check_complete()
    catalog = Catalog(catalog_data)
    assets = {path: asset for asset, path in catalog.paths.items()}
    rows: list[dict] = []
    row_index: dict[str, int] = {}
    objects: list[dict] = []
    counts = {"placements": 0, "objects": 0, "unknownVariants": 0, "byVariant": {}}
    cache: dict = {}

    def row_of(identifier: str, what: str) -> int:
        key = identifier.lower()
        if key in row_index:
            return row_index[key]
        if key not in tables.rows:
            raise Failure(f"{what} names the gatherable row {identifier!r}, and no GatherableData table has it")
        table, cells = tables.rows[key]
        entry = row_entry(cells.get("GatherableID", identifier).strip(), table, cells)
        row_index[key] = len(rows)
        rows.append({"id": entry["id"], "table": table, "time": entry["time"], "respawn": entry["respawn"], "loot": entry["loot"],
                     "event": entry["event"]})
        return row_index[key]

    proximity: list[dict] = []
    spawner_cache: dict[str, Template | None] = {}

    def spawner_template(path: str) -> Template | None:
        if path not in spawner_cache:
            raw = sources.read(path)
            spawner_cache[path] = Template(path, sources.stream(path) or []) if raw is not None and SPAWNER_CLASS.bytes in raw else None
        return spawner_cache[path]

    def add(key: str, actor: bytes, slice_path: str, facts: dict, variant: str, place: list[float], what: str, child: bool) -> None:
        """Adds one quest object of a placement or of a spawner child (stage QG5a: the variant, else the selected variant of the slice)."""
        name = variant.strip() or facts["selected"]
        if facts["table"]:
            entry = tables.variations.get(facts["table"], {}).get(name.lower())
            if entry is None or not entry["gatherable"]:
                counts["unknownVariants"] += 1
                return
            gatherable, tag, only_on_quest = entry["gatherable"], entry.get("tag", ""), bool(entry.get("onlyOnQuest"))
            radius = entry.get("radius", 0.0)
        else:
            # A direct binding (stage QG5a): the row of the controller, when it has the form of a row of the quest tables
            # (gathering.quest_form); no tag; the radius of the sphere of the slice.
            if facts["entry"].lower() not in tables.rows:
                counts["directNoRow"] = counts.get("directNoRow", 0) + 1
                return
            cells = tables.rows[facts["entry"].lower()][1]
            destructible = facts["kind"] == "destructible" and destructible_form(cells, tables.task_events)
            if not destructible and not quest_form(cells, f"the direct gatherable row {facts['entry']}", tables.empty_loot):
                counts["directNotQuest"] = counts.get("directNotQuest", 0) + 1
                return
            gatherable, tag, only_on_quest, radius = facts["entry"], "", False, facts["sphere"] or 0.0
            if destructible:
                # Stage QG5b: the tag and the flag of the Interact component of the slice (the crate: IO_MB_MSQ2_Crate_interact, true).
                tag, only_on_quest = facts["tag"], facts["onlyOnQuest"]
                counts["directDestructible"] = counts.get("directDestructible", 0) + 1
            counts["direct"] = counts.get("direct", 0) + 1
        if gatherable.lower() not in tables.rows:
            # A variation row of the data names a gatherable row that no table has (99A_IsabellaWhisper_02 of the build): no object.
            counts["variantRowMissing"] = counts.get("variantRowMissing", 0) + 1
            return
        row = row_of(gatherable, f"{what}: the gatherable row of {facts['table'] or 'its controller'}/{name}")
        asset = assets.get(slice_path)
        if asset is None:
            raise Failure(f"{slice_path}: the asset catalog has no asset id of the slice")
        item = {"key": key, "actor": actor.hex(), "asset": asset[0].hex(), "sub": asset[1], "slot": facts["slot"], "variant": name, "row": row,
                "tag": tag, "onlyOnQuest": only_on_quest, "position": [round(value, 4) for value in place], "slice": slice_path}
        if facts["kind"] == "proximity":
            if not radius > 0:
                counts["proximityNoRadius"] = counts.get("proximityNoRadius", 0) + 1
                return
            item["radius"] = round(radius, 4)
            proximity.append(item)
        else:
            objects.append(item)
        counts["byVariant"][name] = counts["byVariant"].get(name, 0) + 1
        counts["children" if child else "placementsWithObject"] = counts.get("children" if child else "placementsWithObject", 0) + 1

    region_names = sorted({path[len(REGIONS_FOLDER):].split("/")[0] for path in sources.paths(REGIONS_FOLDER, ".capitals.json")})
    for name in region_names:
        for path in sources.paths(f"{REGIONS_FOLDER}{name}/capitals/", ".capitals.json"):
            data = sources.read(path)
            if data is None:
                continue
            try:
                document = json.loads(data)
            except ValueError as error:
                raise Failure(f"{path} is not JSON: {error}") from error
            for placement in document.get("Capitals") or []:
                slice_name = str(placement.get("sliceName", ""))
                if not slice_name or slice_name == "<PLOT>":
                    continue
                counts["placements"] += 1
                match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", str(placement.get("sliceAssetId", "")))
                slice_path = catalog.path((uuid.UUID(match[1]).bytes, int(match[2], 16))) if match else None
                slice_path = slice_path or slice_path_of(slice_name, sources)
                if slice_path is None:
                    continue
                key = str(placement.get("id", "")).strip().lower()
                position = placement.get("worldPosition") or {}
                facts = slice_facts(sources, slice_path, cache)
                if facts is not None:
                    if not re.fullmatch(r"[0-9a-f-]{36}", key):
                        raise Failure(f"{path}: a quest object placement has no UUID")
                    add(key, actor_of(key), slice_path, facts, str(placement.get("variantName", "")),
                        [float(position.get(axis, 0.0)) for axis in ("x", "y", "z")], f"{path}: the placement {key}", False)
                    continue
                # Stage QG5a: the quest object children of the PrefabSpawners of the placed slice.
                template = spawner_template(slice_path)
                for spawner in template.spawner_list if template is not None else []:
                    child_path = catalog.path(spawner["asset"]) if spawner["asset"] is not None else None
                    if spawner["client"] or not spawner["onInit"] or child_path is None or not sources.has(child_path):
                        continue
                    child = slice_facts(sources, child_path, cache)
                    if child is None:
                        continue
                    if not re.fullmatch(r"[0-9a-f-]{36}", key):
                        raise Failure(f"{path}: a quest object placement has no UUID")
                    world = multiply(placement_matrix(placement.get("rotation") or {}, position), template.matrices.get(spawner["entity"], IDENTITY))
                    place = [world[3], world[7], world[11]]
                    group = child_groups(*place, yaw_of(world), spawner["asset"][0])[0]
                    add(f"{key}/{spawner['entity']:016x}", group.to_bytes(8, "little") + bytes(8), child_path, child, spawner["variant"], place,
                        f"{path}: a child of the placement {key}", True)
    objects.sort(key=lambda item: item["key"])
    proximity.sort(key=lambda item: item["key"])
    counts["objects"] = len(objects)
    counts["proximityObjects"] = len(proximity)
    counts["rows"] = len(rows)
    return {"format": FORMAT, "rows": rows, "objects": objects, "proximity": proximity, "counts": counts}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets", WANTED)
        document = build(sources)
        text = json.dumps(document, indent=1, sort_keys=True)
        partial = target.with_name(target.name + f".partial-{os.getpid()}")
        partial.write_text(text + "\n", encoding="utf-8")
        partial.replace(target)
    except Failure as error:
        print(f"quest objects: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"quest objects: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['objects']} destructible and {counts['proximityObjects']} proximity quest objects of {counts['placements']} "
          f"placements ({counts.get('children', 0)} spawner children), {counts['rows']} gatherable rows, {counts['unknownVariants']} "
          f"placements with no variation row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
