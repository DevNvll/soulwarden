#!/usr/bin/env python3
"""Write the item catalog of the game as one JSON file.

An item on the wire is an index into a list that the client makes from its data tables at
its start (`ItemDataManager::m_indexToIdMap`, protocol notes 4.10). The backend reads this
file and gets, for each item id, the index and the values that it needs to make an item
instance (class, weight, stack limit, durability, gear score).

The list of the client (client code at image RVA 0x4428B00, 0x442DD20, 0x442AAE0):

  index 0 = no item
  for each data table of the type "MasterItemDefinitions", in the byte order of the table name
  of the file header (not the file name):
      for each row in file order, not a row with the item type "HousingItem":
          the row gets the next index
  then the tables "HouseItems" and "HouseItemsMTX" (type "VariationData"), in this sequence:
      for each row in file order, not the row "DEFAULTS", not a row with ExcludeFromGame, not
      a row whose id is the id of a row before it:
          the row gets the next index

Input: the entries `sharedassets/springboardentitites/datatables/**/*.datasheet` of the pak
files of the game copy (`<game>/assets/**/*.pak`). The pak entries of the supported build
have the compression method 15 (Oodle), which Python cannot read. Thus the tool uses the
first of these sources that it has:

  1. The pak entries, when they are stored or deflate data.
  2. The pak entries with the Oodle helper of tools/research (oodle_loader.c), when
     SOULWARDEN_OODLE_DLL names an Oodle library that you supply (SOULWARDEN_OODLE_DEC: the helper).
  3. The files that tools/research/pakx.py extracted: SOULWARDEN_DATATABLES_DIR, default
     ~/Games/soulwarden/work/assets/extract/sharedassets/springboardentitites/datatables.
     The folder must have each data table of the pak files, with the size of its pak entry,
     and no other data table: the index of an item is its position in all item tables, thus
     one table that is not there makes each later index wrong. The tool refuses a folder that
     is not complete, and it then writes no file.

The file also has the items of each backstory of the game (the table of the type
"BackstoryDefinition": the archetypes of the character creation are rows of it). The backend
gives a new character the items of its archetype that have a quantity.

The backstory table has rows for tests and for later parts of the game too (331 rows; some
give 200 of an item). It has no column that marks a row as an archetype of the character
creation. The creation screen of the client sends the id of an archetype button, and those ids
have the prefix "Archetype_" and no value in the column PlaytestType (7 rows), or it sends the
default backstory of the client setting `javelin.default-backstory-id` ("Backstory1"). The file
has these ids in the member `creationBackstories`: the backend refuses a create request with a
different backstory.

The tool needs only the standard library of Python and the module appearance_tables.py of
this folder (pak entry reader).

Output: {"format": 3, "count": N, "tables": [{"name", "rows", "first", "last"}, ...],
"columns": [...], "items": [[...], ...], "backstories": {"<id>": [[item id, quantity, gear
score], ...], ...}, "creationBackstories": ["<id>", ...], "artifactObjectives": [[item id, objective id, achievement id,
achievement index], ...]}. `count` is the length of the list of the client with entry 0. Entry
number K of `items` (from 0) is the item with the index K + 1. The columns are: id, name (the
text key of the display name), type, class, tier, weight (tenths), maxStack, durability,
minGearScore, maxGearScore, gearScoreOverride, bindOnPickup, nonRemovable, notDroppable, maxCraftUpgradeGS. The
nonRemovable and notDroppable columns (format 2, stage 2.2: code review of stage 3.5b.2, finding LB-R1) are the removal protections
of the row (the columns Nonremovable and NotDroppable; a cell can be a text, a number, or a flag):
the backend keeps such an item with its character (no discard, no give into a loot bag). A
house item has no protection. Each master table must have both columns, and each cell must be a
flag form (code review of stage 2.2, finding WT-R3): else the tool stops and the old file stays.
The one exception is MISSING_PROTECTION_TABLES (the store tables have no column NotDroppable).
tools/gamedata/check_item_catalog_tool.py is the check of these columns.

Format 3 (stage B0, the bow build): the column maxCraftUpgradeGS is the cell MaxCraftUpgradeGS of the master row: the largest
gear score of an upgrade of the item (800 for the 73 artifacts; 0 for an empty cell). The client parser keeps the value only
when the cell is not empty (image RVA 0x442DD20+0x1997: a value with a presence byte). The store tables
(MISSING_UPGRADE_TABLES) have no such column (0); a different master table with no column, and a cell that is not a whole
number or an empty text, stop the tool. The member artifactObjectives has the objective of each artifact (the rows of the
type Artifact of the Objectives tables): the item of its equip task (TaskEquipItem in the task tree of its Task, the columns
SubTask1 to SubTask5 of the ObjectiveTasks tables) and its achievement with the AchievementIndex of the AchievementData table.
The client shows the perks 2 to 5 of an artifact only when the character has that achievement (live check U1 of stage B0;
the tooltip says "Equip to view task." with no bit). An objective whose tasks, item, or achievement do not resolve, an
objective with no equip item or with two, and two objectives of one item stop the tool.

A backstory has one entry for
each item of its column InventoryItem: a quantity of 0 is an item that only the character
creation screen shows, and a gear score of 0 is the value of the item table.

Usage: item_catalog.py [OUTPUT.json]
       item_catalog.py --find ID...      print the catalog line of each item id (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/item-catalog.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_DATATABLES_DIR, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC (see above)
"""

