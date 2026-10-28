#!/usr/bin/env python3
"""The encounter recipes of the quests (stage QG5c2, docs/systems/encounters.md): the reader that four tools of this folder share.

encounters.py writes the recipes (encounter-data.json). creature_data.py adds a creature kind for each creature member of a recipe (a kind
with no spawn point), interactions.py the template of each start object (a server child), and gathering.py the gatherable row of each start
object. Each tool reads the recipes again from the game copy with this module: no tool reads the output of a different tool.

The chain of one recipe (all files are in the pak files of the game copy; research quest-encounters.md section 1 and Appendix C):

  1. A placement of a layer of the main world (regions/REGION/capitals/LAYER/LAYER.capitals.json) whose slice name or path has one of the
     texts of PARENT_TEXTS (the world event spawn groups: worldevent_spawngroup_single_master, the c10a_q4 spawn groups, the siege
     container). A rule of this project for the cost: the tool reads only these placed slices (`counts.parentsRead`); research
     quest-encounters.md found 45 of the 48 main-world quest recipes under worldevent_spawngroup_single_master, the rest under the other
     texts or in the script slices of an instance (a later stage).
  2. A spawner component of that slice (an area spawner: m_liveCount and the respawn range; a point spawner: the respawn cooldowns; a prefab
     spawner) whose m_sliceAsset names a slice with an EncounterManagerComponent: the encounter. Its place is the placement matrix times the
     m_worldTM of the spawner entity.
  3. The encounter slice: the manager (m_autoActivate, m_autoActivateDelayMS, m_gracePeriod, m_destroyOnCompletion, m_resetEncounterEventId,
     m_autoPerceiveParticipants, m_stages: the stage order), the participant tracker (m_gracePeriodSec, m_participantAddedGameEventId), the
     detector (a sphere: m_Radius, m_strFilter), the rewards (m_universalSuccessGameEventId), the slots of the root (the manager state 6786,
     the PositionInTheWorld state, the Objectives state), and each stage: an EncounterComponent (its slot: the state 2133; its component id:
     the target of message 1278) with m_stages (the child stages), m_spawnTimeline (m_sliceAsset or m_aliasAsset, m_spawnDelay, m_count,
     m_spawnLocations: local entity refs), m_objectives (the class and its fields), m_startActions, m_endActions, m_userWave, and the server
     facet fields m_childStagesOptional and m_disableSpawnersOnCompletion.
  4. Each slice of a spawn timeline: a creature member (an AIVariantProviderComponent with m_vitalsTableRowId, and a spawner whose
     m_sliceAsset or m_aliasAsset is the creature slice) or a start object (a GatherableController component and an Interact component: a
     quest gatherable with its variation m_selectedVariant). An other slice is an unknown member: the recipe keeps it with its cause.

A quest recipe (`quest` true): its success event, or the game event of the gatherable row of a start object, is a game event that a quest
task names (KillEnemyType of the task tables). The other encounters of the world (the random encounters, the sieges of the open world) are
counted (`counts.notQuest`) and not written: a rule of this project (the runner of stage QG5c2 runs only quest encounters; the other world
events are later stages, and they would change the open world).
"""

from __future__ import annotations

import json
import math
import struct
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import (REGIONS_FOLDER, TABLES_FOLDER, Catalog, Sources, asset_id, read_alias, spawner_path)  # noqa: E402
from interactions import (IDENTITY, INTERACT_CLASS, child_of, matrix_of, multiply, placement_matrix, quaternion_of, text_of,  # noqa: E402
                          walk)
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import (COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, NAME_FIELD, component_id, find_field, id_value)  # noqa: E402

