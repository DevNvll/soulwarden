#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gamedata/mission_data.h"
#include "state/store.h"
#include "world/entity.h"
#include "world/quests.h"

// The mission boards (stage QG6a, docs/systems/mission-boards.md; research mission-boards.md): the town project board of a settlement, its
// offers, the accept of a mission (3819), its hand-in at the board (2461), and the provider state of a character (the ObjectiveInteractor
// state, player slot 10, type 3829).
//
// The board is a server child of its settlement (the spawner Town_Project_Board of the settlement slice; m_isClientAuthoratative false,
// m_spawnOnInitialize true): the interaction system makes it near a character (the spawn rule of the stations). Its option
// OpenMissionBoardOption (action 51, committed) opens the board: this system is the handler of the kind missionBoard. The open gives the
// character the provider state of the board (the actor id of the board, the expiration, the town entry of its territory) and the service of
// the board (Interactions::grant_service, the kind "missionBoard"), and completes the return tasks of the missions of the character whose
// provider is this board (CM_ReturnToGiver: TaskInteract with the tag ReturnToGiver; the factory 0x4461150, case 23, gives the task the
// destination of the instance).
//
// The client computes the town offers itself (GetCurrentCommunityGoals 0x4955A90 -> 0x3A34C70 -> 0x3A362A0): a seed of the server clock
// minute, the territory, and a text (0x3A36220, 0x39D2A70), then for each goal bucket of the mission data one candidate of a 48-bit LCG. The
// server computes the same list (town_offers) and accepts only an offer of the current seed. The town entry of the character (its stored seed
// and its unavailable missions) is the only offer state on the wire: a hand-in makes its mission unavailable for that seed.
//
// The state of a character is in its entity (Entity::missionBoard, the mission values of its quest instances, rule MP-1); only this system
// changes the board state, and only the quest system the instances. The cost depends on the character and its board, not on the size of the
// world (MP-8): an open computes the offers of one territory; a spawn of a board visits the characters with a mission of that board.
//
// World thread only. A route gives the world a task (World::call).

