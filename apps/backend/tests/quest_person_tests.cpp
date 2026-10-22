// Cases of stage 1.8b: the persons give and take quests (world/quests.h, world/persons.h, gamedata/trigger_data.h, the messages 3772, 794,
// 6703, 6706, 6707, 6606, 6542, 5070 of server/rep/quest_messages.h; docs/systems/quests.md, docs/systems/persons.md). The data has the form
// of the files of tools/gamedata/quests.py, conversations.py, and triggers.py: a made-up start quest of the form of 06_getoffbeach (a
// trigger area, the bit of a cutscene, the cutscene, a temporary unlock, an unlock), its hand-in at the artificer of the person fixture
// with the reward of its success event, two variants of a next quest (the archetype rule of 06_boarfeast) with a return task, a whisper,
// and a topic task, and quests with a reward that the backend cannot pay. All ids, bits, items, and places are made up.
//
//   QB1  I3: the loaders: the cells of 1.8b in the quest data (the rewards, the unpaid rewards, the whispers, the trigger tag, the area
//        rule, the CRC of a cutscene), the trigger data (each shape, a turned box, no height, the refusals), the check of the trigger tool
//   QB2  I1 (B1b): the natural start quest: the trigger area at the accepted position, 6703 to 6707, 6706, 6705 to 6709 and the cutscene
//        task, the hand-in 794 at the destination: 200 experience, 150 coin, the completion bit, the undo of the temporary unlock only, one
//        transaction, the completed notice (field 2) and no active element
//   QB3  I2 (B2b): the offer of the next quest after the hand-in (3772), the variant of the archetype of each character, the return task,
//        the whisper task, the topic task; the reward items of each variant (the gear score of the item table) and the objective item
//   QB4  I3: the effects of a topic at its finish (AchievementUnlock one time, TaskConversationTopic), the whisper 5070 (its target and its
//        bit), the cinematic 6606 and 6542 with the pending id, TaskConversationState, the visits of the trigger index
//   QB5  I4 (B3b): the refusals change nothing: a wrong offer, a duplicate accept, a duplicate hand-in (also after a new entry), a far
//        person, a wrong person, a 6705 with no pending cutscene, a 6703 with no waiting task, a full bag (D5 A), a reward that cannot be
//        paid (a loot container, standing), a refused write of the store
//   QB6  I5 (B4b, MP-10): two sessions at one person with different quest states and presence: each its own offer and hand-in; a hand-in
//        of A changes nothing for B
//   QB7  I5 (load, QG4 of the research): 2,000 and 200,000 authored person slots, 50 characters with active quests, 20,000 offer, hand-in,
//        and topic checks: the lookups and the visits do not grow with the slots or the other characters
//   QB8  the completed notice on the wire: owner only, written only with an instance, and gone after a new entry

#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "server/rep/quest_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/person_fixture.h"
#include "support/test_progression.h"

namespace soulwarden::backend::tests {
namespace {

using namespace personfix;

constexpr const char* kStart = "Qb_Start";
constexpr const char* kNext = "Qb_Next";
constexpr const char* kNextSoldier = "Qb_Next_Soldier";
constexpr const char* kCutscene = "Qb_Cutscene_IGC";
constexpr const char* kFilm = "Qb_Film";
/** The trigger volume of the start quest: 20 m to the north of the persons. */
const state::WorldPosition kTriggerPlace{100.0F, 222.0F, 30.0F};
const state::WorldPosition kNearArtificer{99.0F, 200.5F, 30.0F};

std::uint32_t qcrc(const std::string& text) {
    return gamedata::quest_crc(text);
}

/** The quest data of the cases: the person quest document with the quests of stage 1.8b. */
json quest_document_18b() {
    json document = person_quest_document();
    json& tasks = document["tasks"];
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id, type, code, cells);
    };
    // The start quest (the form of 06_getoffbeach, tasks 9 to 13).
    add("Qb_Start_Area", "tasktriggerarea", 3, {{"TP_TargetTag", "Qb_Trigger"}, {"TriggerAreaType", "Enter"}, {"TargetQty", "1"}});
    add("Qb_Start_Igc", "taskunlockachievement", 20, {{"ItemName", "Qb_Start_Igc"}});
    add("Qb_Start_Cutscene", "taskcutscenecomplete", 49, {{"ItemName", kCutscene}, {"TargetQty", "1"}});
    add("Qb_Start_Watched", "taskunlockachievement", 20, {{"ItemName", "Qb_Watched"}});
    add("Qb_Start_Temp", "tasktemporaryunlockachievement", 22, {{"ItemName", "Qb_Temp"}});
    add("Qb_Start_Keep", "taskunlockachievement", 20, {{"ItemName", "Qb_Keep"}});
    add("Qb_Start_Root", "consecutivetaskcontainer", 1);
    // The next quest (two variants with one tree): a return task, a whisper, a topic.
    add("Qb_Next_Relic", "taskhaveandreturnitems", 26, {{"ItemName", "QbRelic"}, {"TargetQty", "2"}});
    add("Qb_Next_Whisper", "tasktriggergameevent", 15, {{"KillEnemyType", "Qb_Whisper_Event"}, {"TargetQty", "1"}});
    add("Qb_Next_Topic", "taskconversationtopic", 17, {{"ItemName", "Test_Artificer"}, {"UIScreenTag", "Qb_Topic"}, {"TargetQty", "1"}});
    add("Qb_Next_Root", "consecutivetaskcontainer", 1);
    // A quest that is ready at once (its reward is the subject).
    add("Qb_Ready_Unlock", "taskunlockachievement", 20, {{"ItemName", "Qb_Ready_Bit"}});
    // The cinematic and the state of a person.
    add("Qb_Cinema_Start", "taskcinematicstart", 50, {{"ItemName", kFilm}});
    add("Qb_Cinema_End", "taskcinematiccomplete", 51, {{"ItemName", kFilm}, {"TargetQty", "1"}});
    add("Qb_Cinema_Root", "consecutivetaskcontainer", 1);
    add("Qb_State_Task", "taskconversationstate", 16, {{"ItemName", "Test_Artificer"}, {"UIScreenTag", "Art_Offer"}, {"TargetQty", "1"}});
    const json startTree = json::array({json::array({0, "qb_start_area", json::array()}), json::array({1, "qb_start_igc", json::array()}),
                                        json::array({2, "qb_start_cutscene", json::array()}), json::array({3, "qb_start_watched", json::array()}),
                                        json::array({4, "qb_start_temp", json::array()}), json::array({5, "qb_start_keep", json::array()}),
                                        json::array({6, "qb_start_root", json::array({0, 1, 2, 3, 4, 5})})});
    const json nextTree = json::array({json::array({0, "qb_next_relic", json::array()}), json::array({1, "qb_next_whisper", json::array()}),
                                       json::array({2, "qb_next_topic", json::array()}),
                                       json::array({3, "qb_next_root", json::array({0, 1, 2})})});
    const json readyTree = json::array({json::array({0, "qb_ready_unlock", json::array()})});
    json& objectives = document["objectives"];
    objectives.push_back({{"id", kStart},
                          {"cells",
                           {{"Type", "MainStoryQuest"}, {"NpcDestinationId", "Test_Artificer"}, {"AchievementId", "Qb_Start_Done"},
                            {"SuccessGameEventId", "Qb_Start_Success"}, {"ExclusiveTerritory", "06"}, {"CanBeAbandoned", "0"}}},
                          {"tree", startTree}});
    objectives.push_back({{"id", kNext},
                          {"cells",
                           {{"Type", "MainStoryQuest"}, {"NpcDestinationId", "Test_Artificer"}, {"AchievementId", "Qb_Next_Done"},
                            {"RequiredAchievementId", "Qb_Start_Done && !Archetype_Mercenary"}, {"SuccessGameEventId", "Qb_Next_Success"},
                            {"ItemRewardName", "QbPotion"}, {"ItemRewardQty", "5"}, {"ExclusiveTerritory", "06"}}},
                          {"tree", nextTree}});
    objectives.push_back({{"id", kNextSoldier},
                          {"cells",
                           {{"Type", "MainStoryQuest"}, {"NpcDestinationId", "Test_Artificer"}, {"AchievementId", "Qb_Next_Done"},
                            {"RequiredAchievementId", "Qb_Start_Done && Archetype_Mercenary"}, {"SuccessGameEventId", "Qb_Next_Success_Soldier"},
                            {"ItemRewardName", "QbPotion"}, {"ItemRewardQty", "5"}, {"ExclusiveTerritory", "06"}}},
                          {"tree", nextTree}});
    for (const auto& [id, event] : std::vector<std::pair<const char*, const char*>>{
             {"Qb_Pouch", "Qb_Pouch_Success"}, {"Qb_Standing", "Qb_Standing_Success"}, {"Qb_Paid", "Qb_Paid_Success"}}) {
        objectives.push_back({{"id", id},
                              {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", event}}},
                              {"tree", readyTree}});
    }
    objectives.push_back({{"id", "Qb_Cinema"},
                          {"cells", {{"Type", "Journey"}}},
                          {"tree", json::array({json::array({0, "qb_cinema_start", json::array()}), json::array({1, "qb_cinema_end", json::array()}),
                                                json::array({2, "qb_cinema_root", json::array({0, 1})})})}});
    objectives.push_back(
        {{"id", "Qb_State"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "qb_state_task", json::array()})})}});
    std::uint32_t bit = 900;
    for (const char* id : {"Qb_Start_Done", "Qb_Start_Igc", "Qb_Watched", "Qb_Temp", "Qb_Keep", "Qb_Next_Done", "Qb_Whisper_Bit", "Qb_Event_Bit",
                           "Qb_Topic_Bit", "Qb_Ready_Bit"}) {
        document["achievements"].push_back(json::array({id, bit++}));
    }
    document["events"] = {
        {"qb_start_success", {{"id", "Qb_Start_Success"}, {"UniversalExpAmount", "200"}, {"CurrencyReward", "150"}, {"TerritoryStanding", "0"}}},
        {"qb_next_success", {{"id", "Qb_Next_Success"}, {"UniversalExpAmount", "350"}, {"CurrencyReward", "250"}, {"ItemReward", "QbPants"}}},
        {"qb_next_success_soldier",
         {{"id", "Qb_Next_Success_Soldier"}, {"UniversalExpAmount", "350"}, {"CurrencyReward", "250"}, {"ItemReward", "QbFeet"}}},
        {"qb_whisper_event", {{"id", "Qb_Whisper_Event"}, {"WhisperId", "Qb_Whisper"}, {"AchievementId", "Qb_Event_Bit"}}},
        {"qb_pouch_success", {{"id", "Qb_Pouch_Success"}, {"UniversalExpAmount", "275"}, {"ItemReward", "QbPouch"}}},
        {"qb_standing_success", {{"id", "Qb_Standing_Success"}, {"UniversalExpAmount", "800"}, {"TerritoryStanding", "120"}}},
        {"qb_paid_success", {{"id", "Qb_Paid_Success"}, {"UniversalExpAmount", "100"}, {"CurrencyReward", "100"}, {"ItemReward", "QbPants"}}}};
    document["whispers"] = json::array({{{"id", "Qb_Whisper"}, {"crc", 0}, {"cells", {{"GameEvent", "Other_Event"}, {"AchievementId", "Qb_Whisper_Bit"}}}},
                                        {{"id", "Qb_Second_Whisper"}, {"crc", 0}, {"cells", {{"GameEvent", "Qb_Second_Event"}}}}});
    return document;
}

