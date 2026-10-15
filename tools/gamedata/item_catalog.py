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

Output: {"format": 1, "count": N, "tables": [{"name", "rows", "first", "last"}, ...],
"columns": [...], "items": [[...], ...], "backstories": {"<id>": [[item id, quantity, gear
score], ...], ...}, "creationBackstories": ["<id>", ...]}. `count` is the length of the list of the client with entry 0. Entry
number K of `items` (from 0) is the item with the index K + 1. The columns are: id, name (the
text key of the display name), type, class, tier, weight (tenths), maxStack, durability,
minGearScore, maxGearScore, gearScoreOverride, bindOnPickup. A backstory has one entry for
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
           "maxGearScore", "gearScoreOverride", "bindOnPickup"]


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
    for data in contents:
        kind = sheet_type(data)
        try:
            if kind == MASTER_TYPE:
                masters.append(Sheet(data))
            elif kind == BACKSTORY_TYPE:
                stories.append(Sheet(data))
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
                          whole(sheet.cell(row, "GearScoreOverride")), bool(sheet.cell(row, "BindOnPickup"))])
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
                              bool(sheet.cell(row, "BindOnPickup"))])
        tables.append({"name": sheet.name, "rows": sheet.row_count, "first": first, "last": len(items)})
    backstories = backstory_items(stories, {item[0].lower() for item in items})
    return {"format": 1, "count": len(items) + 1, "tables": tables, "columns": COLUMNS, "items": items,
            "backstories": backstories, "creationBackstories": creation_backstories(stories)}


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
          f"{len(catalog['creationBackstories'])} backstories of the character creation (source: {source[1]})")
    if not catalog["backstories"]:
        print(f"items: the source has no table of the type {BACKSTORY_TYPE} with items: a new character gets no item")
    return 0


if __name__ == "__main__":
    sys.exit(main())
