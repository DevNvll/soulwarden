#include <string>

#include "gamedata/progression_tables.h"
#include "server/http/character_commands.h"

// The views and the fixture of the heals (stage 3.3b-S6, world/health.h), in the common layer of the development routes
// (character_commands.h): the heal base, the outgoing and the received factors of a character, and the friend pair of a live test.

namespace soulwarden::backend::server::http::commands {

using nlohmann::json;

json heals_json(world::World& target, const std::string& character, const std::string& other) {
    json reply{{"inWorld", false}};
    world::Entity* entity = target.find_character(character);
    if (entity == nullptr) {
        return reply;
    }
    reply["inWorld"] = true;
    world::Entity* healed = other.empty() ? entity : target.find_character(other);
    // The heal base with the weapon in the hand: the base damage of the weapon at its gear score times the scaling of Focus (a decision
    // of the user of 2026-10-15).
    json base{{"weapon", ""}};
    const world::ItemInstance* item = world::Combat::weapon_in_hand(*entity);
    const gamedata::CombatTables* tables = target.combat().tables();
    const gamedata::WeaponRow* weapon = item != nullptr && tables != nullptr ? tables->weapon_of(item->itemId) : nullptr;
    world::HealSource source;
    source.healer = entity->id;
    if (weapon != nullptr) {
        base["weapon"] = item->itemId;
        base["gearScore"] = item->gearScore;
        base["weaponBase"] = target.combat().weapon_base(*weapon, item->gearScore);
        base["scalingFocus"] = weapon->scaling[world::attribute::kFocus];
        base["focus"] = entity->attributes.value(world::attribute::kFocus);
        base["focusDamage"] = tables->attribute_damage(world::attribute::kFocus, entity->attributes.value(world::attribute::kFocus));
        base["healBase"] = target.health().heal_base(*entity, item->itemId, item->gearScore);
        source.table = weapon->mastery.empty() ? 0 : gamedata::progression_id(weapon->mastery);
    }
    reply["base"] = base;
    if (healed != nullptr) {
        reply["outgoing"] = {{"target", healed->name},
                             {"factor", target.health().outgoing(*entity, *healed, source)},
                             {"statuses", entity->gear.healOutgoing},
                             {"rules", target.passives().heal_bonus(*entity, *healed, source)},
                             {"equipLoad", target.gear().heal_factor(*entity)}};
    }
    reply["received"] = {{"ability", target.health().received(*entity, world::HealKind::ability)},
                         {"consumable", target.health().received(*entity, world::HealKind::consumable)},
                         {"lifeSteal", target.health().received(*entity, world::HealKind::lifeSteal)},
                         {"healMod", entity->gear.healTaken},
                         {"consumableHealMod", entity->gear.healConsumable},
                         {"nonConsumableHealMod", entity->gear.healNonConsumable},
                         {"dmgPctToHealthHealMod", entity->gear.healLifeSteal}};
    json friends = json::array();
    target.for_each_character([&](const world::Entity& candidate) {
        if (candidate.id != entity->id && target.friendly(*entity, candidate)) {
            friends.push_back(candidate.name);
        }
    });
    reply["friends"] = friends;
    const world::HealCounts& counts = target.health().heal_counts();
    const world::AbilityCounts& abilities = target.abilities().counts();
    reply["counts"] = {{"heals", counts.heals},
                       {"numbers", counts.numbers},
                       {"notices", counts.notices},
                       {"nothing", counts.nothing},
                       {"abilityHeals", target.statuses().counts().abilityHeals},
                       {"pulsesWithNoHeal", target.statuses().counts().pulsesWithNoHeal},
                       {"directHealsOfAFriend", abilities.healsFriend},
                       {"directHealsOfItself", abilities.healsSelf},
                       {"directHealsRefused", abilities.healsRefused},
                       {"aims", abilities.aims},
                       {"confirms", abilities.confirms}};
    return reply;
}

json make_friends(world::World& target, const std::string& character, const std::string& other) {
    const world::Entity* a = target.find_character(character);
    const world::Entity* b = target.find_character(other);
    if (a == nullptr || b == nullptr || a->id == b->id) {
        return {{"message", "two different characters in the world are necessary"}, {"friends", false}};
    }
    target.set_test_friends(a->characterId, b->characterId);
    return {{"message", a->name + " and " + b->name + " are friends (a test fixture: a heal of a friend)"}, {"friends", true}};
}

} // namespace soulwarden::backend::server::http::commands
