#!/usr/bin/env python3
"""Write the crafting data of the backend (stage 3.7a, docs/systems/crafting.md): one JSON file, crafting-data.json.

The backend reads GAMEDATA_DIR/crafting-data.json at its start. Without the file the backend starts and works, and each craft request is
refused with no change (docs/systems/crafting.md). The file is game data: it stays outside the repository.

The sources (all in the pak files of the game copy; research crafting-stage.md sections 3 to 10):

  1. The recipes: each table of the type CraftingRecipeData (15 tables in the supported build, 8,294 recipe ids; the base table
     javelindata_crafting has no row). A recipe has its station keys (StationType1 to 4), its trade skill and RecipeLevel, seven
     typed ingredients with their quantities (Type: Item, Category_Only, Currency), its output (ItemID, OutputQty), the fee
     (CraftingFee, UseCraftingTax), the gear score and perk options, the event, the achievements, the expansion, the world tags,
     and the cooldown. The loader defaults of the client apply to the perk options (client 0x4481730, research K4): a blank
     PerkItemsBucketPush is true; a MaxPerkItemsAllowed of 0 (or blank) is 1 for a procedural recipe and 0 for a different one.
  2. The station database: sharedassets/genericassets/craftingstations.craftstationdb (63 rows): Name, CraftingTypes (the station
     keys that the row permits: a recipe is permitted when one of its keys is in the list; client 0x2E5ED00), MannequinTag, and
     AzothDiscountPercent.
  3. The Assembly keys of the station slices (slices/structures/settlements/stations/): a key with no row of the station database is a
     diagnostic ("missingStations"; research K1: Smelting2, Carpentry2, Masonry2, Tanning2, Weaving2). The tool does not make a row for
     it: the backend uses the variant of decision D1 (interactions.py, "override" of a server child).
  4. The master item tables (MasterItemDefinitions) and the house item tables (HouseItems, HouseItemsMTX): the items of the ingredients
     and of the outputs, the IngredientCategories of each item (the members of a category ingredient; the UI table
     javelindata_craftingcategories does not define membership), the tier, and the gear score values of an output.
  5. The game events (GameEventData): the event of a recipe (its trade skill experience is stage 3.7b).
  6. The resource items (ResourceItemDefinitions: ResourceID, PerkBucket, GemPerk, SocketType) and the perk buckets (PerkBucketData):
     the perk bucket of each resource must be a bucket of the data (the charms are stage 3.7c).

The support of stage 3.7a (the field "unsupported" of a recipe: each cause that the backend has no rule for yet; an empty list is a
recipe that the backend crafts): the fixed recipes with no reward event, no cooldown, no perk items, no gear score rule, no unlock, and
item ingredients or category ingredients. The causes name the later run that adds the rule (crafting.md, the plan for 3.7b to 3.7e).

The tool stops with status 1 and one message, and writes no file, when a source that it needs is not there or does not decode, when
a needed table or column is not there, when two recipe rows have one id (case folded) or two ids have one CRC-32, when an ingredient
type is a text that is not "", "Item", "Category_Only", or "Currency", when a quantity, a fee, a level, or a gear score is not a whole
number of 0 or more or is above the limit of the request of the client (an ingredient quantity of 50,000, a level of 1,000, a gear
score of 10,000; research crafting.md 4.1), when an ingredient entry has a type and no id (code review of stage 3.7a, finding CR7A-R3; an
entry with no type is not an ingredient, as in the client), when a flag cell has a text that is not a flag, when a station row has no name or no type,
or when the station database has no row. A reference that does not resolve (an output or an ingredient that is not an item, a category
with no item, a station key that no row permits, an event with no row, a perk bucket with no row) is a cause of the recipe or a counted
diagnostic, not a stop: the retail data has some of them. tools/gamedata/check_crafting_tool.py is the check of these rules.

Output (one line of JSON):
  {"format": 1,
   "stations": [{"name", "types": [KEY, ...], "discount", "mannequin"}, ...],
   "missingStations": {KEY: [SLICE, ...], ...},
   "recipes": [{"id", "crc", "table", "category", "group", "known", "listed", "skill", "level", "stations": [KEY, ...],
                "ingredients": [[TYPE, ID, QUANTITY], ...], "item", "outputQty", "procedural", "refining", "craftAll", "temporary",
                "fee", "tax", "baseGearScore", "baseTier", "disallowBonuses", "perkItems", "attributeSlots", "push", "perkCost",
                "event", "requiredAchievement", "hiddenByAchievement", "unlockedAchievement", "firstCraftAchievement", "expansion",
                "worldTags", "cooldownSeconds", "cooldownQuantity", "skipGrant", "bonusChance", "unsupported": [CAUSE, ...]}, ...],
   "categories": {CATEGORY: [ITEM, ...], ...},          (CATEGORY in lower case; the items in the byte order of their ids)
   "events": {EVENT: {"id", "progression", "reward", "xp", "standing"}, ...},
   "resources": {ITEM: {"bucket", "gemPerk", "socket"}, ...},
   "counts": {...}}
  KEY is a station key in lower case. TYPE is "Item", "Category_Only", or "Currency". "crc" is the CRC-32 of the lower-case recipe id
  (the recipe id of request 2997; client 0x4485AF0). "outputQty" is null for a blank cell.

Usage: crafting.py [OUTPUT.json] [--recipe ID]...
  --recipe: print these recipes (with their causes) and write no file.
Default output: <SOULWARDEN_GAMEDATA_DIR>/crafting-data.json
Environment: as creature_data.py (SOULWARDEN_GAME_DIR, SOULWARDEN_GAMEDATA_DIR, SOULWARDEN_OODLE_LIB, SOULWARDEN_OODLE_DLL,
             SOULWARDEN_OODLE_DEC, SOULWARDEN_EXTRACT_DIR).
"""

