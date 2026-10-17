#pragma once

// The world of the cases of stage B3 (the haste chain of the bow build of the user): the Boltcaster shop of stage B2
// (support/boltcaster_fixture.h) with the rows of the game data of Dodge and Weave, Archer's Speed, Evasive Tactics, Closing In, Speedy, and
// Alacrity II (the ids and the cells of the rows; the cells of a sound and of the user interface that no rule reads are left out), the dodge
// status action and the tag removal action of playeractions_ability_bow (the ability data of format 7), the tag Bow of the test bow, and a
// test staff in weapon slot 2. The perks of the shoes and of the charm are on the test bow (a made-up place: the rules are the same for each
// item). No id of an account.
//
// The proof that a case fails on the tree before the stage (stage B2, 598cc02): that tree builds the cases with SOULWARDEN_B3_BASELINE (the
// members of the stage count as absent: no dodge status action, no tag removal, no fixed time of a command, the counts of the stage are 0).

#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/boltcaster_fixture.h"

namespace soulwarden::backend::tests::haste {

using namespace boltcaster;

/** The nodes of the bow tree (javelindata_ability_bow) and the action names of playeractions_ability_bow. */
inline constexpr const char* kDodgeWeave = "Passive_Bow_Skirmish_DodgeHaste";
inline constexpr const char* kArcherSpeed = "Passive_Bow_Skirmish_HasteOnSwap";
inline constexpr const char* kEvasive = "Ultimate_Bow_Skirmish";
inline constexpr const char* kClosingIn = "Passive_Bow_Skirmish_CDRVsLow";
inline constexpr const char* kDodgeAction = "Passive_Bow_Skirmish_HasteOnDodge";
inline constexpr const char* kSheathe = "Bow_Remove_StatusEffects_Sheathe";
/** The statuses. */
inline constexpr const char* kDodgeHaste = "Status_Passive_Bow_Skirmish_DodgeHaste";
inline constexpr const char* kSwapHaste = "Status_Passive_Bow_Skirmish_HasteOnSwap";
inline constexpr const char* kSwapTimer = "Status_Passive_Bow_Skirmish_HasteOnSwap_Timer";
inline constexpr const char* kEvasiveEmpower = "Status_Ultimate_Bow_Skirmish_DodgeDmg";
/** A made-up haste of 3 s with DurationMax 4 (the limit of a time factor). */
inline constexpr const char* kShortHaste = "Test_B3_Short_Haste";
/** The perks and their rows (perk tables of the game). */
inline constexpr const char* kSpeedy = "PerkID_Artifact_Set1_MediumFeet";
inline constexpr const char* kSpeedyTime = "GlobalPerk_Artifact_Set1_MediumFeet";
inline constexpr const char* kSpeedyDamage = "GlobalPerk_Artifact_Set1_MediumFeet_2";
inline constexpr const char* kAlacrity = "PerkID_Slottable_Common_DurHaste";
inline constexpr const char* kAlacrityRow = "Ability_Perk_Amulet_DurHaste";
/** The form of Ability_Perk_Amulet_DurEmpower: a time factor with StatusEffectDurationCats and no StatusEffectCategories. */
inline constexpr const char* kEmpowerTime = "PerkID_Test_B3_DurEmpower";
inline constexpr const char* kEmpowerTimeRow = "Ability_Perk_Amulet_DurEmpower";
/** The form of Global_Common_CDRonDodge: a cooldown change after the end of a dodge (Refreshing Evasion of the perks). */
inline constexpr const char* kDodgeCdr = "PerkID_Test_B3_CDRonDodge";
inline constexpr const char* kDodgeCdrRow = "Global_Common_CDRonDodge";
/** A made-up row with an event in the window after an action (the form of PowerThrowBlockBreakPerk1): not a rule of the backend. */
inline constexpr const char* kWindowRule = "Test_B3_WindowAfterAction";
/** The made-up form of Battle Precision (Passive_Bow_Skirmish_DebuffDur): only the outgoing time factor. */
inline constexpr const char* kOutgoingOnly = "Test_B3_OutgoingOnly";
/** The form of BonusDamageSelfCCPerk1 (a value with a condition of a status of the character and no event) and a made-up slow. */
inline constexpr const char* kWhileSlowed = "PerkID_Test_B3_WhileSlowed";
inline constexpr const char* kWhileSlowedRow = "BonusDamageSelfCCPerk1";
inline constexpr const char* kSlow = "Test_B3_Slow";

/** The status rows of the stage (javelindata_statuseffects_weapons, the timer, the empower of Evasive Tactics). */
inline std::vector<gamedata::StatusRow> haste_statuses() {
    gamedata::StatusRow dodge = status(kDodgeHaste, 2.0F,
                                       cells({{"ShowInUITray", 1.0F}, {"AlwaysReplicate", 1.0F}, {"DurationMax", 4.0F}, {"TickRate", 1.0F},
                                              {"RemoveOnDeath", 1.0F}, {"IgnoreDiminishingReturns", 1.0F}, {"StackMax", 1.0F}, {"PotencyMax", 1.0F},
                                              {"SprintSpeedMod", 0.07F}},
                                             {{"EffectCategories", "Buff+Haste"}}));
    dodge.moveSpeed = 0.07F;
    gamedata::StatusRow swap = status(kSwapHaste, 5.0F,
                                      cells({{"ShowInUITray", 1.0F}, {"DurationMax", 10.0F}, {"TickRate", 1.0F}, {"AddOnStackSize", 1.0F},
                                             {"RemoveOnDeath", 1.0F}, {"IgnoreDiminishingReturns", 1.0F}, {"StackMax", 1.0F},
                                             {"RemoveUnappliedStacks", 1.0F}, {"PotencyMax", 1.0F}, {"SprintSpeedMod", 0.07F}},
                                            {{"EffectCategories", "Buff+Haste"}, {"OnStackStatusEffect", kSwapTimer}, {"AddOnStackSizeComparison", "Equal"}}));
    swap.moveSpeed = 0.07F;
    gamedata::StatusRow shortHaste = status(kShortHaste, 3.0F, cells({{"DurationMax", 4.0F}, {"StackMax", 1.0F}, {"SprintSpeedMod", 0.05F}},
                                                                     {{"EffectCategories", "Buff+Haste"}}));
    shortHaste.moveSpeed = 0.05F;
    gamedata::StatusRow empower = status(kEvasiveEmpower, 5.0F,
                                         cells({{"ShowInUITray", 1.0F}, {"DurationMax", 10.0F}, {"TickRate", 1.0F}, {"RemoveOnDeath", 1.0F},
                                                {"IgnoreDiminishingReturns", 1.0F}, {"DMGSlash", 0.15F}, {"DMGThrust", 0.15F}, {"StackMax", 1.0F}},
                                               {{"EffectCategories", "Buff+Empower"}, {"RefreshDuration", "TRUE"}}));
    empower.damage = 0.15F;
    gamedata::StatusRow slow = status(kSlow, 5.0F, cells({{"StackMax", 1.0F}, {"SprintSpeedMod", -0.1F}}, {{"EffectCategories", "Debuff+Slow"}}));
    slow.moveSpeed = -0.1F;
    return {dodge, swap, slow,
            status(kSwapTimer, 10.0F,
                   cells({{"ShowInUITray", 1.0F}, {"UseLightweightReplication", 1.0F}, {"DurationMax", 10.0F}, {"TickRate", 11.0F},
                          {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}},
                         {{"EffectCategories", "Timer"}})),
            empower, shortHaste};
}

/** The node rows of the stage (javelindata_ability_bow), and a made-up row with an event after an action. */
inline std::vector<gamedata::AbilityRow> haste_rows() {
    return {
        {kDodgeWeave, cells({}, {{"WeaponTag", "Bow"}})},
        {kArcherSpeed, cells({{"OnWeaponSwap", 1.0F}},
                             {{"WeaponTag", "Bow"}, {"DontHaveStatusEffect", kSwapTimer}, {"SelfApplyStatusEffect", kSwapHaste}})},
        {kEvasive, cells({{"Duration", 2.0F}},
                         {{"WeaponTag", "Bow"}, {"AfterAction", "Dodge"}, {"AfterSequence", "Dodge_Roll,Dodge_Roll_Fast,Dodge_Roll_Slow"},
                          {"SelfApplyStatusEffect", kEvasiveEmpower}})},
        {kClosingIn, cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"TargetHealthPercent", 50.0F}, {"Duration", -1.0F}, {"CooldownTimer", -0.05F}},
                           {{"WeaponTag", "Bow"}, {"CDRImmediatelyOptions", "ActiveWeapon"}})},
        {kWindowRule, cells({{"OnHit", 1.0F}, {"Duration", 20.0F}, {"BaseDamage", 0.15F}},
                            {{"WeaponTag", "Bow"}, {"AfterAction", "Dodge"}, {"AfterSequence", "Dodge_Roll,Dodge_Roll_Fast,Dodge_Roll_Slow"}})},
        {kOutgoingOnly, cells({{"RequireReaction", 1.0F}, {"TargetStatusEffectDurationMult", 0.2F}},
                              {{"WeaponTag", "Bow"}, {"TargetStatusEffectCategory", "Debuff"}, {"TargetStatusEffectDurationCats", "Debilitate,DoT"}})},
    };
}

