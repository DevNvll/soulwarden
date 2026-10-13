#!/usr/bin/env python3
"""Write the creature data of the game as one JSON file.

The backend reads this file at its start (protocol notes 4.14). It has the usual spawn points
of the creatures in an area of the world, and for each kind of creature what the backend needs
for its record: the slice asset, the slot of each replicated component of the slice, the ids of
its vitals rows, the values for its health, and its name.

The chain of one spawn point (all files are in the pak files of the game copy):

  1. A layer of a region has a list of spawn points: `AiSpawnLocations` of
     regions/REGION/capitals/LAYER/LAYER.metadata (an object stream). An entry has the position,
     the CRC of the vitals row and of the category row, the level, and four flags.
  2. The same layer has a placement at the same position: LAYER.capitals.json. It names the
     spawner slice (`coatgen/.../pointspawner_*.dynamicslice`), and it has the rotation and the
     id of the placement.
  3. The spawner slice names an alias asset (or a slice), and it has the respawn values.
  4. The alias asset has one or more slices with a percent value. The asset catalog
     (`assetcatalog.catalog`) gives the path of each asset id.
  5. The slice of the creature has the entities and the components. A component with a
     replicated state has the field `m_replicationIndex`: its slot in a state record.
  6. The vitals tables give the base row of a vitals row (the family, the creature type, the
     health modifier), the level table gives the health of a level, and the modifier table
     gives the health modifier of a creature type.
  7. The action list component of the slice names a grid file and a controller file. They give
     the action table of the creature (see `read_action_table`): the index of each action and
     of its animation fragment in a movement state (protocol notes 4.14).

Which entries become a spawn point (a rule of this project):
  - The entry has `SpawnedByCoatlicue`, and it has not `IsOverride` and not `IsEncounter`: a
    usual spawn point, not an entry of an event.
  - The base row of its vitals row has a family: a creature, not a siege object.
  - A placement of its layer is at its position (0.06 m or less).
  - Its alias has a slice with no schedule, and that slice has a movement component and a
    vitals component.
  - The client shows that slice correctly with the record of the backend (the list
    SLICES_NOT_CORRECT: five slices of the default area show no body).
  - Its position is in the area of the file: a circle around a center (--center, --radius).
    The default is 800 m around the start place of a new character: the spawn points of that
    circle are the ones that the research counted and that the live checks of stage 3.4a
    used. A larger area needs only a larger radius.

The pak entries of the supported build have the compression method 15 (Oodle), which Python
cannot read. Thus the tool uses the first of these sources that it has for each entry:

  1. The pak entry, when it is stored or deflate data.
  2. The pak entry with an Oodle library of this computer (SOULWARDEN_OODLE_LIB: a shared library
     with the function OodleLZ_Decompress, for example liboo2core.so).
  3. The pak entry with the Oodle helper of tools/research (oodle_loader.c), when
     SOULWARDEN_OODLE_DLL names an Oodle library for Windows (SOULWARDEN_OODLE_DEC: the helper).
  4. The file that tools/research/pakx.py extracted: SOULWARDEN_EXTRACT_DIR, default
     ~/Games/soulwarden/work/assets/extract.
When one entry that the tool needs has no source, the tool stops with one message and writes
no file: a file with a part of the spawn points would give a world with a part of its
creatures and no sign of the fault.

The tool needs only the standard library of Python and the modules appearance_tables.py,
item_catalog.py, and slice_ids.py of this folder.

Output: see `build_document`. An id of 64 bits is a text of 16 hexadecimal digits.

Usage: creature_data.py [OUTPUT.json] [--center X Y] [--radius METERS]
       creature_data.py --find TEXT...     print the kinds whose id or name has a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/creatures.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see above)
"""

from __future__ import annotations

import argparse
import ctypes
import json
import math
import os
import re
import struct
import subprocess
import sys
import uuid
import xml.etree.ElementTree as ElementTree
import zipfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id, plain_stream  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402
from slice_ids import (COMPONENTS_FIELD, ENTITY_TYPE, ID_FIELD, LOCAL_TRANSFORM_FIELD, NAME_FIELD,  # noqa: E402
                       NO_ENTITY, PARENT_FIELD, Element, component_id, find_field, id_value, parse)

OUTPUT_NAME = "creatures.json"
WORLD = "newworld_vitaeeterna"
REGIONS_FOLDER = f"sharedassets/coatlicue/{WORLD}/regions/"
TABLES_FOLDER = "sharedassets/springboardentitites/datatables/"
CATALOG_PATH = "assetcatalog.catalog"
NAMES_PATH = "localization/en-us/javelindata_vitals.loc.xml"
# The paths that the tool can need: the index of the pak files keeps only these.
WANTED = (REGIONS_FOLDER, TABLES_FOLDER, "coatgen/", "slices/", "sharedassets/", "animations/mannequin/", CATALOG_PATH,
          NAMES_PATH)
# The start place of a new character (meters), and the radius of the default area.
DEFAULT_CENTER = (8880.0, 4250.0)
DEFAULT_RADIUS = 800.0
# Edge of the square of one region (meters): region r_+XX_+YY starts at XX * 2048, YY * 2048.
REGION_SIZE = 2048.0
# A placement is the placement of a spawn entry when it is this near (meters).
PLACEMENT_TOLERANCE = 0.06
OODLE = 15
STORED = 0
DEFLATE = 8

# Table types of the vitals tables.
VARIANT_TYPE = "VitalsLevelVariantData"
BASE_TYPE = "VitalsBaseData"
CATEGORY_TYPE = "VitalsCategoryData"
LEVEL_TYPE = "VitalsLevelData"
MODIFIER_TYPE = "VitalsModifierData"

