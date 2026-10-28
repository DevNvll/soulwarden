// The corrections of the code review of stage QG2a (~/Games/soulwarden/work/evidence/quests/qg2a/code-review/FINDINGS.md), in stage QG3. Each
// case fails on the tree of stage QG2a and passes after the correction (the proof cases of the review, adapted). Made-up accounts, items,
// quests, and bits only.
//
//   QGR2A1  QG2R-R1: the empty category list of a known base row adds no kill or drop category (the vitals id of a variant, the base id
//           as a drop source); the base id still counts for a kill; a row of a file of before keeps the vitals row; B gets nothing (MP-10)
//   QGR2A2  QG2R-R2: the manifest applies the location rule only to the tasks whose runtime path tests it (not TaskAchievement, not
//           TaskTriggerArea), and still to a counter of an event (a screen)
//   QGR2A3  QG2R-R3: a loot container that the runtime refuses to open (a salvage event with an unpaid reward) is no source of its items
//   QGR2A4  QG2R-R4: a fractional CurrencyReward stays unpaid, and the open of its purse changes nothing
//   QGR2A5  QG2R-R5: a negative and a malformed AzothReward stay unpaid; a valid amount keeps the rule of the client

#include <algorithm>
#include <string>
#include <vector>

#include "gamedata/game_event.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_person_fixture.h"
#include "world/item_containers.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::json;

json review_document() {
    return {{"format", 1},
            {"objectives", json::array()},
            {"tasks", json::object()},
            {"achievements", json::array({json::array({"Review_Req", 1600}), json::array({"Review_Done", 1602})})},
            {"npa", json::array()},
            {"territories", json::array()},
            {"backstories", json::array()}};
}

void add_task(json& document, const char* id, const char* type, unsigned code, json cells) {
    document["tasks"][gamedata::lower_text(id)] = questfix::task(id, type, code, std::move(cells));
}

void add_objective(json& document, const char* id, const char* leaf, bool push = true) {
    json cells{{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}};
    if (push) {
        cells["PushObjectiveOnReqMet"] = "1";
    }
    document["objectives"].push_back(
        {{"id", id}, {"cells", cells}, {"tree", json::array({json::array({0, gamedata::lower_text(leaf), json::array()})})}});
}

const json& manifest_row(const json& manifest, const char* id) {
    for (const json& row : manifest["objectives"]) {
        if (row["id"] == id) {
            return row;
        }
    }
    throw std::runtime_error(std::string("no manifest row ") + id);
}

/** QGR2A1: an empty category list of a known base row. */
void empty_categories(const Options&) {
    GatherShop shop;
    std::vector<gamedata::ItemDefinition> items = gathering_items();
    items.push_back(chests::chest_item("Review_BaseTrophy", "Resource", "Resource", 2, 10));
    shop.host.catalog.set(items);
    json document = review_document();
    add_task(document, "Review_Kill", "taskkillcontribution", 13, {{"KillEnemyType", "Test_BagWolf"}, {"TargetQty", "2"}});
    add_task(document, "Review_Drop", "taskhaveitems", 25,
             {{"ItemName", "Review_BaseTrophy"}, {"TargetQty", "1"}, {"ItemDropVC", "Test_BagWolf"}, {"ItemDropProbability", "100"}});
    add_objective(document, "Review_Kill", "Review_Kill");
    add_objective(document, "Review_Drop", "Review_Drop");
    check(shop.host.quests.load_text(document.dump()), "control: the quest data of the case loads");
    gamedata::LootData loot = gathering_loot();
    gamedata::LootVitals base;
    base.id = "Test_BagWolf";
    base.creatureType = "Solo";
    loot.bases = {base};  // a known base row with the member VitalsCategories, an empty list
    loot.vitals = {{"test_bagwolf", 0}};
    check(shop.host.loot.set(loot), "control: the loot data with an empty base category list");
    const std::vector<std::string> variant = shop.host.loot.kill_categories(&base, "Review_Variant", "Test_BagWolf_Category", true);
    check(std::find(variant.begin(), variant.end(), "review_variant") == variant.end()
              && std::find(variant.begin(), variant.end(), "test_bagwolf") != variant.end(),
          "an empty base list adds no vitals id of a variant; the kill keeps the base id (0x3B1A057)");
    gamedata::LootVitals before = base;
    before.hasCategories = false;
    const std::vector<std::string> old = shop.host.loot.kill_categories(&before, "Review_Variant", "Test_BagWolf_Category", false);
    check(std::find(old.begin(), old.end(), "review_variant") != old.end(),
          "a row of a file of before (no member VitalsCategories, 9 members) keeps the vitals row as its category");
    gamedata::SpawnPoint point;
    point.id = "Review_Wolf_Point";
    point.kind = 0;
    point.level = 20;
    point.position = bags::kKillPlace;
    check(shop.host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind()}, {point}, {{20, bags::kWolfHealth, 0, 0}}),
          "control: the wolf has a spawn category that is not its base id");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(questfix::quest_of(a, "Review_Kill") != nullptr && questfix::quest_of(a, "Review_Drop") != nullptr, "control: A has both quests (push)");
    (void)shop.dead_wolf();
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    const world::QuestInstance* kill = questfix::quest_of(a, "Review_Kill");
    check(kill != nullptr && kill->tasks[0].progress == 1, "the base vitals id still gives the kill credit");
    check(world::Inventory::count(a, "Review_BaseTrophy") == 0, "an empty category list does not make the base vitals id a drop category (0x5C758FE)");
    check(world::Inventory::count(b, "Review_BaseTrophy") == 0, "B (not credited) has no drop (MP-10)");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    check(manifest_row(manifest, "Review_Drop")["tasks"][0]["supported"] == false, "the manifest has no base-id drop source either");
}