/** The perk data: the Boltcaster perks of stage B2 and the perks of the stage (on the test bow). */
inline gamedata::PerkTables haste_perks() {
    gamedata::PerkTables tables = boltcaster_perks();
    tables.perks.push_back(perk(kSpeedy, "Inherent", "OnEquip", "Bow", "", {}, {kSpeedyTime, kSpeedyDamage}));
    // The potency of Alacrity II: made-up steps that give 1.274 at the gear score 200 of the test bow (the value of the game at 800).
    tables.perks.push_back(perk(kAlacrity, "OffensiveCharm", "OnEquip", "Bow", "", {}, {kAlacrityRow}, 1, {{0.0F, 0.00274F}}));
    tables.perks.push_back(perk(kEmpowerTime, "Generated", "OnEquip", "Bow", "", {}, {kEmpowerTimeRow}));
    tables.perks.push_back(perk(kDodgeCdr, "Generated", "OnEquip", "Bow", "", {}, {kDodgeCdrRow}));
    tables.perks.push_back(perk(kWhileSlowed, "Generated", "OnEquip", "Bow", "", {}, {kWhileSlowedRow}));
    tables.abilities["bonusdamageselfccperk1"] = cells({{"BaseDamage", 0.12F}}, {{"StatusEffectCategories", "Root,Slow"}});
    tables.abilities["globalperk_artifact_set1_mediumfeet"] =
        cells({{"StatusEffectDurationReduction", 0.5F}}, {{"StatusEffectCategories", "Haste"}, {"StatusEffectDurationCats", "Haste"}});
    tables.abilities["globalperk_artifact_set1_mediumfeet_2"] =
        cells({{"OnHit", 1.0F}, {"RequireReaction", 1.0F}, {"BaseDamage", 0.1F}}, {{"StatusEffectCategories", "Haste"}});
    tables.abilities["ability_perk_amulet_durhaste"] =
        cells({{"IsStackableAbility", 1.0F}, {"IsStackableMax", 5.0F}, {"StatusEffectDurationReduction", 0.094F}, {"TargetStatusEffectDurationMult", 0.094F}},
              {{"StatusEffectCategories", "Haste"}, {"StatusEffectDurationCats", "Haste"}, {"TargetStatusEffectDurationCats", "Haste"}});
    tables.abilities["ability_perk_amulet_durempower"] =
        cells({{"StatusEffectDurationReduction", 0.094F}, {"TargetStatusEffectDurationMult", 0.094F}},
              {{"StatusEffectDurationCats", "Empower"}, {"TargetStatusEffectDurationCats", "Empower"}});
    tables.abilities["global_common_cdrondodge"] =
        cells({{"IsStackableAbility", 1.0F}, {"IsStackableMax", 1.0F}, {"Duration", 2.0F}, {"CooldownTimer", -0.005F}},
              {{"AfterAction", "Dodge"}, {"AfterSequence", "Dodge_Roll,Dodge_Roll_Fast,Dodge_Roll_Slow"}, {"CDRImmediatelyOptions", "ActiveWeapon"}});
    return tables;
}

