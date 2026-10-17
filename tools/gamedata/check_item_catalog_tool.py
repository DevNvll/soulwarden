#!/usr/bin/env python3
"""Check of the item catalog tool (item_catalog.py) with made-up tables (no file of the game). Stage 2.2.

The tool writes the removal protections of each master item row (format 2: the columns nonRemovable and notDroppable from the cells
Nonremovable and NotDroppable; code review of stage 3.5b.2, finding LB-R1). The item tables of the game have such a cell as a flag, a
number (0 or 1), or a text ("" or "1"). This check gives the function build_catalog made-up tables of the types MasterItemDefinitions
and VariationData (the two house tables) and compares the result. It writes the catalog of the case "protections" to
FOLDER/item-catalog-protections.json: the test program of the backend loads that file (case RLB1).

  protections   a helm with Nonremovable (a flag), an ore with NotDroppable (the text "1"), and a resource with NotDroppable (the
                number 1): the columns of format 2 and the protection of each row
  control       a resource with no protection (the number 0, the text "", no cell) and a house item: no protection

The cases of the code review of stage 2.2 (finding WT-R3: the tool must not replace the catalog with items that lost their protection).
Each one runs the main function of the tool on an old file: a source that is not complete stops the tool (status 1), and the old
file stays.

  missing-nonremovable   a master table with no column Nonremovable: status 1, the old file stays
  missing-notdroppable   a master table (not a store table) with no column NotDroppable: status 1, the old file stays
  invalid-flag           a protection cell with the text "tru": status 1, the old file stays
  store-table            a store table (MasterItemDefinitions_MTX_*) with no column NotDroppable (the one exception of the game
                         data): status 0, the file has its items with Nonremovable and no NotDroppable

The cases of stage B0 (format 3, from stage B4 format 4, from stage U4 format 5: the column maxCraftUpgradeGS and the member artifactObjectives; case BP1 of the backend loads the
file of the case "upgrade"):

  upgrade               a made-up artifact bow with MaxCraftUpgradeGS 800, a ring with an empty cell, and a store table with no such
                        column: format 3, 800, 0, 0; the objective of the bow (an objective of the type Artifact, its equip task in a
                        sub task, its achievement index) is in artifactObjectives
  upgrade-invalid       a cell MaxCraftUpgradeGS with the text "high": status 1, the old file stays
  upgrade-missing       a master table (not a store table) with no column MaxCraftUpgradeGS: status 1, the old file stays
  objective-broken      an artifact objective whose task tree names a task that no task table has: status 1, the old file stays
  objective-bit-limit   the achievement index of the objective is the bit limit of the client: status 1, the old file stays (code
                        review of stage B0, finding B0-R4)
  objective-conflict    a second artifact (a made-up ring) with an objective of the same achievement and a different bit: status 1, the
                        old file stays (finding B0-R5)

The cases of stage U3 (the weight of the bag: the WeightOverride of the stats row of a weapon or an armor item; live check of the
inventory screen of the client):

  weight                a rapier (Weight 40, override 25), gloves (Weight 30, override 26.76: 26, cut), an axe (override 79.99999 of a
                        float32: 80), a ring with the override 0 (its Weight 1), and an ore with no stats row (Weight 7)
  weight-missing        a stats table with no column WeightOverride: status 1, the old file stays

The cases of stage B4 (format 4, from stage U4 format 5: the member trinkets, the stat lines of the jewelry; case BR1 of the backend loads the file of the case
"trinket"):

  trinket               an amulet (TrinketBonus 0.01, ScaleHealthPerGS 0.625), a ring (ScaleDamagePerGS 1.25, the cells of a float32),
                        an earring (ScaleCDRPerGS 0.625), and gloves with no TrinketBonus: three rows in trinkets, none for the gloves
  trinket-missing       an armor stats table with no column ScaleCDRPerGS: status 1, the old file stays
  trinket-invalid       a cell TrinketBonus with the text "high": status 1, the old file stays

The cases of the code review of stage B4 (finding B4-R4; case BZ4 of the backend runs them):

  trinket-missing-row   a jewelry item (the amulet) names an armor stats row that the source does not have: status 1, the old file stays
  trinket-missing-table the jewelry master rows and no table of the type ArmorItemDefinitions: status 1, the old file stays

The cases of stage U4 (format 5: the columns armorAppearanceM and armorAppearanceF, the clothing of the main menu; case ME8 of the
backend loads the file of the case "appearance"):

  appearance            a helm with the names of a male and a female row, a skin hat with no tag Armor (its names), a sword with empty
                        cells, and a house item: format 5, the two texts of each master row, and empty texts for the house item
  appearance-missing    a master table with no column ArmorAppearanceF: status 1, the old file stays
  appearance-invalid    a cell ArmorAppearanceM with a number: status 1, the old file stays

Usage: check_item_catalog_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import item_catalog as tool  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

# The text of an old catalog file (the cases of finding WT-R3: the tool must not replace it).
OLD_FILE = "old file\n"
SOURCE_CASES = ("missing-nonremovable", "missing-notdroppable", "invalid-flag", "store-table")
UPGRADE_CASES = ("upgrade-invalid", "upgrade-missing", "objective-broken", "objective-bit-limit", "objective-conflict")
WEIGHT_CASES = ("weight", "weight-missing")
TRINKET_CASES = ("trinket", "trinket-missing", "trinket-invalid", "trinket-missing-row", "trinket-missing-table")
APPEARANCE_CASES = ("appearance", "appearance-missing", "appearance-invalid")
# The made-up items of the cases of stage U4 (the ids of the backend case ME8) and their expected names [M, F].
APPEARANCES = {"ReviewHelm": ["m_Review_Helm", "f_Review_Helm"], "ReviewSkinHat": ["Skin_Review_Hat", "Skin_Review_Hat"],
               "ReviewSword": ["", ""], "House_ReviewTable": ["", ""]}
# The format of the file of the tool.
FORMAT = 5
# The made-up jewelry of the cases of stage B4 (the ids of the backend case BR1) and their expected rows of trinkets.
TRINKETS = [["ReviewAmulet", 0.01, 0.625, 0.0, 0.0], ["ReviewRing", 0.01, 0.0, 1.25, 0.0], ["ReviewEarring", 0.01, 0.0, 0.0, 0.625]]
# The made-up items of the cases of stage U3 and their expected weights (tenths).
WEIGHTS = {"ReviewRapier": 25, "ReviewGloves": 26, "ReviewAxe": 80, "ReviewRing": 1, "ReviewOre": 7}
# The made-up items and rows of the cases of stage B0 (the ids of the backend case BP1).
UPGRADE_BOW = "ReviewArtifactBow"
PLAIN_RING = "ReviewPlainRing"
STORE_HAT = "ReviewStoreHat"
OBJECTIVE = "Objective_ReviewArtifactBow"
OBJECTIVE_ACHIEVEMENT = "Achievement_Objective_ReviewArtifactBow"
OBJECTIVE_INDEX = 4321

# The made-up items of the check (the ids of the backend case RLB1).
PROTECTED_HELM = "ReviewProtectedHelm"
NO_DROP_ORE = "ReviewNoDropOre"
NO_DROP_HIDE = "ReviewNoDropHide"
FREE_ORE = "ReviewFreeOre"
FREE_HIDE = "ReviewFreeHide"
FREE_STONE = "ReviewFreeStone"
HOUSE_CHAIR = "House_ReviewChair"


def master_row(item_id: str, item_type: str, item_class: str, stack: int, **cells) -> dict:
    row = {"ItemID": item_id, "Name": f"@{item_id}", "ItemType": item_type, "ItemClass": item_class, "Tier": 1, "Weight": 1,
           "MaxStackSize": stack, "Durability": 1000 if stack == 1 else 0, "MinGearScore": 75 if stack == 1 else 0,
           "MaxGearScore": 75 if stack == 1 else 0, "GearScoreOverride": 0, "BindOnPickup": False, "MaxCraftUpgradeGS": "",
           "ArmorAppearanceM": "", "ArmorAppearanceF": ""}
    row.update(cells)
    return row


def catalog() -> dict:
    """@return The catalog of the made-up tables (the production function build_catalog)."""
    masters = FakeSheet("ReviewItems", [
        master_row(PROTECTED_HELM, "Armor", "EquippableHead+Armor+Light", 1, Nonremovable=True, NotDroppable=False),
        master_row(NO_DROP_ORE, "Resource", "Resource", 100, Nonremovable="", NotDroppable="1"),
        master_row(NO_DROP_HIDE, "Resource", "Resource", 100, Nonremovable=0, NotDroppable=1),
        master_row(FREE_ORE, "Resource", "Resource", 100, Nonremovable=0, NotDroppable=""),
        master_row(FREE_HIDE, "Resource", "Resource", 100, Nonremovable="", NotDroppable=False),
        master_row(FREE_STONE, "Resource", "Resource", 1)])
    sheets = {masters.name: masters}
    kinds = {masters.name: tool.MASTER_TYPE}
    for name in tool.HOUSE_TABLES:
        rows = [{"HouseItemID": "DEFAULTS"}]
        if name == tool.HOUSE_TABLES[0]:
            rows.append({"HouseItemID": HOUSE_CHAIR, "Name": "@chair", "ItemType": "HousingItem", "Tier": 1, "Weight": 10,
                         "MaxStackSize": 1, "BindOnPickup": False})
        sheets[name] = FakeSheet(name, rows)
        kinds[name] = tool.HOUSE_TYPE
    with patch.object(tool, "sheet_type", side_effect=lambda data: kinds[data.decode()]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()]):
        return tool.build_catalog([name.encode() for name in sheets])


def upgrade_sheets(case: str = "upgrade") -> tuple[dict, dict]:
    """@return The made-up tables of the cases of stage B0 (sheets by name, and their types)."""
    bow = master_row(UPGRADE_BOW, "Weapon", "Artifact+Weapon+EquippableTwoHand+Bow", 1, Nonremovable=False, NotDroppable=True,
                     MinGearScore=700, MaxGearScore=700, MaxCraftUpgradeGS=800)
    ring = master_row(PLAIN_RING, "Armor", "EquippableRing+Jewelry", 1, Nonremovable=False, NotDroppable=False, MinGearScore=725,
                      MaxGearScore=725, MaxCraftUpgradeGS="")
    if case == "upgrade-invalid":
        ring["MaxCraftUpgradeGS"] = "high"
    if case == "upgrade-missing":
        del bow["MaxCraftUpgradeGS"]
        del ring["MaxCraftUpgradeGS"]
    hat = master_row(STORE_HAT, "Armor", "EquippableHead+Armor+Light", 1, Nonremovable=False)
    del hat["MaxCraftUpgradeGS"]  # The store tables of the game have no such column.
    tasks = [{"TaskID": "Task_Container_Review", "Type": "SimpleTaskContainer", "SubTask1": "Task_Equip_Review",
              "SubTask2": "Task_Kill_Review"},
             {"TaskID": "Task_Equip_Review", "Type": "TaskEquipItem", "ItemName": UPGRADE_BOW},
             {"TaskID": "Task_Kill_Review", "Type": "TaskKillContribution", "TargetQty": 5}]
    if case == "objective-broken":
        tasks[0]["SubTask2"] = "Task_Not_There"
    objectives = [{"ObjectiveID": OBJECTIVE, "Type": "Artifact", "Task": "Task_Container_Review", "AchievementId": OBJECTIVE_ACHIEVEMENT},
                  {"ObjectiveID": "Objective_ReviewMission", "Type": "Mission", "Task": "Task_Kill_Review", "AchievementId": ""}]
    achievements = [{"AchievementID": OBJECTIVE_ACHIEVEMENT, "AchievementIndex": OBJECTIVE_INDEX}]
    if case == "objective-bit-limit":
        achievements[0]["AchievementIndex"] = tool.ACHIEVEMENT_BIT_LIMIT
    if case == "objective-conflict":
        ring["ItemClass"] = "Artifact+EquippableRing+Jewelry"
        tasks.append({"TaskID": "Task_Equip_Ring", "Type": "TaskEquipItem", "ItemName": PLAIN_RING})
        objectives.append({"ObjectiveID": "Objective_ReviewRing", "Type": "Artifact", "Task": "Task_Equip_Ring",
                           "AchievementId": "Achievement_Objective_ReviewRing"})
        achievements.append({"AchievementID": "Achievement_Objective_ReviewRing", "AchievementIndex": OBJECTIVE_INDEX})
    sheets = {"ReviewItems": FakeSheet("ReviewItems", [bow, ring]),
              "MasterItemDefinitions_MTX_Review": FakeSheet("MasterItemDefinitions_MTX_Review", [hat]),
              "ReviewObjectives": FakeSheet("ReviewObjectives", objectives),
              "ReviewTasks": FakeSheet("ReviewTasks", tasks),
              "ReviewAchievements": FakeSheet("ReviewAchievements", achievements)}
    kinds = {"ReviewItems": tool.MASTER_TYPE, "MasterItemDefinitions_MTX_Review": tool.MASTER_TYPE, "ReviewObjectives": tool.OBJECTIVES_TYPE,
             "ReviewTasks": tool.TASKS_TYPE, "ReviewAchievements": tool.ACHIEVEMENT_TYPE}
    for name in tool.HOUSE_TABLES:
        sheets[name] = FakeSheet(name, [{"HouseItemID": "DEFAULTS"}])
        kinds[name] = tool.HOUSE_TYPE
    return sheets, kinds


def run_sheets(target: Path, sheets: dict, kinds: dict) -> int:
    """Runs the main function of the tool with made-up tables into `target`. @return Its status."""
    with patch.object(tool, "table_entries", return_value=[None]), \
            patch.object(tool, "tables_of_paks", return_value=([name.encode() for name in sheets], "made-up tables")), \
            patch.object(tool, "sheet_type", side_effect=lambda data: kinds[data.decode()]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()]), \
            patch.object(sys, "argv", ["item_catalog.py", str(target)]), \
            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return tool.main()


def upgrade_case(folder: Path, case: str) -> tuple[bool, str]:
    """A case of stage B0. @return Passed, and the detail."""
    target = folder / f"item-catalog-{case}.json"
    target.write_text(OLD_FILE, encoding="utf-8")
    status = run_sheets(target, *upgrade_sheets(case))
    kept = target.read_text(encoding="utf-8") == OLD_FILE
    if case != "upgrade":
        return status == 1 and kept, f"status {status}, the old file {'stays' if kept else 'was replaced'}"
    if kept:
        return False, f"status {status}, no file"
    document = json.loads(target.read_text(encoding="utf-8"))
    column = document["columns"].index("maxCraftUpgradeGS") if "maxCraftUpgradeGS" in document["columns"] else -1
    upgrades = {row[0]: row[column] for row in document["items"]} if column >= 0 else {}
    objectives = document.get("artifactObjectives")
    # The loader of the backend reads the column at the place 14 (format 3 to 5).
    passed = (status == 0 and document.get("format") == FORMAT and column == 14
              and all(len(row) == len(document["columns"]) for row in document["items"])
              and upgrades.get(UPGRADE_BOW) == 800 and upgrades.get(PLAIN_RING) == 0 and upgrades.get(STORE_HAT) == 0
              and objectives == [[UPGRADE_BOW, OBJECTIVE, OBJECTIVE_ACHIEVEMENT, OBJECTIVE_INDEX]])
    return passed, f"status {status}, format {document.get('format')}, upgrades {upgrades}, objectives {objectives}; the file {target.name}"


def weight_case(folder: Path, case: str) -> tuple[bool, str]:
    """A case of stage U3. @return Passed, and the detail."""
    rows = [master_row("ReviewRapier", "Weapon", "EquippableMainHand+Weapon+Rapier", 1, Weight=40, ItemStatsRef="ReviewRapierStats"),
            master_row("ReviewGloves", "Armor", "EquippableHands+Armor+Medium", 1, Weight=30, ItemStatsRef="reviewglovesstats"),
            master_row("ReviewAxe", "Weapon", "EquippableTwoHand+Weapon+GreatAxe", 1, Weight=80, ItemStatsRef="ReviewAxeStats"),
            master_row("ReviewRing", "Armor", "EquippableRing+Jewelry", 1, Weight=1, ItemStatsRef="ReviewRingStats"),
            master_row("ReviewOre", "Resource", "Resource", 100, Weight=7, ItemStatsRef="")]
    for row in rows:
        row.update(Nonremovable=False, NotDroppable=False)
    weapons = [{"WeaponID": "ReviewRapierStats", "WeightOverride": 25.0}, {"WeaponID": "ReviewAxeStats", "WeightOverride": 79.99999}]
    armor = [{"WeaponID": "ReviewGlovesStats", "WeightOverride": 26.758445, "TrinketBonus": "", "ScaleHealthPerGS": "",
              "ScaleDamagePerGS": "", "ScaleCDRPerGS": ""},
             {"WeaponID": "ReviewRingStats", "WeightOverride": 0.0, "TrinketBonus": 0.0, "ScaleHealthPerGS": 0.0, "ScaleDamagePerGS": 0.0,
              "ScaleCDRPerGS": 0.0}]
    if case == "weight-missing":
        armor = [{"WeaponID": row["WeaponID"]} for row in armor]
    sheets = {"ReviewItems": FakeSheet("ReviewItems", rows), "ReviewWeapons": FakeSheet("ReviewWeapons", weapons),
              "ReviewArmor": FakeSheet("ReviewArmor", armor)}
    kinds = {"ReviewItems": tool.MASTER_TYPE, "ReviewWeapons": "WeaponItemDefinitions", "ReviewArmor": "ArmorItemDefinitions"}
    for name in tool.HOUSE_TABLES:
        sheets[name] = FakeSheet(name, [{"HouseItemID": "DEFAULTS"}])
        kinds[name] = tool.HOUSE_TYPE
    target = folder / f"item-catalog-{case}.json"
    target.write_text(OLD_FILE, encoding="utf-8")
    status = run_sheets(target, sheets, kinds)
    kept = target.read_text(encoding="utf-8") == OLD_FILE
    if case != "weight":
        return status == 1 and kept, f"status {status}, the old file {'stays' if kept else 'was replaced'}"
    if kept:
        return False, f"status {status}, no file"
    document = json.loads(target.read_text(encoding="utf-8"))
    column = document["columns"].index("weight")
    weights = {row[0]: row[column] for row in document["items"]}
    return status == 0 and weights == WEIGHTS, f"status {status}, the weights {weights}"


def trinket_case(folder: Path, case: str) -> tuple[bool, str]:
    """A case of stage B4. @return Passed, and the detail."""
    rows = [master_row("ReviewAmulet", "Armor", "EquippableAmulet+Jewelry", 1, ItemStatsRef="ReviewAmuletStats"),
            master_row("ReviewRing", "Armor", "EquippableRing+Jewelry", 1, ItemStatsRef="reviewringstats"),
            master_row("ReviewEarring", "Armor", "EquippableToken+Jewelry", 1, ItemStatsRef="ReviewEarringStats"),
            master_row("ReviewGloves", "Armor", "EquippableHands+Armor+Medium", 1, ItemStatsRef="ReviewGlovesStats")]
    for row in rows:
        row.update(Nonremovable=False, NotDroppable=False)
    # The float32 values of the game cells (0.01 is 0.0099999998).
    armor = [{"WeaponID": "ReviewAmuletStats", "WeightOverride": 0.0, "TrinketBonus": 0.0099999998, "ScaleHealthPerGS": 0.625,
              "ScaleDamagePerGS": "", "ScaleCDRPerGS": ""},
             {"WeaponID": "ReviewRingStats", "WeightOverride": 0.0, "TrinketBonus": 0.0099999998, "ScaleHealthPerGS": 0.0,
              "ScaleDamagePerGS": 1.25, "ScaleCDRPerGS": 0.0},
             {"WeaponID": "ReviewEarringStats", "WeightOverride": 0.0, "TrinketBonus": 0.0099999998, "ScaleHealthPerGS": "",
              "ScaleDamagePerGS": "", "ScaleCDRPerGS": 0.625},
             {"WeaponID": "ReviewGlovesStats", "WeightOverride": 26.758445, "TrinketBonus": "", "ScaleHealthPerGS": 0.5,
              "ScaleDamagePerGS": "", "ScaleCDRPerGS": ""}]
    if case == "trinket-missing":
        for row in armor:
            del row["ScaleCDRPerGS"]
    if case == "trinket-invalid":
        armor[0]["TrinketBonus"] = "high"
    if case == "trinket-missing-row":
        rows[0]["ItemStatsRef"] = "MissingAmuletStats"
    sheets = {"ReviewItems": FakeSheet("ReviewItems", rows), "ReviewArmor": FakeSheet("ReviewArmor", armor)}
    kinds = {"ReviewItems": tool.MASTER_TYPE, "ReviewArmor": "ArmorItemDefinitions"}
    if case == "trinket-missing-table":
        del sheets["ReviewArmor"]
        del kinds["ReviewArmor"]
    for name in tool.HOUSE_TABLES:
        sheets[name] = FakeSheet(name, [{"HouseItemID": "DEFAULTS"}])
        kinds[name] = tool.HOUSE_TYPE
    target = folder / f"item-catalog-{case}.json"
    target.write_text(OLD_FILE, encoding="utf-8")
    status = run_sheets(target, sheets, kinds)
    kept = target.read_text(encoding="utf-8") == OLD_FILE
    if case != "trinket":
        return status == 1 and kept, f"status {status}, the old file {'stays' if kept else 'was replaced'}"
    if kept:
        return False, f"status {status}, no file"
    document = json.loads(target.read_text(encoding="utf-8"))
    trinkets = document.get("trinkets")
    return (status == 0 and document.get("format") == FORMAT and trinkets == TRINKETS,
            f"status {status}, format {document.get('format')}, trinkets {trinkets}; the file {target.name}")


def appearance_case(folder: Path, case: str) -> tuple[bool, str]:
    """A case of stage U4. @return Passed, and the detail."""
    rows = [master_row("ReviewHelm", "Armor", "EquippableHead+Armor+Light", 1, ArmorAppearanceM="m_Review_Helm",
                       ArmorAppearanceF="f_Review_Helm"),
            master_row("ReviewSkinHat", "Armor", "EquippableHead", 1, ArmorAppearanceM="Skin_Review_Hat", ArmorAppearanceF="Skin_Review_Hat"),
            master_row("ReviewSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 1)]
    for row in rows:
        row.update(Nonremovable=False, NotDroppable=False)
    if case == "appearance-missing":
        for row in rows:
            del row["ArmorAppearanceF"]
    if case == "appearance-invalid":
        rows[0]["ArmorAppearanceM"] = 7
    sheets = {"ReviewItems": FakeSheet("ReviewItems", rows)}
    kinds = {"ReviewItems": tool.MASTER_TYPE}
    for name in tool.HOUSE_TABLES:
        house = [{"HouseItemID": "DEFAULTS"}]
        if name == tool.HOUSE_TABLES[0]:
            house.append({"HouseItemID": "House_ReviewTable", "Name": "@table", "ItemType": "HousingItem", "Tier": 1, "Weight": 10,
                          "MaxStackSize": 1, "BindOnPickup": False})
        sheets[name] = FakeSheet(name, house)
        kinds[name] = tool.HOUSE_TYPE
    target = folder / f"item-catalog-{case}.json"
    target.write_text(OLD_FILE, encoding="utf-8")
    status = run_sheets(target, sheets, kinds)
    kept = target.read_text(encoding="utf-8") == OLD_FILE
    if case != "appearance":
        return status == 1 and kept, f"status {status}, the old file {'stays' if kept else 'was replaced'}"
    if kept:
        return False, f"status {status}, no file"
    document = json.loads(target.read_text(encoding="utf-8"))
    columns = document["columns"]
    names = {row[0]: row[-2:] for row in document["items"]}
    passed = (status == 0 and document.get("format") == FORMAT and columns[-2:] == ["armorAppearanceM", "armorAppearanceF"]
              and all(len(row) == len(columns) for row in document["items"]) and names == APPEARANCES)
    return passed, f"status {status}, format {document.get('format')}, names {names}; the file {target.name}"


def run_tool(target: Path, rows: list[dict], table: str = "ReviewItems") -> int:
    """Runs the main function of the tool with one made-up master table (and the two house tables) into `target`. @return Its status."""
    sheets = {table: FakeSheet(table, rows)}
    kinds = {table: tool.MASTER_TYPE}
    for name in tool.HOUSE_TABLES:
        sheets[name] = FakeSheet(name, [{"HouseItemID": "DEFAULTS"}])
        kinds[name] = tool.HOUSE_TYPE
    with patch.object(tool, "table_entries", return_value=[None]), \
            patch.object(tool, "tables_of_paks", return_value=([name.encode() for name in sheets], "made-up tables")), \
            patch.object(tool, "sheet_type", side_effect=lambda data: kinds[data.decode()]), \
            patch.object(tool, "Sheet", side_effect=lambda data: sheets[data.decode()]), \
            patch.object(sys, "argv", ["item_catalog.py", str(target)]), \
            contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return tool.main()


def source_case(folder: Path, case: str) -> tuple[bool, str]:
    """A case of finding WT-R3: the tool on an old file. @return Passed, and the detail."""
    row = master_row(PROTECTED_HELM, "Armor", "EquippableHead+Armor+Light", 1, Nonremovable=True, NotDroppable=False)
    table = "ReviewItems"
    if case == "missing-nonremovable":
        del row["Nonremovable"]
    elif case == "missing-notdroppable":
        del row["NotDroppable"]
    elif case == "invalid-flag":
        row["Nonremovable"] = "tru"
    elif case == "store-table":
        del row["NotDroppable"]
        table = "MasterItemDefinitions_MTX_Review"
    target = folder / f"item-catalog-{case}.json"
    target.write_text(OLD_FILE, encoding="utf-8")
    status = run_tool(target, [row], table)
    kept = target.read_text(encoding="utf-8") == OLD_FILE
    flags = None if kept else protections(json.loads(target.read_text(encoding="utf-8"))).get(PROTECTED_HELM)
    if case == "store-table":
        return status == 0 and flags == (True, False), f"status {status}, the protections of the helm {flags}"
    return status == 1 and kept, f"status {status}, the old file {'stays' if kept else 'was replaced'}"


def protections(document: dict) -> dict:
    """@return Item id to (nonRemovable, notDroppable) of the document, or an empty map for a document with no such columns."""
    columns = document.get("columns", [])
    if "nonRemovable" not in columns or "notDroppable" not in columns:
        return {}
    first, second = columns.index("nonRemovable"), columns.index("notDroppable")
    return {row[0]: (row[first], row[second]) for row in document["items"]}


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__.strip())
        return 2
    folder = Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    cases = sys.argv[2:] or ["protections", "control", *SOURCE_CASES, "upgrade", *UPGRADE_CASES, *WEIGHT_CASES, *TRINKET_CASES,
                             *APPEARANCE_CASES]
    document = catalog()
    found = protections(document)
    failures = 0
    for case in cases:
        if case == "protections":
            target = folder / "item-catalog-protections.json"
            target.write_text(json.dumps(document) + "\n", encoding="utf-8")
            passed = (document.get("format") == FORMAT and document.get("columns", [])[12:14] == ["nonRemovable", "notDroppable"]
                      and all(len(row) == len(document["columns"]) for row in document["items"])
                      and found.get(PROTECTED_HELM) == (True, False) and found.get(NO_DROP_ORE) == (False, True)
                      and found.get(NO_DROP_HIDE) == (False, True))
            detail = (f"format {document.get('format')}, the helm {found.get(PROTECTED_HELM)}, the ore {found.get(NO_DROP_ORE)}, "
                      f"the hide {found.get(NO_DROP_HIDE)}; the file {target.name}")
        elif case == "control":
            passed = (found.get(FREE_ORE) == (False, False) and found.get(FREE_HIDE) == (False, False)
                      and found.get(FREE_STONE) == (False, False) and found.get(HOUSE_CHAIR) == (False, False))
            detail = f"the free ore {found.get(FREE_ORE)}, the stone {found.get(FREE_STONE)}, the house item {found.get(HOUSE_CHAIR)}"
        elif case in SOURCE_CASES:
            passed, detail = source_case(folder, case)
        elif case == "upgrade" or case in UPGRADE_CASES:
            passed, detail = upgrade_case(folder, case)
        elif case in WEIGHT_CASES:
            passed, detail = weight_case(folder, case)
        elif case in TRINKET_CASES:
            passed, detail = trinket_case(folder, case)
        elif case in APPEARANCE_CASES:
            passed, detail = appearance_case(folder, case)
        else:
            passed, detail = False, "not a case of this check"
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {detail}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
