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
  Expansions (ExpansionData)                     the limits of each expansion row (the largest level,
                                                 the gear score limits, the largest trade skill
                                                 level) and its entitlement; the row "None" also
                                                 gives levelCap and tradeSkillCap (no expansion)
  EntitlementData (EntitlementData)              the entitlements that each account gets (stage 1.1b,
                                                 decision of the user of 2026-10-15): the entitlement
                                                 of each expansion row and the DLC packs of Steam
                                                 (RETAIL_DLC_ROOTS and the entitlements that they
                                                 give), with the bit index of each one
  AchievementDataTable (AchievementData)         the bit index of each achievement that such an
                                                 entitlement names (the expansion: Expansion_2023_Unlock)
  Constitution (AttributeDefinition)             the health that each value of the attribute gives,
                                                 and the smallest and the largest value
  BaseVitals_Player (VitalsBaseData), "Player"   health, stamina, and mana of a player at the start
  CategoricalProgression                         the weapon masteries (rank table "WeaponMastery")
  (CategoricalProgressionData)                   and the trade skills (a rank table of the type
                                                 TradeskillRankData), with the largest rank
                                                 (MaxLevel) and the largest rank with the expansion
                                                 (Expansion01MaxLevel; 0: the row has none)
  the rank tables                                the experience of each rank, and for a weapon the
                                                 cost of a tree reset
  ProgressionPoints (ProgressionPointData)       the nodes of each weapon tree: the pool, and the
                                                 node that a node needs
  the weapon ability tables (AbilityData)        for each node: the tree, the row, the column, and
                                                 if it is an active ability

Output: {"format": 3, "levelCap": N, "tradeSkillCap": N,
"expansions": [[id, maxLevel, maxCraftGearScore, maxEquipGearScore, maxTradeSkillLevel, entitlement], ...],
"entitlements": [[alias, index, rewardType, rewards, achievement], ...],
"achievements": [[id, index], ...],
"levels": [[experience, attributePoints, health, resetCost], ...],
"attributes": {"minimum": N, "maximum": N, "constitutionHealth": [...], "abilities": {"focus": [[VALUE, [ABILITY, ...]], ...], ...}},
"vitals": {"health": N, "stamina": N, "mana": N},
"rankTables": {"<name>": {"experience": [...], "resetCost": [...]}, ...},
"progressions": [[id, kind, maxRank, rankTable, expansionMaxRank], ...],
"abilities": [[table, node, tree, row, column, active, requiredNode], ...],
"standings": [[territory, maxRank, rankTable, pointPool, autoRankUp], ...]}.
Stage 1.8d (format 3): `standings` has the territory standing rows of CategoricalProgression (the rank table Territory_Standing; the id
is the decimal territory id), and `rankTables` the table Territory_Standing with MaximumInfluence as `experience` and XpReward as
`xpReward`. A file of format 2 has no standing: the backend then refuses a quest reward of standing.
Entry K of `levels` (from 0) is the level K + 1 of the screen. `levelCap` is a level of the
screen. Entry K of `constitutionHealth` is the value `minimum` + K. `abilities` (stage 3.5b.2) has, for each attribute, the
threshold abilities of the EquipAbilities column: a character with the attribute at VALUE or above has them (the backend reads
their MaxMana and ManaRate: the mana of the client). `kind` is "weapon" or
"tradeskill". Entry R of a rank table is the experience that takes a character from the rank R
to the next rank. `entitlements` is the list of the entitlements that the backend gives each
account (the expansion first): `index` is the bit of the entitlement in the entitlement state of
the client (EntitlementIndex), `rewards` the text of the column Reward(s), `achievement` the
achievement that the entitlement gives each character (AchievementId; empty: none). The tool
stops (and keeps the old file) when an expansion names an entitlement that the table does not
have, an entitlement of the list is not enabled or names an achievement that the table does not
have, or a bit index is outside the range of the client. It also stops when an attribute table does not have one row
for each value of the attribute range of Constitution, in sequence (stage 2.2: code review of stage 3.5b.2, finding
LB-R7).

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
# The five attribute tables (AttributeDefinition; stage 3.5b.2, the mana of the client): the EquipAbilities of a value are the threshold
# abilities that a character with the attribute at that value or above has (Focus 100: Foc_Bonus_100_1 with MaxMana 0.1).
ATTRIBUTE_TABLES = (("strength", "Strength"), ("dexterity", "Dexterity"), ("intelligence", "Intelligence"), ("focus", "Focus"),
                    ("constitution", "Constitution"))