from __future__ import annotations

import argparse
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
from creature_data import TABLES_FOLDER, Sources  # noqa: E402
from interactions import ASSEMBLY_CLASS, CRAFTING_STATIONS, STATION_ENTRY, child_of, text_of, walk  # noqa: E402
from item_catalog import HOUSE_TABLES, HOUSE_TYPE, MASTER_TYPE, Sheet, sheet_type  # noqa: E402

OUTPUT_NAME = "crafting-data.json"
FORMAT = 1
STATION_SLICES = "slices/structures/settlements/stations/"
WANTED = (TABLES_FOLDER, CRAFTING_STATIONS, STATION_SLICES)
RECIPE_TYPE = "CraftingRecipeData"
EVENT_TYPE = "GameEventData"
RESOURCE_TYPE = "ResourceItemDefinitions"
BUCKET_TYPE = "PerkBucketData"
INGREDIENTS = 7
STATION_COLUMNS = 4
# The ingredient types of the data and of the request (client enum 0x4F43EE7: Invalid 0, Item 1, Currency 2, CategoryAny 3, CategoryOnly
# 4). The retail tables have only these texts.
INGREDIENT_TYPES = {"Item": 1, "Currency": 2, "Category_Only": 4}
# The limits of the request reader of the client (research crafting.md 4.1): an ingredient quantity of 50,000, a gear score of 10,000.
# A level above 1,000 is not a level of a trade skill table (the tables have 501 rows).
QUANTITY_LIMIT = 50000
GEAR_SCORE_LIMIT = 10000
LEVEL_LIMIT = 1000
FLAG_TEXTS = {"": False, "0": False, "1": True, "false": False, "true": True}
RECIPE_COLUMNS = ("RecipeID", "CraftingCategory", "CraftingGroup", "bKnownByDefault", "bListedByDefault", "CraftAll", "IsRefining",
                  "IsProcedural", "AttributeOnlyPerkItemSlotCount", "MaxPerkItemsAllowed", "PerkItemsBucketPush", "IsTemporary",
                  "CraftingFee", "UseCraftingTax", "Tradeskill", "RecipeLevel", "OutputQty", "ItemID", "SkipGrantItems", "BaseGearScore",
                  "BaseTier", "DisallowBonusesToGS", "RequiredAchievementID", "HiddenByAchievementID", "UnlockedAchievementID",
                  "FirstCraftAchievementId", "GameEventID", "CooldownSeconds", "CooldownQuantity", "BonusItemChance", "PerkCost",
                  "RequiredExpansion") \
    + tuple(f"StationType{index}" for index in range(1, STATION_COLUMNS + 1)) \
    + tuple(f"{prefix}{index}" for index in range(1, INGREDIENTS + 1) for prefix in ("Ingredient", "Type", "Qty"))
