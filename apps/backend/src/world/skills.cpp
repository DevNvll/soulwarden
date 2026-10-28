#include "world/skills.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** Largest rank that the wire form holds. */
constexpr std::uint32_t kLargestRank = 0xFFFF;
/** Stage QG1: the largest standing of one gain (the client truncates the f32 to an i32: 0x31F7D37). */
constexpr std::uint64_t kLargestStandingGain = 0x7FFFFF80;

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
        if (row.level == 0) {
            continue;
        }
        // Stage QG1: a row of a territory pool is a card with its level. Code review of stage QG1, finding QG1R-R1: a card that the point data
        // does not have (a file of the format 3) is not loaded, and it stays a row: the next write keeps it (KeptCard).
        if (tables() != nullptr && tables()->standing_of_pool(row.pool) != nullptr) {
            if (const gamedata::TerritoryCard* card = tables()->territory_card(row.node)) {
                skills.territoryPools[row.pool].cards[row.node] = static_cast<std::uint16_t>(std::min(row.level, card->maxLevel));
            } else {
                skills.keptCards.push_back(KeptCard{row.pool, row.node, row.name, row.level});
            }
            continue;
        }
        skills.pools[row.pool][row.node].level = 1;
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
        add_territory_pools(skills);
    }
    return skills;
}

void Skills::add_territory_pools(SkillSet& skills) const {
    // Stage QG1: each territory pool of the standings, also with no card (the client removes a pool that a points state does not have:
    // 0x3CF1880). The spent tokens are the levels of the cards with no DoNotSpendPoint (the count helper of the client 0x3CF0600).
    for (const gamedata::StandingRow& standing : tables()->standings()) {
        if (standing.poolId == 0) {
            continue;
        }
        TerritoryPool& pool = skills.territoryPools[standing.poolId];
        pool.standing = standing.wireId;
        pool.spent = 0;
        for (const auto& [id, level] : pool.cards) {
            const gamedata::TerritoryCard* card = tables()->territory_card(id);
            pool.spent += card != nullptr && !card->doNotSpend ? level : 0;
        }
    }
}