/** The conversation data of the cases: the person document with the offer state of the artificer and the topic of the next quest. */
json conversation_document_18b() {
    json document = conversation_document();
    document["npcs"][0]["states"] = json::array({"Art_Default", "Art_Level", "Art_TaskTopic", "Art_Offer", "Art_NextTopic"});
    document["states"].push_back({{"id", "Art_Offer"},
                                  {"type", "Linear_Objective"},
                                  {"requires", {{"RequiredAchievement", "Qb_Start_Done"}}},
                                  {"objectives", json::array({kNextSoldier, kNext, "Qb_Paid"})}});
    document["states"].push_back({{"id", "Art_NextTopic"},
                                  {"type", "Linear_Objective"},
                                  {"requires", {{"RequiredActiveObjectiveId", "Qb_Next,Qb_Next_Soldier"}, {"RequiredActiveTaskId", "Qb_Next_Topic"}}},
                                  {"topics", json::array({"Qb_Topic"})}});
    document["topics"].push_back({{"id", "Qb_Topic"}, {"prompt", "@Qb"}, {"achievementUnlock", "Qb_Topic_Bit"}});
    return document;
}

std::vector<gamedata::ItemDefinition> quest_items() {
    std::vector<gamedata::ItemDefinition> items = chests::chest_items();
    gamedata::ItemDefinition pants = chests::chest_item("QbPants", "Armor", "EquippableLegs+Armor+Medium+Named", 2, 1, 100, 100);
    pants.gearScoreOverride = 100;
    pants.bindOnPickup = true;
    gamedata::ItemDefinition feet = chests::chest_item("QbFeet", "Armor", "EquippableFeet+Armor+Heavy+Named", 1, 1, 125, 125);
    feet.gearScoreOverride = 125;
    gamedata::ItemDefinition relic = chests::chest_item("QbRelic", "Resource", "Resource", 0, 10000);
    relic.nonRemovable = true;
    items.push_back(pants);
    items.push_back(feet);
    items.push_back(relic);
    items.push_back(chests::chest_item("QbPotion", "Consumable", "Consumable+Potion", 1, 100));
    items.push_back(chests::chest_item("QbPouch", "Resource", "Resource+LootContainer", 2, 10000));
    items.push_back(chests::chest_item("QbStone", "Resource", "Resource", 1, 1));
    return items;
}

/** The ids of the player slice of the cases (the whisper needs its component; each facet of a call is checked then). */
gamedata::SliceIds quest_slice() {
    gamedata::SliceIds slice;
    slice.set(0x100, {{0x100, 0x1C116D259B43203DULL, server::rep::kConversationComponentName},
                      {0x100, server::rep::kPlayerCutsceneComponent, server::rep::kPlayerCutsceneComponentName},
                      {0x100, 0x588A5C029AEC02ACULL, server::rep::kObjectivesComponentName},
                      {0x100, server::rep::kInteractorComponent, "RootPlayer/Interactor"},
                      {0x00000000A0ULL, 0x7D5CA09D7B3CC6FAULL, server::rep::kWhisperComponentName}});
    return slice;
}

