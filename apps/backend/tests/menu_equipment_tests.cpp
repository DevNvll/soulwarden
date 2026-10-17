// Cases of stage U4: the equipment of each character in the character list of the main menu. The backend makes the menu document
// (the member PublishedData of GET /prod/game/getlogininfo) at each request with one writer: the values of the character and the
// armor of its paperdoll slots 0 to 4 (the rule of the original server, 0x673EE40). All items, names, and ids are made up.
//
//   ME1  the writer: the bytes of one entry of the map `equipment` (the form of the create request of the client)
//   ME2  a male character: the entries of the head and the chest with the M names; no weapon, no jewelry, no item of the bag
//   ME3  a female character: the same items with the F names
//   ME4  two sessions (MP-10): an equip and an unequip of a client; each account gets only its own characters and equipment
//   ME5  a seeded character: the default male body with the entries of its paperdoll; with no appearance tables no document
//   ME6  an item with no look: no tag Armor, an empty name, an item that the catalog does not have
//   ME7  the stored creation data does not change (no archetype helmet); the level; the cost (one item query for each character)
//   ME8  the item catalog of format 5 (the tool, the loader) and a file of format 4 (the backend works, no entry)
//
// The proof that a case fails on the tree before the stage (4c6f77d): that tree builds this file with SOULWARDEN_U4_BASELINE. The block
// below then gives the functions of the stage the behavior of that tree: the stored creation data with the level of the row, no entry,
// no appearance column, and no count of the item queries.

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/encoding/base64.h"
#include "gamedata/item_catalog.h"
#include "middleware/compression/zlib.h"
#include "server/hub/wire.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/item_requests.h"
#include "state/creation_data.h"
#include "state/world.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "world/inventory.h"

#ifdef SOULWARDEN_U4_BASELINE
namespace soulwarden::backend::state {
inline constexpr std::array<std::string_view, 5> kMenuSlotKeys{"hat", "shirt", "gloves", "pants", "boots"};
struct MenuEquipment {
    std::uint32_t slot{0};
    std::uint32_t appearanceId{0};
};
struct MenuDocument {
    std::string name;
    AppearanceNames appearance;
    std::string backstory;
    std::string worldId;
    std::uint32_t level{1};
    std::vector<MenuEquipment> equipment;
};
inline std::string make_menu_document(const MenuDocument& values) {
    return with_current_level(make_creation_data(values.name, values.appearance, values.worldId, values.backstory), values.level);
}
} // namespace soulwarden::backend::state
namespace soulwarden::backend::world {
inline std::uint32_t menu_appearance(const gamedata::ItemDefinition&, bool) {
    return 0;
}
inline std::vector<state::MenuEquipment> menu_equipment(const gamedata::ItemCatalog&, const std::vector<state::ItemRow>&, bool) {
    return {};
}
} // namespace soulwarden::backend::world
#endif

