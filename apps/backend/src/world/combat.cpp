#include "world/combat.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** @return The number of input steps from one input number to a later one (16 bits, with wrap). */
unsigned steps_between(std::uint16_t from, std::uint16_t to) {
    return static_cast<std::uint16_t>(to - from);
}

float distance(const state::WorldPosition& a, const state::WorldPosition& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z));
}

} // namespace

const char* hit_verdict_text(HitVerdict verdict) {
    switch (verdict) {
    case HitVerdict::accepted:
        return "accepted";
    case HitVerdict::noEntity:
        return "no entity";
    case HitVerdict::noTables:
        return "no combat tables";
    case HitVerdict::tooMany:
        return "too many claims";
    case HitVerdict::notCreature:
        return "not a creature";
    case HitVerdict::badReference:
        return "not a component of the creature";
    case HitVerdict::notObserved:
        return "not observed";
    case HitVerdict::dead:
        return "dead";
    case HitVerdict::noWeapon:
        return "no melee weapon";
    case HitVerdict::noAttack:
        return "no attack";
    case HitVerdict::sameAttack:
        return "the same attack";
    case HitVerdict::tooFar:
        return "too far";
    case HitVerdict::badPoint:
        return "bad point";
    case HitVerdict::attackerDead:
        return "the character is dead";
    }
    return "unknown";
}

bool AttackTrack::newer(std::uint16_t input) const {
    if (!m_hasInput) {
        return true;
    }
    const unsigned ahead = steps_between(m_lastInput, input);
    return ahead >= 1 && ahead <= combat_limits::kNewerRange;
}

bool AttackTrack::restarted(std::uint16_t input, WorldClock::time_point now) const {
    return !newer(input) && now - m_lastStep >= combat_limits::kSequenceRestart;
}

void AttackTrack::step(std::uint16_t input, bool press, bool hold, bool release, WorldClock::time_point now) {
    if (m_hasInput && !newer(input)) {
        // The input numbers of the client started again: an attack input of the old sequence
        // fits no contact of the new one, and an input number of the old sequence cannot be
        // compared with one of the new sequence.
        m_current.reset();
        m_previous.reset();
        ++m_sequence;
        m_pressed = false;
    }
    m_hasInput = true;
    m_lastInput = input;
    m_lastStep = now;
    const auto end = [&](Press& attack) {
        attack.on = false;
        attack.endInput = input;
        attack.ended = now;
    };
    if (press && !m_pressed) {
        if (m_current && m_current->on) {
            end(*m_current);
        }
        m_previous = m_current;
        Press attack;
        attack.number = ++m_presses;
        attack.input = input;
        attack.started = now;
        m_current = attack;
    } else if (m_current && m_current->on) {
        // The attack input is on from the step after its start to its release.
        if (hold && !release) {
            m_current->holdSteps = steps_between(m_current->input, input);
        } else {
            end(*m_current);
        }
    }
    m_pressed = press;
}

const AttackTrack::Press* AttackTrack::press_for(std::uint16_t input) const {
    for (const std::optional<Press>* attack : {&m_current, &m_previous}) {
        if (*attack) {
            const unsigned steps = steps_between((*attack)->input, input);
            if (steps >= combat_limits::kContactStepsMin && steps <= combat_limits::kNewerRange) {
                return &**attack;
            }
        }
    }
    return nullptr;
}

bool HitLimits::same_attack(EntityId target, std::uint16_t input, SessionId session, std::uint32_t sequence,
                            WorldClock::time_point now) const {
    for (const Hit& hit : hits) {
        if (hit.target != target) {
            continue;
        }
        // The time is a value of the world clock: it applies to each session of the character.
        // An input number is a value of one client: it applies only in its session and sequence.
        if (now - hit.time < combat_limits::kSameAttackTime
            || (hit.session == session && hit.sequence == sequence
                && steps_between(hit.input, input) < combat_limits::kSameAttackSteps)) {
            return true;
        }
    }
    return false;
}

void HitLimits::note_hit(const Hit& hit) {
    while (!hits.empty() && hit.time - hits.front().time >= combat_limits::kClaimWindow) {
        hits.pop_front();
    }
    hits.push_back(hit);
}

bool HitLimits::idle(WorldClock::time_point now) const {
    return (claims.empty() || now - claims.back() >= combat_limits::kClaimWindow)
           && (hits.empty() || now - hits.back().time >= combat_limits::kClaimWindow);
}

