#pragma once

#include <cmath>
#include <string>
#include <string_view>
#include <vector>

// The text and compare rules of the cells of the game data that the gear system and the evaluator of the passive rules share
// (world/gear.h, world/passives.h): one place for the comparator of the client.

namespace soulwarden::backend::world::data_rules {

/** @return The text with the letters A to Z in lower case. */
[[nodiscard]] inline std::string lower(std::string_view text) {
    std::string result(text);
    for (char& letter : result) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + 32);
        }
    }
    return result;
}

/** @return The parts of a text with "+" or "," between them, with no space around them. */
[[nodiscard]] inline std::vector<std::string_view> parts_of(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of("+,", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        if (!part.empty()) {
            parts.push_back(part);
        }
        start = end + 1;
    }
    return parts;
}

[[nodiscard]] inline bool same_text(std::string_view left, std::string_view right) {
    return lower(left) == lower(right);
}

/** @return True when a list of the data ("Light,Heavy") has the value. */
[[nodiscard]] inline bool list_has(std::string_view list, std::string_view value) {
    for (const std::string_view part : parts_of(list)) {
        if (same_text(part, value)) {
            return true;
        }
    }
    return false;
}

/**
 * @return The result of a compare of the data, with the parser of the AbilityData columns of the client (image RVA 0x43B6390,
 *         for TargetComparisonType, MyComparisonType, MyStaminaComparisonType, MyManaComparisonType, NumberOfHitsComparisonType,
 *         NumAroundComparisonType): "Equal" or "=", "NotEqual" or "!=", "LessThan" or "<", "GreaterThan" or ">",
 *         "GreaterThanOrEqual" or ">=". Each other text, also the empty text, is LessThanOrEqual (enum 4: research
 *         abilities-shared.md K15).
 */
[[nodiscard]] inline bool compare(float value, float limit, std::string_view kind) {
    const std::string text = lower(kind);
    // Equal is a band of 0.001 (a percent of a float: 600 of 2000 is 30.000002 percent): the "or equal" of LessThanOrEqual and
    // GreaterThanOrEqual takes the same band, and LessThan and GreaterThan do not (stage 3.3b-S5, the limits of S5-I4).
    const bool equal = std::fabs(value - limit) <= 0.001F;
    if (text == "equal" || text == "=") {
        return equal;
    }
    if (text == "notequal" || text == "!=") {
        return !equal;
    }
    if (text == "lessthan" || text == "<") {
        return value < limit && !equal;
    }
    if (text == "greaterthan" || text == ">") {
        return value > limit && !equal;
    }
    if (text == "greaterthanorequal" || text == ">=") {
        return value > limit || equal;
    }
    return value < limit || equal;
}

/** @return A value as a percent of a maximum (100 with no maximum). */
[[nodiscard]] inline float percent_of(float value, float maximum) {
    return maximum > 0.0F ? value / maximum * 100.0F : 100.0F;
}

} // namespace soulwarden::backend::world::data_rules
