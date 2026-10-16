#include "world/abilities.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/data_rules.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** One ability of this backend: its id text and its rule. */
struct AbilityRule {
    const char* id;
    AbilityKind kind;
    const char* name;
};

constexpr AbilityRule kRules[] = {
    {"Ability_Greataxe_Charge", AbilityKind::charge, "Charge"},
    {"Ability_Greataxe_Maelstorm", AbilityKind::maelstrom, "Maelstrom"},
    {"Ability_Greataxe_GravityWell", AbilityKind::gravityWell, "Gravity Well"},
    {"Ability_WarHammer_WreckingBall", AbilityKind::wreckingBall, "Wrecking Ball"},
    {"Ability_WarHammer_Shockwave", AbilityKind::shockwave, "Shockwave"},
    {"Ability_WarHammer_PathOfDestiny", AbilityKind::pathOfDestiny, "Path of Destiny"},
    {"Ability_Rapier_Riposte", AbilityKind::riposte, "Riposte"},
    {"IceMagic_IcePylon", AbilityKind::icePylon, "Ice Pylon"},
    {"Ability_Musket_Traps", AbilityKind::traps, "Traps"},
    {"Ability_Lifestaff_Beacon", AbilityKind::beacon, "Beacon"},
    {"Ability_Lifestaff_DivineEmbrace", AbilityKind::divineEmbrace, "Divine Embrace"},
    {"Ability_Lifestaff_SacredGround", AbilityKind::sacredGround, "Sacred Ground"},
    {"Ability_Greataxe_Reap", AbilityKind::reap, "Reap"},
    {"Ability_Greataxe_Executioner", AbilityKind::executioner, "Execute"},
    {"Ability_Greataxe_Whirlwind", AbilityKind::whirlwind, "Whirlwind"},
    {"Ability_WarHammer_MightyGavel", AbilityKind::mightyGavel, "Mighty Gavel"},
    {"Ability_Warhammer_ArmorBreaker", AbilityKind::armorBreaker, "Armor Breaker"},
    {"Ability_WarHammer_ClearOut", AbilityKind::clearOut, "Clear Out"},
};

/**
 * @return True for an ability whose hits are only the strikes of its fragment in the weapon actions (format 2, stage 3.3b-M2): with no
 *         such data its press is refused (no rule).
 */
bool needs_fragments(AbilityKind kind) {
    switch (kind) {
    case AbilityKind::reap:
    case AbilityKind::executioner:
    case AbilityKind::whirlwind:
    case AbilityKind::mightyGavel:
    case AbilityKind::armorBreaker:
    case AbilityKind::clearOut:
        return true;
    default:
        return false;
    }
}

/**
 * @return True for an ability whose hits are the strikes of its fragment when the weapon actions have its action (stage 3.3b-M2): the abilities
 *         of the great axe and the war hammer (the build of the user). The abilities of the other weapons (Riposte of the rapier) keep their
 *         plans until their part checks their fragments live (a decision of an agent: the rapier is not in this part).
 */
bool sequence_kind(AbilityKind kind) {
    switch (kind) {
    case AbilityKind::charge:
    case AbilityKind::maelstrom:
    case AbilityKind::gravityWell:
    case AbilityKind::wreckingBall:
    case AbilityKind::shockwave:
    case AbilityKind::pathOfDestiny:
        return true;
    default:
        return needs_fragments(kind);
    }
}

/** @return True when each text of a list is a node that the character has (the conditions of the action data name no tree). */
bool all_learned(const SkillSet& skills, const std::vector<std::string>& nodes);
bool none_learned(const SkillSet& skills, const std::vector<std::string>& nodes);
bool has_learned(const SkillSet& skills, std::uint32_t id);

/** The id text of the ranged attack of the projectile of Gravity Well (the value "attack" of its shot is its CRC-32). */
/** A cast leaves the table this long after its start when no phase waits (the longest contact window is 3 s). */
constexpr std::chrono::milliseconds kCastKeep{5000};
/** Gravity Well: the server throws the projectile itself this long after the press when the client sent no shot. */
constexpr std::chrono::milliseconds kThrowWait{300};
/** The shot of a client starts not farther than this from the position of its character (meters). */
constexpr float kShotOrigin = 5.0F;

const AbilityRule* rule_of(std::uint32_t ability) {
    for (const AbilityRule& rule : kRules) {
        if (gamedata::progression_id(rule.id) == ability) {
            return &rule;
        }
    }
    return nullptr;
}

/** @return True when the character has the node with this id text in the tree of a weapon. */
bool has_node(const SkillSet& skills, const char* table, const char* node) {
    const auto pool = skills.pools.find(gamedata::progression_id(table));
    if (pool == skills.pools.end()) {
        return false;
    }
    const auto found = pool->second.find(gamedata::progression_id(node));
    return found != pool->second.end() && found->second.level != 0;
}

/** @return True when the character has the node with this id text in any tree (the conditions of the action data name no tree). */
bool learned(const SkillSet& skills, std::string_view node) {
    const std::uint32_t id = gamedata::progression_id(node);
    for (const auto& [table, nodes] : skills.pools) {
        const auto found = nodes.find(id);
        if (found != nodes.end() && found->second.level != 0) {
            return true;
        }
    }
    return false;
}

bool has_learned(const SkillSet& skills, std::uint32_t id) {
    for (const auto& [table, nodes] : skills.pools) {
        const auto found = nodes.find(id);
        if (found != nodes.end() && found->second.level != 0) {
            return true;
        }
    }
    return false;
}

bool all_learned(const SkillSet& skills, const std::vector<std::string>& nodes) {
    return std::all_of(nodes.begin(), nodes.end(), [&](const std::string& node) { return learned(skills, node); });
}

bool none_learned(const SkillSet& skills, const std::vector<std::string>& nodes) {
    return std::none_of(nodes.begin(), nodes.end(), [&](const std::string& node) { return learned(skills, node); });
}

/**
 * The actions whose spell the plan takes from the spell selections of the action data (stage 3.3b-S5): the rule of an action of
 * the data. Each one has the 3.3a rule as its fallback for a data file with no selection (a file of an older tool).
 */
constexpr const char* kActionSelections[] = {"Ability_WarHammer_Shockwave", "Ability_WarHammer_PathOfDestiny", "GreatAxe_GravityWell",
                                             "Ability_Lifestaff_Beacon"};

/** The nodes that a plan of this backend reads (has_node): the rules of an action that the plan applies by hand. */
constexpr const char* kPlanNodes[] = {
    "Ult_Passive_WarHammer_SlowOnCC", "Upgrade_Greataxe_Maelstorm_Rng", "Passive_Greataxe_Mauler_PullDur", "Upgrade_Greataxe_Maelstorm_SpinAttack",
    "Upgrade_Greataxe_GravityWell_ArmorInWell", "Upgrade_Greataxe_GravityWell_DmgPerFoe", "Upgrade_WarHammer_Wreck_Pushback",
    "Upgrade_WarHammer_Shockwave_SizeIncrease", "Upgrade_WarHammer_Shockwave_Weaken", "Upgrade_WarHammer_PathOfDestiny_Stagger",
    "Upgrade_WarHammer_PathOfDestiny_CDRPerHit", "Rapier_Riposte_Mod3", "Upgrade_Musket_TrapRend", "Upgrade_Lifestaff_Protection_AoeInc",
    "Upgrade_Lifestaff_Protection_DurOnLow", "Upgrade_Lifestaff_Protection_AddHaste", "Upgrade_Lifestaff_DivineEmbrace_ReduceCost",
    "Upgrade_Lifestaff_SacredGround_Dmg", "Upgrade_Lifestaff_SacredGround_DmgOnEnd"};

/**
 * @return The spell that an action of the data casts for the nodes of a character (stage 3.3b-S5): the first selection whose
 *         nodes the character has and whose other nodes it does not have. No value: the data has no selection for the action.
 */
std::optional<std::string> selected_spell(const gamedata::AbilityData* data, std::string_view action, const SkillSet& skills) {
    if (data == nullptr) {
        return std::nullopt;
    }
    for (const gamedata::ActionSpell* selection : data->action_spells(action)) {
        const bool needs = std::all_of(selection->needs.begin(), selection->needs.end(), [&](const std::string& node) { return learned(skills, node); });
        const bool excludes = std::none_of(selection->excludes.begin(), selection->excludes.end(), [&](const std::string& node) { return learned(skills, node); });
        if (needs && excludes && data->spell(selection->spell) != nullptr) {
            return selection->spell;
        }
    }
    return std::nullopt;
}

/** @return True when the data has the rule row of a node with this column: the evaluator of the passive rules applies it (stage 3.3b-S5). */
bool data_rule(const gamedata::AbilityData* data, std::string_view row, const char* column) {
    const gamedata::AbilityRow* found = data != nullptr ? data->ability_row(row) : nullptr;
    return found != nullptr && found->cells.has(column);
}

constexpr const char* kGreatAxe = "GreatAxeAbilityTable";
constexpr const char* kWarHammer = "WarHammerAbilityTable";
constexpr const char* kRapier = "RapierAbilityTable";
constexpr const char* kMusket = "MusketAbilityTable";
constexpr const char* kLifeMagic = "LifeMagicAbilityTable";

std::chrono::milliseconds milliseconds(float seconds) {
    return std::chrono::milliseconds(static_cast<long long>(std::lround(seconds * 1000.0F)));
}

/** @return The direction of a yaw of the camera (radians from north, positive to the left) in the plane. */
std::array<float, 2> direction_of(float yaw) {
    return {-std::sin(yaw), std::cos(yaw)};
}

float distance3(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::hypot(a.x - b.x, a.y - b.y, a.z - b.z);
}

} // namespace

const char* ability_name(AbilityKind kind) {
    for (const AbilityRule& rule : kRules) {
        if (rule.kind == kind) {
            return rule.name;
        }
    }
    return "ability";
}

const char* cast_verdict_text(CastVerdict verdict) {
    switch (verdict) {
    case CastVerdict::accepted:
        return "accepted";
    case CastVerdict::noEntity:
        return "no entity";
    case CastVerdict::noData:
        return "no ability data or no combat tables";
    case CastVerdict::dead:
        return "the character is dead";
    case CastVerdict::locked:
        return "the client shows the loading screen of a travel";
    case CastVerdict::noWeapon:
        return "no weapon with an ability table in the hand";
    case CastVerdict::emptyPlace:
        return "the place of the bar is empty";
    case CastVerdict::notLearned:
        return "the character does not have the node of the ability";
    case CastVerdict::noRule:
        return "no rule or no data for the ability";
    case CastVerdict::cooldown:
        return "the cooldown runs";
    case CastVerdict::casting:
        return "a different cast plays";
    case CastVerdict::noMana:
        return "the character has less mana than the cost";
    case CastVerdict::aimChanged:
        return "the place of the bar has a different ability than the aim";
    }
    return "unknown";
}

Abilities::Abilities(World& world, const gamedata::AbilityData* data, const gamedata::CombatTables* combat)
    : m_world(world), m_data(data), m_combat(combat) {}

bool Abilities::action_selected(std::string_view action) {
    return std::any_of(std::begin(kActionSelections), std::end(kActionSelections), [&](const char* name) { return data_rules::same_text(name, action); });
}

bool Abilities::plan_node(std::string_view node) {
    return std::any_of(std::begin(kPlanNodes), std::end(kPlanNodes), [&](const char* name) { return data_rules::same_text(name, node); });
}

bool Abilities::has_plan(std::string_view ability) {
    return rule_of(gamedata::progression_id(ability)) != nullptr;
}

