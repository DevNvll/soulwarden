#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
#include "gamedata/combat_tables.h"
#include "gamedata/creature_data.h"
#include "gamedata/perk_data.h"
#include "world/entity.h"
#include "world/world_clock.h"

// The gear system (stage 1.3b, itemization): what the equipment and the effects of a character give it. World thread only.
//
// The perks of an item. The instance has five perk places (a gem is one of them, ItemInstance::perks), and the definition
// can have fixed perks (Perk1..5 of the item tables): the client shows the fixed perks too (live check I2: a heart rune
// with no instance perk shows its two perks). Each perk goes through the perk swap table of the game (ItemPerkSwapData):
// the client shows and uses the new perk of a row whose key fits the item (live check I2: the Soldier perk of a weapon
// shows as "Acutely Empowered"). The potency of a perk is the potency of its gear score scaling at the gear score of the
// item (gamedata::PerkData::potency; live check I2: the numbers of the tooltip fit it).
//
// The conditions of a perk (ConditionEvent): OnEquip, always; OnActive and OnUnsheathed, for a weapon only while it is in
// the hand of the character (the selected weapon slot, Combat::weapon_in_hand), for each other item always.
//
// The values (rules of this project with the cells of the game data; protocol notes 4.23):
//   - Passive. The affixes of a perk (AffixStatData), the status row that an affix keeps (StatusEffect), and an ability
//     row of a perk with no event and no condition: MOD<attribute> (the attribute bonus), MaxHealthMod and MaxHealth (a
//     part of the maximum health), BaseDamage (a part of the damage of each hit), DMG<type> (a part of the damage of that
//     type), ABS<type> (a part of the damage of a creature of that type that the character does not take),
//     PhysicalArmor and ElementalArmor (a part of the armor), each one times the potency. An ability row counts
//     IsStackableMax times at most for one character (the largest potency first).
//   - Conversion. An affix with DamagePercentage and DamageType on the weapon of a hit (a gem: Abyssal IV converts 50
//     percent): that part of the damage of the hit has the type of the affix.
//   - The equip load: the sum of the loads of the armor items (slots 0 to 4; WeightOverride of their armor rows), each
//     load in whole tenths (live checks I4 and I12: 23.25 + 15.5 shows as "Light 3.8", the build 231.57 as "Medium
//     22.9"), its class by the percents of the row Player of the encumbrance
//     table (light below 26, medium below 46, heavy to 100, overburdened above), and the part of the damage of each class
//     (EquipLoadDamageMult*: light +0.15, medium 0, heavy -0.15) and the factor of a heal (EquipLoadHealMult*).
//   - On a hit (Gear::on_hit): the ability rows of the perks with OnHit, with the conditions that the backend can
//     evaluate (AttackType, DamageTableRow, RemoteDamageTableRow, TargetStatusEffectCategory, TargetHealthPercent,
//     MyHealthPercent, MyStaminaPercent, MyManaPercent, HasGritActive, AttackerVitalsCategory, TargetVitalsCategory,
//     DontHaveStatusEffect) and ActivationCooldown: BaseDamage (Gear::hit_damage), OtherApplyStatusEffect (a status on
//     the creature: a rend, a damage over time, a chain), DmgPctToHealth (a heal), CooldownTimer with
//     CDRImmediatelyOptions ActiveWeapon (a shorter cooldown of the abilities of the weapon in the hand). A row with a
//     condition that the backend cannot evaluate has no effect (a rule of this project).
//
// The cost: a hit looks at the perks of the items of one character (55 at most) and at the effects on it and on its
// target; no rule looks at other entities, but the chain of a void perk, which looks at the position index near its
// target (test case IZ10 has a time limit).

