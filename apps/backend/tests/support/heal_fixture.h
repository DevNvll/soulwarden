#pragma once

// The world of the cases of stage 3.3b-S6 (the heals of an ability and of a friend): the world of the spell kinds (stage 3.3b-S2b) with
// the rows of the heals of the life staff in the form of the game data (Divine Embrace, Sacred Ground, the Beacon with
// UseHealScalingValue), the heal modifiers of the statuses (Disease, the arena debuff, Anointed, a lifesteal potion), a potion, a regen
// potion, a food, and the rules with OnHealed (Divine Blessing; a rule with an effect after a heal). All ids, names, and values are made
// up, but for the names and the cells of the rows of the data that the rules read.

#include <string>
#include <vector>

#include "support/spell_kind_fixture.h"

namespace soulwarden::backend::tests::heal {

using namespace spell_kind;

/** The action bit of the self cast input `self_target` (LCTRL; live check of stage 3.3b-S6). */
inline constexpr unsigned kSelfTargetBit = 430;
inline constexpr const char* kDivineNode = "Ability_Lifestaff_DivineEmbrace";
inline constexpr const char* kSacredNode = "Ability_Lifestaff_SacredGround";
inline constexpr const char* kPrivilege = "Upgrade_Lifestaff_DivineEmbrace_ReduceCost";
inline constexpr const char* kAnointed = "Upgrade_Lifestaff_SacredGround_DmgOnEnd";
inline constexpr const char* kBlessing = "Ultimate_Lifestaff_Healing";
inline constexpr const char* kDesperate = "Passive_Lifestaff_Healing_CDROnLow";
inline constexpr const char* kDivineStatus = "Status_Ability_Lifestaff_DivineEmbrace";
inline constexpr const char* kSacredStatus = "Status_Ability_Lifestaff_SacredGround";
inline constexpr const char* kSacredPotency = "Status_Ability_Lifestaff_SacradGround_Beacon_Potency";
inline constexpr const char* kAnointedStatus = "Status_Ability_Lifestaff_SacredGround_Upgrade2";
inline constexpr const char* kDisease = "Disease";
inline constexpr const char* kArenaDebuff = "PVP_Arena_ScalingHealingDebuff";
inline constexpr const char* kLifestealPotion = "PotionLifestealT5";
inline constexpr const char* kPotion = "Test_PotionHeal";
inline constexpr const char* kRegenPotion = "MajorPotionRegen";
inline constexpr const char* kFood = "FoodRecoveryT1";
inline constexpr const char* kDivineSpell = "Spell_LifeStaff_DivineEmbrace";
inline constexpr const char* kSacredSpell = "Spell_LifeStaff_SacredGround_Self";
inline constexpr float kDivineCost = 25.0F;
inline constexpr float kPrivilegeCost = 20.0F;
inline constexpr float kSacredCost = 15.0F;
/** The damage value of Focus 6 in the tables of the cases (the column ModifierValueSum of Focus 300 of the game: 330.85). */
inline constexpr float kFocusDamage = 330.85F;
/** The ScalingFocus of the life staff of the cases (LifeStaffT5 of the game: 0.0095). */
inline constexpr float kScalingFocus = 0.0095F;
/** The heal base of the cases with Focus 6: the base damage of the staff at gear score 100 (100) times 1 + 0.0095 x 330.85. */
inline constexpr float kBase = kWeaponDamage * (1.0F + kScalingFocus * kFocusDamage);

/** The ability data of the cases: the spell kinds and the rows of the heals. */
inline gamedata::AbilityTables heal_abilities() {
    gamedata::AbilityTables tables = kind_abilities();
    std::vector<gamedata::StatusRow> rows;
    for (gamedata::StatusRow row : tables.statusEffects) {
        if (row.id == kBeaconStatus) {
            // The Beacon of the game data: a heal with UseHealScalingValue (stage 3.3b-S2b had a pulse with no heal).
            row.cells.numbers["UseHealScalingValue"] = 1.0F;
            row.cells.texts["EffectCategories"] = "Heal+LifestaffBuff+BeaconCat";
        }
        rows.push_back(row);
    }
    const auto heal_status = [&](const char* id, float duration, float tick, float coefficient, const char* categories) {
        rows.push_back(status(id, duration,
                              cells({{"TickRate", tick}, {"HealthModifierDamageBased", coefficient}, {"UseHealScalingValue", 1.0F}, {"StackMax", 1.0F},
                                     {"ShowInUITray", 1.0F}},
                                    {{"EffectCategories", categories}})));
    };
    heal_status(kDivineStatus, 1.0F, 2.0F, 1.2F, "Heal");
    heal_status(kSacredStatus, 12.0F, 1.0F, 0.16F, "Heal+LifestaffBuff+SacredGroundCat");
    rows.push_back(status(kSacredPotency, 12.0F, cells({{"TickRate", 1.0F}}, {{"EffectPotencyMods", "BeaconCat=-0.1"}})));
    rows.push_back(status(kAnointedStatus, 12.0F, cells({{"TickRate", 1.0F}, {"NonConsumableHealMod", 0.5F}}, {{"EffectCategories", "Heal"}})));
    rows.push_back(status(kDisease, -1.0F, cells({{"HealMod", -0.5F}, {"StackMax", 1.0F}}, {{"EffectCategories", "Debuff+Disease"}})));
    rows.push_back(status(kArenaDebuff, -1.0F, cells({{"HealMod", -0.06F}, {"HealScalingValueMultiplier", -0.03F}, {"StackMax", 12.0F}},
                                                     {{"EffectCategories", "NonDispellableDebuff+UnclampedDisease"}})));
    rows.push_back(status(kLifestealPotion, -1.0F, cells({{"DmgPctToHealthHealMod", 0.35F}, {"StackMax", 1.0F}}, {{"EffectCategories", "SurvivalPotion+Anointed"}})));
    rows.push_back(status(kPotion, -1.0F, cells({{"Health", 400.0F}, {"HealthModifierPercent", 0.1F}}, {{"EffectCategories", "SurvivalPotion"}})));
    rows.push_back(status(kRegenPotion, 5.0F, cells({{"TickRate", 1.0F}, {"HealthModifierPercent", 0.01F}, {"ShowUiDamageNumbersOnHeal", 1.0F}})));
    rows.push_back(status(kFood, 60.0F, cells({{"TickRate", 2.0F}, {"HealthModifierPercent", 0.01F}})));
    rows.push_back(status("Status_Passive_Lifestaff_Healing_CDROnLow_Timer", 5.0F, {}));
    tables.statusEffects = numbered(std::move(rows));
    tables.statusCategories.push_back({"Disease", {{"HealMod", -0.5F}}});
    tables.cooldowns.push_back({kDivineNode, 6.0F, 0.0F});
    tables.cooldowns.push_back({kSacredNode, 20.0F, 0.0F});
    tables.manaCosts.push_back({"LifeStaff_TouchOfLife", kDivineCost});
    tables.manaCosts.push_back({"LifeStaff_TouchOfLife_Mana_Upgrade", kPrivilegeCost});
    tables.manaCosts.push_back({"LifeStaff_HealingWave", kSacredCost});
    const auto direct = [&](const char* id, const char* cost) {
        tables.spells.push_back({id, 0.5F, 5.0F, 25.0F, 0, 0.0F, 500.0F,
                                 cells({}, {{"StatusEffects", kDivineStatus}, {"ManaCost", cost}, {"DamageTableRow", "LifeStaff_DivineEmbrace"},
                                            {"TargetTypes", "Friend"}})});
    };
    direct(kDivineSpell, "LifeStaff_TouchOfLife");
    direct("Spell_LifeStaff_DivineEmbrace_ReducedCost", "LifeStaff_TouchOfLife_Mana_Upgrade");
    const std::string sacred = std::string(kSacredStatus) + "," + kSacredPotency;
    // The cells ClearOnExit and ClearOnEnd of the rows of the game (stage 3.3b-M2: the area takes its statuses back by these cells).
    tables.spells.push_back({kSacredSpell, 3.0F, 5.0F, 25.0F, 0, 0.0F, 12000.0F,
                             cells({{"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}},
                                   {{"StatusEffects", sacred}, {"ManaCost", "LifeStaff_HealingWave"}, {"DamageTableRow", "Lifestaff_SacredGround"},
                                    {"TargetTypes", "Friend"}})});
    tables.spells.push_back({"Spell_LifeStaff_SacredGround_Self_Upgrade2", 3.0F, 5.0F, 25.0F, 0, 0.0F, 12000.0F,
                             cells({{"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}},
                                   {{"StatusEffects", sacred + "," + kAnointedStatus}, {"ManaCost", "LifeStaff_HealingWave"},
                                    {"DamageTableRow", "Lifestaff_SacredGround"}, {"TargetTypes", "Friend"}})});
    // The active row of Divine Embrace (TargetStatusEffect NotEqual: not a target with the status of its heal), Divine Blessing
    // (OnHealed, TargetHealthPercent 50 with the blank comparator, HealScalingValueMultiplier 0.3), and Desperate Speed (an effect
    // after a heal: not applied).
    tables.abilityRows.push_back({kDivineNode, cells({}, {{"TargetStatusEffect", kDivineStatus}, {"TargetStatusEffectComparison", "NotEqual"},
                                                         {"CooldownId", kDivineNode}})});
    tables.abilityRows.push_back({kSacredNode, cells({}, {{"CooldownId", kSacredNode}})});
    tables.abilityRows.push_back({kBlessing, cells({{"OnHealed", 1.0F}, {"TargetHealthPercent", 50.0F}, {"HealScalingValueMultiplier", 0.3F}},
                                                   {{"WeaponTag", "Heal"}})});
    tables.abilityRows.push_back({kDesperate, cells({{"OnHealed", 1.0F}, {"TargetHealthPercent", 50.0F}, {"ActivationCooldown", 5.0F}, {"CooldownTimer", -0.1F}},
                                                    {{"WeaponTag", "Heal"}, {"CDRImmediatelyOptions", "ActiveWeapon"},
                                                     {"SelfApplyStatusEffect", "Status_Passive_Lifestaff_Healing_CDROnLow_Timer"}})});
    return tables;
}

/** The progression of the cases: the spell kinds and the nodes of the heals in the tree of the life staff. */
inline gamedata::ProgressionData heal_progression() {
    gamedata::ProgressionData data = kind_progression();
    std::uint32_t column = 50;
    for (const auto& [id, active] : std::vector<std::pair<const char*, bool>>{
             {kDivineNode, true}, {kSacredNode, true}, {kPrivilege, false}, {kAnointed, false}, {kBlessing, false}, {kDesperate, false}}) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = gamedata::progression_id(kLifeMagic);
        node.column = column++;
        node.active = active;
        data.nodes.push_back(node);
    }
    return data;
}

/** The spell data of the cases: the spell kinds and the slice of Sacred Ground (the structure of spell_aoe_heal_foe). */
inline gamedata::SpellTables heal_spells() {
    gamedata::SpellTables tables = kind_tables();
    gamedata::SpellSlice sacred = beacon_slice();
    sacred.path = "slices/test/sacred.dynamicslice";
    sacred.asset = made_up_asset(0x64);
    sacred.root = 0x0000030000000064ULL;
    tables.slices.push_back(sacred);
    const auto index = static_cast<std::int32_t>(tables.slices.size() - 1);
    tables.spells.push_back({kSacredSpell, index});
    tables.spells.push_back({"Spell_LifeStaff_SacredGround_Self_Upgrade2", index});
    return tables;
}

/** The world of a case: the spell kinds with the rows of the heals, and a life staff with the ScalingFocus of the game. */
struct HealShop : KindShop {
    explicit HealShop(bool observe = false) : KindShop(observe) {
        gamedata::CombatData combat = host.combat.data();
        for (gamedata::WeaponRow& weapon : combat.weapons) {
            if (weapon.id == "TestLifeStaffRow") {
                weapon.scaling[world::attribute::kFocus] = kScalingFocus;
            }
        }
        combat.attributeDamage[world::attribute::kFocus] = {0.0F, kFocusDamage};
        if (!host.combat.set(combat)) {
            throw std::runtime_error("heal test: the combat tables: " + host.combat.error());
        }
        host.progression.set(heal_progression());
        if (!host.abilities.set(heal_abilities())) {
            throw std::runtime_error("heal test: the ability data: " + host.abilities.error());
        }
        if (!host.spells.set(heal_spells())) {
            throw std::runtime_error("heal test: the spell data: " + host.spells.error());
        }
    }
    /** The life staff in the hand of a character with Focus 6, the node of an ability on place Q, and more nodes. */
    void staff(world::Entity& character, const char* node, std::vector<const char*> more = {}) {
        learn(character, "TestLifeStaff", kLifeMagic, node, std::move(more));
        world::Entity& again = *host.world.find_character(character.characterId);
        again.attributes.spent[world::attribute::kFocus] = 1;
        again.vitals.mana = again.vitals.manaMax;
    }
    /** Puts a status of the data on a character (a command source, or a different source). */
    world::StatusVerdict give_status(world::Entity& character, const char* id, float seconds = 0.0F, int count = 1,
                                     world::StatusSourceKind kind = world::StatusSourceKind::command) {
        world::StatusVerdict verdict = world::StatusVerdict::unknown;
        for (int index = 0; index < count; ++index) {
            world::StatusApply how;
            how.seconds = seconds;
            how.source = world::StatusSource{kind, id, 0, character.id};
            verdict = host.world.statuses().apply(character, id, how);
        }
        return verdict;
    }
    /**
     * The key of place Q, then the confirm of the aim (live check of stage 3.3b-S6): the self cast input (`self` true) or a click.
     */
    void cast(bool self) {
        walker.press(0);
        walker.bits({self ? kSelfTargetBit : kActionAttack});
    }
    const world::HealCounts& heals() {
        return host.world.health().heal_counts();
    }
    const world::AbilityCounts& ability_counts() {
        return host.world.abilities().counts();
    }
};

/** The health and the change flags (field 3 of group 0) of each vitals state of a client from the message `from` on. */
inline std::vector<std::pair<float, std::uint8_t>> vitals_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::pair<float, std::uint8_t>> states;
    for (const Fragment& fragment : fragments_of(client, server::rep::kVitalsSlot, server::rep::kTypeVitalsState, from)) {
        const auto health = field_of(fragment, 0, 0);
        const auto flags = field_of(fragment, 0, 3);
        if (health.size() == 4) {
            server::hub::Reader reader(health);
            states.emplace_back(reader.f32(), flags.empty() ? 0xFF : flags[0]);
        }
    }
    return states;
}

/** The (healed target id, amount) of each message 4298 that a client got from the message `from` on. */
inline std::vector<std::pair<std::uint64_t, float>> notices_of(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::pair<std::uint64_t, float>> notices;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != server::rep::kTypeHealedPlayer) {
            continue;
        }
        server::hub::Reader reader(client.got[index].body);
        reader.u64();
        reader.u64();
        const std::uint64_t target = reader.u64();
        notices.emplace_back(target, reader.f32());
    }
    return notices;
}

} // namespace soulwarden::backend::tests::heal