std::uint64_t AttackTrack::refused() const {
    std::uint64_t total = 0;
    for (std::size_t verdict = 0; verdict < m_counts.size(); ++verdict) {
        if (verdict != static_cast<std::size_t>(HitVerdict::accepted)) {
            total += m_counts[verdict];
        }
    }
    return total;
}

bool HitLimits::admit(WorldClock::time_point now) {
    while (!claims.empty() && now - claims.front() >= combat_limits::kClaimWindow) {
        claims.pop_front();
    }
    if (claims.size() >= combat_limits::kClaimsInWindow) {
        return false;
    }
    claims.push_back(now);
    return true;
}

Combat::Combat(World& world, const gamedata::CombatTables* tables) : m_world(world), m_tables(tables) {}

HitLimits& Combat::limits_of(const std::string& characterId) {
    /** With more characters than this in the table, the entries with no value in a time window go away. */
    constexpr std::size_t kSweepSize = 64;
    if (m_limits.size() >= kSweepSize && m_limits.count(characterId) == 0) {
        const WorldClock::time_point now = m_world.clock().now();
        for (auto entry = m_limits.begin(); entry != m_limits.end();) {
            entry = entry->second.idle(now) ? m_limits.erase(entry) : std::next(entry);
        }
    }
    return m_limits[characterId];
}

const ItemInstance* Combat::weapon_in_hand(const Entity& attacker) {
    const Equipment& equipment = attacker.equipment;
    if (attacker.combat.selectedWeapon != 0) {
        if (const ItemInstance* item = equipment.item(attacker.combat.selectedWeapon)) {
            return item;
        }
    }
    const auto slot = equipment.active_weapon();
    return slot ? equipment.item(*slot) : nullptr;
}

void Combat::select_weapon(SessionId session, unsigned key) {
    Entity* entity = m_world.entity_of(session);
    if (entity == nullptr || key < 1 || key > paperdoll::kMainHand3 - paperdoll::kMainHand1 + 1) {
        return;
    }
    const std::uint32_t slot = paperdoll::kMainHand1 + key - 1;
    if (entity->equipment.item(slot) != nullptr) {
        entity->combat.selectedWeapon = slot;
    }
}

Damage Combat::damage(const Entity& attacker,
                      const gamedata::WeaponRow& weapon,
                      std::uint32_t gearScore,
                      const gamedata::DamageRow& row,
                      const gamedata::DefenseRow* defense) const {
    const gamedata::CombatConstants& constants = m_tables->constants();
    const gamedata::DamageType& type = m_tables->damage_type(row.type);
    Damage damage;
    damage.type = type.wire;
    // The gear score: one factor for each full interval above the lowest gear score, and a
    // smaller one for each full interval above the limit (client code at image RVA 0x3691470).
    const float rate = weapon.gearScoreRate > 0.0F ? weapon.gearScoreRate : constants.gearScoreRate;
    const std::uint32_t lower = std::min(gearScore, constants.gearScoreLimit);
    const std::uint32_t first = lower > constants.gearScoreMinimum ? (lower - constants.gearScoreMinimum) / constants.gearScoreInterval : 0;
    const std::uint32_t second = gearScore > constants.gearScoreLimit ? (gearScore - constants.gearScoreLimit) / constants.gearScoreInterval : 0;
    damage.weapon = weapon.baseDamage * std::pow(1.0F + rate, static_cast<float>(first))
                    * std::pow(1.0F + rate * constants.gearScoreSmallerRate, static_cast<float>(second));
    // The attributes and the level of the attacker (client code at image RVA 0x5BFB530).
    float bonus = 0.0F;
    for (std::size_t attribute = 0; attribute < weapon.scaling.size(); ++attribute) {
        if (weapon.scaling[attribute] > 0.0F) {
            bonus += weapon.scaling[attribute]
                     * m_tables->attribute_damage(attribute, attacker.attributes.value(static_cast<unsigned>(attribute)));
        }
    }
    const std::uint32_t level = std::max<std::uint32_t>(attacker.progression.level, 1);
    bonus += constants.levelDamage * static_cast<float>(level - 1);
    damage.core = damage.weapon * (1.0F + bonus);
    damage.raw = damage.core * row.factor + row.added;
    // The defense of the target (a rule of this project).
    damage.dealt = damage.raw;
    if (defense != nullptr) {
        const float mitigation = type.category == gamedata::DamageCategory::physical    ? defense->physical
                                 : type.category == gamedata::DamageCategory::elemental ? defense->elemental
                                                                                        : 0.0F;
        // A value of the game tables can be above 1 (a creature that takes no damage of a
        // category): a factor is not less than 0.
        damage.dealt = damage.raw * std::max(1.0F - mitigation, 0.0F) * std::max(1.0F + defense->weakness[row.type], 0.0F)
                       * std::max(1.0F - defense->absorption[row.type], 0.0F);
    }
    if (!std::isfinite(damage.dealt) || damage.dealt < 0.0F) {
        damage.dealt = 0.0F;
    }
    return damage;
}

