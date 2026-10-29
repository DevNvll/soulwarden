// The cases of the code review of stage QG6b (in stage QG8; the findings: ~/Games/soulwarden/work/evidence/quests/qg6b/code-review/FINDINGS.md,
// the proof cases of the reviewer: its work folder). Each case fails on the tree of stage QG6b (trees/after-qg6b). Made-up ids, names, keys,
// and places only.
//
//   QG6BR1  finding QG6BR-R1: the town buckets are the whole partition: a town row that no bucket names, or an empty bucket list, refuses the
//           whole file (a row of the member townExcluded is not missing); the client offer of the complete file can be accepted
//   QG6BR2  finding QG6BR-R2: the daily uses reset at 00:00 UTC, and the owner gets the GameEvent state again at that time (MP-10: B gets none)
//   QG6BR3  finding QG6BR-R3: the daily bonus of a faction mission: the GainXP card and its truncation first, then the daily factor (276, not 278)
//   QG6BR4  finding QG6BR-R4: a patrol of one POI keeps the effective common POITag (the template, or POITagOverride), not PatrolPoi1
//   QG6BR5  finding QG6BR-R5: after a faction change the faction board of the old faction pays no hand-in; the state does not change
//   QG6BR6  finding QG6BR-R6: a refused skinning row supplies no item source of the support manifest
//   QG6BR7  finding QG6BR-R7: an outside patrol sample keeps the published progress; the next inside sample publishes the saved sum

#include <chrono>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/mission_fixture.h"
#include "support/mission_qg6b_fixture.h"
#include "world/factions.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;
using namespace missionfix;
using nlohmann::json;

void missing_members(const Options&) {
    gamedata::MissionData data;
    json document = mission_document();
    check(data.load_text(document.dump()), "control: the complete bucket partition loads");
    document["townBuckets"][0]["missions"] = json::array({crc(kWolf)});
    const bool partial = data.load_text(document.dump());
    note("QG6BR1 partialLoaded=" + std::to_string(partial));
    check(!partial && !data.loaded(), "a Kill row that no bucket names refuses the whole file");
    document["townExcluded"] = json::array({crc(kBoar)});
    check(data.load_text(document.dump()), "a row of townExcluded (its objective has no row: the tool leaves it out) is not missing");
    document = mission_document();
    document["townBuckets"] = json::array();
    const bool empty = data.load_text(document.dump());
    note("QG6BR1 emptyLoaded=" + std::to_string(empty));
    check(!empty && !data.loaded(), "town rows with an empty bucket list refuse the whole file");
    // The complete file: the row that the client selects can be accepted.
    Boards shop;
    shop.start();
    check(shop.seek([&] { return reference_index(crc(kProject), reference_seed(kCharacterA, 6, shop.server_ns()), 2) == 1; }),
          "setup: the client selects the boar row");
    shop.report(shop.clientB, std::nullopt);
    shop.open(shop.clientA);
    shop.accept(shop.clientA, kCharacterA, crc(kBoar));
    check(shop.mission_of(kCharacterA, crc(kBoar)) != nullptr, "the mission that the client shows can be accepted");
}