namespace soulwarden::backend::tests {

namespace {

#ifndef SOULWARDEN_U4_BASELINE
void set_appearance(gamedata::ItemDefinition& definition, std::uint32_t male, std::uint32_t female) {
    definition.armorAppearanceM = male;
    definition.armorAppearanceF = female;
}
std::pair<std::uint32_t, std::uint32_t> appearance_of(const gamedata::ItemDefinition& definition) {
    return {definition.armorAppearanceM, definition.armorAppearanceF};
}
bool has_appearances(const gamedata::ItemCatalog& catalog) {
    return catalog.has_armor_appearances();
}
std::uint64_t item_queries(const state::Store& store) {
    return store.item_queries();
}
#else
void set_appearance(gamedata::ItemDefinition&, std::uint32_t, std::uint32_t) {}
std::pair<std::uint32_t, std::uint32_t> appearance_of(const gamedata::ItemDefinition&) {
    return {0, 0};
}
bool has_appearances(const gamedata::ItemCatalog&) {
    return false;
}
std::uint64_t item_queries(const state::Store&) {
    return 0;
}
#endif

using nlohmann::json;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr float kHeight = 50.0F;

// The made-up appearance values of the catalog of the cases: the CRC-32 of each lower-case name (zlib), for example
// m_test_helm and f_test_helm.
constexpr std::uint32_t kHelmM = 0x3029B902;
constexpr std::uint32_t kHelmF = 0x13083E46;
constexpr std::uint32_t kShirtM = 0x459FA006;
constexpr std::uint32_t kShirtF = 0x340D0408;
constexpr std::uint32_t kBootsM = 0x497B681A;
constexpr std::uint32_t kBootsF = 0x38E9CC14;
constexpr std::uint32_t kSkinHat = 0x968223F0;
constexpr std::uint32_t kEmptyHelmF = 0xD76FD5DC;
// m_review_helm and f_review_helm: the names of the file of the tool check (case "appearance").
constexpr std::uint32_t kReviewHelmM = 0xEAF6F55E;
constexpr std::uint32_t kReviewHelmF = 0x0D3F4AFD;

// Field names of PlayerSaveData and of an entry of its map (CRC-32 of the lower-case field name).
constexpr std::uint32_t kEquipmentField = 0xD338D583;
constexpr std::uint32_t kKeyField = 0xA2756C5A;
constexpr std::uint32_t kEntryField = 0x3B7C3DE0;
constexpr std::uint32_t kAppearanceIdField = 0xCDCEA5F5;
constexpr std::uint32_t kValueField = 0x1D775834;
constexpr std::uint32_t kPlayerNameField = 0x6E6926DF;
constexpr std::uint32_t kGenderField = 0xC7470A42;
constexpr std::uint32_t kEntityClassField = 0xE9F5E620;
constexpr std::uint32_t kBackstoryField = 0x7DC4476D;

gamedata::ItemDefinition item(const char* id, const char* type, const char* itemClass, std::uint32_t male, std::uint32_t female) {
    gamedata::ItemDefinition definition;
    definition.id = id;
    definition.nameKey = std::string("@") + id;
    definition.type = type;
    definition.itemClass = itemClass;
    definition.maxStack = 1;
    definition.durability = 100;
    definition.minGearScore = 100;
    definition.maxGearScore = 100;
    definition.weight = 10;
    set_appearance(definition, male, female);
    return definition;
}

/** The made-up items of the cases: class texts of the item tables of the game. */
std::vector<gamedata::ItemDefinition> menu_items() {
    return {item("TestHelm", "Armor", "EquippableHead+Armor+Light", kHelmM, kHelmF),
            item("TestShirt", "Armor", "EquippableChest+Armor", kShirtM, kShirtF),
            item("TestBoots", "Armor", "EquippableFeet+Armor", kBootsM, kBootsF),
            item("TestSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 0, 0),
            item("TestRing", "Armor", "EquippableRing+Jewelry", 0, 0),
            // A skin display item: a name in its cells, and no tag Armor.
            item("TestSkinHat", "Armor", "EquippableHead", kSkinHat, kSkinHat),
            // An armor item with an empty cell ArmorAppearanceM.
            item("TestEmptyHelm", "Armor", "EquippableHead+Armor", 0, kEmptyHelmF),
            item("TestOre", "Resource", "Resource", 0, 0)};
}

/** @return An item row with a new made-up instance id. */
state::ItemRow item_row(int container, std::uint32_t slot, const char* itemId) {
    static unsigned counter = 0;
    char instance[33];
    std::snprintf(instance, sizeof(instance), "0000000000000000000000000000%04x", ++counter);
    state::ItemRow row;
    row.instanceId = instance;
    row.container = container;
    row.slot = slot;
    row.itemId = itemId;
    row.quantity = 1;
    row.gearScore = 100;
    row.durability = 100;
    row.maxDurability = 100;
    return row;
}

/** @return Made-up creation data of a created character of a gender: the form of the client (the writer of the backend). */
state::MenuDocument creation_values(const std::string& name, const char* gender) {
    state::MenuDocument values;
    values.name = name;
    values.appearance = {gender, "face01", "skin02", "hair01", "None", "haircolor01", "haircolor01", "eyecolor1", "none",
                         "none", "none", "tattoocolor1"};
    values.backstory = "Archetype_Test";
    values.worldId = state::local_world().worldId;
    return values;
}

/** One element of the object stream of a menu document. */
struct Element {
    std::uint32_t name{0};
    std::vector<std::uint8_t> value;
    std::vector<Element> children;
};

bool read_element(const std::vector<std::uint8_t>& stream, std::size_t& position, Element& element) {
    if (position >= stream.size() || stream[position] == 0) {
        return false;
    }
    const std::uint8_t flags = stream[position++];
    const auto take = [&](std::size_t count) {
        if (stream.size() - position < count) {
            return false;
        }
        position += count;
        return true;
    };
    if ((flags & 0x40) != 0) {
        if (stream.size() - position < 4) {
            return false;
        }
        element.name = (static_cast<std::uint32_t>(stream[position]) << 24) | (static_cast<std::uint32_t>(stream[position + 1]) << 16)
                       | (static_cast<std::uint32_t>(stream[position + 2]) << 8) | stream[position + 3];
        position += 4;
    }
    if (((flags & 0x80) != 0 && !take(1)) || !take(16)) {
        return false;
    }
    if ((flags & 0x10) != 0) {
        std::size_t size = flags & 0x07;
        if ((flags & 0x20) != 0) {
            const std::size_t bytes = size;
            size = 0;
            for (std::size_t index = 0; index < bytes && position < stream.size(); ++index) {
                size = (size << 8) | stream[position++];
            }
        }
        if (stream.size() - position < size) {
            return false;
        }
        element.value.assign(stream.begin() + static_cast<std::ptrdiff_t>(position), stream.begin() + static_cast<std::ptrdiff_t>(position + size));
        position += size;
    }
    while (position < stream.size() && stream[position] != 0) {
        Element child;
        if (!read_element(stream, position, child)) {
            return false;
        }
        element.children.push_back(std::move(child));
    }
    if (position >= stream.size()) {
        return false;
    }
    ++position; // The end of the element.
    return true;
}

const Element* child_of(const Element& element, std::uint32_t name) {
    for (const Element& child : element.children) {
        if (child.name == name) {
            return &child;
        }
    }
    return nullptr;
}

/** The values of a menu document that the cases compare. */
struct MenuView {
    bool ok{false};
    /** The JSON document (with the bytes of the stream). */
    std::string document;
    std::vector<std::uint8_t> stream;
    std::string playerName;
    std::string gender;
    std::string entityClass;
    std::string backstory;
    /** The entries of the map `equipment` in their sequence: key and appearanceId. */
    std::vector<std::pair<std::string, std::uint32_t>> equipment;

    /** @return The entries as text, for example "hat:3029B902 shirt:459FA006 ". */
    [[nodiscard]] std::string entries() const {
        std::string text;
        for (const auto& [key, value] : equipment) {
            char number[9];
            std::snprintf(number, sizeof(number), "%08X", value);
            text += key + ":" + number + " ";
        }
        return text;
    }
};

/** Reads a menu document: base64, zlib, the member visualsBlob (bytes with the escapes of the client writer), the stream. */
MenuView read_menu(const std::string& text) {
    MenuView view;
    std::string compressed;
    std::vector<std::uint8_t> document;
    if (!encoding::base64_decode(text, compressed)
        || !compression::zlib_decode(std::span(reinterpret_cast<const std::uint8_t*>(compressed.data()), compressed.size()), 1 << 20,
                                     document)) {
        return view;
    }
    view.document.assign(document.begin(), document.end());
    const std::string member = "\"visualsBlob\":\"";
    std::size_t position = view.document.find(member);
    if (position == std::string::npos) {
        return view;
    }
    position += member.size();
    while (position < view.document.size() && view.document[position] != '"') {
        const char character = view.document[position++];
        if (character != '\\') {
            view.stream.push_back(static_cast<std::uint8_t>(character));
            continue;
        }
        const char escape = view.document[position++];
        switch (escape) {
        case 'b':
            view.stream.push_back(0x08);
            break;
        case 'f':
            view.stream.push_back(0x0C);
            break;
        case 'n':
            view.stream.push_back(0x0A);
            break;
        case 'r':
            view.stream.push_back(0x0D);
            break;
        case 't':
            view.stream.push_back(0x09);
            break;
        case 'u':
            view.stream.push_back(static_cast<std::uint8_t>(std::stoi(view.document.substr(position, 4), nullptr, 16)));
            position += 4;
            break;
        default:
            view.stream.push_back(static_cast<std::uint8_t>(escape));
            break;
        }
    }
    std::size_t at = 5; // The tag and the stream version.
    Element root;
    if (view.stream.size() < at || !read_element(view.stream, at, root) || at + 1 != view.stream.size() || view.stream[at] != 0) {
        return view;
    }
    const auto text_of = [&root](std::uint32_t name) {
        const Element* field = child_of(root, name);
        return field != nullptr ? std::string(field->value.begin(), field->value.end()) : std::string{};
    };
    view.playerName = text_of(kPlayerNameField);
    view.gender = text_of(kGenderField);
    view.entityClass = text_of(kEntityClassField);
    view.backstory = text_of(kBackstoryField);
    const Element* map = child_of(root, kEquipmentField);
    if (map == nullptr) {
        return view;
    }
    for (const Element& pair : map->children) {
        const Element* key = child_of(pair, kKeyField);
        const Element* entry = child_of(pair, kEntryField);
        const Element* appearance = entry != nullptr ? child_of(*entry, kAppearanceIdField) : nullptr;
        const Element* value = appearance != nullptr ? child_of(*appearance, kValueField) : nullptr;
        if (key == nullptr || value == nullptr || value->value.size() != 4) {
            return view;
        }
        const std::uint32_t number = (static_cast<std::uint32_t>(value->value[0]) << 24) | (static_cast<std::uint32_t>(value->value[1]) << 16)
                                     | (static_cast<std::uint32_t>(value->value[2]) << 8) | value->value[3];
        view.equipment.emplace_back(std::string(key->value.begin(), key->value.end()), number);
    }
    view.ok = true;
    return view;
}

/** One character of the character list of the gateway. */
struct Listed {
    std::string name;
    bool hasDocument{false};
    MenuView menu;
};

/** A test host with the gateway routes and the made-up catalog. */
struct MenuHost {
    ServerHost host;
    server::http::Router router;
    server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};

    MenuHost() {
        host.catalog.set(menu_items());
        server::http::register_gateway_routes(router, services);
    }

    /** @return The characters of the character list of an account, with the id as the key. An empty map for a reply that is not 200. */
    std::map<std::string, Listed> list(const std::string& personaId) {
        service::HttpRequest request;
        request.method = "GET";
        request.host = "gateway.test";
        request.path = "/prod/game/getlogininfo";
        request.target = request.path;
        request.headers.emplace_back("x-nw-auth", encoding::local_jwt(json{{"personaId", personaId}}.dump()));
        std::string route;
        const service::HttpResponse reply = router.dispatch(request, route);
        const json body = json::parse(reply.body, nullptr, false);
        std::map<std::string, Listed> found;
        if (reply.status != 200 || !body.is_object()) {
            return found;
        }
        for (const json& entry : body["LoginInfoList"]["Characters"]) {
            Listed listed;
            listed.name = entry["Name"].get<std::string>();
            listed.hasDocument = entry.contains("PublishedData");
            if (listed.hasDocument) {
                listed.menu = read_menu(entry["PublishedData"].get<std::string>());
            }
            found[entry["CharacterId"].get<std::string>()] = std::move(listed);
        }
        return found;
    }

    /** Adds an account. @return Its persona id. */
    std::string account(const char* steamId, const char* personaName) {
        state::AccountOrigin origin{};
        const auto made = host.store.login_account(steamId, personaName, false, origin);
        return made ? made->personaId : std::string{};
    }

    /** Adds a created character (creation data of a gender) with item rows. @return True when the store has it. */
    bool character(const std::string& personaId, const std::string& id, const std::string& name, const char* gender,
                   const std::vector<state::ItemRow>& rows) {
        state::Character character;
        character.characterId = id;
        character.personaId = personaId;
        character.worldId = state::local_world().worldId;
        character.name = name;
        character.createdDate = "2026-01-01T00:00:00.000Z";
        character.modifiedDate = character.createdDate;
        character.creationParams = state::make_menu_document(creation_values(name, gender));
        character.archetype = "Archetype_Test";
        return host.store.insert_character(character, rows) == state::InsertResult::inserted;
    }
};

/** Writes the made-up appearance tables (the form of tools/gamedata/appearance.sh) and loads them. @return True when they loaded. */
bool load_tables(gamedata::AppearanceTables& tables, const Options& options) {
    const json document = {
        {"genders", json::array({"Male", "Female"})},
        {"Male", {{"races", json::array({{{"name", "face01"}, {"skinTones", "skins"}, {"hairStyles", "hairM"}, {"facialHair", "beardM"}}})}}},
        {"Female", {{"races", json::array({{{"name", "face01f"}, {"skinTones", "skins"}, {"hairStyles", "hairF"}, {"facialHair", "beardF"}}})}}},
        {"skinTones", {{"skins", json::array({"skin01", "skin02", "skin03", "skin04"})}}},
        {"hairStyles",
         {{"hairM", {{"styles", json::array({"None", "hair01m"})}, {"colors", json::array({"haircolor01"})}}},
          {"beardM", {{"styles", json::array({"None"})}, {"colors", json::array({"haircolor01"})}}},
          {"hairF", {{"styles", json::array({"None", "hair01f"})}, {"colors", json::array({"haircolor01"})}}},
          {"beardF", {{"styles", json::array({"None"})}, {"colors", json::array({"haircolor01"})}}}}},
        {"eyeColors", json::array({"eyecolor1"})},
        {"faceMarks", json::array({"none"})},
        {"scars", json::array({"none"})},
        {"tattoos", json::array({"none"})},
        {"tattooColors", json::array({"tattoocolor1"})},
    };
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "menu-appearance-tables.json";
    std::ofstream(path) << document.dump();
    return tables.load(path.string());
}

/** The bytes of a hexadecimal text (white space is ignored). */
Bytes bytes_of(const std::string& hex) {
    Bytes bytes;
    std::string digits;
    for (const char character : hex) {
        if (character != ' ' && character != '\n') {
            digits.push_back(character);
        }
    }
    for (std::size_t index = 0; index + 1 < digits.size(); index += 2) {
        bytes.push_back(static_cast<std::uint8_t>(std::stoi(digits.substr(index, 2), nullptr, 16)));
    }
    return bytes;
}

/** ME1: the writer of the menu document and the bytes of one entry. */
void writer_bytes(const Options&) {
    state::MenuDocument values = creation_values("Menutest", "Male");
    const std::string none = state::make_menu_document(values);
    values.equipment = {{1, 0x12345678}};
    const std::string one = state::make_menu_document(values);
    const MenuView empty = read_menu(none);
    const MenuView shirt = read_menu(one);
    // The form of an entry of the create request of the client (the entry "gloves" has 258 bytes; protocol notes 2.2.5), with the key
    // "shirt" and a made-up appearance: the pair, the key (value1), EquipmentSaveData (value2): itemName, appearanceId, dyeSettings.
    const Bytes expected = bytes_of(
        "48 41405E39 EFF030FB33585613874093041334C206"
        " 5D A2756C5A 03AAAB3F5C475A669EBCD5FA4DB353C9 7368697274 00"
        " C8 3B7C3DE0 01 903C9703A791454DA57645EDE8405B70"
        " 58 F45D1CCE 03AAAB3F5C475A669EBCD5FA4DB353C9 00"
        " 48 CDCEA5F5 9F4E062E06A046D485DFE0DA96467D3A"
        " 5C 1D775834 43DA906B7DEF4CA89790854106D3F983 12345678 00"
        " 00"
        " C8 17454143 03 2FA619C9D71E4E889BE916056F7E38FA"
        " 59 51E3A083 FAAD748808C85FCB8FEEAB1EBE4726B3 00 00"
        " 59 65B89FCC FAAD748808C85FCB8FEEAB1EBE4726B3 00 00"
        " 59 2D5891A8 FAAD748808C85FCB8FEEAB1EBE4726B3 00 00"
        " 59 A3D7964B FAAD748808C85FCB8FEEAB1EBE4726B3 00 00"
        " 00 00 00");
    // The map of the stream with no entry: the element `equipment` and its end byte, then the ends of the root and of the stream.
    const Bytes emptyMap = bytes_of("48 D338D583 44C93A82189756D68CF74BC0289915DE 00 00 00");
    const bool emptyForm = empty.stream.size() >= emptyMap.size()
                           && std::equal(emptyMap.begin(), emptyMap.end(), empty.stream.end() - static_cast<std::ptrdiff_t>(emptyMap.size()));
    // The stream with the entry is the stream with no entry and the entry before the end of the map.
    Bytes withEntry = empty.stream;
    withEntry.insert(withEntry.end() - 3, expected.begin(), expected.end());
    check(expected.size() == 257 && emptyForm && empty.ok && empty.equipment.empty() && shirt.ok && shirt.stream == withEntry
              && shirt.entries() == "shirt:12345678 ",
          "the writer: one entry (the key shirt, appearance 0x12345678) has the 257 bytes of the form of the client (258 for the key "
          "gloves) before the end of the map; with no entry the map is empty");
    const auto read = state::read_creation_data(one);
    check(read && read->archetype == "Archetype_Test" && read->appearance == values.appearance && shirt.playerName == "Menutest"
              && shirt.entityClass == "PlayerMale" && shirt.document.find("\"currentLevel\":0,") != std::string::npos,
          "read_creation_data reads the text with an entry: the archetype and the 12 appearance names; the level 1 is currentLevel 0");
    values.equipment = {{0, 1}, {1, 2}, {2, 3}, {3, 4}, {4, 5}, {7, 6}, {24, 7}};
    check(read_menu(state::make_menu_document(values)).entries() == "hat:00000001 shirt:00000002 gloves:00000003 pants:00000004 boots:00000005 ",
          "the keys of the paperdoll slots 0 to 4 are hat, shirt, gloves, pants, boots; a different slot (a ring, a weapon) gets no entry");
}

/** ME2 and ME3: a male and a female character with armor, a weapon, jewelry, and an item of the bag. */
void body_entries(bool female) {
    MenuHost menu;
    const std::string persona = menu.account(kSteamIdA, "Tester A");
    const std::vector<state::ItemRow> rows = {
        item_row(state::kContainerPaperdoll, world::paperdoll::kHead, "TestHelm"), item_row(state::kContainerPaperdoll, world::paperdoll::kChest, "TestShirt"),
        item_row(state::kContainerPaperdoll, world::paperdoll::kMainHand1, "TestSword"), item_row(state::kContainerPaperdoll, world::paperdoll::kRing, "TestRing"),
        item_row(state::kContainerBag, 0, "TestBoots")};
    const bool stored = menu.character(persona, kCharacterA, "Menubody", female ? "Female" : "Male", rows);
    const auto listed = menu.list(persona);
    const auto found = listed.find(kCharacterA);
    const MenuView& view = found != listed.end() ? found->second.menu : MenuView{};
    char expected[64];
    std::snprintf(expected, sizeof(expected), "hat:%08X shirt:%08X ", female ? kHelmF : kHelmM, female ? kShirtF : kShirtM);
    check(stored && listed.size() == 1 && view.ok && view.entries() == expected && view.gender == (female ? "Female" : "Male")
              && view.entityClass == (female ? "PlayerFemale" : "PlayerMale"),
          female ? "a female character: the entries hat and shirt with the CRC of the F names of the helm and the shirt; no entry for the "
                   "sword (slot 24), the ring (slot 7), and the boots in the bag"
                 : "a male character: the entries hat and shirt with the CRC of the M names of the helm and the shirt; no entry for the "
                   "sword (slot 24), the ring (slot 7), and the boots in the bag");
}

void male_body(const Options&) {
    body_entries(false);
}

void female_body(const Options&) {
    body_entries(true);
}

/** Body of PaperdollComponentServerFacet_ServerEquipItem (type 3895), from the writer of the client. */
Bytes equip_body(std::uint64_t entity, std::uint32_t bagSlot, std::uint32_t paperdollSlot, const world::InstanceId& instance) {
    server::hub::Writer body;
    body.u64(entity + 0x62);
    body.u64(entity);
    body.u32(bagSlot);
    body.u32(paperdollSlot);
    body.u32(1);
    body.raw(instance);
    body.u8(0);
    return body.data();
}

/** Body of PaperdollComponentServerFacet_ServerUnequipItem (type 3896). */
Bytes unequip_body(std::uint64_t entity, std::uint32_t paperdollSlot) {
    server::hub::Writer body;
    body.u64(entity + 0x62);
    body.u64(entity);
    body.u32(paperdollSlot);
    body.u32(0xFFFFFFFF);
    body.u32(1);
    body.u8(1);
    return body.data();
}

/** ME4: two sessions (MP-10). */
void two_sessions(const Options& options) {
    MenuHost menu;
    ServerHost& host = menu.host;
    const bool tables = load_tables(host.tables, options);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, kHeight});
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{102.0F, 200.0F, kHeight});
    world::Inventory& inventory = host.world.inventory();
    const bool given = inventory.give(kCharacterA, "TestBoots", 1).ok() && inventory.give(kCharacterB, "TestShirt", 1).ok()
                       && inventory.equip(kCharacterB, 0).ok();
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    const world::Entity* entityA = host.world.find_character(kCharacterA);
    const world::Entity* entityB = host.world.find_character(kCharacterB);
    check(tables && given && entityA != nullptr && entityB != nullptr && entityA->bag.slots.size() > 0 && entityA->bag.slots[0].item,
          "setup: the made-up appearance tables load; A has the boots in bag slot 0, B wears a shirt; both are in the world");
    if (entityA == nullptr || entityB == nullptr || entityA->bag.slots.empty() || !entityA->bag.slots[0].item) {
        return;
    }
    const world::InstanceId boots = entityA->bag.slots[0].item->instance;
    const auto send = [&](FakeClient& client, const world::Entity* route, std::uint32_t type, const Bytes& body) {
        host.datagram(client.peer, client.component_message(type, route->actor.bytes, body));
        host.steps(2, {&a, &b});
    };
    char bootsText[32];
    std::snprintf(bootsText, sizeof(bootsText), "boots:%08X ", kBootsM);
    char shirtText[32];
    std::snprintf(shirtText, sizeof(shirtText), "shirt:%08X ", kShirtM);

    auto listA = menu.list(alpha.account.personaId);
    auto listB = menu.list(beta.account.personaId);
    check(listA.size() == 1 && listA.count(kCharacterA) == 1 && listA[kCharacterA].menu.ok && listA[kCharacterA].menu.equipment.empty()
              && listB.size() == 1 && listB.count(kCharacterB) == 1 && listB[kCharacterB].menu.entries() == shirtText,
          "before the equip: the list of A has only A (no entry), the list of B has only B with its shirt");

    send(a, entityA, server::rep::kTypeEquipItem, equip_body(entityA->id, 0, world::paperdoll::kFeet, boots));
    listA = menu.list(alpha.account.personaId);
    listB = menu.list(beta.account.personaId);
    const std::string whileIn = listA[kCharacterA].menu.entries();
    check(entityA->equipment.item(world::paperdoll::kFeet) != nullptr && whileIn == bootsText && listA.size() == 1
              && listB.size() == 1 && listB[kCharacterB].menu.entries() == shirtText,
          "A equips the boots with the request 3895 while A is in the world: the list of A has the entry boots; the list of B has "
          "only B with only its shirt");

    send(a, entityA, server::rep::kTypeUnequipItem, unequip_body(entityA->id, world::paperdoll::kFeet));
    listA = menu.list(alpha.account.personaId);
    check(entityA->equipment.item(world::paperdoll::kFeet) == nullptr && listA[kCharacterA].menu.ok && listA[kCharacterA].menu.equipment.empty()
              && menu.list(beta.account.personaId)[kCharacterB].menu.entries() == shirtText,
          "A unequips the boots with the request 3896: the entry of A is gone; B has its shirt");

    const world::InstanceId again = entityA->bag.slots[0].item ? entityA->bag.slots[0].item->instance : world::InstanceId{};
    send(a, entityA, server::rep::kTypeEquipItem, equip_body(entityA->id, 0, world::paperdoll::kFeet, again));
    const std::string inWorld = menu.list(alpha.account.personaId)[kCharacterA].menu.entries();
    host.disconnect(a.peer);
    host.steps(3, {&b});
    const std::string afterLeave = menu.list(alpha.account.personaId)[kCharacterA].menu.entries();
    check(inWorld == bootsText && afterLeave == inWorld && host.world.find_character(kCharacterA) == nullptr
              && host.world.find_character(kCharacterB) != nullptr,
          "A equips the boots again and leaves the world: the list of A is the same while A is in the world and after A leaves; B "
          "stays in the world");
}