/** QGR2A2: the scope of the location rule in the manifest. */
void location_scope(const Options&) {
    questperson::QuestShop shop;
    json document = review_document();
    const json location = {{"EnforceLocationRequirement", "1"}, {"POITag", "Review_No_Poi"}};
    json achievement = location;
    achievement["ItemName"] = "Review_Req";
    add_task(document, "Review_Achievement", "taskachievement", 18, achievement);
    json trigger = location;
    trigger["TP_TargetTag"] = "Qb_Trigger";
    trigger["TriggerAreaType"] = "Enter";
    add_task(document, "Review_Trigger", "tasktriggerarea", 3, trigger);
    json screen = location;
    screen["UIScreenTag"] = "Inventory";
    screen["TargetQty"] = "1";
    add_task(document, "Review_Event", "taskopenuiscreen", 62, screen);
    add_objective(document, "Review_Achievement", "Review_Achievement");
    add_objective(document, "Review_Trigger", "Review_Trigger");
    add_objective(document, "Review_EventQuest", "Review_Event", false);
    check(shop.host.quests.load_text(document.dump()), "control: the quest data of the case loads");
    shop.enter();
    world::Entity& a = *shop.host.world.find_character(kCharacterA);
    world::Quests& quests = shop.host.world.quests();
    check(shop.host.world.ownership().unlock_achievement(a, "Review_Req", 1600), "control: A gets the achievement");
    shop.walk_to(kCharacterA, questperson::kTriggerPlace);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(quests.ready(*questfix::quest_of(a, "Review_Achievement")) && quests.ready(*questfix::quest_of(a, "Review_Trigger")),
          "control: the runtime completes the achievement task and the trigger task with no POI shape");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    for (const char* id : {"Review_Achievement", "Review_Trigger"}) {
        const json& fact = manifest_row(manifest, id)["tasks"][0];
        check(fact["supported"] == true, std::string("the manifest agrees with the runtime for ") + id + " (the location rule is not its path)");
    }
    bool located = false;
    for (const json& reason : manifest_row(manifest, "Review_EventQuest")["blocked"]) {
        located = located || reason.get<std::string>().find("the location rule (EnforceLocationRequirement)") != std::string::npos;
    }
    check(located, "a counter of an event (TaskGameEvent: Quests::matched_tasks) still has the location rule");
}

