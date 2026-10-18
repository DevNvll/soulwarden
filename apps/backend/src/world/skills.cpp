#include "world/skills.h"

#include <algorithm>
#include <array>
#include <cstdio>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** Largest rank that the wire form holds. */
constexpr std::uint32_t kLargestRank = 0xFFFF;

std::uint16_t node_level(const SkillSet& skills, std::uint32_t pool, std::uint32_t node) {
    const auto nodes = skills.pools.find(pool);
    if (nodes == skills.pools.end()) {
        return 0;
    }
    const auto found = nodes->second.find(node);
    return found == nodes->second.end() ? 0 : found->second.level;
}

} // namespace

Skills::Skills(World& world, state::Store& store, const gamedata::ProgressionTables* tables)
    : m_world(world), m_store(store), m_source(tables) {}

SkillSet Skills::read_skills(const std::string& characterId) const {
    const state::SkillRows rows = m_store.list_skills(characterId);
    SkillSet skills;
    for (const state::SkillRows::Rank& row : rows.ranks) {
        std::uint32_t rank = std::min(row.rank, kLargestRank);
        if (tables() != nullptr) {
            if (const gamedata::ProgressionRow* progression = tables()->progression(row.id)) {
                rank = std::min(rank, tables()->rank_cap(*progression));
            }
        }
        if (rank != 0 || row.points != 0) {
            skills.progressions[row.id] = ProgressionRank{static_cast<std::uint16_t>(rank), row.points};
        }
    }
    for (const state::SkillRows::Node& row : rows.nodes) {
        if (row.level != 0) {
            skills.pools[row.pool][row.node].level = 1;
        }
    }
    for (const state::SkillRows::Bar& row : rows.bars) {
        if (row.slot < kBarSlots && row.ability != 0) {
            skills.bars[row.table][row.slot] = row.ability;
        }
    }
    if (tables() != nullptr) {
        // Each weapon has a point pool, also with no point: the client permits a selection in
        // a tree only when it has the pool of the weapon.
        for (const gamedata::ProgressionRow& progression : tables()->progressions()) {
            if (progression.kind == gamedata::ProgressionKind::weapon) {
                skills.pools[progression.wireId];
            }
        }
    }
    return skills;
}

state::SkillRows Skills::rows_of(const SkillSet& skills) const {
    state::SkillRows rows;
    const auto progression_name = [&](std::uint32_t id) {
        const gamedata::ProgressionRow* row = tables() != nullptr ? tables()->progression(id) : nullptr;
        if (row == nullptr && id == gamedata::progression_id(kAzothCurrency)) {
            return std::string(kAzothCurrency); // Stage 3.7c: the balance of Azoth has no row of the progression tables.
        }
        return row != nullptr ? row->id : std::string{};
    };
    const auto node_name = [&](std::uint32_t id) {
        const gamedata::AbilityNode* row = tables() != nullptr ? tables()->node(id) : nullptr;
        return row != nullptr ? row->id : std::string{};
    };
    for (const auto& [id, value] : skills.progressions) {
        if (value.rank != 0 || value.points != 0) {
            rows.ranks.push_back({id, progression_name(id), value.rank, value.points});
        }
    }
    for (const auto& [pool, nodes] : skills.pools) {
        for (const auto& [node, state] : nodes) {
            if (state.level != 0) {
                rows.nodes.push_back({pool, node, node_name(node), state.level});
            }
        }
    }
    for (const auto& [table, bar] : skills.bars) {
        for (std::uint32_t slot = 0; slot < kBarSlots; ++slot) {
            if (bar[slot] != 0) {
                rows.bars.push_back({table, slot, bar[slot], node_name(bar[slot])});
            }
        }
    }
    return rows;
}

