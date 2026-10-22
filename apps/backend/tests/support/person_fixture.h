#pragma once

// The person fixture of the cases of stage 2.5c (person_tests.cpp) and of stage 1.8b (quest_person_tests.cpp, review_persons_25c_tests.cpp):
// four made-up persons near the characters (an artificer that is gone while a swap bit is set, a pirate that is there while a swap bit is
// set, an innkeeper with services, a person with no binding), their NPC rows, conversation states, and topics, and a made-up quest whose
// first task is the condition of a topic state. All keys, ids, bits, and positions are made up.

#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/conversation_messages.h"
#include "server/rep/lock_messages.h"
#include "support/interaction_fixture.h"
#include "support/quest_fixture.h"

namespace soulwarden::backend::tests::personfix {

using namespace interaction;
using nlohmann::json;

/** The persons of the cases (tags of the fixture objects, in the sequence of the objects). */
enum PersonTag : std::uint8_t { kArtificer = 1, kPirate, kInnkeeper, kNoBinding };
constexpr std::uint32_t kTalk = 0;
constexpr std::uint32_t kSwapArtificerBit = 60;
constexpr std::uint32_t kSwapPirateBit = 61;

inline std::uint32_t crc(const std::string& id) {
    return gamedata::interaction_option_id(id);
}

inline gamedata::PersonBinding binding(const std::string& npc, gamedata::PersonPresence presence = gamedata::PersonPresence::always,
                                const char* condition = "") {
    gamedata::PersonBinding person;
    person.npc = npc;
    person.npcCrc = crc(npc);
    person.presence = presence;
    person.condition = condition;
    person.clientChild = presence != gamedata::PersonPresence::always;
    return person;
}

/** The interaction data of the cases: the four persons; `extra` made-up authored person slots far away (case NC7). */
inline bool set_person_data(gamedata::InteractionData& data, std::size_t extra = 0) {
    std::vector<gamedata::InteractionOption> options{option_row("OpenConversation", 63, true)};
    std::vector<gamedata::InteractionObject> objects{object_of(kArtificer, InteractionKind::npc, {98.0F, 201.0F, 30.0F}, {kTalk}),
                                                     object_of(kPirate, InteractionKind::npc, {102.0F, 201.0F, 30.0F}, {kTalk}),
                                                     object_of(kInnkeeper, InteractionKind::npc, {100.0F, 197.0F, 30.0F}, {kTalk}),
                                                     object_of(kNoBinding, InteractionKind::npc, {100.0F, 203.0F, 30.0F}, {kTalk})};
    objects[0].person = binding("Test_Artificer", gamedata::PersonPresence::hideWhen, "Test_Swap_Artificer");
    objects[1].person = binding("Test_Pirate", gamedata::PersonPresence::showWhen, "Test_Swap_Pirate");
    objects[2].person = binding("Test_Innkeeper");
    objects[3].person = binding("");
    objects[3].person->unresolved = "the variant 'Test_Missing' has no row of the variation table 'NPC'";
    objects.reserve(objects.size() + extra);
    for (std::size_t index = 0; index < extra; ++index) {
        // A person child with an id of the client rule (the low 3 bits 7) in a grid of 16 m, from 1 km away.
        gamedata::InteractionObject object = object_of(5, InteractionKind::npc,
                                                       {1000.0F + static_cast<float>(index % 700) * 16.0F,
                                                        1000.0F + static_cast<float>(index / 700) * 16.0F, 30.0F},
                                                       {kTalk});
        const std::uint64_t group = (static_cast<std::uint64_t>(index + 1) << 3) | 7ULL;
        for (std::size_t byte = 0; byte < 8; ++byte) {
            object.actor[byte] = static_cast<std::uint8_t>(group >> (8 * byte));
        }
        object.group = group;
        object.key = "0b1ec700-0000-4000-8000-1" + std::to_string(100000000000ULL + index).substr(1) + "/0000000000000001";
        object.bounds[0].target = 0x7200000000000000ULL + index;
        object.bounds[0].options[0].entity = group ^ 0x40ULL;
        object.person = binding("Test_Far_" + std::to_string(index % 50), gamedata::PersonPresence::showWhen, "Test_Swap_Pirate");
        objects.push_back(std::move(object));
    }
    return data.set(std::move(options), {}, std::move(objects));
}

/** The conversation data of the cases (the form of conversation-data.json). */
inline json conversation_document(std::size_t extraNpcs = 0) {
    json npcs = json::array({
        {{"id", "Test_Artificer"}, {"name", "@Test_Artificer"}, {"states", json::array({"Art_Default", "Art_Level", "Art_TaskTopic"})}},
        {{"id", "Test_Pirate"}, {"name", "@Test_Pirate"}, {"states", json::array({"Pirate_Default"})}},
        {{"id", "Test_Innkeeper"}, {"name", "@Test_Innkeeper"}, {"states", json::array({"Inn_Default", "Inn_Faction", "Missing_State"})}},
    });
    for (std::size_t index = 0; index < extraNpcs; ++index) {
        npcs.push_back({{"id", "Test_Far_" + std::to_string(index)}, {"states", json::array({"Art_Default"})}});
    }
    return {{"format", 1},
            {"npcs", npcs},
            {"states",
             json::array({{{"id", "Art_Default"}, {"type", "Linear_Objective"}, {"initial", true}},
                          {{"id", "Art_Level"}, {"type", "Linear_Objective"}, {"requires", {{"RequiredLevel", "99"}}}, {"topics", json::array({"Test_Topic_Ask"})}},
                          {{"id", "Art_TaskTopic"},
                           {"type", "Linear_Objective"},
                           {"requires", {{"RequiredActiveObjectiveId", "Test_TopicQuest"}, {"RequiredActiveTaskId", "Test_Topic_Talk"}}},
                           {"topics", json::array({"Test_Topic_Ask", "Test_Topic_Gated", "Test_Topic_Missing"})}},
                          {{"id", "Pirate_Default"}, {"type", "Conversation"}, {"services", json::array({"Open_Shop"})}},
                          {{"id", "Inn_Default"}, {"type", "Linear_Objective"}, {"services", json::array({"Inn", "Open_Shop", "No_Such_Service"})}},
                          {{"id", "Inn_Faction"}, {"type", "Linear_Objective"}, {"requires", {{"RequiredFaction", "Syndicate"}}}}})},
            {"topics", json::array({{{"id", "Test_Topic_Ask"}, {"prompt", "@Ask"}, {"response", "@Answer"}},
                                    {{"id", "Test_Topic_Gated"}, {"prompt", "@Gated"}, {"requires", {{"RequiredAchievement", "Test_Never"}}}},
                                    {{"id", "Test_Topic_Pirate"}, {"prompt", "@Pirate"}}})}};
}

/** The quest data of the cases: the document of the quest cases with a quest whose first task is the condition of a topic state. */
inline json person_quest_document() {
    json document = questfix::quest_document();
    document["tasks"]["test_topic_talk"] = questfix::task("Test_Topic_Talk", "taskachievement", 18, {{"ItemName", "Test_Topic_Done"}});
    document["tasks"]["test_topic_after"] = questfix::task("Test_Topic_After", "taskachievement", 18, {{"ItemName", "Test_Never"}});
    document["tasks"]["test_topic_root"] = questfix::task("Test_Topic_Root", "consecutivetaskcontainer", 1);
    document["objectives"].push_back({{"id", "Test_TopicQuest"},
                                      {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "test_topic_talk", json::array()}),
                                                            json::array({1, "test_topic_after", json::array()}),
                                                            json::array({2, "test_topic_root", json::array({0, 1})})})}});
    for (const auto& [id, bit] : std::vector<std::pair<const char*, std::uint32_t>>{
             {"Test_Swap_Artificer", kSwapArtificerBit}, {"Test_Swap_Pirate", kSwapPirateBit}, {"Test_Topic_Done", 62}, {"Test_Never", 63}}) {
        document["achievements"].push_back(json::array({id, bit}));
    }
    return document;
}

