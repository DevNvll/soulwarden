#include <algorithm>

#include "gamedata/progression_tables.h"
#include "server/rep/replication.h"
#include "server/rep/states/state_codecs.h"
#include "world/skills.h"

// The three states of the skills of a character (protocol notes 4.13). Each one has one group,
// and only the owner gets it: the remote player slice has no facet for the three components.
//
// A list field (a replicated vector) in the full form:
//   00 (no change entries), stamp, count:size, the elements
//   stamp := 01 sequence:size64. The backend sends the world revision of the change.
// The state merge of the client keeps its list when the stamp of a fragment is not larger than
// the stamp of the list that it has (merge function of the point entries at image RVA
// 0x3CF3960). A list with a larger stamp replaces the list of the client.
//
// MB::CategoricalProgressionReplicatedState (constructor at image RVA 0x3CA3CA0), slot 65
// (component CategoricalProgression of the entity "GameEvents"): the rank and the experience
// of each weapon mastery and trade skill.
//   0 progressionIds  list of u32    1 ranks  list of u16    2 points  list of u64
// The three lists are parallel, and the client facet (apply at image RVA 0x3CB7170) uses them
// only when they have the same length. It makes its map again from each fragment: a fragment
// has each entry of the character. An id that is not in the lists has the rank 0.
//
// MB::ProgressionPointReplicatedState (constructor at image RVA 0x3CDFA30), slot 67 (component
// ProgressionPoint of the entity "GameEvents"): the point pool of each weapon and the nodes of
// its tree.
//   0 poolIds  list of u32    1 poolCounts  list of u16 (the free points)
//   2 poolCategories  list of u8 (1: a pool of a player)
//   3 pointEntries  list of poolId:u32 pointId:u32 level:u16
// The client facet (apply at image RVA 0x3CF1880) removes a pool that is not in the lists, and
// it gives the selected levels of each pool back when the list of the point entries is empty
// (image RVA 0x3CFBA80). The list of the point entries of a fragment replaces the list of the
// client, and the tree window of the client shows only the nodes of that list (live, stage
// 1.7: a state that the backend cut into two fragments showed only the nodes of the second
// fragment, and with one stamp for the two fragments only the nodes of the first one). Thus
// the backend never cuts this state: one fragment has each pool of the character and each
// node, also when it is larger than the fragment budget of a session. A character with each
// weapon at the largest rank has 304 nodes (3.2 kilobytes). The session sends a fragment of
// that size in a bundle of its own, and the carrier sends a bundle above one carrier message
// in parts (live, stage 1.7: 114 nodes in one bundle of 1,3 kilobytes).
// A node that the character lost goes out one time with the level 0.
//
// MB::AbilityComponentReplicatedState (constructor at image RVA 0x66EA770), slot 46 (component
// Ability of the entity "Stats"): field 0, `persistentAbilityData`, the ability bar of each
// weapon.
//   tables:size ( tableId:u32 slots:size ( abilityId:u32 button:u32 )* )*
//   versions:size ( tableId:u32 version:u32 )*
// The client finds the bar of a weapon by the index of its ability table in the list of its
// ability data manager, not by the table id: the list has one element for each ability table
// of the client, in that sequence, and each element has three places. A fragment with fewer
// elements makes the client read memory that is not a bar.
// The state merge of the client (image RVA 0x6860A30, compare function 0x66277F0) gives the
// field to the facet only when a table id, an ability, or a button number is different from
// the last field of the server. After a bar request that the backend refused, the bars of the
// server are the same as before, and the client has its own bar: the backend then sends the
// state two times, first with the place numbers 0, 1, 2 as the button numbers and then with
// the numbers of the client. Each one is different from the field before it, thus the facet
// applies the bars of the server. The client shows the same bar for the two forms (live proof
// of stage 1.6).
//
// View     Fields
// owner    slot 65: the three lists. Slot 67: the four lists. Slot 46: field 0 (no fragment in
//          the first record of a character with no ability on a bar; two fragments after a bar
//          request that the backend refused).
// observer no fragment.

