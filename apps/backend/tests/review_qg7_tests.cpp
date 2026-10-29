// The cases of the code review of stage QG7 (corrected in stage QG6a; docs/systems/factions.md, encounters.md, quests.md). The review
// (evidence/quests/qg7/code-review/FINDINGS.md) wrote them with made-up characters and data; each one fails on the tree of stage QG7.
//
//   QGR7A  finding QG7R-R1: a refused quest write rolls back the faction change (one transaction: the faction, the cost, the quest ends)
//   QGR7B  finding QG7R-R2: a self completing row tries again after a temporary write failure
//   QGR7C  finding QG7R-R2: a self completing row completes after a full bag gets a free slot
//   QGR7D  finding QG7R-R6: a change of the faction alone grants a pushed row whose faction gate now passes
//   QGR7E  finding QG7R-R7: 3161 on the route of another actor changes nothing
//   QGR7F  finding QG7R-R5: a history of the support manifest uses its earned faction progressions at a person offer
//   QGR7G  control of the review: the migration from schema 17 and the faction masks
//   QGR7H  finding QG7R-R3: DestroySliceAction through another alias removes the selected body of a group part
//   QGR7I  finding QG7R-R4: a child that removes the earlier object before a consumer child refuses the count

#include <sqlite3.h>

#include <cstdarg>
#include <cstdio>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include "gamedata/progression_tables.h"
#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/progress_requests.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_pattern_fixture.h"
#include "support/person_fixture.h"
#include "support/test_progression.h"
#include "world/factions.h"
#include "world/quest_support.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

using namespace personfix;
using nlohmann::json;

constexpr const char* kRecruit = "Test_Recruit_Syndicate";
constexpr const char* kRecruitMarauder = "Test_Recruit_Marauder";
constexpr const char* kRankUp = "Test_9806_S_0001_RankUp";
constexpr const char* kTrial = "Test_S_01_Trial";
constexpr const char* kNextRankUp = "Test_S_0102_RankUp";
constexpr const char* kMarauderOnly = "Test_Marauder_Only";
constexpr const char* kSwampLike = "Test_Faction_Reward";

/** The tables: test_progression with the three factions (the caps and the levels of the game data) and the change rules. */
gamedata::ProgressionData faction_tables(std::uint32_t costMax = 0, std::uint32_t costLevel = 20) {
    gamedata::ProgressionData data = test_progression();
    const auto row = [](std::uint8_t number, const char* reputation, const char* token, const char* achievement, const char* legacy,
                        std::vector<std::string> trials, std::vector<std::string> rankUps) {
        gamedata::FactionRow faction;
        faction.faction = number;
        faction.reputation = reputation;
        faction.token = token;
        faction.reputationCaps = {3000, 11000, 26000, 49000, 85000, 85000};
        faction.requiredLevels = {10, 20, 35, 50, 60, 61};
        faction.tokenCaps = {15000, 30000, 45000, 75000, 150000, 300000};
        faction.maxRank = 4;
        faction.expansionMaxRank = 5;
        faction.achievement = achievement;
        faction.legacyAchievement = legacy;
        faction.trials = std::move(trials);
        faction.rankUps = std::move(rankUps);
        return faction;
    };
    data.factions = {row(1, "Syndicate", "SyndicateTokens", "FactionIntro_Syndicate_Recruitment", "S_syndicatequest_1",
                         {"FactionIntro_Syndicate_Recruitment", "syndicate_faction_quest_01"}, {"FactionIntro_Syndicate_Recruitment", kRankUp}),
                     row(2, "Marauder", "MarauderTokens", "FactionIntro_Marauders_Recruitment", "S_marauderquest_1",
                         {"FactionIntro_Marauders_Recruitment"}, {"FactionIntro_Marauders_Recruitment"}),
                     row(3, "Covenant", "CovenantTokens", "FactionIntro_Covenant_Recruitment", "S_covenantquest_1",
                         {"FactionIntro_Covenant_Recruitment"}, {"FactionIntro_Covenant_Recruitment"})};
    data.factionChange = gamedata::FactionChange{2592000, costLevel, 0, costMax, 50};
    return data;
}