# The texts of a placed slice that the tool reads as a parent of an encounter (lower case; step 1).
PARENT_TEXTS = ("worldevent", "spawngroup", "enc_")
# The class ids of the components and of the objective and action classes (the type index of the engine; research quest-encounters.md).
MANAGER_CLASS = uuid.UUID("48866f1b-791b-4606-a20c-22153d1750fa")
STAGE_CLASS = uuid.UUID("d1bc08ce-e244-4f31-84f4-dbd4d4bed1c8")
PLACE_CLASS = uuid.UUID("6c2fc842-f451-4b45-9d7d-a6ee954df025")
OBJECTIVES_CLASS = uuid.UUID("a42a47db-4f14-41c6-81d1-19a95ba99f84")
SPAWNER_STATE_CLASS = uuid.UUID("1ff7a74b-8c58-42f2-8a68-1706d75f57d0")
TRACKER_CLASS = uuid.UUID("418d3a41-a6f7-47bd-8f7d-33ae499cc523")
DETECTOR_CLASS = uuid.UUID("41bc3bb3-ef30-41e4-b751-12c4eab97c0e")
REWARDS_CLASS = uuid.UUID("c8962cbd-855c-4307-9a21-4ef149b38d5b")
VARIANT_PROVIDER_CLASS = uuid.UUID("8b86f1ed-3ca0-4e28-b816-4f5ba3267c13")
CONTROLLER_CLASS = uuid.UUID("0b382af4-7946-4698-9223-206d1187b612")
SPHERE_CLASS = uuid.UUID("7f2ef312-4089-4582-89c5-5d4156daa7fb")
OBJECTIVE_CLASSES = {
    uuid.UUID("632e9ece-f80c-48c0-9922-711537743873"): "SliceDestroyedObjective",
    uuid.UUID("cddd161a-9a85-4b81-9f7f-18845c6560c9"): "InvasionWaveObjective",
    uuid.UUID("3fb4f4af-5840-4fb0-ae8a-bd72a3f03ac7"): "SliceSpawnedObjective",
    uuid.UUID("ead831aa-836b-4ba5-8e24-4e5bb8119559"): "EncounterEventObjective",
    uuid.UUID("40acda82-8b8c-4bce-83d4-ab03140ca04a"): "EntityActiveStateChangedObjective",
    uuid.UUID("bfbd6466-7738-4378-b22d-402c00f27ab0"): "WaitForParentObjective",
    uuid.UUID("ee96e3a8-fa5e-4264-bb76-066eaf740a7e"): "BossPhaseObjective",
    uuid.UUID("5e59bffb-6af1-4330-a64a-b8b5f8c35c9f"): "CutsceneFinishedObjective",
}
ACTION_CLASSES = {
    uuid.UUID("1c5389f4-c99e-4362-b225-65ab588a8984"): "EntityEventAction",
    uuid.UUID("ba5688e3-0802-428a-b94c-d726b8564add"): "KillSpawnsAction",
    uuid.UUID("8be430f9-e0e5-4453-80aa-4cfdf64e78f4"): "DestroySliceAction",
}
F = {name: field_id(name) for name in (
    "m_autoActivate", "m_autoActivateDelayMS", "m_gracePeriod", "m_destroyOnCompletion", "m_resetEncounterEventId",
    "m_autoPerceiveParticipants", "m_resetOnSpawnError", "m_ignoreSpawnErrors", "m_stages", "m_gracePeriodSec",
    "m_participantAddedGameEventId", "m_universalSuccessGameEventId", "m_Radius", "m_strFilter", "m_shape", "m_replicationIndex",
    "m_spawnTimeline", "m_objectives", "m_startActions", "m_endActions", "m_userWave", "m_childStagesOptional",
    "m_disableSpawnersOnCompletion", "m_sliceAsset", "m_aliasAsset", "m_spawnDelay", "m_count", "m_spawnLocations", "m_timeLimit",
    "m_succeedOnTimeout", "m_failureCondition", "m_countDownProgress", "m_backgroundActivation", "m_title", "m_description",
    "m_trackedSlice", "m_trackedAlias", "m_requiredCount", "m_type", "m_applyRecursively", "m_localPlayerOnly", "m_localOnly",
    "m_vitalsTableRowId", "m_vitalsCategoryTableRowId", "m_vitalsLevel", "m_maintainPopulation", "m_spawnOnEnable", "m_spawnOnTrigger",
    "m_liveCount", "m_minRespawnRange", "m_maxRespawnRange", "m_minRespawnCooldown", "m_maxRespawnCooldown", "m_respawnAfterDeath",
    "m_spawnOnInitialize", "m_isClientAuthoratative", "m_worldTM", "m_selectedVariant", "m_variationTableUniqueName",
    "m_gatherableEntryId", "m_eventId", "m_entityRef", "m_isProximityGatherable",
    "m_overrideAggroDelay", "m_aggroDelayOverride")}
