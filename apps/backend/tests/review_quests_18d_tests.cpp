#include <sqlite3.h>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "support/cases.h"
#include "support/check.h"
#include "support/quest_person_fixture.h"
#include "world/item_containers.h"
#include "world/skills.h"

// The cases of the corrections of the code review of stage 1.8d (stage 1.8e; ~/Games/soulwarden/work/evidence/quests/18d/code-review/
// FINDINGS.md, findings QR8D-R1 to QR8D-R6; QR8D-R7 is the correction of case QD7). Each case fails on the tree of stage 1.8d and passes
// after its correction. QRDC, QRDW, and QRDM are controls of the review (a container transaction, the standing bytes, schema 16). The data
// and the identities are made up.

namespace soulwarden::backend::tests {
namespace {

using namespace questperson;

json node(unsigned index, const char* task, json children = json::array()) {
    return json::array({index, task, children});
}

void add_objective(json& quests, const char* id, json cells, json tree) {
    cells["Type"] = "Journey";
    quests["objectives"].push_back({{"id", id}, {"cells", cells}, {"tree", tree}});
}

/** @return The bag slot and the instance of the pouch of A. */
std::pair<std::uint32_t, world::InstanceId> pouch_of(QuestShop& shop) {
    const world::Entity* a = shop.entity(kCharacterA);
    for (std::uint32_t slot = 0; slot < a->bag.slots.size(); ++slot) {
        if (a->bag.slots[slot].item && a->bag.slots[slot].item->itemId == "QbPouch") {
            return {slot, a->bag.slots[slot].item->instance};
        }
    }
    throw std::runtime_error("control: no pouch in the bag");
}

std::uint32_t gear_score_of(const world::Entity& entity, const char* item) {
    for (const auto& slot : entity.bag.slots) {
        if (slot.item && slot.item->itemId == item) {
            return slot.item->gearScore;
        }
    }
    return 0;
}

bool run_sql(const std::string& path, const char* command) {
    sqlite3* database = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &database) == SQLITE_OK && sqlite3_exec(database, command, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(database);
    return ok;
}

/** QRD1 (QR8D-R1): two return tasks of one item need the sum of their counts at the hand-in. */
void aggregate_returns(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    quests["tasks"]["rd_return_a"] = questfix::task("Rd_Return_A", "taskhaveandreturnitems", 26, {{"ItemName", "QbRelic"}, {"TargetQty", "2"}});
    quests["tasks"]["rd_return_b"] = questfix::task("Rd_Return_B", "taskgatherandreturnitems", 30, {{"ItemName", "QbRelic"}, {"TargetQty", "2"}});
    quests["tasks"]["rd_root"] = questfix::task("Rd_Root", "simpletaskcontainer", 0);
    add_objective(quests, "Rd_Returns", {{"SuccessGameEventId", "Qb_Paid_Success"}},
                  json::array({node(0, "rd_return_a"), node(1, "rd_return_b"), node(2, "rd_root", json::array({0, 1}))}));
    check(shop.host.quests.load_text(quests.dump()), "control: two return tasks of one item load");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Inventory& inventory = shop.host.world.inventory();
    check(inventory.give(kCharacterA, "QbRelic", 4).ok(), "control: A has the four items of the two tasks");
    world::Quests& system = shop.host.world.quests();
    const world::QuestResult grant = system.grant(*a, "Rd_Returns", "command", 0);
    check(grant.ok() && system.ready(*questfix::quest_of(*a, "Rd_Returns")), "control: both return tasks are satisfied");
    world::Inventory::Plan take = inventory.plan_quest_items(kCharacterA, {{false, {"QbRelic", 2, std::nullopt, {}}}}, nullptr);
    check(take.ok() && inventory.write(take, std::nullopt), "control: two of the items go");
    (void)inventory.finish(take, "a case of the review");
    check(shop.count(kCharacterA, "QbRelic") == 2, "control: two of the four items stay");
    const std::uint64_t experience = a->progression.experience;
    const world::HandInResult paid = system.hand_in(*a, grant.instance);
    note("QRD1: accepted " + std::to_string(paid.ok()) + ", experience " + std::to_string(a->progression.experience - experience) + ", items "
         + std::to_string(shop.count(kCharacterA, "QbRelic")) + " (" + paid.detail + ")");
    check(paid.error == world::QuestError::missingReturnItems && a->progression.experience == experience && shop.count(kCharacterA, "QbRelic") == 2
              && questfix::quest_of(*a, "Rd_Returns") != nullptr,
          "the hand-in needs the sum of the return counts (4): with 2 it is refused, nothing is paid, and the quest stays");
    check(shop.count(kCharacterB, "QbPants") == 0, "B gets no reward");
    check(inventory.give(kCharacterA, "QbRelic", 2).ok() && system.hand_in(*a, grant.instance).ok() && shop.count(kCharacterA, "QbRelic") == 0,
          "with 4 items the hand-in takes all 4 and pays");
}

/** QRD2 (QR8D-R2): the open of a loot container item rolls as a loot container (the container floor), not as a creature. */
void container_context(const Options&) {
    QuestShop shop;
    std::vector<gamedata::ItemDefinition> catalog = quest_items();
    for (gamedata::ItemDefinition& item : catalog) {
        if (item.id == "TestSword") {
            item.minGearScore = 1;
            item.maxGearScore = 300;
        }
    }
    shop.host.catalog.set(std::move(catalog));
    gamedata::LootData loot = chests::chest_loot();
    loot.tables.push_back(chests::loot_table("Rd_Container", true, 1, {}, {chests::loot_entry(gamedata::LootEntryKind::item, "TestSword", 0)}, true));
    loot.containerRoots = {{"qbpouch", "Rd_Container"}};
    check(shop.host.loot.set(std::move(loot)), "control: a container table with UseLevelGS loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::LootContext context;
    context.characterLevel = context.creatureLevel = a->progression.level;
    context.container = true;
    const std::optional<std::uint32_t> root = shop.host.loot.container_table("QbPouch");
    const std::vector<world::LootDrop> control = shop.host.world.rewards().roll_root(*root, context);
    check(control.size() == 1 && control[0].gearScore == chests::kFloor, "control: the roll of a loot container gives the floor 250");
    check(shop.host.world.inventory().give(kCharacterA, "QbPouch", 1).ok(), "control: A has one pouch");
    const auto [slot, instance] = pouch_of(shop);
    const world::OpenResult result = shop.host.world.item_containers().open(*a, slot, instance, 1);
    note("QRD2: the gear score of the item " + std::to_string(gear_score_of(*a, "TestSword")) + ", the floor " + std::to_string(chests::kFloor));
    check(result.ok() && gear_score_of(*a, "TestSword") >= chests::kFloor, "the open of an item container has the container floor of the data");
}

/** QRD3 (QR8D-R3): a daily repeat with no completion achievement pays one time in a cycle. */
void repeat_without_bit(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    add_objective(quests, "Rd_Daily_NoBit", {{"Repeatable", "TRUE"}, {"ScheduleId", "Daily"}, {"SuccessGameEventId", "Qb_Paid_Success"}},
                  json::array({node(0, "qb_ready_unlock")}));
    quests["schedules"] = json::array({{{"id", "Daily"}, {"cells", {{"StartDate", "*-*-*TDailyCooldown"}}}}});
    check(shop.host.quests.load_text(quests.dump()), "control: a daily repeat with no completion achievement loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& system = shop.host.world.quests();
    const gamedata::QuestObjective* row = shop.host.quests.objective("Rd_Daily_NoBit");
    const world::QuestResult first = system.accept(*a, *row, 0);
    check(first.ok() && system.hand_in(*a, first.instance).ok(), "control: the first daily hand-in pays");
    const std::uint64_t experience = a->progression.experience;
    const world::QuestResult second = system.accept(*a, *row, 0);
    const bool twice = second.ok() && system.hand_in(*a, second.instance).ok();
    note("QRD3: second paid " + std::to_string(twice) + ", repeat rows " + std::to_string(shop.host.store.list_quest_repeats(kCharacterA).size()));
    check(second.error == world::QuestError::onCooldown && a->progression.experience == experience
              && shop.host.store.list_quest_repeats(kCharacterA).size() == 1,
          "the second accept in the same cycle is refused (OnCooldown) and the hand-in wrote its repeat row");
    check(shop.entity(kCharacterB)->quests.repeats.empty() && system.available(*shop.entity(kCharacterB), *row) == world::QuestError::none,
          "B has no repeat row: the objective is available to B");
}

/** QRD4 (QR8D-R4): a refine that reaches a craft task keeps the smallest gear score of the task. */
void refine_minimum(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    quests["tasks"]["rd_craft"] = questfix::task("Rd_Craft", "taskcraftrecipe", 38, {{"CraftingRecipeId", "Rd_Recipe"}, {"MinGearScore", "250"}, {"TargetQty", "1"}});
    add_objective(quests, "Rd_Craft_Quest", {}, json::array({node(0, "rd_craft")}));
    check(shop.host.quests.load_text(quests.dump()), "control: a craft task with a smallest gear score loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& system = shop.host.world.quests();
    check(system.grant(*a, "Rd_Craft_Quest", "command", 0).ok(), "control: the craft task is active");
    world::QuestEvent event;
    event.kind = world::QuestEventKind::craft;
    event.keys = {"Rd_Recipe"};
    event.gearScore = 100;
    system.event(*a, event);
    check(questfix::quest_of(*a, "Rd_Craft_Quest")->tasks[0].progress == 0, "control: a craft below 250 counts nothing");
    event.kind = world::QuestEventKind::refine;
    system.event(*a, event);
    note("QRD4: the progress after a refine of gear score 100: " + std::to_string(questfix::quest_of(*a, "Rd_Craft_Quest")->tasks[0].progress));
    check(questfix::quest_of(*a, "Rd_Craft_Quest")->tasks[0].progress == 0, "a refine below 250 counts nothing for the craft task");
    event.gearScore = 250;
    system.event(*a, event);
    check(questfix::quest_of(*a, "Rd_Craft_Quest") == nullptr || questfix::quest_of(*a, "Rd_Craft_Quest")->tasks[0].progress == 1,
          "a refine of gear score 250 counts");
}

/** QRD5 (QR8D-R5): a roll that gives a loot container that cannot open refuses the whole open. */
void nested_container(const Options&) {
    QuestShop shop;
    std::vector<gamedata::ItemDefinition> items = quest_items();
    items.push_back(chests::chest_item("Rd_InnerPouch", "Resource", "Resource+LootContainer", 1, 100));
    shop.host.catalog.set(std::move(items));
    gamedata::LootData loot = chests::chest_loot();
    loot.tables.push_back(chests::loot_table("Rd_Nested", true, 1, {},
                                             {chests::loot_entry(gamedata::LootEntryKind::item, "Rd_InnerPouch", 0),
                                              chests::loot_entry(gamedata::LootEntryKind::item, "QbPotion", 0)}));
    loot.containerRoots = {{"qbpouch", "Rd_Nested"}};
    check(shop.host.loot.set(std::move(loot)), "control: the outer container has a table, the inner one has none");
    shop.enter();
    check(shop.host.world.inventory().give(kCharacterA, "QbPouch", 1).ok(), "control: A has the outer container");
    check(!shop.host.world.item_containers().openable("Rd_InnerPouch"), "control: the inner container cannot open");
    const auto [slot, instance] = pouch_of(shop);
    const world::OpenResult result = shop.host.world.item_containers().open(*shop.entity(kCharacterA), slot, instance, 1);
    note("QRD5: accepted " + std::to_string(result.ok()) + ", outer " + std::to_string(shop.count(kCharacterA, "QbPouch")) + ", inner "
         + std::to_string(shop.count(kCharacterA, "Rd_InnerPouch")) + ", potion " + std::to_string(shop.count(kCharacterA, "QbPotion")));
    check(result.error == world::OpenError::innerNotOpenable && shop.count(kCharacterA, "QbPouch") == 1 && shop.count(kCharacterA, "Rd_InnerPouch") == 0
              && shop.count(kCharacterA, "QbPotion") == 0,
          "the open is refused as a whole before the take: the outer container stays, no item comes");
}

/** QRD6 (QR8D-R6): a reward range with text after its number is not a range: the hand-in is refused. */
void malformed_range(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    quests["events"]["qb_paid_success"]["GearScoreRange"] = "100-200junk";
    check(shop.host.quests.load_text(quests.dump()), "control: the event with a malformed range loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& system = shop.host.world.quests();
    const world::QuestResult grant = system.grant(*a, "Qb_Paid", "command", 0);
    check(grant.ok(), "control: the reward quest is ready");
    const std::uint64_t experience = a->progression.experience;
    const world::HandInResult result = system.hand_in(*a, grant.instance);
    note("QRD6: accepted " + std::to_string(result.ok()) + " (" + result.detail + ")");
    check(result.error == world::QuestError::cannotPay && a->progression.experience == experience && questfix::quest_of(*a, "Qb_Paid") != nullptr,
          "GearScoreRange 100-200junk refuses the whole hand-in");
}

/** QRDC (control of the review): a late write failure of an open restores everything; the retry pays one time. */
void container_rollback(const Options& options) {
    const std::string path = (options.workDirectory / "qrdc-container.sqlite3").string();
    std::filesystem::remove(path);
    QuestShop shop(0, path);
    gamedata::LootData loot = chests::chest_loot();
    loot.tables.push_back(chests::loot_table("Rd_Control", true, 1, {}, {chests::loot_entry(gamedata::LootEntryKind::item, "QbPotion", 0, 5, 5)}));
    loot.containerRoots = {{"qbpouch", "Rd_Control"}};
    check(shop.host.loot.set(std::move(loot)), "control: the pouch table loads");
    shop.enter();
    check(shop.host.world.inventory().give(kCharacterA, "QbPouch", 1).ok(), "control: one pouch is in the bag");
    const auto [slot, instance] = pouch_of(shop);
    check(run_sql(path, "CREATE TRIGGER qrdc_fail BEFORE INSERT ON items BEGIN SELECT RAISE(FAIL,'case'); END"), "control: a late item insert fails");
    world::ItemContainers& containers = shop.host.world.item_containers();
    world::Entity* a = shop.entity(kCharacterA);
    const world::OpenResult failed = containers.open(*a, slot, instance, 1);
    bool storedPouch = false;
    for (const auto& row : shop.host.store.list_items(kCharacterA)) {
        storedPouch = storedPouch || row.itemId == "QbPouch";
    }
    check(failed.itemError == world::ItemError::storeFailed && storedPouch && shop.count(kCharacterA, "QbPouch") == 1 && shop.count(kCharacterA, "QbPotion") == 0,
          "a late write failure keeps each stored row and changes no entity");
    check(run_sql(path, "DROP TRIGGER qrdc_fail"), "control: the writes work again");
    const world::OpenResult paid = containers.open(*a, slot, instance, 1);
    const world::OpenResult duplicate = containers.open(*a, slot, instance, 1);
    check(paid.ok() && !duplicate.ok() && shop.count(kCharacterA, "QbPotion") == 5 && shop.count(kCharacterA, "QbPouch") == 0,
          "the retry pays one time; a late duplicate for the open instance pays nothing");
    check(shop.count(kCharacterB, "QbPotion") == 0, "B gets no item of the open of A");
}

/** QRDW (control of the review): the standing of A goes to its owner state after the steps, and B gets no fragment of it. */
void standing_wire(const Options&) {
    QuestShop shop;
    gamedata::ProgressionData tables = test_progression();
    gamedata::StandingRow standing;
    standing.territory = 6;
    standing.id = "6";
    standing.maxRank = 2;
    standing.ranks.experience = {150, 375, 600};
    tables.standings.push_back(standing);
    shop.host.progression.set(std::move(tables));
    shop.enter();
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& system = shop.host.world.quests();
    const world::QuestResult grant = system.grant(*a, "Qb_Standing", "command", 6);
    check(grant.ok() && system.hand_in(*a, grant.instance).ok(), "control: A gets 120 standing of territory 6");
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    bool own = false;
    for (const Seen& seen : fragments_of(shop.clientA, 65, 911, fromA)) {
        const auto& fields = seen.fragment.fields;
        if (seen.interest != 1 || fields.count({0, 0}) == 0 || fields.count({0, 1}) == 0 || fields.count({0, 2}) == 0) {
            continue;
        }
        const WireRanks decoded = read_ranks({fields.at({0, 0}), fields.at({0, 1}), fields.at({0, 2})});
        for (const auto& row : decoded.entries) {
            own = own || (decoded.ok && row.id == gamedata::progression_id("6") && row.rank == 0 && row.points == 120);
        }
    }
    bool foreign = false;
    for (const Seen& seen : fragments_of(shop.clientB, 65, 911, fromB)) {
        foreign = foreign || seen.interest != 1;
    }
    check(own && !foreign && world::Skills::standing(shop.entity(kCharacterB)->skills, 6).points == 0,
          "slot 65 of A has the standing (120 points of territory 6); B gets no fragment of A and no standing");
}

/** QRDM (control of the review): a database of schema 16 opens as schema 17 with its rows. */
void schema16_migration(const Options& options) {
    const std::string path = (options.workDirectory / "qrdm-schema16.sqlite3").string();
    std::filesystem::remove(path);
    std::uint64_t instance = 0;
    {
        QuestShop shop(0, path);
        shop.enter();
        check(shop.host.world.inventory().give(kCharacterA, "QbRelic", 3).ok(), "control: the old store has three items");
        const world::QuestResult grant = shop.host.world.quests().grant(*shop.entity(kCharacterA), "Qb_Paid", "command", 6);
        instance = grant.instance;
        check(grant.ok(), "control: the old store has a quest with its tasks");
    }
    check(run_sql(path, "DROP TABLE character_quest_repeats; PRAGMA user_version=16"), "control: the database has the tables of schema 16");
    {
        QuestShop shop(0, path, nullptr, true);
        shop.enter();
        const world::QuestInstance* quest = questfix::quest_of(*shop.entity(kCharacterA), "Qb_Paid");
        check(quest != nullptr && quest->instance == instance && quest->originTerritory == 6 && shop.count(kCharacterA, "QbRelic") == 3
                  && shop.host.store.list_quest_repeats(kCharacterA).empty(),
              "the migration to schema 17 keeps the character, the items, the quest, and its origin; the repeat table is empty");
    }
}

} // namespace

std::vector<Case> review_quests_18d_cases() {
    return {{"QRD1", "review of 1.8d, R1: the return tasks of one item need the sum of their counts at a hand-in", aggregate_returns},
            {"QRD2", "review of 1.8d, R2: the open of an item container rolls with the container floor", container_context},
            {"QRD3", "review of 1.8d, R3: a daily repeat with no completion achievement pays one time in a cycle", repeat_without_bit},
            {"QRD4", "review of 1.8d, R4: a refine keeps the smallest gear score of a craft task", refine_minimum},
            {"QRD5", "review of 1.8d, R5: an inner container that cannot open refuses the whole open", nested_container},
            {"QRD6", "review of 1.8d, R6: a reward range with trailing text refuses the hand-in", malformed_range},
            {"QRDC", "review of 1.8d (control): a container open transaction and a duplicate", container_rollback},
            {"QRDW", "review of 1.8d (control): the standing bytes of the owner and of an observer", standing_wire},
            {"QRDM", "review of 1.8d (control): the migration from schema 16", schema16_migration}};
}

} // namespace soulwarden::backend::tests
