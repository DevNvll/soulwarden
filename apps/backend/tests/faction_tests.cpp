// The cases of stage QG7: the factions of a character (docs/systems/factions.md). Made-up ids, names, and tables with the shape of the game
// data (the faction rows of CategoricalProgression, the rank tables, the faction table, the validation lists of the base attributes).
//
//   QG7A  the Faction state and the join (I1): no fragment for a character with no faction; the conversation of the recruitment gives the
//         Faction_Selection service; 3161 joins the faction of the recruitment (time 0, count 0); the owner gets groups 1 and 2, the observer
//         group 2; TaskChooseFaction completes, the recruitment row completes itself, and the next rank-up row comes
//   QG7B  the refusals of a join (I1): each refusal changes nothing and sends the faction of the server to the owner (no fragment to the
//         observer): a faction that the data does not offer, Any, a body with a bad faction, the entity of another character, no service
//   QG7C  the change (I2): the first change has no wait; the next change waits 30 days of the wall clock; the cost formula (min(Max, Min +
//         count x Increment)) and its Azoth in the transaction of the change
//   QG7D  the reputation and the tokens (I3): the cap of the rank, the rank of a reward keeps the points, the categorical state of the owner
//   QG7E  the quest rules (I4): the RequiredFaction gate with the faction of the character, TaskHaveLevel of the faction points, the rank-up
//         row with its ProgressionReward and its FactionTokens, the trial hand-in with ProgressionReward and FactionReputation (and the
//         GainFactionRep card), no faction reward for a character with no faction, the end of ShouldResetOnFactionChange rows at a change
//   QG7F  a restart keeps the faction, the change time, the count, the reputation, and the tokens (I5)
//   QG7G  two sessions (I7, MP-10): two characters of different factions; each session gets the public faction of the other and only its
//         own balances
//   QG7H  the person rules: the faction board of the faction of the character, the faction cooldown states of a conversation
//   QG7I  the loader of the member `factions` of progression-tables.json, and the data tool (check_progression_tool.py)
//   QG7J  the faction commands set_faction and set_faction_progress are harness commands: the admin route refuses them with no
//         SOULWARDEN_HARNESS=1 and changes nothing

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
#include "support/person_fixture.h"
#include "support/test_progression.h"
#include "world/factions.h"
#include "world/skills.h"