/** QGR2A3: a container that the runtime refuses is no item source of the manifest. */
void refused_container(const Options&) {
    GatherShop shop;
    std::vector<gamedata::ItemDefinition> items = gathering_items();
    items.push_back(chests::chest_item("Review_ClosedBox", "Resource", "Resource+LootContainer", 2, 10));
    items.push_back(chests::chest_item("Review_OnlyInside", "Resource", "Resource", 2, 10));
    shop.host.catalog.set(items);
    gamedata::LootData loot = gathering_loot();
    using gamedata::LootEntryKind;
    loot.tables.push_back(chests::loot_table("Review_Outer", true, 0, {}, {chests::loot_entry(LootEntryKind::item, "Review_ClosedBox", 0)}));
    const std::size_t outer = loot.tables.size() - 1;
    loot.tables.push_back(chests::loot_table("Review_Inner", true, 0, {}, {chests::loot_entry(LootEntryKind::item, "Review_OnlyInside", 0)}));
    loot.containerRoots = {{"review_closedbox", "Review_Inner"}};
    loot.containerEvents = {{"review_closedbox", "Review_Entitlement"}};
    loot.salvageEvents = {{"review_entitlement", gamedata::read_game_event("Review_Entitlement", {{"EntitlementId", "Review_Skin"}})}};
    gamedata::LootVitals base;
    base.id = "Test_BagWolf";
    base.creatureType = "Solo";
    base.canDropLoot = true;
    base.table = static_cast<std::uint32_t>(outer);
    loot.bases = {base};
    loot.vitals = {{"test_bagwolf", 0}};
    check(shop.host.loot.set(loot), "control: the world loot gives a box with a table and an unpaid salvage event");
    gamedata::SpawnPoint point;
    point.id = "Review_Box_Point";
    point.kind = 0;
    point.level = 20;
    point.position = bags::kKillPlace;
    check(shop.host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind()}, {point}, {{20, bags::kWolfHealth, 0, 0}}),
          "control: a placed creature can drop the box");
    json document = review_document();
    add_task(document, "Review_OnlyInside", "taskhaveitems", 25, {{"ItemName", "Review_OnlyInside"}, {"TargetQty", "1"}});
    add_objective(document, "Review_OnlyInside", "Review_OnlyInside");
    check(shop.host.quests.load_text(document.dump()), "control: the quest needs only the item in the box");
    check(!shop.host.world.item_containers().openable("Review_ClosedBox"), "control: the runtime refuses to open the box");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    check(manifest_row(manifest, "Review_OnlyInside")["tasks"][0]["supported"] == false,
          "a box whose salvage event has an unpaid reward is no source of the items of its table");
}

/** QGR2A4: a fractional CurrencyReward. */
void fractional_coin(const Options&) {
    GatherShop shop;
    std::vector<gamedata::ItemDefinition> items = gathering_items();
    items.push_back(chests::chest_item("Review_Purse", "Resource", "Resource+LootContainer", 2, 10));
    shop.host.catalog.set(items);
    gamedata::LootData loot = gathering_loot();
    loot.containerEvents = {{"review_purse", "Review_Event"}};
    const gamedata::QuestGameEvent event = gamedata::read_game_event("Review_Event", {{"CurrencyReward", "2500.75"}});
    loot.salvageEvents = {{"review_event", event}};
    check(shop.host.loot.set(loot), "control: the purse with a fractional reward");
    check(!event.unpaid.empty() && event.coin == 0, "a fractional CurrencyReward stays an unpaid reward (not 2500)");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const world::ItemResult given = shop.host.world.inventory().give(kCharacterA, "Review_Purse", 1);
    check(given.ok() && !given.slots.empty(), "control: A has the purse");
    const std::uint32_t slot = given.slots.front();
    const auto before = a.wallet.coin;
    const world::OpenResult result = shop.host.world.item_containers().open(a, slot, a.bag.slots[slot].item->instance, 1);
    check(!result.ok() && a.wallet.coin == before && world::Inventory::count(a, "Review_Purse") == 1,
          "the open of the purse is refused with no payment and no take");
    check(gamedata::read_game_event("Review_Whole", {{"CurrencyReward", "2500"}}).coin == 2500, "control: a whole CurrencyReward pays");
}

/** QGR2A5: a negative and a malformed AzothReward. */
void invalid_azoth(const Options&) {
    for (const char* text : {"-0.5", "37junk", "-3"}) {
        const gamedata::QuestGameEvent event = gamedata::read_game_event("Review_Azoth", {{"AzothReward", text}});
        check(!event.unpaid.empty() && event.azoth == 0, std::string("AzothReward ") + text + " stays unpaid");
    }
    check(gamedata::azoth_amount("37.5") == 37U && gamedata::azoth_amount("0") == 0U && gamedata::azoth_amount("70000") == 4464U,
          "control: a valid amount keeps the rule of the client (the truncation, the low 16 bits)");
}

} // namespace

std::vector<Case> review_qg2a_cases() {
    return {{"QGR2A1", "review of QG2a, QG2R-R1: an empty base category list adds no kill or drop category; two sessions", empty_categories},
            {"QGR2A2", "review of QG2a, QG2R-R2: the location rule of the manifest only for the tasks whose runtime path tests it", location_scope},
            {"QGR2A3", "review of QG2a, QG2R-R3: a container that the runtime refuses is no item source of the manifest", refused_container},
            {"QGR2A4", "review of QG2a, QG2R-R4: a fractional CurrencyReward stays unpaid", fractional_coin},
            {"QGR2A5", "review of QG2a, QG2R-R5: a negative or malformed AzothReward stays unpaid", invalid_azoth}};
}

} // namespace soulwarden::backend::tests
