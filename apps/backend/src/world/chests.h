#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "gamedata/chest_data.h"
#include "world/entity.h"
#include "world/inventory.h"
#include "world/world_clock.h"

// The chests of the world (stage 3.5b.1, docs/systems/chests.md, protocol notes 4.41): the open of an ordinary chest (supply, provision,
// alchemy, elite) with its own timer, the loot of its root with the gear score of its context, and the loot claim of each character.
//
// The client makes each chest from its level data and shows its prompt (live check L1). The key E sends GatherOption (action 14,
// committed) as the interaction block of the input report; the interaction base (world/interactions.h) checks the request (the
// reference, the option, the range of the data, the enabled state) and calls the handler of this system (GatherOption of an object of the
// kind "gathering"). The rules of this system, in this sequence (each refusal changes nothing; the source of each rule is in the
// document):
//   1. The object is a chest of the chest data (a level object; its placement key), and the chest is ordinary: an unavailable chest (a
//      key, a glyph, a lock, a quest, an encounter: chests.py) is refused with its reason.
//   2. The character has no active claim on the chest: a Looted claim before its reset is refused.
//   3. The open starts: the Gathering state of the owner gets Success, the time of the gatherable row (BaseGatherTime: 2.5 s or 5 s),
//      the speed 1, and the G of the chest (live check L3: the progress ring of the prompt runs that time). A timer of the world counts
//      the same time from the accepted request.
//   4. At the end of the time: the use is still valid (the base: the source, the life, the exact position, the range), the claim is
//      still not active. The roll: a kept roll of the claim (Rolled), or a new roll of the root of the chest (world/rewards.h,
//      roll_root, with the context of the chest: the level of the character, the content level of its territories, their loot tags,
//      the container floor of the gear score), with the perk roll of each instance (world/perk_roll.h).
//   5. One transaction of the store: the items go into the bag (the cause ItemGathered: the loot notice of the HUD) and the claim gets
//      the state Looted with its reset time. When the bag has no place for the whole result, no item goes into the bag, the roll stays
//      in the claim (Rolled) for the next open, and the use ends (3746): no item is lost, and nothing is rolled again. A write that the
//      store refuses changes nothing (the bag, the claim).
//   6. The Gathering state gets Done (the client ends its use: 2473), and the LootTracker state of the owner has the claim (Looted: the
//      client removes the prompt, live check L2).
// A cancel before the end (the release, 2473, a walk out of the range, a travel, the death, damage: the action Gather of the client ends
// at a hit) gives no item and the Gathering state Failure.
//
// The claims (MP-1, MP-7): a claim is a value of the world with the character id as its key (loaded at the world entry from the store
// table loot_claims, kept until the leave), and a row of the store. A claim ends at its reset time (the world clock; the wall clock in the
// row: a restart keeps the remaining time, and the time goes on while the backend is off): the system removes the row and sends the map
// with no entry (live check L8: the client does not end a claim by itself). The reset time: the reset values of the gatherable row (the
// elite chests: 82,800 s), else the reset of an instanced chest of the base attributes of a player (60 minutes).
//
// The LootTracker of a character has its claims near it (kTrackerRange from the place at its last cell change) and an entry for each
// unavailable chest near it (kUnavailableState). Each claim stays in the store also above the count of the data (60): the map has only
// the claims near the character (a decision of the orchestrator).
//
// The cost depends on the requests, the opens, the claims of the characters, and the chests near them, not on the size of the world
// (MP-8): a request finds its chest by the key (a hash), the map of a character looks at its own claims and at the cells near it at a cell
// change, an open has a timer of the world, and a reset is an entry of the due list. World thread only.

