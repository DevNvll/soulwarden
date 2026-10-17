#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "gamedata/ability_data.h"
#include "gamedata/cell_values.h"
#include "world/entity.h"
#include "world/gear.h"
#include "world/world_clock.h"

// The evaluator of the passive rules of the data (stage 3.3b-S5). World thread only.
//
// A rule is one row of an ability table of the players (game data, tools/gamedata/abilities.sh format 4): a learned node of
// the tree of the weapon of a hit (the active abilities too: their OnHit rows), or an ability row of a perk of an equipped item
// (with the potency of the perk); from stage B2 also a helper row of a learned node and the row that a status of the character
// equips (EquipAbility: the tracker of Shirking Shock II). The evaluator is the one place that reads the cells of these rows; the combat system asks it
// before the damage of each hit (Passives::before_hit) and gives it the result (Passives::after_hit), the health system gives it
// each hit that a character takes (hit_taken), and the world tick runs its rules of the entities near a character (tick).
//
//   Event      A cell On<event> that is not 0: OnHit, OnCrit, OnKill, OnHitTaken, OnHeadShot, OnHitBehind, and from stage B3
//              OnWeaponSwap (the key of a weapon: weapon_changed). A row with no event and a condition of a hit (DamageTableRow,
//              AttackType) is a rule of that hit (Crowded Well: the burst of Gravity Well). A row with no event, no condition, and a
//              value is a fixed stat. A row with no event, NumAroundMe, and a SelfApplyStatusEffect is a status while the condition
//              holds (Center of Attention). Stage B3: a row with no event and AfterAction or AfterSequence of the dodge acts at the
//              end of a dodge (dodge_ended: Evasive Tactics); a row with no event and StatusEffectDurationReduction is a time factor
//              of a status on the character (duration_factor: Speedy, Alacrity II); the dodge status actions of the data give their
//              statuses at the end of a dodge (Dodge and Weave), and the tag removal actions end them when the weapon tag goes.
//   Conditions The conditions of a perk row (Gear: AttackType, DamageTableRow, the health, stamina, and mana percents with the
//              comparator of the client, TargetStatusEffectCategory, DontHaveStatusEffect, InSequence) and InAction,
//              RequireReaction, DamageIsMelee, DamageIsRanged, the hit counts (TrackHitCount with NumberOfTrackedHits,
//              NumSuccessfulHits, NumConsecutiveHits), and NumAroundMe (the entities in the tracking spell of the rule). From
//              stage B2: DamageTypes, TargetStatusEffect with its stack size and comparison (the stacks of each source on the
//              target before the hit, unless CheckStatusEffectsOnTargetOwned), and StatusEffect with its stack size and
//              comparison (the attacker). From stage B3: StatusEffectCategories (the attacker has a status of a category: Speedy).
//   Effects    BaseDamage, CritChance, CritDamage, ArmorPenetration, HeadshotDamage, HitFromBehindDamage (before the
//              damage); SelfApplyStatusEffect, OtherApplyStatusEffect, DmgPctToHealth, CooldownTimer with
//              CDRImmediatelyOptions, and from stage B2 RemoveTargetStatusEffectsList and RemoveStatusEffects (after the hit);
//              the fixed stats through the gear values.
//
// A row with a column that is neither a condition, an effect, nor a mark of the evaluator, or with an event that the backend
// does not have (a block, a heal, a weapon swap), has no effect: the list of the evaluator (Passives::describe, tools/world
// passives) names it with the cause. A node whose rule the action data has (no cell of a rule, a condition IfAbilityActive of
// an action list) is a rule of an action: the plan of the ability applies it when the backend has the ability.
//
// The values are values of the character with the character id as their key (rule MP-1): the activation cooldowns, the
// hits of one attack, and the consecutive hits. The cost: a hit looks at the learned nodes of one weapon tree (40 at most) and
// at the perks of one character (55 at most); the rules near a character look at the position index in the radius of their
// tracking spell, only for the characters with such a rule (test case PS6 has a time limit).