/** ME5: a character that the backend seeded (no creation data). */
void seeded_character(const Options& options) {
    MenuHost menu;
    ServerHost& host = menu.host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", std::nullopt);
    const bool given = host.world.inventory().give(kCharacterA, "TestShirt", 1).ok() && host.world.inventory().equip(kCharacterA, 0).ok()
                       && host.world.inventory().give(kCharacterA, "TestSword", 1).ok() && host.world.inventory().equip(kCharacterA, 0).ok();
    const auto bare = menu.list(alpha.account.personaId);
    check(given && bare.size() == 1 && bare.count(kCharacterA) == 1 && !bare.at(kCharacterA).hasDocument,
          "a seeded character with no appearance tables: the list has the character and no PublishedData (as before)");
    const bool tables = load_tables(host.tables, options);
    const auto names = host.tables.names(state::kDefaultAppearance);
    auto listed = menu.list(alpha.account.personaId);
    const MenuView& view = listed[kCharacterA].menu;
    char expected[32];
    std::snprintf(expected, sizeof(expected), "shirt:%08X ", kShirtM);
    check(tables && names && (*names)[state::kAppearanceSkinTone] == "skin04" && view.ok && view.gender == "Male"
              && view.entityClass == "PlayerMale" && view.backstory.empty() && view.playerName == "Alpha" && view.entries() == expected
              && read_menu(state::make_menu_document([&] {
                     state::MenuDocument values;
                     values.name = "Alpha";
                     values.appearance = *names;
                     values.worldId = state::local_world().worldId;
                     values.equipment = {{world::paperdoll::kChest, kShirtM}};
                     return values;
                 }())).stream == view.stream,
          "a seeded character with appearance tables: the default male body (the default names, no backstory) with the entry of its "
          "shirt; no entry for its sword");
}

