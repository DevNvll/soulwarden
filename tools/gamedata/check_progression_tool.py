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

Usage: check_progression_tool.py
Status 0: each case has the expected result. Each line starts with "ok" or "FAIL".
"""

from __future__ import annotations

import sys
from pathlib import Path

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
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