/** The conversation data: the persons of the person fixture, and the recruiter (the innkeeper) with the states of the c98 rows. */
json faction_conversations() {
    json document = conversation_document();
    for (json& npc : document["npcs"]) {
        if (npc["id"] == "Test_Innkeeper") {
            // The client takes the states in reverse: the last one whose requirements pass.
            npc["states"] = json::array({"Inn_Default", "Rec_Join", "Rec_Join_Marauder", "Rec_Cooldown", "Rec_Board"});
        }
    }
    json& states = document["states"];
    states.push_back({{"id", "Rec_Join"},
                      {"type", "Linear_Objective"},
                      {"requires", {{"RequiredActiveObjectiveId", kRecruit}, {"RequiredActiveTaskId", "Test_Rec_Join"}}},
                      {"services", json::array({"Faction_Selection", "PvP_Guide"})}});
    states.push_back({{"id", "Rec_Join_Marauder"},
                      {"type", "Linear_Objective"},
                      {"requires", {{"RequiredActiveObjectiveId", kRecruitMarauder}, {"RequiredActiveTaskId", "Test_RecM_Join"}}},
                      {"services", json::array({"Faction_Selection"})}});
    states.push_back({{"id", "Rec_Cooldown"},
                      {"type", "Linear_Objective"},
                      {"requires", {{"RequiredFactionCooldown", "InCooldownMultipleDays"}}},
                      {"topics", json::array({"Test_Topic_Ask"})}});
    states.push_back({{"id", "Rec_Board"}, {"type", "Linear_Objective"}, {"requires", {{"RequiredFaction", "Syndicate"}}},
                      {"services", json::array({"Faction_Board"})}});
    return document;
}