std::optional<CastPlan> Abilities::plan(std::uint32_t ability, const SkillSet& skills, const gamedata::AbilityData* data) {
    const AbilityRule* rule = rule_of(ability);
    if (rule == nullptr) {
        return std::nullopt;
    }
    CastPlan plan;
    plan.kind = rule->kind;
    const auto contact = [](std::string row, unsigned first, unsigned last) {
        PhasePlan phase;
        phase.kind = PhaseKind::contact;
        phase.row = std::move(row);
        phase.firstStep = first;
        phase.lastStep = last;
        return phase;
    };
    // Aftershock (Ult_Passive_WarHammer_SlowOnCC): each hit of an ability of the hammer slows the target. From stage 3.3b-S5 the
    // evaluator of the passive rules applies its row of the data (OnHit, AttackType Ability, OtherApplyStatusEffect); the plan
    // gives the slow only with an ability file that has no such row.
    const bool aftershock = has_node(skills, kWarHammer, "Ult_Passive_WarHammer_SlowOnCC")
                            && !data_rule(data, "Ult_Passive_WarHammer_SlowOnCC", "OtherApplyStatusEffect");
    const EffectPlan slowOnHit{"WarHammer_Ult_SlowOnCC", {}, 0, {}};
    switch (rule->kind) {
    case AbilityKind::charge: {
        // One contact of the run (live check L3: 14 input steps after the press for a creature 6 m away).
        plan.length = std::chrono::milliseconds(500);
        plan.phases.push_back(contact("GreatAxe_Engage1", 3, 90));
        // The dash (stage 3.3b-S4, live checks L7-C and L7-D): the client moves the caster itself; the envelope is the
        // measured travel and step times the largest MotionMultiplier of the action.
        CasterMotion motion;
        motion.distance = ability_rules::kChargeTravel * ability_rules::kChargeMultiplier;
        motion.peak = ability_rules::kChargePeak * ability_rules::kChargeMultiplier;
        motion.length = ability_rules::kChargeLength;
        motion.dash = ability_rules::kChargeDash;
        motion.dashTime = ability_rules::kChargeDashTime;
        motion.actions = {MotionAction{std::chrono::milliseconds(0), ability_rules::kChargeState, ability_rules::kChargeDashSequence},
                          MotionAction{ability_rules::kChargeDashTime, ability_rules::kChargeState, ability_rules::kChargeSwingSequence}};
        plan.motion = motion;
        break;
    }
    case AbilityKind::maelstrom: {
        // Two spins with No Reprieve (live check L3: contacts 14 and 26 steps after the press).
        // Storm's Reach and Gravity select the rows of the reach and of the root (RP2).
        const bool reach = has_node(skills, kGreatAxe, "Upgrade_Greataxe_Maelstorm_Rng");
        const bool gravity = has_node(skills, kGreatAxe, "Passive_Greataxe_Mauler_PullDur");
        const bool second = has_node(skills, kGreatAxe, "Upgrade_Greataxe_Maelstorm_SpinAttack");
        plan.length = std::chrono::milliseconds(900);
        std::string row = std::string("GreatAxe_Maelstrom") + (reach ? "_Reach" : "") + (gravity ? "_RP2" : "");
        PhasePlan first = contact(row, 3, second ? 20 : 45);
        if (gravity) {
            // Gravity: a root of 0.4 s after a hold of 0.33 s (Status_Passive_GreatAxe_Gravity_Root, then _Hold).
            first.effects.push_back(EffectPlan{"Status_Passive_GreatAxe_Gravity_Root_Hold", std::chrono::milliseconds(330), 0, {}});
        }
        plan.phases.push_back(first);
        if (second) {
            PhasePlan spin = contact(std::string("GreatAxe_Maelstrom_NoReprieve") + (gravity ? "_RP2" : ""), 21, 45);
            spin.effects = first.effects;
            plan.phases.push_back(spin);
        }
        break;
    }
    case AbilityKind::gravityWell: {
        // The swing of the axe (one contact), the projectile, the vortex (a root and a slow at its
        // start), and the burst 3 s after the start (live check L7: one contact and one shot).
        const bool upgrade = has_node(skills, kGreatAxe, "Upgrade_Greataxe_GravityWell_ArmorInWell");
        std::string vortexSpell = upgrade ? "Spell_GreatAxe_GravityWell_Vortex_Upgrade" : "Spell_GreatAxe_GravityWell_Vortex";
        // The rule of the action (stage 3.3b-S5): the action GreatAxe_GravityWell casts the projectile of the data for the nodes
        // (_Upgrade with Unyielding), and its SecondarySpellID is the vortex.
        if (const std::optional<std::string> projectile = selected_spell(data, "GreatAxe_GravityWell", skills)) {
            if (const std::string_view secondary = data->spell(*projectile)->cells.text("SecondarySpellID");
                !secondary.empty() && data->spell(secondary) != nullptr) {
                vortexSpell = std::string(data->spell(secondary)->id);
            }
        }
        plan.length = std::chrono::milliseconds(600);
        plan.phases.push_back(contact("GreatAxe_GravityWell", 3, 30));
        PhasePlan vortex;
        vortex.kind = PhaseKind::vortex;
        vortex.spell = vortexSpell;
        // The pull of the vortex (stage 3.3b-S4): the child entity VortexPull of the slice hits each creature of its sphere
        // (4 m, checkObstructions) at the start with the row GreatAxe_GravityWell_Vortex (Y -7, no decay rate, no damage).
        vortex.pull = "GreatAxe_GravityWell_Vortex";
        vortex.effects = {EffectPlan{"GreatAxe_Vortex_Root", {}, 0, {}}, EffectPlan{"GreatAxe_Vortex", {}, 0, {}}};
        plan.phases.push_back(vortex);
        PhasePlan burst;
        burst.kind = PhaseKind::burst;
        burst.spell = vortexSpell;
        burst.row = "GreatAxe_GravityWell_Burst";
        burst.at = std::chrono::milliseconds(3000);
        // Crowded Well: the evaluator of the passive rules applies its row of the data (BaseDamage for each creature in the vortex,
        // MaxNumAroundMe; stage 3.3b-S5); the factor of the plan only with an ability file that has no such row.
        burst.crowded = has_node(skills, kGreatAxe, "Upgrade_Greataxe_GravityWell_DmgPerFoe")
                        && !data_rule(data, "Upgrade_Greataxe_GravityWell_DmgPerFoe", "NumAroundMe");
        plan.phases.push_back(burst);
        break;
    }
    case AbilityKind::wreckingBall: {
        // One contact (live check L3: 24 steps after the press), a knockdown (PowerLevel 5 of the row).
        const bool push = has_node(skills, kWarHammer, "Upgrade_WarHammer_Wreck_Pushback");
        plan.length = std::chrono::milliseconds(800);
        PhasePlan phase = contact(push ? "Warhammer_Damage_WreckingBall_Push" : "Warhammer_Damage_WreckingBall", 3, 45);
        phase.effects.push_back(EffectPlan{"", {}, ability_rules::kKnockdownPower, ability_rules::kKnockdownTime});
        if (aftershock) {
            phase.effects.push_back(slowOnHit);
        }
        plan.phases.push_back(phase);
        break;
    }
    case AbilityKind::shockwave: {
        // The creatures around the caster at the slam (live check L7: about 1 s after the press);
        // Meteoric Crater selects the larger spell and its row, Frailty adds the weaken.
        const bool crater = has_node(skills, kWarHammer, "Upgrade_WarHammer_Shockwave_SizeIncrease");
        plan.length = std::chrono::milliseconds(800);
        PhasePlan phase;
        phase.kind = PhaseKind::around;
        phase.at = std::chrono::milliseconds(800);
        phase.spell = crater ? "WarHammer_Spell_Shockwave_L2" : "WarHammer_Spell_Shockwave";
        phase.row = crater ? "Warhammer_Damage_Shockwave_L2" : "Warhammer_Damage_Shockwave";
        // The rule of the action (stage 3.3b-S5): the spell that the action of the data casts for the nodes, and its damage row.
        if (const std::optional<std::string> spell = selected_spell(data, "Ability_WarHammer_Shockwave", skills)) {
            phase.spell = *spell;
            if (const std::string_view row = data->spell(*spell)->cells.text("DamageTableRow"); !row.empty()) {
                phase.row = std::string(row);
            }
        }
        phase.effects.push_back(EffectPlan{"WarHammer_Shockwave_Stun", {}, 0, {}});
        // Frailty: the evaluator applies its row of the data (OnHit, InAction, OtherApplyStatusEffect; stage 3.3b-S5).
        if (has_node(skills, kWarHammer, "Upgrade_WarHammer_Shockwave_Weaken")
            && !data_rule(data, "Upgrade_WarHammer_Shockwave_Weaken", "OtherApplyStatusEffect")) {
            phase.effects.push_back(EffectPlan{"WarHammer_Shockwave_Weaken", {}, 0, {}});
        }
        if (aftershock) {
            phase.effects.push_back(slowOnHit);
        }
        plan.phases.push_back(phase);
        break;
    }
    case AbilityKind::pathOfDestiny: {
        // The circles of a path in front of the caster from the cast marker (1 s); Seismic Waves
        // selects L2 (a stagger), with Stimulated Reduction L3 and a shorter cooldown for each hit.
        const bool seismic = has_node(skills, kWarHammer, "Upgrade_WarHammer_PathOfDestiny_Stagger");
        const bool reduction = has_node(skills, kWarHammer, "Upgrade_WarHammer_PathOfDestiny_CDRPerHit");
        const char* level = seismic ? (reduction ? "_L3" : "_L2") : "";
        plan.length = std::chrono::milliseconds(1000);
        PhasePlan phase;
        phase.kind = PhaseKind::path;
        phase.at = std::chrono::milliseconds(1000);
        phase.spell = std::string("WarHammer_Spell_PathOfDestiny") + level;
        phase.row = std::string("Warhammer_Damage_PathOfDestiny") + level;
        // The rule of the action (stage 3.3b-S5): the spell of the data for the nodes (L2 with Seismic Waves, L3 with Stimulated
        // Reduction), and its damage row.
        if (const std::optional<std::string> spell = selected_spell(data, "Ability_WarHammer_PathOfDestiny", skills)) {
            phase.spell = *spell;
            if (const std::string_view row = data->spell(*spell)->cells.text("DamageTableRow"); !row.empty()) {
                phase.row = std::string(row);
            }
        }
        // Stimulated Reduction: the evaluator applies its row of the data (TrackHitCount, AbilitySpecific; stage 3.3b-S5).
        phase.cooldownPerHit = reduction && !data_rule(data, "Upgrade_WarHammer_PathOfDestiny_CDRPerHit", "CooldownTimer");
        if (seismic) {
            phase.effects.push_back(EffectPlan{"", {}, ability_rules::kStaggerPower, ability_rules::kStaggerTime});
        }
        if (aftershock) {
            phase.effects.push_back(slowOnHit);
        }
        plan.phases.push_back(phase);
        break;
    }
    case AbilityKind::riposte: {
        // The counter of the rapier (stage 3.3b-S4; game data: playeractions_ability_rapier.actionlist, the attachment ADB).
        // The loop puts Rapier_Riposte_Timer (1 s, its absorption takes the countered hit) on the caster; a damaging hit in
        // it starts Ability_Rapier_Riposte_Out (ServerStarted), whose capsule hits at 0.433 s with 1H_Rapier_Riposte_Stun
        // (its StatusEffect Rapier_Riposte_Stun, 2 s), or with the _Enhanced row and stun (2.5 s) when the node
        // Rapier_Riposte_Mod3 is learned (the conditions Rapier_Mod3 and Rapier_Mod3_Not of the two clips).
        const bool enhanced = has_node(skills, kRapier, "Rapier_Riposte_Mod3");
        plan.length = std::chrono::milliseconds(1000);
        plan.window = "Rapier_Riposte_Timer";
        PhasePlan out;
        out.kind = PhaseKind::counter;
        out.at = std::chrono::milliseconds(433);
        out.row = enhanced ? "1H_Rapier_Riposte_Stun_Enhanced" : "1H_Rapier_Riposte_Stun";
        out.effects.push_back(EffectPlan{enhanced ? "Rapier_Riposte_Stun_Enhanced" : "Rapier_Riposte_Stun", {}, 0, {}});
        plan.phases.push_back(out);
        break;
    }
    case AbilityKind::icePylon: {
        // Ice Pylon (stage 3.3b-S2b; research abilities-magic.md Q4, the game data): the cast makes the prefab of its spell
        // IceMagic_Spell_Pylon at 0.6 s of the cast fragment (player_magicgauntlet_ice.adb); the action
        // IceMagic_IcePylon_Cooldown pays the cost of the spell IceMagic_Spell_Pylon_UI (ManaCost IceMagic_IcePylon: 15) at the
        // spawn and holds the cooldown while the Pylon lives. The other Pylon spells of the nodes (_Regen, _Refresh, _Ultimate)
        // are the part of the ice gauntlet.
        plan.length = std::chrono::milliseconds(600);
        SpellPlan spell;
        spell.role = SpellRole::summon;
        spell.spell = "IceMagic_Spell_Pylon";
        spell.at = std::chrono::milliseconds(600);
        spell.payment = "IceMagic_IcePylon";
        plan.spell = spell;
        break;
    }
    case AbilityKind::traps: {
        // Traps of the musket (stage 3.3b-S2b; research abilities-ranged.md Q4.10, the game data): Musket_Spell_Trap tosses
        // Musket_Trap_Toss (the projectile slice trap_bear_projectile), whose hit makes the trap (Musket_Trap_Set, the slice
        // Trap_Bear); with Trapped Damage (Upgrade_Musket_TrapRend) the _DMG spells. The offense of the trap has the row
        // Musket_Trap (Musket_Trap_DMG) with its StatusEffect Musket_Trap_Root (and Musket_Trap_Rend). The shot item of the client
        // comes 8 input steps after the press (live check of stage 3.3b-S2b): the server throws for a character with no client.
        const bool damage = has_node(skills, kMusket, "Upgrade_Musket_TrapRend");
        plan.length = std::chrono::milliseconds(600);
        SpellPlan spell;
        spell.role = SpellRole::trap;
        spell.projectile = damage ? "Musket_Trap_Toss_DMG" : "Musket_Trap_Toss";
        spell.spell = damage ? "Musket_Trap_Set_DMG" : "Musket_Trap_Set";
        spell.at = std::chrono::milliseconds(267);
        spell.effects = damage ? std::vector<std::string>{"Musket_Trap_Root", "Musket_Trap_Rend"} : std::vector<std::string>{"Musket_Trap_Root"};
        plan.spell = spell;
        break;
    }
    case AbilityKind::beacon: {
        // Beacon of the life staff (stage 3.3b-S2b; research abilities-magic.md Q4, the game data): the projectile
        // Spell_Ability_Lifestaff_Beacon_Projectile (TargetTypes All, the row LifeStaff_Beacon) makes its SecondarySpellID, the
        // area Spell_Ability_Lifestaff_Beacon_AOE, on the entity that it hits (AttachToOwner, StickWhenAttached). The nodes of the
        // chain Infused Light, Radiance's Blessing, Speed of Light select the _Upgrade, _Upgrade2, _Upgrade3 spells (a decision of
        // an agent from the spell rows: radius 4 m, then 15 s, then the haste). The shot item of the client comes 4 input steps
        // after the press (live check of stage 3.3b-S2b).
        unsigned upgrades = 0;
        for (const char* node : {"Upgrade_Lifestaff_Protection_AoeInc", "Upgrade_Lifestaff_Protection_DurOnLow", "Upgrade_Lifestaff_Protection_AddHaste"}) {
            if (!has_node(skills, kLifeMagic, node)) {
                break;
            }
            ++upgrades;
        }
        static constexpr const char* kProjectiles[] = {"Spell_Ability_Lifestaff_Beacon_Projectile", "Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade",
                                                       "Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade2", "Spell_Ability_Lifestaff_Beacon_Projectile_Upgrade3"};
        plan.length = std::chrono::milliseconds(600);
        SpellPlan spell;
        spell.role = SpellRole::aura;
        spell.projectile = kProjectiles[upgrades];
        // The rule of the action (stage 3.3b-S5): the projectile that the action of the data casts for the nodes. It replaces the
        // chain of the nodes above (a decision of an agent of stage 3.3b-S2b) where the data has the selection.
        if (const std::optional<std::string> projectile = selected_spell(data, "Ability_Lifestaff_Beacon", skills)) {
            spell.projectile = *projectile;
        }
        spell.at = std::chrono::milliseconds(133);
        spell.characters = true;
        plan.spell = spell;
        break;
    }
    case AbilityKind::divineEmbrace: {
        // Divine Embrace of the life staff (stage 3.3b-S6; research abilities-magic.md Q4, the game data): the cast at 1.0 s of the target
        // or self cast fragment (player_magicstaff_life.adb) puts the status of its spell (Status_Ability_Lifestaff_DivineEmbrace:
        // HealthModifierDamageBased 1.2, UseHealScalingValue) on its target and pays its cost then (ManaCost LifeStaff_TouchOfLife: 25).
        // The node Privilege (Upgrade_Lifestaff_DivineEmbrace_ReduceCost) selects the spell _ReducedCost (LifeStaff_TouchOfLife_Mana_Upgrade:
        // 20; the conditions ConditionCheckDivineEmbraceReducedCost of the action list of the data). The camera target of the client is not
        // decoded: the server chooses the nearest friend in the range, else the caster (a decision of the orchestrator for this stage).
        plan.length = std::chrono::milliseconds(1000);
        plan.aimed = true;
        HealPlan heal;
        heal.spell = has_node(skills, kLifeMagic, "Upgrade_Lifestaff_DivineEmbrace_ReduceCost") ? "Spell_LifeStaff_DivineEmbrace_ReducedCost"
                                                                                               : "Spell_LifeStaff_DivineEmbrace";
        heal.at = std::chrono::milliseconds(1000);
        plan.heal = heal;
        break;
    }
    case AbilityKind::sacredGround: {
        // Sacred Ground of the life staff (stage 3.3b-S6; research abilities-magic.md Q4, the game data): the cast at 0.3333 s of its cast
        // fragment makes the area of its spell (radius 3 m, 12 s; the statuses Status_Ability_Lifestaff_SacredGround: HealthModifierDamageBased
        // 0.16 each 1 s, and the potency status of the Beacon) and pays its cost then (ManaCost LifeStaff_HealingWave: 15, one payment). The
        // nodes Holy Ground (Upgrade_Lifestaff_SacredGround_Dmg) and Anointed (Upgrade_Lifestaff_SacredGround_DmgOnEnd) select the spells
        // _Upgrade and _Upgrade2 (the conditions Condition_Lifestaff_SacredGround_* of the action list of the data). The target point of the
        // client is not decoded: the server makes the area on the ground at the caster with the self spells (_Self, a decision of an agent).
        const bool anointed = has_node(skills, kLifeMagic, "Upgrade_Lifestaff_SacredGround_DmgOnEnd");
        const bool holy = has_node(skills, kLifeMagic, "Upgrade_Lifestaff_SacredGround_Dmg");
        plan.length = std::chrono::milliseconds(600);
        plan.aimed = true;
        SpellPlan spell;
        spell.role = SpellRole::aura;
        spell.spell = anointed ? "Spell_LifeStaff_SacredGround_Self_Upgrade2" : holy ? "Spell_LifeStaff_SacredGround_Self_Upgrade"
                                                                                    : "Spell_LifeStaff_SacredGround_Self";
        spell.at = std::chrono::milliseconds(333);
        plan.spell = spell;
        break;
    }
    case AbilityKind::reap:
    case AbilityKind::executioner:
    case AbilityKind::whirlwind:
    case AbilityKind::mightyGavel:
    case AbilityKind::armorBreaker:
    case AbilityKind::clearOut:
        // Stage 3.3b-M2: the hits, the pull of Reap, the spins of Whirlwind and their exit, and the push of Clear Out are the strikes and
        // the transitions of the fragment of the action in the weapon actions (game data; Abilities::press_entity selects the fragment);
        // the rules of the nodes are the rows of the ability data (the evaluator of the passive rules); the push and the pull are the
        // impacts of the damage rows (forced movement). The fragment that plays decides when the client takes a press (the sequence).
        plan.length = std::chrono::milliseconds(0);
        break;
    }
    return plan;
}

std::string Abilities::ability_of(std::uint64_t cast) const {
    const auto found = m_casts.find(cast);
    return found == m_casts.end() ? std::string() : ability_text(found->second.ability);
}

std::string Abilities::ability_text(std::uint32_t ability) const {
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const gamedata::AbilityNode* node = tables != nullptr ? tables->node(ability) : nullptr;
    return node != nullptr ? node->id : std::string();
}

CastVerdict Abilities::refuse(CastVerdict verdict) {
    ++m_counts.presses[static_cast<std::size_t>(verdict)];
    return verdict;
}

CastVerdict Abilities::press(SessionId session, const AbilityPress& press) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return refuse(CastVerdict::noEntity);
    }
    return press_entity(*character, session, press);
}

