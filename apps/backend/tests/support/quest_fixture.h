#pragma once

// The fixture of the quest cases (stages 1.8a and 1.8c, world/quests.h): the quest data of the cases (the tree of 06_getoffbeach of the
// game data and made-up quests), the client messages of the quests, and the decoder of the Objectives state. Made-up accounts, ids, and
// names only.

#include <algorithm>
#include <string>
#include <tuple>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/rep/quest_messages.h"
#include "support/check.h"
#include "support/fake_client.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "world/quests.h"

namespace soulwarden::backend::tests::questfix {

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

inline json task(const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
    return {{"id", id}, {"crc", 0}, {"type", type}, {"code", code}, {"cells", cells}};
}

/** @return The quest data of the cases: the beach tree of the game data and the made-up quests. */
inline json quest_document() {
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

inline void load_quests(ServerHost& host) {
    if (!host.quests.load_text(quest_document().dump())) {
        throw std::runtime_error("quest test: the quest data did not load: " + host.quests.error());
    }
}

/** @return The body of 2709 for an entity: 36 flags of 1, with 0 for the type CommunityGoal (6). */
inline Bytes auto_pin_body(std::uint64_t entity) {
    server::hub::Writer body;
    body.u64(0);
    body.u64(entity);
    body.size(36);
    for (int index = 0; index < 36; ++index) {
        body.u8(index == 6 ? 0 : 1);
    }
    return body.data();
}

inline Bytes tracked_body(std::uint64_t entity, const std::vector<std::uint64_t>& instances) {
    server::hub::Writer body;
    body.u64(0);
    body.u64(entity);
    body.size(static_cast<std::uint32_t>(instances.size()));
    for (const std::uint64_t instance : instances) {
        body.u64(instance);
    }
    return body.data();
}

inline void send_2709(ServerHost& host, FakeClient& client, const std::string& characterId) {
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
    /** Stage 1.8b: field 2, the completed notice; `hasCompleted` when the fragment has the field. */
    std::vector<std::uint64_t> completed;
    bool hasCompleted{false};
    /** Stage QG5c2: field 5, the encounter roots of the character; `hasPoi` when the fragment has the field. */
    std::vector<std::uint64_t> poi;
    bool hasPoi{false};
};

inline ObjectivesState decode_objectives(const Fragment& fragment) {
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
    if (fragment.fields.contains({1, 2})) {
        state.hasCompleted = true;
        server::hub::Reader reader(fragment.fields.at({1, 2}));
        for (std::uint32_t count = open(2, reader), index = 0; index < count; ++index) {
            state.completed.push_back(reader.u64());
        }
    }
    if (fragment.fields.contains({1, 5})) {
        state.hasPoi = true;
        server::hub::Reader reader(fragment.fields.at({1, 5}));
        for (std::uint32_t count = open(5, reader), index = 0; index < count; ++index) {
            state.poi.push_back(reader.u64());
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
inline std::vector<Fragment> own_fragments(const FakeClient& client, std::uint32_t slot, std::uint32_t type, std::size_t from = 0) {
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
inline std::size_t foreign_fragments(const FakeClient& client, std::uint32_t slot, std::uint32_t type) {
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
inline std::vector<std::uint32_t> bits_of(const Fragment& fragment) {
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
inline Player add_new_character(ServerHost& host, const char* characterId, const char* name, const char* steamId) {
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

inline const world::QuestInstance* quest_of(const world::Entity& entity, const char* objective) {
    for (const world::QuestInstance& quest : entity.quests.active) {
        if (gamedata::lower_text(quest.objective) == gamedata::lower_text(objective)) {
            return &quest;
        }
    }
    return nullptr;
}

/** @return The states of the tasks of a quest: "index:state/progress" for each task, in index order. */
inline std::string states_text(const world::QuestInstance& quest) {
    std::string text;
    for (std::size_t index = 0; index < quest.tasks.size(); ++index) {
        text += (text.empty() ? "" : " ") + std::to_string(index) + ":" + std::to_string(quest.tasks[index].state) + "/"
                + std::to_string(quest.tasks[index].progress);
    }
    return text;
}

inline bool has_bit(const world::Entity& entity, std::uint32_t bit) {
    return std::binary_search(entity.achievements.begin(), entity.achievements.end(), bit);
}

inline bool has_row(state::Store& store, const char* characterId, const char* id) {
    const std::vector<std::string> rows = store.list_achievements(characterId);
    return std::find(rows.begin(), rows.end(), id) != rows.end();
}

/** The expected beach states (research quest-display-18a.md Q3, the corrected candidate; live check L1). */

} // namespace soulwarden::backend::tests::questfix