/** The quest data: the person quest document with a recruitment of the Syndicate and of the Marauders, a rank-up row, a trial, and gates. */
json faction_quests() {
    json document = person_quest_document();
    json& tasks = document["tasks"];
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id, type, code, cells);
    };
    // The recruitment (the form of FactionIntro_Syndicate_Recruitment): Partial[Consecutive[handoff unlock, TaskChooseFaction], decline topic].
    add("Test_Rec_Handoff", "taskunlockachievement", 20, {{"ItemName", "Test_Handoff"}, {"TargetQty", "1"}});
    add("Test_Rec_Join", "taskchoosefaction", 81, {{"ItemName", "Test_Innkeeper"}, {"StayActive", "1"}});
    add("Test_Rec_Container", "consecutivetaskcontainer", 1);
    add("Test_Rec_Decline", "taskconversationtopic", 17, {{"ItemName", "Test_Innkeeper"}, {"UIScreenTag", "Rec_Decline"}, {"TargetQty", "1"}});
    add("Test_Rec_Root", "partialtaskcontainer", 2);
    add("Test_RecM_Join", "taskchoosefaction", 81, {{"ItemName", "Test_Innkeeper"}, {"StayActive", "1"}});
    add("Test_RecM_Root", "consecutivetaskcontainer", 1);
    // The rank-up row (the form of 9806_S_0001_RankUp): reach 3000 Syndicate points.
    add("Test_Reach_Rank1", "taskhavelevel", 48, {{"ItemName", "Syndicate"}, {"TargetQty", "3000"}, {"ShouldCheckPoints", "1"}, {"StayActive", "1"}});
    add("Test_RankUp_Root", "consecutivetaskcontainer", 1);
    // The trial (ready at once) and the next rank-up row of its ObjectiveReward.
    add("Test_Trial_Unlock", "taskunlockachievement", 20, {{"ItemName", "Test_Trial_Bit"}});
    add("Test_Next_Reach", "taskhavelevel", 48, {{"ItemName", "Syndicate"}, {"TargetQty", "11000"}, {"ShouldCheckPoints", "1"}});
    add("Test_Next_Root", "consecutivetaskcontainer", 1);
    add("Test_Gate_Unlock", "taskunlockachievement", 20, {{"ItemName", "Test_Gate_Bit"}});
    add("Test_Reward_Unlock", "taskunlockachievement", 20, {{"ItemName", "Test_Reward_Bit"}});
    const auto tree = [](std::vector<std::tuple<int, const char*, std::vector<int>>> nodes) {
        json array = json::array();
        for (const auto& [index, id, children] : nodes) {
            array.push_back(json::array({index, gamedata::lower_text(id), children}));
        }
        return array;
    };
    json& objectives = document["objectives"];
    objectives.push_back({{"id", kRecruit},
                          {"cells",
                           {{"Type", "FactionStory_Syndicate"}, {"AchievementId", "FactionIntro_Syndicate_Recruitment"},
                            {"RequiredFaction", "!Syndicate"}, {"SuccessGameEventId", "Test_Recruit_Success"}}},
                          {"tree", tree({{0, "Test_Rec_Handoff", {}}, {1, "Test_Rec_Join", {}}, {2, "Test_Rec_Container", {0, 1}},
                                         {3, "Test_Rec_Decline", {}}, {4, "Test_Rec_Root", {2, 3}}})}});
    objectives.push_back({{"id", kRecruitMarauder},
                          {"cells",
                           {{"Type", "FactionStory_Marauders"}, {"AchievementId", "FactionIntro_Marauders_Recruitment"},
                            {"RequiredFaction", "!Marauder"}}},
                          {"tree", tree({{0, "Test_RecM_Join", {}}, {1, "Test_RecM_Root", {0}}})}});
    objectives.push_back({{"id", kRankUp},
                          {"cells",
                           {{"Type", "FactionStory_Syndicate"}, {"AchievementId", "FactionRank_S_0001_RankUp"}, {"RequiredFaction", "Syndicate"},
                            {"ProgressionReward", "Syndicate 1"}, {"SuccessGameEventId", "Test_RankUp_Success"},
                            {"ShouldResetOnFactionChange", "1"}}},
                          {"tree", tree({{0, "Test_Reach_Rank1", {}}, {1, "Test_RankUp_Root", {0}}})}});
    objectives.push_back({{"id", kTrial},
                          {"cells",
                           {{"Type", "FactionStory_Syndicate"}, {"NpcDestinationId", "Test_Innkeeper"}, {"AchievementId", "syndicate_faction_quest_01"},
                            {"RequiredFaction", "Syndicate"}, {"ProgressionReward", "SyndicateTokens 1 1500"}, {"ObjectiveReward", kNextRankUp},
                            {"SuccessGameEventId", "Test_Trial_Success"}, {"ShouldResetOnFactionChange", "1"}}},
                          {"tree", tree({{0, "Test_Trial_Unlock", {}}})}});
    objectives.push_back({{"id", kNextRankUp},
                          {"cells",
                           {{"Type", "FactionStory_Syndicate"}, {"AchievementId", "FactionRank_S_0102_RankUp"}, {"RequiredFaction", "Syndicate"},
                            {"ProgressionReward", "Syndicate 2"}, {"ShouldResetOnFactionChange", "1"}}},
                          {"tree", tree({{0, "Test_Next_Reach", {}}, {1, "Test_Next_Root", {0}}})}});
    objectives.push_back({{"id", kMarauderOnly},
                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Innkeeper"}, {"RequiredFaction", "Marauder"}}},
                          {"tree", tree({{0, "Test_Gate_Unlock", {}}})}});
    objectives.push_back({{"id", kSwampLike},
                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Innkeeper"}, {"SuccessGameEventId", "Test_Swamp_Success"}}},
                          {"tree", tree({{0, "Test_Reward_Unlock", {}}})}});
    document["events"] = {
        {"test_recruit_success", {{"id", "Test_Recruit_Success"}, {"CurrencyReward", "100"}}},
        {"test_rankup_success", {{"id", "Test_RankUp_Success"}, {"UniversalExpAmount", "10"}, {"FactionTokens", "100"}}},
        {"test_trial_success", {{"id", "Test_Trial_Success"}, {"UniversalExpAmount", "10"}, {"FactionReputation", "400"}}},
        {"test_swamp_success", {{"id", "Test_Swamp_Success"}, {"UniversalExpAmount", "5"}, {"FactionReputation", "1"}, {"FactionTokens", "1"}}},
    };
    std::uint32_t bit = 700;
    for (const char* id : {"Test_Handoff", "FactionIntro_Syndicate_Recruitment", "FactionIntro_Marauders_Recruitment",
                           "FactionIntro_Covenant_Recruitment", "S_syndicatequest_1", "S_marauderquest_1", "S_covenantquest_1",
                           "FactionRank_S_0001_RankUp", "syndicate_faction_quest_01", "FactionRank_S_0102_RankUp", "Test_Trial_Bit",
                           "Test_Gate_Bit", "Test_Reward_Bit"}) {
        document["achievements"].push_back(json::array({id, bit++}));
    }
    return document;
}