CastVerdict Abilities::press_character(const std::string& characterId, const AbilityPress& press) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refuse(CastVerdict::noEntity);
    }
    return press_entity(*character, 0, press);
}

CastVerdict Abilities::press_entity(Entity& entity, SessionId session, const AbilityPress& press) {
    Entity* character = &entity;
    // A later press of the session, accepted or refused, ends the contact windows of the earlier cast (code review of
    // stage 3.3a, finding F2): a contact after it is not a hit of that cast. Before each refusal (code review of stage
    // 1.3b, finding F9: a press of an empty place of the bar closed no window).
    if (const auto known = m_characters.find(character->characterId); known != m_characters.end() && known->second.cast) {
        const auto earlier = m_casts.find(*known->second.cast);
        if (earlier != m_casts.end() && earlier->second.session == session && earlier->second.sequence == press.sequence
            && !earlier->second.closedAt) {
            earlier->second.closedAt = press.input;
        }
    }
    // Each press ends the aim of an earlier key (stage 3.3b-S6; the confirm of an aim ends it before its press).
    if (const auto known = m_characters.find(character->characterId); known != m_characters.end()) {
        known->second.aim.reset();
    }
    if (!has_data()) {
        return refuse(CastVerdict::noData);
    }
    if (character->life.dead) {
        return refuse(CastVerdict::dead);
    }
    if (press.locked) {
        return refuse(CastVerdict::locked);
    }
    // The ability of the key: the place of the bar of the weapon in the hand.
    const ItemInstance* item = Combat::weapon_in_hand(*character);
    const gamedata::WeaponRow* weapon = item != nullptr ? m_combat->weapon_of(item->itemId) : nullptr;
    if (weapon == nullptr || weapon->mastery.empty()) {
        return refuse(CastVerdict::noWeapon);
    }
    const std::uint32_t table = gamedata::progression_id(weapon->mastery);
    const auto bar = character->skills.bars.find(table);
    const std::uint32_t ability = bar != character->skills.bars.end() && press.place < bar->second.size() ? bar->second[press.place] : 0;
    if (ability == 0) {
        return refuse(CastVerdict::emptyPlace);
    }
    if (press.expected != 0 && ability != press.expected) {
        return refuse(CastVerdict::aimChanged);
    }
    const auto pool = character->skills.pools.find(table);
    const auto node = pool != character->skills.pools.end() ? pool->second.find(ability) : decltype(pool->second.end()){};
    if (pool == character->skills.pools.end() || node == pool->second.end() || node->second.level == 0) {
        return refuse(CastVerdict::notLearned);
    }
    const AbilityRule* rule = rule_of(ability);
    std::optional<CastPlan> planned = plan(ability, character->skills, m_data);
    const gamedata::CooldownRow* cooldown = rule != nullptr ? m_data->cooldown(rule->id) : nullptr;
    if (!planned || cooldown == nullptr) {
        return refuse(CastVerdict::noRule);
    }
    // Each row and each spell of the plan must be in the data: an ability with a part of its
    // data does nothing.
    for (const PhasePlan& phase : planned->phases) {
        if ((!phase.row.empty() && !m_combat->row_index(phase.row)) || (!phase.pull.empty() && !m_combat->row_index(phase.pull))
            || (!phase.spell.empty() && m_data->spell(phase.spell) == nullptr)) {
            log::write("world: ability: the data has no row %s or no spell %s of %s", phase.row.c_str(), phase.spell.c_str(), rule->name);
            return refuse(CastVerdict::noRule);
        }
        // Each status row of an effect of the plan, also of a delayed effect (code review of stage 3.3a, finding F9), and
        // the status of the window of a counter.
        for (const EffectPlan& effect : phase.effects) {
            if (!effect.status.empty() && m_data->status(effect.status) == nullptr) {
                log::write("world: ability: the data has no status effect %s of %s", effect.status.c_str(), rule->name);
                return refuse(CastVerdict::noRule);
            }
        }
    }
    if (!planned->window.empty() && m_data->status(planned->window) == nullptr) {
        log::write("world: ability: the data has no status effect %s of %s", planned->window.c_str(), rule->name);
        return refuse(CastVerdict::noRule);
    }
    // The direct heal of the cast (stage 3.3b-S6): its spell row, the status rows of the spell, and the cost id of the spell (ManaCost).
    if (planned->heal) {
        HealPlan& heal = *planned->heal;
        const gamedata::SpellRow* row = m_data->spell(heal.spell);
        heal.statuses.clear();
        bool statuses = row != nullptr;
        if (row != nullptr) {
            for (const std::string_view part : data_rules::parts_of(row->cells.text("StatusEffects"))) {
                statuses = statuses && m_data->status(part) != nullptr;
                heal.statuses.emplace_back(part);
            }
            heal.payment = std::string(row->cells.text("ManaCost"));
        }
        if (!statuses || heal.statuses.empty() || heal.payment.empty() || !m_data->mana_cost(heal.payment)) {
            log::write("world: ability: the data has no spell %s, a status of it, or its cost of %s", heal.spell.c_str(), rule->name);
            return refuse(CastVerdict::noRule);
        }
    }
    // The spell entity of the cast (stage 3.3b-S2b): its spell rows, the projectile spell in the projectile data, the status rows of
    // its effects. The area of the Beacon is the SecondarySpellID of its projectile (game data).
    if (planned->spell) {
        SpellPlan& spell = *planned->spell;
        // An area on the ground with no projectile (stage 3.3b-S6: Sacred Ground) pays the cost of its spell at its spawn (ManaCost).
        if (spell.role == SpellRole::aura && spell.projectile.empty() && spell.payment.empty()) {
            if (const gamedata::SpellRow* area = m_data->spell(spell.spell)) {
                spell.payment = std::string(area->cells.text("ManaCost"));
            }
        }
        const gamedata::SpellRow* projectileRow = spell.projectile.empty() ? nullptr : m_data->spell(spell.projectile);
        if (spell.spell.empty() && projectileRow != nullptr) {
            spell.spell = std::string(projectileRow->cells.text("SecondarySpellID"));
        }
        const gamedata::ProjectileData* projectiles = m_world.options().projectileData;
        const bool shotData = spell.projectile.empty()
                              || (projectileRow != nullptr && projectiles != nullptr && projectiles->loaded() && projectiles->ability_shot(spell.projectile));
        bool effects = true;
        for (const std::string& status : spell.effects) {
            effects = effects && m_data->status(status) != nullptr;
        }
        // The statuses of an aura: each row of the StatusEffects of its spell row (finding F9 of the code review of stage
        // 3.3b-S2b: a row that the data does not have refuses the press, not a Beacon with a part of its statuses).
        if (const gamedata::SpellRow* area = spell.role == SpellRole::aura && !spell.spell.empty() ? m_data->spell(spell.spell) : nullptr) {
            spell.statuses.clear();
            const std::string_view list = area->cells.text("StatusEffects");
            std::size_t start = 0;
            while (start < list.size()) {
                const std::size_t comma = list.find(',', start);
                const std::string_view part = list.substr(start, comma == std::string_view::npos ? std::string_view::npos : comma - start);
                if (!part.empty()) {
                    effects = effects && m_data->status(part) != nullptr;
                    spell.statuses.emplace_back(part);
                }
                start = comma == std::string_view::npos ? list.size() : comma + 1;
            }
        }
        if (spell.spell.empty() || m_data->spell(spell.spell) == nullptr || !shotData || !effects
            || (!spell.payment.empty() && !m_data->mana_cost(spell.payment))) {
            log::write("world: ability: the data has no spell %s, no projectile %s, a status, or the cost of %s", spell.spell.c_str(),
                       spell.projectile.c_str(), rule->name);
            return refuse(CastVerdict::noRule);
        }
    }
    // The fragment of the action of the ability in the weapon actions (format 2; stage 3.3b-M2): its strikes are the hits of the cast (the
    // sequence of the character plays it), and its transitions decide when the client takes the press. The action selects its fragment by
    // the nodes and the perks of the character (ChangeActionFragment: the last selection whose conditions hold, as the action runs them in
    // their order). The contact phases of the plan (stage 3.3a) go, and their effects are the effects of each strike.
    const gamedata::WeaponActionSet* graph = m_world.combat().actions_of(*weapon);
    const gamedata::AbilityAction* entry = graph != nullptr && sequence_kind(planned->kind) ? graph->ability(ability) : nullptr;
    if (entry != nullptr) {
        planned->sequence = true;
        planned->fragment = entry->fragment;
        const auto perk = m_world.combat().facts_of(*character).perk;
        const auto perks_hold = [&](const std::vector<std::string>& needs, const std::vector<std::string>& forbids) {
            return std::all_of(needs.begin(), needs.end(), [&](const std::string& name) { return perk(gamedata::progression_id(name)); })
                   && std::none_of(forbids.begin(), forbids.end(), [&](const std::string& name) { return perk(gamedata::progression_id(name)); });
        };
        for (const gamedata::AbilityAction::Selection& selection : entry->selections) {
            if (all_learned(character->skills, selection.needs) && none_learned(character->skills, selection.forbids)
                && perks_hold(selection.perks, selection.perkForbids)) {
                planned->fragment = selection.fragment;
            }
        }
        const bool rowEffects = m_combat->has_row_effects();
        for (auto phase = planned->phases.begin(); phase != planned->phases.end();) {
            if (phase->kind != PhaseKind::contact) {
                ++phase;
                continue;
            }
            for (const EffectPlan& effect : phase->effects) {
                // The root of Gravity is the status of the pull rows (_RP2) when the combat tables have the status of each row.
                if (rowEffects && effect.status == "Status_Passive_GreatAxe_Gravity_Root_Hold") {
                    continue;
                }
                const bool known = std::any_of(planned->strikeEffects.begin(), planned->strikeEffects.end(), [&](const EffectPlan& other) {
                    return other.status == effect.status && other.power == effect.power;
                });
                if (!known) {
                    planned->strikeEffects.push_back(effect);
                }
            }
            phase = planned->phases.erase(phase);
        }
        for (const gamedata::AbilityAction::Cast& cast : entry->casts) {
            if (cast.marker.empty() && all_learned(character->skills, cast.needs) && none_learned(character->skills, cast.forbids)
                && perks_hold(cast.perks, cast.perkForbids) && m_data->spell(cast.spell) != nullptr) {
                planned->actionSpells.push_back(cast.spell);
            }
        }
    } else if (needs_fragments(planned->kind)) {
        log::write("world: ability: %s needs its fragment in the weapon actions (format 2: tools/gamedata/weapon-actions.sh)", rule->name);
        return refuse(CastVerdict::noRule);
    }
    const WorldClock::time_point now = m_world.clock().now();
    CharacterState& state = m_characters[character->characterId];
    // The cooldown of the server, and the cooldown of the client: the client does not play a press in its own cooldown,
    // also when the cooldown of the server ended before (a reset command; stage 1.3b, I8, live check I8-05).
    const auto running = state.cooldowns.find(ability);
    if (running != state.cooldowns.end() && (running->second.indefinite || now < running->second.end || now < running->second.clientEnd)) {
        return refuse(CastVerdict::cooldown);
    }
    // A cast of the sequence (stage 3.3b-M2): the fragment that plays decides (the windows of the transitions to the ability, the mark
    // CanTransition, its end; in the next kAbilityBackSteps steps). Else the length of the plan of a different cast.
    const bool sequenced = planned->sequence && session != 0;
    if (sequenced) {
        const AbilityStart start = m_world.combat().sequences_of_world().start_ability(character->characterId, session, press.sequence, press.input,
                                                                                      Combat::weapon_key(*character), graph, ability, planned->fragment,
                                                                                      0, now, true);
        if (start.verdict == AbilityStart::Verdict::refused) {
            return refuse(CastVerdict::casting);
        }
    } else if (state.cast) {
        const auto playing = m_casts.find(*state.cast);
        if (playing != m_casts.end() && now < playing->second.ends) {
            return refuse(CastVerdict::casting);
        }
    }
    // The old casts of the world that have no phase that waits leave the table.
    for (auto old = m_casts.begin(); old != m_casts.end();) {
        old = old->second.pending == 0 && now - old->second.started >= kCastKeep ? m_casts.erase(old) : std::next(old);
    }
    // The cost of the cast (stage 1.3b, I14): a press with less mana than the cost makes no cast. The six abilities of
    // the build have no cost in the data (their spells name SpecialAttack, which the mana table does not have).
    const float cost = mana_cost(*character, *planned);
    if (cost > 0.0F && character->vitals.mana + 0.001F < cost) {
        return refuse(CastVerdict::noMana);
    }
    // A summon pays at its spawn (stage 3.3b-S2b: the Pylon), an area on the ground at its spawn and a direct heal with its heal (stage
    // 3.3b-S6): a press with less mana than that cost makes no cast.
    if (planned->spell && !planned->spell->payment.empty()) {
        const float payment = mana_cost_of(*character, planned->spell->payment);
        if (payment > 0.0F && character->vitals.mana + 0.001F < payment) {
            return refuse(CastVerdict::noMana);
        }
    }
    if (planned->heal) {
        const float payment = mana_cost_of(*character, planned->heal->payment);
        if (payment > 0.0F && character->vitals.mana + 0.001F < payment) {
            return refuse(CastVerdict::noMana);
        }
    }
    // An aimed ability of a client (stage 3.3b-S6: Divine Embrace, Sacred Ground): the key starts the aim of the client; the cast, its
    // cooldown, and its cost start at the confirm of the same session (Abilities::confirm), with the checks again then. The key with
    // the self cast input held casts at once on the caster (the actions *_SelfCast and *_SelfLoop of the data: the press is confirmed).
    if (planned->aimed && session != 0 && !press.confirmed) {
        Aim aim;
        aim.press = press;
        aim.session = session;
        aim.ability = ability;
        aim.started = now;
        state.aim = aim;
        ++m_counts.aims;
        log::write("world: ability: %s aims %s (place %u of the bar): the cast starts at the confirm", character->name.c_str(),
                   ability_name(planned->kind), press.place + 1);
        // The client leaves its basic attack for the action of the ability (stage 3.3b-M1).
        m_world.combat().end_sequence(character->characterId, SequenceEnd::ability);
        return CastVerdict::accepted;
    }
    Cast cast;
    cast.number = m_nextCast++;
    cast.self = press.self;
    cast.plan = std::move(*planned);
    cast.ability = ability;
    cast.table = table;
    cast.caster = character->id;
    cast.characterId = character->characterId;
    cast.session = session;
    cast.input = press.input;
    cast.sequence = press.sequence;
    cast.started = now;
    cast.ends = now + cast.plan.length;
    cast.place = character->position;
    cast.yaw = std::isfinite(press.yaw) ? press.yaw : 0.0F;
    cast.itemId = item->itemId;
    cast.slot = Combat::weapon_slot_in_hand(*character).value_or(0);
    cast.gearScore = item->gearScore;
    if (const std::optional<Conversion> conversion = m_world.gear().conversion(*item, cast.slot)) {
        cast.conversionType = conversion->type;
        cast.conversionPart = conversion->part;
    }
    // The cooldown of the data starts at the press, as the cooldown of the client (live check L1).
    AbilityCooldown timer;
    timer.start = now;
    timer.end = now + milliseconds(cooldown->time);
    timer.minEnd = now + milliseconds(cooldown->minTime);
    timer.clientEnd = timer.end;
    if (cast.plan.spell && cast.plan.spell->role == SpellRole::summon) {
        // The Pylon (stage 3.3b-S2b): the cooldown holds while the summon lives (the action IceMagic_IcePylon_Cooldown: an
        // indefinite cooldown), and the cooldown of the data starts at its end (end_held_cooldown). The client starts its own
        // cooldown at the message Pylon_Spawn of the server, which the backend does not send: no cooldown of the client.
        timer.indefinite = true;
        timer.clientEnd = now;
    }
    state.cooldowns[ability] = timer;
    state.cast = cast.number;
    const std::uint64_t number = cast.number;
    Cast& stored = m_casts.emplace(number, std::move(cast)).first->second;
    // The hits of the cast for the client (IfAbilityNumHits, stage 3.3b-M2): 0 at the press.
    count_hit(*character, stored.ability, true);
    // The fragment of the cast starts in the sequence (stage 3.3b-M2): at the press, or later in a buffered window of the fragment that
    // played. The timed phases of the plan come that much later.
    if (sequenced) {
        const AbilityStart start = m_world.combat().sequences_of_world().start_ability(character->characterId, session, press.sequence, press.input,
                                                                                      Combat::weapon_key(*character), graph, ability, stored.plan.fragment,
                                                                                      number, now);
        stored.plan.delay = std::chrono::milliseconds(static_cast<long long>(start.delaySteps) * 1000 / ability_rules::kStepsInSecond);
        if (start.verdict == AbilityStart::Verdict::later) {
            ++m_counts.delayedStarts;
        }
    }
    const std::chrono::milliseconds delay = stored.plan.delay;
    // The area phases at their times on the world clock.
    for (std::uint32_t index = 0; index < stored.plan.phases.size(); ++index) {
        const PhasePlan& phase = stored.plan.phases[index];
        if (phase.kind == PhaseKind::around) {
            ++stored.pending;
            m_world.after(phase.at + delay, [this, number, index](World&) { run_area(number, index, {}); });
        } else if (phase.kind == PhaseKind::path) {
            const gamedata::SpellRow* spell = m_data->spell(phase.spell);
            const std::array<float, 2> ahead = direction_of(stored.yaw);
            const std::uint32_t samples = std::max<std::uint32_t>(spell->count, 1);
            for (std::uint32_t sample = 0; sample < samples; ++sample) {
                // A circle of the path at each step of the cast distance, one after the other.
                const float along = spell->castDistance * static_cast<float>(sample + 1);
                const state::WorldPosition center{stored.place.x + ahead[0] * along, stored.place.y + ahead[1] * along, stored.place.z};
                ++stored.pending;
                m_world.after(phase.at + delay + milliseconds(spell->spawnRate / 1000.0F) * sample,
                              [this, number, index, center](World&) { run_area(number, index, center); });
            }
        }
    }
    if (stored.plan.kind == AbilityKind::gravityWell) {
        // The projectile: the shot of the client throws it. With no shot the server throws it
        // from the place of the caster in the direction of the press.
        ++stored.pending;
        m_world.after(ability_rules::kThrowTime + kThrowWait + delay, [this, number](World&) {
            const auto found = m_casts.find(number);
            if (found == m_casts.end()) {
                return;
            }
            Cast& late = found->second;
            --late.pending;
            if (!late.thrown) {
                const Entity* caster = m_world.find(late.caster);
                const std::array<float, 2> ahead = direction_of(late.yaw);
                state::WorldPosition origin = caster != nullptr ? caster->position : late.place;
                origin.z += 1.5F;
                throw_projectile(late, origin, {ahead[0], ahead[1], 0.0F});
            }
        });
    }
    if (stored.plan.spell) {
        // The spell entity of the cast (stage 3.3b-S2b): the spawn of a summon at its time; the projectile of a trap or a Beacon
        // from the shot item of the client, or from the server when no shot came (a character with no client).
        ++stored.pending;
        if (stored.plan.spell->role == SpellRole::summon || (stored.plan.spell->role == SpellRole::aura && stored.plan.spell->projectile.empty())) {
            // A summon, and an area on the ground at the caster (stage 3.3b-S6: Sacred Ground): the spawn at the time of the plan.
            m_world.after(stored.plan.spell->at + delay, [this, number](World&) { spawn_summon(number); });
        } else {
            m_world.after(stored.plan.spell->at + kThrowWait + delay, [this, number](World&) {
                const auto found = m_casts.find(number);
                if (found == m_casts.end()) {
                    return;
                }
                Cast& late = found->second;
                --late.pending;
                if (!late.thrown) {
                    const Entity* caster = m_world.find(late.caster);
                    const std::array<float, 2> ahead = direction_of(late.yaw);
                    state::WorldPosition origin = caster != nullptr ? caster->position : late.place;
                    origin.z += 1.5F;
                    throw_spell(late, origin, {ahead[0], ahead[1], 0.0F});
                }
            });
        }
    }
    if (stored.plan.heal) {
        // The direct heal at the time of the plan (stage 3.3b-S6): the target, the payment, and the statuses then.
        ++stored.pending;
        m_world.after(stored.plan.heal->at + delay, [this, number](World&) { cast_heal(number); });
    }
    // The spells of the action at its start (stage 3.3b-M2: the shield of Maelstrom with Absorb).
    for (const std::string& spell : stored.plan.actionSpells) {
        const EntityId casterId = character->id;
        m_world.after(delay, [this, number, casterId, spell](World&) {
            if (Entity* caster = m_world.find(casterId); caster != nullptr && !caster->life.dead) {
                cast_spell_at(*caster, number, spell);
            }
        });
    }
    publish(stored.characterId);
    if (cost > 0.0F) {
        change_mana(*character, -cost, ability_name(stored.plan.kind));
    }
    if (stored.plan.motion) {
        start_motion(stored, *character);
    }
    if (!stored.plan.window.empty()) {
        // The window of the counter: the status of the data on the caster, through the status system (stage 3.3b-S4).
        StatusApply how;
        how.source = StatusSource{StatusSourceKind::ability, ability_text(stored.ability), 0, character->id};
        m_world.statuses().apply(*character, stored.plan.window, how);
    }
    ++m_counts.presses[static_cast<std::size_t>(CastVerdict::accepted)];
    // The client leaves its basic attack for the action of the ability (stage 3.3b-M1); a cast of the sequence started its fragment.
    if (!sequenced) {
        m_world.combat().end_sequence(character->characterId, SequenceEnd::ability);
    }
    log::write("world: ability: %s casts %s (place %u of the bar): cooldown %.1f s, %zu phase(s)%s",
               character->name.c_str(),
               ability_name(stored.plan.kind),
               press.place + 1,
               static_cast<double>(cooldown->time),
               stored.plan.phases.size(),
               stored.plan.sequence ? (", the fragment " + graph->fragments[stored.plan.fragment].name
                                        + (delay.count() > 0 ? " in " + std::to_string(delay.count()) + " ms" : std::string()))
                                          .c_str()
                                    : "");
    return CastVerdict::accepted;
}