PLAYER_VITALS_TABLE = "BaseVitals_Player"
PLAYER_VITALS_ROW = "Player"
PROGRESSION_TABLE = "CategoricalProgression"
POINT_TABLE = "ProgressionPoints"
WEAPON_RANK_TABLE = "WeaponMastery"
# Stage 1.8d: the territory standing (decision D3 option B of the orchestrator): each categorical progression row with this rank table is
# the standing of the territory whose id is the decimal text of the row id ("6" is Windsward).
STANDING_RANK_TABLE = "Territory_Standing"
TRADE_SKILL_RANK_TYPE = "TradeskillRankData"
RANK_TYPES = ("CategoricalProgressionRankData", TRADE_SKILL_RANK_TYPE)
ABILITY_TYPE = "AbilityData"
ENTITLEMENT_TYPE = "EntitlementData"
ACHIEVEMENT_TYPE = "AchievementData"
# The table types that this tool reads the cells of.
USED_TYPES = ("ExperienceData", "ExpansionData", "AttributeDefinition", "VitalsBaseData",
              "CategoricalProgressionData", "ProgressionPointData", ABILITY_TYPE, ENTITLEMENT_TYPE,
              ACHIEVEMENT_TYPE) + RANK_TYPES
# The DLC packs of Steam (stage 1.1b; research dlc-entitlements.md, Q1): the Steam-named entitlement
# rows of the retail bonus set. A row with the reward type "Entitlement" gives the entitlements of its
# Reward(s) too (the tool adds them). Not in the list: the alpha test title (STEAM_APP_ID.1266160, a
# test participation) and the four WorldId rows (realm access). This list is a decision of the
# project from the research: the table has no column that names a DLC pack.
RETAIL_DLC_ROOTS = ("STEAM_APP_ID.1200566", "STEAM_APP_ID.1211760", "STEAM_APP_ID.1200560", "STEAM_APP_ID.1200561",
                    "STEAM_APP_ID.1200562", "STEAM_APP_ID.1200563", "STEAM_APP_ID.1200564", "STEAM_APP_ID.1200565",
                    "STEAM_APP_ID.1200567")
# The entitlement state of the client takes 575 bytes at most (reader at image RVA 0x3050A50): a bit
# index is below 575 * 8. The achievement state takes 0x2000000 bytes at most (0x279E550).
ENTITLEMENT_BIT_LIMIT = 575 * 8
ACHIEVEMENT_BIT_LIMIT = 0x2000000 * 8


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


def one_of_type(sheets: dict[str, Sheet], kinds: dict[str, str], kind: str) -> Sheet:
    names = [name for name, value in kinds.items() if value == kind]
    if len(names) != 1:
        raise Failure(f"the game data has {len(names)} tables of the type {kind}, not one")
    return sheets[names[0]]


