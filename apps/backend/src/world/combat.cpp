#include "world/combat.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

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
    case HitVerdict::immune:
        return "the creature is on its way back (immune)";
    case HitVerdict::noStrike:
        return "no strike of the attack fits the contact";
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

void AttackTrack::step(std::uint16_t input, bool press, bool hold, bool release, WorldClock::time_point now, std::string_view weapon) {
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
        attack.weapon = std::string(weapon);
        m_current = attack;
    } else if (m_current && m_current->on) {
        // The attack input is on from the step after its start to its release.
        if (hold && !release) {
            m_current->holdSteps = steps_between(m_current->input, input);
        } else {
            end(*m_current);
        }
    }
    // The release bit of the newest attack input (stage 3.3b-S3): also in the step of its start (a press of one step).
    if (release && m_current && !m_current->released) {
        m_current->released = true;
        m_current->releaseInput = input;
        m_current->releasedAt = now;
    }
    // A different weapon in the hand after the press (code review of stage 3.3b-S3, finding F3): the press fits no shot. The
    // change counts also after a change back: the client starts a new attack input for the new weapon.
    for (std::optional<Press>* attack : {&m_current, &m_previous}) {
        if (*attack && !weapon.empty() && (*attack)->weapon != weapon) {
            (*attack)->weaponChanged = true;
        }
    }
    m_pressed = press;
}

void AttackTrack::mark_shot(std::uint32_t number) {
    for (std::optional<Press>* attack : {&m_current, &m_previous}) {
        if (*attack && (*attack)->number == number) {
            (*attack)->shot = true;
        }
    }
}

const AttackTrack::Press* AttackTrack::press_at(std::uint16_t input) const {
    for (const std::optional<Press>* attack : {&m_current, &m_previous}) {
        if (*attack && steps_between((*attack)->input, input) <= combat_limits::kNewerRange) {
            return &**attack;
        }
    }
    return nullptr;
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

Combat::Combat(World& world, const gamedata::CombatTables* tables, const gamedata::WeaponActions* actions)
    : m_world(world), m_tables(tables), m_actions(actions) {}

const gamedata::WeaponActionSet* Combat::actions_of(const gamedata::WeaponRow& weapon) const {
    // The data can be set after the start of the world (a test): `loaded` at each use.
    return m_actions != nullptr && m_actions->loaded() && weapon.melee ? m_actions->of_tags(weapon.tags) : nullptr;
}

const gamedata::StrikeRow* Combat::row_for(const Entity& attacker, const gamedata::AttackStrike& strike) {
    const auto learned = [&](std::uint32_t node) {
        for (const auto& [pool, nodes] : attacker.skills.pools) {
            const auto found = nodes.find(node);
            if (found != nodes.end() && found->second.level != 0) {
                return true;
            }
        }
        return false;
    };
    for (const gamedata::StrikeRow& row : strike.rows) {
        if (std::all_of(row.needs.begin(), row.needs.end(), learned) && std::none_of(row.forbids.begin(), row.forbids.end(), learned)) {
            return &row;
        }
    }
    return nullptr;
}

void Combat::step(SessionId session, std::uint32_t inputSequence, std::uint16_t input, const AttackBits& bits) {
    Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return;
    }
    const ItemInstance* item = weapon_in_hand(*character);
    const gamedata::WeaponRow* weapon = item != nullptr && has_tables() ? m_tables->weapon_of(item->itemId) : nullptr;
    const gamedata::WeaponActionSet* actions = weapon != nullptr ? actions_of(*weapon) : nullptr;
    if (character->life.dead) {
        // A dead character does no damage: its attack ends, and its steps play nothing.
        m_sequences.end(character->characterId, SequenceEnd::death);
        return;
    }
    const std::vector<SequenceEvent> events =
        m_sequences.step(character->characterId, session, inputSequence, input, bits, weapon_key(*character), actions, m_world.clock().now(),
                         facts_of(*character));
    // The payments of the fragments (PayStaminaCost, CAGE-PayManaCost). The backend has no stamina model (the stamina rows of the
    // basic attacks of the reachable graphs are 0); a payment with a condition is a rule of a stance (the greatsword, a later
    // part). A mana payment with a row of the mana table goes through the one mana path. The strikes, the spell casts, and the marks of
    // the fragment of an ability go to the abilities system (stage 3.3b-M2).
    for (const SequenceEvent& event : events) {
        if (event.kind != SequenceEvent::Kind::payment) {
            run_event(character->characterId, session, event);
            continue;
        }
        const gamedata::AttackPayment* payment = &event.fragment->payments[event.index];
        if (!payment->condition.empty()) {
            m_sequences.count_payment(2);
        } else if (!payment->mana) {
            m_sequences.count_payment(0);
        } else {
            m_sequences.count_payment(1);
            const float cost = m_world.abilities().mana_cost_of(*character, payment->cost) * payment->multiplier;
            if (cost > 0.0F) {
                m_world.abilities().change_mana(*character, -std::min(cost, character->vitals.mana), "the payment of a basic attack");
            }
        }
    }
}