void Skills::load(Entity& entity) {
    entity.skills = read_skills(entity.characterId);
    std::size_t nodes = 0;
    for (const auto& [pool, tree] : entity.skills.pools) {
        nodes += tree.size();
    }
    log::write("skills: character %s has %zu weapon masteries and trade skills above the start, %zu node(s) of the "
               "weapon trees, and %zu ability bar(s)%s",
               entity.characterId.c_str(),
               entity.skills.progressions.size(),
               nodes,
               entity.skills.bars.size(),
               tables() == nullptr ? " (no progression tables)" : "");
}

std::optional<SkillView> Skills::view(const std::string& characterId) {
    SkillView view;
    if (const Entity* entity = m_world.find_character(characterId)) {
        view.skills = entity->skills;
        view.inWorld = true;
        return view;
    }
    if (!m_store.find_character(characterId)) {
        return std::nullopt;
    }
    view.skills = read_skills(characterId);
    return view;
}

template <class Change>
Skills::Plan Skills::stage(const std::string& characterId, Change&& apply) {
    Plan plan;
    plan.characterId = characterId;
    Entity* entity = m_world.find_character(characterId);
    if (entity != nullptr) {
        plan.skills = entity->skills;
        plan.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        plan.skills = read_skills(characterId);
    } else {
        plan.error = ProgressError::unknownCharacter;
    }
    if (plan.ok() && tables() == nullptr) {
        plan.error = ProgressError::noTables;
    }
    plan.result.inWorld = plan.inWorld;
    // The change is made on a copy: the skills of the entity change only after the rows.
    if (plan.ok()) {
        plan.error = apply(plan.skills, plan.touched, plan.result);
    }
    return plan;
}

bool Skills::write(const Plan& plan) {
    // All rows of the change are one transaction.
    const Touched& touched = plan.touched;
    return !(touched.ranks || touched.points || touched.bars) || m_store.write_skills(plan.characterId, rows_of(plan.skills));
}

SkillResult Skills::finish(Plan& plan, const char* what) {
    Entity* entity = plan.inWorld ? m_world.find_character(plan.characterId) : nullptr;
    const Touched& touched = plan.touched;
    SkillSet& skills = plan.skills;
    if (entity != nullptr) {
        if (touched.ranks) {
            m_world.touch(entity->id, ComponentKey::categorical);
        }
        if (touched.points) {
            // A session gets a node that the character lost one time: the node keeps the
            // revision of its change.
            const Revision revision = m_world.touch(entity->id, ComponentKey::points);
            for (const auto& [pool, node] : touched.nodes) {
                skills.pools[pool][node].changed = revision;
            }
        }
        if (touched.bars) {
            m_world.touch(entity->id, ComponentKey::abilities);
            // The cooldown state puts each cooldown on the key of the place of its ability: a bar change moves it
            // (code review of stage 3.3a, finding F8). The mark gives the maps a new sequence.
            m_world.touch(entity->id, ComponentKey::cooldowns);
        }
        entity->skills = std::move(skills);
        if (touched.bars || touched.points) {
            // An aim of an ability names the place of the bar and the node of its press: a change of the bar or of the nodes ends it (code
            // review of stage 3.3b-S6, finding S6-R2).
            m_world.abilities().cancel_aim(plan.characterId);
        }
        if (touched.points) {
            // The fixed stats of the learned nodes are gear values (Passives::fixed, Gear::values): a change of the nodes (an
            // assignment, a reset of a tree) refreshes them after the stored change (code review of stage 3.3b-S5, finding F5).
            m_world.gear().refresh(*entity);
        }
    }
    log::write("skills: %s of character %s: done (%s)",
               what,
               plan.characterId.c_str(),
               entity != nullptr ? "the character is in the world" : "the rows only");
    return plan.result;
}

template <class Change>
SkillResult Skills::change(const std::string& characterId, const char* what, Change&& apply) {
    Entity* entity = m_world.find_character(characterId);
    // The log line names the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    Plan plan = stage(characterId, std::forward<Change>(apply));
    if (plan.ok() && !write(plan)) {
        plan.error = ProgressError::storeFailed;
    }
    if (!plan.ok()) {
        plan.result.error = plan.error;
        log::write("skills: %s of character %s refused: %s", what, characterId.c_str(), progress_error_text(plan.error));
        return plan.result;
    }
    return finish(plan, what);
}

