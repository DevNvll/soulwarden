#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "gamedata/progression_tables.h"
#include "state/store.h"
#include "world/entity.h"
#include "world/world_clock.h"

// The faction system (stage QG7; docs/systems/factions.md): the faction of each character, its join and its change, and the wait and the
// cost of a change. The reputation and the tokens of each faction are categorical progressions of the skill system (world/skills.h); the
// quest rules of a faction (the gates, TaskHaveLevel, TaskChooseFaction, the rank-up rows, the rewards) are in the quest system
// (world/quests.h).
//
// The faction is a value of the entity of the character (Entity::faction) and a row of the store (`character_factions`). Only this system
// changes it: a request of the client (3161 RequestSetFaction, RepSession::handle_set_faction) or a command (the development and admin
// routes). Each change writes its rows in one transaction (the faction row, and the Azoth of the cost in the skills rows), then changes the
// entity, marks the Faction state (slot 49), and gives the change to the quest system.
//
// The client publishes the faction of a request on its HUD before the answer (0x30C00B0 calls the HUD setter 0x30C2E50): a refused request
// marks the state too, and the owner gets the faction of the server again (a received faction field republishes its value, 0x30B6210).
//
// The cost depends on the request and on the character, not on the size of the world (MP-8). World thread only.

namespace soulwarden::backend::world {

class World;

/** The causes of a refused faction request or command. */
enum class FactionError : std::uint8_t {
    none,
    /** The store has no character with this id. */
    unknownCharacter,
    /** The character is not in the world (a request of a client). */
    notInWorld,
    /** The backend has no faction data (the member `factions` of progression-tables.json). */
    noData,
    /** The faction is not 1, 2, or 3 (0 None and 4 Any are not a faction of a player: the enum validator 0x2E6A8A0 accepts 0 to 4). */
    badFaction,
    /** The character has this faction already. */
    sameFaction,
    /**
     * The data path does not offer the faction: no active recruitment task of the faction with the Faction_Selection service of a
     * conversation, and no recruitment achievement of the faction (the Bio change panel calls CanSetFactionWithResults with the achievement
     * check, result FailAchievementRequirement 10).
     */
    notOffered,
    /** The wait after the last change is not over (OnCooldown 5: lastFactionChangeTimepoint plus Faction Change Cooldown Seconds). */
    onCooldown,
    /** The Azoth balance is below the cost of the change (InsufficientAzoth 7). */
    notEnoughAzoth,
    /** The database did not take the change. */
    storeFailed,
};

[[nodiscard]] const char* faction_error_text(FactionError error);
/** @return The name of a faction of the client enum ("None", "Syndicate", "Marauder", "Covenant"). */
[[nodiscard]] const char* faction_name(std::uint8_t faction);
/** @return The number of a faction name (Syndicate, Marauder, Covenant; also 1 to 3 and Faction1 to Faction3; the case of the letters does not count), or no value. */
[[nodiscard]] std::optional<std::uint8_t> faction_number(const std::string& text);

struct FactionResult {
    FactionError error{FactionError::none};
    bool inWorld{false};
    /** The faction state after the request (the state before it after a refusal). */
    FactionState state;
    /** The faction before the request. */
    std::uint8_t before{0};
    /** The Azoth that the change took. */
    std::uint64_t cost{0};
    /** Wall nanoseconds that are left of the wait of a change (0: none). */
    std::uint64_t waitNs{0};

    [[nodiscard]] bool ok() const {
        return error == FactionError::none;
    }
};

/** What a command changes (the development and admin routes): the faction, and optionally the time of the last change and the count. */
struct FactionCommand {
    std::uint8_t faction{0};
    /** The time of the last change: no value keeps it. 0 removes the wait. */
    std::optional<std::uint64_t> changedNs;
    std::optional<std::uint16_t> changes;
};

class Factions {
public:
    Factions(World& world, state::Store& store, const gamedata::ProgressionTables* tables);

    /** Reads the faction of the character of a new entity from the store. */
    void load(Entity& entity);

    /** @return The faction of a character (its entity, or its row), or no value when the store does not have the character. */
    [[nodiscard]] std::optional<FactionState> view(const std::string& characterId);

    /**
     * The request 3161 of the client of a character (RequestSetFaction): the join of a character with no faction, or a change. The checks
     * (docs/systems/factions.md): the faction data, the faction (1 to 3, not the faction of the character), the data path that offers it
     * (offered), the wait of a change, the Azoth cost. A join keeps the time 0 and the count 0 (the first change has no wait); a change sets
     * the time to now and adds 1 to the count. A refused request changes nothing and marks the state: the owner gets its faction again.
     */
    FactionResult request(Entity& character, std::uint8_t faction);

    /** A command: sets the faction (and the time or the count) with no check of the data path, the wait, or the cost. */
    FactionResult set(const std::string& characterId, const FactionCommand& command);

    /** @return The first cause that refuses the faction for a request of the character now, or `none` (no change). */
    [[nodiscard]] FactionError check(const Entity& character, std::uint8_t faction, std::uint64_t* cost = nullptr,
                                     std::uint64_t* waitNs = nullptr) const;
    /**
     * @return True when the data path offers the faction to the character (docs/systems/factions.md): its recruitment objective is active
     *         with its TaskChooseFaction task and the character has the Faction_Selection service of a conversation, or the character has
     *         the recruitment achievement of the faction (RequiredAchievementId or RequiredLegacyAchievementId of the faction table) and,
     *         with no faction, the service.
     */
    [[nodiscard]] bool offered(const Entity& character, std::uint8_t faction) const;
    /** @return The wall nanoseconds that are left of the wait of a change of a faction state now (GetCooldownEndTime 0x30B1DE0). 0: none. */
    [[nodiscard]] std::uint64_t wait_left(const FactionState& state) const;
    /** @return The Azoth cost of a change of a character (docs/systems/factions.md, the cost). */
    [[nodiscard]] std::uint64_t change_cost(const Entity& character) const;
    /** @return The wall clock of the world now (Unix nanoseconds). */
    [[nodiscard]] std::uint64_t now_ns() const;

    /** @return The faction row of the data of a faction, or null (no data). */
    [[nodiscard]] const gamedata::FactionRow* row(std::uint8_t faction) const;
    /** @return The faction of the recruitment objective whose completion achievement is this one (the faction table), or 0. */
    [[nodiscard]] std::uint8_t recruitment_faction(const std::string& achievement) const;
    [[nodiscard]] const gamedata::ProgressionTables* tables() const {
        return m_source != nullptr && m_source->loaded() && !m_source->factions().empty() ? m_source : nullptr;
    }

    struct Counts {
        std::uint64_t requests{0};
        std::uint64_t refusals{0};
        std::uint64_t joins{0};
        std::uint64_t changes{0};
    };
    [[nodiscard]] const Counts& counts() const {
        return m_counts;
    }

private:
    /** Writes the row (and the cost), changes the entity, marks the state, and gives the change to the quests. */
    FactionResult apply(Entity* character, const std::string& characterId, FactionState before, FactionState after,
                        std::uint64_t cost, const char* what);

    World& m_world;
    state::Store& m_store;
    const gamedata::ProgressionTables* m_source;
    Counts m_counts;
};

} // namespace soulwarden::backend::world