TASK_TYPE = "ObjectiveTasks"
GATHERABLE_TYPE = "GatherableData"
VARIATION_TYPE = "VariationData"


def number(element) -> int:
    """@return The unsigned number of a field (big-endian, any size); 0 with no field."""
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    if element is None or element.value is None or len(element.value) not in (1, 2, 4, 8):
        return 0
    return int.from_bytes(element.value, "big")


def real(element) -> float:
    if element is None or element.value is None or len(element.value) not in (4, 8):
        return 0.0
    value = struct.unpack(">f" if len(element.value) == 4 else ">d", element.value)[0]
    return value if math.isfinite(value) else 0.0


def text(element) -> str:
    value = text_of(element).strip().strip("\0")
    return "" if value == "0" and element is not None and len(element.value or b"") == 1 else value


def field(component, name: str):
    """@return The first field with this name in a component, its base classes, or its facets (a breadth-first look)."""
    wanted = F[name]
    queue = [component]
    while queue:
        node = queue.pop(0)
        for child in node.children:
            if child.name == wanted:
                return child
        queue.extend(child for child in node.children if child.value is None)
    return None


def refs(element) -> list[int]:
    """@return The entity ids of a list of local entity refs (m_stages, m_spawnLocations)."""
    out = []
    for entry in element.children if element is not None else []:
        identifier = id_value(next((node for node in walk(entry) if node.value is not None and len(node.value) == 8), None))
        if identifier is not None and identifier != 0xFFFFFFFF:
            out.append(identifier)
    return out


def read_entities(roots) -> dict[int, dict]:
    """@return The entities of a slice: the name, the matrix of m_worldTM (relative to the root of the slice), the components."""
    entities: dict[int, dict] = {}
    for root in roots:
        for element in walk(root):
            if element.type_id != ENTITY_TYPE:
                continue
            identifier = id_value(child_of(element, ID_FIELD))
            if identifier is None:
                continue
            components = child_of(element, COMPONENTS_FIELD)
            info = {"name": text_of(child_of(element, NAME_FIELD)), "matrix": IDENTITY, "components": []}
            for component in components.children if components is not None else []:
                world = find_field(component, F["m_worldTM"])
                if world is not None and world.value is not None and len(world.value) == 48:
                    info["matrix"] = matrix_of(world.value, f"the transform of the entity {info['name']}")
                info["components"].append(component)
            entities[identifier] = info
    return entities


def slot_of(component) -> int:
    return number(find_field(component, F["m_replicationIndex"]))


def asset_path(catalog: Catalog, element) -> str | None:
    return catalog.path(asset_id(element))


def place_of(matrix: tuple[float, ...]) -> dict:
    """@return The place and the rotation (x, y, z, w) of a world matrix."""
    return {"position": [round(matrix[3], 4), round(matrix[7], 4), round(matrix[11], 4)],
            "rotation": [round(value, 6) for value in quaternion_of(matrix)],
            "yaw": round(math.degrees(math.atan2(matrix[4], matrix[0])), 3)}