void Skills::clear_tree(SkillSet& skills, std::uint32_t table, Touched& touched) const {
    for (auto& [node, state] : skills.pools[table]) {
        if (state.level != 0) {
            state.level = 0;
            touched.nodes.emplace_back(table, node);
        }
    }
    skills.bars.erase(table);
    touched.points = true;
    touched.bars = true;
}

ProgressError Skills::node_condition(const SkillSet& skills, const gamedata::AbilityNode& node) const {
    if (node.required != 0 && node_level(skills, node.table, node.required) == 0) {
        return ProgressError::nodeLocked;
    }
    // The points of the character in the row before the node, and in the tree of the node.
    std::uint32_t inRowBefore = 0;
    std::uint32_t inTree = 0;
    const auto nodes = skills.pools.find(node.table);
    if (nodes != skills.pools.end()) {
        for (const auto& [id, state] : nodes->second) {
            const gamedata::AbilityNode* other = tables()->node(id);
            if (state.level == 0 || other == nullptr || other->tree != node.tree) {
                continue;
            }
            inTree += state.level;
            if (other->row + 1 == node.row) {
                inRowBefore += state.level;
            }
        }
    }
    if (node.row > 0 && node.row - 1 <= kLastRowWithRequirement && inRowBefore < kRowPoints) {
        return ProgressError::nodeLocked;
    }
    if (node.row == tables()->last_row(node.table, node.tree) && inTree < kLastRowPoints) {
        return ProgressError::nodeLocked;
    }
    return ProgressError::none;
}

SkillResult Skills::set_rank(const std::string& characterId,
                             std::string_view id,
                             gamedata::ProgressionKind kind,
                             std::uint32_t rank,
                             std::uint64_t points) {
    const std::string what = "rank " + std::to_string(rank) + " of " + std::string(id);
    return change(characterId, what.c_str(), [&](SkillSet& skills, Touched& touched, SkillResult& result) {
        return rank_into(skills, touched, result, id, kind, rank, points);
    });
}

ProgressError Skills::rank_into(SkillSet& skills,
                                Touched& touched,
                                SkillResult& result,
                                std::string_view id,
                                gamedata::ProgressionKind kind,
                                std::uint32_t rank,
                                std::uint64_t points) const {
    const gamedata::ProgressionRow* row = tables()->progression(id);
    if (row == nullptr) {
        return ProgressError::unknownProgression;
    }
    if (row->kind != kind) {
        return ProgressError::wrongKind;
    }
    // The experience of a rank is below the experience that gives the next rank. The
    // largest rank has no experience.
    // The largest rank is the rank of the owned expansion (stage 1.1b: Riding 250, a trade skill 250).
    const std::uint32_t cap = tables()->rank_cap(*row);
    const std::uint64_t limit = rank < cap && rank < row->ranks.experience.size() ? row->ranks.experience[rank] : 0;
    if (rank > cap || (points != 0 && points >= limit)) {
        return ProgressError::badRank;
    }
    if (rank == 0 && points == 0) {
        skills.progressions.erase(row->wireId);
    } else {
        skills.progressions[row->wireId] = ProgressionRank{static_cast<std::uint16_t>(rank), points};
    }
    touched.ranks = true;
    if (row->kind == gamedata::ProgressionKind::weapon) {
        // The free points of the weapon follow from its rank.
        touched.points = true;
        if (skills.spent_points(row->wireId) > rank) {
            clear_tree(skills, row->wireId, touched);
        }
        result.freePoints = skills.free_points(row->wireId);
    }
    result.rank = rank;
    result.points = points;
    return ProgressError::none;
}

SkillResult Skills::add_experience(const std::string& characterId, std::string_view id, std::uint64_t amount) {
    const std::string what = "gain of " + std::to_string(amount) + " experience of " + std::string(id);
    return change(characterId, what.c_str(), [&](SkillSet& skills, Touched& touched, SkillResult& result) {
        return experience_into(skills, touched, result, id, amount);
    });
}

