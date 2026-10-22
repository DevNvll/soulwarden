// Cases of stage 2.5c: the persons (world/persons.h, gamedata/conversation_data.h, the member person of gamedata/interaction_data.h, the
// messages 3158 and 5397 of server/rep/conversation_messages.h; docs/systems/persons.md). The data has the form of the files of
// tools/gamedata/interactions.py (format 4) and tools/gamedata/conversations.py: four made-up persons near the characters (an artificer
// that is gone while a swap bit is set, a pirate that is there while a swap bit is set, an innkeeper with services, a person with no
// binding), their NPC rows, conversation states, and topics, and a made-up quest whose first task is the condition of a topic state. All
// keys, ids, bits, and positions are made up.
//
//   NC1  gate R (live check L1): the client id of a person child and the export of a person (check_interactions_tool.py --persons)
//   NC2  I1: the loaders: a person of format 4 (a presence rule, a cause), the conversation data (the rows, a state or a topic with no
//        row, two CRCs), no file (a person keeps its conversation), the conversation data tool (check_conversations_tool.py)
//   NC3  I2 (MP-10): two sessions at one person: both get 3743 result 1; the close of A ends only A; B gets no 6709 and no 3746
//   NC4  I3: the state selection of each character and the topics (3158, 5397): A has the quest task, B does not; a wrong source, a stale
//        revision, a completed task, a second finish, a closed conversation change nothing; no reward and no quest change
//   NC5  I4: the services of a person in the grant API: two characters, the close before the operation, a new source, the range, the
//        loss of the presence (one character), the death, a travel, a new world entry
//   NC6  I5: requests that are not correct (an unknown actor at the place of a person, a known actor with a wrong target, option, or
//        option entity, too far, not there, no binding): 3743 result 2, nothing changes for both characters, no alias entity
//   NC7  I6: the load: 2,000 and 200,000 authored person slots, the same two characters, 20,000 requests; the lookups and the visits do
//        not grow with the slots; no tick visits the persons
//   NC8  I3: the rule of each requirement (the static read of the client): the faction values (Any, a name with !, a name that the client
//        does not know), the cooldown of a state only, the objective rule with a list, the completed objective, the progression rule,
//        the ProvidedTopics of a topic, and the gates of the faction services

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/progression_tables.h"
#include "server/rep/conversation_messages.h"
#include "server/rep/lock_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/interaction_fixture.h"
#include "support/quest_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;
using nlohmann::json;

/** The persons of the cases (tags of the fixture objects, in the sequence of the objects). */
enum PersonTag : std::uint8_t { kArtificer = 1, kPirate, kInnkeeper, kNoBinding };
constexpr std::uint32_t kTalk = 0;
constexpr std::uint32_t kSwapArtificerBit = 60;
constexpr std::uint32_t kSwapPirateBit = 61;

std::uint32_t crc(const std::string& id) {
    return gamedata::interaction_option_id(id);
}