namespace soulwarden::backend::world {

class World;

namespace passive_rules {
/** The rules of the entities near a character run each this time (a decision of this project: the action data starts the tracking spell and has no time). */
inline constexpr std::chrono::milliseconds kAroundStep{500};
/** The hits of one attack are kept this long after its last hit (a bound of the cost: the longest attack of the data is shorter). */
inline constexpr std::chrono::milliseconds kAttackKeep{10000};
/** The kinds of the identity of an attack (HitContext::attack): the press of a basic attack, a cast, a projectile. */
inline constexpr std::uint64_t kAttackPress = 1ULL << 60;
inline constexpr std::uint64_t kAttackCast = 2ULL << 60;
inline constexpr std::uint64_t kAttackShot = 3ULL << 60;
} // namespace passive_rules

/** Where a rule comes from. */
enum class RuleOrigin : std::uint8_t {
    /** A learned node of a weapon tree that is not an active ability. */
    node,
    /** A learned active ability: its row has the rules of its hits (Executioner: BaseDamage at 50 percent health). */
    active,
    /** An ability row of a perk of an equipped item (with the potency of the perk). */
    perk,
    /**
     * Stage B2: a helper row of a learned node of the tree (RequiredEquippedAbilityId names the node, EquipWhenUnsheathed: the tracker of
     * Surprise Attack, Passive_Bow_Hunter_DmgOnFirst_Tracker). The client equips it with the node; it has no node of its own.
     */
    helper,
    /**
     * Stage B2: the row that a status of the character equips (EquipAbility of the status row, with the potency of the status: the tracker
     * of Shirking Shock II equips Ability_Perk_Weapon_DodgeSuccess_LightningDMG while it runs).
     */
    status,
};

/** One rule of a character for one hit. */
struct PassiveRule {
    std::string id;
    const gamedata::CellValues* cells{nullptr};
    float potency{1.0F};
    RuleOrigin origin{RuleOrigin::node};
};

/** The modifiers of one hit before its damage (Passives::before_hit). Parts: 0.1 is 10 percent. */
struct HitBonus {
    /** BaseDamage of the rules of the hit whose conditions pass (and HitFromBehindDamage for a hit from behind). */
    float damage{0.0F};
    /** The critical chance: the fixed parts, the parts of the rules of the hit (CritChance, also with OnCrit), and the statuses. */
    float critChance{0.0F};
    /** The critical damage: CritDamage of the rules, CritDamageModifier of the statuses, HeadshotDamage of a headshot. */
    float critDamage{0.0F};
    /**
     * The CritDamage of the rules with OnCrit (Vicious): it counts only when the hit is critical (code review of stage 3.3b-S5,
     * finding F3). A headshot takes the HeadshotDamage of such a row with OnHeadShot in `critDamage` instead (one cell for each row).
     */
    float critOnCrit{0.0F};
    /** ArmorPenetration: the part of the mitigation of the target that the hit does not meet. */
    float penetration{0.0F};
    /** The rules that the evaluator looked at (the load). */
    std::uint32_t looked{0};
};

/** How the evaluator takes one rule of the data (Passives::describe, for the list). */
struct RuleView {
    std::string id;
    RuleOrigin origin{RuleOrigin::node};
    /** The kinds of the rule ("fixed stat", "status on the caster", "status on the target", "change of an ability", ...). */
    std::vector<std::string> kinds;
    /** True: the evaluator (or the plan of the ability, for a rule of an action) applies the rule. */
    bool applied{false};
    /** What the rule does in the backend, or why it has no effect. */
    std::string note;
};

/** Counts of the evaluator, for the log, tools/world list, and the tests. */
struct PassiveCounts {
    std::uint64_t hits{0};
    std::uint64_t rulesLooked{0};
    std::uint64_t applied{0};
    std::uint64_t casterStatuses{0};
    std::uint64_t targetStatuses{0};
    std::uint64_t heals{0};
    std::uint64_t cooldownReductions{0};
    std::uint64_t aroundSteps{0};
    std::uint64_t aroundLooked{0};
    std::uint64_t aroundStatuses{0};
    std::uint64_t hitTakenRules{0};
    /** Rules with OnHitTakenWhileInvulnerable that acted (stage 3.4f-B). */
    std::uint64_t evadedRules{0};
    /**
     * Stage B3: the rules with OnWeaponSwap that acted (Archer's Speed), the statuses of the dodge status actions at the end of a dodge (Dodge
     * and Weave), the rules after the end of a dodge that acted (Evasive Tactics), and the statuses that a tag removal action ended.
     */
    std::uint64_t swapRules{0};
    std::uint64_t dodgeStatuses{0};
    std::uint64_t afterDodgeRules{0};
    std::uint64_t tagRemovals{0};
};

class Passives {
public:
    explicit Passives(World& world) : m_world(world) {}

