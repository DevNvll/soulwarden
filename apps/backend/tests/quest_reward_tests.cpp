// The cases of stage 1.8d (docs/systems/quests.md): the full reward join of a hand-in (QD1), the open of a loot container item (QD2), the
// availability of an offer (QD3), a timer across a reconnect (QD4), the repeat of a quest (QD5), no reward two times (QD6), two sessions
// (QD7, rule MP-10), the load of the repeats and of the timers (QD8), a refining recipe for TaskCraftRecipe (QD9), and the start of the
// instance ids (QD10). Made-up data only.
//
//   QD1   the reward join: experience, coin, an item with its perk roll, the gear score of GearScoreRange, a loot container item, the
//         completion achievement, the territory standing of the origin territory (D3 option B), a rank-up and a faction reward refused
//   QD2   the open of a loot container (1733): the roll of its table, the cause OpenRewardChest, a wrong facet, a lock, a quantity, a full bag
//   QD3   the availability: RequiredLevel, the faction masks of the parser, a negated NPA, a gate with no rule, a variant with the same
//         completion achievement, the state of the person (3772)
//   QD4   a timer: the fraction, a reconnect keeps its start (no restart), its end completes; a failure timer fails its objective
//   QD5   the repeat: the cycles of the schedules (hourly, daily 05:00 UTC, weekly Tuesday 05:00 UTC), the cooldown, the reset on the world
//         clock, the generation, a reset after an absence, a repeat with no schedule stays completed (D4 open)
//   QD6   no reward two times: a refused store write at the inventory, the progression, the ownership, the standing, the repeat; a
//         duplicate 794 after a reconnect; a full bag (D5 A)
//   QD7   two sessions with different schedules, balances, and standings: one never changes the other (MP-10, QG7)
//   QD8   the load: many dormant repeats and due timers: the work follows the due entries
//   QD9   a refine of a recipe with IsRefining counts for TaskCraftRecipe (06_boarfeast, the Light Ration of the camp)
//   QD10  the first instance id of a run: a new instance does not take the id of a completed instance of the same world entry
//   QD11  a quest whose root is satisfied holds its StayActive tasks (06_crewmates); a quest that is not ready regresses (QR8A-R3)

#include <sqlite3.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include "server/rep/crafting_messages.h"
#include "server/rep/item_requests.h"
#include "server/rep/quest_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/person_fixture.h"
#include "support/procedural_fixture.h"
#include "support/quest_person_fixture.h"
#include "support/shop_fixture.h"
#include "support/test_progression.h"
#include "world/item_containers.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

using namespace personfix;
using namespace questperson;

/** The id of the standing of Windsward on the wire (CRC-32 of "6"; live check I0 of stage 1.8d: the map shows "120 / 150"). */
constexpr std::uint32_t kWindswardStanding = 0x1DB87A14U;

bool d_sql(const std::string& path, const char* text) {
    sqlite3* db = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &db) == SQLITE_OK && sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

/** The progression tables of the cases with the standing of the territory 6 (rank 0: 150 points, rank 1: 375). */
gamedata::ProgressionData standing_progression() {
    gamedata::ProgressionData data = test_progression();
    gamedata::StandingRow standing;
    standing.territory = 6;
    standing.id = "6";
    standing.maxRank = 2;
    standing.ranks.experience = {150, 375, 600};
    standing.xpReward = {0, 100, 150};
    standing.pool = "TerritoryRankBonus_6";
    standing.autoRankUp = true;
    data.standings.push_back(standing);
    return data;
}

/** The loot data of the cases: the tables of the chests and the table of the loot container QbPouch (AND: 5 QbPotion, 3 QbStone). */
gamedata::LootData pouch_loot() {
    using gamedata::LootEntryKind;
    gamedata::LootData data = chests::chest_loot();
    data.tables.push_back(chests::loot_table("Test_PouchRoot", true, 1, {},
                                             {chests::loot_entry(LootEntryKind::item, "QbPotion", 0, 5, 5),
                                              chests::loot_entry(LootEntryKind::item, "QbStone", 0, 3, 3)}));
    data.containerRoots = {{"qbpouch", "Test_PouchRoot"}};
    return data;
}

/** The quest data of the cases: the data of stage 1.8b and the rows of stage 1.8d. */
json reward_document() {
    json q = quest_document_18b();
    json& events = q["events"];
    events["qd_full_success"] = {{"id", "Qd_Full_Success"}, {"UniversalExpAmount", "275"}, {"CurrencyReward", "225"}, {"ItemReward", "QbPouch"},
                                 {"AchievementId", "Qb_Event_Bit"}, {"TerritoryStanding", "120"}};
    events["qd_range_success"] = {{"id", "Qd_Range_Success"}, {"UniversalExpAmount", "40"}, {"ItemReward", "QbPants"}, {"GearScoreRange", "100"}};
    events["qd_rankup_success"] = {{"id", "Qd_Rankup_Success"}, {"UniversalExpAmount", "10"}, {"TerritoryStanding", "40"}};
    events["qd_faction_success"] = {{"id", "Qd_Faction_Success"}, {"UniversalExpAmount", "10"}, {"FactionReputation", "40"}};
    events["qd_daily_success"] = {{"id", "Qd_Daily_Success"}, {"UniversalExpAmount", "50"}, {"CurrencyReward", "100"}};
    const auto add = [&](const char* id, json cells) {
        cells["Type"] = cells.contains("Type") ? cells["Type"] : json("Journey");
        q["objectives"].push_back({{"id", id}, {"cells", cells}, {"tree", json::array({json::array({0, "qb_ready_unlock", json::array()})})}});
    };
    add("Qd_Full", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Full_Success"}, {"AchievementId", "Qd_Full_Done"},
                    {"ExclusiveTerritory", "06"}, {"ExperienceReward", "25"}});
    add("Qd_Range", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Range_Success"}});
    add("Qd_Rankup", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Rankup_Success"}, {"ExclusiveTerritory", "06"}});
    add("Qd_Faction", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Faction_Success"}});
    add("Qd_Daily", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Daily_Success"}, {"AchievementId", "Qd_Daily_Done"},
                     {"Repeatable", "TRUE"}, {"ScheduleId", "Daily"}});
    add("Qd_Weekly", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Daily_Success"}, {"AchievementId", "Qd_Weekly_Done"},
                      {"Repeatable", "1"}, {"ScheduleId", "Weekly"}});
    add("Qd_NoSchedule", {{"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qd_Daily_Success"}, {"AchievementId", "Qd_Free_Done"},
                          {"Repeatable", "1"}});
    add("Qd_Level", {{"RequiredLevel", "5"}});
    add("Qd_Covenant", {{"RequiredFaction", "Covenant"}});
    add("Qd_NotCovenant", {{"RequiredFaction", "!Covenant"}});
    add("Qd_Any", {{"RequiredFaction", "Any"}});
    add("Qd_NotNone", {{"RequiredFaction", "!None"}});
    add("Qd_Arc", {{"QuestArcTag", "TestArc"}});
    add("Qd_Npa", {{"RequiredAchievementId", "!Qd_Npa_Check"}});
    add("Qd_VariantA", {{"AchievementId", "Qd_Variant_Done"}});
    add("Qd_VariantB", {{"AchievementId", "Qd_Variant_Done"}});
    q["schedules"] = json::array({{{"id", "Daily"}, {"cells", {{"StartDate", "*-*-*TDailyCooldown"}}}},
                                  {{"id", "Weekly"}, {"cells", {{"StartWeekday", "WeeklyCooldown"}}}},
                                  {{"id", "Hourly"}, {"cells", {{"StartDate", "*-*-*T*:00:00"}}}},
                                  {{"id", "EventWinter"}, {"cells", {{"UTC", "1"}}}}});
    q["npa"] = json::array({json::array({"Qd_Npa_Check", "Qb_Event_Bit && !Qb_Keep", "", ""})});
    std::uint32_t bit = 960;
    for (const char* id : {"Qd_Full_Done", "Qd_Daily_Done", "Qd_Weekly_Done", "Qd_Free_Done", "Qd_Variant_Done"}) {
        q["achievements"].push_back(json::array({id, bit++}));
    }
    return q;
}