/** A host with the persons, the quests of stage 1.8b, the trigger volume, the items, and the levels. */
struct QuestShop : PersonShop {
    explicit QuestShop(std::size_t extra = 0, const std::string& database = ":memory:", const gamedata::SliceIds* slice = nullptr,
                       bool existing = false)
        : PersonShop(true, extra, database, slice, existing) {
        if (!host.quests.load_text(quest_document_18b().dump())) {
            throw std::runtime_error("quest person test: the quest data was not set: " + host.quests.error());
        }
        if (!host.conversations.load_text(conversation_document_18b().dump())) {
            throw std::runtime_error("quest person test: the conversation data was not set: " + host.conversations.error());
        }
        gamedata::TriggerVolume volume;
        volume.tag = "qb_trigger";
        volume.center = {kTriggerPlace.x, kTriggerPlace.y, kTriggerPlace.z};
        volume.half = {4.0F, 4.0F, 3.0F};
        host.triggers.set({volume});
        host.catalog.set(quest_items());
        host.progression.set(test_progression());
    }

    /** A message of the client of a character with one u32 after the call (3772, 794, 6703, 6706, 6606, 6542). */
    void send(const char* character, std::uint32_t type, std::uint64_t component, std::uint32_t value) {
        const world::Entity* own = entity(character);
        FakeClient& client = client_of(character);
        server::hub::Writer body;
        body.u64(own->id + component);
        body.u64(own->id);
        body.u32(value);
        host.datagram(client.peer, client.component_message(type, own->actor.bytes, body.data()));
        host.steps(2, {&clientA, &clientB});
    }
    void accept(const char* character, const char* objective) {
        send(character, server::rep::kTypeAcceptObjective, 0x1C116D259B43203DULL, qcrc(objective));
    }
    void hand_in(const char* character, const char* objective) {
        send(character, server::rep::kTypeCompleteObjective, 0x1C116D259B43203DULL, qcrc(objective));
    }
    /** 3158 (or one 5397) with the target of the Conversation component (a case with the ids of the player slice checks the target). */
    void topic18(const char* character, std::uint32_t topic, bool closing = false) {
        const world::Entity* own = entity(character);
        FakeClient& client = client_of(character);
        const std::uint64_t target = own->id + 0x1C116D259B43203DULL;
        const Bytes body = closing ? server::rep::finished_topics_body(target, own->id, {topic}) : server::rep::conversation_topic_body(target, own->id, topic);
        host.datagram(client.peer, client.component_message(closing ? server::rep::kTypeConversationTopicsFinished : server::rep::kTypeConversationTopic,
                                                            own->actor.bytes, body));
        host.steps(2, {&clientA, &clientB});
    }
    void cutscene_end(const char* character) {
        const world::Entity* own = entity(character);
        FakeClient& client = client_of(character);
        host.datagram(client.peer, client.component_call(server::rep::kTypeCutsceneEnded, own->actor.bytes,
                                                         own->id + server::rep::kPlayerCutsceneComponent, own->id));
        host.steps(2, {&clientA, &clientB});
    }
    [[nodiscard]] std::uint64_t offers(world::QuestRequestResult result) {
        return persons().counts().offers[static_cast<std::size_t>(result)];
    }
    [[nodiscard]] std::uint64_t hand_ins(world::QuestRequestResult result) {
        return persons().counts().handIns[static_cast<std::size_t>(result)];
    }
    /** The start quest of a character to its ready state: the area, the grant of the cutscene, its start and end. */
    void play_start(const char* character) {
        walk_to(character, kTriggerPlace);
        send(character, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
        send(character, server::rep::kTypeCutsceneStarted, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
        cutscene_end(character);
        walk_to(character, std::string(character) == kCharacterA ? kNearArtificer : state::WorldPosition{100.0F, 200.0F, 30.0F});
    }
    /** A walk in parts of 8 m at most (the position check accepts a walk at the speed of a run). */
    void walk_to(const char* character, const state::WorldPosition& place) {
        const world::Entity* own = entity(character);
        state::WorldPosition from = std::string(character) == kCharacterA ? placeA : placeB;
        if (own != nullptr && !own->positionEstimated) {
            from = own->position;
        }
        const float dx = place.x - from.x;
        const float dy = place.y - from.y;
        const int parts = std::max(1, static_cast<int>(std::ceil(std::sqrt(dx * dx + dy * dy) / 8.0F)));
        for (int part = 1; part <= parts; ++part) {
            const float share = static_cast<float>(part) / static_cast<float>(parts);
            walk(character, state::WorldPosition{from.x + dx * share, from.y + dy * share, from.z + (place.z - from.z) * share});
        }
    }
    /** @return The bag count of an item of a character. */
    [[nodiscard]] std::uint64_t count(const char* character, const char* item) {
        return world::Inventory::count(*entity(character), item);
    }
    [[nodiscard]] bool has_bit(const char* character, const char* id) {
        return host.world.quests().conditional(*entity(character), id);
    }
    /** @return The last Objectives state of the own character of a client from a message index on. */
    [[nodiscard]] std::optional<questfix::ObjectivesState> last_state(const FakeClient& client, std::size_t from) {
        const std::vector<Fragment> fragments = questfix::own_fragments(client, questfix::kObjectivesSlot, questfix::kObjectivesType, from);
        if (fragments.empty()) {
            return std::nullopt;
        }
        return questfix::decode_objectives(fragments.back());
    }
};

/** The body of the check script of the trigger tool. */
std::tuple<int, std::size_t, std::size_t> run_tool_check(const Options& options, const char* script, const char* name) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata" / script;
    const std::filesystem::path output = options.workDirectory / name;
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command =
        std::string(python != nullptr ? python : "python3") + " -B \"" + path.lexically_normal().string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::size_t ok = 0;
    std::size_t fail = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 160));
        ok += line.rfind("ok", 0) == 0 ? 1 : 0;
        fail += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    std::filesystem::remove(output);
    return {status, ok, fail};
}