float Combat::armor_of(const Entity& character, gamedata::DamageCategory category) const {
    if (m_tables == nullptr || category == gamedata::DamageCategory::direct) {
        return 0.0F;
    }
    const gamedata::ArmorConstants& constants = m_tables->armor_constants();
    const auto one_decimal = [](float value) { return std::round(value * 10.0F) / 10.0F; };
    float armor = 0.0F;
    for (std::uint32_t slot = paperdoll::kHead; slot <= paperdoll::kFeet; ++slot) {
        const ItemInstance* item = character.equipment.item(slot);
        const gamedata::ArmorRow* row = item != nullptr ? m_tables->armor_of(item->itemId) : nullptr;
        if (row == nullptr) {
            continue;
        }
        // The armor of one item (client code at image RVA 0x5BFB400): a power of its gear
        // score times its factor for the category, then times its rating factor.
        const float factor = category == gamedata::DamageCategory::physical ? row->physical : row->elemental;
        const float scaled = one_decimal(std::pow(static_cast<float>(item->gearScore), constants.ratingExponent) * factor);
        armor += one_decimal(row->rating * scaled);
    }
    return armor;
}

CreatureDamage Combat::creature_damage(const Entity& creature,
                                       const gamedata::CreatureContact& contact,
                                       const Entity& target) const {
    CreatureDamage damage;
    const gamedata::CreatureData* data = m_world.creatures().data();
    if (data == nullptr || creature.creature.kind == nullptr) {
        return damage;
    }
    damage.base = data->damage(*creature.creature.kind, creature.creature.level);
    damage.raw = damage.base * contact.factor + contact.added;
    if (m_tables != nullptr) {
        // The level difference: a creature that is some levels above a character does more damage.
        const auto difference = static_cast<std::int64_t>(creature.creature.level)
                                - static_cast<std::int64_t>(std::max<std::uint32_t>(target.progression.level, 1));
        damage.levelFactor = 1.0F + m_tables->level_damage(difference);
        damage.armor = armor_of(target, contact.category);
        const float gearScore = data->gear_score(creature.creature.level);
        if (damage.armor > 0.0F && gearScore > 0.0F) {
            const float against = std::pow(gearScore, m_tables->armor_constants().mitigationExponent);
            damage.mitigation = damage.armor / (damage.armor + against);
        }
    }
    damage.dealt = damage.raw * std::max(damage.levelFactor, 0.0F) * std::max(1.0F - damage.mitigation, 0.0F);
    if (!std::isfinite(damage.dealt) || damage.dealt < 0.0F) {
        damage.dealt = 0.0F;
    }
    return damage;
}

