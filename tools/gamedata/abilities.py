#!/usr/bin/env python3
"""Write the ability data of the game as one JSON file (stage 3.3a).

The backend reads this file and gets what the abilities of a player need (protocol notes 4.22):
the cooldown of each ability, the values of each status effect with its number on the wire, and
the shape of each spell. The damage rows of the abilities are in the combat tables
(tools/gamedata/combat.sh). Without the file the backend starts and works, and it refuses each
ability press.

Input: the game copy, from the same sources as the creature data tool (creature_data.py: the
pak entries, the pak entries with an Oodle library, or the files that tools/research/pakx.py
extracted).

  Source (table type)              What the file gets
  CooldownData                     for each ability: the time and the minimum time of its
                                   cooldown (columns AbilityID, Time, MinTime)
  StatusEffectData (each table)    for each status effect: its number on the wire, BaseDuration,
                                   MoveSpeedMod, Stunned, NoNav, StopOnHitCount, ShowInNameplates,
                                   DMGStandard
  SpellData (each table)           for each spell: Radius, Height, CastDistance, NumToSpawn,
                                   SpawnRate, Duration

The number of a status effect on the wire is the position of the CRC-32 of its lower-case id in
the list of the CRC values of all rows of all status tables, in the unsigned order, with each
duplicate (the list of the client: live check L5 of stage 3.3a, 4,517 rows of the supported
build). A duplicate id has the first position.

Output: {"format": 1, "statusRows": N,
"counts": {"cooldowns": N, "statusEffects": N, "spells": N},
"cooldowns": [[id, time, minTime], ...],
"statusEffects": [[id, index, duration, moveSpeed, stunned, root, stopOnHit, nameplate, damage], ...],
"spells": [[id, radius, height, castDistance, count, spawnRate, duration], ...]}.

The tool writes no file (status 1) when a table type is not in the game copy, when a row has an
empty id, when a number is not finite, when a cooldown is negative, and when two rows of one
list have the same id with different values.

Usage: abilities.py [OUTPUT.json]
       abilities.py --find TEXT...   print the rows with a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/ability-data.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see creature_data.py)
"""

from __future__ import annotations

import bisect
import json
import math
import os
import struct
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "ability-data.json"
COOLDOWN_TYPE = "CooldownData"
STATUS_TYPE = "StatusEffectData"
SPELL_TYPE = "SpellData"
USED_TYPES = (COOLDOWN_TYPE, STATUS_TYPE, SPELL_TYPE)
# Largest number of a status effect on the wire (16 bits).
MAX_STATUS_INDEX = 0xFFFF


def crc(text: str) -> int:
    return zlib.crc32(text.lower().encode("utf-8")) & 0xFFFFFFFF


def number(sheet: Sheet, row: int, column: str) -> float:
    """@return The number of a cell: 0 for an empty cell, 1 or 0 for a flag. A text that is not a number stops the tool."""
    value = sheet.cell(row, column)
    if isinstance(value, bool):
        return 1.0 if value else 0.0
    if isinstance(value, str):
        if value.strip() == "":
            return 0.0
        try:
            value = float(value)
        except ValueError as error:
            raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a number: {value!r}") from error
    if not math.isfinite(value):
        raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a finite number")
    return float(value)


# The texts of a flag cell (a table can have a flag as a text cell). Each other text stops the tool.
FLAG_TEXTS = {"": False, "0": False, "1": True, "false": False, "true": True}


def flag(sheet: Sheet, row: int, column: str) -> bool:
    value = sheet.cell(row, column)
    if isinstance(value, str):
        text = value.strip().lower()
        if text not in FLAG_TEXTS:
            raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a flag: {value!r}")
        return FLAG_TEXTS[text]
    return number(sheet, row, column) != 0.0


def rounded(value: float) -> float:
    return float(f"{value:.6g}")


def read_sheets(sources: Sources) -> dict[str, list[Sheet]]:
    """@return The data tables of the types that the tool uses, by type, in the byte order of their paths."""
    sheets: dict[str, list[Sheet]] = {kind: [] for kind in USED_TYPES}
    for path in sources.paths(TABLES_FOLDER, ".datasheet"):
        data = sources.read(path)
        if data is None:
            continue
        kind = sheet_type(data)
        if kind not in sheets:
            continue
        try:
            sheets[kind].append(Sheet(data))
        except (struct.error, ValueError, IndexError) as error:
            raise Failure(f"{path}: the table did not parse: {error}") from error
    sources.check_complete()
    for kind, tables in sheets.items():
        if not tables:
            raise Failure(f"the game copy has no data table of the type {kind}")
    return sheets