/** QB1: the loaders of stage 1.8b. */
void loaders(const Options& options) {
    gamedata::QuestData data;
    check(data.load_text(quest_document_18b().dump()), "the quest data of the cases loads (" + data.error() + ")");
    const gamedata::QuestObjective* start = data.objective(kStart);
    const gamedata::QuestObjective* next = data.objective(kNext);
    const gamedata::QuestGameEvent* success = data.game_event("QB_START_SUCCESS");
    const gamedata::QuestGameEvent* standing = data.game_event("Qb_Standing_Success");
    check(start != nullptr && next != nullptr && success != nullptr && success->experience == 200 && success->coin == 150
              && success->coinChance == 1.0 && success->item.empty() && success->unpaid.empty() && next->itemReward == "QbPotion"
              && next->itemRewardQty == 5 && start->itemRewardQty == 1 && standing != nullptr
              && standing->unpaid == std::vector<std::string>{"TerritoryStanding 120"},
          "the rewards: experience, coin (the chance 1 of a blank cell), the item of the row with its quantity, a standing of 0 is no "
          "reward, a standing of 120 is a reward that the backend cannot pay");
    const gamedata::QuestWhisper* byId = data.whisper_of_event("qb_whisper_event");
    const gamedata::QuestWhisper* byEvent = data.whisper_of_event("Qb_Second_Event");
    check(byId != nullptr && byId->id == "Qb_Whisper" && byId->crc == qcrc("Qb_Whisper") && byId->achievement == "Qb_Whisper_Bit"
              && byEvent != nullptr && byEvent->id == "Qb_Second_Whisper" && data.whisper_of_event("No_Event") == nullptr,
          "the whisper of an event: its WhisperId first, else the whisper whose GameEvent names the event; the CRC-32 of the lower-case id");
    const gamedata::QuestTask& area = data.task(start->nodes[0].task);
    const gamedata::QuestTask& cutscene = data.task(start->nodes[2].task);
    check(area.type == gamedata::task_type::kTriggerArea && area.targetTag == "qb_trigger" && area.areaRule == gamedata::AreaRule::enter
              && cutscene.nameCrc == qcrc(kCutscene) && qcrc("06_GetOffBeach_MSQ_RevivalShoreKnight_WW_IGC") == 0x4817DD08U,
          "the cells of the tasks: the tag (lower case) and the rule of a trigger area; the CRC of a cutscene (4817DD08 for the beach "
          "cutscene of live check C1)");
    // The trigger data.
    const json volumes = {
        {"format", 1},
        {"volumes",
         json::array({{{"tag", "Box_A"}, {"shape", "aabb"}, {"center", {10, 10, 5}}, {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}}, {"half", {2, 2, 1}}},
                      {{"tag", "turned"},
                       {"shape", "box"},
                       {"center", {0, 0, 0}},
                       {"axes", {0.7071068, 0.7071068, 0, -0.7071068, 0.7071068, 0, 0, 0, 1}},
                       {"half", {4, 1, 1}}},
                      {{"tag", "ball"}, {"shape", "sphere"}, {"center", {0, 0, 0}}, {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}}, {"radius", 3}},
                      {{"tag", "pipe"},
                       {"shape", "cylinder"},
                       {"center", {0, 0, 0}},
                       {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
                       {"radius", 2},
                       {"height", 4},
                       {"ignoreHeight", false}},
                      {{"tag", "flat"},
                       {"shape", "aabb"},
                       {"center", {0, 0, 0}},
                       {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}},
                       {"half", {1, 1, 1}},
                       {"ignoreHeight", true}},
                      {{"tag", "box_a"}, {"shape", "sphere"}, {"center", {50, 50, 0}}, {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}}, {"radius", 1}}})}};
    gamedata::TriggerData triggers;
    std::uint64_t visits = 0;
    check(triggers.load_text(volumes.dump()) && triggers.volume_count() == 6 && triggers.tag_count() == 5 && triggers.knows("box_a")
              && triggers.inside("box_a", 11.9F, 8.1F, 5.9F, &visits) && !triggers.inside("box_a", 12.1F, 10.0F, 5.0F)
              && !triggers.inside("box_a", 10.0F, 10.0F, 6.5F) && triggers.inside("box_a", 50.5F, 50.0F, 0.0F) && visits == 1,
          "an aabb (the border is inside; the height counts); a tag (lower case) with two volumes is inside when one of them has the point; "
          "a lookup visits only the volumes of its tag");
    check(triggers.inside("turned", 2.5F, 2.5F, 0.0F) && !triggers.inside("turned", 2.5F, -2.5F, 0.0F) && triggers.inside("ball", 0.0F, 2.0F, 2.0F)
              && !triggers.inside("ball", 0.0F, 2.5F, 2.5F) && triggers.inside("pipe", 1.9F, 0.0F, 1.9F) && !triggers.inside("pipe", 0.0F, 0.0F, 2.1F)
              && triggers.inside("flat", 0.5F, 0.5F, 500.0F) && !triggers.inside("unknown", 0.0F, 0.0F, 0.0F) && !triggers.knows("unknown"),
          "a box turned by 45 degrees, a sphere, a cylinder (its height), a volume with no height limit, a tag that the data does not have");
    json wrong = volumes;
    wrong["volumes"][0]["shape"] = "cone";
    json noSize = volumes;
    noSize["volumes"][2].erase("radius");
    check(!triggers.load_text(wrong.dump()) && !triggers.load_text(noSize.dump()) && !triggers.load("/no/such/trigger-data.json")
              && !triggers.load_text(json{{"format", 2}, {"volumes", json::array()}}.dump()) && !triggers.loaded(),
          "the loader refuses a shape that it does not know, a volume with no size, no file, and a different format (no volume then)");
    const auto [status, ok, fail] = run_tool_check(options, "check_triggers_tool.py", "check-triggers.log");
    check(status == 0 && fail == 0 && ok >= 5, "check_triggers_tool.py: each case passes (" + std::to_string(ok) + " cases)");
}

