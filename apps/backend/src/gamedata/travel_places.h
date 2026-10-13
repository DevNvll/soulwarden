#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "state/store.h"

// The travel places: the places of the world to which the travel command can move a character
// (protocol notes 4.16). Each one is a place where the game itself puts a character: a spirit
// shrine (the arrival place of a fast travel) or the respawn place of an inn, with its name,
// its territory, and a position on the ground.
//
// The data is game data: the tool tools/gamedata/places.sh makes the file from the game copy
// of the user, and the file stays outside the repository. Without the file the backend starts
// and works: the travel command then takes only a position in numbers.

namespace soulwarden::backend::gamedata {

/** File name of the travel places in the game data directory of the backend. */
inline constexpr const char* kTravelPlacesFile = "travel-places.json";

struct TravelPlace {
    /** The text of the place for a command: letters, digits, and "_" ("shrine_40170"). */
    std::string id;
    /** "shrine" or "inn". */
    std::string kind;
    /** English name of the place, and of its territory. For a list. */
    std::string name;
    std::string territory;
    std::uint32_t territoryId{0};
    /** A position on the ground: the client lets the character fall to the ground from the spawn lift. */
    state::WorldPosition position;
};

class TravelPlaces {
public:
    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a place
     * with no id or with an id of a wrong form, two places with one id, a position that is
     * not a place for a character (a number that is not finite, a height outside the range of
     * the wire), and a member "counts" whose number is not the size of the list.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /**
     * Sets the places with no file (tests), with the same check as the places of a file.
     * @return False when a place is not correct: the object then has no place.
     */
    [[nodiscard]] bool set(std::vector<TravelPlace> places);

    [[nodiscard]] bool loaded() const {
        return !m_places.empty();
    }
    /** @return Why the last load gave no place. Empty after a load that gave places. */
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<TravelPlace>& places() const {
        return m_places;
    }
    /** @return The number of different territories of the places. */
    [[nodiscard]] std::size_t territory_count() const {
        return m_territories;
    }

    /** @return The place with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const TravelPlace* find(std::string_view id) const;

private:
    std::vector<TravelPlace> m_places;
    /** Lower-case id to the position in the list. */
    std::unordered_map<std::string, std::size_t> m_byId;
    std::size_t m_territories{0};
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata
