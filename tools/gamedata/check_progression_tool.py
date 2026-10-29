#!/usr/bin/env python3
"""Check of the progression data tool (progression_tables.py) with made-up tables (no file of the game). Stage 1.1b.

The tool writes the expansion rows, the entitlements that each account gets, and the achievements that they give (format 2). This
check gives its function build_ownership made-up tables of the types ExpansionData, EntitlementData, and AchievementData, with
the structure of the rows of the game (the row None, the row Expansion2023 with its entitlement, the nine Steam roots of the DLC
packs and the entitlements that they give), and compares the result of each case. The test program of the backend runs it
(case DL9).

  valid                 the expansion rows, the entitlements of the grant (the expansion first, the roots, the entitlements
                        of the roots), the achievement of the expansion with its bit
  missing-expansion-entitlement   an expansion names an entitlement that the table does not have: the tool stops
  disabled-entitlement  an entitlement of the grant is not enabled: the tool stops
  bit-out-of-range      an entitlement has a bit index of 4600 (the client reads 575 bytes): the tool stops
  missing-achievement   the entitlement of the expansion names an achievement that the table does not have: the tool stops
  missing-root          a root of a DLC pack is not in the table: the tool stops
  level-out-of-range    an expansion has a level that the level table does not have: the tool stops
  no-achievement-table  the game data has no table of the type AchievementData: the tool stops
  rank-control          the entry point of the tool with made-up tables (the levels, the vitals, a weapon, Mining with
                        MaxLevel 200 and Expansion01MaxLevel 250, a rank table with the ranks 0 to 250): status 0, the
                        Mining cap with the expansion is 250
  short-rank-source     the same with the ranks 0 to 200 of Mining: status 1, the old file stays (code review of stage 1.1b,
                        finding F3: the tool wrote the cap 200)
  attribute-thresholds  the threshold abilities of each attribute table (stage 3.5b.2)
  no-equip-abilities    an attribute table with no column EquipAbilities: status 1, the old file stays (stage 3.5b.2)
  short-attribute-table the Focus table with no row of the value 100 (its mana threshold): status 1, the old file stays (stage 2.2,
                        code review of stage 3.5b.2, finding LB-R7; case RLB7)
  attribute-gap         the Strength table with no row of the value 50: status 1, the old file stays (finding LB-R7)
  standing              the standing row of the territory 6 and the rank table Territory_Standing with its XpReward (stage 1.8d,
                        format 3)
  short-standing-table  a standing MaxLevel with no row in Territory_Standing: status 1, the old file stays (stage 1.8d)
  territory-cards       stage QG1 (format 4): the TerritoryStandingXpModifier of the standing, the cards of each rank of
                        Territory_Standing, and the territory cards with the values of the Territory Bonus Data (the f32
                        values of the file)
  unknown-offer-card    a rank offers a card that is not a territory card: status 1, the old file stays (stage QG1)
  no-bonus-row          a territory card has no row of the Territory Bonus Data: status 1, the old file stays (stage QG1)
  no-xp-modifier        the territory of a standing has no TerritoryStandingXpModifier: status 1, the old file stays (stage QG1)
  factions              stage QG7: the member factions: the three factions with their reputation and token rows, the caps and the
                        RequiredLevel of their rank tables, the recruitment achievements of the faction table, the change rules and the
                        validation lists of the base attributes
  faction-bad-lists     a faction with a trial list and a rank-up list of different lengths: status 1, the old file stays (stage QG7)
  faction-auto-rank-up  a faction row with AutoRankUp: status 1, the old file stays (stage QG7)

The attribute tables of the entry point have one row for each value 5 to 100 (the range of Constitution).

Usage: check_progression_tool.py
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
from pathlib import Path
from unittest.mock import patch

sys.dont_write_bytecode = True  # keep __pycache__ out of the repository
sys.path.insert(0, str(Path(__file__).resolve().parent))
import progression_tables as tool  # noqa: E402
from appearance_tables import Failure  # noqa: E402
from check_ability_tool import FakeSheet  # noqa: E402

EXPANSION = "Ent_Xpack_RiseOfTheAngryEarth_Owner"


def sheets(case: str) -> tuple[dict, dict, FakeSheet]:
    expansions = FakeSheet("Expansions", [
        {"ExpansionId": "None", "MaxDisplayLevel": 60, "MaxCraftGS": 625, "MaxEquipGS": 625, "MaxTradeskillLevel": 200, "EntitlementId": ""},
        {"ExpansionId": "Expansion2023", "MaxDisplayLevel": 71 if case == "level-out-of-range" else 70, "MaxCraftGS": 700, "MaxEquipGS": 800,
         "MaxTradeskillLevel": 250, "EntitlementId": "Ent_Unknown" if case == "missing-expansion-entitlement" else EXPANSION}])
    rows = [{"UniqueTagID": EXPANSION, "EntitlementIndex": 4600 if case == "bit-out-of-range" else 1484, "IsEnabled": True,
             "RewardType": "Expansion2023", "Reward(s)": "",
             "AchievementId": "Unknown_Achievement" if case == "missing-achievement" else "Expansion_2023_Unlock"}]
    # The nine roots of the DLC packs; four give an entitlement (one of them three).
    children = {"STEAM_APP_ID.1200566": "DigitalArtBook", "STEAM_APP_ID.1200560": "DLC_Item_Emote_FistBump",
                "STEAM_APP_ID.1200561": "DLC_Item_Woodsman_Crest_1+DLC_Item_Woodsman_Crest_2+DLC_Item_Woodsman_Crest_3"}
    for number, root in enumerate(tool.RETAIL_DLC_ROOTS):
        if case == "missing-root" and number == 3:
            continue
        child = children.get(root, "")
        rows.append({"UniqueTagID": root, "EntitlementIndex": 220 + number, "IsEnabled": not (case == "disabled-entitlement" and number == 0),
                     "RewardType": "Entitlement" if child else "InventoryItem", "Reward(s)": child or f"TestItem{number}", "AchievementId": ""})
    for number, child in enumerate(["DigitalArtBook", "DLC_Item_Emote_FistBump", "DLC_Item_Woodsman_Crest_1", "DLC_Item_Woodsman_Crest_2",
                                    "DLC_Item_Woodsman_Crest_3"]):
        rows.append({"UniqueTagID": child, "EntitlementIndex": 300 + number, "IsEnabled": True, "RewardType": "Misc", "Reward(s)": child,
                     "AchievementId": ""})
    rows.append({"UniqueTagID": "STEAM_APP_ID.1266160", "EntitlementIndex": 232, "IsEnabled": True, "RewardType": "PlayerTitle",
                 "Reward(s)": "PlayerTitle_AlphaTester", "AchievementId": ""})
    tables = {"EntitlementData": FakeSheet("EntitlementData", rows),
              "AchievementDataTable": FakeSheet("AchievementDataTable", [
                  {"AchievementID": "Other_Achievement", "AchievementIndex": 5},
                  {"AchievementID": "Expansion_2023_Unlock", "AchievementIndex": 11979}])}
    kinds = {"EntitlementData": tool.ENTITLEMENT_TYPE, "AchievementDataTable": tool.ACHIEVEMENT_TYPE}
    if case == "no-achievement-table":
        del tables["AchievementDataTable"]
        del kinds["AchievementDataTable"]
    return tables, kinds, expansions


def run(case: str):
    tables, kinds, expansions = sheets(case)
    try:
        return tool.build_ownership(tables, kinds, expansions, 70), ""
    except Failure as error:
        return None, str(error)


ATTRIBUTE_VALUES = range(5, 101)


def attribute_rows(table: str, thresholds: bool, missing: int | None) -> list[dict]:
    """@return The rows of a made-up attribute table: the values 5 to 100 (not `missing`), the abilities of the value 100 (not for
    Constitution), and the column EquipAbilities with `thresholds`."""
    rows = []
    for value in ATTRIBUTE_VALUES:
        if value == missing:
            continue
        row = {"Level": value, "Health": value if table == tool.CONSTITUTION_TABLE else 0}
        if thresholds:
            row["EquipAbilities"] = (f"{table[:3]}_Bonus_100_1,{table[:3]}_Bonus_100_2"
                                     if value == 100 and table != tool.CONSTITUTION_TABLE else "")
        rows.append(row)
    return rows


# Stage QG1: the made-up Territory Bonus Data (the reader of the base attributes file is patched): Territory Bonus Id, Initial Bonus,
# Reduction Modifier, Min Value.
BONUS = {"tb_gainstanding": [1, 0.05000000074505806, 0.13500000536441803, 0.0024999999441206455],
         "tb_purchasehouse": [10, 1.0, 0.0, 0.0]}


FACTIONS = (("Syndicate", "SyndicateTokens", "MerchantGuild", "Faction1", "FactionIntro_Syndicate_Recruitment"),
            ("Marauder", "MarauderTokens", "ProcurerGuild", "Faction2", "FactionIntro_Marauders_Recruitment"),
            ("Covenant", "CovenantTokens", "ExplorerGuild", "Faction3", "FactionIntro_Covenant_Recruitment"))
# Stage QG7: the faction values of the base attributes file (made up, with the shape of the game data).
FACTION_BASE = {"change": [2592000, 20, 0, 0, 50],
                "trials": {1: ["FactionIntro_Syndicate_Recruitment", "syndicate_faction_quest_01"], 2: ["FactionIntro_Marauders_Recruitment"],
                           3: ["FactionIntro_Covenant_Recruitment"]},
                "rankUps": {1: ["FactionIntro_Syndicate_Recruitment", "9806_S_0001_RankUp"], 2: ["FactionIntro_Marauders_Recruitment"],
                            3: ["FactionIntro_Covenant_Recruitment"]}}


def faction_sources(case: str) -> list:
    """Stage QG7: the made-up faction tables: the six categorical rows, the six rank tables, and the faction table."""
    rows, sheets = [], []
    for name, token, table, kind, achievement in FACTIONS:
        rows.append({"CategoricalProgressionId": name, "RankTableId": table, "MaxLevel": 4, "Expansion01MaxLevel": 5,
                     "ProgressionType": "FactionReputation", "ProgressionCurrencyId": token,
                     "AutoRankUp": case == "faction-auto-rank-up" and name == "Covenant"})
        rows.append({"CategoricalProgressionId": token, "RankTableId": token, "MaxLevel": 4, "Expansion01MaxLevel": 5,
                     "ProgressionType": "FactionCurrency", "AutoRankUp": False})
        sheets.append(("CategoricalProgressionRankData", FakeSheet(table, [
            {"Rank": rank, "MaximumInfluence": cap, "RequiredLevel": level}
            for rank, (cap, level) in enumerate(zip((3000, 11000, 26000, 49000, 85000, 85000), (10, 20, 35, 50, 60, 61)))])))
        sheets.append(("CategoricalProgressionRankData", FakeSheet(token, [
            {"Rank": rank, "MaximumInfluence": cap} for rank, cap in enumerate((15000, 30000, 45000, 75000, 150000, 300000))])))
    sheets.append(("FactionData", FakeSheet("Factions", [{"FactionType": "None"}] + [
        {"FactionType": kind, "RequiredAchievementId": achievement, "RequiredLegacyAchievementId": f"S_{name.lower()}quest_1"}
        for name, token, table, kind, achievement in FACTIONS])))
    return rows, sheets


def rank_sources(short: bool, thresholds: bool = True, missing: tuple[str, int] | None = None, standing_rank: int = 2,
                 card_case: str = "", faction_case: str = "") -> tuple[dict, dict]:
    """@return The made-up tables of the entry point of the tool (the reviewer's probe RDT1 of stage 1.1b). `thresholds`: the attribute
    tables have the column EquipAbilities (stage 3.5b.2: Focus 100 gives Foc_Bonus_100_1). `missing`: the table and the value of a row
    that the source does not have (stage 2.2, finding LB-R7). `standing_rank`: the MaxLevel of the standing row of the territory 6 (stage
    1.8d; the rank table Territory_Standing has the ranks 0 to 2)."""
    tables, kinds, expansions = sheets("valid")
    tables[expansions.name] = expansions
    kinds[expansions.name] = "ExpansionData"
    additions = [
        ("ExperienceData", FakeSheet(tool.LEVEL_TABLE, [
            {"Level Number": level, "XPToLevel": level * 100, "AttributePoints": 0, "Health": 1, "AttributeRespecCost": 0,
             "MaxEquippableGearScore": min(100 + level * 10, 800) if level < 64 else 800, "IgnoreItemRarity": level >= 60}
            for level in range(70)])),
        ("AttributeDefinition", FakeSheet(tool.CONSTITUTION_TABLE, attribute_rows(tool.CONSTITUTION_TABLE, True, None))),
        *[("AttributeDefinition", FakeSheet(table, attribute_rows(table, thresholds,
                                                                  missing[1] if missing is not None and missing[0] == table else None)))
          for name, table in getattr(tool, "ATTRIBUTE_TABLES", ()) if table != tool.CONSTITUTION_TABLE],
        ("VitalsBaseData", FakeSheet(tool.PLAYER_VITALS_TABLE, [{"VitalsID": tool.PLAYER_VITALS_ROW, "HealthBaseMax": 800,
                                                                 "StaminaBaseMax": 100, "ManaBaseMax": 100, "ManaBaseRegenRate": 0,
                                                                 "ManaRegenDelay": 0, "ManaRegenDelayOnEmpty": 0}])),
        ("CategoricalProgressionData", FakeSheet(tool.PROGRESSION_TABLE, [
            {"CategoricalProgressionId": "SwordAbilityTable", "RankTableId": "WeaponMastery", "MaxLevel": 19, "Expansion01MaxLevel": 0},
            {"CategoricalProgressionId": "Mining", "RankTableId": "MiningRanks", "MaxLevel": 200, "Expansion01MaxLevel": 250},
            {"CategoricalProgressionId": "6", "RankTableId": tool.STANDING_RANK_TABLE, "MaxLevel": standing_rank, "Expansion01MaxLevel": 0,
             "ProgressionPointPool": "TerritoryStandingPool", "AutoRankUp": True}])),
        ("CategoricalProgressionRankData", FakeSheet(tool.STANDING_RANK_TABLE, [
            {"Rank": rank, "MaximumInfluence": influence, "XpReward": reward,
             "AvailableProgressionPoint1": "TB_Unknown" if card_case == "unknown-offer-card" and rank == 1 else "TB_GainStanding",
             "AvailableProgressionPoint2": "", "AvailableProgressionPoint3": "", "AvailableProgressionPoint4": "TB_PurchaseHouse" if rank < 2 else ""}
            for rank, (influence, reward) in enumerate([(150, 0), (375, 100), (600, 150)])])),
        ("TerritoryDefinition", FakeSheet("Territories", [
            {"TerritoryID": 6, "NameLocalizationKey": "@Windsward", "TerritoryStandingXpModifier": 1.25 if card_case != "no-xp-modifier" else ""},
            {"TerritoryID": 7, "NameLocalizationKey": "@Other", "TerritoryStandingXpModifier": 0.5}])),
        ("CategoricalProgressionRankData", FakeSheet("WeaponMastery", [
            {"Rank": rank, "MaximumInfluence": 10, "AzothRespecCost": 0} for rank in range(20)])),
        ("TradeskillRankData", FakeSheet("MiningRanks", [
            {"Rank": rank, "MaximumInfluence": 100, "AzothRespecCost": 0} for rank in range(201 if short else 251)])),
        ("AbilityData", FakeSheet("SwordAbilityTable", [{"AbilityID": "TestNode", "TreeId": 0, "TreeRowPosition": 0,
                                                         "TreeColumnPosition": 0, "IsActiveAbility": True}])),
        ("ProgressionPointData", FakeSheet(tool.POINT_TABLE, [
            {"PointPoolId": "SwordAbilityTable", "ProgressionPointId": "TestNode", "RequiredProgressionPointId": "", "DoNotSpendPoint": False,
             "PoolCategory": "", "TerritoryBonusCategory": "", "MaxLevel": 1, "RequiredCategoricalProgressionLevel": 0},
            {"PointPoolId": "", "ProgressionPointId": "TB_GainStanding", "RequiredProgressionPointId": "", "DoNotSpendPoint": False,
             "PoolCategory": "Territory", "TerritoryBonusCategory": "GainStanding", "MaxLevel": 102, "RequiredCategoricalProgressionLevel": 0},
            {"PointPoolId": "", "ProgressionPointId": "TB_PurchaseHouse", "RequiredProgressionPointId": "", "DoNotSpendPoint": True,
             "PoolCategory": "Territory", "TerritoryBonusCategory": "HousePurchase", "MaxLevel": 1, "RequiredCategoricalProgressionLevel": 1}]))]
    if faction_case:
        faction_rows, faction_sheets = faction_sources(faction_case)
        progression = next(sheet for kind, sheet in additions if sheet.name == tool.PROGRESSION_TABLE)
        additions = [entry for entry in additions if entry[1] is not progression]
        additions.append(("CategoricalProgressionData", FakeSheet(tool.PROGRESSION_TABLE, progression._rows + faction_rows)))
        additions.extend(faction_sheets)
    for kind, sheet in additions:
        tables[sheet.name] = sheet
        kinds[sheet.name] = kind
    return tables, kinds


def run_ranks(short: bool, thresholds: bool = True, missing: tuple[str, int] | None = None, standing_rank: int = 2,
              card_case: str = "", faction_case: str = "") -> tuple[int, bool, object]:
    """@return The status of the entry point, True when the old file stayed, and the Mining cap with the expansion (with the document:
    run_ranks.document)."""
    tables, kinds = rank_sources(short, thresholds, missing, standing_rank, card_case, faction_case)
    base = json.loads(json.dumps(FACTION_BASE), object_hook=lambda item: {int(key) if key.isdigit() else key: value for key, value in item.items()})
    if faction_case == "faction-bad-lists":
        base["rankUps"][2] = ["FactionIntro_Marauders_Recruitment", "9806_M_0001_RankUp"]
    bonus = {key: value for key, value in BONUS.items() if not (card_case == "no-bonus-row" and key == "tb_purchasehouse")}
    with tempfile.TemporaryDirectory(prefix="soulwarden-progression-check-") as folder:
        target = Path(folder) / "progression-tables.json"
        target.write_text("earlier output\n")
        contents = [name.encode() for name in tables]
        with patch.object(tool, "table_entries", return_value=[]), \
                patch.object(tool, "tables_of_paks", return_value=(contents, "made-up source")), \
                patch.object(tool, "sheet_type", side_effect=lambda data: kinds[data.decode()]), \
                patch.object(tool, "Sheet", side_effect=lambda data: tables[data.decode()]), \
                patch.object(tool, "read_territory_bonus", return_value=bonus), \
                patch.object(tool, "read_faction_base", return_value=base), \
                patch.object(sys, "argv", ["progression_tables.py", str(target)]), \
                contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = tool.main()
        text = target.read_text()
    document = json.loads(text) if status == 0 else {}
    mining = next((row[4] for row in document.get("progressions", []) if row[0] == "Mining"), None)
    run_ranks.document = document
    return status, text == "earlier output\n", mining


def main() -> int:
    failures = 0
    result, error = run("valid")
    passed = False
    if result is not None:
        expansion_rows, entitlements, achievements = result
        aliases = [entry[0] for entry in entitlements]
        passed = (expansion_rows == [["None", 60, 625, 625, 200, ""], ["Expansion2023", 70, 700, 800, 250, EXPANSION]]
                  and entitlements[0] == [EXPANSION, 1484, "Expansion2023", "", "Expansion_2023_Unlock"]
                  and len(entitlements) == 1 + len(tool.RETAIL_DLC_ROOTS) + 5 and aliases.index("DigitalArtBook") > aliases.index("STEAM_APP_ID.1200566")
                  and "STEAM_APP_ID.1266160" not in aliases and achievements == [["Expansion_2023_Unlock", 11979]])
    print(f"{'ok  ' if passed else 'FAIL'}  valid: {error or str(len(result[1])) + ' entitlements, achievements ' + str(result[2])}")
    failures += 0 if passed else 1
    for case in ("missing-expansion-entitlement", "disabled-entitlement", "bit-out-of-range", "missing-achievement", "missing-root",
                 "level-out-of-range", "no-achievement-table"):
        result, error = run(case)
        passed = result is None and bool(error)
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: {'the tool stops: ' + error if error else 'the tool did not stop'}")
        failures += 0 if passed else 1
    status, kept, mining = run_ranks(False)
    passed = status == 0 and mining == 250
    print(f"{'ok  ' if passed else 'FAIL'}  rank-control: status {status}, Mining cap with the expansion {mining}")
    failures += 0 if passed else 1
    thresholds = run_ranks.document.get("attributes", {}).get("abilities", {})
    passed = status == 0 and thresholds.get("focus") == [[100, ["Foc_Bonus_100_1", "Foc_Bonus_100_2"]]] and thresholds.get("constitution") == []
    print(f"{'ok  ' if passed else 'FAIL'}  attribute-thresholds: the threshold abilities of each attribute (stage 3.5b.2): focus "
          f"{thresholds.get('focus')}")
    failures += 0 if passed else 1
    # Stage 1.8d (format 3): the standing row of a territory and the rank table Territory_Standing with its XpReward.
    standings = run_ranks.document.get("standings")
    standing_table = run_ranks.document.get("rankTables", {}).get(tool.STANDING_RANK_TABLE)
    cards = [["TB_GainStanding", "", "", "TB_PurchaseHouse"], ["TB_GainStanding", "", "", "TB_PurchaseHouse"], ["TB_GainStanding", "", "", ""]]
    passed = status == 0 and run_ranks.document.get("format") == 4 \
        and standings == [["6", 2, tool.STANDING_RANK_TABLE, "TerritoryStandingPool", True, 1.25]] \
        and standing_table == {"experience": [150, 375, 600], "xpReward": [0, 100, 150], "cards": cards}
    print(f"{'ok  ' if passed else 'FAIL'}  standing: the standing of the territory 6 (stage 1.8d, format 4): {standings} {standing_table}")
    failures += 0 if passed else 1
    # Stage QG4: the item level of a gear score: MaxEquippableGearScore and IgnoreItemRarity of each row of XPLevels (the row 64 admits 700).
    equip = run_ranks.document.get("equipGearScores") or []
    passed = status == 0 and len(equip) == 70 and equip[0] == [100, False] and equip[63] == [730, True] and equip[64] == [800, True] \
        and next(index for index, row in enumerate(equip) if row[0] >= 700) + 1 == 61
    print(f"{'ok  ' if passed else 'FAIL'}  equip-gear-scores: the cap of each level (stage QG4): {equip[:2]} ... {equip[63:65]}")
    failures += 0 if passed else 1
    # Stage QG1 (format 4): the territory cards with the values of the Territory Bonus Data.
    territory_cards = run_ranks.document.get("territoryCards")
    passed = status == 0 and territory_cards == [
        ["TB_GainStanding", "GainStanding", 1, 102, False, 0, 0.05000000074505806, 0.13500000536441803, 0.0024999999441206455],
        ["TB_PurchaseHouse", "HousePurchase", 10, 1, True, 1, 1.0, 0.0, 0.0]]
    print(f"{'ok  ' if passed else 'FAIL'}  territory-cards: the cards of the territory pools (stage QG1): {territory_cards}")
    failures += 0 if passed else 1
    for case in ("unknown-offer-card", "no-bonus-row", "no-xp-modifier"):
        status, kept, mining = run_ranks(False, card_case=case)
        passed = status == 1 and kept
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, old file {'kept' if kept else 'replaced'} (stage QG1)")
        failures += 0 if passed else 1
    # Stage QG7: the factions.
    status, kept, mining = run_ranks(False, faction_case="factions")
    factions = run_ranks.document.get("factions") or {}
    expected_rows = [[1, "Syndicate", "MerchantGuild", "SyndicateTokens", "SyndicateTokens", 4, 5, "FactionIntro_Syndicate_Recruitment",
                      "S_syndicatequest_1", FACTION_BASE["trials"][1], FACTION_BASE["rankUps"][1]],
                     [2, "Marauder", "ProcurerGuild", "MarauderTokens", "MarauderTokens", 4, 5, "FactionIntro_Marauders_Recruitment",
                      "S_marauderquest_1", FACTION_BASE["trials"][2], FACTION_BASE["rankUps"][2]],
                     [3, "Covenant", "ExplorerGuild", "CovenantTokens", "CovenantTokens", 4, 5, "FactionIntro_Covenant_Recruitment",
                      "S_covenantquest_1", FACTION_BASE["trials"][3], FACTION_BASE["rankUps"][3]]]
    passed = status == 0 and factions.get("change") == [2592000, 20, 0, 0, 50] and factions.get("rows") == expected_rows \
        and factions.get("rankTables", {}).get("MerchantGuild") == {"experience": [3000, 11000, 26000, 49000, 85000, 85000],
                                                                    "requiredLevel": [10, 20, 35, 50, 60, 61]} \
        and factions.get("rankTables", {}).get("CovenantTokens") == {"experience": [15000, 30000, 45000, 75000, 150000, 300000]}
    print(f"{'ok  ' if passed else 'FAIL'}  factions: the member factions (stage QG7): status {status}, {factions.get('rows')}")
    failures += 0 if passed else 1
    for case in ("faction-bad-lists", "faction-auto-rank-up"):
        status, kept, mining = run_ranks(False, faction_case=case)
        passed = status == 1 and kept
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: status {status}, old file {'kept' if kept else 'replaced'} (stage QG7)")
        failures += 0 if passed else 1
    status, kept, mining = run_ranks(False, standing_rank=3)
    passed = status == 1 and kept
    print(f"{'ok  ' if passed else 'FAIL'}  short-standing-table: a standing MaxLevel 3 with the ranks 0 to 2: status {status}, old file "
          f"{'kept' if kept else 'replaced'} (stage 1.8d)")
    failures += 0 if passed else 1
    status, kept, mining = run_ranks(True)
    passed = status == 1 and kept
    print(f"{'ok  ' if passed else 'FAIL'}  short-rank-source: status {status}, old file {'kept' if kept else 'replaced'}, Mining cap {mining}")
    failures += 0 if passed else 1
    status, kept, mining = run_ranks(False, thresholds=False)
    passed = status == 1 and kept
    print(f"{'ok  ' if passed else 'FAIL'}  no-equip-abilities: an attribute table with no column EquipAbilities: status {status}, old file "
          f"{'kept' if kept else 'replaced'} (stage 3.5b.2)")
    failures += 0 if passed else 1
    # Stage 2.2 (code review of stage 3.5b.2, finding LB-R7): an attribute table that does not cover the attribute range stops the tool.
    for case, missing in (("short-attribute-table", ("Focus", 100)), ("attribute-gap", ("Strength", 50))):
        status, kept, mining = run_ranks(False, missing=missing)
        focus = run_ranks.document.get("attributes", {}).get("abilities", {}).get("focus")
        passed = status == 1 and kept
        print(f"{'ok  ' if passed else 'FAIL'}  {case}: the table {missing[0]} with no row of the value {missing[1]}: status {status}, "
              f"old file {'kept' if kept else 'replaced'}, focus thresholds {focus}")
        failures += 0 if passed else 1
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