/** A host of the cases of stage 1.8d: the quest data of stage 1.8d, the standing, the loot tables, and the perk data. */
struct RewardShop : QuestShop {
    explicit RewardShop(const std::string& database = ":memory:", const gamedata::SliceIds* slice = nullptr) : QuestShop(0, database, slice) {
        if (!host.quests.load_text(reward_document().dump())) {
            throw std::runtime_error("quest reward test: the quest data was not set: " + host.quests.error());
        }
        host.progression.set(standing_progression());
        if (!host.loot.set(pouch_loot())) {
            throw std::runtime_error("quest reward test: the loot data was not set: " + host.loot.error());
        }
    }
    /** A ready quest of the cases for a character (granted by the command path, with an origin territory). */
    std::uint64_t ready(const char* character, const char* objective, std::uint16_t territory = 0) {
        const world::QuestResult result = host.world.quests().grant(*entity(character), objective, "command", territory);
        check(result.ok() && host.world.quests().ready(*questfix::quest_of(*entity(character), objective)),
              std::string("control: ") + objective + " is ready");
        return result.instance;
    }
    world::ProgressionRank standing(const char* character) {
        return world::Skills::standing(entity(character)->skills, 6);
    }
    /** The world clock of the case (Unix nanoseconds). */
    std::uint64_t now_ns() {
        return host.clock.wall_time_ns(host.clock.now());
    }
};

/** QD1: the reward join. */
void reward_join(const Options&) {
    RewardShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const std::uint32_t experience = a->progression.experience;
    const std::uint64_t coin = a->wallet.coin;
    // The preview of the command (the same join) before the hand-in.
    world::RewardJoin join;
    std::string detail;
    check(shop.host.world.quests().reward_join(*a, *shop.host.quests.objective("Qd_Full"), 6, join, detail) == world::QuestError::none
              && join.eventExperience == 275 && join.rowExperience == 25 && join.coin == 225 && join.items.size() == 1
              && join.items[0].itemId == "QbPouch" && join.standing == 120 && join.standingTerritory == 6 && join.achievement == "Qd_Full_Done",
          "the join of the data: 275 + 25 experience, 225 coin units, the container QbPouch, 120 standing of the territory 6");
    const world::HandInResult paid = shop.host.world.quests().hand_in(*a, shop.ready(kCharacterA, "Qd_Full", 6));
    note("QD1 hand-in: " + std::string(world::quest_error_text(paid.error)) + " " + paid.detail + ", experience " + std::to_string(paid.experience)
         + ", standing " + std::to_string(paid.standing));
    check(paid.ok() && a->progression.experience == experience + 300 && a->wallet.coin == coin + 225 && shop.count(kCharacterA, "QbPouch") == 1
              && shop.has_bit(kCharacterA, "Qd_Full_Done") && shop.has_bit(kCharacterA, "Qb_Event_Bit"),
          "the hand-in pays the experience, the coin, the loot container item, the achievement of the event, and the completion achievement");
    const world::ProgressionRank standing = shop.standing(kCharacterA);
    bool stored = false;
    for (const auto& row : shop.host.store.list_skills(kCharacterA).ranks) {
        stored = stored || (row.id == kWindswardStanding && row.rank == 0 && row.points == 120 && row.name == "6");
    }
    check(standing.rank == 0 && standing.points == 120 && a->skills.progressions.count(kWindswardStanding) == 1 && stored,
          "the standing goes to the origin territory of the instance (decision D3 option B): rank 0, 120 points in the entity and the row");
    check(world::Skills::standing(shop.entity(kCharacterB)->skills, 6).points == 0, "B gets no standing");
    // GearScoreRange (client 0x31E43A0: one number is both ends) gives the gear score of the item of the event.
    const world::HandInResult ranged = shop.host.world.quests().hand_in(*a, shop.ready(kCharacterA, "Qd_Range"));
    std::uint32_t pantsScore = 0;
    for (const auto& slot : a->bag.slots) {
        pantsScore = slot.item && slot.item->itemId == "QbPants" ? slot.item->gearScore : pantsScore;
    }
    check(ranged.ok() && pantsScore == 100, "GearScoreRange 100: the item of the event has the gear score 100");
    // A rank-up (120 + 40 reaches 150): its token of the territory pool has no owner: refused, nothing changes.
    const std::uint64_t rankup = shop.ready(kCharacterA, "Qd_Rankup", 6);
    const std::uint32_t before = a->progression.experience;
    const world::HandInResult refused = shop.host.world.quests().hand_in(*a, rankup);
    check(refused.error == world::QuestError::cannotPay && a->progression.experience == before && shop.standing(kCharacterA).points == 120
              && questfix::quest_of(*a, "Qd_Rankup") != nullptr,
          "a standing that gives a new rank is refused (the token of TerritoryRankBonus_6 has no owner): the quest stays ready");
    // A faction reward (roadmap 4.5) stays refused; a standing with no origin territory too.
    const world::HandInResult faction = shop.host.world.quests().hand_in(*a, shop.ready(kCharacterA, "Qd_Faction"));
    check(faction.error == world::QuestError::cannotPay && faction.detail.find("FactionReputation") != std::string::npos,
          "FactionReputation has no owner: the hand-in is refused");
}