    /**
     * @return The rules of a character for a hit with a weapon of this ability table (its progression id; 0: none): the learned
     *         nodes of the table with a row in the ability data, and the ability rows of its perks.
     */
    [[nodiscard]] std::vector<PassiveRule> rules_of(const Entity& character, std::uint32_t table) const;
    /** The modifiers of one hit of a character on a creature before its damage. The conditions see the target before the hit. */
    [[nodiscard]] HitBonus before_hit(const Entity& attacker, const Entity& target, const HitContext& hit) const;
    /** A hit of a character was accepted (Combat::strike): the effects of the rules whose events and conditions pass. */
    void after_hit(Entity& attacker, Entity& target, const HitContext& hit);
    /**
     * A creature hit a character (Health::creature_hit, after the damage `dealt`): the rules with OnHitTaken of the weapon in its hand
     * and of its perks, with the conditions that a hit taken has a source for (code review of stage 3.3b-S5, finding F6).
     */
    void hit_taken(Entity& character, float dealt);
    /**
     * A creature hit came in the immune window of the dodge of a character (stage 3.4f-B, Stamina::evaded): the rules with
     * OnHitTakenWhileInvulnerable of the weapon in its hand and of its perks (Shirking Fortification: the fortify of the armor perks),
     * with the conditions that such a hit has a source for (its values, its statuses, its load class, the dodge as the action and its
     * fragment as the sequence, RequireReaction: the damage that the hit would do above 0).
     */
    void hit_evaded(Entity& character, float wouldDeal);
    /**
     * A dodge of a character started or reached its mark CanTransition (stage 3.4f-B): the status actions of the character look at it in
     * the next tick of the evaluator (the tag Dodging), not after the step of `passive_rules::kAroundStep`.
     */
    void recheck(const std::string& characterId);
    /**
     * @return The outgoing part of a heal of a healer from its rules (stage 3.3b-S6): the sum of the HealScalingValueMultiplier of each
     *         rule with OnHealed of the tree of the weapon of the heal (HealSource::table) and of the perks whose conditions pass for
     *         the target (its health before the heal, the row of the spell, the categories of its statuses), times the potency.
     */
    [[nodiscard]] float heal_bonus(const Entity& healer, const Entity& target, const HealSource& source) const;
    /**
     * Stage B3: the weapon in the hand of a character changed (`before`: the weapon row before the change, or null). The tag removal actions of
     * the data end their statuses when a tag of the weapon before goes (Bow_Remove_StatusEffects_Sheathe: the haste of Dodge and Weave ends
     * when the bow leaves the hand). `swap` true: a change of the weapon slot with the key of a weapon (Combat::change_weapon): the rules with
     * OnWeaponSwap of the tree of the new weapon and of the perks (Archer's Speed), with the conditions that a swap has a source for.
     */
    void weapon_changed(Entity& character, const gamedata::WeaponRow* before, bool swap);
    /**
     * Stage B3: a dodge action of a character starts (a dodge while no dodge of the character plays; a next dodge of a cancel window continues
     * the action). The start tests of the dodge status actions of the data (Passive_Bow_Skirmish_HasteOnDodge: the node, the tag of the weapon
     * in the hand, the stamina before the payment as a part of the maximum, the statuses that the character must not have) are read now.
     */
    void dodge_started(Entity& character);
    /**
     * Stage B3: the dodge action of a character ended (the mark CanTransition of its last dodge, `fragment`): the statuses of the dodge status
     * actions whose start tests passed and whose exit tags hold (the bow still in the hand), and the rules with AfterAction and AfterSequence of
     * the dodge and no event (Evasive Tactics).
     */
    void dodge_ended(Entity& character, std::string_view fragment);
    /** Stage B3: the dodge action of a character ended with no exit (a death, a travel, a leave, a new entry): its start tests are forgotten. */
    void dodge_cancelled(const std::string& characterId);
    /**
     * Stage B3: @return The time factor of a status row on a character: 1 plus, for each rule of the character with StatusEffectDurationReduction
     *         and no event whose StatusEffect names the status or whose StatusEffectCategories has a category of the status, and whose own
     *         conditions pass with the new status counted as present, the value times the potency (client getter 0x68308E0 sums them: Speedy
     *         0.5 and Alacrity II 0.094 x 1.274 give 1.62). Not below 0.
     */
    [[nodiscard]] float duration_factor(const Entity& character, const gamedata::StatusRow& row) const;
    /** Each world tick: the rules of the entities near a character (a status while NumAroundMe holds), each kAroundStep. */
    void tick();
    /** A character leaves the world: the statuses of its rules near it end, and its values of the evaluator go. */
    void left(Entity& character);