ITEM_COLUMNS = ("ItemID", "IngredientCategories", "Tier", "GearScoreOverride", "MinGearScore", "MaxGearScore")
EVENT_COLUMNS = ("EventID", "CategoricalProgressionId", "CategoricalProgressionReward", "UniversalExpAmount", "TerritoryStanding")
NAME_FIELD = field_id("Name")
TYPES_FIELD = field_id("CraftingTypes")
MANNEQUIN_FIELD = field_id("MannequinTag")
DISCOUNT_FIELD = field_id("AzothDiscountPercent")

# The causes of a recipe that the backend of stage 3.7a does not craft, with the run that adds the rule (crafting.md, the plan).
CAUSE_PROCEDURAL = "procedural result (stage 3.7c)"
CAUSE_REFINING = "refining (stage 3.7b)"
CAUSE_CRAFT_ALL = "craft-all result presentation (stage 3.7b)"
CAUSE_EVENT = "reward event (stage 3.7b)"
CAUSE_FIRST_CRAFT = "first-craft achievement (stage 3.7b)"
CAUSE_COOLDOWN = "cooldown (stage 3.7b)"
CAUSE_CURRENCY = "currency ingredient (stage 3.7b)"
CAUSE_PERKS = "perk items or Azoth upgrade (stage 3.7c)"
CAUSE_GEAR_SCORE = "craft gear score rule (stage 3.7c)"
CAUSE_UNLOCK = "recipe unlock or achievement rule (a later stage)"
CAUSE_SKIP_GRANT = "no granted item (SkipGrantItems: a later stage)"
CAUSE_WORLD_TAGS = "world tags of a seasonal server (no such server)"
CAUSE_TEMPORARY = "temporary recipe (no rule)"
CAUSE_NO_OUTPUT = "no output item"
CAUSE_NO_QUANTITY = "no output quantity"
CAUSE_NOT_ITEM = "output is not an item of the item tables"
CAUSE_RANGE = "output has a gear score range and no override (stage 3.7c)"
CAUSE_INGREDIENT = "an ingredient is not an item of the item tables"
CAUSE_CATEGORY = "a category ingredient has no item"
CAUSE_NO_INGREDIENT = "no ingredient"
CAUSE_ZERO = "an ingredient has the quantity 0 (no rule)"
CAUSE_NO_STATION = "no station row permits a station key of the recipe"


def flag(text: str, what: str) -> bool:
    value = FLAG_TEXTS.get(text.strip().lower())
    if value is None:
        raise Failure(f"{what} has the text {text!r}, which is not a flag (\"\", \"0\", \"1\", \"true\", \"false\")")
    return value


def number(text: str, what: str, default: float = 0.0) -> float:
    text = text.strip()
    if not text:
        return default
    try:
        value = float(text)
    except ValueError as error:
        raise Failure(f"{what} is not a number: {text!r}") from error
    if not math.isfinite(value):
        raise Failure(f"{what} is not a finite number: {text!r}")
    return value


def whole(text: str, what: str, limit: int, default: int = 0) -> int:
    value = number(text, what, float(default))
    if value != int(value) or value < 0 or value > limit:
        raise Failure(f"{what} is not a whole number of 0 to {limit}: {text!r}")
    return int(value)


def need(sheet: Sheet, path: str, columns: tuple[str, ...]) -> None:
    for column in columns:
        if column not in sheet.columns:
            raise Failure(f"{path}: the table {sheet.name} has no column {column}")