/** The component Crafting of the player slice (the target of 1733; live check I0 of stage 1.8d: component 065458ab26042e65). */
constexpr std::uint64_t kCraftingComponent = 0x065458AB26042E65ULL;

/** QD2: the open of a loot container. */
void container_open(const Options&) {
    gamedata::SliceIds slice;
    slice.set(0x100, {{0x100, 0x1C116D259B43203DULL, server::rep::kConversationComponentName},
                      {0x100, kCraftingComponent, server::rep::kCraftingComponentName},
                      {0x100, server::rep::kInteractorComponent, "RootPlayer/Interactor"}});
    RewardShop shop(":memory:", &slice);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.inventory().give(kCharacterA, "QbPouch", 2).ok(), "control: A has 2 pouches");
    std::uint32_t slot = 0;
    world::InstanceId instance{};
    for (std::uint32_t index = 0; index < a->bag.slots.size(); ++index) {
        if (a->bag.slots[index].item && a->bag.slots[index].item->itemId == "QbPouch") {
            slot = index;
            instance = a->bag.slots[index].item->instance;
        }
    }
    const auto send = [&](const char* character, std::uint64_t target, std::uint32_t bagSlot, const world::InstanceId& id, std::uint32_t quantity) {
        const world::Entity* own = shop.entity(character);
        FakeClient& client = shop.client_of(character);
        server::hub::Writer body;
        body.u64(target);
        body.u64(own->id);
        body.u32(1);
        body.u32(bagSlot);
        body.raw(id);
        body.u32(0);
        body.u32(quantity);
        shop.host.datagram(client.peer, client.component_message(server::rep::kTypeInitiateSalvage, own->actor.bytes, body.data()));
        shop.host.steps(2, {&shop.clientA, &shop.clientB});
    };
    const std::uint64_t target = a->id + kCraftingComponent;
    const std::size_t from = shop.clientA.got.size();
    send(kCharacterA, target, slot, instance, 1);
    check(shop.count(kCharacterA, "QbPouch") == 1 && shop.count(kCharacterA, "QbPotion") == 5 && shop.count(kCharacterA, "QbStone") == 3
              && shop.host.world.item_containers().counts().opens == 1,
          "1733 of the entry Open: one pouch goes, the AND table gives 5 QbPotion and 3 QbStone");
    check(!fragments_of(shop.clientA, 61, 1755, from).empty(), "the session of A gets its bag state (the container state, slot 61)");
    const auto refused_now = [&](const char* text) {
        check(shop.count(kCharacterA, "QbPouch") == 1 && shop.count(kCharacterA, "QbPotion") == 5, text);
    };
    send(kCharacterA, target + 1, slot, instance, 1);
    refused_now("a facet that is not the Crafting component of A changes nothing");
    send(kCharacterA, target, slot, world::InstanceId{}, 1);
    refused_now("a wrong instance changes nothing");
    send(kCharacterA, target, slot, instance, 2);
    refused_now("a quantity above the stack changes nothing");
    check(shop.host.world.inventory().set_locked(kCharacterA, slot, nullptr, true).ok(), "control: A locks the pouch");
    send(kCharacterA, target, slot, instance, 1);
    refused_now("a locked pouch stays (the lock of the player)");
    check(shop.host.world.inventory().set_locked(kCharacterA, slot, nullptr, false).ok(), "control: the lock goes");
    // A full bag: the whole open is refused (the take frees the slot of the last pouch, the two items need two new slots).
    std::uint32_t given = 0;
    while (given < 600 && shop.host.world.inventory().give(kCharacterA, "QbStone", 1).ok() && ++given) {
    }
    const world::OpenResult full = shop.host.world.item_containers().open(*a, slot, instance, 1);
    check(!full.ok() && full.itemError == world::ItemError::bagFull && shop.count(kCharacterA, "QbPouch") == 1,
          "a full bag refuses the whole open: the pouch stays");
    check(shop.count(kCharacterB, "QbPotion") == 0 && shop.count(kCharacterB, "QbPouch") == 0, "B changes nothing");
}

/** QD3: the availability of an offer. */
void availability(const Options&) {
    RewardShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& quests = shop.host.world.quests();
    const auto available = [&](const char* id) { return quests.available(*a, *shop.host.quests.objective(id)); };
    check(shop.host.world.progress().set_level(kCharacterA, 4).ok() && available("Qd_Level") == world::QuestError::levelTooLow,
          "RequiredLevel 5 at the level 4: hidden (client 0x3A4C13E: the level index plus 1)");
    check(shop.host.world.progress().set_level(kCharacterA, 5).ok() && available("Qd_Level") == world::QuestError::none,
          "RequiredLevel 5 at the level 5: offered");
    check(available("Qd_Covenant") == world::QuestError::wrongFaction && available("Qd_Any") == world::QuestError::wrongFaction
              && available("Qd_NotNone") == world::QuestError::wrongFaction && available("Qd_NotCovenant") == world::QuestError::none,
          "the faction masks of the parser 0x445CA30 for an unaligned character: Covenant, Any, !None hidden; !Covenant (bit 0) offered");
    check(gamedata::faction_mask("") == 0xF && gamedata::faction_mask("None") == 0xF && gamedata::faction_mask("Any") == 0xE
              && gamedata::faction_mask("!Any") == 0x1 && gamedata::faction_mask("Syndicate") == 0x2 && gamedata::faction_mask("!Marauder") == 0xB
              && gamedata::faction_mask("!None") == 0xE,
          "the masks of the parser: blank 0xF, Any 0xE, !Any 0x1, Syndicate 0x2, !Marauder 0xB, !None 0xE");
    check(available("Qd_Arc") == world::QuestError::unsupportedGate, "a QuestArcTag (no rule of the service): hidden");
    check(available("Qd_Npa") == world::QuestError::none, "!NPA with a false NPA: offered");
    check(shop.host.world.ownership().unlock_achievement(*a, "Qb_Event_Bit", 907), "control: the bit of the NPA");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(available("Qd_Npa") == world::QuestError::notAvailable, "!NPA with a true NPA (Qb_Event_Bit && !Qb_Keep): hidden");
    check(quests.grant(*a, "Qd_VariantA", "command", 0).ok() && available("Qd_VariantB") == world::QuestError::alreadyActive,
          "a variant with the same completion achievement of an active quest: hidden (HasObjectiveWithCompletionAchievement 0x3A47310)");
    check(quests.available(*shop.entity(kCharacterB), *shop.host.quests.objective("Qd_VariantB")) == world::QuestError::none,
          "B has no variant: offered to B");
}

