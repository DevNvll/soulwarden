// The cases of stage QG6b (docs/systems/mission-boards.md): the override columns of the mission rows, the delivery at a town board, the
// faction boards with their offers, the accept 3816, the daily bonus, the patrols, and the gather drop of a skinned body. Made-up ids, names,
// keys, and places with the shape of the game data.
//
//   QG6BA  the overrides (I1): the rules of the task factory 0x4461150 (FACTORY.md): the common values of each leaf (TerritoryID, POITag,
//          OverrideLocation, also on the return task), the item values of the types 25, 26, 29, 30 (always: a blank cell is 0 or empty), the
//          optional pause and storage flags, the give item with the quantity of the template, the timer, the patrol of one POI, a custom row,
//          a loot tag and a column with no rule (refused), the town spelling of the pause column (no effect)
//   QG6BB  a delivery at the town board (I1, I5, I7): the accept of a Gather offer with an item override, the possession task, a hand-in that
//          is not ready, the hand-in that takes the items; a full bag refuses the hand-in and the return task stays pending; B does not change
//   QG6BC  the faction board (I3, I7): the open at a representative (the faction of the character, the claim as the provider identity), the
//          offers of field 2 (the faction, the territory, the level, no PvP), the accept 3816 and each refusal, the hand-in with the daily
//          bonus (experience x3, coin x10, tokens x5), the GameEvent state, the store rows, a restart, a hand-in with no bonus left; B of
//          another faction gets no board of the Marauders and its own offers
//   QG6BD  the patrol (I4): the time in the POI sums across the visits and pauses outside; the progress in whole seconds; B outside does not
//          change
//   QG6BE  the support manifest (I6): the faction boards give their missions; the counts
//   QG6BF  the gather drop of a skinned body (GATHERDROP.md): a drop source that is a category of the creature of the body

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <tuple>

#include "gamedata/game_event.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/mission_fixture.h"
#include "support/mission_qg6b_fixture.h"
#include "world/factions.h"
#include "world/inventory.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;
using namespace missionfix;
using nlohmann::json;