def crc(text: str) -> int:
    """@return The id on the wire of an id text: the CRC-32 of the text in lower case (client 0x4485AF0, 0x12F4730)."""
    return zlib.crc32(text.lower().encode("utf-8")) & 0xFFFFFFFF


def read_stations(sources: Sources) -> list[dict]:
    """@return The rows of the crafting station database, in the order of the file."""
    roots = sources.stream(CRAFTING_STATIONS)
    if roots is None:
        sources.check_complete()
        raise Failure(f"{CRAFTING_STATIONS} has no source")
    rows = []
    for root in roots:
        for element in walk(root):
            types_element = child_of(element, TYPES_FIELD)
            if types_element is None:
                continue
            name = text_of(child_of(element, NAME_FIELD)).strip()
            types = [text_of(item).strip().lower() for item in walk(types_element) if item is not types_element and item.value]
            if not name or not types:
                raise Failure(f"{CRAFTING_STATIONS}: a station row has no name or no type ({name!r})")
            discount_element = child_of(element, DISCOUNT_FIELD)
            discount = 0.0
            if discount_element is not None and discount_element.value is not None:
                if len(discount_element.value) != 4:
                    raise Failure(f"{CRAFTING_STATIONS}: the station {name}: AzothDiscountPercent is not a number of 4 bytes")
                discount = struct.unpack(">f", discount_element.value)[0]
                if not math.isfinite(discount):
                    raise Failure(f"{CRAFTING_STATIONS}: the station {name}: AzothDiscountPercent is not finite")
            rows.append({"name": name, "types": types, "discount": round(discount, 6),
                         "mannequin": text_of(child_of(element, MANNEQUIN_FIELD)).strip()})
    if not rows:
        raise Failure("the crafting station database has no row")
    names = [row["name"].lower() for row in rows]
    if len(set(names)) != len(names):
        raise Failure("the crafting station database has two rows with one name")
    return rows


def read_station_slices(sources: Sources) -> dict[str, list[str]]:
    """@return The Assembly key of each station slice: KEY -> [SLICE, ...]."""
    keys: dict[str, list[str]] = {}
    for path in sources.paths(STATION_SLICES, ".dynamicslice"):
        roots = sources.stream(path)
        if roots is None:
            continue
        for root in roots:
            for element in walk(root):
                if element.type_id != ASSEMBLY_CLASS:
                    continue
                entry = next((item for item in walk(element) if item.name == STATION_ENTRY), None)
                key = text_of(entry).strip()
                if key:
                    keys.setdefault(key, []).append(path)
    return keys