namespace soulwarden::backend::server::rep {
namespace {

constexpr unsigned kGroups = 1;
// Slot 65.
constexpr unsigned kProgressionIds = 0;
constexpr unsigned kRanks = 1;
constexpr unsigned kPoints = 2;
// Slot 67.
constexpr unsigned kPoolIds = 0;
constexpr unsigned kPoolCounts = 1;
constexpr unsigned kPoolCategories = 2;
constexpr unsigned kPointEntries = 3;
/** `Javelin::PoolCategory`: a pool of a player (2 is a pool of a territory). */
constexpr std::uint8_t kPoolCategoryPlayer = 1;
/** Largest count of the pool lists and of the entry list that the client reads. */
constexpr std::size_t kMaxPools = 50;
constexpr std::size_t kMaxPointEntries = 1000;
// Slot 46.
constexpr unsigned kPersistentAbilityData = 0;
/**
 * The input numbers of the three ability keys of a player ("Ability1" to "Ability3"). The
 * client writes them into each place of its own list (client code at image RVA 0x6870BB0):
 * the values of the live client of the supported build, component +0x6A8.
 */
constexpr std::uint32_t kAbilityButtons[world::kBarSlots] = {0x192, 0x73, 0x8B};

/** Starts a list field in the full form. */
void begin_list(hub::Writer& writer, std::uint64_t sequence, std::size_t count) {
    writer.size(0);
    writer.u8(1);
    writer.size64(sequence);
    writer.size(static_cast<std::uint32_t>(count));
}

} // namespace

hub::Bytes encode_categorical(const world::SkillSet& skills, std::uint64_t sequence) {
    std::size_t count = 0;
    for (const auto& [id, value] : skills.progressions) {
        count += value.rank != 0 || value.points != 0 ? 1 : 0;
    }
    StateWriter state(kGroups);
    hub::Writer& ids = state.field(0, kProgressionIds);
    hub::Writer& ranks = state.field(0, kRanks);
    hub::Writer& points = state.field(0, kPoints);
    begin_list(ids, sequence, count);
    begin_list(ranks, sequence, count);
    begin_list(points, sequence, count);
    for (const auto& [id, value] : skills.progressions) {
        if (value.rank == 0 && value.points == 0) {
            continue;
        }
        ids.u32(id);
        ranks.u16(value.rank);
        points.u64(value.points);
    }
    return state.finish();
}

hub::Bytes encode_points(const world::SkillSet& skills, std::uint64_t sequence, world::Revision since) {
    struct Entry {
        std::uint32_t pool;
        std::uint32_t node;
        std::uint16_t level;
    };
    std::vector<Entry> entries;
    std::size_t pools = 0;
    for (const auto& [pool, nodes] : skills.pools) {
        if (++pools > kMaxPools) {
            break; // The client reads no more pools. The game has 16 weapons.
        }
        for (const auto& [node, state] : nodes) {
            // A node that the character lost: only for a client that had it, one time.
            if ((state.level != 0 || (since != 0 && state.changed > since)) && entries.size() < kMaxPointEntries) {
                entries.push_back(Entry{pool, node, state.level});
            }
        }
    }
    pools = std::min(skills.pools.size(), kMaxPools);
    // One state has each pool and each node: the client replaces its lists with the lists of
    // a state, thus a state with a part of the nodes makes the client lose the other nodes.
    StateWriter state(kGroups);
    hub::Writer& ids = state.field(0, kPoolIds);
    hub::Writer& counts = state.field(0, kPoolCounts);
    hub::Writer& categories = state.field(0, kPoolCategories);
    begin_list(ids, sequence, pools);
    begin_list(counts, sequence, pools);
    begin_list(categories, sequence, pools);
    std::size_t written = 0;
    for (const auto& [pool, nodes] : skills.pools) {
        if (written++ == pools) {
            break;
        }
        ids.u32(pool);
        counts.u16(static_cast<std::uint16_t>(std::min<std::uint32_t>(skills.free_points(pool), 0xFFFF)));
        categories.u8(kPoolCategoryPlayer);
    }
    hub::Writer& points = state.field(0, kPointEntries);
    begin_list(points, sequence, entries.size());
    for (const Entry& entry : entries) {
        points.u32(entry.pool);
        points.u32(entry.node);
        points.u16(entry.level);
    }
    return state.finish();
}

hub::Bytes encode_ability_bars(const world::SkillSet& skills, bool placeNumbers) {
    StateWriter state(kGroups);
    hub::Writer& data = state.field(0, kPersistentAbilityData);
    data.size(static_cast<std::uint32_t>(gamedata::kAbilityTableOrder.size()));
    for (const std::uint32_t table : gamedata::kAbilityTableOrder) {
        const auto bar = skills.bars.find(table);
        data.u32(table);
        data.size(world::kBarSlots);
        for (std::uint32_t slot = 0; slot < world::kBarSlots; ++slot) {
            data.u32(bar == skills.bars.end() ? 0 : bar->second[slot]);
            data.u32(placeNumbers ? slot : kAbilityButtons[slot]);
        }
    }
    data.size(0); // No table version: the bar is correct in the live client without one.
    return state.finish();
}

hub::Bytes encode_categorical_state(const world::Entity& entity, View view, const world::WorldClock&) {
    if (view != View::owner) {
        return {};
    }
    const auto changed = entity.changed.find(world::ComponentKey::categorical);
    return encode_categorical(entity.skills, changed == entity.changed.end() ? 1 : changed->second);
}

std::vector<hub::Bytes> encode_points_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner) {
        return {};
    }
    // One fragment for each size of the state: the budget of the request does not cut it.
    const auto changed = entity.changed.find(world::ComponentKey::points);
    return {encode_points(entity.skills, changed == entity.changed.end() ? 1 : changed->second, request.since)};
}

std::vector<hub::Bytes> encode_ability_state(const world::Entity& entity, View view, const PartsRequest& request) {
    if (view != View::owner) {
        return {};
    }
    // The client makes its own list with an empty bar for each ability table at its world
    // entry (client code at image RVA 0x6870BB0, live proof). Thus a session with no record of
    // a character that has no ability on a bar gets no fragment: the state is 850 bytes.
    if (request.since == 0 && entity.skills.bars.empty()) {
        return {};
    }
    // The backend refused a bar request after the last state of this session: the two forms,
    // so that the client applies the bars of the server and not its own bar.
    if (request.since != 0 && entity.skills.barsRefused > request.since) {
        return {encode_ability_bars(entity.skills, true), encode_ability_bars(entity.skills)};
    }
    return {encode_ability_bars(entity.skills)};
}

} // namespace soulwarden::backend::server::rep
