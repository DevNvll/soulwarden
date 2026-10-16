#!/usr/bin/env python3
"""Write the loot tables and the kill rewards of the game as one JSON file.

The backend reads this file and gets what the rewards of a kill need (protocol notes 4.21): the
experience and the coin of a kill for each creature level, the reward modifiers of each creature
type, the level difference rows, the credit values of the game, the loot data of each vitals row
(may it drop loot, the outer drop chance, its loot table, its loot tags), the loot tables and the
loot buckets that a creature can reach, and the gear score ranges of a level. Without the file
the backend starts and works, and a kill gives nothing.

Input: the game copy, from the same sources as the creature data tool (creature_data.py: the pak
entries, the pak entries with an Oodle library, or the files that tools/research/pakx.py extracted).

  Source (table type)                             What the file gets
  Rewards (RewardData)                            for each creature level: KillExperience, KillCurrency
                                                  (hundredths of a coin), KillProgressionCurrency
  RewardModifiers (RewardModifierData)            for each creature type (Solo, Elite, ...): the factors
                                                  of the experience, of the coin, and of the loot drop,
                                                  the coin chance, and ProgressionCurrencyAmount (null for
                                                  an empty cell: this build has no value)
  AILevelDisparity (LevelDisparityData)           for each level difference (creature minus character):
                                                  KillExpModifier and MaxRewardLevelDelta
  the game events (GameEventData)                 the coin chance of KillEvent and of each kill event that
                                                  a vitals row names in KillEventIdOverride
  XPLevels (ExperienceData)                       the gear score ranges GSLimitT1 to GSLimitT5 of each level
  VitalsLevels (VitalsLevelData)                  AILootGSCeiling of each creature level
  the vitals tables (VitalsBaseData,              for each base row: CanDropLoot, LootDropChance,
  VitalsLevelVariantData)                         IgnoreLootDropModifier, LootTableId, LootTags,
                                                  NoExpNoCoin, KillEventIdOverride, CreatureType; and the
                                                  base row of each level variant row
  the loot tables (LootTablesData)                each table that a vitals row can reach: AND or OR, the
                                                  roll bonus setting, MaxRoll (the row NAME_Probs), the
                                                  conditions, ConditionOverridesRoll, UseLevelGS, GSBonus,
                                                  and each entry with its kind (table, bucket, item), its
                                                  threshold (NAME_Probs), its quantity range (NAME_Qty), and
                                                  its gear score range
  the loot buckets (LootBucketData)               each bucket that such a table names: each candidate
                                                  with its item, its quantity range, its odds, MatchOne, and
                                                  its tags (a name, or a name with a number range)
  sharedassets/genericassets/                     the credit data of the game (Event Credit Data): the time
  playerbaseattributes.pbadb                      limit, the part of the health, the distance, and the
                                                  modifiers of the experience, of the loot, and of the coin
                                                  for 1 to 10 characters with credit

Output: {"format": 1,
"rewards": [[level, killExperience, killCurrency, killProgressionCurrency], ...]   (levels 1 to N),
"modifiers": {"<creature type>": [experience, currency, currencyChance, lootDrop, progression|null], ...},
"disparity": [[difference, killExpModifier, maxRewardLevelDelta], ...],
"events": {"<event id in lower case>": coinChance, ...}   (KillEvent and the overrides),
"credit": {"timeLimit": S, "healthShare": F, "range": M, "receivers": N, "modifiers": [[xp, loot, coin], ...]},
"gearScore": [[t1min, t1max, ..., t5min, t5max], ...]   (entry K: the level K + 1),
"lootCeiling": [[level, ceiling], ...],
"bases": [[id, creatureType, canDropLoot, dropChance, ignoreDropModifier, table, [tags], noExpNoCoin, event], ...],
"vitals": {"<vitals row id in lower case>": base, ...},
"tables": {"<table id>": [and, bonus, maxRoll, [conditions], overridesRoll, useLevelGS, gsBonus,
           [[kind, id, threshold, quantityMin, quantityMax, gearScoreMin, gearScoreMax], ...]], ...},
"buckets": {"<bucket id>": [[item, quantityMin, quantityMax, odds, matchOne, [[tag] | [tag, low, high], ...]], ...]},
"counts": {...}}
`and` is 1 for AND and 0 for OR. `bonus` is 0 AddToRoll, 1 ClampMax, 2 IgnoreBonus. `kind` is "t" (a table),
"b" (a bucket), or "i" (an item). A gear score range of 0 and 0 is no range. The entries of a table are in the
sequence of their thresholds (the client sorts them so, image RVA 0x366F799). A tag of one number ("Level:5") has
the range from that number to 10000 (the bucket parser of the client, 0x4458900, with its default 10000).

Usage: loot_tables.py [OUTPUT.json]
       loot_tables.py --find TEXT...   print the loot data of the vitals rows and of the tables with a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/loot-tables.json
Environment: SOULWARDEN_GAME_DIR (default ~/Games/soulwarden/game),
             SOULWARDEN_GAMEDATA_DIR (default ~/.local/share/soulwarden/gamedata),
             SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL, SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR (see creature_data.py)
"""