/** A host with the persons, the faction data, and the characters A and B near the recruiter. */
struct FactionShop : PersonShop {
    explicit FactionShop(const std::string& database = ":memory:", bool existing = false, std::uint32_t costMax = 0,
                         std::uint32_t costLevel = 20)
        : PersonShop(false, 0, database, nullptr, existing) {
        host.progression.set(faction_tables(costMax, costLevel));
        if (!host.conversations.load_text(faction_conversations().dump())) {
            throw std::runtime_error("faction test: the conversation data was not set: " + host.conversations.error());
        }
        if (!host.quests.load_text(faction_quests().dump())) {
            throw std::runtime_error("faction test: the quest data was not set: " + host.quests.error());
        }
    }

    world::Factions& factions() {
        return host.world.factions();
    }
    /** The request 3161 of the client of a character (the entity of the body: its own, or `entity`). */
    void request_faction(const char* character, std::uint8_t faction, std::optional<std::uint64_t> entity = std::nullopt) {
        const world::Entity* own = this->entity(character);
        FakeClient& client = client_of(character);
        server::hub::Writer body;
        body.u64(0x5008EB930DB9653BULL);
        body.u64(entity.value_or(own->id));
        body.u8(faction);
        host.datagram(client.peer, client.component_message(server::rep::kTypeRequestSetFaction, own->actor.bytes, body.data()));
        host.steps(3, {&clientA, &clientB});
    }
    void settle(int steps = 6) {
        host.steps(steps, {&clientA, &clientB});
    }
    [[nodiscard]] world::ProgressionRank progression(const char* character, const char* id) {
        const std::optional<world::SkillView> view = host.world.skills().view(character);
        return view ? world::Skills::progression_of(view->skills, gamedata::progression_id(id)) : world::ProgressionRank{};
    }
    [[nodiscard]] bool has(const char* character, const char* achievement) {
        const world::Entity* own = this->entity(character);
        return own != nullptr && host.world.quests().conditional(*own, achievement);
    }
    [[nodiscard]] const world::QuestInstance* active(const char* character, const char* objective) {
        const world::Entity* own = this->entity(character);
        for (const world::QuestInstance& quest : own->quests.active) {
            if (quest.objective == objective) {
                return &quest;
            }
        }
        return nullptr;
    }
};


bool review_sql(const std::string& path, const char* text) {
    sqlite3* db = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &db) == SQLITE_OK && sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

void faction_atomic_failure(const Options& options) {
    const auto path = options.workDirectory / "qg7-review-atomic.sqlite3";
    std::filesystem::remove(path);
    FactionShop shop(path.string(),false,120,0);
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    auto& quests = shop.host.world.quests();
    check(shop.factions().set(kCharacterA, world::FactionCommand{1, 0, 2}).ok(), "control: A is Syndicate");
    check(shop.host.world.skills().set_balance(kCharacterA,world::kAzothCurrency,150,2500).ok(), "control: 150 Azoth before a 100 Azoth change");
    const auto quest = quests.grant(*a, kTrial, "test", 0);
    check(quest.ok() && shop.active(kCharacterA, kTrial), "control: an active reset-on-change trial");
    check(shop.host.world.ownership().unlock_achievement(*a, "FactionIntro_Marauders_Recruitment", 702), "control: Marauder recruitment");
    check(review_sql(path.string(), "CREATE TRIGGER qg7_fail BEFORE DELETE ON character_quests BEGIN SELECT RAISE(FAIL, 'review quest write'); END"), "control: reject quest deletion");
    const auto result = shop.factions().request(*a, 2);
    note("QGR71 accepted=" + std::to_string(result.ok()) + " faction=" + std::to_string(a->faction.faction)
        + " stored=" + std::to_string(shop.host.store.faction(kCharacterA).faction)
        + " oldTrial=" + std::to_string(shop.active(kCharacterA, kTrial) != nullptr)
        + " azoth=" + std::to_string(world::Skills::balance(a->skills,world::kAzothCurrency)));
    check(!result.ok() && a->faction.faction == 1 && shop.host.store.faction(kCharacterA).faction == 1 && world::Skills::balance(a->skills,world::kAzothCurrency)==150,
          "a refused quest write rolls back the faction change");
    check(shop.active(kCharacterA, kTrial) != nullptr && shop.entity(kCharacterB)->faction.faction == 0, "control: the trial and B stay");
    check(review_sql(path.string(), "DROP TRIGGER qg7_fail"), "control: allow quest deletion again");
    shop.settle(10);
    check(a->faction.faction == 1 || shop.active(kCharacterA, kTrial) == nullptr,
          "no old faction trial stays after the failed cancellation can be retried");
    if(a->faction.faction==2 && shop.active(kCharacterA,kTrial)) {
        const auto paid=quests.hand_in(*a,quest.instance);
        note("QGR71 oldTrialHandIn="+std::to_string(paid.ok())+" oldTokensRank="+std::to_string(shop.progression(kCharacterA,"SyndicateTokens").rank)
             +" newFactionRep="+std::to_string(shop.progression(kCharacterA,"Marauder").points));
        check(!paid.ok(),"the changed character cannot cash the old faction trial");
    }
}