class Tables:
    """The tables of the crafting data."""

    def __init__(self, sources: Sources) -> None:
        self.recipes: dict[str, tuple[str, Sheet, int]] = {}
        self.items: dict[str, dict] = {}
        self.house_items: set[str] = set()
        self.events: dict[str, dict] = {}
        self.resources: dict[str, dict] = {}
        self.buckets: set[str] = set()
        found = set()
        for path in sources.paths(TABLES_FOLDER, ".datasheet"):
            data = sources.read(path)
            if data is None:
                continue
            kind = sheet_type(data)
            if kind not in (RECIPE_TYPE, MASTER_TYPE, HOUSE_TYPE, EVENT_TYPE, RESOURCE_TYPE, BUCKET_TYPE):
                continue
            sheet = Sheet(data)
            if kind == HOUSE_TYPE:
                if sheet.name in HOUSE_TABLES:
                    self._house(sheet, path)
                continue
            found.add(kind)
            if kind == RECIPE_TYPE:
                self._recipes(sheet, path)
            elif kind == MASTER_TYPE:
                self._items(sheet, path)
            elif kind == EVENT_TYPE:
                self._events(sheet, path)
            elif kind == RESOURCE_TYPE:
                self._resources(sheet, path)
            elif kind == BUCKET_TYPE:
                self._buckets(sheet, path)
        sources.check_complete()
        for kind in (RECIPE_TYPE, MASTER_TYPE, EVENT_TYPE, RESOURCE_TYPE, BUCKET_TYPE):
            if kind not in found:
                raise Failure(f"the game copy has no table of the type {kind}")

    def _recipes(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, RECIPE_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "RecipeID").strip()
            if not identifier:
                continue
            if identifier.lower() in self.recipes:
                raise Failure(f"{path}: two recipe rows have the id {identifier}")
            self.recipes[identifier.lower()] = (identifier, sheet, row)

    def _items(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, ITEM_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "ItemID").strip()
            if not identifier or identifier.lower() in self.items:
                continue
            what = f"{path}: the item {identifier}"
            self.items[identifier.lower()] = {
                "id": identifier,
                "categories": [part.strip() for part in sheet.source(row, "IngredientCategories").split(",") if part.strip()],
                "tier": whole(sheet.source(row, "Tier"), f"{what}: Tier", 100),
                "override": whole(sheet.source(row, "GearScoreOverride"), f"{what}: GearScoreOverride", GEAR_SCORE_LIMIT),
                "min": whole(sheet.source(row, "MinGearScore"), f"{what}: MinGearScore", GEAR_SCORE_LIMIT),
                "max": whole(sheet.source(row, "MaxGearScore"), f"{what}: MaxGearScore", GEAR_SCORE_LIMIT)}

    def _house(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, ("HouseItemID",))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "HouseItemID").strip()
            if identifier:
                self.house_items.add(identifier.lower())

    def _events(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, EVENT_COLUMNS)
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "EventID").strip()
            if not identifier or identifier.lower() in self.events:
                continue
            # The cells are read for the events of a recipe only (an event of a different system can have a text such as "N/A").
            self.events[identifier.lower()] = {"id": identifier, "sheet": sheet, "row": row, "path": path}

    def _resources(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, ("ResourceID", "PerkBucket", "GemPerk", "SocketType"))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "ResourceID").strip()
            if identifier and identifier.lower() not in self.resources:
                self.resources[identifier.lower()] = {"id": identifier, "bucket": sheet.source(row, "PerkBucket").strip(),
                                                      "gemPerk": sheet.source(row, "GemPerk").strip(),
                                                      "socket": sheet.source(row, "SocketType").strip()}

    def _buckets(self, sheet: Sheet, path: str) -> None:
        need(sheet, path, ("PerkBucketID",))
        for row in range(sheet.row_count):
            identifier = sheet.source(row, "PerkBucketID").strip()
            if identifier:
                self.buckets.add(identifier.lower())