from __future__ import annotations

import json
import os
import struct
import subprocess
import sys
import zipfile
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, plain_stream, read_entry  # noqa: E402

TABLES_FOLDER = "sharedassets/springboardentitites/datatables/"
OUTPUT_NAME = "item-catalog.json"
MASTER_TYPE = "MasterItemDefinitions"
HOUSE_TYPE = "VariationData"
HOUSE_TABLES = ("HouseItems", "HouseItemsMTX")
BACKSTORY_TYPE = "BackstoryDefinition"
# The objectives of the artifacts (format 3, stage B0): the objective rows, their tasks, and the achievement table.
OBJECTIVES_TYPE = "Objectives"
TASKS_TYPE = "ObjectiveTasks"
ACHIEVEMENT_TYPE = "AchievementData"
ARTIFACT_OBJECTIVE = "Artifact"
EQUIP_TASK = "TaskEquipItem"
SUBTASK_PREFIX = "SubTask"
# The backstories that the character creation of the client can send (see the text above).
CREATION_PREFIX = "Archetype_"
DEFAULT_BACKSTORY = "Backstory1"
HOUSE_DEFAULTS_ROW = "DEFAULTS"
HOUSING_ITEM_TYPE = "HousingItem"
# Zip compression method of an Oodle entry.
OODLE = 15
# Layout of a data table file (little-endian, format version 0x12).
HEADER_SIZE = 0x5C
CELL_TEXT, CELL_NUMBER, CELL_FLAG = 1, 2, 3
COLUMNS = ["id", "name", "type", "class", "tier", "weight", "maxStack", "durability", "minGearScore",
           "maxGearScore", "gearScoreOverride", "bindOnPickup", "nonRemovable", "notDroppable", "maxCraftUpgradeGS"]
# The largest gear score of an upgrade of a master item row (format 3, stage B0).
UPGRADE_COLUMN = "MaxCraftUpgradeGS"
# The master tables that can have no upgrade column: the prefix of the table names. The 22 store tables of the game copy
# (MasterItemDefinitions_MTX_*) have no column MaxCraftUpgradeGS (a census of the game data): their items have no upgrade.
MISSING_UPGRADE_TABLES = "MasterItemDefinitions_MTX_"
# The removal protections of a master item row (format 2; code review of stage 3.5b.2, finding LB-R1).
PROTECTION_COLUMNS = ("Nonremovable", "NotDroppable")
# The master tables that can have no protection column (code review of stage 2.2, finding WT-R3): the column, and the prefix of the table
# names. The store tables of the game copy (22 tables, MasterItemDefinitions_MTX_*) have no column NotDroppable (a census of the game
# data); their items have no such protection (a decision of an agent: the value of a cell with no text). Each other master table must
# have both columns: a table without one stops the tool, and the old file stays.
MISSING_PROTECTION_TABLES = {"NotDroppable": "MasterItemDefinitions_MTX_"}
# The forms of a protection cell in the item tables of the game (a census of the game data): a flag, the number 0 or 1, the text "" or
# "1". The texts "0", "true", and "false" are also read. Each other value stops the tool.
FLAG_TEXTS = {"": False, "0": False, "1": True, "false": False, "true": True}


