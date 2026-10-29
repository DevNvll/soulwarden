// The corrections of the code review of stage DG1 (corrected in stage QG9a; docs/systems/instances.md, the review findings DG1R-R1 to R8 of
// evidence/dungeons/dg1/code-review/FINDINGS.md). The cases are the proof cases of the review (made-up players, objects, places, and data),
// with the ids DG1R1 to DG1R8 (DG1R5B the second failure path of R5) and the controls DG1RC, DG1RCC, DG1RCL of the review. Each case of a
// finding fails on the tree of stage DG1.
#include <chrono>
#include <filesystem>
#include <string>
#include <sqlite3.h>

#include "support/cases.h"
#include "support/chest_fixture.h"
#include "support/loot_bag_fixture.h"
#include "support/shop_fixture.h"
#include "support/heal_fixture.h"
#include "support/test_navigation.h"
#include "support/quest_fixture.h"
#include "support/crafting_fixture.h"

namespace soulwarden::backend::tests {
namespace {
using namespace std::chrono_literals;

void maps(ServerHost& host, state::WorldPosition start) {
    gamedata::GameMap map;
    map.id = "Review_Map";
    map.mode = map.id;
    map.level = world::kMainLevel;
    map.terrain = world::kMainTerrain;
    map.starts = {{start, 0.0F}};
    map.teamCapacity = 5;
    check(host.gameModes.set({map}), "setup: a story map with its own space and the main-world level");
}

bool sql(const std::filesystem::path& path, const std::string& text) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.string().c_str(), &db) != SQLITE_OK) {
        if (db != nullptr) sqlite3_close(db);
        return false;
    }
    const bool ok = sqlite3_exec(db, text.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

void pending_contact(const Options&) {
    for (const bool move : {false, true}) {
        shop::Shop fixture(false, false, true);
        fixture.enter_b();
        fixture.world().creatures().behavior().set_ignored(shop::kCharacterB, true);
        maps(fixture.host, shop::kPlace);
        world::Entity* wolf = fixture.wolf(1.0F);
        check(wolf != nullptr, "setup: a wolf is beside A");
        if (wolf == nullptr) return;
        fixture.world().set_creature_behavior(true);
        for (int step = 0; step < 100 && !wolf->creature.mind.attack; ++step) fixture.walker.idle(1);
        check(wolf->creature.mind.attack.has_value() && wolf->creature.mind.target == fixture.character()->id,
              "setup: the wolf has started its bite at A");
        const float before = fixture.character()->vitals.health;
        const float other = fixture.world().find_character(shop::kCharacterB)->vitals.health;
        if (move) {
            check(fixture.world().instances().enter(shop::kCharacterA, "Review_Map").error == world::InstanceError::none,
                  "setup: A enters a different space at the same coordinates");
        }
        fixture.host.clock.advance(600ms);
        fixture.host.tick();
        const float after = fixture.character()->vitals.health;
        note(std::string("DG1R1 moved=") + (move ? "1" : "0") + " health=" + std::to_string(before) + " -> " + std::to_string(after)
             + " sourceSpace=" + std::to_string(wolf->space) + " targetSpace=" + std::to_string(fixture.character()->space));
        check(move ? after == before : after < before, move ? "a pending bite cannot hurt A in another space" : "control: the bite hurts A in the same space");
        check(fixture.world().find_character(shop::kCharacterB)->vitals.health == other, "B is unchanged");
    }
}

void pending_ability(const Options&) {
    for (const bool move : {false, true}) {
        shop::Shop fixture(false, false, true);
        fixture.enter_b();
        maps(fixture.host, shop::kPlace);
        check(fixture.wear("TestHammer", world::paperdoll::kMainHand1).ok(), "setup: A wears a hammer");
        world::Entity& own = *fixture.character();
        const auto node = gamedata::progression_id("Ability_WarHammer_Shockwave");
        own.skills.pools[shop::kWarHammer][node].level = 1;
        own.skills.bars[shop::kWarHammer] = {0, node, 0};
        own.combat.selectedWeapon = world::paperdoll::kMainHand1;
        world::Entity* wolf = fixture.wolf(1.0F);
        check(wolf != nullptr, "setup: the main world has a wolf beside the return place");
        if (wolf == nullptr) return;
        if (move) check(fixture.world().instances().enter(shop::kCharacterA, "Review_Map").error == world::InstanceError::none,
                        "setup: A enters the instance");
        world::AbilityPress press;
        press.place = 1;
        press.input = 1000;
        check(fixture.world().abilities().press_character(shop::kCharacterA, press) == world::CastVerdict::accepted,
              "setup: Shockwave starts and its timed area has not run");
        const float before = wolf->vitals.health;
        const float other = fixture.world().find_character(shop::kCharacterB)->vitals.health;
        world::SpaceId ended = 0;
        if (move) {
            ended = own.space;
            check(fixture.world().instances().leave(shop::kCharacterA).error == world::InstanceError::none
                      && !fixture.world().has_space(ended), "setup: A returns and the instance ends before the area time");
        }
        fixture.host.clock.advance(1500ms);
        fixture.host.tick();
        note(std::string("DG1R2 leftInstance=") + (move ? "1" : "0") + " wolfHealth=" + std::to_string(before) + " -> "
             + std::to_string(wolf->vitals.health) + " endedSpace=" + std::to_string(ended));
        check(move ? wolf->vitals.health == before : wolf->vitals.health < before,
              move ? "a cast of an ended instance cannot run in the main world" : "control: Shockwave hits in its original space");
        check(fixture.world().find_character(shop::kCharacterB)->vitals.health == other, "B is unchanged");
    }
}

void tracker_caches(const Options&) {
    {
        chests::ChestShop fixture;
        fixture.enter();
        fixture.hold(fixture.clientA, fixture.open_block(chests::kSupply), 2800);
        world::Entity& a = *fixture.entity(interaction::kCharacterA);
        world::Entity& b = *fixture.entity(interaction::kCharacterB);
        check(!a.lootTracker.empty(), "setup: A has a main-world chest claim in its tracker");
        const auto beforeB = b.lootTracker;
        maps(fixture.host, a.position);
        const std::size_t from = fixture.clientA.got.size();
        check(fixture.host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none,
              "setup: A enters at the same coordinates");
        fixture.host.steps(3, {&fixture.clientA, &fixture.clientB});
        const auto emitted = chests::tracker_maps(fixture.clientA, 1, from);
        std::size_t nonempty = 0;
        for (const auto& map : emitted) nonempty += !map.empty();
        note("DG1R3 chestEntries=" + std::to_string(a.lootTracker.size()) + " nonemptyNewContextMaps=" + std::to_string(nonempty));
        check(a.lootTracker.empty() && nonempty == 0, "the new space has no main-world chest entry in the entity or the emitted owner record");
        check(b.lootTracker == beforeB, "B keeps its own tracker");
    }
    {
        bags::BagShop fixture;
        fixture.enter();
        fixture.kill({interaction::kCharacterA});
        world::Entity& a = *fixture.entity(interaction::kCharacterA);
        world::Entity& b = *fixture.entity(interaction::kCharacterB);
        check(!a.bagTracker.empty() && !a.storageMap.empty(), "setup: A has a main-world bag and storage selection");
        const auto beforeB = b.storageMap;
        maps(fixture.host, a.position);
        check(fixture.host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none,
              "setup: A enters at the same coordinates");
        fixture.host.steps(3, {&fixture.clientA, &fixture.clientB});
        note("DG1R3 bagEntries=" + std::to_string(a.bagTracker.size()) + " storageKeys=" + std::to_string(a.storageMap.size()));
        check(a.bagTracker.empty() && a.storageMap.empty(), "a space move clears main-world bag tracking and storage selection");
        check(b.storageMap == beforeB, "B keeps its storage selection");
    }
}

void return_height(const Options&) {
    for (const float height : {50.0F, -99.5F}) {
        ServerHost host;
        const state::WorldPosition home{100.0F, 200.0F, height};
        const Player a = add_player(host.store, kSteamIdA, "", shop::kCharacterA, "ReviewA", home);
        FakeClient client(27000);
        host.enter(client, a.registration());
        maps(host, shop::kPlace);
        world::PositionTrack track;
        world::PositionReport report;
        report.input = 1;
        report.position = home;
        report.height = height < 0 ? 30 : 9000;
        check(host.world.report_position(1, report, track) == world::PositionVerdict::accepted,
              "setup: the exact position report passes the normal position check");
        const auto entered = host.world.instances().enter(a.characterId, "Review_Map");
        if (entered.error != world::InstanceError::none) {
            check(height < -99.0F && host.world.find_character(a.characterId)->space == world::kMainSpace
                      && host.store.list_instance_members().empty(), "an unusable return place is refused before any change");
            continue;
        }
        const auto left = host.world.instances().leave(a.characterId);
        world::Entity& entity = *host.world.find_character(a.characterId);
        note("DG1R4 returnHeight=" + std::to_string(height) + " exitError=" + std::to_string(static_cast<int>(left.error))
             + " entitySpace=" + std::to_string(entity.space) + " spaceExists=" + std::to_string(host.world.has_space(entity.space))
             + " memberRows=" + std::to_string(host.store.list_instance_members().size()));
        check(left.error == world::InstanceError::none && entity.space == world::kMainSpace && entity.position.z == height,
              "an accepted entry has a return that succeeds without an orphan space");
    }
}

void recovery_failure(const Options& options) {
    const auto path = options.workDirectory / "review-dg1-recovery.sqlite3";
    std::filesystem::remove(path);
    const state::WorldPosition home{100.0F, 200.0F, 50.0F};
    const state::WorldPosition later{300.0F, 400.0F, 50.0F};
    {
        state::Store store;
        check(store.open(path.string()), "setup: a private database");
        add_player(store, kSteamIdA, "", shop::kCharacterA, "ReviewA", home);
        check(store.write_instance_member({shop::kCharacterA, 7, "Review_Map", home, 1}), "setup: a member row left by a stopped backend");
    }
    check(sql(path, "CREATE TRIGGER refuse_recovery BEFORE DELETE ON character_instances BEGIN SELECT RAISE(ABORT, 'review'); END;"),
          "setup: a transient failure refuses recovery");
    {
        ServerHost host(service::RepServerOptions{}, path.string());
        host.tick();
        check(host.store.list_instance_members().size() == 1, "setup: failed recovery keeps its row");
        check(sql(path, "DROP TRIGGER refuse_recovery"), "setup: the transient storage failure ends");
        host.tick();
        note("DG1R5 rowsAfterStorageRecovers=" + std::to_string(host.store.list_instance_members().size()));
        check(host.store.list_instance_members().empty(), "recovery retries after the storage failure ends");
        const Player a = interaction::player_of(host.store, kSteamIdA, "", shop::kCharacterA);
        FakeClient client(27000);
        host.enter(client, a.registration());
        check(host.world.travel(a.characterId, later).error == world::TravelError::none, "setup: a later main-world travel commits");
    }
    {
        ServerHost host(service::RepServerOptions{}, path.string());
        host.tick();
        const auto row = host.store.find_character(shop::kCharacterA);
        note("DG1R5 afterNextStart=" + std::to_string(row->position->x) + "," + std::to_string(row->position->y));
        check(row && row->position && row->position->x == later.x && row->position->y == later.y,
              "a later backend start keeps the newer committed main-world place");
    }
}

void instance_sight(const Options& options) {
    for (const int variant : {0, 1, 2}) {
        const bool wall = variant != 0;
        const bool instance = variant != 1;
        heal::HealShop fixture(true);
        const state::WorldPosition there = shop::at(shop::kPlace.x - 4.0F, shop::kPlace.y);
        if (wall) {
            const auto folder = options.workDirectory / "review-dg1-main-wall";
            TestNavRegion region;
            region.tiles[{31, 31}] = [](int cx, int, std::vector<TestFloor>& floors) {
                const float x = 1984.0F + (static_cast<float>(cx) + 0.5F) * 0.25F;
                if (x > 1998.0F && x < 1998.3F) floors = {{shop::kHeight + 4.0F, gamedata::NavFloor::kBlocked}};
            };
            region.write(folder);
            check(fixture.host.navigation.set_directory(folder.string()), "setup: the main world has a wall");
        }
        fixture.enter_b_near(there);
        fixture.staff(*fixture.character(), heal::kDivineNode);
        fixture.host.world.set_test_friends(shop::kCharacterA, shop::kCharacterB);
        if (wall) check(!fixture.host.world.navigation().sees(shop::at(shop::kPlace.x, shop::kPlace.y, shop::kHeight + 1.5F),
                                                             shop::at(there.x, there.y, shop::kHeight + 1.5F)),
                        "setup: the loaded main-world wall blocks the line");
        if (instance) {
            maps(fixture.host, shop::kPlace);
            auto rows = fixture.host.gameModes.maps();
            rows[0].level = "nw_review_other_level";
            rows[0].terrain = "coatlicue/nw_review_other_level";
            check(fixture.host.gameModes.set(rows), "setup: the instance uses a different level with no collision data");
            const auto a = fixture.host.world.instances().enter(shop::kCharacterA, "Review_Map");
            check(a.error == world::InstanceError::none, "setup: A enters the other level");
            check(fixture.host.world.instances().enter(shop::kCharacterB, "Review_Map", a.instance->id).error == world::InstanceError::none,
                  "setup: B joins A in the other level");
            check(fixture.host.world.travel(shop::kCharacterB, there).error == world::TravelError::none, "setup: B is four meters from A");
        }
        world::Entity& b = *fixture.host.world.find_character(shop::kCharacterB);
        fixture.host.world.health().hurt(b.characterId, b.vitals.healthMax * 0.5F);
        fixture.host.world.health().fought(b);
        const float before = b.vitals.health;
        world::AbilityPress press;
        press.place = 0;
        press.input = 1000;
        press.confirmed = true;
        check(fixture.host.world.abilities().press_character(shop::kCharacterA, press) == world::CastVerdict::accepted,
              "setup: Divine Embrace starts");
        fixture.host.clock.advance(1500ms);
        fixture.host.tick();
        note("DG1R6 instance=" + std::to_string(instance) + " mainWorldWall=" + std::to_string(wall) + " friendHealth="
             + std::to_string(before) + " -> " + std::to_string(b.vitals.health));
        check((!instance && wall) ? b.vitals.health == before : b.vitals.health > before,
              (!instance && wall) ? "control: the main-world wall blocks the main-world heal"
                                  : "a wall of the main world does not block a heal in a different level");
    }
}

void departure_failure(const Options& options) {
    const auto path = options.workDirectory / "review-dg1-departure.sqlite3";
    std::filesystem::remove(path);
    const state::WorldPosition home{100.0F, 200.0F, 50.0F};
    const state::WorldPosition later{300.0F, 400.0F, 50.0F};
    {
        ServerHost host(service::RepServerOptions{}, path.string());
        maps(host, shop::kPlace);
        const Player a = add_player(host.store, kSteamIdA, "", shop::kCharacterA, "ReviewA", home);
        FakeClient client(27000);
        host.enter(client, a.registration());
        check(host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none, "setup: A enters");
        check(sql(path, "CREATE TRIGGER refuse_departure BEFORE DELETE ON character_instances BEGIN SELECT RAISE(ABORT, 'review'); END;"),
              "setup: the database refuses member cleanup at disconnect");
        host.datagram(client.peer, client.carrier_close());
        host.steps(2, {});
        check(host.world.find_character(a.characterId) == nullptr && host.store.list_instance_members().size() == 1,
              "setup: the disconnected member row remains after a refused cleanup");
        check(sql(path, "DROP TRIGGER refuse_departure"), "setup: the database accepts writes again");
        FakeClient again(27001);
        host.enter(again, a.registration());
        check(host.world.find_character(a.characterId) != nullptr, "setup: A logs in to the main world again");
        check(host.world.travel(a.characterId, later).error == world::TravelError::none, "setup: A commits a later main-world travel");
        note("DG1R5B staleRowsDuringMainWorldPlay=" + std::to_string(host.store.list_instance_members().size()));
    }
    {
        ServerHost host(service::RepServerOptions{}, path.string());
        host.tick();
        const auto row = host.store.find_character(shop::kCharacterA);
        note("DG1R5B afterNextStart=" + std::to_string(row->position->x) + "," + std::to_string(row->position->y));
        check(row && row->position && row->position->x == later.x && row->position->y == later.y,
              "failed cleanup at a prior disconnect cannot replace a newer main-world place");
    }
}

void transaction_controls(const Options& options) {
    const auto path = options.workDirectory / "review-dg1-schema20.sqlite3";
    std::filesystem::remove(path);
    const state::WorldPosition stored{100.0F, 200.0F, 50.0F};
    const state::WorldPosition unsaved{110.0F, 200.0F, 50.0F};
    {
        state::Store store;
        check(store.open(path.string()), "setup: a synthetic database");
        add_player(store, kSteamIdA, "", shop::kCharacterA, "ReviewA", stored);
        check(store.set_currency(shop::kCharacterA, state::kCurrencyCoin, 1234), "setup: a currency row as a migration canary");
    }
    check(sql(path, "DROP TABLE character_instances; PRAGMA user_version=20;"), "setup: the database has schema 20 and no instance table");
    {
        ServerHost host(service::RepServerOptions{}, path.string());
        const auto character = host.store.find_character(shop::kCharacterA);
        check(character && character->position && character->position->x == stored.x
                  && host.store.list_instance_members().empty(), "schema 20 opens and keeps the character position");
        const Player a = interaction::player_of(host.store, kSteamIdA, "", shop::kCharacterA);
        FakeClient client(27000);
        host.enter(client, a.registration());
        maps(host, shop::kPlace);
        check(host.world.set_position(host.world.find_character(a.characterId)->id, unsaved, false), "setup: the entity has a newer unsaved place");
        check(sql(path, "CREATE TRIGGER refuse_entry BEFORE INSERT ON character_instances BEGIN SELECT RAISE(ABORT, 'review'); END;"),
              "setup: the member insert fails after the position write");
        check(host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::storage
                  && host.world.find_character(a.characterId)->space == world::kMainSpace
                  && host.store.find_character(a.characterId)->position->x == stored.x,
              "the entry transaction rolls back a position write that would have changed the stored place");
        check(sql(path, "DROP TRIGGER refuse_entry"), "setup: inserts work again");
        const auto entered = host.world.instances().enter(a.characterId, "Review_Map");
        check(entered.error == world::InstanceError::none && host.store.list_instance_members().size() == 1
                  && host.store.find_character(a.characterId)->position->x == unsaved.x,
              "the migrated instance table takes a member and stores its current return place");
        FakeClient replacement(27002);
        host.enter(replacement, a.registration());
        check(host.world.find_character(a.characterId)->space == entered.instance->space
                  && host.world.instances().instance_of(a.characterId)->members.size() == 1,
              "a replacement session keeps the entity in its instance with one membership");
        check(host.world.instances().leave(a.characterId).error == world::InstanceError::none
                  && host.store.list_instance_members().empty(), "the exit transaction removes the migrated member row");
    }
}

void quest_object_space(const Options&) {
    using nlohmann::json;
    for (const bool strikeInInstance : {false, true}) {
        shop::Shop fixture(false, false, true);
        fixture.enter_b();
        json quests = questfix::quest_document();
        quests["tasks"]["review_strike"] = questfix::task("Review_Strike", "taskgameevent", 14,
                                                          {{"KillEnemyType", "Review_Strike_Event"}, {"TargetQty", "1"}});
        quests["objectives"].push_back({{"id", "Review_Strike_Quest"}, {"cells", {{"Type", "Journey"}}},
                                       {"tree", json::array({json::array({0, "review_strike", json::array()})})}});
        check(fixture.host.quests.load_text(quests.dump()), "setup: a quest counts the object's destruction event");
        json objects{{"format", 1},
                     {"rows", json::array({{{"id", "Review_Object"}, {"time", 1.0}, {"respawn", {200.0, 200.0}},
                                             {"event", "Review_Strike_Event"}}})},
                     {"objects", json::array({{{"key", "55555555-0000-4000-8000-000000000071"},
                                               {"actor", "0c000000000000710000000000000000"},
                                               {"asset", "00112233445566778899aabbccddeeff"}, {"slot", 3}, {"row", 0},
                                               {"tag", ""}, {"onlyOnQuest", false}, {"position", {2000.0, 2001.0, 50.0}},
                                               {"variant", "Review_Object"}}})}};
        check(fixture.host.questObjects.load_text(objects.dump()), "setup: a destructible main-world object loads");
        world::Entity& a = *fixture.character();
        // The fixture entered before the quest data was set. The ordinary quest entry path loads its state now.
        fixture.host.world.quests().entered(a);
        check(fixture.host.world.quests().grant(a, "Review_Strike_Quest", "command", 0).ok(), "setup: A has the active event task");
        maps(fixture.host, strikeInInstance ? shop::kPlace : shop::at(3000.0F, 3000.0F));
        if (strikeInInstance) check(fixture.host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none,
                                   "setup: A enters a different space at the object's coordinates");
        world::PositionTrack track;
        world::PositionReport report;
        report.input = 1;
        report.position = a.position;
        report.height = 9000;
        report.look = 0.0F;
        check(fixture.world().report_position(1, report, track) == world::PositionVerdict::accepted, "setup: A has an accepted exact position");
        fixture.world().quest_objects().strike(a, 1, 1);
        const world::QuestInstance* quest = questfix::quest_of(a, "Review_Strike_Quest");
        const bool completed = quest != nullptr && quest->tasks[0].state == world::quest_state::kComplete;
        note("DG1R7 strikeInInstance=" + std::to_string(strikeInInstance) + " destroyed="
             + std::to_string(fixture.world().quest_objects().counts().destroyed) + " taskComplete=" + std::to_string(completed));
        check(strikeInInstance ? (!completed && fixture.world().quest_objects().counts().destroyed == 0) : completed,
              strikeInInstance ? "a strike in an instance cannot destroy an object or advance a quest of the main world"
                               : "control: a main-world strike destroys the object and completes its event task");
        if (!strikeInInstance) {
            const auto from = fixture.a.got.size();
            check(fixture.world().instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none,
                  "setup: A enters a different space far from its destroyed main-world object");
            fixture.host.steps(3, {&fixture.a, &fixture.b});
            const auto emitted = chests::tracker_maps(fixture.a, 1, from);
            std::size_t nonempty = 0;
            for (const auto& map : emitted) nonempty += !map.empty();
            note("DG1R7 nonemptyQuestObjectMapsInNewContext=" + std::to_string(nonempty));
            check(nonempty == 0, "main-world destruction state is absent from the new space's owner record");
        }
    }
}

void craft_move_control(const Options&) {
    crafting::Crafter fixture;
    fixture.enter();
    fixture.give(interaction::kCharacterA, "TestFlintT1", 2);
    fixture.give(interaction::kCharacterA, "TestWoodT1", 2);
    check(fixture.host.world.inventory().set_coin(interaction::kCharacterA, 1000).ok(), "setup: A has coin for the craft");
    fixture.use_station(fixture.clientA);
    fixture.craft(fixture.clientA, interaction::kCharacterA, "TestKnifeT1", 2,
                  {crafting::entry("TestFlintT1"), crafting::entry("TestWoodT1")});
    auto& a = *fixture.entity(interaction::kCharacterA);
    check(a.craftJob.has_value() && a.interaction.has_value(), "setup: the craft batch and station use are active");
    maps(fixture.host, shop::kPlace);
    const auto from = fixture.clientA.got.size();
    check(fixture.host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none,
          "setup: A enters an instance before its first craft job");
    fixture.finish();
    check(!a.craftJob && !a.interaction && fixture.count(interaction::kCharacterA, "TestKnifeT1") == 0
              && fixture.count(interaction::kCharacterA, "TestFlintT1") == 2 && fixture.count(interaction::kCharacterA, "TestWoodT1") == 2
              && a.wallet.coin == 1000,
          "the space move ends the use and batch, with no later output, item cost, or fee");
    bool lockMessage = false;
    for (std::size_t index = from; index < fixture.clientA.got.size(); ++index) {
        lockMessage = lockMessage || fixture.clientA.got[index].type == 3746 || fixture.clientA.got[index].type == 6709;
    }
    check(!lockMessage, "the space move emits no old-context input-lock message");
}

void rapid_level_control(const Options& options) {
    ServerHost host;
    const Player a = add_player(host.store, kSteamIdA, "", shop::kCharacterA, "ReviewA", shop::kPlace);
    FakeClient client(27000);
    host.enter(client, a.registration());
    maps(host, shop::kPlace);
    check(host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none
              && host.world.instances().leave(a.characterId).error == world::InstanceError::none,
          "two space changes to the same level text complete before a tick");
    host.steps(3, {&client});
    std::vector<std::uint64_t> versions;
    std::vector<std::uint8_t> contexts;
    std::vector<std::string> levels;
    for (const auto& sent : client.got) {
        if (sent.type != 1635) continue;
        server::hub::Reader reader(sent.body);
        levels.push_back(reader.string());
        reader.string();
        for (int index = 0; index < 4; ++index) reader.u32();
        reader.u64(); reader.size(); reader.u8();
        contexts.push_back(reader.u8());
        reader.u8(); reader.u8();
        versions.push_back(reader.u64());
        check(reader.ok() && reader.left() == 0, "the level information has the expected wire form");
    }
    check(versions == std::vector<std::uint64_t>{1, 2, 3} && contexts == std::vector<std::uint8_t>{1, 2, 3}
              && levels == std::vector<std::string>(3, world::kMainLevel),
          "rapid same-level changes emit distinct versions and context instances in order");
    gamedata::GameModes modes;
    check(!modes.load((options.workDirectory / "absent-review-modes.json").string()) && !modes.loaded(),
          "an absent game-mode file is a normal load refusal");
}

void reward_space(const Options&) {
    for (const bool move : {false, true}) {
        bags::BagShop fixture;
        fixture.enter();
        auto& a = *fixture.entity(interaction::kCharacterA);
        auto& b = *fixture.entity(interaction::kCharacterB);
        maps(fixture.host, a.position);
        auto* wolf = fixture.host.world.creatures().spawn("Test_BagWolf", bags::kKillPlace, 0.0F).entity;
        check(wolf != nullptr, "setup: a main-world wolf for two contributors");
        if (wolf == nullptr) return;
        fixture.host.world.rewards().set_forced(a.characterId, true);
        fixture.host.world.rewards().set_forced(b.characterId, true);
        fixture.host.world.creatures().hurt(wolf->id, 100.0F, a.id);
        check(!wolf->creature.dead && !wolf->creature.credit.empty(), "setup: A makes a qualifying hit before entry");
        const auto beforeXp = a.progression.experience;
        const auto beforeCoin = a.wallet.coin;
        const auto beforeBXp = b.progression.experience;
        if (move) check(fixture.host.world.instances().enter(a.characterId, "Review_Map").error == world::InstanceError::none,
                        "setup: A enters an instance while B stays with the creature");
        fixture.host.world.creatures().hurt(wolf->id, bags::kWolfHealth, b.id);
        const auto xp = a.progression.experience - beforeXp;
        const auto coin = a.wallet.coin - beforeCoin;
        note("DG1R8 moved=" + std::to_string(move) + " experienceDelta=" + std::to_string(xp) + " coinDelta=" + std::to_string(coin)
             + " sourceSpace=" + std::to_string(wolf->space) + " recipientSpace=" + std::to_string(a.space));
        check(move ? (xp == 0 && coin == 0) : (xp > 0 && coin > 0),
              move ? "a contributor in another space is outside the kill's reward range"
                   : "control: a contributor in the same space receives the reward");
        check(b.progression.experience > beforeBXp, "B in the creature's space receives its reward");
    }
}
} // namespace

