#include "world/item.h"

#include <algorithm>

namespace soulwarden::backend::world {
namespace paperdoll {
namespace {

/** The slots `first` to `last` take an item whose class text has one of the tags. */
struct SlotRule {
    std::uint32_t first;
    std::uint32_t last;
    std::array<std::string_view, 2> tags;
};

// The switch of the client check (image RVA 0x679C060) with the names of the item class
// numbers (the table of the client at image RVA 0x5CB4770; the bag tag is from the item
// tables). The slots that are not here have no rule in the backend: the hand slot of a
// gathered object (9), the two old weapon slots (10, 11), and the two ammunition slots (12,
// 13), whose rule needs the ammunition type of an item.
constexpr SlotRule kSlotRules[] = {
    {kHead, kHead, {"EquippableHead"}},
    {kChest, kChest, {"EquippableChest"}},
    {kHands, kHands, {"EquippableHands"}},
    {kLegs, kLegs, {"EquippableLegs"}},
    {kFeet, kFeet, {"EquippableFeet"}},
    {kAmulet, kAmulet, {"EquippableAmulet"}},
    {kToken, kToken, {"EquippableToken"}},
    {kRing, kRing, {"EquippableRing"}},
    {8, 8, {"EquippablePaint"}},
    {14, 19, {"Consumable", "Blueprint"}}, // The quick slots.
    {20, 20, {"EquippableBackpack"}},
    {kMainHand1, kMainHand3, {"EquippableMainHand", "EquippableTwoHand"}},
    {kOffHand1, kOffHand3, {"EquippableOffHand"}},
    {30, 32, {"Bag"}},
    {33, 33, {"LoggingAxe"}},
    {34, 34, {"Sickle"}},
    {35, 35, {"SkinningKnife"}},
    {36, 36, {"Pickaxe"}},
    {37, 37, {"FishingPole"}},
    {38, 38, {"SoulwardenStaff"}},
    {39, 39, {"InstrumentFlute"}},
    {40, 40, {"InstrumentGuitar"}},
    {41, 41, {"InstrumentMandolin"}},
    {42, 42, {"InstrumentUprightBass"}},
    {43, 43, {"InstrumentDrums"}},
    {44, 44, {"ThrowableObject"}},
    {45, 45, {"HeartGem"}},
    {46, 46, {"MountBuff1"}},
    {47, 47, {"MountBuff2"}},
    {48, 49, {"MountAttachment"}},
};

/** The client compares the tags of an item class in lower case. */
bool same_tag(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    const auto lower = [](char letter) { return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + 32) : letter; };
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool valid_slot(std::uint32_t slot) {
    // The slot database has no row for the slots 21, 22, and 23 (mouth, saddle, bridle).
    return slot <= 20 || (slot >= kMainHand1 && slot < kSlotCount);
}

bool slot_accepts(std::uint32_t slot, std::string_view itemClass) {
    for (const SlotRule& rule : kSlotRules) {
        if (slot < rule.first || slot > rule.last) {
            continue;
        }
        std::size_t start = 0;
        while (start <= itemClass.size()) {
            const std::size_t end = std::min(itemClass.find('+', start), itemClass.size());
            const std::string_view tag = itemClass.substr(start, end - start);
            for (const std::string_view accepted : rule.tags) {
                if (!accepted.empty() && same_tag(tag, accepted)) {
                    return true;
                }
            }
            start = end + 1;
        }
        return false;
    }
    return false;
}

bool visible_slot(std::uint32_t slot) {
    return slot <= kFeet || weapon_slot(slot);
}

bool weapon_slot(std::uint32_t slot) {
    return slot >= kMainHand1 && slot <= kOffHand3;
}

} // namespace paperdoll

std::string instance_text(const InstanceId& instance) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    text.reserve(instance.size() * 2);
    for (const std::uint8_t byte : instance) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

std::optional<InstanceId> read_instance(const std::string& text) {
    InstanceId instance{};
    if (text.size() != instance.size() * 2) {
        return std::nullopt;
    }
    const auto digit = [](char letter) -> int {
        if (letter >= '0' && letter <= '9') return letter - '0';
        if (letter >= 'a' && letter <= 'f') return letter - 'a' + 10;
        if (letter >= 'A' && letter <= 'F') return letter - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < instance.size(); ++index) {
        const int high = digit(text[index * 2]);
        const int low = digit(text[index * 2 + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        instance[index] = static_cast<std::uint8_t>(high * 16 + low);
    }
    return instance;
}

} // namespace soulwarden::backend::world
