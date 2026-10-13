#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

// Creation data of a character: the text that the client sends as CharacterCreationParams in
// the create request, and reads as PublishedData of a character of the login information.
//
//   text        := base64( zlib( document ) )            standard alphabet
//   document    := {"currency":0,"visualsBlob":"<bytes>","position":{...},"currentLevel":0,
//                   "worldId":"<world id>"}
//   visualsBlob := binary engine object stream of the class PlayerSaveData. The JSON writer of
//                  the client copies bytes of 0x80 and more with no change, thus the document
//                  is not valid UTF-8 text. Read it as bytes.
// Client writer at image RVA 0x2EB0FE0, 0x17A75B0 (document), and 0x16A6030 (zlib, base64).

namespace soulwarden::backend::state {

/** Fields of MB::PlayerAppearanceComponentReplicatedState, in the order of the state. */
inline constexpr std::size_t kAppearanceGender = 0;
inline constexpr std::size_t kAppearanceRace = 1;
inline constexpr std::size_t kAppearanceSkinTone = 2;
inline constexpr std::size_t kAppearanceHairstyle = 3;
inline constexpr std::size_t kAppearanceFacialHair = 4;
inline constexpr std::size_t kAppearanceHairColor = 5;
inline constexpr std::size_t kAppearanceFacialHairColor = 6;
inline constexpr std::size_t kAppearanceEyeColor = 7;
inline constexpr std::size_t kAppearanceFaceMark = 8;
inline constexpr std::size_t kAppearanceScar = 9;
inline constexpr std::size_t kAppearanceTattoo = 10;
inline constexpr std::size_t kAppearanceTattooColor = 11;
inline constexpr std::size_t kAppearanceFieldCount = 12;

/** Appearance as names of the character creation databases of the client, for example "skin04". */
using AppearanceNames = std::array<std::string, kAppearanceFieldCount>;
/** Appearance as list indexes of those databases: the values of the appearance state. */
using AppearanceIndexes = std::array<std::uint8_t, kAppearanceFieldCount>;

/**
 * Appearance of a character that has no creation data (a character that the backend seeded),
 * or when the backend has no appearance tables: male, the first race, the fourth skin tone
 * (the default skin tone of that race), the first hairstyle that is not "None".
 */
inline constexpr AppearanceIndexes kDefaultAppearance{0, 0, 3, 1, 0, 0, 0, 0, 0, 0, 0, 0};

/** Values of the creation data that the backend uses. */
struct CreationData {
    /** Backstory of the archetype that the user selected, for example "Archetype_Ranger". */
    std::string archetype;
    AppearanceNames appearance;
};

/**
 * Reads the creation data of a character.
 * @return No value when the text is not creation data in the form of the client.
 */
[[nodiscard]] std::optional<CreationData> read_creation_data(const std::string& text);

/**
 * Makes creation data in the form of the client for a character that has none. The object
 * stream has each field of the creation data of the client, with no clothing. The menu of
 * the client makes the body of a character from this text.
 * @param appearance Names of the character creation databases of the client.
 * @param backstory The archetype of the character, for example "Archetype_Soldier". Empty: a
 *        character that the backend seeded has none.
 * @return The text for the member PublishedData of a character.
 */
[[nodiscard]] std::string make_creation_data(const std::string& characterName,
                                             const AppearanceNames& appearance,
                                             const std::string& worldId,
                                             const std::string& backstory = {});

/**
 * @return The creation data with a new value of the member `currentLevel` of its document: the
 *         level that the main menu of the client shows for the character, plus 1 (the member
 *         is the level of the screen minus 1). The text with no change when it is not creation
 *         data in the form of the client, or when its document has no such member.
 * @param level The level of the screen (1 is the first level).
 */
[[nodiscard]] std::string with_current_level(const std::string& text, std::uint32_t level);

} // namespace soulwarden::backend::state
