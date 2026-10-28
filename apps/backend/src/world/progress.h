#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

#include "gamedata/progression_tables.h"
#include "state/store.h"
#include "world/entity.h"

// The progress system: the level, the experience, and the attributes of each character, and
// the vitals that follow from them.
//
// Each function names a character by its id (roadmap, rule MP-9). The system finds the values
// of the character: the entity of the character when it is in the world, or the row of the
// store when it is not. It checks the request, computes the result itself (rule MP-5), writes
// the row (and the coin of an attribute reset in the same transaction, rule MP-7), and then
// changes the entity and marks the component states: the encoders send the result to each
// session that observes the entity (rule MP-2). A request that is not correct changes nothing.
//
// The rules (protocol notes 4.12):
// - A level has a range of total experience (the level table of the game). Experience that
//   reaches the start of the next level gives that level, up to the largest level.
// - A character has the attribute points of its level (the sum of the level table). The value
//   of an attribute is the minimum of the attribute tables plus the points that the character
//   spent on it. The free points are the points of the level minus the spent points.
// - The maximum health of a player is the base health plus the health of its level plus the
//   health of its constitution. The game data has the three values; the sum is a rule of this
//   project (the code of the original server is not in the client).
//
// A system that gives experience later (a kill, a quest, gathering) calls add_experience.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

/** Why the progress system or the skill system (world/skills.h) refused a request. */
enum class ProgressError {
    none,
    /** The store has no character with this id. */
    unknownCharacter,
    /** The backend has no progression tables (tools/gamedata/progression.sh makes them). */
    noTables,
    /** The level is not a level of a player (1 to the largest level). */
    badLevel,
    /** The amount is 0, or it is larger than the amount that the request can have. */
    badAmount,
    /** The attribute number is not an attribute, or the value is not a value of an attribute. */
    badAttribute,
    /** The request spends more points than the character has. */
    notEnoughPoints,
    /** The lists of the request do not agree, or an attribute is in the request two times. */
    badRequest,
    /** The character does not have the coin for the cost. */
    notEnoughCoin,
    /** The game data has no weapon mastery and no trade skill with this id. */
    unknownProgression,
    /** The id is a trade skill where the request needs a weapon, or a weapon where it needs a trade skill. */
    wrongKind,
    /** The rank is larger than the largest rank, or the experience is not experience of the rank. */
    badRank,
    /** The node is not a node of the tree of this weapon. */
    unknownNode,
    /** The character has the node already. */
    nodeOwned,
    /** A condition of the node is not met: the node that it needs, or the points in the row before it, or the points in its tree. */
    nodeLocked,
    /** The place is not a place of the ability bar (0 to 2). */
    badSlot,
    /** The ability is not an active ability of the tree of this weapon. */
    notActive,
    /** The character does not have the node of the ability. */
    notOwned,
    /** The tree reset has a cost in a currency that the backend does not have. */
    costNotPaid,
    /** Stage 3.7c: the balance of a currency (Azoth) is below the cost. */
    notEnoughBalance,
    /** The database did not accept the change. */
    storeFailed,
    /**
     * Stage 1.8d: a gain of territory standing that gives a new rank. Stage QG1: only for a standing row with no AutoRankUp (the data has
     * none: each standing row has AutoRankUp, and the backend has no rule of a manual rank-up).
     */
    standingRankUp,
    /** Stage QG1: the id is not a card of the territory pools (the point data). */
    unknownCard,
    /** Stage QG1: the card screen does not offer the card: it is not a card of the rank row of the spent tokens (client 0x3CEBDA0). */
    cardNotOffered,
    /** Stage QG1: the standing rank of the territory is below RequiredCategoricalProgressionLevel of the card (TB_PurchaseHouse 1). */
    standingTooLow,
    /** Stage QG1: the card has its MaxLevel. */
    cardAtMaxLevel,
    /** Stage QG1: the territory pool has no free token. */
    noToken,
};

/** @return A short text for a log line and for the reply of a command. */
[[nodiscard]] const char* progress_error_text(ProgressError error);