HitResult Combat::claim(SessionId session, const HitClaim& claim, AttackTrack& track) {
    HitResult result;
    const auto refuse = [&](HitVerdict verdict) {
        result.verdict = track.counted(verdict);
        return result;
    };
    Entity* attacker = m_world.entity_of(session);
    if (attacker == nullptr) {
        return refuse(HitVerdict::noEntity);
    }
    if (!has_tables()) {
        return refuse(HitVerdict::noTables);
    }
    if (attacker->life.dead) {
        return refuse(HitVerdict::attackerDead);
    }
    const WorldClock::time_point now = m_world.clock().now();
    // The limits are the limits of the character: a new session of it has the same ones.
    HitLimits& limits = limits_of(attacker->characterId);
    if (!limits.admit(now)) {
        return refuse(HitVerdict::tooMany);
    }
    // The target: the world finds it from the reference of the contact. A reference is two
    // ids and a facet target: each one must be a value of this creature.
    Entity* target = m_world.creatures().find(claim.targetId);
    if (target == nullptr || target->actor != claim.target) {
        return refuse(HitVerdict::notCreature);
    }
    if (claim.groupId != claim.targetId || claim.group != claim.target
        || target->creature.body->find(target->id, claim.facet) == nullptr) {
        return refuse(HitVerdict::badReference);
    }
    if (!claim.observed) {
        return refuse(HitVerdict::notObserved);
    }
    if (target->creature.dead) {
        return refuse(HitVerdict::dead);
    }
    // The attack input of the contact: it is on, or it went off a short time before.
    const AttackTrack::Press* attack = track.press_for(claim.input);
    if (attack == nullptr) {
        return refuse(HitVerdict::noAttack);
    }
    if (!attack->on) {
        const unsigned steps = static_cast<std::uint16_t>(claim.input - attack->endInput);
        const auto oldest = std::chrono::milliseconds(combat_limits::kReleaseStepsMax * 1000 / combat_limits::kStepsInSecond)
                            + combat_limits::kReleaseSlack;
        if (steps > combat_limits::kReleaseStepsMax || now - attack->ended > oldest) {
            return refuse(HitVerdict::noAttack);
        }
    }
    result.kind = attack->holdSteps >= combat_limits::kHeavyHoldSteps ? AttackKind::heavy : AttackKind::light;
    // The weapon in the hand of the character, and the damage row of the attack.
    const ItemInstance* item = weapon_in_hand(*attacker);
    const gamedata::WeaponRow* weapon = item != nullptr ? m_tables->weapon_of(item->itemId) : nullptr;
    if (weapon == nullptr || !weapon->melee) {
        return refuse(HitVerdict::noWeapon);
    }
    const std::optional<std::uint32_t> rowIndex = result.kind == AttackKind::heavy ? weapon->heavyRow : weapon->lightRow;
    if (!rowIndex) {
        return refuse(HitVerdict::noWeapon);
    }
    if (limits.same_attack(target->id, claim.input, session, track.sequence(), now)) {
        return refuse(HitVerdict::sameAttack);
    }
    // The distance, with the exact position of the character that the world has.
    if (distance(attacker->position, target->position) > combat_limits::kMeleeReach + combat_limits::kReachTolerance) {
        return refuse(HitVerdict::tooFar);
    }
    if (!valid_position(claim.point) || distance(claim.point, target->position) > combat_limits::kPointRange) {
        return refuse(HitVerdict::badPoint);
    }
    const gamedata::DamageRow& row = m_tables->damage_row(*rowIndex);
    result.damage = damage(*attacker, *weapon, item->gearScore, row, m_tables->defense_of(target->creature.kind->vitals));
    result.healthBefore = target->vitals.health;
    limits.note_hit(HitLimits::Hit{target->id, now, claim.input, session, track.sequence()});
    const EntityId targetId = target->id;
    const ActorId targetActor = target->actor;
    const CreatureResult hurt = m_world.creatures().hurt(targetId, result.damage.dealt, attacker->id);
    if (hurt.error != CreatureError::none || hurt.entity == nullptr) {
        return refuse(HitVerdict::dead);
    }
    result.healthAfter = hurt.entity->vitals.health;
    result.killed = hurt.entity->creature.dead;
    result.verdict = track.counted(HitVerdict::accepted);
    log::write("world: hit: %s hurts creature %016llx (%s) with %s (%s attack, row %s): %.1f damage of the type %s (weapon "
               "%.1f, with attributes and level %.1f, before the defense %.1f), health %.1f to %.1f%s",
               attacker->name.c_str(),
               static_cast<unsigned long long>(targetId),
               hurt.entity->creature.kind->id.c_str(),
               item->itemId.c_str(),
               result.kind == AttackKind::heavy ? "heavy" : "light",
               row.id.c_str(),
               result.damage.dealt,
               m_tables->damage_type(row.type).name.c_str(),
               result.damage.weapon,
               result.damage.core,
               result.damage.raw,
               result.healthBefore,
               result.healthAfter,
               result.killed ? ": dead" : "");
    DamageDealt dealt;
    dealt.target = targetId;
    dealt.targetActor = targetActor;
    dealt.type = result.damage.type;
    dealt.amount = result.damage.dealt;
    dealt.killed = result.killed;
    m_world.damage_dealt(attacker->owner, dealt);
    return result;
}

} // namespace soulwarden::backend::world