namespace soulwarden::backend::world {

class World;
class InteractionHandler;
struct InteractionContext;
enum class InteractionEnd : std::uint8_t;
enum class InteractionVerdict : std::uint8_t;

/** Rules of this project for the chests (docs/systems/chests.md). */
namespace chest_rules {
/** The claims and the unavailable chests of a character in this distance (horizontal, meters) go into its LootTracker. */
inline constexpr float kTrackerRange = 192.0F;
/** The edge of the cell of a character whose change makes its LootTracker again (meters). */
inline constexpr float kTrackerCell = 64.0F;
/** The speed of the progress of an open (field 2 of the Gathering state; live check L3: 1 runs the time of the data). */
inline constexpr float kGatherSpeed = 1.0F;
/** InstancedLootState values (client reflection 0x36FD370). */
inline constexpr std::uint32_t kInitial = 0;
inline constexpr std::uint32_t kRolled = 1;
inline constexpr std::uint32_t kLooted = 2;
inline constexpr std::uint32_t kLocked = 5;
/** The LootTracker state of an unavailable chest near a character (live check L9). */
inline constexpr std::uint32_t kUnavailableState = kLocked;
/** A reset that the store did not write comes again after this time. */
inline constexpr std::chrono::seconds kResetRetry{10};
/** The development override of a reset: from 1 s to the largest reset of the data (a week). */
inline constexpr std::chrono::seconds kLongestOverride{7 * 24 * 3600};
} // namespace chest_rules

enum class ClaimState : std::uint8_t {
    rolled = 1,
    looted = 2,
};

[[nodiscard]] const char* claim_state_text(ClaimState state);

/** The loot claim of a character on a chest (a value of the world; a row of the store). */
struct ChestClaim {
    std::uint32_t chest{0};
    ClaimState state{ClaimState::looted};
    std::uint64_t generation{0};
    /** Unix milliseconds of the row. */
    std::int64_t rolledAt{0};
    std::int64_t lootedAt{0};
    std::int64_t resetAt{0};
    /** The reset on the world clock (a looted claim). */
    WorldClock::time_point reset{};
    /** The kept roll of a rolled claim (the bag had no place for it). */
    std::vector<GrantItem> items;
};

/** The open of a chest by a character (the time of the open runs). */
struct ChestOpen {
    std::uint32_t chest{0};
    /** The open time of the gatherable row of the chest (seconds): the time of the timed use of the base (finding CH-R2). */
    float seconds{0.0F};
    WorldClock::time_point started{};
    WorldClock::time_point due{};
};

/** The verdicts of the chest handler (the log and the counts). */
enum class ChestRefusal : std::uint8_t {
    noData,
    notChest,
    unavailable,
    noLoot,
    claimed,
};
inline constexpr std::size_t kChestRefusalCount = 5;

[[nodiscard]] const char* chest_refusal_text(ChestRefusal refusal);

/** The counts of the system: for the log, the commands, and the tests. */
struct ChestCounts {
    std::uint64_t begins{0};
    std::uint64_t completions{0};
    std::uint64_t cancels{0};
    std::array<std::uint64_t, kChestRefusalCount> refusals{};
    std::uint64_t rolls{0};
    /** Completions that found no place in the bag: the roll stays in the claim. */
    std::uint64_t kept{0};
    std::uint64_t itemsGiven{0};
    /** Stage QG5a: the quest drops of the opens (ChestDropProbability; also in itemsGiven). */
    std::uint64_t questDrops{0};
    std::uint64_t resets{0};
    std::uint64_t storeFailures{0};
    /** The maps of the characters made again, the claims and the cells that they looked at (case LC8). */
    std::uint64_t trackers{0};
    std::uint64_t trackerClaims{0};
    gamedata::ChestLookupWork lookups;
    /** The due resets that the tick looked at (case LC8). */
    std::uint64_t dueLooks{0};
};

/** A claim of a character, for a reader (a command). */
struct ClaimView {
    std::string key;
    ClaimState state{ClaimState::looted};
    std::uint64_t generation{0};
    /** Seconds to the reset (a looted claim). */
    double resetSeconds{0.0};
    std::int64_t lootedAt{0};
    std::int64_t resetAt{0};
    std::vector<GrantItem> items;
    const gamedata::ChestDefinition* chest{nullptr};
};

/** A chest near a character, for a reader. */
struct ChestNearView {
    std::uint32_t index{0};
    const gamedata::ChestDefinition* chest{nullptr};
    float distance{0.0F};
    /** The claim of the character on it. No value: none. */
    std::optional<ClaimState> claim;
};

struct ChestsView {
    bool inWorld{false};
    std::optional<ChestOpen> open;
    std::vector<ClaimView> claims;
    std::vector<ChestNearView> nearby;
    std::size_t trackerEntries{0};
};

enum class ClaimError {
    none,
    notInWorld,
    /** The character has no claim on this chest (or no looted claim, for a reset override). */
    noClaim,
    /** The chest data has no chest with this key. */
    unknownChest,
    /** The time is not in the range of the override. */
    badTime,
    storeFailed,
};

[[nodiscard]] const char* claim_error_text(ClaimError error);

class Chests {
public:
    /** @param data The chest data (game data), or null: the handler refuses each open. */
    Chests(World& world, const gamedata::ChestData* data);
    ~Chests();
    Chests(const Chests&) = delete;
    Chests& operator=(const Chests&) = delete;

