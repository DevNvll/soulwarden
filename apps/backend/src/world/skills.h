#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gamedata/progression_tables.h"
#include "state/store.h"
#include "world/entity.h"
#include "world/progress.h"

// The skill system: the weapon masteries, the weapon trees, the ability bars, and the trade
// skills of each character.
//
// It works as the progress system does (world/progress.h): each function names a character by
// its id, finds its values in its entity or in its rows, checks the request, computes the
// result, writes all rows of the change in one transaction, and then changes the entity and
// marks the component states. A request that is not correct changes nothing.
//
// The rules (protocol notes 4.13):
// - A weapon mastery and a trade skill have a rank and the experience in that rank (the rank
//   tables of the game). The screen shows the rank plus 1 for a weapon and the rank for a
//   trade skill.
// - A weapon gives one mastery point for each rank (a rule of this project: the game data has
//   no value for it). A point unlocks one node of one of the two trees of the weapon.
// - The conditions of a node (the check of the client, image RVA 0x6849F10): the node that it
//   needs is there; a node below the first row needs a point in the row before it; a node of
//   the last row of a tree needs 10 points in that tree.
// - The ability bar of a weapon has three places for active abilities of its tree that the
//   character has.
// - A tree reset gives all points of a weapon back and makes its bar empty. It has a cost in
//   the currency "Azoth" from rank 10 on. The backend does not have that currency: it does a
//   reset only when the cost is 0.
//
// A system that gives mastery or trade skill experience later calls add_experience.
//
// World thread only.

namespace soulwarden::backend::world {

class World;

/** Points in a tree that a node of its last row needs (`playerbaseattributes.pbadb` of the game). */
inline constexpr std::uint32_t kLastRowPoints = 10;
/**
 * Points in a row that a node of the next row needs, for the rows 0 to 3 (the same file). A
 * row after them needs no point in the row before it.
 */
inline constexpr std::uint32_t kRowPoints = 1;
inline constexpr std::uint32_t kLastRowWithRequirement = 3;
/** Places of the ability bar of a weapon. */
inline constexpr std::uint32_t kBarSlots = 3;
/** The categorical progression of the Azoth balance (stage 3.7c; game data: CategoricalProgressionId Azoth_Currency, MaxLevel 0). */
inline constexpr const char* kAzothCurrency = "Azoth_Currency";
/** Largest amount of one mastery or trade skill experience command. */
inline constexpr std::uint64_t kSkillExperienceLimit = 10000000;

struct SkillResult {
    ProgressError error{ProgressError::none};
    /** True: the character is in the world, and its session gets the change. False: only the rows changed. */
    bool inWorld{false};
    /** Rank and experience of the weapon mastery or trade skill of the request, after it. */
    std::uint32_t rank{0};
    std::uint64_t points{0};
    /** Weapons: the free mastery points after the request. */
    std::uint32_t freePoints{0};

    [[nodiscard]] bool ok() const {
        return error == ProgressError::none;
    }
};

/**
 * One weapon tree of a build (the build preset, stage B0; world/build_presets.h): the mastery rank, the nodes in a sequence that the
 * rules of the tree accept, and the abilities of the bar. The plan replaces the tree: its old nodes and its bar go.
 */
struct TreeBuild {
    /** The id text of the ability table ("BowAbilityTable"). */
    std::string table;
    /** The rank on the wire: the level of the screen minus 1. */
    std::uint32_t rank{0};
    /** The id texts of the nodes. */
    std::vector<std::string> nodes;
    /** The id text of the ability of each place of the bar; an empty text: an empty place. */
    std::array<std::string, kBarSlots> bar;
};

/**
 * Stage QG1: the result of a gain of territory standing (Skills::add_standing; docs/systems/progression-and-skills.md, the standing).
 */
struct StandingGain {
    /** The points of the source (the data of a quest reward, a command), and the points of the categorical progression after the card. */
    std::uint64_t base{0};
    std::uint64_t points{0};
    /** The GainStanding bonus of the territory pool of the character (card_bonus of its level; 0 with no card). */
    float bonus{0.0F};
    /** The ranks of the gain: each one is one token of the territory pool. */
    std::uint32_t ranks{0};
    /** The character experience of the new ranks: for each one its XpReward times TerritoryStandingXpModifier. */
    std::uint64_t experience{0};
    /** The rank and the points in the rank after the gain. */
    std::uint32_t rank{0};
    std::uint64_t rankPoints{0};
};

/** Stage QG1: the result of a standing command (Skills::give_standing). */
struct StandingResult {
    ProgressError error{ProgressError::none};
    bool inWorld{false};
    StandingGain gain;
    /** The free tokens of the territory pool after the change. */
    std::uint32_t tokens{0};
    /** The level and the experience of the character after the change (the experience of the ranks). */
    std::uint32_t level{0};
    std::uint32_t levelExperience{0};