# Type ids of the engine classes.
SLICE_COMPONENT_TYPE = uuid.UUID("afd304e4-1773-47c8-855a-8b622398934f")
ASSET_TYPE = uuid.UUID("77a19d40-8731-4d3c-9041-1b43047366a4")
SLICE_ASSET_TYPE = uuid.UUID("78802abf-9595-463a-8d2b-d022f906f9b1")
TEXT_TYPE = uuid.UUID("03aaab3f-5c47-5a66-9ebc-d5fa4db353c9")

# The component classes of a creature slice that have a replicated state: class id, name, and
# the type index of the state type in the supported client build (research files
# creatures.md and enemies-start-area.md, from the constructors of the state types). The
# backend sends the states of the first three. A class that is not here gets the type 0.
COMPONENT_CLASSES = {
    "30ed0ace-51dd-48b9-ba41-2fa6775cd106": ("ActionList", 11),
    "0c848755-1f41-4408-b2be-0a3627674f50": ("Vitals", 15),
    "98021a39-d35a-423d-bdcd-86aac61c6207": ("Marker", 5399),
    "1bd9d601-9de8-4a8b-b115-bb534eb6020c": ("DamageReceiver", 1528),
    "0b382af4-7946-4698-9223-206d1187b612": ("GatherableController", 12),
    "5c822d39-e3ca-73f0-7ab5-922457e9cc9c": ("StatusEffects", 4236),
    "dd164eef-7dbd-4f7c-bcef-b01a344a33f2": ("Ability", 185),
    "1154125c-dc3e-479f-b336-41b76e1963e7": ("AlignToTerrain", 2850),
    "504323cb-f59a-4c94-a32b-ddf174102c58": ("Magic", 2895),
    "a4cf5c98-487e-4db9-ac48-9698f965688d": ("Mana", 1652),
    "d353f7e8-4266-f749-19a1-aa2c3841fbca": ("DelayedEvent", 3139),
    "476b4b9f-9c00-4476-afa2-2d9f1179bad5": ("ItemManagement", 5437),
    "ba93b9ff-7a7e-4028-889e-12d418a11c71": ("Paperdoll", 3183),
    "b69045a3-a129-4334-bdfb-069c18cc84a2": ("Interact", 2930),
    "05e9d829-20e1-40b2-a8ba-ba70b6024921": ("Grit", 17),
    "167eed78-848f-48e4-acf6-141bf356ca7d": ("Stamina", 4297),
    "1e84749a-ae32-4ac1-8391-03955663f3ea": ("LookTargeting", 2443),
    "579d9f45-aa85-4407-ae43-b0c058a60e5a": ("StatMultiplierTable", 1525),
    "32f0711b-235e-43ee-9687-253ae1fcd68a": ("ReactionTracking", 1927),
    "171fa882-c8af-41df-bd19-09cbbfeb1c1c": ("TemporaryAffiliation", 3563),
}
MOVEMENT, VITALS, MARKER = "ActionList", "Vitals", "Marker"

# Slices that the client does not show correctly with the record of the backend (live tour of
# stage 3.4a: one creature of each slice of the default area). A spawn point with only such
# slices is not in the file, until a later stage sends what the slice needs.
SLICES_NOT_CORRECT = {
    "slices/characters/ancientguardian_javelineer.dynamicslice": "a blue light, no body, no name plate",
    "slices/characters/ancientguardian_reaver.dynamicslice": "a blue light, no body, no name plate",
    "slices/characters/ancientguardian_spearman.dynamicslice": "a blue light, no body, no name plate",
    "slices/characters/wex_levelvariants/ancientguardian_javelineer_12.dynamicslice": "a blue light, no body, no name plate",
    "slices/characters/corruption_swarmer.dynamicslice": "a red effect, no body, no name plate",
}

REPLICATION_FIELD = field_id("m_replicationIndex")
FACET_FIELDS = (field_id("m_clientFacetPtr"), field_id("m_serverFacetPtr"))
SPAWN_RADIUS_FIELD = field_id("m_sliceSpawnRadius")
GRID_FIELD = field_id("m_actionGridNew")
CONTROLLER_FIELD = field_id("m_controllerDefinition")
ASSET_PATH_FIELD = field_id("AssetPath")
# An asset field has the id (16 bytes), the sub id and 12 zero bytes, the type id (16 bytes), the
# size of the path text (8 bytes), and the path text of the asset.
ASSET_PATH_OFFSET = 56
SPAWN_LIST_FIELD = field_id("AiSpawnLocations")
ALIAS_FIELD = field_id("m_aliasAsset")
SLICE_FIELD = field_id("m_sliceAsset")
SLICES_FIELD = field_id("Slices")
ALIAS_SLICE_FIELD = field_id("Slice")
PERCENT_FIELD = field_id("Percent")
SCHEDULE_FIELD = field_id("Schedule")
ENTRY_FIELDS = {name: field_id(name) for name in ("VitalsId", "VitalsCategoryId", "VitalsLevel", "WorldPosition",
                                                  "SpawnedByCoatlicue", "IsAlias", "IsOverride", "IsEncounter")}
SPAWNER_FIELDS = {name: field_id(name) for name in ("m_minRespawnCooldown", "m_maxRespawnCooldown",
                                                    "m_respawnAfterDeath", "m_spawnOnInitialize",
                                                    "m_spawnOnlyOnce")}


def name_crc(text: str) -> int:
    """@return The id of a table row on the wire: the CRC-32 of its name in lower case."""
    return zlib.crc32(text.lower().encode("utf-8")) & 0xFFFFFFFF


def walk(element: Element):
    yield element
    for child in element.children:
        yield from walk(child)


def first(element: Element, wanted: int) -> Element | None:
    """@return The first element below `element` with this field number, at each depth."""
    for node in walk(element):
        if node.name == wanted:
            return node
    return None


def number(element: Element | None) -> int:
    """@return The whole number of a field: its own value, or the value of its one child."""
    while element is not None and element.value is None and len(element.children) == 1:
        element = element.children[0]
    return int.from_bytes(element.value, "big") if element is not None and element.value is not None else 0