namespace soulwarden::backend::tests {
namespace {

using namespace personfix;
using nlohmann::json;

constexpr std::uint32_t kFactionSlot = 49;
constexpr std::uint32_t kFactionType = 3152;
constexpr const char* kRecruit = "Test_Recruit_Syndicate";
constexpr const char* kRecruitMarauder = "Test_Recruit_Marauder";
constexpr const char* kRankUp = "Test_9806_S_0001_RankUp";
constexpr const char* kTrial = "Test_S_01_Trial";
constexpr const char* kNextRankUp = "Test_S_0102_RankUp";
constexpr const char* kMarauderOnly = "Test_Marauder_Only";
constexpr const char* kSwampLike = "Test_Faction_Reward";

std::string hex_of(const Bytes& data) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : data) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

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

/** The faction fragments of a client from a message index on: the (1,3) time, the (1,4) count, the (2,0) faction; `owner` the owner form. */
struct FactionSeen {
    std::size_t count{0};
    std::size_t owner{0};
    std::optional<std::uint8_t> last;
    std::string lastHex;
};

FactionSeen faction_fragments(const FakeClient& client, std::size_t from) {
    FactionSeen seen;
    for (const Seen& entry : fragments_of(client, kFactionSlot, kFactionType, from)) {
        ++seen.count;
        const auto field = entry.fragment.fields.find({2, 0});
        if (entry.fragment.fields.count({1, 3}) != 0) {
            ++seen.owner;
        }
        if (field != entry.fragment.fields.end() && field->second.size() == 1) {
            seen.last = field->second[0];
        }
    }
    return seen;
}

/** QG7A: the state and the join from the recruitment. */
void join_from_recruitment(const Options&) {
    FactionShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const FactionSeen first = faction_fragments(shop.clientA, 0);
    const FactionSeen firstB = faction_fragments(shop.clientB, 0);
    check(first.count == 0 && firstB.count == 0 && a->faction.faction == 0,
          "a character with no faction, the time 0, and the count 0 has no Faction fragment in its first record (owner and observer)");
    check(shop.host.world.quests().grant(*a, kRecruit, "test", 0).ok(), "control: A has the recruitment of the Syndicate");
    shop.settle();
    check(shop.host.world.quests().choose_faction_active(*a, 1) && !shop.host.world.quests().choose_faction_active(*a, 2),
          "the recruitment has its TaskChooseFaction task active (the Syndicate, not the Marauders)");
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->serviceGrant && std::find(a->serviceGrant->ids.begin(), a->serviceGrant->ids.end(), 4U) != a->serviceGrant->ids.end(),
          "the recruiter state with the active recruitment task gives the Faction_Selection service (4)");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.request_faction(kCharacterA, 1);
    const state::CharacterFactionRow row = shop.host.store.faction(kCharacterA);
    check(a->faction.faction == 1 && row.faction == 1 && row.changedNs == 0 && row.changes == 0,
          "3161 Syndicate joins: the entity and the row have the Syndicate, the time 0 (the first change has no wait), and the count 0");
    const FactionSeen ownerSeen = faction_fragments(shop.clientA, fromA);
    const FactionSeen observerSeen = faction_fragments(shop.clientB, fromB);
    check(ownerSeen.count == 1 && ownerSeen.owner == 1 && ownerSeen.last == 1,
          "the owner gets one Faction fragment with the time, the count, and the faction 1");
    check(observerSeen.count == 1 && observerSeen.owner == 0 && observerSeen.last == 1,
          "the observer B gets one Faction fragment with only the public faction 1 (group 2)");
    shop.settle(10);
    check(shop.has(kCharacterA, "FactionIntro_Syndicate_Recruitment") && shop.active(kCharacterA, kRecruit) == nullptr,
          "TaskChooseFaction completes, and the recruitment row (no NpcDestinationId) completes itself: its achievement is set");
    check(shop.active(kCharacterA, kRankUp) != nullptr,
          "the validation lists give the next rank-up row after the recruitment achievement (9806_S_0001_RankUp)");
    world::Entity encoder = *a;
    encoder.faction = world::FactionState{1, 0, 0, 0};
    const auto owner = server::rep::encode_faction_state(encoder, server::rep::View::owner, server::rep::PartsRequest{});
    const auto observer = server::rep::encode_faction_state(encoder, server::rep::View::observer, server::rep::PartsRequest{});
    const Bytes social = server::rep::encode_social_state(encoder, server::rep::View::owner, shop.host.clock);
    check(hex_of(social) == "0307" "000000000000000000" "71" "00010100" "00010100" "00010100" "00010100"
              && server::rep::encode_social_state(encoder, server::rep::View::observer, shop.host.clock).empty(),
          "the empty Social state of the owner (31 bytes with the slot and the type: research social.md, section A); no observer view");
    check(owner.size() == 1 && hex_of(owner[0]) == "06180000000000000000000001" "01"
              && observer.size() == 1 && hex_of(observer[0]) == "040101",
          "the bytes: the owner `06 18 <time 0> <count 0> 01 01`, an observer `04 01 01` (research factions.md 7.1)");
}

/** QG7B: the refusals of a join: no change, and the faction of the server for the owner only. */
void join_refusals(const Options&) {
    FactionShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(*a, kRecruit, "test", 0).ok(), "control: A has the recruitment of the Syndicate");
    shop.settle();
    const auto refused = [&](const char* what, std::uint8_t faction, std::optional<std::uint64_t> entity) {
        const std::size_t fromA = shop.clientA.got.size();
        const std::size_t fromB = shop.clientB.got.size();
        const std::uint64_t refusals = shop.factions().counts().refusals;
        shop.request_faction(kCharacterA, faction, entity);
        const FactionSeen owner = faction_fragments(shop.clientA, fromA);
        const FactionSeen observer = faction_fragments(shop.clientB, fromB);
        note(std::string("QG7B ") + what + ": owner fragments " + std::to_string(owner.count) + ", observer " + std::to_string(observer.count)
             + ", refusals " + std::to_string(shop.factions().counts().refusals - refusals));
        check(a->faction.faction == 0 && shop.host.store.faction(kCharacterA).faction == 0 && b->faction.faction == 0,
              std::string(what) + ": no change of the entity and of the row");
        check(owner.count == 1 && owner.last == 0 && observer.count == 0,
              std::string(what) + ": the owner gets the faction 0 of the server again (the HUD correction), the observer nothing");
    };
    refused("no Faction_Selection service (no conversation)", 1, std::nullopt);
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    refused("a faction that the data does not offer (the Marauders)", 2, std::nullopt);
    refused("Any (4): not a faction of a player", 4, std::nullopt);
    refused("a body with the faction 5 (the enum validator of the client accepts 0 to 4)", 5, std::nullopt);
    refused("the entity of another character", 1, b->id);
    const std::uint64_t joins = shop.factions().counts().joins;
    shop.request_faction(kCharacterA, 1);
    check(a->faction.faction == 1 && shop.factions().counts().joins == joins + 1, "control: the correct request joins after the refusals");
    shop.request_faction(kCharacterA, 1);
    check(a->faction.faction == 1 && shop.host.store.faction(kCharacterA).changes == 0, "a request of the faction that the character has changes nothing");
}