void self_hand_in_retry(const Options& options) {
    const auto path = options.workDirectory / "qg7-review-retry.sqlite3";
    std::filesystem::remove(path);
    FactionShop shop(path.string());
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    check(shop.factions().set(kCharacterA, world::FactionCommand{1, 0, 0}).ok(), "control: A is Syndicate");
    const auto granted = shop.host.world.quests().grant(*a, kRankUp, "test", 0);
    check(granted.ok(), "control: the rank-up row is active");
    check(shop.host.world.skills().set_faction_progress(kCharacterA, gamedata::progression_id("Syndicate"), 0, 3000).ok(), "control: reach the required reputation");
    check(review_sql(path.string(), "CREATE TRIGGER qg7_retry BEFORE DELETE ON character_quests BEGIN SELECT RAISE(FAIL, 'review temporary write'); END"), "control: temporarily reject the hand-in");
    shop.settle(8);
    check(shop.active(kCharacterA, kRankUp) && shop.host.world.quests().ready(*shop.active(kCharacterA, kRankUp))
          && !shop.has(kCharacterA, "FactionRank_S_0001_RankUp"), "control: the failed hand-in leaves the ready rank-up row");
    const auto failures = shop.host.world.quests().counts().handInRefusals;
    check(review_sql(path.string(), "DROP TRIGGER qg7_retry"), "control: the store takes the hand-in again");
    shop.settle(50);
    note("QGR72 ready=" + std::to_string(shop.active(kCharacterA, kRankUp) != nullptr)
         + " rank=" + std::to_string(shop.progression(kCharacterA, "Syndicate").rank)
         + " retries=" + std::to_string(shop.host.world.quests().counts().handInRefusals - failures));
    check(shop.active(kCharacterA, kRankUp) == nullptr && shop.progression(kCharacterA, "Syndicate").rank == 1,
          "a self-completing row retries after a temporary write failure");
    if (shop.active(kCharacterA, kRankUp)) {
        check(shop.host.world.quests().hand_in(*a, granted.instance).ok(), "control: an explicit hand-in now succeeds");
    }
}


void self_hand_in_full_bag(const Options&) {
    FactionShop shop;
    gamedata::ItemDefinition fill; fill.id="ReviewFill"; fill.type="Resource"; fill.itemClass="Resource"; fill.maxStack=1;
    auto box=fill; box.id="ReviewBox";
    shop.host.catalog.set({fill,box});
    auto data=faction_quests();
    data["events"]["test_rankup_success"]["ItemReward"]="ReviewBox";
    data["events"]["test_rankup_success"]["ItemRewardQty"]="1";
    check(shop.host.quests.load_text(data.dump()),"control: the automatic row has one reward item");
    shop.enter();
    auto* a=shop.entity(kCharacterA);
    check(shop.factions().set(kCharacterA,world::FactionCommand{1,0,0}).ok(),"control: A is Syndicate");
    const auto row=shop.host.world.quests().grant(*a,kRankUp,"test",0);
    check(row.ok()&&shop.host.world.inventory().give(kCharacterA,"ReviewFill",world::kMaxContainerSlots).ok(),"control: fill all 500 bag slots");
    check(shop.host.world.skills().set_faction_progress(kCharacterA,gamedata::progression_id("Syndicate"),0,3000).ok(),"control: reach 3000 reputation");
    shop.settle(10);
    check(shop.active(kCharacterA,kRankUp)&&shop.host.world.quests().hand_in(*a,row.instance).error==world::QuestError::bagFull,
          "control: the automatic row is ready and only the full bag refuses it");
    check(shop.host.world.inventory().remove(kCharacterA,0).ok(),"control: free one bag slot");
    shop.settle(50);
    note("QGR72B activeAfterFreeSlot="+std::to_string(shop.active(kCharacterA,kRankUp)!=nullptr)+" rank="+std::to_string(shop.progression(kCharacterA,"Syndicate").rank));
    check(shop.active(kCharacterA,kRankUp)==nullptr&&shop.progression(kCharacterA,"Syndicate").rank==1,
          "the automatic row completes after space is available");
    if(shop.active(kCharacterA,kRankUp)) check(shop.host.world.quests().hand_in(*a,row.instance).ok(),"control: an explicit hand-in now succeeds");
}