class Sheet:
    """One data table file.

    0x08 nameOffset:u32   0x10 typeOffset:u32   0x38 textBlock:u32 (relative to 0x3C)
    0x44 columns:u32 rows:u32
    0x5C columns * {crc:u32 nameOffset:u32 kind:u32}, then rows * columns * {text:u32 value:u32}
    A cell of the kind 1 is a text (offset in the text block), 2 is a float, 3 is a flag.
    """

    def __init__(self, data: bytes) -> None:
        if len(data) < HEADER_SIZE:
            raise Failure("the data table is too short")
        self._data = data
        self._texts = struct.unpack_from("<I", data, 0x38)[0] + 0x3C
        self.name = self._text(struct.unpack_from("<I", data, 0x08)[0])
        self.type = self._text(struct.unpack_from("<I", data, 0x10)[0])
        column_count, self.row_count = struct.unpack_from("<II", data, 0x44)
        self.columns: dict[str, int] = {}
        self._kinds: list[int] = []
        offset = HEADER_SIZE
        for column in range(column_count):
            _, name_offset, kind = struct.unpack_from("<III", data, offset)
            self.columns.setdefault(self._text(name_offset), column)
            self._kinds.append(kind)
            offset += 12
        self._cells = offset
        self._column_count = column_count

    def _text(self, offset: int) -> str:
        start = self._texts + offset
        return self._data[start:self._data.index(b"\0", start)].decode("utf-8", "replace")

    def cell(self, row: int, name: str):
        """@return The value of one cell: a text, a number, or a flag. An absent column gives an empty text."""
        column = self.columns.get(name)
        if column is None:
            return ""
        offset = self._cells + (row * self._column_count + column) * 8
        text, raw = struct.unpack_from("<II", self._data, offset)
        kind = self._kinds[column]
        if kind == CELL_TEXT:
            return self._text(text)
        if kind == CELL_NUMBER:
            return struct.unpack_from("<f", self._data, offset + 4)[0]
        return bool(raw)

    def source(self, row: int, name: str) -> str:
        """@return The source text of one cell of each kind: an empty number cell is an empty text here, and 0 in
        cell(). An absent column gives an empty text."""
        column = self.columns.get(name)
        if column is None:
            return ""
        offset = self._cells + (row * self._column_count + column) * 8
        return self._text(struct.unpack_from("<I", self._data, offset)[0])


def sheet_type(data: bytes) -> str:
    """@return The table type of a data table file, with no read of its cells."""
    if len(data) < HEADER_SIZE:
        return ""
    texts = struct.unpack_from("<I", data, 0x38)[0] + 0x3C
    start = texts + struct.unpack_from("<I", data, 0x10)[0]
    end = data.find(b"\0", start)
    return "" if end < 0 else data[start:end].decode("utf-8", "replace")


def oodle_entry(pak: Path, info: zipfile.ZipInfo, library: str, helper: Path) -> bytes:
    """@return The content of an Oodle pak entry."""
    with open(pak, "rb") as handle:
        handle.seek(info.header_offset)
        header = handle.read(30)
        name_size, extra_size = struct.unpack_from("<HH", header, 26)
        handle.seek(info.header_offset + 30 + name_size + extra_size)
        data = handle.read(info.compress_size)
    result = subprocess.run([str(helper), library, str(info.file_size)], input=data, capture_output=True, check=False)
    if result.returncode != 0:
        raise Failure(f"the Oodle helper failed: {result.stderr.decode(errors='replace').strip()}")
    return plain_stream(result.stdout)


def table_entries(assets: Path) -> list[tuple[Path, zipfile.ZipInfo]]:
    """@return The pak file and the index entry of each data table of the game copy. The index of a pak file is
    plain data, also when its entries have Oodle data: this list is the full list of the tables of the game."""
    if not assets.is_dir():
        raise Failure(f"no game copy: {assets} is not a folder (SOULWARDEN_GAME_DIR is the folder of the game)")
    paks = sorted(assets.rglob("*.pak"))
    if not paks:
        raise Failure(f"no game copy: {assets} has no pak file")
    entries: list[tuple[Path, zipfile.ZipInfo]] = []
    for pak in paks:
        try:
            with zipfile.ZipFile(pak) as archive:
                entries += [(pak, info) for info in archive.infolist()
                            if info.filename.lower().startswith(TABLES_FOLDER) and info.filename.lower().endswith(".datasheet")]
        except (OSError, zipfile.BadZipFile) as error:
            raise Failure(f"{pak.name} is not a readable pak file: {error}") from error
    if not entries:
        raise Failure(f"the pak files of {assets} have no data table in {TABLES_FOLDER}")
    return entries