/** A host with the persons, the conversation data, the quest data, and the characters A and B near the persons. */
struct PersonShop : Shop {
    explicit PersonShop(bool conversations = true, std::size_t extra = 0, const std::string& database = ":memory:",
                        const gamedata::SliceIds* slice = nullptr, bool existing = false)
        : Shop(false, database, true, existing, slice) {
        if (!set_person_data(host.interactions, extra)) {
            throw std::runtime_error("person test: the interaction data was not set: " + host.interactions.error());
        }
        if (conversations && !host.conversations.load_text(conversation_document(extra > 0 ? 50 : 0).dump())) {
            throw std::runtime_error("person test: the conversation data was not set: " + host.conversations.error());
        }
        if (!host.quests.load_text(person_quest_document().dump())) {
            throw std::runtime_error("person test: the quest data was not set: " + host.quests.error());
        }
    }

    [[nodiscard]] world::Persons& persons() {
        return host.world.persons();
    }
    [[nodiscard]] world::Interactions& interactions() {
        return host.world.interactions();
    }
    [[nodiscard]] InteractionBlock talk(std::uint8_t tag) const {
        return block_for(host.interactions, tag, kTalk);
    }
    FakeClient& client_of(const char* character) {
        return std::string(character) == kCharacterA ? clientA : clientB;
    }
    /** 3158 (or one 5397 with the topics) of the client of a character. */
    void topic(const char* character, const std::vector<std::uint32_t>& topics, bool closing = false) {
        const world::Entity* own = entity(character);
        FakeClient& client = client_of(character);
        const Bytes body = closing ? server::rep::finished_topics_body(0, own->id, topics)
                                   : server::rep::conversation_topic_body(0, own->id, topics.front());
        host.datagram(client.peer, client.component_message(closing ? server::rep::kTypeConversationTopicsFinished
                                                                    : server::rep::kTypeConversationTopic,
                                                            own->actor.bytes, body));
        host.steps(2, {&clientA, &clientB});
    }
    /** The close of the conversation by the client of a character: 6705, then 2473 with the reference, then the end of the block. */
    void close(const char* character, std::uint8_t tag) {
        const world::Entity* own = entity(character);
        FakeClient& client = client_of(character);
        host.datagram(client.peer, client.component_call(server::rep::kTypeCutsceneEnded, own->actor.bytes,
                                                         own->id + server::rep::kPlayerCutsceneComponent, own->id));
        const InteractionBlock block = talk(tag);
        server::hub::Writer cancel;
        cancel.u64(own->id + server::rep::kInteractorComponent);
        cancel.u64(own->id);
        cancel.raw(block.actor);
        cancel.u64(block.group);
        cancel.u64(block.target);
        host.datagram(client.peer, client.component_message(kCancelMessage, own->actor.bytes, cancel.data()));
        host.steps(2, {&clientA, &clientB});
        end_block(client);
    }
    /** A walk of a character to a place: the reports of its client there for 4 s (the position check takes a move in its time). */
    void walk(const char* character, const state::WorldPosition& place) {
        FakeClient& client = client_of(character);
        (std::string(character) == kCharacterA ? placeA : placeB) = place;
        for (int step = 0; step < 120; ++step) {
            report(client, std::nullopt);
            host.steps(1, {&clientA, &clientB});
        }
    }
    [[nodiscard]] std::uint64_t topic_count(world::TopicResult result) {
        return persons().counts().topics[static_cast<std::size_t>(result)];
    }
};

} // namespace soulwarden::backend::tests::personfix