def recipe_entry(identifier: str, sheet: Sheet, row: int, tables: Tables, categories: dict[str, list[str]],
                 station_types: set[str]) -> dict:
    """@return The entry of one recipe row with its causes (an empty list: the backend of stage 3.7a crafts it)."""
    what = f"the recipe {identifier} ({sheet.name})"

    def cell(name: str) -> str:
        return sheet.source(row, name).strip()

    procedural = flag(cell("IsProcedural"), f"{what}: IsProcedural")
    # The loader defaults of the client (client 0x4481730, research K4): a blank push flag is true; a limit of 0 is 1 for a procedural
    # recipe and 0 for a different one.
    perk_items = whole(cell("MaxPerkItemsAllowed"), f"{what}: MaxPerkItemsAllowed", 100)
    if perk_items == 0:
        perk_items = 1 if procedural else 0
    push = True if not cell("PerkItemsBucketPush") else flag(cell("PerkItemsBucketPush"), f"{what}: PerkItemsBucketPush")
    output_text = cell("OutputQty")
    entry = {
        "id": identifier, "crc": crc(identifier), "table": sheet.name, "category": cell("CraftingCategory"),
        "group": cell("CraftingGroup"), "known": flag(cell("bKnownByDefault"), f"{what}: bKnownByDefault"),
        "listed": flag(cell("bListedByDefault"), f"{what}: bListedByDefault"), "skill": cell("Tradeskill"),
        "level": whole(cell("RecipeLevel"), f"{what}: RecipeLevel", LEVEL_LIMIT),
        "stations": [cell(f"StationType{index}").lower() for index in range(1, STATION_COLUMNS + 1) if cell(f"StationType{index}")],
        "ingredients": [], "item": cell("ItemID"),
        "outputQty": whole(output_text, f"{what}: OutputQty", QUANTITY_LIMIT) if output_text else None,
        "procedural": procedural, "refining": flag(cell("IsRefining"), f"{what}: IsRefining"),
        "craftAll": flag(cell("CraftAll"), f"{what}: CraftAll"), "temporary": flag(cell("IsTemporary"), f"{what}: IsTemporary"),
        "fee": whole(cell("CraftingFee"), f"{what}: CraftingFee", 100000000),
        "tax": flag(cell("UseCraftingTax"), f"{what}: UseCraftingTax"),
        "baseGearScore": whole(cell("BaseGearScore"), f"{what}: BaseGearScore", GEAR_SCORE_LIMIT),
        "baseTier": whole(cell("BaseTier"), f"{what}: BaseTier", 100),
        "disallowBonuses": flag(cell("DisallowBonusesToGS"), f"{what}: DisallowBonusesToGS"), "perkItems": perk_items,
        "attributeSlots": whole(cell("AttributeOnlyPerkItemSlotCount"), f"{what}: AttributeOnlyPerkItemSlotCount", 100), "push": push,
        "perkCost": whole(cell("PerkCost"), f"{what}: PerkCost", 1000000), "event": cell("GameEventID"),
        "requiredAchievement": cell("RequiredAchievementID"), "hiddenByAchievement": cell("HiddenByAchievementID"),
        "unlockedAchievement": cell("UnlockedAchievementID"), "firstCraftAchievement": cell("FirstCraftAchievementId"),
        "expansion": cell("RequiredExpansion"), "worldTags": cell("RequiredWorldTags"),
        "cooldownSeconds": whole(cell("CooldownSeconds"), f"{what}: CooldownSeconds", 100000000),
        "cooldownQuantity": whole(cell("CooldownQuantity"), f"{what}: CooldownQuantity", 255),
        "skipGrant": flag(cell("SkipGrantItems"), f"{what}: SkipGrantItems"),
        "bonusChance": round(number(cell("BonusItemChance"), f"{what}: BonusItemChance"), 6), "unsupported": []}
    causes: list[str] = entry["unsupported"]
    for index in range(1, INGREDIENTS + 1):
        name = cell(f"Ingredient{index}")
        kind = cell(f"Type{index}")
        if kind not in INGREDIENT_TYPES and kind != "":
            raise Failure(f"{what}: Type{index} has the text {kind!r}, which is not an ingredient type ({', '.join(INGREDIENT_TYPES)})")
        quantity = whole(cell(f"Qty{index}"), f"{what}: Qty{index}", QUANTITY_LIMIT)
        # A blank type is the type Invalid (0): the entry is not an ingredient (client 0x5C47AC0 skips it), also with an id (88 entries of
        # the supported build).
        if not kind:
            continue
        # Code review of stage 3.7a, finding CR7A-R3: an entry with a type and no id is not complete. Before, the tool skipped it, and the
        # recipe took one material less. The supported build has no such entry: the tool stops, and the old file stays.
        if not name:
            raise Failure(f"{what}: Type{index} is {kind!r} and Ingredient{index} has no id")
        entry["ingredients"].append([kind, name, quantity])
        if quantity == 0:
            # A blank or 0 quantity (retail rows of the dungeon tables): no rule of the client for the cost of such an entry.
            causes.append(CAUSE_ZERO)
        if kind == "Item" and name.lower() not in tables.items:
            causes.append(CAUSE_INGREDIENT)
        elif kind == "Category_Only" and not categories.get(name.lower()):
            causes.append(CAUSE_CATEGORY)
        elif kind == "Currency":
            causes.append(CAUSE_CURRENCY)
    if not entry["ingredients"]:
        causes.append(CAUSE_NO_INGREDIENT)
    for test, cause in ((procedural, CAUSE_PROCEDURAL), (entry["refining"], CAUSE_REFINING), (entry["craftAll"], CAUSE_CRAFT_ALL),
                        (bool(entry["event"]), CAUSE_EVENT), (bool(entry["firstCraftAchievement"]), CAUSE_FIRST_CRAFT),
                        (entry["cooldownSeconds"] > 0 or entry["cooldownQuantity"] > 0, CAUSE_COOLDOWN),
                        (perk_items > 0 or entry["attributeSlots"] > 0 or entry["perkCost"] > 0, CAUSE_PERKS),
                        (entry["baseGearScore"] > 0, CAUSE_GEAR_SCORE),
                        (not entry["known"] or bool(entry["requiredAchievement"]) or bool(entry["hiddenByAchievement"])
                         or bool(entry["unlockedAchievement"]), CAUSE_UNLOCK),
                        (entry["skipGrant"], CAUSE_SKIP_GRANT), (bool(entry["worldTags"]), CAUSE_WORLD_TAGS),
                        (entry["temporary"], CAUSE_TEMPORARY)):
        if test:
            causes.append(cause)
    output = entry["item"].lower()
    if not output:
        causes.append(CAUSE_NO_OUTPUT)
    elif output not in tables.items and output not in tables.house_items:
        causes.append(CAUSE_NOT_ITEM)
    elif output in tables.items:
        item = tables.items[output]
        if item["max"] != 0 and item["override"] == 0:
            causes.append(CAUSE_RANGE)
    if entry["outputQty"] is None or entry["outputQty"] == 0:
        causes.append(CAUSE_NO_QUANTITY)
    if not any(key in station_types for key in entry["stations"]):
        causes.append(CAUSE_NO_STATION)
    entry["unsupported"] = list(dict.fromkeys(causes))
    return entry