void midnight(const Options&) {
    Factions shop;
    shop.start_factions();
    world::Entity* a = shop.entity(kCharacterA);
    shop.open_representative(shop.clientA, kMarauderTag);
    const int kill = shop.offer_index(kCharacterA, kFactionKill);
    check(kill >= 0, "setup: A has the kill offer");
    shop.select(shop.clientA, kCharacterA, static_cast<std::uint8_t>(kill));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kFactionKill));
    check(quest != nullptr, "setup: A accepted the kill mission");
    if (quest == nullptr) {
        return;
    }
    const std::uint64_t instance = quest->instance;
    shop.kill(kCharacterA, "wolf", 2);
    shop.open_representative(shop.clientA, kMarauderTag);
    shop.hand_in(shop.clientA, kCharacterA, instance);
    const std::size_t before = questfix::own_fragments(shop.clientA, kGameEventSlot, kGameEventType).size();
    const auto revision = a->changed.at(world::ComponentKey::gameEvent);
    const world::DailyBonusUse use = a->dailyBonuses.uses.at(crc("FactionMissionBonus"));
    check(before > 0 && shop.missions().daily_bonuses_left(*a) == 2, "control: the owner has the state of one use before midnight");
    const std::uint64_t now = shop.host.clock.wall_time_ns(shop.host.clock.now());
    shop.host.clock.advance(std::chrono::nanoseconds(use.resetNs - now - 1));
    check(shop.missions().daily_bonuses_left(*a) == 2, "one nanosecond before 00:00 UTC: two bonuses left");
    shop.host.clock.advance(std::chrono::nanoseconds(1));
    shop.report(shop.clientA, std::nullopt);
    shop.report(shop.clientB, std::nullopt);
    shop.host.steps(10, {&shop.clientA, &shop.clientB});
    const std::size_t after = questfix::own_fragments(shop.clientA, kGameEventSlot, kGameEventType).size();
    note("QG6BR2 left=" + std::to_string(shop.missions().daily_bonuses_left(*a)) + " fragmentsBefore=" + std::to_string(before)
         + " fragmentsAfter=" + std::to_string(after) + " revisionChanged=" + std::to_string(a->changed.at(world::ComponentKey::gameEvent) != revision));
    check(shop.missions().daily_bonuses_left(*a) == 3, "the daily uses reset at 00:00 UTC");
    check(after > before && a->changed.at(world::ComponentKey::gameEvent) != revision,
          "the owner gets the GameEvent state again at the reset (a new sequence), with no open of the board");
    check(questfix::own_fragments(shop.clientB, kGameEventSlot, kGameEventType).empty(), "B gets no GameEvent state of A (MP-10)");
}

void xp_order(const Options&) {
    Factions shop;
    gamedata::ProgressionData progression = qg6b_progression();
    gamedata::TerritoryCard card;
    card.id = "TB_GainXP";
    card.category = "GainXP";
    card.kind = gamedata::TerritoryBonus::gainXp;
    card.maxLevel = 100;
    card.initialBonus = 0.03F;
    card.reduction = 0.1F;
    card.minValue = 0.0025F;
    progression.territoryCards.push_back(card);
    shop.host.progression.set(progression);
    shop.start_factions();
    world::Entity* a = shop.entity(kCharacterA);
    const auto* standing = shop.host.progression.standing(6);
    const auto* loaded = shop.host.progression.territory_card(gamedata::TerritoryBonus::gainXp);
    check(standing != nullptr && loaded != nullptr, "setup: the standing and the GainXP card");
    if (standing == nullptr || loaded == nullptr) {
        return;
    }
    a->skills.territoryPools[standing->poolId].cards[loaded->wireId] = 1;
    const std::uint64_t carded = shop.host.world.skills().card_experience(kCharacterA, 90);
    const auto expected = static_cast<std::uint32_t>(static_cast<float>(carded) * 3.0F);
    check(carded == 92 && expected == 276, "control: the order of the client: trunc(90 * 1.03) * 3 = 276");
    shop.open_representative(shop.clientA, kMarauderTag);
    const int kill = shop.offer_index(kCharacterA, kFactionKill);
    shop.select(shop.clientA, kCharacterA, static_cast<std::uint8_t>(kill));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kFactionKill));
    check(quest != nullptr, "setup: A accepted the kill mission");
    if (quest == nullptr) {
        return;
    }
    const std::uint64_t instance = quest->instance;
    shop.kill(kCharacterA, "wolf", 2);
    shop.open_representative(shop.clientA, kMarauderTag);
    const std::uint64_t before = a->progression.experience;
    shop.hand_in(shop.clientA, kCharacterA, instance);
    const std::uint64_t actual = a->progression.experience - before;
    note("QG6BR3 actualXp=" + std::to_string(actual) + " clientXp=" + std::to_string(expected));
    check(shop.mission_of(kCharacterA, crc(kFactionKill)) == nullptr, "control: the hand-in succeeds");
    check(actual == expected, "the daily reward has the truncation order of the client preview (the card, then the daily factor)");
    check(shop.entity(kCharacterB)->progression.experience == 0, "B gets no experience of A");
}