from __future__ import annotations

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
from creature_data import TABLES_FOLDER, Sources, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "loot-tables.json"
BASE_ATTRIBUTES_PATH = "sharedassets/genericassets/playerbaseattributes.pbadb"
LOOT_TABLE_TYPE = "LootTablesData"
LOOT_BUCKET_TYPE = "LootBucketData"
VARIANT_TYPE = "VitalsLevelVariantData"
BASE_TYPE = "VitalsBaseData"
REWARD_TYPE = "RewardData"
MODIFIER_TYPE = "RewardModifierData"
DISPARITY_TYPE = "LevelDisparityData"
EVENT_TYPE = "GameEventData"
EXPERIENCE_TYPE = "ExperienceData"
VITALS_LEVEL_TYPE = "VitalsLevelData"
# Stage 3.5b.1: the loot roots of the chests (the loot container variation table) and the loot limits (exported, not used:
# docs/systems/chests.md).
VARIATION_TYPE = "VariationData"
LIMIT_TYPE = "LootLimitData"
USED_TYPES = (LOOT_TABLE_TYPE, LOOT_BUCKET_TYPE, VARIANT_TYPE, BASE_TYPE, REWARD_TYPE, MODIFIER_TYPE, DISPARITY_TYPE,
              EVENT_TYPE, EXPERIENCE_TYPE, VITALS_LEVEL_TYPE, VARIATION_TYPE, LIMIT_TYPE)
# The loot container variation table of the chests (the variation component of slices/master_lootcontainer.dynamicslice; the chest
# tool, chests.py): the root of each row. Each chest placement of the main world names a row of it (the census of chests.py). The
# older table LootContainers names roots that no loot table has (POI_LootLarge and others) and no placement uses: not read.
CHEST_VARIATION_TABLES = {"Gatherable_LootContainers": "LootTableComponent"}
# The root of a chest with no loot (a quest or a script chest): no table.
EMPTY_ROOT = "Empty"
# Roots of the chest variation table that no loot table of the game has (facts of the game data of the supported build: rows of
# seasonal and event props, Darkness_StarterPack, LNY_Tribute_Table_Variant_01, Season_05_Trial_Entrance_Dancefloor; no chest
# placement of the main world names them). A different root with no table stops the tool.
CHEST_ROOTS_NOT_IN_GAME = frozenset({"DarknessStarterPack", "Halloween_TreatBasket", "SummerMedley_DancefloorBoogie"})
# The one table of each of these types that the rewards of a kill use (the game has more tables of some types).
REWARD_TABLE = "Rewards"
MODIFIER_TABLE = "RewardModifiers"
DISPARITY_TABLE = "AILevelDisparity"
EXPERIENCE_TABLE = "XPLevels"
VITALS_LEVEL_TABLE = "VitalsLevels"
KILL_EVENT = "KillEvent"
TABLE_PREFIX = "[LTID]"
BUCKET_PREFIX = "[LBID]"
# The largest value of a tag with one number (the default of the bucket parser of the client).
TAG_LIMIT = 10000
GEAR_SCORE_TIERS = 5
# The credit data of the base attributes file (the field id is the CRC-32 of the name in lower case).
CREDIT_DATA = "Event Credit Data"
CREDIT_FIELDS = {"timeLimit": "Credit Time Limit Seconds", "healthShare": "Credit Health Threshold Percentage",
                 "range": "Credit Range Limit Meters"}
CREDIT_MODIFIERS = "Event Credit Modifiers"
MODIFIER_FIELDS = ("Xp Modifier", "Loot Modifier", "Currency Modifier")
RECEIVERS_FIELD = "Max AI Loot Receiver Count"
TRUE_TEXTS = frozenset({"true", "1"})
# Texts of a flag cell of the vitals tables that are not TRUE, FALSE, 1, 0, or empty, with the value that the tool
# gives them. They are facts of the game data (a typing fault of a designer), not faults of a source.
FLAG_TEXTS_NOT_IN_GAME = {"trule": True}
# Range texts of the loot buckets with a typing fault of a designer, and the range that the tool gives them (the
# first two numbers).
RANGE_TEXTS_NOT_IN_GAME = {"49-59-59": "49-59"}
# Texts of the cell LootDropChance that are not a number: no loot drops (the chance 0). A fact of the game data.
CHANCE_TEXTS_NOT_IN_GAME = frozenset({"z"})
# Variant vitals rows whose base row the game does not have (a fact of the game data of the supported build): the
# creature tool leaves out such a kind too. A different variant with no base row stops the tool (code review of stage
# 3.5a, finding F3).
VARIANTS_WITH_NO_BASE_IN_GAME = frozenset({"werewolf_rabid_20328", "upyr_10221a_named", "upyr_10220a", "upyr_10220b"})