Skills::Plan Skills::plan_experience(const std::string& characterId, std::string_view id, std::uint64_t amount) {
    return stage(characterId, [&](SkillSet& skills, Touched& touched, SkillResult& result) {
        return experience_into(skills, touched, result, id, amount);
    });
}

Skills::Plan Skills::plan_none(const std::string& characterId) {
    return stage(characterId, [](SkillSet&, Touched&, SkillResult&) { return ProgressError::none; });
}

ProgressError Skills::add_balance(Plan& plan, std::string_view id, std::int64_t delta, std::uint64_t cap) const {
    if (!plan.ok()) {
        return plan.error;
    }
    const std::uint32_t wire = gamedata::progression_id(id);
    const auto found = plan.skills.progressions.find(wire);
    const std::uint64_t points = found != plan.skills.progressions.end() ? found->second.points : 0;
    if (delta < 0 && points < static_cast<std::uint64_t>(-delta)) {
        return ProgressError::notEnoughBalance;
    }
    const std::uint64_t next = delta < 0 ? points - static_cast<std::uint64_t>(-delta)
                                         : std::min<std::uint64_t>(points + static_cast<std::uint64_t>(delta), std::max(cap, points));
    if (next == 0) {
        plan.skills.progressions.erase(wire);
    } else {
        plan.skills.progressions[wire] = ProgressionRank{0, next};
    }
    plan.touched.ranks = true;
    return ProgressError::none;
}

SkillResult Skills::set_balance(const std::string& characterId, std::string_view id, std::uint64_t amount, std::uint64_t cap) {
    return change(characterId, "a balance command", [&](SkillSet& skills, Touched& touched, SkillResult& result) {
        if (amount > cap) {
            return ProgressError::badAmount;
        }
        const std::uint32_t wire = gamedata::progression_id(id);
        if (amount == 0) {
            skills.progressions.erase(wire);
        } else {
            skills.progressions[wire] = ProgressionRank{0, amount};
        }
        result.points = amount;
        touched.ranks = true;
        return ProgressError::none;
    });
}

std::uint64_t Skills::balance(const SkillSet& skills, std::string_view id) {
    const auto found = skills.progressions.find(gamedata::progression_id(id));
    return found != skills.progressions.end() ? found->second.points : 0;
}

ProgressError Skills::experience_into(SkillSet& skills, Touched& touched, SkillResult& result, std::string_view id,
                                      std::uint64_t amount) const {
    const gamedata::ProgressionRow* row = tables()->progression(id);
    if (row == nullptr) {
        return ProgressError::unknownProgression;
    }
    if (amount == 0 || amount > kSkillExperienceLimit) {
        return ProgressError::badAmount;
    }
    const auto found = skills.progressions.find(row->wireId);
    ProgressionRank value = found == skills.progressions.end() ? ProgressionRank{} : found->second;
    const std::uint16_t before = value.rank;
    value.points += amount;
    // The counter is wider than the rank on the wire, and the largest rank is a rank on the
    // wire: the loop has an end for each table, also for one that no loader checked.
    const std::uint32_t largest = std::min(tables()->rank_cap(*row), gamedata::kLargestRank);
    std::uint32_t rank = value.rank;
    while (rank < largest && rank < row->ranks.experience.size() && value.points >= row->ranks.experience[rank]) {
        value.points -= row->ranks.experience[rank];
        ++rank;
    }
    value.rank = static_cast<std::uint16_t>(rank);
    if (rank >= largest) {
        value.points = 0;
    }
    if (value.rank == 0 && value.points == 0) {
        skills.progressions.erase(row->wireId);
    } else {
        skills.progressions[row->wireId] = value;
    }
    touched.ranks = true;
    if (row->kind == gamedata::ProgressionKind::weapon) {
        touched.points = touched.points || value.rank != before;
        result.freePoints = skills.free_points(row->wireId);
    }
    result.rank = value.rank;
    result.points = value.points;
    return ProgressError::none;
}

