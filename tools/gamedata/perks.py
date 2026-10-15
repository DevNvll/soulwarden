#!/usr/bin/env python3
"""Write the perk data of the game as one JSON file (stage 1.3b, itemization).

The backend reads this file and gets what the perks of an item instance need (protocol notes 4.23):
the index of each perk on the wire, the values of each perk (its type, its item classes, its
labels, its gear score scaling, its affix, and its abilities), the affixes and the ability rows
that the perks name, the perk buckets of a roll, the perk places of each item, the consumables,
and the equip load data. Without the file the backend starts and works: an item then has no
perk, and no perk has an effect.

A perk on the wire is an index into a list that the client makes at its start
(ReplicationDataManager::BuildPerkList, image RVA 0x448E310; live check I1 of stage 1.3b):

  index 0 = no perk
  for each perk table (type PerkData) in the sequence of the table map of the client
  (TABLE_ORDER below: a constant of the supported build, read from the live client):
      the rows of the table in the sequence of its stored list: the copy of a hash map
      (0x477D8F0) puts the rows of each bucket in front of the rows before them, thus the rows
      go in the descending sequence of their bucket, and in file order in one bucket. The
      bucket of a row is hash(crc) & (buckets - 1), with buckets the smallest power of two that
      is not less than the row count (4 at least), crc the CRC-32 of the lower-case PerkID,
      and hash(crc) = (low 64 bits + high 64 bits of crc * 0xDE5FB9D2630458E9) mod 2^64
      (helper 0x14A0FA0).

Input: the game copy, from the same sources as the creature data tool (creature_data.py).

  Source (table type)                 What the file gets
  PerkData (7 tables)                 each perk in the sequence of the list of the client:
                                      PerkID, PerkType, ConditionEvent, ItemClass,
                                      ExcludeItemClass, ExclusiveLabels, ScalingPerGearScore,
                                      Affix, EquipAbility
  AffixStatData                       each affix that a perk names: its cells that are not empty
  AbilityData (each table)            each ability row that a perk or a heart rune names: its
                                      cells that are not empty (the display columns are not in it)
  PerkBucketData                      each perk bucket: PerkType, PerkChance, its entries (a perk,
                                      or "[PBID]" and a bucket), and the weights of the row
                                      "<bucket>_Weights"
  MasterItemDefinitions               for each item with a perk place: PerkSlot1..5, Perk1..5,
                                      PerkBucket1..5, ForceRarity, CanHavePerks
  ConsumableItemDefinitions           each consumable: AddStatusEffects, DurationOverrides,
                                      CooldownId, CooldownDuration, RemoveStatusEffects,
                                      RemoveStatusEffectCategories, DisableUseInCombat; and the
                                      RequiredLevel of its row of MasterItemDefinitions (format 2,
                                      code review of stage 1.3b, finding F5)
  EncumbranceData                     the row Player: the equip load limits and their effects
  ArmorItemDefinitions                the equip load of each armor item (WeightOverride of its row)
  PerkExclusiveLabelData              the weight of each exclusive label, with its item class overrides
  ItemPerkSwapData                    the perk swaps of the game: a perk that the client shows and uses as
                                      a different perk on an item of a key (an item class or an item id)
  playerbaseattributes.pbadb          PerkGenerationData: for each tier the largest perk channel and the
                                      gear score ranges of each perk count; Min Level Roll Perks

The tool writes no file (status 1) when the game copy does not have the seven perk tables of
TABLE_ORDER, when a perk id is empty or is in two rows, when a perk names an affix or an ability
row that the tables do not have (but the references of REFERENCES_NOT_IN_GAME, which the game
itself does not resolve), when a bucket or an item names a perk or a bucket that the tables do
not have, when a column that the tool reads is not in its table (also each value column of the
encumbrance row and of the affix and ability tables: code review of stage 1.3b, finding F14), when
two affix rows or two ability rows have one id and different values (finding F15), and when a
number is not finite.

The entries of a perk bucket end at the first empty perk cell or empty weight cell, and at Perk500
(the parser of the client, image RVA 0x4480767, 0x448077B, 0x4480951: finding F16). A weight 0 is
an entry with the weight 0; an empty weight cell ends the list.

Output: {"format": 2, "count": N, "tables": [{"name", "file", "rows", "buckets", "first", "last"}, ...],
"perks": [[id, type, condition, itemClass, excludeItemClass, labels, scaling, [affix, ...], [ability, ...], channel,
deprecated], ...],
"affixes": {"<id>": {"<column>": value, ...}, ...}, "abilities": {"<id>": {"<column>": value, ...}, ...},
"buckets": {"<id>": [type, chance, [[entry, weight], ...], ignoreLabelWeights, disableBiasing], ...},
"layouts": [[[slot1..slot5], [perk1..perk5], [bucket1..bucket5], forceRarity, canHavePerks], ...],
"items": {"<item id in lower case>": layout, ...},
"consumables": {"<id in lower case>": {...}, ...}, "consumablesWithNoItem": N, "encumbrance": {...}, "equipLoad": {"<item id in lower case>": load, ...},
"labels": {"<label>": [weight, {"<item class>": weight, ...}], ...}, "swaps": [[id, keyType, key, oldPerk, newPerk], ...],
"runes": {"<weapon id of a heart rune in lower case>": [ability, ...], ...},
"generation": {"minLevelRollPerks": N, "tiers": [[maxPerkChannel, [[perkCount, [[gsMin, gsMax], ...]], ...]], ...]}}.
Perk number K of `perks` (from 0) is the perk with the index K + 1. `scaling` is a list of
[gearScore, slope] pairs: the slope applies from that gear score to the next pair (the first
pair starts at the lowest gear score). A bucket entry that starts with "[PBID]" is a bucket.

Usage: perks.py [OUTPUT.json]
       perks.py --find TEXT...   print the perks, affixes, abilities, buckets, and items with a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/perk-data.json
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
import zlib
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import TABLES_FOLDER, Sources, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "perk-data.json"
PERK_TYPE = "PerkData"
BUCKET_TYPE = "PerkBucketData"
AFFIX_TYPE = "AffixStatData"
ABILITY_TYPE = "AbilityData"
ITEM_TYPE = "MasterItemDefinitions"
CONSUMABLE_TYPE = "ConsumableItemDefinitions"
ENCUMBRANCE_TYPE = "EncumbranceData"
ARMOR_TYPE = "ArmorItemDefinitions"
RUNE_TYPE = "WeaponItemDefinitions"
LABEL_TYPE = "PerkExclusiveLabelData"
SWAP_TYPE = "ItemPerkSwapData"
USED_TYPES = (PERK_TYPE, BUCKET_TYPE, AFFIX_TYPE, ABILITY_TYPE, ITEM_TYPE, CONSUMABLE_TYPE, ENCUMBRANCE_TYPE,
              ARMOR_TYPE, RUNE_TYPE, LABEL_TYPE, SWAP_TYPE)
BASE_ATTRIBUTES_PATH = "sharedassets/genericassets/playerbaseattributes.pbadb"

# The sequence of the perk tables in the table map of the client: a constant of the supported
# build, read from the live client (live check I1 of stage 1.3b, two starts: the names, the
# row counts, and the bucket counts). The game files do not give it.
TABLE_ORDER = ("ItemPerks_Gems", "ItemPerks_Infix", "ItemPerks_Deprecated", "ItemPerks_Artifacts", "ItemPerks_2025",
               "ItemPerks_EquipmentSetBonuses", "ItemPerks")
HASH_FACTOR = 0xDE5FB9D2630458E9
MASK64 = (1 << 64) - 1
# The lowest gear score of a perk scaling (the start of the first slope).
SCALING_START = 0

PERK_COLUMNS = ("PerkID", "PerkType", "ConditionEvent", "ItemClass", "ExcludeItemClass", "ExclusiveLabels",
                "ScalingPerGearScore", "Affix", "EquipAbility", "Channel", "DeprecatedPerkId")
BUCKET_COLUMNS = ("PerkBucketID", "PerkType", "PerkChance", "IgnoreExclusiveLabelWeights", "DisablePerkBiasing")
ITEM_COLUMNS = tuple(f"PerkSlot{n}" for n in range(1, 6)) + tuple(f"Perk{n}" for n in range(1, 6)) + \
    tuple(f"PerkBucket{n}" for n in range(1, 6)) + ("ItemID", "ForceRarity", "CanHavePerks", "ItemStatsRef")
CONSUMABLE_COLUMNS = ("ConsumableID", "AddStatusEffects", "DurationOverrides", "CooldownId", "CooldownDuration",
                      "RemoveStatusEffects", "RemoveStatusEffectCategories", "DisableUseInCombat", "SharedCooldownLocString")
ENCUMBRANCE_COLUMNS = {"equipLoadMax": "EquipLoadBaseMax", "ratioMedium": "EquipLoadRatioNormal",
                       "ratioHeavy": "EquipLoadRatioSlow", "ratioOverburdened": "EquipLoadRatioOverburdened",
                       "damageLight": "EquipLoadDamageMultFast", "damageMedium": "EquipLoadDamageMultNormal",
                       "damageHeavy": "EquipLoadDamageMultSlow", "healLight": "EquipLoadHealMultFast",
                       "healMedium": "EquipLoadHealMultNormal", "healHeavy": "EquipLoadHealMultSlow"}
# The value columns that the backend reads from an affix row and from an ability row (the gear system): each affix
# table and each ability table must have them (finding F14 of the review of stage 1.3b: an absent column is not 0).
# Code review of stage 3.3b-S1, finding F12: the damage type columns (DMG<type>, ABS<type>) that the gear values read are
# required too. The affix table of the supported build has no PhysicalArmor, ElementalArmor, and BaseDamageReduction
# column (AFFIX_COLUMNS_NOT_IN_GAME): an affix gives no armor; the backend reads 0 there for each affix.
AFFIX_VALUE_COLUMNS = ("MODStrength", "MODDexterity", "MODIntelligence", "MODFocus", "MODConstitution", "MaxHealthMod",
                       "StatusEffect", "DamagePercentage", "DamageType", "ABSSlash", "ABSThrust", "ABSStrike", "ABSFire",
                       "ABSIce", "ABSNature", "ABSArcane", "ABSCorruption", "ABSLightning", "ABSStandard", "DMGSlash",
                       "DMGThrust", "DMGStrike", "DMGFire", "DMGIce", "DMGNature", "DMGArcane", "DMGCorruption",
                       "DMGLightning", "DMGStandard")
AFFIX_COLUMNS_NOT_IN_GAME = ("PhysicalArmor", "ElementalArmor", "BaseDamageReduction")
ABILITY_VALUE_COLUMNS = ("BaseDamage", "OnHit", "DmgPctToHealth", "CooldownTimer", "OtherApplyStatusEffect", "MaxHealth")
# The largest number of entries of a perk bucket (the loop of the parser of the client, image RVA 0x4480951).
BUCKET_ENTRIES_MOST = 500
# The ability columns that are text for a person, not a value of a rule.
ABILITY_TEXT_COLUMNS = {"DisplayName", "Description", "UICategory", "Icon", "StatusEffectIcon", "Sound", "Notes"}
# References that the game itself does not resolve (research itemization.md Q3, "Missing links"):
# the perk keeps its other links, and the missing one has no effect.
REFERENCES_NOT_IN_GAME = {"affix_statswap_craftingbonus", "global_jewelry_hercyneraid_1_block", ".",
                          "abilitytable_setbonus_enrage_perk3_incduration_ss", "ability_perk_meleeshield_blockingstam",
                          "ability_perk_meleeshield_blockingmove", "globalperk_artifact_set1_spearperk",
                          "globalperk_artifact_set1_iceperk", "globalperk_artifact_set1_voidperk"}
# An item place that names the weights row of a bucket, not a bucket (15 items of the Breach set of
# the game): the place has no bucket.
ITEM_REFERENCES_NOT_IN_GAME = {"perkbucket_breachbucketchancet5bucket5_weights"}
BUCKET_PREFIX = "[PBID]"
# Old perks of the swap table of the game that no perk table has (72 rows): a perk of the wire never has such an id,
# thus the row has no effect.
SWAP_PERKS_NOT_IN_GAME = {"perkid_armor_conditioning_lightning", "perkid_weapon_attunement_lightning"}


def crc(text: str) -> int:
    """@return The CRC-32 of a text as the client computes it: only the letters A to Z go to lower case."""
    data = bytes(byte + 0x20 if 0x41 <= byte <= 0x5A else byte for byte in text.encode("utf-8"))
    return zlib.crc32(data) & 0xFFFFFFFF


def bucket_of(value: int, buckets: int) -> int:
    product = value * HASH_FACTOR
    return (((product & MASK64) + (product >> 64)) & MASK64) & (buckets - 1)


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


def text(sheet: Sheet, row: int, column: str) -> str:
    return str(sheet.source(row, column)).strip()


def rounded(value: float) -> float:
    return float(f"{value:.6g}")


def require(sheet: Sheet, columns) -> None:
    """Stops the tool when a column that the tool reads is not in the table (an absent column is not 0)."""
    missing = [column for column in columns if column not in sheet.columns]
    if missing:
        raise Failure(f"the table {sheet.name} has no column {', '.join(missing)}")


def split_list(value: str, separator: str = "+") -> list[str]:
    return [part.strip() for part in value.replace(",", separator).split(separator) if part.strip()]


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


def scaling_of(value: str, perk: str) -> list[list[float]]:
    """@return The [gearScore, slope] pairs of a ScalingPerGearScore text ("0.0019,550:0.00")."""
    if not value:
        return []
    pairs = []
    for position, part in enumerate(value.split(",")):
        part = part.strip()
        if not part:
            continue
        if ":" in part:
            start, slope = part.split(":", 1)
        elif position == 0:
            start, slope = str(SCALING_START), part
        else:
            raise Failure(f"the perk {perk} has a scaling part with no gear score: {value!r}")
        try:
            pair = [float(start), float(slope)]
        except ValueError as error:
            raise Failure(f"the perk {perk} has a scaling that is not a number: {value!r}") from error
        if not all(math.isfinite(entry) for entry in pair):
            raise Failure(f"the perk {perk} has a scaling that is not finite: {value!r}")
        pairs.append([rounded(pair[0]), rounded(pair[1])])
    for earlier, later in zip(pairs, pairs[1:]):
        if later[0] <= earlier[0]:
            raise Failure(f"the perk {perk} has a scaling whose gear scores do not go up: {value!r}")
    return pairs


def cells_of(sheet: Sheet, row: int, skip: set[str]) -> dict:
    """@return The cells of a row that are not empty and not 0: a number as a number, each other cell as a text."""
    values: dict = {}
    for column in sheet.columns:
        if column in skip:
            continue
        source = text(sheet, row, column)
        if source == "" or source == "0":
            continue
        try:
            value = float(source)
        except ValueError:
            values[column] = source
            continue
        if not math.isfinite(value):
            raise Failure(f"the cell {column} of row {row} of the table {sheet.name} is not a finite number")
        if value != 0.0:
            values[column] = rounded(value)
    return values


def build_data(sources: Sources) -> dict:
    sheets = read_sheets(sources)

    # The perk list of the client.
    by_name = {sheet.name: sheet for sheet in sheets[PERK_TYPE]}
    if sorted(by_name) != sorted(TABLE_ORDER) or len(sheets[PERK_TYPE]) != len(TABLE_ORDER):
        raise Failure(f"the perk tables of the game copy are {sorted(sheet.name for sheet in sheets[PERK_TYPE])}, not the "
                      f"seven tables of the supported build: the sequence of the list of the client is not known")
    tables = []
    perks = []
    seen: set[str] = set()
    for name in TABLE_ORDER:
        sheet = by_name[name]
        require(sheet, PERK_COLUMNS)
        rows = []
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "PerkID")
            if not identifier:
                raise Failure(f"row {row} of the perk table {name} has no PerkID")
            if identifier.lower() in seen:
                raise Failure(f"two perk rows have the id {identifier}")
            seen.add(identifier.lower())
            rows.append((row, identifier))
        buckets = 1 << max(2, (len(rows) - 1).bit_length())
        ordered = sorted(rows, key=lambda entry: -bucket_of(crc(entry[1]), buckets))
        first = len(perks) + 1
        for row, identifier in ordered:
            perks.append([identifier, text(sheet, row, "PerkType"), text(sheet, row, "ConditionEvent"),
                          text(sheet, row, "ItemClass"), text(sheet, row, "ExcludeItemClass"),
                          text(sheet, row, "ExclusiveLabels"),
                          scaling_of(text(sheet, row, "ScalingPerGearScore"), identifier),
                          split_list(text(sheet, row, "Affix"), ","), split_list(text(sheet, row, "EquipAbility"), ","),
                          int(number(sheet, row, "Channel")), bool(text(sheet, row, "DeprecatedPerkId"))])
        tables.append({"name": name, "file": "", "rows": len(rows), "buckets": buckets, "first": first,
                       "last": len(perks)})
    if len(perks) + 1 > 0xFFFF:
        raise Failure(f"the perk list has {len(perks) + 1} entries, above 16 bits")
    perk_ids = {perk[0].lower() for perk in perks}

    # The affixes and the ability rows that the perks name (and the ability of each heart rune).
    def add_cells(rows: dict[str, dict], identifier: str, cells: dict, what: str) -> None:
        # Two rows of one id must have the same values (finding F15 of the review of stage 1.3b): the gear values
        # must not depend on the sequence of the source tables.
        key = identifier.lower()
        if key in rows and rows[key] != cells:
            raise Failure(f"two rows of the {what} have the id {identifier} with different values")
        rows.setdefault(key, cells)

    affix_rows: dict[str, dict] = {}
    for sheet in sheets[AFFIX_TYPE]:
        require(sheet, ("StatusID",) + AFFIX_VALUE_COLUMNS)
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "StatusID")
            if identifier:
                add_cells(affix_rows, identifier, cells_of(sheet, row, {"StatusID"}), "affix tables")
    ability_rows: dict[str, dict] = {}
    for sheet in sheets[ABILITY_TYPE]:
        require(sheet, ("AbilityID",) + ABILITY_VALUE_COLUMNS)
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "AbilityID")
            if identifier:
                add_cells(ability_rows, identifier, cells_of(sheet, row, ABILITY_TEXT_COLUMNS | {"AbilityID"}), "ability tables")
    wanted_abilities: set[str] = set()
    affixes: dict[str, dict] = {}
    for perk in perks:
        found = []
        for affix in perk[7]:
            if affix.lower() in affix_rows:
                affixes[affix] = affix_rows[affix.lower()]
                found.append(affix)
            elif affix.lower() not in REFERENCES_NOT_IN_GAME:
                raise Failure(f"the perk {perk[0]} names the affix {affix}, which the affix table does not have")
        perk[7] = found
        resolved = []
        # A reference can name its table first ("Rapier:GlobalPerk_Ability_Rapier_Tondo2").
        for ability in [entry.split(":", 1)[-1] for entry in perk[8]]:
            if ability.lower() in ability_rows:
                wanted_abilities.add(ability)
                resolved.append(ability)
            elif ability.lower() not in REFERENCES_NOT_IN_GAME:
                raise Failure(f"the perk {perk[0]} names the ability {ability}, which the ability tables do not have")
        perk[8] = resolved
    runes: dict[str, list] = {}
    for sheet in sheets[RUNE_TYPE]:
        if "EquipAbility" not in sheet.columns:
            continue
        require(sheet, ("WeaponID",))
        for row in range(sheet.row_count):
            found = []
            for ability in split_list(text(sheet, row, "EquipAbility"), ","):
                if ability.lower() in ability_rows:
                    wanted_abilities.add(ability)
                    found.append(ability)
            if found and text(sheet, row, "WeaponID"):
                runes[text(sheet, row, "WeaponID").lower()] = found
    abilities = {name: ability_rows[name.lower()] for name in sorted(wanted_abilities)}

    # The perk buckets, with the weights of their "_Weights" rows.
    raw_buckets: dict[str, tuple[Sheet, int, str]] = {}
    for sheet in sheets[BUCKET_TYPE]:
        require(sheet, BUCKET_COLUMNS)
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "PerkBucketID")
            if identifier:
                raw_buckets.setdefault(identifier.lower(), (sheet, row, identifier))
    buckets: dict[str, list] = {}
    for key, (sheet, row, identifier) in raw_buckets.items():
        if key.endswith("_weights"):
            continue
        entries = []
        weights = raw_buckets.get(key + "_weights")
        # The parser of the client (finding F16): the list ends at the first empty perk cell or empty weight cell, and
        # at Perk500. An empty weight cell is not the weight 0.
        for column in range(1, BUCKET_ENTRIES_MOST + 1):
            name = f"Perk{column}"
            if name not in sheet.columns:
                break
            entry = text(sheet, row, name)
            if not entry:
                break
            if weights:
                if name not in weights[0].columns or text(weights[0], weights[1], name) == "":
                    break
                weight = number(weights[0], weights[1], name)
            else:
                weight = 1.0
            if weight < 0.0:
                raise Failure(f"the perk bucket {identifier} has a negative weight for {entry}")
            entries.append([entry, rounded(weight)])
        chance = number(sheet, row, "PerkChance")
        if not 0.0 <= chance <= 1.0:
            raise Failure(f"the perk bucket {identifier} has the chance {chance}, not from 0 to 1")
        buckets[identifier] = [text(sheet, row, "PerkType"), rounded(chance), entries,
                               number(sheet, row, "IgnoreExclusiveLabelWeights") != 0.0,
                               number(sheet, row, "DisablePerkBiasing") != 0.0]
    bucket_ids = {name.lower() for name in buckets}
    for identifier, (_, _, entries, _, _) in buckets.items():
        for entry, _ in entries:
            if entry.startswith(BUCKET_PREFIX):
                if entry[len(BUCKET_PREFIX):].lower() not in bucket_ids:
                    raise Failure(f"the perk bucket {identifier} names the bucket {entry}, which the buckets do not have")
            elif entry.lower() not in perk_ids:
                raise Failure(f"the perk bucket {identifier} names the perk {entry}, which the perk tables do not have")

    # The perk places of the items.
    items: dict[str, int] = {}
    layouts: list[list] = []
    layout_index: dict[str, int] = {}
    perk_place_columns = [column for column in ITEM_COLUMNS if column.startswith("Perk")]
    for sheet in sheets[ITEM_TYPE]:
        require(sheet, ("ItemID", "ItemStatsRef"))
        # Each item table has the columns Perk1..5, PerkBucket1..5, ForceRarity, and CanHavePerks. The tables of
        # the shop items (MTX) have no column PerkSlot1..5: their items have no perk slot.
        require(sheet, [column for column in ITEM_COLUMNS if not column.startswith("PerkSlot")])
        slot_columns = [column for column in perk_place_columns if column.startswith("PerkSlot")]
        if any(column in sheet.columns for column in slot_columns):
            require(sheet, slot_columns)
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "ItemID")
            if not identifier or identifier.lower() in items:
                continue
            def place(column: str) -> str:
                value = text(sheet, row, column)
                return "" if value.lower() in ITEM_REFERENCES_NOT_IN_GAME else value

            slots = [place(f"PerkSlot{n}") for n in range(1, 6)]
            fixed = [place(f"Perk{n}") for n in range(1, 6)]
            rolled = [place(f"PerkBucket{n}") for n in range(1, 6)]
            if not any(slots) and not any(fixed) and not any(rolled):
                continue
            for value in slots + fixed + rolled:
                if value and value.lower() not in perk_ids and value.lower() not in bucket_ids:
                    raise Failure(f"the item {identifier} names the perk or bucket {value}, which the tables do not have")
            force = text(sheet, row, "ForceRarity")
            # A blank ForceRarity is -1 (loader 0x442DD20): the rarity comes from the perks.
            layout = [slots, fixed, rolled, int(number(sheet, row, "ForceRarity")) if force else -1,
                      number(sheet, row, "CanHavePerks") != 0.0]
            key = json.dumps(layout)
            if key not in layout_index:
                layout_index[key] = len(layouts)
                layouts.append(layout)
            items[identifier.lower()] = layout_index[key]

    # The level that a consumable needs (RequiredLevel of its master item row; finding F5 of the review of stage 1.3b).
    # Each master item table has the column (finding F11 of the review of stage 3.3b-S1: a table with no such column is not
    # level 0), and each consumable has a master item row. An empty cell is the level 0 (the loader of the client reads 0).
    item_levels: dict[str, float] = {}
    for sheet in sheets[ITEM_TYPE]:
        require(sheet, ("ItemID", "RequiredLevel"))
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "ItemID").lower()
            if identifier and identifier not in item_levels:
                item_levels[identifier] = number(sheet, row, "RequiredLevel")

    # The consumables.
    consumables: dict[str, dict] = {}
    consumables_with_no_item: list[str] = []
    for sheet in sheets[CONSUMABLE_TYPE]:
        require(sheet, CONSUMABLE_COLUMNS)
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "ConsumableID")
            if not identifier or identifier.lower() in consumables:
                continue
            if identifier.lower() not in item_levels:
                # No master item row (138 consumables of the supported build): no item of the game uses this consumable, and
                # the file has no row for it, not a row with the level 0 (finding F11).
                consumables_with_no_item.append(identifier)
                continue
            durations = []
            for part in split_list(text(sheet, row, "DurationOverrides")):
                try:
                    durations.append(rounded(float(part)))
                except ValueError as error:
                    raise Failure(f"the consumable {identifier} has a duration that is not a number") from error
            cooldown = number(sheet, row, "CooldownDuration")
            if cooldown < 0.0:
                raise Failure(f"the consumable {identifier} has a negative cooldown")
            consumables[identifier.lower()] = {
                "id": identifier, "effects": split_list(text(sheet, row, "AddStatusEffects")), "durations": durations,
                "cooldown": text(sheet, row, "CooldownId"), "cooldownTime": rounded(cooldown),
                "removeEffects": split_list(text(sheet, row, "RemoveStatusEffects")),
                "removeCategories": split_list(text(sheet, row, "RemoveStatusEffectCategories")),
                "noCombat": number(sheet, row, "DisableUseInCombat") != 0.0,
                "shared": bool(text(sheet, row, "SharedCooldownLocString")),
                "level": int(item_levels[identifier.lower()])}
            if consumables[identifier.lower()]["level"] < 0:
                raise Failure(f"the consumable {identifier} has a negative RequiredLevel")

    # The equip load: the row Player of the encumbrance table, and the load of each armor item.
    encumbrance = None
    for sheet in sheets[ENCUMBRANCE_TYPE]:
        require(sheet, ("ContainerTypeID",))
        for row in range(sheet.row_count):
            if text(sheet, row, "ContainerTypeID") == "Player":
                # Each value of the row Player is a column of its table (finding F14): an absent column is not 0.
                require(sheet, ENCUMBRANCE_COLUMNS.values())
                encumbrance = {member: rounded(number(sheet, row, column)) for member, column in ENCUMBRANCE_COLUMNS.items()}
    if encumbrance is None:
        raise Failure("the encumbrance table has no row Player")
    for member in ("equipLoadMax", "ratioMedium", "ratioHeavy"):
        if encumbrance[member] <= 0.0:
            raise Failure(f"the row Player of the encumbrance table has no value {member}")
    armor_load: dict[str, float] = {}
    for sheet in sheets[ARMOR_TYPE]:
        require(sheet, ("WeaponID", "WeightOverride"))
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "WeaponID").lower()
            load = number(sheet, row, "WeightOverride")
            if identifier and load > 0.0:
                armor_load.setdefault(identifier, rounded(load))
    equip_load: dict[str, float] = {}
    for sheet in sheets[ITEM_TYPE]:
        for row in range(sheet.row_count):
            identifier = text(sheet, row, "ItemID").lower()
            load = armor_load.get(text(sheet, row, "ItemStatsRef").lower())
            if identifier and load is not None:
                equip_load.setdefault(identifier, load)

    # The weights of the exclusive labels for the perk roll (the label roll of 0x448E4F0).
    labels: dict[str, list] = {}
    for sheet in sheets[LABEL_TYPE]:
        require(sheet, ("ExclusiveLabelID", "Weight", "ItemClassOverrides"))
        for row in range(sheet.row_count):
            label = text(sheet, row, "ExclusiveLabelID")
            if not label:
                continue
            overrides = {}
            for part in split_list(text(sheet, row, "ItemClassOverrides"), ","):
                if ":" not in part:
                    raise Failure(f"the exclusive label {label} has an item class override with no weight: {part!r}")
                name, weight = part.split(":", 1)
                try:
                    overrides[name.strip()] = rounded(float(weight))
                except ValueError as error:
                    raise Failure(f"the exclusive label {label} has an override weight that is not a number") from error
            labels.setdefault(label, [rounded(number(sheet, row, "Weight")), overrides])

    # The perk swaps of the game (ItemPerkSwapData): the client shows and uses the new perk of a row whose key fits
    # the item (live check I2 of stage 1.3b). In the sequence of the column ID.
    swaps = []
    for sheet in sheets[SWAP_TYPE]:
        require(sheet, ("ID", "Key", "KeyType", "OldPerk", "NewPerk"))
        for row in range(sheet.row_count):
            entry = [int(number(sheet, row, "ID")), text(sheet, row, "KeyType"), text(sheet, row, "Key"),
                     text(sheet, row, "OldPerk"), text(sheet, row, "NewPerk")]
            if entry[1] not in ("ItemClass", "ItemId") or not entry[2] or not entry[3]:
                raise Failure(f"row {row} of the perk swap table {sheet.name} has no key type, key, or old perk")
            for perk in entry[3:]:
                if perk and perk.lower() not in perk_ids and perk.lower() not in SWAP_PERKS_NOT_IN_GAME:
                    raise Failure(f"the perk swap {entry[0]} names the perk {perk}, which the perk tables do not have")
            swaps.append(entry)
    swaps.sort()

    return {"format": 2, "count": len(perks) + 1, "tables": tables, "perks": perks, "affixes": affixes,
            "abilities": abilities, "buckets": buckets, "layouts": layouts, "items": items, "consumables": consumables,
            "consumablesWithNoItem": len(consumables_with_no_item),
            "encumbrance": encumbrance, "equipLoad": equip_load, "labels": labels, "swaps": swaps, "runes": runes,
            "generation": read_generation(sources)}


def read_generation(sources: Sources) -> dict:
    """@return The perk generation data of the base attributes file (PerkGenerationData): for each tier the largest
    perk channel and the gear score ranges of each perk count (General), and the level of a player from which a loot
    item gets rolled perks (Min Level Roll Perks)."""
    roots = sources.stream(BASE_ATTRIBUTES_PATH)
    if roots is None:
        raise Failure(f"the game copy has no source for {BASE_ATTRIBUTES_PATH}")

    def find(name: str):
        wanted = field_id(name)
        for root in roots:
            for element in walk(root):
                if element.name == wanted:
                    return element
        raise Failure(f"{BASE_ATTRIBUTES_PATH} has no field \"{name}\"")

    def child_of(element, name: str):
        wanted = field_id(name)
        return next((entry for entry in element.children if entry.name == wanted), None)

    def whole(element, name: str) -> int:
        child = child_of(element, name) if element is not None else None
        if child is None or not child.value or len(child.value) > 8:
            raise Failure(f"a field \"{name}\" of the perk generation data is not a whole number")
        return int.from_bytes(child.value, "big")

    generation = find("Perk Generation Data")
    # The tier list is the member whose elements have a "Max Perk Channel" (the label of the list has no known name).
    tier_list = next((member for member in generation.children
                      if member.children and all(child_of(entry, "Max Perk Channel") is not None for entry in member.children)), None)
    if tier_list is None:
        raise Failure("the perk generation data has no tier")
    tiers = []
    for tier in tier_list.children:
        counts = []
        # The perk count ranges: the first member of the tier whose elements have a "value1" (General; the second one
        # is Crafting, with the same ranges in the supported build).
        general = next((member for member in tier.children
                        if member.children and all(child_of(entry, "value1") is not None for entry in member.children)), None)
        for entry in ([] if general is None else general.children):
            ranges = child_of(entry, "value2")
            pairs = [[whole(pair, "value1"), whole(pair, "value2")] for pair in ([] if ranges is None else ranges.children)]
            if any(high < low for low, high in pairs):
                raise Failure("a gear score range of the perk generation data ends below its start")
            counts.append([whole(entry, "value1"), pairs])
        tiers.append([whole(tier, "Max Perk Channel"), counts])
    minimum = find("Min Level Roll Perks")
    return {"minLevelRollPerks": int.from_bytes(minimum.value, "big"), "tiers": tiers}


def print_found(data: dict, texts: list[str]) -> None:
    wanted = [entry.lower() for entry in texts]
    for index, perk in enumerate(data["perks"], start=1):
        if any(entry in perk[0].lower() for entry in wanted):
            print(f"perk {index}: {perk}")
    for name in ("affixes", "abilities", "buckets", "items", "consumables", "equipLoad"):
        for key, value in data[name].items():
            if any(entry in key.lower() for entry in wanted):
                print(f"{name}: {key}: {data['layouts'][value] if name == 'items' else value}")


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
        print(f"perks: {error}. No file was written.", file=sys.stderr)
        return 1
    except (OSError, ValueError) as error:
        print(f"perks: {target}: {error}", file=sys.stderr)
        return 1
    print(f"{target}: {data['count']} entries of the perk list ({len(data['tables'])} tables), {len(data['affixes'])} "
          f"affixes, {len(data['abilities'])} ability rows, {len(data['buckets'])} perk buckets, {len(data['items'])} "
          f"items with perk places ({len(data['layouts'])} layouts), {len(data['consumables'])} consumables ({data['consumablesWithNoItem']} with no "
          f"master item row left out), {len(data['equipLoad'])} armor items "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
