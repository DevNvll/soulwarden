#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gamedata/progression_tables.h"
#include "server/rep/progress_requests.h"
#include "support/fake_client.h"
#include "support/server_host.h"
#include "support/state_decoder.h"

// Progression tables of the tests, and what a client keeps of the progress and skill states.
// The values of the tables are made up. The two weapons and the two trade skills have id texts
// of the game ("SwordAbilityTable", "BowAbilityTable", "Mining", "Riding"): a client request
// names a weapon by its index in the ability table list of the client, which has the ids of
// the game. The nodes of the trees are made up.

namespace soulwarden::backend::tests {

inline constexpr std::uint32_t kProgressionSlot = 70;
inline constexpr std::uint32_t kAttributeSlot = 27;
inline constexpr std::uint32_t kVitalsSlot = 41;
inline constexpr std::uint32_t kCurrencySlot = 60;
inline constexpr std::uint32_t kCategoricalSlot = 65;
inline constexpr std::uint32_t kPointsSlot = 67;
inline constexpr std::uint32_t kAbilitySlot = 46;

/** The ids on the wire of the weapons and trade skills of the test tables. */
inline constexpr std::uint32_t kSword = 0xE8EC5716;  // SwordAbilityTable
inline constexpr std::uint32_t kBow = 0xC7EA8FF8;    // BowAbilityTable
inline constexpr std::uint32_t kMining = 0x25DD6A56; // Mining
/** The index of the two weapons in the ability table list of the client. */
inline constexpr std::uint16_t kSwordIndex = 27;
inline constexpr std::uint16_t kBowIndex = 25;

inline std::uint32_t node_id(const char* text) {
    return gamedata::progression_id(text);
}

/**
 * The tables of the tests.
 *   Levels 1 to 7 start at 0, 100, 300, 600, 1000, 1500, 2100 experience and give 0, 5, 5, 6, 6,
 *   7, 7 attribute points and 0, 50, ... 300 health. The largest level of a player is 6. An
 *   attribute reset has the cost 5.00 coin at the levels 6 and 7.
 *   An attribute is 5 to 30. Each point of constitution gives 40 health. The base health is 800.
 *   The sword and the bow have 15 ranks (0 to 14) with 10, 20, ... experience; a tree reset has
 *   a cost from rank 10 on. Mining has the ranks 0 to 5 with 100 experience each. Riding has
 *   the largest rank 0.
 *   Sword tree 0: row 0 Slash (active), P0a, P0b, P0c; row 1 SlashUp (needs Slash), Stab
 *   (active), P1a, P1b; row 2 P2a, P2b, Leap (active); row 3 P3a, P3b; row 4 P4a; row 5 (the
 *   last row) Ultimate. Sword tree 1: row 0 Bash (active), D0a; row 1 D1a; row 2 (the last row)
 *   DUltimate. Bow tree 0: row 0 Shot (active), P0a.
 */
inline gamedata::ProgressionData test_progression() {
    gamedata::ProgressionData data;
    data.levelCap = 6;
    const std::uint32_t experience[] = {0, 100, 300, 600, 1000, 1500, 2100};
    const std::uint32_t points[] = {0, 5, 5, 6, 6, 7, 7};
    for (std::uint32_t level = 0; level < 7; ++level) {
        data.levels.push_back({experience[level], points[level], level * 50, level >= 5 ? 500U : 0U});
    }
    data.attributeMinimum = 5;
    for (std::uint32_t value = 5; value <= 30; ++value) {
        data.constitutionHealth.push_back((value - 5) * 40);
    }
    data.baseHealth = 800;
    data.baseStamina = 100;
    data.baseMana = 100;
    gamedata::RankTable weaponRanks;
    for (std::uint32_t rank = 0; rank < 15; ++rank) {
        weaponRanks.experience.push_back((rank + 1) * 10);
        weaponRanks.resetCost.push_back(rank >= 10 ? 6 : 0);
    }
    gamedata::RankTable miningRanks;
    miningRanks.experience.assign(6, 100);
    gamedata::RankTable ridingRanks;
    ridingRanks.experience.assign(1, 450);
    data.progressions = {{"SwordAbilityTable", 0, gamedata::ProgressionKind::weapon, 14, weaponRanks},
                         {"BowAbilityTable", 0, gamedata::ProgressionKind::weapon, 14, weaponRanks},
                         {"Mining", 0, gamedata::ProgressionKind::tradeSkill, 5, miningRanks},
                         {"Riding", 0, gamedata::ProgressionKind::tradeSkill, 0, ridingRanks}};
    const auto node = [&](std::uint32_t table, const char* id, std::uint32_t tree, std::uint32_t row, std::uint32_t column,
                          bool active = false, const char* required = nullptr) {
        gamedata::AbilityNode entry;
        entry.id = id;
        entry.table = table;
        entry.tree = tree;
        entry.row = row;
        entry.column = column;
        entry.active = active;
        entry.required = required != nullptr ? node_id(required) : 0;
        data.nodes.push_back(entry);
    };
    node(kSword, "Test_Sword_Slash", 0, 0, 0, true);
    node(kSword, "Test_Sword_P0a", 0, 0, 1);
    node(kSword, "Test_Sword_P0b", 0, 0, 2);
    node(kSword, "Test_Sword_P0c", 0, 0, 3);
    node(kSword, "Test_Sword_SlashUp", 0, 1, 0, false, "Test_Sword_Slash");
    node(kSword, "Test_Sword_Stab", 0, 1, 1, true);
    node(kSword, "Test_Sword_P1a", 0, 1, 2);
    node(kSword, "Test_Sword_P1b", 0, 1, 3);
    node(kSword, "Test_Sword_P2a", 0, 2, 0);
    node(kSword, "Test_Sword_P2b", 0, 2, 1);
    node(kSword, "Test_Sword_Leap", 0, 2, 2, true);
    node(kSword, "Test_Sword_P3a", 0, 3, 0);
    node(kSword, "Test_Sword_P3b", 0, 3, 1);
    node(kSword, "Test_Sword_P4a", 0, 4, 0);
    node(kSword, "Test_Sword_Ultimate", 0, 5, 0);
    node(kSword, "Test_Sword_Bash", 1, 0, 0, true);
    node(kSword, "Test_Sword_D0a", 1, 0, 1);
    node(kSword, "Test_Sword_D1a", 1, 1, 0);
    node(kSword, "Test_Sword_DUltimate", 1, 2, 0);
    node(kBow, "Test_Bow_Shot", 0, 0, 0, true);
    node(kBow, "Test_Bow_P0a", 0, 0, 1);
    return data;
}

inline void set_progression(ServerHost& host) {
    host.progression.set(test_progression());
}

/** The fragments of one slot that a client got for an interest, from the message `from` on. */
struct SlotUpdates {
    std::vector<Fragment> fragments;
    /** Each record that has a fragment of the slot: its fragments as text. */
    std::vector<std::string> records;
    /** True when each bundle decodes to its last byte. */
    bool complete{true};
    std::size_t largestFrame{0};
};

inline SlotUpdates slot_updates(const FakeClient& client, std::uint32_t slot, std::size_t from = 0, std::uint32_t interest = 1) {
    SlotUpdates updates;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(client.got[index].body);
        updates.complete = updates.complete && bundle.complete;
        for (const Record& record : bundle.records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot) {
                    updates.fragments.push_back(fragment);
                    updates.records.push_back(order_of(record));
                    updates.largestFrame = std::max(updates.largestFrame, client.got[index].frame.size());
                }
            }
        }
    }
    return updates;
}