void faction_push_refresh(const Options&) {
    FactionShop shop;
    auto data = faction_quests();
    for (auto& objective : data["objectives"]) {
        if (objective["id"] == kMarauderOnly) objective["cells"]["PushObjectiveOnReqMet"] = "1";
    }
    check(shop.host.quests.load_text(data.dump()), "control: a pushed row requires Marauder");
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    check(shop.active(kCharacterA, kMarauderOnly) == nullptr, "control: the push does not pass while unaligned");
    check(shop.factions().set(kCharacterA, world::FactionCommand{2, 0, 0}).ok(), "control: change only the faction");
    shop.settle(10);
    note("QGR73 available=" + std::to_string(shop.host.world.quests().available(*a, *shop.host.quests.objective(kMarauderOnly)) == world::QuestError::none)
          + " active=" + std::to_string(shop.active(kCharacterA, kMarauderOnly) != nullptr));
    check(shop.active(kCharacterA, kMarauderOnly) != nullptr, "a faction change retries a pushed row whose faction gate now passes");
}

void wrong_actor_route(const Options&) {
    FactionShop shop;
    shop.enter();
    auto* a=shop.entity(kCharacterA);
    auto* b=shop.entity(kCharacterB);
    check(shop.factions().set(kCharacterA, world::FactionCommand{1,0,0}).ok()
          && shop.host.world.ownership().unlock_achievement(*a,"FactionIntro_Marauders_Recruitment",702), "control: A may change to Marauder");
    server::hub::Writer body;
    body.u64(0x5008EB930DB9653BULL);
    body.u64(a->id);
    body.u8(2);
    shop.host.datagram(shop.clientA.peer,shop.clientA.component_message(server::rep::kTypeRequestSetFaction,b->actor.bytes,body.data()));
    shop.settle();
    note("QGR74 factionAfterWrongRoute="+std::to_string(a->faction.faction));
    check(a->faction.faction==1 && b->faction.faction==0,"3161 addressed to another actor cannot change A");
}


void manifest_progression_offer(const Options& options) {
    FactionShop shop;
    auto quests=faction_quests();
    for(auto& row:quests["objectives"]) {
        if(row["id"]==kRecruit) {
            row["tree"]=json::array({json::array({0,"test_rec_join",json::array()})});
        }
    }
    quests["events"]["test_recruit_success"]["FactionReputation"]="3000";
    check(shop.host.quests.load_text(quests.dump()),"control: recruitment grants 3000 reputation");
    auto conversations=faction_conversations();
    for(auto& npc:conversations["npcs"]) if(npc["id"]=="Test_Innkeeper") npc["states"].push_back("Review_Rank_Offer");
    conversations["states"].push_back({{"id","Review_Rank_Offer"},{"type","Linear_Objective"},
        {"requires",{{"RequiredFaction","Syndicate"},{"RequiredProgression","Syndicate 1 3000"}}},
        {"objectives",json::array({kTrial})}});
    check(shop.host.conversations.load_text(conversations.dump()),"control: recruiter offers trial at rank 1 and 3000 reputation");
    world::QuestSupport support(shop.host.world);
    world::SupportProfile profile;
    profile.id="review:faction"; profile.group="review"; profile.level=60; profile.starts={kRecruit}; profile.faction=1;
    const auto manifest=support.manifest({profile});
    note("QGR77 profile="+manifest["profiles"][0].dump());
    const auto& played=manifest["profiles"][0]["played"];
    check(std::find(played.begin(),played.end(),json(kTrial))!=played.end(),"history completes the trial offered by its earned faction rank");
    shop.enter();
    auto* a=shop.entity(kCharacterA);
    check(shop.factions().set(kCharacterA,world::FactionCommand{1,0,0}).ok()
          && shop.host.world.skills().set_faction_progress(kCharacterA,gamedata::progression_id("Syndicate"),1,3000).ok(),
          "control: the real world has the rank and reputation earned by the history");
    const auto* npc=shop.host.conversations.npc("Test_Innkeeper");
    const auto selected=npc?shop.host.world.persons().selected_state(*a,*npc):-1;
    check(selected>=0 && shop.host.conversations.state(static_cast<std::uint32_t>(selected)).id=="Review_Rank_Offer"
          && shop.host.world.quests().available(*a,*shop.host.quests.objective(kTrial))==world::QuestError::none,
          "control: the real recruiter selects the trial offer at these faction values");

}