void Combat::run_event(const std::string& characterId, SessionId session, const SequenceEvent& event) {
    // An item of the fragment of an ability runs at its due time on the world clock (code review of stage 3.3b-M2, finding M2-R1), with
    // the tolerance of a contact (one input step and attack_rules::kWorldEarly, finding M1-R1): an input number that comes before that (a
    // faster client, the gap fill) does not run the pull of Reap or the ground of Mighty Gavel early. The item waits for its due time,
    // and it runs only when no end of the sequence interrupted its visit, and when no end of the sequence came after it at all (code
    // review of stage 3.3b-M2b, finding M2b-R2: a travel or a change of the weapon after the end of the visit).
    const WorldClock::time_point now = m_world.clock().now();
    const auto earliest = event.due
                          - std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                              std::chrono::duration<double>(attack_rules::kEarlySteps / attack_rules::kStepsInSecond))
                          - attack_rules::kWorldEarly;
    if (now < earliest) {
        const auto wait = std::chrono::ceil<std::chrono::milliseconds>(event.due - now);
        m_sequences.count_delayed();
        m_world.after(wait, [this, characterId, event](World&) {
            Entity* own = m_world.find_character(characterId);
            if (own != nullptr && !own->life.dead && m_sequences.live_visit(characterId, event.visit)
                && m_sequences.generation_of(characterId) == event.generation) {
                m_world.abilities().fragment_event(*own, event);
            }
        });
        return;
    }
    if (Entity* own = m_world.entity_of(session)) {
        m_world.abilities().fragment_event(*own, event);
    }
}

AttackFacts Combat::facts_of(const Entity& character) const {
    AttackFacts facts;
    const EntityId id = character.id;
    World* world = &m_world;
    facts.learned = [world, id](std::uint32_t node) {
        const Entity* own = world->find(id);
        if (own == nullptr) {
            return false;
        }
        for (const auto& [pool, nodes] : own->skills.pools) {
            const auto found = nodes.find(node);
            if (found != nodes.end() && found->second.level != 0) {
                return true;
            }
        }
        return false;
    };
    facts.perk = [world, id](std::uint32_t perkAbility) {
        const Entity* own = world->find(id);
        if (own == nullptr) {
            return false;
        }
        for (const GearPerk& perk : world->gear().perks_of(*own)) {
            for (const std::string& name : perk.perk->abilities) {
                if (gamedata::progression_id(name) == perkAbility) {
                    return true;
                }
            }
        }
        return false;
    };
    return facts;
}

float Combat::strike_reach(const gamedata::AttackStrike& strike, const gamedata::StrikeRow* row) {
    // The reach of a strike of an ability (stage 3.3b-M2, a decision of an agent): the extent of the shape of the row of the character (all
    // shapes for a row with no shape) from the joint of the weapon (the offset, the half height of a capsule, the radius; a box: its half
    // diagonal) and 1 m of the arm, and not less than the reach of a basic attack. Reap: 3.25 m, thus 3.5 m; with The Collector:
    // 1.5 + 3.0 + 0.75 + 1.0 = 6.25 m (game text: 5 m, and 8 m with The Collector, with the dash of the action).
    float extent = 0.0F;
    for (std::size_t index = 0; index < strike.shapes.size(); ++index) {
        if (row != nullptr && row->shape < strike.shapes.size() && row->shape != index) {
            continue;
        }
        const gamedata::AttackShape& shape = strike.shapes[index];
        const float offset = std::hypot(shape.offset[0], shape.offset[1], shape.offset[2]);
        const float size = shape.type.find("Box") != std::string::npos
                               ? 0.5F * std::hypot(shape.box[0], shape.box[1], shape.box[2])
                               : shape.halfHeight + shape.radius;
        extent = std::max(extent, offset + size);
    }
    return std::max(combat_limits::kMeleeReach, extent + combat_limits::kArmReach);
}

void Combat::end_sequence(const std::string& characterId, SequenceEnd why) {
    m_sequences.end(characterId, why);
}

void Combat::forget_sequence(const std::string& characterId) {
    m_sequences.forget(characterId);
}

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

std::optional<std::uint32_t> Combat::weapon_slot_in_hand(const Entity& attacker) {
    const Equipment& equipment = attacker.equipment;
    if (attacker.combat.selectedWeapon != 0 && equipment.item(attacker.combat.selectedWeapon) != nullptr) {
        return attacker.combat.selectedWeapon;
    }
    return equipment.active_weapon();
}

