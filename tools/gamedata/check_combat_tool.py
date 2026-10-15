#!/usr/bin/env python3
"""Check of the combat data tool with made-up data tables (no file of the game).

The combat data tool (combat_tables.py) must write no file from sources that are not complete:
a weapon that names a damage row which the damage table does not have, a vitals row that names
a base row which is not there, a table with no column that the tool reads, and a cell that is
not a finite number. Each of these would give a file that looks complete, and the backend would
then refuse the hits of a weapon or give a creature the raw damage, with no sign of the fault
(code review of stage 3.2a, finding 3).

This check makes the binary data tables in memory, gives them to the entry point of the tool
with a source provider that reads no game copy, and compares the status and the output file of
each case. The test program of the backend runs it (case HT11).

Usage: check_combat_tool.py [WORK_FOLDER]
Status 0: each case has the expected result.
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import tempfile
from pathlib import Path
from types import SimpleNamespace

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import combat_tables as tool  # noqa: E402

CONSTANTS = {"levelDamage": 0.025, "gearScoreRate": 0.0112, "gearScoreSmallerRate": 0.6667,
             "gearScoreInterval": 5, "gearScoreMinimum": 100, "gearScoreLimit": 500}
# A damage row name that the tables of the supported build name and that the game does not have.
KNOWN_ABSENT_ROW = "1H_Axe_Attack2"


def sheet(name: str, kind: str, rows: list[dict]) -> bytes:
    """@return One data table file with the layout of the game (item_catalog.Sheet reads it)."""
    texts = bytearray()

    def text(value) -> int:
        offset = len(texts)
        texts.extend(str(value).encode() + b"\0")
        return offset

    columns = list(rows[0])
    head = bytearray(0x5C)
    struct.pack_into("<I", head, 8, text(name))
    struct.pack_into("<I", head, 16, text(kind))
    struct.pack_into("<II", head, 0x44, len(columns), len(rows))
    numeric = [isinstance(rows[0][column], (int, float)) for column in columns]
    descriptors = bytearray()
    for column, is_number in zip(columns, numeric):
        descriptors.extend(struct.pack("<III", 0, text(column), 2 if is_number else 1))
    cells = bytearray()
    for row in rows:
        for column, is_number in zip(columns, numeric):
            cells.extend(struct.pack("<If", 0, row[column]) if is_number else struct.pack("<II", text(row[column]), 0))
    struct.pack_into("<I", head, 0x38, len(head) + len(descriptors) + len(cells) - 0x3C)
    return bytes(head + descriptors + cells + texts)


class MadeUpSources:
    """The source provider of the tool, with made-up tables. `mode` selects the fault of the tables."""

    mode = "valid"

    def __init__(self, assets) -> None:
        self.used = {"made-up tables": 10}
        self.files: dict[str, bytes] = {}

        def add(name: str, kind: str, rows: list[dict]) -> None:
            self.files[tool.TABLES_FOLDER + name.lower() + ".datasheet"] = sheet(name, kind, rows)

        mode = self.mode
        add("Items", tool.ITEM_TYPE, [{"ItemID": "TestSword", "ItemStatsRef": "Sword"}])
        weapon = {"WeaponID": "Sword", "PrimaryUse": "Combat Melee", "MannequinTag": "Sword", "BaseDamage": 64.0,
                  "ScalingStrength": 0.0085, "ScalingDexterity": 0.0065, "ScalingIntelligence": 0.0, "ScalingFocus": 0.0,
                  "TooltipPrimaryAttackData": "Light",
                  "TooltipAlternateAttackData": KNOWN_ABSENT_ROW if mode == "known-absent-row" else "Heavy",
                  "WeaponBaseDamageCompoundIncrease": 0.0}
        if mode == "missing-column":
            weapon.pop("BaseDamage")
        if mode == "text-cell":
            weapon["BaseDamage"] = "sixty-four"
        add("Weapons", tool.WEAPON_TYPE, [weapon])
        damage = [{"DamageID": "Light", "WeaponCategory": "Sword", "AttackType": "Light", "DamageType": "Slash",
                   "DmgCoef": 1.0, "AddDmg": 0.0, "AttackRuneCharge": 1.105, "HitRuneCharge": 0.27625}]
        if mode != "missing-row":
            damage.append({"DamageID": "Heavy", "WeaponCategory": "Sword", "AttackType": "Heavy",
                           "DamageType": "Slsh" if mode == "unknown-type" else "Slash", "DmgCoef": 1.2, "AddDmg": 0.0,
                           "AttackRuneCharge": 1.604, "HitRuneCharge": 0.401})
        add(tool.DAMAGE_TABLE, tool.DAMAGE_TYPE, damage)
        add("DamageTypes", tool.DAMAGE_TYPE_TYPE, [{"TypeID": "Slash", "IntID": 4, "Category": "Physical"}])
        for attribute in tool.ATTRIBUTE_TABLES:
            add(attribute, tool.ATTRIBUTE_TYPE, [{"Level": 5, "ModifierValueSum": 0.0}, {"Level": 6, "ModifierValueSum": 1.25}])
        base = {"VitalsID": "BaseWolf", "PhysicalMitigation": 0.35, "ElementalMitigation": 0.0, "WKNSlash": 0.2,
                "ABSSlash": 0.1}
        if mode == "nan-weakness":
            base["WKNSlash"] = float("nan")
        if mode == "no-defense-column":
            base.pop("WKNSlash")
        add("Base", tool.BASE_TYPE, [base])
        add("Variant", tool.VARIANT_TYPE, [{"VitalsID": "TestWolf",
                                            "BaseVitalsId": "Missing" if mode == "missing-base" else "BaseWolf"}])
        add("ArmorItems", tool.ITEM_TYPE, [{"ItemID": "TestChest", "ItemStatsRef": "Chest"}])
        chest = {"WeaponID": "Chest", "PhysicalArmorSetScaleFactor": 0.25, "ElementalArmorSetScaleFactor": 0.25,
                 "ArmorRatingScaleFactor": 0.3}
        if mode == "nan-armor":
            chest["ArmorRatingScaleFactor"] = float("nan")
        if mode == "no-armor-column":
            chest.pop("ElementalArmorSetScaleFactor")
        add("Armor", tool.ARMOR_TYPE, [chest])
        add("LevelDisparity", tool.LEVEL_DIFFERENCE_TYPE,
            [{"LevelDisparity": 5.0, "DamageModifier": 0.0},
             {"LevelDisparity": 6.5 if mode == "part-level" else 6.0, "DamageModifier": 0.2}])

    def stream(self, path):
        fields = [SimpleNamespace(name=tool.field_id(label),
                                  value=(int(CONSTANTS[member]).to_bytes(4, "big") if member in tool.WHOLE_CONSTANTS
                                         else struct.pack(">f", CONSTANTS[member])),
                                  children=[]) for member, label in tool.CONSTANT_FIELDS.items()]
        if self.mode != "no-armor-constant":
            fields += [SimpleNamespace(name=tool.field_id(label), value=struct.pack(">f", 1.2), children=[])
                       for label in tool.ARMOR_FIELDS.values()]
        return fields

    def paths(self, prefix, suffix):
        return sorted(self.files)

    def read(self, path):
        return self.files[path]

    def check_complete(self) -> None:
        pass


# Each case: the fault of the tables, and True when the tool must write its file.
CASES = [
    ("valid", True, "complete tables"),
    ("known-absent-row", True, "a weapon that names a damage row which the game itself does not have"),
    ("missing-row", False, "a weapon that names a damage row which the damage table does not have"),
    ("unknown-type", False, "a damage row that names a damage type which the damage type table does not have"),
    ("missing-base", False, "a vitals row that names a base row which is not there"),
    ("missing-column", False, "a weapon table with no column BaseDamage"),
    ("text-cell", False, "a base damage cell with a text that is not a number"),
    ("nan-weakness", False, "a weakness cell that is not a finite number"),
    ("no-defense-column", False, "a base vitals table with no weakness column"),
    ("nan-armor", False, "an armor factor that is not a finite number"),
    ("no-armor-column", False, "an armor table with no column ElementalArmorSetScaleFactor"),
    ("no-armor-constant", False, "a base attributes file with no exponent of the armor rule"),
    ("part-level", False, "a level difference that is not a whole number"),
]


def main() -> int:
    work = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(tempfile.mkdtemp(prefix="soulwarden-combat-tool-"))
    work.mkdir(parents=True, exist_ok=True)
    tool.Sources = MadeUpSources
    failures = 0
    for mode, writes, what in CASES:
        MadeUpSources.mode = mode
        target = work / f"combat-tool-check-{mode}.json"
        target.unlink(missing_ok=True)
        sys.argv = ["combat_tables.py", str(target)]
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = tool.main()
        exists = target.exists()
        correct = (status == 0 and exists) if writes else (status == 1 and not exists)
        if correct and mode == "valid":
            tables = json.loads(target.read_text(encoding="utf-8"))
            correct = (tables["weapons"][0][3] == 64.0 and tables["weapons"][0][9] == "Heavy"
                       and sorted(tables["vitals"]) == ["basewolf", "testwolf"] and tables["defenses"][0][2] == {"Slash": 0.2}
                       and tables["armor"]["items"] == {"testchest": [0.25, 0.25, 0.3]}
                       and tables["armor"]["ratingExponent"] == 1.2 and tables["levelDamage"] == [[6, 0.2]])
        if correct and mode == "known-absent-row":
            correct = json.loads(target.read_text(encoding="utf-8"))["weapons"][0][9] == ""
        message = output.getvalue().strip().splitlines()[-1] if output.getvalue().strip() else ""
        print(f"{'ok  ' if correct else 'FAIL'} {what}: status {status}, {'a file' if exists else 'no file'}"
              + (f" ({message})" if not writes and message else ""))
        failures += not correct
        target.unlink(missing_ok=True)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