struct ProgressResult {
    ProgressError error{ProgressError::none};
    /** True: the character is in the world, and its session gets the change. False: only the row changed. */
    bool inWorld{false};
    /** The values after the request (the values of the character when the request was refused). */
    std::uint32_t level{0};
    std::uint32_t experience{0};
    std::uint32_t unspent{0};
    /** The levels that the request gave. */
    std::uint32_t levelsGained{0};

    [[nodiscard]] bool ok() const {
        return error == ProgressError::none;
    }
};

/** One entry of a spend request: points for one attribute. */
struct AttributeSpend {
    std::uint32_t type{0};
    std::uint32_t points{0};
};

/** The level, the attributes, and the vitals of a character, for a reader. */
struct ProgressView {
    Progression progression;
    Attributes attributes;
    Vitals vitals;
    /** Total experience at which the level of the character starts, and at which the next level starts (0: no next level, or no tables). */
    std::uint32_t levelExperience{0};
    std::uint32_t nextLevelExperience{0};
    /** True: the values are the values of the entity of the character. False: of the row. */
    bool inWorld{false};
};

/** Largest amount of one experience command. */
inline constexpr std::uint64_t kExperienceLimit = 10000000;
/** Largest number of points of one entry of a spend request (the limit of the client reader). */
inline constexpr std::uint32_t kSpendLimit = 700;

class Progress {
public:
    /** @param tables The progression tables, or null: then the system has no rule of a table. */
    Progress(World& world, state::Store& store, const gamedata::ProgressionTables* tables);

    /** Reads the level, the experience, and the attributes of the character of a new entity from the store, and sets its vitals. */
    void load(Entity& entity);
    /**
     * The attribute bonus or the health part of a character changed (the gear system, stage 1.3b): the maximum health
     * again from the values of the entity, with the health keeping its part of the maximum. Marks the vitals state when
     * it changed. World thread only.
     */
    void refresh_vitals(Entity& entity);

    /** @return The level, the attributes, and the vitals of a character, or no value when the store does not have the character. */
    [[nodiscard]] std::optional<ProgressView> view(const std::string& characterId);

    /**
     * Sets the level of a character (a level of the screen: 1 to the largest level). The
     * experience becomes the experience at the start of the level. When the character spent
     * more attribute points than the new level has, all its attribute points are free again.
     */
    ProgressResult set_level(const std::string& characterId, std::uint32_t level);

    /**
     * Gives experience to a character. The level goes up while the experience is at the start
     * of the next level or above it. At the largest level the experience stays at the start of
     * that level.
     */
    ProgressResult add_experience(const std::string& characterId, std::uint64_t amount);

    /**
     * Sets the value of one attribute (a development command). The value is the minimum of
     * the attribute tables or more, and the character must have the points for it.
     */
    ProgressResult set_attribute(const std::string& characterId, unsigned type, std::uint32_t value);

    /**
     * Spends free points on attributes: the request of a client. When the sum of the points
     * is larger than the free points of the character, nothing changes. An attribute does not
     * go above the largest value of the attribute tables: the points above it stay free. After
     * a request that the system refused, the session of the character gets the attribute
     * state again: the client changed its own values before it sent the request.
     */
    ProgressResult spend(const std::string& characterId, std::span<const AttributeSpend> entries);

    /**
     * Makes all attribute points of a character free again: the request of a client. The
     * level table has the cost in coin (0 below the last levels). When the character does not
     * have the coin, nothing changes.
     */
    ProgressResult reset_attributes(const std::string& characterId);

private:
    /** The values of a character that a change works on. */
    struct Sheet {
        Progression progression;
        Attributes attributes;
        /** The part of the maximum health of the gear and the effects (Entity::gear, stage 1.3b). */
        float healthPart{0.0F};
        /** The mana parts of the gear and the effects (Entity::gear, stage 3.5b.2): MaxMana and ManaRate. */
        float manaPart{0.0F};
        float manaRatePart{0.0F};
    };

public:
    /**
     * A change of the level and the attributes of a character that is made on a copy and not written (stage B0): one part of a change
     * of more than one system (world/build_presets.h). Each change of this system is a plan, a write, and a finish (Inventory::Plan).
     */
    struct Plan {
        std::string characterId;
        ProgressError error{ProgressError::none};
        bool inWorld{false};
        Sheet before;
        Sheet sheet;
        /** A cost in coin (an attribute reset): the write takes it through the inventory system. */
        std::uint64_t cost{0};

