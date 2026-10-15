#!/usr/bin/env python3
"""Write the progression tables of the game as one JSON file.

The backend reads this file and gets the rules of the level, of the attributes, of the weapon
mastery, and of the trade skills of a character (protocol notes 4.12 and 4.13). Without the
file the backend starts and works: it sends the stored values, and it refuses a command that
needs a table (a level, experience, an attribute, a mastery level, a trade skill level).

Input: the data tables of the game copy, from the same sources as the item catalog tool
(item_catalog.py: the pak entries, the pak entries with the Oodle helper, or the files that
tools/research/pakx.py extracted).

  Table (type)                                   What the file gets
  XPLevels (ExperienceData)                      for each level: the total experience at its start,
                                                 the attribute points of the level, the health of
                                                 the level, the cost of an attribute reset
  Expansions (ExpansionData), row "None"         the largest level and the largest trade skill level
                                                 of a player with no expansion
  Constitution (AttributeDefinition)             the health that each value of the attribute gives,
                                                 and the smallest and the largest value
  BaseVitals_Player (VitalsBaseData), "Player"   health, stamina, and mana of a player at the start
  CategoricalProgression                         the weapon masteries (rank table "WeaponMastery")
  (CategoricalProgressionData)                   and the trade skills (a rank table of the type
                                                 TradeskillRankData), with the largest rank
  the rank tables                                the experience of each rank, and for a weapon the
                                                 cost of a tree reset
  ProgressionPoints (ProgressionPointData)       the nodes of each weapon tree: the pool, and the
                                                 node that a node needs
  the weapon ability tables (AbilityData)        for each node: the tree, the row, the column, and
                                                 if it is an active ability

Output: {"format": 1, "levelCap": N, "tradeSkillCap": N,
"levels": [[experience, attributePoints, health, resetCost], ...],
"attributes": {"minimum": N, "maximum": N, "constitutionHealth": [...]},
"vitals": {"health": N, "stamina": N, "mana": N},
"rankTables": {"<name>": {"experience": [...], "resetCost": [...]}, ...},
"progressions": [[id, kind, maxRank, rankTable], ...],
"abilities": [[table, node, tree, row, column, active, requiredNode], ...]}.
Entry K of `levels` (from 0) is the level K + 1 of the screen. `levelCap` is a level of the
screen. Entry K of `constitutionHealth` is the value `minimum` + K. `kind` is "weapon" or
"tradeskill". Entry R of a rank table is the experience that takes a character from the rank R
to the next rank.

Usage: progression_tables.py [OUTPUT.json]
Default output: <SOULWARDEN_GAMEDATA_DIR>/progression-tables.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_DATATABLES_DIR, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC (see item_catalog.py)
"""

from __future__ import annotations

import json
import math
import os
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure  # noqa: E402
from item_catalog import (  # noqa: E402
    TABLES_FOLDER,
    Sheet,
    sheet_type,
    table_entries,
    tables_of_folder,
    tables_of_paks,
)

OUTPUT_NAME = "progression-tables.json"
LEVEL_TABLE = "XPLevels"
EXPANSION_TABLE = "Expansions"
NO_EXPANSION_ROW = "None"
CONSTITUTION_TABLE = "Constitution"
PLAYER_VITALS_TABLE = "BaseVitals_Player"
PLAYER_VITALS_ROW = "Player"
PROGRESSION_TABLE = "CategoricalProgression"
POINT_TABLE = "ProgressionPoints"
WEAPON_RANK_TABLE = "WeaponMastery"
TRADE_SKILL_RANK_TYPE = "TradeskillRankData"
RANK_TYPES = ("CategoricalProgressionRankData", TRADE_SKILL_RANK_TYPE)
ABILITY_TYPE = "AbilityData"
# The table types that this tool reads the cells of.
USED_TYPES = ("ExperienceData", "ExpansionData", "AttributeDefinition", "VitalsBaseData",
              "CategoricalProgressionData", "ProgressionPointData", ABILITY_TYPE) + RANK_TYPES