std::string Combat::weapon_key(const Entity& attacker) {
    const std::optional<std::uint32_t> slot = weapon_slot_in_hand(attacker);
    const ItemInstance* item = slot ? attacker.equipment.item(*slot) : nullptr;
    return item != nullptr ? std::to_string(*slot) + "/" + instance_text(item->instance) : std::string();
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
    if (entity->equipment.item(slot) != nullptr && entity->combat.selectedWeapon != slot) {
        entity->combat.selectedWeapon = slot;
        // The attack of the weapon before the change ends (stage 3.3b-M1).
        m_sequences.end(entity->characterId, SequenceEnd::weapon);
        // The perks of a weapon with OnActive count only while it is in the hand (stage 1.3b).
        m_world.gear().refresh(*entity);
        // The window of a counter ends with the weapon of its cast (finding F2 of the review of stage 3.3b-S4).
        m_world.abilities().weapon_changed(*entity);
    }
}

float Combat::defense_factor(std::uint32_t typeIndex, const gamedata::DefenseRow* defense, float armor, float penetration) const {
    if (defense == nullptr) {
        return 1.0F;
    }
    const gamedata::DamageType& type = m_tables->damage_type(typeIndex);
    const float mitigation = type.category == gamedata::DamageCategory::physical    ? defense->physical
                             : type.category == gamedata::DamageCategory::elemental ? defense->elemental
                                                                                    : 0.0F;
    // A value of the game tables can be above 1 (a creature that takes no damage of a
    // category): a factor is not less than 0. A rend of an effect lowers the mitigation.
    const float weakness = typeIndex < defense->weakness.size() ? defense->weakness[typeIndex] : 0.0F;
    const float absorption = typeIndex < defense->absorption.size() ? defense->absorption[typeIndex] : 0.0F;
    // ArmorPenetration of the passive rules of the hit (stage 3.3b-S5): the hit meets a smaller part of the mitigation.
    return std::max(1.0F - mitigation * std::max(1.0F + armor, 0.0F) * std::clamp(1.0F - penetration, 0.0F, 1.0F), 0.0F)
           * std::max(1.0F + weakness, 0.0F) * std::max(1.0F - absorption, 0.0F);
}

float Combat::weapon_base(const gamedata::WeaponRow& weapon, std::uint32_t gearScore) const {
    if (m_tables == nullptr) {
        return 0.0F;
    }
    const gamedata::CombatConstants& constants = m_tables->constants();
    // The gear score: one factor for each full interval above the lowest gear score, and a
    // smaller one for each full interval above the limit (client code at image RVA 0x3691470).
    const float rate = weapon.gearScoreRate > 0.0F ? weapon.gearScoreRate : constants.gearScoreRate;
    const std::uint32_t lower = std::min(gearScore, constants.gearScoreLimit);
    const std::uint32_t first = lower > constants.gearScoreMinimum ? (lower - constants.gearScoreMinimum) / constants.gearScoreInterval : 0;
    const std::uint32_t second = gearScore > constants.gearScoreLimit ? (gearScore - constants.gearScoreLimit) / constants.gearScoreInterval : 0;
    return weapon.baseDamage * std::pow(1.0F + rate, static_cast<float>(first))
           * std::pow(1.0F + rate * constants.gearScoreSmallerRate, static_cast<float>(second));
}