void patrol_poi(const Options&) {
    gamedata::QuestData quests;
    gamedata::MissionData missions;
    check(quests.load_text(qg6b_quests().dump()), "setup: the patrol template loads");
    json document = qg6b_missions();
    document["missions"].push_back(world_row("Review_Patrol", 50, "Test_Patrol", {{"PatrolDuration", "3"}, {"PatrolPoi1", "test_loot"}}));
    check(missions.load_text(document.dump()), "setup: the one-POI mission loads");
    quests.bind_missions(missions);
    const gamedata::MissionBinding* binding = quests.mission(crc("Review_Patrol"));
    check(binding != nullptr && binding->refusals.empty(), "setup: the one-POI binding is supported");
    if (binding == nullptr) {
        return;
    }
    const gamedata::QuestTask& task = quests.task(binding->objective->nodes[0].task);
    note("QG6BR4 effectivePoi=" + (task.poiTags.empty() ? std::string("empty") : task.poiTags[0]));
    check(task.poiTags == std::vector<std::string>{"test_patrol"} && task.duration == 3.0,
          "a single PatrolPoi selects the branch and keeps the effective common POITag (the template), with the duration of the row");
    document["missions"].back()["cells"]["POITagOverride"] = "test_override";
    check(missions.load_text(document.dump()), "setup: the mission with a common POI override loads");
    quests.bind_missions(missions);
    binding = quests.mission(crc("Review_Patrol"));
    check(binding != nullptr && quests.task(binding->objective->nodes[0].task).poiTags == std::vector<std::string>{"test_override"},
          "a one-POI patrol has the place of POITagOverride, not PatrolPoi1");
}

void faction_change(const Options&) {
    Factions shop;
    gamedata::ProgressionData progression = qg6b_progression();
    progression.factions[0].achievement = "Archetype_Mercenary";
    shop.host.progression.set(progression);
    shop.start_factions();
    world::Entity* a = shop.entity(kCharacterA);
    shop.open_representative(shop.clientA, kMarauderTag);
    const int kill = shop.offer_index(kCharacterA, kFactionKill);
    shop.select(shop.clientA, kCharacterA, static_cast<std::uint8_t>(kill));
    const world::QuestInstance* quest = shop.mission_of(kCharacterA, crc(kFactionKill));
    check(quest != nullptr, "setup: A accepted the Marauder mission");
    if (quest == nullptr) {
        return;
    }
    const std::uint64_t instance = quest->instance;
    shop.kill(kCharacterA, "wolf", 2);
    shop.open_representative(shop.clientA, kMarauderTag);
    const auto recruitment = shop.host.world.ownership().achievement_bit("Archetype_Mercenary");
    check(recruitment && shop.host.world.ownership().unlock_achievement(*a, "Archetype_Mercenary", *recruitment),
          "setup: A has the recruitment achievement of the Syndicate");
    const world::FactionResult changed = shop.host.world.factions().request(*a, kSyndicateFaction);
    note(std::string("QG6BR5 change=") + world::faction_error_text(changed.error));
    check(changed.ok() && a->faction.faction == kSyndicateFaction, "setup: the faction change request makes A a Syndicate");
    const std::uint64_t coin = a->wallet.coin;
    const std::size_t daily = a->dailyBonuses.uses.size();
    const world::MissionResult result = shop.missions().complete(*a, instance);
    const std::uint64_t tokens = world::Skills::progression_of(a->skills, gamedata::progression_id("SyndicateTokens")).points;
    note("QG6BR5 handInOk=" + std::to_string(result.ok()) + " coinDelta=" + std::to_string(a->wallet.coin - coin) + " syndicateTokens="
         + std::to_string(tokens));
    check(!result.ok() && a->wallet.coin == coin && tokens == 0 && a->dailyBonuses.uses.size() == daily
              && shop.mission_of(kCharacterA, crc(kFactionKill)) != nullptr,
          "a Syndicate character hands in nothing through the board of the Marauders: no reward, no daily use, the mission stays");
    check(shop.entity(kCharacterB)->wallet.coin == 0, "B does not change");
}

