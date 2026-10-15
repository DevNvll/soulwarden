#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <set>
#include <string>
#include <string_view>
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
// (with the potency of the perk). The evaluator is the one place that reads the cells of these rows; the combat system asks it
// before the damage of each hit (Passives::before_hit) and gives it the result (Passives::after_hit), the health system gives it
// each hit that a character takes (hit_taken), and the world tick runs its rules of the entities near a character (tick).
//
//   Event      A cell On<event> that is not 0: OnHit, OnCrit, OnKill, OnHitTaken, OnHeadShot, OnHitBehind. A row with no event
//              and a condition of a hit (DamageTableRow, AttackType) is a rule of that hit (Crowded Well: the burst of Gravity
//              Well). A row with no event, no condition, and a value is a fixed stat. A row with no event, NumAroundMe, and a
//              SelfApplyStatusEffect is a status while the condition holds (Center of Attention).
//   Conditions The conditions of a perk row (Gear: AttackType, DamageTableRow, the health, stamina, and mana percents with the
//              comparator of the client, TargetStatusEffectCategory, DontHaveStatusEffect, InSequence) and InAction,
//              RequireReaction, DamageIsMelee, DamageIsRanged, the hit counts (TrackHitCount with NumberOfTrackedHits,
//              NumSuccessfulHits, NumConsecutiveHits), and NumAroundMe (the entities in the tracking spell of the rule).
//   Effects    BaseDamage, CritChance, CritDamage, ArmorPenetration, HeadshotDamage, HitFromBehindDamage (before the
//              damage); SelfApplyStatusEffect, OtherApplyStatusEffect, DmgPctToHealth, CooldownTimer with
//              CDRImmediatelyOptions (after the hit); the fixed stats through the gear values.
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
    /** A creature hit a character (Health::creature_hit, after the damage): the rules with OnHitTaken of the weapon in its hand. */
    void hit_taken(Entity& character);
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
        /** The rules near the character whose status it has now. */
        std::set<std::string> around;
        WorldClock::time_point nextAround{};
    };

    /** @return The event of a rule, or an empty text (no event). */
    [[nodiscard]] static std::string event_of(const gamedata::CellValues& cells);
    /** @return The columns of a rule that the evaluator does not have (no effect for the rule), and the unsupported events. */
    [[nodiscard]] static std::vector<std::string> unknown_columns(const gamedata::CellValues& cells);
    /** @return True when the hit counts of a rule pass for this hit; `spend` true counts the hit. */
    bool counts_pass(const PassiveRule& rule, const Entity& attacker, const HitContext& hit, bool spend);
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