/** ME6: an item with no look. */
void no_look(const Options&) {
    gamedata::ItemCatalog catalog;
    catalog.set(menu_items());
    const std::vector<state::ItemRow> skin = {item_row(state::kContainerPaperdoll, world::paperdoll::kHead, "TestSkinHat")};
    const std::vector<state::ItemRow> empty = {item_row(state::kContainerPaperdoll, world::paperdoll::kHead, "TestEmptyHelm")};
    const std::vector<state::ItemRow> unknown = {item_row(state::kContainerPaperdoll, world::paperdoll::kChest, "TestNotAnItem")};
    const auto entries = world::menu_equipment(catalog, empty, true);
    check(world::menu_equipment(catalog, skin, false).empty() && world::menu_equipment(catalog, skin, true).empty()
              && world::menu_appearance(*catalog.find("TestSkinHat"), false) == 0,
          "a skin display item with no tag Armor (a name in its cells) in slot 0: no entry for a male and for a female body");
    check(world::menu_equipment(catalog, empty, false).empty() && entries.size() == 1 && entries[0].slot == 0
              && entries[0].appearanceId == kEmptyHelmF,
          "an armor item with an empty cell ArmorAppearanceM: no entry for a male body; the female body has the entry of its F name");
    check(world::menu_equipment(catalog, unknown, false).empty() && world::menu_equipment(gamedata::ItemCatalog{}, empty, true).empty(),
          "an item that the catalog does not have, and a backend with no catalog: no entry");

    MenuHost menu;
    const std::string persona = menu.account(kSteamIdA, "Tester A");
    const bool stored = menu.character(persona, kCharacterA, "Nolook", "Male",
                                       {item_row(state::kContainerPaperdoll, world::paperdoll::kHead, "TestSkinHat"),
                                        item_row(state::kContainerPaperdoll, world::paperdoll::kHands, "TestEmptyHelm")});
    auto listed = menu.list(persona);
    check(stored && listed[kCharacterA].menu.ok && listed[kCharacterA].menu.equipment.empty(),
          "the character list: a character with only such items has a document with an empty map");
}