void Abilities::shot(SessionId session, const AbilityShot& shot) {
    const Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return;
    }
    const auto state = m_characters.find(character->characterId);
    if (state == m_characters.end() || !state->second.cast) {
        return;
    }
    const auto found = m_casts.find(*state->second.cast);
    if (found == m_casts.end() || found->second.thrown || found->second.session != session || !takes_shot(session, shot.attack)) {
        return;
    }
    ++m_counts.shots;
    // The shot is a claim of the client: an origin far from the character, or a direction that
    // is not a direction, is not used.
    Cast& cast = found->second;
    state::WorldPosition launch = character->position;
    launch.z += 1.5F;
    state::WorldPosition origin = shot.origin;
    // The origin must be near the character, with no wall between the launch place of the server and it (code review of
    // stage 3.3a, finding F3): a client cannot put the origin behind a wall.
    if (!valid_position(origin) || distance3(origin, character->position) > kShotOrigin || !m_world.navigation().sees(launch, origin)) {
        origin = launch;
    }
    std::array<float, 3> direction = shot.direction;
    // The shot of a spell (the trap, the Beacon) flies in each direction, also straight down (code review of stage 3.3b-S2b,
    // finding F10): its direction is a direction when its length in three dimensions is one. The basic projectile of a cast
    // keeps the rule of its horizontal part.
    const float length = cast.plan.spell ? std::hypot(direction[0], direction[1], direction[2]) : std::hypot(direction[0], direction[1]);
    if (!std::isfinite(length) || length < 0.01F) {
        const std::array<float, 2> ahead = direction_of(cast.yaw);
        direction = {ahead[0], ahead[1], 0.0F};
    }
    if (cast.plan.spell) {
        // The trap, the Beacon (stage 3.3b-S2b): the projectile of the spell flies on the server (the direction of the item, a
        // unit vector; the client reports no hit).
        const float size = std::hypot(direction[0], direction[1], direction[2]);
        throw_spell(cast, origin, {direction[0] / size, direction[1] / size, direction[2] / size});
        return;
    }
    throw_projectile(cast, origin, direction);
}

bool Abilities::takes_shot(SessionId session, std::uint32_t attack) const {
    const Entity* character = m_world.entity_of(session);
    const auto state = character != nullptr ? m_characters.find(character->characterId) : m_characters.end();
    if (state == m_characters.end() || !state->second.cast) {
        return false;
    }
    const auto found = m_casts.find(*state->second.cast);
    if (found == m_casts.end() || found->second.thrown || found->second.session != session) {
        return false;
    }
    const Cast& cast = found->second;
    if (cast.plan.kind == AbilityKind::gravityWell) {
        return attack == gamedata::progression_id(kGravityWellAttack);
    }
    if (!cast.plan.spell || cast.plan.spell->projectile.empty()) {
        return false;
    }
    // The ranged attack of the projectile spell (the trap: "Traps"; the Beacon: "Heavy"): the value "attack" of its shot item.
    const gamedata::ProjectileData* projectiles = m_world.options().projectileData;
    const gamedata::ShotRule* rule = projectiles != nullptr && projectiles->loaded() ? projectiles->ability_shot(cast.plan.spell->projectile) : nullptr;
    return rule != nullptr && attack == gamedata::progression_id(rule->attack);
}

HitWeapon Abilities::weapon_of(const Cast& cast) const {
    HitWeapon weapon;
    weapon.itemId = cast.itemId;
    weapon.slot = cast.slot;
    weapon.row = m_combat != nullptr ? m_combat->weapon_of(cast.itemId) : nullptr;
    weapon.gearScore = cast.gearScore;
    weapon.ability = ability_text(cast.ability);
    return weapon;
}

void Abilities::throw_spell(Cast& cast, const state::WorldPosition& origin, const std::array<float, 3>& direction) {
    cast.thrown = true;
    const gamedata::ProjectileData* projectiles = m_world.options().projectileData;
    const gamedata::ShotRule* rule =
        projectiles != nullptr && projectiles->loaded() && cast.plan.spell ? projectiles->ability_shot(cast.plan.spell->projectile) : nullptr;
    if (rule == nullptr) {
        return;
    }
    const std::uint64_t number = cast.number;
    SpellShot shot;
    shot.caster = cast.caster;
    shot.rule = rule;
    shot.origin = origin;
    shot.direction = direction;
    shot.weapon = weapon_of(cast);
    shot.conversion = CastConversion{cast.conversionType, cast.conversionPart};
    shot.characters = cast.plan.spell->characters;
    shot.landed = [this, number](const SpellLanding& landing) { spell_landed(number, landing); };
    ++cast.pending;
    if (m_world.projectiles().launch_spell(shot).verdict != ShotVerdict::accepted) {
        --cast.pending;
    }
}

void Abilities::cancel_aim(const std::string& characterId) {
    const auto known = m_characters.find(characterId);
    if (known != m_characters.end() && known->second.aim) {
        known->second.aim.reset();
        log::write("world: ability: the aim of character %s ends (a change of its bar, its nodes, or its place)", characterId.c_str());
    }
}

bool Abilities::confirm(SessionId session, std::uint16_t input, bool self, float yaw, bool locked) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return false;
    }
    const auto known = m_characters.find(character->characterId);
    if (known == m_characters.end() || !known->second.aim || known->second.aim->session != session) {
        return false;
    }
    const Aim aim = *known->second.aim;
    known->second.aim.reset();
    if (m_world.clock().now() - aim.started > ability_rules::kAimKeep) {
        return false;
    }
    AbilityPress press = aim.press;
    press.input = input;
    press.confirmed = true;
    press.self = self;
    // The lock of the session now, not the lock of the press of the aim (code review of stage 3.3b-S6, finding S6-R8), and the ability
    // of the aim on its place of the bar (finding S6-R2).
    press.locked = locked;
    press.expected = aim.ability;
    if (std::isfinite(yaw)) {
        press.yaw = yaw;
    }
    const CastVerdict verdict = press_entity(*character, session, press);
    if (verdict != CastVerdict::accepted) {
        log::write("world: ability: the confirm of the aim of %s refused: %s", character->name.c_str(), cast_verdict_text(verdict));
        return false;
    }
    ++m_counts.confirms;
    return true;
}

void Abilities::spell_landed(std::uint64_t number, const SpellLanding& landing) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end() || !found->second.plan.spell) {
        return;
    }
    Cast& cast = found->second;
    if (cast.pending > 0) {
        --cast.pending;
    }
    const SpellPlan& plan = *cast.plan.spell;
    SpellRequest request;
    request.spellId = plan.spell;
    request.caster = cast.caster;
    request.cast = number;
    request.role = plan.role;
    request.weapon = weapon_of(cast);
    request.conversion = CastConversion{cast.conversionType, cast.conversionPart};
    request.effects = plan.effects;
    state::WorldPosition place = landing.point;
    if (plan.role == SpellRole::aura) {
        // The area of the Beacon: on the entity that the projectile hit (a creature, a character), or on the ground at the end of
        // its flight. The radius, the life, and the statuses of its spell row (game data).
        request.attach = landing.struck;
        if (const gamedata::SpellRow* row = m_data->spell(plan.spell)) {
            request.radius = row->radius;
            request.life = std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(row->duration, 0.0F))));
        }
        // The rows that the press resolved (each one is in the status data: finding F9).
        request.statuses = plan.statuses;
        // The heal of its pulses (stage 3.3b-S6): the caster and its heal base with the weapon of the cast.
        if (const Entity* caster = m_world.find(cast.caster); caster != nullptr && caster->kind == EntityKind::player) {
            request.heal = heal_source(cast, *caster, plan.spell);
        }
    }
    if (request.attach == 0) {
        // On the ground below the point (the navigation data; with no data the point of the landing).
        if (const std::optional<float> ground = m_world.navigation().ground_height(place)) {
            place.z = *ground;
        }
    }
    request.position = place;
    if (const Entity* spell = m_world.spells().make(request)) {
        cast.spell = spell->id;
        ++cast.pending;
    }
}

void Abilities::spawn_summon(std::uint64_t number) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end() || !found->second.plan.spell) {
        return;
    }
    Cast& cast = found->second;
    if (cast.pending > 0) {
        --cast.pending;
    }
    const SpellPlan& plan = *cast.plan.spell;
    Entity* caster = m_world.find(cast.caster);
    if (caster == nullptr || caster->kind != EntityKind::player || caster->life.dead) {
        end_held_cooldown(cast.characterId, cast.ability, false);
        return;
    }
    // The place: CharRelPosOffsetY of the spell row in front of the caster (the direction of the press), on the ground
    // (NoTargetSpawnFromRaycast; the navigation data gives the floor). An area of a self spell (Sacred Ground): at the caster.
    const gamedata::SpellRow* row = m_data->spell(plan.spell);
    const float ahead = row != nullptr && plan.role == SpellRole::summon ? row->cells.number("CharRelPosOffsetY") : 0.0F;
    const std::array<float, 2> front = direction_of(cast.yaw);
    state::WorldPosition place{caster->position.x + front[0] * ahead, caster->position.y + front[1] * ahead, caster->position.z};
    if (const std::optional<float> ground = m_world.navigation().ground_height(place); ground && std::fabs(*ground - caster->position.z) < 3.0F) {
        place.z = *ground;
    }
    // The payment at the spawn (the action IceMagic_IcePylon_Cooldown pays the cost of the UI spell; the one mana path).
    const float cost = plan.payment.empty() ? 0.0F : mana_cost_of(*caster, plan.payment);
    if (cost > 0.0F && caster->vitals.mana + 0.001F < cost) {
        log::write("world: ability: %s has less mana than the cost of %s (%.1f) at its spawn: no summon", caster->name.c_str(), plan.spell.c_str(),
                   static_cast<double>(cost));
        end_held_cooldown(cast.characterId, cast.ability, false);
        return;
    }
    SpellRequest request;
    request.spellId = plan.spell;
    request.caster = cast.caster;
    request.cast = number;
    request.role = plan.role;
    request.position = place;
    request.weapon = weapon_of(cast);
    request.conversion = CastConversion{cast.conversionType, cast.conversionPart};
    if (plan.role == SpellRole::aura && row != nullptr) {
        // An area that heals (stage 3.3b-S6: Sacred Ground): the radius and the life of its spell row, its statuses, the heal source.
        request.radius = row->radius;
        request.life = std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(row->duration, 0.0F))));
        request.statuses = plan.statuses;
        request.heal = heal_source(cast, *caster, plan.spell);
    }
    const Entity* spell = m_world.spells().make(request);
    if (spell == nullptr) {
        end_held_cooldown(cast.characterId, cast.ability, false);
        return;
    }
    if (cost > 0.0F) {
        if (Entity* payer = m_world.find(cast.caster)) {
            change_mana(*payer, -cost, ability_name(cast.plan.kind));
        }
    }
    cast.spell = spell->id;
    ++cast.pending;
}

