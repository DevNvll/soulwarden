#pragma once

#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "state/creation_data.h"

// Name lists of the character creation databases of the client. The creation data of a
// character has names, and the appearance state of the world has the list index of each name.
// The lists are game data: the tool tools/gamedata/appearance.sh makes the file from the game
// copy of the user, and the file stays outside the repository.

namespace soulwarden::backend::gamedata {

/** File name of the tables in the game data directory of the backend. */
inline constexpr const char* kAppearanceTablesFile = "appearance-tables.json";

class AppearanceTables {
public:
    /** Reads the file of the tool. @return False when the file is not there or has a different form. */
    [[nodiscard]] bool load(const std::string& path);

    [[nodiscard]] bool loaded() const {
        return !m_genders.empty();
    }

    /**
     * Converts the names of an appearance to list indexes. A name that is not in its list gets
     * the index 0 and a log line (the client also uses the first entry for such a name).
     * @return No value when the tables are not loaded.
     */
    [[nodiscard]] std::optional<state::AppearanceIndexes> indexes(const state::AppearanceNames& names) const;

    /**
     * Converts list indexes to the names of an appearance.
     * @return No value when the tables are not loaded, or when a list does not have its index.
     */
    [[nodiscard]] std::optional<state::AppearanceNames> names(const state::AppearanceIndexes& indexes) const;

private:
    using NameList = std::vector<std::string>;
    struct HairLists {
        NameList styles;
        NameList colors;
    };
    /** One face of a gender. The three texts are keys of the skin tone lists and of the hair lists. */
    struct Race {
        std::string name;
        std::string skinTones;
        std::string hairStyles;
        std::string facialHair;
    };
    struct Gender {
        std::string name;
        std::vector<Race> races;
    };

    /** The lists of one race, in the order of the appearance fields 2 to 11. */
    [[nodiscard]] std::array<const NameList*, state::kAppearanceFieldCount> lists_of(const Race& race) const;

    std::vector<Gender> m_genders;
    std::map<std::string, NameList> m_skinTones;
    std::map<std::string, HairLists> m_hairLists;
    NameList m_eyeColors;
    NameList m_faceMarks;
    NameList m_scars;
    NameList m_tattoos;
    NameList m_tattooColors;
};

} // namespace soulwarden::backend::gamedata