void migration_and_masks(const Options& options) {
    const auto path=options.workDirectory / "qg7-review-migration.sqlite3";
    std::filesystem::remove(path);
    {
        FactionShop shop(path.string());
        check(shop.host.world.skills().set_faction_progress(kCharacterA,gamedata::progression_id("Mining"),0,0).error==world::ProgressError::unknownProgression,
              "control: faction command refuses an ordinary trade skill");
        check(shop.host.world.skills().set_balance(kCharacterA,world::kAzothCurrency,321,2500).ok(),"control: stored schema-17 character balance");
    }
    check(review_sql(path.string(),"DROP TABLE character_factions; PRAGMA user_version=17"),"control: exact schema-17 shape with two characters");
    FactionShop shop(path.string(),true);
    check(shop.factions().view(kCharacterA)->faction==0 && shop.factions().view(kCharacterB)->faction==0,
          "schema-18 migration leaves both characters unaligned");
    check(world::Skills::balance(shop.host.world.skills().view(kCharacterA)->skills,world::kAzothCurrency)==321,
          "migration keeps character progression rows");
    shop.enter();
    auto* a=shop.entity(kCharacterA);
    const auto* row=shop.host.quests.objective(kMarauderOnly);
    for(unsigned faction=0;faction<=3;++faction) {
        check(shop.factions().set(kCharacterA,world::FactionCommand{static_cast<std::uint8_t>(faction),0,0}).ok(),"control: set test faction");
        for(const auto& [text,mask]:std::vector<std::pair<const char*,unsigned>>{{"None",15},{"Any",14},{"Syndicate",2},{"Marauder",4},{"Covenant",8},{"!Syndicate",13}}) {
            auto test=*row;
            test.factionMask=gamedata::faction_mask(text);
            const bool expected=(mask&1)||((mask>>faction)&1);
            check((shop.host.world.quests().available(*a,test)==world::QuestError::none)==expected,"the faction mask has the documented bits and bypass");
        }
    }
}

namespace encounter_review {
using namespace encfix;
using questfix::json;

/** A note of the log of a case with the values of the review (printf form). */
void report(const char* format, ...) {
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    std::string line = text;
    while (!line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    note(line);
}

constexpr const char* kGroup = "slices/test/review_group.dynamicslice";
constexpr const char* kBody = "slices/test/bag-wolf.dynamicslice";
constexpr const char* kBodyAlias = "sharedassets/test/review_body.aliasasset";

json group_members(bool bodyAlias = false) {
    json members = pattern_members();
    json part{{"name", "Review part"}, {"vitals", "Test_Member"}, {"category", "Test_Member_Category"},
              {"body", bodyAlias ? kBodyAlias : kBody}, {"bodySlices", {kBody}}, {"offset", {0,0,0}}, {"yaw", 0}};
    members[kGroup] = {{"kind", "creature"}, {"vitals", "Test_Member"}, {"body", kBody}, {"group", {part,part}}};
    return members;
}

struct ReviewShop : PatternShop {
    ReviewShop(const std::vector<json>& recipes, json members = group_members()) : PatternShop(recipes, std::move(members)) {
        host.creatures.set_members({{kMemberSlice,{1,""}}, {kBossSlice,{1,""}},
                                   {std::string(kGroup)+"#0",{1,""}}, {std::string(kGroup)+"#1",{1,""}}});
    }
};

json destroy_action(const char* slice) {
    return {{"type","DestroySliceAction"},{"slice",slice},{"alias",nullptr},{"aliasSlices",json::array()},{"localOnly",false}};
}


void destroy_selected_alias(const Options&) {
    json recipe = pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Make group",0x100,kSetupSlot,false,timed(4),json::array({spawn_json(kGroup,0,1,kMemberAt)})),
                     stage_json("Remove selected body",0x200,kCleanupSlot,false,timed(100),json::array())}), {0,1});
    auto action=destroy_action("");
    action["slice"]=nullptr;
    action["alias"]="sharedassets/test/cleanup_body.aliasasset";
    action["aliasSlices"]={kBody};
    recipe["stages"][1]["startActions"]=json::array({action});
    auto unrelated=recipe;
    unrelated["stages"][1]["startActions"][0]["aliasSlices"]={"slices/test/unselected.dynamicslice"};
    ReviewShop other({unrelated},group_members(true));
    begin(other); other.wait(5.0F);
    check(other.members(false).size()==2,"control: a cleanup alias that does not contain the selected body leaves it alive");
    ReviewShop shop({recipe},group_members(true));
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty() && shop.members(false).size()==2,
          "control: two parts use the selected body in the cleanup alias");
    shop.wait(5.0F);
    report("QGR75 remaining=%zu removed=%llu",shop.members(false).size(),
           static_cast<unsigned long long>(shop.encounters().counts().slicesDestroyed));
    check(shop.members(false).empty(),"DestroySliceAction through another alias removes the selected body slice");
}

