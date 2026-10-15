#include "world/runes.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "core/logging/log.h"
#include "gamedata/perk_data.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

bool same_text(std::string_view left, std::string_view right) {
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        const auto lower = [](char letter) { return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter + 32) : letter; };
        if (lower(left[index]) != lower(right[index])) {
            return false;
        }
    }
    return true;
}

} // namespace

const char* rune_verdict_text(RuneVerdict verdict) {
    switch (verdict) {
    case RuneVerdict::activated:
        return "activated";
    case RuneVerdict::noEntity:
        return "no character in the world";
    case RuneVerdict::noData:
        return "no perk data, no ability data, or no combat tables";
    case RuneVerdict::dead:
        return "the character is dead";
    case RuneVerdict::noRune:
        return "no heart rune with a rule of this backend";
    case RuneVerdict::notFull:
        return "the charge is not full";
    case RuneVerdict::active:
        return "an activation runs";
    }
    return "unknown";
}

const ItemInstance* Runes::detonate_rune(const Entity& character) const {
    const ItemInstance* rune = character.equipment.item(paperdoll::kHeartRune);
    const gamedata::PerkData* perks = m_world.perk_data();
    const gamedata::CombatTables* tables = m_world.combat().tables();
    if (rune == nullptr || perks == nullptr || tables == nullptr) {
        return nullptr;
    }
    const gamedata::WeaponRow* weapon = tables->weapon_of(rune->itemId);
    if (weapon == nullptr) {
        return nullptr;
    }
    const std::vector<std::string>& abilities = perks->rune_abilities(weapon->id);
    const bool detonate = std::any_of(abilities.begin(), abilities.end(),
                                      [](const std::string& ability) { return same_text(ability, rune_rules::kDetonateAbility); });
    return detonate ? rune : nullptr;
}

void Runes::gain(Entity& character, float amount) {
    if (character.kind != EntityKind::player || character.life.dead || !(amount > 0.0F) || detonate_rune(character) == nullptr) {
        return;
    }
    CharacterState& state = m_characters[character.characterId];
    const float before = state.charge;
    state.charge = std::min(state.charge + std::min(amount, rune_rules::kGainMost), rune_rules::kChargeMax);
    if (state.charge != before) {
        ++m_counts.gains;
        publish(character, state);
    }
}

void Runes::publish(Entity& character, const CharacterState& state) {
    const float percent = std::clamp(state.charge / rune_rules::kChargeMax * 100.0F, 0.0F, 100.0F);
    // The state has a whole percent: a change of a part of a percent does not go to the client.
    if (std::floor(percent) != std::floor(character.runeCharge) || (percent == 0.0F) != (character.runeCharge == 0.0F)) {
        character.runeCharge = percent;
        m_world.touch(character.id, ComponentKey::charge);
    } else {
        character.runeCharge = percent;
    }
}

RuneVerdict Runes::activate(SessionId session) {
    const Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return counted(RuneVerdict::noEntity);
    }
    return activate_character(character->characterId);
}

RuneVerdict Runes::activate_character(const std::string& characterId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return counted(RuneVerdict::noEntity);
    }
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (m_world.perk_data() == nullptr || abilities == nullptr || !abilities->loaded() || m_world.combat().tables() == nullptr) {
        return counted(RuneVerdict::noData);
    }
    if (character->life.dead) {
        return counted(RuneVerdict::dead);
    }
    if (detonate_rune(*character) == nullptr) {
        return counted(RuneVerdict::noRune);
    }
    CharacterState& state = m_characters[characterId];
    const WorldClock::time_point now = m_world.clock().now();
    if (state.activation && now < state.ends) {
        return counted(RuneVerdict::active);
    }
    // The action needs a full charge (a part of a point below the maximum counts as full: the state has whole percents).
    if (state.charge < rune_rules::kChargeMax - 0.01F) {
        return counted(RuneVerdict::notFull);
    }
    // The status of the action: the first variant whose ability row a perk of the character has.
    std::string statusId = rune_rules::kDetonateStatus;
    const std::vector<GearPerk> perks = m_world.gear().perks_of(*character);
    for (const auto& [ability, status] : rune_rules::kDetonateVariants) {
        const bool has = std::any_of(perks.begin(), perks.end(), [&](const GearPerk& perk) {
            return std::any_of(perk.perk->abilities.begin(), perk.perk->abilities.end(),
                               [&](const std::string& name) { return same_text(name, ability); });
        });
        if (has && abilities->status(status) != nullptr) {
            statusId = status;
            break;
        }
    }
    const gamedata::StatusRow* status = abilities->status(statusId);
    if (status == nullptr) {
        return counted(RuneVerdict::noData);
    }
    state.charge = 0.0F;
    const std::uint64_t activation = m_nextActivation++;
    state.activation = activation;
    const float seconds = std::max(status->duration, 0.0F);
    const auto time = std::chrono::milliseconds(static_cast<long long>(std::lround(seconds * 1000.0F)));
    state.ends = now + time;
    publish(*character, state);
    m_world.consumables().add_effect(*character, *status, seconds);
    {
        const std::string mark = log::session_mark(character->owner, characterId);
        const log::Context context(mark);
        log::write("world: heart rune: %s activates Detonate with the status %s: the explosion comes in %.1f s",
                   character->name.c_str(),
                   status->id.c_str(),
                   static_cast<double>(seconds));
    }
    m_world.after(time, [this, characterId, activation](World&) { explode(characterId, activation); });
    return counted(RuneVerdict::activated);
}