gamedata::PersonBinding binding(const std::string& npc, gamedata::PersonPresence presence = gamedata::PersonPresence::always,
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
bool set_person_data(gamedata::InteractionData& data, std::size_t extra = 0) {
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
json conversation_document(std::size_t extraNpcs = 0) {
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
json person_quest_document() {
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
    explicit PersonShop(bool conversations = true, std::size_t extra = 0) : Shop(false) {
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

/** Runs a check script of the data tools and @return (status, ok lines, FAIL lines). */
std::tuple<int, std::size_t, std::size_t> run_check(const Options& options, const char* script, const char* arguments, const char* name) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata" / script;
    const std::filesystem::path output = options.workDirectory / name;
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + path.lexically_normal().string() + "\" "
                                + arguments + " > \"" + output.string() + "\" 2>&1";
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

/** NC1: gate R: the client id rule of a person child and the export of a person (the tool with made-up sources). */
void client_id_rule(const Options& options) {
    const auto [status, ok, fail] = run_check(options, "check_interactions_tool.py", "--persons", "check-persons.log");
    check(status == 0 && fail == 0 && ok >= 8, "check_interactions_tool.py --persons: each case passes (" + std::to_string(ok) + " cases)");
}

/** NC2: the loaders of the person member and of the conversation data; no conversation data; the conversation data tool. */
void loaders(const Options& options) {
    const auto document = [](const json& person) {
        json object = {{"key", "0b1ec700-0000-4000-8000-000000000001/0000000000000001"},
                       {"kind", "npc"},
                       {"slice", "slices/test/person.dynamicslice"},
                       {"asset", "a55e7001000000000000000000000001"},
                       {"sub", 2},
                       {"actor", "0f11223344556677" + std::string(16, '0')},
                       {"region", "r_+00_+00"},
                       {"position", {100.0, 200.0, 30.0}},
                       {"bounds",
                        json::array({{{"slot", 5},
                                      {"component", "c000000000000001"},
                                      {"target", "7100000000000001"},
                                      {"position", {100.0, 200.0, 30.0}},
                                      {"rangeSq", 25.0},
                                      {"capacity", -1},
                                      {"lineOfSight", false},
                                      {"arcRequired", false},
                                      {"arc", 0.0},
                                      {"options", json::array({json::array({0, "1122334455667700"})})},
                                      {"lore", -1},
                                      {"requires", json::object()}}})}};
        if (!person.is_null()) {
            object["person"] = person;
        }
        return json{{"format", 4},
                    {"options", json::array({{{"name", "OpenConversation"},
                                              {"id", crc("OpenConversation")},
                                              {"action", 63},
                                              {"committed", true},
                                              {"delay", 0.0},
                                              {"hold", 0.5},
                                              {"instantCancel", false},
                                              {"cooldown", 0},
                                              {"maxUsage", 0.0},
                                              {"inactive", 0.0},
                                              {"requiredLevel", 0},
                                              {"requiredItem", ""},
                                              {"requiredItemCount", 0},
                                              {"requiredCurrency", 0},
                                              {"requiredAchievement", -1},
                                              {"statusRules", false}}})},
                    {"achievements", json::array()},
                    {"objects", json::array({object})},
                    {"templates", json::array()},
                    {"spawned", json::array()},
                    {"fixtures", json::object()},
                    {"lootContainers", json::object()}};
    };
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "nc2-interaction-data.json";
    const auto load = [&](const json& value) {
        std::ofstream(path) << value.dump();
        gamedata::InteractionData data;
        const bool ok = data.load(path.string());
        if (!ok) {
            note("NC2 refused: " + data.error());
        }
        return std::make_pair(ok, ok && !data.objects().empty() ? data.objects()[0].person : std::optional<gamedata::PersonBinding>{});
    };
    const auto [walkaway, walkawayPerson] = load(document({{"npc", "01_Test_Person"},
                                                           {"variant", "01_Test_Person"},
                                                           {"table", "NPC_Walkaway"},
                                                           {"row", 7},
                                                           {"presence", {{"mode", "showWhen"}, {"condition", "!Test_Swap"}}},
                                                           {"clientChild", true},
                                                           {"spawner", "0b1ec700-0000-4000-8000-000000000001"}}));
    check(walkaway && walkawayPerson && walkawayPerson->npc == "01_Test_Person" && walkawayPerson->npcCrc == crc("01_test_person")
              && walkawayPerson->presence == gamedata::PersonPresence::showWhen && walkawayPerson->condition == "!Test_Swap"
              && walkawayPerson->row == 7 && walkawayPerson->clientChild && walkawayPerson->unresolved.empty(),
          "format 4: a person child with its NPC row, its CRC (lower case), its presence rule, its spawner");
    const auto [unresolved, unresolvedPerson] = load(document({{"npc", ""}, {"unresolved", "the variant 'X' has no row"}}));
    const auto [old, oldPerson] = load(document(nullptr));
    check(unresolved && unresolvedPerson && !unresolvedPerson->unresolved.empty() && old && !oldPerson,
          "a person with a cause and no NPC id loads (the backend refuses its use); an object with no member person has no binding");
    check(!load(document({{"npc", "X"}, {"presence", {{"mode", "sometimes"}, {"condition", "A"}}}})).first
              && !load(document({{"npc", "X"}, {"presence", {{"mode", "showWhen"}, {"condition", ""}}}})).first
              && !load(document({{"npc", ""}})).first && !load(document({{"npc", "X"}, {"row", -2}})).first,
          "the loader refuses a person with an unknown mode, an empty condition, no NPC id and no cause, or a row below 0");
    std::filesystem::remove(path);
    // The conversation data.
    gamedata::ConversationData conversations;
    check(conversations.load_text(conversation_document().dump()) && conversations.npc("test_artificer") != nullptr
              && conversations.npc("Test_Artificer")->states.size() == 3 && conversations.npc("Test_Innkeeper")->states.size() == 2
              && conversations.state(conversations.npc("Test_Artificer")->states[2]).topics.size() == 2
              && conversations.topic_of_crc(crc("Test_Topic_Ask")).has_value() && conversations.npc("Missing") == nullptr,
          "the conversation data: an NPC row (any case), its states in order; a state with no row and a topic with no row are not in the lists");
    json level = conversation_document();
    level["states"][1]["requires"]["RequiredLevel"] = "ten";
    check(conversations.load_text(level.dump())
              && conversations.state(conversations.npc("Test_Artificer")->states[1]).requirements.malformed,
          "a level that is not a whole number marks the requirement (it does not pass)");
    json twice = conversation_document();
    twice["topics"].push_back({{"id", "TEST_TOPIC_ASK"}});
    json noNpc = conversation_document();
    noNpc["npcs"] = json::array();
    check(!conversations.load_text(twice.dump()) && !conversations.load_text(noNpc.dump()) && !conversations.load("/no/such/file.json")
              && !conversations.load_text(json{{"format", 2}, {"npcs", json::array()}, {"states", json::array()}, {"topics", json::array()}}.dump()),
          "the loader refuses two topics with one id, a file with no NPC row, no file, and a different format");
    // No conversation data: a person keeps its conversation (3743 result 1), with no state, no topic, and no service.
    PersonShop shop(false);
    shop.enter();
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    const world::Entity* a = shop.entity(kCharacterA);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{63U, 1U}} && a->interaction && a->conversation
              && a->conversation->state < 0 && a->conversation->services.empty() && !a->serviceGrant,
          "no conversation data: the use is accepted (3743 result 1), with no state, no service, and no grant");
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    check(shop.topic_count(world::TopicResult::noData) == 1, "no conversation data: a topic changes nothing (noData)");
    const auto [status, ok, fail] = run_check(options, "check_conversations_tool.py", "", "check-conversations.log");
    check(status == 0 && fail == 0 && ok >= 5, "check_conversations_tool.py: each case passes (" + std::to_string(ok) + " cases)");
}

/** NC3: two sessions at one person (MP-10): the close of A ends only A; B gets no lock reply. */
void two_sessions(const Options&) {
    PersonShop shop;
    shop.enter();
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.request(shop.clientB, shop.talk(kArtificer));
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    const std::uint64_t person = shop.talk(kArtificer).group;
    check(replies(shop.clientA, fromA) == std::vector<std::pair<unsigned, unsigned>>{{63U, 1U}}
              && replies(shop.clientB, fromB) == std::vector<std::pair<unsigned, unsigned>>{{63U, 1U}} && a->interaction && b->interaction
              && a->conversation && a->conversation->open && b->conversation && b->conversation->open
              && shop.persons().conversation_count() == 2,
          "A and B use one person (no limit of users): each gets 3743 result 1 and its own conversation");
    shop.host.steps(60, {&shop.clientA, &shop.clientB});
    const std::vector<std::uint64_t> interactorA = interactor_values(shop.clientA, 1, fromA);
    const auto statesA = object_states(shop.clientA, fromA);
    note("NC3 Interactor values of A: " + std::to_string(interactorA.size()) + ", last " + (interactorA.empty() ? std::string("-") : std::to_string(interactorA.back()))
         + ", person " + std::to_string(person) + "; object states " + std::to_string(statesA.size()) + ", last users "
         + (statesA.empty() ? std::string("-") : std::to_string(statesA.back().second)));
    check(!interactorA.empty() && interactorA.back() == person && !statesA.empty() && statesA.back() == std::make_pair(true, 2U),
          "the Interactor state of A has the G of the person; the object state has 2 users (one record path for each observer)");
    const std::size_t closeA = shop.clientA.got.size();
    const std::size_t closeB = shop.clientB.got.size();
    shop.close(kCharacterA, kArtificer);
    check(count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, closeA) == 1 && !a->interaction && a->conversation
              && !a->conversation->open,
          "the close of A: 6709 to A, the use of A ends, its conversation is closed (it stays for the 5397 of the close)");
    shop.host.steps(60, {&shop.clientA, &shop.clientB});
    const auto statesB = object_states(shop.clientB, closeB);
    check(count_type(shop.clientB, server::rep::kTypeCutsceneEndFinalized, closeB) == 0 && count_type(shop.clientB, kServerCancel, closeB) == 0
              && b->interaction && b->conversation && b->conversation->open && !statesB.empty() && statesB.back() == std::make_pair(true, 1U),
          "B: no 6709, no 3746; its use and its conversation stay; the object state has 1 user");
    shop.close(kCharacterB, kArtificer);
    check(!b->interaction && count_type(shop.clientB, server::rep::kTypeCutsceneEndFinalized, closeB) == 1,
          "the close of B ends B with its own 6709");
}

/** NC4: the state selection of each character and the topics. */
void topics(const Options&) {
    PersonShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(*a, "Test_TopicQuest", "command", 0).ok(), "control: A gets the quest of the topic");
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.request(shop.clientB, shop.talk(kArtificer));
    const gamedata::ConversationData& data = shop.host.conversations;
    const auto state_of = [&](const world::Entity* entity) {
        return entity->conversation && entity->conversation->state >= 0 ? data.state(static_cast<std::uint32_t>(entity->conversation->state)).id
                                                                         : std::string("(none)");
    };
    check(state_of(a) == "Art_TaskTopic" && a->conversation->topics.size() == 1
              && data.topic(a->conversation->topics[0]).id == "Test_Topic_Ask" && state_of(b) == "Art_Default" && b->conversation->topics.empty(),
          "the selection (states in reverse, the first that passes): A with the active task gets Art_TaskTopic and its topic (not the "
          "gated topic, not the topic with no row); B gets Art_Default (a level 99 state does not pass)");
    const world::QuestInstance* quest = questfix::quest_of(*a, "Test_TopicQuest");
    const std::string before = quest != nullptr ? questfix::states_text(*quest) : "";
    const std::vector<std::uint32_t> bitsBefore = a->achievements;
    shop.topic(kCharacterB, {crc("Test_Topic_Ask")});
    check(shop.topic_count(world::TopicResult::notOffered) == 1 && b->conversation->finished.empty(),
          "B (no task) asks for the topic: not offered, no change");
    shop.topic(kCharacterA, {crc("Test_Topic_Gated")});
    shop.topic(kCharacterA, {crc("No_Such_Topic")});
    check(shop.topic_count(world::TopicResult::notOffered) == 2 && shop.topic_count(world::TopicResult::unknownTopic) == 1
              && a->conversation->finished.empty(),
          "A asks for the gated topic (its achievement is false) and an unknown topic: no change");
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    quest = questfix::quest_of(*a, "Test_TopicQuest");
    check(shop.topic_count(world::TopicResult::accepted) == 1 && shop.topic_count(world::TopicResult::duplicate) == 1
              && a->conversation->finished == std::vector<std::uint32_t>{crc("Test_Topic_Ask")} && quest != nullptr
              && questfix::states_text(*quest) == before && a->achievements == bitsBefore,
          "A asks for its topic: accepted one time (a second finish changes nothing); no quest task and no achievement changes "
          "(stage 1.8b gives the topic its effects)");
    shop.close(kCharacterA, kArtificer);
    shop.topic(kCharacterA, {crc("Test_Topic_Ask"), crc("Test_Topic_Gated")}, true);
    check(shop.topic_count(world::TopicResult::duplicate) == 2 && shop.topic_count(world::TopicResult::notOffered) == 3,
          "the 5397 of the close (after the 2473, live check L3): the finished topic again changes nothing; the gated topic is not offered");
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    check(shop.topic_count(world::TopicResult::noConversation) == 1, "a 3158 after the close: no open conversation, no change");
    // A stale revision: the task completes after the selection; the next topic request makes the selection again (no task: no topic).
    shop.request(shop.clientA, shop.talk(kArtificer));
    check(state_of(a) == "Art_TaskTopic" && a->conversation->finished.empty(), "control: a new conversation has its own finished topics");
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Topic_Done", 62), "control: the bit of the task");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    quest = questfix::quest_of(*a, "Test_TopicQuest");
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    note("NC4 after the task: " + (quest != nullptr ? questfix::states_text(*quest) : std::string("(no quest)")) + ", state " + state_of(a));
    check(quest != nullptr && quest->tasks[0].state == world::quest_state::kComplete && state_of(a) == "Art_Default"
              && shop.topic_count(world::TopicResult::notOffered) == 4 && a->conversation->finished.empty(),
          "a completed task and a stale revision: the selection again gives Art_Default, the topic is not offered, no change");
    // A wrong source: the topic of the artificer in a conversation with the pirate.
    shop.end_block(shop.clientA);
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit), "control: the pirate is there for A");
    shop.host.steps(2, {&shop.clientA});
    shop.request(shop.clientA, shop.talk(kPirate));
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    check(a->conversation && a->conversation->npc == "Test_Pirate" && shop.topic_count(world::TopicResult::notOffered) == 5,
          "a topic of a different person (a wrong source): not offered, no change");
}