/** @return The bodies of the messages of a type that a client got, from the message `from` on. */
inline std::vector<Bytes> messages_of(const FakeClient& client, std::uint32_t type, std::size_t from = 0) {
    std::vector<Bytes> bodies;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type == type) {
            bodies.push_back(client.got[index].body);
        }
    }
    return bodies;
}

/**
 * The weapon trees of a client: it applies each fragment of slot 67 as the live client does
 * (protocol notes 4.13).
 * - The state merge compares the stamp of a list with the stamp of the list that the client
 *   has (image RVA 0x3CF3960): a list whose stamp is not larger is not used.
 * - The lists of a fragment replace the lists of the client: a pool that is not in a fragment
 *   goes away, and a node that is not in a fragment goes away too (live, stage 1.7: the tree
 *   window shows only the nodes of the last fragment).
 * - A node with the level 0 is not a node of the character.
 * - A fragment with no node makes the client give all points of each pool back itself.
 */
struct ClientTrees {
    struct Pool {
        std::uint16_t free{0};
        std::map<std::uint32_t, std::uint16_t> nodes;
    };
    std::map<std::uint32_t, Pool> pools;
    bool fault{false};
    unsigned fragments{0};
    /** Fragments that the state merge did not use. */
    unsigned refused{0};
    bool stamped{false};
    std::uint64_t entryStamp{0};

