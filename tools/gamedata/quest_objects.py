#!/usr/bin/env python3
"""Write the destructible quest objects of the world (stage 1.8e, docs/systems/quests.md): one JSON file.

The backend reads GAMEDATA_DIR/quest-objects.json at its start. Without the file no quest object can be destroyed (a strike changes
nothing), and the rest of the backend works.

A destructible quest object is a placement of a layer whose slice has the variation table Gatherable_Quest_Damageable (the corruption
nodules of 06_survivors: IO_Custom_MSQ_CorruptedNodule_A to _D; the monolith of 06_demonstorm2: Master_Quest_Damageable_Instanced with the
variant MSQ_Beach_Damned_Monolith). The client makes it itself from its level data (a managed actor: the UUID rule of interactions.py). It
has a DamageReceiver, a GatherableController (m_isDamageableGatherable), and no HitVolume: the client reports no melee contact on it (the
static read of stage 1.8e, research quests-18e/destructible). The sources (all in the pak files of the game copy):

  1. The placements: each placement of the layers (regions/REGION/capitals/LAYER/LAYER.capitals.json) whose slice (sliceAssetId and the
     asset catalog, else sliceName) has the variation table Gatherable_Quest_Damageable and one GatherableController: its UUID, its
     managed actor, its position (worldPosition), its slice asset and sub id, the slot of the GatherableController (m_replicationIndex),
     and its variant (variantName).
  2. The variation row of the variant (VariantID): its gatherable row (GatherableEntryID), InteractTag, and InteractOnlyOnQuest.
  3. The gatherable row (the GatherableData tables, the rules of gathering.py): BaseGatherTime (the work of the destruction), the respawn
     bounds, the loot root, and GameEventID (the game event of the quests).

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when a
variation row names a gatherable row that no table has, and when a value of a row has a wrong form (the rules of gathering.py). A placement
whose variant has no variation row is counted (unknownVariants) and not written.

Usage: quest_objects.py [OUTPUT.json]      (default: GAMEDATA_DIR/quest-objects.json; a test: a path outside the common folder)

The document (format 1):
  {"format": 1,
   "rows": [{"id", "table", "time", "respawn": [MIN, MAX], "loot", "event"}, ...],
   "objects": [{"key": PLACEMENT_UUID, "actor": HEX32, "asset": HEX32, "sub": N, "slot": N, "variant": TEXT, "row": INDEX,
                "tag": TEXT, "onlyOnQuest": BOOL, "position": [X, Y, Z], "slice": PATH}, ...],
   "counts": {...}}
"""

from __future__ import annotations

import json
import os
import re
import sys
import uuid
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))

from appearance_tables import Failure  # noqa: E402
from creature_data import CATALOG_PATH, REGIONS_FOLDER, Catalog, Sources  # noqa: E402
from gathering import (CONTROLLER_CLASS, DAMAGEABLE_TABLES, SLOT_FIELD, TABLE_FIELD, WANTED, Tables, row_entry,  # noqa: E402
                       slice_path_of)
from interactions import actor_of, walk  # noqa: E402
from slice_ids import find_field  # noqa: E402

OUTPUT_NAME = "quest-objects.json"
FORMAT = 1


def slice_facts(sources: Sources, path: str, cache: dict) -> dict | None:
    """@return The slot of the GatherableController and the variation table of a destructible slice, or no value for another slice."""
    if path in cache:
        return cache[path]
    cache[path] = None
    raw = sources.read(path)
    if raw is None:
        sources.check_complete()
        return None
    if CONTROLLER_CLASS.bytes not in raw or not any(table.encode() in raw for table in DAMAGEABLE_TABLES):
        return None
    slots = []
    tables = []
    for root in sources.stream(path) or []:
        for element in walk(root):
            if element.type_id == CONTROLLER_CLASS:
                slot = find_field(element, SLOT_FIELD)
                slots.append(int.from_bytes(slot.value, "big") if slot is not None and slot.value else 0)
            if element.name == TABLE_FIELD and element.value:
                tables.append(element.value.decode("utf-8", "replace"))
    if len(slots) != 1 or not any(table in DAMAGEABLE_TABLES for table in tables):
        return None
    cache[path] = {"slot": slots[0], "table": next(table for table in tables if table in DAMAGEABLE_TABLES)}
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
                facts = slice_facts(sources, slice_path, cache)
                if facts is None:
                    continue
                key = str(placement.get("id", "")).strip().lower()
                if not re.fullmatch(r"[0-9a-f-]{36}", key):
                    raise Failure(f"{path}: a destructible placement has no UUID")
                variant = str(placement.get("variantName", "")).strip()
                entry = tables.variations.get(facts["table"], {}).get(variant.lower())
                if entry is None or not entry["gatherable"]:
                    counts["unknownVariants"] += 1
                    continue
                row = row_of(entry["gatherable"], f"{path}: the variation row {facts['table']}/{variant}")
                position = placement.get("worldPosition") or {}
                place = [float(position.get(axis, 0.0)) for axis in ("x", "y", "z")]
                asset = assets.get(slice_path)
                if asset is None:
                    raise Failure(f"{slice_path}: the asset catalog has no asset id of the slice")
                objects.append({"key": key, "actor": actor_of(key).hex(), "asset": asset[0].hex(), "sub": asset[1], "slot": facts["slot"],
                                "variant": variant, "row": row, "tag": entry.get("tag", ""), "onlyOnQuest": bool(entry.get("onlyOnQuest")),
                                "position": [round(value, 4) for value in place], "slice": slice_path})
                counts["byVariant"][variant] = counts["byVariant"].get(variant, 0) + 1
    objects.sort(key=lambda item: item["key"])
    counts["objects"] = len(objects)
    counts["rows"] = len(rows)
    return {"format": FORMAT, "rows": rows, "objects": objects, "counts": counts}


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
    print(f"{target}: {counts['objects']} destructible quest objects of {counts['placements']} placements, {counts['rows']} gatherable rows, "
          f"{counts['unknownVariants']} placements with no variation row")
    return 0


if __name__ == "__main__":
    sys.exit(main())