def is_true(value) -> bool:
    if isinstance(value, bool):
        return value
    text = str(value).strip().lower()
    if text in TRUE_TEXTS:
        return True
    # "none" is not a flag text (code review of stage 3.5a, finding F4).
    if text in ("", "false", "0"):
        return False
    if text in FLAG_TEXTS_NOT_IN_GAME:
        return FLAG_TEXTS_NOT_IN_GAME[text]
    raise Failure(f"the flag text \"{value}\" is not TRUE or FALSE")


def number_of(text: str, what: str, empty: float = 0.0) -> float:
    """@return The number of a source text. An empty text is `empty`; a text that is not a finite number stops the tool."""
    text = str(text).strip()
    if not text:
        return empty
    try:
        value = float(text)
    except ValueError as error:
        raise Failure(f"{what} is the text \"{text}\", not a number") from error
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return value


def rounded(value: float) -> float:
    """A cell is a 32-bit float: 7 digits give the same float again, and the file is shorter."""
    return float(f"{value:.7g}")


def column(sheet: Sheet, name: str) -> None:
    if name not in sheet.columns:
        raise Failure(f"the table {sheet.name} has no column {name}")


def range_of(text: str, what: str, default_high: int | None = None) -> tuple[int, int]:
    """@return The range of a text "N" or "N-M" (the quantity parser of the client, image RVA 0x31E43A0). An empty
    text is no range (0, 0). With `default_high`, "N" is the range from N to that value (a tag of a bucket)."""
    text = str(text).strip()
    if not text:
        return 0, 0
    if text in RANGE_TEXTS_NOT_IN_GAME:
        text = RANGE_TEXTS_NOT_IN_GAME[text]
    match = re.fullmatch(r"(\d+)\s*(?:-\s*(\d+))?", text)
    if match is None:
        raise Failure(f"{what} is the text \"{text}\", not a number or a range")
    low = int(match[1])
    if match[2] is not None:
        high = int(match[2])
    else:
        high = default_high if default_high is not None else low
    if high < low:
        raise Failure(f"{what} is the range \"{text}\", whose end is below its start")
    return low, high


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


def one_table(sheets: dict[str, list[Sheet]], kind: str, name: str) -> Sheet:
    found = [sheet for sheet in sheets[kind] if sheet.name == name]
    if len(found) != 1:
        raise Failure(f"the game copy has {len(found)} tables {name} of the type {kind}, not 1")
    return found[0]


def read_credit(sources: Sources) -> dict:
    roots = sources.stream(BASE_ATTRIBUTES_PATH)
    if roots is None:
        raise Failure(f"the game copy has no source for {BASE_ATTRIBUTES_PATH}")
    data = None
    receivers = None
    for root in roots:
        for element in walk(root):
            if element.name == field_id(CREDIT_DATA) and data is None:
                data = element
            elif element.name == field_id(RECEIVERS_FIELD) and element.value is not None and receivers is None:
                receivers = int.from_bytes(element.value, "big")
    if data is None or receivers is None:
        raise Failure(f"{BASE_ATTRIBUTES_PATH} has no \"{CREDIT_DATA}\" or no \"{RECEIVERS_FIELD}\"")
    values = {child.name: child for child in data.children}

    def real(element, label: str) -> float:
        if element is None or element.value is None or len(element.value) != 4:
            raise Failure(f"the field \"{label}\" of {BASE_ATTRIBUTES_PATH} is not a 32-bit float")
        value = struct.unpack(">f", element.value)[0]
        if not math.isfinite(value):
            raise Failure(f"the field \"{label}\" of {BASE_ATTRIBUTES_PATH} is not finite")
        return rounded(value)

    def whole(element, label: str) -> int:
        if element is None or element.value is None:
            raise Failure(f"{BASE_ATTRIBUTES_PATH} has no field \"{label}\"")
        return int.from_bytes(element.value, "big")

    credit = {"timeLimit": whole(values.get(field_id(CREDIT_FIELDS["timeLimit"])), CREDIT_FIELDS["timeLimit"]),
              "healthShare": real(values.get(field_id(CREDIT_FIELDS["healthShare"])), CREDIT_FIELDS["healthShare"]),
              "range": whole(values.get(field_id(CREDIT_FIELDS["range"])), CREDIT_FIELDS["range"]),
              "receivers": receivers, "modifiers": []}
    listing = values.get(field_id(CREDIT_MODIFIERS))
    if listing is None or not listing.children:
        raise Failure(f"{BASE_ATTRIBUTES_PATH} has no \"{CREDIT_MODIFIERS}\"")
    for entry in listing.children:
        fields = {child.name: child for child in entry.children}
        credit["modifiers"].append([real(fields.get(field_id(label)), label) for label in MODIFIER_FIELDS])
    return credit