    [[nodiscard]] bool ok() const {
        return error == ProgressError::none;
    }
};

/** The skills of a character, for a reader. */
struct SkillView {
    SkillSet skills;
    /** True: the values are the values of the entity of the character. False: of the rows. */
    bool inWorld{false};
};

class Skills {
public:
    /** @param tables The progression tables, or null: then the system has no rule of a table. */
    Skills(World& world, state::Store& store, const gamedata::ProgressionTables* tables);

    /** Reads the skills of the character of a new entity from the store. */
    void load(Entity& entity);

    /** @return The skills of a character, or no value when the store does not have the character. */
    [[nodiscard]] std::optional<SkillView> view(const std::string& characterId);

    /**
     * Sets the rank of a weapon mastery or of a trade skill, and the experience in the rank.
     * @param id The id text of the game table ("SwordAbilityTable", "Mining").
     * @param kind The kind that the command is for: an id of the other kind is refused.
     * @param rank The rank on the wire: the level of the screen minus 1 for a weapon, and the
     *        level of the screen for a trade skill.
     * When a weapon gets a rank with fewer points than the character spent in its tree, the
     * tree is reset: all its points are free again, and its bar is empty.
     */
    SkillResult set_rank(const std::string& characterId,
                         std::string_view id,
                         gamedata::ProgressionKind kind,
                         std::uint32_t rank,
                         std::uint64_t points = 0);

    /**
     * Gives experience to a weapon mastery or to a trade skill. The rank goes up while the
     * experience is the experience of the rank or more; the rest stays as experience of the new
     * rank. At the largest rank the experience is 0.
     */
    SkillResult add_experience(const std::string& characterId, std::string_view id, std::uint64_t amount);

    /**
     * Spends one mastery point on a node of a weapon tree: the request of a client.
     * @param pool The point pool of the weapon, @param node the node (ids on the wire).
     * @param count The points of the request. A node takes one point.
     * After a request that the system refused, the session of the character gets the node
     * with the level 0 and the free points again: the client changed its own values before it
     * sent the request.
     */
    SkillResult assign(const std::string& characterId, std::uint32_t pool, std::uint32_t node, std::uint32_t count);

    /**
     * Puts an active ability on a place of the ability bar of a weapon, or makes the place
     * empty (ability 0): the request of a client. The ability leaves the place that it had.
     */
    SkillResult set_bar(const std::string& characterId, std::uint32_t table, std::uint32_t slot, std::uint32_t ability);

    /** Gives all mastery points of a weapon back and makes its bar empty: the request of a client. */
    SkillResult reset_tree(const std::string& characterId, std::uint32_t table);