/** QD4: a timer across a reconnect, and a failure timer. */
void timer_reconnect(const Options&) {
    RewardShop shop;
    json q = reward_document();
    q["tasks"]["qd_timer"] = questfix::task("Qd_Timer", "tasktimer", 64, {{"Duration", "2.5"}, {"TP_DescriptionTag", "@Qd_Timer"}});
    q["tasks"]["qd_fail_timer"] = questfix::task("Qd_Fail_Timer", "tasktimer", 64, {{"Duration", "1.5"}, {"FailOnCondition", "1"}});
    q["tasks"]["qd_wait"] = questfix::task("Qd_Wait", "taskachievement", 18, {{"ItemName", "Qd_Never"}});
    q["tasks"]["qd_fail_root"] = questfix::task("Qd_Fail_Root", "simpletaskcontainer", 0);
    q["objectives"].push_back({{"id", "Qd_Timed"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "qd_timer", json::array()})})}});
    q["objectives"].push_back({{"id", "Qd_Failing"}, {"cells", {{"Type", "Journey"}}},
                               {"tree", json::array({json::array({0, "qd_fail_timer", json::array()}), json::array({1, "qd_wait", json::array()}),
                                                     json::array({2, "qd_fail_root", json::array({0, 1})})})}});
    check(shop.host.quests.load_text(q.dump()), "control: the timer quests load");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, "Qd_Timed", "command", 0).ok(), "control: A has the timer");
    const std::uint64_t start = questfix::quest_of(*a, "Qd_Timed")->tasks[0].startNs;
    shop.host.advance(std::chrono::milliseconds(1000));
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(3, {&shop.clientB});
    FakeClient again(27005);
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(5, {&again, &shop.clientB});
    a = shop.entity(kCharacterA);
    check(a != nullptr && questfix::quest_of(*a, "Qd_Timed")->tasks[0].startNs == start
              && questfix::quest_of(*a, "Qd_Timed")->tasks[0].state == world::quest_state::kActive,
          "a reconnect after 1 s: the timer keeps its start (no restart)");
    // The steps of the case move the clock too: the time to 2.4 s after the start, from the clock.
    shop.host.advance(std::chrono::milliseconds(static_cast<std::int64_t>(start + 2400000000ULL - shop.now_ns()) / 1000000));
    shop.host.steps(1, {&again, &shop.clientB});
    check(shop.now_ns() < start + 2500000000ULL && questfix::quest_of(*a, "Qd_Timed")->tasks[0].state == world::quest_state::kActive,
          "2.4 s after the start: still active (the fraction 2.5 s)");
    shop.host.advance(std::chrono::milliseconds(200));
    shop.host.steps(2, {&again, &shop.clientB});
    check(shop.host.world.quests().ready(*questfix::quest_of(*a, "Qd_Timed")), "2.6 s after the start: the timer is complete");
    check(shop.host.world.quests().grant(*a, "Qd_Failing", "command", 0).ok(), "control: the failure timer");
    shop.host.advance(std::chrono::milliseconds(1600));
    shop.host.steps(3, {&again, &shop.clientB});
    check(questfix::quest_of(*a, "Qd_Failing") == nullptr && shop.host.world.quests().counts().failures == 1,
          "the end of a failure timer fails its objective (the authored failure action)");
    shop.host.disconnect(again.peer);
    shop.host.steps(2, {&shop.clientB});
}

/** QD5: the repeat. */
void recurrence(const Options&) {
    // The cycles: 2026-10-22 (a Thursday) 04:00 UTC is 1792641600 s.
    RewardShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& quests = shop.host.world.quests();
    const world::HandInResult first = quests.hand_in(*a, shop.ready(kCharacterA, "Qd_Daily"));
    const std::uint64_t now = shop.now_ns();
    const std::uint64_t seconds = now / 1000000000ULL;
    const std::uint64_t expected = ((seconds < 18000 ? 0 : (seconds - 18000) / 86400 * 86400 + 18000) + 86400) * 1000000000ULL;
    note("QD5 daily reset " + std::to_string(first.resetNs) + " expected " + std::to_string(expected));
    check(first.ok() && first.resetNs == expected && a->quests.repeats.count(gamedata::quest_crc("Qd_Daily")) == 1
              && shop.host.store.list_quest_repeats(kCharacterA).size() == 1 && shop.host.store.list_quest_repeats(kCharacterA)[0].completions == 1,
          "a daily repeat: the reset at the next 05:00 UTC after the completion (client 0x616C670), a repeat row of the generation 1");
    check(quests.available(*a, *shop.host.quests.objective("Qd_Daily")) == world::QuestError::onCooldown,
          "the completion achievement in the cycle: the cooldown (the OnCooldown answer of the person)");
    const world::HandInResult free = quests.hand_in(*a, shop.ready(kCharacterA, "Qd_NoSchedule"));
    check(free.ok() && free.resetNs == 0 && quests.available(*a, *shop.host.quests.objective("Qd_NoSchedule")) == world::QuestError::completedBefore,
          "a repeat with no schedule: no reset (decision D4 of the user is open), the quest stays completed");
    const std::uint64_t due = (first.resetNs - now) / 1000000ULL + 1000;
    shop.host.advance(std::chrono::milliseconds(due));
    shop.report(shop.clientA, std::nullopt); // A datagram of each client right after the jump of the clock: the sessions stay.
    shop.report(shop.clientB, std::nullopt);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!shop.has_bit(kCharacterA, "Qd_Daily_Done") && shop.host.store.list_quest_repeats(kCharacterA).size() == 1
              && shop.host.store.list_quest_repeats(kCharacterA)[0].resetNs == 0 && shop.host.store.list_quest_repeats(kCharacterA)[0].completions == 1
              && quests.available(*a, *shop.host.quests.objective("Qd_Daily")) == world::QuestError::none && quests.counts().repeatResets == 1,
          "at the deadline of the world clock the reset removes the completion achievement (the row keeps the generation, no deadline): offered "
          "again");
    const world::HandInResult second = quests.hand_in(*a, shop.ready(kCharacterA, "Qd_Daily"));
    check(second.ok() && shop.host.store.list_quest_repeats(kCharacterA)[0].completions == 2 && second.resetNs == first.resetNs + 86400000000000ULL,
          "the second completion: the generation 2, the reset one day later");
    // An absence over the reset: the entry runs it.
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(3, {&shop.clientB});
    shop.host.advance(std::chrono::milliseconds(86400000ULL + 1000));
    shop.report(shop.clientB, std::nullopt);
    shop.host.steps(2, {&shop.clientB});
    FakeClient again(27006);
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(5, {&again, &shop.clientB});
    a = shop.entity(kCharacterA);
    check(a != nullptr && !shop.has_bit(kCharacterA, "Qd_Daily_Done") && shop.host.store.list_quest_repeats(kCharacterA)[0].resetNs == 0
              && shop.host.store.list_quest_repeats(kCharacterA)[0].completions == 2,
          "a reset whose time passed while the character was away runs at the entry");
    shop.host.disconnect(again.peer);
    shop.host.steps(2, {&shop.clientB});
}