def build(sources: Sources) -> dict:
    stations = read_stations(sources)
    slice_keys = read_station_slices(sources)
    tables = Tables(sources)
    station_types = {key for row in stations for key in row["types"]}
    station_names = {row["name"].lower() for row in stations}
    missing = {key: sorted(paths) for key, paths in sorted(slice_keys.items()) if key.lower() not in station_names}
    # The members of each category (IngredientCategories of the master items), in the byte order of their ids.
    categories: dict[str, list[str]] = {}
    for item in sorted(tables.items.values(), key=lambda value: value["id"].encode()):
        for category in item["categories"]:
            categories.setdefault(category.lower(), []).append(item["id"])
    for category, members in categories.items():
        if len({crc(member) for member in members}) != len(members):
            raise Failure(f"the category {category} has two items with one CRC-32")
    recipes = []
    crcs: dict[int, str] = {}
    for key in sorted(tables.recipes, key=lambda value: value.encode()):
        identifier, sheet, row = tables.recipes[key]
        entry = recipe_entry(identifier, sheet, row, tables, categories, station_types)
        if entry["crc"] in crcs:
            raise Failure(f"the recipes {crcs[entry['crc']]} and {identifier} have one CRC-32 ({entry['crc']:08x})")
        crcs[entry["crc"]] = identifier
        recipes.append(entry)
    events = {}
    missing_events = 0
    for entry in recipes:
        if entry["event"]:
            event = tables.events.get(entry["event"].lower())
            if event is None:
                missing_events += 1
                continue
            sheet, row, what = event["sheet"], event["row"], f"{event['path']}: the event {event['id']}"
            events[event["id"]] = {
                "progression": sheet.source(row, "CategoricalProgressionId").strip(),
                "reward": number(sheet.source(row, "CategoricalProgressionReward"), f"{what}: CategoricalProgressionReward"),
                "xp": number(sheet.source(row, "UniversalExpAmount"), f"{what}: UniversalExpAmount"),
                "standing": number(sheet.source(row, "TerritoryStanding"), f"{what}: TerritoryStanding")}
    resources = {}
    missing_buckets = 0
    for resource in sorted(tables.resources.values(), key=lambda value: value["id"].encode()):
        if resource["bucket"] and resource["bucket"].lower() not in tables.buckets:
            missing_buckets += 1
        resources[resource["id"]] = {"bucket": resource["bucket"], "gemPerk": resource["gemPerk"], "socket": resource["socket"]}
    supported = [entry for entry in recipes if not entry["unsupported"]]
    by_cause: dict[str, int] = {}
    for entry in recipes:
        for cause in entry["unsupported"]:
            by_cause[cause] = by_cause.get(cause, 0) + 1
    camp = [entry for entry in recipes if any(key.startswith("camp") for key in entry["stations"])]
    counts = {"recipes": len(recipes), "supported": len(supported), "refining": sum(entry["refining"] for entry in recipes),
              "procedural": sum(entry["procedural"] for entry in recipes), "campRecipes": len(camp),
              "campSupported": sum(1 for entry in camp if not entry["unsupported"]), "stations": len(stations),
              "missingStations": len(missing), "categories": len(categories), "items": len(tables.items),
              "houseItems": len(tables.house_items), "events": len(events), "missingEvents": missing_events,
              "resources": len(resources), "missingBuckets": missing_buckets, "byCause": dict(sorted(by_cause.items()))}
    return {"format": FORMAT, "stations": stations, "missingStations": missing, "recipes": recipes, "categories": categories,
            "events": events, "resources": resources, "counts": counts}