Damage Combat::damage(const Entity& attacker,
                      const gamedata::WeaponRow& weapon,
                      std::uint32_t gearScore,
                      const gamedata::DamageRow& row,
                      const gamedata::DefenseRow* defense,
                      const HitModifiers* modifiers) const {
    const gamedata::CombatConstants& constants = m_tables->constants();
    const gamedata::DamageType& type = m_tables->damage_type(row.type);
    Damage damage;
    damage.type = type.wire;
    damage.weapon = weapon_base(weapon, gearScore);
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
    // The gear of the attacker (stage 1.3b): a part of the damage, a conversion of a gem, and the DMG<type> parts.
    float convertedRaw = 0.0F;
    std::optional<std::uint32_t> convertedIndex;
    const auto typePart = [&](const std::string& name) {
        if (modifiers == nullptr || modifiers->typeParts == nullptr) {
            return 0.0F;
        }
        const auto found = modifiers->typeParts->find(name);
        return found == modifiers->typeParts->end() ? 0.0F : found->second;
    };
    const float armor = modifiers != nullptr ? modifiers->armor : 0.0F;
    const float penetration = modifiers != nullptr ? modifiers->penetration : 0.0F;
    if (modifiers != nullptr) {
        damage.raw *= std::max(1.0F + modifiers->part, 0.0F);
        if (modifiers->conversionPart > 0.0F && !modifiers->conversionType.empty()) {
            for (std::uint32_t index = 0; index < m_tables->data().damageTypes.size(); ++index) {
                if (m_tables->data().damageTypes[index].name == modifiers->conversionType) {
                    convertedIndex = index;
                }
            }
            // A row of the type of the gem: the damage has one type, and one part.
            if (convertedIndex && *convertedIndex == row.type) {
                convertedIndex.reset();
            }
            if (convertedIndex) {
                convertedRaw = damage.raw * std::min(modifiers->conversionPart, 1.0F);
            }
        }
    }
    // The defense of the target (a rule of this project), for each part with its type.
    const float mainDealt = (damage.raw - convertedRaw) * std::max(1.0F + typePart(type.name), 0.0F) * defense_factor(row.type, defense, armor, penetration);
    float convertedDealt = 0.0F;
    if (convertedIndex) {
        const gamedata::DamageType& converted = m_tables->damage_type(*convertedIndex);
        convertedDealt = convertedRaw * std::max(1.0F + typePart(converted.name), 0.0F) * defense_factor(*convertedIndex, defense, armor, penetration);
        damage.convertedType = converted.wire;
    }
    if (!std::isfinite(convertedDealt) || convertedDealt < 0.0F) {
        convertedDealt = 0.0F;
    }
    damage.converted = convertedDealt;
    damage.dealt = mainDealt + convertedDealt;
    if (!std::isfinite(damage.dealt) || damage.dealt < 0.0F) {
        damage.dealt = 0.0F;
        damage.converted = 0.0F;
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
        // A summon (stage 3.3b-S2b: the Pylon): the armor of its base vitals row (PhysicalArmorRatingOverride,
        // ElementalArmorRatingOverride; game data) and the level of its caster (Spells::make).
        if (target.kind == EntityKind::spell && target.spell.vitalsRow != nullptr) {
            damage.armor = contact.category == gamedata::DamageCategory::physical    ? target.spell.vitalsRow->physicalArmor
                           : contact.category == gamedata::DamageCategory::elemental ? target.spell.vitalsRow->elementalArmor
                                                                                     : 0.0F;
        }
        // The armor parts of the perks and of the effects of the character (stage 1.3b: a dust, the status of a rune).
        const float part = contact.category == gamedata::DamageCategory::physical    ? target.gear.physicalArmor
                           : contact.category == gamedata::DamageCategory::elemental ? target.gear.elementalArmor
                                                                                     : 0.0F;
        damage.armor *= std::max(1.0F + part, 0.0F);
        const float gearScore = data->gear_score(creature.creature.level);
        if (damage.armor > 0.0F && gearScore > 0.0F) {
            const float against = std::pow(gearScore, m_tables->armor_constants().mitigationExponent);
            damage.mitigation = damage.armor / (damage.armor + against);
        }
    }
    // A weaken of an ability on the creature lowers its damage (stage 3.3a, Creature::damage_factor).
    damage.dealt = damage.raw * std::max(damage.levelFactor, 0.0F) * std::max(1.0F - damage.mitigation, 0.0F)
                   * creature.creature.damage_factor(m_world.clock().now());
    // The absorption of the damage type (ABS<type>: a ward of a gem or of an amulet) and BaseDamageReduction (stage 1.3b).
    if (const auto absorbed = target.gear.absorption.find(contact.type); absorbed != target.gear.absorption.end()) {
        damage.dealt *= std::max(1.0F - absorbed->second, 0.0F);
    }
    damage.dealt *= std::max(1.0F - target.gear.damageReduction, 0.0F);
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
    // A creature on its way back takes no damage. Its vitals state has the mark "invulnerable",
    // and the client makes no contact with it (live check of stage 3.4e): this is the rule of
    // the world for a claim that comes before the client has the mark.
    if (target->creature.immune()) {
        m_world.creatures().behavior().immune_hit();
        return refuse(HitVerdict::immune);
    }
    // A contact in the window of a phase of an ability that plays (world/abilities.h): a hit of
    // that phase, with its damage row. The hit check of a melee attack stays.
    const AttackTrack::Press* fitting = track.press_for(claim.input);
    if (const std::optional<MeleePhase> phase = m_world.abilities().contact_phase(
            *attacker, session, claim.input, track.sequence(), target->id,
            fitting != nullptr ? std::optional<std::uint16_t>(fitting->input) : std::nullopt)) {
        if (phase->done) {
            return refuse(HitVerdict::sameAttack);
        }
        const gamedata::WeaponRow* weapon = m_tables->weapon_of(phase->itemId);
        if (weapon == nullptr || !weapon->melee) {
            return refuse(HitVerdict::noWeapon);
        }
        if (distance(attacker->position, target->position) > combat_limits::kMeleeReach + combat_limits::kReachTolerance) {
            return refuse(HitVerdict::tooFar);
        }
        if (!valid_position(claim.point) || distance(claim.point, target->position) > combat_limits::kPointRange) {
            return refuse(HitVerdict::badPoint);
        }
        limits.note_hit(HitLimits::Hit{target->id, now, claim.input, session, track.sequence()});
        const AbilityKind kind = m_world.abilities().kind_of(phase->cast);
        const CastConversion conversion{phase->conversionType, phase->conversionPart};
        HitWeapon use{phase->itemId, phase->slot, weapon, phase->gearScore, m_world.abilities().ability_of(phase->cast)};
        use.attack = passive_rules::kAttackCast | phase->cast; // The hits of one cast are the hits of one attack (stage 3.3b-S5).
        result = strike(*attacker, *target, use, phase->row, 1.0F, ability_name(kind), &conversion);
        if (result.verdict != HitVerdict::accepted) {
            return refuse(result.verdict);
        }
        result.verdict = track.counted(HitVerdict::accepted);
        if (Entity* hurt = m_world.creatures().find(target->id)) {
            m_world.abilities().contact_hit(phase->cast, phase->phase, *attacker, *hurt);
        }
        return result;
    }
    // The sequence of the basic attacks (stage 3.3b-M1): the fragment and the strike of the contact, when the weapon in the hand has
    // an attack graph.
    if (const ItemInstance* inHand = weapon_in_hand(*attacker)) {
        if (const gamedata::WeaponRow* held = m_tables->weapon_of(inHand->itemId); held != nullptr && actions_of(*held) != nullptr) {
            return claim_strike(session, claim, track, *attacker, *target, *inHand, *held, limits);
        }
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
    limits.note_hit(HitLimits::Hit{target->id, now, claim.input, session, track.sequence()});
    const AttackKind kind = result.kind;
    // The hits of one attack input are the hits of one attack (stage 3.3b-S5: TrackHitCount of a passive rule), with the session.
    HitWeapon use{item->itemId, weapon_slot_in_hand(*attacker).value_or(0), weapon, item->gearScore, {}};
    use.attack = passive_rules::kAttackPress | (static_cast<std::uint64_t>(session & 0xFFFFFFU) << 32) | attack->number;
    result = strike(*attacker, *target, use, *rowIndex, 1.0F, kind == AttackKind::heavy ? "heavy attack" : "light attack");
    result.kind = kind;
    if (result.verdict != HitVerdict::accepted) {
        return refuse(result.verdict);
    }
    result.verdict = track.counted(HitVerdict::accepted);
    return result;
}

HitResult Combat::claim_strike(SessionId session, const HitClaim& claim, AttackTrack& track, Entity& attacker, Entity& target,
                               const ItemInstance& item, const gamedata::WeaponRow& weapon, HitLimits& limits) {
    HitResult result;
    const auto refuse = [&](HitVerdict verdict) {
        result.verdict = track.counted(verdict);
        return result;
    };
    const WorldClock::time_point now = m_world.clock().now();
    const auto usable = [&](const gamedata::AttackStrike& strike) { return row_for(attacker, strike) != nullptr; };
    const StrikeMatch match =
        m_sequences.match(attacker.characterId, session, track.sequence(), claim.input, target.id, now, usable, facts_of(attacker));
    switch (match.verdict) {
    case StrikeMatch::Verdict::noVisit:
        return refuse(HitVerdict::noAttack);
    case StrikeMatch::Verdict::noStrike:
        return refuse(HitVerdict::noStrike);
    case StrikeMatch::Verdict::sameStrike:
        return refuse(HitVerdict::sameAttack);
    case StrikeMatch::Verdict::hit:
        break;
    }
    if (m_sequences.strike_hits(attacker.characterId, match.visit, match.strike) >= combat_limits::kStrikeTargets) {
        return refuse(HitVerdict::noStrike);
    }
    const gamedata::StrikeRow* strikeRow = row_for(attacker, *match.strikeData);
    const std::optional<std::uint32_t> rowIndex = strikeRow != nullptr ? m_tables->row_index(strikeRow->row) : std::nullopt;
    if (!rowIndex) {
        return refuse(HitVerdict::noWeapon);
    }
    // The distance, with the exact position of the character that the world has, and the point of the contact. A strike of an ability
    // has the reach of the shape of its row (stage 3.3b-M2: Reap reaches 5 m, 8 m with The Collector).
    const float reach = match.cast != 0 ? strike_reach(*match.strikeData, strikeRow) : combat_limits::kMeleeReach;
    if (distance(attacker.position, target.position) > reach + combat_limits::kReachTolerance) {
        return refuse(HitVerdict::tooFar);
    }
    if (!valid_position(claim.point) || distance(claim.point, target.position) > combat_limits::kPointRange) {
        return refuse(HitVerdict::badPoint);
    }
    if (match.cast != 0) {
        // A strike of the fragment of an ability (stage 3.3b-M2): the abilities system makes the hit with the weapon of the cast.
        const AbilityStrike ability = m_world.abilities().strike_of(match.cast, session, track.sequence(), claim.input);
        if (!ability.valid) {
            return refuse(HitVerdict::noStrike);
        }
        // The sequence notes the hit (its ledger, the hits of the cast, a transition at a collision) only after the hit is accepted: a
        // refused contact changes nothing.
        const EntityId targetId = target.id;
        result = m_world.abilities().strike_hit(match.cast, *match.fragment, match.strike, *rowIndex, attacker, target);
        result.fragment = match.fragment->name;
        result.row = m_tables->damage_row(*rowIndex).id;
        if (result.verdict != HitVerdict::accepted) {
            return refuse(result.verdict);
        }
        m_sequences.note_match(attacker.characterId, match, targetId, now);
        limits.note_hit(HitLimits::Hit{targetId, now, claim.input, session, track.sequence()});
        result.verdict = track.counted(HitVerdict::accepted);
        return result;
    }
    m_sequences.note_match(attacker.characterId, match, target.id, now);
    limits.note_hit(HitLimits::Hit{target.id, now, claim.input, session, track.sequence()});
    const gamedata::DamageRow& row = m_tables->damage_row(*rowIndex);
    const AttackKind kind = row.attackType == "Heavy" ? AttackKind::heavy : AttackKind::light;
    // The hits of one fragment visit are the hits of one attack (TrackHitCount of a passive rule; the two thrusts of the second
    // step of the rapier), with the session.
    HitWeapon use{item.itemId, weapon_slot_in_hand(attacker).value_or(0), &weapon, item.gearScore, {}};
    use.attack = passive_rules::kAttackPress | (static_cast<std::uint64_t>(session & 0xFFFFFFU) << 32) | match.visit;
    const std::string what = std::string(kind == AttackKind::heavy ? "heavy attack" : "light attack") + ", " + match.fragment->name + " "
                             + match.strikeData->key;
    result = strike(attacker, target, use, *rowIndex, 1.0F, what.c_str());
    result.kind = kind;
    result.fragment = match.fragment->name;
    result.row = row.id;
    if (result.verdict != HitVerdict::accepted) {
        return refuse(result.verdict);
    }
    result.verdict = track.counted(HitVerdict::accepted);
    return result;
}

HitResult Combat::strike(Entity& attacker, Entity& target, const HitWeapon& use, std::uint32_t rowIndex, float factor, const char* what,
                         const CastConversion* cast, const state::WorldPosition* source) {
    HitResult result;
    result.verdict = HitVerdict::dead;
    if (!has_tables() || use.row == nullptr || rowIndex >= m_tables->data().damageRows.size()) {
        result.verdict = HitVerdict::noTables;
        return result;
    }
    const gamedata::DamageRow& row = m_tables->damage_row(rowIndex);
    // The gear of the attacker and the effects on the target (stage 1.3b, world/gear.h).
    const gamedata::WeaponRow& weapon = *use.row;
    const std::string& itemId = use.itemId;
    const std::uint32_t gearScore = use.gearScore;
    const EntityId attackerId = attacker.id;
    const EntityId targetId = target.id;
    const state::WorldPosition attackerPlace = attacker.position;
    const state::WorldPosition from = source != nullptr ? *source : attackerPlace;
    HitContext hit;
    hit.row = row.id;
    hit.attackType = row.attackType;
    hit.ability = use.ability;
    hit.fragment = use.fragment;
    hit.table = weapon.mastery.empty() ? 0 : gamedata::progression_id(weapon.mastery);
    // The facts of the hit (stage 3.3b-S5): its attack, its kind, its area, the health of the target before it, a hit from behind
    // (the angles of the DamageReceiver of the slice of the target: game data), and a headshot of a projectile.
    hit.attack = use.attack;
    hit.kind = use.kind;
    hit.spell = use.spell;
    hit.areaCount = use.areaCount;
    hit.targetHealth = target.vitals.health;
    hit.targetHealthMax = target.vitals.healthMax;
    bool behindCrit = false;
    if (target.creature.body != nullptr && target.creature.body->behind) {
        const float angle = source_angle(target, from);
        hit.behind = angle >= 0.0F && angle >= target.creature.body->behind->hit;
        behindCrit = angle >= 0.0F && angle >= target.creature.body->behind->critical && !row.noBackstab;
    }
    hit.head = use.head && use.kind == HitKind::projectile && !row.noHeadshot;
    // The modifiers of the passive rules of the hit (world/passives.h): the damage part, the critical parts, the penetration.
    const HitBonus bonus = m_world.passives().before_hit(attacker, target, hit);
    HitModifiers modifiers;
    modifiers.part = attacker.gear.baseDamage + bonus.damage + m_world.gear().load_part(attacker);
    modifiers.typeParts = &attacker.gear.damage;
    modifiers.armor = target.creature.armor_part(m_world.clock().now());
    modifiers.penetration = bonus.penetration;
    // The gem of the weapon of the hit: the instance of the slot of the hit (finding F1 of the review of stage 1.3b), when
    // that slot still has the item of the hit.
    if (cast != nullptr) {
        // The hit of a cast: the gem of the weapon instance of the press (finding F3 of the review of stage 3.3b-S1).
        if (cast->part > 0.0F) {
            modifiers.conversionType = cast->type;
            modifiers.conversionPart = cast->part;
        }
    } else if (const ItemInstance* instance = use.slot != 0 ? attacker.equipment.item(use.slot) : nullptr; instance != nullptr && instance->itemId == itemId) {
        if (const std::optional<Conversion> conversion = m_world.gear().conversion(*instance, use.slot)) {
            modifiers.conversionType = conversion->type;
            modifiers.conversionPart = conversion->part;
        }
    }
    result.damage = damage(attacker, weapon, gearScore, row, m_tables->defense_of(target.creature.kind->vitals), &modifiers);
    if (factor != 1.0F) {
        result.damage.dealt *= std::max(factor, 0.0F);
        result.damage.converted *= std::max(factor, 0.0F);
    }
    // The critical rule (Combat::critical): the bonus multiplies the damage that the target takes (both parts of a conversion).
    const Critical critical = this->critical(weapon, row, bonus.critChance, bonus.critDamage, behindCrit, hit.head, bonus.critOnCrit);
    if (critical.critical) {
        result.damage.dealt *= critical.multiplier;
        result.damage.converted *= critical.multiplier;
    }
    hit.critical = critical.critical;
    std::uint16_t flags = 0;
    if (use.kind == HitKind::projectile) {
        flags |= damage_flags::kRanged;
    }
    if (critical.critical) {
        flags |= damage_flags::kCrit | damage_flags::kCanCrit;
    }
    if (hit.head) {
        flags |= damage_flags::kHeadshot;
    }
    // The note of the log: the cause of a critical hit, then each fact of the hit that the cause does not already name.
    char note[160] = "";
    if (critical.critical || hit.behind || hit.head) {
        const std::string_view cause = critical.critical ? critical.cause : "";
        std::string facts = critical.critical ? "critical: " + std::string(cause) : std::string();
        if (hit.head && cause != "headshot") {
            facts += facts.empty() ? "headshot" : ", headshot";
        }
        if (hit.behind && cause != "behind") {
            facts += facts.empty() ? "from behind" : ", from behind";
        }
        std::snprintf(note, sizeof(note), "%s (chance %.3f, roll %.3f, multiplier %.3f)", facts.c_str(),
                      static_cast<double>(critical.chance), static_cast<double>(critical.roll), static_cast<double>(critical.multiplier));
    }
    const Damage dealt = result.damage;
    result = apply_hit(attacker, target, dealt, itemId, row.id, what, use.threat, flags, note[0] != '\0' ? note : nullptr);
    result.critical = critical;
    result.behind = hit.behind;
    result.head = hit.head;
    if (result.verdict != HitVerdict::accepted) {
        return result;
    }
    Entity* own = m_world.find(attackerId);
    Entity* hurt = m_world.creatures().find(targetId);
    if (own == nullptr || hurt == nullptr) {
        return result;
    }
    // The impact of the row (stage 3.3b-S4): a creature that lives after the hit moves by it (a pull, a push).
    if (row.impact && !hurt->creature.dead) {
        m_world.creatures().impact(*hurt, *row.impact, from, attackerPlace, row.id);
    }
    // The heart rune charge of the hit (AttackRuneCharge of the row), and the perks and the passive rules of a hit (stage 1.3b,
    // stage 3.3b-S5).
    if (row.attackCharge > 0.0F) {
        m_world.runes().gain(*own, row.attackCharge);
    }
    hit.dealt = result.damage.dealt;
    hit.raw = result.damage.raw;
    hit.killed = result.killed;
    m_world.gear().on_hit(*own, *hurt, hit);
    return result;
}

Critical Combat::critical(const gamedata::WeaponRow& weapon, const gamedata::DamageRow& row, float bonusChance, float bonusDamage,
                          bool behindCrit, bool head, float bonusOnCrit) {
    Critical result;
    if (!weapon.critical) {
        return result; // No critical values (a combat file of a tool from before stage 3.3b-S5): no critical hit.
    }
    if (row.noCrit) {
        // An explicit CanCrit 0 of the row (code review of stage 3.3b-S5, finding F2): no roll, and no critical hit from behind or
        // of a headshot (a decision of an agent: the cell is a prohibition of the row; the facts of the hit stay).
        return result;
    }
    result.chance = std::clamp(weapon.critChance + bonusChance, 0.0F, 1.0F);
    // DmgCoefCrit of the row in the place of the multiplier of the weapon (a decision of an agent: the data has it on 11 rows, the
    // critical coefficient of the row as DmgCoefHead is its head coefficient; Path of Destiny 1.2, the war hammer 1.2), then one
    // additive pool of the bonuses (a decision of the user).
    result.multiplier = std::max((row.critFactor > 0.0F ? row.critFactor : weapon.critMultiplier) + bonusDamage, 0.0F);
    if (behindCrit) {
        result.critical = true;
        result.cause = "behind";
    } else if (head) {
        result.critical = true;
        result.cause = "headshot";
    } else if (result.chance > 0.0F) {
        // One independent roll for each hit with the generator of the world (a decision of the user). A chance of 0 takes no number.
        result.roll = m_world.random_unit();
        result.critical = result.roll < result.chance;
        result.cause = "roll";
    }
    // The damage cells of the rules with OnCrit (code review of stage 3.3b-S5, finding F3: Vicious, CritDamage 0.015): only a critical
    // hit takes them, in the same additive pool.
    if (result.critical) {
        result.multiplier = std::max(result.multiplier + bonusOnCrit, 0.0F);
    }
    return result;
}

float Combat::source_angle(const Entity& target, const state::WorldPosition& source) {
    const float dx = source.x - target.position.x;
    const float dy = source.y - target.position.y;
    const float away = std::hypot(dx, dy);
    if (!(away > 0.05F)) {
        return -1.0F;
    }
    // The front of the creature: its yaw in degrees, 0 for north, larger to the west (world/projectiles.cpp, capsule_of).
    const float yaw = target.creature.yaw * 3.14159265F / 180.0F;
    const float frontX = -std::sin(yaw);
    const float frontY = std::cos(yaw);
    const float cosine = std::clamp((frontX * dx + frontY * dy) / away, -1.0F, 1.0F);
    return std::acos(cosine);
}

HitResult Combat::strike_amount(Entity& attacker, Entity& target, float amount, const std::string& typeName, const char* what) {
    HitResult result;
    result.verdict = HitVerdict::noTables;
    if (!has_tables() || !std::isfinite(amount) || amount <= 0.0F) {
        return result;
    }
    std::optional<std::uint32_t> typeIndex;
    for (std::uint32_t index = 0; index < m_tables->data().damageTypes.size(); ++index) {
        if (m_tables->data().damageTypes[index].name == typeName) {
            typeIndex = index;
        }
    }
    if (!typeIndex) {
        return result;
    }
    Damage damage;
    damage.type = m_tables->damage_type(*typeIndex).wire;
    damage.raw = amount;
    damage.dealt = amount * defense_factor(*typeIndex, m_tables->defense_of(target.creature.kind->vitals),
                                           target.creature.armor_part(m_world.clock().now()));
    if (!std::isfinite(damage.dealt) || damage.dealt < 0.0F) {
        damage.dealt = 0.0F;
    }
    return apply_hit(attacker, target, damage, "", typeName, what);
}

HitResult Combat::apply_hit(Entity& attacker, Entity& target, const Damage& damage, const std::string& itemId, const std::string& row,
                            const char* what, EntityId threat, std::uint16_t flags, const char* note) {
    HitResult result;
    result.verdict = HitVerdict::dead;
    result.damage = damage;
    result.healthBefore = target.vitals.health;
    const EntityId targetId = target.id;
    const ActorId targetActor = target.actor;
    const CreatureResult hurt = m_world.creatures().hurt(targetId, result.damage.dealt, attacker.id, threat);
    if (hurt.error != CreatureError::none || hurt.entity == nullptr) {
        if (hurt.error == CreatureError::immune) {
            result.verdict = HitVerdict::immune;
        }
        return result;
    }
    m_world.health().fought(attacker); // The health of the attacker does not come back while it fights.
    result.healthAfter = hurt.entity->vitals.health;
    result.killed = hurt.entity->creature.dead;
    result.verdict = HitVerdict::accepted;
    const auto type_name = [&](std::uint8_t wire) -> const char* {
        for (const gamedata::DamageType& type : m_tables->data().damageTypes) {
            if (type.wire == wire) {
                return type.name.c_str();
            }
        }
        return "?";
    };
    log::write("world: hit: %s hurts creature %016llx (%s) with %s (%s, row %s): %.1f damage of the type %s%s%s%s (weapon "
               "%.1f, with attributes and level %.1f, before the defense %.1f), health %.1f to %.1f%s",
               attacker.name.c_str(),
               static_cast<unsigned long long>(targetId),
               hurt.entity->creature.kind->id.c_str(),
               itemId.empty() ? "no item" : itemId.c_str(),
               what,
               row.c_str(),
               result.damage.dealt,
               type_name(result.damage.type),
               result.damage.converted > 0.0F ? " (converted part " : "",
               result.damage.converted > 0.0F ? (std::to_string(result.damage.converted).substr(0, 8) + " of the type ").c_str() : "",
               result.damage.converted > 0.0F ? (std::string(type_name(result.damage.convertedType)) + ")").c_str() : "",
               result.damage.weapon,
               result.damage.core,
               result.damage.raw,
               result.healthBefore,
               result.healthAfter,
               result.killed ? ": dead" : "");
    if (note != nullptr) {
        log::write("world: hit: %s, the hit on creature %016llx: %s", attacker.name.c_str(), static_cast<unsigned long long>(targetId), note);
    }
    DamageDealt dealt;
    dealt.target = targetId;
    dealt.targetActor = targetActor;
    dealt.type = result.damage.type;
    dealt.amount = result.damage.dealt - result.damage.converted;
    dealt.secondType = result.damage.convertedType;
    dealt.secondAmount = result.damage.converted;
    dealt.killed = result.killed;
    dealt.flags = flags;
    m_world.damage_dealt(attacker.owner, dealt);
    return result;
}

} // namespace soulwarden::backend::world