SkillResult Skills::assign(const std::string& characterId, std::uint32_t pool, std::uint32_t node, std::uint32_t count) {
    char what[96];
    std::snprintf(what, sizeof(what), "mastery point for node %08X of pool %08X", node, pool);
    const SkillResult result = change(characterId, what, [&](SkillSet& skills, Touched& touched, SkillResult& done) {
        return assign_into(skills, touched, done, pool, node, count);
    });
    if (!result.ok()) {
        // The client has its own result of the request already: the state puts its values
        // back. A node that the character does not have goes out with the level 0.
        if (Entity* entity = m_world.find_character(characterId)) {
            const Revision revision = m_world.touch(entity->id, ComponentKey::points);
            const gamedata::AbilityNode* row = tables() != nullptr ? tables()->node(node) : nullptr;
            if (row != nullptr && row->table == pool) {
                TreeNode& state = entity->skills.pools[pool][node];
                if (state.level == 0) {
                    state.changed = revision;
                }
            }
        }
    }
    return result;
}

ProgressError Skills::assign_into(SkillSet& skills,
                                  Touched& touched,
                                  SkillResult& done,
                                  std::uint32_t pool,
                                  std::uint32_t node,
                                  std::uint32_t count) const {
    const gamedata::ProgressionRow* weapon = tables()->progression(pool);
    if (weapon == nullptr) {
        return ProgressError::unknownProgression;
    }
    if (weapon->kind != gamedata::ProgressionKind::weapon) {
        return ProgressError::wrongKind;
    }
    const gamedata::AbilityNode* row = tables()->node(node);
    if (row == nullptr || row->table != pool) {
        return ProgressError::unknownNode;
    }
    // A node of a weapon tree takes one point.
    if (count != 1) {
        return ProgressError::badAmount;
    }
    if (node_level(skills, pool, node) != 0) {
        return ProgressError::nodeOwned;
    }
    if (skills.free_points(pool) < count) {
        return ProgressError::notEnoughPoints;
    }
    const ProgressError condition = node_condition(skills, *row);
    if (condition != ProgressError::none) {
        return condition;
    }
    skills.pools[pool][node].level = 1;
    touched.points = true;
    touched.nodes.emplace_back(pool, node);
    done.rank = skills.rank(pool);
    done.freePoints = skills.free_points(pool);
    return ProgressError::none;
}

SkillResult Skills::set_bar(const std::string& characterId, std::uint32_t table, std::uint32_t slot, std::uint32_t ability) {
    char what[112];
    std::snprintf(what, sizeof(what), "ability %08X on place %u of the bar of weapon %08X", ability, slot, table);
    const SkillResult result = change(characterId, what, [&](SkillSet& skills, Touched& touched, SkillResult&) {
        return bar_into(skills, touched, table, slot, ability);
    });
    if (!result.ok()) {
        // The client changed its own bar before it sent the request. The state merge of the
        // client applies a bar only when the state is different from the last state of the
        // server, and the bars of the entity did not change: the encoder sends the state in
        // two forms to a session that had the bars before this revision (skill_states.cpp).
        if (Entity* entity = m_world.find_character(characterId)) {
            entity->skills.barsRefused = m_world.touch(entity->id, ComponentKey::abilities);
        }
    }
    return result;
}

ProgressError Skills::bar_into(SkillSet& skills, Touched& touched, std::uint32_t table, std::uint32_t slot, std::uint32_t ability) const {
    const gamedata::ProgressionRow* weapon = tables()->progression(table);
    if (weapon == nullptr) {
        return ProgressError::unknownProgression;
    }
    if (weapon->kind != gamedata::ProgressionKind::weapon) {
        return ProgressError::wrongKind;
    }
    if (slot >= kBarSlots) {
        return ProgressError::badSlot;
    }
    if (ability != 0) {
        const gamedata::AbilityNode* row = tables()->node(ability);
        if (row == nullptr || row->table != table || !row->active) {
            return ProgressError::notActive;
        }
        if (node_level(skills, table, ability) == 0) {
            return ProgressError::notOwned;
        }
    }
    std::array<std::uint32_t, kBarSlots> bar{};
    const auto found = skills.bars.find(table);
    if (found != skills.bars.end()) {
        bar = found->second;
    }
    if (ability != 0) {
        // An ability has one place: it leaves the place that it had.
        std::replace(bar.begin(), bar.end(), ability, 0U);
    }
    bar[slot] = ability;
    if (bar == std::array<std::uint32_t, kBarSlots>{}) {
        skills.bars.erase(table);
    } else {
        skills.bars[table] = bar;
    }
    touched.bars = true;
    return ProgressError::none;
}

