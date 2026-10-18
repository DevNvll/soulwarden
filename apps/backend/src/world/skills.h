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

private:
    /** What a change touched. */
    struct Touched {
        bool ranks{false};
        bool points{false};
        bool bars{false};
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

        [[nodiscard]] bool ok() const {
            return error == ProgressError::none;
        }
    };

    /**
     * Plans a gain of experience of a weapon mastery or a trade skill (stage 3.7b: the trade skill experience of a craft job), with the
     * rules of add_experience, and no write: the craft job writes the rows in its transaction (write) and finishes the plan after the commit.
     */
    [[nodiscard]] Plan plan_experience(const std::string& characterId, std::string_view id, std::uint64_t amount);
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
