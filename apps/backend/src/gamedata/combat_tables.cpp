#include "gamedata/combat_tables.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

// Numbers of the attributes of a character on the wire (world/entity.h, namespace attribute).
constexpr std::size_t kFocus = 1;
constexpr std::size_t kStrength = 2;
constexpr std::size_t kDexterity = 3;
constexpr std::size_t kIntelligence = 4;
/** The member of the file for each attribute that a weapon can have a factor for. */
constexpr std::pair<const char*, std::size_t> kAttributeMembers[] = {
    {"strength", kStrength}, {"dexterity", kDexterity}, {"intelligence", kIntelligence}, {"focus", kFocus}};
/** The use of a melee weapon in the weapon table of the game. */
constexpr const char* kMeleeUse = "Combat Melee";

/** @return The text with the letters A to Z in lower case. */
std::string lower(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

bool read_float(const json& value, float& number) {
    if (!value.is_number()) {
        return false;
    }
    number = value.get<float>();
    return std::isfinite(number);
}

bool read_whole(const json& value, std::uint32_t& number) {
    if (!value.is_number_unsigned() && !(value.is_number_integer() && value.get<std::int64_t>() >= 0)) {
        return false;
    }
    const std::uint64_t wide = value.get<std::uint64_t>();
    if (wide > 0xFFFFFFFFULL) {
        return false;
    }
    number = static_cast<std::uint32_t>(wide);
    return true;
}

/** Reads the values of one defense list: {"<damage type>": value, ...}. */
bool read_type_values(const json& value, const std::unordered_map<std::string, std::uint32_t>& types,
                      std::vector<float>& values) {
    if (!value.is_object()) {
        return false;
    }
    values.assign(types.size(), 0.0F);
    for (const auto& [name, entry] : value.items()) {
        const auto type = types.find(name);
        if (type == types.end() || !read_float(entry, values[type->second])) {
            return false;
        }
    }
    return true;
}

} // namespace

bool CombatTables::check(const CombatData& data, std::string& error) {
    const auto fail = [&error](const char* text) {
        error = text;
        return false;
    };
    const CombatConstants& constants = data.constants;
    if (!std::isfinite(constants.levelDamage) || constants.levelDamage < 0.0F || !std::isfinite(constants.gearScoreRate)
        || constants.gearScoreRate < 0.0F || !std::isfinite(constants.gearScoreSmallerRate)
        || constants.gearScoreSmallerRate < 0.0F || constants.gearScoreInterval == 0
        || constants.gearScoreLimit < constants.gearScoreMinimum) {
        return fail("a constant of the damage rule is not valid");
    }
    if (data.damageTypes.empty() || data.weapons.empty()) {
        return fail("the tables have no damage type or no weapon");
    }
    for (const DamageType& type : data.damageTypes) {
        if (type.name.empty() || type.wire == 0) {
            return fail("a damage type has no name or no number");
        }
    }
    for (const std::vector<float>& values : data.attributeDamage) {
        for (const float value : values) {
            if (!std::isfinite(value) || value < 0.0F) {
                return fail("a damage value of an attribute is not valid");
            }
        }
    }
    for (const DamageRow& row : data.damageRows) {
        if (row.type >= data.damageTypes.size() || !std::isfinite(row.factor) || row.factor < 0.0F
            || !std::isfinite(row.added) || row.added < 0.0F) {
            return fail("a damage row is not valid");
        }
    }
    for (const WeaponRow& weapon : data.weapons) {
        if (!std::isfinite(weapon.baseDamage) || weapon.baseDamage < 0.0F || !std::isfinite(weapon.gearScoreRate)
            || weapon.gearScoreRate < 0.0F || (weapon.lightRow && *weapon.lightRow >= data.damageRows.size())
            || (weapon.heavyRow && *weapon.heavyRow >= data.damageRows.size())) {
            return fail("a weapon is not valid");
        }
        for (const float factor : weapon.scaling) {
            if (!std::isfinite(factor) || factor < 0.0F) {
                return fail("an attribute factor of a weapon is not valid");
            }
        }
    }
    for (const auto& [item, weapon] : data.items) {
        if (weapon >= data.weapons.size()) {
            return fail("an item names a weapon that the tables do not have");
        }
    }
    for (const DefenseRow& defense : data.defenses) {
        // The game tables have values outside the range 0 to 1 (a mitigation of 2.35, an
        // absorption of -15): the damage rule sets the limits (world/combat.h).
        if (!std::isfinite(defense.physical) || !std::isfinite(defense.elemental)
            || defense.weakness.size() != data.damageTypes.size() || defense.absorption.size() != data.damageTypes.size()) {
            return fail("the defense values of a creature are not valid");
        }
        for (std::size_t type = 0; type < data.damageTypes.size(); ++type) {
            if (!std::isfinite(defense.weakness[type]) || !std::isfinite(defense.absorption[type])) {
                return fail("a weakness or an absorption of a creature is not valid");
            }
        }
    }
    for (const auto& [vitals, defense] : data.vitals) {
        if (defense >= data.defenses.size()) {
            return fail("a vitals row names defense values that the tables do not have");
        }
    }
    return true;
}