def real(element: Element | None) -> float:
    if element is None or element.value is None or len(element.value) != 4:
        return 0.0
    return struct.unpack(">f", element.value)[0]


class Sources:
    """The pak files of the game copy, and the content of one entry from the first source that has it."""

    def __init__(self, assets: Path) -> None:
        if not assets.is_dir():
            raise Failure(f"no game copy: {assets} is not a folder (SOULWARDEN_GAME_DIR is the folder of the game)")
        paks = sorted(assets.rglob("*.pak"))
        if not paks:
            raise Failure(f"no game copy: {assets} has no pak file")
        self.entries: dict[str, tuple[Path, zipfile.ZipInfo]] = {}
        for pak in paks:
            try:
                with zipfile.ZipFile(pak) as archive:
                    for info in archive.infolist():
                        name = info.filename.lower().replace("\\", "/")
                        if name.startswith(WANTED):
                            self.entries.setdefault(name, (pak, info))
            except (OSError, zipfile.BadZipFile) as error:
                raise Failure(f"{pak.name} is not a readable pak file: {error}") from error
        home = Path.home()
        self.extracted = Path(os.environ.get("SOULWARDEN_EXTRACT_DIR") or home / "Games/soulwarden/work/assets/extract")
        self.windows_library = os.environ.get("SOULWARDEN_OODLE_DLL", "")
        self.helper = Path(os.environ.get("SOULWARDEN_OODLE_DEC") or home / "Games/soulwarden/work/assets/bin/oodle_dec")
        self.native = None
        library = os.environ.get("SOULWARDEN_OODLE_LIB", "")
        if library:
            try:
                self.native = ctypes.CDLL(library)
                self.native.OodleLZ_Decompress.restype = ctypes.c_ssize_t
                self.native.OodleLZ_Decompress.argtypes = [
                    ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int, ctypes.c_int,
                    ctypes.c_int, ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_void_p, ctypes.c_void_p,
                    ctypes.c_void_p, ctypes.c_ssize_t, ctypes.c_int]
            except (OSError, AttributeError) as error:
                raise Failure(f"SOULWARDEN_OODLE_LIB ({library}) is not an Oodle library: {error}") from error
        self.used: dict[str, int] = {}
        self.absent: list[str] = []

    def has(self, path: str) -> bool:
        return path in self.entries

    def paths(self, prefix: str, suffix: str) -> list[str]:
        return sorted(name for name in self.entries if name.startswith(prefix) and name.endswith(suffix))

    def _raw(self, pak: Path, info: zipfile.ZipInfo) -> bytes:
        with open(pak, "rb") as handle:
            handle.seek(info.header_offset)
            header = handle.read(30)
            name_size, extra_size = struct.unpack_from("<HH", header, 26)
            handle.seek(info.header_offset + 30 + name_size + extra_size)
            return handle.read(info.compress_size)

    def read(self, path: str) -> bytes | None:
        """@return The plain content of one entry. No value, with a mark, when the entry has no source."""
        entry = self.entries.get(path)
        if entry is None:
            self.absent.append(path)
            return None
        pak, info = entry
        try:
            if info.compress_type == STORED:
                return self._done("pak files", plain_stream(self._raw(pak, info)))
            if info.compress_type == DEFLATE:
                return self._done("pak files", plain_stream(zlib.decompress(self._raw(pak, info), -15)))
            if info.compress_type != OODLE:
                raise Failure(f"compression method {info.compress_type} is not supported")
            if self.native is not None:
                data = self._raw(pak, info)
                out = ctypes.create_string_buffer(info.file_size)
                size = self.native.OodleLZ_Decompress(data, len(data), out, info.file_size, 1, 1, 0, None, 0, None,
                                                      None, None, 0, 3)
                if size != info.file_size:
                    raise Failure(f"the Oodle library gave {size} bytes, not {info.file_size}")
                return self._done("pak files (Oodle library)", plain_stream(out.raw))
            if self.windows_library and self.helper.is_file():
                result = subprocess.run([str(self.helper), self.windows_library, str(info.file_size)],
                                        input=self._raw(pak, info), capture_output=True, check=False)
                if result.returncode != 0:
                    raise Failure(f"the Oodle helper failed: {result.stderr.decode(errors='replace').strip()}")
                return self._done("pak files (Oodle helper)", plain_stream(result.stdout))
            file = self.extracted / path
            if file.is_file():
                return self._done("extracted files", plain_stream(file.read_bytes()))
        except (Failure, OSError, zlib.error, struct.error) as error:
            raise Failure(f"{pak.name}: {path}: {error}") from error
        self.absent.append(path)
        return None

    def _done(self, source: str, data: bytes) -> bytes:
        self.used[source] = self.used.get(source, 0) + 1
        return data

    def stream(self, path: str) -> list[Element] | None:
        data = self.read(path)
        if data is None:
            return None
        try:
            return parse(data)
        except (Failure, struct.error, IndexError, ValueError) as error:
            raise Failure(f"{path}: not an object stream: {error}") from error

    def check_complete(self) -> None:
        if self.absent:
            names = sorted(set(self.absent))
            raise Failure(f"{len(names)} file(s) of the game copy have no source (first: {names[0]}). The pak entries "
                          f"have Oodle data: set SOULWARDEN_OODLE_LIB or SOULWARDEN_OODLE_DLL to an Oodle library, or extract "
                          f"the files with tools/research/pakx.py (SOULWARDEN_EXTRACT_DIR, default {self.extracted})")