    /**
     * Stage QG1: the card choice of a client (message 4040, RequestSpendTerritoryStandingPoint; protocol notes 4.13): one level of a card
     * of the territory pool of a standing. The checks: the standing (progressionId: the CRC-32 of the territory id text, not the pool),
     * the card (a territory card of the point data), the card of the rank row whose index is the spent tokens of the pool (client
     * 0x3CEBDA0), a free token (also for a card with DoNotSpendPoint: 0x3CE7564), RequiredCategoricalProgressionLevel against the
     * standing rank, and MaxLevel, in the order of CanSpend of the client (0x3CE7490). A card with DoNotSpendPoint (TB_PurchaseHouse) does
     * not spend the token. The answer is the changed points state; a refused request sends the state again and changes nothing.
     * `result.rank` is the level of the card after the request, `result.freePoints` the free tokens.
     */
    SkillResult spend_territory_point(const std::string& characterId, std::uint32_t progressionId, std::uint32_t pointId);
    /**
     * Stage QG1: a standing command (the development and admin routes): the gain of add_standing with its card, its ranks, its tokens,
     * and the character experience of the ranks (the progress system), in one transaction.
     */
    StandingResult give_standing(const std::string& characterId, std::uint16_t territory, std::uint64_t amount);
    /**
     * Stage QG1: @return The bonus of the card of a kind in the territory pool of a territory (card_bonus of the level of the character; 0
     *         with no card, no pool, or no tables). GainStanding goes to the standing of the territory (add_standing), GainXP to the
     *         experience of the character in the territory, GainGatherRate to its gather speed in the territory (docs/systems).
     */
    [[nodiscard]] float territory_bonus(const SkillSet& skills, std::uint16_t territory, gamedata::TerritoryBonus kind) const;
    /**
     * Stage QG1: @return The character experience of a game event (a quest hand-in, a kill, a standing rank) with the GainXP card of the
     *         territory at the position of a character in the world (client 0x3CEBF84, 0x3CF04A9: the multiplier is 1 plus the card plus
     *         other bonus terms that the backend does not have, and the experience is trunc(f32(amount * multiplier))). The amount with no
     *         card, for a character that is not in the world, or outside each territory. A command of experience (the reason Debug of the
     *         client, 0x3CEBF0D) does not use it.
     */
    [[nodiscard]] std::uint64_t card_experience(const std::string& characterId, std::uint64_t amount) const;
    /** Stage QG1: @return The GainXP bonus of a character in the world at its territory (0: none). */
    [[nodiscard]] float experience_card(const Entity& entity) const;

private:
    /** What a change touched. */
    struct Touched {
        bool ranks{false};
        bool points{false};
        bool bars{false};
        /** Stage QG1: the territory pools changed (the tokens or the cards): the points state, and no change of a weapon tree. */
        bool tokens{false};
        /** The nodes whose level changed: pool and node. */
        std::vector<std::pair<std::uint32_t, std::uint32_t>> nodes;
    };

public:
    /**
     * A change of the skills of a character that is made on a copy and not written (stage B0): one part of a change of more than one
     * system (world/build_presets.h). Each change of this system is a plan, a write, and a finish (Inventory::Plan).
     */
    struct Plan {
        std::string characterId;
        ProgressError error{ProgressError::none};
        bool inWorld{false};
        SkillSet skills;
        Touched touched;
        SkillResult result;
        /** Stage QG1: the character experience of the standing ranks of the plan (add_standing): the caller gives it (the progress plan). */
        std::uint64_t rankExperience{0};
        /** Stage QG1: the GainXP bonus of the territory of the character in the world (experience_card), for the experience of a rank. */
        float experienceBonus{0.0F};

        [[nodiscard]] bool ok() const {
            return error == ProgressError::none;
        }
    };