HealSource Abilities::heal_source(const Cast& cast, const Entity& caster, std::string_view spell) const {
    HealSource source;
    source.healer = caster.id;
    source.healerCharacter = caster.characterId;
    source.base = m_world.health().heal_base(caster, cast.itemId, cast.gearScore);
    source.table = cast.table;
    if (const gamedata::SpellRow* row = has_data() ? m_data->spell(spell) : nullptr) {
        source.row = std::string(row->cells.text("DamageTableRow"));
    }
    return source;
}

Entity* Abilities::heal_target(Entity& caster, const gamedata::SpellRow& spell, std::string_view ability, bool self, const HealSource& source) {
    // The rule of the ability row (game data): TargetStatusEffect with TargetStatusEffectComparison NotEqual (Divine Embrace: not a target
    // that has the status of its heal).
    const gamedata::AbilityRow* row = m_data->ability_row(ability);
    const std::string_view excluded = row != nullptr && data_rules::same_text(row->cells.text("TargetStatusEffectComparison"), "NotEqual")
                                          ? row->cells.text("TargetStatusEffect")
                                          : std::string_view();
    // A target that cannot gain health takes no direct heal (code review of stage 3.3b-S6, finding S6-R3; code review of stage 3.3b-M1,
    // finding M1-R7): its health is at its maximum, or the heal of the cast for it is not above 0 (the heal base of the caster, its
    // outgoing factor for the target, and the received factor of the target for an ability heal: Disease and the arena debuff can make
    // it 0). The choice goes on to the next friend or to the caster, and with no target the cast heals nobody and pays nothing.
    const auto permitted = [&](const Entity& target) {
        return (excluded.empty() || !m_world.statuses().has(target, excluded)) && target.vitals.health < target.vitals.healthMax
               && source.base > 0.0F && m_world.health().outgoing(caster, target, source) > 0.0F
               && m_world.health().received(target, HealKind::ability) > 0.0F;
    };
    // The self cast input (the confirm `self_target`, live check of stage 3.3b-S6): the caster, the self form of the spell.
    if (self) {
        return !caster.life.dead && permitted(caster) ? &caster : nullptr;
    }
    // The friends in the range of the spell (CastDistance 25 m), alive, in sight (from the chest of the caster to the chest of the
    // friend), the nearest first (the position index: the load is the entities near the caster).
    const float range = spell.castDistance > 0.0F ? spell.castDistance : 25.0F;
    state::WorldPosition eye = caster.position;
    eye.z += 1.5F;
    Entity* best = nullptr;
    float nearest = range;
    m_world.for_each_near(caster.position, range, [&](Entity& entity) {
        if (entity.kind != EntityKind::player || entity.id == caster.id) {
            return;
        }
        ++m_counts.healLooked;
        const float away = distance3(entity.position, caster.position);
        if (entity.life.dead || away > nearest || !m_world.friendly(caster, entity) || !permitted(entity)) {
            return;
        }
        state::WorldPosition chest = entity.position;
        chest.z += 1.5F;
        if (!m_world.navigation().sees(eye, chest)) {
            return;
        }
        best = &entity;
        nearest = away;
    });
    if (best != nullptr) {
        return best;
    }
    // The self form of the spell (Spell_LifeStaff_DivineEmbrace_Self: TargetTypes Friend; a group of one).
    return !caster.life.dead && permitted(caster) ? &caster : nullptr;
}

void Abilities::cast_heal(std::uint64_t number) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end() || !found->second.plan.heal) {
        return;
    }
    Cast& cast = found->second;
    if (cast.pending > 0) {
        --cast.pending;
    }
    const HealPlan plan = *cast.plan.heal;
    Entity* caster = m_world.find(cast.caster);
    const gamedata::SpellRow* spell = has_data() ? m_data->spell(plan.spell) : nullptr;
    if (caster == nullptr || caster->kind != EntityKind::player || caster->life.dead || spell == nullptr) {
        ++m_counts.healsRefused;
        return;
    }
    const std::string ability = ability_text(cast.ability);
    const HealSource source = heal_source(cast, *caster, plan.spell);
    Entity* target = heal_target(*caster, *spell, ability, cast.self, source);
    // The choice of the target and the cost go together (stage 3.3b-S6): no target, or less mana than the cost now: no heal, no payment.
    const float cost = mana_cost_of(*caster, plan.payment);
    if (target == nullptr || (cost > 0.0F && caster->vitals.mana + 0.001F < cost)) {
        ++m_counts.healsRefused;
        log::write("world: ability: %s of %s heals nobody (%s): no payment", ability_name(cast.plan.kind), caster->name.c_str(),
                   target == nullptr ? "no target that the data permits" : "less mana than the cost");
        return;
    }
    StatusApply how;
    how.source = StatusSource{StatusSourceKind::ability, ability, 0, caster->id};
    how.heal = source;
    const std::string targetName = target->name;
    const bool self = target->id == caster->id;
    bool given = false;
    for (const std::string& status : plan.statuses) {
        Entity* own = m_world.find(target->id);
        if (own == nullptr) {
            break;
        }
        const StatusVerdict verdict = m_world.statuses().apply(*own, status, how);
        given = given || verdict == StatusVerdict::applied || verdict == StatusVerdict::renewed || verdict == StatusVerdict::instant;
    }
    if (!given) {
        ++m_counts.healsRefused;
        log::write("world: ability: %s of %s: the target %s takes no status of the heal: no payment", ability_name(cast.plan.kind),
                   caster->name.c_str(), targetName.c_str());
        return;
    }
    if (Entity* payer = m_world.find(cast.caster); payer != nullptr && cost > 0.0F) {
        change_mana(*payer, -cost, ability_name(cast.plan.kind));
    }
    ++(self ? m_counts.healsSelf : m_counts.healsFriend);
    log::write("world: ability: %s of %s heals %s (%s, heal base %.1f, cost %.1f)", ability_name(cast.plan.kind), caster->name.c_str(),
               self ? "itself" : targetName.c_str(), plan.spell.c_str(), static_cast<double>(how.heal.base), static_cast<double>(cost));
}

void Abilities::end_held_cooldown(const std::string& characterId, std::uint32_t ability, bool countdown) {
    const auto state = m_characters.find(characterId);
    const AbilityRule* rule = rule_of(ability);
    const gamedata::CooldownRow* row = rule != nullptr && has_data() ? m_data->cooldown(rule->id) : nullptr;
    if (state == m_characters.end() || row == nullptr) {
        return;
    }
    const auto timer = state->second.cooldowns.find(ability);
    if (timer == state->second.cooldowns.end() || !timer->second.indefinite) {
        return;
    }
    // The cooldown of the data from the end of the summon (the action IceMagic_IcePylon_Cooldown; game data: 10 s), or no
    // cooldown after a spawn that failed.
    const WorldClock::time_point now = m_world.clock().now();
    timer->second.indefinite = false;
    timer->second.start = now;
    timer->second.end = countdown ? now + milliseconds(row->time) : now;
    timer->second.minEnd = countdown ? now + milliseconds(row->minTime) : now;
    timer->second.clientEnd = now;
    publish(characterId);
}

void Abilities::throw_projectile(Cast& cast, const state::WorldPosition& origin, const std::array<float, 3>& direction) {
    cast.thrown = true;
    const PhasePlan* vortexPlan = nullptr;
    std::uint32_t vortex = 0;
    std::uint32_t burst = 0;
    for (std::uint32_t index = 0; index < cast.plan.phases.size(); ++index) {
        if (cast.plan.phases[index].kind == PhaseKind::vortex) {
            vortex = index;
            vortexPlan = &cast.plan.phases[index];
        } else if (cast.plan.phases[index].kind == PhaseKind::burst) {
            burst = index;
        }
    }
    if (vortexPlan == nullptr) {
        return;
    }
    // The projectile flies in the plane to the first creature on its line, to a wall, or to the
    // cast distance of its spell; the vortex starts where it lands (rules of this project).
    const gamedata::SpellRow* projectile = m_data->spell("Spell_GreatAxe_GravityWell_Projectile");
    const float range = projectile != nullptr && projectile->castDistance > 0.0F ? projectile->castDistance : 10.0F;
    const float flat = std::hypot(direction[0], direction[1]);
    const float dx = direction[0] / flat;
    const float dy = direction[1] / flat;
    float best = range;
    std::optional<state::WorldPosition> touched;
    m_world.for_each_near(origin, range + ability_rules::kProjectileWidth, [&](Entity& entity) {
        if (entity.kind != EntityKind::creature || entity.creature.dead || entity.creature.immune()) {
            return;
        }
        const float ex = entity.position.x - origin.x;
        const float ey = entity.position.y - origin.y;
        const float along = ex * dx + ey * dy;
        const float side = std::fabs(ex * dy - ey * dx);
        if (along >= 0.0F && along < best && side <= ability_rules::kProjectileWidth
            && std::fabs(entity.position.z - origin.z) <= ability_rules::kAreaHeight) {
            best = along;
            touched = entity.position;
        }
    });
    // A wall on the line stops the projectile.
    for (float along = 0.5F; along < best; along += 0.5F) {
        const state::WorldPosition point{origin.x + dx * along, origin.y + dy * along, origin.z};
        if (!m_world.navigation().sees(origin, point)) {
            best = std::max(along - 0.5F, 0.0F);
            touched.reset();
            ++m_counts.blocked;
            break;
        }
    }
    state::WorldPosition landing{origin.x + dx * best, origin.y + dy * best, origin.z};
    if (touched) {
        landing = *touched;
    } else if (const std::optional<float> ground = m_world.navigation().ground_height(landing)) {
        landing.z = *ground;
    } else {
        landing.z = origin.z - 1.5F;
    }
    const auto flight = std::chrono::milliseconds(static_cast<long long>(best / ability_rules::kProjectileSpeed * 1000.0F));
    const std::uint64_t number = cast.number;
    // The vortex is a spell entity at the landing (stage 3.3b-S2a, world/spells.h): it runs the vortex phase at its start and
    // the burst at the activation of the burst entity of its slice (game data: 3 s), and its hits follow it.
    SpellRequest request;
    request.spellId = vortexPlan->spell;
    request.caster = cast.caster;
    request.position = landing;
    request.cast = number;
    // The statuses of the friends in the well (stage 3.3b-M2, Unyielding: the AreaStatusEffectComponent of the slice of the upgraded
    // vortex gives Status_Upgrade_Greataxe_GravityWell_ArmorInWell to the friends in its detection volume, 4 m, and takes it at their
    // exit and at the end; game data).
    if (const gamedata::SpellData* spells = m_world.options().spellData; spells != nullptr && spells->loaded()) {
        if (const gamedata::SpellSlice* slice = spells->slice_of(vortexPlan->spell)) {
            for (const gamedata::SpellAreaStatus& area : slice->areaStatuses) {
                if (area.targets != 1) {
                    continue;
                }
                for (const std::string& status : area.statuses) {
                    if (m_data->status(status) != nullptr) {
                        request.statuses.push_back(status);
                    }
                }
                request.radius = std::max(request.radius, area.radius);
                request.areaStatuses = !request.statuses.empty();
            }
        }
    }
    std::chrono::milliseconds burstAt = cast.plan.phases[burst].at;
    if (const std::optional<std::chrono::milliseconds> data = m_world.spells().activation_time(vortexPlan->spell, cast.plan.phases[burst].row)) {
        burstAt = *data;
    }
    request.phases = {SpellPhase{vortex, std::chrono::milliseconds(0)}, SpellPhase{burst, burstAt}};
    request.fallbackEnd = burstAt;
    ++cast.pending;
    ++m_counts.vortexes;
    m_world.after(flight, [this, number, request](World& world) {
        const auto found = m_casts.find(number);
        if (found == m_casts.end()) {
            return;
        }
        --found->second.pending;
        if (const Entity* spell = world.spells().make(request)) {
            found->second.spell = spell->id;
            ++found->second.pending;
        }
    });
    log::write("world: ability: the projectile of Gravity Well lands %.1f m from its origin%s: the vortex starts at %.2f %.2f %.2f "
               "in %lld ms",
               static_cast<double>(best),
               touched ? " at a creature" : "",
               static_cast<double>(landing.x),
               static_cast<double>(landing.y),
               static_cast<double>(landing.z),
               static_cast<long long>(flight.count()));
}

std::optional<MeleePhase> Abilities::contact_phase(const Entity& attacker, SessionId session, std::uint16_t input,
                                                   std::uint32_t sequence, EntityId target, std::optional<std::uint16_t> attack) {
    const auto state = m_characters.find(attacker.characterId);
    if (state == m_characters.end() || !state->second.cast) {
        return std::nullopt;
    }
    const auto found = m_casts.find(*state->second.cast);
    if (found == m_casts.end()) {
        return std::nullopt;
    }
    const Cast& cast = found->second;
    // An input number is a value of one client: it applies only in its session and its sequence.
    if (cast.session != session || cast.sequence != sequence) {
        return std::nullopt;
    }
    const unsigned steps = static_cast<std::uint16_t>(input - cast.input);
    // A newer press (finding F2): an attack input of the client after the press of the cast, or a later ability press
    // of the session, owns the contacts from its input number on.
    if (attack) {
        const unsigned attackSteps = static_cast<std::uint16_t>(*attack - cast.input);
        if (attackSteps > 0 && attackSteps <= steps) {
            return std::nullopt;
        }
    }
    if (cast.closedAt && static_cast<std::uint16_t>(*cast.closedAt - cast.input) <= steps) {
        return std::nullopt;
    }
    const WorldClock::time_point now = m_world.clock().now();
    for (std::uint32_t index = 0; index < cast.plan.phases.size(); ++index) {
        const PhasePlan& phase = cast.plan.phases[index];
        if (phase.kind != PhaseKind::contact || steps < phase.firstStep || steps > phase.lastStep) {
            continue;
        }
        // The window on the world clock too: the input numbers are values of the client. The first time of a phase
        // (finding F1): a client that sends its input numbers faster than 30 in a second does not reach a later phase
        // before its time; the tolerance is ability_rules::kPhaseEarly.
        const auto latest = std::chrono::milliseconds(phase.lastStep * 1000 / ability_rules::kStepsInSecond + 500);
        const auto earliest = std::chrono::milliseconds(phase.firstStep * 1000 / ability_rules::kStepsInSecond) - ability_rules::kPhaseEarly;
        if (now - cast.started > latest || now - cast.started < earliest) {
            return std::nullopt;
        }
        MeleePhase result;
        result.cast = cast.number;
        result.phase = index;
        result.row = *m_combat->row_index(phase.row);
        result.itemId = cast.itemId;
        result.slot = cast.slot;
        result.gearScore = cast.gearScore;
        result.conversionType = cast.conversionType;
        result.conversionPart = cast.conversionPart;
        result.done = cast.hits.count({index, target}) != 0;
        return result;
    }
    return std::nullopt;
}

AbilityKind Abilities::kind_of(std::uint64_t cast) const {
    const auto found = m_casts.find(cast);
    return found == m_casts.end() ? AbilityKind::wreckingBall : found->second.plan.kind;
}

void Abilities::contact_hit(std::uint64_t number, std::uint32_t phase, Entity& attacker, Entity& target) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end() || phase >= found->second.plan.phases.size()) {
        return;
    }
    Cast& cast = found->second;
    cast.hits.insert({phase, target.id});
    ++m_counts.contactHits;
    if (!target.creature.dead) {
        give_effects(cast, cast.plan.phases[phase], target);
    }
}

void Abilities::spell_phase(std::uint64_t cast, std::uint32_t phase, const state::WorldPosition& center) {
    const auto found = m_casts.find(cast);
    if (found == m_casts.end() || phase >= found->second.plan.phases.size()) {
        return;
    }
    run_area(cast, phase, center, true);
}

void Abilities::spell_ended(std::uint64_t number, EntityId spell) {
    const auto found = m_casts.find(number);
    if (found != m_casts.end() && found->second.spell == spell) {
        found->second.spell = 0;
        if (found->second.pending > 0) {
            --found->second.pending;
        }
        // The cooldown that held while the summon lived starts now (stage 3.3b-S2b: the Pylon).
        if (found->second.plan.spell && found->second.plan.spell->role == SpellRole::summon) {
            end_held_cooldown(found->second.characterId, found->second.ability, true);
        }
    }
}

