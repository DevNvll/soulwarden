#include "gamedata/combat_tables.h"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <unordered_map>

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
/** The three damage categories of the damage type table of the game. "True" is damage with no mitigation. */
constexpr const char* kCategoryPhysical = "Physical";
constexpr const char* kCategoryElemental = "Elemental";
constexpr const char* kCategoryDirect = "True";

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
    // A list with no value gives the damage 0 for each value of the attribute: the tables of
    // the game have a row for each value of each attribute that a weapon can have a factor for.
    for (const auto& [member, attribute] : kAttributeMembers) {
        if (data.attributeDamage[attribute].empty()) {
            return fail("the damage list of an attribute is empty");
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
            || !std::isfinite(row.added) || row.added < 0.0F || !std::isfinite(row.attackCharge) || row.attackCharge < 0.0F
            || !std::isfinite(row.hitCharge) || row.hitCharge < 0.0F) {
            return fail("a damage row is not valid");
        }
        if (row.impact) {
            // An impact distance of more than 100 m, or a decay rate above 1 (a travel that grows), is not a value of the game.
            const DamageImpact& impact = *row.impact;
            for (const float value : {impact.distance[0], impact.distance[1], impact.distance[2], impact.creatureDistance[0],
                                      impact.creatureDistance[1], impact.creatureDistance[2]}) {
                if (!std::isfinite(value) || std::fabs(value) > 100.0F) {
                    return fail(("the impact of the damage row " + row.id + " is not valid").c_str());
                }
            }
            if (!std::isfinite(impact.decay) || impact.decay > 1.0F || !std::isfinite(impact.creatureDecay) || impact.creatureDecay > 1.0F) {
                return fail(("the decay rate of the impact of the damage row " + row.id + " is not valid").c_str());
            }
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
    // The armor rule: a power of a gear score. An exponent outside a small range, or a factor
    // that is not a number, would give an armor that is not a number.
    const ArmorConstants& armor = data.armorConstants;
    if (!std::isfinite(armor.ratingExponent) || armor.ratingExponent < 0.0F || armor.ratingExponent > 4.0F
        || !std::isfinite(armor.mitigationExponent) || armor.mitigationExponent < 0.0F || armor.mitigationExponent > 4.0F) {
        return fail("an exponent of the armor rule is not valid");
    }
    for (const auto& [item, row] : data.armor) {
        if (!std::isfinite(row.physical) || row.physical < 0.0F || !std::isfinite(row.elemental) || row.elemental < 0.0F
            || !std::isfinite(row.rating) || row.rating < 0.0F) {
            return fail("the armor values of an item are not valid");
        }
    }
    for (const auto& [difference, value] : data.levelDamage) {
        if (!std::isfinite(value) || value < -1.0F || value > 1000.0F) {
            return fail("the damage of a level difference is not valid");
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
    m_rows.clear();
    for (std::uint32_t index = 0; index < m_data.damageRows.size(); ++index) {
        m_rows.emplace(lower(m_data.damageRows[index].id), index);
    }
    m_error.clear();
    return true;
}

std::optional<std::uint32_t> CombatTables::row_index(std::string_view id) const {
    const auto found = m_rows.find(lower(id));
    return found == m_rows.end() ? std::nullopt : std::optional<std::uint32_t>(found->second);
}

bool CombatTables::load(const std::string& path) {
    const auto fail = [this](const std::string& text) {
        m_data = CombatData{};
        m_rows.clear();
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
        // The three categories of the game table. A different text is a fault of the file: with
        // the direct category as its default, a wrong letter would take the mitigation of each
        // creature away for that damage type, with no sign of the fault.
        const std::string category = row[2].get<std::string>();
        if (category == kCategoryPhysical) {
            type.category = DamageCategory::physical;
        } else if (category == kCategoryElemental) {
            type.category = DamageCategory::elemental;
        } else if (category == kCategoryDirect) {
            type.category = DamageCategory::direct;
        } else {
            return fail("the damage type " + type.name + " has the category \"" + category
                        + "\", which is not Physical, Elemental, or True");
        }
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
    // The damage rows: [id, weapon category, attack type, damage type, factor, added, attack rune charge, hit rune
    // charge]. A file of a tool from before stage 1.3b has no rune charge (6 values): a hit then gives no charge.
    std::unordered_map<std::string, std::uint32_t> rowIndex;
    if (!document["damageRows"].is_array()) {
        return fail("the damage rows are not a list");
    }
    for (const json& entry : document["damageRows"]) {
        DamageRow row;
        if (!entry.is_array() || (entry.size() != 6 && entry.size() != 8) || !entry[0].is_string() || !entry[2].is_string()
            || !entry[3].is_string() || !read_float(entry[4], row.factor) || !read_float(entry[5], row.added)
            || (entry.size() == 8 && (!read_float(entry[6], row.attackCharge) || !read_float(entry[7], row.hitCharge)))) {
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
    // row, heavy row, gear score rate, mastery]. A file of a tool from before stage 3.5a has no
    // mastery (11 values): the weapons then give no mastery experience for a kill.
    if (!document["weapons"].is_array()) {
        return fail("the weapons are not a list");
    }
    for (const json& entry : document["weapons"]) {
        WeaponRow weapon;
        if (!entry.is_array() || (entry.size() != 11 && entry.size() != 12) || !entry[0].is_string() || !entry[1].is_string()
            || !entry[2].is_string() || (entry.size() == 12 && !entry[11].is_string())
            || !read_float(entry[3], weapon.baseDamage) || !read_float(entry[4], weapon.scaling[kStrength])
            || !read_float(entry[5], weapon.scaling[kDexterity]) || !read_float(entry[6], weapon.scaling[kIntelligence])
            || !read_float(entry[7], weapon.scaling[kFocus]) || !entry[8].is_string() || !entry[9].is_string()
            || !read_float(entry[10], weapon.gearScoreRate)) {
            return fail("a weapon does not have the form of the tool");
        }
        weapon.id = entry[0].get<std::string>();
        weapon.melee = entry[1].get<std::string>() == kMeleeUse;
        if (entry.size() == 12) {
            weapon.mastery = entry[11].get<std::string>();
        }
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
    // The armor of the items (a file of stage 3.4b):
    // {"ratingExponent": N, "mitigationExponent": N, "items": {"<item id>": [physical, elemental, rating], ...}}.
    if (document.contains("armor")) {
        const json& armor = document["armor"];
        if (!armor.is_object() || !armor.contains("ratingExponent") || !armor.contains("mitigationExponent")
            || !armor.contains("items") || !armor["items"].is_object()
            || !read_float(armor["ratingExponent"], data.armorConstants.ratingExponent)
            || !read_float(armor["mitigationExponent"], data.armorConstants.mitigationExponent)) {
            return fail("the armor values do not have the form of the tool");
        }
        for (const auto& [item, values] : armor["items"].items()) {
            ArmorRow row;
            if (!values.is_array() || values.size() != 3 || !read_float(values[0], row.physical)
                || !read_float(values[1], row.elemental) || !read_float(values[2], row.rating)) {
                return fail("the armor values of the item " + item + " do not have the form of the tool");
            }
            data.armor.emplace(lower(item), row);
        }
    }
    // The impacts of the damage rows (stage 3.3b-S4): [[id, x, y, z, decay, aiX, aiY, aiZ, aiDecay, fromAttacker], ...]. A
    // file of a tool from before the stage has none: a hit then moves no target.
    if (document.contains("impacts")) {
        if (!document["impacts"].is_array()) {
            return fail("the impacts are not a list");
        }
        for (const json& entry : document["impacts"]) {
            DamageImpact impact;
            if (!entry.is_array() || entry.size() != 10 || !entry[0].is_string() || !entry[9].is_boolean()
                || !read_float(entry[1], impact.distance[0]) || !read_float(entry[2], impact.distance[1])
                || !read_float(entry[3], impact.distance[2]) || !read_float(entry[4], impact.decay)
                || !read_float(entry[5], impact.creatureDistance[0]) || !read_float(entry[6], impact.creatureDistance[1])
                || !read_float(entry[7], impact.creatureDistance[2]) || !read_float(entry[8], impact.creatureDecay)) {
                return fail("an impact does not have the form of the tool");
            }
            impact.fromAttacker = entry[9].get<bool>();
            const auto row = rowIndex.find(lower(entry[0].get<std::string>()));
            if (row == rowIndex.end()) {
                return fail("the impact of " + entry[0].get<std::string>() + " names a damage row that the file does not have");
            }
            if (data.damageRows[row->second].impact) {
                return fail("the damage row " + entry[0].get<std::string>() + " has two impacts");
            }
            data.damageRows[row->second].impact = impact;
        }
    }
    // The critical values (stage 3.3b-S5): [[weapon id, critChance, critDamageMultiplier], ...] and [[row id, canCrit,
    // noHeadshot, noBackstab, dmgCoefHead, dmgCoefCrit], ...]. A file of a tool from before the stage has none: no critical hit.
    if (document.contains("criticals") || document.contains("hitRules")) {
        if (!document.contains("criticals") || !document.contains("hitRules") || !document["criticals"].is_array()
            || !document["hitRules"].is_array()) {
            return fail("the criticals and the hit rules are not two lists");
        }
        std::unordered_map<std::string, std::uint32_t> weaponIndex;
        for (std::uint32_t index = 0; index < data.weapons.size(); ++index) {
            weaponIndex.emplace(lower(data.weapons[index].id), index);
        }
        for (const json& entry : document["criticals"]) {
            float chance = 0.0F;
            float multiplier = 0.0F;
            if (!entry.is_array() || entry.size() != 3 || !entry[0].is_string() || !read_float(entry[1], chance)
                || !read_float(entry[2], multiplier) || chance < 0.0F || chance > 1.0F || multiplier < 0.0F) {
                return fail("a critical entry does not have the form of the tool");
            }
            const auto weapon = weaponIndex.find(lower(entry[0].get<std::string>()));
            if (weapon == weaponIndex.end() || data.weapons[weapon->second].critical) {
                return fail("the critical entry of " + entry[0].get<std::string>() + " names no weapon of the file, or a weapon two times");
            }
            WeaponRow& row = data.weapons[weapon->second];
            row.critical = true;
            row.critChance = chance;
            row.critMultiplier = multiplier;
        }
        std::vector<bool> seen(data.damageRows.size(), false);
        for (const json& entry : document["hitRules"]) {
            float head = 0.0F;
            float crit = 0.0F;
            if (!entry.is_array() || entry.size() != 6 || !entry[0].is_string() || !entry[1].is_boolean() || !entry[2].is_boolean()
                || !entry[3].is_boolean() || !read_float(entry[4], head) || !read_float(entry[5], crit) || head < 0.0F || crit < 0.0F) {
                return fail("a hit rule does not have the form of the tool");
            }
            const auto row = rowIndex.find(lower(entry[0].get<std::string>()));
            if (row == rowIndex.end() || seen[row->second]) {
                return fail("the hit rule of " + entry[0].get<std::string>() + " names no damage row of the file, or a row two times");
            }
            seen[row->second] = true;
            DamageRow& damage = data.damageRows[row->second];
            damage.canCrit = entry[1].get<bool>();
            damage.noHeadshot = entry[2].get<bool>();
            damage.noBackstab = entry[3].get<bool>();
            damage.headFactor = head;
            damage.critFactor = crit;
        }
        if (std::find(seen.begin(), seen.end(), false) != seen.end()) {
            return fail("a damage row has no hit rule");
        }
    }
    // The damage of a level difference: [[difference, part of the damage], ...].
    if (document.contains("levelDamage")) {
        if (!document["levelDamage"].is_array()) {
            return fail("the level differences are not a list");
        }
        for (const json& row : document["levelDamage"]) {
            float value = 0.0F;
            if (!row.is_array() || row.size() != 2 || !row[0].is_number_integer() || !read_float(row[1], value)
                || row[0].get<std::int64_t>() < -1000 || row[0].get<std::int64_t>() > 1000) {
                return fail("a level difference does not have the form of the tool");
            }
            data.levelDamage[static_cast<std::int32_t>(row[0].get<std::int64_t>())] = value;
        }
    }
    std::string error;
    if (!check(data, error)) {
        return fail(error);
    }
    return set(std::move(data));
}

const WeaponRow* CombatTables::weapon_of(std::string_view itemId) const {
    const auto found = m_data.items.find(lower(itemId));
    return found == m_data.items.end() ? nullptr : &m_data.weapons[found->second];
}

const DefenseRow* CombatTables::defense_of(std::string_view vitalsId) const {
    const auto found = m_data.vitals.find(lower(vitalsId));
    return found == m_data.vitals.end() ? nullptr : &m_data.defenses[found->second];
}

const ArmorRow* CombatTables::armor_of(std::string_view itemId) const {
    const auto found = m_data.armor.find(lower(itemId));
    return found == m_data.armor.end() ? nullptr : &found->second;
}

float CombatTables::level_damage(std::int64_t difference) const {
    if (difference < -1000 || difference > 1000) {
        return 0.0F;
    }
    const auto found = m_data.levelDamage.find(static_cast<std::int32_t>(difference));
    return found == m_data.levelDamage.end() ? 0.0F : found->second;
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