class Catalog:
    """The asset catalog of the game: the path of each asset id.

    header := "RASC" version:u32 size:u32 u32 guids:u32 types:u32 folders:u32 files:u32 end:u32 count:u32
    record := guid:u32 sub:u32 u32 u32 type:u32 u32 size:u64 folder:u32 file:u32     (40 bytes, from offset 40)
    `guid` and `type` are indexes into two tables of 16 bytes for each entry, and `folder` and
    `file` are offsets into two blocks of texts that end with a zero byte.
    """

    def __init__(self, data: bytes) -> None:
        if len(data) < 40 or data[:4] != b"RASC":
            raise Failure("the asset catalog has no RASC header")
        _, _, size, _, guids, _, folders, files, _, count = struct.unpack_from("<4s9I", data)
        if size != len(data) or 40 + 40 * count > len(data):
            raise Failure("the asset catalog does not have the size of its header")
        self.paths: dict[tuple[bytes, int], str] = {}
        folder_names: dict[int, str | None] = {}
        view = memoryview(data)
        for guid, sub, _, _, _, _, _, folder, file in struct.iter_unpack("<6IQ2I", view[40:40 + 40 * count]):
            name = folder_names.get(folder, "")
            if name == "":
                start = folders + folder
                name = data[start:data.index(b"\0", start)].decode("utf-8", "replace").lower()
                # Only the folders of slices and of alias assets: the catalog has more than 2 million records.
                name = name if name.startswith("slices/") or "/aliasassets/" in name or name.startswith("coatgen/") else None
                folder_names[folder] = name
            if name is None:
                continue
            start = files + file
            text = name + data[start:data.index(b"\0", start)].decode("utf-8", "replace").lower()
            self.paths.setdefault((data[guids + 16 * guid:guids + 16 * guid + 16], sub), text)

    def path(self, asset: tuple[bytes, int] | None) -> str | None:
        return self.paths.get(asset) if asset is not None else None


def asset_id(element: Element | None) -> tuple[bytes, int] | None:
    """@return The asset id of an asset field (16 bytes and the sub id), or no value for an empty field."""
    if element is None or element.value is None or len(element.value) < 20:
        return None
    guid, sub = element.value[:16], int.from_bytes(element.value[16:20], "big")
    return None if guid == bytes(16) else (guid, sub)