def read_objective(component) -> dict:
    """@return The objective of a stage: its class and its fields (no objective: the class "none")."""
    holder = field(component, "m_objectives")
    for node in walk(holder) if holder is not None else []:
        kind = OBJECTIVE_CLASSES.get(node.type_id)
        if kind is None:
            continue
        objective = {"type": kind, "timeLimit": number(field(node, "m_timeLimit")), "succeedOnTimeout": number(field(node, "m_succeedOnTimeout")) != 0,
                     "failureCondition": number(field(node, "m_failureCondition")) != 0,
                     "countDown": number(field(node, "m_countDownProgress")) != 0,
                     "background": number(field(node, "m_backgroundActivation")),
                     "title": text(field(node, "m_title")), "description": text(field(node, "m_description"))}
        if kind in ("SliceDestroyedObjective", "SliceSpawnedObjective"):
            objective["requiredCount"] = number(field(node, "m_requiredCount"))
            objective["trackedSlice"] = None
            objective["trackedAlias"] = None
            objective["_tracked"] = (field(node, "m_trackedSlice"), field(node, "m_trackedAlias"))
        if kind == "EncounterEventObjective":
            objective["eventId"] = text(field(node, "m_eventId"))
        return objective
    return {"type": "none"}


def read_actions(component, name: str) -> list[dict]:
    holder = field(component, name)
    actions = []
    for node in walk(holder) if holder is not None else []:
        kind = ACTION_CLASSES.get(node.type_id)
        if kind is None:
            continue
        action = {"type": kind}
        if kind == "EntityEventAction":
            action.update(event=number(field(node, "m_type")), recursive=number(field(node, "m_applyRecursively")) != 0,
                          localPlayerOnly=number(field(node, "m_localPlayerOnly")) != 0,
                          entity=next(iter(refs(field(node, "m_entityRef")) or [None])) if field(node, "m_entityRef") is not None else None)
        elif kind == "KillSpawnsAction":
            action["localOnly"] = number(field(node, "m_localOnly")) != 0
        actions.append(action)
    return actions