/** NC5: the services of a person in the grant API of stage 2.5b. */
void services(const Options&) {
    PersonShop shop;
    shop.enter();
    world::Interactions& interactions = shop.interactions();
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    shop.request(shop.clientB, shop.talk(kInnkeeper));
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(a->serviceGrant && a->serviceGrant->kind == world::kConversationServiceKind && a->serviceGrant->ids == std::vector<std::uint32_t>{1, 16}
              && b->serviceGrant && b->serviceGrant->ids == std::vector<std::uint32_t>{1, 16} && shop.persons().counts().grants == 2,
          "each character gets the services of the state (Inn 1, Open_Shop 16; a name with no number of the client enum is not in it)");
    shop.close(kCharacterA, kInnkeeper);
    check(!a->interaction && interactions.require_service(kCharacterA, world::kConversationServiceKind, 16) == world::ServiceError::none
              && interactions.require_service(kCharacterA, world::kConversationServiceKind, 6) == world::ServiceError::notOffered,
          "the close (a conversation-only 2473) keeps the grant of A: a later operation with no source passes for an offered id only");
    shop.request(shop.clientA, shop.talk(kArtificer));
    check(!a->serviceGrant && b->serviceGrant
              && interactions.require_service(kCharacterA, world::kConversationServiceKind, 1) == world::ServiceError::noGrant,
          "A uses a different person: its grant ends; the grant of B stays");
    shop.close(kCharacterA, kArtificer);
    // The range: B walks 20 m away (the use ends too).
    shop.end_block(shop.clientB);
    shop.walk(kCharacterB, {101.0F, 212.0F, 30.0F});
    check(!b->serviceGrant && b->position.y > 211.0F, "B walks out of the range of the innkeeper: its grant ends");
    shop.walk(kCharacterB, {101.0F, 200.0F, 30.0F});
    check(!b->serviceGrant, "B walks back: the grant does not come back");
    // The loss of the presence for one character: the pirate is there for A and B while their bit is set.
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit)
              && shop.host.world.ownership().unlock_achievement(*b, "Test_Swap_Pirate", kSwapPirateBit),
          "control: the pirate is there for A and B");
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    shop.request(shop.clientA, shop.talk(kPirate));
    shop.request(shop.clientB, shop.talk(kPirate));
    check(a->serviceGrant && a->serviceGrant->ids == std::vector<std::uint32_t>{16} && b->serviceGrant && a->interaction && b->interaction,
          "control: A and B use the pirate (Open_Shop)");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    check(shop.host.world.ownership().lock_achievement(*a, "Test_Swap_Pirate"), "control: A loses the bit");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!a->interaction && !a->serviceGrant && !a->conversation && count_type(shop.clientA, kServerCancel, fromA) == 1
              && shop.persons().counts().presenceEnds == 1,
          "the pirate goes for A (its bit): the use of A ends (3746), its grant and its conversation end");
    check(b->interaction && b->serviceGrant && b->conversation && count_type(shop.clientB, kServerCancel, fromB) == 0,
          "B keeps its use, its grant, and its conversation (the presence of one character changes no shared state)");
    // The death of B, then a travel of A, then a new world entry of B.
    (void)shop.host.world.health().hurt(kCharacterB, 1.0e9F);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!b->serviceGrant && !b->conversation, "the death of B: its grant and its conversation end");
    (void)shop.host.world.health().heal(kCharacterB);
    shop.end_block(shop.clientA);
    shop.request(shop.clientA, shop.talk(kInnkeeper));
    check(a->serviceGrant && a->conversation, "control: A uses the innkeeper again");
    (void)shop.host.world.travel(kCharacterA, state::WorldPosition{140.0F, 200.0F, 30.0F});
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!a->serviceGrant && !a->conversation, "a travel of A: its grant and its conversation end");
    shop.end_block(shop.clientB);
    shop.request(shop.clientB, shop.talk(kInnkeeper));
    const world::EntityId before = b->id;
    shop.host.disconnect(shop.clientB.peer);
    shop.host.steps(60, {&shop.clientA});
    FakeClient again{27002};
    shop.host.enter(again, shop.b.registration());
    shop.host.steps(30, {&shop.clientA, &again});
    const world::Entity* back = shop.entity(kCharacterB);
    check(back != nullptr && back->id != before && !back->serviceGrant && !back->conversation,
          "a new world entry of B: no grant and no conversation");
}

