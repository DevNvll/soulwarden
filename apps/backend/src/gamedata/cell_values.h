#pragma once

#include <map>
#include <string>
#include <string_view>

// The cells of one row of a game table that a tool writes as a map: each cell that is not empty
// and not 0, a number as a number and each other cell as a text (stage 1.3b: the affixes and the
// ability rows of the perks, and the other cells of a status effect and of a spell). The rules
// that read a cell are in the world, not here.

namespace soulwarden::backend::gamedata {

struct CellValues {
    std::map<std::string, float, std::less<>> numbers;
    std::map<std::string, std::string, std::less<>> texts;

    /** @return The number of a cell, or 0 when the row has no such number. */
    [[nodiscard]] float number(std::string_view column) const {
        const auto found = numbers.find(column);
        return found == numbers.end() ? 0.0F : found->second;
    }
    /** @return True when the row has a number in this cell (not 0). */
    [[nodiscard]] bool has(std::string_view column) const {
        return numbers.find(column) != numbers.end() || texts.find(column) != texts.end();
    }
    /** @return The text of a cell, or an empty text. A number cell has no text here. */
    [[nodiscard]] std::string_view text(std::string_view column) const {
        const auto found = texts.find(column);
        return found == texts.end() ? std::string_view{} : std::string_view(found->second);
    }
};

} // namespace soulwarden::backend::gamedata