    /** @return How the evaluator takes each rule of a character: the learned nodes of its weapon trees, and its perks. */
    [[nodiscard]] std::vector<RuleView> describe(const Entity& character) const;
    /** @return How the evaluator takes each row of an ability table (its progression id): the rules of a whole tree. */
    [[nodiscard]] std::vector<RuleView> describe_table(std::uint32_t table) const;
    /** @return How the evaluator takes one row of the data (a node of a table, an active ability, or a perk). */
    [[nodiscard]] RuleView describe_row(const std::string& id, const gamedata::CellValues* cells, RuleOrigin origin) const;

    [[nodiscard]] const PassiveCounts& counts() const {
        return m_counts;
    }
    /** @return True for a fixed stat: a row with no event, no condition, and only columns that the evaluator has (Gear::values). */
    [[nodiscard]] static bool fixed(const gamedata::CellValues& cells);
    /**
     * Stage B3: @return True for a value of a hit with a condition of a status of the character and no event (StatusEffect or
     *         StatusEffectCategories, only the conditions of a rule with no hit, only values of a hit: the form of BonusDamageSelfCCPerk1). The
     *         evaluator gives its values to each hit while the condition holds; the gear values do not take it.
     */
    [[nodiscard]] static bool conditional_value(const gamedata::CellValues& cells);

    /**
     * @return True when the conditions of a rule pass for a hit (no hit count is spent). The tests of the comparator use it.
     * @param target The creature as it was before the hit.
     */
    [[nodiscard]] bool conditions_pass(const PassiveRule& rule, const Entity& attacker, const Entity& target, const HitContext& hit) const;

private:
    /** The values of one character: the hits of each attack for each rule, the consecutive hits, and the statuses of its rules near it. */
    struct CharacterState {
        /** (rule, attack) to the number of hits of the attack that passed the other conditions, and the time of the last one. */
        std::map<std::pair<std::string, std::uint64_t>, std::pair<std::uint32_t, WorldClock::time_point>> attackHits;
        /** (rule, attack) whose NumSuccessfulHits fired: one time for each attack. */
        std::set<std::pair<std::string, std::uint64_t>> fired;
        /** Rule to the consecutive hits and the last attack. */
        std::map<std::string, std::pair<std::uint32_t, std::uint64_t>> consecutive;
        /**
         * Stage B1: (rule, volley, target) to the attacks of the volley that hit the target (the arrows of one cast of Rapid Shot: decision
         * D11), and the time of the last one.
         */
        std::map<std::tuple<std::string, std::uint64_t, EntityId>, std::pair<std::set<std::uint64_t>, WorldClock::time_point>> volleyHits;
        /** The rules near the character whose status it has now. */
        std::set<std::string> around;
        WorldClock::time_point nextAround{};
        /** Stage B3: the dodge status actions (indexes of actionDodgeStatuses) whose start tests passed in the dodge action that plays. */
        std::vector<std::size_t> dodgeActions;
    };