std::vector<Case> review_dg1_cases() {
    return {{"DG1R1", "review DG1 (corrected in stage QG9a): a pending creature contact across spaces", pending_contact},
            {"DG1R2", "review DG1 (corrected in stage QG9a): a delayed cast after the instance ends", pending_ability},
            {"DG1R3", "review DG1 (corrected in stage QG9a): tracking caches at the same coordinates in a new space", tracker_caches},
            {"DG1R4", "review DG1 (corrected in stage QG9a): a return place outside the travel margin", return_height},
            {"DG1R5", "review DG1 (corrected in stage QG9a): recovery after a transient storage failure", recovery_failure},
            {"DG1R5B", "review DG1 (corrected in stage QG9a): a failed departure cleanup and a later login", departure_failure},
            {"DG1R6", "review DG1 (corrected in stage QG9a): the main-world collision data in a different level", instance_sight},
            {"DG1RC", "review DG1 (corrected in stage QG9a): schema 20, rollback, and session replacement controls", transaction_controls},
            {"DG1R7", "review DG1 (corrected in stage QG9a): strikes and destruction state of main-world quest objects", quest_object_space},
            {"DG1RCC", "review DG1 (corrected in stage QG9a): a space move during a craft", craft_move_control},
            {"DG1RCL", "review DG1 (corrected in stage QG9a): rapid same-level changes and the retail game-mode file", rapid_level_control},
            {"DG1R8", "review DG1 (corrected in stage QG9a): kill credit of a contributor that changed its space", reward_space}};
}
} // namespace soulwarden::backend::tests
