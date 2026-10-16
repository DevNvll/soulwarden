#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <vector>

#include "server/hub/wire.h"

// Decoder of the attribute state (type 129) and of the three skill states (types 911, 3681,
// 185) from the formats of the protocol notes (4.12, 4.13). It follows the readers of the
// client with their limits: a count above a limit, or a flag byte that is not 0 or 1, is a
// read fault, as in the client.

namespace soulwarden::backend::tests {

namespace progress_wire {

using Reader = server::hub::Reader;

/** Limits of the readers of the client. */
inline constexpr std::uint32_t kMaxPlacingBonuses = 5;
inline constexpr std::uint32_t kMaxPools = 50;
inline constexpr std::uint32_t kMaxPointEntries = 1000;
inline constexpr std::uint32_t kMaxBarSlots = 3;
inline constexpr std::uint32_t kMaxListEntries = 0x2000000;

/** Reads the start of a replicated container in the full form. @return The count, or no value. */
inline std::optional<std::uint32_t> read_full_form(Reader& reader, std::uint64_t* sequence = nullptr) {
    if (reader.size() != 0) {
        return std::nullopt; // Change entries: the backend sends the full form only.
    }
    const std::uint8_t stamp = reader.u8();
    if (stamp > 1) {
        return std::nullopt;
    }
    const std::uint64_t value = stamp == 1 ? reader.size64() : 0;
    if (sequence != nullptr) {
        *sequence = value;
    }
    const std::uint32_t count = reader.size();
    return reader.ok() ? std::optional(count) : std::nullopt;
}

/** @return The size of a list field with elements of a constant size, or -1. */
inline int list_size(std::span<const std::uint8_t> rest, std::size_t element, std::uint32_t limit) {
    Reader reader(rest);
    const auto count = read_full_form(reader);
    if (!count || *count > limit) {
        return -1;
    }
    reader.raw(static_cast<std::size_t>(*count) * element);
    return reader.ok() ? static_cast<int>(reader.position()) : -1;
}

/** @return The size of the value of a field of the attribute state, or -1. */
inline int attribute_value_size(unsigned group, unsigned field, std::span<const std::uint8_t> rest) {
    if (group != 0) {
        return -1;
    }
    Reader reader(rest);
    switch (field) {
    case 0: { // attributes: count, then type and value.
        const std::uint32_t count = reader.size();
        if (!reader.ok() || count > kMaxListEntries) {
            return -1;
        }
        reader.raw(static_cast<std::size_t>(count) * 8);
        break;
    }
    case 1: // attributeBonuses: a replicated map of type and value.
        return list_size(rest, 8, kMaxListEntries);
    case 2: // placingBonuses: a replicated array of numbers.
        return list_size(rest, 4, kMaxPlacingBonuses);
    case 3: { // persistentAttributeData.
        reader.u32();
        if (reader.u8() > 1 || reader.u8() > 1) {
            return -1;
        }
        const std::uint32_t count = reader.size();
        if (!reader.ok() || count > kMaxListEntries) {
            return -1;
        }
        reader.raw(static_cast<std::size_t>(count) * 8);
        reader.u32();
        break;
    }
    default:
        return -1;
    }
    return reader.ok() ? static_cast<int>(reader.position()) : -1;
}

/** @return The size of the value of a field of the state of slot 65, or -1. */
inline int categorical_value_size(unsigned group, unsigned field, std::span<const std::uint8_t> rest) {
    constexpr std::size_t kElements[] = {4, 2, 8};
    return group == 0 && field < 3 ? list_size(rest, kElements[field], kMaxListEntries) : -1;
}

/** @return The size of the value of a field of the state of slot 67, or -1. */
inline int points_value_size(unsigned group, unsigned field, std::span<const std::uint8_t> rest) {
    constexpr std::size_t kElements[] = {4, 2, 1, 10};
    return group == 0 && field < 4 ? list_size(rest, kElements[field], field == 3 ? kMaxPointEntries : kMaxPools) : -1;
}

/** @return The size of the value of a field of the state of slot 46 (field 0, and fields 1 and 2 of stage 3.3b-M2), or -1. */
inline int ability_value_size(unsigned group, unsigned field, std::span<const std::uint8_t> rest) {
    // Fields 1 and 2 (stage 3.3b-M2): hitDataNumHits (a list of u8) and hitDataAbilityIds (a list of u32).
    if (group == 0 && (field == 1 || field == 2)) {
        return list_size(rest, field == 1 ? 1 : 4, kMaxListEntries);
    }
    if (group != 0 || field != 0) {
        return -1;
    }
    Reader reader(rest);
    const std::uint32_t tables = reader.size();
    if (!reader.ok() || tables > kMaxListEntries) {
        return -1;
    }
    for (std::uint32_t table = 0; table < tables && reader.ok(); ++table) {
        reader.u32();
        const std::uint32_t slots = reader.size();
        if (slots > kMaxBarSlots) {
            return -1;
        }
        reader.raw(static_cast<std::size_t>(slots) * 8);
    }
    const std::uint32_t versions = reader.size();
    reader.raw(static_cast<std::size_t>(versions) * 8);
    return reader.ok() ? static_cast<int>(reader.position()) : -1;
}

} // namespace progress_wire

/** The attribute state as the client reads it. */
struct WireAttributes {
    bool ok{false};
    std::map<std::uint32_t, std::uint32_t> values;
    std::uint32_t bonuses{0};
    std::uint32_t placing{0};
    std::uint32_t unspent{0};
    bool ignoreBackstory{false};
    bool hasRespec{false};
    std::map<std::uint32_t, std::uint32_t> spent;
    std::uint32_t behavior{0};
};

/** Reads the four fields of an attribute state: `fields[N]` is the value of field N. */
inline WireAttributes read_attributes(const std::array<std::span<const std::uint8_t>, 4>& fields) {
    WireAttributes result;
    server::hub::Reader values(fields[0]);
    const std::uint32_t count = values.size();
    for (std::uint32_t index = 0; index < count && values.ok(); ++index) {
        const std::uint32_t type = values.u32();
        result.values[type] = values.u32();
    }
    server::hub::Reader bonuses(fields[1]);
    server::hub::Reader placing(fields[2]);
    const auto bonusCount = progress_wire::read_full_form(bonuses);
    const auto placingCount = progress_wire::read_full_form(placing);
    server::hub::Reader data(fields[3]);
    result.unspent = data.u32();
    result.ignoreBackstory = data.u8() == 1;
    result.hasRespec = data.u8() == 1;
    const std::uint32_t spent = data.size();
    for (std::uint32_t index = 0; index < spent && data.ok(); ++index) {
        const std::uint32_t type = data.u32();
        result.spent[type] = data.u32();
    }
    result.behavior = data.u32();
    result.bonuses = bonusCount.value_or(0);
    result.placing = placingCount.value_or(0);
    result.ok = values.ok() && values.left() == 0 && bonusCount && placingCount && data.ok() && data.left() == 0;
    return result;
}

/** One entry of the state of slot 65. */
struct WireRank {
    std::uint32_t id{0};
    std::uint16_t rank{0};
    std::uint64_t points{0};
    bool operator==(const WireRank&) const = default;
};

struct WireRanks {
    bool ok{false};
    std::uint64_t sequence{0};
    std::vector<WireRank> entries;
};

/** Reads the three lists of the state of slot 65. The client uses them only when they have the same length. */
inline WireRanks read_ranks(const std::array<std::span<const std::uint8_t>, 3>& fields) {
    WireRanks result;
    server::hub::Reader ids(fields[0]);
    server::hub::Reader ranks(fields[1]);
    server::hub::Reader points(fields[2]);
    const auto idCount = progress_wire::read_full_form(ids, &result.sequence);
    const auto rankCount = progress_wire::read_full_form(ranks);
    const auto pointCount = progress_wire::read_full_form(points);
    if (!idCount || !rankCount || !pointCount || *idCount != *rankCount || *idCount != *pointCount) {
        return result;
    }
    for (std::uint32_t index = 0; index < *idCount; ++index) {
        WireRank entry;
        entry.id = ids.u32();
        entry.rank = ranks.u16();
        entry.points = points.u64();
        result.entries.push_back(entry);
    }
    result.ok = ids.ok() && ranks.ok() && points.ok() && ids.left() == 0 && ranks.left() == 0 && points.left() == 0;
    return result;
}

struct WirePool {
    std::uint32_t id{0};
    std::uint16_t free{0};
    std::uint8_t category{0};
    bool operator==(const WirePool&) const = default;
};

struct WireNode {
    std::uint32_t pool{0};
    std::uint32_t node{0};
    std::uint16_t level{0};
    bool operator==(const WireNode&) const = default;
};

struct WirePools {
    bool ok{false};
    std::vector<WirePool> pools;
    std::vector<WireNode> nodes;
    /** The stamp of the list of the pool ids, and of the list of the point entries. */
    std::uint64_t poolStamp{0};
    std::uint64_t entryStamp{0};
};

/** Reads the four lists of the state of slot 67. */
inline WirePools read_pools(const std::array<std::span<const std::uint8_t>, 4>& fields) {
    WirePools result;
    server::hub::Reader ids(fields[0]);
    server::hub::Reader counts(fields[1]);
    server::hub::Reader categories(fields[2]);
    server::hub::Reader entries(fields[3]);
    const auto idCount = progress_wire::read_full_form(ids, &result.poolStamp);
    const auto countCount = progress_wire::read_full_form(counts);
    const auto categoryCount = progress_wire::read_full_form(categories);
    const auto entryCount = progress_wire::read_full_form(entries, &result.entryStamp);
    if (!idCount || !countCount || !categoryCount || !entryCount || *idCount != *countCount || *idCount != *categoryCount
        || *idCount > progress_wire::kMaxPools || *entryCount > progress_wire::kMaxPointEntries) {
        return result;
    }
    for (std::uint32_t index = 0; index < *idCount; ++index) {
        WirePool pool;
        pool.id = ids.u32();
        pool.free = counts.u16();
        pool.category = categories.u8();
        result.pools.push_back(pool);
    }
    for (std::uint32_t index = 0; index < *entryCount; ++index) {
        WireNode node;
        node.pool = entries.u32();
        node.node = entries.u32();
        node.level = entries.u16();
        result.nodes.push_back(node);
    }
    result.ok = ids.ok() && counts.ok() && categories.ok() && entries.ok() && ids.left() == 0 && counts.left() == 0
                && categories.left() == 0 && entries.left() == 0;
    return result;
}

struct WireBar {
    std::uint32_t table{0};
    std::vector<std::uint32_t> abilities;
    std::vector<std::uint32_t> buttons;
};

struct WireBars {
    bool ok{false};
    std::vector<WireBar> tables;
    std::uint32_t versions{0};
};

/**
 * @return True when two ability states are equal for the state merge of the client (compare
 *         function at image RVA 0x66277F0): the table ids, the abilities, and the button
 *         numbers. The client does not give an equal state to its facet.
 */
inline bool same_bars(const WireBars& left, const WireBars& right) {
    if (left.tables.size() != right.tables.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.tables.size(); ++index) {
        if (left.tables[index].table != right.tables[index].table || left.tables[index].abilities != right.tables[index].abilities
            || left.tables[index].buttons != right.tables[index].buttons) {
            return false;
        }
    }
    return true;
}

/** Reads field 0 of the state of slot 46. */
inline WireBars read_bars(std::span<const std::uint8_t> field) {
    WireBars result;
    server::hub::Reader reader(field);
    const std::uint32_t tables = reader.size();
    for (std::uint32_t index = 0; index < tables && reader.ok(); ++index) {
        WireBar bar;
        bar.table = reader.u32();
        const std::uint32_t slots = reader.size();
        if (slots > progress_wire::kMaxBarSlots) {
            return result;
        }
        for (std::uint32_t slot = 0; slot < slots; ++slot) {
            bar.abilities.push_back(reader.u32());
            bar.buttons.push_back(reader.u32());
        }
        result.tables.push_back(std::move(bar));
    }
    result.versions = reader.size();
    reader.raw(static_cast<std::size_t>(result.versions) * 8);
    result.ok = reader.ok() && reader.left() == 0;
    return result;
}

} // namespace soulwarden::backend::tests
