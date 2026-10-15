#include "gamedata/projectile_data.h"

#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

#include "gamedata/progression_tables.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

constexpr std::uint64_t kFormat = 1;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

std::string attack_key(std::string_view profile, std::uint32_t attackId) {
    return lower_ascii(profile) + '/' + std::to_string(attackId);
}

bool text_of(const json& value, std::string& out) {
    if (!value.is_string()) {
        return false;
    }
    out = value.get<std::string>();
    return true;
}

bool number_of(const json& value, float& out) {
    if (!value.is_number() || !std::isfinite(value.get<double>())) {
        return false;
    }
    out = value.get<float>();
    return true;
}

bool whole_of(const json& value, std::uint32_t& out) {
    if (!value.is_number_unsigned() || value.get<std::uint64_t>() > 0xFFFFFFFFULL) {
        return false;
    }
    out = static_cast<std::uint32_t>(value.get<std::uint64_t>());
    return true;
}

bool index_of(const json& value, std::int32_t& out) {
    if (!value.is_number_integer() || value.get<std::int64_t>() < -1 || value.get<std::int64_t>() > 0x7FFFFFFF) {
        return false;
    }
    out = static_cast<std::int32_t>(value.get<std::int64_t>());
    return true;
}

bool flag_of(const json& value, bool& out) {
    if (!value.is_boolean()) {
        return false;
    }
    out = value.get<bool>();
    return true;
}

bool vector_of(const json& value, std::array<float, 3>& out) {
    return value.is_array() && value.size() == 3 && number_of(value[0], out[0]) && number_of(value[1], out[1])
           && number_of(value[2], out[2]);
}

bool read_attack(const json& row, RangedAttack& attack) {
    return row.is_array() && row.size() == 16 && text_of(row[0], attack.profile) && text_of(row[1], attack.name)
           && flag_of(row[2], attack.hitScan) && number_of(row[3], attack.maxRange) && number_of(row[4], attack.radius)
           && number_of(row[5], attack.nearRange) && number_of(row[6], attack.penetration) && number_of(row[7], attack.accelDelay)
           && number_of(row[8], attack.falloffStart) && number_of(row[9], attack.falloffEnd) && number_of(row[10], attack.falloffMost)
           && whole_of(row[11], attack.bounces) && flag_of(row[12], attack.destroyOnBlock) && flag_of(row[13], attack.destroyOnMiss)
           && vector_of(row[14], attack.predictedAccel) && whole_of(row[15], attack.spreadDirections);
}

bool read_slice(const json& row, ProjectileSlice& slice) {
    return row.is_array() && row.size() == 7 && text_of(row[0], slice.path) && number_of(row[1], slice.speed)
           && number_of(row[2], slice.extraSpeed) && number_of(row[3], slice.timeForMaxSpeed) && vector_of(row[4], slice.accel)
           && number_of(row[5], slice.flyingLifetime) && number_of(row[6], slice.stuckLifetime);
}

bool read_ammo(const json& row, AmmoRow& ammo) {
    return row.is_array() && row.size() == 4 && text_of(row[0], ammo.id) && text_of(row[1], ammo.type)
           && number_of(row[2], ammo.damageModifier) && index_of(row[3], ammo.slice);
}

bool read_weapon(const json& row, RangedWeapon& weapon) {
    return row.is_array() && row.size() == 3 && text_of(row[0], weapon.weapon) && text_of(row[1], weapon.profile)
           && text_of(row[2], weapon.ammoType);
}

bool read_shot(const json& row, ShotRule& shot) {
    return row.is_array() && row.size() == 10 && text_of(row[0], shot.profile) && text_of(row[1], shot.attack)
           && text_of(row[2], shot.row) && number_of(row[3], shot.charge) && index_of(row[4], shot.slice)
           && text_of(row[5], shot.spell) && text_of(row[6], shot.manaCost) && flag_of(row[7], shot.heavy)
           && whole_of(row[8], shot.ammoSlot) && flag_of(row[9], shot.consume);
}

template <typename Row, typename Read>
bool read_list(const json& document, const char* name, std::vector<Row>& out, Read read, std::string& error) {
    if (!document.contains(name) || !document[name].is_array()) {
        error = std::string("the file has no list ") + name;
        return false;
    }
    for (const json& row : document[name]) {
        if (!read(row, out.emplace_back())) {
            error = std::string("entry ") + std::to_string(out.size() - 1) + " of the list " + name + " does not have the form of the tool";
            return false;
        }
    }
    return true;
}

} // namespace

bool ProjectileData::load(const std::string& path) {
    m_tables = {};
    m_weapons.clear();
    m_attacks.clear();
    m_ammo.clear();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object() || !document.contains("format") || !document["format"].is_number_unsigned()
        || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file is not a JSON object of the format 1";
        return false;
    }
    ProjectileTables tables;
    if (!read_list(document, "attacks", tables.attacks, read_attack, m_error) || !read_list(document, "slices", tables.slices, read_slice, m_error)
        || !read_list(document, "ammo", tables.ammo, read_ammo, m_error) || !read_list(document, "weapons", tables.weapons, read_weapon, m_error)
        || !read_list(document, "shots", tables.shots, read_shot, m_error)) {
        return false;
    }
    if (document.contains("counts")) {
        const json& counts = document["counts"];
        const auto count = [&](const char* name, std::size_t size) {
            return counts.is_object() && counts.contains(name) && counts[name].is_number_unsigned() && counts[name].get<std::uint64_t>() == size;
        };
        if (!count("attacks", tables.attacks.size()) || !count("slices", tables.slices.size()) || !count("ammo", tables.ammo.size())
            || !count("weapons", tables.weapons.size()) || !count("shots", tables.shots.size())) {
            m_error = "a list does not have the count of the file";
            return false;
        }
    }
    return set(std::move(tables));
}