/** NC6: requests that are not correct change nothing for both characters, and no alias is made. */
void refusals(const Options&) {
    PersonShop shop;
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(*b, "Test_TopicQuest", "command", 0).ok(), "control: B has the quest of the topic");
    shop.request(shop.clientB, shop.talk(kInnkeeper));
    const world::PersonConversation conversationB = *b->conversation;
    const world::ServiceGrant grantB = *b->serviceGrant;
    const std::string questB = questfix::states_text(*questfix::quest_of(*b, "Test_TopicQuest"));
    const std::size_t entities = shop.interactions().object_entities();
    std::vector<InteractionBlock> wrong;
    // An actor of the client rule (the low 3 bits 7) at the place of the artificer, with the target of the XOR rule for that G.
    InteractionBlock alias = shop.talk(kArtificer);
    const std::uint64_t made = 0x5A5A5A5A12345677ULL;
    for (std::size_t byte = 0; byte < 8; ++byte) {
        alias.actor[byte] = static_cast<std::uint8_t>(made >> (8 * byte));
    }
    alias.group = made;
    alias.target = (alias.target ^ shop.talk(kArtificer).group) ^ made;
    alias.optionEntity = made ^ 0x40ULL;
    wrong.push_back(alias);
    InteractionBlock target = shop.talk(kArtificer);
    target.target ^= 0x10ULL;
    wrong.push_back(target);
    InteractionBlock entity = shop.talk(kArtificer);
    entity.optionEntity ^= 0x1ULL;
    wrong.push_back(entity);
    InteractionBlock option = shop.talk(kArtificer);
    option.optionId = gamedata::interaction_option_id("OpenMissionBoardOption");
    wrong.push_back(option);
    InteractionBlock actor = shop.talk(kArtificer);
    actor.actor[10] = 0x01;
    wrong.push_back(actor);
    wrong.push_back(shop.talk(kNoBinding));
    wrong.push_back(shop.talk(kPirate)); // not there for A (no bit)
    const auto unchanged = [&] {
        return !a->interaction && !a->conversation && !a->serviceGrant && b->interaction && b->conversation
               && b->conversation->serial == conversationB.serial && b->conversation->open && b->serviceGrant
               && b->serviceGrant->ids == grantB.ids && questfix::states_text(*questfix::quest_of(*b, "Test_TopicQuest")) == questB
               && shop.interactions().object_entities() == entities;
    };
    std::size_t refused = 0;
    for (const InteractionBlock& block : wrong) {
        const std::size_t from = shop.clientA.got.size();
        shop.request(shop.clientA, block);
        refused += replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{63U, 2U}} ? 1 : 0;
        shop.end_block(shop.clientA);
    }
    note("NC6 refused " + std::to_string(refused) + " of " + std::to_string(wrong.size()));
    check(refused == wrong.size() && unchanged(),
          "an unknown actor at the place of a person, a wrong target, option entity, option, or actor, a person with no binding, and a "
          "person that is not there for A: 3743 result 2 each; A has no use, conversation, or grant; B and its quest do not change; no "
          "object entity is made");
    const auto& verdicts = shop.interactions().counts().verdicts;
    check(verdicts[static_cast<std::size_t>(world::InteractionVerdict::unknownObject)] >= 3
              && verdicts[static_cast<std::size_t>(world::InteractionVerdict::unknownOption)] >= 2
              && verdicts[static_cast<std::size_t>(world::InteractionVerdict::unsupported)] >= 1
              && verdicts[static_cast<std::size_t>(world::InteractionVerdict::notPresent)] == 1,
          "the causes: unknown object (no alias), unknown option, unsupported (no binding), not present");
    // Too far: the known actor and reference of the artificer from 10 m.
    shop.walk(kCharacterA, {100.0F, 210.0F, 30.0F});
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, shop.talk(kArtificer));
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{63U, 2U}} && unchanged(),
          "the reference of a person from 10 m: refused (too far), no change");
    shop.end_block(shop.clientA);
    shop.topic(kCharacterA, {crc("Test_Topic_Ask")});
    check(shop.topic_count(world::TopicResult::noConversation) == 1 && unchanged(), "a topic with no conversation: no change");
}

