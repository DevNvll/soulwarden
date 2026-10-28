// The case of stage QG2a for the support manifest (world/quest_support.h; docs/systems/quests.md, the support manifest): the corrected
// reasons (a blank item of a template, a weapon filter, a pin task, a never-true tag, TaskHaveLevel of a faction), no QuestArcTag gate,
// the name of RequiredGameModeId, the push grant source (decision B), TaskHaveLevel in a history, and the category rule of the source index.
// Made-up data only.
//
//   QG2D  the manifest of the rules of stage QG2a

#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_person_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using questfix::json;

json gap_manifest_document() {
    json tasks = json::object();
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id, type, code, cells);
    };
    add("Gm_Unlock", "taskunlockachievement", 20, {{"ItemName", "Gm_Bit"}});
    add("Gm_TriggerBlank", "tasktriggerarea", 3, {{"TriggerAreaType", "Enter"}, {"OverrideLocation", "1 2 3"}});
    add("Gm_TriggerNever", "tasktriggerarea", 3, {{"TP_TargetTag", "nvere true"}, {"TriggerAreaType", "Enter"}});
    add("Gm_EventBlank", "taskgameevent", 14, {{"TargetQty", "1"}, {"InteractTag", "neverbetrue"}});
    add("Gm_ItemBlank", "taskhaveitems", 25, {{"TargetQty", "3"}});
    add("Gm_GiveBlank", "taskgiveitem", 33, {{"TargetQty", "1"}});
    add("Gm_Weapon", "taskkillcontribution", 13, {{"KillEnemyType", "Test_BagWolf_Category"}, {"KillEnemyWeaponID", "BowAbilityTable"}});
    add("Gm_Turkey", "taskkillcontribution", 13, {{"KillEnemyType", "Test_BagWolf_Category"}, {"KillEnemyWeaponID", "Turkey"}});
    add("Gm_KillParent", "taskkillcontribution", 13, {{"KillEnemyType", "Test_WolfPack"}, {"TargetQty", "2"}});
    add("Gm_LevelFaction", "taskhavelevel", 48, {{"TargetQty", "3000"}, {"ItemName", "Covenant"}, {"ShouldCheckPoints", "1"}});
    add("Gm_Level3", "taskhavelevel", 48, {{"TargetQty", "3"}});
    add("Gm_Level999", "taskhavelevel", 48, {{"TargetQty", "999"}});
    const auto one = [](const char* id, json cells, const char* task) {
        cells["NpcDestinationId"] = "Test_Artificer";
        if (!cells.contains("Type")) {
            cells["Type"] = "Journey";
        }
        return json{{"id", id}, {"cells", cells}, {"tree", json::array({json::array({0, task, json::array()})})}};
    };
    json objectives = json::array();
    objectives.push_back(one("Gm_TriggerBlank", json::object(), "gm_triggerblank"));
    objectives.push_back(one("Gm_TriggerNever", json::object(), "gm_triggernever"));
    objectives.push_back(one("Gm_EventBlank", json::object(), "gm_eventblank"));
    objectives.push_back(one("Gm_TemplateItem", {{"Type", "Mission"}}, "gm_itemblank"));
    objectives.push_back(one("Gm_ObjectiveItem", json::object(), "gm_giveblank"));
    objectives.push_back(one("Gm_Weapon", json::object(), "gm_weapon"));
    objectives.push_back(one("Gm_Turkey", json::object(), "gm_turkey"));
    objectives.push_back(one("Gm_KillParent", json::object(), "gm_killparent"));
    objectives.push_back(one("Gm_LevelFaction", json::object(), "gm_levelfaction"));
    objectives.push_back(one("Gm_Arc", {{"QuestArcTag", "TestArc"}, {"PushObjectiveOnReqMet", "1"}}, "gm_unlock"));
    objectives.push_back(one("Gm_GameMode", {{"RequiredGameModeId", "TestMode"}}, "gm_unlock"));
    objectives.push_back(one("01_SoleSurvivor2", {{"PushObjectiveOnReqMet", "1"}}, "gm_unlock"));
    objectives.push_back(one("Gm_PushTerritory", {{"PushObjectiveOnReqMet", "1"}, {"ExclusiveTerritory", "12"}}, "gm_unlock"));
    objectives.push_back(one("Gm_PushLevel", {{"PushObjectiveOnReqMet", "1"}, {"AchievementId", "Gm_Level_Done"}}, "gm_level3"));
    objectives.push_back(one("Gm_PushHigh", {{"PushObjectiveOnReqMet", "1"}}, "gm_level999"));
    json achievements = json::array();
    std::uint32_t bit = 760;
    for (const char* id : {"Gm_Bit", "Gm_Level_Done", "Archetype_Mercenary"}) {
        achievements.push_back(json::array({id, bit++}));
    }
    return {{"format", 1},
            {"objectives", objectives},
            {"tasks", tasks},
            {"achievements", achievements},
            {"territories", json::array()},
            {"backstories", json::array({{{"id", "Archetype_Soldier"}, {"objectives", json::array()}, {"achievements", json::array({"Archetype_Mercenary"})},
                                          {"archetype", true}}})}};
}