/** QG7C: the change, its wait, and its cost. */
void change_wait_cost(const Options&) {
    FactionShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.factions().set(kCharacterA, world::FactionCommand{1, std::nullopt, std::nullopt}).ok(), "control: A is in the Syndicate (the join)");
    check(shop.host.world.ownership().unlock_achievement(*a, "FactionIntro_Marauders_Recruitment", 702)
              && shop.host.world.ownership().unlock_achievement(*a, "FactionIntro_Syndicate_Recruitment", 701),
          "control: A has the recruitment achievements of the Marauders and of the Syndicate (the Bio change panel)");
    check(shop.factions().wait_left(a->faction) == 0 && shop.factions().change_cost(*a) == 0,
          "the time 0 of the join: no wait; the cost of a change at the level of A is 0 (the level index is below 20)");
    shop.request_faction(kCharacterA, 3);
    check(a->faction.faction == 1, "a change to the Covenant with no Covenant recruitment achievement is refused (not offered)");
    const std::uint64_t before = shop.factions().now_ns();
    shop.request_faction(kCharacterA, 2);
    const state::CharacterFactionRow row = shop.host.store.faction(kCharacterA);
    check(a->faction.faction == 2 && row.faction == 2 && row.changes == 1 && row.changedNs >= before && row.changedNs <= shop.factions().now_ns(),
          "the first change (Bio: the recruitment achievement, no service) is accepted with no wait: the time is now, the count 1");
    const std::uint64_t wait = shop.factions().wait_left(a->faction);
    check(wait > 2591990ULL * 1000000000ULL && wait <= 2592000ULL * 1000000000ULL, "the wait is 30 days (2,592,000 s) of the wall clock");
    shop.request_faction(kCharacterA, 1);
    check(a->faction.faction == 2 && shop.host.store.faction(kCharacterA).changes == 1, "a change in the wait is refused (OnCooldown): no change");
    // The time of the last change one minute short of the wait, then one minute past it (a command: the sessions stay; the world clock
    // does not jump 30 days).
    constexpr std::uint64_t kWaitNs = 2592000ULL * 1000000000ULL;
    constexpr std::uint64_t kMinuteNs = 60ULL * 1000000000ULL;
    check(shop.factions().set(kCharacterA, world::FactionCommand{2, shop.factions().now_ns() - kWaitNs + kMinuteNs, std::uint16_t{1}}).ok(),
          "control: the last change is 30 days minus one minute ago");
    shop.request_faction(kCharacterA, 1);
    check(a->faction.faction == 2, "one minute before the end of the wait: still refused");
    check(shop.factions().set(kCharacterA, world::FactionCommand{2, shop.factions().now_ns() - kWaitNs - kMinuteNs, std::uint16_t{1}}).ok(),
          "control: the last change is 30 days and one minute ago");
    shop.request_faction(kCharacterA, 1);
    check(a->faction.faction == 1 && shop.host.store.faction(kCharacterA).changes == 2, "after the wait the change is accepted: the count 2");
    // The cost formula of the client (0x5C41E30): min(Max, Min + count x Increment), from the level index Cost Level on.
    FactionShop paid(":memory:", false, 120, 0);
    paid.enter();
    world::Entity* c = paid.entity(kCharacterA);
    check(paid.factions().set(kCharacterA, world::FactionCommand{1, 0, std::uint16_t{2}}).ok()
              && paid.host.world.ownership().unlock_achievement(*c, "FactionIntro_Marauders_Recruitment", 702),
          "control: A is in the Syndicate with 2 changes, and has the Marauder recruitment");
    check(paid.factions().change_cost(*c) == 100, "the cost: min(120, 0 + 2 x 50) = 100 Azoth");
    paid.request_faction(kCharacterA, 2);
    check(c->faction.faction == 1, "a change with 0 Azoth is refused (InsufficientAzoth)");
    check(paid.host.world.skills().set_balance(kCharacterA, world::kAzothCurrency, 150, 2500).ok(), "control: A has 150 Azoth");
    paid.request_faction(kCharacterA, 2);
    check(c->faction.faction == 2 && world::Skills::balance(c->skills, world::kAzothCurrency) == 50
              && paid.host.store.faction(kCharacterA).changes == 3,
          "the change takes the 100 Azoth in its transaction: 50 stay, the count is 3");
    check(paid.factions().set(kCharacterA, world::FactionCommand{2, 0, std::uint16_t{9}}).ok() && paid.factions().change_cost(*c) == 120,
          "the cost stops at the Max: min(120, 9 x 50) = 120");
}

