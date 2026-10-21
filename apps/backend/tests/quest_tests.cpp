// Cases of stage 1.8a: the saved quests, the achievements of the quest tasks, the start grant of a new character, and the HUD pin
// (world/quests.h, docs/systems/quests.md). The quest data has the form of the tool (quest-data.json, format 1): the tree of 06_getoffbeach
// of the game data (its 17 tasks with the index rule of the client) and made-up quests (Test_*). All accounts, characters, and names are made
// up.
//
//   QG1   two sessions (MP-10, A3a): the quest state, the readiness state, and the quest bits of one character never reach the other session
//   QG2   the load: 50 characters with 20 waiting quests each; an achievement event visits only the waits of its character; the work counts
//   QG3   a new character (A1a): the rows of the create route, the beach position, the grant at the first entry (tasks 0 to 6 done, 7 to 9
//         active), the archetype bit, one time only; the wire after 2709: slot 73, then slot 22 with the task elements in index order
//   QG4   the store (A2a): a reconnect and a restart keep the instance, the pin, the task states, and the timer start, with no new grant
//   QG5   the lock of the last bit (A4a): an empty list fragment of the achievement state; the row goes away
//   QG6   no quest data (A5a): no quest, no quest state, no start rows, and the backend works
//   QG7   the evaluators: TaskAchievement with a non-persisted achievement, the change event, the partial container and its siblings
//   QG8   TaskTimer on the world clock: 25.4 s (a fraction), the start in field 0, the completion and the next action
//   QG9   decision D2 option C: a backstory template (in the world and as rows), no level and no item change, the refusals
//   QG10  the gate of 2709 and the pins of 1458: no quest state before the first 2709; the list of the client is stored, a foreign one not
//   QG11  the loader refuses a missing task link, a CRC conflict, an index above 255, a child that is not below its container
//   QG-TOOL the check of the data tool (runs check_quests_tool.py)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "server/rep/quest_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/fake_client.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "world/quests.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;

constexpr const char* kQuestA = "0e180000-0000-4000-8000-0000000000a1";
constexpr const char* kQuestB = "0e180000-0000-4000-8000-0000000000b1";
constexpr const char* kQuestNew = "0e180000-0000-4000-8000-0000000000c1";
constexpr std::uint32_t kObjectivesSlot = 22;
constexpr std::uint32_t kObjectivesType = 3857;
constexpr std::uint32_t kSeasonsSlot = 73;
constexpr std::uint32_t kSeasonsType = 5606;
constexpr std::uint32_t kAchievementSlot = 66;
constexpr std::uint32_t kAchievementType = 3210;
/** The bits of the made-up achievements of the cases (the beach ids have their bits of the game data). */
constexpr std::uint32_t kStartIgcBit = 15276;

json task(const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
    return {{"id", id}, {"crc", 0}, {"type", type}, {"code", code}, {"cells", cells}};
}