void Runes::explode(const std::string& characterId, std::uint64_t activation) {
    const auto found = m_characters.find(characterId);
    if (found == m_characters.end() || found->second.activation != activation) {
        return;
    }
    found->second.activation.reset();
    Entity* character = m_world.find_character(characterId);
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const gamedata::CombatTables* tables = m_world.combat().tables();
    const ItemInstance* rune = character != nullptr ? detonate_rune(*character) : nullptr;
    // RemoveOnDeath and DontApplyOnEndEffectOnRemove: a character that died or left the world makes no explosion.
    if (character == nullptr || character->life.dead || rune == nullptr || abilities == nullptr || tables == nullptr) {
        ++m_counts.cancelled;
        return;
    }
    const gamedata::SpellRow* spell = abilities->spell(rune_rules::kDetonateSpell);
    const std::optional<std::uint32_t> row = tables->row_index(rune_rules::kDetonateRow);
    const gamedata::WeaponRow* weapon = tables->weapon_of(rune->itemId);
    if (spell == nullptr || !row || weapon == nullptr) {
        ++m_counts.cancelled;
        return;
    }
    ++m_counts.explosions;
    const state::WorldPosition center = character->position;
    const float height = std::max(spell->height, 0.5F);
    std::vector<std::pair<float, EntityId>> found_;
    m_world.for_each_near(center, spell->radius, [&](Entity& entity) {
        if (entity.kind == EntityKind::creature && !entity.creature.dead && std::fabs(entity.position.z - center.z) <= height) {
            found_.emplace_back(horizontal_distance(entity.position, center), entity.id);
        }
    });
    std::sort(found_.begin(), found_.end());
    const std::string itemId = rune->itemId;
    const std::uint32_t gearScore = rune->gearScore;
    const EntityId casterId = character->id;
    unsigned hits = 0;
    for (const auto& [away, id] : found_) {
        if (hits >= rune_rules::kExplosionMost) {
            break;
        }
        Entity* caster = m_world.find(casterId);
        Entity* target = m_world.creatures().find(id);
        if (caster == nullptr || target == nullptr || target->creature.dead || target->creature.immune()) {
            continue;
        }
        // CheckObstructions: a wall between the character and the creature stops the explosion.
        if (!m_world.navigation().sees(center, target->position)) {
            continue;
        }
        const HitResult result = m_world.combat().strike(*caster, *target, itemId, *weapon, gearScore, *row, 1.0F, "Detonate");
        if (result.verdict == HitVerdict::accepted) {
            ++hits;
            ++m_counts.explosionHits;
        }
    }
    const std::string mark = log::session_mark(character->owner, characterId);
    const log::Context context(mark);
    log::write("world: heart rune: the explosion of Detonate at %.2f %.2f %.2f (radius %.1f m): %zu creature(s) near, %u hit(s)",
               static_cast<double>(center.x),
               static_cast<double>(center.y),
               static_cast<double>(center.z),
               static_cast<double>(spell->radius),
               found_.size(),
               hits);
}

bool Runes::set_charge(const std::string& characterId, float percent) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || !std::isfinite(percent)) {
        return false;
    }
    CharacterState& state = m_characters[characterId];
    state.charge = std::clamp(percent, 0.0F, 100.0F) / 100.0F * rune_rules::kChargeMax;
    publish(*character, state);
    return true;
}

void Runes::entered(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found != m_characters.end()) {
        character.runeCharge = std::clamp(found->second.charge / rune_rules::kChargeMax * 100.0F, 0.0F, 100.0F);
    }
}

float Runes::percent_of(const std::string& characterId) const {
    const auto found = m_characters.find(characterId);
    return found == m_characters.end() ? 0.0F : std::clamp(found->second.charge / rune_rules::kChargeMax * 100.0F, 0.0F, 100.0F);
}

} // namespace soulwarden::backend::world
