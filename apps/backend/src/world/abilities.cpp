#include "world/abilities.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
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
};

/** The id text of the ranged attack of the projectile of Gravity Well (the value "attack" of its shot is its CRC-32). */
constexpr const char* kGravityWellAttack = "GravityWell";
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

constexpr const char* kGreatAxe = "GreatAxeAbilityTable";
constexpr const char* kWarHammer = "WarHammerAbilityTable";

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
    }
    return "unknown";
}

Abilities::Abilities(World& world, const gamedata::AbilityData* data, const gamedata::CombatTables* combat)
    : m_world(world), m_data(data), m_combat(combat) {}

std::optional<CastPlan> Abilities::plan(std::uint32_t ability, const SkillSet& skills) {
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
    // Aftershock (Ult_Passive_WarHammer_SlowOnCC): each hit of an ability of the hammer slows the target.
    const bool aftershock = has_node(skills, kWarHammer, "Ult_Passive_WarHammer_SlowOnCC");
    const EffectPlan slowOnHit{"WarHammer_Ult_SlowOnCC", {}, 0, {}};
    switch (rule->kind) {
    case AbilityKind::charge:
        // One contact of the run (live check L3: 14 input steps after the press for a creature 6 m away).
        plan.length = std::chrono::milliseconds(500);
        plan.phases.push_back(contact("GreatAxe_Engage1", 3, 90));
        break;
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
        const std::string vortexSpell = upgrade ? "Spell_GreatAxe_GravityWell_Vortex_Upgrade" : "Spell_GreatAxe_GravityWell_Vortex";
        plan.length = std::chrono::milliseconds(600);
        plan.phases.push_back(contact("GreatAxe_GravityWell", 3, 30));
        PhasePlan vortex;
        vortex.kind = PhaseKind::vortex;
        vortex.spell = vortexSpell;
        vortex.effects = {EffectPlan{"GreatAxe_Vortex_Root", {}, 0, {}}, EffectPlan{"GreatAxe_Vortex", {}, 0, {}}};
        plan.phases.push_back(vortex);
        PhasePlan burst;
        burst.kind = PhaseKind::burst;
        burst.spell = vortexSpell;
        burst.row = "GreatAxe_GravityWell_Burst";
        burst.at = std::chrono::milliseconds(3000);
        burst.crowded = has_node(skills, kGreatAxe, "Upgrade_Greataxe_GravityWell_DmgPerFoe");
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
        phase.effects.push_back(EffectPlan{"WarHammer_Shockwave_Stun", {}, 0, {}});
        if (has_node(skills, kWarHammer, "Upgrade_WarHammer_Shockwave_Weaken")) {
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
        phase.cooldownPerHit = reduction;
        if (seismic) {
            phase.effects.push_back(EffectPlan{"", {}, ability_rules::kStaggerPower, ability_rules::kStaggerTime});
        }
        if (aftershock) {
            phase.effects.push_back(slowOnHit);
        }
        plan.phases.push_back(phase);
        break;
    }
    }
    return plan;
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
    const auto pool = character->skills.pools.find(table);
    const auto node = pool != character->skills.pools.end() ? pool->second.find(ability) : decltype(pool->second.end()){};
    if (pool == character->skills.pools.end() || node == pool->second.end() || node->second.level == 0) {
        return refuse(CastVerdict::notLearned);
    }
    const AbilityRule* rule = rule_of(ability);
    std::optional<CastPlan> planned = plan(ability, character->skills);
    const gamedata::CooldownRow* cooldown = rule != nullptr ? m_data->cooldown(rule->id) : nullptr;
    if (!planned || cooldown == nullptr) {
        return refuse(CastVerdict::noRule);
    }
    // Each row and each spell of the plan must be in the data: an ability with a part of its
    // data does nothing.
    for (const PhasePlan& phase : planned->phases) {
        if ((!phase.row.empty() && !m_combat->row_index(phase.row)) || (!phase.spell.empty() && m_data->spell(phase.spell) == nullptr)) {
            log::write("world: ability: the data has no row %s or no spell %s of %s", phase.row.c_str(), phase.spell.c_str(), rule->name);
            return refuse(CastVerdict::noRule);
        }
        // Each status row of an effect of the plan, also of a delayed effect (code review of stage 3.3a, finding F9).
        for (const EffectPlan& effect : phase.effects) {
            if (!effect.status.empty() && m_data->status(effect.status) == nullptr) {
                log::write("world: ability: the data has no status effect %s of %s", effect.status.c_str(), rule->name);
                return refuse(CastVerdict::noRule);
            }
        }
    }
    const WorldClock::time_point now = m_world.clock().now();
    CharacterState& state = m_characters[character->characterId];
    // A later press of the session, accepted or refused, ends the contact windows of the earlier cast (code review of
    // stage 3.3a, finding F2): a contact after it is not a hit of that cast.
    if (state.cast) {
        const auto earlier = m_casts.find(*state.cast);
        if (earlier != m_casts.end() && earlier->second.session == session && earlier->second.sequence == press.sequence
            && !earlier->second.closedAt) {
            earlier->second.closedAt = press.input;
        }
    }
    // The cooldown of the server, and the cooldown of the client: the client does not play a press in its own cooldown,
    // also when the cooldown of the server ended before (a reset command; stage 1.3b, I8, live check I8-05).
    const auto running = state.cooldowns.find(ability);
    if (running != state.cooldowns.end() && (now < running->second.end || now < running->second.clientEnd)) {
        return refuse(CastVerdict::cooldown);
    }
    if (state.cast) {
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
    Cast cast;
    cast.number = m_nextCast++;
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
    cast.gearScore = item->gearScore;
    // The cooldown of the data starts at the press, as the cooldown of the client (live check L1).
    AbilityCooldown timer;
    timer.start = now;
    timer.end = now + milliseconds(cooldown->time);
    timer.minEnd = now + milliseconds(cooldown->minTime);
    timer.clientEnd = timer.end;
    state.cooldowns[ability] = timer;
    state.cast = cast.number;
    const std::uint64_t number = cast.number;
    Cast& stored = m_casts.emplace(number, std::move(cast)).first->second;
    // The area phases at their times on the world clock.
    for (std::uint32_t index = 0; index < stored.plan.phases.size(); ++index) {
        const PhasePlan& phase = stored.plan.phases[index];
        if (phase.kind == PhaseKind::around) {
            ++stored.pending;
            m_world.after(phase.at, [this, number, index](World&) { run_area(number, index, {}); });
        } else if (phase.kind == PhaseKind::path) {
            const gamedata::SpellRow* spell = m_data->spell(phase.spell);
            const std::array<float, 2> ahead = direction_of(stored.yaw);
            const std::uint32_t samples = std::max<std::uint32_t>(spell->count, 1);
            for (std::uint32_t sample = 0; sample < samples; ++sample) {
                // A circle of the path at each step of the cast distance, one after the other.
                const float along = spell->castDistance * static_cast<float>(sample + 1);
                const state::WorldPosition center{stored.place.x + ahead[0] * along, stored.place.y + ahead[1] * along, stored.place.z};
                ++stored.pending;
                m_world.after(phase.at + milliseconds(spell->spawnRate / 1000.0F) * sample,
                              [this, number, index, center](World&) { run_area(number, index, center); });
            }
        }
    }
    if (stored.plan.kind == AbilityKind::gravityWell) {
        // The projectile: the shot of the client throws it. With no shot the server throws it
        // from the place of the caster in the direction of the press.
        ++stored.pending;
        m_world.after(ability_rules::kThrowTime + kThrowWait, [this, number](World&) {
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
    publish(stored.characterId);
    if (cost > 0.0F) {
        change_mana(*character, -cost, ability_name(stored.plan.kind));
    }
    ++m_counts.presses[static_cast<std::size_t>(CastVerdict::accepted)];
    log::write("world: ability: %s casts %s (place %u of the bar): cooldown %.1f s, %zu phase(s)",
               character->name.c_str(),
               ability_name(stored.plan.kind),
               press.place + 1,
               static_cast<double>(cooldown->time),
               stored.plan.phases.size());
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
    if (found == m_casts.end() || found->second.plan.kind != AbilityKind::gravityWell || found->second.thrown
        || found->second.session != session || shot.attack != gamedata::progression_id(kGravityWellAttack)) {
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
    const float length = std::hypot(direction[0], direction[1]);
    if (!std::isfinite(length) || length < 0.01F) {
        const std::array<float, 2> ahead = direction_of(cast.yaw);
        direction = {ahead[0], ahead[1], 0.0F};
    }
    throw_projectile(cast, origin, direction);
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
    cast.pending += 2;
    ++m_counts.vortexes;
    m_world.after(flight, [this, number, vortex, landing](World&) { run_area(number, vortex, landing); });
    m_world.after(flight + cast.plan.phases[burst].at, [this, number, burst, landing](World&) { run_area(number, burst, landing); });
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
        result.gearScore = cast.gearScore;
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

void Abilities::run_area(std::uint64_t number, std::uint32_t index, state::WorldPosition center) {
    const auto found = m_casts.find(number);
    if (found == m_casts.end()) {
        return;
    }
    Cast& cast = found->second;
    --cast.pending;
    Entity* caster = m_world.find(cast.caster);
    // A caster that left the world or died does no damage (a rule of this project).
    if (caster == nullptr || caster->characterId != cast.characterId || caster->life.dead || !has_data()) {
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
        if (area_hit(cast, index, *caster, *target, factor)) {
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

bool Abilities::area_hit(Cast& cast, std::uint32_t index, Entity& caster, Entity& target, float factor) {
    const PhasePlan& phase = cast.plan.phases[index];
    if (phase.row.empty()) {
        // A phase with no damage (the start of the vortex): only its effects.
        cast.hits.insert({index, target.id});
        give_effects(cast, phase, target);
        return true;
    }
    const std::optional<std::uint32_t> row = m_combat->row_index(phase.row);
    const gamedata::WeaponRow* weapon = m_combat->weapon_of(cast.itemId);
    if (!row || weapon == nullptr) {
        return false;
    }
    const HitResult result =
        m_world.combat().strike(caster, target, cast.itemId, *weapon, cast.gearScore, *row, factor, ability_name(cast.plan.kind));
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
    // The cooldowns that ended leave the state: the client keeps an entry until a new map comes.
    const WorldClock::time_point now = m_world.clock().now();
    for (auto timer = state->second.cooldowns.begin(); timer != state->second.cooldowns.end();) {
        timer = timer->second.end <= now ? state->second.cooldowns.erase(timer) : std::next(timer);
    }
    entity->cooldowns = state->second.cooldowns;
    m_world.touch(entity->id, ComponentKey::cooldowns);
}

void Abilities::entered(Entity& character) {
    const auto state = m_characters.find(character.characterId);
    if (state == m_characters.end()) {
        return;
    }
    // A new entity of the character: no cast plays, and the cooldowns stay.
    state->second.cast.reset();
    const WorldClock::time_point now = m_world.clock().now();
    for (auto timer = state->second.cooldowns.begin(); timer != state->second.cooldowns.end();) {
        timer = timer->second.end <= now ? state->second.cooldowns.erase(timer) : std::next(timer);
    }
    character.cooldowns = state->second.cooldowns;
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
    std::optional<float> base;
    std::string id;
    for (const PhasePlan& phase : plan.phases) {
        const gamedata::SpellRow* spell = phase.spell.empty() ? nullptr : m_data->spell(phase.spell);
        if (spell == nullptr) {
            continue;
        }
        const std::string_view costId = spell->cells.text("ManaCost");
        if (!costId.empty()) {
            if (const std::optional<float> cost = m_data->mana_cost(costId)) {
                base = cost;
                id = std::string(costId);
                break;
            }
        }
    }
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
            const std::uint32_t wanted = gamedata::progression_id(id);
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

void Abilities::tick() {
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
        if (m_world.combat().strike(*own, *target, itemId, *weapon, gearScore, *row, 1.0F, "a burst of a perk").verdict == HitVerdict::accepted) {
            ++hits;
            ++m_counts.areaHits;
        }
    }
    log::write("world: ability: the burst of a perk (%s) of %s (radius %.1f m): %zu creature(s) in the area, %u hit(s)",
               spell->id.c_str(), caster.name.c_str(), static_cast<double>(spell->radius), found_.size(), hits);
}

void Abilities::reduce_weapon_cooldowns(Entity& character, float part) {
    if (!has_data() || !(part > 0.0F)) {
        return;
    }
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
        if (timer == state->second.cooldowns.end() || row == nullptr || timer->second.end <= now) {
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

} // namespace soulwarden::backend::world