/** QG6BA: the override rules of the task factory. */
void override_rules(const Options&) {
    gamedata::QuestData data;
    check(data.load_text(qg6b_quests().dump()), "setup: the quest data loads: " + data.error());
    json document = qg6b_missions();
    json& rows = document["missions"];
    // A loot row with each value, a row with blank item cells, a timer and give row, a patrol row, a custom row, a loot tag, an unknown column.
    rows.push_back(world_row("FM_Test_Values", 10, "Test_FM_Loot",
                             {{"TaskHaveAndReturnItemsOverride", "TestHide"}, {"TaskHaveAndReturnItemsQtyOverride", "4"},
                              {"TaskHaveAndReturnItemsDropProbabilityOverride", "30"}, {"TaskHaveAndReturnChestDropProbabilityOverride", "100"},
                              {"TaskHaveAndReturnItemsItemsDropVCOverride", "Wolf_Black_Test, Boar"}, {"PauseAndClearLootOnTaskCompleteOverride", "0"},
                              {"UseGlobalStorageOverride", "1"}, {"TerritoryIDOverride", "7"}, {"POITagOverride", "test_loot"},
                              {"OverrideLocation", "1 2 3"}}));
    rows.push_back(world_row("FM_Test_Blank", 11, "Test_FM_Loot", {{"PauseAndClearLootTagOnTaskCompleteOverride", "1"}}));
    rows.push_back(world_row("FM_Test_TimerGive", 12, "Test_Timer_Give", {{"TaskTimerOverride", "3600"}, {"TaskGiveAndTakeItemOverride", "TestBrick"}}));
    rows.push_back(world_row("FM_Test_Patrol", 13, "Test_Patrol", {{"PatrolDuration", "90"}, {"PatrolPoi1", "test_area"}}));
    rows.push_back(world_row("FM_Test_TwoPois", 14, "Test_Patrol", {{"PatrolPoi1", "a"}, {"PatrolPoi2", "b"}}));
    rows.push_back(world_row("FM_Test_Custom", 15, "Test_FM_Loot", {{"IsCustomObjective", "1"}, {"TaskHaveAndReturnItemsOverride", "TestHide"}}));
    rows.push_back(world_row("FM_Test_LootTag", 16, "Test_FM_Loot", {{"LootTagOverride", "Test_Tag"}, {"TaskHaveAndReturnItemsOverride", "TestHide"}}));
    rows.push_back(world_row("FM_Test_Unknown", 17, "Test_FM_Kill", {{"TestNoRuleOverride", "x"}}));
    gamedata::MissionData missions;
    check(missions.load_text(document.dump()), "setup: the mission data loads: " + missions.error());
    data.bind_missions(missions);
    const auto task = [&](const char* mission, std::uint8_t index) -> const gamedata::QuestTask& {
        return data.task(data.mission(crc(mission))->objective->nodes[index].task);
    };
    const gamedata::MissionBinding* values = data.mission(crc("FM_Test_Values"));
    const gamedata::QuestTask& loot = task("FM_Test_Values", 0);
    check(values != nullptr && values->refusals.empty() && loot.itemName == "TestHide" && loot.targetQty == 4
              && values->objective->nodes[0].target == 4 && loot.dropChance == 30.0 && loot.chestChance == 100.0
              && loot.dropSources == std::vector<std::string>{"wolf_black_test", "boar"} && !loot.pauseLoot && loot.useGlobalStorage
              && loot.territory == 7 && loot.poiTags == std::vector<std::string>{"test_loot"} && loot.pin,
          "the item leaf (26): the item, the quantity (target 4), the two drop chances, the drop sources, the pause 0 (a present cell), the "
          "storage, and the common values (TerritoryID 7, POITag test_loot, OverrideLocation)");
    const gamedata::QuestTask& back = task("FM_Test_Values", 1);
    check(back.territory == 7 && back.poiTags == std::vector<std::string>{"test_loot"} && !back.pin,
          "the return task (TaskInteract ReturnToGiver) gets TerritoryID and POITag, but keeps its template location (0x446272B)");
    const gamedata::QuestObjective* base = data.objective("Test_FM_Loot");
    check(data.task(base->nodes[0].task).itemName.empty() && base->nodes[0].target == 1 && data.task(base->nodes[0].task).poiTags.empty(),
          "the template keeps its own values (the copies of a variant are other task rows)");
    const gamedata::QuestTask& blank = task("FM_Test_Blank", 0);
    check(data.mission(crc("FM_Test_Blank"))->refusals.empty() && blank.itemName.empty() && blank.targetQty == 0
              && data.mission(crc("FM_Test_Blank"))->objective->nodes[0].target == 0 && blank.dropChance == 0.0 && !blank.pauseLoot,
          "blank item cells replace the template (no item, quantity 0, chance 0: 0x4463798); the town spelling of the pause column has no "
          "effect (the loader 0x39C5933 reads only PauseAndClearLootOnTaskCompleteOverride)");
    const gamedata::QuestTask& timer = task("FM_Test_TimerGive", 0);
    const gamedata::QuestTask& give = task("FM_Test_TimerGive", 1);
    check(timer.duration == 3600.0 && give.itemName == "TestBrick" && give.targetQty == 2,
          "TaskTimerOverride is the duration of the TaskTimer leaf (case 64); the give item of TaskGiveAndTakeItemOverride with the quantity of "
          "the template (case 33)");
    const gamedata::QuestTask& patrol = task("FM_Test_Patrol", 0);
    check(patrol.duration == 90.0 && patrol.poiTags == std::vector<std::string>{"test_patrol"}
              && data.mission(crc("FM_Test_Patrol"))->objective->nodes[0].target == 90 && data.objective("Test_Patrol")->nodes[0].target == 10,
          "the patrol of one POI: the duration of the row and the place of the template (finding QG6BR-R4: PatrolPoi1 only selects the "
          "branch), the target 90 whole seconds (the template: 10)");
    check(!data.mission(crc("FM_Test_TwoPois"))->refusals.empty() && !data.mission(crc("FM_Test_LootTag"))->refusals.empty()
              && !data.mission(crc("FM_Test_Unknown"))->refusals.empty(),
          "refused: a patrol of two POIs (a tree that is not the tree of the template), a loot tag, a column with no rule");
    check(data.mission(crc("FM_Test_Custom"))->objective == data.objective("Test_FM_Loot") && data.mission(crc("FM_Test_Custom"))->refusals.empty(),
          "a custom row (IsCustomObjective 1) keeps the tasks of its template");
    // The CurrencyReward of a mission event is the float32 cell of the data sheet: the source text 113.99999999999999 is 114 (the town screen
    // shows 1.14 coin); a fraction of the cell (2500.75) stays unpaid (QG2R-R4).
    const gamedata::QuestGameEvent artifact = gamedata::read_game_event("x", {{"EventID", "Test_Artifact"}, {"CurrencyReward", "113.99999999999999"}});
    const gamedata::QuestGameEvent fraction = gamedata::read_game_event("y", {{"EventID", "Test_Fraction"}, {"CurrencyReward", "2500.75"}});
    check(artifact.coin == 114 && artifact.unpaid.empty() && fraction.coin == 0 && fraction.unpaid.size() == 1,
          "CurrencyReward 113.99999999999999 pays 114 coin units; 2500.75 stays unpaid");
}