/** QD6: no reward two times. */
void no_double_reward(const Options& options) {
    const std::string path = (options.workDirectory / "quest-rewards-qd6.sqlite3").string();
    std::filesystem::remove(path);
    RewardShop shop(path);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& quests = shop.host.world.quests();
    struct Owner {
        const char* table;
        const char* objective;
        std::uint16_t territory;
    };
    // The owners in the order that makes each one write a new row: the achievements first (their bits are new then).
    for (const Owner owner : {Owner{"achievements", "Qd_Full", 6}, Owner{"items", "Qd_Full", 6}, Owner{"character_progress", "Qd_Full", 6},
                              Owner{"progressions", "Qd_Full", 6}, Owner{"character_quest_repeats", "Qd_Daily", 0}}) {
        const std::uint64_t instance = shop.ready(kCharacterA, owner.objective, owner.territory);
        const std::uint32_t experience = a->progression.experience;
        const std::uint64_t coin = a->wallet.coin;
        const std::uint64_t standing = shop.standing(kCharacterA).points;
        const std::size_t repeats = a->quests.repeats.size();
        const std::string insert = std::string("CREATE TRIGGER qd6i BEFORE INSERT ON ") + owner.table + " BEGIN SELECT RAISE(FAIL, 'qd6'); END";
        const std::string update = std::string("CREATE TRIGGER qd6u BEFORE UPDATE ON ") + owner.table + " BEGIN SELECT RAISE(FAIL, 'qd6'); END";
        check(d_sql(path, insert.c_str()) && d_sql(path, update.c_str()), std::string("control: the store refuses a row of ") + owner.table);
        const world::HandInResult refused = quests.hand_in(*a, instance);
        check(refused.error == world::QuestError::storeFailed && a->progression.experience == experience && a->wallet.coin == coin
                  && shop.standing(kCharacterA).points == standing && questfix::quest_of(*a, owner.objective) != nullptr
                  && a->quests.repeats.size() == repeats,
              std::string("a refused write of ") + owner.table + ": no part of the reward, the quest stays ready");
        check(d_sql(path, "DROP TRIGGER qd6i") && d_sql(path, "DROP TRIGGER qd6u"), "control: the store takes the rows again");
        const world::HandInResult paid = quests.hand_in(*a, instance);
        const world::HandInResult twice = quests.hand_in(*a, instance);
        check(paid.ok() && !twice.ok(), std::string("after ") + owner.table + ": the retry pays one time, a second hand-in is refused");
        // The standing again at 0, so that the next 120 is no rank-up (a balance command of the skill system).
        (void)shop.host.world.skills().set_balance(kCharacterA, "6", 0, 0);
    }
    // A duplicate 794 after a reconnect: the person path refuses it.
    const std::uint64_t instance = shop.ready(kCharacterA, "Qb_Paid");
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.hand_in(kCharacterA, "Qb_Paid");
    const std::uint32_t experience = a->progression.experience;
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(3, {&shop.clientB});
    FakeClient again(27007);
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(5, {&again, &shop.clientB});
    a = shop.entity(kCharacterA);
    check(questfix::quest_of(*a, "Qb_Paid") == nullptr && quests.hand_in(*a, instance).error == world::QuestError::unknownInstance
              && a->progression.experience == experience,
          "a duplicate hand-in after a reconnect: refused, no second reward");
    // A full bag (decision D5 option A).
    const std::uint64_t full = shop.ready(kCharacterA, "Qd_Range");
    std::uint32_t given = 0;
    while (given < 600 && shop.host.world.inventory().give(kCharacterA, "QbStone", 1).ok() && ++given) {
    }
    const world::HandInResult bag = quests.hand_in(*a, full);
    check(bag.error == world::QuestError::bagFull && questfix::quest_of(*a, "Qd_Range") != nullptr && a->progression.experience == experience,
          "a full bag refuses the hand-in (D5 A): the quest stays ready, nothing is paid");
    shop.host.disconnect(again.peer);
    shop.host.steps(2, {&shop.clientB});
}