namespace soulwarden::backend::world {

class World;
struct DamageDealt;

namespace gear_rules {
/** The lowest gear score of a perk scaling: the scaling starts here (Min Possible Weapon Gear Score of the base attributes). */
inline constexpr std::uint32_t kScalingStart = 100;
/** Largest number of swaps of one perk: the passes of the client (a chain of the data: TwoHandSoldier to ArmorSoldier to StatSwap_HP). */
inline constexpr unsigned kSwapChain = 3;
/** Largest number of creatures of one chain of a void perk (a limit of the cost; the spell has MaxChainNum). */
inline constexpr unsigned kChainMost = 8;
/**
 * The bursts of a perk: an ability row of a perk with OnHit, and the spell that a hit that passes its conditions casts
 * around the character (its radius and its damage row are cells of the spell row). The data has no link from the perk
 * row to the spell; the client text of the perk has it (PerkID_Ability_GreatAxe_GravityWell, "Insatiable Gravity Well":
 * "On hit, cast another 4m radius burst around you that deals 50% weapon damage"; the spell InsatiableGravityWell has the
 * radius 4 and the row GreatAxe_GravityWell_Burst_Perk). A rule of this project.
 */
inline constexpr std::array<std::array<const char*, 2>, 1> kPerkBursts = {{{"GlobalPerk_Ability_GreatAxe_GravityWell", "InsatiableGravityWell"}}};
} // namespace gear_rules

/** One perk of an equipped item that has an effect for its character now. */
struct GearPerk {
    /** The perk after the swap of the game. */
    const gamedata::PerkRow* perk{nullptr};
    /** The perk of the instance or of the definition before the swap (for a log line). */
    std::string original;
    float potency{1.0F};
    /** The slot of the paperdoll and the item. */
    std::uint32_t slot{0};
    std::string itemId;
};

/** The equip load classes of a character (the row Player of the encumbrance table). */
enum class LoadClass : std::uint8_t { light, medium, heavy, overburdened };

/** The passive values of the equipment and the effects of a character (Gear::values). */
struct GearValues {
    std::vector<GearPerk> perks;
    std::array<float, attribute::kCount> attributes{};
    /** Parts (0.03: 3 percent more). */
    float maxHealth{0.0F};
    float baseDamage{0.0F};
    float physicalArmor{0.0F};
    float elementalArmor{0.0F};
    /** A part of the damage that the character takes (BaseDamageReduction). */
    float damageReduction{0.0F};
    /** DMG<type> and ABS<type> by the name of the damage type of the combat tables ("Slash", "Corruption"). */
    std::unordered_map<std::string, float> damage;
    std::unordered_map<std::string, float> absorption;
    /** The health that the character gets in each second (a part of its maximum health: HealthModifierPercent with TickRate 1). */
    float regeneration{0.0F};
    float equipLoad{0.0F};
    LoadClass load{LoadClass::light};
};

/** The conversion of the damage of a hit by a gem of the weapon (an affix with DamagePercentage and DamageType). */
struct Conversion {
    std::string type;
    float part{0.0F};
};

/** What a hit of a character on a creature is, for the conditions of a perk. */
struct HitContext {
    /** The damage row of the hit ("GreatAxe_PrimaryAttack", "Rune_Detonate_AoE"), and its attack type ("Light", "Heavy", "Ability", "Heartgem"). */
    std::string row;
    std::string attackType;
    /** The damage that the creature took, and the damage before its defense. */
    float dealt{0.0F};
    float raw{0.0F};
    /** True when the hit was the death of the creature. */
    bool killed{false};
};

/** Counts of the gear system, for the log and for the tests. */
struct GearCounts {
    std::uint64_t refreshes{0};
    std::uint64_t perkHits{0};
    std::uint64_t perkEffects{0};
    std::uint64_t perkHeals{0};
    std::uint64_t cooldownReductions{0};
    std::uint64_t chainHits{0};
    std::uint64_t tickHits{0};
};

class Gear {
public:
    explicit Gear(World& world) : m_world(world) {}

    /** @return The perks of the equipped items of a character that have an effect now, with the swap and the potency. */
    [[nodiscard]] std::vector<GearPerk> perks_of(const Entity& character) const;
    /** @return The perks of one item (its places and the fixed perks of its definition) after the swap, with no condition. */
    [[nodiscard]] std::vector<GearPerk> item_perks(const ItemInstance& item, std::uint32_t slot) const;
    /** @return The perk that the game shows for a perk on an item: the swap table of the game, in the sequence of its ID. */
    [[nodiscard]] const gamedata::PerkRow* swapped(const gamedata::PerkRow& perk, const ItemInstance& item) const;
    /** @return The passive values of the equipment and the effects of a character now. */
    [[nodiscard]] GearValues values(const Entity& character) const;
    /**
     * The equipment, the weapon in the hand, or an effect of a character changed: the attribute bonus, the part of the
     * maximum health, and the equip load of the entity again (Entity::attributes.bonus, Entity::gear), the maximum
     * health through the progress system, and the attribute state when the bonus changed.
     */
    void refresh(Entity& character);

    /** @return The conversion of a hit with the weapon of this item: the first gem of the item with a conversion. */
    [[nodiscard]] std::optional<Conversion> conversion(const ItemInstance& weapon, std::uint32_t slot) const;
    /**
     * @return The part of the damage of one hit of a character on a creature that its gear adds: the passive BaseDamage,
     *         the BaseDamage of each perk with OnHit whose conditions pass (no ActivationCooldown is spent), and the
     *         part of its equip load class.
     */
    [[nodiscard]] float hit_damage(const Entity& attacker, const Entity& target, const HitContext& hit) const;
    /**
     * A hit of a character on a creature was accepted (Combat::strike): the perks with OnHit and an effect (a status on the
     * creature, a heal, a shorter cooldown) and the heart rune charge of the hit. The target can be dead.
     */
    void on_hit(Entity& attacker, Entity& target, const HitContext& hit);
    /** A character took a hit of a creature: the effects that end after hits, and the rune charge of the hit. */
    void on_hit_taken(Entity& character, const gamedata::CreatureContact& contact);
    /** @return The heal factor of the equip load class of a character (EquipLoadHealMult*). */
    [[nodiscard]] float heal_factor(const Entity& character) const;

    [[nodiscard]] const GearCounts& counts() const {
        return m_counts;
    }

private:
    /** @return True when the conditions of an OnHit ability row pass for this hit. */
    [[nodiscard]] bool conditions_pass(const gamedata::CellValues& row, const Entity& attacker, const Entity& target,
                                       const HitContext& hit) const;
    /** Gives the creature the damage over time or the chain of a status of a perk, with the damage of the hit. */
    void perk_status(Entity& attacker, Entity& target, std::string_view status, float potency, const HitContext& hit);
    /** One hit of a perk with a fixed amount (a tick, a chain): through Combat::strike_amount. */
    void perk_damage(EntityId attacker, EntityId target, float amount, const std::string& type, const char* what, float charge);

    World& m_world;
    /** The end of the ActivationCooldown of each ability row of each character (the character id and the row id). */
    std::unordered_map<std::string, WorldClock::time_point> m_activations;
    GearCounts m_counts;
};

} // namespace soulwarden::backend::world
