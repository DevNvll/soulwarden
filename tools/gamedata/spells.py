#!/usr/bin/env python3
"""Write the spell entity data of the game as one JSON file (stage 3.3b-S2a; format 2: stage 3.3b-S2b).

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

Format 2 (stage 3.3b-S2b: the Pylon of the ice gauntlet, the trap of the musket, the Beacon of the life staff) adds the
values of the components that a spell entity of the backend uses:

  each delayed event   its owner entity (the entity of the DelayedEventComponent), m_startDelayOnInit, and
                       m_startDelayOnTrigger: an event that starts on a trigger runs only after its owner is triggered
                       (the death chain of the Pylon: live check of stage 3.3b-S2b)
  positionSlot         the slot of a PositionInTheWorld component (state type 13): the place of a slice with no Spell
                       component (the trap: live check of stage 3.3b-S2b)
  vitals               [slot, m_rowReference, the target of m_onDeathEvent] of a Vitals component (the health of a summon)
  autoSpell            [m_spellName, m_lifetime, m_lifetimeOnHit, m_initialCastDelay and m_cooldown of
                       m_defaultRateOfFire, the target of m_lifetimeExpireEvent, the target of m_castEvent,
                       m_excludeTargetNWTags] of an AutoSpellComponent (the shots of the Pylon)
  volumes              [entity, radius of its sphere, the time from the start at which it is active (0: at the start;
                       the earliest activation at the init of it or of a parent; -1: never)] of each
                       DetectionVolumeComponent (the trigger of the trap, the target area of the Pylon)
  offenses             [entity, row of m_damageData, radius of its sphere] of each MomentaryOffenseComponent
  detectionEvents      [target entity, event type] of the events of a DetectionVolumeEventComponent
  timelines            [owner entity, destroy time]: for each TimelineEntry of a TimelineComponent, the start of the first
                       clip of the layer whose entity has a DestroyGDEComponent (the timeline asset, LayerEntityAssignments;
                       the trap: 0.508 s after its trigger), -1 for none
  "vitals" (top)       the rows of the base vitals tables (VitalsBaseData) that the slices name: [id, HealthBaseMax,
                       HealthOverridePercentSpellOwnerMaxHealth, PhysicalArmorRatingOverride, ElementalArmorRatingOverride,
                       GearScoreOverride]; a column that a table of the game does not have is 0 (a count)

A spell row whose prefab names no slice of the game copy (a path that the catalog or the pak files do not have) is left
out, with a count. The tool stops with status 1 and writes no file when a file that it needs has no source (also a
timeline asset of a slice), when a spell table has no column SpellID or SpellPrefabPath, and when a slice of a spell has
no root entity, two components with one slot, or a delayed event with no delay or no target.

A prefab slice with no Spell component and no PositionInTheWorld component (a waypoint or a rally point of an encounter)
is not a spell entity: the tool leaves it out with a count.

Output: {"format": 2, "counts": {"spells": N, "slices": N, "spellsWithNoSlice": N, "slicesWithNoSpellComponent": N,
                                 "vitals": N, "vitalsRowsNotInBaseTables": N, "vitalsCellsOfAbsentColumns": N},
"slices": [{"path": text, "asset": "UUID", "sub": N, "root": "16 hex digits", "interestRadius": meters,
            "spellSlot": N (0: none), "positionSlot": N (0: none), "slots": [[slot, class name, state type], ...],
            "events": [[slot, delay seconds, event type, target name, offense row, destroys, owner name, onInit,
                        onTrigger], ...],
            "components": [["entity", "component", "name/class"], ...], "vitals": [...] or null,
            "autoSpell": [...] or null, "volumes": [...], "offenses": [...], "detectionEvents": [...],
            "timelines": [...]}, ...],
"spells": [[id, slice index], ...], "vitals": [[id, ...], ...]}

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
WANTED = (TABLES_FOLDER, "slices/", "libs/timeline/", CATALOG_PATH)
VITALS_TYPE = "VitalsBaseData"
# The columns of a vitals row of a spell entity (format 2): the health, the part of the health of the caster, the armor, the
# gear score.
VITALS_COLUMNS = ("VitalsID", "HealthBaseMax", "HealthOverridePercentSpellOwnerMaxHealth", "PhysicalArmorRatingOverride",
                  "ElementalArmorRatingOverride", "GearScoreOverride")
SUB_ID = 2
TEXT_TYPE = uuid.UUID("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")
SLICE_COMPONENT_TYPE = uuid.UUID("afd304e4-1773-47c8-855a-8b622398934f")

# The component classes of a spell slice (the class id of the slice, the name, and the state type of the supported client
# build: research abilities-shared.md Q2 and K5; constructors 0x67185C0, 0x360B3C0, 0x405C370, 0x3066370). A class that
# is not here has the type 0.
COMPONENT_CLASSES = {
    "2412ac34-f256-4124-9035-6c73124dcfb6": ("Spell", 2912),
    "6c2fc842-f451-4b45-9d7d-a6ee954df025": ("PositionInTheWorld", 13),
    "711f015c-c03b-4735-8c69-b21a224ef2dd": ("Timeline", 1647),
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
# Format 2 (stage 3.3b-S2b): the components of the Pylon, the trap, and the Beacon (class ids of the slices).
VOLUME_CLASS = uuid.UUID("41bc3bb3-ef30-41e4-b751-12c4eab97c0e")       # DetectionVolumeComponent
AUTO_SPELL_CLASS = uuid.UUID("f084d5d2-d54e-416a-a8a4-7fe339801113")   # AutoSpellComponent
TIMELINE_CLASS = uuid.UUID("711f015c-c03b-4735-8c69-b21a224ef2dd")     # TimelineComponent (state type 1647)
TIMELINE_ENTRY_CLASS = uuid.UUID("e14cf7d7-9f3b-4aae-a320-a284dffd02ee")  # TimelineEntry of the list m_timelines
LAYER_ASSIGNMENT_CLASS = uuid.UUID("2acd7f8c-9f1a-41f3-b8b9-a64d2fc3bb09")  # LayerEntityAssignment: a layer and its entity
LAYER_CLASS = uuid.UUID("f99a21ab-f403-4b90-ad52-25ee26c36f25")         # TimelineLayer of a timeline asset
SPHERE_CLASS = uuid.UUID("7f2ef312-4089-4582-89c5-5d4156daa7fb")        # QueryShapeSphere
# The event type of an activation (m_type of m_eventData): spell_event::kActivate of the backend.
spell_event_activate = 2
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
ACTIVE_FIELD = field_id("IsRuntimeActive")
START_ON_INIT_FIELD = field_id("m_startDelayOnInit")
START_ON_TRIGGER_FIELD = field_id("m_startDelayOnTrigger")
RADIUS_FIELD = field_id("m_Radius")
ROW_REFERENCE_FIELD = field_id("m_rowReference")
DEATH_EVENT_FIELD = field_id("m_onDeathEvent")
SPELL_NAME_FIELD = field_id("m_spellName")
LIFETIME_FIELD = field_id("m_lifetime")
LIFETIME_ON_HIT_FIELD = field_id("m_lifetimeOnHit")
DEFAULT_RATE_FIELD = field_id("m_defaultRateOfFire")
INITIAL_DELAY_FIELD = field_id("m_initialCastDelay")
COOLDOWN_FIELD = field_id("m_cooldown")
LIFETIME_EXPIRE_FIELD = field_id("m_lifetimeExpireEvent")
CAST_EVENT_FIELD = field_id("m_castEvent")
DETECTION_EVENT_FIELD = field_id("m_event")
TIMELINE_OWNER_FIELD = field_id("TimelineOwnerId")
TIMELINE_ASSET_FIELD = field_id("TimelineAsset")
LAYER_ID_FIELD = field_id("LayerId")
START_TIME_FIELD = field_id("StartTime")
EXCLUDE_TAGS_FIELD = field_id("m_excludeTargetNWTags")


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


def entity_fields(element) -> tuple[int | None, str, bool, object | None]:
    """@return The id, the name, the start state (IsRuntimeActive), and the component list of an entity element."""
    identifier = next((id_value(child) for child in element.children if child.name == ID_FIELD), None)
    name = next((text_of(child) for child in element.children if child.name == NAME_FIELD and child.type_id == TEXT_TYPE), "")
    active = next((bool(number_of(child)) for child in element.children if child.name == ACTIVE_FIELD), True)
    listed = next((child for child in element.children if child.name == COMPONENTS_FIELD), None)
    return identifier, name, active, listed


def event_target(event) -> int | None:
    """@return The target entity of an event (m_eventData, m_onDeathEvent, m_lifetimeExpireEvent): its m_entityRef."""
    reference = deep(event, ENTITY_REF_FIELD) if event is not None else None
    target = id_value(deep(reference, ID_FIELD)) if reference is not None else None
    if target is None and reference is not None:
        target = next((id_value(child) for child in walk(reference) if child is not reference and id_value(child) is not None), None)
    return target


def sphere_radius(component) -> float:
    """@return The radius of the first QueryShapeSphere of a component (a detection volume, an offense), or 0."""
    for child in walk(component):
        if child.type_id == SPHERE_CLASS:
            return real_of(find_field(child, RADIUS_FIELD))
    return 0.0


def asset_hint(element) -> str:
    """@return The path hint of an asset field (the bytes after the asset id, the sub id, and the type: a lower-case path)."""
    if element is None or element.value is None:
        return ""
    data = element.value
    for start in range(len(data)):
        if data[start:start + 5] == b"libs/" or data[start:start + 7] == b"slices/":
            return data[start:].split(b"\0")[0].decode("utf-8", "replace").lower()
    return ""


def timeline_destroys(sources: Sources, path: str) -> dict[str, float]:
    """@return For each layer id of a timeline asset, the start time of its first clip (seconds)."""
    roots = sources.stream(path) if path and sources.has(path) else None
    if roots is None:
        raise Failure(f"the timeline {path} of a spell slice has no source")
    starts: dict[str, float] = {}
    for root in roots:
        for layer in walk(root):
            if layer.type_id != LAYER_CLASS:
                continue
            identifier = next((child for child in layer.children if child.name == ID_FIELD), None)
            if identifier is None or identifier.value is None or len(identifier.value) != 16:
                continue
            times = [real_of(find_field(child, START_TIME_FIELD)) for child in walk(layer) if find_field(child, START_TIME_FIELD) is not None
                     and child.name != START_TIME_FIELD]
            if times:
                starts[str(uuid.UUID(bytes=identifier.value))] = round(min(times), 4)
    return starts


def read_spell_slice(path: str, asset: tuple[bytes, int], roots: list, sources: Sources) -> dict:
    """@return The data of one spell slice: its root, its slots, its delayed events, its faceted components, and the values of
    the components that a spell entity of the backend uses (format 2: the vitals, the AutoSpell, the detection volumes, the
    offenses, the detection events, the timelines). An empty value: not a spell entity (no Spell component and no
    PositionInTheWorld component)."""
    entities: dict[int, dict] = {}
    slots: list[list] = []
    components: list[list] = []
    raw_events: list[tuple] = []
    vitals: list = []
    auto_spells: list[dict] = []
    volumes: list[tuple] = []
    offenses: list[tuple] = []
    detection_events: list[tuple] = []
    timelines: list[tuple] = []
    radius = 0.0
    for root in roots:
        for element in walk(root):
            if element.name == SPAWN_RADIUS_FIELD and radius == 0.0:
                radius = real_of(element)
            if element.type_id != ENTITY_TYPE:
                continue
            entity_id, name, active, listed = entity_fields(element)
            # The outer entity of the file has the slice component: it is not an entity of an instance.
            if listed is None or entity_id is None or any(c.type_id == SLICE_COMPONENT_TYPE for c in listed.children):
                continue
            entry = {"name": name, "parent": None, "destroys": False, "offense": "", "active": active}
            for component in listed.children:
                if find_field(component, LOCAL_TRANSFORM_FIELD) is not None:
                    entry["parent"] = id_value(find_field(component, PARENT_FIELD))
                class_name, state_type = COMPONENT_CLASSES.get(str(component.type_id), ("", 0))
                if component.type_id == DESTROY_CLASS:
                    entry["destroys"] = True
                if component.type_id == OFFENSE_CLASS:
                    data = find_field(component, DAMAGE_DATA_FIELD)
                    row = text_of(deep(data, DAMAGE_ENTRY_FIELD) if data is not None else None)
                    entry["offense"] = entry["offense"] or row
                    offenses.append((entity_id, row, sphere_radius(component)))
                if component.type_id == VOLUME_CLASS:
                    volumes.append((entity_id, sphere_radius(component)))
                if component.type_id == AUTO_SPELL_CLASS:
                    rate = deep(component, DEFAULT_RATE_FIELD)
                    # The times of the rule of a summon are required 32-bit floats (code review of stage 3.3b-S2b, finding
                    # F12): a field with a different size or no field stops the tool, not a life of 0 at the creation.
                    what = f"the AutoSpell of {name} in the slice {path}"
                    if rate is None:
                        raise Failure(f"{what} has no m_defaultRate")
                    auto_spells.append({"spell": text_of(deep(component, SPELL_NAME_FIELD)),
                                        "lifetime": round(required_real(deep(component, LIFETIME_FIELD), f"m_lifetime of {what}"), 4),
                                        "lifetimeOnHit": round(required_real(deep(component, LIFETIME_ON_HIT_FIELD),
                                                                             f"m_lifetimeOnHit of {what}"), 4),
                                        "initialDelay": round(required_real(deep(rate, INITIAL_DELAY_FIELD), f"m_initialCastDelay of {what}"), 4),
                                        "cooldown": round(required_real(deep(rate, COOLDOWN_FIELD), f"m_cooldown of {what}"), 4),
                                        "expire": event_target(deep(component, LIFETIME_EXPIRE_FIELD)),
                                        "cast": event_target(deep(component, CAST_EVENT_FIELD)),
                                        "exclude": sorted(text_of(child) for child in walk(deep(component, EXCLUDE_TAGS_FIELD) or component)
                                                          if child.type_id == TEXT_TYPE and child.name != EXCLUDE_TAGS_FIELD
                                                          and deep(component, EXCLUDE_TAGS_FIELD) is not None and text_of(child))})
                if class_name == "Vitals":
                    vitals.append((text_of(deep(component, ROW_REFERENCE_FIELD)), event_target(deep(component, DEATH_EVENT_FIELD))))
                if class_name == "DetectionVolumeEvent":
                    for child in walk(component):
                        if child.name == DETECTION_EVENT_FIELD:
                            target = event_target(child)
                            if target is not None:
                                detection_events.append((target, number_of(deep(child, EVENT_TYPE_FIELD))))
                if component.type_id == TIMELINE_CLASS:
                    for timeline in walk(component):
                        if timeline.type_id != TIMELINE_ENTRY_CLASS:
                            continue
                        owner = id_value(deep(timeline, TIMELINE_OWNER_FIELD))
                        hint = asset_hint(deep(timeline, TIMELINE_ASSET_FIELD))
                        layers = []
                        for child in walk(timeline):
                            if child.type_id == LAYER_ASSIGNMENT_CLASS:
                                layer = deep(child, LAYER_ID_FIELD)
                                layers.append((id_value(deep(child, ID_FIELD)) if deep(child, ID_FIELD) is not None else None,
                                               str(uuid.UUID(bytes=layer.value)) if layer is not None and layer.value and len(layer.value) == 16 else ""))
                        timelines.append((owner, hint, layers))
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
                    target = event_target(event)
                    if delay is None or event is None or target is None:
                        raise Failure(f"{path}: a delayed event of slot {slot} has no delay or no target")
                    seconds = required_real(delay, f"{path}: the delay of the delayed event of slot {slot}")
                    init = find_field(component, START_ON_INIT_FIELD)
                    trigger = find_field(component, START_ON_TRIGGER_FIELD)
                    raw_events.append((slot, round(seconds, 4), number_of(deep(event, EVENT_TYPE_FIELD)), target, entity_id,
                                       bool(number_of(init)) if init is not None else True, bool(number_of(trigger)) if trigger is not None else False))
            entities[entity_id] = entry
    roots_found = [identifier for identifier, entry in entities.items() if entry["parent"] == NO_ENTITY]
    if not roots_found:
        raise Failure(f"{path}: the slice has no root entity")
    slots.sort()
    numbers = [slot[0] for slot in slots]
    if len(set(numbers)) != len(numbers):
        raise Failure(f"{path}: two components of the slice have the same slot")
    spell_slots = [slot[0] for slot in slots if slot[1] == "Spell"]
    position_slots = [slot[0] for slot in slots if slot[1] == "PositionInTheWorld"]
    if not spell_slots and not position_slots:
        return {}  # Not a spell entity (a waypoint, a rally point of an encounter): the caller leaves it out.

    def named(identifier) -> str:
        entry = entities.get(identifier)
        return entry["name"] if entry else (f"{identifier:016X}" if identifier is not None else "")

    def ancestors(identifier):
        seen = set()
        while identifier is not None and identifier in entities and identifier not in seen:
            seen.add(identifier)
            yield identifier
            identifier = entities[identifier]["parent"]

    def active_after(identifier) -> float:
        """The time from the start at which the entity is active: 0 when it and its parents are active at the start, else
        the earliest activation at the init of it or of a parent (-1: never)."""
        chain = list(ancestors(identifier))
        if all(entities[entity]["active"] for entity in chain):
            return 0.0
        times = [delay for _, delay, kind, target, _, init, _ in raw_events if kind == spell_event_activate and init and target in chain]
        return round(max(min(times), 0.0), 4) if times else -1.0

    resolved = []
    for slot, delay, kind, target, owner, init, trigger in sorted(raw_events):
        entry = entities.get(target)
        resolved.append([slot, delay, kind, entry["name"] if entry else f"{target:016X}", entry["offense"] if entry else "",
                         bool(entry and entry["destroys"]), named(owner), init, trigger])
    timeline_rows = []
    for owner, hint, layers in timelines:
        starts = timeline_destroys(sources, hint) if hint else {}
        destroy = -1.0
        for entity, layer in layers:
            if entity in entities and entities[entity]["destroys"] and layer in starts:
                destroy = starts[layer] if destroy < 0 else min(destroy, starts[layer])
        timeline_rows.append([named(owner), destroy])
    row, death = vitals[0] if vitals else ("", None)
    auto = auto_spells[0] if auto_spells else None
    if auto is not None:
        auto = [auto["spell"], auto["lifetime"], auto["lifetimeOnHit"], auto["initialDelay"], auto["cooldown"], named(auto["expire"]),
                named(auto["cast"]), auto["exclude"]]
    vitals_slots = [slot[0] for slot in slots if slot[1] == "Vitals"]
    # A Vitals component takes damage only with its vitals row (code review of stage 1.1b, finding F2): a component with no
    # row reference stops the tool before the file changes (the backend refuses a Vitals slot with no vitals row too).
    if vitals_slots and not row:
        raise Failure(f"the spell slice {path} has a Vitals component with no row reference (m_rowReference)")
    return {"path": path, "asset": str(uuid.UUID(bytes=asset[0])).upper(), "sub": asset[1], "root": f"{roots_found[-1]:016X}",
            "interestRadius": round(radius, 3), "spellSlot": spell_slots[0] if spell_slots else 0,
            "positionSlot": position_slots[0] if position_slots else 0, "slots": slots, "events": resolved,
            "components": components,
            "vitals": [vitals_slots[0], row, named(death)] if vitals_slots and row else None,
            "autoSpell": auto,
            "volumes": [[named(entity), round(radius_of, 4), active_after(entity)] for entity, radius_of in volumes],
            "offenses": [[named(entity), row_of, round(radius_of, 4)] for entity, row_of, radius_of in offenses],
            "detectionEvents": [[named(target), kind] for target, kind in detection_events],
            "timelines": timeline_rows}


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
                entry = read_spell_slice(path, asset, roots, sources)
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
    named = {entry["vitals"][1].lower(): entry["vitals"][1] for entry in slices if entry["vitals"]}
    vitals, no_row, no_column = read_vitals(sources, set(named))
    # A vitals row that the base vitals tables do not have (code review of stage 3.3b-S2b, finding F8): the rows of the list
    # VITALS_ROWS_NOT_IN_BASE_TABLES go to the member vitalsNotInData (the backend makes no summon and no trap of such a slice);
    # a different row stops the tool.
    found = {row[0].lower() for row in vitals}
    elsewhere = sorted(named[key] for key in named if key not in found)
    unknown = [row for row in elsewhere if row.lower() not in {name.lower() for name in VITALS_ROWS_NOT_IN_BASE_TABLES}]
    if unknown:
        raise Failure(f"a spell slice names the vitals row {unknown[0]}, which no base vitals table has")
    return {"format": 2, "counts": {"spells": len(spells), "slices": len(slices), "spellsWithNoSlice": no_slice,
                                    "slicesWithNoSpellComponent": no_spell_component, "vitals": len(vitals),
                                    "vitalsRowsNotInBaseTables": no_row, "vitalsCellsOfAbsentColumns": no_column},
            "slices": slices, "spells": sorted(spells.values()), "vitals": vitals, "vitalsNotInData": elsewhere}


# The vitals rows of the spell slices of the game copy that are not in a base vitals table (VitalsBaseData): a row of a creature
# table (the seed of the Goth elephant, ai_spell_goth_seed_aoe). Code review of stage 3.3b-S2b, finding F8.
VITALS_ROWS_NOT_IN_BASE_TABLES = ("CK_Elephant_H_Goth_Blossom",)


def read_vitals(sources: Sources, wanted: set[str]) -> tuple[list[list], int, int]:
    """@return The rows of the base vitals tables (VitalsBaseData) that a spell slice names (m_rowReference of its Vitals
    component): [id, HealthBaseMax, HealthOverridePercentSpellOwnerMaxHealth, PhysicalArmorRatingOverride,
    ElementalArmorRatingOverride, GearScoreOverride], the count of the wanted rows that no such table has, and the count of the
    cells of absent columns."""
    rows: dict[str, list] = {}
    missing_columns = [0]
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        if "vitals" not in path.rsplit("/", 1)[-1]:
            continue
        data = sources.read(path)
        if data is None or sheet_type(data) != VITALS_TYPE:
            continue
        sheet = Sheet(data)
        if "VitalsID" not in sheet.columns:
            continue
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "VitalsID")).strip()
            if identifier.lower() not in wanted or identifier.lower() in rows:
                continue
            # Some tables of the game have fewer columns (BaseVitals_Catacombs has no HealthBaseMax): a column that the table
            # does not have is 0 (the empty cell), with a count.
            values = []
            for column in VITALS_COLUMNS[1:]:
                if column not in sheet.columns:
                    missing_columns[0] += 1
                    values.append(0.0)
                    continue
                # The source text of the cell: some tables have these columns as text columns.
                text = str(sheet.source(row, column)).strip()
                try:
                    value = float(text) if text else 0.0
                except ValueError as error:
                    raise Failure(f"the vitals row {identifier} has a {column} that is not a number") from error
                if not math.isfinite(value):
                    raise Failure(f"the vitals row {identifier} has a {column} that is not a number")
                values.append(round(value, 6))
            rows[identifier.lower()] = [identifier, *values]
    sources.check_complete()
    # A row of a different table type (a creature of a dungeon) is not in the list: the backend gives its spell no health.
    return sorted(rows.values()), len(wanted - set(rows)), missing_columns[0]


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