    /**
     * Plans a gain of experience of a weapon mastery or a trade skill (stage 3.7b: the trade skill experience of a craft job), with the
     * rules of add_experience, and no write: the craft job writes the rows in its transaction (write) and finishes the plan after the commit.
     */
    [[nodiscard]] Plan plan_experience(const std::string& characterId, std::string_view id, std::uint64_t amount);
    /**
     * Stage QG5b: adds a gain of experience of a weapon mastery or a trade skill to a plan (the rules of add_experience; the categorical
     * progression reward of a hand-in: the AzothStaff rank). No write.
     */
    [[nodiscard]] ProgressError add_experience(Plan& plan, std::string_view id, std::uint64_t amount) const;
    /** @return A plan of a character with no change (stage 3.7c: a craft job with a cost of Azoth and no experience adds its balance). */
    [[nodiscard]] Plan plan_none(const std::string& characterId);
    /**
     * Stage 3.7c: adds a change of a balance to a plan of the character (a categorical progression of rank 0 with no rank-up, such as
     * Azoth_Currency: the points of the progression on the wire, research crafting-stage.md section 4): the points plus `delta`; a cost above
     * the points is refused (notEnoughBalance), and a gain stops at `cap` (the MaximumInfluence of the rank 0; AllowOverflow is 0). One plan
     * has the experience and the balance of a craft job: the rows of the skills are one write.
     * @param id The id text of the progression (the id on the wire is its CRC-32).
     */
    [[nodiscard]] ProgressError add_balance(Plan& plan, std::string_view id, std::int64_t delta, std::uint64_t cap) const;
    /**
     * Stage 1.8d, stage QG1: adds territory standing to a plan of the character (the categorical progression of the territory: its id text
     * is the decimal territory id, on the wire its CRC-32; points in the rank, client 0x3CAACF0). The gain is the amount times 1 plus the
     * GainStanding bonus of the territory pool, truncated (client 0x31F7D19 to 0x31F7D37). AutoRankUp: while the points reach
     * MaximumInfluence of the rank (equality too, 0x3CAAF15), they lose it and the rank goes up (0x3CAAFDF, the rest again at 0x3CAB113);
     * each new rank is one token of the territory pool (0x3CB7500) and the character experience of its XpReward times
     * TerritoryStandingXpModifier (0x31FD6AC; Plan::rankExperience). At the largest rank (300: MaximumInfluence 0, AllowOverflow false) the
     * points are 0, and a later gain adds nothing. A territory with no standing row: unknownProgression (noTables with no tables).
     */
    [[nodiscard]] ProgressError add_standing(Plan& plan, std::uint16_t territory, std::uint64_t amount, StandingGain* gain = nullptr) const;
    /**
     * Stage QG7: adds points to a faction progression of a plan (the reputation or the tokens of a faction; docs/systems/factions.md): the
     * points are a total, and a gain stops at the MaximumInfluence of the rank of the progression (the faction rows have AutoRankUp false
     * and AllowOverflow false: no rank-up by points). A cost (a negative delta) above the points is refused (notEnoughBalance).
     * `gained` gets the change of the points. A progression that is not a faction progression: unknownProgression.
     */
    [[nodiscard]] ProgressError add_faction_points(Plan& plan, std::uint32_t progressionId, std::int64_t delta,
                                                   std::int64_t* gained = nullptr) const;
    /**
     * Stage QG7: sets the rank of a faction progression of a plan (the ProgressionReward of a rank-up row or of a trial, a command). A rank
     * above the largest rank of a player (Expansion01MaxLevel with the owned expansion): badRank. The points stay; a lower rank cuts them at
     * its cap.
     */
    [[nodiscard]] ProgressError set_faction_rank(Plan& plan, std::uint32_t progressionId, std::uint32_t rank) const;
    /** Stage QG7: a command: sets the rank and the points of a faction progression (the points at most the cap of the rank). */
    SkillResult set_faction_progress(const std::string& characterId, std::uint32_t progressionId, std::uint32_t rank, std::uint64_t points);
    /** Stage QG7: @return The rank and the points of a categorical progression of a skill set (rank 0 and 0 points when it has none). */
    [[nodiscard]] static ProgressionRank progression_of(const SkillSet& skills, std::uint32_t progressionId);