def tables_of_paks(entries: list[tuple[Path, zipfile.ZipInfo]]) -> tuple[list[bytes], str] | None:
    """@return The content of each data table of the pak files and a text that names the source, or no value when
    the entries have Oodle data and the tool has no Oodle helper."""
    library = os.environ.get("SOULWARDEN_OODLE_DLL", "")
    helper = Path(os.environ.get("SOULWARDEN_OODLE_DEC") or Path.home() / "Games/soulwarden/work/assets/bin/oodle_dec")
    have_helper = bool(library) and helper.is_file()
    tables: list[bytes] = []
    used_helper = False
    for pak, info in entries:
        try:
            if info.compress_type != OODLE:
                tables.append(read_entry(pak, info))
            elif have_helper:
                tables.append(oodle_entry(pak, info, library, helper))
                used_helper = True
            else:
                return None
        except (Failure, OSError, zlib.error, struct.error, IndexError, ValueError) as error:
            raise Failure(f"{pak.name}: {info.filename}: {error}") from error
    return tables, "the pak files" + (" (Oodle helper)" if used_helper else "")


def tables_of_folder(folder: Path, entries: list[tuple[Path, zipfile.ZipInfo]]) -> tuple[list[bytes], str]:
    """@return The content of each extracted data table. The folder must be equal to the list of the pak files:
    each table of the game copy with the size of its entry, and no other table."""
    files = sorted(folder.rglob("*.datasheet")) if folder.is_dir() else []
    if not files:
        raise Failure(f"the data tables of the pak files have Oodle data. Set SOULWARDEN_OODLE_DLL to an Oodle library, "
                      f"or extract the tables with tools/research/pakx.py (SOULWARDEN_DATATABLES_DIR, default {folder})")
    expected = {info.filename.lower().replace("\\", "/")[len(TABLES_FOLDER):]: info.file_size for _, info in entries}
    try:
        have = {path.relative_to(folder).as_posix().lower(): path.stat().st_size for path in files}
    except OSError as error:
        raise Failure(f"{folder}: {error}") from error
    absent = sorted(set(expected) - set(have))
    other = sorted(set(have) - set(expected))
    changed = sorted(name for name in expected if name in have and have[name] != expected[name])
    faults = [f"{len(names)} {what} (first: {names[0]})" for names, what in
              ((absent, "table(s) of the game copy are not in the folder"),
               (other, "table(s) of the folder are not in the game copy"),
               (changed, "table(s) do not have the size of the pak entry")) if names]
    if faults:
        raise Failure(f"the extracted tables in {folder} are not the {len(expected)} data tables of the game copy: "
                      + "; ".join(faults) + ". An item index from them is not the index of the client. Extract the "
                      "tables again with tools/research/pakx.py")
    try:
        return [plain_stream(path.read_bytes()) for path in files], f"extracted files in {folder}"
    except OSError as error:
        raise Failure(f"{folder}: {error}") from error


def item_id_crc(item_id: str) -> int:
    """@return The CRC-32 of an item id as the client computes it: only the letters A to Z go to lower case."""
    data = bytes(byte + 0x20 if 0x41 <= byte <= 0x5A else byte for byte in item_id.encode("utf-8"))
    return zlib.crc32(data) & 0xFFFFFFFF


def whole(value) -> int:
    return int(value) if isinstance(value, (int, float)) else 0


def flag(value, where: str = "a cell") -> bool:
    """@return The value of a flag cell. The item tables have a flag as a flag, a number (0 or 1), or a text ("" or "1").
    A different value (finding WT-R3) is a Failure: the tool writes no file."""
    if isinstance(value, bool):
        return value
    if isinstance(value, (int, float)) and value in (0, 1):
        return bool(value)
    if isinstance(value, str) and value.strip().lower() in FLAG_TEXTS:
        return FLAG_TEXTS[value.strip().lower()]
    raise Failure(f"{where} has the value {value!r}, which is not a flag (a flag, 0, 1, \"\", \"0\", \"1\", \"true\", \"false\")")