/** QD7: two sessions with different schedules, balances, and standings (MP-10, QG7). */
void two_sessions(const Options&) {
    RewardShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    world::Quests& quests = shop.host.world.quests();
    const std::uint64_t coinB = b->wallet.coin;
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    check(quests.hand_in(*a, shop.ready(kCharacterA, "Qd_Full", 6)).ok() && quests.hand_in(*a, shop.ready(kCharacterA, "Qd_Daily")).ok(),
          "control: A completes a quest with standing and a daily repeat");
    check(quests.hand_in(*b, shop.ready(kCharacterB, "Qd_Weekly")).ok(), "control: B completes a weekly repeat");
    check(b->wallet.coin == coinB + 100 && world::Skills::standing(b->skills, 6).points == 0 && b->quests.repeats.size() == 1
              && b->quests.repeats.count(gamedata::quest_crc("Qd_Weekly")) == 1 && a->quests.repeats.count(gamedata::quest_crc("Qd_Weekly")) == 0,
          "B has its own coin, no standing, and only its weekly repeat");
    // Code review of stage 1.8d, finding QR8D-R7: the host sends the changed states only at its steps. The wire check runs after the steps:
    // the owner state of A has the standing (territory 6, rank 0, 120 points), and B gets no categorical state of A.
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
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
    check(own, "the owner state of A (slot 65) has its standing: territory 6, rank 0, 120 points");
    check(!foreign, "the categorical state of A (the standing) does not go to B");
    const std::uint64_t daily = a->quests.repeats.begin()->second.resetNs;
    shop.host.advance(std::chrono::milliseconds((daily - shop.now_ns()) / 1000000ULL + 1000));
    shop.report(shop.clientA, std::nullopt);
    shop.report(shop.clientB, std::nullopt);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!shop.has_bit(kCharacterA, "Qd_Daily_Done") && b->quests.repeats.size() == 1 && shop.has_bit(kCharacterB, "Qd_Weekly_Done")
              && b->quests.repeats.begin()->second.resetNs != 0,
          "the daily reset of A does not change the weekly repeat of B");
}

/** QD8: the load of the repeats and of the timers. */
void repeat_load(const Options&) {
    RewardShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    world::Quests& quests = shop.host.world.quests();
    // 2,000 dormant repeats of 200 characters that are not in the world (rows of the store: no deadline, no work), 400 repeats of A with no
    // deadline (the generation only), one due repeat of A (a daily completion), and one weekly repeat of B (not due).
    // Code review of stage 1.8d (an examined note): 2,000 distinct rows (10 objectives of each of 200 characters), not 200 keys ten times.
    for (int index = 0; index < 2000; ++index) {
        (void)shop.host.store.write_quest_repeat("dormant-" + std::to_string(index / 10),
                                                 state::QuestRepeatRow{"Qd_Dormant_" + std::to_string(index % 10), 1, 1, 1});
    }
    for (std::uint32_t index = 0; index < 400; ++index) {
        a->quests.repeats[index + 1] = state::QuestRepeatRow{"Qd_Daily", 1, 1, 0};
    }
    check(quests.hand_in(*a, shop.ready(kCharacterA, "Qd_Daily")).ok() && quests.hand_in(*b, shop.ready(kCharacterB, "Qd_Weekly")).ok(),
          "control: A has a daily repeat, B a weekly one");
    const std::uint64_t scheduled = quests.counts().repeatsScheduled;
    const std::uint64_t due = quests.counts().repeatsDue;
    const std::uint64_t visits = quests.counts().subscribersVisited + quests.counts().questVisits;
    const std::uint64_t reset = a->quests.repeats.at(gamedata::quest_crc("Qd_Daily")).resetNs;
    const auto begin = std::chrono::steady_clock::now();
    shop.host.advance(std::chrono::milliseconds((reset - shop.now_ns()) / 1000000ULL + 1000));
    shop.report(shop.clientA, std::nullopt);
    shop.report(shop.clientB, std::nullopt);
    for (int step = 0; step < 200; ++step) {
        shop.host.steps(1, {&shop.clientA, &shop.clientB});
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    note("QD8 load: scheduled " + std::to_string(scheduled) + ", due " + std::to_string(quests.counts().repeatsDue - due) + ", visits "
         + std::to_string(quests.counts().subscribersVisited + quests.counts().questVisits - visits) + ", " + std::to_string(seconds) + " s");
    check(scheduled == 2 && quests.counts().repeatsDue - due == 1 && !shop.has_bit(kCharacterA, "Qd_Daily_Done")
              && shop.has_bit(kCharacterB, "Qd_Weekly_Done") && seconds < 2.0,
          "2 deadlines for 2,402 repeat rows: one due entry runs (the daily of A), the weekly of B waits; 200 steps after a day take less "
          "than 2 s (the work follows the due entries, not each quest of each player)");
}

/** QD9: a refine of a recipe with IsRefining counts for TaskCraftRecipe. */
void refine_counts(const Options&) {
    RewardShop shop;
    json q = reward_document();
    q["tasks"]["qd_cook"] = questfix::task("Qd_Cook", "taskcraftrecipe", 38, {{"CraftingRecipeId", "Test_Ration"}, {"TargetQty", "1"}});
    q["tasks"]["qd_refine"] = questfix::task("Qd_Refine", "taskrefinerecipe", 42, {{"CraftingRecipeId", "Test_Ration"}, {"TargetQty", "1"}});
    q["objectives"].push_back({{"id", "Qd_Cooking"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "qd_cook", json::array()})})}});
    q["objectives"].push_back({{"id", "Qd_Refining"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "qd_refine", json::array()})})}});
    check(shop.host.quests.load_text(q.dump()), "control: the craft quests load");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, "Qd_Cooking", "command", 0).ok() && shop.host.world.quests().grant(*a, "Qd_Refining", "command", 0).ok(),
          "control: the craft task and the refine task");
    world::QuestEvent refined;
    refined.kind = world::QuestEventKind::refine;
    refined.keys = {"Test_Ration"};
    shop.host.world.quests().event(*a, refined);
    check(shop.host.world.quests().ready(*questfix::quest_of(*a, "Qd_Cooking")) && shop.host.world.quests().ready(*questfix::quest_of(*a, "Qd_Refining")),
          "a refine of the recipe completes TaskCraftRecipe (the cooking of 06_boarfeast at the camp) and TaskRefineRecipe");
    world::QuestEvent crafted;
    crafted.kind = world::QuestEventKind::craft;
    crafted.keys = {"Test_Ration"};
    check(shop.host.world.quests().grant(*shop.entity(kCharacterB), "Qd_Refining", "command", 0).ok(), "control: B has the refine task");
    shop.host.world.quests().event(*shop.entity(kCharacterB), crafted);
    check(!shop.host.world.quests().ready(*questfix::quest_of(*shop.entity(kCharacterB), "Qd_Refining")),
          "a craft (not a refine) does not count for TaskRefineRecipe");
}

