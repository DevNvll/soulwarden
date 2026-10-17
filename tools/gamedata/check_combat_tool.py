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
    # A column is a number column when a value of a row is a number. None is a blank cell (its source text is empty);
    # each other number cell has its value as its source text, as the tables of the game have.
    numeric = [any(isinstance(row.get(column), (int, float)) for row in rows) for column in columns]
    descriptors = bytearray()
    for column, is_number in zip(columns, numeric):
        descriptors.extend(struct.pack("<III", 0, text(column), 2 if is_number else 1))
    cells = bytearray()
    for row in rows:
        for column, is_number in zip(columns, numeric):
            value = row[column]
            if is_number:
                source = "" if value is None else ("1" if value is True else "0" if value is False else str(value))
                cells.extend(struct.pack("<If", text(source), 0.0 if value is None else float(value)))
            else:
                cells.extend(struct.pack("<II", text("" if value is None else value), 0))
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
                  "WeaponBaseDamageCompoundIncrease": 0.0, "CritChance": 0.07, "CritDamageMultiplier": 1.3}
        if mode == "missing-column":
            weapon.pop("BaseDamage")
        if mode == "one-crit-column":
            weapon.pop("CritDamageMultiplier")
        if mode == "crit-above-one":
            weapon["CritChance"] = 1.5
        if mode == "text-cell":
            weapon["BaseDamage"] = "sixty-four"
        add("Weapons", tool.WEAPON_TYPE, [weapon])
        # The impact columns (stage 3.3b-S4): the light row pulls 1.5 m (a creature: its "_AI" value 2 m), the heavy
        # row moves nothing.
        light_impact = {"ImpactDistanceX": 0.0, "ImpactDistanceY": -1.5, "ImpactDistanceZ": 0.0, "ImpactDecayRate": 0.8,
                        "ImpactDistanceX_AI": 0.0, "ImpactDistanceY_AI": -2.0, "ImpactDistanceZ_AI": 0.0,
                        "ImpactDecayRate_AI": 0.0, "UseAttackerPosForReaction": 1.0}
        heavy_impact = {column: 0.0 for column in light_impact}
        if mode == "decay-above-one":
            light_impact["ImpactDecayRate"] = 1.5
        # The critical, head, and behind cells (stage 3.3b-S5).
        hit_cells = {"CanCrit": False, "NoHeadshot": False, "NoBackstab": False, "DmgCoefHead": 1.3, "DmgCoefCrit": 0.0}
        # The reaction cells (user test 2): the light row has the values for a creature, the heavy row NoReaction and blank "_AI" cells.
        light_reaction = {"NoReaction": False, "PowerLevel": 0.0, "HitStun": 15.0, "PowerLevel_AI": 2.0, "HitStun_AI": 23.0}
        heavy_reaction = {"NoReaction": True, "PowerLevel": 0.0, "HitStun": 25.0, "PowerLevel_AI": None, "HitStun_AI": None}
        if mode == "part-power":
            light_reaction["PowerLevel_AI"] = 2.5
        if mode == "text-flag":
            light_reaction["NoReaction"] = "maybe"  # A text column: the other row has the text "1".
            heavy_reaction["NoReaction"] = "1"
        # The light row has a blank CanCrit cell (an ordinary attack), the heavy row an explicit 0 (format 2: null and false;
        # code review of stage 3.3b-S5, finding F2).
        damage = [{"DamageID": "Light", "WeaponCategory": "Sword", "AttackType": "Light", "DamageType": "Slash",
                   "DmgCoef": 1.0, "AddDmg": 0.0, "AttackRuneCharge": 1.105, "HitRuneCharge": 0.27625, **light_impact, **hit_cells,
                   **light_reaction, "CanCrit": None}]
        if mode == "no-impact-column":
            damage[0].pop("ImpactDecayRate_AI")
        if mode == "no-hit-column":
            damage[0].pop("NoBackstab")
        if mode == "no-reaction-column":
            damage[0].pop("HitStun_AI")
        if mode != "missing-row":
            damage.append({"DamageID": "Heavy", "WeaponCategory": "Sword", "AttackType": "Heavy",
                           "DamageType": "Slsh" if mode == "unknown-type" else "Slash", "DmgCoef": 1.2, "AddDmg": 0.0,
                           "AttackRuneCharge": 1.604, "HitRuneCharge": 0.401, **heavy_impact, **hit_cells, **heavy_reaction,
                           # The status effects of a row (format 3, stage 3.3b-M2): a list with a comma, only after damage.
                           "StatusEffect": "Test_Root,Test_Slow", "EffectOnlyOnDamage": True})
            damage[0]["StatusEffect"] = ""
            damage[0]["EffectOnlyOnDamage"] = False
            if mode == "no-impact-column":
                damage[1].pop("ImpactDecayRate_AI")
            if mode == "no-hit-column":
                damage[1].pop("NoBackstab")
            if mode == "no-reaction-column":
                damage[1].pop("HitStun_AI")
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
    ("no-impact-column", False, "a damage table with no column ImpactDecayRate_AI (stage 3.3b-S4)"),
    ("decay-above-one", False, "a decay rate of an impact above 1 (stage 3.3b-S4)"),
    ("one-crit-column", False, "a weapon table with CritChance and no CritDamageMultiplier (stage 3.3b-S5)"),
    ("crit-above-one", False, "a CritChance above 1 (stage 3.3b-S5)"),
    ("no-hit-column", False, "a damage table with no column NoBackstab (stage 3.3b-S5)"),
    ("no-reaction-column", False, "a damage table with no column HitStun_AI (user test 2)"),
    ("part-power", False, "a PowerLevel_AI that is not a whole number (user test 2)"),
    ("text-flag", False, "a NoReaction cell with a text that is not a flag (user test 2)"),
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
                       and tables["armor"]["ratingExponent"] == 1.2 and tables["levelDamage"] == [[6, 0.2]]
                       and tables["impacts"] == [["Light", 0.0, -1.5, 0.0, 0.8, 0.0, -2.0, 0.0, 0.0, True]]
                       and tables["criticals"] == [["Sword", 0.07, 1.3]]
                       and tables["format"] == 3 and tables["rowEffects"] == [["Heavy", ["Test_Root", "Test_Slow"], True]]
                       and tables["hitRules"] == [["Light", None, False, False, 1.3, 0.0], ["Heavy", False, False, False, 1.3, 0.0]]
                       and tables["reactions"] == [["Light", False, 0, 15, 2, 23], ["Heavy", True, 0, 25, -1, -1]])
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