def add_row(rows: dict[str, list], row: list, what: str) -> None:
    """Adds a row by its lower-case id. The same id again with the same values is one row; with different values it stops the tool."""
    key = row[0].lower()
    if key in rows and rows[key][1:] != row[1:]:
        raise Failure(f"two rows of the {what} have the id {row[0]} with different values")
    rows.setdefault(key, row)


def build_data(sources: Sources) -> dict:
    sheets = read_sheets(sources)
    cooldowns: dict[str, list] = {}
    for sheet in sheets[COOLDOWN_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "AbilityID")).strip()
            if not identifier:
                raise Failure(f"row {row} of the cooldown table {sheet.name} has no AbilityID")
            time = number(sheet, row, "Time")
            minimum = number(sheet, row, "MinTime")
            if time < 0.0 or minimum < 0.0:
                raise Failure(f"the cooldown of {identifier} is negative")
            add_row(cooldowns, [identifier, rounded(time), rounded(minimum)], "cooldown tables")

    # The numbers of the status effects: the sorted CRC values of all rows, with each duplicate.
    all_crcs: list[int] = []
    status_rows: list[tuple[Sheet, int, str]] = []
    for sheet in sheets[STATUS_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "StatusID")).strip()
            if not identifier:
                continue  # The client adds only a row with an id (loader 0x44B4430).
            all_crcs.append(crc(identifier))
            status_rows.append((sheet, row, identifier))
    all_crcs.sort()
    statuses: dict[str, list] = {}
    for sheet, row, identifier in status_rows:
        index = bisect.bisect_left(all_crcs, crc(identifier))
        if index > MAX_STATUS_INDEX:
            raise Failure(f"the status effect {identifier} has the number {index}, above 16 bits")
        entry = [identifier, index, rounded(number(sheet, row, "BaseDuration")), rounded(number(sheet, row, "MoveSpeedMod")),
                 flag(sheet, row, "Stunned"), flag(sheet, row, "NoNav"), int(number(sheet, row, "StopOnHitCount")),
                 flag(sheet, row, "ShowInNameplates"), rounded(number(sheet, row, "DMGStandard"))]
        if entry[6] < 0:
            raise Failure(f"the status effect {identifier} has a negative StopOnHitCount")
        # The duplicate of the game (Sanctuary) has its first row.
        statuses.setdefault(identifier.lower(), entry)

    spells: dict[str, list] = {}
    for sheet in sheets[SPELL_TYPE]:
        for row in range(sheet.row_count):
            identifier = str(sheet.cell(row, "SpellID")).strip()
            if not identifier:
                continue
            count = number(sheet, row, "NumToSpawn")
            if count < 0 or count != int(count):
                raise Failure(f"the spell {identifier} has a NumToSpawn that is not a whole number")
            entry = [identifier, rounded(number(sheet, row, "Radius")), rounded(number(sheet, row, "Height")),
                     rounded(number(sheet, row, "CastDistance")), int(count), rounded(number(sheet, row, "SpawnRate")),
                     rounded(number(sheet, row, "Duration"))]
            spells.setdefault(identifier.lower(), entry)

    if not cooldowns:
        raise Failure("the cooldown tables have no row")
    data = {"format": 1, "statusRows": len(all_crcs),
            "counts": {"cooldowns": len(cooldowns), "statusEffects": len(statuses), "spells": len(spells)},
            "cooldowns": sorted(cooldowns.values()), "statusEffects": sorted(statuses.values(), key=lambda entry: entry[1]),
            "spells": sorted(spells.values())}
    return data


def print_found(data: dict, texts: list[str]) -> None:
    wanted = [text.lower() for text in texts]
    for name in ("cooldowns", "statusEffects", "spells"):
        for row in data[name]:
            if any(text in row[0].lower() for text in wanted):
                print(f"{name}: {row}")


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    arguments = sys.argv[1:]
    find = arguments[1:] if arguments[:1] == ["--find"] else None
    target = Path(arguments[0]) if arguments and find is None else gamedata / OUTPUT_NAME
    try:
        sources = Sources(game / "assets")
        data = build_data(sources)
        if find is not None:
            print_found(data, find)
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(data, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"abilities: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"abilities: {target}: {error}", file=sys.stderr)
        return 1
    counts = data["counts"]
    print(f"{target}: {counts['cooldowns']} cooldowns, {counts['statusEffects']} status effects (numbers of "
          f"{data['statusRows']} rows), {counts['spells']} spells "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