/** QG6BB: a delivery mission at the town board, and a full bag at the hand-in. */
void delivery(const Options&) {
    Factions shop;
    shop.start_factions();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    const world::TownOffers offers = shop.missions().offers_of(*a, 6);
    check(offers.offers.size() == 2 && offers.offers[1].mission == crc(kPoi), "setup: the Gather offer of A is the delivery (one candidate)");
    shop.open(shop.clientA);
    shop.accept(shop.clientA, kCharacterA, crc(kPoi));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kPoi));
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kActive, "the accept of the delivery: the possession task is Active");
    const std::uint64_t instance = quest != nullptr ? quest->instance : 0;
    (void)shop.host.world.inventory().give(kCharacterA, "TestOre", 2);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    shop.hand_in(shop.clientA, kCharacterA, instance);
    quest = shop.mission_of(kCharacterA, crc(kPoi));
    check(quest != nullptr && quest->tasks[0].progress == 2 && quest->tasks[1].state == world::quest_state::kNew,
          "2 of 3 TestOre: the task has 2; a hand-in now is refused (not ready)");
    (void)shop.host.world.inventory().give(kCharacterA, "TestOre", 3);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    quest = shop.mission_of(kCharacterA, crc(kPoi));
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kComplete && quest->tasks[1].state == world::quest_state::kActive,
          "5 TestOre: the possession task is satisfied, the return task is Active");
    // A full bag: the reward item does not fit. The hand-in changes nothing, and the return task stays pending (open problem 3 of QG6a).
    std::uint32_t bricks = 0;
    while (bricks < 1000 && shop.host.world.inventory().give(kCharacterA, "TestBrick", 1).ok()) {
        ++bricks;
    }
    const std::uint32_t experience = a->progression.experience;
    shop.open(shop.clientA);
    shop.hand_in(shop.clientA, kCharacterA, instance);
    quest = shop.mission_of(kCharacterA, crc(kPoi));
    check(bricks > 0 && quest != nullptr && quest->tasks[1].state == world::quest_state::kActive && a->progression.experience == experience
              && world::Inventory::count(*a, "TestOre") == 5,
          "a full bag refuses the hand-in: no reward, the items stay, and the return task stays pending");
    const std::optional<questfix::ObjectivesState> state = last_objectives(shop.clientA);
    const auto* returning = state ? task_of(*state, instance, 1) : nullptr;
    const auto* container = state ? task_of(*state, instance, 2) : nullptr;
    check(returning != nullptr && returning->state == world::quest_state::kActive && container != nullptr && container->progress == 1,
          "the client keeps the completion condition (the container has its child count minus one)");
    // Free one place: the hand-in takes the 3 TestOre, pays the event of the row, and gives its item.
    std::optional<std::uint32_t> brickSlot;
    for (std::size_t slot = 0; slot < a->bag.slots.size() && !brickSlot; ++slot) {
        if (a->bag.slots[slot].item && a->bag.slots[slot].item->itemId == "TestBrick") {
            brickSlot = static_cast<std::uint32_t>(slot);
        }
    }
    check(brickSlot && shop.host.world.inventory().remove(kCharacterA, *brickSlot, 1).ok(), "setup: one place in the bag");
    shop.hand_in(shop.clientA, kCharacterA, instance);
    check(shop.mission_of(kCharacterA, crc(kPoi)) == nullptr && world::Inventory::count(*a, "TestOre") == 2
              && a->progression.experience - experience == 50 && world::Inventory::count(*a, "TestBrick") == bricks,
          "the hand-in takes the 3 TestOre of the return task (2 stay), pays 50 experience and the item of the event of the row");
    check(b->quests.active.empty() && world::Inventory::count(*b, "TestOre") == 0, "B does not change");
}