class Reader:
    """Reads the recipes of the game copy one time: the placements, the encounter slices, and the member slices."""

    def __init__(self, sources: Sources, catalog: Catalog) -> None:
        self.sources = sources
        self.catalog = catalog
        self.streams: dict[str, list | None] = {}
        self.members: dict[str, dict] = {}
        self.counts = {"layers": 0, "placements": 0, "parentsRead": 0, "encounters": 0, "quest": 0, "notQuest": 0}

    def stream(self, path: str):
        if path not in self.streams:
            self.streams[path] = self.sources.stream(path) if self.sources.has(path) else None
        return self.streams[path]

    def task_events(self) -> set[str]:
        """@return The game events (lower case) that a quest task names: KillEnemyType of the task tables."""
        events: set[str] = set()
        for path in self.sources.paths(TABLES_FOLDER, ".datasheet"):
            data = self.sources.read(path)
            if data is None or sheet_type(data) != TASK_TYPE:
                continue
            sheet = Sheet(data)
            if "KillEnemyType" not in sheet.columns:
                continue
            for row in range(sheet.row_count):
                for event in str(sheet.cell(row, "KillEnemyType")).split(","):
                    if event.strip():
                        events.add(event.strip().lower())
        return events

    def object_rows(self) -> tuple[dict[tuple[str, str], dict], dict[str, dict]]:
        """@return The variation rows (table and variant id, lower case, to the row: the gatherable row, InteractTag, InteractOnlyOnQuest) and
        the gatherable rows (lower-case id to GameEventID) that a start object can name."""
        variations: dict[tuple[str, str], dict] = {}
        gatherables: dict[str, dict] = {}
        for path in self.sources.paths(TABLES_FOLDER, ".datasheet"):
            data = self.sources.read(path)
            kind = sheet_type(data) if data is not None else None
            if kind not in (GATHERABLE_TYPE, VARIATION_TYPE):
                continue
            sheet = Sheet(data)
            if kind == GATHERABLE_TYPE and "GatherableID" in sheet.columns:
                for row in range(sheet.row_count):
                    identifier = sheet.source(row, "GatherableID").strip()
                    if identifier:
                        gatherables.setdefault(identifier.lower(), {"id": identifier,
                                                                    "event": sheet.source(row, "GameEventID").strip()
                                                                    if "GameEventID" in sheet.columns else ""})
            elif kind == VARIATION_TYPE and "VariantID" in sheet.columns:
                entry = next((column for column in sheet.columns if column.lower() == "gatherableentryid"), None)
                if entry is None:
                    continue
                for row in range(sheet.row_count):
                    identifier = sheet.source(row, "VariantID").strip()
                    if identifier:
                        variations.setdefault((sheet.name.lower(), identifier.lower()), {
                            "gatherable": sheet.source(row, entry).strip(),
                            "tag": sheet.source(row, "InteractTag").strip() if "InteractTag" in sheet.columns else "",
                            "onlyOnQuest": sheet.source(row, "InteractOnlyOnQuest").strip() == "1"
                            if "InteractOnlyOnQuest" in sheet.columns else False})
        return variations, gatherables

    def member(self, path: str) -> dict:
        """@return The kind of the slice of a spawn timeline entry (step 4)."""
        if path in self.members:
            return self.members[path]
        roots = self.stream(path)
        result: dict = {"slice": path, "kind": "unknown", "cause": "the slice has no source" if roots is None else ""}
        if roots is not None:
            vitals = category = ""
            level = 0
            body = None
            controller = interact = proximity = False
            variant = table = entry = ""
            for info in read_entities(roots).values():
                for component in info["components"]:
                    if component.type_id == VARIANT_PROVIDER_CLASS:
                        vitals = vitals or text(field(component, "m_vitalsTableRowId"))
                        category = category or text(field(component, "m_vitalsCategoryTableRowId"))
                        level = level or number(field(component, "m_vitalsLevel"))
                    if component.type_id == CONTROLLER_CLASS:
                        controller = True
                        entry = entry or text(field(component, "m_gatherableEntryId"))
                        proximity = proximity or number(field(component, "m_isProximityGatherable")) != 0
                    if component.type_id == INTERACT_CLASS:
                        interact = True
                    if field(component, "m_variationTableUniqueName") is not None and not table:
                        table = text(field(component, "m_variationTableUniqueName"))
                        variant = text(field(component, "m_selectedVariant"))
                    for name in ("m_sliceAsset", "m_aliasAsset"):
                        found = asset_id(field(component, name))
                        if body is None and found is not None and self.catalog.path(found) is not None \
                                and component.type_id != STAGE_CLASS:
                            body = (name, found)
            if path.endswith(".aliasasset"):
                result["cause"] = "a spawn of an alias asset (no member slice)"
            elif vitals and body is not None:
                body_path = self.catalog.path(body[1])
                bodies = [body_path]
                if body[0] == "m_aliasAsset":
                    alias_roots = self.stream(body_path)
                    bodies = [self.catalog.path(asset) for asset, _ in (read_alias(alias_roots) if alias_roots is not None else [])]
                result = {"slice": path, "kind": "creature", "vitals": vitals, "category": category, "level": level,
                          "bodyField": "alias" if body[0] == "m_aliasAsset" else "slice", "body": body_path,
                          "bodySlices": sorted(path for path in bodies if path), "_asset": body[1]}
            elif controller and interact:
                result = {"slice": path, "kind": "object", "variant": variant, "table": table, "entry": entry, "proximity": proximity}
            else:
                result["cause"] = ("a member with a vitals row and no creature slice" if vitals
                                   else "no vitals row and no start object (GatherableController and Interact)")
        self.members[path] = result
        return result

    def encounter(self, path: str, roots, matrix: tuple[float, ...]) -> dict:
        """@return The recipe of one encounter slice at a world matrix (step 3)."""
        entities = read_entities(roots)
        recipe: dict = {"slice": path, "unsupported": []}
        stages: dict[int, dict] = {}
        for identifier, info in entities.items():
            for component in info["components"]:
                kind = component.type_id
                if kind == MANAGER_CLASS:
                    recipe["root"] = identifier
                    recipe["rootName"] = info["name"]
                    recipe["managerSlot"] = slot_of(component)
                    recipe["manager"] = {"autoActivate": number(field(component, "m_autoActivate")) != 0,
                                         "autoActivateDelayMs": number(field(component, "m_autoActivateDelayMS")),
                                         "gracePeriod": number(field(component, "m_gracePeriod")),
                                         "destroyOnCompletion": number(field(component, "m_destroyOnCompletion")) != 0,
                                         "resetEvent": text(field(component, "m_resetEncounterEventId")),
                                         "autoPerceiveParticipants": number(field(component, "m_autoPerceiveParticipants")) != 0,
                                         "resetOnSpawnError": number(field(component, "m_resetOnSpawnError")) != 0,
                                         "overrideAggroDelay": number(field(component, "m_overrideAggroDelay")) != 0,
                                         "aggroDelay": round(real(field(component, "m_aggroDelayOverride")), 3)}
                    recipe["stageOrder"] = refs(field(component, "m_stages"))
                elif kind == STAGE_CLASS:
                    stages[identifier] = {"entity": identifier, "name": info["name"], "slot": slot_of(component),
                                          "component": component_id(component) or 0,
                                          "userWave": number(field(component, "m_userWave")) != 0,
                                          "children": refs(field(component, "m_stages")),
                                          "childrenOptional": number(field(component, "m_childStagesOptional")) != 0,
                                          "disableSpawners": number(field(component, "m_disableSpawnersOnCompletion")) != 0,
                                          "objective": read_objective(component),
                                          "startActions": read_actions(component, "m_startActions"),
                                          "endActions": read_actions(component, "m_endActions"),
                                          "_spawns": field(component, "m_spawnTimeline")}
                elif kind == PLACE_CLASS and identifier == recipe.get("root", identifier):
                    recipe.setdefault("placeSlot", slot_of(component))
                elif kind == OBJECTIVES_CLASS:
                    recipe.setdefault("objectivesSlot", slot_of(component))
                elif kind == TRACKER_CLASS:
                    recipe["tracker"] = {"component": component_id(component) or 0, "grace": number(field(component, "m_gracePeriodSec")),
                                         "addedEvent": text(field(component, "m_participantAddedGameEventId"))}
                elif kind == DETECTOR_CLASS:
                    shape = field(component, "m_shape")
                    sphere = next((node for node in walk(shape) if node.type_id == SPHERE_CLASS), None) if shape is not None else None
                    recipe["detector"] = {"radius": round(real(field(sphere, "m_Radius")), 3) if sphere is not None else 0.0,
                                          "filter": text(field(component, "m_strFilter")),
                                          "sphere": sphere is not None}
                elif kind == REWARDS_CLASS:
                    recipe["successEvent"] = text(field(component, "m_universalSuccessGameEventId"))
        if "root" not in recipe:
            raise Failure(f"{path}: the encounter slice has no EncounterManagerComponent")
        place = place_of(matrix)
        recipe.update(position=place["position"], rotation=place["rotation"], yaw=place["yaw"])
        # The stages in the manager order; a stage that no stage list names stays a diagnostic (research: 5 unreferenced definitions).
        order: list[int] = []
        pending = list(recipe["stageOrder"])
        while pending:
            identifier = pending.pop(0)
            if identifier in order:
                recipe["unsupported"].append(f"the stage {identifier} is in two stage lists")
                continue
            if identifier not in stages:
                recipe["unsupported"].append(f"the manager or a stage names the entity {identifier}, which is not a stage")
                continue
            order.append(identifier)
            pending.extend(stages[identifier]["children"])
        recipe["unreachable"] = sorted(stages[identifier]["name"] for identifier in stages if identifier not in order)
        index = {identifier: position for position, identifier in enumerate(order)}
        recipe["stages"] = []
        for identifier in order:
            stage = stages[identifier]
            stage["children"] = [index[child] for child in stage["children"] if child in index]
            spawns = []
            for entry in stage.pop("_spawns").children if stage.get("_spawns") is not None else []:
                member_path = asset_path(self.catalog, field(entry, "m_sliceAsset")) or asset_path(self.catalog, field(entry, "m_aliasAsset"))
                locations = []
                for location in refs(field(entry, "m_spawnLocations")):
                    local = entities.get(location, {}).get("matrix")
                    if local is None:
                        recipe["unsupported"].append(f"the spawn location {location} of the stage {stage['name']} is not an entity")
                        continue
                    locations.append(place_of(multiply(matrix, local)))
                spawn = {"member": member_path or "", "delay": number(field(entry, "m_spawnDelay")), "count": number(field(entry, "m_count")),
                         "locations": locations}
                if member_path is None:
                    recipe["unsupported"].append(f"a spawn of the stage {stage['name']} names no slice of the catalog")
                spawns.append(spawn)
            stage["spawns"] = spawns
            objective = stage["objective"]
            tracked = objective.pop("_tracked", None)
            if tracked is not None:
                objective["trackedSlice"] = asset_path(self.catalog, tracked[0])
                objective["trackedAlias"] = asset_path(self.catalog, tracked[1])
            recipe["stages"].append(stage)
        recipe["stageOrder"] = [index[identifier] for identifier in recipe["stageOrder"] if identifier in index]
        return recipe

    def read(self) -> tuple[list[dict], dict]:
        """@return The quest recipes of the main world (sorted by their key) and the counts."""
        events = self.task_events()
        recipes: dict[str, dict] = {}
        encounter_cache: dict[str, bool] = {}
        for layer in self.sources.paths(REGIONS_FOLDER, ".capitals.json"):
            data = self.sources.read(layer)
            if data is None:
                continue
            self.counts["layers"] += 1
            try:
                placements = json.loads(data.decode("utf-8-sig")).get("Capitals") or []
            except (ValueError, AttributeError) as error:
                raise Failure(f"{layer}: the placements of the layer did not parse: {error}") from error
            for placement in placements:
                self.counts["placements"] += 1
                name = str(placement.get("sliceName", "")).lower()
                if not any(part in name for part in PARENT_TEXTS):
                    continue
                path = spawner_path(placement, self.catalog, self.sources)
                if path is None:
                    continue
                roots = self.stream(path)
                if roots is None:
                    continue
                self.counts["parentsRead"] += 1
                world = placement_matrix(placement.get("rotation") or {}, placement.get("worldPosition") or {"x": 0, "y": 0, "z": 0})
                for spawner_entity, info in read_entities(roots).items():
                    for component in info["components"]:
                        if component.type_id == STAGE_CLASS:
                            continue
                        target = asset_path(self.catalog, field(component, "m_sliceAsset"))
                        if target is None:
                            continue
                        if target not in encounter_cache:
                            target_roots = self.stream(target)
                            encounter_cache[target] = target_roots is not None and any(
                                node.type_id == MANAGER_CLASS for root in target_roots for node in walk(root))
                        if not encounter_cache[target]:
                            continue
                        key = f"{placement.get('id', '')}/{spawner_entity}"
                        if key in recipes:
                            continue
                        recipe = self.encounter(target, self.stream(target), multiply(world, info["matrix"]))
                        recipe["key"] = key
                        recipe["layer"] = layer[len(REGIONS_FOLDER):-len(".capitals.json")]
                        recipe["parentSlice"] = path
                        recipe["parent"] = {
                            "spawner": info["name"],
                            "kind": ("area" if field(component, "m_liveCount") is not None
                                     else "point" if field(component, "m_minRespawnCooldown") is not None else "prefab"),
                            "maintainPopulation": number(field(component, "m_maintainPopulation")) != 0,
                            "spawnOnEnable": number(field(component, "m_spawnOnEnable")) != 0,
                            "spawnOnTrigger": number(field(component, "m_spawnOnTrigger")) != 0,
                            "liveCount": number(field(component, "m_liveCount")),
                            "respawn": [round(real(field(component, "m_minRespawnRange") or field(component, "m_minRespawnCooldown")), 3),
                                        round(real(field(component, "m_maxRespawnRange") or field(component, "m_maxRespawnCooldown")), 3)],
                            "respawnAfterDeath": number(field(component, "m_respawnAfterDeath")) != 0,
                        }
                        self.counts["encounters"] += 1
                        for stage in recipe["stages"]:
                            for spawn in stage["spawns"]:
                                if spawn["member"]:
                                    self.member(spawn["member"])
                        recipes[key] = recipe
        # The game event of each start object: its variation row (the table and the variant of its slice) and its gatherable row (the row
        # of the variant, else m_gatherableEntryId of the slice).
        variations, gatherables = self.object_rows()
        for member in self.members.values():
            if member["kind"] != "object":
                continue
            row = variations.get((member["table"].lower(), member["variant"].lower()))
            gatherable = gatherables.get((row["gatherable"] if row is not None else member["entry"]).lower())
            member["row"] = gatherable["id"] if gatherable is not None else ""
            member["event"] = gatherable["event"] if gatherable is not None else ""
            member["tag"] = row["tag"] if row is not None else ""
            member["onlyOnQuest"] = row["onlyOnQuest"] if row is not None else False
            if gatherable is None:
                member["cause"] = "the start object has no gatherable row (its variant and its m_gatherableEntryId)"
        for recipe in recipes.values():
            quest_events = {recipe.get("successEvent", "").lower()}
            for stage in recipe["stages"]:
                for spawn in stage["spawns"]:
                    member = self.members.get(spawn["member"]) if spawn["member"] else None
                    if member is not None and member["kind"] == "object" and member.get("event"):
                        quest_events.add(member["event"].lower())
            quest_events.discard("")
            recipe["questEvents"] = sorted(event for event in quest_events if event in events)
            recipe["quest"] = bool(recipe["questEvents"])
        out = []
        for key in sorted(recipes):
            recipe = recipes[key]
            if recipe["quest"]:
                self.counts["quest"] += 1
                out.append(recipe)
            else:
                self.counts["notQuest"] += 1
        return out, self.counts

    def start_objects(self, recipes: list[dict]) -> list[dict]:
        """@return The start object slices of the recipes (step 4), each one time, sorted by the slice path."""
        paths = sorted({spawn["member"] for recipe in recipes for stage in recipe["stages"] for spawn in stage["spawns"]
                        if spawn["member"] and self.member(spawn["member"])["kind"] == "object"})
        return [self.member(path) for path in paths]

    def creature_members(self, recipes: list[dict]) -> list[dict]:
        """@return The creature member slices of the recipes (step 4), each one time, sorted by the slice path."""
        paths = sorted({spawn["member"] for recipe in recipes for stage in recipe["stages"] for spawn in stage["spawns"]
                        if spawn["member"] and self.member(spawn["member"])["kind"] == "creature"})
        return [self.member(path) for path in paths]


def read_recipes(sources: Sources, catalog: Catalog) -> tuple[Reader, list[dict], dict]:
    """@return The reader (its caches), the quest recipes, and the counts."""
    reader = Reader(sources, catalog)
    recipes, counts = reader.read()
    # The start object events (the game event of the gatherable row of a start object) are read by gathering.py; a recipe whose only
    # quest event is such an event is selected by encounters.py with the rows (a second pass with the gatherable rows).
    return reader, recipes, counts