/** QD10: the first instance id of a run. */
void instance_start(const Options&) {
    RewardShop shop;
    // A ready quest of the store from an earlier run (the instance 2: the largest id of the store, B has the instance 1), the world entry, its hand-in, then a new grant: the
    // first new instance of the run must not take the id of the removed row (live run of stage 1.8d: 0x13 again).
    state::QuestRow other;
    other.instance = 1;
    other.objective = "Qb_Pouch";
    other.objectiveCrc = gamedata::quest_crc("Qb_Pouch");
    other.acceptNs = 1;
    other.tasks = {{0, "Qb_Ready_Unlock", gamedata::quest_crc("Qb_Ready_Unlock"), 1, world::quest_state::kComplete, 0}};
    check(shop.host.store.write_quest(kCharacterB, other), "control: a stored quest of B (instance 1)");
    state::QuestRow row;
    row.instance = 2;
    row.objective = "Qb_Paid";
    row.objectiveCrc = gamedata::quest_crc("Qb_Paid");
    row.acceptNs = 1;
    row.tasks = {{0, "Qb_Ready_Unlock", gamedata::quest_crc("Qb_Ready_Unlock"), 1, world::quest_state::kComplete, 0}};
    check(shop.host.store.write_quest(kCharacterA, row), "control: a stored ready quest of an earlier run");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().hand_in(*a, 2).ok(), "control: the hand-in removes the row of the instance 2 (the largest)");
    const world::QuestResult next = shop.host.world.quests().grant(*a, "Qb_Pouch", "command", 0);
    note("QD10 next instance " + std::to_string(next.instance));
    check(next.ok() && next.instance != 2, "a new instance after a hand-in does not take the id of the completed instance of the same world entry");
}

/** QD11: a quest whose root is satisfied holds its StayActive tasks (06_crewmates: the hand-in is outside the place of task 6). */
void ready_root_holds(const Options&) {
    ServerHost host;
    json document = questfix::quest_document();
    document["tasks"]["test_wait_flag"]["cells"]["StayActive"] = "1";
    // A second objective whose root waits for a third child (an achievement that the data does not know): it is not ready.
    document["tasks"]["test_wait_more"] = questfix::task("Test_Wait_More", "taskachievement", 18, {{"ItemName", "Test_Never_Known"}});
    document["objectives"].push_back({{"id", "Test_WaitLong"},
                                      {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "test_wait_flag", json::array()}), json::array({1, "test_wait_done", json::array()}),
                                                            json::array({2, "test_wait_more", json::array()}),
                                                            json::array({3, "test_wait_root", json::array({0, 1, 2})})})}});
    check(host.quests.load_text(document.dump()), "setup: a StayActive TaskAchievement in a ready quest and in a waiting quest");
    const Player playerA = add_player(host.store, kSteamIdA, "Tester", questfix::kQuestA, "Readyroot", state::WorldPosition{100, 200, 30});
    const Player playerB = add_player(host.store, kSteamIdB, "Other", questfix::kQuestB, "Waitroot", state::WorldPosition{120, 200, 30});
    FakeClient clientA(27100);
    FakeClient clientB(27101);
    host.enter(clientA, playerA.registration());
    host.enter(clientB, playerB.registration());
    world::Entity* a = host.world.find_character(questfix::kQuestA);
    world::Entity* b = host.world.find_character(questfix::kQuestB);
    world::Quests& quests = host.world.quests();
    check(quests.grant(*a, "Test_WaitQuest", "command", 0).ok() && quests.grant(*b, "Test_WaitLong", "command", 0).ok(), "setup: the two quests");
    check(host.world.ownership().unlock_achievement(*a, "Test_Flag", 22) && host.world.ownership().unlock_achievement(*b, "Test_Flag", 22),
          "setup: the conditional is true for each character");
    check(quests.ready(*questfix::quest_of(*a, "Test_WaitQuest")) && !quests.ready(*questfix::quest_of(*b, "Test_WaitLong")),
          "setup: the quest of A is ready, the quest of B waits for its third child");
    check(host.world.ownership().lock_achievement(*a, "Test_Flag") && host.world.ownership().lock_achievement(*b, "Test_Flag"),
          "the conditional becomes false for each character");
    // A later plan of A visits the held wait in its settle rounds: it must end (a wait that does not match its value in each round would
    // end the plan with no fixed point, and the plan would change nothing).
    check(quests.grant(*a, "Test_LockQuest", "command", 0).ok(), "a later grant of A: the settle rounds end with the held wait");
    const world::QuestInstance* readyA = questfix::quest_of(*a, "Test_WaitQuest");
    const world::QuestInstance* waitB = questfix::quest_of(*b, "Test_WaitLong");
    note("A: " + questfix::states_text(*readyA) + "; B: " + questfix::states_text(*waitB));
    check(readyA->tasks[0].state == world::quest_state::kCompleteActive && quests.ready(*readyA),
          "the StayActive task of a ready quest holds: the quest stays ready (the walk back to the person)");
    check(waitB->tasks[0].state == world::quest_state::kActive && waitB->tasks[3].state == world::quest_state::kActive,
          "the StayActive task of a quest that is not ready regresses (QR8A-R3), in the other session");
    bool stored = false;
    for (const state::QuestRow& row : host.store.list_quests(questfix::kQuestA)) {
        stored = stored || (row.objective == "Test_WaitQuest" && row.tasks[0].state == world::quest_state::kCompleteActive);
    }
    check(stored, "the rows of A keep the satisfied task");
    check(quests.hand_in(*a, readyA->instance).ok(), "the hand-in of A after the conditional became false");
}