void Abilities::run_area(std::uint64_t number, std::uint32_t index, state::WorldPosition center, bool fromSpell) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end()) {
        return;
    }
    Cast& cast = found->second;
    if (!fromSpell) {
        --cast.pending;
    }
    Entity* caster = m_world.find(cast.caster);
    // A caster that left the world does no damage; a dead caster does none with a timer of the cast (a rule of this
    // project). The phase of a spell entity hits while the spell lives: the data ends a spell at the death of its caster
    // only with EndOnCasterDeath (world/spells.h).
    if (caster == nullptr || caster->characterId != cast.characterId || (caster->life.dead && !fromSpell) || !has_data()) {
        return;
    }
    const PhasePlan& phase = cast.plan.phases[index];
    const gamedata::SpellRow* spell = m_data->spell(phase.spell);
    if (spell == nullptr) {
        return;
    }
    if (phase.kind == PhaseKind::around) {
        center = caster->position;
    }
    const float height = std::max(ability_rules::kAreaHeight, spell->height);
    // The creatures of the area from the position index: the cost depends on the creatures near
    // the center, not on the creatures of the world.
    std::vector<std::pair<float, EntityId>> found_;
    m_world.for_each_near(center, spell->radius, [&](Entity& entity) {
        ++m_counts.areaLooked;
        if (entity.kind == EntityKind::creature && !entity.creature.dead && std::fabs(entity.position.z - center.z) <= height) {
            found_.emplace_back(horizontal_distance(entity.position, center), entity.id);
        }
    });
    std::sort(found_.begin(), found_.end());
    if (found_.size() > ability_rules::kAreaMost) {
        found_.resize(ability_rules::kAreaMost);
    }
    float factor = 1.0F;
    if (phase.crowded) {
        // Crowded Well: more damage for each creature in the vortex.
        const auto count = static_cast<float>(std::min<std::size_t>(found_.size(), ability_rules::kCrowdedMost));
        factor += ability_rules::kCrowdedPart * count;
    }
    unsigned hits = 0;
    // The cap is for the whole phase, also for a path of more circles (code review of stage 3.3a, finding F4): the hits
    // of the circles before this one count.
    std::size_t phaseHits = static_cast<std::size_t>(
        std::distance(cast.hits.lower_bound({index, 0}), cast.hits.lower_bound({index + 1, 0})));
    for (const auto& [away, id] : found_) {
        if (phaseHits >= ability_rules::kAreaMost) {
            break;
        }
        Entity* target = m_world.creatures().find(id);
        if (target == nullptr || target->creature.dead || cast.hits.count({index, id}) != 0) {
            continue;
        }
        if (target->creature.immune()) {
            ++m_counts.effectsRefused;
            continue;
        }
        // A wall between the center and the creature stops the area.
        if (!m_world.navigation().sees(center, target->position)) {
            ++m_counts.blocked;
            continue;
        }
        if (area_hit(cast, index, *caster, *target, factor, center, static_cast<std::uint32_t>(found_.size()))) {
            ++hits;
            ++phaseHits;
        }
    }
    log::write("world: ability: %s of %s at %.2f %.2f %.2f (radius %.1f m): %zu creature(s) in the area, %u hit(s)",
               phase.kind == PhaseKind::vortex ? "the vortex" : phase.kind == PhaseKind::burst ? "the burst" : "the area",
               ability_name(cast.plan.kind),
               static_cast<double>(center.x),
               static_cast<double>(center.y),
               static_cast<double>(center.z),
               static_cast<double>(spell->radius),
               found_.size(),
               hits);
}

bool Abilities::area_hit(Cast& cast, std::uint32_t index, Entity& caster, Entity& target, float factor, const state::WorldPosition& center,
                         std::uint32_t areaCount) {
    const PhasePlan& phase = cast.plan.phases[index];
    if (phase.row.empty()) {
        // A phase with no damage (the start of the vortex): only its effects, and the impact of its pull row (stage 3.3b-S4).
        cast.hits.insert({index, target.id});
        give_effects(cast, phase, target);
        if (!phase.pull.empty()) {
            const std::optional<std::uint32_t> pull = m_combat->row_index(phase.pull);
            const gamedata::DamageRow* row = pull ? &m_combat->damage_row(*pull) : nullptr;
            Entity* pulled = m_world.creatures().find(target.id);
            if (row != nullptr && row->impact && pulled != nullptr) {
                m_world.creatures().impact(*pulled, *row->impact, center, caster.position, row->id);
            }
        }
        return true;
    }
    const std::optional<std::uint32_t> row = m_combat->row_index(phase.row);
    const gamedata::WeaponRow* weapon = m_combat->weapon_of(cast.itemId);
    if (!row || weapon == nullptr) {
        return false;
    }
    const CastConversion conversion{cast.conversionType, cast.conversionPart};
    // The facts of the hit for the passive rules (stage 3.3b-S5): the cast is the attack, the spell of the phase and the number of
    // the creatures of its area (Crowded Well counts the creatures in the vortex).
    HitWeapon use{cast.itemId, cast.slot, weapon, cast.gearScore, ability_text(cast.ability)};
    use.attack = passive_rules::kAttackCast | cast.number;
    use.kind = HitKind::area;
    use.spell = phase.spell;
    use.areaCount = areaCount;
    const HitResult result = m_world.combat().strike(caster, target, use, *row, factor, ability_name(cast.plan.kind), &conversion, &center);
    if (result.verdict != HitVerdict::accepted) {
        return false;
    }
    cast.hits.insert({index, target.id});
    ++m_counts.areaHits;
    if (Entity* hurt = m_world.creatures().find(target.id); hurt != nullptr && !hurt->creature.dead) {
        give_effects(cast, phase, *hurt);
    }
    if (phase.cooldownPerHit && cast.cooldownHits < ability_rules::kCooldownHits) {
        // Stimulated Reduction: each hit takes a part of the cooldown of the ability away, not
        // below its minimum end and not below now.
        ++cast.cooldownHits;
        CharacterState& state = m_characters[cast.characterId];
        const auto timer = state.cooldowns.find(cast.ability);
        const AbilityRule* rule = rule_of(cast.ability);
        const gamedata::CooldownRow* row = rule != nullptr ? m_data->cooldown(rule->id) : nullptr;
        if (timer != state.cooldowns.end() && row != nullptr) {
            // The part of the whole cooldown of the data, for each hit.
            const auto part = milliseconds(row->time * ability_rules::kCooldownPerHit);
            timer->second.end = std::max({timer->second.end - part, timer->second.minEnd, m_world.clock().now()});
            timer->second.clientEnd = std::max({timer->second.clientEnd - part, timer->second.minEnd, m_world.clock().now()});
            publish(cast.characterId);
        }
    }
    return true;
}

void Abilities::give_effects(Cast& cast, const PhasePlan& phase, Entity& target) {
    for (const EffectPlan& effect : phase.effects) {
        const EntityId source = cast.caster;
        const EntityId id = target.id;
        const auto apply = [this, effect, source, id](World& world) {
            Entity* creature = world.creatures().find(id);
            if (creature == nullptr || creature->creature.dead) {
                return;
            }
            const bool done = effect.power != 0 ? world.creatures().knock(*creature, effect.power, effect.time)
                                                : apply_status(*creature, effect.status, source);
            ++(done ? m_counts.effects : m_counts.effectsRefused);
        };
        if (effect.delay.count() > 0) {
            m_world.after(effect.delay, apply);
        } else {
            apply(m_world);
        }
    }
}

bool Abilities::apply_status(Entity& creature, std::string_view status, EntityId source, std::optional<float> seconds) {
    const gamedata::StatusRow* row = m_data != nullptr ? m_data->status(status) : nullptr;
    if (row == nullptr || creature.kind != EntityKind::creature) {
        return false;
    }
    CreatureEffect given;
    given.status = row->id;
    given.index = row->index;
    given.shown = row->nameplate;
    given.stun = row->stunned;
    given.root = row->root;
    given.speed = 1.0F + row->moveSpeed;
    given.damage = row->damage;
    given.endsOnHit = row->stopOnHit != 0;
    given.ends = m_world.clock().now() + milliseconds(seconds ? *seconds : row->duration);
    given.source = source;
    return m_world.creatures().apply_effect(creature, given);
}

void Abilities::publish(const std::string& characterId) {
    Entity* entity = m_world.find_character(characterId);
    const auto state = m_characters.find(characterId);
    if (entity == nullptr || state == m_characters.end()) {
        return;
    }
    // A cooldown leaves the state when its end and the end of the client cooldown passed (code review of stage 1.3b,
    // finding F11: a cast of a different ability erased the end of the client). The map of the owner has only the
    // cooldowns of the server that run: the client keeps an entry until a new map comes.
    const WorldClock::time_point now = m_world.clock().now();
    for (auto timer = state->second.cooldowns.begin(); timer != state->second.cooldowns.end();) {
        timer = !timer->second.indefinite && std::max(timer->second.end, timer->second.clientEnd) <= now ? state->second.cooldowns.erase(timer)
                                                                                                       : std::next(timer);
    }
    entity->cooldowns.clear();
    for (const auto& [ability, timer] : state->second.cooldowns) {
        if (timer.indefinite || timer.end > now) {
            entity->cooldowns.emplace(ability, timer);
        }
    }
    m_world.touch(entity->id, ComponentKey::cooldowns);
}

void Abilities::entered(Entity& character) {
    const auto state = m_characters.find(character.characterId);
    if (state == m_characters.end()) {
        return;
    }
    // A new entity of the character: no cast plays, and the cooldowns stay (with the end of the client: finding F11).
    state->second.cast.reset();
    const WorldClock::time_point now = m_world.clock().now();
    for (auto timer = state->second.cooldowns.begin(); timer != state->second.cooldowns.end();) {
        timer = !timer->second.indefinite && std::max(timer->second.end, timer->second.clientEnd) <= now ? state->second.cooldowns.erase(timer)
                                                                                                       : std::next(timer);
    }
    character.cooldowns.clear();
    for (const auto& [ability, timer] : state->second.cooldowns) {
        if (timer.indefinite || timer.end > now) {
            character.cooldowns.emplace(ability, timer);
        }
    }
}

bool Abilities::reset_cooldowns(const std::string& characterId, bool client) {
    CharacterState& state = m_characters[characterId];
    // The cooldowns of the server end now. The client keeps its own cooldown (live check I8-05): the entry keeps the end
    // of the client until it comes.
    const WorldClock::time_point now = m_world.clock().now();
    for (auto timer = state.cooldowns.begin(); timer != state.cooldowns.end();) {
        if (client || timer->second.clientEnd <= now) {
            timer = state.cooldowns.erase(timer);
            continue;
        }
        timer->second.end = now;
        timer->second.minEnd = now;
        timer->second.indefinite = false;
        ++timer;
    }
    if (Entity* entity = m_world.find_character(characterId)) {
        entity->cooldowns.clear();
        m_world.touch(entity->id, ComponentKey::cooldowns);
        return true;
    }
    return false;
}

float Abilities::mana_cost(const Entity& character, const CastPlan& plan) const {
    if (!has_data()) {
        return 0.0F;
    }
    for (const PhasePlan& phase : plan.phases) {
        const gamedata::SpellRow* spell = phase.spell.empty() ? nullptr : m_data->spell(phase.spell);
        if (spell == nullptr) {
            continue;
        }
        const std::string_view costId = spell->cells.text("ManaCost");
        if (!costId.empty() && m_data->mana_cost(costId)) {
            return mana_cost_of(character, costId);
        }
    }
    // The projectile spell of the spell entity of the cast (stage 3.3b-S2b: the Beacon, ManaCost LifeStaff_Beacon, 16), paid at the
    // press as each cost. A summon pays at its spawn (SpellPlan::payment).
    if (plan.spell && plan.spell->payment.empty() && !plan.spell->projectile.empty()) {
        if (const gamedata::SpellRow* spell = m_data->spell(plan.spell->projectile)) {
            const std::string_view costId = spell->cells.text("ManaCost");
            if (!costId.empty() && m_data->mana_cost(costId)) {
                return mana_cost_of(character, costId);
            }
        }
    }
    return 0.0F;
}

float Abilities::mana_cost_of(const Entity& character, std::string_view costId) const {
    if (!has_data() || costId.empty()) {
        return 0.0F;
    }
    const std::optional<float> base = m_data->mana_cost(costId);
    if (!base || *base <= 0.0F) {
        return 0.0F;
    }
    // The ManaCostMult of the learned nodes of the character whose ManaCostList has the id or "All".
    float factor = 1.0F;
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    for (const auto& [pool, nodes] : character.skills.pools) {
        for (const auto& [node, value] : nodes) {
            const gamedata::AbilityNode* row = tables != nullptr && value.level > 0 ? tables->node(node) : nullptr;
            const gamedata::AbilityCostRow* cost = row != nullptr ? m_data->ability_cost(row->id) : nullptr;
            if (cost == nullptr || cost->manaMultiplier == 0.0F) {
                continue;
            }
            // The client compares the CRC-32 of the lower-case texts (0x9F761B8: the CRC of "all").
            const std::uint32_t wanted = gamedata::progression_id(costId);
            const std::uint32_t all = gamedata::progression_id("All");
            if (std::any_of(cost->mana.begin(), cost->mana.end(), [&](const std::string& entry) {
                    const std::uint32_t crc = gamedata::progression_id(entry);
                    return crc == wanted || crc == all;
                })) {
                factor *= 1.0F + cost->manaMultiplier;
            }
        }
    }
    return std::max(*base * factor, 0.0F);
}

void Abilities::change_mana(Entity& character, float amount, const char* what) {
    if (character.kind != EntityKind::player || !std::isfinite(amount) || amount == 0.0F) {
        return;
    }
    const float before = character.vitals.mana;
    character.vitals.mana = std::clamp(character.vitals.mana + amount, 0.0F, character.vitals.manaMax);
    if (character.vitals.mana == before) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const float rate = tables != nullptr && tables->loaded() ? tables->data().manaRegenRate : 0.0F;
    if (amount < 0.0F) {
        // A spend: the regeneration starts after the delay of the data (client code at image RVA 0x6949DE0).
        const float delay = tables != nullptr && tables->loaded()
                                ? (character.vitals.mana > 0.0F ? tables->data().manaRegenDelay : tables->data().manaRegenDelayOnEmpty)
                                : 0.0F;
        character.vitals.manaRegenFrom = now + milliseconds(delay);
    } else if (character.vitals.mana < character.vitals.manaMax && !character.vitals.manaRegenFrom) {
        character.vitals.manaRegenFrom = now;
    }
    character.vitals.manaRegenRate = rate;
    if (character.vitals.mana >= character.vitals.manaMax) {
        character.vitals.manaRegenFrom.reset();
        m_regenerating.erase(character.characterId);
    } else if (rate > 0.0F) {
        m_regenerating[character.characterId] = std::max(now, *character.vitals.manaRegenFrom);
    }
    m_world.touch(character.id, ComponentKey::vitals);
    m_world.touch(character.id, ComponentKey::mana);
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: mana: %s %s %.1f mana (%s), mana %.1f to %.1f",
               character.name.c_str(),
               amount < 0.0F ? "pays" : "gets",
               static_cast<double>(std::fabs(amount)),
               what,
               static_cast<double>(before),
               static_cast<double>(character.vitals.mana));
}

bool Abilities::holds_cast_weapon(const Cast& cast, const Entity& character) const {
    const ItemInstance* item = Combat::weapon_in_hand(character);
    return item != nullptr && item->itemId == cast.itemId && Combat::weapon_slot_in_hand(character).value_or(0) == cast.slot;
}

void Abilities::weapon_changed(Entity& character) {
    const auto state = m_characters.find(character.characterId);
    if (state == m_characters.end()) {
        return;
    }
    // A change of the weapon ends the aim of an ability (stage 3.3b-S6).
    state->second.aim.reset();
    if (!state->second.cast) {
        return;
    }
    const auto found = m_casts.find(*state->second.cast);
    if (found == m_casts.end() || found->second.plan.window.empty() || found->second.countered || holds_cast_weapon(found->second, character)) {
        return;
    }
    // The window of a counter ends with the weapon of its cast (finding F2 of the review of stage 3.3b-S4).
    const std::string& window = found->second.plan.window;
    m_world.statuses().remove(character, [&](const ActiveStatus& status) { return status.status == window; }, StatusEnd::replaced);
    log::write("world: ability: %s changes the weapon: the window of %s ends", character.name.c_str(), ability_name(found->second.plan.kind));
}