def build_ownership(sheets: dict[str, Sheet], kinds: dict[str, str], expansions: Sheet,
                    level_count: int) -> tuple[list, list, list]:
    """@return The expansion rows, the entitlements of the grant (stage 1.1b), and the achievements that they name."""
    for column in ("ExpansionId", "MaxDisplayLevel", "MaxCraftGS", "MaxEquipGS", "MaxTradeskillLevel", "EntitlementId"):
        if column not in expansions.columns:
            raise Failure(f"the table {EXPANSION_TABLE} has no column {column}")
    table = one_of_type(sheets, kinds, ENTITLEMENT_TYPE)
    for column in ("UniqueTagID", "EntitlementIndex", "IsEnabled", "RewardType", "Reward(s)", "AchievementId"):
        if column not in table.columns:
            raise Failure(f"the table {table.name} has no column {column}")
    rows = {str(table.cell(row, "UniqueTagID")): row for row in range(table.row_count)}
    achievement_table = one_of_type(sheets, kinds, ACHIEVEMENT_TYPE)
    for column in ("AchievementID", "AchievementIndex"):
        if column not in achievement_table.columns:
            raise Failure(f"the table {achievement_table.name} has no column {column}")
    achievement_rows = {str(achievement_table.cell(row, "AchievementID")): row for row in range(achievement_table.row_count)}

    expansion_rows = []
    wanted: list[str] = []
    for row in range(expansions.row_count):
        maximum = whole(expansions.cell(row, "MaxDisplayLevel"))
        if maximum < 1 or maximum > level_count:
            raise Failure(f"row {row} of the table {EXPANSION_TABLE} has the level {maximum}: not a level of the level table")
        alias = str(expansions.cell(row, "EntitlementId"))
        expansion_rows.append([str(expansions.cell(row, "ExpansionId")), maximum, whole(expansions.cell(row, "MaxCraftGS")),
                               whole(expansions.cell(row, "MaxEquipGS")), whole(expansions.cell(row, "MaxTradeskillLevel")), alias])
        if alias:
            if alias not in rows:
                raise Failure(f"the expansion {expansion_rows[-1][0]} names the entitlement {alias}, which the table {table.name} does not have")
            wanted.append(alias)
    # The DLC packs: each root and the entitlements that an entitlement of the list gives (reward type
    # "Entitlement", the ids of Reward(s) with "+" between them).
    queue = list(RETAIL_DLC_ROOTS)
    while queue:
        alias = queue.pop(0)
        if alias in wanted:
            continue
        if alias not in rows:
            raise Failure(f"the DLC entitlement {alias} is not a row of the table {table.name}")
        wanted.append(alias)
        if str(table.cell(rows[alias], "RewardType")) == "Entitlement":
            queue.extend(part for part in str(table.cell(rows[alias], "Reward(s)")).split("+") if part)

    entitlements = []
    achievements = []
    for alias in wanted:
        row = rows[alias]
        index = whole(table.cell(row, "EntitlementIndex"))
        if not flag(table.cell(row, "IsEnabled")):
            raise Failure(f"the entitlement {alias} is not enabled")
        if index < 0 or index >= ENTITLEMENT_BIT_LIMIT:
            raise Failure(f"the entitlement {alias} has the bit index {index}: outside the range of the client")
        achievement = str(table.cell(row, "AchievementId"))
        if achievement and all(entry[0] != achievement for entry in achievements):
            if achievement not in achievement_rows:
                raise Failure(f"the entitlement {alias} names the achievement {achievement}, which the table "
                              f"{achievement_table.name} does not have")
            bit = whole(achievement_table.cell(achievement_rows[achievement], "AchievementIndex"))
            if bit < 0 or bit >= ACHIEVEMENT_BIT_LIMIT:
                raise Failure(f"the achievement {achievement} has the bit index {bit}: outside the range of the client")
            achievements.append([achievement, bit])
        entitlements.append([alias, index, str(table.cell(row, "RewardType")), str(table.cell(row, "Reward(s)")), achievement])
    return expansion_rows, entitlements, achievements


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
    expansion_rows, entitlements, achievements = build_ownership(sheets, kinds, expansions, len(levels))

    constitution = one_table(sheets, CONSTITUTION_TABLE)
    minimum = whole(constitution.cell(0, "Level"))
    health = []
    for row in range(constitution.row_count):
        if whole(constitution.cell(row, "Level")) != minimum + row:
            raise Failure(f"row {row} of the table {CONSTITUTION_TABLE} is not the value {minimum + row}")
        health.append(whole(constitution.cell(row, "Health")))

    # The threshold abilities of each attribute (stage 3.5b.2): [value, [ability ids]] for each row with EquipAbilities. Each attribute
    # table has one row for each value of the attribute range of Constitution, in sequence (stage 2.2, code review of stage 3.5b.2, finding
    # LB-R7): a table with a row that is not there would lose its thresholds with no error (Focus 100: the mana of the client).
    attribute_abilities: dict[str, list] = {}
    for name, table in ATTRIBUTE_TABLES:
        sheet = one_table(sheets, table)
        if "EquipAbilities" not in sheet.columns:
            raise Failure(f"the table {table} has no column EquipAbilities")
        if sheet.row_count != len(health):
            raise Failure(f"the table {table} has {sheet.row_count} rows, not one row for each value {minimum} to "
                          f"{minimum + len(health) - 1} of the attribute range of {CONSTITUTION_TABLE}")
        for row in range(sheet.row_count):
            if whole(sheet.cell(row, "Level")) != minimum + row:
                raise Failure(f"row {row} of the table {table} is not the value {minimum + row} of the attribute range")
        thresholds = []
        for row in range(sheet.row_count):
            text = str(sheet.cell(row, "EquipAbilities") or "").strip()
            if text:
                thresholds.append([whole(sheet.cell(row, "Level")), [part.strip() for part in text.split(",") if part.strip()]])
        attribute_abilities[name] = thresholds

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
    standing_rows: list = []
    rank_tables: dict[str, dict] = {}
    for row in range(progression_sheet.row_count):
        identifier = str(progression_sheet.cell(row, "CategoricalProgressionId"))
        table = str(progression_sheet.cell(row, "RankTableId"))
        if table == STANDING_RANK_TABLE:
            if not identifier.isdigit() or not 0 < int(identifier) <= 65535:
                raise Failure(f"the standing row {identifier} of the table {PROGRESSION_TABLE} is not a territory id")
            standing_rows.append([identifier, whole(progression_sheet.cell(row, "MaxLevel")), table,
                                  str(progression_sheet.cell(row, "ProgressionPointPool") or "").strip(),
                                  flag(progression_sheet.cell(row, "AutoRankUp"))])
            continue
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
        # The largest rank, and the largest rank with the expansion (the client takes a value that is not 0, 0x3CB38E0). The rank
        # table must have a row for each rank to the largest one: a short source stops the tool, the cap of the data stays (code
        # review of stage 1.1b, finding F3: a short Mining table gave the cap 200 in place of 250).
        if "Expansion01MaxLevel" not in progression_sheet.columns:
            raise Failure(f"the table {PROGRESSION_TABLE} has no column Expansion01MaxLevel")
        largest = whole(progression_sheet.cell(row, "MaxLevel"))
        expansion_largest = whole(progression_sheet.cell(row, "Expansion01MaxLevel"))
        ranks = len(rank_tables[table]["experience"])
        for column, value in (("MaxLevel", largest), ("Expansion01MaxLevel", expansion_largest)):
            if value >= ranks:
                raise Failure(f"the {column} {value} of {identifier} has no row in its rank table {table} (ranks 0 to {ranks - 1})")
        progressions.append([identifier, kind, largest, table, expansion_largest])
    # Stage 1.8d: the rank table of the standing: MaximumInfluence of each rank (the points of a rank to the next one; client 0x3CAACF0) and
    # XpReward. Each standing row must have a rank row for each rank to its MaxLevel.
    standings = []
    if standing_rows:
        sheet = one_table(sheets, STANDING_RANK_TABLE)
        influence, experience = [], []
        for rank in range(sheet.row_count):
            if whole(sheet.cell(rank, "Rank")) != rank:
                raise Failure(f"row {rank} of the rank table {STANDING_RANK_TABLE} is not the rank {rank}")
            influence.append(whole(sheet.cell(rank, "MaximumInfluence")))
            experience.append(whole(sheet.cell(rank, "XpReward")))
        rank_tables[STANDING_RANK_TABLE] = {"experience": influence, "xpReward": experience}
        for identifier, largest, table, pool, auto in standing_rows:
            if largest >= len(influence):
                raise Failure(f"the MaxLevel {largest} of the standing {identifier} has no row in its rank table {table}")
            standings.append([identifier, largest, table, pool, auto])
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
    return {"format": 3, "levelCap": level_cap, "tradeSkillCap": trade_skill_cap, "expansions": expansion_rows,
            "entitlements": entitlements, "achievements": achievements, "levels": levels,
            "attributes": {"minimum": minimum, "maximum": minimum + len(health) - 1, "constitutionHealth": health,
                           "abilities": attribute_abilities},
            "vitals": vitals, "rankTables": rank_tables, "progressions": progressions, "abilities": abilities,
            "standings": standings}


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
    owned = [row for row in tables["expansions"] if row[5]]
    print(f"{target}: {len(tables['levels'])} levels (the largest level with no expansion is {tables['levelCap']}; "
          + ", ".join(f"{row[0]}: level {row[1]}, equip gear score {row[3]}, craft gear score {row[2]}, trade skill "
                      f"level {row[4]}" for row in owned) + "), "
          f"{len(tables['entitlements'])} entitlements of each account, {len(tables['achievements'])} achievement(s) of each "
          f"character, "
          f"{len(tables['attributes']['constitutionHealth'])} values of an attribute, {weapons} weapon masteries, "
          f"{len(tables['progressions']) - weapons} trade skills (the largest level is {tables['tradeSkillCap']}), "
          f"{len(tables['rankTables'])} rank tables, {len(tables['abilities'])} nodes of the weapon trees "
          f"(source: {source[1]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
