#include "gamedata/appearance_tables.h"

#include <cctype>
#include <fstream>

#include <nlohmann/json.hpp>

#include "core/logging/log.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

bool equal_names(const std::string& left, const std::string& right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::tolower(static_cast<unsigned char>(left[index]))
            != std::tolower(static_cast<unsigned char>(right[index]))) {
            return false;
        }
    }
    return true;
}

/** @return The texts of a JSON list. An entry that is not a text becomes an empty text. */
std::vector<std::string> name_list(const json& parent, const char* key) {
    std::vector<std::string> names;
    if (!parent.is_object() || !parent.contains(key) || !parent[key].is_array()) {
        return names;
    }
    for (const json& entry : parent[key]) {
        names.push_back(entry.is_string() ? entry.get<std::string>() : std::string{});
    }
    return names;
}

std::string text_member(const json& parent, const char* key) {
    return parent.is_object() && parent.contains(key) && parent[key].is_string() ? parent[key].get<std::string>()
                                                                                 : std::string{};
}

/** @return The index of a name in a list. Index 0 and a log line when the list does not have it. */
std::uint8_t index_of(const std::vector<std::string>& list, const std::string& name, const char* field) {
    constexpr std::size_t kMaxIndex = 0xFF;
    for (std::size_t index = 0; index < list.size() && index <= kMaxIndex; ++index) {
        if (equal_names(list[index], name)) {
            return static_cast<std::uint8_t>(index);
        }
    }
    log::write("gamedata: the appearance tables have no %s \"%s\": index 0 is used", field, name.c_str());
    return 0;
}

} // namespace

bool AppearanceTables::load(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        return false;
    }
    std::vector<Gender> genders;
    for (const std::string& genderName : name_list(document, "genders")) {
        Gender gender;
        gender.name = genderName;
        if (document.contains(genderName) && document[genderName].is_object()
            && document[genderName].contains("races") && document[genderName]["races"].is_array()) {
            for (const json& entry : document[genderName]["races"]) {
                gender.races.push_back({text_member(entry, "name"),
                                        text_member(entry, "skinTones"),
                                        text_member(entry, "hairStyles"),
                                        text_member(entry, "facialHair")});
            }
        }
        if (gender.races.empty()) {
            return false;
        }
        genders.push_back(std::move(gender));
    }
    if (genders.empty()) {
        return false;
    }
    m_skinTones.clear();
    if (document.contains("skinTones") && document["skinTones"].is_object()) {
        for (const auto& [key, value] : document["skinTones"].items()) {
            m_skinTones[key] = name_list(document["skinTones"], key.c_str());
        }
    }
    m_hairLists.clear();
    if (document.contains("hairStyles") && document["hairStyles"].is_object()) {
        for (const auto& [key, value] : document["hairStyles"].items()) {
            m_hairLists[key] = {name_list(value, "styles"), name_list(value, "colors")};
        }
    }
    m_eyeColors = name_list(document, "eyeColors");
    m_faceMarks = name_list(document, "faceMarks");
    m_scars = name_list(document, "scars");
    m_tattoos = name_list(document, "tattoos");
    m_tattooColors = name_list(document, "tattooColors");
    m_genders = std::move(genders);
    return true;
}

std::optional<state::AppearanceIndexes> AppearanceTables::indexes(const state::AppearanceNames& names) const {
    if (!loaded()) {
        return std::nullopt;
    }
    state::AppearanceIndexes result{};
    std::vector<std::string> genderNames;
    for (const Gender& gender : m_genders) {
        genderNames.push_back(gender.name);
    }
    result[state::kAppearanceGender] = index_of(genderNames, names[state::kAppearanceGender], "gender");
    const Gender& gender = m_genders[result[state::kAppearanceGender]];
    std::vector<std::string> raceNames;
    for (const Race& race : gender.races) {
        raceNames.push_back(race.name);
    }
    result[state::kAppearanceRace] = index_of(raceNames, names[state::kAppearanceRace], "race");
    const Race& race = gender.races[result[state::kAppearanceRace]];

    constexpr std::array<const char*, state::kAppearanceFieldCount> kLabels{
        "gender",            "race",      "skin tone", "hairstyle", "facial hair", "hair color",
        "facial hair color", "eye color", "face mark", "scar",      "tattoo",      "tattoo color"};
    const auto lists = lists_of(race);
    for (std::size_t field = state::kAppearanceSkinTone; field < state::kAppearanceFieldCount; ++field) {
        result[field] = index_of(*lists[field], names[field], kLabels[field]);
    }
    return result;
}

std::optional<state::AppearanceNames> AppearanceTables::names(const state::AppearanceIndexes& indexes) const {
    if (!loaded() || indexes[state::kAppearanceGender] >= m_genders.size()) {
        return std::nullopt;
    }
    const Gender& gender = m_genders[indexes[state::kAppearanceGender]];
    if (indexes[state::kAppearanceRace] >= gender.races.size()) {
        return std::nullopt;
    }
    const Race& race = gender.races[indexes[state::kAppearanceRace]];
    state::AppearanceNames result;
    result[state::kAppearanceGender] = gender.name;
    result[state::kAppearanceRace] = race.name;
    const auto lists = lists_of(race);
    for (std::size_t field = state::kAppearanceSkinTone; field < state::kAppearanceFieldCount; ++field) {
        if (indexes[field] >= lists[field]->size()) {
            return std::nullopt;
        }
        result[field] = (*lists[field])[indexes[field]];
    }
    return result;
}

std::array<const AppearanceTables::NameList*, state::kAppearanceFieldCount> AppearanceTables::lists_of(
    const Race& race) const {
    static const NameList kNoNames;
    static const HairLists kNoHair;
    const auto skinTones = m_skinTones.find(race.skinTones);
    const auto hair = m_hairLists.find(race.hairStyles);
    const auto facialHair = m_hairLists.find(race.facialHair);
    const HairLists& hairLists = hair != m_hairLists.end() ? hair->second : kNoHair;
    const HairLists& facialHairLists = facialHair != m_hairLists.end() ? facialHair->second : kNoHair;
    std::array<const NameList*, state::kAppearanceFieldCount> lists{};
    // The gender and the race are not names of a list of the race.
    lists[state::kAppearanceGender] = &kNoNames;
    lists[state::kAppearanceRace] = &kNoNames;
    lists[state::kAppearanceSkinTone] = skinTones != m_skinTones.end() ? &skinTones->second : &kNoNames;
    lists[state::kAppearanceHairstyle] = &hairLists.styles;
    lists[state::kAppearanceFacialHair] = &facialHairLists.styles;
    lists[state::kAppearanceHairColor] = &hairLists.colors;
    lists[state::kAppearanceFacialHairColor] = &facialHairLists.colors;
    lists[state::kAppearanceEyeColor] = &m_eyeColors;
    lists[state::kAppearanceFaceMark] = &m_faceMarks;
    lists[state::kAppearanceScar] = &m_scars;
    lists[state::kAppearanceTattoo] = &m_tattoos;
    lists[state::kAppearanceTattooColor] = &m_tattooColors;
    return lists;
}

} // namespace soulwarden::backend::gamedata