def protection_columns(sheet: Sheet) -> list[str | None]:
    """@return For each protection column: its name, or None for a column that the table can omit (MISSING_PROTECTION_TABLES).
    A missing column of a different table (finding WT-R3) is a Failure: a blank cell is not the same as a missing column."""
    columns: list[str | None] = []
    for column in PROTECTION_COLUMNS:
        if column in sheet.columns:
            columns.append(column)
        elif sheet.name.startswith(MISSING_PROTECTION_TABLES.get(column, "\0")):
            columns.append(None)
        else:
            raise Failure(f"the item table {sheet.name} has no column {column} (the removal protection of its items)")
    return columns


def upgrade_gear_score(sheet: Sheet, row: int, item_id: str) -> int:
    """@return The cell MaxCraftUpgradeGS of a master row (format 3): a whole number, or 0 for an empty cell. A store table has no
    such column (0). A missing column of a different table, and a cell that is not a whole number or an empty text, are a Failure."""
    if UPGRADE_COLUMN not in sheet.columns:
        if sheet.name.startswith(MISSING_UPGRADE_TABLES):
            return 0
        raise Failure(f"the item table {sheet.name} has no column {UPGRADE_COLUMN} (the gear score limit of an upgrade)")
    value = sheet.cell(row, UPGRADE_COLUMN)
    if isinstance(value, str) and value.strip() == "":
        return 0
    if isinstance(value, (int, float)) and not isinstance(value, bool) and value >= 0 and float(value).is_integer() \
            and value <= 0xFFFF:
        return int(value)
    raise Failure(f"the cell {UPGRADE_COLUMN} of {item_id} in {sheet.name} has the value {value!r}, which is not a whole gear score")


def artifact_objectives(objectives: list[Sheet], tasks: list[Sheet], achievements: list[Sheet], item_ids: set[str]) -> list[list]:
    """@return [item id, objective id, achievement id, achievement index] for each objective of the type Artifact (format 3), in the
    sequence of the objective tables. A Failure for a link that does not resolve (see the text at the top)."""
    task_rows: dict[str, tuple[str, str, list[str]]] = {}
    for sheet in tasks:
        subtasks = [column for column in sheet.columns if column.startswith(SUBTASK_PREFIX)]
        for row in range(sheet.row_count):
            task_id = str(sheet.cell(row, "TaskID"))
            children = [str(sheet.cell(row, column)) for column in subtasks if str(sheet.cell(row, column)).strip()]
            task_rows[task_id] = (str(sheet.cell(row, "Type")), str(sheet.cell(row, "ItemName")), children)
    indexes: dict[str, float] = {}
    for sheet in achievements:
        for row in range(sheet.row_count):
            indexes[str(sheet.cell(row, "AchievementID"))] = sheet.cell(row, "AchievementIndex")
    found: list[list] = []
    seen: set[str] = set()
    for sheet in objectives:
        for row in range(sheet.row_count):
            if sheet.cell(row, "Type") != ARTIFACT_OBJECTIVE:
                continue
            objective = str(sheet.cell(row, "ObjectiveID"))
            items: list[str] = []
            pending, visited = [str(sheet.cell(row, "Task"))], set()
            while pending:
                task = pending.pop()
                if task in visited:
                    continue
                visited.add(task)
                if task not in task_rows:
                    raise Failure(f"the artifact objective {objective} names the task {task!r}, which no task table has")
                kind, item, children = task_rows[task]
                if kind == EQUIP_TASK:
                    items.append(item)
                pending.extend(children)
            if len(items) != 1:
                raise Failure(f"the artifact objective {objective} has {len(items)} equip task(s), not one")
            if items[0].lower() not in item_ids:
                raise Failure(f"the artifact objective {objective} names the item {items[0]!r}, which the item tables do not have")
            if items[0].lower() in seen:
                raise Failure(f"two artifact objectives name the item {items[0]}")
            seen.add(items[0].lower())
            achievement = str(sheet.cell(row, "AchievementId"))
            index = indexes.get(achievement)
            if not isinstance(index, (int, float)) or isinstance(index, bool) or index < 0 or not float(index).is_integer():
                raise Failure(f"the artifact objective {objective} names the achievement {achievement!r}, which has no "
                              f"AchievementIndex in the achievement table")
            found.append([items[0], objective, achievement, int(index)])
    return found