bool Abilities::countered(Entity& character, Entity& attacker, float reach) {
    const auto state = m_characters.find(character.characterId);
    if (state == m_characters.end() || !state->second.cast || character.life.dead) {
        return false;
    }
    const auto found = m_casts.find(*state->second.cast);
    if (found == m_casts.end() || found->second.plan.window.empty() || found->second.countered || found->second.caster != character.id) {
        return false;
    }
    Cast& cast = found->second;
    const std::string& window = cast.plan.window;
    // The counter needs the weapon of the cast in the hand (the condition Riposte_Attack of the action data: the mannequin tag
    // Rapier and the timer; code review of stage 3.3b-S4, finding F2). A different weapon ends the window.
    if (!holds_cast_weapon(cast, character)) {
        m_world.statuses().remove(
            character, [&](const ActiveStatus& status) { return status.status == window; }, StatusEnd::replaced);
        return false;
    }
    // The window is the status of the data on the character: the hit is a counter only while it is there.
    const bool open = std::any_of(character.statuses.begin(), character.statuses.end(), [&](const ActiveStatus& status) {
        return status.status == window && (status.permanent || m_world.clock().now() < status.ends);
    });
    if (!open) {
        return false;
    }
    cast.countered = true;
    ++m_counts.counters;
    // The counter action ends the window (the action Out removes the timer), and its capsule hits at its time.
    m_world.statuses().remove(
        character, [&](const ActiveStatus& status) { return status.status == window; }, StatusEnd::replaced);
    const std::uint64_t number = cast.number;
    const EntityId target = attacker.id;
    for (std::uint32_t index = 0; index < cast.plan.phases.size(); ++index) {
        const PhasePlan& phase = cast.plan.phases[index];
        if (phase.kind != PhaseKind::counter) {
            continue;
        }
        ++cast.pending;
        m_world.after(phase.at, [this, number, index, target, reach](World&) { counter_hit(number, index, target, reach); });
    }
    log::write("world: ability: %s counters the hit of creature %016llx (%s): the counter action starts on the server",
               character.name.c_str(),
               static_cast<unsigned long long>(attacker.id),
               attacker.kind == EntityKind::creature ? attacker.creature.kind->id.c_str() : attacker.name.c_str());
    return true;
}

void Abilities::counter_hit(std::uint64_t number, std::uint32_t index, EntityId attacker, float reach) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end()) {
        return;
    }
    Cast& cast = found->second;
    --cast.pending;
    Entity* caster = m_world.find(cast.caster);
    Entity* target = m_world.creatures().find(attacker);
    if (caster == nullptr || caster->characterId != cast.characterId || caster->life.dead || target == nullptr || target->creature.dead
        || cast.hits.count({index, attacker}) != 0 || !has_data()) {
        return;
    }
    // The counter action ends with a change of the weapon (finding F2 of the review of stage 3.3b-S4).
    if (!holds_cast_weapon(cast, *caster)) {
        log::write("world: ability: the counter of %s has no hit: the weapon of the cast is not in the hand", caster->name.c_str());
        return;
    }
    // The capsule of the counter action is in front of the caster: the attacker must still be in the reach of its hit, with
    // no wall between them.
    if (distance3(caster->position, target->position) > reach || !m_world.navigation().sees(caster->position, target->position)) {
        log::write("world: ability: the counter of %s does not reach creature %016llx", caster->name.c_str(),
                   static_cast<unsigned long long>(attacker));
        return;
    }
    const PhasePlan& phase = cast.plan.phases[index];
    const std::optional<std::uint32_t> row = m_combat->row_index(phase.row);
    const gamedata::WeaponRow* weapon = m_combat->weapon_of(cast.itemId);
    if (!row || weapon == nullptr) {
        return;
    }
    const CastConversion conversion{cast.conversionType, cast.conversionPart};
    HitWeapon use{cast.itemId, cast.slot, weapon, cast.gearScore, ability_text(cast.ability)};
    use.attack = passive_rules::kAttackCast | cast.number;
    const HitResult result = m_world.combat().strike(*caster, *target, use, *row, 1.0F, ability_name(cast.plan.kind), &conversion);
    if (result.verdict != HitVerdict::accepted) {
        return;
    }
    cast.hits.insert({index, attacker});
    ++m_counts.counterHits;
    if (Entity* hurt = m_world.creatures().find(attacker); hurt != nullptr && !hurt->creature.dead) {
        give_effects(cast, phase, *hurt);
    }
}

MotionAllowance Abilities::motion_allowance(const Entity& character, WorldClock::time_point now) const {
    const auto state = m_characters.find(character.characterId);
    if (state == m_characters.end() || !state->second.cast) {
        return {};
    }
    const auto found = m_casts.find(*state->second.cast);
    if (found == m_casts.end() || !found->second.plan.motion || found->second.caster != character.id) {
        return {};
    }
    const CasterMotion& motion = *found->second.plan.motion;
    // The envelope runs for the length of the movement, and for one window of the check after it: the window of a report
    // after the dash still has the places of the dash. A fast step only in the movement, and the extra distance one time
    // for the cast: the budget from the place of the press (code review of stage 3.3b-S4, finding F1).
    const Cast& cast = found->second;
    if (now < cast.started || now > cast.started + motion.length + position_limits::kWindow) {
        return {};
    }
    MotionAllowance allowance;
    allowance.distance = motion.distance;
    allowance.peak = now <= cast.started + motion.length ? motion.peak : 0.0F;
    allowance.from = cast.place;
    allowance.since = std::chrono::duration<float>(now - cast.started).count();
    return allowance;
}

void Abilities::mark_motion(Entity& character) {
    m_motionMarks[character.characterId] = m_world.clock().now();
    m_world.touch(character.id, ComponentKey::motion);
}

void Abilities::caster_moved(Entity& character) {
    const auto mark = m_motionMarks.find(character.characterId);
    if (mark == m_motionMarks.end() || m_world.clock().now() - mark->second < ability_rules::kMotionMark) {
        return;
    }
    mark_motion(character);
}

void Abilities::start_motion(Cast& cast, Entity& character) {
    const CasterMotion& motion = *cast.plan.motion;
    const std::uint64_t number = cast.number;
    const EntityId caster = character.id;
    const std::string characterId = character.characterId;
    // The direction of the body for the observers: the direction of the press (degrees from north, to the west).
    character.facing = cast.yaw * 180.0F / 3.14159265358979F;
    // The actions of layer 0 for the observers at their times, and the action at rest at the end of the movement. A later
    // cast of the character replaces them (the check of the cast number).
    const auto set_action = [this, number, caster, characterId](std::int32_t actionState, std::int32_t sequence) {
        Entity* entity = m_world.find(caster);
        const auto state = m_characters.find(characterId);
        if (entity == nullptr || state == m_characters.end() || state->second.cast != number) {
            return;
        }
        entity->action.state = actionState;
        entity->action.sequence = sequence;
        ++entity->action.started;
        mark_motion(*entity);
    };
    for (const MotionAction& action : motion.actions) {
        if (action.at.count() <= 0) {
            set_action(action.state, action.sequence);
        } else {
            m_world.after(action.at, [set_action, action](World&) { set_action(action.state, action.sequence); });
        }
    }
    std::vector<std::int32_t> sequences;
    for (const MotionAction& action : motion.actions) {
        sequences.push_back(action.sequence);
    }
    m_world.after(motion.length, [this, caster, characterId, number, sequences](World&) {
        // The character is at rest again, also when a later cast of it came in the movement: an action of this movement
        // must not stay on an observer.
        Entity* entity = m_world.find(caster);
        if (entity != nullptr && entity->characterId == characterId
            && std::find(sequences.begin(), sequences.end(), entity->action.sequence) != sequences.end()) {
            const ActionState rest;
            entity->action.state = rest.state;
            entity->action.sequence = rest.sequence;
            ++entity->action.started;
            mark_motion(*entity);
        }
        const auto state = m_characters.find(characterId);
        if (state != m_characters.end() && state->second.cast == number) {
            m_motionMarks.erase(characterId);
        }
    });
    m_motionMarks[characterId] = m_world.clock().now();
    if (cast.session != 0 || character.owner != 0 || motion.dash <= 0.0F) {
        return; // The client of the caster makes the movement: the server accepts it in the envelope.
    }
    // The dash of the server for a character with no client (a remote test character): on the line of the press, to the
    // dash distance, or to the first creature on the line (it stops kDashStop in front of it), through the navigation.
    const std::array<float, 2> ahead = direction_of(cast.yaw);
    float distance = motion.dash;
    m_world.for_each_near(character.position, motion.dash + ability_rules::kProjectileWidth, [&](Entity& entity) {
        if (entity.kind != EntityKind::creature || entity.creature.dead) {
            return;
        }
        const float ex = entity.position.x - character.position.x;
        const float ey = entity.position.y - character.position.y;
        const float along = ex * ahead[0] + ey * ahead[1];
        const float side = std::fabs(ex * ahead[1] - ey * ahead[0]);
        if (along > 0.0F && side <= ability_rules::kProjectileWidth && std::fabs(entity.position.z - character.position.z) <= ability_rules::kAreaHeight) {
            distance = std::min(distance, std::max(along - ability_rules::kDashStop, 0.0F));
        }
    });
    Dash dash;
    dash.caster = caster;
    dash.characterId = characterId;
    dash.start = character.position;
    dash.direction = ahead;
    dash.distance = distance;
    dash.started = m_world.clock().now();
    dash.time = motion.dashTime;
    m_dashes[number] = dash;
    log::write("world: ability: %s makes the dash of %s on the server (no client): %.2f m in %.2f s",
               character.name.c_str(),
               ability_name(cast.plan.kind),
               static_cast<double>(distance),
               std::chrono::duration<double>(motion.dashTime).count());
}

