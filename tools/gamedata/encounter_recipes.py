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
     Stage QG5c3 adds three kinds of the data:
       - a direct creature member: a creature slice itself (a CharacterComponent and a Vitals component with m_rowReference, no
         AIVariantProvider: the inquisitors of the corrupted tree, the named anubian brute of the tomb). Its vitals row is m_rowReference,
         its body the slice itself ("direct": true); a creature slice with a GatherableController (the mining of the body) is a creature;
       - a proximity object: a GatherableController with m_isProximityGatherable (the ambush starts of Gatherable_Quest_Proximity, the 20 m
         area trigger of Grivaros, which has no Interact component: "interact": false). Its "radius" is DetectionVolumeSphereRadius of its
         variation row, else the m_Radius of the sphere of its DetectionVolumeComponent (the rule of quest_objects.py, stage QG5a);
       - a member that ends itself: an unknown member whose delayed events of the init reach the trigger of an entity with a
         DestroyGDEComponent ("lifetime": the seconds; the smoke burst of the lost pylons 4.2 s, the powder keg 13.15 s). The reach follows
         the event types of spells.py (0 trigger): a trigger of an entity starts its delayed events with m_startDelayOnTrigger and sends the
         events of its EventRerouterComponent at once; an activation or a deactivation (2, 3) ends nothing.