def region_names(center: tuple[float, float], radius: float) -> list[str]:
    """@return The names of the regions whose square touches the circle."""
    names = []
    low_x, high_x = int((center[0] - radius) // REGION_SIZE), int((center[0] + radius) // REGION_SIZE)
    low_y, high_y = int((center[1] - radius) // REGION_SIZE), int((center[1] + radius) // REGION_SIZE)
    for x in range(max(low_x, 0), high_x + 1):
        for y in range(max(low_y, 0), high_y + 1):
            near_x = min(max(center[0], x * REGION_SIZE), (x + 1) * REGION_SIZE)
            near_y = min(max(center[1], y * REGION_SIZE), (y + 1) * REGION_SIZE)
            if math.hypot(near_x - center[0], near_y - center[1]) <= radius:
                names.append(f"r_+{x:02d}_+{y:02d}")
    return names


def read_tables(sources: Sources) -> dict:
    """@return The rows of the vitals tables, with the lower-case CRC of the row id as the key."""
    variants: dict[int, dict] = {}
    bases: dict[int, dict] = {}
    categories: dict[int, dict] = {}
    levels: dict[int, float] = {}
    modifiers: dict[str, float] = {}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        if "vitals" not in path.rsplit("/", 1)[-1]:
            continue
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        if kind not in (VARIANT_TYPE, BASE_TYPE, CATEGORY_TYPE, LEVEL_TYPE, MODIFIER_TYPE):
            continue
        try:
            sheet = Sheet(data)
            for row in range(sheet.row_count):
                if kind == VARIANT_TYPE:
                    name = str(sheet.cell(row, "VitalsID"))
                    variants.setdefault(name_crc(name), {"id": name, "base": str(sheet.cell(row, "BaseVitalsId")),
                                                         "level": int(sheet.cell(row, "Level") or 0),
                                                         "name": str(sheet.cell(row, "DisplayName"))})
                elif kind == BASE_TYPE:
                    name = str(sheet.cell(row, "VitalsID"))
                    bases.setdefault(name_crc(name), {"id": name, "family": str(sheet.cell(row, "Family")),
                                                      "creatureType": str(sheet.cell(row, "CreatureType")),
                                                      "healthMod": float(sheet.cell(row, "HealthMod") or 0.0),
                                                      "name": str(sheet.cell(row, "DisplayName"))})
                elif kind == CATEGORY_TYPE:
                    name = str(sheet.cell(row, "VitalsCategoryID"))
                    categories.setdefault(name_crc(name), {"id": name, "name": str(sheet.cell(row, "DisplayName"))})
                elif kind == LEVEL_TYPE:
                    levels[int(sheet.cell(row, "Level"))] = float(sheet.cell(row, "BaseMaxHealth"))
                else:
                    modifiers[str(sheet.cell(row, "CategoryId"))] = float(sheet.cell(row, "CategoryHealthMod"))
        except (struct.error, ValueError, IndexError, TypeError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    for what, rows in (("level variant vitals", variants), ("base vitals", bases), ("vitals category", categories),
                       ("vitals level", levels), ("vitals modifier", modifiers)):
        if not rows:
            raise Failure(f"the game copy has no {what} table")
    return {"variants": variants, "bases": bases, "categories": categories, "levels": levels, "modifiers": modifiers}


def read_names(sources: Sources) -> dict[str, str]:
    """@return The English text of each key of the vitals text file. A key has no "@" here."""
    data = sources.read(NAMES_PATH)
    if data is None:
        return {}
    text = data.decode("utf-8", "replace")
    names = {}
    for key, value in re.findall(r'<string key="([^"]+)"[^>]*>([^<]*)</string>', text):
        names[key.lower()] = (value.replace("&amp;", "&").replace("&apos;", "'").replace("&quot;", '"')
                              .replace("&lt;", "<").replace("&gt;", ">"))
    return names


def read_spawn_entries(roots: list[Element]) -> list[dict]:
    entries = []
    for root in roots:
        spawn_list = first(root, SPAWN_LIST_FIELD)
        if spawn_list is None:
            continue
        for element in spawn_list.children:
            fields = {child.name: child for child in element.children}
            position = fields.get(ENTRY_FIELDS["WorldPosition"])
            if position is None or position.value is None or len(position.value) != 12:
                continue
            entries.append({
                "vitals": number(fields.get(ENTRY_FIELDS["VitalsId"])),
                "category": number(fields.get(ENTRY_FIELDS["VitalsCategoryId"])),
                "level": number(fields.get(ENTRY_FIELDS["VitalsLevel"])),
                "position": struct.unpack(">fff", position.value),
                "coatlicue": number(fields.get(ENTRY_FIELDS["SpawnedByCoatlicue"])) != 0,
                "override": number(fields.get(ENTRY_FIELDS["IsOverride"])) != 0,
                "encounter": number(fields.get(ENTRY_FIELDS["IsEncounter"])) != 0,
            })
    return entries


def spawner_path(placement: dict, catalog: Catalog, sources: Sources) -> str | None:
    """@return The pak path of the spawner slice of a placement."""
    name = str(placement.get("sliceName", "")).replace("\\", "/").lower()
    while "//" in name:
        name = name.replace("//", "/")
    if name.endswith(".slice"):
        name = name[:-6] + ".dynamicslice"
    if sources.has(name):
        return name
    match = re.fullmatch(r"\{?([0-9A-Fa-f-]{36})\}?:([0-9A-Fa-f]+)", str(placement.get("sliceAssetId", "")))
    return catalog.path((uuid.UUID(match[1]).bytes, int(match[2], 16))) if match else None


def read_spawner(roots: list[Element]) -> dict:
    """@return The asset of the spawn (an alias or a slice) and the respawn values of a spawner slice."""
    alias = None
    direct = None
    values: dict[str, Element] = {}
    for root in roots:
        for node in walk(root):
            if node.name == ALIAS_FIELD and alias is None:
                alias = asset_id(node)
            elif node.name == SLICE_FIELD and direct is None:
                direct = asset_id(node)
            else:
                for name, wanted in SPAWNER_FIELDS.items():
                    if node.name == wanted:
                        values.setdefault(name, node)
    return {"alias": alias, "slice": direct,
            "respawnMin": real(values.get("m_minRespawnCooldown")),
            "respawnMax": real(values.get("m_maxRespawnCooldown")),
            "respawnAfterDeath": number(values.get("m_respawnAfterDeath")) != 0,
            "spawnOnInitialize": number(values.get("m_spawnOnInitialize")) != 0,
            "spawnOnlyOnce": number(values.get("m_spawnOnlyOnce")) != 0}


def read_alias(roots: list[Element]) -> list[tuple[tuple[bytes, int], int]]:
    """@return The slices of an alias asset that have no schedule: (asset id, percent)."""
    slices = []
    for root in roots:
        for node in walk(root):
            if node.name != SLICES_FIELD:
                continue
            for element in node.children:
                fields = {child.name: child for child in element.children}
                asset = asset_id(fields.get(ALIAS_SLICE_FIELD))
                schedule = fields.get(SCHEDULE_FIELD)
                if asset is None or (schedule is not None and schedule.children):
                    continue
                slices.append((asset, number(fields.get(PERCENT_FIELD))))
    return slices


def game_path(text: str) -> str:
    return text.replace("\\", "/").lower().lstrip("/")


def read_xml(sources: Sources, path: str) -> ElementTree.Element | None:
    """@return The root of an XML file of the game, or no value when the file has no source or does not parse."""
    data = sources.read(game_path(path)) if sources.has(game_path(path)) else None
    if data is None:
        return None
    try:
        return ElementTree.fromstring(data.decode("utf-8-sig", "replace"))
    except ElementTree.ParseError:
        return None


def read_tag_names(sources: Sources, path: str, depth: int = 0) -> list[str] | None:
    """@return The names of a tag definition file, in the sequence of the client: the names of each imported file
    first, then the tags of the file in their sequence (the tags of a group at the place of the group). A name
    that is in the list already is not added again. The position of a fragment name in the list of the fragment
    file of a controller is the fragment index on the wire (live proof: 117 fragments of three kinds)."""
    root = read_xml(sources, path)
    if root is None or depth > 8:
        return None
    names: list[str] = []
    seen: set[str] = set()

    def add(name: str | None) -> None:
        if name and name.lower() not in seen:
            seen.add(name.lower())
            names.append(name)

    for imported in root.iter("Import"):
        for name in read_tag_names(sources, imported.get("filename", ""), depth + 1) or []:
            add(name)
    tags = root.find("Tags")
    for child in (tags if tags is not None else []):
        if child.tag == "Tag":
            add(child.get("name"))
        else:
            for tag in child.iter("Tag"):
                add(tag.get("name"))
    return names


def read_action_table(sources: Sources, roots: list[Element]) -> list[list] | None:
    """@return The action table of a creature slice: [action name, fragment name, fragment index, server started]
    for each action, or no value when the slice has no grid file. The position of an action in the list is its
    index on the wire: the scope record of a movement state has the index plus 1.

    The rule of the client (builder at image RVA 0x4840D90, live proof for the tables of three kinds: 50, 62,
    and 19 actions): the table has the action lists of the grid file in their sequence, and the action
    definitions of each list in their sequence. An action definition is in the table only when the alias map
    of its list in the grid file names it. A definition with the mark Disabled is not in the table.
    """
    grid_path = ""
    controller_path = ""
    for root in roots:
        for node in walk(root):
            if node.name == GRID_FIELD and node.value is not None and len(node.value) > ASSET_PATH_OFFSET and not grid_path:
                grid_path = node.value[ASSET_PATH_OFFSET:].split(b"\0")[0].decode("utf-8", "replace")
            elif node.name == CONTROLLER_FIELD and not controller_path:
                found = first(node, ASSET_PATH_FIELD)
                if found is not None and found.value is not None:
                    controller_path = found.value.decode("utf-8", "replace")
    grid = read_xml(sources, grid_path) if grid_path else None
    if grid is None:
        return None
    fragments: dict[str, int] = {}
    controller = read_xml(sources, controller_path) if controller_path else None
    fragment_file = controller.find("Fragments") if controller is not None else None
    if fragment_file is not None:
        for position, name in enumerate(read_tag_names(sources, fragment_file.get("filename", "")) or []):
            fragments.setdefault(name.lower(), position)
    actions: list[list] = []
    # Only the lists of the element ActionLists: a grid file names action lists in other elements too.
    lists = grid.find("ActionLists")
    for entry in (lists.findall("ActionList") if lists is not None else []):
        listed = {action.get("ActionName", "").lower() for action in entry.iter("Action")}
        definitions = read_xml(sources, entry.get("Path", ""))
        container = definitions.find("Actions") if definitions is not None else None
        if container is None:
            # A list of the grid that is not there: each index after it would be wrong.
            return None
        for action in container.findall("Action"):
            name = action.get("Name", "")
            if action.get("Disabled", "").lower() in ("true", "1") or name.lower() not in listed:
                continue
            fragment = action.get("FragmentName", "")
            actions.append([name, fragment, fragments.get(fragment.lower(), -1),
                            action.get("ServerStarted", "").lower() in ("true", "1")])
    return actions


def read_creature_slice(path: str, asset: tuple[bytes, int], roots: list[Element]) -> dict:
    """@return The data of one creature slice: its root entity, and each component with a replicated state or a facet."""
    entities: list[tuple[int, int | None]] = []
    slots: list[list] = []
    components: list[list] = []
    radius = 0.0
    for root in roots:
        for element in walk(root):
            if element.name == SPAWN_RADIUS_FIELD and radius == 0.0:
                radius = real(element)
            if element.type_id != ENTITY_TYPE:
                continue
            listed = next((child for child in element.children if child.name == COMPONENTS_FIELD), None)
            entity_id = next((id_value(child) for child in element.children if child.name == ID_FIELD), None)
            name = next((child.value.decode("utf-8", "replace") for child in element.children
                         if child.name == NAME_FIELD and child.value is not None and child.type_id == TEXT_TYPE), "")
            # The outer entity of the file has the slice component: it is not an entity of an instance.
            if listed is None or entity_id is None or any(c.type_id == SLICE_COMPONENT_TYPE for c in listed.children):
                continue
            parent = None
            for component in listed.children:
                if find_field(component, LOCAL_TRANSFORM_FIELD) is not None:
                    parent = id_value(find_field(component, PARENT_FIELD))
                class_name, state_type = COMPONENT_CLASSES.get(str(component.type_id), ("", 0))
                identifier = component_id(component)
                slot = number(find_field(component, REPLICATION_FIELD))
                faceted = any(find_field(component, facet) is not None for facet in FACET_FIELDS)
                if identifier is None or (slot == 0 and not faceted):
                    continue
                label = f"{name}/{class_name or component.type_id}"
                if slot != 0:
                    slots.append([slot, class_name or str(component.type_id), state_type])
                if faceted:
                    components.append([f"{entity_id:016X}", f"{identifier:016X}", label])
            entities.append((entity_id, parent))
    slice_roots = [entity for entity, parent in entities if parent == NO_ENTITY]
    if not slice_roots:
        raise Failure(f"{path}: the slice has no root entity")
    slots.sort()
    numbers = [slot[0] for slot in slots]
    if len(set(numbers)) != len(numbers):
        raise Failure(f"{path}: two components of the slice have the same slot")
    by_name = {slot[1]: slot[0] for slot in reversed(slots)}
    return {"path": path, "asset": str(uuid.UUID(bytes=asset[0])).upper(), "sub": asset[1],
            "root": f"{slice_roots[-1]:016X}", "interestRadius": round(radius, 3),
            "movement": by_name.get(MOVEMENT, 0), "vitals": by_name.get(VITALS, 0), "marker": by_name.get(MARKER, 0),
            "slots": slots, "components": components}


def yaw_of(rotation: dict) -> float:
    """@return The angle about the vertical axis of a rotation quaternion, in degrees (0 to 360)."""
    x, y, z = (float(rotation.get(axis, 0.0)) for axis in "xyz")
    w = float(rotation.get("w", 1.0))
    return math.degrees(math.atan2(2.0 * (w * z + x * y), 1.0 - 2.0 * (y * y + z * z))) % 360.0


def build_document(sources: Sources, center: tuple[float, float], radius: float) -> dict:
    """Output:
    {"format": 1, "world": TEXT, "center": [x, y], "radius": METERS, "regions": [...],
     "counts": {...},
     "levels": [[level, health], ...]                  the health of each level (the level table)
     "slices": [{"path", "asset", "sub", "root", "interestRadius", "movement", "vitals", "marker",
                 "slots": [[slot, component name, state type], ...],
                 "components": [[entity id, component id, name], ...],
                 "actions": [[action name, fragment name, fragment index, server started], ...]}, ...]
     "kinds":  [{"id", "vitals", "category", "vitalsId", "categoryId", "name", "vitalsName",
                 "family", "creatureType", "health", "level", "bodies": [[slice, percent], ...],
                 "points": N}, ...]
     "spawnPoints": [[placement id, kind, level, x, y, z, yaw, respawnMin, respawnMax], ...]}
    `slices`: "movement", "vitals", and "marker" are the slots of these three components (0: the
    slice has none), "components" are the components with a facet (the targets of client
    messages), and "actions" is the action table: the position of an action is its index in a
    movement state, and a fragment index of -1 is "no fragment" (an empty list: the slice has no
    grid file). `kinds`: "vitalsId" and "categoryId" are the ids on the wire, "health" is the
    health of the kind as a part of the health of its level (the health modifier of its base
    row times the modifier of its creature type), "level" is the level of the most spawn
    points, and "bodies" are the slices of its alias (index into `slices`). A spawn point has
    an index into `kinds`.
    """
    tables = read_tables(sources)
    names = read_names(sources)
    catalog_data = sources.read(CATALOG_PATH)
    if catalog_data is None:
        sources.check_complete()
    catalog = Catalog(catalog_data)
    regions = region_names(center, radius)
    counts = {"entries": 0, "inArea": 0, "usual": 0, "creatures": 0}
    left_out: dict[str, int] = {}
    slices: list[dict] = []
    slice_index: dict[str, int] = {}
    spawners: dict[str, dict | None] = {}
    aliases: dict[tuple[bytes, int], list] = {}
    kinds: dict[tuple, dict] = {}
    points: list[list] = []
    placed: dict[str, int] = {}

    def leave_out(cause: str) -> None:
        left_out[cause] = left_out.get(cause, 0) + 1

    def body_of(asset: tuple[bytes, int]) -> int | None:
        path = catalog.path(asset)
        if path is None:
            return None
        if path not in slice_index:
            roots = sources.stream(path)
            if roots is None:
                return None
            slice_index[path] = len(slices)
            slices.append(read_creature_slice(path, asset, roots))
            slices[-1]["actions"] = read_action_table(sources, roots) or []
        return slice_index[path]

    def bodies_of(spawner: dict) -> list[tuple[int, int]]:
        if spawner["alias"] is None:
            direct = body_of(spawner["slice"]) if spawner["slice"] is not None else None
            return [(direct, 100)] if direct is not None else []
        if spawner["alias"] not in aliases:
            path = catalog.path(spawner["alias"])
            roots = sources.stream(path) if path is not None else None
            alternatives = []
            for asset, percent in (read_alias(roots) if roots is not None else []):
                body = body_of(asset)
                if body is not None:
                    alternatives.append((body, percent))
            aliases[spawner["alias"]] = alternatives
        return aliases[spawner["alias"]]

    for region in regions:
        for metadata_path in sources.paths(f"{REGIONS_FOLDER}{region}/capitals/", ".metadata"):
            roots = sources.stream(metadata_path)
            if roots is None:
                continue
            entries = read_spawn_entries(roots)
            counts["entries"] += len(entries)
            selected = []
            for entry in entries:
                if math.hypot(entry["position"][0] - center[0], entry["position"][1] - center[1]) > radius:
                    continue
                counts["inArea"] += 1
                if not entry["coatlicue"] or entry["override"] or entry["encounter"]:
                    continue
                counts["usual"] += 1
                variant = tables["variants"].get(entry["vitals"])
                base = tables["bases"].get(name_crc(variant["base"])) if variant else tables["bases"].get(entry["vitals"])
                if base is None or not base["family"]:
                    leave_out("no creature family (a siege object)")
                    continue
                counts["creatures"] += 1
                selected.append((entry, variant, base))
            if not selected:
                continue
            capitals = sources.read(metadata_path[:-len(".metadata")] + ".capitals.json")
            if capitals is None:
                continue
            try:
                placements = json.loads(capitals.decode("utf-8-sig")).get("Capitals") or []
            except (ValueError, AttributeError) as error:
                raise Failure(f"{metadata_path}: the placements of the layer did not parse: {error}") from error
            for entry, variant, base in selected:
                placement = None
                for candidate in placements:
                    where = candidate.get("worldPosition") or {}
                    if math.dist(entry["position"], tuple(float(where.get(axis, 0.0)) for axis in "xyz")) \
                            <= PLACEMENT_TOLERANCE:
                        placement = candidate
                        break
                if placement is None:
                    leave_out("no placement at the position")
                    continue
                path = spawner_path(placement, catalog, sources)
                if path is None:
                    leave_out("no spawner slice")
                    continue
                if path not in spawners:
                    roots = sources.stream(path)
                    spawners[path] = read_spawner(roots) if roots is not None else None
                spawner = spawners[path]
                if spawner is None:
                    continue
                bodies = [(body, percent) for body, percent in bodies_of(spawner)
                          if slices[body]["movement"] != 0 and slices[body]["vitals"] != 0]
                if not bodies:
                    leave_out("no slice with a movement component and a vitals component")
                    continue
                shown = [(body, percent) for body, percent in bodies if slices[body]["path"] not in SLICES_NOT_CORRECT]
                if not shown:
                    leave_out("the client does not show its slice correctly (" + slices[bodies[0][0]]["path"].rsplit("/", 1)[-1] + ")")
                    continue
                bodies = shown
                category = tables["categories"].get(entry["category"])
                modifier = tables["modifiers"].get(base["creatureType"])
                if category is None or modifier is None:
                    leave_out("no category row or no modifier row of the creature type")
                    continue
                vitals_text = variant["id"] if variant else base["id"]
                key = (vitals_text, category["id"], tuple(bodies))
                kind = kinds.get(key)
                if kind is None:
                    vitals_name = (variant or base)["name"]
                    kind = kinds[key] = {
                        "vitals": vitals_text, "category": category["id"],
                        "vitalsId": entry["vitals"], "categoryId": entry["category"],
                        "name": names.get(category["name"].lstrip("@").lower(), ""),
                        "vitalsName": names.get(vitals_name.lstrip("@").lower(), ""),
                        "family": base["family"], "creatureType": base["creatureType"],
                        "health": round(base["healthMod"] * modifier, 6),
                        "bodies": [list(body) for body in bodies], "levels": {}, "index": len(kinds)}
                # The metadata of a layer has one entry for each slice of an alias with more than
                # one slice: the entries of one placement are one spawn point.
                identifier = str(placement.get("id", ""))
                if identifier in placed:
                    leave_out("a second entry of one placement" if placed[identifier] == kind["index"]
                              else "a second entry of one placement, with a different kind")
                    continue
                placed[identifier] = kind["index"]
                kind["levels"][entry["level"]] = kind["levels"].get(entry["level"], 0) + 1
                points.append([identifier, kind["index"], entry["level"],
                               round(entry["position"][0], 4), round(entry["position"][1], 4),
                               round(entry["position"][2], 4), round(yaw_of(placement.get("rotation") or {}), 2),
                               spawner["respawnMin"], spawner["respawnMax"]])
    sources.check_complete()
    if not points:
        raise Failure(f"the area (center {center[0]:g} {center[1]:g}, radius {radius:g} m) has no spawn point of a "
                      f"creature in the regions {', '.join(regions) or 'none'}")
    if "" in placed:
        raise Failure("a placement of a spawn point has no id")
    # The kinds in the sequence of their first spawn point. A kind id is the vitals row; when
    # more than one kind has the row, the kinds after the one with the most spawn points get a number.
    kind_list = sorted((kind for kind in kinds.values() if kind["levels"]), key=lambda kind: kind["index"])
    kind_number = {kind["index"]: position for position, kind in enumerate(kind_list)}
    for point in points:
        point[1] = kind_number[point[1]]
    by_row: dict[str, list[dict]] = {}
    for kind in kind_list:
        by_row.setdefault(kind["vitals"], []).append(kind)
    for row, group in by_row.items():
        group.sort(key=lambda kind: (-sum(kind["levels"].values()), kind["index"]))
        for position, kind in enumerate(group):
            kind["id"] = row if position == 0 else f"{row}:{position + 1}"
    used = sorted({body for kind in kind_list for body, _ in kind["bodies"]})
    renumber = {old: new for new, old in enumerate(used)}
    document_kinds = []
    for kind in kind_list:
        total = sum(kind["levels"].values())
        level = max(kind["levels"].items(), key=lambda item: (item[1], -item[0]))[0]
        document_kinds.append({"id": kind["id"], "vitals": kind["vitals"], "category": kind["category"],
                               "vitalsId": kind["vitalsId"], "categoryId": kind["categoryId"],
                               "name": kind["name"], "vitalsName": kind["vitalsName"], "family": kind["family"],
                               "creatureType": kind["creatureType"], "health": kind["health"], "level": level,
                               "bodies": [[renumber[body], percent] for body, percent in kind["bodies"]],
                               "points": total})
    counts["slicesWithNoActions"] = sum(1 for body in used if not slices[body]["actions"])
    counts.update({"spawnPoints": len(points), "kinds": len(document_kinds), "slices": len(used),
                   "leftOut": dict(sorted(left_out.items()))})
    return {"format": 1, "world": WORLD, "center": list(center), "radius": radius, "regions": regions,
            "counts": counts, "levels": [[level, health] for level, health in sorted(tables["levels"].items())],
            "slices": [slices[body] for body in used], "kinds": document_kinds, "spawnPoints": points}


def health_of(document: dict, kind: dict, level: int) -> float:
    levels = dict((int(row[0]), row[1]) for row in document["levels"])
    return levels.get(level, 0.0) * kind["health"]


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the creature data of the game as one JSON file.")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--center", nargs=2, type=float, default=list(DEFAULT_CENTER), metavar=("X", "Y"))
    parser.add_argument("--radius", type=float, default=DEFAULT_RADIUS, metavar="METERS")
    parser.add_argument("--find", nargs="+", metavar="TEXT", help="print the kinds with a text, and write no file")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    if not (math.isfinite(arguments.radius) and 0.0 < arguments.radius <= 20000.0):
        print("creatures: the radius must be a number from 1 to 20000. No file was written.", file=sys.stderr)
        return 1
    try:
        sources = Sources(game / "assets")
        document = build_document(sources, (arguments.center[0], arguments.center[1]), arguments.radius)
        if arguments.find is None:
            target.parent.mkdir(parents=True, exist_ok=True)
            # The backend must not read a file that is not complete: write, then rename.
            temporary = target.with_name(target.name + ".tmp")
            temporary.write_text(json.dumps(document, separators=(",", ":")) + "\n", encoding="utf-8")
            temporary.replace(target)
    except Failure as error:
        print(f"creatures: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"creatures: {target}: {error}", file=sys.stderr)
        return 1
    if arguments.find is not None:
        for kind in document["kinds"]:
            text = f"{kind['id']} {kind['name']} {kind['vitalsName']} {kind['category']}".lower()
            if any(word.lower() in text for word in arguments.find):
                body = document["slices"][kind["bodies"][0][0]]
                print(f"{kind['id']}: {kind['name'] or '(no name)'}, category {kind['category']}, {kind['family']}, "
                      f"{kind['creatureType']}, level {kind['level']}, health {health_of(document, kind, kind['level']):.3f}, "
                      f"{kind['points']} spawn point(s), slice {body['path']} (asset {body['asset']}:{body['sub']}, "
                      f"slots: movement {body['movement']}, vitals {body['vitals']}, marker {body['marker']}, "
                      f"interest radius {body['interestRadius']:g} m)")
        return 0
    counts = document["counts"]
    print(f"{target}: {counts['spawnPoints']} spawn points of {counts['kinds']} kinds with {counts['slices']} slices, "
          f"in {arguments.radius:g} m around {arguments.center[0]:g} {arguments.center[1]:g} "
          f"(regions {', '.join(document['regions'])})")
    print(f"  entries of the layers: {counts['entries']}; in the area: {counts['inArea']}; usual (not an event): "
          f"{counts['usual']}; with a creature family: {counts['creatures']}")
    for cause, count in counts["leftOut"].items():
        print(f"  left out, {cause}: {count}")
    print(f"  slices with no action table: {counts['slicesWithNoActions']}")
    print("  source: " + ", ".join(f"{count} files from the {name}" for name, count in sorted(sources.used.items())))
    return 0


if __name__ == "__main__":
    sys.exit(main())
