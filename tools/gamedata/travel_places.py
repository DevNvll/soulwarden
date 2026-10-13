#!/usr/bin/env python3
"""Write the travel places of the game as one JSON file.

The backend reads this file at its start (protocol notes 4.16). A travel place is a place of the
world to which the travel command of the backend can move a character: the tool window, the
admin interface, and tools/world show the list.

The source (a decision of this project). Each region of the world has a list of territory
landmarks: `TerritoryLandmarks` of regions/REGION/region.metadata (an object stream). A landmark
has a type, a world position, and the ids of the territory, of the area, and of the point of
interest that contain it. The tool takes two types:

  27  a spirit shrine: the place where the game puts a character after a fast travel to it
  20  the respawn place of an inn: the place where the game puts a character after a recall

These are the places of the game data where the game itself puts a character: each one is a
position in the open world where a character can stand, with a height on the ground, and each
territory with a settlement has some of them. A landmark of the type 1 (a settlement) is the
center of an area with a height that is not a ground height, and the other types are objects
(a crafting station, a storage) and not places for a character.

The name of a place comes from the territory tables (the table type TerritoryDefinition: the
name key of each territory, area, and point of interest) and from the English text files of
the game.

Output: {"format": 1, "world": TEXT, "counts": {"places": N, "territories": N},
         "places": [{"id", "kind", "name", "territory", "territoryId", "position": [x, y, z]}, ...]}
`id` is the text of a place for a command ("shrine_10228": the kind and the id of the point of
interest, or the number of the place in its territory), `kind` is "shrine" or "inn", and
`territory` is the English name of the territory. The places are in the sequence of the
territory names, then of the kinds, then of the names.

The pak entries have Oodle data: the tool reads them as the creature data tool does
(creature_data.py: SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, or the extracted files). When one
file that it needs has no source, the tool stops with one message and writes no file. It does
the same for a spirit shrine or an inn whose position has a wrong form (no position, a position
that does not have 12 bytes, a number that is not finite): tools/gamedata/check_places_tool.py is
the check of this rule.

Usage: travel_places.py [OUTPUT.json]
       travel_places.py --list          print the places, and write no file
Default output: <SOULWARDEN_GAMEDATA_DIR>/travel-places.json
Environment: as creature_data.py.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import REGIONS_FOLDER, TABLES_FOLDER, WORLD, Sources, first, number  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "travel-places.json"
TEXTS_FOLDER = "localization/en-us/"
DEFINITION_TYPE = "TerritoryDefinition"
# The landmark types that are a place for a character, and the kind of each one.
KINDS = {27: "shrine", 20: "inn"}
# Limits of a place: the height range of a world position on the wire, with the margin of the backend.
LOWEST_HEIGHT, HIGHEST_HEIGHT = -99.0, 999.0

LANDMARKS_FIELD = field_id("TerritoryLandmarks")
IDS_FIELD = field_id("TerritoryIds")
TYPE_FIELD = field_id("landmarkType")
POSITION_FIELD = field_id("WorldPosition")


def read_definitions(sources: Sources) -> dict[int, dict]:
    """@return The rows of the territory tables: the name key and the kind of each id."""
    rows: dict[int, dict] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None or sheet_type(data) != DEFINITION_TYPE:
            continue
        try:
            sheet = Sheet(data)
            for row in range(sheet.row_count):
                identifier = int(sheet.cell(row, "TerritoryID") or 0)
                kind = "area" if sheet.cell(row, "IsArea") else "poi" if sheet.cell(row, "IsPOI") else "territory"
                rows.setdefault(identifier, {"key": str(sheet.cell(row, "NameLocalizationKey")), "kind": kind})
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    if not rows:
        raise Failure("the game copy has no territory table")
    return rows


def read_texts(sources: Sources, keys: set[str]) -> dict[str, str]:
    """@return The English text of each of the keys (lower case, with no "@") that a text file of the game has."""
    texts: dict[str, str] = {}
    for path in sources.paths(TEXTS_FOLDER, ".loc.xml"):
        data = sources.read(path)
        if data is None:
            continue
        for key, value in re.findall(r'<string key="([^"]+)"[^>]*>([^<]*)</string>', data.decode("utf-8", "replace")):
            if key.lower() in keys:
                texts.setdefault(key.lower(), (value.replace("&amp;", "&").replace("&apos;", "'").replace("&quot;", '"')
                                               .replace("&lt;", "<").replace("&gt;", ">").strip()))
    return texts


def read_landmarks(sources: Sources) -> list[dict]:
    """@return Each landmark of the world with a type of KINDS: the type, the ids, and the position.

    A landmark of a different type is not a place for a character: it is left out. A landmark
    with a type of KINDS must have a position of three finite numbers (12 bytes). One that does
    not is a fault of the sources, and the tool writes no file: a file with the other places
    looks complete, and the backend cannot see that a place is not there.
    """
    paths = [path for path in sources.paths(REGIONS_FOLDER, "/region.metadata")
             if re.fullmatch(r"r_\+\d\d_\+\d\d/region\.metadata", path[len(REGIONS_FOLDER):])]
    if not paths:
        raise Failure("the game copy has no region file (regions/REGION/region.metadata)")
    landmarks = []
    for path in paths:
        for root in sources.stream(path) or []:
            listed = first(root, LANDMARKS_FIELD)
            for index, element in enumerate(listed.children if listed is not None else []):
                fields = {child.name: child for child in element.children}
                kind = number(fields.get(TYPE_FIELD))
                if kind not in KINDS:
                    continue
                what = f"{path}: landmark {index} (type {kind}, {KINDS[kind]})"
                position = fields.get(POSITION_FIELD)
                if position is None or position.value is None:
                    raise Failure(f"{what} has no position")
                if len(position.value) != 12:
                    raise Failure(f"{what} has a position of {len(position.value)} bytes, not of 12 bytes")
                place = struct.unpack(">fff", position.value)
                if not all(math.isfinite(value) for value in place):
                    raise Failure(f"{what} has a position with a number that is not finite")
                ids = fields.get(IDS_FIELD)
                landmarks.append({"type": kind, "ids": [number(child) for child in (ids.children if ids is not None else [])],
                                  "position": place})
    return landmarks


def build_document(sources: Sources) -> dict:
    definitions = read_definitions(sources)
    landmarks = read_landmarks(sources)
    sources.check_complete()
    keys = {definitions[identifier]["key"].lstrip("@").lower()
            for landmark in landmarks for identifier in landmark["ids"] if identifier in definitions}
    texts = read_texts(sources, keys)
    sources.check_complete()

    def name_of(identifier: int | None) -> str:
        row = definitions.get(identifier) if identifier is not None else None
        return texts.get(row["key"].lstrip("@").lower(), "") if row else ""

    places = []
    seen: set[tuple] = set()
    for landmark in landmarks:
        x, y, z = (round(float(value), 3) for value in landmark["position"])
        # A height outside the range of the wire is not a place for a character.
        if not LOWEST_HEIGHT <= z <= HIGHEST_HEIGHT:
            continue
        # A region file has a landmark two times when two layers place it.
        if (landmark["type"], x, y, z) in seen:
            continue
        seen.add((landmark["type"], x, y, z))
        by_kind: dict[str, int] = {}
        for identifier in landmark["ids"]:
            row = definitions.get(identifier)
            if row is not None:
                by_kind.setdefault(row["kind"], identifier)
        territory = by_kind.get("territory")
        point = by_kind.get("poi")
        kind = KINDS[landmark["type"]]
        territory_name = name_of(territory) or (f"Territory {territory}" if territory is not None else "No territory")
        name = name_of(point) or name_of(by_kind.get("area"))
        if kind == "inn":
            name = f"Inn of {name}" if name else "Inn"
        elif not name:
            name = "Spirit Shrine"
        places.append({"kind": kind, "name": name, "territory": territory_name, "territoryId": territory or 0,
                       "position": [x, y, z], "point": point})
    if not places:
        raise Failure("the region files of the game copy have no spirit shrine and no inn")
    places.sort(key=lambda place: (place["territory"], place["kind"], place["name"], place["position"]))
    # The id: the kind and the id of the point of interest. Two places of one point get a number.
    used: dict[str, int] = {}
    for place in places:
        point = place.pop("point")
        base = f"{place['kind']}_{point}" if point is not None else f"{place['kind']}_t{place['territoryId']}"
        used[base] = used.get(base, 0) + 1
        place["id"] = base if used[base] == 1 else f"{base}_{used[base]}"
    # Two places with one name in one territory: the number of the place makes the name clear.
    names: dict[tuple, int] = {}
    for place in places:
        names[(place["territory"], place["name"])] = names.get((place["territory"], place["name"]), 0) + 1
    count: dict[tuple, int] = {}
    for place in places:
        key = (place["territory"], place["name"])
        if names[key] > 1:
            count[key] = count.get(key, 0) + 1
            place["name"] = f"{place['name']} {count[key]}"
    territories = sorted({place["territory"] for place in places})
    return {"format": 1, "world": WORLD, "counts": {"places": len(places), "territories": len(territories)},
            "places": [{"id": place["id"], "kind": place["kind"], "name": place["name"], "territory": place["territory"],
                        "territoryId": place["territoryId"], "position": place["position"]} for place in places]}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the travel places of the game as one JSON file.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--list", action="store_true", help="print the places, and write no file")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        sources = Sources(game / "assets", (REGIONS_FOLDER, TABLES_FOLDER, TEXTS_FOLDER))
        document = build_document(sources)
        if not arguments.list:
            target.parent.mkdir(parents=True, exist_ok=True)
            # The backend must not read a file that is not complete: write, then rename.
            temporary = target.with_name(target.name + ".tmp")
            temporary.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
            temporary.replace(target)
    except Failure as error:
        print(f"places: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"places: {target}: {error}", file=sys.stderr)
        return 1
    if arguments.list:
        for place in document["places"]:
            x, y, z = place["position"]
            print(f"{place['id']:<20} {place['kind']:<7} {place['territory']:<20} {place['name']:<40} {x:.3f} {y:.3f} {z:.3f}")
    counts = document["counts"]
    kinds = {kind: sum(1 for place in document["places"] if place["kind"] == kind) for kind in sorted(set(KINDS.values()))}
    print(f"{'the game copy' if arguments.list else target}: {counts['places']} travel places in {counts['territories']} "
          f"territories (" + ", ".join(f"{count} of the kind {kind}" for kind, count in kinds.items()) + ")")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
