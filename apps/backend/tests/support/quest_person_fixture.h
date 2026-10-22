#pragma once

// The quest and person fixture of the cases of stages 1.8b and 1.8d (made-up data only): the quest data of stage 1.8b, the conversation data
// of its persons, the items, the ids of the player slice, and QuestShop (a host with the persons, the quests, the trigger volume, the items,
// and the levels). The cases of stage 1.8b (quest_person_tests.cpp) keep their own copy.

#include <chrono>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "server/rep/quest_messages.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/person_fixture.h"
#include "support/quest_fixture.h"
#include "support/shop_fixture.h"
#include "support/test_progression.h"

namespace soulwarden::backend::tests::questperson {

using namespace personfix;

constexpr const char* kStart = "Qb_Start";
constexpr const char* kNext = "Qb_Next";
constexpr const char* kNextSoldier = "Qb_Next_Soldier";
constexpr const char* kCutscene = "Qb_Cutscene_IGC";
constexpr const char* kFilm = "Qb_Film";
/** The trigger volume of the start quest: 20 m to the north of the persons. */
const state::WorldPosition kTriggerPlace{100.0F, 222.0F, 30.0F};
const state::WorldPosition kNearArtificer{99.0F, 200.5F, 30.0F};

inline std::uint32_t qcrc(const std::string& text) {
    return gamedata::quest_crc(text);
}

/** The quest data of the cases: the person quest document with the quests of stage 1.8b. */
inline json quest_document_18b() {
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
inline json conversation_document_18b() {
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

inline std::vector<gamedata::ItemDefinition> quest_items() {
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
inline gamedata::SliceIds quest_slice() {
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

} // namespace soulwarden::backend::tests::questperson