A quest recipe (`quest` true): its success event, or the game event of the gatherable row of a start object, is a game event that a quest
task names (KillEnemyType of the task tables). The other encounters of the world (the random encounters, the sieges of the open world) are
counted (`counts.notQuest`) and not written: a rule of this project (the runner of stage QG5c2 runs only quest encounters; the other world
events are later stages, and they would change the open world).
"""

from __future__ import annotations

import heapq
import json
import math
import struct
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import (REGIONS_FOLDER, TABLES_FOLDER, Catalog, Sources, asset_id, read_alias, read_target_list,  # noqa: E402
                           slice_assets, spawner_path)
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
# Stage QG5c3: the shapes of a detector (the query shapes of a DetectionVolumeComponent: spells.py SHAPE_CLASSES and the name index).
CYLINDER_CLASS = uuid.UUID("709b11ea-fd56-4fef-b841-7cea549368e6")
AABB_CLASS = uuid.UUID("27462017-fe0f-4b81-96e9-8875b750ec69")
# Stage QG5c3: the parts of a creature slice and of a slice that ends itself.
CHARACTER_CLASS = uuid.UUID("15407caa-4970-4d06-8b5c-612fba11bb45")
VITALS_CLASS = uuid.UUID("0c848755-1f41-4408-b2be-0a3627674f50")
DELAYED_EVENT_CLASS = uuid.UUID("d353f7e8-4266-f749-19a1-aa2c3841fbca")
REROUTER_CLASS = uuid.UUID("ea210bd0-1072-c3d6-25f0-82bbbecb0bfb")
DESTROY_CLASS = uuid.UUID("28e4c1a4-b9b4-8675-59e8-a21527d9da65")
# The event type of a trigger (spells.py: 0 trigger, 2 activate, 3 deactivate).
EVENT_TRIGGER = 0
EVENT_ACTIVATE = 2
EVENT_DEACTIVATE = 3
# Stage QG5c4: the SliceComponent of the root entity of a slice (its list "Entities" repeats the entities of the slice: it is no spawner),
# the parts of a spawner entity (the base SpawnerComponent with the spawned slice; the area, point, or prefab component with its rules on the
# same entity), the event component of a detection volume (the trigger of an EntityActiveStateChangedObjective), and the point spawner of a
# creature (the parts of a group member).
SLICE_CLASS = uuid.UUID("afd304e4-1773-47c8-855a-8b622398934f")
BASE_SPAWNER_CLASS = SPAWNER_STATE_CLASS
DETECTION_EVENT_CLASS = uuid.UUID("7c84dee6-f1df-4b85-8cd5-6bfb970b70d0")
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
    "m_overrideAggroDelay", "m_aggroDelayOverride",
    "m_rowReference", "m_timeDelaySeconds", "m_eventData", "m_startDelayOnInit", "m_startDelayOnTrigger", "m_events", "m_Height",
    "m_center", "m_aabb",
    "m_targetEntityId", "m_completeOnActivated", "m_completeOnDeactivated", "m_detectionVolumeEntity", "m_eventsOnEnter",
    "m_eventsOnFirstEnter", "m_event", "m_detectDeadTargets", "m_editPrefab", "m_persistPrefab", "m_gatheringStartEvent",
    "m_gatheringEndEvent", "m_shapeEntityID",
    "m_patrolPathEntity", "m_waypointIds", "m_pathTravesalPolicy", "m_taskName", "m_minDuration", "m_maxDuration", "m_matchAlignment",
    "m_cutsceneName",
    "Tags", "Tag", "Slices", "Slice", "Schedule")}
# Stage QG5c5: the patrol path of a creature spawner of a group member (the boat landings, the portal waves: creature_data.py PATROL_TYPE,
# WAYPOINT_TYPE) and the number of the policies of the client (creature_data.py POLICIES).
PATH_CLASS = uuid.UUID("fa7fa996-f8c3-4cd0-8313-042fb6a66578")
WAYPOINT_CLASS = uuid.UUID("3d1526bf-ac9c-4a5d-8b0d-00abfccd19f1")
PATH_POLICIES = 4
TASK_TYPE = "ObjectiveTasks"
# Stage QG9a (docs/systems/instances.md): the tables of the game modes and of their maps. A map of a solo mode (IsSoloTrial 1) whose level is
# the main world (CoatlicueName coatlicue\\NewWorld_VitaeEterna) has a mode root slice (SlicePath) that a layer of the main world places
# (Dungeon/MedeaWindsward/MedeaScript in the layer 06sw_medea_script): its spawners are the encounters and the objects of the instance.
MODES_TABLE = TABLES_FOLDER + "javelindata_gamemodes.datasheet"
MAPS_TABLE = TABLES_FOLDER + "javelindata_gamemodemaps.datasheet"
MAIN_TERRAIN = "coatlicue/newworld_vitaeeterna"
# Stage QG9a: the text of the slice path of the daily alternative of an encounter of a story instance (ai_ww_medea_encounter_console_daily).
DAILY_TEXT = "_daily"
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


def vector(element) -> list[float] | None:
    """@return The three finite floats of an AZ::Vector3 field (12 bytes), or no value."""
    if element is None or element.value is None or len(element.value) != 12:
        return None
    values = struct.unpack(">fff", element.value)
    return [round(value, 4) for value in values] if all(math.isfinite(value) for value in values) else None


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


def detector_shape(component) -> dict:
    """@return The query shape of a DetectionVolumeComponent (stage QG5c3): "sphere" with its radius, "cylinder" with its radius and its
    height (centered on m_center: the Torment detector has the height 10 and the center 5 m up), or "aabb" with its extents (the half sizes
    on each axis: the box of the Bathory hall has the z extent 4.25, the half of the height 8.5 of the polygon prism of the same entity);
    "none" for an other or no shape. m_center is the offset from the entity. Stage QG5c4: "_shapeEntity" is m_shapeEntityID (the entity
    whose place holds the shape; no key: none)."""
    center = vector(field(component, "m_center")) or [0.0, 0.0, 0.0]
    holder = field(component, "m_shape")
    shape = {"shape": "none", "center": center}
    for node in walk(holder) if holder is not None else []:
        if node.type_id == SPHERE_CLASS:
            shape = {"shape": "sphere", "radius": round(real(field(node, "m_Radius")), 3), "center": center}
            break
        if node.type_id == CYLINDER_CLASS:
            shape = {"shape": "cylinder", "radius": round(real(field(node, "m_Radius")), 3), "height": round(real(field(node, "m_Height")), 3),
                     "center": center}
            break
        if node.type_id == AABB_CLASS:
            shape = {"shape": "aabb", "extents": vector(field(node, "m_aabb")) or [0.0, 0.0, 0.0], "center": center}
            break
    shape_entity = number(field(component, "m_shapeEntityID"))
    if shape_entity not in (0, 0xFFFFFFFF):
        shape["_shapeEntity"] = shape_entity
    return shape


def event_target(element) -> tuple[int | None, int]:
    """@return The target entity (no value: none) and the type of an event of m_eventData or of m_events."""
    target = refs(field(element, "m_entityRef")) if element is not None else []
    return (target[0] if target else None), number(field(element, "m_type")) if element is not None else 0


def lifetime_of(entities: dict[int, dict]) -> float | None:
    """@return The seconds after which a slice ends itself (stage QG5c3), or no value: the earliest trigger of an entity with a
    DestroyGDEComponent from the delayed events of the init (m_startDelayOnInit; with no field: of the init, the rule of spells.py). A
    trigger (type 0) of an entity starts its delayed events with m_startDelayOnTrigger and sends the trigger events of its
    EventRerouterComponent at once (the powder keg: the fuse 8 s, the micro delay 0.15 s, the rerouter, the destroy delay 5 s: 13.15 s)."""
    delays: list[tuple[int, float, int | None, int, bool, bool]] = []
    reroutes: dict[int, list[tuple[int | None, int]]] = {}
    destroys: set[int] = set()
    for identifier, info in entities.items():
        for component in info["components"]:
            if component.type_id == DESTROY_CLASS:
                destroys.add(identifier)
            elif component.type_id == DELAYED_EVENT_CLASS:
                target, kind = event_target(field(component, "m_eventData"))
                init = field(component, "m_startDelayOnInit")
                trigger = field(component, "m_startDelayOnTrigger")
                delays.append((identifier, max(real(field(component, "m_timeDelaySeconds")), 0.0), target, kind,
                               number(init) != 0 if init is not None else True, number(trigger) != 0 if trigger is not None else False))
            elif component.type_id == REROUTER_CLASS:
                holder = field(component, "m_events")
                reroutes.setdefault(identifier, []).extend(event_target(entry) for entry in (holder.children if holder is not None else []))
    if not destroys:
        return None
    queue = [(delay, target) for _, delay, target, kind, init, _ in delays if init and kind == EVENT_TRIGGER and target is not None]
    heapq.heapify(queue)
    reached: set[int] = set()
    while queue:
        time, entity = heapq.heappop(queue)
        if entity in reached:
            continue
        reached.add(entity)
        if entity in destroys:
            return round(time, 3)
        for owner, delay, target, kind, _, trigger in delays:
            if owner == entity and trigger and kind == EVENT_TRIGGER and target is not None:
                heapq.heappush(queue, (time + delay, target))
        for target, kind in reroutes.get(entity, []):
            if kind == EVENT_TRIGGER and target is not None:
                heapq.heappush(queue, (time, target))
    return None


def asset_path(catalog: Catalog, element) -> str | None:
    return catalog.path(asset_id(element))


def place_of(matrix: tuple[float, ...]) -> dict:
    """@return The place and the rotation (x, y, z, w) of a world matrix."""
    return {"position": [round(matrix[3], 4), round(matrix[7], 4), round(matrix[11], 4)],
            "rotation": [round(value, 6) for value in quaternion_of(matrix)],
            "yaw": round(math.degrees(math.atan2(matrix[4], matrix[0])), 3)}


def spawner_rules(info: dict, component) -> dict:
    """@return The parent spawner of a recipe (step 2): the rules of the spawner entity `info` whose component `component` names the
    encounter slice. Stage QG5c4: a spawner entity has the base SpawnerComponent (the spawned slice) and, on the same entity, the component
    of its kind with its rules: an area spawner (m_liveCount, the respawn range, m_spawnOnEnable), a prefab spawner (m_editPrefab,
    m_persistPrefab; the start m_spawnOnInitialize), or a point spawner (the respawn cooldowns; the start m_spawnOnInitialize). The base
    component alone has none of these fields: a look at it alone found "a prefab spawner that does not spawn" for the area spawner of
    Gazsala. "spawnOnEnable" is the start field of the kind; "startField" names it."""
    parts = [part for part in info["components"] if part.type_id not in (STAGE_CLASS, SLICE_CLASS)]
    area = next((part for part in parts if field(part, "m_liveCount") is not None), None)
    prefab = next((part for part in parts if field(part, "m_editPrefab") is not None or field(part, "m_persistPrefab") is not None), None)
    point = next((part for part in parts if field(part, "m_minRespawnCooldown") is not None), None)
    rules, kind, start = (area, "area", "m_spawnOnEnable") if area is not None else \
        (prefab, "prefab", "m_spawnOnInitialize") if prefab is not None else \
        (point, "point", "m_spawnOnInitialize") if point is not None else (component, "spawner", "m_spawnOnEnable")
    return {
        "spawner": info["name"], "kind": kind, "startField": start,
        "maintainPopulation": number(field(rules, "m_maintainPopulation")) != 0,
        "spawnOnEnable": number(field(rules, start)) != 0,
        "spawnOnTrigger": number(field(rules, "m_spawnOnTrigger")) != 0,
        "liveCount": number(field(rules, "m_liveCount")),
        "respawn": [round(real(field(rules, "m_minRespawnRange") or field(rules, "m_minRespawnCooldown")), 3),
                    round(real(field(rules, "m_maxRespawnRange") or field(rules, "m_maxRespawnCooldown")), 3)],
        "respawnAfterDeath": number(field(rules, "m_respawnAfterDeath")) != 0,
    }


def objective_of(node) -> dict:
    """@return One objective of a stage: its class and its fields."""
    kind = OBJECTIVE_CLASSES[node.type_id]
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
    if kind == "CutsceneFinishedObjective":
        # Stage QG9a: the cutscene of the stage (the IGC of a story instance: 06A_MSQ_21_MedeaShowdownIntro_IGC).
        objective["cutscene"] = text(field(node, "m_cutsceneName"))
    if kind == "EntityActiveStateChangedObjective":
        objective["target"] = number(field(node, "m_targetEntityId"))
        objective["completeOn"] = ("deactivated" if number(field(node, "m_completeOnDeactivated")) != 0
                                   else "activated" if number(field(node, "m_completeOnActivated")) != 0 else "")
    return objective


def read_objective(component) -> dict:
    """@return The objective of a stage: its class and its fields (no objective: the class "none"). Stage QG5c4: "more" has the other
    objectives of the stage in their order (one for each tracked member slice: the four braziers of 99A_MSQ_10, the two bosses of the tarot
    tower 10306, the creatures and the smoke burst of a kill stage of the lost pylons; only the first one has a title), and an
    EntityActiveStateChangedObjective has its target entity and the change that completes it ("completeOn": "deactivated" or
    "activated")."""
    holder = field(component, "m_objectives")
    nodes = [node for node in (walk(holder) if holder is not None else []) if node.type_id in OBJECTIVE_CLASSES]
    if not nodes:
        return {"type": "none"}
    objective = objective_of(nodes[0])
    objective["more"] = [objective_of(node) for node in nodes[1:]]
    return objective


def state_senders(entities: dict[int, dict], target: int, kind: int) -> dict:
    """@return The entities of a slice that send the event `kind` (2 activate, 3 deactivate) to `target` (stage QG5c4: the source of the
    change that an EntityActiveStateChangedObjective waits for), in two groups:
      "volumes": [(EVENT ENTITY, VOLUME ENTITY, DEAD TARGETS)] the DetectionVolumeEventComponents that send it when a target enters the
                 detection volume (m_eventsOnEnter, m_eventsOnFirstEnter): the Trigger_Start of the tarot towers deactivates itself;
      "gathers": [ENTITY NAME] the GatherableControllers whose gather start or end event reaches a sender through the triggers (type 0) of
                 the delayed events and the event rerouters of the slice: the horn of victory of 09A_M5 (Gatherable_Complete, its start
                 event, Interaction_Event_Activate, the delayed event of 0.1 s, the deactivation of Start_Trigger_Event)."""
    volumes: list[tuple[int, int, bool]] = []
    direct: set[int] = set()
    triggers: dict[int, set[int]] = {}
    gathers: dict[int, list[int]] = {}
    for identifier, info in entities.items():
        for component in info["components"]:
            if component.type_id == DETECTION_EVENT_CLASS:
                volume = refs(field(component, "m_detectionVolumeEntity"))
                dead = number(field(component, "m_detectDeadTargets")) != 0
                for name in ("m_eventsOnEnter", "m_eventsOnFirstEnter"):
                    holder = field(component, name)
                    for entry in holder.children if holder is not None else []:
                        if event_target(field(entry, "m_event")) == (target, kind):
                            volumes.append((identifier, volume[0] if volume else identifier, dead))
            elif component.type_id == DELAYED_EVENT_CLASS:
                aimed, aimed_kind = event_target(field(component, "m_eventData"))
                if (aimed, aimed_kind) == (target, kind):
                    direct.add(identifier)
                elif aimed is not None and aimed_kind == EVENT_TRIGGER:
                    triggers.setdefault(identifier, set()).add(aimed)
            elif component.type_id == REROUTER_CLASS:
                holder = field(component, "m_events")
                for entry in holder.children if holder is not None else []:
                    aimed, aimed_kind = event_target(entry)
                    if (aimed, aimed_kind) == (target, kind):
                        direct.add(identifier)
                    elif aimed is not None and aimed_kind == EVENT_TRIGGER:
                        triggers.setdefault(identifier, set()).add(aimed)
            elif component.type_id == CONTROLLER_CLASS:
                for name in ("m_gatheringStartEvent", "m_gatheringEndEvent"):
                    aimed, aimed_kind = event_target(field(component, name))
                    if aimed is not None and aimed_kind == EVENT_TRIGGER:
                        gathers.setdefault(identifier, []).append(aimed)
                    elif (aimed, aimed_kind) == (target, kind):
                        gathers.setdefault(identifier, []).append(identifier)
                        direct.add(identifier)
    # The entities that reach a direct sender through triggers (a breadth walk from each gatherable, bounded by the entities of the slice).
    found = []
    for controller, starts in gathers.items():
        pending, seen = list(starts), set()
        while pending:
            entity = pending.pop()
            if entity in seen:
                continue
            seen.add(entity)
            if entity in direct:
                found.append(entities[controller]["name"])
                break
            pending.extend(triggers.get(entity, ()))
    return {"volumes": volumes, "gathers": sorted(set(found))}


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
        elif kind == "DestroySliceAction":
            # Stage QG5c5: the slice or the alias whose spawns the action removes (the corruption hazard of the setup of a monolith:
            # alias_darkness_corruption_hazard_major; the solid door of the braziers). Encounter.encounter writes the paths.
            action["localOnly"] = number(field(node, "m_localOnly")) != 0
            action["_assets"] = (asset_id(field(node, "m_sliceAsset")), asset_id(field(node, "m_aliasAsset")))
        actions.append(action)
    return actions


def part_route(entities: dict[int, dict], info: dict) -> dict | None:
    """@return The patrol path of a creature spawner entity of a group member (stage QG5c5), in the coordinates of the member slice:
    {"policy": N, "points": [[x, y, z, yaw, min wait, max wait, task, alignment], ...]}, or no value (no path, or a path that is not
    complete). The spawner names its path entity (m_patrolPathEntity); the path entity has the AIPathComponent with its waypoints
    (m_waypointIds) and its policy (m_pathTravesalPolicy); each waypoint entity has its AIWaypointComponent (the task, the waits, the
    alignment) and its m_worldTM (relative to the root of the slice: the rule of creature_data.read_patrol)."""
    reference = None
    for component in info["components"]:
        holder = field(component, "m_patrolPathEntity")
        if holder is not None:
            found = refs(holder) or [id_value(next((node for node in walk(holder) if node.value is not None and len(node.value) == 8),
                                                     None))]
            reference = found[0] if found and found[0] not in (None, 0, 0xFFFFFFFF) else None
            break
    if reference is None or reference not in entities:
        return None
    path = next((component for component in entities[reference]["components"] if component.type_id == PATH_CLASS), None)
    if path is None:
        return None
    policy = number(field(path, "m_pathTravesalPolicy"))
    points = []
    for waypoint in refs(field(path, "m_waypointIds")):
        entity = entities.get(waypoint)
        part = next((component for component in entity["components"] if component.type_id == WAYPOINT_CLASS), None) if entity else None
        if part is None:
            return None
        matrix = entity["matrix"]
        task = field(part, "m_taskName")
        points.append([round(matrix[3], 4), round(matrix[7], 4), round(matrix[11], 4),
                       round(math.degrees(math.atan2(matrix[4], matrix[0])) % 360.0, 2),
                       round(real(field(part, "m_minDuration")), 3), round(real(field(part, "m_maxDuration")), 3),
                       task.value.decode("utf-8", "replace").strip("\0") if task is not None and task.value else "",
                       1 if number(field(part, "m_matchAlignment")) != 0 else 0])
    if not points or policy >= PATH_POLICIES:
        return None
    return {"policy": policy, "points": points}


def alias_tiers(roots) -> list[tuple[int, list]]:
    """@return The slices of an alias asset by their tag (stage QG5c5): [(TAG, [ASSET ID, ...]), ...] in the order of the asset, with no
    slice that has a schedule. The Darkness aliases have the tags 0 to 6 (the slices of the tiers dt1 to dt6: the pylon, the cores)."""
    out = []
    for root in roots:
        for node in walk(root):
            if node.name != F["Tags"]:
                continue
            for element in node.children:
                fields = {child.name: child for child in element.children}
                slices = []
                holder = fields.get(F["Slices"])
                for entry in holder.children if holder is not None else []:
                    parts = {child.name: child for child in entry.children}
                    asset = asset_id(parts.get(F["Slice"]))
                    schedule = parts.get(F["Schedule"])
                    if asset is not None and not (schedule is not None and schedule.children):
                        slices.append(asset)
                out.append((number(fields.get(F["Tag"])), slices))
    return out


class Reader:
    """Reads the recipes of the game copy one time: the placements, the encounter slices, and the member slices."""

    def __init__(self, sources: Sources, catalog: Catalog) -> None:
        self.sources = sources
        self.catalog = catalog
        self.streams: dict[str, list | None] = {}
        self.members: dict[str, dict] = {}
        self.counts = {"layers": 0, "placements": 0, "parentsRead": 0, "encounters": 0, "quest": 0, "notQuest": 0}
        self.assets: dict[str, tuple[bytes, int]] | None = None
        self.target_list: dict[str, str] | None = None
        # Stage QG9a: the objects of the mode root slices (a spawner of the root whose slice is not an encounter: the exit portal, the fog
        # wall, the checkpoint), each with its mode, its map, and its world place.
        self.mode_objects: list[dict] = []

    def targets(self) -> dict[str, str]:
        """@return The slices of the stationary targets with a live check (creature_data.read_target_list; stage QG5c5)."""
        if self.target_list is None:
            self.target_list = read_target_list()
        return self.target_list

    def own_asset(self, path: str) -> tuple[bytes, int] | None:
        """@return The asset id of a slice path (stage QG5c3: the body of a direct creature member is the member slice itself)."""
        if self.assets is None:
            self.assets = slice_assets(self.catalog)
        return self.assets.get(path)

    def stream(self, path: str):
        if path not in self.streams:
            self.streams[path] = self.sources.stream(path) if self.sources.has(path) else None
        return self.streams[path]

    def alias(self, path: str | None) -> tuple[str | None, list[str]]:
        """@return For an alias asset (stage QG5c5): the slice that a spawn of the alias makes, and each slice of the alias (sorted). The
        spawn takes the first slice of the highest tag [a rule of this project: the data has no field that names the tier of a placement;
        the Darkness events of this build are at the levels 64 to 66, and their direct members are dt6 slices (risen_corruption_entity_dt6);
        the highest tag of the pylon and of the cores is dt6]. No value: not an alias, or an alias with no slice."""
        if not path or not path.endswith(".aliasasset"):
            return None, []
        roots = self.stream(path)
        tiers = alias_tiers(roots) if roots is not None else []
        every = sorted({found for _, slices in tiers for asset in slices if (found := self.catalog.path(asset))})
        chosen = None
        for _, slices in sorted(tiers, key=lambda tier: tier[0]):
            found = next((self.catalog.path(asset) for asset in slices if self.catalog.path(asset)), None)
            chosen = found or chosen
        return chosen, every

    def mode_roots(self) -> dict[str, dict]:
        """Stage QG9a: @return The mode root slices of the solo modes whose level is the main world: the lower-case slice name of a placement
        (SlicePath of the map row, with "/") to its mode and its map. A solo mode is a row with IsSoloTrial 1 [data]; a separate level
        (CoatlicueName of a different terrain) is stage DG2 (its level data)."""
        modes_data = self.sources.read(MODES_TABLE)
        maps_data = self.sources.read(MAPS_TABLE)
        if modes_data is None or maps_data is None:
            raise Failure(f"the game copy has no {MODES_TABLE if modes_data is None else MAPS_TABLE}")
        modes, maps = Sheet(modes_data), Sheet(maps_data)
        for sheet, path, columns in ((modes, MODES_TABLE, ("GameModeId", "IsSoloTrial")),
                                     (maps, MAPS_TABLE, ("GameModeMapId", "GameModeId", "SlicePath", "CoatlicueName"))):
            missing = [column for column in columns if column not in sheet.columns]
            if missing:
                raise Failure(f"{path} has no column {', '.join(missing)}")
        solo = {modes.source(row, "GameModeId").strip().lower() for row in range(modes.row_count)
                if modes.source(row, "IsSoloTrial").strip() == "1"}
        roots: dict[str, dict] = {}
        for row in range(maps.row_count):
            mode = maps.source(row, "GameModeId").strip()
            terrain = maps.source(row, "CoatlicueName").replace("\\", "/").strip().lower()
            path = maps.source(row, "SlicePath").replace("\\", "/").strip().lower()
            if mode.lower() in solo and terrain == MAIN_TERRAIN and path:
                roots[path] = {"mode": mode, "map": maps.source(row, "GameModeMapId").strip()}
        return roots

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
                        radius = sheet.source(row, "DetectionVolumeSphereRadius").strip() \
                            if "DetectionVolumeSphereRadius" in sheet.columns else ""
                        try:
                            radius_value = float(radius) if radius else 0.0
                        except ValueError:
                            radius_value = 0.0
                        variations.setdefault((sheet.name.lower(), identifier.lower()), {
                            "gatherable": sheet.source(row, entry).strip(),
                            "tag": sheet.source(row, "InteractTag").strip() if "InteractTag" in sheet.columns else "",
                            "onlyOnQuest": sheet.source(row, "InteractOnlyOnQuest").strip() == "1"
                            if "InteractOnlyOnQuest" in sheet.columns else False,
                            # Stage QG5c3: the sphere of a proximity object ("Change Sphere Shape Radius" of the row; quest_objects.py).
                            "radius": radius_value if math.isfinite(radius_value) and radius_value > 0.0 else 0.0})
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
            controller = interact = proximity = character = place = False
            variant = table = entry = own_vitals = ""
            sphere = 0.0
            providers = 0
            nested = False
            entities = read_entities(roots)
            for info in entities.values():
                for component in info["components"]:
                    if component.type_id == MANAGER_CLASS:
                        nested = True
                    if component.type_id == PLACE_CLASS:
                        place = True
                    if component.type_id == VARIANT_PROVIDER_CLASS:
                        providers += 1
                        vitals = vitals or text(field(component, "m_vitalsTableRowId"))
                        category = category or text(field(component, "m_vitalsCategoryTableRowId"))
                        level = level or number(field(component, "m_vitalsLevel"))
                    if component.type_id == CHARACTER_CLASS:
                        character = True
                    if component.type_id == VITALS_CLASS:
                        own_vitals = own_vitals or text(field(component, "m_rowReference"))
                    if component.type_id == DETECTOR_CLASS and not sphere:
                        shape = detector_shape(component)
                        sphere = shape["radius"] if shape["shape"] == "sphere" else 0.0
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
            elif nested:
                # Stage QG5c4: a member that is an encounter itself (its own manager and stages: the four braziers of 99A_MSQ_10, each a
                # sub-encounter that spawns its destructible brazier): a nested encounter, a later stage.
                result["cause"] = "an encounter slice (a nested encounter with its own manager and stages)"
                result["nested"] = True
            elif vitals and body is not None:
                body_path = self.catalog.path(body[1])
                bodies = [body_path]
                if body[0] == "m_aliasAsset":
                    alias_roots = self.stream(body_path)
                    bodies = [self.catalog.path(asset) for asset, _ in (read_alias(alias_roots) if alias_roots is not None else [])]
                result = {"slice": path, "kind": "creature", "vitals": vitals, "category": category, "level": level,
                          "bodyField": "alias" if body[0] == "m_aliasAsset" else "slice", "body": body_path,
                          "bodySlices": sorted(path for path in bodies if path), "_asset": body[1]}
                if providers > 1:
                    # Stage QG5c4: a member slice with more than one creature spawner (the boat landings of the boat reinforcements:
                    # an officer and a navigator, each on a path of three waypoints). The fields above are of the first one.
                    result["parts"] = providers
                    # Stage QG5c5: each creature spawner of the group is one part (one creature of its own kind: creature_data.py writes
                    # the kind of the part N as "SLICE#N"), at its place in the member slice (the matrix of its entity), with its patrol
                    # path in the coordinates of the member slice where the data has one.
                    result["group"] = self.group_parts(entities)
            elif not vitals and character and own_vitals and self.own_asset(path) is not None:
                # Stage QG5c3: a direct creature member (the creature slice itself; its Vitals row; a GatherableController of the mining
                # or the skinning of its body is a part of the creature).
                result = {"slice": path, "kind": "creature", "vitals": own_vitals, "category": "", "level": 0, "bodyField": "slice",
                          "body": path, "bodySlices": [path], "direct": True, "_asset": self.own_asset(path)}
            elif not vitals and not character and own_vitals and place and not controller and path in self.targets() \
                    and self.own_asset(path) is not None:
                # Stage QG5c5: a direct stationary member: a slice of the list targets of creature_slices.json (the project rule of
                # stage QG5c1: each record has a live check) with a Vitals component (m_rowReference), a PositionInTheWorld component, no
                # CharacterComponent, and no GatherableController (the core shells of the Darkness monoliths, "Damage Carapace":
                # Darkness_Monolith_Shell_*, Elite). creature_data.py makes its kind with the rules of a stationary target.
                result = {"slice": path, "kind": "creature", "vitals": own_vitals, "category": "", "level": 0, "bodyField": "slice",
                          "body": path, "bodySlices": [path], "direct": True, "stationary": True, "_asset": self.own_asset(path)}
            elif controller and (interact or proximity):
                result = {"slice": path, "kind": "object", "variant": variant, "table": table, "entry": entry, "proximity": proximity,
                          "interact": interact}
                if proximity:
                    result["_sphere"] = sphere
            else:
                result["cause"] = ("a member with a vitals row and no creature slice" if vitals
                                   else "no vitals row and no start object (GatherableController and Interact)")
                lifetime = lifetime_of(entities)
                if lifetime is not None:
                    # Stage QG5c3: a member that ends itself (a visual of its stage that counts as destroyed at its end).
                    result["lifetime"] = lifetime
                    result["cause"] += f"; it ends itself {lifetime:g} s after its spawn (a delayed event of its init, DestroyGDEComponent)"
        self.members[path] = result
        return result

    def group_parts(self, entities: dict[int, dict]) -> list[dict]:
        """@return The parts of a group member (stage QG5c5): one for each entity with an AIVariantProviderComponent and a spawner of a
        creature slice or alias on the same entity, in the order of the slice. A part: its vitals row, its category row, its level, its
        body (the slice or the alias of its spawner and the slices of that alias), its place in the member slice ("offset" [x, y, z] and
        "yaw": the matrix of its entity), and "route" (its patrol path, part_route) where the data has one."""
        parts = []
        for info in entities.values():
            provider = next((component for component in info["components"] if component.type_id == VARIANT_PROVIDER_CLASS), None)
            if provider is None:
                continue
            body = None
            for component in info["components"]:
                if component.type_id in (STAGE_CLASS, SLICE_CLASS):
                    continue
                for name in ("m_sliceAsset", "m_aliasAsset"):
                    found = asset_id(field(component, name))
                    if body is None and found is not None and self.catalog.path(found) is not None:
                        body = (name, found)
            if body is None:
                continue
            body_path = self.catalog.path(body[1])
            bodies = [body_path]
            if body[0] == "m_aliasAsset":
                alias_roots = self.stream(body_path)
                bodies = [self.catalog.path(asset) for asset, _ in (read_alias(alias_roots) if alias_roots is not None else [])]
            matrix = info["matrix"]
            part = {"name": info["name"], "vitals": text(field(provider, "m_vitalsTableRowId")),
                    "category": text(field(provider, "m_vitalsCategoryTableRowId")), "level": number(field(provider, "m_vitalsLevel")),
                    "bodyField": "alias" if body[0] == "m_aliasAsset" else "slice", "body": body_path,
                    "bodySlices": sorted(path for path in bodies if path), "_asset": body[1],
                    "offset": [round(matrix[3], 4), round(matrix[7], 4), round(matrix[11], 4)],
                    "yaw": round(math.degrees(math.atan2(matrix[4], matrix[0])), 3)}
            route = part_route(entities, info)
            if route is not None:
                part["route"] = route
            parts.append(part)
        return parts

    def encounter(self, path: str, roots, matrix: tuple[float, ...]) -> dict:
        """@return The recipe of one encounter slice at a world matrix (step 3)."""
        entities = read_entities(roots)
        recipe: dict = {"slice": path, "unsupported": []}
        stages: dict[int, dict] = {}
        detectors: dict[int, dict] = {}
        tracker_entity = None
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
                    tracker_entity = identifier
                elif kind == DETECTOR_CLASS:
                    # Stage QG5c3: the shape of each detector (a sphere, a cylinder, an axis box) and its entity.
                    shape = detector_shape(component)
                    shape["filter"] = text(field(component, "m_strFilter"))
                    detectors.setdefault(identifier, []).append(shape)
                elif kind == REWARDS_CLASS:
                    recipe["successEvent"] = text(field(component, "m_universalSuccessGameEventId"))
        if "root" not in recipe:
            raise Failure(f"{path}: the encounter slice has no EncounterManagerComponent")
        # The detector of the participants (stage QG5c3): the detection volumes of the entity of the ParticipantTracker (the Bathory hall has
        # its box there and an other sphere elsewhere). 29 of the 65 encounter slices of the build have two volumes there (the master
        # template: a sphere of 30 m and a second one of 12 to 50 m), and no field names one of them: a character in one of them is in the
        # detector (a rule of this project). "radius" is the largest sphere or cylinder (the field of format 1 for a backend of before).
        place = place_of(matrix)
        recipe.update(position=place["position"], rotation=place["rotation"], yaw=place["yaw"])
        holder = tracker_entity if tracker_entity in detectors else recipe["root"] if recipe["root"] in detectors \
            else next(iter(detectors), None)
        shapes = detectors.get(holder) if holder is not None else None
        if shapes is not None:
            # Stage QG5c4: a shape whose m_shapeEntityID names an other entity of the slice is at the place of that entity (its offset from
            # the root in world axes, plus m_center); "shapeEntity" names it, or is "missing" for an id that no entity of the slice has
            # (the eight tarot towers: the id is in no slice of the game copy; encounters.py adds their trigger, see below).
            for shape in shapes:
                entity = shape.pop("_shapeEntity", None)
                if entity is None or entity == holder:
                    continue
                if entity not in entities:
                    shape["shapeEntity"] = "missing"
                    continue
                at = place_of(multiply(matrix, entities[entity]["matrix"]))["position"]
                shape["center"] = [round(at[axis] - place["position"][axis] + shape["center"][axis], 4) for axis in range(3)]
                shape["shapeEntity"] = entities[entity]["name"]
            radii = [shape.get("radius", 0.0) for shape in shapes if shape["shape"] in ("sphere", "cylinder")]
            recipe["detector"] = {"radius": max(radii, default=0.0), "filter": shapes[0]["filter"],
                                  "sphere": all(shape["shape"] == "sphere" for shape in shapes),
                                  "shapes": [{key: value for key, value in shape.items() if key != "filter"} for shape in shapes]}
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
                # Stage QG5c5: a spawn of an alias makes a slice of the alias (Reader.alias: the highest tag); the spawn keeps the alias
                # ("alias") for an objective that tracks it. An entry with a slice and an alias takes the alias [a rule of this project
                # from the data: the core stage of the monolith sm_01 names the shell of the stage before it and the core alias, and
                # its objective tracks the core alias].
                alias_path = asset_path(self.catalog, field(entry, "m_aliasAsset"))
                chosen, _ = self.alias(alias_path)
                if chosen is not None:
                    member_path = chosen
                locations = []
                for location in refs(field(entry, "m_spawnLocations")):
                    local = entities.get(location, {}).get("matrix")
                    if local is None:
                        recipe["unsupported"].append(f"the spawn location {location} of the stage {stage['name']} is not an entity")
                        continue
                    locations.append(place_of(multiply(matrix, local)))
                spawn = {"member": member_path or "", "delay": number(field(entry, "m_spawnDelay")), "count": number(field(entry, "m_count")),
                         "locations": locations}
                if chosen is not None:
                    spawn["alias"] = alias_path
                if member_path is None:
                    recipe["unsupported"].append(f"a spawn of the stage {stage['name']} names no slice of the catalog")
                spawns.append(spawn)
            stage["spawns"] = spawns
            for objective in [stage["objective"], *stage["objective"].get("more", [])]:
                tracked = objective.pop("_tracked", None)
                if tracked is not None:
                    objective["trackedSlice"] = asset_path(self.catalog, tracked[0])
                    objective["trackedAlias"] = asset_path(self.catalog, tracked[1])
                    if objective["trackedAlias"] and not objective["trackedSlice"]:
                        # Stage QG5c5: the slices of a tracked alias (a part of a group counts when its body is one of them: the 01_Attack
                        # of the monoliths tracks alias_corrupted_entity_and_impaler_elite, the bodies of the parts of its subwave).
                        objective["trackedSlices"] = self.alias(objective["trackedAlias"])[1]
            # Stage QG5c5: the slice or the alias of a DestroySliceAction and the slices of that alias.
            for action in stage["startActions"] + stage["endActions"]:
                assets = action.pop("_assets", None)
                if assets is not None:
                    action["slice"] = self.catalog.path(assets[0]) if assets[0] is not None else None
                    action["alias"] = self.catalog.path(assets[1]) if assets[1] is not None else None
                    action["aliasSlices"] = self.alias(action["alias"])[1]
            recipe["stages"].append(stage)
        recipe["stageOrder"] = [index[identifier] for identifier in recipe["stageOrder"] if identifier in index]
        # Stage QG5c4: the trigger of an EntityActiveStateChangedObjective (m_targetEntityId, m_completeOnDeactivated): the detection
        # volumes of the slice whose event component sends that change to the target when a target enters (the Trigger_Start of the tarot
        # towers: a sphere of 20 m, PlayerOnly, that deactivates itself), each with its shapes at its place in the world; "changedBy" names
        # the gatherables of the slice whose gather reaches the change (the horn of 09A_M5).
        for stage in recipe["stages"]:
            objective = stage["objective"]
            if objective["type"] != "EntityActiveStateChangedObjective":
                continue
            target = objective.pop("target", 0)
            wanted = {"deactivated": EVENT_DEACTIVATE, "activated": EVENT_ACTIVATE}.get(objective["completeOn"])
            senders = state_senders(entities, target, wanted) if wanted is not None else {"volumes": [], "gathers": []}
            objective["target"] = f"{target:016X}"
            objective["targetName"] = entities.get(target, {}).get("name", "")
            triggers = []
            for _, volume, dead in senders["volumes"]:
                shapes = [shape for shape in detectors.get(volume, []) if shape["shape"] != "none"]
                if not shapes or volume not in entities:
                    continue
                at = place_of(multiply(matrix, entities[volume]["matrix"]))
                triggers.append({"entity": entities[volume]["name"], "position": at["position"], "filter": shapes[0]["filter"],
                                 "deadTargets": dead,
                                 "shapes": [{key: value for key, value in shape.items() if key not in ("filter", "_shapeEntity")}
                                            for shape in shapes]})
            objective["triggers"] = triggers
            objective["changedBy"] = senders["gathers"]
            # Stage QG5c4 (a rule of this project): the detector of the participants of the tarot towers is a box at the ground whose
            # shape entity is not in the game data, and the fight is at the top of the tower, 120 m up, where the trigger of the setup is:
            # the trigger shapes are shapes of the detector too ("fromTrigger"), at their offset from the root in world axes.
            detector = recipe.get("detector")
            if detector and any(shape.get("shapeEntity") == "missing" for shape in detector["shapes"]):
                for trigger in triggers:
                    for shape in trigger["shapes"]:
                        center = [round(trigger["position"][axis] - place["position"][axis] + shape["center"][axis], 4) for axis in range(3)]
                        detector["shapes"].append({**shape, "center": center, "fromTrigger": trigger["entity"]})
        return recipe

    def read(self) -> tuple[list[dict], dict]:
        """@return The quest recipes of the main world (sorted by their key) and the counts."""
        events = self.task_events()
        recipes: dict[str, dict] = {}
        encounter_cache: dict[str, bool] = {}
        mode_roots = self.mode_roots()
        self.counts["modeRoots"] = 0
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
                # Stage QG9a: the placement of a mode root slice of a solo mode of the main world level (its encounters are the content of
                # an instance of the mode, never of the main world).
                mode_root = mode_roots.get(name.replace("\\", "/"))
                if mode_root is None and not any(part in name for part in PARENT_TEXTS):
                    continue
                path = spawner_path(placement, self.catalog, self.sources)
                if path is None:
                    continue
                roots = self.stream(path)
                if roots is None:
                    continue
                self.counts["parentsRead"] += 1
                self.counts["modeRoots"] += 1 if mode_root is not None else 0
                world = placement_matrix(placement.get("rotation") or {}, placement.get("worldPosition") or {"x": 0, "y": 0, "z": 0})
                for spawner_entity, info in read_entities(roots).items():
                    for component in info["components"]:
                        # Stage QG5c4: the SliceComponent of the root entity of the slice repeats the entities of the slice in its list
                        # "Entities"; a look into it found the fields of a spawner of an other entity (the twin spawners of stage QG5c3:
                        # Gazsala, the Bathory hall, the rotating bookcase). The entities of the list are read as entities.
                        if component.type_id in (STAGE_CLASS, SLICE_CLASS):
                            continue
                        target = asset_path(self.catalog, field(component, "m_sliceAsset"))
                        if target is None:
                            continue
                        if target not in encounter_cache:
                            target_roots = self.stream(target)
                            encounter_cache[target] = target_roots is not None and any(
                                node.type_id == MANAGER_CLASS for root in target_roots for node in walk(root))
                        if not encounter_cache[target]:
                            object_key = f"{placement.get('id', '')}/{spawner_entity}"
                            if mode_root is not None and all(entry["key"] != object_key for entry in self.mode_objects):
                                # Stage QG9a: an object of the instance (the exit portal, the fog wall, the checkpoint, the quest
                                # gatherable): its slice and its world place (one entry for each spawner entity).
                                place = place_of(multiply(world, info["matrix"]))
                                self.mode_objects.append({"mode": mode_root["mode"], "map": mode_root["map"],
                                                          "key": object_key, "slice": target,
                                                          "spawner": info["name"], **place,
                                                          "onInit": number(field(component, "m_spawnOnInitialize")) != 0})
                            continue
                        key = f"{placement.get('id', '')}/{spawner_entity}"
                        if key in recipes:
                            continue
                        recipe = self.encounter(target, self.stream(target), multiply(world, info["matrix"]))
                        recipe["key"] = key
                        if mode_root is not None:
                            # Stage QG9a: an instance recipe: the content of an instance of its mode (the instance system of the backend
                            # starts it in the space of the instance). The daily alternative is marked (its selection is in the mode
                            # script, which the client does not have).
                            recipe["mode"] = mode_root["mode"]
                            recipe["map"] = mode_root["map"]
                            recipe["daily"] = DAILY_TEXT in target.rsplit("/", 1)[-1]
                        recipe["layer"] = layer[len(REGIONS_FOLDER):-len(".capitals.json")]
                        recipe["parentSlice"] = path
                        recipe["parent"] = spawner_rules(info, component)
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
            if member.get("proximity"):
                # Stage QG5c3: the sphere of a proximity object: DetectionVolumeSphereRadius of its variation row, else the sphere of its
                # DetectionVolumeComponent (the rule of quest_objects.py, stage QG5a).
                member["radius"] = round((row or {}).get("radius", 0.0) or member.get("_sphere", 0.0), 4)
                if not member["radius"] > 0.0:
                    member["cause"] = "the proximity object has no sphere (no DetectionVolumeSphereRadius and no sphere of the slice)"
        for recipe in recipes.values():
            quest_events = {recipe.get("successEvent", "").lower()}
            for stage in recipe["stages"]:
                for spawn in stage["spawns"]:
                    member = self.members.get(spawn["member"]) if spawn["member"] else None
                    # Stage QG5c3: an area trigger (a proximity object with no Interact component) has the shared row PressurePlate_Ancient:
                    # its event names no encounter (one task of the instance QuestYonas names it), and it makes no quest recipe.
                    if member is not None and member["kind"] == "object" and member.get("event") and member.get("interact", True):
                        quest_events.add(member["event"].lower())
            quest_events.discard("")
            recipe["questEvents"] = sorted(event for event in quest_events if event in events)
            # Stage QG9a: an instance recipe is the content of its mode, also with no quest event (the kill of its boss is the quest task).
            recipe["quest"] = bool(recipe["questEvents"]) or bool(recipe.get("mode"))
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
