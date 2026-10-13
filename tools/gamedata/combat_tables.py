#!/usr/bin/env python3
"""Write the combat tables of the game as one JSON file.

The backend reads this file and gets what the damage of a melee attack of a player needs
(protocol notes 4.15): the weapon of an item, the damage row of each attack, the damage that
the attributes and the level add, the rule of the gear score, and the defense values of each
creature. Without the file the backend starts and works, and an attack of a player on a
creature has no effect.

Input: the game copy, from the same sources as the creature data tool (creature_data.py: the
pak entries, the pak entries with an Oodle library, or the files that tools/research/pakx.py
extracted).

  Source (table type)                             What the file gets
  the item tables (MasterItemDefinitions)         for each item with a weapon: the weapon row
                                                  (column ItemStatsRef)
  WeaponItemDefinitions                           for each weapon: its use (melee, ranged, ...),
                                                  its tags, its base damage, its four attribute
                                                  factors, the damage rows of its light and of
                                                  its heavy attack (the columns
                                                  TooltipPrimaryAttackData and
                                                  TooltipAlternateAttackData), and its own
                                                  gear score rate
  DamageTable (DamageData)                        for each damage row: the weapon category, the
                                                  attack type, the damage type, the damage
                                                  factor, the damage that the row adds
  DamageTypes (DamageTypeData)                    for each damage type: its number on the wire
                                                  and its category (physical, elemental, true)
  Strength, Dexterity, Intelligence, Focus        for each value of the attribute: the sum of
  (AttributeDefinition)                           its damage values (column ModifierValueSum)
  the vitals tables (VitalsLevelVariantData,      for each vitals row of a creature: the
  VitalsBaseData)                                 mitigation of physical and of elemental
                                                  damage, and for each damage type the weakness
                                                  and the absorption of its base row
  sharedassets/genericassets/                     the damage that one level adds, and the rule
  playerbaseattributes.pbadb                      of the gear score (rate, interval, the lowest
                                                  gear score, the limit above which the rate is
                                                  smaller, and the factor of the smaller rate)

Output: {"format": 1,
"constants": {"levelDamage": N, "gearScoreRate": N, "gearScoreInterval": N, "gearScoreMinimum": N,
"gearScoreLimit": N, "gearScoreSmallerRate": N},
"damageTypes": [[name, number, category], ...],
"attributes": {"minimum": N, "strength": [...], "dexterity": [...], "intelligence": [...], "focus": [...]},
"damageRows": [[id, weaponCategory, attackType, damageType, factor, added], ...],
"weapons": [[id, use, tags, baseDamage, strength, dexterity, intelligence, focus, lightRow, heavyRow,
gearScoreRate], ...],
"items": {"<item id in lower case>": weapon, ...},
"defenses": [[physicalMitigation, elementalMitigation, {"<damage type>": weakness, ...},
{"<damage type>": absorption, ...}], ...],
"vitals": {"<vitals row id in lower case>": defense, ...}}.
Entry K of an attribute list is the value `minimum` + K of the attribute. `weapon` is an index
into `weapons`, and `defense` an index into `defenses`. A damage type of a defense entry that is
not there has the value 0.

Usage: combat_tables.py [OUTPUT.json]
       combat_tables.py --find TEXT...   print the weapons and the creatures with a text (no file)
Default output: <SOULWARDEN_GAMEDATA_DIR>/combat-tables.json
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
from pathlib import Path

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
from appearance_tables import Failure, field_id  # noqa: E402
from creature_data import TABLES_FOLDER, Sources, walk  # noqa: E402
from item_catalog import Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "combat-tables.json"
BASE_ATTRIBUTES_PATH = "sharedassets/genericassets/playerbaseattributes.pbadb"
ITEM_TYPE = "MasterItemDefinitions"
WEAPON_TYPE = "WeaponItemDefinitions"
DAMAGE_TYPE = "DamageData"
# The damage table of the weapons of a player. The game has more tables of this type: one for
# each creature family.
DAMAGE_TABLE = "DamageTable"
DAMAGE_TYPE_TYPE = "DamageTypeData"
ATTRIBUTE_TYPE = "AttributeDefinition"
VARIANT_TYPE = "VitalsLevelVariantData"
BASE_TYPE = "VitalsBaseData"
# The armor of a character against an attack of a creature (stage 3.4b): the armor values of
# each armor item, and the damage of a level difference.
ARMOR_TYPE = "ArmorItemDefinitions"
LEVEL_DIFFERENCE_TYPE = "LevelDisparityData"
USED_TYPES = (ITEM_TYPE, WEAPON_TYPE, DAMAGE_TYPE, DAMAGE_TYPE_TYPE, ATTRIBUTE_TYPE, VARIANT_TYPE, BASE_TYPE, ARMOR_TYPE,
              LEVEL_DIFFERENCE_TYPE)
# The table name of each attribute that a weapon can have a factor for, and its member of the file.
ATTRIBUTE_TABLES = {"Strength": "strength", "Dexterity": "dexterity", "Intelligence": "intelligence", "Focus": "focus"}
# The fields of the base attributes file (the field id is the CRC-32 of the name in lower case).
CONSTANT_FIELDS = {
    "levelDamage": "Level Damage Multiplier",
    "gearScoreRate": "Base Damage Compound Increase",
    "gearScoreSmallerRate": "Compound Increase Diminishing Multiplier",
    "gearScoreInterval": "Base Damage Gear Score Interval",
    "gearScoreMinimum": "Min Possible Weapon Gear Score",
    "gearScoreLimit": "Diminishing Gear Score Threshold",
}
WHOLE_CONSTANTS = ("gearScoreInterval", "gearScoreMinimum", "gearScoreLimit")
# The fields of the base attributes file for the armor rule.
ARMOR_FIELDS = {
    "ratingExponent": "Armor Set Rating Exponent",
    "mitigationExponent": "Armor Mitigation Exponent",
}
# References of the tables of the supported build that name a row which no table of the game has
# (lower case). They are facts of the game data, not faults of a source: the weapons are weapons
# of creatures and key items, and the vitals rows are rows of creatures with no base row. A
# weapon with such a row gets no damage row, and such a vitals row gets no defense values. Each
# other reference that does not resolve stops the tool: a table that lost a row, or a table of a
# different game version, must not make a file that looks complete.
ROWS_NOT_IN_GAME = frozenset({"1h_axe_attack1", "1h_axe_attack2", "1h_knife_attack1", "2h_sword_attack1",
                              "2h_sword_attack2"})
BASES_NOT_IN_GAME = frozenset({"werewolf_rabid_20328", "upyr_10221a_named", "upyr_10220a", "upyr_10220b"})
# The one column of the tool that a table of its type can have or not: each other column must be there.
OWN_RATE_COLUMN = "WeaponBaseDamageCompoundIncrease"


def cell(sheet: Sheet, row: int, column: str):
    """@return The value of one cell of a column that the tool needs. A table with no such column stops the tool:
    a cell of an absent column would be an empty text, which the tool cannot tell from a real empty cell."""
    if column not in sheet.columns:
        raise Failure(f"the table {sheet.name} has no column {column}")
    return sheet.cell(row, column)


def number(sheet: Sheet, row: int, column: str) -> float:
    """@return The number of a cell of a column that the tool needs. Some tables have a number as a text, and an
    empty text is 0. A text that is not a number, and a number that is not finite, stop the tool."""
    value = cell(sheet, row, column)
    what = f"the cell {column} of row {row} of the table {sheet.name}"
    if isinstance(value, bool):
        return 0.0
    if not isinstance(value, (int, float)):
        text = str(value).strip()
        try:
            value = float(text) if text else 0.0
        except ValueError as error:
            raise Failure(f"{what} is the text \"{text}\", not a number") from error
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return float(value)


def finite(value: float, what: str) -> float:
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number")
    return value


def defense_columns(sheet: Sheet, type_names: list[str], prefix: str) -> list[str]:
    """@return The damage types for which a base vitals table has a column with this start ("WKN": the weakness,
    "ABS": the absorption). The tables of the game have such a column for a part of the damage types only (not
    for the damage with no mitigation), and not each table has the same part. A table with no such column at all
    is not a base vitals table of the game: it stops the tool."""
    kinds = [kind for kind in type_names if prefix + kind in sheet.columns]
    if not kinds:
        raise Failure(f"the table {sheet.name} has no column {prefix}<damage type>")
    return kinds


def rounded(value: float) -> float:
    """A cell is a 32-bit float: 7 digits give the same float again, and the file is shorter."""
    return float(f"{value:.7g}")


def read_constants(sources: Sources, fields: dict[str, str] = CONSTANT_FIELDS) -> dict:
    roots = sources.stream(BASE_ATTRIBUTES_PATH)
    if roots is None:
        raise Failure(f"the game copy has no source for {BASE_ATTRIBUTES_PATH}")
    values: dict[int, bytes] = {}
    for root in roots:
        for element in walk(root):
            if element.name is not None and element.value is not None:
                values.setdefault(element.name, element.value)
    constants = {}
    for member, label in fields.items():
        value = values.get(field_id(label))
        if value is None:
            raise Failure(f"{BASE_ATTRIBUTES_PATH} has no field \"{label}\"")
        if member in WHOLE_CONSTANTS:
            constants[member] = int.from_bytes(value, "big")
        elif len(value) == 4:
            constants[member] = rounded(finite(struct.unpack(">f", value)[0], f"the field \"{label}\""))
        else:
            raise Failure(f"the field \"{label}\" of {BASE_ATTRIBUTES_PATH} is not a 32-bit float")
    if constants.get("gearScoreInterval", 1) <= 0:
        raise Failure(f"the gear score interval of {BASE_ATTRIBUTES_PATH} is 0")
    return constants


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


def build_tables(sources: Sources) -> dict:
    constants = read_constants(sources)
    sheets = read_sheets(sources)

    damage_types = []
    for sheet in sheets[DAMAGE_TYPE_TYPE]:
        for row in range(sheet.row_count):
            name = str(cell(sheet, row, "TypeID"))
            wire = int(number(sheet, row, "IntID"))
            if name and 0 < wire < 256:
                damage_types.append([name, wire, str(cell(sheet, row, "Category"))])
    if not damage_types:
        raise Failure("the damage type table has no row")
    type_names = [entry[0] for entry in damage_types]

    attributes: dict = {}
    for sheet in sheets[ATTRIBUTE_TYPE]:
        member = ATTRIBUTE_TABLES.get(sheet.name)
        if member is None or member in attributes:
            continue
        first = int(number(sheet, 0, "Level")) if sheet.row_count else 0
        sums = []
        for row in range(sheet.row_count):
            if int(number(sheet, row, "Level")) != first + row:
                raise Failure(f"row {row} of the table {sheet.name} is not the attribute value {first + row}")
            sums.append(rounded(number(sheet, row, "ModifierValueSum")))
        if attributes.setdefault("minimum", first) != first:
            raise Failure(f"the table {sheet.name} starts at the value {first}, not at {attributes['minimum']}")
        attributes[member] = sums
    for member in ATTRIBUTE_TABLES.values():
        if not attributes.get(member):
            raise Failure(f"the game copy has no attribute table for {member}")

    damage_rows = []
    row_ids: set[str] = set()
    # The rows of the damage table that have no damage type: a weapon can name one, and it then has no damage row.
    typeless: set[str] = set()
    for sheet in sheets[DAMAGE_TYPE]:
        if sheet.name != DAMAGE_TABLE:
            continue
        for row in range(sheet.row_count):
            name = str(cell(sheet, row, "DamageID"))
            kind = str(cell(sheet, row, "DamageType"))
            if not name or name.lower() in row_ids:
                continue
            if kind not in type_names:
                # A row with no damage type is a row of an attack that does no damage (a block, a
                # dodge). A type that the damage type table does not have is a fault of a table.
                if kind:
                    raise Failure(f"the damage row {name} names the damage type {kind}, which the damage type table "
                                  f"does not have")
                typeless.add(name.lower())
                continue
            row_ids.add(name.lower())
            damage_rows.append([name, str(cell(sheet, row, "WeaponCategory")), str(cell(sheet, row, "AttackType")), kind,
                                rounded(number(sheet, row, "DmgCoef")), rounded(number(sheet, row, "AddDmg"))])
    if not damage_rows:
        raise Failure(f"the game copy has no damage table with the name {DAMAGE_TABLE}")

    weapons = []
    weapon_index: dict[str, int] = {}

    def row_of(weapon: str, reference: str) -> str:
        """@return The damage row that a weapon names, or an empty text for no row. A name that does not resolve
        stops the tool, when it is not a name that the game itself does not have (ROWS_NOT_IN_GAME)."""
        if reference.lower() in row_ids:
            return reference
        if reference and reference.lower() not in typeless and reference.lower() not in ROWS_NOT_IN_GAME:
            raise Failure(f"the weapon {weapon} names the damage row {reference}, which the damage table "
                          f"{DAMAGE_TABLE} does not have")
        return ""

    for sheet in sheets[WEAPON_TYPE]:
        for row in range(sheet.row_count):
            name = str(cell(sheet, row, "WeaponID"))
            if not name or name.lower() in weapon_index:
                continue
            light = row_of(name, str(cell(sheet, row, "TooltipPrimaryAttackData")))
            heavy = row_of(name, str(cell(sheet, row, "TooltipAlternateAttackData")))
            weapon_index[name.lower()] = len(weapons)
            weapons.append([name, str(cell(sheet, row, "PrimaryUse")), str(cell(sheet, row, "MannequinTag")),
                            rounded(number(sheet, row, "BaseDamage")),
                            rounded(number(sheet, row, "ScalingStrength")),
                            rounded(number(sheet, row, "ScalingDexterity")),
                            rounded(number(sheet, row, "ScalingIntelligence")),
                            rounded(number(sheet, row, "ScalingFocus")),
                            light, heavy,
                            # The own gear score rate of a weapon. A weapon table can have no such
                            # column (the table of the heart runes): its weapons have the rate 0,
                            # which is the rate of the constants.
                            rounded(number(sheet, row, OWN_RATE_COLUMN)) if OWN_RATE_COLUMN in sheet.columns else 0.0])

    items: dict[str, int] = {}
    for sheet in sheets[ITEM_TYPE]:
        for row in range(sheet.row_count):
            weapon = weapon_index.get(str(cell(sheet, row, "ItemStatsRef")).lower())
            name = str(cell(sheet, row, "ItemID")).lower()
            if weapon is not None and name:
                items.setdefault(name, weapon)

    bases: dict[str, list] = {}
    for sheet in sheets[BASE_TYPE]:
        weak_kinds = defense_columns(sheet, type_names, "WKN")
        absorb_kinds = defense_columns(sheet, type_names, "ABS")
        for row in range(sheet.row_count):
            name = str(cell(sheet, row, "VitalsID")).lower()
            if not name or name in bases:
                continue
            weakness = {}
            absorption = {}
            for kind in weak_kinds:
                value = number(sheet, row, "WKN" + kind)
                if value != 0.0:
                    weakness[kind] = rounded(value)
            for kind in absorb_kinds:
                value = number(sheet, row, "ABS" + kind)
                if value != 0.0:
                    absorption[kind] = rounded(value)
            bases[name] = [rounded(number(sheet, row, "PhysicalMitigation")),
                           rounded(number(sheet, row, "ElementalMitigation")), weakness, absorption]
    defenses: list[list] = []
    defense_index: dict[str, int] = {}
    vitals: dict[str, int] = {}

    def defense_of(base: str) -> int | None:
        entry = bases.get(base)
        if entry is None:
            return None
        key = json.dumps(entry, sort_keys=True)
        if key not in defense_index:
            defense_index[key] = len(defenses)
            defenses.append(entry)
        return defense_index[key]

    for sheet in sheets[VARIANT_TYPE]:
        for row in range(sheet.row_count):
            name = str(cell(sheet, row, "VitalsID")).lower()
            base = str(cell(sheet, row, "BaseVitalsId")).lower()
            index = defense_of(base)
            if name and index is None and base not in BASES_NOT_IN_GAME:
                # Without its base row a creature has no defense values: the backend would give
                # it the raw damage, with no sign of the fault.
                raise Failure(f"the vitals row {name} names the base row {base or '(no name)'}, which the base vitals "
                              f"tables do not have")
            if name and index is not None:
                vitals.setdefault(name, index)
    # A creature can use a base row with no level variant row.
    for name in bases:
        if name not in vitals:
            vitals[name] = defense_of(name)
    if not vitals:
        raise Failure("the vitals tables have no row")

    # The armor of each armor item: its factor for a physical and for an elemental damage, and
    # the part of the armor of a full set that the item gives. An item names its armor row in
    # the column ItemStatsRef, as a weapon does.
    armor_rows: dict[str, list] = {}
    for sheet in sheets[ARMOR_TYPE]:
        for row in range(sheet.row_count):
            name = str(cell(sheet, row, "WeaponID")).lower()
            if not name or name in armor_rows:
                continue
            values = [rounded(number(sheet, row, "PhysicalArmorSetScaleFactor")),
                      rounded(number(sheet, row, "ElementalArmorSetScaleFactor")),
                      rounded(number(sheet, row, "ArmorRatingScaleFactor"))]
            if any(value < 0.0 for value in values):
                raise Failure(f"the armor row {name} has a factor below 0")
            if values[2] > 0.0 and (values[0] > 0.0 or values[1] > 0.0):
                armor_rows[name] = values
    armor_items: dict[str, list] = {}
    for sheet in sheets[ITEM_TYPE]:
        for row in range(sheet.row_count):
            values = armor_rows.get(str(cell(sheet, row, "ItemStatsRef")).lower())
            name = str(cell(sheet, row, "ItemID")).lower()
            if values is not None and name:
                armor_items.setdefault(name, values)
    armor = dict(read_constants(sources, ARMOR_FIELDS), items=armor_items)
    # The damage of a level difference (the level of the attacker minus the level of its target).
    level_damage = []
    for sheet in sheets[LEVEL_DIFFERENCE_TYPE]:
        for row in range(sheet.row_count):
            difference = number(sheet, row, "LevelDisparity")
            value = rounded(number(sheet, row, "DamageModifier"))
            if difference != int(difference):
                raise Failure(f"the level difference {difference:g} of the table {sheet.name} is not a whole number")
            if value != 0.0:
                level_damage.append([int(difference), value])

    return {"format": 1, "constants": constants, "damageTypes": damage_types, "attributes": attributes,
            "damageRows": damage_rows, "weapons": weapons, "items": items, "defenses": defenses, "vitals": vitals,
            "armor": armor, "levelDamage": sorted(level_damage)}


def print_found(tables: dict, texts: list[str]) -> None:
    rows = {row[0].lower(): row for row in tables["damageRows"]}
    for text in texts:
        wanted = text.lower()
        for name, index in sorted(tables["items"].items()):
            weapon = tables["weapons"][index]
            if wanted not in name and wanted not in weapon[0].lower():
                continue
            attacks = ", ".join(f"{label} {row[0]} ({row[3]}, factor {row[4]}, added {row[5]})"
                                for label, row in (("light", rows.get(weapon[8].lower())),
                                                   ("heavy", rows.get(weapon[9].lower()))) if row)
            print(f"item {name}: weapon {weapon[0]}, use \"{weapon[1]}\", tags {weapon[2]}, base damage {weapon[3]}, "
                  f"factors strength {weapon[4]} dexterity {weapon[5]} intelligence {weapon[6]} focus {weapon[7]}; "
                  f"{attacks or 'no damage row'}")
        for name, index in sorted(tables["vitals"].items()):
            if wanted in name:
                defense = tables["defenses"][index]
                print(f"creature {name}: mitigation physical {defense[0]} elemental {defense[1]}, weakness "
                      f"{defense[2] or 'none'}, absorption {defense[3] or 'none'}")


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
        tables = build_tables(sources)
        if find is not None:
            print_found(tables, find)
            return 0
        target.parent.mkdir(parents=True, exist_ok=True)
        # The backend must not read a file that is not complete: write, then rename.
        temporary = target.with_name(target.name + ".tmp")
        temporary.write_text(json.dumps(tables, separators=(",", ":")) + "\n", encoding="utf-8")
        temporary.replace(target)
    except Failure as error:
        print(f"combat: {error}. No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"combat: {target}: {error}", file=sys.stderr)
        return 1
    melee = sum(1 for weapon in tables["weapons"] if weapon[1] == "Combat Melee" and weapon[8])
    constants = tables["constants"]
    print(f"{target}: {len(tables['items'])} items with a weapon, {len(tables['weapons'])} weapons ({melee} melee "
          f"weapons with a damage row), {len(tables['damageRows'])} damage rows, {len(tables['damageTypes'])} damage "
          f"types, {len(tables['vitals'])} vitals rows with {len(tables['defenses'])} different defense entries "
          f"(source: {', '.join(f'{count} from the {name}' for name, count in sorted(sources.used.items()))})")
    print(f"  one level adds {constants['levelDamage']} of the base damage; the gear score: {constants['gearScoreRate']} "
          f"for each {constants['gearScoreInterval']} above {constants['gearScoreMinimum']}, and above "
          f"{constants['gearScoreLimit']} the rate times {constants['gearScoreSmallerRate']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