/** QG2D: the support manifest of the rules of stage QG2a. */
void gap_manifest(const Options&) {
    questperson::QuestShop shop;
    check(shop.host.quests.load_text(gap_manifest_document().dump()), "control: the manifest data loads");
    // The creature of a spawn point (category Test_BagWolf_Category) with the parents of the loot data.
    gamedata::SpawnPoint point;
    point.id = "Gm_Wolf_Point";
    point.kind = 0;
    point.level = 20;
    point.position = {300.0F, 300.0F, 30.0F};
    check(shop.host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind()}, {point}, {{20, bags::kWolfHealth, 0.0F, 0.0F}}),
          "control: the creature data with one spawn point");
    gamedata::LootData loot = gather::gathering_loot();
    loot.categoryGroups = {{"test_bagwolf_category", "test_wolfpack"}};
    check(shop.host.loot.set(loot), "control: the loot data with the parent of the category");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest(support.default_profiles());
    note("QG2D counts: " + manifest["counts"].dump());
    const auto row = [&](const char* id) -> const json& {
        for (const char* list : {"objectives", "missionTemplates"}) {
            for (const json& entry : manifest[list]) {
                if (entry["id"] == id) {
                    return entry;
                }
            }
        }
        throw std::runtime_error("no row " + std::string(id));
    };
    const auto first = [&](const char* id) { return row(id)["blocked"].empty() ? std::string{} : row(id)["blocked"][0].get<std::string>(); };
    const auto has = [&](const char* id, const char* text) { return first(id).find(text) != std::string::npos; };
    note("QG2D reasons: " + first("Gm_TriggerBlank") + " | " + first("Gm_TriggerNever") + " | " + first("Gm_EventBlank") + " | " + first("Gm_TemplateItem")
         + " | " + first("Gm_ObjectiveItem") + " | " + first("Gm_Weapon") + " | " + first("Gm_Turkey") + " | " + first("Gm_LevelFaction"));
    check(has("Gm_TriggerBlank", "has no TP_TargetTag") && has("Gm_TriggerNever", "never-true tag") && has("Gm_EventBlank", "names no game event"),
          "a trigger task with no tag, a never-true tag, and a game event task with no event have their own reasons");
    check(has("Gm_TemplateItem", "the mission instance of a template gives it") && has("Gm_ObjectiveItem", "no ItemName (a blank cell"),
          "a blank item of a template and of an objective: not an item that the catalog does not have");
    check(has("Gm_Weapon", "is a weapon filter") && has("Gm_Turkey", "names no item and no ability table"),
          "KillEnemyWeaponID: an ability table is a weapon filter; Turkey names neither");
    check(has("Gm_LevelFaction", "TaskHaveLevel of the categorical progression Covenant (points 3000)"),
          "TaskHaveLevel of a faction progression: the faction has no owner");
    check(row("Gm_KillParent")["tasks"][0]["supplier"] == "the kills of the category test_wolfpack",
          "the source index has the parent of the spawn category (the rule of the kill credit)");
    check(row("Gm_Arc")["gates"]["blocked"].empty() && row("Gm_Arc")["supported"] == true,
          "QuestArcTag is no gate: the row with a tag (and a push) is supported");
    check(!row("Gm_GameMode")["gates"]["blocked"].empty()
              && row("Gm_GameMode")["gates"]["blocked"][0].get<std::string>().find("RequiredGameModeId TestMode is false: no game mode is active") == 0,
          "RequiredGameModeId is false with its name: no game mode is active in the open world");
    const json& old = row("01_SoleSurvivor2")["grants"][0];
    const json& territory = row("Gm_PushTerritory")["grants"][0];
    check(old["source"] == "push" && old["supported"] == false && old["reason"].get<std::string>().find("decision B") != std::string::npos
              && territory["supported"] == false && territory["reason"].get<std::string>().find("ExclusiveTerritory") != std::string::npos
              && row("Gm_PushLevel")["grants"][0]["supported"] == true,
          "the push grant source: decision B leaves out 01_SoleSurvivor2; a row with ExclusiveTerritory needs the territory; a push row runs");
    const json& newCharacter = manifest["profiles"][0];
    bool level = false;
    bool arc = false;
    for (const json& played : newCharacter["played"]) {
        level = level || played == "Gm_PushLevel";
        arc = arc || played == "Gm_Arc";
    }
    check(level && arc && row("Gm_PushHigh")["supported"] == true && row("Gm_PushHigh")["primary"]["played"] == false
              && row("Gm_PushHigh")["primary"]["cause"].get<std::string>().find("of the history is below 999") != std::string::npos,
          "the new character (level 6) gets the push rows and completes TaskHaveLevel 3; TaskHaveLevel 999 waits with its cause");
    const json& reasons = manifest["counts"]["firstReasonOfTheOthers"];
    check(reasons.contains("task: a weapon filter of a kill (KillEnemyWeaponID: an ability table)")
              && reasons.contains("task: a pin or a puzzle task with no trigger tag (never completes)")
              && reasons.contains("task: TaskHaveLevel of a faction progression (no owner)"),
          "the counts of the first reasons have the new kinds");
}

} // namespace

std::vector<Case> quest_gap_support_cases() {
    return {{"QG2D", "quests QG2a (I4, I5, I7): the manifest: the corrected reasons, no arc gate, RequiredGameModeId, the push source, the level",
             gap_manifest}};
}

} // namespace soulwarden::backend::tests