def write(target: Path, document: dict) -> None:
    """Writes the file through a temporary file in the same folder: a stop leaves the earlier file."""
    target.parent.mkdir(parents=True, exist_ok=True)
    temporary = target.with_name(f".{target.name}.{os.getpid()}.tmp")
    try:
        temporary.write_text(json.dumps(document, separators=(",", ":"), allow_nan=False) + "\n", encoding="utf-8")
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)


def main() -> int:
    home = Path.home()
    game = Path(os.environ.get("SOULWARDEN_GAME_DIR") or home / "Games/soulwarden/game")
    data_home = Path(os.environ.get("XDG_DATA_HOME") or home / ".local/share")
    gamedata = Path(os.environ.get("SOULWARDEN_GAMEDATA_DIR") or data_home / "soulwarden/gamedata")
    parser = argparse.ArgumentParser(description="Write the crafting data of the backend (crafting-data.json).")
    parser.add_argument("output", nargs="?", default=str(gamedata / OUTPUT_NAME))
    parser.add_argument("--recipe", action="append", help="print a recipe (with its causes) and write no file; more than one is possible")
    arguments = parser.parse_args()
    target = Path(arguments.output)
    try:
        document = build(Sources(game / "assets", WANTED))
        if arguments.recipe:
            wanted = {name.lower() for name in arguments.recipe}
            for entry in document["recipes"]:
                if entry["id"].lower() in wanted:
                    print(json.dumps(entry, indent=1))
            return 0
        write(target, document)
    except Failure as error:
        print(f"crafting: {error}. No file was written.", file=sys.stderr)
        return 1
    except ValueError as error:
        print(f"crafting: the document has a number that is not finite ({error}). No file was written.", file=sys.stderr)
        return 1
    except OSError as error:
        print(f"crafting: {target}: {error}. No file was written.", file=sys.stderr)
        return 1
    counts = document["counts"]
    print(f"{target}: {counts['recipes']} recipes ({counts['supported']} that the backend crafts in stage 3.7a; {counts['campRecipes']} "
          f"with a camp key, {counts['campSupported']} of them supported), {counts['stations']} station rows, {counts['categories']} "
          f"ingredient categories, {counts['events']} events, {counts['resources']} resource items")
    if document["missingStations"]:
        print("  station keys of the slices with no row of the station database: "
              + ", ".join(f"{key} ({len(paths)} slice(s))" for key, paths in document["missingStations"].items()))
    print("  causes: " + ", ".join(f"{cause} {count}" for cause, count in counts["byCause"].items()))
    if counts["missingEvents"] or counts["missingBuckets"]:
        print(f"  references with no row: {counts['missingEvents']} recipe event(s), {counts['missingBuckets']} perk bucket(s) of a resource")
    return 0


if __name__ == "__main__":
    sys.exit(main())