/** The world of a case: the Boltcaster shop with the data of the stage. */
struct HasteShop : BoltShop {
    explicit HasteShop(bool observe = false, bool actions = true) : BoltShop(observe) {
        gamedata::CombatData combat = host.combat.data();
        for (gamedata::WeaponRow& weapon : combat.weapons) {
            if (weapon.id == "TestBowRow") {
                weapon.tags = "Bow+LongBow";
            } else if (weapon.id == "TestStaffRow") {
                weapon.tags = "MagicStaff";
            }
        }
        if (!host.combat.set(combat)) {
            throw std::runtime_error("haste test: the combat tables are not valid: " + host.combat.error());
        }
        gamedata::AbilityTables tables = host.abilities.tables();
        std::vector<gamedata::StatusRow> rows = tables.statusEffects;
        for (gamedata::StatusRow& row : haste_statuses()) {
            rows.push_back(std::move(row));
        }
        tables.statusEffects = numbered(std::move(rows));
        for (gamedata::AbilityRow& row : haste_rows()) {
            tables.abilityRows.push_back(std::move(row));
        }
#ifndef SOULWARDEN_B3_BASELINE
        if (actions) {
            gamedata::ActionDodgeStatus dodge;
            dodge.list = "playeractions_ability_bow";
            dodge.action = kDodgeAction;
            dodge.dodge = "Dodge";
            dodge.nodes = {kDodgeWeave};
            dodge.tags = {"Bow"};
            dodge.stamina = gamedata::ActionHitStatus::Compare{"GreaterThanOrEqual", 0.99F};
            dodge.without = {kDodgeHaste};
            dodge.exitTags = {"Bow"};
            dodge.statuses = {kDodgeHaste};
            tables.actionDodgeStatuses = {dodge};
            tables.actionTagRemovals = {gamedata::ActionTagRemoval{"playeractions_ability_bow", kSheathe, {"Bow"}, {kDodgeHaste}}};
        }
#else
        (void)actions;
#endif
        if (!host.abilities.set(tables)) {
            throw std::runtime_error("haste test: the ability data is not valid: " + host.abilities.error());
        }
        gamedata::ProgressionData progression = host.progression.data();
        std::uint32_t column = 30;
        for (const char* id : {kDodgeWeave, kArcherSpeed, kEvasive, kClosingIn, kWindowRule, kOutgoingOnly}) {
            gamedata::AbilityNode node;
            node.id = id;
            node.table = gamedata::progression_id(kBowTable);
            node.column = column++;
            progression.nodes.push_back(node);
        }
        host.progression.set(progression);
        if (!host.perks.set(haste_perks())) {
            throw std::runtime_error("haste test: the perk data is not valid: " + host.perks.error());
        }
    }
    /** The test bow in weapon slot 1 (selected) with perks and nodes, and the test staff in weapon slot 2. */
    void ready(const char* characterId, world::PerkIds perks, std::vector<const char*> nodes, std::uint32_t gearScore = 100) {
        arm(characterId, std::move(perks), gearScore, std::move(nodes));
        world::GiveOptions options;
        options.gearScore = 100;
        const world::ItemResult staff = host.world.inventory().give(characterId, "TestStaff", 1, options);
        if (!staff.ok() || staff.slots.empty() || !host.world.inventory().equip(characterId, staff.slots.front(), world::paperdoll::kMainHand1 + 1).ok()) {
            throw std::runtime_error("haste test: the staff");
        }
    }
    /** The key of a weapon slot (1 or 2) of a character: the change of the weapon of Combat::select_weapon with no input step. */
    void key(unsigned slot, const char* characterId = kCharacterA) {
        world::Entity* own = host.world.find_character(characterId);
        host.world.combat().select_weapon(own->owner, slot);
    }
    /** A dodge of a character now. @return The verdict. */
    world::DodgeVerdict dodge(const char* characterId = kCharacterA) {
        world::Entity* own = host.world.find_character(characterId);
        return host.world.stamina().dodge(*own, host.clock.now(), false);
    }
    /** The clock moves on by `milliseconds`, then one tick of the world. */
    void wait(int milliseconds) {
        host.clock.advance(std::chrono::milliseconds(milliseconds));
        host.world.tick();
    }
    /** @return A status of a character, or null. */
    const world::ActiveStatus* find(const char* status, const char* characterId = kCharacterA) {
        const world::Entity* own = host.world.find_character(characterId);
        if (own == nullptr) {
            return nullptr;
        }
        const auto now = host.clock.now();
        for (const world::ActiveStatus& entry : own->statuses) {
            if (entry.status == status && (entry.permanent || now < entry.ends)) {
                return &entry;
            }
        }
        return nullptr;
    }
    /** @return The seconds to the end of a status (negative: none). */
    float left(const char* status, const char* characterId = kCharacterA) {
        const world::ActiveStatus* entry = find(status, characterId);
        return entry == nullptr ? -1.0F : std::chrono::duration<float>(entry->ends - host.clock.now()).count();
    }
    /** @return The potency of a perk of a character (0: none). */
    float potency(const char* perkId, const char* characterId = kCharacterA) {
        const world::Entity* own = host.world.find_character(characterId);
        for (const world::GearPerk& entry : host.world.gear().perks_of(*own)) {
            if (entry.perk->id == perkId) {
                return entry.potency;
            }
        }
        return 0.0F;
    }
    /** The counts of the stage (0 on the tree before the stage). */
#ifndef SOULWARDEN_B3_BASELINE
    std::uint64_t swap_rules() {
        return host.world.passives().counts().swapRules;
    }
    std::uint64_t dodge_statuses() {
        return host.world.passives().counts().dodgeStatuses;
    }
    std::uint64_t after_dodge() {
        return host.world.passives().counts().afterDodgeRules;
    }
    std::uint64_t tag_removals() {
        return host.world.passives().counts().tagRemovals;
    }
    std::uint64_t on_stack() {
        return host.world.statuses().counts().onStack;
    }
#else
    std::uint64_t swap_rules() {
        return 0;
    }
    std::uint64_t dodge_statuses() {
        return 0;
    }
    std::uint64_t after_dodge() {
        return 0;
    }
    std::uint64_t tag_removals() {
        return 0;
    }
    std::uint64_t on_stack() {
        return 0;
    }
#endif
};