/** QG7D: the reputation and the tokens. */
void reputation_tokens(const Options&) {
    FactionShop shop;
    shop.enter();
    world::Skills& skills = shop.host.world.skills();
    const std::uint32_t syndicate = gamedata::progression_id("Syndicate");
    const std::uint32_t tokens = gamedata::progression_id("SyndicateTokens");
    check(syndicate == 0x56FBDE12U && tokens == 0xEBD7B082U, "the ids on the wire: Syndicate 56FBDE12, SyndicateTokens EBD7B082 (factions.md 3.1)");
    world::Skills::Plan plan = skills.plan_none(kCharacterA);
    std::int64_t gained = 0;
    check(skills.add_faction_points(plan, syndicate, 2500, &gained) == world::ProgressError::none && gained == 2500
              && skills.add_faction_points(plan, syndicate, 2500, &gained) == world::ProgressError::none && gained == 500
              && world::Skills::progression_of(plan.skills, syndicate).points == 3000,
          "a gain stops at the cap of the rank 0 (3000; AutoRankUp false, AllowOverflow false): 2500, then 500 of 2500");
    check(skills.add_faction_points(plan, syndicate, 10) == world::ProgressError::none && world::Skills::progression_of(plan.skills, syndicate).points == 3000,
          "at the cap a gain adds nothing (no rank-up by points)");
    check(skills.set_faction_rank(plan, syndicate, 1) == world::ProgressError::none
              && world::Skills::progression_of(plan.skills, syndicate).rank == 1 && world::Skills::progression_of(plan.skills, syndicate).points == 3000
              && skills.add_faction_points(plan, syndicate, 9000) == world::ProgressError::none
              && world::Skills::progression_of(plan.skills, syndicate).points == 11000,
          "a rank keeps the points (the setter 0x3CBF9F0), and the next gain stops at the cap of the new rank (11000): the points are a total");
    check(skills.set_faction_rank(plan, syndicate, 5) == world::ProgressError::badRank && skills.set_faction_rank(plan, syndicate, 4) == world::ProgressError::none,
          "the test tables have no expansion: the largest rank is MaxLevel 4, and 5 is refused (Expansion01MaxLevel 5 needs the owned "
          "expansion, 0x3CB38E0)");
    check(skills.add_faction_points(plan, tokens, -1) == world::ProgressError::notEnoughBalance
              && skills.add_faction_points(plan, gamedata::progression_id("Mining"), 1) == world::ProgressError::unknownProgression,
          "a cost above the tokens is refused; a progression that is not a faction progression is refused");
    const std::size_t from = shop.clientA.got.size();
    check(skills.set_faction_progress(kCharacterA, tokens, 1, 2000).ok() && skills.set_faction_progress(kCharacterA, syndicate, 0, 1200).ok(),
          "the command: tokens rank 1 with 2000, reputation rank 0 with 1200");
    check(!skills.set_faction_progress(kCharacterA, syndicate, 0, 3001).ok() && shop.progression(kCharacterA, "Syndicate").points == 1200,
          "a command above the cap of the rank is refused, and changes nothing");
    shop.settle();
    bool found = false;
    for (const Seen& entry : fragments_of(shop.clientA, kCategoricalSlot, 911, from)) {
        found = found || entry.fragment.fields.count({0, 0}) != 0;
    }
    check(found, "the owner gets the categorical state (slot 65) with the entries");
}