def backstory_items(sheets: list[Sheet], item_ids: set[str]) -> dict[str, list[list]]:
    """@return For each backstory with items: [item id, quantity, gear score] for each entry of its column
    InventoryItem. An entry is `ItemID[-gearScore[-perk...]][:quantity]` (no quantity: 1). The perks of an entry
    are not kept."""
    backstories: dict[str, list[list]] = {}
    for sheet in sheets:
        for row in range(sheet.row_count):
            name = sheet.cell(row, "BackstoryID")
            entries = []
            for part in str(sheet.cell(row, "InventoryItem")).split(","):
                spec, _, count = part.strip().partition(":")
                if not spec:
                    continue
                item_id, gear_score = spec, 0
                # An item id can have a dash: the full text is an item id first.
                if spec.lower() not in item_ids and "-" in spec:
                    words = spec.split("-")
                    item_id = words[0]
                    gear_score = int(words[1]) if words[1].isdigit() else 0
                entries.append([item_id, int(count) if count.strip().isdigit() else 1, gear_score])
            if name and entries:
                backstories[name] = entries
    return backstories


def creation_backstories(sheets: list[Sheet]) -> list[str]:
    """@return The ids of the backstories that the character creation of the client can send."""
    found: list[str] = []
    for sheet in sheets:
        for row in range(sheet.row_count):
            name = str(sheet.cell(row, "BackstoryID"))
            archetype = name.startswith(CREATION_PREFIX) and str(sheet.cell(row, "PlaytestType")) == ""
            if (archetype or name == DEFAULT_BACKSTORY) and name not in found:
                found.append(name)
    return found