def read_rewards(sheets: dict[str, list[Sheet]]) -> dict:
    rewards = one_table(sheets, REWARD_TYPE, REWARD_TABLE)
    for name in ("Level", "KillExperience", "KillCurrency", "KillProgressionCurrency"):
        column(rewards, name)
    rows = []
    for row in range(rewards.row_count):
        what = f"row {row} of the table {REWARD_TABLE}"
        level = int(number_of(rewards.source(row, "Level"), what))
        if level != len(rows) + 1:
            raise Failure(f"{what} is the level {level}, not {len(rows) + 1}")
        values = [number_of(rewards.source(row, name), what) for name in ("KillExperience", "KillCurrency", "KillProgressionCurrency")]
        # The range of the backend: not negative (code review of stage 3.5a, finding F4). The whole part is the value
        # (a cell of the game has the text "340.00000000000006").
        if any(value < 0 or value > 0xFFFFFFFF for value in values):
            raise Failure(f"{what} has a reward that is negative or too large")
        rows.append([level] + [int(value) for value in values])

    modifiers_sheet = one_table(sheets, MODIFIER_TYPE, MODIFIER_TABLE)
    names = ("Modifiers", "Experience", "Currency", "CurrencyRewardChance", "LootDropModifier", "ProgressionCurrencyAmount")
    for name in names:
        column(modifiers_sheet, name)
    modifiers = {}
    for row in range(modifiers_sheet.row_count):
        what = f"row {row} of the table {MODIFIER_TABLE}"
        name = modifiers_sheet.source(row, "Modifiers").strip()
        if not name:
            continue
        progression = modifiers_sheet.source(row, "ProgressionCurrencyAmount").strip()
        # A blank chance is 0 in the client (loot.md 5.1): the chance of the event applies.
        modifiers[name] = [rounded(number_of(modifiers_sheet.source(row, "Experience"), what)),
                           rounded(number_of(modifiers_sheet.source(row, "Currency"), what)),
                           rounded(number_of(modifiers_sheet.source(row, "CurrencyRewardChance"), what)),
                           rounded(number_of(modifiers_sheet.source(row, "LootDropModifier"), what, 1.0)),
                           rounded(number_of(progression, what)) if progression else None]
        # The ranges of the backend (code review of stage 3.5a, finding F4): no negative factor, a chance from 0 to 1.
        factors = modifiers[name]
        if min(factors[0], factors[1], factors[3]) < 0 or not 0 <= factors[2] <= 1 or (factors[4] is not None and factors[4] < 0):
            raise Failure(f"the reward factors of the creature type {name} ({what}) are not in the range of the backend")

    disparity_sheet = one_table(sheets, DISPARITY_TYPE, DISPARITY_TABLE)
    for name in ("LevelDisparity", "KillExpModifier", "MaxRewardLevelDelta"):
        column(disparity_sheet, name)
    disparity = []
    for row in range(disparity_sheet.row_count):
        what = f"row {row} of the table {DISPARITY_TABLE}"
        disparity.append([int(number_of(disparity_sheet.source(row, "LevelDisparity"), what)),
                          rounded(number_of(disparity_sheet.source(row, "KillExpModifier"), what)),
                          int(number_of(disparity_sheet.source(row, "MaxRewardLevelDelta"), what))])
    if len({entry[0] for entry in disparity}) != len(disparity):
        raise Failure(f"the table {DISPARITY_TABLE} has a level difference two times")

    experience = one_table(sheets, EXPERIENCE_TYPE, EXPERIENCE_TABLE)
    column(experience, "Level Number")
    gear_score = []
    for row in range(experience.row_count):
        what = f"row {row} of the table {EXPERIENCE_TABLE}"
        if int(number_of(experience.source(row, "Level Number"), what)) != row:
            raise Failure(f"{what} is not the level number {row}")
        ranges = []
        for tier in range(1, GEAR_SCORE_TIERS + 1):
            column(experience, f"GSLimitT{tier}")
            low, high = range_of(experience.source(row, f"GSLimitT{tier}"), f"the cell GSLimitT{tier} of {what}")
            ranges += [low, high]
        gear_score.append(ranges)

    vitals_levels = one_table(sheets, VITALS_LEVEL_TYPE, VITALS_LEVEL_TABLE)
    for name in ("Level", "AILootGSCeiling", "ContainerLootGSFloor"):
        column(vitals_levels, name)
    ceiling = []
    floor = []
    for row in range(vitals_levels.row_count):
        what = f"row {row} of the table {VITALS_LEVEL_TABLE}"
        level = int(number_of(vitals_levels.source(row, "Level"), what))
        ceiling.append([level, int(number_of(vitals_levels.source(row, "AILootGSCeiling"), what))])
        # The gear score floor of a loot container of a level (stage 3.5b.1; research chests-ground-loot.md, "Loot and gear score").
        floor.append([level, int(number_of(vitals_levels.source(row, "ContainerLootGSFloor"), what))])
    return {"rewards": rows, "modifiers": modifiers, "disparity": sorted(disparity), "gearScore": gear_score,
            "lootCeiling": ceiling, "containerFloor": floor}