    /** Stage 1.8d: @return The standing of a territory of a skill set: its rank and its points in the rank. */
    [[nodiscard]] static ProgressionRank standing(const SkillSet& skills, std::uint16_t territory);
    /** A command (stage 3.7c): sets a balance to `amount` (0 to `cap`). */
    SkillResult set_balance(const std::string& characterId, std::string_view id, std::uint64_t amount, std::uint64_t cap);
    /** @return The balance of a progression of rank 0 of a skill set (the points; 0 with no entry). */
    [[nodiscard]] static std::uint64_t balance(const SkillSet& skills, std::string_view id);
    /**
     * Plans weapon trees (the build preset): for each tree the rank of set_rank, the reset of the tree (its nodes and its bar go,
     * with no cost: a rank change that frees the points), each node with the checks of assign (one point, the node that it needs,
     * the points of the row before it and of its tree), and each place of the bar with the checks of set_bar. No write.
     */
    [[nodiscard]] Plan plan_trees(const std::string& characterId, const std::vector<TreeBuild>& trees);
    /**
     * @return The first error of the trees on a character with no skill (the check of the build presets at the start of the backend):
     *         the same rules as plan_trees. `noTables` with no progression tables.
     */
    [[nodiscard]] ProgressError check_trees(const std::vector<TreeBuild>& trees) const;
    /** Writes the rows of a plan (in a batch: a savepoint). @return False: the database did not take them. */
    [[nodiscard]] bool write(const Plan& plan);
    /** Changes the entity of a written plan, marks its states, and refreshes the gear. */
    SkillResult finish(Plan& plan, const char* what);

private:
    /** @return The plan of a change: `apply(SkillSet&, Touched&, SkillResult&)` on a copy. */
    template <class Change>
    Plan stage(const std::string& characterId, Change&& apply);
    /** The trees of a build on a skill set (plan_trees, check_trees). */
    [[nodiscard]] ProgressError trees_into(SkillSet& skills, Touched& touched, const std::vector<TreeBuild>& trees) const;
    /** The experience of add_experience on a skill set. */
    [[nodiscard]] ProgressError experience_into(SkillSet& skills, Touched& touched, SkillResult& result, std::string_view id,
                                                std::uint64_t amount) const;
    /** The rank of set_rank on a skill set. */
    [[nodiscard]] ProgressError rank_into(SkillSet& skills, Touched& touched, SkillResult& result, std::string_view id,
                                          gamedata::ProgressionKind kind, std::uint32_t rank, std::uint64_t points) const;
    /** The node of assign on a skill set. */
    [[nodiscard]] ProgressError assign_into(SkillSet& skills, Touched& touched, SkillResult& result, std::uint32_t pool,
                                            std::uint32_t node, std::uint32_t count) const;
    /** Stage QG1: the card of spend_territory_point on a skill set. */
    [[nodiscard]] ProgressError card_into(SkillSet& skills, Touched& touched, SkillResult& result, std::uint32_t progressionId,
                                          std::uint32_t pointId) const;
    /** Stage QG1: adds the territory pools of the tables to a skill set (each pool, also with no card) and sets the spent tokens. */
    void add_territory_pools(SkillSet& skills) const;
    /** The place of the bar of set_bar on a skill set. */
    [[nodiscard]] ProgressError bar_into(SkillSet& skills, Touched& touched, std::uint32_t table, std::uint32_t slot,
                                         std::uint32_t ability) const;

    /** A change: `apply(SkillSet&, Touched&, SkillResult&)` gives a ProgressError. */
    template <class Change>
    SkillResult change(const std::string& characterId, const char* what, Change&& apply);
    [[nodiscard]] SkillSet read_skills(const std::string& characterId) const;
    [[nodiscard]] state::SkillRows rows_of(const SkillSet& skills) const;
    /** Removes the nodes and the bar of a weapon. */
    void clear_tree(SkillSet& skills, std::uint32_t table, Touched& touched) const;
    /** @return The condition of a node that is not met, or `none`. */
    [[nodiscard]] ProgressError node_condition(const SkillSet& skills, const gamedata::AbilityNode& node) const;

    World& m_world;
    state::Store& m_store;
    /** @return The tables when the backend has them, or null. A test can set the tables after the start of the world. */
    [[nodiscard]] const gamedata::ProgressionTables* tables() const {
        return m_source != nullptr && m_source->loaded() ? m_source : nullptr;
    }

    const gamedata::ProgressionTables* m_source;
};

} // namespace soulwarden::backend::world