/** NC7: the load: the authored person slots do not change the work of a request or of a tick. */
void load(const Options&) {
    struct Work {
        double seconds{0.0};
        std::uint64_t lookups{0};
        std::uint64_t states{0};
        std::uint64_t topics{0};
        std::uint64_t selections{0};
        std::uint64_t accepted{0};
        std::uint64_t tickChecks{0};
    };
    const auto run = [](std::size_t slots) {
        PersonShop shop(true, slots);
        shop.enter();
        world::Entity* a = shop.entity(kCharacterA);
        world::Entity* b = shop.entity(kCharacterB);
        (void)shop.host.world.quests().grant(*a, "Test_TopicQuest", "command", 0);
        world::Interactions& interactions = shop.interactions();
        world::Persons& persons = shop.persons();
        const world::InteractionCounts countsBefore = interactions.counts();
        const world::PersonCounts personsBefore = persons.counts();
        const auto request_of = [&](std::uint8_t tag, bool valid) {
            const InteractionBlock block = shop.talk(tag);
            world::InteractionRequest request;
            request.object = world::InteractionReference{block.actor, block.group, valid ? block.target : block.target ^ 0x10ULL};
            request.action = block.action;
            request.baseAction = block.baseAction;
            request.optionEntity = block.optionEntity;
            request.optionId = block.optionId;
            request.committed = true;
            return request;
        };
        const auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < 20000; ++index) {
            world::Entity* who = (index % 2 == 0) ? a : b;
            const std::uint8_t tag = (index % 4 < 2) ? kArtificer : kInnkeeper;
            const world::InteractionRequest request = request_of(tag, index % 5 != 4);
            (void)interactions.request(who->owner, request);
            (void)persons.topic(*who, crc(index % 3 == 0 ? "Test_Topic_Ask" : "No_Such_Topic"), false);
            (void)interactions.client_cancel(who->owner, request.object);
        }
        Work work;
        work.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        work.lookups = interactions.counts().referenceLookups - countsBefore.referenceLookups;
        work.states = persons.counts().stateVisits - personsBefore.stateVisits;
        work.topics = persons.counts().topicVisits - personsBefore.topicVisits;
        work.selections = persons.counts().selections - personsBefore.selections;
        work.accepted = persons.counts().topics[0] - personsBefore.topics[0];
        // 100 ticks with no change of a bit: the person system looks at no character and no person.
        const std::uint64_t checks = persons.counts().presenceChecks;
        shop.host.steps(100, {&shop.clientA, &shop.clientB});
        work.tickChecks = persons.counts().presenceChecks - checks;
        return work;
    };
    const Work small = run(2000);
    const Work large = run(200000);
    const auto text = [](const Work& work) {
        return std::to_string(work.seconds) + " s, lookups " + std::to_string(work.lookups) + ", state visits " + std::to_string(work.states)
               + ", topic visits " + std::to_string(work.topics) + ", selections " + std::to_string(work.selections) + ", accepted topics "
               + std::to_string(work.accepted);
    };
    note("NC7 2,000 slots: " + text(small));
    note("NC7 200,000 slots: " + text(large));
    check(small.lookups == 20000 && large.lookups == 20000 && small.states == large.states && small.topics == large.topics
              && small.selections == large.selections && small.accepted == large.accepted && small.accepted > 0,
          "the same work for 2,000 and 200,000 authored slots: one reference lookup for each request, the same state and topic visits");
    check(small.selections > 0 && large.states <= large.selections * 3 && large.topics <= large.selections * 3,
          "a selection visits the states and the topics of one NPC row only (3 at most here)");
    check(small.tickChecks == 0 && large.tickChecks == 0, "100 ticks with no change of a bit: no presence check (no scan of the persons)");
    check(small.seconds < 1.0 && large.seconds < 1.0, "20,000 requests in less than 1 s for each size (the project limit of the research)");
}

