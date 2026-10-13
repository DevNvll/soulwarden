#include "state/character_name.h"

#include <cstdint>

namespace soulwarden::backend::state {
namespace {

/** Limits of the name rule (arguments of the client call at image RVA 0x41F6530). */
constexpr std::size_t kNameMinCharacters = 3;
constexpr std::size_t kNameMaxCharacters = 15;
/** The pattern of the client refuses a name with 7 digits. */
constexpr std::size_t kNameMaxDigits = 6;

/** A range of code points, with its two ends. */
struct CodeRange {
    char32_t first;
    char32_t last;
};
/** The characters that the pattern of the client permits (pattern text at image RVA 0x8159340). */
constexpr CodeRange kPermittedCodes[] = {
    {0x20, 0x20},   {0x27, 0x27},   {0x2C, 0x2E},   {0x30, 0x39},   {0x41, 0x5A},   {0x61, 0x7A},
    {0xC0, 0xD6},   {0xD8, 0xF6},   {0xF8, 0x108},  {0x10A, 0x124}, {0x126, 0x127}, {0x12A, 0x12B},
    {0x12D, 0x133}, {0x136, 0x14D}, {0x150, 0x167}, {0x169, 0x16B}, {0x16D, 0x174}, {0x176, 0x17E},
};

// UTF-8: each character that the name rule permits has one byte or two bytes (up to U+07FF).
constexpr std::uint8_t kTwoByteMask = 0xE0;
constexpr std::uint8_t kTwoByteLead = 0xC0;
constexpr std::uint8_t kTwoByteBits = 0x1F;
constexpr std::uint8_t kContinuationMask = 0xC0;
constexpr std::uint8_t kContinuationLead = 0x80;
constexpr std::uint8_t kContinuationBits = 0x3F;
/** The first code point that has two bytes. */
constexpr char32_t kFirstTwoByteCode = 0x80;

/** Distance from an upper-case letter to its lower-case letter in ASCII and in U+00C0 to U+00DE. */
constexpr char32_t kLatinCaseDistance = 0x20;
constexpr char32_t kMultiplicationSign = 0xD7;
constexpr char32_t kCapitalIWithDot = 0x130;
constexpr char32_t kCapitalYWithDiaeresis = 0x178;
constexpr char32_t kSmallYWithDiaeresis = 0xFF;

/**
 * @return The code point of the two-byte sequence at a position. 0 for a byte of a longer
 *         sequence, a byte that is not UTF-8, or a two-byte form of an ASCII character.
 */
char32_t two_byte_code(std::string_view name, std::size_t position) {
    const auto first = static_cast<std::uint8_t>(name[position]);
    if ((first & kTwoByteMask) != kTwoByteLead || position + 1 >= name.size()) {
        return 0;
    }
    const auto second = static_cast<std::uint8_t>(name[position + 1]);
    if ((second & kContinuationMask) != kContinuationLead) {
        return 0;
    }
    const char32_t code = (static_cast<char32_t>(first & kTwoByteBits) << 6) | (second & kContinuationBits);
    return code < kFirstTwoByteCode ? 0 : code;
}

bool is_permitted(char32_t code) {
    for (const CodeRange& range : kPermittedCodes) {
        if (code >= range.first && code <= range.last) {
            return true;
        }
    }
    return false;
}

/** @return The lower-case letter of a letter up to U+017F. Each other code point stays. */
char32_t lower_case(char32_t code) {
    if ((code >= U'A' && code <= U'Z') || (code >= 0xC0 && code <= 0xDE && code != kMultiplicationSign)) {
        return code + kLatinCaseDistance;
    }
    if (code == kCapitalIWithDot) {
        return U'i';
    }
    if (code == kCapitalYWithDiaeresis) {
        return kSmallYWithDiaeresis;
    }
    // U+0100 to U+017E has pairs of an upper-case letter and its lower-case letter. The first
    // letter of a pair has an even number in two ranges and an odd number in the other two.
    const bool evenPair = (code >= 0x100 && code <= 0x137) || (code >= 0x14A && code <= 0x177);
    const bool oddPair = (code >= 0x139 && code <= 0x148) || (code >= 0x179 && code <= 0x17E);
    if ((evenPair && code % 2 == 0) || (oddPair && code % 2 == 1)) {
        return code + 1;
    }
    return code;
}

} // namespace

NameRule check_name(std::string_view name) {
    std::size_t characters = 0;
    std::size_t digits = 0;
    bool permitted = true;
    char32_t previous = U' '; // A space at the start is the same fault as two spaces in sequence.
    std::size_t position = 0;
    while (position < name.size()) {
        char32_t code = static_cast<std::uint8_t>(name[position]);
        std::size_t size = 1;
        if (code >= kFirstTwoByteCode) {
            code = two_byte_code(name, position);
            size = 2;
        }
        // A character of three or four bytes, or bytes that are not UTF-8: the count of the
        // characters is not known, and no such character is permitted.
        if (code == 0) {
            return NameRule::characters;
        }
        if (!is_permitted(code) || (code == U' ' && previous == U' ')) {
            permitted = false;
        }
        if (code >= U'0' && code <= U'9') {
            ++digits;
        }
        ++characters;
        previous = code;
        position += size;
    }
    // The client makes the length check first.
    if (characters < kNameMinCharacters || characters > kNameMaxCharacters) {
        return NameRule::length;
    }
    const bool spaceAtEnd = previous == U' ';
    return permitted && !spaceAtEnd && digits <= kNameMaxDigits ? NameRule::permitted : NameRule::characters;
}

std::string name_compare_form(std::string_view name) {
    std::string form;
    form.reserve(name.size());
    std::size_t position = 0;
    while (position < name.size()) {
        const auto first = static_cast<std::uint8_t>(name[position]);
        if (first < kFirstTwoByteCode) {
            form.push_back(static_cast<char>(lower_case(first)));
            ++position;
            continue;
        }
        // Only the code points up to U+07FF (one or two bytes) can have a letter of the table.
        const char32_t code = two_byte_code(name, position);
        if (code == 0) {
            form.push_back(name[position]);
            ++position;
            continue;
        }
        const char32_t lower = lower_case(code);
        if (lower < kFirstTwoByteCode) {
            form.push_back(static_cast<char>(lower));
        } else {
            form.push_back(static_cast<char>(kTwoByteLead | (lower >> 6)));
            form.push_back(static_cast<char>(kContinuationLead | (lower & kContinuationBits)));
        }
        position += 2;
    }
    return form;
}

} // namespace soulwarden::backend::state