def whole(value) -> int:
    """@return The whole number of a cell. Some tables have a number as a text ("856")."""
    if isinstance(value, str):
        try:
            return int(float(value))
        except ValueError:
            return 0
    return int(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else 0


def flag(value) -> bool:
    """@return The value of a flag cell. Some tables have a flag as a text."""
    if isinstance(value, str):
        return value.strip().lower() in ("true", "1")
    return bool(value)


def one_table(sheets: dict[str, Sheet], name: str) -> Sheet:
    sheet = sheets.get(name)
    if sheet is None:
        raise Failure(f"no data table with the name {name}")
    return sheet


def row_of(sheet: Sheet, column: str, key: str) -> int:
    for row in range(sheet.row_count):
        if str(sheet.cell(row, column)) == key:
            return row
    raise Failure(f"the table {sheet.name} has no row {key}")


def build_tables(contents: list[bytes]) -> dict:
    sheets: dict[str, Sheet] = {}
    kinds: dict[str, str] = {}
    for data in contents:
        kind = sheet_type(data)
        if kind not in USED_TYPES:
            continue
        try:
            sheet = Sheet(data)
        except (struct.error, ValueError, IndexError) as error:
            raise Failure(f"a data table of the type {kind} did not parse: {error}") from error
        sheets.setdefault(sheet.name, sheet)
        kinds.setdefault(sheet.name, kind)

    levels_sheet = one_table(sheets, LEVEL_TABLE)
    levels = []
    for row in range(levels_sheet.row_count):
        if whole(levels_sheet.cell(row, "Level Number")) != row:
            raise Failure(f"row {row} of the table {LEVEL_TABLE} is not the level {row}")
        levels.append([whole(levels_sheet.cell(row, "XPToLevel")), whole(levels_sheet.cell(row, "AttributePoints")),
                       whole(levels_sheet.cell(row, "Health")), whole(levels_sheet.cell(row, "AttributeRespecCost"))])
    if not levels or levels[0][0] != 0 or any(levels[row][0] <= levels[row - 1][0] for row in range(1, len(levels))):
        raise Failure(f"the experience column of the table {LEVEL_TABLE} does not start at 0 and go up")

    expansions = one_table(sheets, EXPANSION_TABLE)
    expansion = row_of(expansions, "ExpansionId", NO_EXPANSION_ROW)
    level_cap = min(whole(expansions.cell(expansion, "MaxDisplayLevel")), len(levels))
    trade_skill_cap = whole(expansions.cell(expansion, "MaxTradeskillLevel"))
    if level_cap < 1:
        raise Failure(f"the row {NO_EXPANSION_ROW} of the table {EXPANSION_TABLE} has no level")

    constitution = one_table(sheets, CONSTITUTION_TABLE)
    minimum = whole(constitution.cell(0, "Level"))
    health = []
    for row in range(constitution.row_count):
        if whole(constitution.cell(row, "Level")) != minimum + row:
            raise Failure(f"row {row} of the table {CONSTITUTION_TABLE} is not the value {minimum + row}")
        health.append(whole(constitution.cell(row, "Health")))

    vitals_sheet = one_table(sheets, PLAYER_VITALS_TABLE)
    vitals_row = row_of(vitals_sheet, "VitalsID", PLAYER_VITALS_ROW)
    vitals = {"health": whole(vitals_sheet.cell(vitals_row, "HealthBaseMax")),
              "stamina": whole(vitals_sheet.cell(vitals_row, "StaminaBaseMax")),
              "mana": whole(vitals_sheet.cell(vitals_row, "ManaBaseMax"))}
    # The mana regeneration of a player (stage 1.3b, I14): the rate (mana in one second) and the delays after a spend
    # and after a spend to 0 (seconds). A column that is not there stops the tool.
    for member, column in (("manaRegenRate", "ManaBaseRegenRate"), ("manaRegenDelay", "ManaRegenDelay"),
                           ("manaRegenDelayOnEmpty", "ManaRegenDelayOnEmpty")):
        if column not in vitals_sheet.columns:
            raise Failure(f"the table {PLAYER_VITALS_TABLE} has no column {column}")
        value = vitals_sheet.cell(vitals_row, column)
        value = float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else float(str(value).strip() or 0)
        if not math.isfinite(value) or value < 0:
            raise Failure(f"the value {column} of the row {PLAYER_VITALS_ROW} is not a number of 0 or more")
        vitals[member] = round(value, 6)

    # The weapon masteries and the trade skills, with the rank table of each one.
    progression_sheet = one_table(sheets, PROGRESSION_TABLE)
    progressions = []
    rank_tables: dict[str, dict] = {}
    for row in range(progression_sheet.row_count):
        identifier = str(progression_sheet.cell(row, "CategoricalProgressionId"))
        table = str(progression_sheet.cell(row, "RankTableId"))
        if table == WEAPON_RANK_TABLE:
            kind = "weapon"
        elif kinds.get(table) == TRADE_SKILL_RANK_TYPE:
            kind = "tradeskill"
        else:
            continue
        sheet = one_table(sheets, table)
        if table not in rank_tables:
            key = "Rank" if "Rank" in sheet.columns else "Level"
            experience, cost = [], []
            for rank in range(sheet.row_count):
                if whole(sheet.cell(rank, key)) != rank:
                    raise Failure(f"row {rank} of the rank table {table} is not the rank {rank}")
                experience.append(whole(sheet.cell(rank, "MaximumInfluence")))
                cost.append(whole(sheet.cell(rank, "AzothRespecCost")))
            rank_tables[table] = {"experience": experience}
            if kind == "weapon":
                rank_tables[table]["resetCost"] = cost
        largest = min(whole(progression_sheet.cell(row, "MaxLevel")), len(rank_tables[table]["experience"]) - 1)
        progressions.append([identifier, kind, largest, table])
    weapons = {entry[0].lower() for entry in progressions if entry[1] == "weapon"}
    if not weapons:
        raise Failure(f"the table {PROGRESSION_TABLE} has no weapon mastery")

    # The nodes of the weapon trees: a row of the points table whose pool is a weapon, with the
    # place of its ability in the tree.
    places: dict[str, tuple] = {}
    for name, sheet in sheets.items():
        if kinds[name] != ABILITY_TYPE or name.lower() not in weapons:
            continue
        for row in range(sheet.row_count):
            ability = str(sheet.cell(row, "AbilityID"))
            places.setdefault(ability.lower(), (whole(sheet.cell(row, "TreeId")), whole(sheet.cell(row, "TreeRowPosition")),
                                                whole(sheet.cell(row, "TreeColumnPosition")),
                                                flag(sheet.cell(row, "IsActiveAbility"))))
    point_sheet = one_table(sheets, POINT_TABLE)
    abilities = []
    for row in range(point_sheet.row_count):
        pool = str(point_sheet.cell(row, "PointPoolId"))
        if pool.lower() not in weapons:
            continue
        node = str(point_sheet.cell(row, "ProgressionPointId"))
        place = places.get(node.lower())
        if place is None:
            raise Failure(f"the node {node} of the pool {pool} is not a row of a weapon ability table")
        # The pool id of the progression row has the letter case of the id on the wire.
        table = next(entry[0] for entry in progressions if entry[0].lower() == pool.lower())
        abilities.append([table, node, place[0], place[1], place[2], place[3],
                          str(point_sheet.cell(row, "RequiredProgressionPointId"))])
    if not abilities:
        raise Failure(f"the table {POINT_TABLE} has no node of a weapon tree")
    return {"format": 1, "levelCap": level_cap, "tradeSkillCap": trade_skill_cap, "levels": levels,
            "attributes": {"minimum": minimum, "maximum": minimum + len(health) - 1, "constitutionHealth": health},
            "vitals": vitals, "rankTables": rank_tables, "progressions": progressions, "abilities": abilities}


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    extracted = Path(os.environ.get("SOULWARDEN_DATATABLES_DIR") or home / "Games/soulwarden/work/assets/extract" / TABLES_FOLDER)
    arguments = sys.argv[1:]
    if arguments[:1] in (["-h"], ["--help"]):
        print(__doc__.strip())
        return 0
    target = Path(arguments[0]) if arguments else gamedata / OUTPUT_NAME
    try:
        entries = table_entries(game / "assets")
        source = tables_of_paks(entries) or tables_of_folder(extracted, entries)
        tables = build_tables(source[0])
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(tables, separators=(",", ":")) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"progression: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"progression: {target}: {error}", file=sys.stderr)
        return 1
    weapons = sum(1 for entry in tables["progressions"] if entry[1] == "weapon")
    print(f"{target}: {len(tables['levels'])} levels (the largest level of a player is {tables['levelCap']}), "
          f"{len(tables['attributes']['constitutionHealth'])} values of an attribute, {weapons} weapon masteries, "
          f"{len(tables['progressions']) - weapons} trade skills (the largest level is {tables['tradeSkillCap']}), "
          f"{len(tables['rankTables'])} rank tables, {len(tables['abilities'])} nodes of the weapon trees "
          f"(source: {source[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