/** The conversation data of NC8: one NPC row for each rule, with the states Base (no requirement) and Rule (the rule). */
json rule_document() {
    json npcs = json::array();
    json states = json::array();
    const auto rule = [&](const std::string& name, json cells, json topics = json::array(), json services = json::array()) {
        npcs.push_back({{"id", "Test_Rule_" + name}, {"states", json::array({"Base_" + name, "Rule_" + name})}});
        states.push_back({{"id", "Base_" + name}, {"type", "Conversation"}});
        states.push_back({{"id", "Rule_" + name}, {"type", "Conversation"}, {"requires", cells}, {"topics", topics}, {"services", services}});
    };
    rule("Any", {{"RequiredFaction", "Any"}}, json::array({"Topic_Any", "Topic_None", "Topic_Cooldown", "Topic_Root"}));
    rule("Named", {{"RequiredFaction", "Syndicate"}});
    rule("Not", {{"RequiredFaction", "!Syndicate"}});
    rule("Unknown", {{"RequiredFaction", "Maruaders"}});
    rule("Days", {{"RequiredFactionCooldown", "InCooldownMultipleDays"}});
    rule("Day", {{"RequiredFactionCooldown", "InCooldownSingleDay"}});
    rule("NoCooldown", {{"RequiredFactionCooldown", "NotInCooldown"}});
    rule("ObjectiveOnly", {{"RequiredActiveObjectiveId", "Test_No_Quest"}});
    rule("ObjectiveList", {{"RequiredActiveObjectiveId", "Test_No_Quest, Test_TopicQuest"}, {"RequiredActiveTaskId", "Test_Topic_Talk"}});
    rule("Completed", {{"RequiredCompletedObjective", "Test_DoneQuest"}});
    rule("NoAchievement", {{"RequiredCompletedObjective", "Test_TopicQuest"}});
    rule("Rank", {{"RequiredProgression", "Test_Craft 2 0"}});
    rule("Points", {{"RequiredProgression", "Test_Craft 1 500"}});
    rule("NoRule", {{"RequiredProgression", "Test_Craft 0 0"}});
    rule("Services", json::object(), json::array(),
         json::array({"Faction_Board", "Faction_Selection", "Invalid", "Barbershop", "Wolf_Store"}));
    rule("ForeignServices", {{"RequiredFaction", "Syndicate"}, {"RequiredLevel", "99"}}, json::array(),
         json::array({"Faction_Board", "Faction_Selection"}));
    rule("AnyServices", {{"RequiredFaction", "Any"}}, json::array(), json::array({"Faction_Board", "Faction_Selection"}));
    json topics = json::array({{{"id", "Topic_Any"}, {"requires", {{"RequiredFaction", "Any"}}}},
                               {{"id", "Topic_None"}, {"requires", {{"RequiredFaction", "None"}}}},
                               {{"id", "Topic_Cooldown"}, {"requires", {{"RequiredFactionCooldown", "InCooldownSingleDay"}}}},
                               {{"id", "Topic_Root"}, {"providedTopics", "Topic_Next, Topic_Gated,Topic_No_Row"}},
                               {{"id", "Topic_Next"}, {"providedTopics", "Topic_Root,Topic_Deep"}},
                               {{"id", "Topic_Deep"}},
                               {{"id", "Topic_Gated"}, {"requires", {{"RequiredAchievement", "Test_Never"}}}, {"providedTopics", "Topic_Hidden"}},
                               {{"id", "Topic_Hidden"}}});
    return {{"format", 1}, {"npcs", npcs}, {"states", states}, {"topics", topics}};
}

