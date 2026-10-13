#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <span>
#include <vector>

#include "server/hub/wire.h"
#include "support/item_decoder.h"

// Decoder of the fields of the paperdoll state (protocol notes 4.11), written from the readers
// of the client and not from the encoder of the backend: a test reads each list that a session
// sends, and it proves that a fragment has no unknown byte.

namespace soulwarden::backend::tests {

/** The look of one item for the client of an observer (client reader at image RVA 0x5CE9070). */
struct WireLook {
    std::uint32_t index{0};
    std::uint8_t flags{0};
    /** The value of flag bit 0. Its meaning is not known. */
    std::uint32_t value{0};
    std::array<std::uint8_t, 4> dye{};
    std::uint32_t appearance{0};
};

/** The head of a list in the full form: `00 version count`. The backend sends no other form. */
inline bool read_list_head(server::hub::Reader& reader, std::uint32_t& count) {
    if (reader.size() != 0) {
        return false; // A journal: the paperdoll encoder of the backend sends none.
    }
    const std::uint8_t has = reader.u8();
    if (has != 0) {
        return false; // A version with a sequence: the backend sends none.
    }
    count = reader.size();
    return reader.ok() && count <= 64;
}

inline bool read_number_list(server::hub::Reader& reader, std::vector<std::uint32_t>& values) {
    std::uint32_t count = 0;
    if (!read_list_head(reader, count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        values.push_back(reader.u32());
    }
    return reader.ok();
}

inline bool read_item_list(server::hub::Reader& reader, std::vector<WireItem>& items) {
    std::uint32_t count = 0;
    if (!read_list_head(reader, count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        WireItem item;
        if (!read_item(reader, item)) {
            return false;
        }
        items.push_back(item);
    }
    return reader.ok();
}

inline bool read_look(server::hub::Reader& reader, WireLook& look, bool transmog = true) {
    look.index = reader.size();
    look.flags = reader.u8();
    if ((look.flags & 1) != 0) {
        look.value = reader.size();
    }
    if ((look.flags & 2) != 0) {
        const auto dye = reader.raw(4);
        if (dye.size() == look.dye.size()) {
            std::copy(dye.begin(), dye.end(), look.dye.begin());
        }
    }
    if (transmog) {
        look.appearance = reader.u32();
        reader.raw(4); // Skin dye settings.
    }
    return reader.ok();
}

inline bool read_look_list(server::hub::Reader& reader, std::vector<WireLook>& looks) {
    std::uint32_t count = 0;
    if (!read_list_head(reader, count)) {
        return false;
    }
    for (std::uint32_t index = 0; index < count; ++index) {
        WireLook look;
        if (!read_look(reader, look)) {
            return false;
        }
        looks.push_back(look);
    }
    return reader.ok();
}

/**
 * @return The size of the value of one field of the paperdoll state (type 3183) at the start
 *         of `rest`, or -1 for a field that the backend does not send or a value with a fault.
 */
inline int paperdoll_value_size(unsigned group, unsigned field, std::span<const std::uint8_t> rest) {
    server::hub::Reader reader(rest);
    std::vector<std::uint32_t> numbers;
    std::vector<WireItem> items;
    std::vector<WireLook> looks;
    bool ok = false;
    if (group == 1) {
        switch (field) {
        case 0: // visibleDurability
        case 3: // nonVisiblePaperdollSlots
        case 4: // nonVisibleDurability
            ok = read_number_list(reader, numbers);
            break;
        case 1: // visibleFullItemData
        case 2: // nonVisibleFullItemData
            ok = read_item_list(reader, items);
            break;
        case 6: // isLocalPlayer
            return rest.empty() ? -1 : 1;
        default:
            return -1;
        }
    } else if (group == 2) {
        switch (field) {
        case 0: // sheatheMap
        case 2: // activeMap
            return rest.size() < 7 ? -1 : 7;
        case 3: // visiblePaperdollSlots
            ok = read_number_list(reader, numbers);
            break;
        case 4: // visibleItemVisualData
            ok = read_look_list(reader, looks);
            break;
        default:
            return -1;
        }
    }
    return ok ? static_cast<int>(reader.position()) : -1;
}

} // namespace soulwarden::backend::tests