/** @return The status numbers of the full map (group `group` field 0) of each player status fragment of a client from the message `from` on. */
inline std::vector<std::uint16_t> full_map_numbers(const FakeClient& client, unsigned group, std::size_t from = 0) {
    std::vector<std::uint16_t> numbers;
    for (const Fragment& fragment : fragments_of(client, 44, 4236, from)) {
        const auto found = fragment.fields.find({group, 0});
        if (found == fragment.fields.end()) {
            continue;
        }
        server::hub::Reader reader(found->second);
        const auto count = progress_wire::read_full_form(reader);
        for (std::uint32_t index = 0; count && index < *count && reader.ok(); ++index) {
            numbers.push_back(reader.u16());
            reader.u64();
            reader.u16();
            reader.u8();
            reader.u8();
        }
    }
    return numbers;
}

/** @return The status numbers of the compact maps (group 3 field 1) of each player status fragment of a client from the message `from` on. */
inline std::vector<std::uint16_t> compact_numbers(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::uint16_t> numbers;
    for (const Fragment& fragment : fragments_of(client, 44, 4236, from)) {
        const auto found = fragment.fields.find({3, 1});
        if (found == fragment.fields.end()) {
            continue;
        }
        server::hub::Reader reader(found->second);
        const auto count = progress_wire::read_full_form(reader);
        for (std::uint32_t index = 0; count && index < *count && reader.ok(); ++index) {
            numbers.push_back(reader.u16());
            reader.u8();
        }
    }
    return numbers;
}

} // namespace soulwarden::backend::tests::haste