namespace soulwarden::backend::world {

class World;

/**
 * The inputs of the town seed of the client (static read evidence/quests/qg6a/implement/static/offers/OFFERS.md): the text of the FNV-1a
 * hash is the characterId of the local player (0x4955C1A to 0x4955E95 through PlayerComponent +0x1F0, 0x67F0540; the Player state 3935
 * group 2 field 0 gives it: the backend sends Entity::characterId with the flags 0); the interval is the registered default of the CVar
 * javelin.territory-progression-mission-cooldown-min (30.0f: 0x61743A9 to 0x6176250; no configuration file of the game copy sets it, and no
 * server message for it is known); the seed addition of the provider (+0x230) is 0 (the constructor 0x3A1C52A; the other writes copy it).
 */
namespace town_rule {
inline constexpr float kIntervalMinutes = 30.0F;
inline constexpr std::uint32_t kSeedAddition = 0;
/** The key of the active-project map of the TerritoryInterface state for an AlwaysAvailable project (the type enum 6 of the report). */
inline constexpr std::uint8_t kAlwaysAvailableKey = 6;
} // namespace town_rule

/** The LCG of the town selection (0x3A36539, 0x3A36809 to 0x3A3683D): the multiplier and the 48-bit mask of java.util.Random. */
inline constexpr std::uint64_t kTownLcgMultiplier = 0x5DEECE66DULL;
inline constexpr std::uint64_t kTownLcgMask = (1ULL << 48) - 1;

/**
 * @return The town seed of the client (0x3A36220 -> 0x39D2A70): FNV-1a 64 of `text`, plus the territory, plus the server clock minute
 *         rounded to the interval with the half-interval phase, plus `addition`; the low 32 bits.
 */
[[nodiscard]] std::uint32_t town_seed(std::string_view text, std::uint16_t territory, std::uint64_t serverNs, float intervalMinutes,
                                      std::uint32_t addition);
/** @return GetSecondsToNextSeed (0x4957F20): 60 * (P - ((m + P/2) % P)) - s for the server clock minute m and second s. */
[[nodiscard]] std::uint64_t seconds_to_next_seed(std::uint64_t serverNs, float intervalMinutes);
/** @return The server clock nanoseconds of the start of the next seed (the expiration of the provider state of a town board). */
[[nodiscard]] std::uint64_t next_seed_ns(std::uint64_t serverNs, float intervalMinutes);

/** One town offer of the client list. */
struct TownOffer {
    std::uint32_t mission{0};
    /** The goal bucket key, the index of the candidate in its filtered bucket, and the number of the candidates. */
    std::uint8_t bucket{0};
    std::uint32_t index{0};
    std::uint32_t candidates{0};
    /** The project of the selection (the active project of the town). */
    std::uint32_t project{0};
    /** False when the stored seed of the town entry is the current seed and the mission is in its unavailable list (0x3A3684C). */
    bool available{true};
};

/** The inputs of the town offers of one character at one board: its territory, its projects, the character values. */
struct TownInputs {
    /** The seed text: the characterId of the character. */
    std::string seedText;
    std::uint16_t territory{0};
    std::uint64_t serverNs{0};
    /** The active projects of the town (the CRC-32 of each lower-case ProjectId). */
    std::vector<std::uint32_t> projects;
    /**
     * The level of the filter 0x3A09900: the value of the level request of the client (0x4955CAF, no added 1), the level index (the level
     * of the screen minus 1; the objective eligibility 0x3A4C13E adds 1 to the same request) [client; inferred: docs/systems/mission-boards.md].
     */
    std::uint32_t level{0};
};

struct TownOffers {
    std::uint32_t seed{0};
    std::vector<TownOffer> offers;
};

/** The causes of a refused accept or hand-in of a mission. */
enum class MissionError : std::uint8_t {
    none,
    /** The backend has no mission data or no quest data. */
    noData,
    notInWorld,
    /** The character has no service of a board (no open), or of a different board, or the service ended (range, entry, removal). */
    noProvider,
    /** The mission is not an offer of the current seed of the board (an old seed, a forged id, another board). */
    notOffered,
    /** The mission is an offer that a hand-in of this seed made unavailable. */
    unavailable,
    /** The character has an active instance of this mission. */
    alreadyActive,
    /** The mission has a rule that the backend cannot run (QuestData::mission refusals). */
    unsupported,
    /** The instance is not an active mission of the character with this board as its provider. */
    unknownInstance,
    /** The quest system refused (not ready, the bag, a reward with no owner, the store). */
    questRefused,
    storeFailed,
};

[[nodiscard]] const char* mission_error_text(MissionError error);

struct MissionResult {
    MissionError error{MissionError::none};
    std::uint64_t instance{0};
    std::string detail;
    /** The result of the hand-in of the quest system (2461). */
    HandInResult handIn;
    [[nodiscard]] bool ok() const {
        return error == MissionError::none;
    }
};

/** The counts of the system (the tests and `tools/world missions`). */
struct MissionCounts {
    std::uint64_t opens{0};
    std::uint64_t accepts{0};
    std::uint64_t acceptRefusals{0};
    std::uint64_t handIns{0};
    std::uint64_t handInRefusals{0};
    std::uint64_t returns{0};
    /** The offer computations, and the candidates that they visited (each mission of the buckets, before any filter). */
    std::uint64_t offerRuns{0};
    std::uint64_t candidateVisits{0};
    std::uint64_t destinationUpdates{0};
};

/** The view of a character at a board (a command). */
struct MissionView {
    bool inWorld{false};
    MissionBoardState board;
    TownOffers offers;
    std::uint64_t secondsToNextSeed{0};
    std::vector<QuestInstance> missions;
};

class Missions {
public:
    Missions(World& world, state::Store& store, const gamedata::MissionData* data);
    ~Missions();
    Missions(const Missions&) = delete;
    Missions& operator=(const Missions&) = delete;