state::SkillRows Skills::rows_of(const SkillSet& skills) const {
    state::SkillRows rows;
    const auto progression_name = [&](std::uint32_t id) {
        const gamedata::ProgressionRow* row = tables() != nullptr ? tables()->progression(id) : nullptr;
        if (row == nullptr && id == gamedata::progression_id(kAzothCurrency)) {
            return std::string(kAzothCurrency); // Stage 3.7c: the balance of Azoth has no row of the progression tables.
        }
        if (const gamedata::StandingRow* standing = row == nullptr && tables() != nullptr ? tables()->standing_of(id) : nullptr) {
            return standing->id; // Stage 1.8d: the standing of a territory ("6").
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
    // Stage QG1: the cards of the territory pools are rows of ability_nodes with the pool of the territory and the level of the card.
    for (const auto& [pool, territory] : skills.territoryPools) {
        for (const auto& [card, level] : territory.cards) {
            const gamedata::TerritoryCard* row = tables() != nullptr ? tables()->territory_card(card) : nullptr;
            if (level != 0) {
                rows.nodes.push_back({pool, card, row != nullptr ? row->id : std::string{}, level});
            }
        }
    }
    // Code review of stage QG1, finding QG1R-R1: the stored cards that the loaded data cannot read, as they were.
    for (const KeptCard& kept : skills.keptCards) {
        rows.nodes.push_back({kept.pool, kept.card, kept.name, kept.level});
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
    // Stage QG1: the GainXP card of the territory of a character in the world (the experience of a standing rank).
    plan.experienceBonus = entity != nullptr && plan.ok() && tables() != nullptr ? experience_card(*entity) : 0.0F;
    // The change is made on a copy: the skills of the entity change only after the rows.
    if (plan.ok()) {
        plan.error = apply(plan.skills, plan.touched, plan.result);
    }
    return plan;
}

bool Skills::write(const Plan& plan) {
    // All rows of the change are one transaction.
    const Touched& touched = plan.touched;
    return !(touched.ranks || touched.points || touched.bars || touched.tokens) || m_store.write_skills(plan.characterId, rows_of(plan.skills));
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
        if (touched.tokens && !touched.points) {
            // Stage QG1: a token or a card of a territory pool: the points state (one fragment with each pool and each entry), and no
            // change of a weapon tree (no aim end, no gear refresh).
            m_world.touch(entity->id, ComponentKey::points);
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

ProgressError Skills::add_standing(Plan& plan, std::uint16_t territory, std::uint64_t amount, StandingGain* gain) const {
    if (!plan.ok()) {
        return plan.error;
    }
    if (tables() == nullptr) {
        return ProgressError::noTables;
    }
    const gamedata::StandingRow* row = tables()->standing(territory);
    if (row == nullptr) {
        return ProgressError::unknownProgression;
    }
    StandingGain result;
    result.base = amount;
    const auto found = plan.skills.progressions.find(row->wireId);
    ProgressionRank value = found != plan.skills.progressions.end() ? found->second : ProgressionRank{};
    // The GainStanding card of the territory pool (client 0x31F7D19 to 0x31F7D37: (bonus + 1) times the standing in f32, times the
    // multiplier of the event (1: the backend has no other), truncated to a whole number).
    result.bonus = territory_bonus(plan.skills, territory, gamedata::TerritoryBonus::gainStanding);
    const float scaled = (result.bonus + 1.0F) * static_cast<float>(std::min<std::uint64_t>(amount, kLargestStandingGain));
    result.points = scaled >= static_cast<float>(kLargestStandingGain) ? kLargestStandingGain : static_cast<std::uint64_t>(scaled);
    // The largest rank (300: MaximumInfluence 0, AllowOverflow 0): no more points (0x3CAACF0 takes no gain at the cap).
    const std::uint32_t largest = std::min<std::uint32_t>(row->maxRank, static_cast<std::uint32_t>(row->ranks.experience.size() - 1));
    if (result.points != 0 && value.rank < largest) {
        value.points += result.points;
        // AutoRankUp (0x3CAAF15: equality too): the threshold goes, the rank goes up, and the rest goes on to the next rank (0x3CAB113).
        while (value.rank < largest && value.points >= row->ranks.experience[value.rank]) {
            if (!row->autoRankUp) {
                return ProgressError::standingRankUp;
            }
            value.points -= row->ranks.experience[value.rank];
            ++value.rank;
            ++result.ranks;
            // The character experience of the new rank (0x31FD650, 0x31FD6AC): XpReward of the rank times TerritoryStandingXpModifier (a
            // modifier of 0 is no multiplication, 0x31FD6B4), the multiplier of the game event CategoricalProgressionRankUp
            // (UniversalExpAmount 1, truncated: 0x31F6816), then the GainXP card of the character (0x3CF04A9, truncated).
            const std::uint32_t reward = value.rank < row->xpReward.size() ? row->xpReward[value.rank] : 0;
            const float scaled = row->xpModifier != 0.0F ? static_cast<float>(reward) * row->xpModifier : static_cast<float>(reward);
            const auto base = static_cast<std::uint64_t>(scaled);
            result.experience += plan.experienceBonus == 0.0F
                                     ? base
                                     : static_cast<std::uint64_t>(static_cast<float>(base) * (1.0F + plan.experienceBonus));
        }
        if (value.rank >= largest) {
            value.points = 0;
        }
        if (value.rank == 0 && value.points == 0) {
            plan.skills.progressions.erase(row->wireId);
        } else {
            plan.skills.progressions[row->wireId] = value;
        }
        plan.touched.ranks = true;
        // A new rank is a new token of the territory pool (0x3CB7500): the points state (slot 67) too.
        plan.touched.tokens = plan.touched.tokens || result.ranks > 0;
        plan.rankExperience += result.experience;
    } else {
        result.points = 0;
    }
    result.rank = value.rank;
    result.rankPoints = value.points;
    if (gain != nullptr) {
        *gain = result;
    }
    return ProgressError::none;
}

float Skills::territory_bonus(const SkillSet& skills, std::uint16_t territory, gamedata::TerritoryBonus kind) const {
    const gamedata::StandingRow* standing = tables() != nullptr && territory != 0 ? tables()->standing(territory) : nullptr;
    const gamedata::TerritoryCard* card = standing != nullptr ? tables()->territory_card(kind) : nullptr;
    if (card == nullptr || standing->poolId == 0) {
        return 0.0F;
    }
    const auto pool = skills.territoryPools.find(standing->poolId);
    const auto level = pool != skills.territoryPools.end() ? pool->second.cards.find(card->wireId) : decltype(pool->second.cards.end()){};
    return pool != skills.territoryPools.end() && level != pool->second.cards.end() ? gamedata::card_bonus(*card, level->second) : 0.0F;
}

float Skills::experience_card(const Entity& entity) const {
    return territory_bonus(entity.skills, m_world.territory_of(entity), gamedata::TerritoryBonus::gainXp);
}

std::uint64_t Skills::card_experience(const std::string& characterId, std::uint64_t amount) const {
    const Entity* entity = amount != 0 && tables() != nullptr ? m_world.find_character(characterId) : nullptr;
    const float bonus = entity != nullptr ? experience_card(*entity) : 0.0F;
    if (bonus == 0.0F) {
        return amount; // No card: the amount itself (an f32 product would change an amount above 2^24).
    }
    // The multiplier M = 1 + GainXP (0x3CEC0CA: the card is one additive term), then trunc(f32(amount * M)) (0x3CF04A9, 0x3CF49C4).
    const float experience = static_cast<float>(amount) * (1.0F + bonus);
    return experience >= static_cast<float>(kExperienceLimit) ? kExperienceLimit : static_cast<std::uint64_t>(experience);
}

SkillResult Skills::spend_territory_point(const std::string& characterId, std::uint32_t progressionId, std::uint32_t pointId) {
    char what[96];
    std::snprintf(what, sizeof(what), "card %08X of the territory standing %08X", pointId, progressionId);
    const SkillResult result = change(characterId, what, [&](SkillSet& skills, Touched& touched, SkillResult& done) {
        return card_into(skills, touched, done, progressionId, pointId);
    });
    if (!result.ok()) {
        // The client can show its own result of the choice already (the Lua callback reports success after the send): the points state
        // goes again with the values of the server.
        if (Entity* entity = m_world.find_character(characterId)) {
            m_world.touch(entity->id, ComponentKey::points);
        }
    }
    return result;
}

ProgressError Skills::card_into(SkillSet& skills, Touched& touched, SkillResult& result, std::uint32_t progressionId,
                                std::uint32_t pointId) const {
    // progressionId is the CRC-32 of the territory id text (RedeemTerritoryReward of territorydatahandler.lua; the sender 0x3CD8E80).
    const gamedata::StandingRow* standing = tables()->standing_of(progressionId);
    if (standing == nullptr || standing->poolId == 0) {
        return ProgressError::unknownProgression;
    }
    const gamedata::TerritoryCard* card = tables()->territory_card(pointId);
    if (card == nullptr) {
        return ProgressError::unknownCard;
    }
    TerritoryPool& pool = skills.territoryPools[standing->poolId];
    pool.standing = standing->wireId;
    // The checks of CanSpend in the order of the client (0x3CE7490; the server handler 0x3CFD510 calls it again, static read
    // evidence/quests/qg1/implement/static/ANSWERS.md Q5). The card screen offers the cards of the rank row whose index is the count of the
    // spent levels (0x3CE7501 to 0x3CE7538).
    if (pool.spent >= standing->offers.size()
        || std::find(standing->offers[pool.spent].begin(), standing->offers[pool.spent].end(), card->wireId) == standing->offers[pool.spent].end()) {
        return ProgressError::cardNotOffered;
    }
    // A free token, also for a card with DoNotSpendPoint (TB_PurchaseHouse): the check 0x3CE7564 has no exemption.
    if (skills.free_tokens(standing->poolId) == 0) {
        return ProgressError::noToken;
    }
    // The requirement evaluator (0x3CF3770): RequiredCategoricalProgressionLevel against the standing rank of the pool.
    if (skills.rank(standing->wireId) < card->requiredRank) {
        return ProgressError::standingTooLow;
    }
    const auto found = pool.cards.find(card->wireId);
    const std::uint32_t level = found != pool.cards.end() ? found->second : 0;
    if (level >= card->maxLevel) {
        return ProgressError::cardAtMaxLevel;
    }
    // A card with DoNotSpendPoint spends no token: its level goes up, and the free count stays (0x3CE6076 to 0x3CE60C3; the loader
    // 0x3CE0370 stores the spend flag as the inverse of DoNotSpendPoint).
    pool.cards[card->wireId] = static_cast<std::uint16_t>(level + 1);
    pool.spent += card->doNotSpend ? 0 : 1;
    touched.tokens = true;
    result.rank = level + 1;
    result.freePoints = skills.free_tokens(standing->poolId);
    return ProgressError::none;
}

StandingResult Skills::give_standing(const std::string& characterId, std::uint16_t territory, std::uint64_t amount) {
    Entity* entity = m_world.find_character(characterId);
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    StandingResult result;
    Plan plan = plan_none(characterId);
    if (plan.ok()) {
        plan.error = add_standing(plan, territory, amount, &result.gain);
    }
    // The experience of the ranks: one plan of the progress system in the same transaction (the pattern of the hand-in of a quest).
    std::optional<Progress::Plan> sheet;
    if (plan.ok() && plan.rankExperience > 0) {
        sheet = m_world.progress().plan_experience(characterId, std::min(plan.rankExperience, kExperienceLimit));
        if (!sheet->ok()) {
            plan.error = sheet->error;
        }
    }
    if (plan.ok()) {
        state::Store::Batch batch(m_store);
        const bool ok = batch.open() && write(plan) && (!sheet || m_world.progress().write(*sheet)) && batch.commit();
        if (!ok) {
            plan.error = ProgressError::storeFailed;
        }
    }
    result.error = plan.error;
    result.inWorld = plan.inWorld;
    if (!plan.ok()) {
        log::write("skills: a standing of %llu for the territory %u of character %s refused: %s", static_cast<unsigned long long>(amount),
                   static_cast<unsigned>(territory), characterId.c_str(), progress_error_text(plan.error));
        return result;
    }
    if (sheet) {
        const ProgressResult experience = m_world.progress().finish(*sheet, "the experience of a standing rank");
        result.level = experience.level;
        result.levelExperience = experience.experience;
    } else if (const std::optional<ProgressView> current = m_world.progress().view(characterId)) {
        // Code review of stage QG1, finding QG1R-R3: a gain with no rank experience reports the level and the experience of the character.
        result.level = current->progression.level;
        result.levelExperience = current->progression.experience;
    }
    const gamedata::StandingRow* row = tables()->standing(territory);
    result.tokens = row != nullptr ? plan.skills.free_tokens(row->poolId) : 0;
    (void)finish(plan, "a territory standing");
    log::write("skills: character %s got %llu standing of the territory %u (%llu with the card bonus %.4f): rank %u, %llu points, %u new "
               "rank(s), %u free token(s), %llu experience",
               characterId.c_str(), static_cast<unsigned long long>(amount), static_cast<unsigned>(territory),
               static_cast<unsigned long long>(result.gain.points), static_cast<double>(result.gain.bonus), result.gain.rank,
               static_cast<unsigned long long>(result.gain.rankPoints), result.gain.ranks, result.tokens,
               static_cast<unsigned long long>(result.gain.experience));
    return result;
}

ProgressionRank Skills::standing(const SkillSet& skills, std::uint16_t territory) {
    const auto found = skills.progressions.find(gamedata::progression_id(std::to_string(territory)));
    return found != skills.progressions.end() ? found->second : ProgressionRank{};
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