/** QG6BC: the faction board. */
void faction_board(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "faction-board.db";
    std::filesystem::remove(path);
    {
        Factions shop(path.string());
        shop.start_factions();
        world::Entity* a = shop.entity(kCharacterA);
        world::Entity* b = shop.entity(kCharacterB);
        // B (a Syndicate) at the representative of the Marauders: refused, no provider state.
        const std::size_t beforeB = shop.clientB.got.size();
        shop.open_representative(shop.clientB, kMarauderTag);
        check(!b->missionBoard.opened && !provider_state(shop.clientB, beforeB), "B (Syndicate) opens the board of the Marauders: refused, no state");
        // A opens the board of its faction.
        shop.open_representative(shop.clientA, kMarauderTag);
        const world::MissionBoardState& board = a->missionBoard;
        std::vector<std::uint32_t> missions;
        for (const world::MissionOffer& offer : board.offers) {
            missions.push_back(offer.mission);
        }
        std::sort(missions.begin(), missions.end());
        std::vector<std::uint32_t> expected{crc(kFactionKill), crc(kFactionLoot)};
        std::sort(expected.begin(), expected.end());
        check(board.opened && board.faction && board.provider == kClaim6 && board.providerFaction == kMarauderFaction && missions == expected,
              "A opens the board of the Marauders: the provider is the claim of the territory 6; the offers are the Marauder rows of the "
              "territory 6 at its level (not the Syndicate row, the PvP row, the row of MinLevel 50, the row of the territory 7)");
        const std::optional<Fragment> provider = provider_state(shop.clientA);
        check(provider && provider->fields.contains({0, 2})
                  && provider->fields.at({0, 2}) == expected_offers(board.offers, a->changed.at(world::ComponentKey::objectiveInteractor)),
              "field 2 of the provider state has the offers in their order (52 bytes each: the mission, the claim, the origin 6, available)");
        // The refusals of 3816: an index with no offer, B with no faction board open; then the accept of the kill offer.
        shop.select(shop.clientA, kCharacterA, 7);
        check(a->quests.active.empty(), "3816 with the index 7 (no offer): refused");
        shop.select(shop.clientB, kCharacterB, 0);
        check(b->quests.active.empty(), "3816 of B with no open faction board: refused");
        const int kill = shop.offer_index(kCharacterA, kFactionKill);
        shop.select(shop.clientA, kCharacterA, static_cast<std::uint8_t>(kill));
        const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kFactionKill));
        check(quest != nullptr && !quest->communityGoal && quest->destination == kClaim6 && quest->originTerritory == 6
                  && !board.offers[static_cast<std::size_t>(kill)].available && board.offers[static_cast<std::size_t>(kill)].instance == quest->instance,
              "3816 accepts the offer of its index: the instance (community 0, the destination the claim, origin 6); the offer stays with "
              "available 0 and the instance (0x495A166)");
        const std::uint64_t instance = quest != nullptr ? quest->instance : 0;
        shop.select(shop.clientA, kCharacterA, static_cast<std::uint8_t>(kill));
        check(a->quests.active.size() == 1, "a second 3816 of the same index: refused (not available)");
        // The progress, the hand-in at the representative with the daily bonus.
        shop.kill(kCharacterA, "wolf", 2);
        shop.open_representative(shop.clientA, kMarauderTag);
        const std::uint32_t experience = a->progression.experience;
        const std::uint64_t coin = a->wallet.coin;
        const auto points = [&](const char* id) { return world::Skills::progression_of(a->skills, gamedata::progression_id(id)).points; };
        const auto reputation = points("Marauder");
        const auto tokens = points("MarauderTokens");
        shop.hand_in(shop.clientA, kCharacterA, instance);
        check(shop.mission_of(kCharacterA, crc(kFactionKill)) == nullptr && a->progression.experience - experience == 270
                  && a->wallet.coin - coin == 4800 && points("Marauder") - reputation == 45 && points("MarauderTokens") - tokens == 275,
              "the hand-in with the daily bonus (FactionMissionBonus: experience 2, currency 9, tokens 4): 90 x3 = 270 experience, 480 x10 = "
              "4800 coin units, 45 reputation (no daily part), 55 x5 = 275 tokens");
        check(shop.missions().daily_bonuses_left(*a) == 2 && a->dailyBonuses.uses.at(crc("FactionMissionBonus")).used == 1,
              "one daily bonus is used (2 of 3 left)");
        const std::vector<Fragment> events = questfix::own_fragments(shop.clientA, kGameEventSlot, kGameEventType);
        server::hub::Writer field;
        field.size(0);
        field.u8(1);
        field.size64(a->changed.at(world::ComponentKey::gameEvent));
        field.size(1);
        field.u32(crc("FactionMissionBonus"));
        field.u8(1);
        check(!events.empty() && events.back().fields.size() == 1 && events.back().fields.contains({0, 1})
                  && events.back().fields.at({0, 1}) == field.data(),
              "the GameEvent state (slot 68, type 497): field 1 dailyBonusesUsed with FactionMissionBonus 1");
        const std::vector<state::FactionOfferRow> rows = shop.host.store.list_faction_offers(kCharacterA);
        check(!board.offers[static_cast<std::size_t>(kill)].available && rows.size() == 1
                  && rows[0].unavailable == std::vector<std::uint32_t>{crc(kFactionKill)} && shop.host.store.list_daily_bonuses(kCharacterA).size() == 1,
              "the store has the faction board entry (the mission is unavailable in the window) and the daily bonus row");
        check(b->quests.active.empty() && b->dailyBonuses.uses.empty() && questfix::own_fragments(shop.clientB, kGameEventSlot, kGameEventType).empty(),
              "B does not change and gets no GameEvent state of A");
        // B at its own board: the Syndicate row.
        shop.open_representative(shop.clientB, kSyndicateTag);
        check(b->missionBoard.faction && b->missionBoard.offers.size() == 1 && b->missionBoard.offers[0].mission == crc(kSyndicateKill),
              "B at the board of the Syndicate: its offer is the Syndicate row");
    }
    // A restart keeps the daily use and the unavailable mission of the window; a hand-in with no bonus left pays the base rewards.
    Factions again(path.string(), true);
    again.start_factions();
    world::Entity* a = again.entity(kCharacterA);
    check(a->dailyBonuses.uses.at(crc("FactionMissionBonus")).used == 1 && again.missions().daily_bonuses_left(*a) == 2,
          "after a restart: one daily bonus used");
    again.open_representative(again.clientA, kMarauderTag);
    const int kill = again.offer_index(kCharacterA, kFactionKill);
    check(kill >= 0 && !a->missionBoard.offers[static_cast<std::size_t>(kill)].available,
          "after a restart the handed-in mission is not available in its window");
    a->dailyBonuses.uses[crc("FactionMissionBonus")].used = 3; // Setup: the three bonuses of the day are used.
    const int loot = again.offer_index(kCharacterA, kFactionLoot);
    check(loot >= 0, "setup: the loot offer");
    again.select(again.clientA, kCharacterA, static_cast<std::uint8_t>(loot));
    const world::QuestInstance* quest = again.mission_of(kCharacterA, crc(kFactionLoot));
    check(quest != nullptr, "setup: A accepts the loot mission");
    (void)again.host.world.inventory().give(kCharacterA, "TestHide", 2);
    again.host.steps(3, {&again.clientA, &again.clientB});
    quest = again.mission_of(kCharacterA, crc(kFactionLoot));
    check(quest != nullptr && quest->tasks[0].progress == 2 && quest->tasks[1].state == world::quest_state::kActive,
          "the 2 TestHide in the loot place (test_loot): the loot task is satisfied, the return is Active");
    // The location rule gates the gain only [data: the mission templates collect at a place and return elsewhere]: out of the place the
    // satisfied task keeps its value.
    again.walk_a(212.0F);
    quest = again.mission_of(kCharacterA, crc(kFactionLoot));
    check(quest != nullptr && quest->tasks[0].progress == 2 && quest->tasks[1].state == world::quest_state::kActive,
          "out of the loot place the loot task keeps its value 2 and the return stays Active");
    again.walk_a(200.0F);
    const std::uint32_t experience = a->progression.experience;
    again.open_representative(again.clientA, kMarauderTag);
    again.hand_in(again.clientA, kCharacterA, quest != nullptr ? quest->instance : 0);
    check(again.mission_of(kCharacterA, crc(kFactionLoot)) == nullptr && a->progression.experience - experience == 90
              && world::Inventory::count(*a, "TestHide") == 0,
          "with the three daily bonuses used the hand-in pays the base 90 experience (no bonus) and takes the 2 TestHide in the place");
}