def read_chest_roots(sheets: dict[str, list[Sheet]]) -> list[str]:
    """@return The loot roots of the chests (stage 3.5b.1): each root of the loot container variation table, but Empty."""
    roots: set[str] = set()
    found = set()
    for sheet in sheets[VARIATION_TYPE]:
        root_column = CHEST_VARIATION_TABLES.get(sheet.name)
        if root_column is None:
            continue
        found.add(sheet.name)
        column(sheet, root_column)
        for row in range(sheet.row_count):
            root = sheet.source(row, root_column).strip()
            if root and root != EMPTY_ROOT:
                roots.add(root)
    for name in CHEST_VARIATION_TABLES:
        if name not in found:
            raise Failure(f"the game copy has no variation table {name}")
    return sorted(roots)


def read_limits(sheets: dict[str, list[Sheet]]) -> dict:
    """@return The loot limits of the game (stage 3.5b.1: exported, not used by the backend; docs/systems/chests.md)."""
    limits = {}
    for sheet in sheets[LIMIT_TYPE]:
        for name in ("LootLimitID", "CountLimit", "MinLimitSeconds", "MaxLimitSeconds", "LimitExpireSeconds"):
            column(sheet, name)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "LootLimitID").strip()
            if not identifier:
                continue
            what = f"the loot limit {identifier}"
            limits.setdefault(identifier, [int(number_of(sheet.source(row, "CountLimit"), what)),
                                           rounded(number_of(sheet.source(row, "MinLimitSeconds"), what)),
                                           rounded(number_of(sheet.source(row, "MaxLimitSeconds"), what)),
                                           rounded(number_of(sheet.source(row, "LimitExpireSeconds"), what)),
                                           rounded(number_of(sheet.source(row, "MinLimitMult"), what)),
                                           rounded(number_of(sheet.source(row, "MaxLimitMult"), what)),
                                           rounded(number_of(sheet.source(row, "PityTickerOddsMod"), what))])
    return dict(sorted(limits.items()))


def read_vitals(sheets: dict[str, list[Sheet]]) -> tuple[list, dict, dict]:
    """@return The loot data of each base vitals row, the base row of each vitals row id (lower case), and the counts."""
    bases: list[list] = []
    base_index: dict[str, int] = {}
    counts = {"flagTextsNotInGame": 0, "chanceTextsNotANumber": 0}

    def flag(sheet: Sheet, row: int, name: str) -> bool:
        text = sheet.source(row, name) if not isinstance(sheet.cell(row, name), bool) else sheet.cell(row, name)
        if isinstance(text, str) and text.strip().lower() in FLAG_TEXTS_NOT_IN_GAME:
            counts["flagTextsNotInGame"] += 1
        return is_true(text)

    for sheet in sheets[BASE_TYPE]:
        # The base tables of the catacombs have no column NoExpNoCoin: their rows give experience and coin.
        for name in ("VitalsID", "CreatureType", "CanDropLoot", "LootDropChance", "IgnoreLootDropModifier", "LootTableId",
                     "LootTags", "KillEventIdOverride"):
            column(sheet, name)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "VitalsID").strip()
            if not identifier or identifier.lower() in base_index:
                continue
            what = f"row {row} of the table {sheet.name}"
            tags = [tag.strip() for tag in sheet.source(row, "LootTags").split(",") if tag.strip()]
            base_index[identifier.lower()] = len(bases)
            chance_text = sheet.source(row, "LootDropChance").strip()
            if chance_text.lower() in CHANCE_TEXTS_NOT_IN_GAME:
                counts["chanceTextsNotANumber"] += 1
                chance = 0.0
            else:
                chance = rounded(number_of(chance_text, f"the cell LootDropChance of {what}"))
            bases.append([identifier, sheet.source(row, "CreatureType").strip(), flag(sheet, row, "CanDropLoot"), chance,
                          flag(sheet, row, "IgnoreLootDropModifier"), sheet.source(row, "LootTableId").strip(), tags,
                          flag(sheet, row, "NoExpNoCoin"), sheet.source(row, "KillEventIdOverride").strip()])
    vitals = dict(base_index)
    missing = 0
    for sheet in sheets[VARIANT_TYPE]:
        for name in ("VitalsID", "BaseVitalsId"):
            column(sheet, name)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "VitalsID").strip().lower()
            base = sheet.source(row, "BaseVitalsId").strip().lower()
            if not identifier or identifier in vitals:
                continue
            if base not in base_index:
                # A variant row with no base row: a fact of the game data for some rows, which the creature tool leaves
                # out too. Each other one stops the tool (code review of stage 3.5a, finding F3).
                if identifier not in VARIANTS_WITH_NO_BASE_IN_GAME:
                    raise Failure(f"the variant vitals row {identifier} names the base row {base}, which the tables do not have")
                missing += 1
                continue
            vitals[identifier] = base_index[base]
    counts["variantsWithNoBase"] = missing
    return bases, vitals, counts