/** @return The quest data of the cases: the beach tree of the game data and the made-up quests. */
json quest_document() {
    json tasks = json::object();
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = task(id, type, code, cells);
    };
    const json hidden = {{"IsHidden", "1"}};
    // The tree of 06_getoffbeach (game data: c01 objective tasks; research quest-engine.md Q7).
    add("06_getoffbeach_01_01_SliceSwap_01_HoweWW_A_On", "taskunlockachievement", 20, {{"ItemName", "SwapActive_0425_HerbalistPath_ww"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_02_SliceSwap_01_CaveCorruption_On", "tasklockachievement", 19,
        {{"ItemName", "SwapActive_01_CaveCorruption"}, {"TargetQty", "1"}, {"StayActive", "1"}});
    add("06_getoffbeach_01_04_SliceSwap_01_UrdaWW_p01_On", "taskunlockachievement", 20, {{"ItemName", "SwapActive_01_UrdaWW_01"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_05_HideCorinthGrace", "taskunlockachievement", 20, {{"ItemName", "SwapActive_06_GraceWW_A"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_initialize_container", "simpletaskcontainer", 0, hidden);
    add("06_getoffbeach_02_UnlockAchievement_GraceCombat_On", "taskunlockachievement", 20, {{"ItemName", "SwapActive_01_GraceCombatLoop"}, {"TargetQty", "1"}});
    add("06_getoffbeach_03_UnlockAchievement_GraceCombat_VO_On", "taskunlockachievement", 20,
        {{"ItemName", "SwapActive_01_GraceCombatLoop_VO_WW"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_Watched_OtherBeachIntro", "taskachievement", 18, {{"ItemName", "Watched_IGC_Aelstrom_MB"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_Watched_IntroWatched", "taskachievement", 18, {{"ItemName", "Watched_IGC_Aelstrom_WW"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_Aelstrom_Beach_Start_Trigger", "tasktriggerarea", 3,
        {{"TargetQty", "1"}, {"TriggerAreaType", "Enter"}, {"TP_TargetTag", "Aelstrom_IGC_Trigger"}, {"TP_DescriptionTag", "@01_GetOffBeach_task"}});
    add("06_getoffbeach_01_Aelstrom_Beach_Unlock", "taskunlockachievement", 20, {{"ItemName", "Start_IGC_Aelstrom_WW"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_Aelstrom_Beach_cinematic_timer", "taskcutscenecomplete", 49,
        {{"ItemName", "06_GetOffBeach_MSQ_RevivalShoreKnight_WW_IGC"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_Aelstrom_Beach_Watched", "taskunlockachievement", 20, {{"ItemName", "Watched_IGC_Aelstrom_WW"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_03_SliceSwap_01_AelstromWW_p01_On", "tasklockachievement", 19, {{"ItemName", "SwapActive_01_AelstromWW_01"}, {"TargetQty", "1"}});
    add("06_getoffbeach_01_Container_IGC_Aelstrom_Beach", "consecutivetaskcontainer", 1, hidden);
    add("06_getoffbeach_01_Aelstrom_Beach_OtherBeachCheck", "partialtaskcontainer", 2, hidden);
    add("06_getoffbeach_container", "consecutivetaskcontainer", 1, hidden);
    // Made-up quests: a timer with a fraction, a wait for a non-persisted achievement, the lock of a last bit.
    add("Test_Timer_Unlock", "taskunlockachievement", 20, {{"ItemName", "Test_Before"}});
    add("Test_Timer_Wait", "tasktimer", 64, {{"Duration", "25.4"}, {"FailOnCondition", "0"}, {"TP_DescriptionTag", "@test_wait"}});
    add("Test_Timer_After", "taskunlockachievement", 20, {{"ItemName", "Test_After"}});
    add("Test_Timer_Root", "consecutivetaskcontainer", 1);
    add("Test_Wait_Flag", "taskachievement", 18, {{"ItemName", "Test_Npa"}});
    add("Test_Wait_Done", "taskunlockachievement", 20, {{"ItemName", "Test_Done"}});
    add("Test_Wait_Root", "consecutivetaskcontainer", 1);
    add("Test_Lock_Last", "tasklockachievement", 19, {{"ItemName", "Test_Last"}});
    add("Test_Lock_Root", "simpletaskcontainer", 0);
    json objectives = json::array();
    objectives.push_back({{"id", "06_getoffbeach"},
                          {"cells",
                           {{"Type", "MainStoryQuest"}, {"ExclusiveTerritory", "06"}, {"AchievementId", "S_getoffbeach_ww"},
                            {"NpcDestinationId", "01_AelstromWW_01"}, {"CanBeAbandoned", "0"}, {"Title", "@01_GetOffBeach_title"}}},
                          {"tree",
                           json::array({json::array({0, "06_getoffbeach_01_01_sliceswap_01_howeww_a_on", json::array()}),
                                        json::array({1, "06_getoffbeach_01_02_sliceswap_01_cavecorruption_on", json::array()}),
                                        json::array({2, "06_getoffbeach_01_04_sliceswap_01_urdaww_p01_on", json::array()}),
                                        json::array({3, "06_getoffbeach_01_05_hidecorinthgrace", json::array()}),
                                        json::array({4, "06_getoffbeach_01_initialize_container", json::array({0, 1, 2, 3})}),
                                        json::array({5, "06_getoffbeach_02_unlockachievement_gracecombat_on", json::array()}),
                                        json::array({6, "06_getoffbeach_03_unlockachievement_gracecombat_vo_on", json::array()}),
                                        json::array({7, "06_getoffbeach_01_watched_otherbeachintro", json::array()}),
                                        json::array({8, "06_getoffbeach_01_watched_introwatched", json::array()}),
                                        json::array({9, "06_getoffbeach_01_aelstrom_beach_start_trigger", json::array()}),
                                        json::array({10, "06_getoffbeach_01_aelstrom_beach_unlock", json::array()}),
                                        json::array({11, "06_getoffbeach_01_aelstrom_beach_cinematic_timer", json::array()}),
                                        json::array({12, "06_getoffbeach_01_aelstrom_beach_watched", json::array()}),
                                        json::array({13, "06_getoffbeach_01_03_sliceswap_01_aelstromww_p01_on", json::array()}),
                                        json::array({14, "06_getoffbeach_01_container_igc_aelstrom_beach", json::array({9, 10, 11, 12, 13})}),
                                        json::array({15, "06_getoffbeach_01_aelstrom_beach_otherbeachcheck", json::array({7, 8, 14})}),
                                        json::array({16, "06_getoffbeach_container", json::array({4, 5, 6, 15})})})}});
    objectives.push_back({{"id", "Test_TimerQuest"},
                          {"cells", {{"Type", "Journey"}}},
                          {"tree", json::array({json::array({0, "test_timer_unlock", json::array()}), json::array({1, "test_timer_wait", json::array()}),
                                                json::array({2, "test_timer_after", json::array()}),
                                                json::array({3, "test_timer_root", json::array({0, 1, 2})})})}});
    objectives.push_back({{"id", "Test_WaitQuest"},
                          {"cells", {{"Type", "Journey"}}},
                          {"tree", json::array({json::array({0, "test_wait_flag", json::array()}), json::array({1, "test_wait_done", json::array()}),
                                                json::array({2, "test_wait_root", json::array({0, 1})})})}});
    objectives.push_back({{"id", "Test_LockQuest"},
                          {"cells", {{"Type", "Journey"}}},
                          {"tree", json::array({json::array({0, "test_lock_last", json::array()}), json::array({1, "test_lock_root", json::array({0})})})}});
    json achievements = json::array();
    std::uint32_t made = 20;
    for (const char* id : {"Test_Before", "Test_After", "Test_Flag", "Test_Block", "Test_Done", "Test_Last", "Test_Story_1", "Test_Story_2",
                           "Archetype_Mercenary", "Test_Other"}) {
        achievements.push_back(json::array({id, made++}));
    }
    // The beach ids with their bits of the game data (javelindata_achievements).
    for (const auto& [id, bit] : std::vector<std::pair<const char*, std::uint32_t>>{{"SwapActive_0425_HerbalistPath_ww", 14470},
                                                                                    {"SwapActive_01_CaveCorruption", 14486},
                                                                                    {"SwapActive_01_UrdaWW_01", 14495},
                                                                                    {"SwapActive_06_GraceWW_A", 14552},
                                                                                    {"SwapActive_01_GraceCombatLoop", 14488},
                                                                                    {"SwapActive_01_GraceCombatLoop_VO_WW", 14489},
                                                                                    {"Watched_IGC_Aelstrom_MB", 15279},
                                                                                    {"Watched_IGC_Aelstrom_WW", 15277},
                                                                                    {"Start_IGC_Aelstrom_WW", kStartIgcBit},
                                                                                    {"SwapActive_01_AelstromWW_01", 14492}}) {
        achievements.push_back(json::array({id, bit}));
    }
    return {{"format", 1},
            {"objectives", objectives},
            {"tasks", tasks},
            {"achievements", achievements},
            {"npa", json::array({json::array({"Test_Npa", "Test_Flag && !Test_Block", "", ""})})},
            {"territories", json::array({{{"territory", 6}, {"objective", "06_getoffbeach"}, {"table", "t"}}})},
            {"backstories",
             json::array({{{"id", "Archetype_Soldier"}, {"objectives", json::array()}, {"achievements", json::array({"Archetype_Mercenary"})},
                           {"archetype", true}},
                          {{"id", "Level60_Test"},
                           {"level", "60"},
                           {"objectives", json::array({"Test_WaitQuest", "Missing_Objective"})},
                           {"objectiveText", "Test_WaitQuest : a note,Missing_Objective"},
                           {"achievements", json::array({"Test_Story_1", "Test_Story_2"})},
                           {"archetype", false}}})}};
}

void load_quests(ServerHost& host) {
    if (!host.quests.load_text(quest_document().dump())) {
        throw std::runtime_error("quest test: the quest data did not load: " + host.quests.error());
    }
}

/** @return The body of 2709 for an entity: 36 flags of 1, with 0 for the type CommunityGoal (6). */
Bytes auto_pin_body(std::uint64_t entity) {
    server::hub::Writer body;
    body.u64(0);
    body.u64(entity);
    body.size(36);
    for (int index = 0; index < 36; ++index) {
        body.u8(index == 6 ? 0 : 1);
    }
    return body.data();
}

Bytes tracked_body(std::uint64_t entity, const std::vector<std::uint64_t>& instances) {
    server::hub::Writer body;
    body.u64(0);
    body.u64(entity);
    body.size(static_cast<std::uint32_t>(instances.size()));
    for (const std::uint64_t instance : instances) {
        body.u64(instance);
    }
    return body.data();
}

void send_2709(ServerHost& host, FakeClient& client, const std::string& characterId) {
    const world::Entity* entity = host.world.find_character(characterId);
    host.datagram(client.peer, client.component_message(server::rep::kTypeObjectiveAutoPinning, entity->actor.bytes, auto_pin_body(entity->id)));
}

/** One decoded Objectives state. */
struct ObjectivesState {
    std::vector<std::uint64_t> tracked;
    /** objectiveCrc, instance */
    std::vector<std::pair<std::uint32_t, std::uint64_t>> active;
    struct Task {
        std::uint64_t instance;
        std::uint8_t index;
        std::uint32_t crc;
        std::uint32_t progress;
        std::uint8_t state;
    };
    std::vector<Task> tasks;
    /** instance, index, start */
    std::vector<std::tuple<std::uint64_t, std::uint8_t, std::uint64_t>> timers;
    std::vector<std::uint16_t> territories;
    std::vector<std::uint64_t> acceptTimes;
};

ObjectivesState decode_objectives(const Fragment& fragment) {
    ObjectivesState state;
    const auto open = [&](unsigned field, server::hub::Reader& reader) -> std::uint32_t {
        reader.size();
        if (reader.u8() == 1) {
            reader.size64();
        }
        return reader.size();
    };
    if (fragment.fields.contains({1, 0})) {
        server::hub::Reader reader(fragment.fields.at({1, 0}));
        for (std::uint32_t count = open(0, reader), index = 0; index < count; ++index) {
            const std::uint64_t instance = reader.u64();
            const std::uint8_t task = reader.u8();
            state.timers.emplace_back(instance, task, reader.u64());
        }
    }
    if (fragment.fields.contains({1, 1})) {
        server::hub::Reader reader(fragment.fields.at({1, 1}));
        for (std::uint32_t count = open(1, reader), index = 0; index < count; ++index) {
            state.tracked.push_back(reader.u64());
        }
    }
    if (fragment.fields.contains({1, 3})) {
        server::hub::Reader reader(fragment.fields.at({1, 3}));
        for (std::uint32_t count = open(3, reader), index = 0; index < count; ++index) {
            const std::uint32_t crc = reader.u32();
            const std::uint64_t instance = reader.u64();
            reader.u32();
            reader.raw(16);
            state.acceptTimes.push_back(reader.u64());
            state.territories.push_back(reader.u16());
            reader.raw(4);
            reader.u64();
            reader.u8();
            reader.size();
            state.active.emplace_back(crc, instance);
        }
    }
    if (fragment.fields.contains({1, 4})) {
        server::hub::Reader reader(fragment.fields.at({1, 4}));
        for (std::uint32_t count = open(4, reader), index = 0; index < count; ++index) {
            ObjectivesState::Task task;
            task.instance = reader.u64();
            task.index = reader.u8();
            task.crc = reader.u32();
            task.progress = reader.u32();
            task.state = reader.u8();
            state.tasks.push_back(task);
        }
    }
    return state;
}

/** @return The fragments of a slot and type of the own character (interest 1) that a client got from the message `from` on. */
std::vector<Fragment> own_fragments(const FakeClient& client, std::uint32_t slot, std::uint32_t type, std::size_t from = 0) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest != 1) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot && fragment.type == type) {
                    found.push_back(fragment);
                }
            }
        }
    }
    return found;
}

/** @return The number of fragments of a slot and type that a client got for an entity that is not its own (an observer view). */
std::size_t foreign_fragments(const FakeClient& client, std::uint32_t slot, std::uint32_t type) {
    std::size_t count = 0;
    for (const auto& sent : client.got) {
        if (sent.type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(sent.body).records) {
            if (record.interest == 1) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                count += fragment.slot == slot && fragment.type == type ? 1 : 0;
            }
        }
    }
    return count;
}

/** @return The bit indexes of an achievement fragment. */
std::vector<std::uint32_t> bits_of(const Fragment& fragment) {
    server::hub::Reader reader(fragment.fields.at({0, 0}));
    reader.size();
    if (reader.u8() == 1) {
        reader.size64();
    }
    const std::uint32_t count = reader.size();
    std::vector<std::uint32_t> bits;
    for (std::uint32_t position = 0; position < count; ++position) {
        const std::uint8_t byte = reader.u8();
        for (unsigned bit = 0; bit < 8; ++bit) {
            if ((byte & (1U << bit)) != 0) {
                bits.push_back(position * 8 + bit);
            }
        }
    }
    return bits;
}

/** A new character of the create route (the quest rows of Quests::new_character_rows) with its own account. */
Player add_new_character(ServerHost& host, const char* characterId, const char* name, const char* steamId) {
    Player player;
    player.steamId = steamId;
    player.personaName = "Tester";
    player.characterId = characterId;
    player.characterName = name;
    state::AccountOrigin origin{};
    player.account = *host.store.login_account(steamId, "Tester", false, origin);
    state::Character character;
    character.characterId = characterId;
    character.personaId = player.account.personaId;
    character.worldId = kWorldId;
    character.name = name;
    character.createdDate = "2026-01-01T00:00:00.000Z";
    character.modifiedDate = character.createdDate;
    character.archetype = "Archetype_Soldier";
    std::optional<state::WorldPosition> start;
    const state::NewCharacterRows rows = world::Quests::new_character_rows(host.world.quests().data(), character.archetype, start);
    character.position = start;
    if (host.store.insert_character(character, {}, rows) != state::InsertResult::inserted) {
        throw std::runtime_error("quest test: the new character was not inserted");
    }
    return player;
}

const world::QuestInstance* quest_of(const world::Entity& entity, const char* objective) {
    for (const world::QuestInstance& quest : entity.quests.active) {
        if (gamedata::lower_text(quest.objective) == gamedata::lower_text(objective)) {
            return &quest;
        }
    }
    return nullptr;
}

/** @return The states of the tasks of a quest: "index:state/progress" for each task, in index order. */
std::string states_text(const world::QuestInstance& quest) {
    std::string text;
    for (std::size_t index = 0; index < quest.tasks.size(); ++index) {
        text += (text.empty() ? "" : " ") + std::to_string(index) + ":" + std::to_string(quest.tasks[index].state) + "/"
                + std::to_string(quest.tasks[index].progress);
    }
    return text;
}

bool has_bit(const world::Entity& entity, std::uint32_t bit) {
    return std::binary_search(entity.achievements.begin(), entity.achievements.end(), bit);
}

bool has_row(state::Store& store, const char* characterId, const char* id) {
    const std::vector<std::string> rows = store.list_achievements(characterId);
    return std::find(rows.begin(), rows.end(), id) != rows.end();
}

/** The expected beach states (research quest-display-18a.md Q3, the corrected candidate; live check L1). */
constexpr const char* kBeachStates = "0:2/1 1:3/1 2:2/1 3:2/1 4:3/4 5:2/1 6:2/1 7:1/0 8:1/0 9:1/0 10:0/0 11:0/0 12:0/0 13:0/0 14:1/0 15:1/0 16:1/3";

/** QG3: a new character: the start grant, the archetype bit, the beach, the wire after 2709. */
void new_character(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_new_character(host, kQuestNew, "Questnew", kSteamIdA);
    const auto row = host.store.find_character(kQuestNew);
    check(row && row->position && row->position->x == world::kBeachStart.x && row->position->y == world::kBeachStart.y
              && row->position->z == world::kBeachStart.z,
          "the create rows: the start position on the Windsward beach (9622.62 2306.48 40.24)");
    const std::vector<state::QuestGrantRow> pending = host.store.list_quest_grants(kQuestNew);
    check(pending.size() == 1 && pending[0].source == "territory" && pending[0].key == "6" && pending[0].objective == "06_getoffbeach"
              && pending[0].state == "pending" && host.store.list_quests(kQuestNew).empty(),
          "the create rows: one pending grant of the start objective of territory 6 (TerritoryDefinition.ObjectiveID), no quest yet");
    check(has_row(host.store, kQuestNew, "Archetype_Mercenary"), "the create rows: the achievement of the archetype (AchievementUnlockOverride)");
    FakeClient client(27000);
    host.enter(client, player.registration());
    host.steps(10, {&client});
    const world::Entity* entity = host.world.find_character(kQuestNew);
    const world::QuestInstance* quest = entity != nullptr ? quest_of(*entity, "06_getoffbeach") : nullptr;
    check(quest != nullptr && states_text(*quest) == kBeachStates,
          std::string("the start quest: tasks 0 to 6 done (CompleteActive 3 with an effective StayActive), 7 to 9, 14, 15, 16 active, 10 to 13 "
                      "new; the counts of the containers (4: 4, 16: 3): ") + (quest != nullptr ? states_text(*quest) : "no quest"));
    check(quest != nullptr && entity->quests.tracked == std::vector<std::uint64_t>{quest->instance} && quest->originTerritory == 6,
          "the start quest is pinned (the default auto-pin of the client), with the territory 6");
    check(entity != nullptr && has_bit(*entity, 14470) && has_bit(*entity, 14495) && has_bit(*entity, 14552) && has_bit(*entity, 14488)
              && has_bit(*entity, 14489) && !has_bit(*entity, 14486) && has_bit(*entity, 28),
          "the bits: the unlocks of tasks 0, 2, 3, 5, 6, no bit of the lock of task 1, and the archetype bit");
    check(has_row(host.store, kQuestNew, "SwapActive_01_GraceCombatLoop") && host.store.list_quest_undo(kQuestNew).size() == 5,
          "the rows: the achievements of the actions, and an undo row for each action with no StayActive (5)");
    const std::vector<state::QuestGrantRow> granted = host.store.list_quest_grants(kQuestNew);
    check(granted.size() == 1 && granted[0].state == "granted" && quest != nullptr && granted[0].instance == quest->instance,
          "the grant row is granted, with the instance");
    // The wire: no quest state before the first 2709; then slot 73 and slot 22 in one record, the tasks in index order.
    check(own_fragments(client, kObjectivesSlot, kObjectivesType).empty() && own_fragments(client, kSeasonsSlot, kSeasonsType).empty(),
          "before the first 2709: no Objectives state and no seasons readiness state");
    const std::size_t from = client.got.size();
    send_2709(host, client, kQuestNew);
    host.steps(5, {&client});
    const std::vector<Fragment> seasons = own_fragments(client, kSeasonsSlot, kSeasonsType, from);
    const std::vector<Fragment> objectives = own_fragments(client, kObjectivesSlot, kObjectivesType, from);
    check(seasons.size() == 1 && seasons[0].fields.at({0, 0}) == Bytes{1}, "after 2709: the seasons readiness state, initialized 1");
    check(objectives.size() == 1, "after 2709: one Objectives state");
    if (objectives.size() == 1 && quest != nullptr) {
        const ObjectivesState state = decode_objectives(objectives[0]);
        bool ordered = state.tasks.size() == 17;
        for (std::size_t index = 0; ordered && index < state.tasks.size(); ++index) {
            ordered = state.tasks[index].index == index && state.tasks[index].instance == quest->instance;
        }
        check(ordered, "the task elements of the quest are in index order (live check L1)");
        check(state.active.size() == 1 && state.active[0].first == gamedata::quest_crc("06_getoffbeach") && state.tracked.size() == 1
                  && state.tracked[0] == quest->instance && state.timers.empty() && state.acceptTimes[0] == 0 && state.territories[0] == 6,
              "the active element (objective E4D66020), the tracked list, no timer, acceptTime 0, territory 6");
        check(state.tasks.size() > 9 && state.tasks[9].crc == 0x1B73198BU && state.tasks[9].state == 1 && state.tasks[4].progress == 4
                  && state.tasks[4].state == 3 && state.tasks[16].progress == 3,
              "task 9 (CRC 1B73198B) active; container 4 CompleteActive with the count 4; the root count 3");
    }
    // One time only: a new entry gives no second grant.
    host.disconnect(client.peer);
    host.steps(5, {});
    FakeClient again(27001);
    host.enter(again, player.registration());
    host.steps(10, {&again});
    const world::Entity* second = host.world.find_character(kQuestNew);
    check(second != nullptr && second->quests.active.size() == 1 && host.store.list_quests(kQuestNew).size() == 1
              && log_count("got the objective 06_getoffbeach") == 1,
          "a second entry: the same quest, no second grant");
    // A5a, part: with no quest data the create rows are empty and the position stays.
    std::optional<state::WorldPosition> none;
    const state::NewCharacterRows empty = world::Quests::new_character_rows(nullptr, "Archetype_Soldier", none);
    check(empty.grants.empty() && empty.achievements.empty() && !none, "no quest data: no start rows and no start position");
}

/** QG1: two sessions (MP-10, A3a). */
void two_sessions(const Options&) {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    ServerHost host(options);
    load_quests(host);
    const Player a = add_player(host.store, kSteamIdA, "Tester A", kQuestA, "Questa", state::WorldPosition{100.0F, 200.0F, 30.0F});
    const Player b = add_player(host.store, kSteamIdB, "Tester B", kQuestB, "Questb", state::WorldPosition{102.0F, 200.0F, 30.0F});
    FakeClient clientA(27000);
    FakeClient clientB(27001);
    host.enter(clientA, a.registration());
    host.enter(clientB, b.registration());
    host.datagram(clientA.peer, clientA.input_at(1, 100.0F, 200.0F, 30.0F));
    host.datagram(clientB.peer, clientB.input_at(1, 102.0F, 200.0F, 30.0F));
    send_2709(host, clientA, kQuestA);
    send_2709(host, clientB, kQuestB);
    host.steps(20, {&clientA, &clientB});
    world::Entity* entityA = host.world.find_character(kQuestA);
    world::Entity* entityB = host.world.find_character(kQuestB);
    const world::QuestResult grantA = host.world.quests().grant(*entityA, "06_getoffbeach", "command", 0);
    const world::QuestResult grantB = host.world.quests().grant(*entityB, "Test_WaitQuest", "command", 0);
    host.steps(20, {&clientA, &clientB});
    check(grantA.ok() && grantB.ok() && host.session(1).observer().known().size() == 2 && host.session(2).observer().known().size() == 2,
          "A gets the beach quest, B a different quest; each session observes the other character");
    const std::vector<Fragment> stateA = own_fragments(clientA, kObjectivesSlot, kObjectivesType);
    const std::vector<Fragment> stateB = own_fragments(clientB, kObjectivesSlot, kObjectivesType);
    check(!stateA.empty() && !stateB.empty(), "each client gets its own Objectives state");
    bool separate = !stateA.empty() && !stateB.empty();
    if (separate) {
        const ObjectivesState lastA = decode_objectives(stateA.back());
        const ObjectivesState lastB = decode_objectives(stateB.back());
        separate = lastA.active.size() == 1 && lastA.active[0].second == grantA.instance && lastB.active.size() == 1
                   && lastB.active[0].second == grantB.instance
                   && std::none_of(lastB.tasks.begin(), lastB.tasks.end(), [&](const auto& task) { return task.instance == grantA.instance; })
                   && std::none_of(lastA.tasks.begin(), lastA.tasks.end(), [&](const auto& task) { return task.instance == grantB.instance; });
    }
    check(separate, "the Objectives state of each client has only its own quest and its own task elements");
    check(foreign_fragments(clientA, kObjectivesSlot, kObjectivesType) == 0 && foreign_fragments(clientB, kObjectivesSlot, kObjectivesType) == 0
              && foreign_fragments(clientA, kSeasonsSlot, kSeasonsType) == 0 && foreign_fragments(clientB, kSeasonsSlot, kSeasonsType) == 0
              && foreign_fragments(clientA, kAchievementSlot, kAchievementType) == 0
              && foreign_fragments(clientB, kAchievementSlot, kAchievementType) == 0,
          "no Objectives, readiness, or achievement state of the other character (owner only)");
    const std::vector<Fragment> bitsB = own_fragments(clientB, kAchievementSlot, kAchievementType);
    check(has_bit(*entityA, 14470) && !has_bit(*entityB, 14470)
              && std::none_of(bitsB.begin(), bitsB.end(), [](const Fragment& fragment) {
                     const std::vector<std::uint32_t> bits = bits_of(fragment);
                     return std::find(bits.begin(), bits.end(), 14470U) != bits.end();
                 }),
          "the bits of the beach actions are bits of A only: B and its client have none");
    // An achievement event of B does not visit the quests of A.
    const std::uint64_t visitedBefore = host.world.quests().counts().waitsVisited;
    check(host.world.ownership().unlock_achievement(*entityB, "Test_Other", 29), "B unlocks a different achievement");
    check(host.world.quests().counts().waitsVisited - visitedBefore == 1,
          "the event of B visits only the one waiting task of B (A has two waiting TaskAchievement tasks)");
}

/** QG2: the load: 50 characters with 20 waiting quests each. */
void load(const Options&) {
    ServerHost host;
    load_quests(host);
    constexpr int kCharacters = 50;
    constexpr int kQuests = 20;
    std::vector<std::string> ids;
    const gamedata::QuestObjective* wait = host.quests.objective("Test_WaitQuest");
    for (int character = 0; character < kCharacters; ++character) {
        char id[48];
        std::snprintf(id, sizeof(id), "0e180000-0000-4000-8000-%012d", 100 + character);
        char name[16];
        std::snprintf(name, sizeof(name), "Load%d", character);
        (void)add_player(host.store, std::to_string(76561202255300000ULL + character), "Tester", id, name,
                         state::WorldPosition{100.0F + character * 1000.0F, 200.0F, 30.0F});
        for (int quest = 0; quest < kQuests; ++quest) {
            state::QuestRow row;
            row.instance = static_cast<std::uint64_t>(character * kQuests + quest + 1);
            row.objective = wait->id;
            row.objectiveCrc = wait->crc;
            row.source = "command";
            row.tasks = {{0, "Test_Wait_Flag", gamedata::quest_crc("Test_Wait_Flag"), 0, 1, 0},
                         {1, "Test_Wait_Done", gamedata::quest_crc("Test_Wait_Done"), 0, 0, 0},
                         {2, "Test_Wait_Root", gamedata::quest_crc("Test_Wait_Root"), 0, 1, 0}};
            if (!host.store.write_quest(id, row)) {
                throw std::runtime_error("quest load test: a row");
            }
        }
        ids.push_back(id);
    }
    const auto start = std::chrono::steady_clock::now();
    for (const std::string& id : ids) {
        check(host.world.spawn_character(id, *host.store.find_character(id)->position) == world::SpawnError::none, "");
    }
    const auto entered = std::chrono::steady_clock::now();
    world::Entity* first = host.world.find_character(ids.front());
    check(first != nullptr && first->quests.active.size() == kQuests, "the first character has its 20 saved quests");
    const world::QuestCounts before = host.world.quests().counts();
    constexpr int kEvents = 2000;
    for (int event = 0; event < kEvents; ++event) {
        if (event % 2 == 0) {
            (void)host.world.ownership().unlock_achievement(*first, "Test_Block", 23);
        } else {
            (void)host.world.ownership().lock_achievement(*first, "Test_Block");
        }
    }
    const auto ended = std::chrono::steady_clock::now();
    const world::QuestCounts after = host.world.quests().counts();
    const std::uint64_t visits = after.waitsVisited - before.waitsVisited;
    const double enterSeconds = std::chrono::duration<double>(entered - start).count();
    const double eventSeconds = std::chrono::duration<double>(ended - entered).count();
    std::printf("    QG2: %d characters x %d quests entered in %.3f s; %d events: %llu visits (%llu events), %.3f s\n", kCharacters, kQuests,
                enterSeconds, kEvents, static_cast<unsigned long long>(visits),
                static_cast<unsigned long long>(after.events - before.events), eventSeconds);
    check(after.events - before.events == kEvents && visits == static_cast<std::uint64_t>(kEvents) * kQuests,
          "each event visits the 20 waiting tasks of its character only (not the 1,000 of the world)");
    check(enterSeconds < 5.0 && eventSeconds < 5.0, "the time limits: the entries and the events in less than 5 s each");
    check(std::all_of(first->quests.active.begin(), first->quests.active.end(),
                      [](const world::QuestInstance& quest) { return quest.tasks[0].state == world::quest_state::kActive; }),
          "the waits stay active (Test_Flag is not there)");
}

/** QG4: the store: a reconnect and a restart. */
void persistence(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::string database = (options.workDirectory / "quests-qg4.sqlite3").string();
    std::filesystem::remove(database);
    std::filesystem::remove(database + "-wal");
    std::filesystem::remove(database + "-shm");
    service::RepServerOptions serverOptions;
    serverOptions.serverVersion = "test-version";
    std::uint64_t beach = 0;
    std::uint64_t timer = 0;
    std::uint64_t timerStart = 0;
    {
        ServerHost host(serverOptions, database);
        load_quests(host);
        const Player player = add_new_character(host, kQuestNew, "Questnew", kSteamIdA);
        FakeClient client(27000);
        host.enter(client, player.registration());
        host.steps(5, {&client});
        world::Entity* entity = host.world.find_character(kQuestNew);
        const world::QuestResult granted = host.world.quests().grant(*entity, "Test_TimerQuest", "command", 0);
        host.steps(5, {&client});
        beach = quest_of(*entity, "06_getoffbeach")->instance;
        timer = granted.instance;
        timerStart = quest_of(*entity, "Test_TimerQuest")->tasks[1].startNs;
        // The client pins only the timer quest (1458): the rows get its list.
        check(host.world.quests().client_tracked(*entity, {timer}), "the pin list of the client: the timer quest only");
        host.disconnect(client.peer);
        host.steps(5, {});
        FakeClient again(27001);
        host.enter(again, player.registration());
        host.steps(5, {&again});
        const world::Entity* back = host.world.find_character(kQuestNew);
        check(back != nullptr && back->quests.active.size() == 2 && quest_of(*back, "06_getoffbeach")->instance == beach
                  && states_text(*quest_of(*back, "06_getoffbeach")) == kBeachStates && back->quests.tracked == std::vector<std::uint64_t>{timer}
                  && quest_of(*back, "Test_TimerQuest")->tasks[1].startNs == timerStart,
              "a reconnect: the same instances, the task states, the pin of the client, the timer start");
        host.disconnect(again.peer);
        host.steps(5, {});
    }
    // A restart: a new backend with the same database.
    ServerHost host(serverOptions, database);
    load_quests(host);
    const auto account = host.store.find_character(kQuestNew);
    Player player;
    player.characterId = kQuestNew;
    player.account = *host.store.find_account(account->personaId);
    player.steamId = kSteamIdA;
    player.personaName = "Tester";
    FakeClient client(27002);
    host.enter(client, player.registration());
    host.steps(5, {&client});
    const world::Entity* entity = host.world.find_character(kQuestNew);
    check(entity != nullptr && entity->quests.active.size() == 2 && quest_of(*entity, "06_getoffbeach")->instance == beach
              && quest_of(*entity, "Test_TimerQuest")->instance == timer && states_text(*quest_of(*entity, "06_getoffbeach")) == kBeachStates
              && entity->quests.tracked == std::vector<std::uint64_t>{timer} && quest_of(*entity, "Test_TimerQuest")->tasks[1].startNs == timerStart
              && host.store.list_quests(kQuestNew).size() == 2,
          "a restart: the same instances, states, pin, and timer start");
    check(log_count("got the objective 06_getoffbeach") == 1, "one grant of the start objective in the whole case (no new grant)");
    const world::QuestResult next = host.world.quests().grant(*host.world.find_character(kQuestNew), "Test_LockQuest", "command", 0);
    check(next.ok() && next.instance > std::max(beach, timer), "a new instance after a restart is above the saved ones");
}

/** QG5: the lock of the last bit gives an empty list fragment (A4a). */
void last_bit(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Questa", state::WorldPosition{100.0F, 200.0F, 30.0F});
    check(host.store.grant_achievements(kQuestA, {"Test_Last"}), "control: the character has one achievement");
    FakeClient client(27000);
    host.enter(client, player.registration());
    send_2709(host, client, kQuestA);
    host.steps(10, {&client});
    const std::vector<Fragment> first = own_fragments(client, kAchievementSlot, kAchievementType);
    check(first.size() == 1 && bits_of(first[0]) == std::vector<std::uint32_t>{25}, "the first record: the one bit (25)");
    const std::size_t from = client.got.size();
    world::Entity* entity = host.world.find_character(kQuestA);
    const world::QuestResult granted = host.world.quests().grant(*entity, "Test_LockQuest", "command", 0);
    host.steps(5, {&client});
    const std::vector<Fragment> after = own_fragments(client, kAchievementSlot, kAchievementType, from);
    check(granted.ok() && entity->achievements.empty() && after.size() == 1 && bits_of(after[0]).empty()
              && after[0].fields.at({0, 0}).size() >= 3 && !has_row(host.store, kQuestA, "Test_Last"),
          "the lock of the last bit: the row goes away, and the client gets an empty list (count 0)");
    // A character that never had a bit gets no achievement fragment (the first record of stage 1.1b stays).
    const Player other = add_player(host.store, kSteamIdB, "Tester B", kQuestB, "Questb", state::WorldPosition{300.0F, 200.0F, 30.0F});
    FakeClient second(27001);
    host.enter(second, other.registration());
    host.steps(10, {&second});
    check(own_fragments(second, kAchievementSlot, kAchievementType).empty(), "a character with no bit: no achievement fragment");
}

/** QG6: no quest data (A5a). */
void no_data(const Options& options) {
    ServerHost host;
    gamedata::QuestData missing;
    check(!missing.load((options.workDirectory / "no-such-quest-data.json").string()) && !missing.loaded(),
          "the loader: no file, no data");
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Questa", state::WorldPosition{100.0F, 200.0F, 30.0F});
    FakeClient client(27000);
    host.enter(client, player.registration());
    send_2709(host, client, kQuestA);
    host.steps(10, {&client});
    const world::Entity* entity = host.world.find_character(kQuestA);
    check(entity != nullptr && !entity->quests.loaded && host.world.quests().data() == nullptr, "no quest data: the character has no quest");
    check(own_fragments(client, kObjectivesSlot, kObjectivesType).empty() && own_fragments(client, kSeasonsSlot, kSeasonsType).empty(),
          "no quest data: no Objectives state and no readiness state after 2709");
    check(!host.world.quests().grant(*host.world.find_character(kQuestA), "06_getoffbeach", "command", 0).ok()
              && host.world.quests().apply_template(kQuestA, "Level60_Test").error == world::QuestError::noData,
          "no quest data: a grant and a template are refused");
}

/** QG7: the evaluators and the change event. */
void evaluators(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Questa", state::WorldPosition{100.0F, 200.0F, 30.0F});
    check(host.store.grant_achievements(kQuestA, {"Test_Block"}), "control: Test_Block");
    FakeClient client(27000);
    host.enter(client, player.registration());
    host.steps(5, {&client});
    world::Entity* entity = host.world.find_character(kQuestA);
    check(host.world.quests().grant(*entity, "Test_WaitQuest", "command", 0).ok(), "the wait quest");
    const world::QuestInstance* quest = quest_of(*entity, "Test_WaitQuest");
    check(quest != nullptr && states_text(*quest) == "0:1/0 1:0/0 2:1/0", "the conditional Test_Npa (Test_Flag && !Test_Block) is false: task 0 waits");
    check(host.world.ownership().unlock_achievement(*entity, "Test_Flag", 22), "unlock Test_Flag (a different system)");
    quest = quest_of(*entity, "Test_WaitQuest");
    check(states_text(*quest) == "0:1/0 1:0/0 2:1/0", "Test_Block is still there: the non-persisted achievement is false");
    check(host.world.ownership().lock_achievement(*entity, "Test_Block"), "lock Test_Block");
    quest = quest_of(*entity, "Test_WaitQuest");
    check(states_text(*quest) == "0:2/1 1:2/1 2:2/2" && has_bit(*entity, 24) && has_row(host.store, kQuestA, "Test_Done"),
          "the change event completes the wait, the next task unlocks Test_Done, and the root completes with the count 2");
    // The partial container of the beach: Watched_IGC_Aelstrom_MB completes it; its other children without StayActive end.
    check(host.world.quests().grant(*entity, "06_getoffbeach", "command", 0).ok(), "the beach quest");
    check(host.world.ownership().unlock_achievement(*entity, "Watched_IGC_Aelstrom_MB", 15279), "unlock Watched_IGC_Aelstrom_MB");
    const world::QuestInstance* beach = quest_of(*entity, "06_getoffbeach");
    check(beach != nullptr && beach->tasks[7].state == world::quest_state::kComplete && beach->tasks[15].state == world::quest_state::kComplete
              && beach->tasks[8].state == world::quest_state::kInactive && beach->tasks[9].state == world::quest_state::kInactive
              && beach->tasks[14].state == world::quest_state::kInactive && beach->tasks[16].state == world::quest_state::kCompleteActive
              && beach->tasks[16].progress == 4,
          std::string("the partial container: task 7 completes 15; 8, 14, 9 end (Inactive); the root completes (CompleteActive, count 4): ")
              + (beach != nullptr ? states_text(*beach) : ""));
}

/** QG8: TaskTimer on the world clock. */
void timer(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Questa", state::WorldPosition{100.0F, 200.0F, 30.0F});
    FakeClient client(27000);
    host.enter(client, player.registration());
    send_2709(host, client, kQuestA);
    host.steps(5, {&client});
    world::Entity* entity = host.world.find_character(kQuestA);
    const std::uint64_t now = host.clock.wall_time_ns(host.clock.now());
    const std::size_t from = client.got.size();
    check(host.world.quests().grant(*entity, "Test_TimerQuest", "command", 0).ok(), "the timer quest");
    host.steps(2, {&client});
    const world::QuestInstance* quest = quest_of(*entity, "Test_TimerQuest");
    check(quest != nullptr && states_text(*quest) == "0:2/1 1:1/0 2:0/0 3:1/1" && quest->tasks[1].startNs == now && has_bit(*entity, 20),
          "the unlock runs, the timer starts at the wall time of the world clock");
    const std::vector<Fragment> states = own_fragments(client, kObjectivesSlot, kObjectivesType, from);
    bool started = false;
    if (!states.empty()) {
        const ObjectivesState state = decode_objectives(states.back());
        started = state.timers.size() == 1 && std::get<0>(state.timers[0]) == quest->instance && std::get<1>(state.timers[0]) == 1
                  && std::get<2>(state.timers[0]) == now;
    }
    check(started, "field 0 of the state: the start of the timer (instance, index 1, Unix nanoseconds)");
    // 25.4 s: at 25.3 s the timer runs, at 25.5 s it is done.
    host.steps(2530 - 4, {&client});
    quest = quest_of(*entity, "Test_TimerQuest");
    check(quest->tasks[1].state == world::quest_state::kActive && !has_bit(*entity, 21), "after 25.3 s the timer runs");
    const std::size_t later = client.got.size();
    host.steps(20, {&client});
    quest = quest_of(*entity, "Test_TimerQuest");
    check(states_text(*quest) == "0:2/1 1:2/1 2:2/1 3:2/3" && has_bit(*entity, 21) && quest->tasks[1].startNs == 0,
          "after 25.5 s the timer is complete, the next task unlocks Test_After, the root completes");
    const std::vector<Fragment> ended = own_fragments(client, kObjectivesSlot, kObjectivesType, later);
    check(!ended.empty() && decode_objectives(ended.back()).timers.empty(), "the state after the timer: an empty timer map");
}

/** QG9: decision D2 option C. */
void templates(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_player(host.store, kSteamIdA, "Tester", kQuestA, "Questa", state::WorldPosition{100.0F, 200.0F, 30.0F});
    const Player other = add_player(host.store, kSteamIdB, "Tester B", kQuestB, "Questb", state::WorldPosition{300.0F, 200.0F, 30.0F});
    FakeClient client(27000);
    host.enter(client, player.registration());
    host.steps(5, {&client});
    const std::uint32_t levelBefore = host.world.find_character(kQuestA)->progression.level;
    const std::size_t itemsBefore = host.store.list_items(kQuestA).size();
    const world::TemplateResult result = host.world.quests().apply_template(kQuestA, "Level60_Test");
    const world::Entity* entity = host.world.find_character(kQuestA);
    check(result.ok() && result.inWorld && result.achievements == std::vector<std::string>{"Test_Story_1", "Test_Story_2"}
              && result.objectives == std::vector<std::string>{"Test_WaitQuest"}
              && result.objectivesWithNoRow == std::vector<std::string>{"Missing_Objective"},
          "the template: two achievements, one start objective, one objective with no row");
    check(has_bit(*entity, 26) && has_bit(*entity, 27) && quest_of(*entity, "Test_WaitQuest") != nullptr
              && entity->quests.tracked.size() == 1,
          "the character in the world: the bits and the pinned start objective");
    check(entity->progression.level == levelBefore && host.store.list_items(kQuestA).size() == itemsBefore,
          "the template does not change the level (60 of the row) or the items");
    const std::size_t quests = entity->quests.active.size();
    check(host.world.quests().apply_template(kQuestA, "Level60_Test").ok() && host.world.find_character(kQuestA)->quests.active.size() == quests,
          "the template again: no second instance of the objective");
    // A character that is not in the world: the rows, and the next entry gives the objective.
    const world::TemplateResult stored = host.world.quests().apply_template(kQuestB, "Level60_Test");
    check(stored.ok() && !stored.inWorld && has_row(host.store, kQuestB, "Test_Story_1") && host.store.list_quests(kQuestB).empty()
              && host.store.list_quest_grants(kQuestB).size() == 1 && host.store.list_quest_grants(kQuestB)[0].state == "pending",
          "not in the world: the achievement rows and a pending grant");
    FakeClient second(27001);
    host.enter(second, other.registration());
    host.steps(5, {&second});
    const world::Entity* entered = host.world.find_character(kQuestB);
    check(entered != nullptr && quest_of(*entered, "Test_WaitQuest") != nullptr && has_bit(*entered, 26)
              && host.store.list_quest_grants(kQuestB)[0].state == "granted",
          "the next entry gives the objective and the bits");
    check(host.world.quests().apply_template(kQuestA, "Archetype_Soldier").error == world::QuestError::unknownTemplate
              && host.world.quests().apply_template(kQuestA, "No_Such_Template").error == world::QuestError::unknownTemplate
              && host.world.quests().apply_template("0e180000-0000-4000-8000-0000000000ff", "Level60_Test").error
                     == world::QuestError::unknownCharacter,
          "refusals: an archetype row, an unknown template, an unknown character");
    // The common layer: the command of the admin and development routes.
    service::HttpResponse refusal;
    const json arguments = {{"template", "Level60_Test"}};
    const server::http::commands::JsonArguments args(arguments);
    const server::http::commands::WorldCommand command = server::http::commands::apply_backstory(kQuestA, args, refusal);
    const service::HttpResponse reply = command ? command(host.world) : refusal;
    const json body = json::parse(reply.body, nullptr, false);
    check(reply.status == 200 && body.is_object() && body["applied"] == "world" && body["objectivesWithNoRow"].size() == 1,
          "the command apply_backstory: reply 200 with the ids that the data does not have");
    const json catalog = server::http::commands::backstories_json(host.world);
    check(catalog["templates"].size() == 1 && catalog["templates"][0]["id"] == "Level60_Test" && catalog["templates"][0]["objectives"][1]["inData"] == false,
          "the template list: the templates only (no archetype row), each start objective with its mark inData");
}

/** QG10: the gate of 2709 and the pins of 1458. */
void gate_and_pins(const Options&) {
    ServerHost host;
    load_quests(host);
    const Player player = add_new_character(host, kQuestNew, "Questnew", kSteamIdA);
    FakeClient client(27000);
    host.enter(client, player.registration());
    host.steps(30, {&client});
    check(own_fragments(client, kObjectivesSlot, kObjectivesType).empty() && own_fragments(client, kSeasonsSlot, kSeasonsType).empty(),
          "no quest state while the client sent no 2709 (300 ms)");
    send_2709(host, client, kQuestNew);
    host.steps(5, {&client});
    check(own_fragments(client, kObjectivesSlot, kObjectivesType).size() == 1 && own_fragments(client, kSeasonsSlot, kSeasonsType).size() == 1,
          "after the first 2709: one of each");
    send_2709(host, client, kQuestNew);
    host.steps(5, {&client});
    check(own_fragments(client, kObjectivesSlot, kObjectivesType).size() == 1, "a second 2709 sends nothing again");
    world::Entity* entity = host.world.find_character(kQuestNew);
    const std::uint64_t instance = entity->quests.active.front().instance;
    const std::size_t from = client.got.size();
    host.datagram(client.peer, client.component_message(server::rep::kTypeUpdateTrackedObjectives, entity->actor.bytes,
                                                        tracked_body(entity->id, {instance + 1000})));
    host.steps(3, {&client});
    check(entity->quests.tracked == std::vector<std::uint64_t>{instance} && host.store.list_quest_pins(kQuestNew).size() == 1,
          "a 1458 with an instance that the character does not have: no change");
    host.datagram(client.peer, client.component_message(server::rep::kTypeUpdateTrackedObjectives, entity->actor.bytes, tracked_body(entity->id, {})));
    host.steps(3, {&client});
    check(entity->quests.tracked.empty() && host.store.list_quest_pins(kQuestNew).empty()
              && own_fragments(client, kObjectivesSlot, kObjectivesType, from).empty(),
          "a 1458 with the empty list: the rows and the entity get it, and no state goes back to the client");
}

/** QG11: the loader refuses a file with a missing link or a conflict. */
void loader(const Options&) {
    gamedata::QuestData data;
    check(data.load_text(quest_document().dump()) && data.objectives().size() == 4 && data.objective("06_GETOFFBEACH") != nullptr
              && data.objective_by_crc(0xE4D66020U) != nullptr && data.territory_objective(6) != nullptr && data.achievement_index("test_flag") == 22U,
          "the document of the cases loads; the objective CRC of 06_getoffbeach is E4D66020");
    const gamedata::QuestObjective* beach = data.objective("06_getoffbeach");
    check(beach->nodes[4].target == 4 && beach->nodes[15].target == 1 && beach->nodes[16].target == 4 && beach->nodes[4].stayActive
              && beach->nodes[16].stayActive && !beach->nodes[14].stayActive && data.task(beach->nodes[9].task).crc == 0x1B73198BU,
          "the targets (simple 4, partial 1, root 4), the effective StayActive (4 and the root from task 1), the task CRC 1B73198B");
    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json document = quest_document();
        change(document);
        gamedata::QuestData copy;
        check(!copy.load_text(document.dump()) && !copy.loaded(), std::string("the loader refuses: ") + what);
    };
    refused("a tree node with a task that the file does not have", [](json& document) { document["objectives"][1]["tree"][0][1] = "no_such_task"; });
    refused("two objectives with one CRC-32 (plumless, buckeroo)", [](json& document) {
        json first = document["objectives"][1];
        json second = document["objectives"][1];
        first["id"] = "plumless";
        second["id"] = "buckeroo";
        document["objectives"].push_back(first);
        document["objectives"].push_back(second);
    });
    refused("two tasks with one CRC-32", [](json& document) {
        document["tasks"]["plumless"] = task("plumless", "tasktimer", 64);
        document["tasks"]["buckeroo"] = task("buckeroo", "tasktimer", 64);
    });
    refused("an index above 255", [](json& document) { document["objectives"][1]["tree"][3][0] = 256; });
    refused("a child that is not below its container", [](json& document) { document["objectives"][1]["tree"][3][2][0] = 3; });
    refused("two achievements with one bit", [](json& document) { document["achievements"].push_back(json::array({"Test_Twin", 20})); });
    refused("the format 2", [](json& document) { document["format"] = 2; });
}

/** QG-TOOL: the check of the data tool. */
void tool_check(const Options& options) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_quests_tool.py";
    std::filesystem::create_directories(options.workDirectory / "quests-tool");
    const std::string command = "python3 -B \"" + script.string() + "\" \"" + (options.workDirectory / "quests-tool").string() + "\" > \""
                                + (options.workDirectory / "quests-tool.log").string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    check(status == 0, "the check of the quest data tool (check_quests_tool.py): status 0 (log: work folder quests-tool.log)");
}

} // namespace

std::vector<Case> quest_cases() {
    return {{"QG1", "quests (1.8a, A3a, MP-10): two sessions: the quest state, the readiness, and the bits of one never reach the other", two_sessions},
            {"QG2", "quests (1.8a, I6): 50 characters x 20 waiting quests: an event visits only its character; entries and 2,000 events in 5 s",
             load},
            {"QG3", "quests (1.8a, A1a): a new character: the beach, the start grant one time, the archetype bit, slot 73 and 22 after 2709", new_character},
            {"QG4", "quests (1.8a, A2a): a reconnect and a restart keep the instance, the pin, the task states, the timer start; no new grant",
             persistence},
            {"QG5", "quests (1.8a, A4a): the lock of the last bit: an empty list fragment", last_bit},
            {"QG6", "quests (1.8a, A5a): no quest data: no quest, no quest state, the refusals", no_data},
            {"QG7", "quests (1.8a, I4): TaskAchievement with a non-persisted achievement, the change event, the partial container", evaluators},
            {"QG8", "quests (1.8a, I4): TaskTimer of 25.4 s on the world clock, the start in field 0, the completion", timer},
            {"QG9", "quests (1.8a, I5, D2 option C): a backstory template in the world and as rows; no level or item change; refusals", templates},
            {"QG10", "quests (1.8a, L1): the gate of the first 2709, a second 2709, the pins of 1458 (a foreign instance refused)", gate_and_pins},
            {"QG11", "quests (1.8a, I1): the loader refuses a missing task link, a CRC conflict, an index above 255", loader},
            {"QG-TOOL", "quests (1.8a, I1): the check of the data tool (runs check_quests_tool.py)", tool_check}};
}

} // namespace soulwarden::backend::tests
