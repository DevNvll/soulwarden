#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include "server/hub/wire.h"

// Decoder of the Container field of the container state (protocol notes 4.10), written from
// the reader of the client and not from the encoder of the backend: a test reads each element
// that a session sends, and it proves that a fragment has no unknown byte.

namespace soulwarden::backend::tests {

/** One element of a container on the wire. */
struct WireItem {
    std::uint32_t quantity{0};
    std::uint32_t action{0};
    /** Slot code of the packed number: 0 to 500, or 501 for "no slot". */
    std::uint32_t slot{0};
    std::uint32_t index{0};
    std::uint32_t maxDurability{0};
    std::uint32_t durability{0};
    std::uint32_t chrono{0};
    std::uint32_t gearScore{0};
    std::array<std::uint32_t, 5> perks{};
    std::array<std::uint8_t, 16> instance{};
    std::uint8_t flags{0};
    std::uint32_t appearance{0};

    /** @return True for an element that is an empty slot in the client. */
    [[nodiscard]] bool empty() const {
        return quantity == 0 || index == 0;
    }
};

/** The value of the Container field. */
struct WireContainer {
    /** True when the decoder read the value with no fault. */
    bool ok{false};
    /** True: the full vector. False: a journal. */
    bool full{false};
    /** True when a version has a sequence (the backend sends none). */
    bool sequence{false};
    /** Number of journal entries that erase an element (the backend sends none). */
    unsigned erases{0};
    /** The element number and the element: each element of the full vector, or each element that a journal sets. */
    std::vector<std::pair<std::uint32_t, WireItem>> elements;
    /** Number of bytes of the value. */
    std::size_t size{0};
};

/** Reads one element. @param transmog True: the element has the appearance id and the skin dye bytes. */
inline bool read_item(server::hub::Reader& reader, WireItem& item, bool transmog = true) {
    std::uint64_t packed = reader.u64();
    item.maxDurability = static_cast<std::uint32_t>(packed % 8001);
    packed /= 8001;
    item.index = static_cast<std::uint32_t>(packed & 0xFFFFF);
    packed >>= 20;
    item.slot = static_cast<std::uint32_t>(packed % 502);
    packed /= 502;
    item.action = static_cast<std::uint32_t>(packed % 64);
    item.quantity = static_cast<std::uint32_t>(packed / 64);
    item.durability = reader.size();
    item.chrono = reader.size();
    item.gearScore = reader.size();
    for (std::uint32_t& perk : item.perks) {
        perk = reader.size();
    }
    reader.raw(4); // Dye settings.
    const auto instance = reader.raw(16);
    if (instance.size() == item.instance.size()) {
        std::copy(instance.begin(), instance.end(), item.instance.begin());
    }
    item.flags = reader.u8();
    if (transmog) {
        item.appearance = reader.u32();
        reader.raw(4); // Skin dye settings.
    }
    return reader.ok();
}

inline bool read_version(server::hub::Reader& reader, WireContainer& container) {
    const std::uint8_t has = reader.u8();
    if (has > 1) {
        return false;
    }
    if (has == 1) {
        container.sequence = true;
        reader.size64();
    }
    return reader.ok();
}

/** Reads the value of the Container field at the start of `data`. */
inline WireContainer read_container(std::span<const std::uint8_t> data) {
    WireContainer container;
    server::hub::Reader reader(data);
    const std::uint32_t changes = reader.size();
    if (!reader.ok()) {
        return container;
    }
    if (changes == 0) {
        container.full = true;
        if (!read_version(reader, container)) {
            return container;
        }
        const std::uint32_t count = reader.size();
        if (!reader.ok() || count > 500) {
            return container; // The client reads 500 elements at most.
        }
        for (std::uint32_t index = 0; index < count; ++index) {
            WireItem item;
            if (!read_item(reader, item)) {
                return container;
            }
            container.elements.emplace_back(index, item);
        }
    } else {
        for (std::uint32_t first = 0; first < changes; first += 8) {
            const std::uint8_t mask = reader.u8();
            for (std::uint32_t bit = 0; bit < 8 && first + bit < changes; ++bit) {
                const std::uint64_t index = reader.size64();
                if (!read_version(reader, container)) {
                    return container;
                }
                if ((mask & (1U << bit)) == 0) {
                    ++container.erases;
                    continue;
                }
                WireItem item;
                if (!read_item(reader, item)) {
                    return container;
                }
                container.elements.emplace_back(static_cast<std::uint32_t>(index), item);
            }
        }
    }
    container.ok = reader.ok();
    container.size = reader.position();
    return container;
}

} // namespace soulwarden::backend::tests