/** QG7E: the quest rules of the factions. */
void quest_rules(const Options&) {
    FactionShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    world::Quests& quests = shop.host.world.quests();
    const gamedata::QuestObjective* marauderOnly = shop.host.quests.objective(kMarauderOnly);
    check(quests.available(*a, *marauderOnly) == world::QuestError::wrongFaction, "RequiredFaction Marauder: refused for a character with no faction");
    check(shop.factions().set(kCharacterA, world::FactionCommand{1, std::nullopt, std::nullopt}).ok()
              && shop.factions().set(kCharacterB, world::FactionCommand{2, std::nullopt, std::nullopt}).ok(),
          "control: A Syndicate, B Marauder");
    shop.settle();
    check(quests.available(*a, *marauderOnly) == world::QuestError::wrongFaction && quests.available(*b, *marauderOnly) == world::QuestError::none,
          "the gate with the faction of the character: refused for the Syndicate A, available for the Marauder B");
    const gamedata::QuestObjective* recruit = shop.host.quests.objective(kRecruit);
    check(quests.available(*a, *recruit) == world::QuestError::none,
          "!Syndicate has bit 0 (the bypass of the client test 0x3A4C166): the Syndicate recruitment is open to the Syndicate A too");
    shop.settle(10);
    check(shop.active(kCharacterA, kRankUp) == nullptr, "no rank-up row with no recruitment achievement (the validation lists)");
    check(shop.host.world.ownership().unlock_achievement(*a, "FactionIntro_Syndicate_Recruitment", 701), "control: the recruitment of A");
    shop.settle(10);
    const world::QuestInstance* rankUp = shop.active(kCharacterA, kRankUp);
    check(rankUp != nullptr, "the rank-up row after the recruitment achievement (the next row of the validation lists)");
    check(shop.host.world.skills().set_faction_progress(kCharacterA, gamedata::progression_id("Syndicate"), 0, 2999).ok(), "control: 2999 points");
    shop.settle(10);
    check(shop.active(kCharacterA, kRankUp) != nullptr, "TaskHaveLevel of 3000 Syndicate points waits at 2999");
    check(shop.host.world.skills().set_faction_progress(kCharacterA, gamedata::progression_id("Syndicate"), 0, 3000).ok(), "control: 3000 points");
    shop.settle(10);
    check(shop.active(kCharacterA, kRankUp) == nullptr && shop.has(kCharacterA, "FactionRank_S_0001_RankUp")
              && shop.progression(kCharacterA, "Syndicate").rank == 1 && shop.progression(kCharacterA, "Syndicate").points == 3000
              && shop.progression(kCharacterA, "SyndicateTokens").points == 100,
          "at 3000 the rank-up row completes itself: ProgressionReward Syndicate 1 (the points stay), FactionTokens 100 of its event");
    // The trial: ProgressionReward SyndicateTokens 1 1500 and FactionReputation 400; its ObjectiveReward is the next rank-up row.
    const world::QuestResult trial = quests.grant(*a, kTrial, "test", 0);
    shop.settle();
    const world::HandInResult handIn = quests.hand_in(*a, trial.instance);
    shop.settle();
    check(handIn.ok() && handIn.reputation == 400 && handIn.tokens == 0 && handIn.rankReward == "SyndicateTokens" && handIn.rankRewardRank == 1
              && shop.progression(kCharacterA, "SyndicateTokens").rank == 1 && shop.progression(kCharacterA, "SyndicateTokens").points == 1600
              && shop.progression(kCharacterA, "Syndicate").points == 3400 && shop.active(kCharacterA, kNextRankUp) != nullptr,
          "the trial hand-in: the token rank 1 and 1500 tokens (1600), the reputation 400 (3400), and its next rank-up row");
    // A character with no faction: the faction rewards of the event pay nothing, and the hand-in goes on.
    check(shop.factions().set(kCharacterB, world::FactionCommand{0, std::nullopt, std::nullopt}).ok(), "control: B has no faction");
    const world::QuestResult swamp = quests.grant(*b, kSwampLike, "test", 0);
    shop.settle();
    const world::HandInResult free = quests.hand_in(*b, swamp.instance);
    check(free.ok() && free.reputation == 0 && free.tokens == 0 && shop.progression(kCharacterB, "Syndicate").points == 0
              && shop.progression(kCharacterB, "MarauderTokens").points == 0,
          "faction None: the FactionReputation and FactionTokens of the event pay nothing (0x31F6DFE), and the hand-in completes");
    // A change ends the ShouldResetOnFactionChange rows of the old faction (the next rank-up row of A).
    check(shop.host.world.ownership().unlock_achievement(*a, "FactionIntro_Marauders_Recruitment", 702), "control: A has the Marauder recruitment");
    shop.request_faction(kCharacterA, 2);
    shop.settle();
    check(a->faction.faction == 2 && shop.active(kCharacterA, kNextRankUp) == nullptr && shop.has(kCharacterA, "syndicate_faction_quest_01")
              && shop.progression(kCharacterA, "Syndicate").points == 3400,
          "the change to the Marauders ends the active rank-up row (ShouldResetOnFactionChange); the trial achievement and the Syndicate "
          "reputation stay");
}

/** QG7F: a restart keeps the faction, the time, the count, the reputation, and the tokens. */
void restart_keeps(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path database = options.workDirectory / "faction-restart.sqlite3";
    std::filesystem::remove(database);
    {
        FactionShop shop(database.string());
        check(shop.factions().set(kCharacterA, world::FactionCommand{3, 1792000000000000000ULL, std::uint16_t{4}}).ok()
                  && shop.host.world.skills().set_faction_progress(kCharacterA, gamedata::progression_id("Covenant"), 2, 20000).ok()
                  && shop.host.world.skills().set_faction_progress(kCharacterA, gamedata::progression_id("CovenantTokens"), 1, 7000).ok(),
              "control: A Covenant with a change time and 4 changes, reputation rank 2 with 20000, tokens rank 1 with 7000");
    }
    FactionShop shop(database.string(), true);
    shop.enter(false);
    const world::Entity* a = shop.entity(kCharacterA);
    check(a != nullptr && a->faction.faction == 3 && a->faction.changedNs == 1792000000000000000ULL && a->faction.changes == 4
              && shop.progression(kCharacterA, "Covenant").rank == 2 && shop.progression(kCharacterA, "Covenant").points == 20000
              && shop.progression(kCharacterA, "CovenantTokens").rank == 1 && shop.progression(kCharacterA, "CovenantTokens").points == 7000,
          "after the restart the entity has the faction, the time, the count, the reputation, and the tokens");
    const FactionSeen seen = faction_fragments(shop.clientA, 0);
    check(seen.count == 1 && seen.owner == 1 && seen.last == 3, "the first record of the owner has the Faction fragment of the stored faction");
}