bool CombatTables::set(CombatData data) {
    std::string error;
    if (!check(data, error)) {
        return false;
    }
    m_data = std::move(data);
    m_error.clear();
    return true;
}

bool CombatTables::load(const std::string& path) {
    const auto fail = [this](const std::string& text) {
        m_data = CombatData{};
        m_error = text;
        return false;
    };
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return fail("the file is not there");
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        return fail("the file is not a JSON object");
    }
    for (const char* member : {"constants", "damageTypes", "attributes", "damageRows", "weapons", "items", "defenses", "vitals"}) {
        if (!document.contains(member)) {
            return fail(std::string("the file has no member ") + member);
        }
    }
    CombatData data;
    const json& constants = document["constants"];
    if (!constants.is_object() || !constants.contains("levelDamage") || !constants.contains("gearScoreRate")
        || !constants.contains("gearScoreSmallerRate") || !constants.contains("gearScoreInterval")
        || !constants.contains("gearScoreMinimum") || !constants.contains("gearScoreLimit")
        || !read_float(constants["levelDamage"], data.constants.levelDamage)
        || !read_float(constants["gearScoreRate"], data.constants.gearScoreRate)
        || !read_float(constants["gearScoreSmallerRate"], data.constants.gearScoreSmallerRate)
        || !read_whole(constants["gearScoreInterval"], data.constants.gearScoreInterval)
        || !read_whole(constants["gearScoreMinimum"], data.constants.gearScoreMinimum)
        || !read_whole(constants["gearScoreLimit"], data.constants.gearScoreLimit)) {
        return fail("the constants do not have the form of the tool");
    }
    // The damage types: [name, number, category].
    std::unordered_map<std::string, std::uint32_t> typeIndex;
    if (!document["damageTypes"].is_array()) {
        return fail("the damage types are not a list");
    }
    for (const json& row : document["damageTypes"]) {
        DamageType type;
        std::uint32_t wire = 0;
        if (!row.is_array() || row.size() != 3 || !row[0].is_string() || !read_whole(row[1], wire) || wire > 255
            || !row[2].is_string()) {
            return fail("a damage type does not have the form of the tool");
        }
        type.name = row[0].get<std::string>();
        type.wire = static_cast<std::uint8_t>(wire);
        const std::string category = row[2].get<std::string>();
        type.category = category == "Physical"    ? DamageCategory::physical
                        : category == "Elemental" ? DamageCategory::elemental
                                                  : DamageCategory::direct;
        if (!typeIndex.emplace(type.name, static_cast<std::uint32_t>(data.damageTypes.size())).second) {
            return fail("a damage type is in the file two times");
        }
        data.damageTypes.push_back(std::move(type));
    }
    // The attributes: {"minimum": N, "<attribute>": [value, ...], ...}.
    const json& attributes = document["attributes"];
    if (!attributes.is_object() || !attributes.contains("minimum") || !read_whole(attributes["minimum"], data.attributeMinimum)) {
        return fail("the attributes do not have the form of the tool");
    }
    for (const auto& [member, attribute] : kAttributeMembers) {
        if (!attributes.contains(member) || !attributes[member].is_array()) {
            return fail(std::string("the attributes have no list ") + member);
        }
        for (const json& entry : attributes[member]) {
            float value = 0.0F;
            if (!read_float(entry, value)) {
                return fail(std::string("a value of the attribute list ") + member + " is not a number");
            }
            data.attributeDamage[attribute].push_back(value);
        }
    }
    // The damage rows: [id, weapon category, attack type, damage type, factor, added].
    std::unordered_map<std::string, std::uint32_t> rowIndex;
    if (!document["damageRows"].is_array()) {
        return fail("the damage rows are not a list");
    }
    for (const json& entry : document["damageRows"]) {
        DamageRow row;
        if (!entry.is_array() || entry.size() != 6 || !entry[0].is_string() || !entry[2].is_string() || !entry[3].is_string()
            || !read_float(entry[4], row.factor) || !read_float(entry[5], row.added)) {
            return fail("a damage row does not have the form of the tool");
        }
        row.id = entry[0].get<std::string>();
        row.attackType = entry[2].get<std::string>();
        const auto type = typeIndex.find(entry[3].get<std::string>());
        if (type == typeIndex.end()) {
            return fail("the damage row " + row.id + " names a damage type that the file does not have");
        }
        row.type = type->second;
        rowIndex.emplace(lower(row.id), static_cast<std::uint32_t>(data.damageRows.size()));
        data.damageRows.push_back(std::move(row));
    }
    // The weapons: [id, use, tags, base damage, strength, dexterity, intelligence, focus, light
    // row, heavy row, gear score rate].
    if (!document["weapons"].is_array()) {
        return fail("the weapons are not a list");
    }
    for (const json& entry : document["weapons"]) {
        WeaponRow weapon;
        if (!entry.is_array() || entry.size() != 11 || !entry[0].is_string() || !entry[1].is_string() || !entry[2].is_string()
            || !read_float(entry[3], weapon.baseDamage) || !read_float(entry[4], weapon.scaling[kStrength])
            || !read_float(entry[5], weapon.scaling[kDexterity]) || !read_float(entry[6], weapon.scaling[kIntelligence])
            || !read_float(entry[7], weapon.scaling[kFocus]) || !entry[8].is_string() || !entry[9].is_string()
            || !read_float(entry[10], weapon.gearScoreRate)) {
            return fail("a weapon does not have the form of the tool");
        }
        weapon.id = entry[0].get<std::string>();
        weapon.melee = entry[1].get<std::string>() == kMeleeUse;
        weapon.tags = entry[2].get<std::string>();
        const auto row_of = [&](const json& name, std::optional<std::uint32_t>& row) {
            const std::string text = name.get<std::string>();
            if (text.empty()) {
                return true;
            }
            const auto found = rowIndex.find(lower(text));
            if (found == rowIndex.end()) {
                return false;
            }
            row = found->second;
            return true;
        };
        if (!row_of(entry[8], weapon.lightRow) || !row_of(entry[9], weapon.heavyRow)) {
            return fail("the weapon " + weapon.id + " names a damage row that the file does not have");
        }
        data.weapons.push_back(std::move(weapon));
    }
    if (!document["items"].is_object()) {
        return fail("the items are not an object");
    }
    for (const auto& [item, weapon] : document["items"].items()) {
        std::uint32_t index = 0;
        if (!read_whole(weapon, index)) {
            return fail("the item " + item + " has no weapon index");
        }
        data.items.emplace(lower(item), index);
    }
    // The defense values: [physical mitigation, elemental mitigation, weakness, absorption].
    if (!document["defenses"].is_array()) {
        return fail("the defense values are not a list");
    }
    for (const json& entry : document["defenses"]) {
        DefenseRow defense;
        if (!entry.is_array() || entry.size() != 4 || !read_float(entry[0], defense.physical)
            || !read_float(entry[1], defense.elemental) || !read_type_values(entry[2], typeIndex, defense.weakness)
            || !read_type_values(entry[3], typeIndex, defense.absorption)) {
            return fail("the defense values of a creature do not have the form of the tool");
        }
        data.defenses.push_back(std::move(defense));
    }
    if (!document["vitals"].is_object()) {
        return fail("the vitals rows are not an object");
    }
    for (const auto& [vitals, defense] : document["vitals"].items()) {
        std::uint32_t index = 0;
        if (!read_whole(defense, index)) {
            return fail("the vitals row " + vitals + " has no defense index");
        }
        data.vitals.emplace(lower(vitals), index);
    }
    std::string error;
    if (!check(data, error)) {
        return fail(error);
    }
    m_data = std::move(data);
    m_error.clear();
    return true;
}

const WeaponRow* CombatTables::weapon_of(std::string_view itemId) const {
    const auto found = m_data.items.find(lower(itemId));
    return found == m_data.items.end() ? nullptr : &m_data.weapons[found->second];
}

const DefenseRow* CombatTables::defense_of(std::string_view vitalsId) const {
    const auto found = m_data.vitals.find(lower(vitalsId));
    return found == m_data.vitals.end() ? nullptr : &m_data.defenses[found->second];
}

float CombatTables::attribute_damage(std::size_t attribute, std::uint32_t value) const {
    if (attribute >= m_data.attributeDamage.size()) {
        return 0.0F;
    }
    const std::vector<float>& values = m_data.attributeDamage[attribute];
    if (values.empty() || value < m_data.attributeMinimum) {
        return 0.0F;
    }
    const std::size_t index = value - m_data.attributeMinimum;
    return values[index < values.size() ? index : values.size() - 1];
}

} // namespace soulwarden::backend::gamedata
