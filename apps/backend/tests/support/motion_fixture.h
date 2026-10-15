#pragma once

// The world of the cases of stage 3.3b-S4 (forced movement, the dash of a caster, the counter of Riposte) and of the cases
// that use the same data (the corrections of the review of stage 3.3b-S4): the shop of the itemization cases with made-up
// impacts, the rapier, Riposte, and the nodes and bars of the great axe, the war hammer, and the rapier. All ids, names, and
// values are made up.

#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/shop_fixture.h"

namespace soulwarden::backend::tests::motion {

using namespace shop;

inline const std::uint32_t kRapier = gamedata::progression_id("RapierAbilityTable");
inline const std::uint32_t kRiposte = gamedata::progression_id("Ability_Rapier_Riposte");
inline const std::uint32_t kCharge = gamedata::progression_id("Ability_Greataxe_Charge");
inline const std::uint32_t kMaelstrom = gamedata::progression_id("Ability_Greataxe_Maelstorm");
inline const std::uint32_t kGravityWell = gamedata::progression_id("Ability_Greataxe_GravityWell");

/** An impact of the form of the tool: the normal values, and the values for a creature (0: none). */
inline gamedata::DamageImpact impact_of(float y, float decay, float creatureY = 0.0F, bool fromAttacker = false) {
    gamedata::DamageImpact impact;
    impact.distance = {0.0F, y, 0.0F};
    impact.decay = decay;
    impact.creatureDistance = {0.0F, creatureY, 0.0F};
    impact.fromAttacker = fromAttacker;
    return impact;
}

/** The combat tables of the cases: the tables of the shop with impacts, the rapier and its counter row. */
inline gamedata::CombatData motion_combat() {
    gamedata::CombatData data = shop_combat();
    const auto add = [&](const char* id, std::uint32_t type, float factor, std::optional<gamedata::DamageImpact> impact) {
        gamedata::DamageRow row{id, "Ability", type, factor, 0.0F};
        row.impact = impact;
        data.damageRows.push_back(row);
    };
    add("Test_Pull", 1, 0.1F, impact_of(-1.5F, 0.8F));
    add("Test_PullAI", 1, 0.1F, impact_of(0.5F, 0.85F, 1.0F));
    add("Test_Vortex", 3, 0.0F, impact_of(-7.0F, 0.0F));
    add("Test_Out", 3, 0.0F, impact_of(7.0F, 0.0F));
    add("Test_Push", 1, 0.1F, impact_of(3.0F, 0.9F));
    add("1H_Rapier_Riposte_Stun", 2, 0.5F, impact_of(1.5F, 0.85F));
    add("GreatAxe_Engage1", 1, 1.2F, impact_of(0.25F, 0.85F));
    for (gamedata::DamageRow& row : data.damageRows) {
        if (row.id == "GreatAxe_GravityWell_Vortex") {
            row.impact = impact_of(-7.0F, 0.0F);
        } else if (row.id == "GreatAxe_GravityWell_Burst") {
            row.impact = impact_of(0.5F, 0.3F);
        } else if (row.id == "Warhammer_Damage_PathOfDestiny") {
            row.impact = impact_of(2.0F, 0.9F, 0.0F, true);
        }
    }
    gamedata::WeaponRow rapier = data.weapons.front();
    rapier.id = "TestRapierRow";
    rapier.mastery = "RapierAbilityTable";
    data.weapons.push_back(rapier);
    data.items["testrapier"] = static_cast<std::uint32_t>(data.weapons.size() - 1);
    return data;
}

/** The ability data of the shop with the rows of Gravity Well and of Riposte (the values of the game data). */
inline gamedata::AbilityTables motion_abilities() {
    gamedata::AbilityTables tables = shop_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    gamedata::StatusRow root = status("GreatAxe_Vortex_Root", 1.0F, {}, true);
    root.root = true;
    rows.push_back(root);
    gamedata::StatusRow slow = status("GreatAxe_Vortex", 3.0F, {});
    slow.moveSpeed = -0.6F;
    rows.push_back(slow);
    // Rapier_Riposte_Timer: 1 s, the absorption 2 of each damage type (game data); Rapier_Riposte_Stun: 2 s, a stun that the
    // next hit ends.
    rows.push_back(status("Rapier_Riposte_Timer", 1.0F, cells({{"ABSSlash", 2.0F}, {"ABSStrike", 2.0F}, {"ABSStandard", 2.0F}})));
    gamedata::StatusRow stun = status("Rapier_Riposte_Stun", 2.0F, {}, true, 1);
    stun.stunned = true;
    rows.push_back(stun);
    tables.statusEffects = numbered(std::move(rows));
    tables.cooldowns.push_back({"Ability_Rapier_Riposte", 18.0F, 0.0F});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Projectile", 0.0F, 0.0F, 10.0F, 0, 0.0F, 2000.0F, {}});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    return tables;
}

/** The world of a case: the shop with the impacts, the rapier, Riposte, and the nodes and bars of the three weapons. */
struct MotionShop : Shop {
    explicit MotionShop(bool observe = false) : Shop(false, false, observe) {
        if (!host.combat.set(motion_combat())) {
            throw std::runtime_error("motion test: the combat tables are not valid: " + host.combat.error());
        }
        std::vector<gamedata::ItemDefinition> items = shop_catalog();
        items.push_back(definition("TestRapier", "Weapon", "Weapon+EquippableMainHand+Rapier+Melee", 5, 100, 600));
        host.catalog.set(items);
        if (!host.abilities.set(motion_abilities())) {
            throw std::runtime_error("motion test: the ability data is not valid: " + host.abilities.error());
        }
        give_skills(*character());
    }
    /** The nodes and the bars of the axe, the hammer, and the rapier. */
    static void give_skills(world::Entity& character) {
        for (const std::uint32_t node : {kCharge, kMaelstrom, kGravityWell}) {
            character.skills.pools[kGreatAxe][node].level = 1;
        }
        character.skills.bars[kGreatAxe] = {kCharge, kMaelstrom, kGravityWell};
        std::array<std::uint32_t, 3> hammer{};
        std::size_t place = 0;
        for (const char* node : {"Ability_WarHammer_WreckingBall", "Ability_WarHammer_Shockwave", "Ability_WarHammer_PathOfDestiny"}) {
            character.skills.pools[kWarHammer][gamedata::progression_id(node)].level = 1;
            hammer[place++] = gamedata::progression_id(node);
        }
        character.skills.bars[kWarHammer] = hammer;
        character.skills.pools[kRapier][kRiposte].level = 1;
        character.skills.bars[kRapier] = {kRiposte, 0, 0};
    }
    /** Puts an item of the shop into the main hand of A. */
    void hold(const char* item) {
        world::ItemResult worn = wear(item, world::paperdoll::kMainHand1);
        if (!worn.ok()) {
            throw std::runtime_error(std::string("motion test: the item ") + item);
        }
        character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    }
    world::Entity* wolf_at(float east, float north) {
        return host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
    }
    world::Entity* creature(world::EntityId id) {
        return host.world.creatures().find(id);
    }
    const world::ForcedCounts& forced() {
        return host.world.creatures().forced().counts();
    }
    /** Ticks of the world of `seconds` with idle reports of A. */
    void run(float seconds) {
        walker.idle(static_cast<int>(std::lround(seconds * 30.0F)));
    }
};

} // namespace soulwarden::backend::tests::motion
