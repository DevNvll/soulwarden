#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
//
// The main menu of the client makes the body of a character from this text (stage U4). The
// original server made the text again from the character at each publish (0x67F0600, with
// PaperdollComponent::WriteEquipmentToPlayerSaveData): the backend makes it at each request of
// the character list with one writer (make_menu_document), from the values of the character.

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
 * The keys of the map `equipment` of PlayerSaveData: the visual slot name of each paperdoll slot 0 to 4 (head, chest, hands,
 * legs, feet), with the slot number as the index. A wire text of the client: m_paperdollVisualSlotMapping of
 * slices/player.dynamicslice, and the keys of each create request of the client (5 of 5 captures). The original server wrote
 * only these five keys (0x673EE40 returns with no entry for a slot above 4): the menu has no weapon, no jewelry, and no tool.
 */
inline constexpr std::array<std::string_view, 5> kMenuSlotKeys{"hat", "shirt", "gloves", "pants", "boots"};

/** One entry of the map `equipment` of the menu document (EquipmentSaveData of the client, reflection 0x36FC490). */
struct MenuEquipment {
    /** The paperdoll slot, 0 to 4: the key of the entry is kMenuSlotKeys[slot]. */
    std::uint32_t slot{0};
    /** The value of `appearanceId` (AZ::Crc32): the CRC-32 of the lower-case name of an armor appearance row. */
    std::uint32_t appearanceId{0};
};

/** The values of the menu document of a character. */
struct MenuDocument {
    /** The name of the character: the member `playerName`, and `<name>'s Company` as `companyName`. */
    std::string name;
    /** The names of the character creation databases of the client (the gender first). */
    AppearanceNames appearance;
    /** The archetype of the character, for example "Archetype_Soldier". Empty: a character that the backend seeded has none. */
    std::string backstory;
    std::string worldId;
    /** The level of the screen (1 is the first level): the member `currentLevel` is this level minus 1 (protocol notes 2.2.5). */
    std::uint32_t level{1};
    /** The entries of the map `equipment`, in this sequence. Only a slot of kMenuSlotKeys gets an entry. */
    std::vector<MenuEquipment> equipment;
};

/** @return True for a female body: the gender name "Female". Each other text is a male body (entityClassName, armor appearance). */
[[nodiscard]] bool female_body(const AppearanceNames& appearance);

/**
 * Reads the creation data of a character.
 * @return No value when the text is not creation data in the form of the client.
 */
[[nodiscard]] std::optional<CreationData> read_creation_data(const std::string& text);

/**
 * The one writer of the menu document of a character (the member PublishedData of the character list): creation data in the
 * form of the client. The object stream has each field of the creation request of the client in its sequence (protocol notes
 * 2.2.5): the values that the client writes for a new character, and the map `equipment` with the entries of `values`. Each
 * entry has an empty `itemName` and the dye 0 in its four channels (the backend has no dye). A tool or a test can use the text
 * as the creation data of a create request too.
 * @return The text for the member PublishedData of a character.
 */
[[nodiscard]] std::string make_menu_document(const MenuDocument& values);

} // namespace soulwarden::backend::state