/** QG7G (MP-10): two characters of different factions in two sessions. */
void two_sessions(const Options&) {
    FactionShop shop;
    shop.enter();
    check(shop.factions().set(kCharacterA, world::FactionCommand{1, std::nullopt, std::nullopt}).ok()
              && shop.factions().set(kCharacterB, world::FactionCommand{2, std::nullopt, std::nullopt}).ok(),
          "control: A Syndicate, B Marauder");
    shop.settle();
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    check(shop.host.world.skills().set_faction_progress(kCharacterA, gamedata::progression_id("SyndicateTokens"), 0, 900).ok(),
          "control: A gets 900 Syndicate tokens");
    shop.settle();
    std::size_t aCategorical = 0;
    for (const Seen& entry : fragments_of(shop.clientA, kCategoricalSlot, 911, fromA)) {
        aCategorical += entry.fragment.fields.empty() ? 0 : 1;
    }
    check(aCategorical >= 1 && fragments_of(shop.clientB, kCategoricalSlot, 911, fromB).empty(),
          "the balance change goes to the owner A only: B gets no categorical fragment (owner only)");
    std::map<std::uint32_t, std::uint8_t> seenByA;
    std::map<std::uint32_t, std::uint8_t> seenByB;
    for (const Seen& entry : fragments_of(shop.clientA, kFactionSlot, kFactionType, 0)) {
        if (entry.fragment.fields.count({1, 3}) == 0 && entry.fragment.fields.count({2, 0}) != 0) {
            seenByA[entry.interest] = entry.fragment.fields.at({2, 0})[0];
        }
    }
    for (const Seen& entry : fragments_of(shop.clientB, kFactionSlot, kFactionType, 0)) {
        if (entry.fragment.fields.count({1, 3}) == 0 && entry.fragment.fields.count({2, 0}) != 0) {
            seenByB[entry.interest] = entry.fragment.fields.at({2, 0})[0];
        }
    }
    const bool aSeesB = std::any_of(seenByA.begin(), seenByA.end(), [](const auto& item) { return item.second == 2; });
    const bool bSeesA = std::any_of(seenByB.begin(), seenByB.end(), [](const auto& item) { return item.second == 1; });
    check(aSeesB && bSeesA, "A gets the public faction Marauder of B, and B the public faction Syndicate of A (group 2 only)");
    const FactionSeen ownA = faction_fragments(shop.clientA, 0);
    const FactionSeen ownB = faction_fragments(shop.clientB, 0);
    check(ownA.owner == 1 && ownB.owner == 1, "each session gets the owner form (the time and the count) of its own character only");
    // The world rows: one character per session, the character of the request is the character of the session (MP-4).
    shop.request_faction(kCharacterA, 3, shop.entity(kCharacterB)->id);
    check(shop.entity(kCharacterB)->faction.faction == 2 && shop.entity(kCharacterA)->faction.faction == 1,
          "a request of A that names the entity of B changes neither character");
}

/** QG7H: the person rules with a faction. */
void person_rules(const Options&) {
    FactionShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->conversation && a->conversation->services == std::vector<std::uint32_t>{1, 16}, "control: no faction: the default state of the innkeeper");
    shop.close(kCharacterA, kInnkeeper);
    check(shop.factions().set(kCharacterA, world::FactionCommand{1, std::nullopt, std::nullopt}).ok(), "control: A Syndicate");
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->conversation && a->conversation->services == std::vector<std::uint32_t>{2},
          "RequiredFaction Syndicate: the state of the Syndicate with the Faction_Board service (2) for the faction of the character");
    shop.close(kCharacterA, kInnkeeper);
    check(shop.factions().set(kCharacterA, world::FactionCommand{2, shop.factions().now_ns(), std::uint16_t{1}}).ok(),
          "control: A Marauder with a change now (a wait of 30 days)");
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->conversation && a->conversation->services.empty() && a->conversation->topics.size() == 1,
          "RequiredFactionCooldown InCooldownMultipleDays: the cooldown state passes with more than 24 hours of the wait");
    shop.close(kCharacterA, kInnkeeper);
    check(shop.factions().set(kCharacterA, world::FactionCommand{2, shop.factions().now_ns() - (2592000ULL - 3600ULL * 20) * 1000000000ULL,
                                                                 std::uint16_t{1}}).ok(),
          "control: 20 hours of the wait are left");
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->conversation && a->conversation->services == std::vector<std::uint32_t>{1, 16},
          "with 20 hours of the wait left the cooldown state does not pass (more than 24 whole hours)");
}