def read_events(sheets: dict[str, list[Sheet]], wanted: set[str]) -> dict:
    events = {}
    for sheet in sheets[EVENT_TYPE]:
        # The game has event tables of other parts of the game with other columns: an event of a table with no coin
        # chance has the chance 0.
        if "EventID" not in sheet.columns:
            continue
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "EventID").strip()
            if identifier.lower() in wanted and identifier.lower() not in events:
                events[identifier.lower()] = rounded(number_of(sheet.source(row, "CoinRewardChance"),
                                                               f"the coin chance of the event {identifier}"))
    if KILL_EVENT.lower() not in events:
        raise Failure(f"the game events have no event {KILL_EVENT}")
    return events


def read_loot(sheets: dict[str, list[Sheet]], roots: list[str]) -> tuple[dict, dict, dict]:
    """@return The loot tables and the loot buckets that the root tables reach, and the counts."""
    rows: dict[str, tuple[Sheet, int]] = {}
    duplicates = 0
    for sheet in sheets[LOOT_TABLE_TYPE]:
        column(sheet, "LootTableID")
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "LootTableID").strip()
            if not identifier:
                continue
            if identifier.lower() in rows:
                duplicates += 1  # The first table of the byte order of the paths has the row (as the research reader).
                continue
            rows[identifier.lower()] = (sheet, row)
    buckets_source: dict[str, list] = {}
    for sheet in sheets[LOOT_BUCKET_TYPE]:
        groups: dict[int, dict[str, str]] = {}
        for name in sheet.columns:
            match = re.fullmatch(r"(LootBucket|Item|Quantity|Tags|MatchOne|Odds|odds)(\d+)", name)
            if match:
                groups.setdefault(int(match[2]), {})["Odds" if match[1] == "odds" else match[1]] = name
        for number, group in sorted(groups.items()):
            if "LootBucket" not in group or "Item" not in group or sheet.row_count == 0:
                continue
            bucket = sheet.source(0, group["LootBucket"]).strip()
            if not bucket or bucket.lower() in buckets_source:
                continue
            buckets_source[bucket.lower()] = [bucket, sheet, group]

    tables: dict[str, list] = {}
    buckets: dict[str, list] = {}
    unresolved: dict[str, int] = {}
    pending = list(roots)
    names: dict[str, str] = {}
    while pending:
        key = pending.pop().lower()
        if key in tables:
            continue
        if key not in rows:
            unresolved[key] = unresolved.get(key, 0) + 1
            continue
        sheet, row = rows[key]
        name = sheet.source(row, "LootTableID").strip()
        names[key] = name
        what = f"the loot table {name}"
        quantity = rows.get(f"{key}_qty")
        probability = rows.get(f"{key}_probs")
        mode = sheet.source(row, "AND/OR").strip().upper()
        if mode not in ("AND", "OR"):
            raise Failure(f"{what} has the mode \"{mode}\", not AND or OR")
        setting = sheet.source(row, "RollBonusSetting").strip()
        bonus = {"": 0, "AddToRoll": 0, "ClampMax": 1, "IgnoreBonus": 2}.get(setting)
        if bonus is None:
            raise Failure(f"{what} has the roll bonus setting \"{setting}\"")
        max_roll = int(number_of(probability[0].source(probability[1], "MaxRoll"), f"MaxRoll of {what}")) if probability else 0
        conditions = [text.strip() for text in sheet.source(row, "Conditions").split(",") if text.strip()]
        entries = []
        for index in range(1, 1000):
            item_column = f"Item{index}"
            if item_column not in sheet.columns:
                break
            text = sheet.source(row, item_column).strip()
            if not text:
                continue
            threshold_text = probability[0].source(probability[1], item_column) if probability else ""
            threshold = range_of(threshold_text, f"the threshold of entry {index} of {what}")[0]
            quantity_text = quantity[0].source(quantity[1], item_column) if quantity else "1"
            low, high = range_of(quantity_text, f"the quantity of entry {index} of {what}")
            if not quantity_text.strip():
                low, high = 1, 1
            gear = range_of(sheet.source(row, f"GearScoreRange{index}"), f"the gear score range of entry {index} of {what}")
            if text.startswith(TABLE_PREFIX):
                kind, target = "t", text[len(TABLE_PREFIX):].strip()
                pending.append(target)
            elif text.startswith(BUCKET_PREFIX):
                kind, target = "b", text[len(BUCKET_PREFIX):].strip()
                if target.lower() not in buckets_source:
                    unresolved[target.lower()] = unresolved.get(target.lower(), 0) + 1
                    continue
                buckets.setdefault(target.lower(), None)
            else:
                kind, target = "i", text
            entries.append([kind, target, threshold, low, high, gear[0], gear[1]])
        # The client sorts the entries by their threshold (image RVA 0x366F799); a stable sort keeps the sequence of
        # entries with one threshold.
        entries.sort(key=lambda entry: entry[2])
        tables[key] = [1 if mode == "AND" else 0, bonus, max_roll, conditions,
                       is_true(sheet.source(row, "ConditionOverridesRoll")), is_true(sheet.source(row, "UseLevelGS")),
                       int(number_of(sheet.source(row, "GSBonus"), f"GSBonus of {what}")), entries]
    # The references between tables by the name of their row (the case of a reference can differ).
    for table in tables.values():
        for entry in table[7]:
            if entry[0] == "t":
                entry[1] = names.get(entry[1].lower(), entry[1])
            elif entry[0] == "b":
                entry[1] = buckets_source[entry[1].lower()][0]
    resolved_tables = {names[key]: value for key, value in tables.items()}
    for key in list(buckets):
        bucket, sheet, group = buckets_source[key]
        candidates = []
        for row in range(sheet.row_count):
            item = sheet.source(row, group["Item"]).strip()
            if not item:
                continue
            what = f"row {row} of the loot bucket {bucket}"
            low, high = range_of(sheet.source(row, group["Quantity"]) if "Quantity" in group else "1", f"the quantity of {what}")
            odds_text = sheet.source(row, group["Odds"]).strip() if "Odds" in group else ""
            odds = rounded(number_of(odds_text, f"the odds of {what}", 1.0))
            tags = []
            for tag in (sheet.source(row, group["Tags"]) if "Tags" in group else "").split(","):
                tag = tag.strip()
                if not tag:
                    continue
                if ":" in tag:
                    name, values = tag.split(":", 1)
                    first, last = range_of(values, f"the tag {tag} of {what}", TAG_LIMIT)
                    tags.append([name.strip(), first, last])
                else:
                    tags.append([tag])
            match_one = is_true(sheet.source(row, group["MatchOne"])) if "MatchOne" in group else False
            candidates.append([item, low, high, odds, match_one, tags])
        buckets[key] = candidates
    resolved_buckets = {buckets_source[key][0]: value for key, value in buckets.items()}
    counts = {"tableRows": len(rows), "secondRowsOfOneId": duplicates, "unresolved": dict(sorted(unresolved.items()))}
    return resolved_tables, resolved_buckets, counts