    void apply(const Fragment& fragment) {
        const WirePools wire = pools_of(fragment);
        ++fragments;
        if (!wire.ok) {
            fault = true;
            return;
        }
        if (stamped && wire.entryStamp <= entryStamp) {
            ++refused;
            return;
        }
        stamped = true;
        entryStamp = wire.entryStamp;
        std::map<std::uint32_t, Pool> next;
        for (const WirePool& pool : wire.pools) {
            next[pool.id].free = pool.free;
            fault = fault || (pool.category != 1 && pool.category != 2); // 2: a territory pool (stage QG1).
        }
        if (wire.nodes.empty()) {
            // The client gives the levels that it had back to each pool of the fragment.
            for (auto& [id, pool] : next) {
                const auto old = pools.find(id);
                if (old != pools.end()) {
                    for (const auto& [node, level] : old->second.nodes) {
                        pool.free = static_cast<std::uint16_t>(pool.free + level);
                    }
                }
            }
        }
        for (const WireNode& node : wire.nodes) {
            const auto pool = next.find(node.pool);
            if (pool == next.end()) {
                fault = true; // A node of a pool that the fragment does not have.
            } else if (node.level != 0) {
                pool->second.nodes[node.node] = node.level;
            }
        }
        pools = std::move(next);
    }

    /** @return The nodes of a pool as text: the count, for a compare. */
    [[nodiscard]] std::size_t count(std::uint32_t pool) const {
        const auto found = pools.find(pool);
        return found == pools.end() ? 0 : found->second.nodes.size();
    }
    [[nodiscard]] bool has(std::uint32_t pool, std::uint32_t node) const {
        const auto found = pools.find(pool);
        return found != pools.end() && found->second.nodes.count(node) != 0;
    }
    [[nodiscard]] int free(std::uint32_t pool) const {
        const auto found = pools.find(pool);
        return found == pools.end() ? -1 : found->second.free;
    }
};

/** Applies the fragments of slot 67 that a client got from the message `from` on. @return Their number. */
inline std::size_t follow_trees(ClientTrees& trees, const FakeClient& client, std::size_t& from) {
    const SlotUpdates updates = slot_updates(client, kPointsSlot, from);
    for (const Fragment& fragment : updates.fragments) {
        trees.apply(fragment);
    }
    trees.fault = trees.fault || !updates.complete;
    from = client.got.size();
    return updates.fragments.size();
}

/** The facet target of a component of an entity in the tests: the backend copies it into a reply. */
inline std::uint64_t target_of(std::uint64_t entity, std::uint32_t slot) {
    return entity + slot;
}

/** Body of AttributeComponentServerFacet_RequestSpendAttributePoints (type 1502), from the writer of the client. */
inline Bytes spend_body(std::uint64_t entity, const std::vector<std::pair<std::uint32_t, std::uint32_t>>& entries) {
    server::hub::Writer body;
    body.u64(target_of(entity, kAttributeSlot));
    body.u64(entity);
    body.size(static_cast<std::uint32_t>(entries.size()));
    for (const auto& [type, points] : entries) {
        body.u32(type);
    }
    body.size(static_cast<std::uint32_t>(entries.size()));
    for (const auto& [type, points] : entries) {
        body.u32(points);
    }
    return body.data();
}

/** Body of a request with no field: the facet target and the entity. */
inline Bytes plain_body(std::uint64_t entity, std::uint32_t slot) {
    server::hub::Writer body;
    body.u64(target_of(entity, slot));
    body.u64(entity);
    return body.data();
}

/** Body of ProgressionPointComponentServerFacet_RequestAssignPoolPoints (type 1112). */
inline Bytes assign_body(std::uint64_t entity, std::uint32_t pool, std::uint32_t node, std::uint32_t count = 1) {
    server::hub::Writer body;
    body.u64(target_of(entity, kPointsSlot));
    body.u64(entity);
    body.u32(pool);
    body.u32(node);
    body.u32(count);
    return body.data();
}

/** Body of AbilityComponentServerFacet_RequestChangeMappedAbility (type 2701). */
inline Bytes bar_body(std::uint64_t entity, std::uint16_t tableIndex, std::uint32_t slot, std::uint32_t ability) {
    server::hub::Writer body;
    body.u64(target_of(entity, kAbilitySlot));
    body.u64(entity);
    body.u16(tableIndex);
    body.u32(slot);
    body.u32(ability);
    return body.data();
}

/** Body of AbilityComponentServerFacet_RequestMasteryTreeRespec (type 186). */
inline Bytes tree_reset_body(std::uint64_t entity, std::uint16_t tableIndex, std::uint8_t flag = 0) {
    server::hub::Writer body;
    body.u64(target_of(entity, kAbilitySlot));
    body.u64(entity);
    body.u16(tableIndex);
    body.u8(flag);
    return body.data();
}

} // namespace soulwarden::backend::tests