    [[nodiscard]] const gamedata::ChestData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /** The world calls it at the entry of a character (World::make_entity): its claims from the store, and its LootTracker. */
    void entered(Entity& character);
    /** The world calls it at the leave of a character: its open ends, and its claims leave the memory (the rows stay). */
    void left(Entity& character);
    /** The world calls it after an accepted position of a character: a new cell makes its LootTracker again. O(1). */
    void moved(Entity& character);
    /** Code review of stage DG1, finding DG1R-R3: the character changed its space: its tracker of now (no cell cache). */
    void space_changed(Entity& character);
    /** The world tick: the claims whose reset time came (the due list). */
    void tick();

    /** The handler of GatherOption (world/interactions.h): an accepted use of a chest begins its open. */
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context);
    /**
     * The handler: the timed use of the base reached the end of the open, and the checks of the base again passed (code review of
     * stage 3.5b.1, finding CH-R2: the timer of the base, thus a release at or after the time completes first). The roll and the grant.
     */
    /** @return True when the open completed (code review of stage 1.8c, finding QR8C-R12). */
    bool complete(Entity& character, const InteractionContext& context);
    /** The handler: the use of a chest ends (before its completion: a cancel). */
    void ended(Entity& character, const InteractionContext& context, InteractionEnd cause);
    /** @return True when a use is the use of a chest of the chest data (stage 3.6a: the gathering handler gives it to this system). */
    [[nodiscard]] bool owns(const InteractionContext& context) const {
        return chest_of(context).has_value();
    }
    /** @return The open time of the open of a character (the time of the timed use of the base), or 0: no open. */
    [[nodiscard]] float open_seconds(const Entity& character) const;

    /** @return The open, the claims, and the chests that are `radius` or less from a character (a command). */
    [[nodiscard]] ChestsView view(const std::string& characterId, float radius) const;
    /**
     * Removes the claim of a character on a chest (a command: the chest can be opened again). A character that is not in the world: its
     * row. @param key The placement key of the chest.
     */
    ClaimError clear(const std::string& characterId, const std::string& key);
    /**
     * Development aid (live check L8; only the development routes, which need SOULWARDEN_HARNESS=1): the looted claim of a character in the
     * world resets after `seconds` (1 s to a week), not at its time of the data. Not a rule of the product.
     */
    ClaimError set_reset(const std::string& characterId, const std::string& key, std::chrono::seconds seconds);

    [[nodiscard]] const ChestCounts& counts() const {
        return m_counts;
    }
    /** @return The number of claims in the memory of the world (the characters in the world). */
    [[nodiscard]] std::size_t claim_count() const;
    /** @return The number of resets in the due list. */
    [[nodiscard]] std::size_t due_count() const {
        return m_due.size();
    }
    /** @return The number of opens that run. */
    [[nodiscard]] std::size_t open_count() const;

private:
    /** The claims and the open of a character in the world. */
    struct Own {
        std::map<std::string, ChestClaim> claims;
        std::optional<ChestOpen> open;
        std::int64_t cell{0};
        bool hasCell{false};
        std::uint64_t generation{0};
    };
    struct DueReset {
        std::string characterId;
        std::string key;
        std::uint64_t generation{0};
    };

    [[nodiscard]] InteractionVerdict refuse(ChestRefusal refusal, const Entity& character, const std::string& key, const char* detail);
    /** The open of a character ends with no completion: the Gathering state Failure. */
    void fail_open(Entity& character, Own& own);
    void set_gather(Entity& character, std::uint8_t status, float remaining, bool active, std::uint64_t target);
    void refresh_tracker(Entity& character, Own& own);
    [[nodiscard]] std::chrono::milliseconds reset_time(const gamedata::ChestDefinition& chest);
    [[nodiscard]] std::int64_t wall_ms(WorldClock::time_point time) const;
    [[nodiscard]] state::LootClaimRow row_of(const std::string& key, const ChestClaim& claim) const;
    [[nodiscard]] std::uint64_t micros(WorldClock::time_point time) const;
    /** @return The chest index of a use of a level object, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> chest_of(const InteractionContext& context) const;

    World& m_world;
    const gamedata::ChestData* m_data;
    std::unique_ptr<InteractionHandler> m_handler;
    std::unordered_map<std::string, Own> m_own;
    std::multimap<WorldClock::time_point, DueReset> m_due;
    ChestCounts m_counts;
};

} // namespace soulwarden::backend::world