    /** @return The mission data, or null when the backend has none. */
    [[nodiscard]] const gamedata::MissionData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /** The character comes into the world (after the quests): its town entries, the destinations of its missions. */
    void entered(Entity& entity);
    void left(const Entity& entity);

    /** The client opened a board (the interaction handler of OpenMissionBoardOption). @return False: refused (no data, no board). */
    bool opened(Entity& character, const std::string& boardKey, const Entity& board);

    /**
     * The accept of a town offer (3819 OnCommunityGoalSelectionRequest: missionId:u32, destinationOverride:u32). The character from the
     * session; the service of its board (the provider, the range, the entry); an offer of the current seed that is available; no active
     * instance of the mission; a mission that the backend can run. Then the instance (Quests::grant_mission). `destination` is the second
     * argument of the client: the project of the card of the town screen (townproject_activeproject.luac OnTaskStart), or 0 (the detail
     * popup, townprojects.luac); another value is refused.
     */
    [[nodiscard]] MissionResult accept(Entity& character, std::uint32_t mission, std::uint32_t destination);
    /**
     * The hand-in at the board (2461 OnObjectiveCompletionRequest: instanceId:u64): an active mission of the character whose provider is
     * the board of its service, a ready root; then Quests::hand_in (one transaction: the rewards of the success event of the mission), and
     * the town entry: the mission is unavailable for the current seed (one row).
     */
    [[nodiscard]] MissionResult complete(Entity& character, std::uint64_t instance);

    /** @return The town offers of a character for its board state now (no change). */
    [[nodiscard]] TownOffers offers_of(const Entity& character, std::uint16_t territory) const;
    /** @return The offers of the inputs and the town entry (the rule of the client; tests). */
    [[nodiscard]] TownOffers town_offers(const TownInputs& inputs, const MissionTownEntry* entry, const Entity* character) const;
    /** @return The projects of a town whose offers the board gives (docs/systems/mission-boards.md: the always available project). */
    [[nodiscard]] std::vector<std::uint32_t> town_projects(std::uint16_t territory) const;

    /** The interaction system made a server child of a placement (a board): the destinations of the missions of that board. */
    void child_spawned(const std::string& key, Entity& entity);

    /** @return The view of a character (a command), or no value when the store has no such character. */
    [[nodiscard]] std::optional<MissionView> view(const std::string& characterId);
    [[nodiscard]] const MissionCounts& counts() const {
        return m_counts;
    }

private:
    class BoardHandler;
    /** @return The live board entity of a key (a server child), or null. */
    [[nodiscard]] const Entity* board_of(const std::string& key) const;
    /** @return The territory of a board (the territory at its place: World::territory_of). */
    [[nodiscard]] std::uint16_t territory_of(const Entity& board) const;
    /** Sets the destination of each mission of a character with the provider `key` to `actor` (the Objectives state when one changed). */
    void set_destinations(Entity& character, const std::string& key, const std::array<std::uint8_t, 16>& actor);
    /** The return task of a mission at its provider board (the request 2461): the restricted interact event of the tag ReturnToGiver. */
    void complete_return(Entity& character, const QuestInstance& quest);
    [[nodiscard]] bool candidate(const gamedata::MissionRow& row, const TownInputs& inputs, const std::set<std::uint32_t>& allowed,
                                 const Entity* character) const;

    World& m_world;
    state::Store& m_store;
    const gamedata::MissionData* m_data;
    std::unique_ptr<BoardHandler> m_handler;
    /** The characters with a mission of each board (its key): a spawn of the board visits only them (MP-8). */
    std::map<std::string, std::set<EntityId>> m_byProvider;
    mutable MissionCounts m_counts;
};

} // namespace soulwarden::backend::world