/** QB2: the natural start quest to its hand-in (I1, B1b). */
void start_quest(const Options&) {
    QuestShop shop;
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    world::Entity* a = shop.entity(kCharacterA);
    const world::QuestResult grant = shop.host.world.quests().grant(*a, kStart, "territory", 6);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    const world::QuestInstance* quest = questfix::quest_of(*a, kStart);
    check(grant.ok() && quest != nullptr && quest->tasks[0].state == world::quest_state::kActive && quest->tasks[1].state == world::quest_state::kNew,
          "the grant: the trigger area task is Active (the character is not in its volume)");
    shop.walk_to(kCharacterA, kTriggerPlace);
    quest = questfix::quest_of(*a, kStart);
    note("QB2 after the walk: position " + std::to_string(a->position.x) + " " + std::to_string(a->position.y) + " " + std::to_string(a->position.z)
         + (a->positionEstimated ? " (estimate)" : "") + ", tasks " + questfix::states_text(*quest) + ", place looks "
         + std::to_string(shop.host.world.quests().counts().placeLooks) + ", trigger visits " + std::to_string(shop.host.world.quests().counts().triggerVisits));
    check(quest->tasks[0].state == world::quest_state::kComplete && quest->tasks[2].state == world::quest_state::kActive
              && shop.has_bit(kCharacterA, "Qb_Start_Igc"),
          "the accepted position in the volume completes the trigger area task; the next task sets the bit of the cutscene");
    // A cutscene that no task waits for gets no grant; the cutscene of the task gets 6707 with its CRC.
    std::size_t from = shop.clientA.got.size();
    shop.send(kCharacterA, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc("Other_IGC"));
    check(count_type(shop.clientA, server::rep::kTypeCutsceneGranted, from) == 0 && a->quests.pendingCutscene == 0,
          "6703 of a cutscene that no task waits for: no 6707, no pending cutscene");
    // A 6705 after the grant and before the 6706 (a mismatched grant can send it): 6709, and no task completes.
    shop.send(kCharacterA, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    const Sent* granted = nullptr;
    for (std::size_t index = from; index < shop.clientA.got.size(); ++index) {
        granted = shop.clientA.got[index].type == server::rep::kTypeCutsceneGranted ? &shop.clientA.got[index] : granted;
    }
    server::hub::Writer expected;
    expected.u64(a->id + server::rep::kPlayerCutsceneComponent);
    expected.u64(a->id);
    expected.u32(qcrc(kCutscene));
    check(granted != nullptr && granted->body == expected.data() && a->quests.pendingCutscene == qcrc(kCutscene),
          "6703 of the cutscene of the active task: 6707 with the PlayerCutscene target and the CRC (live check C1)");
    from = shop.clientA.got.size();
    shop.cutscene_end(kCharacterA);
    check(count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1
              && questfix::quest_of(*a, kStart)->tasks[2].state == world::quest_state::kActive && !a->quests.cutsceneStarted,
          "a 6705 before the 6706 of the granted cutscene (a mismatched grant can send it): 6709 (the input is free), the task waits");
    shop.send(kCharacterA, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    shop.send(kCharacterA, server::rep::kTypeCutsceneStarted, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    from = shop.clientA.got.size();
    shop.cutscene_end(kCharacterA);
    quest = questfix::quest_of(*a, kStart);
    const gamedata::QuestObjective* objective = shop.host.quests.objective(kStart);
    check(count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1 && quest->tasks[2].state == world::quest_state::kComplete
              && shop.has_bit(kCharacterA, "Qb_Watched") && shop.has_bit(kCharacterA, "Qb_Temp") && shop.has_bit(kCharacterA, "Qb_Keep")
              && shop.host.world.quests().ready(*quest) && quest->tasks[objective->root].state == world::quest_state::kComplete,
          "6703, 6706, 6705: 6709, the cutscene task completes, the later actions run, the root is complete (the quest waits for its hand-in)");
    // The hand-in at the destination person.
    shop.walk_to(kCharacterA, kNearArtificer);
    shop.request(shop.clientA, shop.talk(kArtificer));
    const std::uint64_t instance = quest->instance;
    const std::uint32_t experience = a->progression.experience;
    const std::uint64_t coin = a->wallet.coin;
    from = shop.clientA.got.size();
    shop.hand_in(kCharacterA, kStart);
    shop.host.steps(20, {&shop.clientA, &shop.clientB});
    bool rowGone = true;
    for (const state::QuestRow& row : shop.host.store.list_quests(kCharacterA)) {
        rowGone = rowGone && row.instance != instance;
    }
    check(shop.hand_ins(world::QuestRequestResult::accepted) == 1 && questfix::quest_of(*a, kStart) == nullptr && rowGone
              && a->progression.experience == experience + 200 && a->wallet.coin == coin + 150 && shop.has_bit(kCharacterA, "Qb_Start_Done")
              && shop.host.store.currency(kCharacterA, state::kCurrencyCoin) == coin + 150,
          "794 at the destination: the instance and its rows go; 200 experience, 150 coin (the row too), and the completion bit");
    check(!shop.has_bit(kCharacterA, "Qb_Temp") && shop.has_bit(kCharacterA, "Qb_Keep") && shop.has_bit(kCharacterA, "Qb_Watched"),
          "the completion reverts the temporary unlock only (client 0x8260590); an unlock with no StayActive stays (0x8260730)");
    const std::optional<questfix::ObjectivesState> state = shop.last_state(shop.clientA, from);
    note("QB2 the last state: " + (state ? "active " + std::to_string(state->active.size()) + ", completed " + std::to_string(state->completed.size())
                                                + (state->hasCompleted ? " (field 2)" : "") + ", tracked " + std::to_string(state->tracked.size())
                                          : std::string("none")));
    bool active = false;
    for (const auto& [crc, inst] : state ? state->active : std::vector<std::pair<std::uint32_t, std::uint64_t>>{}) {
        active = active || inst == instance;
    }
    check(state && state->completed == std::vector<std::uint64_t>{instance} && !active && a->quests.completed == std::vector<std::uint64_t>{instance}
              && std::find(state->tracked.begin(), state->tracked.end(), instance) == state->tracked.end(),
          "the Objectives state after the hand-in: the instance in the completed notice (field 2), no active element, no pin");
}

/** QB3: the offer of the next quest, the archetype variants, the return, the whisper, the topic, the reward items (I2, B2b). */
void next_offer(const Options&) {
    const gamedata::SliceIds slice = quest_slice();
    QuestShop shop(0, ":memory:", &slice);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    (void)shop.host.world.ownership().unlock_achievement(*b, "Archetype_Mercenary", *shop.host.world.ownership().achievement_bit("Archetype_Mercenary"));
    for (const char* character : {kCharacterA, kCharacterB}) {
        world::Entity* own = shop.entity(character);
        check(shop.host.world.quests().grant(*own, kStart, "territory", 6).ok(), std::string("control: the start quest of ") + character);
        shop.play_start(character);
        shop.request(shop.client_of(character), shop.talk(kArtificer));
        shop.hand_in(character, kStart);
    }
    check(shop.has_bit(kCharacterA, "Qb_Start_Done") && shop.has_bit(kCharacterB, "Qb_Start_Done") && shop.has_bit(kCharacterB, "Archetype_Mercenary")
              && !shop.has_bit(kCharacterA, "Archetype_Mercenary"),
          "control: A and B handed in the start quest; B has the archetype bit");
    // The offer: the state of the hand-in bit (the stale revision gives the selection again at the request).
    shop.accept(kCharacterA, kNextSoldier);
    check(shop.offers(world::QuestRequestResult::refused) == 1 && questfix::quest_of(*a, kNextSoldier) == nullptr,
          "A (no archetype bit) asks for the variant of the archetype: refused (RequiredAchievementId), no change");
    shop.accept(kCharacterA, kNext);
    shop.accept(kCharacterB, kNextSoldier);
    const world::QuestInstance* nextA = questfix::quest_of(*a, kNext);
    const world::QuestInstance* nextB = questfix::quest_of(*b, kNextSoldier);
    check(shop.offers(world::QuestRequestResult::accepted) == 2 && nextA != nullptr && nextB != nullptr && nextA->source == "offer"
              && nextA->originTerritory == 6 && questfix::quest_of(*b, kNext) == nullptr && a->conversation
              && shop.host.conversations.state(static_cast<std::uint32_t>(a->conversation->state)).id == "Art_Offer",
          "3772: A gets the base variant and B the variant of its archetype (one variant each), from the offer state of the person");
    // The return task, the whisper, the topic.
    std::size_t from = shop.clientA.got.size();
    (void)shop.host.world.inventory().give(kCharacterA, "QbRelic", 2);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    nextA = questfix::quest_of(*a, kNext);
    const Sent* whisper = nullptr;
    for (std::size_t index = from; index < shop.clientA.got.size(); ++index) {
        whisper = shop.clientA.got[index].type == server::rep::kTypeStartWhisper ? &shop.clientA.got[index] : whisper;
    }
    server::hub::Writer expected;
    expected.u64(((0xA0ULL ^ 0x100ULL ^ a->id) + 0x7D5CA09D7B3CC6FAULL));
    expected.u64(a->id);
    expected.u32(qcrc("Qb_Whisper"));
    check(nextA->tasks[0].state == world::quest_state::kComplete && nextA->tasks[1].state == world::quest_state::kComplete && whisper != nullptr
              && whisper->body == expected.data() && shop.has_bit(kCharacterA, "Qb_Whisper_Bit") && shop.has_bit(kCharacterA, "Qb_Event_Bit")
              && count_type(shop.clientB, server::rep::kTypeStartWhisper, 0) == 0,
          "the items of the return task, then the whisper task: 5070 to A only (the Whisper target, the CRC), the bits of the whisper and of "
          "the event");
    shop.close(kCharacterA, kArtificer);
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.topic18(kCharacterA, crc("Qb_Topic"));
    check(questfix::quest_of(*a, kNext)->tasks[2].state == world::quest_state::kActive && !shop.has_bit(kCharacterA, "Qb_Topic_Bit"),
          "the selection of the topic (3158): no effect yet");
    shop.close(kCharacterA, kArtificer);
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    nextA = questfix::quest_of(*a, kNext);
    check(nextA->tasks[2].state == world::quest_state::kComplete && shop.host.world.quests().ready(*nextA) && shop.has_bit(kCharacterA, "Qb_Topic_Bit"),
          "the finish of the topic (5397 of the close): the topic task completes, the AchievementUnlock is set, the quest is ready");
    // The hand-in: the items of the variant and of the row, the return of the relics.
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.hand_in(kCharacterA, kNext);
    const world::Entity* own = shop.entity(kCharacterA);
    std::optional<std::uint32_t> pantsScore;
    for (const world::BagSlot& slot : own->bag.slots) {
        if (slot.item && slot.item->itemId == "QbPants") {
            pantsScore = slot.item->gearScore;
        }
    }
    check(questfix::quest_of(*a, kNext) == nullptr && shop.count(kCharacterA, "QbPants") == 1 && shop.count(kCharacterA, "QbFeet") == 0
              && shop.count(kCharacterA, "QbPotion") == 5 && shop.count(kCharacterA, "QbRelic") == 0 && pantsScore == 100U
              && shop.has_bit(kCharacterA, "Qb_Next_Done"),
          "the hand-in of A: the item of its variant (the gear score of the item table: 100) and the 5 items of the row; the relics go back");
    note("QB3 the state of B: " + questfix::states_text(*questfix::quest_of(*b, kNextSoldier)));
    check(shop.count(kCharacterB, "QbPants") == 0 && questfix::quest_of(*b, kNextSoldier) != nullptr,
          "B: no item of A, its quest stays");
}

/** QB4: the topic effects, the cinematic, the conversation state, the trigger index (I3). */
void effects(const Options&) {
    QuestShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    // TaskConversationState: the selection of the state of the person.
    check(shop.host.world.ownership().unlock_achievement(*a, "Qb_Start_Done", *shop.host.world.ownership().achievement_bit("Qb_Start_Done"))
              && shop.host.world.quests().grant(*a, "Qb_State", "command", 0).ok(),
          "control: A has the bit of the offer state and the quest of the state");
    shop.request(shop.clientA, shop.talk(kArtificer));
    check(questfix::quest_of(*a, "Qb_State") != nullptr && questfix::quest_of(*a, "Qb_State")->tasks[0].state == world::quest_state::kComplete,
          "the selection of the state Art_Offer at the person completes TaskConversationState");
    shop.close(kCharacterA, kArtificer);
    // The cinematic: an end with no start changes nothing; the start, then the end.
    check(shop.host.world.quests().grant(*a, "Qb_Cinema", "command", 0).ok(), "control: the quest of the cinematic");
    shop.send(kCharacterA, server::rep::kTypeCinematicEnded, server::rep::kPlayerCutsceneComponent, qcrc(kFilm));
    const world::QuestInstance* cinema = questfix::quest_of(*a, "Qb_Cinema");
    check(cinema->tasks[0].state == world::quest_state::kActive && cinema->tasks[1].state == world::quest_state::kNew,
          "6542 with no 6606: no change");
    shop.send(kCharacterA, server::rep::kTypeCinematicStarted, server::rep::kPlayerCutsceneComponent, qcrc(kFilm));
    shop.send(kCharacterA, server::rep::kTypeCinematicEnded, server::rep::kPlayerCutsceneComponent, qcrc("Other_Film"));
    cinema = questfix::quest_of(*a, "Qb_Cinema");
    check(cinema->tasks[0].state == world::quest_state::kComplete && cinema->tasks[1].state == world::quest_state::kActive,
          "6606: TaskCinematicStart completes; a 6542 of a different cinematic changes nothing");
    shop.send(kCharacterA, server::rep::kTypeCinematicEnded, server::rep::kPlayerCutsceneComponent, qcrc(kFilm));
    cinema = questfix::quest_of(*a, "Qb_Cinema");
    check(cinema->tasks[1].state == world::quest_state::kComplete && a->quests.pendingCinematic == 0, "6542 of the started cinematic: it completes");
    // The topic effects: one time.
    check(shop.host.world.quests().grant(*a, kNext, "command", 6).ok(), "control: A has the next quest");
    (void)shop.host.world.inventory().give(kCharacterA, "QbRelic", 2);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    shop.request(shop.clientA, shop.talk(kArtificer));
    const std::uint64_t effects = shop.host.world.quests().counts().topicEffects;
    shop.close(kCharacterA, kArtificer);
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    note("QB4 topic effects " + std::to_string(shop.host.world.quests().counts().topicEffects - effects) + ", duplicates "
         + std::to_string(shop.topic_count(world::TopicResult::duplicate)) + ", not offered " + std::to_string(shop.topic_count(world::TopicResult::notOffered))
         + ", state " + (a->conversation && a->conversation->state >= 0 ? shop.host.conversations.state(static_cast<std::uint32_t>(a->conversation->state)).id : std::string("-"))
         + ", next " + questfix::states_text(*questfix::quest_of(*a, kNext)));
    check(shop.host.world.quests().counts().topicEffects == effects + 1 && shop.has_bit(kCharacterA, "Qb_Topic_Bit")
              && shop.topic_count(world::TopicResult::duplicate) + shop.topic_count(world::TopicResult::notOffered) == 1,
          "the finish of a topic: its effects one time (a second 5397 changes nothing: the task is done, and the selection of now does not "
          "offer the topic)");
    // The trigger index: a move looks only at the volumes of the tags of the active tasks of the character.
    check(shop.host.world.quests().grant(*a, kStart, "command", 6).ok(), "control: A has the start quest");
    const std::uint64_t visits = shop.host.world.quests().counts().triggerVisits;
    shop.walk_to(kCharacterA, {100.0F, 205.0F, 30.0F});
    const std::uint64_t moved = shop.host.world.quests().counts().triggerVisits - visits;
    note("QB4 trigger visits for a walk of 120 reports: " + std::to_string(moved));
    check(moved >= 1 && moved <= 130, "a walk visits the one volume of the tag of the active task (one visit for each look at most)");
}

/** QB5: the refusals change nothing (I4, B3b). */
void refusals(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::string path = (options.workDirectory / "qb5.sqlite3").string();
    std::filesystem::remove(path);
    {
        QuestShop shop(0, path);
        shop.enter();
        world::Entity* a = shop.entity(kCharacterA);
        check(shop.host.world.quests().grant(*a, kStart, "territory", 6).ok(), "control: the start quest");
        // A 6705 with no pending cutscene, and a hand-in before the end of the tasks.
        std::size_t from = shop.clientA.got.size();
        shop.cutscene_end(kCharacterA);
        shop.request(shop.clientA, shop.talk(kArtificer));
        shop.hand_in(kCharacterA, kStart);
        check(count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1 && shop.hand_ins(world::QuestRequestResult::refused) == 1
                  && questfix::quest_of(*a, kStart) != nullptr && questfix::quest_of(*a, kStart)->tasks[0].state == world::quest_state::kActive,
              "a 6705 with no pending cutscene: 6709 and no quest change; a hand-in before the end of the tasks: refused");
        shop.close(kCharacterA, kArtificer);
        shop.play_start(kCharacterA);
        // A wrong person: the pirate is not the destination.
        check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit), "control: the pirate is there for A");
        shop.host.steps(2, {&shop.clientA});
        shop.request(shop.clientA, shop.talk(kPirate));
        shop.hand_in(kCharacterA, kStart);
        check(shop.hand_ins(world::QuestRequestResult::wrongPerson) == 1 && questfix::quest_of(*a, kStart) != nullptr,
              "a hand-in at a different person: refused (wrongPerson)");
        shop.close(kCharacterA, kPirate);
        // No conversation, a far person.
        shop.hand_in(kCharacterA, kStart);
        check(shop.hand_ins(world::QuestRequestResult::noConversation) == 1, "a hand-in with no open conversation: refused");
        shop.request(shop.clientA, shop.talk(kArtificer));
        shop.walk_to(kCharacterA, {98.0F, 212.0F, 30.0F});
        shop.hand_in(kCharacterA, kStart);
        check((shop.hand_ins(world::QuestRequestResult::noConversation) == 2 || shop.hand_ins(world::QuestRequestResult::tooFar) == 1)
                  && questfix::quest_of(*a, kStart) != nullptr,
              "a hand-in after a walk out of the range of the person: refused (the range ended the use)");
        shop.walk_to(kCharacterA, kNearArtificer);
        // A refused write of the store: no part of the reward.
        const auto sql = [&](const char* text) {
            sqlite3* database = nullptr;
            const bool ok = sqlite3_open(path.c_str(), &database) == SQLITE_OK && sqlite3_exec(database, text, nullptr, nullptr, nullptr) == SQLITE_OK;
            sqlite3_close(database);
            return ok;
        };
        check(sql("CREATE TRIGGER qb5_refuse BEFORE DELETE ON character_quests BEGIN SELECT RAISE(FAIL, 'qb5'); END"),
              "control: the store refuses the removal of a quest");
        const std::uint32_t experience = a->progression.experience;
        const std::uint64_t coin = a->wallet.coin;
        shop.request(shop.clientA, shop.talk(kArtificer));
        shop.hand_in(kCharacterA, kStart);
        check(questfix::quest_of(*a, kStart) != nullptr && a->progression.experience == experience && a->wallet.coin == coin
                  && !shop.has_bit(kCharacterA, "Qb_Start_Done") && shop.host.store.currency(kCharacterA, state::kCurrencyCoin) == coin
                  && shop.host.world.quests().counts().storeFailures >= 1,
              "a refused write of the store: no experience, no coin, no bit; the quest stays ready");
        check(sql("DROP TRIGGER qb5_refuse"), "control: the store takes the writes again");
        shop.hand_in(kCharacterA, kStart);
        check(questfix::quest_of(*a, kStart) == nullptr && a->progression.experience == experience + 200, "control: the hand-in passes now");
        // A duplicate hand-in.
        shop.hand_in(kCharacterA, kStart);
        check(a->progression.experience == experience + 200 && a->wallet.coin == coin + 150, "a second hand-in: refused, no second reward");
        // A wrong offer, a duplicate accept.
        shop.accept(kCharacterA, "Qb_Cinema");
        shop.accept(kCharacterA, kNext);
        shop.accept(kCharacterA, kNext);
        check(shop.offers(world::QuestRequestResult::notOffered) == 1 && shop.offers(world::QuestRequestResult::accepted) == 1
                  && shop.offers(world::QuestRequestResult::refused) == 1 && questfix::quest_of(*a, "Qb_Cinema") == nullptr,
              "an objective that the state does not offer: refused; a second accept: refused (alreadyActive)");
        // A reward that the backend cannot pay: a loot container, a standing. The quest stays ready.
        for (const char* id : {"Qb_Pouch", "Qb_Standing"}) {
            check(shop.host.world.quests().grant(*a, id, "command", 6).ok(), std::string("control: the ready quest ") + id);
        }
        const std::uint32_t before = a->progression.experience;
        const world::HandInResult pouch = shop.host.world.quests().hand_in(*a, questfix::quest_of(*a, "Qb_Pouch")->instance);
        const world::HandInResult standing = shop.host.world.quests().hand_in(*a, questfix::quest_of(*a, "Qb_Standing")->instance);
        note("QB5 refused rewards: " + pouch.detail + "; " + standing.detail);
        check(pouch.error == world::QuestError::cannotPay && standing.error == world::QuestError::cannotPay && a->progression.experience == before
                  && questfix::quest_of(*a, "Qb_Pouch") != nullptr && questfix::quest_of(*a, "Qb_Standing") != nullptr
                  && shop.count(kCharacterA, "QbPouch") == 0,
              "a loot container item and a territory standing: the hand-in is refused, the quest stays ready, nothing is paid");
        // A full bag (decision D5 option A): refused, the quest stays ready.
        check(shop.host.world.quests().grant(*a, "Qb_Paid", "command", 6).ok(), "control: the ready quest with an item");
        std::uint32_t given = 0;
        while (given < 600 && shop.host.world.inventory().give(kCharacterA, "QbStone", 1).ok()) {
            ++given;
        }
        const std::uint64_t coinBefore = a->wallet.coin;
        const world::HandInResult full = shop.host.world.quests().hand_in(*a, questfix::quest_of(*a, "Qb_Paid")->instance);
        check(full.error == world::QuestError::bagFull && questfix::quest_of(*a, "Qb_Paid") != nullptr && a->wallet.coin == coinBefore
                  && shop.count(kCharacterA, "QbPants") == 0,
              "a full bag (" + std::to_string(given) + " items): the hand-in is refused (D5 A), no coin, no item, the quest stays ready");
    }
    // A duplicate hand-in after a new world entry.
    QuestShop shop(0, path, nullptr, true);
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    shop.walk_to(kCharacterA, kNearArtificer);
    const std::uint32_t experience = a->progression.experience;
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.hand_in(kCharacterA, kStart);
    check(questfix::quest_of(*a, kStart) == nullptr && a->progression.experience == experience && shop.hand_ins(world::QuestRequestResult::refused) == 1
              && a->quests.completed.empty(),
          "a hand-in of the completed quest after a new entry: refused, no reward; the completed notice of a new entry is empty");
    std::filesystem::remove(path);
}

/** QB6: two sessions at one person (I5, B4b, MP-10). */
void two_sessions(const Options&) {
    QuestShop shop;
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(*a, kStart, "territory", 6).ok() && shop.host.world.quests().grant(*b, kStart, "territory", 6).ok(),
          "control: A and B have the start quest");
    shop.play_start(kCharacterA);
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.request(shop.clientB, shop.talk(kArtificer));
    const std::string stateB = shop.host.conversations.state(static_cast<std::uint32_t>(b->conversation->state)).id;
    const std::uint64_t coinB = b->wallet.coin;
    const std::uint32_t experienceB = b->progression.experience;
    const std::string questB = questfix::states_text(*questfix::quest_of(*b, kStart));
    const std::size_t fromB = shop.clientB.got.size();
    shop.hand_in(kCharacterB, kStart);
    check(shop.hand_ins(world::QuestRequestResult::refused) == 1 && questfix::quest_of(*b, kStart) != nullptr,
          "B (its quest is not ready) hands in at the same person: refused");
    shop.hand_in(kCharacterA, kStart);
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    check(questfix::quest_of(*a, kStart) == nullptr && shop.has_bit(kCharacterA, "Qb_Start_Done") && !shop.has_bit(kCharacterB, "Qb_Start_Done")
              && b->wallet.coin == coinB && b->progression.experience == experienceB && questfix::states_text(*questfix::quest_of(*b, kStart)) == questB
              && b->conversation && b->conversation->open && b->interaction,
          "the hand-in of A changes nothing for B: its quest, its bits, its coin, its experience, its open conversation");
    check(questfix::foreign_fragments(shop.clientB, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0
              && count_type(shop.clientB, server::rep::kTypeCutsceneEndFinalized, fromB) == 0,
          "B gets no Objectives state of A and no lock reply");
    // The offer: A sees the offer state, B the state with no offer.
    shop.accept(kCharacterB, kNext);
    shop.accept(kCharacterA, kNext);
    check(stateB != "Art_Offer" && questfix::quest_of(*b, kNext) == nullptr && questfix::quest_of(*a, kNext) != nullptr
              && shop.offers(world::QuestRequestResult::notOffered) == 1,
          "the offer of the next quest: A accepts it from its state; B (no hand-in) gets notOffered at the same person");
    // The presence: the pirate is there for A only.
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit), "control: the pirate is there for A");
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    check(shop.persons().present(*a, *shop.host.interactions.objects()[1].person) && !shop.persons().present(*b, *shop.host.interactions.objects()[1].person),
          "the presence of a person of a swap for each character (no shared state)");
}

