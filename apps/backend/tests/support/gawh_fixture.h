#pragma once

// The world of the cases of stage 3.3b-M2 (the other abilities of the great axe and the war hammer): the shop of the itemization cases
// with the weapon actions of format 2. The axe and the hammer graphs have the form and the times of the fragments of the game data
// (tools/gamedata/weapon-actions.sh) with made-up rows; the damage rows have made-up factors and impacts in the form of the data; the
// status rows, the spells, and the rules of the nodes have the id texts of the game where a rule of the backend names them, with
// made-up values. No file of the game and no value of a capture is here.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "support/attack_fixture.h"
#include "support/motion_fixture.h"

namespace soulwarden::backend::tests::gawh {

using namespace shop;
namespace af = attack_fixture;

inline const std::uint32_t kReap = gamedata::progression_id("Ability_Greataxe_Reap");
inline const std::uint32_t kExecute = gamedata::progression_id("Ability_Greataxe_Executioner");
inline const std::uint32_t kWhirlwind = gamedata::progression_id("Ability_Greataxe_Whirlwind");
inline const std::uint32_t kCharge = gamedata::progression_id("Ability_Greataxe_Charge");
inline const std::uint32_t kMaelstrom = gamedata::progression_id("Ability_Greataxe_Maelstorm");
inline const std::uint32_t kGavel = gamedata::progression_id("Ability_WarHammer_MightyGavel");
inline const std::uint32_t kArmorBreaker = gamedata::progression_id("Ability_Warhammer_ArmorBreaker");
inline const std::uint32_t kClearOut = gamedata::progression_id("Ability_WarHammer_ClearOut");
/** The nodes of the cases (the id texts of the game). */
inline constexpr const char* kCollector = "Upgrade_Greataxe_Reap_RngInc";
inline constexpr const char* kGravity = "Passive_Greataxe_Mauler_PullDur";
inline constexpr const char* kFatalAttraction = "Upgrade_Greataxe_Reap_SpingAttack";
inline constexpr const char* kUnendingWinds = "Upgrade_Greataxe_Whirlwind_MoreSpins";
inline constexpr const char* kArtifact = "GlobalPerk_Artifact_Set2_GreataxePerk";
inline constexpr const char* kAbsorb = "Upgrade_Greataxe_Maelstorm_AbsorbProjectiles";
inline constexpr const char* kDmgPerDistance = "Upgrade_Greataxe_Charge_DmgPerDistance";
inline constexpr const char* kSwingAttack = "Upgrade_Greataxe_Charge_SwingAttack";
inline constexpr const char* kAddGrit = "Upgrade_Greataxe_Executioner_AddGrit";
inline constexpr const char* kSpeed = "Upgrade_Greataxe_Whirlwind_Speed";
inline constexpr const char* kFortifyAoe = "Upgrade_WarHammer_ClearOut_FortifyAOE";
inline constexpr const char* kGuardedSprint = "Passive_WarHammer_CrowdCrusher_RangeAbsorbSprint";
inline constexpr const char* kGravityRoot = "Status_Passive_GreatAxe_Gravity_Root";
inline constexpr const char* kGravityHold = "Status_Passive_GreatAxe_Gravity_Root_Hold";
inline constexpr const char* kFortifyBuff = "WarHammer_ClearOut_FortifyBuff";
inline constexpr const char* kSprintStatus = "WarHammer_Pass_SprintDmgReduction";
inline constexpr const char* kShieldSpell = "Spell_Upgrade_GreatAxe_Maelstrom_ProjectileConsume";
/** The block press (block_press_virtual, bit 484). */
inline constexpr unsigned kBlockBit = 484;

const char* const kAxeNodes[] = {"Ability_Greataxe_Reap", "Ability_Greataxe_Executioner", "Ability_Greataxe_Whirlwind",
                                 "Ability_Greataxe_Charge", "Ability_Greataxe_Maelstorm", kCollector, kGravity, kFatalAttraction,
                                 kUnendingWinds, kAbsorb, kDmgPerDistance, kSwingAttack, kAddGrit, kSpeed};
const char* const kHammerNodes[] = {"Ability_WarHammer_MightyGavel", "Ability_Warhammer_ArmorBreaker", "Ability_WarHammer_ClearOut",
                                    kFortifyAoe, kGuardedSprint};

/** A start window of an ability (the condition CanAttack_<ability> of the action lists): a press of its key in the window. */
inline gamedata::AttackTransition ability_window(gamedata::TransitionKind kind, float start, float end, const char* to, const char* ability) {
    gamedata::AttackTransition result = af::transition(kind, start, end, gamedata::AttackInput::ability, to);
    result.ability = ability;
    result.abilityId = gamedata::progression_id(ability);
    return result;
}

/** The two start windows of each ability of a weapon in a fragment: buffered from `buffer` to `from`, then instant to the end. */
inline std::vector<gamedata::AttackTransition> ability_windows(float buffer, float from, const std::vector<std::pair<const char*, const char*>>& abilities) {
    std::vector<gamedata::AttackTransition> windows;
    for (const auto& [fragment, ability] : abilities) {
        windows.push_back(ability_window(gamedata::TransitionKind::buffered, buffer, from, fragment, ability));
        windows.push_back(ability_window(gamedata::TransitionKind::instant, from, -1.0F, fragment, ability));
    }
    return windows;
}

/** A transition inside an ability with the condition of the data as terms (an OR of ANDs). */
inline gamedata::AttackTransition when(float start, float end, const char* to, std::vector<std::vector<gamedata::ConditionLiteral>> terms) {
    gamedata::AttackTransition result = af::transition(gamedata::TransitionKind::instant, start, end, gamedata::AttackInput::terms, to);
    result.terms = std::move(terms);
    return result;
}

inline gamedata::ConditionLiteral node_literal(const char* node, bool negated = false) {
    gamedata::ConditionLiteral literal;
    literal.kind = gamedata::LiteralKind::node;
    literal.text = node;
    literal.id = gamedata::progression_id(node);
    literal.negated = negated;
    return literal;
}

inline gamedata::ConditionLiteral input_literal(gamedata::LiteralKind kind, bool negated = false) {
    gamedata::ConditionLiteral literal;
    literal.kind = kind;
    literal.text = kind == gamedata::LiteralKind::attack ? "attack" : kind == gamedata::LiteralKind::block ? "block" : "collided";
    literal.negated = negated;
    return literal;
}

inline gamedata::ConditionLiteral hits_literal(std::uint32_t count) {
    gamedata::ConditionLiteral literal;
    literal.kind = gamedata::LiteralKind::hits;
    literal.count = count;
    return literal;
}

/** A capsule of a damage clip with its offset along the axis of the weapon (the third offset value of the data). */
inline gamedata::AttackShape capsule(float radius, float halfHeight, float along) {
    gamedata::AttackShape shape;
    shape.type = "MeleeAttackShapeCastTypeCapsule";
    shape.radius = radius;
    shape.halfHeight = halfHeight;
    shape.offset[2] = along;
    shape.joint = "master_root";
    return shape;
}

inline gamedata::StrikeRow shaped(const char* id, std::vector<std::string> needs, std::vector<std::string> forbids, std::uint32_t shape) {
    gamedata::StrikeRow result = af::row(id, std::move(needs), std::move(forbids));
    result.shape = shape;
    return result;
}

inline gamedata::AttackStrike strike_of(const char* key, float start, float end, std::vector<gamedata::StrikeRow> rows,
                                        std::vector<gamedata::AttackShape> shapes) {
    gamedata::AttackStrike result = af::strike(key, start, end, std::move(rows));
    result.shapes = std::move(shapes);
    return result;
}

inline gamedata::AttackFragment with_mark(gamedata::AttackFragment fragment, float canTransition) {
    fragment.canTransition = canTransition;
    return fragment;
}

inline gamedata::AbilityAction::Selection selection(const char* fragment, std::vector<std::string> needs, std::vector<std::string> perks = {},
                                                    std::vector<std::string> perkForbids = {}) {
    gamedata::AbilityAction::Selection result;
    result.fragmentName = fragment;
    result.needs = std::move(needs);
    result.perks = std::move(perks);
    result.perkForbids = std::move(perkForbids);
    return result;
}

inline gamedata::AbilityAction action_of(const char* ability, const char* fragment, std::vector<gamedata::AbilityAction::Selection> selections = {},
                                         std::vector<gamedata::AbilityAction::Cast> casts = {}) {
    gamedata::AbilityAction result;
    result.ability = ability;
    result.abilityId = gamedata::progression_id(ability);
    result.action = std::string("Test_") + ability;
    result.fragmentName = fragment;
    result.selections = std::move(selections);
    result.casts = std::move(casts);
    return result;
}

inline gamedata::NodeUse node_use(const char* node, const char* use, const char* place) {
    return gamedata::NodeUse{node, gamedata::progression_id(node), use, place};
}

/** The axe graph: the form and the times of the fragments of the great axe of the game data. */
inline gamedata::WeaponActionSet axe_graph() {
    using gamedata::AttackInput;
    using gamedata::LiteralKind;
    const auto buffered = gamedata::TransitionKind::buffered;
    const auto instant = gamedata::TransitionKind::instant;
    const std::vector<std::pair<const char*, const char*>> abilities = {
        {"Ability_GreatAxe_JudgementsReach", "Ability_Greataxe_Reap"},  {"Ability_GreatAxe_Executioner", "Ability_Greataxe_Executioner"},
        {"Ability_GreatAxe_Whirlwind", "Ability_Greataxe_Whirlwind"},   {"Ability_GreatAxe_Engage", "Ability_Greataxe_Charge"},
        {"Ability_GreatAxe_Maelstrom", "Ability_Greataxe_Maelstorm"}};
    const auto with_windows = [&](std::vector<gamedata::AttackTransition> own, float buffer, float from) {
        for (gamedata::AttackTransition& window : ability_windows(buffer, from, abilities)) {
            own.push_back(std::move(window));
        }
        return own;
    };
    const auto spin = [](const char* key, float start, float end) {
        return strike_of(key, start, end, {shaped("GreatAxe_Whirlwind", {}, {}, 0)}, {capsule(0.35F, 0.75F, 0.0F)});
    };
    gamedata::WeaponActionSet axe;
    axe.tags = {"2H_Melee", "GreatAxe"};
    axe.fragments = {
        af::fragment("Attack_Start_Charge", 0.7F,
                     {af::transition(instant, 0.0F, 0.333333F, AttackInput::notHeld, "Attack_Primary"),
                      af::transition(instant, 0.333333F, -1.0F, AttackInput::released, "Attack_Primary")}),
        with_mark(af::fragment("Attack_Primary", 1.616667F,
                               with_windows({af::transition(buffered, 0.1875F, 0.566667F, AttackInput::virtualInput, "Attack_Start_Charge"),
                                             af::transition(instant, 0.566667F, -1.0F, AttackInput::virtualInput, "Attack_Start_Charge")},
                                            0.0F, 0.1F),
                               {af::strike("atk1", 0.3F, 0.4F, {af::row("Test_Light")})}),
                  1.166667F),
        with_mark(af::fragment("Ability_GreatAxe_JudgementsReach", 1.676667F,
                               with_windows({af::transition(buffered, 0.3F, 0.9F, AttackInput::virtualInput, "Attack_Start_Charge")}, 0.3F, 0.9F),
                               {strike_of("atk1", 0.3F, 0.366667F,
                                          {shaped("GreatAxe_JudgementsReach", {}, {kCollector}, 0), shaped("GreatAxe_JudgementsReach", {kCollector}, {}, 1)},
                                          {capsule(0.75F, 1.5F, 0.0F), capsule(0.75F, 3.0F, 1.5F)}),
                                strike_of("atk2", 0.433333F, 0.466667F,
                                          {shaped("GreatAxe_JudgementsReach_Pull", {}, {kGravity}, 0),
                                           shaped("GreatAxe_JudgementsReach_upgrade", {kCollector}, {kGravity}, 1),
                                           shaped("GreatAxe_JudgementsReach_upgrade2", {kCollector}, {kGravity}, 2),
                                           shaped("GreatAxe_JudgementsReach_Pull_RP2", {kGravity}, {}, 0),
                                           shaped("GreatAxe_JudgementsReach_upgrade_RP2", {kGravity, kCollector}, {}, 1),
                                           shaped("GreatAxe_JudgementsReach_upgrade2_RP2", {kGravity, kCollector}, {}, 2),
                                           shaped("GreatAxe_JudgementsReach_Close_RP2", {kGravity}, {}, 3)},
                                          {capsule(0.75F, 0.3F, 1.2F), capsule(0.75F, 0.3F, 2.7F), capsule(0.75F, 0.3F, 4.3F), capsule(0.6F, 0.0F, 0.0F)})}),
                  0.933333F),
        with_mark(af::fragment("Ability_GreatAxe_JudgementsReach_Upgrade", 2.176667F, {},
                               {strike_of("atk1", 0.3F, 0.366667F, {shaped("GreatAxe_JudgementsReach", {}, {}, 0)}, {capsule(0.75F, 2.9F, 0.0F)}),
                                strike_of("atk3", 0.966667F, 1.133333F, {shaped("GreatAxe_JudgementsReach_FA", {}, {}, 0)}, {capsule(0.5F, 0.5F, 0.0F)})}),
                  1.533333F),
        with_mark(af::fragment("Ability_GreatAxe_Executioner", 2.044444F,
                               {af::transition(buffered, 1.0F, 1.533333F, AttackInput::virtualInput, "Attack_Start_Charge")},
                               {strike_of("atk1", 0.666667F, 0.766667F, {shaped("GreatAxe_Executioner", {}, {}, 0)}, {capsule(0.35F, 0.75F, 0.0F)})}),
                  1.533333F),
        with_mark(af::fragment("Ability_GreatAxe_Whirlwind", 0.0F,
                               with_windows({when(2.133333F, -1.0F, "Ability_GreatAxe_Whirlwind_Exit_Long", {{}}),
                                             when(1.966667F, -1.0F, "Ability_GreatAxe_Whirlwind_Exit_Short", {{}})},
                                            0.5F, 1.3F),
                               {spin("atk1", 0.5F, 0.866667F), spin("atk2", 0.866667F, 1.233333F), spin("atk3", 1.233333F, 1.575F),
                                spin("atk4", 1.575F, 2.25F)}),
                  2.466667F),
        with_mark(af::fragment("Ability_GreatAxe_Whirlwind_Upgrade", 0.0F,
                               with_windows({when(2.3F, 3.033333F, "Ability_GreatAxe_Whirlwind_Exit_Short", {{node_literal(kUnendingWinds, true)}}),
                                             when(3.033333F, -1.0F, "Ability_GreatAxe_Whirlwind_Exit_Short", {{}}),
                                             when(1.933333F, 2.3F, "Ability_GreatAxe_Whirlwind_Exit_Short", {{hits_literal(1)}}),
                                             when(2.3F, 2.666667F, "Ability_GreatAxe_Whirlwind_Exit_Short", {{hits_literal(2)}}),
                                             when(2.666667F, 3.033333F, "Ability_GreatAxe_Whirlwind_Exit_Short", {{hits_literal(3)}})},
                                            0.5F, 1.733333F),
                               {spin("atk1", 0.5F, 0.866667F), spin("atk2", 0.866667F, 1.233333F), spin("atk3", 1.233333F, 1.575F),
                                spin("atk4", 1.575F, 1.933333F), spin("atk5", 1.933333F, 2.3F), spin("atk6", 2.3F, 2.666667F),
                                spin("atk7", 2.666667F, 3.033333F)}),
                  10.0F),
        with_mark(af::fragment("Ability_GreatAxe_Whirlwind_Upgrade_Artifact", 1.5F, {}, {spin("atk1", 0.5F, 0.866667F)}), 1.0F),
        with_mark(af::fragment("Ability_GreatAxe_Whirlwind_Exit_Short", 2.111111F,
                               with_windows({af::transition(buffered, 0.0F, 0.666667F, AttackInput::virtualInput, "Attack_Start_Charge")}, 0.0F, 0.533333F)),
                  0.7F),
        with_mark(af::fragment("Ability_GreatAxe_Whirlwind_Exit_Long", 2.5F, with_windows({}, 0.0F, 0.6F)), 0.9F),
        with_mark(af::fragment("Ability_GreatAxe_Engage", 0.0F,
                               with_windows({when(0.1F, 0.966667F, "Ability_GreatAxe_Engage_Uppercut",
                                                  {{input_literal(LiteralKind::attack), node_literal(kDmgPerDistance, true)}}),
                                             when(0.966667F, -1.0F, "Ability_GreatAxe_Engage_Uppercut",
                                                  {{input_literal(LiteralKind::attack, true), input_literal(LiteralKind::collided, true),
                                                    node_literal(kDmgPerDistance, true)}}),
                                             when(0.1F, 0.4F, "Ability_GreatAxe_Engage_Swing", {{input_literal(LiteralKind::block), node_literal(kSwingAttack)}}),
                                             when(0.133333F, 0.933333F, "Ability_GreatAxe_Engage_Uppercut",
                                                  {{input_literal(LiteralKind::collided), input_literal(LiteralKind::attack, true),
                                                    node_literal(kDmgPerDistance, true)}})},
                                            1.133333F, 1.538462F)),
                  1.633333F),
        with_mark(af::fragment("Ability_GreatAxe_Engage_Uppercut", 1.666667F, {},
                               {strike_of("atk1", 0.033333F, 0.166667F, {shaped("GreatAxe_Engage1", {}, {}, 0)}, {capsule(0.4F, 0.75F, 0.0F)})}),
                  0.833333F),
        with_mark(af::fragment("Ability_GreatAxe_Engage_Swing", 2.09F, {},
                               {strike_of("atk1", 0.1F, 0.233333F, {shaped("GreatAxe_Engage_Swing1", {}, {}, 0)}, {capsule(0.4F, 0.75F, 0.0F)})}),
                  0.833333F),
        with_mark(af::fragment("Ability_GreatAxe_Maelstrom", 1.783333F, {},
                               {strike_of("atk1", 0.366667F, 1.033333F, {shaped("GreatAxe_Maelstrom", {}, {}, 0)}, {capsule(0.35F, 0.8F, 0.0F)})}),
                  0.866667F),
    };
    gamedata::AbilityAction::Cast shield;
    shield.spell = kShieldSpell;
    shield.needs = {kAbsorb};
    axe.abilities = {action_of("Ability_Greataxe_Reap", "Ability_GreatAxe_JudgementsReach",
                               {selection("Ability_GreatAxe_JudgementsReach_Upgrade", {kFatalAttraction})}),
                     action_of("Ability_Greataxe_Executioner", "Ability_GreatAxe_Executioner"),
                     action_of("Ability_Greataxe_Whirlwind", "Ability_GreatAxe_Whirlwind",
                               {selection("Ability_GreatAxe_Whirlwind_Upgrade", {kUnendingWinds}, {}, {kArtifact}),
                                selection("Ability_GreatAxe_Whirlwind_Upgrade_Artifact", {}, {kArtifact})}),
                     action_of("Ability_Greataxe_Charge", "Ability_GreatAxe_Engage"),
                     action_of("Ability_Greataxe_Maelstorm", "Ability_GreatAxe_Maelstrom", {}, {shield})};
    axe.nodeUses = {node_use(kCollector, "row", "Ability_GreatAxe_JudgementsReach atk1"),
                    node_use(kFatalAttraction, "selection", "Test_Ability_Greataxe_Reap"),
                    node_use(kSwingAttack, "transition", "Ability_GreatAxe_Engage"),
                    node_use(kAddGrit, "grit", "Ability_GreatAxe_Executioner"),
                    node_use(kSpeed, "motion", "Test_Ability_Greataxe_Whirlwind")};
    return axe;
}

/** The hammer graph: the form and the times of the fragments of the war hammer of the game data. */
inline gamedata::WeaponActionSet hammer_graph() {
    using gamedata::AttackInput;
    const auto buffered = gamedata::TransitionKind::buffered;
    const auto instant = gamedata::TransitionKind::instant;
    std::vector<gamedata::AttackTransition> primary = {af::transition(buffered, 0.1875F, 0.566667F, AttackInput::virtualInput, "Attack_Start_Charge"),
                                                       af::transition(instant, 0.566667F, -1.0F, AttackInput::virtualInput, "Attack_Start_Charge")};
    for (gamedata::AttackTransition& window : ability_windows(0.0F, 0.1F, {{"Ability_WarHammer_MightyGavel", "Ability_WarHammer_MightyGavel"},
                                                                           {"Ability_WarHammer_BlockBreaker", "Ability_Warhammer_ArmorBreaker"},
                                                                           {"Ability_WarHammer_ClearOut", "Ability_WarHammer_ClearOut"}})) {
        primary.push_back(std::move(window));
    }
    gamedata::AttackFragment gavel = with_mark(
        af::fragment("Ability_WarHammer_MightyGavel", 1.818182F, {},
                     {strike_of("atk1", 0.6F, 0.73354F, {shaped("Warhammer_Damage_MightyGavel", {}, {}, 0)}, {capsule(1.3F, 0.35F, 0.0F)})}),
        1.366667F);
    gavel.casts = {gamedata::FragmentCast{0.638283F, "WarHammer_Spell_MightyGavel", {}, {}}};
    gamedata::WeaponActionSet hammer;
    hammer.tags = {"2H_Melee", "Warhammer"};
    hammer.fragments = {
        af::fragment("Attack_Start_Charge", 0.7F,
                     {af::transition(instant, 0.0F, 0.333333F, AttackInput::notHeld, "Attack_Primary"),
                      af::transition(instant, 0.333333F, -1.0F, AttackInput::released, "Attack_Primary")}),
        with_mark(af::fragment("Attack_Primary", 1.616667F, primary, {af::strike("atk1", 0.3F, 0.4F, {af::row("Test_Light")})}), 1.166667F),
        gavel,
        with_mark(af::fragment("Ability_WarHammer_BlockBreaker", 1.833636F, {},
                               {strike_of("atk1", 0.466667F, 0.566667F, {shaped("Warhammer_Damage_ArmorBreaker", {}, {}, 0)},
                                          {capsule(0.45F, 0.6F, 1.0F)})}),
                  1.233333F),
        with_mark(af::fragment("Ability_WarHammer_ClearOut", 2.636364F, {},
                               {strike_of("atk1", 0.733333F, 0.933333F, {shaped("Warhammer_Damage_ClearOut", {}, {}, 0)}, {capsule(0.7F, 0.6F, 0.8F)})}),
                  1.7F),
    };
    gamedata::AbilityAction::Cast fortify;
    fortify.spell = "WarHammer_Spell_ClearOut_Fortify";
    fortify.needs = {kFortifyAoe};
    fortify.marker = "WarHammer_SpellMarker_ClearOutFortify";
    hammer.abilities = {action_of("Ability_WarHammer_ArmorBreaker", "Ability_WarHammer_BlockBreaker"),
                        action_of("Ability_WarHammer_ClearOut", "Ability_WarHammer_ClearOut", {}, {fortify}),
                        action_of("Ability_WarHammer_MightyGavel", "Ability_WarHammer_MightyGavel")};
    return hammer;
}

inline gamedata::WeaponActionTables gawh_actions() {
    gamedata::WeaponActionTables tables;
    tables.weapons = {axe_graph(), hammer_graph()};
    return tables;
}

/** The combat tables: the tables of the shop (the axe and the hammer with the tags of the graphs) and the rows of the six abilities. */
inline gamedata::CombatData gawh_combat() {
    gamedata::CombatData data = shop_combat();
    data.weapons[0].tags = "2H_Melee,GreatAxe";
    data.weapons[1].tags = "2H_Melee,Warhammer";
    const auto add = [&](const char* id, std::uint32_t type, float factor, std::optional<gamedata::DamageImpact> impact = std::nullopt,
                         std::vector<std::string> effects = {}) {
        gamedata::DamageRow row{id, "Ability", type, factor, 0.0F};
        row.impact = impact;
        row.effects = std::move(effects);
        row.effectOnlyOnDamage = !row.effects.empty();
        data.damageRows.push_back(row);
    };
    add("GreatAxe_JudgementsReach", 1, 0.8F);
    // The pull rows: no damage (the _RP2 rows: 0.0001 and the root of Gravity), a pull to the attacker of 1, 2, or 3 m.
    add("GreatAxe_JudgementsReach_Pull", 1, 0.0F, motion::impact_of(-1.0F, 0.8F));
    add("GreatAxe_JudgementsReach_upgrade", 1, 0.0F, motion::impact_of(-2.0F, 0.8F));
    add("GreatAxe_JudgementsReach_upgrade2", 1, 0.0F, motion::impact_of(-3.0F, 0.8F));
    add("GreatAxe_JudgementsReach_Pull_RP2", 1, 0.0001F, motion::impact_of(-1.0F, 0.8F), {kGravityRoot});
    add("GreatAxe_JudgementsReach_upgrade_RP2", 1, 0.0001F, motion::impact_of(-2.0F, 0.8F), {kGravityRoot});
    add("GreatAxe_JudgementsReach_upgrade2_RP2", 1, 0.0001F, motion::impact_of(-3.0F, 0.8F), {kGravityRoot});
    add("GreatAxe_JudgementsReach_Close_RP2", 1, 0.0001F, std::nullopt, {kGravityRoot});
    add("GreatAxe_JudgementsReach_FA", 1, 1.15F);
    add("GreatAxe_Executioner", 1, 2.0F);
    add("GreatAxe_Whirlwind", 1, 0.5F);
    add("GreatAxe_Engage1", 1, 1.2F);
    add("GreatAxe_Engage_Swing1", 1, 1.4F);
    add("GreatAxe_Maelstrom", 1, 1.1F);
    add("Warhammer_Damage_MightyGavel", 2, 1.9F);
    add("Warhammer_Damage_ArmorBreaker", 2, 1.6F);
    add("Warhammer_Damage_ClearOut", 2, 1.0F, motion::impact_of(3.8F, 0.85F));
    data.rowEffects = true;
    return data;
}

/** The ability data: the shop data with the cooldowns, the statuses, the spells, the rules, and the status action of this part. */
inline gamedata::AbilityTables gawh_abilities(bool armorBreakerRule = true) {
    gamedata::AbilityTables tables = shop_abilities();
    tables.cooldowns.push_back({"Ability_Greataxe_Reap", 10.0F, 0.0F});
    tables.cooldowns.push_back({"Ability_Greataxe_Executioner", 12.0F, 0.0F});
    tables.cooldowns.push_back({"Ability_Greataxe_Whirlwind", 14.0F, 0.0F});
    tables.cooldowns.push_back({"Ability_WarHammer_MightyGavel", 9.0F, 0.0F});
    tables.cooldowns.push_back({"Ability_Warhammer_ArmorBreaker", 8.0F, 0.0F});
    tables.cooldowns.push_back({"Ability_WarHammer_ClearOut", 11.0F, 0.0F});
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    // The root of Gravity: a tracking status of 0.33 s with ApplicationCooldown 1 s, and its end status, the root of 0.4 s.
    rows.push_back(status(kGravityRoot, 0.33F, cells({{"ApplicationCooldown", 1.0F}}, {{"OnEndStatusEffect", kGravityHold}})));
    gamedata::StatusRow hold = status(kGravityHold, 0.4F, {}, true);
    hold.root = true;
    rows.push_back(hold);
    rows.push_back(status(kFortifyBuff, 4.0F, cells({{"PhysicalArmor", 0.2F}}, {{"EffectCategories", "Buff+ArmorFortify"}})));
    rows.push_back(status(kSprintStatus, -1.0F, cells({{"ABSSlash", 0.1F}}, {{"EffectCategories", "Buff+Fortify"}})));
    rows.push_back(status("Status_Upgrade_Greataxe_GravityWell_ArmorInWell", -1.0F, cells({{"PhysicalArmor", 0.1F}}, {{"EffectCategories", "Buff+ArmorFortify"}})));
    tables.statusEffects = numbered(std::move(rows));
    tables.spells.push_back({kShieldSpell, 2.0F, 1.0F, 0.0F, 0, 0.0F, 1125.0F,
                             cells({{"AttachToOwner", 1.0F}, {"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}, {"CastHeight", 1.0F}}, {{"TargetTypes", "Friend"}})});
    tables.spells.push_back({"WarHammer_Spell_ClearOut_Fortify", 6.0F, 5.0F, 6.0F, 0, 0.0F, 100.0F,
                             cells({{"AttachToOwner", 1.0F}, {"UseStatusEffectDuration", 1.0F}},
                                   {{"StatusEffects", kFortifyBuff}, {"TargetTypes", "Friend"}})});
    tables.spells.push_back({"WarHammer_Spell_MightyGavel", 0.0F, 0.0F, 0.0F, 0, 0.0F, 600.0F,
                             cells({{"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}}, {{"TargetTypes", "Enemy"}})});
    tables.abilityRows.push_back({"Ability_Greataxe_Executioner",
                                  cells({{"OnHit", 1.0F}, {"TargetHealthPercent", 50.0F}, {"BaseDamage", 0.5F}},
                                        {{"WeaponTag", "GreatAxe"}, {"DamageTableRow", "GreatAxe_Executioner"}, {"CooldownId", "Executioner"}})});
    if (armorBreakerRule) {
        tables.abilityRows.push_back({"Ability_Warhammer_ArmorBreaker",
                                      cells({{"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}, {"ArmorPenetration", 0.35F}},
                                            {{"WeaponTag", "Warhammer"}, {"InAction", "Ability_WarHammer_ArmorBreaker"},
                                             {"InSequence", "Ability_WarHammer_BlockBreaker"}})});
    }
    tables.abilityRows.push_back({kFortifyAoe, cells({{"OnHit", 1.0F}, {"OnBlockedHit", 1.0F}, {"InActionTime", 0.1F}, {"InSequenceTime", 0.1F}},
                                                     {{"WeaponTag", "Warhammer"}, {"RequiredAbilityId", "Ability_WarHammer_ClearOut"},
                                                      {"DamageTableRow", "Warhammer_Damage_ClearOut,Warhammer_Damage_ClearOut_Perk"},
                                                      {"InAction", "Ability_WarHammer_ClearOut"}, {"InSequence", "Ability_WarHammer_ClearOut"},
                                                      {"CastSpell", "WarHammer_Spell_ClearOut_Fortify"}})});
    // Guarded Sprint: the status action of the action list of the hammer (the node, a movement tag, not Dodging, the hammer).
    gamedata::ActionStatus sprint;
    sprint.list = "playeractions_ability_warhammer";
    sprint.action = "Warhammer_Passve_SprintDmgRed";
    for (const char* movement : {"ForwardNav", "LeftNav", "RightNav", "BackNav"}) {
        sprint.terms.push_back({gamedata::ActionStatus::Literal{false, kGuardedSprint, gamedata::progression_id(kGuardedSprint), false},
                                gamedata::ActionStatus::Literal{true, movement, gamedata::progression_id(movement), false},
                                gamedata::ActionStatus::Literal{true, "Dodging", gamedata::progression_id("Dodging"), true},
                                gamedata::ActionStatus::Literal{true, "Warhammer", gamedata::progression_id("Warhammer"), false}});
    }
    sprint.statuses = {kSprintStatus};
    tables.actionStatuses = {sprint};
    return tables;
}

/** The progression tables: the trees of the axe and the hammer with each node of the cases. */
inline gamedata::ProgressionData gawh_progression() {
    gamedata::ProgressionData data = shop_progression();
    std::uint32_t column = 10;
    for (const char* id : kAxeNodes) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = kGreatAxe;
        node.column = column++;
        node.active = std::string(id).rfind("Ability_", 0) == 0;
        data.nodes.push_back(node);
    }
    column = 10;
    for (const char* id : kHammerNodes) {
        gamedata::AbilityNode node;
        node.id = id;
        node.table = kWarHammer;
        node.column = column++;
        node.active = std::string(id).rfind("Ability_", 0) == 0;
        data.nodes.push_back(node);
    }
    return data;
}

/** A slice of a spell of this part (the spells of an action, a fragment, or a rule need their slice to be an entity). */
inline gamedata::SpellSlice plain_slice(const char* path, std::uint8_t mark) {
    gamedata::SpellSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x0000030000000100ULL + mark;
    slice.interestRadius = 50.0F;
    slice.spellSlot = 1;
    slice.slots = {{1, "Spell", 2912}, {2, "Ownership", 3217}, {3, "TemporaryAffiliation", 3563}};
    return slice;
}

/** The spell data: the slices of the ground of Mighty Gavel, of the fortify of Clear Out, and of the shield of Maelstrom. */
inline gamedata::SpellTables gawh_spells() {
    gamedata::SpellTables tables;
    tables.slices = {plain_slice("slices/test/gavel_ground.dynamicslice", 0x61), plain_slice("slices/test/fortify.dynamicslice", 0x62),
                     plain_slice("slices/test/shield.dynamicslice", 0x63)};
    tables.spells = {{"WarHammer_Spell_MightyGavel", 0}, {"WarHammer_Spell_ClearOut_Fortify", 1}, {kShieldSpell, 2}};
    return tables;
}

/** @return The count of the hit claims of A with a verdict (the attack track of its session). */
inline std::string claim_counts(ServerHost& host) {
    std::string text;
    const world::AttackTrack& track = host.session(1).attack_track();
    for (std::size_t verdict = 0; verdict < world::kHitVerdictCount; ++verdict) {
        if (const std::uint64_t count = track.count(static_cast<world::HitVerdict>(verdict)); count != 0) {
            text += std::string(text.empty() ? "" : ", ") + world::hit_verdict_text(static_cast<world::HitVerdict>(verdict)) + " "
                    + std::to_string(count);
        }
    }
    return text;
}

/** The contact of a weapon with the hit volume of a creature, as a client reports it (a point 0.5 m from the creature). */
inline InputContactItem touch(const world::Entity& creature) {
    InputContactItem contact;
    contact.groupActor = creature.actor.bytes;
    contact.group = creature.id;
    contact.actor = creature.actor.bytes;
    contact.entity = creature.id;
    contact.facet = creature.creature.body->target(creature.id, creature.creature.body->components[1]);
    contact.point = {creature.position.x + 0.5F, creature.position.y, creature.position.z + 1.0F};
    return contact;
}

/** The world of a case: A (and B with enter_b) with the axe or the hammer of the shop and the graphs of format 2. */
struct GawhShop : Shop {
    explicit GawhShop(bool armorBreakerRule = true, bool observe = false) : Shop(false, false, observe) {
        host.progression.set(gawh_progression());
        if (!host.combat.set(gawh_combat())) {
            throw std::runtime_error("gawh test: the combat tables are not valid: " + host.combat.error());
        }
        if (!host.abilities.set(gawh_abilities(armorBreakerRule))) {
            throw std::runtime_error("gawh test: the ability data is not valid: " + host.abilities.error());
        }
        if (!host.weaponActions.set(gawh_actions())) {
            throw std::runtime_error("gawh test: the weapon actions are not valid: " + host.weaponActions.error());
        }
        if (!host.spells.set(gawh_spells())) {
            throw std::runtime_error("gawh test: the spell data is not valid: " + host.spells.error());
        }
    }
    /** Puts an item of the shop into the main hand of A (or of B) and selects it. */
    void hold(const char* item, const char* character = kCharacterA) {
        world::GiveOptions options;
        options.gearScore = 100;
        const world::ItemResult given = host.world.inventory().give(character, item, 1, options);
        if (!given.ok() || given.slots.empty() || !host.world.inventory().equip(character, given.slots.front(), world::paperdoll::kMainHand1).ok()) {
            throw std::runtime_error(std::string("gawh test: the item ") + item);
        }
        world::Entity* own = host.world.find_character(character);
        own->combat.selectedWeapon = world::paperdoll::kMainHand1;
        host.world.gear().refresh(*own);
    }
    /** Gives a character nodes of a tree (no point rule: a fixture). */
    void learn(std::vector<const char*> nodes, const char* character = kCharacterA) {
        world::Entity* own = host.world.find_character(character);
        for (const char* node : nodes) {
            const gamedata::AbilityNode* row = host.progression.node(node);
            if (row == nullptr) {
                throw std::runtime_error(std::string("gawh test: no node ") + node);
            }
            own->skills.pools[row->table][gamedata::progression_id(node)].level = 1;
        }
    }
    /** Puts abilities on the bar of a tree of a character (and learns them). */
    void bar(std::uint32_t table, std::array<const char*, 3> abilities, const char* character = kCharacterA) {
        world::Entity* own = host.world.find_character(character);
        std::array<std::uint32_t, 3> ids{};
        for (std::size_t place = 0; place < 3; ++place) {
            if (abilities[place] != nullptr) {
                learn({abilities[place]}, character);
                ids[place] = gamedata::progression_id(abilities[place]);
            }
        }
        own->skills.bars[table] = ids;
    }
    world::Abilities& abilities() {
        return host.world.abilities();
    }
    std::uint64_t accepted() {
        return abilities().counts().presses[static_cast<std::size_t>(world::CastVerdict::accepted)];
    }
    std::uint64_t refused(world::CastVerdict verdict) {
        return abilities().counts().presses[static_cast<std::size_t>(verdict)];
    }
    /** A key press of the bar of A: the press with the virtual bit, the hold, the release. @return The input number of the press. */
    std::uint16_t press(unsigned place) {
        walker.bits({kAbilityBits[place], kVirtualBit});
        const std::uint16_t pressed = walker.input;
        walker.bits({kHoldBit});
        walker.bits({kReleaseBit});
        return pressed;
    }
    /** One step of A with bits and contacts. @return Its input number. */
    std::uint16_t step(std::vector<unsigned> bits = {}, std::vector<InputContactItem> contacts = {}) {
        InputEntry made = walker.entry(std::move(bits));
        made.contacts = std::move(contacts);
        walker.send(made);
        return walker.input;
    }
    /** Idle steps of A until the next step is the input number `target`. */
    void until(std::uint16_t target) {
        while (static_cast<std::uint16_t>(target - walker.input) > 1 && static_cast<std::uint16_t>(target - walker.input) < 0x8000) {
            walker.idle(1);
        }
    }
    /** The contact of A with a creature at the input number `at` (idle steps before it). @return The hits of the abilities after it. */
    std::uint64_t contact_at(std::uint16_t at, const world::Entity& creature) {
        until(at);
        step({}, {touch(creature)});
        return abilities().counts().strikeHits;
    }
    world::Entity* creature(world::EntityId id) {
        return host.world.creatures().find(id);
    }
    /** @return The fragment names of the visits of A, oldest first. */
    std::vector<std::string> visits(const char* character = kCharacterA) {
        std::vector<std::string> names;
        const world::AttackSequences::State* found = host.world.combat().sequences().state_of(character);
        if (found != nullptr && found->actions != nullptr) {
            for (const world::AttackVisit& visit : found->visits) {
                names.push_back(found->actions->fragments[visit.fragment].name);
            }
        }
        return names;
    }
    const world::ActiveStatus* status_of(const char* id, const char* character = kCharacterA) {
        const world::Entity* own = host.world.find_character(character);
        for (const world::ActiveStatus& active : own->statuses) {
            if (active.status == id) {
                return &active;
            }
        }
        return nullptr;
    }
    bool creature_has(const world::Entity& creature, const char* id) {
        const auto now = host.clock.now();
        return std::any_of(creature.creature.effects.begin(), creature.creature.effects.end(),
                           [&](const world::CreatureEffect& effect) { return effect.status == id && now < effect.ends; });
    }
    /** The spell entities of the world. */
    std::vector<const world::Entity*> spells() {
        std::vector<const world::Entity*> found;
        for (const auto& [id, entity] : host.world.entities()) {
            if (entity.kind == world::EntityKind::spell) {
                found.push_back(&entity);
            }
        }
        return found;
    }
};

} // namespace soulwarden::backend::tests::gawh