/** ME7: the stored creation data, the level, and the cost of the route. */
void stored_text_and_cost(const Options&) {
    MenuHost menu;
    const std::string persona = menu.account(kSteamIdA, "Tester A");
    const std::string other = menu.account(kSteamIdB, "Tester B");
    // The creation data of the client has the clothing of the archetype, also a helmet that the character does not get (quantity 0).
    state::MenuDocument creation = creation_values("Creator", "Male");
    creation.equipment = {{world::paperdoll::kHead, 0xAAAAAAAA}, {world::paperdoll::kChest, 0xBBBBBBBB}};
    state::Character character;
    character.characterId = kCharacterA;
    character.personaId = persona;
    character.worldId = state::local_world().worldId;
    character.name = "Creator";
    character.createdDate = "2026-01-01T00:00:00.000Z";
    character.modifiedDate = character.createdDate;
    character.creationParams = state::make_menu_document(creation);
    character.archetype = "Archetype_Test";
    std::vector<state::ItemRow> rows = {item_row(state::kContainerPaperdoll, world::paperdoll::kChest, "TestShirt")};
    // A full bag: the route reads only the slots 0 to 4 of the paperdoll.
    for (std::uint32_t slot = 0; slot < 400; ++slot) {
        rows.push_back(item_row(state::kContainerBag, slot, "TestOre"));
    }
    state::ProgressRow level;
    level.level = 33;
    const bool stored = menu.host.store.insert_character(character, rows) == state::InsertResult::inserted
                        && menu.host.store.set_progress(kCharacterA, level);
    const std::string before = menu.host.store.find_character(kCharacterA)->creationParams;
    auto listed = menu.list(persona);
    const std::string after = menu.host.store.find_character(kCharacterA)->creationParams;
    const MenuView& view = listed[kCharacterA].menu;
    char expected[32];
    std::snprintf(expected, sizeof(expected), "shirt:%08X ", kShirtM);
    check(stored && before == character.creationParams && after == before && view.ok && view.entries() == expected
              && view.document.find("\"currentLevel\":32,") != std::string::npos && view.playerName == "Creator"
              && view.backstory == "Archetype_Test" && read_menu(before).entries() == "hat:AAAAAAAA shirt:BBBBBBBB ",
          "a created character: the stored creation data is byte-equal before and after the route; the document has the shirt of "
          "the paperdoll with the value of the catalog, no helmet of the creation data, and currentLevel 32 for the level 33");

    // The cost: one item query for each character of the account (and one level query), none for the characters of a different
    // account.
    for (int index = 2; index <= 4; ++index) {
        const std::string id = "aaaaaaaa-0000-4000-8000-00000000010" + std::to_string(index);
        menu.character(persona, id, "Creator" + std::to_string(index), "Male",
                       {item_row(state::kContainerPaperdoll, world::paperdoll::kFeet, "TestBoots")});
    }
    for (int index = 0; index < 3; ++index) {
        menu.character(other, "bbbbbbbb-0000-4000-8000-00000000020" + std::to_string(index), "Other" + std::to_string(index), "Female",
                       {item_row(state::kContainerPaperdoll, world::paperdoll::kHead, "TestHelm")});
    }
    const std::uint64_t queries = item_queries(menu.host.store);
    listed = menu.list(persona);
    const std::uint64_t used = item_queries(menu.host.store) - queries;
    note("item queries of one character list with 4 characters: " + std::to_string(used));
    check(listed.size() == 4 && used == 4,
          "the character list of an account with 4 characters (one of them with 400 items in the bag) makes 4 item queries: one for "
          "each character, none for the 3 characters of a different account");
}