/** QB7: the load (QG4 of the research). */
void load(const Options&) {
    struct Work {
        double seconds{0.0};
        std::uint64_t lookups{0};
        std::uint64_t states{0};
        std::uint64_t selections{0};
        std::uint64_t questVisits{0};
        std::uint64_t instanceLookups{0};
        std::uint64_t accepts{0};
    };
    const auto run = [](std::size_t slots) {
        QuestShop shop(slots);
        shop.enter();
        world::Entity* a = shop.entity(kCharacterA);
        world::Entity* b = shop.entity(kCharacterB);
        // 48 more characters in the world with active quests (no session): a request of A or B does not look at them.
        for (int index = 0; index < 48; ++index) {
            const std::string id = "0e18b000-0000-4000-8000-" + std::to_string(100000000000ULL + index).substr(0, 12);
            const Player extra = add_player(shop.host.store, kSteamIdA, "Load", id, "Loadone" + std::to_string(index), state::WorldPosition{120.0F, 200.0F, 30.0F});
            (void)extra;
            (void)shop.host.world.spawn_character(id, {120.0F + static_cast<float>(index), 200.0F, 30.0F});
            if (world::Entity* spawned = shop.host.world.find_character(id)) {
                (void)shop.host.world.quests().grant(*spawned, kStart, "command", 6);
                (void)shop.host.world.quests().grant(*spawned, "Qb_Cinema", "command", 6);
            }
        }
        for (world::Entity* who : {a, b}) {
            (void)shop.host.world.ownership().unlock_achievement(*who, "Qb_Start_Done", *shop.host.world.ownership().achievement_bit("Qb_Start_Done"));
            (void)shop.host.world.quests().grant(*who, "Qb_Cinema", "command", 6);
        }
        shop.request(shop.clientA, shop.talk(kArtificer));
        shop.request(shop.clientB, shop.talk(kArtificer));
        world::Persons& persons = shop.persons();
        const world::InteractionCounts countsBefore = shop.interactions().counts();
        const world::PersonCounts personsBefore = persons.counts();
        const world::QuestCounts questsBefore = shop.host.world.quests().counts();
        const auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < 20000; ++index) {
            world::Entity* who = index % 2 == 0 ? a : b;
            switch (index % 4) {
            case 0:
                (void)persons.accept(*who, qcrc(index % 8 == 0 ? kNext : "Qb_Cinema"));
                break;
            case 1:
                (void)persons.hand_in(*who, qcrc(kStart));
                break;
            case 2:
                (void)persons.topic(*who, crc("Qb_Topic"), false);
                break;
            default:
                (void)persons.accept(*who, qcrc("No_Such_Objective"));
                break;
            }
        }
        Work work;
        work.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        work.lookups = shop.interactions().counts().referenceLookups - countsBefore.referenceLookups;
        work.states = persons.counts().stateVisits - personsBefore.stateVisits;
        work.selections = persons.counts().selections - personsBefore.selections;
        work.questVisits = shop.host.world.quests().counts().questVisits - questsBefore.questVisits;
        work.instanceLookups = shop.host.world.quests().counts().instanceLookups - questsBefore.instanceLookups;
        work.accepts = shop.host.world.quests().counts().accepts - questsBefore.accepts;
        return work;
    };
    const Work small = run(2000);
    const Work large = run(200000);
    const auto text = [](const Work& work) {
        return std::to_string(work.seconds) + " s, reference lookups " + std::to_string(work.lookups) + ", state visits " + std::to_string(work.states)
               + ", selections " + std::to_string(work.selections) + ", quest visits " + std::to_string(work.questVisits) + ", instance lookups "
               + std::to_string(work.instanceLookups) + ", accepts " + std::to_string(work.accepts);
    };
    note("QB7 2,000 slots: " + text(small));
    note("QB7 200,000 slots: " + text(large));
    check(small.lookups == 0 && large.lookups == 0 && small.states == large.states && small.selections == large.selections
              && small.questVisits == large.questVisits && small.instanceLookups == large.instanceLookups && small.accepts == 1 && large.accepts == 1,
          "20,000 offer, hand-in, and topic checks of 2 characters with 48 more characters: no reference lookup (the conversation is the "
          "source), the same visits with 2,000 and 200,000 slots, one accept of the next quest (A asks for it; B gets nothing to accept)");
    check(small.seconds < 2.0 && large.seconds < 2.0, "the time of the 20,000 checks stays below 2 s with 200,000 slots");
}