void Abilities::step_dashes() {
    if (m_dashes.empty()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    for (auto entry = m_dashes.begin(); entry != m_dashes.end();) {
        Dash& dash = entry->second;
        Entity* character = m_world.find(dash.caster);
        if (character == nullptr || character->characterId != dash.characterId || character->life.dead || character->owner != 0) {
            entry = m_dashes.erase(entry);
            continue;
        }
        const auto mark = m_motionMarks.find(dash.characterId);
        if (mark != m_motionMarks.end() && now - mark->second < ability_rules::kMotionMark && now < dash.started + dash.time) {
            ++entry;
            continue;
        }
        const float part = std::min(std::chrono::duration<float>(now - dash.started) / std::chrono::duration<float>(dash.time), 1.0F);
        const float more = dash.distance * part - dash.travelled;
        const state::WorldPosition goal{character->position.x + dash.direction[0] * more, character->position.y + dash.direction[1] * more,
                                        character->position.z};
        const Slide slide = m_world.navigation().slide(character->position, goal);
        bool done = part >= 1.0F || slide.blocked;
        if (!slide.waiting) {
            dash.travelled += horizontal_distance(character->position, slide.position);
            if (m_world.move_character(*character, slide.position)) {
                mark_motion(*character);
            } else {
                done = true;
            }
        }
        if (done) {
            log::write("world: ability: the dash of %s on the server ends after %.2f m%s",
                       character->name.c_str(),
                       static_cast<double>(dash.travelled),
                       slide.blocked ? " (a wall or a drop stopped it)" : "");
            entry = m_dashes.erase(entry);
            continue;
        }
        ++entry;
    }
}

void Abilities::tick() {
    step_dashes();
    if (m_regenerating.empty()) {
        return;
    }
    const gamedata::ProgressionTables* tables = m_world.options().progressionTables;
    const float rate = tables != nullptr && tables->loaded() ? tables->data().manaRegenRate : 0.0F;
    const WorldClock::time_point now = m_world.clock().now();
    for (auto entry = m_regenerating.begin(); entry != m_regenerating.end();) {
        Entity* character = m_world.find_character(entry->first);
        if (character == nullptr || rate <= 0.0F || !character->vitals.manaRegenFrom) {
            entry = m_regenerating.erase(entry);
            continue;
        }
        const WorldClock::time_point from = std::max(entry->second, *character->vitals.manaRegenFrom);
        if (now <= from) {
            ++entry;
            continue;
        }
        // The client predicts the same gain with the rate of the mana state: the server sends the state only when the mana
        // is full again (and at each spend), and the client compares its value with it.
        const float seconds = std::chrono::duration<float>(now - from).count();
        character->vitals.mana = std::min(character->vitals.mana + rate * seconds, character->vitals.manaMax);
        entry->second = now;
        if (character->vitals.mana >= character->vitals.manaMax) {
            character->vitals.manaRegenFrom.reset();
            m_world.touch(character->id, ComponentKey::vitals);
            m_world.touch(character->id, ComponentKey::mana);
            entry = m_regenerating.erase(entry);
            continue;
        }
        ++entry;
    }
}

void Abilities::perk_burst(Entity& caster, std::string_view spellId) {
    if (!has_data() || caster.life.dead) {
        return;
    }
    const gamedata::SpellRow* spell = m_data->spell(spellId);
    const ItemInstance* item = Combat::weapon_in_hand(caster);
    const gamedata::WeaponRow* weapon = item != nullptr ? m_combat->weapon_of(item->itemId) : nullptr;
    const std::optional<std::uint32_t> row = spell != nullptr ? m_combat->row_index(spell->cells.text("DamageTableRow")) : std::nullopt;
    if (spell == nullptr || weapon == nullptr || !row) {
        return;
    }
    const state::WorldPosition center = caster.position;
    std::vector<std::pair<float, EntityId>> found_;
    m_world.for_each_near(center, spell->radius, [&](Entity& entity) {
        ++m_counts.areaLooked;
        if (entity.kind == EntityKind::creature && !entity.creature.dead && std::fabs(entity.position.z - center.z) <= ability_rules::kAreaHeight) {
            found_.emplace_back(horizontal_distance(entity.position, center), entity.id);
        }
    });
    std::sort(found_.begin(), found_.end());
    const std::string itemId = item->itemId;
    const std::uint32_t gearScore = item->gearScore;
    const std::uint32_t slot = Combat::weapon_slot_in_hand(caster).value_or(0);
    const EntityId casterId = caster.id;
    unsigned hits = 0;
    for (const auto& [away, id] : found_) {
        Entity* own = m_world.find(casterId);
        Entity* target = m_world.creatures().find(id);
        if (hits >= ability_rules::kAreaMost || own == nullptr || own->life.dead) {
            break;
        }
        if (target == nullptr || target->creature.dead || target->creature.immune() || !m_world.navigation().sees(center, target->position)) {
            continue;
        }
        if (m_world.combat().strike(*own, *target, HitWeapon{itemId, slot, weapon, gearScore, {}}, *row, 1.0F, "a burst of a perk").verdict
            == HitVerdict::accepted) {
            ++hits;
            ++m_counts.areaHits;
        }
    }
    log::write("world: ability: the burst of a perk (%s) of %s (radius %.1f m): %zu creature(s) in the area, %u hit(s)",
               spell->id.c_str(), caster.name.c_str(), static_cast<double>(spell->radius), found_.size(), hits);
}

void Abilities::reduce_weapon_cooldowns(Entity& character, float part) {
    reduce_weapon_cooldowns(character, part, {}, {});
}

void Abilities::reduce_weapon_cooldowns(Entity& character, float part, std::string_view only, std::string_view except) {
    if (!has_data() || !(part > 0.0F)) {
        return;
    }
    // An ability is the one of the filter when its id or the cooldown id of its row (CooldownId: "Executioner") is the text.
    const auto named = [&](const AbilityRule& rule, std::string_view text) {
        if (data_rules::same_text(rule.id, text)) {
            return true;
        }
        const gamedata::AbilityRow* row = m_data->ability_row(rule.id);
        return row != nullptr && data_rules::same_text(row->cells.text("CooldownId"), text);
    };
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::WeaponRow* weapon = item != nullptr ? m_combat->weapon_of(item->itemId) : nullptr;
    if (weapon == nullptr || weapon->mastery.empty()) {
        return;
    }
    const auto bar = character.skills.bars.find(gamedata::progression_id(weapon->mastery));
    const auto state = m_characters.find(character.characterId);
    if (bar == character.skills.bars.end() || state == m_characters.end()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    bool changed = false;
    for (const std::uint32_t ability : bar->second) {
        const auto timer = state->second.cooldowns.find(ability);
        const AbilityRule* rule = rule_of(ability);
        const gamedata::CooldownRow* row = rule != nullptr ? m_data->cooldown(rule->id) : nullptr;
        if (timer == state->second.cooldowns.end() || row == nullptr || timer->second.end <= now || timer->second.indefinite) {
            continue;
        }
        if ((!only.empty() && !named(*rule, only)) || (!except.empty() && named(*rule, except))) {
            continue;
        }
        const auto cut = milliseconds(row->time * part);
        timer->second.end = std::max({timer->second.end - cut, timer->second.minEnd, now});
        timer->second.clientEnd = std::max({timer->second.clientEnd - cut, timer->second.minEnd, now});
        changed = true;
    }
    if (changed) {
        publish(character.characterId);
    }
}

const std::map<std::uint32_t, AbilityCooldown>* Abilities::cooldowns_of(const std::string& characterId) const {
    const auto state = m_characters.find(characterId);
    return state == m_characters.end() ? nullptr : &state->second.cooldowns;
}

AbilityStrike Abilities::strike_of(std::uint64_t cast) const {
    const auto found = m_casts.find(cast);
    AbilityStrike result;
    result.valid = found != m_casts.end() && found->second.plan.sequence && has_data();
    return result;
}

HitResult Abilities::strike_hit(std::uint64_t number, const gamedata::AttackFragment& fragment, std::uint32_t strike, std::uint32_t rowIndex,
                                Entity& attacker, Entity& target) {
    HitResult result;
    result.verdict = HitVerdict::noStrike;
    const auto found = m_casts.find(number);
    const gamedata::WeaponRow* weapon = found != m_casts.end() ? m_combat->weapon_of(found->second.itemId) : nullptr;
    if (found == m_casts.end() || weapon == nullptr || found->second.caster != attacker.id) {
        return result;
    }
    Cast& cast = found->second;
    // The hit with the weapon, the gem, and the attack of the cast (the hits of one cast are the hits of one attack: stage 3.3b-S5).
    const CastConversion conversion{cast.conversionType, cast.conversionPart};
    HitWeapon use{cast.itemId, cast.slot, weapon, cast.gearScore, ability_text(cast.ability)};
    use.attack = passive_rules::kAttackCast | cast.number;
    use.fragment = fragment.name;
    const std::string what = std::string(ability_name(cast.plan.kind)) + ", " + fragment.name + " " + fragment.strikes[strike].key;
    const EntityId targetId = target.id;
    result = m_world.combat().strike(attacker, target, use, rowIndex, 1.0F, what.c_str(), &conversion);
    if (result.verdict != HitVerdict::accepted) {
        return result;
    }
    cast.struck.insert(targetId);
    ++m_counts.contactHits;
    ++m_counts.strikeHits;
    count_hit(attacker, cast.ability, false);
    if (Entity* hurt = m_world.creatures().find(targetId); hurt != nullptr && !hurt->creature.dead) {
        // The status of the row (the root of Gravity on a pull row), and the effects of the plan (the knockdown of Wrecking Ball).
        row_effects(*hurt, m_combat->damage_row(rowIndex), result.damage.dealt, attacker.id);
        PhasePlan effects;
        effects.effects = cast.plan.strikeEffects;
        if (Entity* again = m_world.creatures().find(targetId); again != nullptr && !again->creature.dead) {
            give_effects(cast, effects, *again);
        }
    }
    return result;
}

void Abilities::fragment_event(Entity& character, const SequenceEvent& event) {
    const auto found = m_casts.find(event.cast);
    if (found == m_casts.end() || found->second.caster != character.id || event.fragment == nullptr || !has_data()) {
        return;
    }
    Cast& cast = found->second;
    switch (event.kind) {
    case SequenceEvent::Kind::payment:
        return;
    case SequenceEvent::Kind::strike:
        if (event.index < event.fragment->strikes.size()) {
            follow_strike(cast, character, event.fragment->strikes[event.index]);
        }
        return;
    case SequenceEvent::Kind::cast:
        // A spell cast of the fragment (CAGE-CastSpell: the ground of Mighty Gavel), with its node conditions. Only for the abilities of
        // this part: the plans of stage 3.3a make the areas of their spells (Shockwave, Path of Destiny) and the throw of Gravity Well.
        if (event.index < event.fragment->casts.size() && needs_fragments(cast.plan.kind)) {
            const gamedata::FragmentCast& spell = event.fragment->casts[event.index];
            if (all_learned(character.skills, spell.needs) && none_learned(character.skills, spell.forbids)) {
                cast_spell_at(character, cast.number, spell.spell);
            }
        }
        return;
    case SequenceEvent::Kind::marker:
        // The spells of the action with this mark (an ability of this part).
        if (event.index < event.fragment->markers.size()) {
            const std::string& marker = event.fragment->markers[event.index].name;
            const ItemInstance* item = Combat::weapon_in_hand(character);
            const gamedata::WeaponRow* weapon = item != nullptr ? m_combat->weapon_of(item->itemId) : nullptr;
            const gamedata::WeaponActionSet* graph = weapon != nullptr ? m_world.combat().actions_of(*weapon) : nullptr;
            const gamedata::AbilityAction* entry = graph != nullptr ? graph->ability(cast.ability) : nullptr;
            if (entry == nullptr || !needs_fragments(cast.plan.kind)) {
                return; // A plan of stage 3.3a makes the spells of its marks (the areas, the throw of Gravity Well).
            }
            for (const gamedata::AbilityAction::Cast& spell : entry->casts) {
                if (spell.marker == marker && all_learned(character.skills, spell.needs) && none_learned(character.skills, spell.forbids)) {
                    cast_spell_at(character, cast.number, spell.spell);
                }
            }
        }
        return;
    }
}

void Abilities::follow_strike(Cast& cast, Entity& caster, const gamedata::AttackStrike& strike) {
    // A strike whose usable rows do no damage (the pull of Reap: the rows _Pull, _upgrade, _upgrade2 with the damage factor 0, the _RP2
    // rows with 0.0001): the client reports no contact for it (live check I1 of stage 3.3b-M2), thus the server tests its shapes itself.
    // It acts on each creature that a strike of the cast hit before, and on each creature in a shape of a usable row in front of the
    // caster (the capsule along the direction of the press: from its offset minus its half height and its radius to its offset plus them,
    // its radius to the side, and kShapeSlack for the body of the creature; live run of stage 3.3b-M2: with Fatal Attraction and The
    // Collector the first strike reaches 3.65 m and the pull 5.35 m). Each one gets the row of the shape nearest to it (the capsules of
    // the axe at 1.2 m, 2.7 m, and 4.3 m: the farther the creature, the longer its pull), at the time of the strike. A decision of an
    // agent from the data and the live checks.
    std::vector<const gamedata::StrikeRow*> rows;
    for (const gamedata::StrikeRow& row : strike.rows) {
        if (std::all_of(row.needs.begin(), row.needs.end(), [&](std::uint32_t node) { return has_learned(caster.skills, node); })
            && std::none_of(row.forbids.begin(), row.forbids.end(), [&](std::uint32_t node) { return has_learned(caster.skills, node); })) {
            rows.push_back(&row);
        }
    }
    if (rows.empty()) {
        return;
    }
    for (const gamedata::StrikeRow* row : rows) {
        const std::optional<std::uint32_t> index = m_combat->row_index(row->row);
        if (!index || m_combat->damage_row(*index).factor > 0.001F) {
            return; // A strike with damage: the contacts of the client are its hits.
        }
    }
    std::set<EntityId> targets = cast.struck;
    const std::array<float, 2> ahead = direction_of(cast.yaw);
    float reach = 0.0F;
    for (const gamedata::StrikeRow* row : rows) {
        if (row->shape < strike.shapes.size()) {
            const gamedata::AttackShape& shape = strike.shapes[row->shape];
            reach = std::max(reach, shape.offset[2] + shape.halfHeight + shape.radius);
        }
    }
    if (reach > 0.0F) {
        m_world.for_each_near(caster.position, reach + ability_rules::kShapeSlack, [&](Entity& entity) {
            if (entity.kind != EntityKind::creature || entity.creature.dead || entity.creature.immune()
                || std::fabs(entity.position.z - caster.position.z) > ability_rules::kShapeHeight) {
                return;
            }
            const float dx = entity.position.x - caster.position.x;
            const float dy = entity.position.y - caster.position.y;
            const float along = dx * ahead[0] + dy * ahead[1];
            const float side = std::fabs(dx * ahead[1] - dy * ahead[0]);
            for (const gamedata::StrikeRow* row : rows) {
                if (row->shape >= strike.shapes.size()) {
                    continue;
                }
                const gamedata::AttackShape& shape = strike.shapes[row->shape];
                const float extent = shape.halfHeight + shape.radius + ability_rules::kShapeSlack;
                if (along >= shape.offset[2] - extent && along <= shape.offset[2] + extent
                    && side <= shape.radius + ability_rules::kShapeSlack) {
                    // No pull through a wall: the sight from the chest of the caster to the creature (1.5 m and 1 m above the feet).
                    const state::WorldPosition from{caster.position.x, caster.position.y, caster.position.z + 1.5F};
                    const state::WorldPosition to{entity.position.x, entity.position.y, entity.position.z + 1.0F};
                    if (m_world.navigation().sees(from, to)) {
                        targets.insert(entity.id);
                    }
                    return;
                }
            }
        });
    }
    for (const EntityId id : targets) {
        Entity* creature = m_world.creatures().find(id);
        ++m_counts.followLooked;
        if (creature == nullptr || creature->creature.dead || creature->creature.immune()) {
            continue;
        }
        const float away = horizontal_distance(caster.position, creature->position);
        const gamedata::StrikeRow* best = rows.front();
        float nearest = std::numeric_limits<float>::max();
        for (const gamedata::StrikeRow* row : rows) {
            const float along = row->shape < strike.shapes.size() ? strike.shapes[row->shape].offset[2] : 0.0F;
            if (std::fabs(away - along) < nearest) {
                nearest = std::fabs(away - along);
                best = row;
            }
        }
        const std::optional<std::uint32_t> index = m_combat->row_index(best->row);
        if (!index) {
            continue;
        }
        const gamedata::DamageRow& row = m_combat->damage_row(*index);
        if (row.impact) {
            m_world.creatures().impact(*creature, *row.impact, caster.position, caster.position, row.id);
        }
        if (Entity* again = m_world.creatures().find(id); again != nullptr && !again->creature.dead) {
            row_effects(*again, row, row.factor > 0.0F ? row.factor : 0.0F, caster.id);
        }
        ++m_counts.follows;
        log::write("world: ability: %s of %s moves creature %016llx %.1f m away with the row %s", ability_name(cast.plan.kind), caster.name.c_str(),
                   static_cast<unsigned long long>(id), static_cast<double>(away), row.id.c_str());
    }
}

void Abilities::row_effects(Entity& creature, const gamedata::DamageRow& row, float dealt, EntityId source) {
    if (row.effects.empty() || creature.kind != EntityKind::creature || creature.creature.dead || !has_data()) {
        return;
    }
    if (row.effectOnlyOnDamage && !(dealt > 0.0F)) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    if (m_rowCooldowns.size() > 256) {
        for (auto entry = m_rowCooldowns.begin(); entry != m_rowCooldowns.end();) {
            entry = entry->second <= now ? m_rowCooldowns.erase(entry) : std::next(entry);
        }
    }
    for (const std::string& status : row.effects) {
        const gamedata::StatusRow* data = m_data->status(status);
        if (data == nullptr) {
            continue;
        }
        // ApplicationCooldown of the status row: one application for each creature in that time (Gravity: 1 s for each target).
        const float wait = data->cells.number("ApplicationCooldown");
        const auto key = std::make_pair(creature.id, data->id);
        if (wait > 0.0F) {
            const auto until = m_rowCooldowns.find(key);
            if (until != m_rowCooldowns.end() && now < until->second) {
                continue;
            }
            m_rowCooldowns[key] = now + milliseconds(wait);
        }
        const bool given = apply_status(creature, status, source);
        ++(given ? m_counts.effects : m_counts.effectsRefused);
        // The end status of the row at its end (OnEndStatusEffect: the root of Gravity after its hold of 0.33 s).
        const std::string_view after = data->cells.text("OnEndStatusEffect");
        if (given && !after.empty() && data->duration > 0.0F && m_data->status(after) != nullptr) {
            const EntityId id = creature.id;
            const std::string end(after);
            m_world.after(milliseconds(data->duration), [this, id, end, source](World& world) {
                if (Entity* again = world.creatures().find(id); again != nullptr && !again->creature.dead) {
                    ++(apply_status(*again, end, source) ? m_counts.effects : m_counts.effectsRefused);
                }
            });
        }
    }
}

void Abilities::count_hit(Entity& character, std::uint32_t ability, bool reset) {
    // Only an ability whose row has TrackHitCount (game data: Ability_Greataxe_Whirlwind): the client tests its hits with
    // IfAbilityNumHits (the action list of the great axe).
    const gamedata::AbilityRow* row = has_data() ? m_data->ability_row(ability_text(ability)) : nullptr;
    if (row == nullptr || row->cells.number("TrackHitCount") == 0.0F) {
        return;
    }
    auto& hits = character.combat.abilityHits;
    auto entry = std::find_if(hits.begin(), hits.end(), [&](const auto& item) { return item.first == ability; });
    if (entry == hits.end()) {
        if (hits.size() >= ability_rules::kTrackedAbilities) {
            return;
        }
        hits.emplace_back(ability, 0);
        entry = std::prev(hits.end());
    }
    const std::uint8_t count = reset ? 0 : static_cast<std::uint8_t>(std::min(entry->second + 1, 255));
    if (count == entry->second && !reset) {
        return;
    }
    entry->second = count;
    m_world.touch(character.id, ComponentKey::abilities);
}

void Abilities::rule_spell(Entity& character, std::string_view spell, std::string_view rule) {
    if (!has_data() || m_data->spell(spell) == nullptr || character.life.dead) {
        return;
    }
    ++m_counts.ruleSpells;
    log::write("world: ability: the rule %.*s of %s casts %.*s", static_cast<int>(rule.size()), rule.data(), character.name.c_str(),
               static_cast<int>(spell.size()), spell.data());
    cast_spell_at(character, 0, spell);
}

void Abilities::cast_spell_at(Entity& character, std::uint64_t cast, std::string_view spellId) {
    const gamedata::SpellRow* row = has_data() ? m_data->spell(spellId) : nullptr;
    if (row == nullptr) {
        return;
    }
    // The role of the spell from its row (game data; a decision of an agent): an area for friends with statuses (WarHammer_Spell_ClearOut_
    // Fortify: its StatusEffects on the friends in its Radius), a shield at its owner with no status (the projectile shield of Maelstrom:
    // AttachToOwner, TargetTypes Friend), else the picture of the spell (the ground of Mighty Gavel: no offense in its slice).
    SpellRequest request;
    request.spellId = row->id;
    request.caster = character.id;
    request.cast = cast;
    request.position = character.position;
    const bool attach = row->cells.number("AttachToOwner") != 0.0F;
    const bool friends = data_rules::same_text(row->cells.text("TargetTypes"), "Friend");
    for (const std::string_view part : data_rules::parts_of(row->cells.text("StatusEffects"))) {
        if (m_data->status(part) != nullptr) {
            request.statuses.emplace_back(part);
        }
    }
    request.life = std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(row->duration, 1.0F))));
    request.radius = row->radius;
    if (friends && !request.statuses.empty()) {
        request.role = SpellRole::aura;
        request.attach = attach ? character.id : 0;
    } else if (friends && attach) {
        request.role = SpellRole::shield;
        request.attach = character.id;
    } else {
        request.role = SpellRole::visual;
        if (const std::optional<float> ground = m_world.navigation().ground_height(request.position);
            ground && std::fabs(*ground - request.position.z) < 3.0F) {
            request.position.z = *ground;
        }
    }
    if (m_world.spells().make(request) != nullptr) {
        ++m_counts.fragmentSpells;
    }
}

} // namespace soulwarden::backend::world