/** QG6BD: the patrol. */
void patrol(const Options&) {
    Factions shop;
    shop.start_factions();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(*a, "Test_Patrol", "command", 6).ok() && shop.host.world.quests().grant(*b, "Test_Patrol", "command", 6).ok(),
          "setup: A and B have the patrol (10 s in test_patrol)");
    const auto progress = [&](const world::Entity& entity) {
        for (const world::QuestInstance& quest : entity.quests.active) {
            if (quest.objective == "Test_Patrol") {
                return std::make_pair(quest.tasks[0].progress, quest.tasks[0].state);
            }
        }
        return std::make_pair(std::uint32_t{0}, std::uint8_t{255});
    };
    const auto wait = [&](int seconds) {
        for (int step = 0; step < seconds * 10; ++step) {
            shop.host.clock.advance(std::chrono::milliseconds(100));
            shop.host.steps(1, {&shop.clientA, &shop.clientB});
        }
    };
    wait(3);
    check(progress(*a).first == 0, "outside the area: no time");
    shop.walk_a(212.0F);
    wait(4);
    const std::uint32_t first = progress(*a).first;
    check(first >= 3 && first <= 5, "4 s in the area: the progress is the whole seconds (" + std::to_string(first) + ")");
    shop.walk_a(200.0F);
    wait(6);
    const std::uint32_t paused = progress(*a).first;
    check(paused >= first && paused <= first + 1 && progress(*a).second == world::quest_state::kActive,
          "6 s outside: the time pauses (" + std::to_string(paused) + "), the task stays Active");
    shop.walk_a(212.0F);
    wait(9);
    check(progress(*a).first == 10 && progress(*a).second == world::quest_state::kCompleteActive,
          "back in the area: the time sums across the visits to 10 s; the task is satisfied (CompleteActive: StayActive)");
    const std::optional<questfix::ObjectivesState> state = last_objectives(shop.clientA);
    bool sent = false;
    if (state) {
        for (const auto& task : state->tasks) {
            sent = sent || (task.index == 0 && task.progress == 10);
        }
    }
    check(sent, "the client gets the progress 10");
    check(progress(*b).first == 0, "B outside the area: no time");
}