def build_tables(sources: Sources) -> dict:
    credit = read_credit(sources)
    sheets = read_sheets(sources)
    document = {"format": 2}
    document.update(read_rewards(sheets))
    bases, vitals, vitals_counts = read_vitals(sheets)
    wanted_events = {KILL_EVENT.lower()} | {base[8].lower() for base in bases if base[8]}
    document["events"] = read_events(sheets, wanted_events)
    # Each reference of a base row resolves (code review of stage 3.5a, finding F3): the event of a kill and the
    # creature type.
    absent_events = sorted(event for event in wanted_events if event not in document["events"])
    if absent_events:
        raise Failure("the game events have no event " + ", ".join(absent_events))
    absent_types = sorted({base[1] for base in bases if base[1] and base[1] not in document["modifiers"]})
    if absent_types:
        raise Failure("the reward modifiers have no creature type " + ", ".join(absent_types))
    chest_roots = read_chest_roots(sheets)
    roots = sorted({base[5] for base in bases if base[5]} | set(chest_roots))
    tables, buckets, loot_counts = read_loot(sheets, roots)
    # A chest root of the variation tables with no table (stage 3.5b.1): only the roots of CHEST_ROOTS_NOT_IN_GAME.
    for root in chest_roots:
        if root.lower() in loot_counts["unresolved"] and root in CHEST_ROOTS_NOT_IN_GAME:
            del loot_counts["unresolved"][root.lower()]
    chest_roots = [root for root in chest_roots if root not in CHEST_ROOTS_NOT_IN_GAME]
    # A vitals row whose table the loot tables do not have, and a reference that does not resolve: the game has no
    # such table. The tool stops: a table that lost a row must not make a file that looks complete.
    if loot_counts["unresolved"]:
        raise Failure("the loot tables have no table or bucket with the id(s) "
                      + ", ".join(sorted(loot_counts["unresolved"])))
    if len(credit["modifiers"]) < credit["receivers"]:
        raise Failure(f"the credit data has {len(credit['modifiers'])} modifiers for {credit['receivers']} characters")
    document["credit"] = credit
    document["bases"] = bases
    document["vitals"] = vitals
    document["tables"] = tables
    document["buckets"] = buckets
    names = {name.lower(): name for name in tables}
    document["chestRoots"] = [names[root.lower()] for root in chest_roots]
    document["limits"] = read_limits(sheets)
    items = {entry[1].lower() for table in tables.values() for entry in table[7] if entry[0] == "i"}
    items |= {candidate[0].lower() for candidate in buckets.values() for candidate in candidate}
    document["counts"] = {"bases": len(bases), "vitals": len(vitals), "basesThatDropLoot": sum(1 for base in bases if base[2] and base[5]),
                          "tables": len(tables), "buckets": len(buckets), "items": len(items),
                          "noExpNoCoin": sum(1 for base in bases if base[7]), "chestRoots": len(chest_roots),
                          "limits": len(document["limits"]), **vitals_counts,
                          **{key: value for key, value in loot_counts.items() if key != "unresolved"}}
    return document


