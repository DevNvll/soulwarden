#!/usr/bin/env python3
"""Check of the crafting data tool (crafting.py) with made-up sources (no file of the game). Stages 3.7a to 3.7c.

The tool stops with status 1, and writes no file, at a source that it cannot use (its docstring has the list). This check gives the
functions of the tool made-up tables, station rows, and station slices, and compares each result. The test program of the backend runs it
(case CRAFT-TOOL; case CRAFT1 to CRAFT-D1 have the backend side of the file).

  recipe          a recipe row: its values, the station keys in lower case, the typed ingredients, the loader defaults of the client
                  (a blank push flag is true; a MaxPerkItemsAllowed of 0 is 1 for a procedural recipe), a blank output quantity (null)
  causes          the causes of a recipe that the backend does not craft: a currency with no cap of its balance, an unlock, an output
                  that is not an item, an output with a gear score range, no override, and no BaseGearScore, an ingredient that is not an
                  item, a category with no item, a station key that no row permits, a 0 quantity, more perk items than the 4 controls of the
                  client; stage 3.7b: an event with no row, with character experience, or of a progression with no rank table, a
                  first-craft achievement with no row, a cooldown with only a time, refining with no rank table of its trade skill
  stops           an ingredient type "Category_Any2", a quantity of 50,001, a level of 1,001, a flag "yes", a table with no column, two rows
                  of one recipe id, no station row, a station row with no type: each one stops the tool
  stations        the station rows (name, types in lower case, discount, mannequin) and the authored keys with no row ("missingStations")
  build           the whole build with the made-up sources: the supported recipes, the categories (members in the byte order of the ids),
                  the events of the recipes, a resource with a bucket that the bucket table does not have (a counted diagnostic)
  write           a run that stops leaves the earlier file; a run that completes replaces it (one file, through a temporary file)
  refining        stage 3.7b: refining, craft-all, an event, a first-craft achievement, and a cooldown with their data give no cause; the
                  rank tables (the RollBonus of each rank, a table with no RollBonus column is 0), the first-craft bits, the bonus values
                  of the items (a table with no bonus column is 0), the tier lists of a recipe; a rank table with a gap stops the tool
  procedural      stage 3.7c: a procedural recipe with a BaseGearScore, perk items, a PerkCost, and an Azoth ingredient gives no cause; the
                  MaxCraftGS of its output, the largest gear score bonus of each rank for the tiers (a table with no such column: 0), the
                  gear score values of an ingredient (a text that is not a number: 0 and counted), the cap of a currency (the
                  MaximumInfluence of the rank 0 of its rank table), the list Crafting of the perk generation data and the three chance
                  modifiers; a base attributes file with no list Crafting stops the tool
  incomplete      code review of stage 3.7a, finding CR7A-R3: an ingredient entry with the type Item, the quantity 1, and no id stops the
                  tool (also Category_Only and Currency), and a run of the tool leaves the earlier file; an entry with an id and no type is
                  not an ingredient (the client skips it), and the recipe keeps its other ingredients

Usage: check_crafting_tool.py FOLDER [CASE ...]
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import contextlib
import io
import json
import struct
import sys
import uuid
import zlib
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import crafting as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402
from slice_ids import Element  # noqa: E402

CASES = ("recipe", "causes", "stops", "stations", "build", "write", "incomplete", "refining", "procedural")
MARK = "earlier file\n"
FAILED: list[str] = []


def report(ok: bool, text: str) -> None:
    print(("ok   " if ok else "FAIL ") + text)
    if not ok:
        FAILED.append(text)


def stops(action) -> str | None:
    try:
        action()
    except Failure as error:
        return str(error)
    return None


def recipe_row(**cells) -> dict:
    """@return A recipe row with each column of the tool (RECIPE_COLUMNS): the fixed knife of Flint and Wood."""
    row = {column: "" for column in tool.RECIPE_COLUMNS}
    row.update({"RecipeID": "TestKnifeT1", "CraftingCategory": "Tools", "CraftingGroup": "SurvivalTools", "bKnownByDefault": "1",
                "bListedByDefault": "1", "CraftAll": "0", "IsRefining": "0", "IsProcedural": "0", "IsTemporary": "0", "CraftingFee": "0",
                "UseCraftingTax": "1", "Tradeskill": "Engineering", "RecipeLevel": "0", "StationType1": "Engineering2", "StationType2": "camp1",
                "OutputQty": "1", "ItemID": "TestKnifeT1", "Ingredient1": "TestFlintT1", "Type1": "Item", "Qty1": "1",
                "Ingredient2": "TestWoodT1", "Type2": "Item", "Qty2": "1"})
    row.update(cells)
    return row


def element(name: int | None = None, value: bytes | None = None, children: list[Element] | None = None, type_id: uuid.UUID | None = None) -> Element:
    item = Element()
    item.name = name
    item.value = value
    item.children = children or []
    if type_id is not None:
        item.type_id = type_id
    return item


def station_element(name: str, types: list[str], discount: float = 0.0, mannequin: str = "") -> Element:
    return element(children=[element(tool.NAME_FIELD, name.encode()),
                             element(tool.TYPES_FIELD, None, [element(None, key.encode()) for key in types]),
                             element(tool.MANNEQUIN_FIELD, mannequin.encode()),
                             element(tool.DISCOUNT_FIELD, struct.pack(">f", discount))])


def assembly_element(key: str) -> Element:
    return element(children=[element(None, None, [element(tool.STATION_ENTRY, key.encode())], tool.ASSEMBLY_CLASS)])


class FakeSources:
    """The interface of creature_data.Sources that the tool uses: table paths, station streams, slice streams."""

    def __init__(self, sheets: dict[str, tuple[str, FakeSheet]], stations: list[Element] | None, slices: dict[str, str]) -> None:
        self.sheets = sheets
        self.stations = stations
        self.slices = slices
        self.generation = made_up_generation()

    def paths(self, folder: str, suffix: str) -> list[str]:
        if folder == tool.STATION_SLICES:
            return [f"{folder}{name}{suffix}" for name in self.slices]
        return [f"{folder}{name}{suffix}" for name in self.sheets]

    def read(self, path: str) -> bytes:
        return Path(path).stem.encode()

    def stream(self, path: str):
        if path == tool.CRAFTING_STATIONS:
            return self.stations
        if path == tool.BASE_ATTRIBUTES:
            return self.generation
        return [assembly_element(self.slices[Path(path).stem])]

    def check_complete(self) -> None:
        return None


def number(value: int) -> bytes:
    return value.to_bytes(4, "big")


def made_up_generation(crafting: bool = True) -> list[Element]:
    """@return The base attributes file: the perk generation data with the tiers 0 to 5 (the lists General and Crafting; Crafting of tier 5:
    4 perks at 470 to 599, 5 at 600 to 625) and the three chance modifiers (1.0, 0.5, 2.0)."""
    def counts(entries):
        return element(None, None, [element(None, None, [element(tool.field_id("value1"), number(count)),
                                                         element(tool.field_id("value2"), None, [
                                                             element(None, None, [element(tool.field_id("value1"), number(low)),
                                                                                  element(tool.field_id("value2"), number(high))])
                                                             for low, high in ranges])])
                                    for count, ranges in entries])
    tiers = []
    for tier in range(6):
        general = [] if tier < 2 else [(1, [(0, 300)]), (2, [(301, 700)])]
        craft = [] if tier < 2 else ([(4, [(470, 599)]), (5, [(600, 625)])] if tier == 5 else [(1, [(0, 199)]), (2, [(200, 700)])])
        members = [element(tool.field_id("Max Perk Channel"), number(min(tier, 4))), counts(general)]
        if crafting:
            members.append(counts(craft))
        members.append(element(None, None, [element(None, b"x")]))
        tiers.append(element(None, None, members))
    generation = element(tool.field_id("Perk Generation Data"), None, [element(None, None, tiers)])
    modifiers = [element(tool.field_id(name), struct.pack(">f", value))
                 for name, value in (("Perk Chance Modifier", 1.0), ("Attribute Chance Modifier", 0.5), ("Gem Slot Chance Modifier", 2.0))]
    return [element(None, None, [generation, *modifiers])]


def made_up_sheets(**changes) -> dict[str, tuple[str, FakeSheet]]:
    sheets = {
        "Recipes": (tool.RECIPE_TYPE, FakeSheet("Recipes", [
            recipe_row(), recipe_row(RecipeID="TestBar", ItemID="TestBar", OutputQty="2", StationType1="blacksmith2", StationType2="",
                                     Ingredient1="TestMetal", Type1="Category_Only", Qty1="2"),
            recipe_row(RecipeID="TestIngot", IsRefining="1", CraftAll="1", GameEventID="Craft_Test", ItemID="TestIngotA", Tradeskill="Smelting",
                       FirstCraftAchievementId="FC_TestIngot", CooldownSeconds="86400", CooldownQuantity="10",
                       BonusItemChanceIncrease="0.25,0.5,0.75,1", BonusItemChanceDecrease="-0.05,-0.1")])),
        "Items": (tool.MASTER_TYPE, FakeSheet("Items", [
            {"ItemID": "TestKnifeT1", "IngredientCategories": "", "Tier": "1", "GearScoreOverride": "100", "MinGearScore": "100",
             "MaxGearScore": "100"},
            {"ItemID": "TestFlintT1", "IngredientCategories": "", "Tier": "1", "GearScoreOverride": "", "MinGearScore": "", "MaxGearScore": "",
             "ExtraBonusItemChance": "", "IngredientBonusPrimary": "10", "IngredientBonusSecondary": "5"},
            {"ItemID": "TestWoodT1", "IngredientCategories": "Wood", "Tier": "1", "GearScoreOverride": "", "MinGearScore": "", "MaxGearScore": ""},
            {"ItemID": "TestIngotB", "IngredientCategories": "TestMetal,Metal", "Tier": "2", "GearScoreOverride": "", "MinGearScore": "",
             "MaxGearScore": ""},
            {"ItemID": "TestIngotA", "IngredientCategories": "TestMetal", "Tier": "2", "GearScoreOverride": "", "MinGearScore": "",
             "MaxGearScore": ""},
            {"ItemID": "TestBar", "IngredientCategories": "", "Tier": "2", "GearScoreOverride": "", "MinGearScore": "", "MaxGearScore": ""},
            {"ItemID": "TestSword", "IngredientCategories": "", "Tier": "2", "GearScoreOverride": "", "MinGearScore": "200",
             "MaxGearScore": "300"}])),
        "HouseItems": (tool.HOUSE_TYPE, FakeSheet("HouseItems", [{"HouseItemID": "TestChair"}])),
        "Events": (tool.EVENT_TYPE, FakeSheet("Events", [
            {"EventID": "Craft_Test", "CategoricalProgressionId": "Smelting", "CategoricalProgressionReward": "11", "UniversalExpAmount": "",
             "TerritoryStanding": ""},
            {"EventID": "Other_Event", "CategoricalProgressionId": "", "CategoricalProgressionReward": "", "UniversalExpAmount": "",
             "TerritoryStanding": "N/A"},
            {"EventID": "Xp_Event", "CategoricalProgressionId": "Smelting", "CategoricalProgressionReward": "11", "UniversalExpAmount": "50",
             "TerritoryStanding": ""},
            {"EventID": "Music_Event", "CategoricalProgressionId": "Musician", "CategoricalProgressionReward": "5", "UniversalExpAmount": "",
             "TerritoryStanding": ""},
            {"EventID": "Standing_Event", "CategoricalProgressionId": "Smelting", "CategoricalProgressionReward": "11", "UniversalExpAmount": "",
             "TerritoryStanding": "2"}])),
        "Resources": (tool.RESOURCE_TYPE, FakeSheet("Resources", [
            {"ResourceID": "TestCharm", "PerkBucket": "Bucket_Test", "GemPerk": "", "SocketType": ""},
            {"ResourceID": "TestBadCharm", "PerkBucket": "Bucket_Missing", "GemPerk": "", "SocketType": ""}])),
        "Buckets": (tool.BUCKET_TYPE, FakeSheet("Buckets", [{"PerkBucketID": "Bucket_Test"}])),
        "Smelting": (tool.RANK_TYPE, FakeSheet("Smelting", [{"Level": str(level), "RollBonus": str(0 if level == 0 else 500 + 10 * level)}
                                                           for level in range(4)])),
        "Riding": (tool.RANK_TYPE, FakeSheet("Riding", [{"Level": "0"}, {"Level": "1"}])),
        "Achievements": (tool.ACHIEVEMENT_TYPE, FakeSheet("Achievements", [{"AchievementID": "FC_TestIngot", "AchievementIndex": "5330"},
                                                                            {"AchievementID": "FC_Other", "AchievementIndex": "77"}])),
        # Stage 3.7c: the currencies (Azoth_Currency with its rank table; Other_Currency with no rank table) and a rank table with the gear
        # score columns.
        "CategoricalProgression": (tool.CURRENCY_TYPE, FakeSheet("CategoricalProgression", [
            {"CategoricalProgressionId": "Azoth_Currency", "RankTableId": "AzothCurrency"},
            {"CategoricalProgressionId": "Other_Currency", "RankTableId": "NoSuchTable"}])),
        "AzothCurrency": (tool.CURRENCY_RANK_TYPE, FakeSheet("AzothCurrency", [{"Rank": "0", "MaximumInfluence": "2500"}])),
        "Armoring": (tool.RANK_TYPE, FakeSheet("Armoring", [
            {"Level": str(level), "RollBonus": "0", "MaxGearScoreBonus": "1", "MaxGearScoreBonusT2": str(5 + level),
             "MaxGearScoreBonusT3": "0", "MaxGearScoreBonusT4": "0", "MaxGearScoreBonusT5": str(level)} for level in range(3)])),
    }
    for name, value in changes.items():
        if value is None:
            sheets.pop(name, None)
        else:
            sheets[name] = value
    return sheets


def made_up_stations() -> list[Element]:
    return [element(children=[station_element("Engineering2", ["engineering1", "Engineering2"], 0.0, "Hand_Craft"),
                              station_element("Blacksmith5", ["blacksmith1", "blacksmith2", "blacksmith3"], 0.45, "Blacksmithing_Craft"),
                              station_element("Camp1", ["camp1"])])]


def build(sheets=None, stations=None, slices=None, generation=None) -> dict:
    sources = FakeSources(sheets if sheets is not None else made_up_sheets(), stations if stations is not None else made_up_stations(),
                          slices if slices is not None else {"station_engineering_t2": "Engineering2", "station_carpentry_master": "Carpentry2"})
    sources.generation = generation if generation is not None else made_up_generation()
    sheet_of = {name.encode(): sheet for name, (_, sheet) in sources.sheets.items()}
    type_of = {name.encode(): kind for name, (kind, _) in sources.sheets.items()}
    with patch.object(tool, "sheet_type", lambda data: type_of.get(data, "")), patch.object(tool, "Sheet", lambda data: sheet_of[data]):
        return tool.build(sources)


def recipe_of(document: dict, identifier: str) -> dict:
    return next(entry for entry in document["recipes"] if entry["id"] == identifier)


def case_recipe() -> None:
    document = build()
    knife = recipe_of(document, "TestKnifeT1")
    report(knife["crc"] == zlib.crc32(b"testknifet1") and knife["stations"] == ["engineering2", "camp1"]
           and knife["ingredients"] == [["Item", "TestFlintT1", 1], ["Item", "TestWoodT1", 1]] and knife["item"] == "TestKnifeT1"
           and knife["outputQty"] == 1 and knife["push"] is True and knife["perkItems"] == 0 and knife["fee"] == 0 and knife["tax"] is True
           and knife["unsupported"] == [], "recipe: the knife (the CRC-32 of the lower-case id, keys in lower case, typed ingredients, no cause)")
    tables = FakeSheet("Recipes", [recipe_row(IsProcedural="1", MaxPerkItemsAllowed="0", PerkItemsBucketPush="", OutputQty="")])
    sheets = made_up_sheets(Recipes=(tool.RECIPE_TYPE, tables))
    entry = recipe_of(build(sheets), "TestKnifeT1")
    report(entry["perkItems"] == 1 and entry["push"] is True and entry["outputQty"] is None and tool.CAUSE_NO_QUANTITY in entry["unsupported"],
           "recipe: the loader defaults of the client (procedural: MaxPerkItemsAllowed 0 -> 1, a blank push is true) and a blank OutputQty")


def case_causes() -> None:
    cases = [({"IsRefining": "1"}, tool.CAUSE_NO_RANKS),
             ({"GameEventID": "NoSuchEvent"}, tool.CAUSE_EVENT_ROW), ({"GameEventID": "Xp_Event"}, tool.CAUSE_EVENT_XP),
             ({"GameEventID": "Music_Event"}, tool.CAUSE_EVENT_SKILL), ({"FirstCraftAchievementId": "FC_Missing"}, tool.CAUSE_FIRST_CRAFT_ROW),
             ({"CooldownSeconds": "86400"}, tool.CAUSE_COOLDOWN_PART), ({"CooldownQuantity": "10"}, tool.CAUSE_COOLDOWN_PART),
             ({"Ingredient3": "Other_Currency", "Type3": "Currency", "Qty3": "5"}, tool.CAUSE_CURRENCY),
             ({"bKnownByDefault": "0"}, tool.CAUSE_UNLOCK), ({"RequiredAchievementID": "Test_Unlock"}, tool.CAUSE_UNLOCK),
             ({"ItemID": "NoSuchItem"}, tool.CAUSE_NOT_ITEM), ({"ItemID": "TestSword"}, tool.CAUSE_RANGE),
             ({"Ingredient1": "NoSuchFlint"}, tool.CAUSE_INGREDIENT), ({"Ingredient1": "NoMetal", "Type1": "Category_Only"}, tool.CAUSE_CATEGORY),
             ({"StationType1": "engineering9", "StationType2": ""}, tool.CAUSE_NO_STATION), ({"Qty2": ""}, tool.CAUSE_ZERO),
             ({"MaxPerkItemsAllowed": "4", "AttributeOnlyPerkItemSlotCount": "1"}, tool.CAUSE_PERKS),
             ({"SkipGrantItems": "1"}, tool.CAUSE_SKIP_GRANT)]
    for cells, cause in cases:
        sheets = made_up_sheets(Recipes=(tool.RECIPE_TYPE, FakeSheet("Recipes", [recipe_row(**cells)])))
        entry = recipe_of(build(sheets), "TestKnifeT1")
        report(cause in entry["unsupported"], f"causes: {cells} -> {cause}")
    house = recipe_of(build(made_up_sheets(Recipes=(tool.RECIPE_TYPE, FakeSheet("Recipes", [recipe_row(ItemID="TestChair")])))), "TestKnifeT1")
    report(house["unsupported"] == [], "causes: an output of the house item tables is an item (no cause)")


def case_stops() -> None:
    def with_recipes(*rows):
        return made_up_sheets(Recipes=(tool.RECIPE_TYPE, FakeSheet("Recipes", list(rows))))
    missing = recipe_row()
    del missing["PerkCost"]
    cases = [("an ingredient type Category_Any2", lambda: build(with_recipes(recipe_row(Type1="Category_Any2")))),
             ("a quantity of 50,001", lambda: build(with_recipes(recipe_row(Qty1="50001")))),
             ("a level of 1,001", lambda: build(with_recipes(recipe_row(RecipeLevel="1001")))),
             ("a flag yes", lambda: build(with_recipes(recipe_row(IsRefining="yes")))),
             ("a table with no column PerkCost", lambda: build(with_recipes(missing))),
             ("two rows of one recipe id (any case)", lambda: build(with_recipes(recipe_row(), recipe_row(RecipeID="testknifet1")))),
             ("no station row", lambda: build(stations=[])),
             ("a station row with no type", lambda: build(stations=[element(children=[station_element("Bad", [])])])),
             ("no table of the type PerkBucketData", lambda: build(made_up_sheets(Buckets=None)))]
    for what, action in cases:
        message = stops(action)
        report(message is not None, f"stops: {what} ({message})")


def case_stations() -> None:
    document = build()
    rows = {row["name"]: row for row in document["stations"]}
    report(rows["Engineering2"]["types"] == ["engineering1", "engineering2"] and abs(rows["Blacksmith5"]["discount"] - 0.45) < 1e-6
           and rows["Blacksmith5"]["mannequin"] == "Blacksmithing_Craft" and document["missingStations"] == {
               "Carpentry2": [f"{tool.STATION_SLICES}station_carpentry_master.dynamicslice"]},
           "stations: the rows (types in lower case, the discount, the mannequin) and the authored key with no row (Carpentry2)")


def case_build() -> None:
    document = build()
    supported = sorted(entry["id"] for entry in document["recipes"] if not entry["unsupported"])
    counts = document["counts"]
    report(supported == ["TestBar", "TestIngot", "TestKnifeT1"] and document["categories"]["testmetal"] == ["TestIngotA", "TestIngotB"]
           and document["categories"]["wood"] == ["TestWoodT1"] and document["events"] == {
               "Craft_Test": {"progression": "Smelting", "reward": 11.0, "xp": 0.0, "standing": 0.0}}
           and counts["missingBuckets"] == 1 and counts["supported"] == 3 and counts["campRecipes"] == 2
           and document["resources"]["TestCharm"]["bucket"] == "Bucket_Test",
           "build: 3 recipes with no cause (stage 3.7b: the refining recipe too), the members of a category in the byte order of the ids, "
           "the event of a recipe (the event with N/A of a different system is not read), one resource bucket with no row (a diagnostic)")


def case_write(folder: Path) -> None:
    target = folder / "crafting-data.json"
    target.write_text(MARK)
    with patch.object(tool, "Sources", lambda *arguments, **keywords: FakeSources(made_up_sheets(), [], {})), \
            patch.object(sys, "argv", ["crafting.py", str(target)]):
        status = tool.main()
    report(status == 1 and target.read_text() == MARK and not list(folder.glob(".crafting-data.json.*")),
           "write: a run that stops (no station row) leaves the earlier file, and no temporary file")
    sheets = made_up_sheets()
    sheet_of = {name.encode(): sheet for name, (_, sheet) in sheets.items()}
    type_of = {name.encode(): kind for name, (kind, _) in sheets.items()}
    with patch.object(tool, "Sources", lambda *arguments, **keywords: FakeSources(sheets, made_up_stations(), {})), \
            patch.object(tool, "sheet_type", lambda data: type_of.get(data, "")), patch.object(tool, "Sheet", lambda data: sheet_of[data]), \
            patch.object(sys, "argv", ["crafting.py", str(target)]):
        status = tool.main()
    written = json.loads(target.read_text())
    report(status == 0 and written["format"] == tool.FORMAT and len(written["recipes"]) == 3,
           "write: a run that completes replaces the file (format 3, 3 recipes)")


def case_incomplete(folder: Path) -> None:
    def with_recipe(**cells):
        return made_up_sheets(Recipes=(tool.RECIPE_TYPE, FakeSheet("Recipes", [recipe_row(**cells)])))
    for kind in ("Item", "Category_Only", "Currency"):
        message = stops(lambda: build(with_recipe(Ingredient2="", Type2=kind, Qty2="1")))
        report(message is not None and "Ingredient2 has no id" in message, f"incomplete: an entry of the type {kind} with no id stops the tool ({message})")
    untyped = recipe_of(build(with_recipe(Type2="", Qty2="1")), "TestKnifeT1")
    report(untyped["ingredients"] == [["Item", "TestFlintT1", 1]],
           f"incomplete: an entry with an id and no type is not an ingredient (the client skips it): {untyped['ingredients']}")
    target = folder / "crafting-data-incomplete.json"
    target.write_text(MARK)
    sheets = with_recipe(Ingredient2="", Type2="Item", Qty2="1")
    sheet_of = {name.encode(): sheet for name, (_, sheet) in sheets.items()}
    type_of = {name.encode(): kind for name, (kind, _) in sheets.items()}
    with patch.object(tool, "Sources", lambda *arguments, **keywords: FakeSources(sheets, made_up_stations(), {})), \
            patch.object(tool, "sheet_type", lambda data: type_of.get(data, "")), patch.object(tool, "Sheet", lambda data: sheet_of[data]), \
            patch.object(sys, "argv", ["crafting.py", str(target)]), contextlib.redirect_stdout(io.StringIO()), \
            contextlib.redirect_stderr(io.StringIO()):
        status = tool.main()
    report(status == 1 and target.read_text() == MARK,
           f"incomplete: a run of the tool with such an entry has the status 1 ({status}) and leaves the earlier file")


def case_refining() -> None:
    document = build()
    ingot = recipe_of(document, "TestIngot")
    report(ingot["unsupported"] == [] and ingot["refining"] is True and ingot["craftAll"] is True and ingot["event"] == "Craft_Test"
           and ingot["firstCraftAchievement"] == "FC_TestIngot" and ingot["cooldownSeconds"] == 86400 and ingot["cooldownQuantity"] == 10
           and ingot["bonusIncrease"] == [0.25, 0.5, 0.75, 1.0] and ingot["bonusDecrease"] == [-0.05, -0.1],
           f"refining: refining, craft-all, the event, the first craft, and the cooldown with their data give no cause ({ingot['unsupported']})")
    report(document["ranks"] == {"Armoring": [0.0, 0.0, 0.0], "Riding": [0.0, 0.0], "Smelting": [0.0, 510.0, 520.0, 530.0]}
           and document["achievements"] == {"FC_TestIngot": 5330} and document["materials"] == {"TestFlintT1": [0, 10, 5, 0, 0]}
           and recipe_of(document, "TestKnifeT1")["bonusIncrease"] == [],
           "refining: the RollBonus of each rank (a table with no RollBonus column: 0), the bit of the first-craft achievement of a recipe, "
           "the bonus values of an item (only the items with a value), an empty tier list")
    standing = recipe_of(build(made_up_sheets(Recipes=(tool.RECIPE_TYPE, FakeSheet("Recipes", [recipe_row(GameEventID="Standing_Event")])))),
                         "TestKnifeT1")
    report(standing["unsupported"] == [], "refining: an event with territory standing gives no cause (the backend gives no standing)")
    gap = made_up_sheets(Smelting=(tool.RANK_TYPE, FakeSheet("Smelting", [{"Level": "0", "RollBonus": "0"}, {"Level": "2", "RollBonus": "520"}])))
    message = stops(lambda: build(gap))
    report(message is not None and "no gap" in message, f"refining: a rank table with a gap stops the tool ({message})")
    no_ranks = stops(lambda: build(made_up_sheets(Smelting=None, Riding=None, Armoring=None)))
    report(no_ranks is not None and tool.RANK_TYPE in no_ranks, f"refining: no rank table stops the tool ({no_ranks})")


def case_procedural() -> None:
    procedural = recipe_row(RecipeID="TestSwordRecipe", ItemID="TestSword", IsProcedural="1", BaseGearScore="200", BaseTier="2",
                            MaxPerkItemsAllowed="", PerkItemsBucketPush="0", PerkCost="15", Ingredient1="TestMetal", Type1="Category_Only",
                            Qty1="7", Ingredient3="Azoth_Currency", Type3="Currency", Qty3="25")
    rows = [dict(row) for row in made_up_sheets()["Items"][1]._rows]
    sheets = made_up_sheets(Recipes=(tool.RECIPE_TYPE, FakeSheet("Recipes", [procedural, recipe_row(RecipeID="TestRange", ItemID="TestSword")])))
    for row in rows:
        if row["ItemID"] == "TestSword":
            row["MaxCraftGS"] = "650"
        if row["ItemID"] == "TestIngotB":
            row["IngredientGearScoreBaseBonus"] = "5"
            row["IngredientGearScoreMaxBonus"] = "10"
        if row["ItemID"] == "TestIngotA":
            row["IngredientGearScoreMaxBonus"] = "Expansion2023"
    sheets["Items"] = (tool.MASTER_TYPE, FakeSheet("Items", rows))
    document = build(sheets)
    sword = recipe_of(document, "TestSwordRecipe")
    report(sword["unsupported"] == [] and sword["procedural"] is True and sword["perkItems"] == 1 and sword["push"] is False
           and sword["perkCost"] == 15 and sword["baseGearScore"] == 200 and sword["maxCraftGS"] == 650
           and ["Currency", "Azoth_Currency", 25] in sword["ingredients"],
           f"procedural: a procedural recipe with a BaseGearScore, a perk item, a PerkCost, and Azoth gives no cause ({sword['unsupported']}), "
           f"the MaxCraftGS of its output ({sword['maxCraftGS']})")
    report(tool.CAUSE_RANGE in recipe_of(document, "TestRange")["unsupported"],
           "procedural: an output with a gear score range and no BaseGearScore stays a cause")
    report(document["rankGearScore"]["Armoring"] == [[1, 5, 0, 0, 0], [1, 6, 0, 0, 1], [1, 7, 0, 0, 2]]
           and document["rankGearScore"]["Smelting"] == [[0, 0, 0, 0, 0]] * 4,
           "procedural: the largest gear score bonus of each rank (MaxGearScoreBonus, T2 to T5; a table with no such column: 0)")
    report(document["materials"].get("TestIngotB") == [0, 0, 0, 5, 10] and "TestIngotA" not in document["materials"]
           and document["counts"]["gearScoreTextCells"] == 1,
           "procedural: the gear score values of an ingredient; a text that is not a number is 0 and counted")
    report(document["currencies"] == {"Azoth_Currency": {"cap": 2500}},
           "procedural: the cap of a currency of a recipe (the MaximumInfluence of the rank 0 of its rank table)")
    generation = document["perkGeneration"]
    report(generation["chanceModifiers"] == [1.0, 0.5, 2.0] and generation["tiers"][5] == [[4, [[470, 599]]], [5, [[600, 625]]]]
           and generation["tiers"][0] == [] and generation["tiers"][2] == [[1, [[0, 199]]], [2, [[200, 700]]]],
           "procedural: the list Crafting of the perk generation data (not General) and the chance modifiers")
    message = stops(lambda: build(generation=made_up_generation(crafting=False)))
    report(message is not None and "Crafting" in message, f"procedural: a base attributes file with no list Crafting stops the tool ({message})")


def main() -> int:
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    folder = Path(sys.argv[1])
    folder.mkdir(parents=True, exist_ok=True)
    wanted = sys.argv[2:] or list(CASES)
    for name in wanted:
        if name == "recipe":
            case_recipe()
        elif name == "causes":
            case_causes()
        elif name == "stops":
            case_stops()
        elif name == "stations":
            case_stations()
        elif name == "build":
            case_build()
        elif name == "write":
            case_write(folder)
        elif name == "incomplete":
            case_incomplete(folder)
        elif name == "refining":
            case_refining()
        elif name == "procedural":
            case_procedural()
        else:
            report(False, f"unknown case {name}")
    print(f"{len(FAILED)} failed")
    return 0 if not FAILED else 1


if __name__ == "__main__":
    sys.exit(main())