/** QG6BE: the support manifest of the faction boards. */
void manifest_faction(const Options&) {
    Factions shop;
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    json kill;
    for (const json& row : manifest["missionTemplates"]) {
        if (row["id"] == "Test_FM_Kill") {
            kill = row;
        }
    }
    const std::string marauder = shop.representative_of(kMarauderTag).key;
    const std::string syndicate = shop.representative_of(kSyndicateTag).key;
    check(manifest["counts"]["factionBoards"] == 2 && !kill.is_null() && kill.contains("mission")
              && (kill["mission"]["board"] == marauder || kill["mission"]["board"] == syndicate),
          "the counts: 2 faction boards; the faction kill template has a mission of a faction board");
    check(!kill.is_null() && std::any_of(kill["grants"].begin(), kill["grants"].end(), [](const json& grant) { return grant["source"] == "board"; }),
          "the grant of the template: a board");
}

/** QG6BF: the gather drop of a skinned body (a drop source that is a category of the creature). */
void gather_drop(const Options&) {
    Factions shop;
    json quests = qg6b_quests();
    quests["tasks"]["test_hunt_gather"] = questfix::task("Test_Hunt_Gather", "taskgatherandreturnitems", 30,
                                                         {{"TargetQty", "2"}, {"ItemName", "TestHide"}, {"ItemDropVC", "Wolf_Black_Test"},
                                                          {"ItemDropProbability", "100"}});
    quests["tasks"]["test_hunt_container"] = questfix::task("Test_Hunt_Container", "consecutivetaskcontainer", 1);
    quests["objectives"].push_back({{"id", "Test_Hunt"},
                                    {"cells", {{"Type", "Mission"}}},
                                    {"tree", json::array({json::array({0, "test_hunt_gather", json::array()}),
                                                          json::array({1, "test_hunt_container", json::array({0})})})}});
    check(shop.host.quests.load_text(quests.dump()), "setup: the hunt quest");
    shop.host.quests.bind_missions(shop.host.missions);
    shop.start_factions();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, "Test_Hunt", "command", 6).ok(), "setup: A has the hunt task");
    // The sources of a skinned body: its gatherable row, then its categories (Gathering::complete); a category of the drop list drops.
    const std::vector<world::QuestDrop> none = shop.host.world.quests().plan_drops(*a, true, {"Test_Skin_Row"});
    const std::vector<world::QuestDrop> drop = shop.host.world.quests().plan_drops(*a, true, {"Test_Skin_Row", "wolf_black_test", "wolf"});
    check(none.empty() && drop.size() == 1 && drop[0].itemId == "TestHide",
          "a gather drop: the gatherable row alone drops nothing; with the category of the creature one drop (one for each task)");
}

} // namespace

std::vector<Case> mission_qg6b_cases() {
    return {{"QG6BA", "mission boards QG6b (I1): the override rules of the task factory", override_rules},
            {"QG6BB", "mission boards QG6b (I1, I5, I7): a delivery at the town board; a full bag keeps the return pending", delivery},
            {"QG6BC", "mission boards QG6b (I3, I7): the faction board, 3816, the daily bonus, a restart", faction_board},
            {"QG6BD", "mission boards QG6b (I4, I7): the patrol sums the time across the visits and pauses outside", patrol},
            {"QG6BE", "mission boards QG6b (I6): the support manifest of the faction boards", manifest_faction},
            {"QG6BF", "mission boards QG6b: the gather drop of a skinned body", gather_drop}};
}

} // namespace soulwarden::backend::tests