def print_found(document: dict, texts: list[str]) -> None:
    for text in texts:
        wanted = text.lower()
        for name, index in sorted(document["vitals"].items()):
            if wanted in name:
                base = document["bases"][index]
                print(f"vitals {name}: base {base[0]}, {base[1]}, drops loot {base[2]} with the chance {base[3]} "
                      f"(ignore the modifier: {base[4]}), table {base[5] or '-'}, tags {','.join(base[6]) or '-'}, "
                      f"no experience and no coin: {base[7]}, kill event {base[8] or 'KillEvent'}")
        for name, table in sorted(document["tables"].items()):
            if wanted in name.lower():
                print(f"table {name}: {'AND' if table[0] else 'OR'}, bonus {table[1]}, MaxRoll {table[2]}, conditions "
                      f"{','.join(table[3]) or '-'}, condition overrides the roll {table[4]}, UseLevelGS {table[5]}, "
                      f"GSBonus {table[6]}")
                for entry in table[7]:
                    print(f"    {entry[2]:>8} {entry[0]} {entry[1]} quantity {entry[3]}-{entry[4]}"
                          + (f" gear score {entry[5]}-{entry[6]}" if entry[6] else ""))


# The largest level of a reward and of a reward level delta: the limit of the backend (gamedata/loot_tables.cpp).
LARGEST_LEVEL = 1000


def check_ranges(document: dict) -> None:
    """Stops the tool when a value of the document is outside the range that the backend accepts (LootTables::check):
    the tool does not replace a working file with a file that the backend refuses (code review of stage 3.3a, finding
    F13: a kill experience modifier below -1, a reward level delta above 1000, and negative bucket odds gave a file that
    the backend refused)."""
    def bad(what: str) -> None:
        raise Failure(f"{what} is outside the range of the backend")

    if not document["rewards"] or len(document["rewards"]) > LARGEST_LEVEL:
        bad("the number of the reward levels")
    for name, values in document["modifiers"].items():
        experience, currency, chance, loot, progression = values
        if experience < 0 or currency < 0 or not 0 <= chance <= 1 or loot < 0 or (progression is not None and progression < 0):
            bad(f"a reward factor of the creature type {name}")
    for difference, experience, delta in document["disparity"]:
        if not -1.0 <= experience <= 100.0 or not 0 <= delta <= LARGEST_LEVEL:
            bad(f"the level difference row {difference} (KillExpModifier {experience}, MaxRewardLevelDelta {delta})")
    for event, chance in document["events"].items():
        if not 0 <= chance <= 1:
            bad(f"the coin chance of the event {event}")
    credit = document["credit"]
    if credit["timeLimit"] <= 0 or not 0 <= credit["healthShare"] <= 1 or credit["range"] <= 0 or credit["receivers"] <= 0:
        bad("the credit data")
    for modifier in credit["modifiers"]:
        if any(value < 0 for value in modifier):
            bad("a credit modifier")
    for level in document["gearScore"]:
        for low, high in zip(level[0::2], level[1::2]):
            if high < low:
                bad("a gear score range of a level")
    for name, candidates in document["buckets"].items():
        for item, low, high, odds, _, tags in candidates:
            if high < low or odds < 0:
                bad(f"the candidate {item} of the loot bucket {name}")
            for tag in tags:
                if len(tag) == 3 and tag[2] < tag[1]:
                    bad(f"a tag of the loot bucket {name}")
    for name, table in document["tables"].items():
        for entry in table[7]:
            if entry[4] < entry[3] or entry[6] < entry[5]:
                bad(f"an entry of the loot table {name}")


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
        document = build_tables(sources)
        check_ranges(document)
        if find is not None:
            print_found(document, find)
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename. A number that is not finite is
        # not JSON (json.dumps stops with ValueError).
        text = json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n"
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(text, encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"loot: {error}. No file was written.", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"loot: the document has a number that is not finite ({error}). No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"loot: {target}: {error}", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {len(document['rewards'])} reward levels, {len(document['modifiers'])} creature types, "
          f"{counts['bases']} base vitals rows ({counts['basesThatDropLoot']} drop loot, {counts['noExpNoCoin']} give no "
          f"experience and no coin), {counts['vitals']} vitals ids, {counts['tables']} loot tables, {counts['buckets']} "
          f"loot buckets, {counts['items']} items (source: "
          f"{', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    credit = document["credit"]
    print(f"  credit: {credit['timeLimit']} s, {credit['healthShare']} of the health, {credit['range']} m, "
          f"{credit['receivers']} receivers at most, {len(credit['modifiers'])} credit modifiers")
    print(f"  a second row of one loot table id (the first one is used): {counts['secondRowsOfOneId']}; variant rows with "
          f"no base row: {counts['variantsWithNoBase']}; flag texts that are not TRUE or FALSE: "
          f"{counts['flagTextsNotInGame']}; chance texts that are not a number: {counts['chanceTextsNotANumber']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