/** ME8: the item catalog of format 5 and of format 4. */
void catalog_format(const Options& options) {
    const std::filesystem::path work = options.workDirectory / "menu-catalog";
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_item_catalog_tool.py";
    std::filesystem::create_directories(work);
    const std::filesystem::path output = work / "tool.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string()
                                + "\" appearance appearance-missing appearance-invalid > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    int passed = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        passed += line.rfind("ok", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && passed == 3,
          "the item catalog tool (check_item_catalog_tool.py): the cases appearance, appearance-missing, appearance-invalid");

    gamedata::ItemCatalog catalog;
    const bool loaded = catalog.load((work / "item-catalog-appearance.json").string());
    const gamedata::ItemDefinition* helm = catalog.find("ReviewHelm");
    const gamedata::ItemDefinition* skin = catalog.find("ReviewSkinHat");
    const gamedata::ItemDefinition* sword = catalog.find("ReviewSword");
    check(loaded && has_appearances(catalog) && helm != nullptr && skin != nullptr && sword != nullptr
              && appearance_of(*helm) == std::make_pair(kReviewHelmM, kReviewHelmF) && world::menu_appearance(*helm, false) == kReviewHelmM
              && world::menu_appearance(*helm, true) == kReviewHelmF && appearance_of(*skin).first != 0 && world::menu_appearance(*skin, false) == 0
              && appearance_of(*sword) == std::make_pair(0U, 0U),
          "the loader reads format 5: the CRC-32 of the lower-case M and F names of the helm (m_Review_Helm: 0xEAF6F55E); the skin hat "
          "has its names and no look (no tag Armor); an empty cell is 0");

    std::ifstream input(work / "item-catalog-appearance.json");
    const json document = json::parse(input, nullptr, false);
    const auto write = [&](const json& changed) {
        const auto path = work / "item-catalog-form.json";
        std::ofstream(path) << changed.dump();
        return path.string();
    };
    json older = document;
    older["format"] = 4;
    older["columns"].erase(older["columns"].begin() + 15, older["columns"].end());
    for (json& row : older["items"]) {
        row.erase(row.begin() + 15, row.end());
    }
    json shortFive = older;
    shortFive["format"] = 5;
    json longFour = document;
    longFour["format"] = 4;
    json number = document;
    number["items"][0][15] = 7;
    gamedata::ItemCatalog other;
    check(!other.load(write(shortFive)) && !other.load(write(longFour)) && !other.load(write(number)),
          "a file of format 5 with the 15 columns of format 4, a file of format 4 with 17 columns, and a name that is not a text do not "
          "load");

    // A catalog of format 4 (a file from before stage U4): the backend loads it and works; the menu document has no entry.
    MenuHost menu;
    const bool olderLoaded = menu.host.catalog.load(write(older));
    const gamedata::ItemDefinition* oldHelm = menu.host.catalog.find("ReviewHelm");
    const std::string persona = menu.account(kSteamIdA, "Tester A");
    const bool stored = menu.character(persona, kCharacterA, "Oldfile", "Male", {item_row(state::kContainerPaperdoll, world::paperdoll::kHead, "ReviewHelm")});
    auto listed = menu.list(persona);
    check(olderLoaded && !has_appearances(menu.host.catalog) && oldHelm != nullptr && appearance_of(*oldHelm).first == 0 && stored
              && listed.size() == 1 && listed[kCharacterA].menu.ok && listed[kCharacterA].menu.equipment.empty(),
          "a file of format 4 loads (no armor appearance); the character list gives the document with an empty map, as before stage U4");
}

} // namespace

std::vector<Case> menu_equipment_cases() {
    return {
        {"ME1", "stage U4: the writer of the menu document, the bytes of one entry of the map equipment", writer_bytes},
        {"ME2", "stage U4: a male character in the character list: the armor of the slots 0 to 4 with the M names", male_body},
        {"ME3", "stage U4: a female character in the character list: the F names", female_body},
        {"ME4", "stage U4: two sessions (MP-10): an equip and an unequip of a client, each account with its own characters", two_sessions},
        {"ME5", "stage U4: a seeded character: the default body with its armor; no appearance tables: no document", seeded_character},
        {"ME6", "stage U4: an item with no look (no tag Armor, an empty name, not in the catalog): no entry", no_look},
        {"ME7", "stage U4: the stored creation data does not change, the level, one item query for each character", stored_text_and_cost},
        {"ME8", "stage U4: the item catalog of format 5 (tool, loader) and of format 4 (no entry)", catalog_format},
    };
}

} // namespace soulwarden::backend::tests