bool ProjectileData::set(ProjectileTables tables) {
    m_tables = {};
    m_weapons.clear();
    m_attacks.clear();
    m_ammo.clear();
    std::string error;
    const auto size = [](float value) { return std::isfinite(value) && value >= 0.0F; };
    const auto in_slices = [&](std::int32_t index) { return index >= -1 && index < static_cast<std::int32_t>(tables.slices.size()); };
    std::unordered_map<std::string, std::size_t> attacks;
    for (std::size_t index = 0; index < tables.attacks.size() && error.empty(); ++index) {
        const RangedAttack& attack = tables.attacks[index];
        if (attack.profile.empty() || attack.name.empty() || !size(attack.maxRange) || !size(attack.radius) || !size(attack.nearRange)
            || !size(attack.penetration) || !size(attack.accelDelay)) {
            error = "the attack " + attack.profile + "/" + attack.name + " has a value that is not a size";
        } else if (!attacks.emplace(attack_key(attack.profile, progression_id(attack.name)), index).second) {
            error = "two attacks of the profile " + attack.profile + " have the name " + attack.name;
        }
    }
    for (const ProjectileSlice& slice : tables.slices) {
        // A speed of 0 is in the game data (a barrel that a siege dropper drops): a shot with such a slice is refused at its launch.
        if (error.empty() && (!size(slice.speed) || !size(slice.flyingLifetime) || !size(slice.extraSpeed))) {
            error = "the projectile slice " + slice.path + " has a speed or a lifetime that is not a size";
        }
    }
    std::unordered_map<std::uint32_t, std::size_t> ammo;
    for (std::size_t index = 0; index < tables.ammo.size() && error.empty(); ++index) {
        const AmmoRow& row = tables.ammo[index];
        if (row.id.empty() || !size(row.damageModifier) || !in_slices(row.slice)) {
            error = "the ammunition " + row.id + " has a damage modifier or a slice that is not correct";
        } else {
            ammo.emplace(progression_id(row.id), index);
        }
    }
    std::unordered_map<std::string, std::size_t> weapons;
    for (std::size_t index = 0; index < tables.weapons.size() && error.empty(); ++index) {
        const RangedWeapon& weapon = tables.weapons[index];
        if (weapon.weapon.empty() || weapon.profile.empty()) {
            error = "a weapon of the projectile data has no id or no profile";
        } else {
            weapons.emplace(lower_ascii(weapon.weapon), index);
        }
    }
    for (const ShotRule& shot : tables.shots) {
        if (error.empty() && (!attacks.contains(attack_key(shot.profile, progression_id(shot.attack))) || shot.row.empty()
                              || !in_slices(shot.slice) || !std::isfinite(shot.charge) || shot.charge < 0.0F)) {
            error = "the shot " + shot.profile + "/" + shot.attack + " (" + shot.row + ") has an attack, a row, or a slice that is not correct";
        }
    }
    if (!error.empty()) {
        m_error = error;
        return false;
    }
    m_tables = std::move(tables);
    m_weapons = std::move(weapons);
    m_attacks = std::move(attacks);
    m_ammo = std::move(ammo);
    m_error.clear();
    return true;
}

const RangedWeapon* ProjectileData::weapon(std::string_view weaponId) const {
    const auto found = m_weapons.find(lower_ascii(weaponId));
    return found != m_weapons.end() ? &m_tables.weapons[found->second] : nullptr;
}

const RangedAttack* ProjectileData::attack(std::string_view profile, std::uint32_t attackId) const {
    const auto found = m_attacks.find(attack_key(profile, attackId));
    return found != m_attacks.end() ? &m_tables.attacks[found->second] : nullptr;
}

std::vector<const ShotRule*> ProjectileData::shots(std::string_view profile, std::uint32_t attackId) const {
    std::vector<const ShotRule*> found;
    const std::string wanted = lower_ascii(profile);
    for (const ShotRule& shot : m_tables.shots) {
        if (lower_ascii(shot.profile) == wanted && progression_id(shot.attack) == attackId) {
            found.push_back(&shot);
        }
    }
    return found;
}

const AmmoRow* ProjectileData::ammo(std::uint32_t ammoId) const {
    const auto found = m_ammo.find(ammoId);
    return found != m_ammo.end() ? &m_tables.ammo[found->second] : nullptr;
}

const ProjectileSlice* ProjectileData::slice(std::int32_t index) const {
    return index >= 0 && index < static_cast<std::int32_t>(m_tables.slices.size()) ? &m_tables.slices[static_cast<std::size_t>(index)]
                                                                                  : nullptr;
}

} // namespace soulwarden::backend::gamedata