def build_catalog(contents: list[bytes]) -> dict:
    masters: list[Sheet] = []
    houses: dict[str, Sheet] = {}
    stories: list[Sheet] = []
    objectives: list[Sheet] = []
    tasks: list[Sheet] = []
    achievements: list[Sheet] = []
    for data in contents:
        kind = sheet_type(data)
        try:
            if kind == MASTER_TYPE:
                masters.append(Sheet(data))
            elif kind == BACKSTORY_TYPE:
                stories.append(Sheet(data))
            elif kind == OBJECTIVES_TYPE:
                objectives.append(Sheet(data))
            elif kind == TASKS_TYPE:
                tasks.append(Sheet(data))
            elif kind == ACHIEVEMENT_TYPE:
                achievements.append(Sheet(data))
            elif kind == HOUSE_TYPE:
                sheet = Sheet(data)
                if sheet.name in HOUSE_TABLES:
                    houses[sheet.name] = sheet
        except (struct.error, ValueError, IndexError) as error:
            raise Failure(f"a data table of the type {kind} did not parse: {error}") from error
    if not masters:
        raise Failure(f"no data table of the type {MASTER_TYPE}")
    names = [sheet.name for sheet in masters]
    if len(set(names)) != len(names):
        raise Failure("two item tables have the same table name")
    # The client keeps the tables in a map with the table name as the key: bytes, then length.
    masters.sort(key=lambda sheet: sheet.name.encode("utf-8"))
    items: list[list] = []
    tables: list[dict] = []
    seen: set[int] = set()
    for sheet in masters:
        if "ItemID" not in sheet.columns or sheet.columns["ItemID"] != 0:
            raise Failure(f"the first column of the table {sheet.name} is not ItemID")
        first = len(items) + 1
        protections = protection_columns(sheet)
        for row in range(sheet.row_count):
            item_type = sheet.cell(row, "ItemType")
            if item_type == HOUSING_ITEM_TYPE:
                continue
            item_id = sheet.cell(row, "ItemID")
            seen.add(item_id_crc(item_id))
            items.append([item_id, sheet.cell(row, "Name"), item_type, sheet.cell(row, "ItemClass"),
                          whole(sheet.cell(row, "Tier")), whole(sheet.cell(row, "Weight")),
                          whole(sheet.cell(row, "MaxStackSize")), whole(sheet.cell(row, "Durability")),
                          whole(sheet.cell(row, "MinGearScore")), whole(sheet.cell(row, "MaxGearScore")),
                          whole(sheet.cell(row, "GearScoreOverride")), bool(sheet.cell(row, "BindOnPickup")),
                          *(flag(sheet.cell(row, column), f"the cell {column} of {item_id} in {sheet.name}") if column else False
                            for column in protections),
                          upgrade_gear_score(sheet, row, item_id)])
        # A table with no row that gets an index has no range.
        tables.append({"name": sheet.name, "rows": sheet.row_count, "first": first, "last": len(items)})
    for name in HOUSE_TABLES:
        sheet = houses.get(name)
        if sheet is None:
            raise Failure(f"no data table with the name {name}")
        first = len(items) + 1
        # The client reads a house table only when its first row is the row of the defaults.
        if sheet.row_count > 0 and sheet.cell(0, "HouseItemID") == HOUSE_DEFAULTS_ROW:
            for row in range(1, sheet.row_count):
                item_id = sheet.cell(row, "HouseItemID")
                excluded = sheet.cell(row, "ExcludeFromGame")
                crc = item_id_crc(item_id)
                if item_id == HOUSE_DEFAULTS_ROW or (excluded not in ("", 0, 0.0, False)) or crc in seen:
                    continue
                seen.add(crc)
                items.append([item_id, sheet.cell(row, "Name"), sheet.cell(row, "ItemType"), "",
                              whole(sheet.cell(row, "Tier")), whole(sheet.cell(row, "Weight")),
                              whole(sheet.cell(row, "MaxStackSize")), 0, 0, 0, 0,
                              bool(sheet.cell(row, "BindOnPickup")), False, False, 0])
        tables.append({"name": sheet.name, "rows": sheet.row_count, "first": first, "last": len(items)})
    item_ids = {item[0].lower() for item in items}
    backstories = backstory_items(stories, item_ids)
    return {"format": 3, "count": len(items) + 1, "tables": tables, "columns": COLUMNS, "items": items,
            "backstories": backstories, "creationBackstories": creation_backstories(stories),
            "artifactObjectives": artifact_objectives(objectives, tasks, achievements, item_ids)}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    extracted = Path(os.environ.get("SOULWARDEN_DATATABLES_DIR") or home / "Games/soulwarden/work/assets/extract" / TABLES_FOLDER)
    arguments = sys.argv[1:]
    find = arguments[1:] if arguments[:1] == ["--find"] else None
    target = Path(arguments[0]) if arguments and find is None else gamedata / OUTPUT_NAME
    try:
        entries = table_entries(game / "assets")
        source = tables_of_paks(entries) or tables_of_folder(extracted, entries)
        catalog = build_catalog(source[0])
        if find is not None:
            by_id = {item[0].lower(): (index + 1, item) for index, item in enumerate(catalog["items"])}
            for item_id in find:
                found = by_id.get(item_id.lower())
                if found is None:
                    print(f"{item_id}: not in the catalog")
                else:
                    print(f"{found[1][0]}: index {found[0]}, CRC 0x{item_id_crc(found[1][0]):08X}, "
                          + ", ".join(f"{name} {value}" for name, value in zip(COLUMNS[1:], found[1][1:])))
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(catalog, separators=(",", ":")) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"items: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"items: {target}: {error}", file=sys.stderr)
        return 1
    for table in catalog["tables"]:
        span = f"index {table['first']} to {table['last']}" if table["last"] >= table["first"] else "no index"
        print(f"  {table['name']:<40} {table['rows']:>5} rows, {span}")
    print(f"{target}: {len(catalog['items'])} items in {len(catalog['tables'])} tables, the list of the client has "
          f"{catalog['count']} entries, the items of {len(catalog['backstories'])} backstories, "
          f"{len(catalog['creationBackstories'])} backstories of the character creation, "
          f"{sum(1 for item in catalog['items'] if item[-1] != 0)} items with an upgrade gear score, "
          f"{len(catalog['artifactObjectives'])} artifact objectives (source: {source[1]})")
    if not catalog["backstories"]:
        print(f"items: the source has no table of the type {BACKSTORY_TYPE} with items: a new character gets no item")
    return 0


if __name__ == "__main__":
    sys.exit(main())
