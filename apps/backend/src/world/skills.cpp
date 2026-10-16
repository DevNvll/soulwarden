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
SkillResult Skills::change(const std::string& characterId, const char* what, Change&& apply) {
    SkillResult result;
    Entity* entity = m_world.find_character(characterId);
    // The log line names the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    SkillSet skills;
    if (entity != nullptr) {
        skills = entity->skills;
        result.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        skills = read_skills(characterId);
    } else {
        result.error = ProgressError::unknownCharacter;
    }
    if (result.ok() && tables() == nullptr) {
        result.error = ProgressError::noTables;
    }
    // The change is made on a copy: the skills of the entity change only after the rows.
    Touched touched;
    if (result.ok()) {
        result.error = apply(skills, touched, result);
    }
    // All rows of the change are one transaction.
    if (result.ok() && (touched.ranks || touched.points || touched.bars)
        && !m_store.write_skills(characterId, rows_of(skills))) {
        result.error = ProgressError::storeFailed;
    }
    if (!result.ok()) {
        log::write("skills: %s of character %s refused: %s", what, characterId.c_str(), progress_error_text(result.error));
        return result;
    }
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
            m_world.abilities().cancel_aim(characterId);
        }
        if (touched.points) {
            // The fixed stats of the learned nodes are gear values (Passives::fixed, Gear::values): a change of the nodes (an
            // assignment, a reset of a tree) refreshes them after the stored change (code review of stage 3.3b-S5, finding F5).
            m_world.gear().refresh(*entity);
        }
    }
    log::write("skills: %s of character %s: done (%s)",
               what,
               characterId.c_str(),
               entity != nullptr ? "the character is in the world" : "the rows only");
    return result;
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
    });
}

SkillResult Skills::add_experience(const std::string& characterId, std::string_view id, std::uint64_t amount) {
    const std::string what = "gain of " + std::to_string(amount) + " experience of " + std::string(id);
    return change(characterId, what.c_str(), [&](SkillSet& skills, Touched& touched, SkillResult& result) {
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
    });
}

SkillResult Skills::assign(const std::string& characterId, std::uint32_t pool, std::uint32_t node, std::uint32_t count) {
    char what[96];
    std::snprintf(what, sizeof(what), "mastery point for node %08X of pool %08X", node, pool);
    const SkillResult result = change(characterId, what, [&](SkillSet& skills, Touched& touched, SkillResult& done) {
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

SkillResult Skills::set_bar(const std::string& characterId, std::uint32_t table, std::uint32_t slot, std::uint32_t ability) {
    char what[112];
    std::snprintf(what, sizeof(what), "ability %08X on place %u of the bar of weapon %08X", ability, slot, table);
    const SkillResult result = change(characterId, what, [&](SkillSet& skills, Touched& touched, SkillResult&) {
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