/** QG7I: the loader of the member factions, and the data tool. */
void loader_and_tool(const Options& options) {
    const std::string base = R"({"format":4,"levelCap":2,"levels":[[0,0,0,0],[100,1,0,0]],"attributes":{"minimum":5,"constitutionHealth":[0]},
        "vitals":{"health":800,"stamina":100,"mana":100},"rankTables":{},"progressions":[],"abilities":[],"expansions":[],"entitlements":[],
        "achievements":[],"standings":[],"territoryCards":[])";
    const std::string tables = R"("rankTables":{"MerchantGuild":{"experience":[3000,11000,26000,49000,85000,85000],"requiredLevel":[10,20,35,50,60,61]},
        "SyndicateTokens":{"experience":[15000,30000,45000,75000,150000,300000]}})";
    const auto rows = [](const std::string& first) {
        std::string text = "[" + first;
        for (int faction = 2; faction <= 3; ++faction) {
            text += R"(,[)" + std::to_string(faction) + R"(,"F)" + std::to_string(faction) + R"(","MerchantGuild","T)" + std::to_string(faction)
                    + R"(","SyndicateTokens",4,5,"A","L",["A"],["A"]])";
        }
        return text + "]";
    };
    const std::string good = R"([1,"Syndicate","MerchantGuild","SyndicateTokens","SyndicateTokens",4,5,"FactionIntro_Syndicate_Recruitment","S_syndicatequest_1",["FactionIntro_Syndicate_Recruitment","q1"],["FactionIntro_Syndicate_Recruitment","9806_S_0001_RankUp"]])";
    const auto load = [&](const char* name, const std::string& factions) {
        std::filesystem::create_directories(options.workDirectory);
        const std::filesystem::path path = options.workDirectory / name;
        std::ofstream(path) << base + ",\"factions\":" + factions + "}";
        gamedata::ProgressionTables loaded;
        const bool ok = loaded.load(path.string());
        return std::make_pair(ok, ok ? loaded.data().factions.size() : 0U);
    };
    const auto document = [&](const std::string& change, const std::string& first) {
        return "{\"change\":" + change + ",\"rows\":" + rows(first) + "," + tables + "}";
    };
    const auto valid = load("faction-valid.json", document("[2592000,20,0,0,50]", good));
    check(valid.first && valid.second == 3, "a valid member factions loads with three factions");
    gamedata::ProgressionTables loaded;
    (void)loaded.load((options.workDirectory / "faction-valid.json").string());
    const gamedata::FactionRow* syndicate = loaded.faction(1);
    bool token = false;
    check(syndicate != nullptr && syndicate->reputationId == 0x56FBDE12U && syndicate->tokenId == 0xEBD7B082U
              && loaded.faction_of_progression(0xEBD7B082U, &token) == syndicate && token && loaded.faction_change().cooldownSeconds == 2592000
              && loaded.faction_change().costIncrement == 50,
          "the ids on the wire, the lookup of a token progression, and the change rules");
    check(!load("faction-short.json", document("[2592000,20,0,0]", good)).first, "a change list with four values is refused");
    check(!load("faction-zero-wait.json", document("[0,20,0,0,50]", good)).first, "a wait of 0 seconds is refused");
    const std::string lists = R"([1,"Syndicate","MerchantGuild","SyndicateTokens","SyndicateTokens",4,5,"A","L",["A","B"],["A"]])";
    check(!load("faction-lists.json", document("[2592000,20,0,0,50]", lists)).first, "trial and rank-up lists of different lengths are refused");
    const std::string rank = R"([1,"Syndicate","MerchantGuild","SyndicateTokens","SyndicateTokens",4,6,"A","L",["A"],["A"]])";
    check(!load("faction-rank.json", document("[2592000,20,0,0,50]", rank)).first, "a largest rank with no row in its rank table is refused");
    const std::string order = R"([2,"Syndicate","MerchantGuild","SyndicateTokens","SyndicateTokens",4,5,"A","L",["A"],["A"]])";
    check(!load("faction-order.json", document("[2592000,20,0,0,50]", order)).first, "rows out of the order of the factions are refused");
    // The data tool (made-up tables): the factions case of check_progression_tool.py.
    const std::string command = "python3 -B tools/gamedata/check_progression_tool.py > \"" + (options.workDirectory / "check_progression_tool.log").string()
                                + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(options.workDirectory / "check_progression_tool.log");
    const std::string text((std::istreambuf_iterator<char>(log)), std::istreambuf_iterator<char>());
    check(status == 0 && text.find("ok    factions") != std::string::npos && text.find("ok    faction-bad-lists") != std::string::npos,
          "check_progression_tool.py: each case passes, also the faction cases (log: work folder check_progression_tool.log)");
}