/** NC8: the rule of each requirement of the static read of the client (docs/systems/persons.md, the section of the requirements). */
void requirement_rules(const Options&) {
    PersonShop shop;
    json quests = person_quest_document();
    quests["tasks"]["test_done_wait"] = questfix::task("Test_Done_Wait", "taskachievement", 18, {{"ItemName", "Test_Never"}});
    quests["objectives"].push_back({{"id", "Test_DoneQuest"},
                                    {"cells", {{"Type", "Journey"}, {"AchievementId", "Test_Done_Achievement"}}},
                                    {"tree", json::array({json::array({0, "test_done_wait", json::array()})})}});
    quests["achievements"].push_back(json::array({"Test_Done_Achievement", 64}));
    check(shop.host.quests.load_text(quests.dump()) && shop.host.conversations.load_text(rule_document().dump()),
          "setup: the rule data and an objective with a completion achievement");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    const gamedata::ConversationData& data = shop.host.conversations;
    const auto rule_of = [&](const std::string& name) {
        const world::PersonSelection selection = shop.persons().select(*a, binding("Test_Rule_" + name));
        return selection.state >= 0 ? data.state(static_cast<std::uint32_t>(selection.state)).id.substr(0, 4) : std::string("none");
    };
    const auto passes = [&](const std::string& name) { return rule_of(name) == "Rule"; };
    note("faction: Any " + rule_of("Any") + ", Syndicate " + rule_of("Named") + ", !Syndicate " + rule_of("Not") + ", Maruaders "
         + rule_of("Unknown"));
    check(passes("Any") && !passes("Named") && passes("Not") && passes("Unknown"),
          "RequiredFaction of a state for a character with no faction: Any, a name with !, and a name that the client does not know pass "
          "(the value 0 or 4); a named faction does not pass");
    check(!passes("Days") && !passes("Day") && passes("NoCooldown"),
          "RequiredFactionCooldown of a state: NotInCooldown passes; the two cooldown values do not pass (no faction change: no cooldown)");
    check(passes("ObjectiveOnly") && !passes("ObjectiveList"),
          "an objective with no task is no rule; an objective list with a task needs the task active in one objective of the list");
    check(shop.host.world.quests().grant(*a, "Test_TopicQuest", "command", 0).ok(), "setup: A gets the quest of the task");
    check(passes("ObjectiveList"), "the second objective of the list has the task active: the state passes");
    check(!passes("Completed") && !passes("NoAchievement"),
          "RequiredCompletedObjective: no completion achievement yet, and an objective with no AchievementId, do not pass");
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Done_Achievement", 64) && passes("Completed"),
          "RequiredCompletedObjective passes with the completion achievement of the objective (no active instance needed)");
    check(!passes("Rank") && !passes("Points") && passes("NoRule"),
          "RequiredProgression: no rank does not pass; a rule with 0 and 0 is no rule");
    a->skills.progressions[gamedata::progression_id("Test_Craft")] = world::ProgressionRank{2, 100};
    check(passes("Rank") && !passes("Points"), "rank 2 passes the rank rule; rank 2 with 100 points does not pass a rule of 500 points");
    a->skills.progressions[gamedata::progression_id("Test_Craft")] = world::ProgressionRank{2, 500};
    check(passes("Points"), "a higher rank with the points of the rule passes (the rank and the points)");
    // The topics: Any needs a faction for a topic; the cooldown is no rule of a topic; the ProvidedTopics graph, each topic one time.
    const world::PersonSelection any = shop.persons().select(*a, binding("Test_Rule_Any"));
    std::vector<std::string> offered;
    for (const std::uint32_t topic : any.topics) {
        offered.push_back(data.topic(topic).id);
    }
    std::string text;
    for (const std::string& id : offered) {
        text += (text.empty() ? "" : ", ") + id;
    }
    note("topics of Rule_Any: " + text);
    check(offered == std::vector<std::string>{"Topic_None", "Topic_Cooldown", "Topic_Root", "Topic_Next", "Topic_Deep"},
          "the topics: Any is not offered (no faction), None and a cooldown are; ProvidedTopics adds the passing next topics, not a gated "
          "one or its next topic, each topic one time");
    // The services: the faction board for the faction of the state and its level, the faction selection also for Any; Invalid is no
    // service and no unknown name.
    const world::PersonSelection services = shop.persons().select(*a, binding("Test_Rule_Services"));
    const world::PersonSelection foreign = shop.persons().select(*a, binding("Test_Rule_ForeignServices"));
    const world::PersonSelection anyServices = shop.persons().select(*a, binding("Test_Rule_AnyServices"));
    check(services.services == std::vector<std::uint32_t>{2, 4, 15, 8} && services.unknownServices.empty(),
          "a state with no faction: Faction_Board 2, Faction_Selection 4, Barbershop 15, Wolf_Store 8; Invalid gives nothing");
    check(foreign.state < 0 || data.state(static_cast<std::uint32_t>(foreign.state)).id == "Base_ForeignServices",
          "control: a state of a named faction and level 99 does not pass");
    check(anyServices.services == std::vector<std::uint32_t>{4}, "a state of Any: the faction selection only (the board needs the faction)");
}

} // namespace

std::vector<Case> person_cases() {
    return {{"NC1", "persons (2.5c, gate R): the client id of a person child and its export (check_interactions_tool.py --persons)", client_id_rule},
            {"NC2", "persons (2.5c, I1): the loaders of format 4 and of the conversation data, no data, the conversation tool", loaders},
            {"NC3", "persons (2.5c, I2, MP-10): two sessions at one person: the close of A ends only A", two_sessions},
            {"NC4", "persons (2.5c, I3): the state of each character, the topics 3158 and 5397, stale, completed, duplicate", topics},
            {"NC5", "persons (2.5c, I4): the services in the grant API: close, new source, range, presence, death, travel, entry", services},
            {"NC6", "persons (2.5c, I5): requests that are not correct change nothing for both characters; no alias", refusals},
            {"NC7", "persons (2.5c, I6): 2,000 and 200,000 authored person slots, 20,000 requests; no tick scan", load},
            {"NC8", "persons (2.5c, I3): the rule of each requirement (faction, cooldown, objectives, progression, ProvidedTopics, services)",
             requirement_rules}};
}

} // namespace soulwarden::backend::tests