/** QB8: the completed notice on the wire. */
void notice(const Options&) {
    QuestShop shop;
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, kStart, "territory", 6).ok() && shop.host.world.quests().grant(*a, "Qb_Cinema", "command", 6).ok(),
          "control: A has two quests");
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    std::optional<questfix::ObjectivesState> before = shop.last_state(shop.clientA, 0);
    check(before && !before->hasCompleted && before->active.size() == 2, "before a hand-in the state has no field 2");
    shop.play_start(kCharacterA);
    shop.request(shop.clientA, shop.talk(kArtificer));
    const std::size_t from = shop.clientA.got.size();
    const std::uint64_t instance = questfix::quest_of(*a, kStart)->instance;
    shop.hand_in(kCharacterA, kStart);
    shop.host.steps(20, {&shop.clientA, &shop.clientB});
    const std::optional<questfix::ObjectivesState> after = shop.last_state(shop.clientA, from);
    check(after && after->hasCompleted && after->completed == std::vector<std::uint64_t>{instance} && after->active.size() == 1
              && questfix::foreign_fragments(shop.clientB, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0,
          "after the hand-in: field 2 has the instance, the other quest stays active; B gets no Objectives state of A");
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(60, {&shop.clientB});
    FakeClient again{27003};
    shop.host.enter(again, shop.a.registration());
    questfix::send_2709(shop.host, again, kCharacterA);
    shop.host.steps(30, {&again, &shop.clientB});
    const std::optional<questfix::ObjectivesState> entry = shop.last_state(again, 0);
    check(entry && !entry->hasCompleted && entry->active.size() == 1, "a new world entry: no completed notice, the other quest is active");
}

} // namespace

std::vector<Case> quest_person_cases() {
    return {{"QB1", "quests 1.8b (I3): the loaders of the rewards, the whispers, the task cells, and the trigger data; the trigger tool", loaders},
            {"QB2", "quests 1.8b (I1, B1b): the start quest: trigger area, 6703 to 6707, 6706, 6705, hand-in 794 and its rewards", start_quest},
            {"QB3", "quests 1.8b (I2, B2b): the offer 3772, the archetype variants, return, whisper 5070, topic, reward items", next_offer},
            {"QB4", "quests 1.8b (I3): topic effects one time, cinematic 6606 and 6542, conversation state, trigger index", effects},
            {"QB5", "quests 1.8b (I4, B3b): the refusals change nothing (D5 A, unpaid rewards, a refused store write)", refusals},
            {"QB6", "quests 1.8b (I5, B4b, MP-10): two sessions at one person with different quest states", two_sessions},
            {"QB7", "quests 1.8b (I5): the load: 200,000 person slots, 50 characters, 20,000 checks", load},
            {"QB8", "quests 1.8b: the completed notice (field 2) on the wire", notice}};
}

} // namespace soulwarden::backend::tests