void child_removes_before_count(const Options&) {
    json recipe=pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Earlier object",0x100,kSetupSlot,false,timed(3),json::array({spawn_json(kStartSlice,0,1,kStartAt)})),
                     stage_json("Consumer parent",0x200,kChildSlotA,true,destroyed(1,kStartSlice),json::array()),
                     stage_json("Remove object child",0x300,kChildSlotB,false,timed(1),json::array()),
                     stage_json("Consume object child",0x400,kInteractSlot,true,destroyed(1,kStartSlice),json::array()),
                     stage_json("Cleanup",0x500,kCleanupSlot,false,timed(100),json::array())}),{0,1,4});
    recipe["stages"][1]["children"]={2,3};
    recipe["stages"][1]["childrenOptional"]=false;
    recipe["stages"][2]["startActions"]=json::array({destroy_action(kStartSlice)});
    auto surviving=recipe;
    surviving["stages"][2]["startActions"]=json::array();
    ReviewShop control(std::vector<json>{surviving});
    begin(control); control.wait(5.0F);
    check(control.encounters().placements()[0].refused.empty()&&control.members(true).size()==1,
          "control: the same child sequence with no removal keeps the required source");
    ReviewShop shop(std::vector<json>{recipe});
    begin(shop);
    const auto& placement=shop.encounters().placements()[0];
    report("QGR76 refused=%zu",placement.refused.size());
    check(!placement.refused.empty(),"a child cleanup before the consumer must remove the earlier-object source count");
    if(!placement.refused.empty()) return;
    shop.wait(5.0F);
    const auto* run=shop.instance();
    report("QGR76 childStarted=%d childEnded=%d count=%u objects=%zu",run&&run->stages[3].started,
           run&&run->stages[3].ended,run?run->stages[3].counts[0]:999U,shop.members(true).size());
    check(run&&run->stages[3].started&&!run->stages[3].ended&&shop.members(true).empty(),
          "control: accepted consumer has no remaining object");
}
} // namespace encounter_review

} // namespace

std::vector<Case> review_qg7_cases() {
    return {{"QGR7A", "review of QG7, R1: a refused quest write rolls back the faction change", faction_atomic_failure},
            {"QGR7B", "review of QG7, R2: a self completing row tries again after a temporary write failure", self_hand_in_retry},
            {"QGR7C", "review of QG7, R2: a self completing row completes after its full bag gets a free slot", self_hand_in_full_bag},
            {"QGR7D", "review of QG7, R6: a change of the faction alone grants a pushed row of that faction", faction_push_refresh},
            {"QGR7E", "review of QG7, R7: 3161 on the route of another actor changes nothing", wrong_actor_route},
            {"QGR7F", "review of QG7, R5: a manifest history uses its earned faction progressions at a person offer", manifest_progression_offer},
            {"QGR7G", "review of QG7 (control): the migration from schema 17 and the faction masks", migration_and_masks},
            {"QGR7H", "review of QG7, R3: DestroySliceAction through another alias removes the selected body of a group part",
             encounter_review::destroy_selected_alias},
            {"QGR7I", "review of QG7, R4: a child that removes the earlier object before a consumer child refuses the count",
             encounter_review::child_removes_before_count}};
}
} // namespace soulwarden::backend::tests