void skin_source(const Options&) {
    Factions shop;
    json quests = qg6b_quests();
    quests["tasks"]["review_skin_task"] = questfix::task("Review_Skin_Task", "taskgatherandreturnitems", 30,
                                                         {{"TargetQty", "2"}, {"ItemName", "Review_SkinDrop"}, {"ItemDropVC", "Test_BagWolf_Category"},
                                                          {"ItemDropProbability", "100"}});
    quests["objectives"].push_back({{"id", "Review_Skin"},
                                    {"cells", {{"Type", "Journey"}, {"PushObjectiveOnReqMet", "1"}}},
                                    {"tree", json::array({json::array({0, "review_skin_task", json::array()})})}});
    check(shop.host.quests.load_text(quests.dump()), "setup: a quest whose item only drops at the skinning of the wolf");
    shop.host.quests.bind_missions(shop.host.missions);
    std::vector<gamedata::ItemDefinition> items = chests::chest_items();
    items.push_back(chests::chest_item("Review_SkinDrop", "Resource", "Resource", 1, 100));
    shop.host.catalog.set(items);
    gamedata::SpawnPoint point;
    point.id = "Review_Wolf_Point";
    point.kind = 0;
    point.level = 20;
    point.position = {300.0F, 300.0F, 30.0F};
    check(shop.host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind()}, {point}, {{20, bags::kWolfHealth, 0.0F, 0.0F}}),
          "setup: a placed wolf with a skinning slice");
    check(shop.host.loot.set(bags::bag_loot()), "setup: the wolf categories");
    const auto supported = [&](bool refused) {
        json gathering = gather::gathering_document();
        if (refused) {
            gathering["rows"][gather::kSkinRow]["unsupported"] = json::array({"review: the skin row has a gate with no rule"});
        }
        check(shop.host.gathering.set(gathering.dump(), {{gather::kRegion, gather::region_records()}}), "setup: the gathering data");
        world::QuestSupport support(shop.host.world);
        const json manifest = support.manifest({});
        for (const json& entry : manifest["objectives"]) {
            if (entry["id"] == "Review_Skin") {
                note("QG6BR6 refused=" + std::to_string(refused) + " supported=" + entry["supported"].dump() + " supplier="
                     + entry["tasks"][0].value("supplier", ""));
                return entry["supported"].get<bool>();
            }
        }
        return false;
    };
    check(supported(false), "control: a skinning row that the backend gathers supplies the item");
    check(!supported(true), "a refused skinning row supplies no item source of the support manifest");
}

void patrol_border(const Options&) {
    Factions shop;
    shop.start_factions();
    shop.walk_a(212.0F);
    world::Entity* a = shop.entity(kCharacterA);
    check(!a->positionEstimated && shop.host.pois.inside({"test_patrol"}, 0, a->position.x, a->position.y), "setup: A is inside the patrol area");
    check(shop.host.world.quests().grant(*a, "Test_Patrol", "command", 6).ok(), "setup: A has the patrol");
    shop.host.world.tick();
    const auto progress = [&] { return a->quests.active[0].tasks[0].progress; };
    check(progress() == 0, "setup: the first sample is inside, at 0 s");
    shop.host.clock.advance(std::chrono::milliseconds(1100));
    shop.placeA.y = 216.0F;
    shop.report(shop.clientA, std::nullopt);
    check(!a->positionEstimated && a->position.y > 215.0F, "setup: an accepted position outside the area");
    shop.host.world.tick();
    const std::uint32_t outside = progress();
    note("QG6BR7 progressAtOutsideSample=" + std::to_string(outside));
    check(outside == 0, "an outside sample keeps the published progress (0x3B095E8: no call of the setter)");
    shop.host.clock.advance(std::chrono::milliseconds(500));
    shop.placeA.y = 212.0F;
    shop.report(shop.clientA, std::nullopt);
    check(!a->positionEstimated && a->position.y < 215.0F, "setup: an accepted position inside again");
    shop.host.world.tick();
    check(progress() == 1, "the next inside sample publishes the saved whole second");
    check(shop.entity(kCharacterB)->quests.active.empty(), "B has no patrol of A");
}

} // namespace

std::vector<Case> review_qg6b_cases() {
    return {{"QG6BR1", "review of QG6b, R1: the town buckets are the whole partition of the client", missing_members},
            {"QG6BR2", "review of QG6b, R2 (MP-10): the daily reset reaches the owner at 00:00 UTC", midnight},
            {"QG6BR3", "review of QG6b, R3: the XP card and its truncation before the daily factor", xp_order},
            {"QG6BR4", "review of QG6b, R4: the place of a patrol of one POI", patrol_poi},
            {"QG6BR5", "review of QG6b, R5: a faction change closes the faction board of the old faction", faction_change},
            {"QG6BR6", "review of QG6b, R6: a refused skinning row is no item source of the manifest", skin_source},
            {"QG6BR7", "review of QG6b, R7 (MP-10): an outside patrol sample keeps the published progress", patrol_border}};
}

} // namespace soulwarden::backend::tests