        [[nodiscard]] bool ok() const {
            return error == ProgressError::none;
        }
    };

    /**
     * Plans a level and the value of each attribute (the build preset): the checks of set_level (1 to the largest level of the owned
     * expansion) and of set_attribute (each value from the minimum to the maximum of the attribute tables, and the points of all of
     * them not more than the points of the level). The experience becomes the start of the level. No write.
     * @param values The value of each attribute (index world::attribute::k*).
     */
    [[nodiscard]] Plan plan_sheet(const std::string& characterId, std::uint32_t level, const std::array<std::uint32_t, attribute::kCount>& values);
    /**
     * @return The first error of a level and attribute values on a character with nothing (the check of the build presets at the start
     *         of the backend): the rules of plan_sheet. `noTables` with no progression tables.
     */
    [[nodiscard]] ProgressError check_sheet(std::uint32_t level, const std::array<std::uint32_t, attribute::kCount>& values) const;
    /**
     * Stage 1.8b: plans a gain of experience (the rules of add_experience) for one change of more than one system (the hand-in of a quest,
     * world/quests.h). No write.
     */
    [[nodiscard]] Plan plan_experience(const std::string& characterId, std::uint64_t amount);
    /** Writes the row of a plan (in a batch: a savepoint). @return False: the database did not take it. */
    [[nodiscard]] bool write(const Plan& plan);
    /** Changes the entity of a written plan and marks its states. */
    ProgressResult finish(Plan& plan, const char* what);

private:
    /** @return The plan of a change: `apply(Sheet&, std::uint64_t& cost)` on a copy, then the values that agree with the tables. */
    template <class Change>
    Plan stage(const std::string& characterId, bool needsTables, Change&& apply);
    /** The level and the values of plan_sheet on a sheet. */
    [[nodiscard]] ProgressError sheet_into(Sheet& sheet, std::uint32_t level, const std::array<std::uint32_t, attribute::kCount>& values) const;
    /** The experience of add_experience on a sheet. */
    [[nodiscard]] ProgressError experience_into(Sheet& sheet, std::uint64_t amount) const;
    /** The level of set_level on a sheet. */
    [[nodiscard]] ProgressError level_into(Sheet& sheet, std::uint32_t level) const;
    /** The value of one attribute of set_attribute on a sheet. */
    [[nodiscard]] ProgressError attribute_into(Sheet& sheet, unsigned type, std::uint32_t value) const;
    /**
     * @return The MaxMana and ManaRate parts of the threshold abilities of the attributes of a sheet (stage 3.5b.2; the attribute tables,
     *         EquipAbilities: Focus 100 gives Foc_Bonus_100_1 with MaxMana 0.1 and ManaRate 0.2; the value with the bonus of the gear).
     */
    [[nodiscard]] std::pair<float, float> threshold_mana(const Sheet& sheet) const;

    /** A change: `apply(Sheet&, std::uint64_t& cost)` gives a ProgressError. */
    template <class Change>
    ProgressResult change(const std::string& characterId, const char* what, bool needsTables, Change&& apply);
    [[nodiscard]] Sheet read_sheet(const std::string& characterId, bool log) const;
    /** Makes the values agree with the tables: the level range, the experience of the level, the free points. */
    void settle(Sheet& sheet) const;
    /** @return The vitals of a character with these values: the maximum of each one, and the amounts of `old` for the same part of the maximum. */
    [[nodiscard]] Vitals vitals_of(const Sheet& sheet, const Vitals& old) const;

    World& m_world;
    state::Store& m_store;
    /** @return The tables when the backend has them, or null. A test can set the tables after the start of the world. */
    [[nodiscard]] const gamedata::ProgressionTables* tables() const {
        return m_source != nullptr && m_source->loaded() ? m_source : nullptr;
    }

    const gamedata::ProgressionTables* m_source;
};

} // namespace soulwarden::backend::world