/** QF7 (stage 1.8e, I3): AzothReward goes into the Azoth balance (stage 3.7c); the cap; a blank chance; no cap data; two sessions. */
void azoth_reward(const Options&) {
    json document = reward_document();
    document["events"]["qf_azoth_success"] = {{"id", "Qf_Azoth_Success"}, {"UniversalExpAmount", "20"}, {"AzothReward", "30"}, {"AzothRewardChance", "1"}};
    document["events"]["qf_azoth_blank"] = {{"id", "Qf_Azoth_Blank"}, {"UniversalExpAmount", "20"}, {"AzothReward", "50"}};
    for (const char* id : {"Qf_Azoth", "Qf_Azoth_Again"}) {
        document["objectives"].push_back({{"id", id},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qf_Azoth_Success"}}},
                                          {"tree", json::array({json::array({0, "qb_ready_unlock", json::array()})})}});
    }
    document["objectives"].push_back({{"id", "Qf_Azoth_Blank"},
                                      {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qf_Azoth_Blank"}}},
                                      {"tree", json::array({json::array({0, "qb_ready_unlock", json::array()})})}});
    const auto load = [&](RewardShop& shop, bool crafting) {
        if (!shop.host.quests.load_text(document.dump())) {
            throw std::runtime_error("QF7: the quest data was not set: " + shop.host.quests.error());
        }
        if (crafting && !shop.host.crafting.set(procedural::procedural_document())) {
            throw std::runtime_error("QF7: the crafting data was not set: " + shop.host.crafting.error());
        }
    };
    RewardShop shop;
    load(shop, true);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    world::Quests& quests = shop.host.world.quests();
    const std::uint32_t azothId = gamedata::progression_id(world::kAzothCurrency);
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    world::RewardJoin join;
    std::string detail;
    check(quests.reward_join(*a, *shop.host.quests.objective("Qf_Azoth"), 0, join, detail) == world::QuestError::none && join.azoth == 30
              && join.azothGain == 30 && join.azothCap == 2500,
          "the join: AzothReward 30 with AzothRewardChance 1, the cap 2,500 of the crafting data");
    const world::HandInResult paid = quests.hand_in(*a, shop.ready(kCharacterA, "Qf_Azoth"));
    bool stored = false;
    for (const auto& row : shop.host.store.list_skills(kCharacterA).ranks) {
        stored = stored || (row.id == azothId && row.points == 30);
    }
    note("QF7 hand-in: " + std::string(world::quest_error_text(paid.error)) + " " + paid.detail + ", Azoth " + std::to_string(paid.azoth));
    check(paid.ok() && paid.azoth == 30 && world::Skills::balance(a->skills, world::kAzothCurrency) == 30 && stored,
          "the hand-in adds 30 to the Azoth balance of A (the entity and the row)");
    check(world::Skills::balance(b->skills, world::kAzothCurrency) == 0, "B gets no Azoth");
    // The cap: a gain stops at 2,500 (AllowOverflow 0), and the hand-in pays the rest.
    (void)shop.host.world.skills().set_balance(kCharacterA, world::kAzothCurrency, 2490, 2500);
    const world::HandInResult capped = quests.hand_in(*a, shop.ready(kCharacterA, "Qf_Azoth_Again"));
    check(capped.ok() && capped.azoth == 10 && world::Skills::balance(a->skills, world::kAzothCurrency) == 2500,
          "at 2,490 of 2,500 the hand-in gives 10: the balance stops at the cap");
    // A blank AzothRewardChance: the default of the client loader is not read, the reward stays unpaid.
    const std::uint64_t blank = shop.ready(kCharacterB, "Qf_Azoth_Blank");
    const world::HandInResult refused = quests.hand_in(*b, blank);
    check(refused.error == world::QuestError::cannotPay && refused.detail.find("AzothRewardChance blank") != std::string::npos
              && world::Skills::balance(b->skills, world::kAzothCurrency) == 0 && questfix::quest_of(*b, "Qf_Azoth_Blank") != nullptr,
          "AzothReward with a blank chance is refused: the quest of B stays ready");
    // The owner view: the categorical state of A has its balance; B gets no state of A.
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    bool own = false;
    for (const Seen& seen : fragments_of(shop.clientA, 65, 911, fromA)) {
        const auto& fields = seen.fragment.fields;
        if (seen.interest != 1 || fields.count({0, 0}) == 0 || fields.count({0, 1}) == 0 || fields.count({0, 2}) == 0) {
            continue;
        }
        const WireRanks decoded = read_ranks({fields.at({0, 0}), fields.at({0, 1}), fields.at({0, 2})});
        for (const auto& row : decoded.entries) {
            own = own || (decoded.ok && row.id == azothId && row.points == 2500);
        }
    }
    bool foreign = false;
    for (const Seen& seen : fragments_of(shop.clientB, 65, 911, fromB)) {
        foreign = foreign || seen.interest != 1;
    }
    check(own && !foreign, "the owner state of A (slot 65) has the balance 2,500; B gets no categorical state of A");
    // With no cap in the crafting data the reward has no owner: refused, no change.
    RewardShop bare;
    load(bare, false);
    bare.enter();
    world::Entity* c = bare.entity(kCharacterA);
    const world::HandInResult noCap = bare.host.world.quests().hand_in(*c, bare.ready(kCharacterA, "Qf_Azoth"));
    check(noCap.error == world::QuestError::cannotPay && noCap.detail.find("no cap") != std::string::npos
              && world::Skills::balance(c->skills, world::kAzothCurrency) == 0,
          "with no cap of Azoth_Currency in the crafting data the hand-in is refused");
}

} // namespace

std::vector<Case> quest_reward_cases() {
    return {{"QD1", "quests 1.8d (I1): the reward join, the container item, GearScoreRange, the standing of the origin territory, the refusals", reward_join},
            {"QD2", "quests 1.8d (I1): the open of a loot container item (1733), its checks, a full bag", container_open},
            {"QD3", "quests 1.8d (I2): the availability: the level, the faction masks, an NPA, a gate, the variants", availability},
            {"QD4", "quests 1.8d (I3): a timer across a reconnect keeps its start; a failure timer fails its objective", timer_reconnect},
            {"QD5", "quests 1.8d (I4): the repeat: the daily cycle, the cooldown, the reset of the world clock, the generation, an absence", recurrence},
            {"QD6", "quests 1.8d (I5): no reward two times: a refused write at each owner, a duplicate 794, a full bag", no_double_reward},
            {"QD7", "quests 1.8d (I7): two sessions with different schedules, balances, and standings (MP-10)", two_sessions},
            {"QD8", "quests 1.8d (I7): the load of the repeats: the work follows the due entries", repeat_load},
            {"QD9", "quests 1.8d (I6): a refine of a recipe with IsRefining counts for TaskCraftRecipe", refine_counts},
            {"QD10", "quests 1.8d: a new instance does not take the id of a completed instance of the same world entry", instance_start},
            {"QD11", "quests 1.8d: a quest whose root is satisfied holds its StayActive tasks; a waiting quest regresses", ready_root_holds},
            {"QF7", "quests 1.8e (I3): AzothReward goes into the Azoth balance; the cap; a blank chance; no cap data; two sessions", azoth_reward}};
}

} // namespace soulwarden::backend::tests