ProgressError Skills::trees_into(SkillSet& skills, Touched& touched, const std::vector<TreeBuild>& trees) const {
    for (const TreeBuild& tree : trees) {
        const gamedata::ProgressionRow* weapon = tables()->progression(tree.table);
        if (weapon == nullptr) {
            return ProgressError::unknownProgression;
        }
        SkillResult ignored;
        // The rank first (set_rank), then the reset of the tree with no cost: the build replaces the tree. A rank of 0 frees the points
        // of a tree in set_rank too; here each tree of the build starts with no node and an empty bar.
        if (const ProgressError error = rank_into(skills, touched, ignored, tree.table, gamedata::ProgressionKind::weapon, tree.rank, 0);
            error != ProgressError::none) {
            return error;
        }
        clear_tree(skills, weapon->wireId, touched);
        for (const std::string& name : tree.nodes) {
            const gamedata::AbilityNode* node = tables()->node(name);
            if (node == nullptr) {
                return ProgressError::unknownNode;
            }
            if (const ProgressError error = assign_into(skills, touched, ignored, weapon->wireId, node->wireId, 1);
                error != ProgressError::none) {
                return error;
            }
        }
        for (std::uint32_t slot = 0; slot < kBarSlots; ++slot) {
            const std::string& name = tree.bar[slot];
            const gamedata::AbilityNode* node = name.empty() ? nullptr : tables()->node(name);
            if (!name.empty() && node == nullptr) {
                return ProgressError::notActive;
            }
            if (const ProgressError error = bar_into(skills, touched, weapon->wireId, slot, node != nullptr ? node->wireId : 0);
                error != ProgressError::none) {
                return error;
            }
        }
    }
    return ProgressError::none;
}

Skills::Plan Skills::plan_trees(const std::string& characterId, const std::vector<TreeBuild>& trees) {
    return stage(characterId, [&](SkillSet& skills, Touched& touched, SkillResult&) { return trees_into(skills, touched, trees); });
}

ProgressError Skills::check_trees(const std::vector<TreeBuild>& trees) const {
    if (tables() == nullptr) {
        return ProgressError::noTables;
    }
    SkillSet skills;
    Touched touched;
    return trees_into(skills, touched, trees);
}

SkillResult Skills::reset_tree(const std::string& characterId, std::uint32_t table) {
    char what[64];
    std::snprintf(what, sizeof(what), "reset of the tree of weapon %08X", table);
    return change(characterId, what, [&](SkillSet& skills, Touched& touched, SkillResult& result) {
        const gamedata::ProgressionRow* weapon = tables()->progression(table);
        if (weapon == nullptr) {
            return ProgressError::unknownProgression;
        }
        if (weapon->kind != gamedata::ProgressionKind::weapon) {
            return ProgressError::wrongKind;
        }
        // The cost is in the currency "Azoth", which the backend does not have.
        // A rank with no cost row has a cost that is not known: the reset is refused too.
        const std::uint32_t rank = skills.rank(table);
        if (rank >= weapon->ranks.resetCost.size() || weapon->ranks.resetCost[rank] != 0) {
            return ProgressError::costNotPaid;
        }
        clear_tree(skills, table, touched);
        result.rank = rank;
        result.freePoints = skills.free_points(table);
        return ProgressError::none;
    });
}

} // namespace soulwarden::backend::world
