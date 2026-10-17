#!/usr/bin/env python3
"""Check of the crafting data tool (crafting.py) with made-up sources (no file of the game). Stage 3.7a.

The tool stops with status 1, and writes no file, at a source that it cannot use (its docstring has the list). This check gives the
functions of the tool made-up tables, station rows, and station slices, and compares each result. The test program of the backend runs it
(case CRAFT-TOOL; case CRAFT1 to CRAFT-D1 have the backend side of the file).

  recipe          a recipe row: its values, the station keys in lower case, the typed ingredients, the loader defaults of the client
                  (a blank push flag is true; a MaxPerkItemsAllowed of 0 is 1 for a procedural recipe), a blank output quantity (null)
  causes          the causes of a recipe that the backend of stage 3.7a does not craft: a procedural recipe, refining, an event, a cooldown,
                  a currency ingredient, an unlock, an output that is not an item, an output with a gear score range and no override, an
                  ingredient that is not an item, a category with no item, a station key that no row permits, a 0 quantity
  stops           an ingredient type "Category_Any2", a quantity of 50,001, a level of 1,001, a flag "yes", a table with no column, two rows
                  of one recipe id, no station row, a station row with no type: each one stops the tool
  stations        the station rows (name, types in lower case, discount, mannequin) and the authored keys with no row ("missingStations")
  build           the whole build with the made-up sources: the supported recipes, the categories (members in the byte order of the ids),
                  the events of the recipes, a resource with a bucket that the bucket table does not have (a counted diagnostic)
  write           a run that stops leaves the earlier file; a run that completes replaces it (one file, through a temporary file)
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

CASES = ("recipe", "causes", "stops", "stations", "build", "write", "incomplete")
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

    def paths(self, folder: str, suffix: str) -> list[str]:
        if folder == tool.STATION_SLICES:
            return [f"{folder}{name}{suffix}" for name in self.slices]
        return [f"{folder}{name}{suffix}" for name in self.sheets]

    def read(self, path: str) -> bytes:
        return Path(path).stem.encode()

    def stream(self, path: str):
        if path == tool.CRAFTING_STATIONS:
            return self.stations
        return [assembly_element(self.slices[Path(path).stem])]

    def check_complete(self) -> None:
        return None


def made_up_sheets(**changes) -> dict[str, tuple[str, FakeSheet]]:
    sheets = {
        "Recipes": (tool.RECIPE_TYPE, FakeSheet("Recipes", [
            recipe_row(), recipe_row(RecipeID="TestBar", ItemID="TestBar", OutputQty="2", StationType1="blacksmith2", StationType2="",
                                     Ingredient1="TestMetal", Type1="Category_Only", Qty1="2"),
            recipe_row(RecipeID="TestIngot", IsRefining="1", GameEventID="Craft_Test", ItemID="TestIngotA")])),
        "Items": (tool.MASTER_TYPE, FakeSheet("Items", [
            {"ItemID": "TestKnifeT1", "IngredientCategories": "", "Tier": "1", "GearScoreOverride": "100", "MinGearScore": "100",
             "MaxGearScore": "100"},
            {"ItemID": "TestFlintT1", "IngredientCategories": "", "Tier": "1", "GearScoreOverride": "", "MinGearScore": "", "MaxGearScore": ""},
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
             "TerritoryStanding": "N/A"}])),
        "Resources": (tool.RESOURCE_TYPE, FakeSheet("Resources", [
            {"ResourceID": "TestCharm", "PerkBucket": "Bucket_Test", "GemPerk": "", "SocketType": ""},
            {"ResourceID": "TestBadCharm", "PerkBucket": "Bucket_Missing", "GemPerk": "", "SocketType": ""}])),
        "Buckets": (tool.BUCKET_TYPE, FakeSheet("Buckets", [{"PerkBucketID": "Bucket_Test"}])),
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


def build(sheets=None, stations=None, slices=None) -> dict:
    sources = FakeSources(sheets if sheets is not None else made_up_sheets(), stations if stations is not None else made_up_stations(),
                          slices if slices is not None else {"station_engineering_t2": "Engineering2", "station_carpentry_master": "Carpentry2"})
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
    cases = [({"IsProcedural": "1"}, tool.CAUSE_PROCEDURAL), ({"IsRefining": "1"}, tool.CAUSE_REFINING), ({"GameEventID": "Craft_Test"}, tool.CAUSE_EVENT),
             ({"CooldownSeconds": "86400", "CooldownQuantity": "10"}, tool.CAUSE_COOLDOWN),
             ({"Ingredient3": "Azoth_Currency", "Type3": "Currency", "Qty3": "5"}, tool.CAUSE_CURRENCY),
             ({"bKnownByDefault": "0"}, tool.CAUSE_UNLOCK), ({"RequiredAchievementID": "Test_Unlock"}, tool.CAUSE_UNLOCK),
             ({"ItemID": "NoSuchItem"}, tool.CAUSE_NOT_ITEM), ({"ItemID": "TestSword"}, tool.CAUSE_RANGE),
             ({"Ingredient1": "NoSuchFlint"}, tool.CAUSE_INGREDIENT), ({"Ingredient1": "NoMetal", "Type1": "Category_Only"}, tool.CAUSE_CATEGORY),
             ({"StationType1": "engineering9", "StationType2": ""}, tool.CAUSE_NO_STATION), ({"Qty2": ""}, tool.CAUSE_ZERO),
             ({"MaxPerkItemsAllowed": "3"}, tool.CAUSE_PERKS), ({"BaseGearScore": "200"}, tool.CAUSE_GEAR_SCORE),
             ({"SkipGrantItems": "1"}, tool.CAUSE_SKIP_GRANT), ({"CraftAll": "1"}, tool.CAUSE_CRAFT_ALL)]
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
    report(supported == ["TestBar", "TestKnifeT1"] and document["categories"]["testmetal"] == ["TestIngotA", "TestIngotB"]
           and document["categories"]["wood"] == ["TestWoodT1"] and document["events"] == {
               "Craft_Test": {"progression": "Smelting", "reward": 11.0, "xp": 0.0, "standing": 0.0}}
           and counts["missingBuckets"] == 1 and counts["supported"] == 2 and counts["campRecipes"] == 2
           and document["resources"]["TestCharm"]["bucket"] == "Bucket_Test",
           "build: 2 recipes with no cause, the members of a category in the byte order of the ids, the event of a recipe (the event with "
           "N/A of a different system is not read), one resource bucket with no row (a diagnostic)")


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
           "write: a run that completes replaces the file (format 1, 3 recipes)")


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
        else:
            report(False, f"unknown case {name}")
    print(f"{len(FAILED)} failed")
    return 0 if not FAILED else 1


if __name__ == "__main__":
    sys.exit(main())