    /** Stage B2: @return The rows that the statuses of a character equip (EquipAbility), with the potency of each status. */
    [[nodiscard]] std::vector<PassiveRule> status_rules(const Entity& character) const;
    /**
     * Stage B2: @return True when the target status conditions of a row pass (TargetStatusEffect with TargetStatusEffectStackSize and
     *         TargetStatusEffectComparison, TargetStatusEffectCategory, CheckStatusEffectsOnTargetOwned; client hit test 0x67A18F0), with the
     *         effects of the target before the hit.
     */
    [[nodiscard]] bool target_status_pass(const gamedata::CellValues& row, const Entity& attacker, const Entity& target, const HitContext& hit) const;
    /**
     * Stage B2: @return True when the condition StatusEffect (with StatusEffectStackSize, StatusEffectComparison) of a row passes (client
     *         0x67A0130). Stage B3: also StatusEffectCategories (the character has a status of a category of the list; StatusEffectComparison
     *         NotEqual: none). `adding`: a status that comes now and counts as present (the time factor of a new haste).
     */
    [[nodiscard]] bool own_status_pass(const gamedata::CellValues& row, const Entity& character, const gamedata::StatusRow* adding = nullptr) const;
    /**
     * Stage B3: @return True when the conditions of a rule with no hit pass for a character (a swap, the end of a dodge, a time factor): its
     *         values (health, stamina, mana), grit (none), DontHaveStatusEffect, EquipLoadCategory, StatusEffect, StatusEffectCategories.
     */
    [[nodiscard]] bool self_pass(const PassiveRule& rule, const Entity& character, const gamedata::StatusRow* adding = nullptr) const;
    /** Stage B3: the effects of a rule with no hit on its character: a cooldown change, RemoveStatusEffects, SelfApplyStatusEffect. */
    void self_effects(const PassiveRule& rule, Entity& character);
    /** The cooldown change of a rule (CooldownTimer with CDRImmediatelyOptions; stage B3: one function for a hit and a rule with no hit). */
    void cooldown_effect(const PassiveRule& rule, Entity& character);
    /** RemoveStatusEffects of a rule on its character (stage B2; stage B3: one function for a hit and a rule with no hit). */
    void remove_own(const PassiveRule& rule, Entity& character);
    /** Stage B3: @return The progression id of the ability table of the weapon in the hand of a character (0: none). */
    [[nodiscard]] std::uint32_t table_in_hand(const Entity& character) const;
    /** @return The event of a rule, or an empty text (no event). */
    [[nodiscard]] static std::string event_of(const gamedata::CellValues& cells);
    /** @return The columns of a rule that the evaluator does not have (no effect for the rule), and the unsupported events. */
    [[nodiscard]] static std::vector<std::string> unknown_columns(const gamedata::CellValues& cells);
    /** @return True when the hit counts of a rule pass for this hit; `spend` true counts the hit. */
    bool counts_pass(const PassiveRule& rule, const Entity& attacker, const HitContext& hit, bool spend);
    /** @return The result of counts_pass for this hit with nothing spent (the values before the damage; finding F8). */
    [[nodiscard]] bool counts_peek(const PassiveRule& rule, const Entity& attacker, const HitContext& hit) const;
    /** The rule of the hit counts on the values of one character. */
    bool count_rule(const PassiveRule& rule, CharacterState& state, const HitContext& hit, bool spend) const;
    /** @return True when a rule has a hit count (TrackHitCount with NumberOfTrackedHits, NumSuccessfulHits, NumConsecutiveHits). */
    [[nodiscard]] static bool has_count_gate(const gamedata::CellValues& row);
    /** @return The number of the entities in the tracking spell of a rule near a character (or the creatures of the area of the hit). */
    [[nodiscard]] std::uint32_t around_count(const gamedata::CellValues& cells, std::string_view rule, const Entity& character,
                                             const HitContext* hit) const;
    /** The effects of a rule after a hit. */
    void apply_effects(const PassiveRule& rule, Entity& attacker, Entity& target, const HitContext& hit);
    /** The status of a rule on a creature (OtherApplyStatusEffect). */
    void target_status(const PassiveRule& rule, Entity& attacker, Entity& target, std::string_view status, const HitContext& hit);
    /** @return True when the ActivationCooldown of a rule of a character ended; `start` starts it. */
    bool activation_ready(const std::string& characterId, const PassiveRule& rule, bool start);

    World& m_world;
    std::unordered_map<std::string, CharacterState> m_characters;
    /** The end of the ActivationCooldown of each rule of each character (the character id and the rule id). */
    std::unordered_map<std::string, WorldClock::time_point> m_activations;
    WorldClock::time_point m_nextSweep{};
    PassiveCounts m_counts;
};

} // namespace soulwarden::backend::world