/** The harness gate of the process for one scope (SOULWARDEN_HARNESS=1 on or off), then the value before it. */
struct HarnessScope {
    std::optional<std::string> old;
    explicit HarnessScope(bool on) {
        if (const char* value = std::getenv("SOULWARDEN_HARNESS")) {
            old = value;
        }
        if (on) {
            setenv("SOULWARDEN_HARNESS", "1", 1);
        } else {
            unsetenv("SOULWARDEN_HARNESS");
        }
    }
    ~HarnessScope() {
        if (old) {
            setenv("SOULWARDEN_HARNESS", old->c_str(), 1);
        } else {
            unsetenv("SOULWARDEN_HARNESS");
        }
    }
    HarnessScope(const HarnessScope&) = delete;
    HarnessScope& operator=(const HarnessScope&) = delete;
};

/** QG7J: the faction commands need the harness gate of the process, also through the admin routes (rule of AGENTS.md). */
void harness_commands(const Options&) {
    FactionShop shop;
    shop.enter();
    server::http::Router router;
    server::http::AdminAccess access;
    constexpr const char* kToken = "faction-qg7-made-up-token-0123456789abcdef";
    check(access.add(kToken, shop.a.steamId), "setup: the admin token of the account of A");
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_admin_routes(router, services, access);
    shop.host.tick();
    const auto command = [&](const char* name, const json& arguments) {
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/admin/v1/commands";
        request.target = request.path;
        request.body = json{{"command", name}, {"character", kCharacterA}, {"args", arguments}}.dump();
        request.headers.emplace_back(server::http::kAdminTokenHeader, kToken);
        return server::http::handle_request(router, request);
    };
    {
        const HarnessScope off(false);
        const service::HttpResponse faction = command("set_faction", {{"faction", "syndicate"}});
        const service::HttpResponse points =
            command("set_faction_progress", {{"faction", "syndicate"}, {"kind", "reputation"}, {"rank", 0}, {"points", 100}});
        check(faction.status == 403 && points.status == 403 && shop.factions().view(kCharacterA)->faction == 0
                  && shop.progression(kCharacterA, "Syndicate").points == 0,
              "with no SOULWARDEN_HARNESS=1: set_faction and set_faction_progress of the admin route are refused (403), and nothing changes");
    }
    const HarnessScope on(true);
    check(command("set_faction", {{"faction", "syndicate"}}).status == 200
              && command("set_faction_progress", {{"faction", "syndicate"}, {"kind", "reputation"}, {"rank", 0}, {"points", 100}}).status == 200
              && shop.factions().view(kCharacterA)->faction == 1 && shop.progression(kCharacterA, "Syndicate").points == 100,
          "with the harness gate the commands run");
    check(shop.factions().view(kCharacterB)->faction == 0, "the other character has no faction change");
}

} // namespace

std::vector<Case> faction_cases() {
    return {{"QG7A", "factions QG7 (I1): the Faction state and the join from the recruitment", join_from_recruitment},
            {"QG7B", "factions QG7 (I1): each refusal of a join changes nothing and corrects the HUD of the owner", join_refusals},
            {"QG7C", "factions QG7 (I2): the change, its wait of 30 days, and its Azoth cost", change_wait_cost},
            {"QG7D", "factions QG7 (I3): the reputation and the tokens, their caps and ranks", reputation_tokens},
            {"QG7E", "factions QG7 (I4): the gates, TaskHaveLevel, the rank-up rows, the faction rewards, the reset at a change", quest_rules},
            {"QG7F", "factions QG7 (I5): a restart keeps the faction, the change, the reputation, and the tokens", restart_keeps},
            {"QG7G", "factions QG7 (I7, MP-10): two sessions of two factions: the public faction and the own balances", two_sessions},
            {"QG7H", "factions QG7: the faction board and the faction cooldown states of a person", person_rules},
            {"QG7I", "factions QG7: the loader of the faction data and the data tool", loader_and_tool},
            {"QG7J", "factions QG7: the faction commands are harness commands, also through the admin routes", harness_commands}};
}

} // namespace soulwarden::backend::tests
