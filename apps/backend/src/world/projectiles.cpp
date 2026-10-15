#include "world/projectiles.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/abilities.h"
#include "world/position_index.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

constexpr float kPi = 3.14159265358979F;

using Vector = std::array<float, 3>;

Vector minus(const state::WorldPosition& a, const state::WorldPosition& b) {
    return {a.x - b.x, a.y - b.y, a.z - b.z};
}

float dot(const Vector& a, const Vector& b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

state::WorldPosition along(const state::WorldPosition& from, const state::WorldPosition& to, float part) {
    return {from.x + (to.x - from.x) * part, from.y + (to.y - from.y) * part, from.z + (to.z - from.z) * part};
}

float length(const Vector& vector) {
    return std::sqrt(dot(vector, vector));
}

float distance3(const state::WorldPosition& a, const state::WorldPosition& b) {
    return length(minus(a, b));
}

/** @return The distance of a point to a segment. */
float point_segment(const state::WorldPosition& point, const state::WorldPosition& a, const state::WorldPosition& b) {
    const Vector ab = minus(b, a);
    const float squared = dot(ab, ab);
    const float part = squared > 0.0F ? std::clamp(dot(minus(point, a), ab) / squared, 0.0F, 1.0F) : 0.0F;
    return distance3(point, along(a, b, part));
}

/** @return The smallest t of `origin + t * direction` (direction a unit vector, t >= 0) on a sphere, or no value. */
std::optional<float> ray_sphere(const state::WorldPosition& origin, const Vector& direction, const state::WorldPosition& center, float radius) {
    const Vector oc = minus(origin, center);
    const float b = dot(oc, direction);
    const float c = dot(oc, oc) - radius * radius;
    const float h = b * b - c;
    if (h < 0.0F) {
        return std::nullopt;
    }
    const float t = -b - std::sqrt(h);
    return t >= 0.0F ? std::optional<float>(t) : std::nullopt;
}

/** The yaw on the wire (a creature: 0 north, growing to the west) in radians. */
float radians(float degrees) {
    return degrees * kPi / 180.0F;
}

/** The ammunition of an empty slot for an ammunition type (client code; no equipped ammunition in this backend). */
const char* default_ammo(const std::string& type) {
    if (type == "Arrow") {
        return projectile_rules::kDefaultArrow;
    }
    if (type == "Shot") {
        return projectile_rules::kDefaultShot;
    }
    return nullptr;
}

std::string what_of(const gamedata::ShotRule& rule) {
    return (rule.spell.empty() ? std::string("shot ") : std::string("spell ")) + rule.profile + "/" + rule.attack;
}

} // namespace

const char* shot_verdict_text(ShotVerdict verdict) {
    switch (verdict) {
    case ShotVerdict::accepted:
        return "accepted";
    case ShotVerdict::noEntity:
        return "the session has no entity";
    case ShotVerdict::noData:
        return "no projectile data or no combat tables";
    case ShotVerdict::dead:
        return "the character is dead";
    case ShotVerdict::locked:
        return "the input is locked (a travel)";
    case ShotVerdict::noWeapon:
        return "the weapon in the hand has no ranged attack profile";
    case ShotVerdict::unknownAttack:
        return "the profile of the weapon has no such attack";
    case ShotVerdict::badRow:
        return "no shot of the data has this attack, row, and charge";
    case ShotVerdict::badCharge:
        return "the hold is shorter than the hold of a heavy shot";
    case ShotVerdict::badAmmo:
        return "the ammunition is not the ammunition of the weapon";
    case ShotVerdict::noAttack:
        return "no press of the attack input fits the shot";
    case ShotVerdict::repeated:
        return "the press has a projectile";
    case ShotVerdict::badOrigin:
        return "the origin is not near the character or a wall is between them";
    case ShotVerdict::badDirection:
        return "the direction is not a unit vector";
    case ShotVerdict::badReference:
        return "the receiver near the muzzle is not a creature with this reference";
    case ShotVerdict::notObserved:
        return "the session does not observe the receiver near the muzzle";
    case ShotVerdict::nearTooFar:
        return "the receiver is not in the near range of the origin";
    case ShotVerdict::noMana:
        return "not enough mana";
    case ShotVerdict::busy:
        return "the character has too many projectiles in the air";
    case ShotVerdict::unsupported:
        return "the attack has no flight or a grouped spread";
    }
    return "?";
}

const char* projectile_end_text(ProjectileEnd end) {
    switch (end) {
    case ProjectileEnd::hit:
        return "it hit a creature";
    case ProjectileEnd::nearHit:
        return "it hit a creature near the muzzle";
    case ProjectileEnd::wall:
        return "a wall or the ground stopped it";
    case ProjectileEnd::range:
        return "it flew its range";
    case ProjectileEnd::lifetime:
        return "its life time ended";
    case ProjectileEnd::caster:
        return "its caster left the world or died";
    }
    return "?";
}

WorldCapsule capsule_of(const Entity& creature) {
    gamedata::CreatureCapsule capsule;
    capsule.radius = projectile_rules::kDefaultRadius;
    capsule.height = projectile_rules::kDefaultHeight;
    if (creature.creature.body != nullptr && creature.creature.body->capsule) {
        capsule = *creature.creature.body->capsule;
    }
    // The frame of the creature: x to its right, y to its front, z up. Its yaw is 0 for north and grows to the west.
    const float yaw = radians(creature.creature.yaw);
    const std::array<float, 2> front{-std::sin(yaw), std::cos(yaw)};
    const std::array<float, 2> right{std::cos(yaw), std::sin(yaw)};
    const auto turn = [&](const Vector& local) {
        return Vector{right[0] * local[0] + front[0] * local[1], right[1] * local[0] + front[1] * local[1], local[2]};
    };
    Vector axis = capsule.axis;
    const float size = length(axis);
    axis = size > 1.0e-6F ? Vector{axis[0] / size, axis[1] / size, axis[2] / size} : Vector{0.0F, 0.0F, 0.0F};
    const Vector world = turn(axis);
    const Vector offset = turn(capsule.offset);
    const float half = capsule.height * 0.5F;
    // The lowest point of the capsule is at the feet of the creature (a decision of this project).
    const float lift = std::fabs(world[2]) * half + capsule.radius;
    const state::WorldPosition center{creature.position.x + offset[0], creature.position.y + offset[1],
                                      creature.position.z + offset[2] + lift};
    WorldCapsule result;
    result.a = {center.x - world[0] * half, center.y - world[1] * half, center.z - world[2] * half};
    result.b = {center.x + world[0] * half, center.y + world[1] * half, center.z + world[2] * half};
    result.radius = capsule.radius;
    return result;
}

std::optional<float> sweep(const state::WorldPosition& from, const state::WorldPosition& to, const WorldCapsule& capsule, float radius) {
    const float reach = capsule.radius + radius;
    if (point_segment(from, capsule.a, capsule.b) <= reach) {
        return 0.0F;
    }
    const Vector line = minus(to, from);
    const float size = length(line);
    if (!(size > 1.0e-6F)) {
        return std::nullopt;
    }
    const Vector direction{line[0] / size, line[1] / size, line[2] / size};
    std::optional<float> best;
    const auto take = [&](std::optional<float> t) {
        if (t && *t <= size && (!best || *t < *best)) {
            best = t;
        }
    };
    // The two half spheres of the ends.
    take(ray_sphere(from, direction, capsule.a, reach));
    take(ray_sphere(from, direction, capsule.b, reach));
    // The cylinder of the segment (IQ's capsule intersection): only the part between the two ends.
    const Vector ba = minus(capsule.b, capsule.a);
    const float baba = dot(ba, ba);
    if (baba > 1.0e-8F) {
        const Vector oa = minus(from, capsule.a);
        const float bard = dot(ba, direction);
        const float baoa = dot(ba, oa);
        const float rdoa = dot(direction, oa);
        const float oaoa = dot(oa, oa);
        const float a = baba - bard * bard;
        const float b = baba * rdoa - baoa * bard;
        const float c = baba * oaoa - baoa * baoa - reach * reach * baba;
        const float h = b * b - a * c;
        if (a > 1.0e-8F && h >= 0.0F) {
            const float t = (-b - std::sqrt(h)) / a;
            const float y = baoa + t * bard;
            if (t >= 0.0F && y > 0.0F && y < baba) {
                take(t);
            }
        }
    }
    if (!best) {
        return std::nullopt;
    }
    return *best / size;
}

Projectiles::Projectiles(World& world, const gamedata::ProjectileData* data, const gamedata::CombatTables* combat)
    : m_world(world), m_data(data), m_combat(combat) {}

float Projectiles::reach() {
    // The reach of the largest capsule of the creature data from the place of its creature: a creature farther from a segment
    // than this cannot touch it. The world computes it again when the creature data has a different number of slices.
    const gamedata::CreatureData* creatures = m_world.options().creatureData;
    const std::size_t slices = creatures != nullptr ? creatures->slices().size() : 0;
    if (m_reach > 0.0F && slices == m_reachSlices) {
        return m_reach;
    }
    m_reachSlices = slices;
    m_reach = projectile_rules::kDefaultRadius * 2.0F + projectile_rules::kDefaultHeight;
    if (creatures != nullptr) {
        for (const gamedata::CreatureSlice& slice : creatures->slices()) {
            if (slice.capsule) {
                const float offset = std::hypot(slice.capsule->offset[0], slice.capsule->offset[1], slice.capsule->offset[2]);
                m_reach = std::max(m_reach, 2.0F * slice.capsule->radius + slice.capsule->height + offset);
            }
        }
    }
    return m_reach;
}

ShotVerdict Projectiles::rule_of(const Entity& character, std::uint32_t attackId, std::uint32_t row, float charge, std::uint32_t ammo,
                                 Launch& launch) const {
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const gamedata::WeaponRow* weapon = item != nullptr ? m_combat->weapon_of(item->itemId) : nullptr;
    const gamedata::RangedWeapon* ranged = weapon != nullptr ? m_data->weapon(weapon->id) : nullptr;
    if (ranged == nullptr) {
        return ShotVerdict::noWeapon;
    }
    launch.attack = m_data->attack(ranged->profile, attackId);
    if (launch.attack == nullptr) {
        return ShotVerdict::unknownAttack;
    }
    if (launch.attack->hitScan || launch.attack->spreadDirections > 0) {
        return ShotVerdict::unsupported;
    }
    // The rule of the item: a clip has the row and the charge of the item; a spell of a magic weapon has its own row (the
    // item of a magic shot has the row 0 and the charge 0: live check L5).
    for (const gamedata::ShotRule* rule : m_data->shots(ranged->profile, attackId)) {
        const bool fits = rule->spell.empty() ? gamedata::progression_id(rule->row) == row && std::fabs(rule->charge - charge) < 0.5F
                                              : row == 0 && charge == 0.0F;
        if (fits && m_combat->row_index(rule->row)) {
            launch.rule = rule;
            break;
        }
    }
    if (launch.rule == nullptr) {
        return ShotVerdict::badRow;
    }
    // The ammunition: the backend has no equipped ammunition (the slots 12 and 13 take no item), thus the key of a weapon with
    // ammunition is the ammunition of an empty slot (client code; live check L1: DefaultArrow), and a magic weapon has none.
    const gamedata::AmmoRow* row_of_ammo = nullptr;
    if (!ranged->ammoType.empty()) {
        const char* fallback = default_ammo(ranged->ammoType);
        row_of_ammo = fallback != nullptr && ammo == gamedata::progression_id(fallback) ? m_data->ammo(ammo) : nullptr;
        if (row_of_ammo == nullptr) {
            return ShotVerdict::badAmmo;
        }
        launch.factor = row_of_ammo->damageModifier;
    } else if (ammo != 0) {
        return ShotVerdict::badAmmo;
    }
    launch.slice = m_data->slice(launch.rule->slice >= 0 ? launch.rule->slice : (row_of_ammo != nullptr ? row_of_ammo->slice : -1));
    if (launch.slice == nullptr || !(launch.slice->speed > 0.0F)) {
        return ShotVerdict::unsupported;
    }
    return ShotVerdict::accepted;
}

ShotResult Projectiles::launch(SessionId session, const ShotClaim& claim, const AttackTrack& track) {
    ShotResult result;
    const auto refuse = [&](ShotVerdict verdict) {
        result.verdict = counted(verdict);
        return result;
    };
    Entity* character = m_world.entity_of(session);
    if (character == nullptr) {
        return refuse(ShotVerdict::noEntity);
    }
    if (!has_data()) {
        return refuse(ShotVerdict::noData);
    }
    if (character->life.dead) {
        return refuse(ShotVerdict::dead);
    }
    if (claim.locked) {
        return refuse(ShotVerdict::locked);
    }
    Launch launch;
    launch.session = session;
    launch.sequence = claim.sequence;
    launch.input = claim.input;
    if (const ShotVerdict verdict = rule_of(*character, claim.attack, claim.row, claim.charge, claim.ammo, launch);
        verdict != ShotVerdict::accepted) {
        return refuse(verdict);
    }
    // The press of the attack input of the shot: the newest press at or before its input step. A released press must be
    // recent; a heavy shot needs the hold of the data (from the press to the release, or to the shot).
    const AttackTrack::Press* press = track.press_at(claim.input);
    if (press == nullptr) {
        return refuse(ShotVerdict::noAttack);
    }
    const auto steps = [](std::uint16_t from, std::uint16_t to) { return static_cast<unsigned>(static_cast<std::uint16_t>(to - from)); };
    if (press->released && steps(press->releaseInput, claim.input) > projectile_rules::kAfterRelease) {
        return refuse(ShotVerdict::noAttack);
    }
    if (launch.rule->heavy) {
        const unsigned held = press->released ? steps(press->input, press->releaseInput) : steps(press->input, claim.input);
        const unsigned needed = launch.rule->spell.empty() ? projectile_rules::kChargedSteps : projectile_rules::kMagicHeavySteps;
        if (held < needed) {
            return refuse(ShotVerdict::badCharge);
        }
    }
    launch.press = press->number;
    // The origin: near the character, with no wall between the launch place of the server and it.
    state::WorldPosition place = character->position;
    place.z += projectile_rules::kLaunchHeight;
    if (!valid_position(claim.origin) || distance3(claim.origin, character->position) > projectile_rules::kOriginReach
        || m_world.navigation().line_stop(place, claim.origin)) {
        return refuse(ShotVerdict::badOrigin);
    }
    const float size = length(claim.direction);
    if (!std::isfinite(size) || std::fabs(size - 1.0F) > projectile_rules::kUnitTolerance) {
        return refuse(ShotVerdict::badDirection);
    }
    launch.origin = claim.origin;
    launch.direction = {claim.direction[0] / size, claim.direction[1] / size, claim.direction[2] / size};
    // The hit near the muzzle: a creature of the world with the reference of its slice, that the session observes, in the near
    // range of the profile from the origin, with no wall between. A dead creature or one on its way back is passed: the server
    // flies the projectile.
    if (claim.hasReceiver) {
        Entity* target = m_world.creatures().find(claim.nearId);
        if (launch.attack->nearRange <= 0.0F || target == nullptr || target->actor != claim.nearActor
            || target->creature.body == nullptr || target->creature.body->find(target->id, claim.nearFacet) == nullptr) {
            return refuse(ShotVerdict::badReference);
        }
        if (!claim.observed) {
            return refuse(ShotVerdict::notObserved);
        }
        if (!target->creature.dead && !target->creature.immune()) {
            const WorldCapsule capsule = capsule_of(*target);
            const float gap = point_segment(launch.origin, capsule.a, capsule.b) - capsule.radius;
            const state::WorldPosition middle = along(capsule.a, capsule.b, 0.5F);
            if (gap > launch.attack->nearRange + launch.attack->radius + projectile_rules::kHitTolerance || !valid_position(claim.nearPoint)
                || distance3(claim.nearPoint, target->position) > projectile_rules::kNearPointRange
                || m_world.navigation().line_stop(launch.origin, middle)) {
                return refuse(ShotVerdict::nearTooFar);
            }
            launch.nearTarget = target->id;
        }
    }
    return fire(*character, launch);
}

ShotResult Projectiles::launch_character(const std::string& characterId, const ServerShot& shot) {
    ShotResult result;
    const auto refuse = [&](ShotVerdict verdict) {
        result.verdict = counted(verdict);
        return result;
    };
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refuse(ShotVerdict::noEntity);
    }
    if (!has_data()) {
        return refuse(ShotVerdict::noData);
    }
    if (character->life.dead) {
        return refuse(ShotVerdict::dead);
    }
    // The rule of the weapon: its first light shot, or its first heavy one, with the ammunition of an empty slot. A heavy shot
    // takes an attack named "..._Heavy" first: the charged arrow of the client has Basic_Heavy (live check L2).
    const ItemInstance* item = Combat::weapon_in_hand(*character);
    const gamedata::WeaponRow* weapon = item != nullptr ? m_combat->weapon_of(item->itemId) : nullptr;
    const gamedata::RangedWeapon* ranged = weapon != nullptr ? m_data->weapon(weapon->id) : nullptr;
    if (ranged == nullptr) {
        return refuse(ShotVerdict::noWeapon);
    }
    const char* fallback = default_ammo(ranged->ammoType);
    const std::uint32_t ammo = fallback != nullptr ? gamedata::progression_id(fallback) : 0;
    Launch launch;
    ShotVerdict verdict = ShotVerdict::badRow;
    std::vector<const gamedata::ShotRule*> rules;
    for (const gamedata::ShotRule& rule : m_data->tables().shots) {
        if (rule.heavy == shot.heavy && gamedata::progression_id(rule.profile) == gamedata::progression_id(ranged->profile)) {
            rules.push_back(&rule);
        }
    }
    std::stable_partition(rules.begin(), rules.end(), [](const gamedata::ShotRule* rule) {
        return rule->attack.size() >= 6 && gamedata::progression_id(rule->attack.substr(rule->attack.size() - 6)) == gamedata::progression_id("_heavy");
    });
    for (const gamedata::ShotRule* found : rules) {
        const gamedata::ShotRule& rule = *found;
        Launch candidate;
        verdict = rule_of(*character, gamedata::progression_id(rule.attack), rule.spell.empty() ? gamedata::progression_id(rule.row) : 0,
                          rule.spell.empty() ? rule.charge : 0.0F, ammo, candidate);
        if (verdict == ShotVerdict::accepted) {
            launch = candidate;
            break;
        }
    }
    if (verdict != ShotVerdict::accepted) {
        return refuse(verdict);
    }
    launch.origin = character->position;
    launch.origin.z += projectile_rules::kLaunchHeight;
    const float yaw = radians(shot.yaw);
    const float pitch = radians(shot.pitch);
    launch.direction = {std::sin(yaw) * std::cos(pitch), std::cos(yaw) * std::cos(pitch), std::sin(pitch)};
    // A shot of the server has its own ledger number: the session 0, the sequence 0, and a new press for each shot.
    launch.press = static_cast<std::uint32_t>(m_next);
    return fire(*character, launch);
}

ShotResult Projectiles::fire(Entity& character, const Launch& launch) {
    ShotResult result;
    const auto refuse = [&](ShotVerdict verdict) {
        result.verdict = counted(verdict);
        return result;
    };
    auto key = std::make_tuple(character.characterId, launch.session, launch.sequence, launch.press);
    if (m_ledger.contains(key)) {
        return refuse(ShotVerdict::repeated);
    }
    const std::size_t flying = static_cast<std::size_t>(std::count_if(
        m_active.begin(), m_active.end(), [&](const auto& entry) { return entry.second.characterId == character.characterId; }));
    if (flying >= projectile_rules::kCharacterMost) {
        return refuse(ShotVerdict::busy);
    }
    // The cost of a magic shot: the ManaCost of its spell through the one mana path (game data: 0 for the basic attacks of
    // the fire staff).
    if (!launch.rule->manaCost.empty()) {
        const float cost = m_world.abilities().mana_cost_of(character, launch.rule->manaCost);
        if (cost > 0.0F && character.vitals.mana + 0.001F < cost) {
            return refuse(ShotVerdict::noMana);
        }
        if (cost > 0.0F) {
            m_world.abilities().change_mana(character, -cost, "a shot");
        }
    }
    const WorldClock::time_point now = m_world.clock().now();
    Projectile projectile;
    projectile.number = m_next++;
    projectile.characterId = character.characterId;
    projectile.caster = character.id;
    projectile.attack = launch.attack;
    projectile.rule = launch.rule;
    projectile.slice = launch.slice;
    projectile.rowIndex = *m_combat->row_index(launch.rule->row);
    projectile.factor = launch.factor;
    const ItemInstance* item = Combat::weapon_in_hand(character);
    const std::uint32_t slot = Combat::weapon_slot_in_hand(character).value_or(0);
    projectile.weapon = HitWeapon{item->itemId, slot, m_combat->weapon_of(item->itemId), item->gearScore, {}};
    // The gem of the weapon at the launch (as the conversion of a cast at its press: finding F3 of the review of 3.3b-S1).
    if (const std::optional<Conversion> conversion = m_world.gear().conversion(*item, slot)) {
        projectile.conversion = CastConversion{conversion->type, conversion->part};
    }
    projectile.start = launch.origin;
    projectile.position = launch.origin;
    projectile.velocity = {launch.direction[0] * launch.slice->speed, launch.direction[1] * launch.slice->speed,
                           launch.direction[2] * launch.slice->speed};
    projectile.launched = now;
    projectile.at = now;
    projectile.key = key;
    // The entry stays for the life time of the projectile and the ledger time after it.
    const auto life = std::chrono::duration_cast<WorldClock::time_point::duration>(
        std::chrono::duration<float>(std::max(launch.slice->flyingLifetime, 0.0F)));
    m_ledger[key] = now + life + projectile_rules::kLedgerTime;
    result.verdict = counted(ShotVerdict::accepted);
    result.projectile = projectile.number;
    log::write("world: projectile %llu: %s launches %s (row %s, %s) from %.2f %.2f %.2f to %.3f %.3f %.3f at %.0f m/s",
               static_cast<unsigned long long>(projectile.number),
               character.name.c_str(),
               what_of(*launch.rule).c_str(),
               launch.rule->row.c_str(),
               launch.slice->path.c_str(),
               launch.origin.x,
               launch.origin.y,
               launch.origin.z,
               launch.direction[0],
               launch.direction[1],
               launch.direction[2],
               launch.slice->speed);
    if (launch.nearTarget != 0) {
        if (Entity* target = m_world.creatures().find(launch.nearTarget)) {
            result.nearHit = true;
            result.hit = hit(projectile, *target, launch.origin);
        }
        finish(projectile, ProjectileEnd::nearHit, launch.origin);
        return result;
    }
    m_active.emplace(projectile.number, std::move(projectile));
    m_counts.largest = std::max(m_counts.largest, m_active.size());
    return result;
}

HitResult Projectiles::hit(Projectile& projectile, Entity& target, const state::WorldPosition& point) {
    Entity* caster = m_world.find(projectile.caster);
    HitResult result;
    if (caster == nullptr || caster->life.dead || projectile.weapon.row == nullptr) {
        return result;
    }
    const std::string what = what_of(*projectile.rule);
    result = m_world.combat().strike(*caster, target, projectile.weapon, projectile.rowIndex, projectile.factor, what.c_str(),
                                     &projectile.conversion, &point);
    if (result.verdict == HitVerdict::accepted) {
        ++m_counts.damage;
    }
    return result;
}

void Projectiles::finish(const Projectile& projectile, ProjectileEnd end, const state::WorldPosition& at) {
    ++m_counts.ends[static_cast<std::size_t>(end)];
    const Entity* caster = m_world.find(projectile.caster);
    log::write("world: projectile %llu of %s ends after %.1f m and %lld ms at %.2f %.2f %.2f: %s",
               static_cast<unsigned long long>(projectile.number),
               caster != nullptr ? caster->name.c_str() : "a character that left",
               end == ProjectileEnd::nearHit ? 0.0F : projectile.travelled + distance3(projectile.position, at),
               static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(m_world.clock().now() - projectile.launched).count()),
               at.x,
               at.y,
               at.z,
               projectile_end_text(end));
}

std::optional<ProjectileEnd> Projectiles::advance(Projectile& projectile, WorldClock::time_point now) {
    const gamedata::RangedAttack& attack = *projectile.attack;
    const gamedata::ProjectileSlice& slice = *projectile.slice;
    const float lifetime = slice.flyingLifetime;
    while (projectile.at < now) {
        Entity* caster = m_world.find(projectile.caster);
        if (caster == nullptr || caster->life.dead) {
            finish(projectile, ProjectileEnd::caster, projectile.position);
            return ProjectileEnd::caster;
        }
        ++m_counts.steps;
        const auto step = std::min<WorldClock::time_point::duration>(now - projectile.at, projectile_rules::kStep);
        const float seconds = std::chrono::duration<float>(step).count();
        const float age = std::chrono::duration<float>(projectile.at - projectile.launched).count();
        // The acceleration of the slice starts after the delay distance of the profile (client code at image RVA 0x695CCC0).
        const Vector acceleration = projectile.travelled >= attack.accelDelay ? slice.accel : Vector{0.0F, 0.0F, 0.0F};
        state::WorldPosition next{
            projectile.position.x + (projectile.velocity[0] + acceleration[0] * seconds * 0.5F) * seconds,
            projectile.position.y + (projectile.velocity[1] + acceleration[1] * seconds * 0.5F) * seconds,
            projectile.position.z + (projectile.velocity[2] + acceleration[2] * seconds * 0.5F) * seconds};
        const float segment = distance3(projectile.position, next);
        // The range of the profile and the life time of the slice cut the last segment.
        float part = 1.0F;
        std::optional<ProjectileEnd> limit;
        if (attack.maxRange > 0.0F && projectile.travelled + segment >= attack.maxRange) {
            part = segment > 0.0F ? std::clamp((attack.maxRange - projectile.travelled) / segment, 0.0F, 1.0F) : 0.0F;
            limit = ProjectileEnd::range;
        }
        if (lifetime > 0.0F && age + seconds >= lifetime) {
            const float timePart = seconds > 0.0F ? std::clamp((lifetime - age) / seconds, 0.0F, 1.0F) : 0.0F;
            if (timePart < part) {
                part = timePart;
                limit = ProjectileEnd::lifetime;
            }
        }
        const state::WorldPosition end = along(projectile.position, next, part);
        // A wall or the ground on the segment (the cells of the navigation data), and the first creature whose capsule the
        // segment touches (the creatures near the segment: the position index).
        const std::optional<float> wall = m_world.navigation().line_stop(projectile.position, end);
        const state::WorldPosition middle = along(projectile.position, end, 0.5F);
        const float range = distance3(projectile.position, end) * 0.5F + reach() + attack.radius + projectile_rules::kHitTolerance;
        std::vector<EntityId> candidates;
        m_world.for_each_near(middle, range, [&](Entity& entity) {
            if (entity.kind == EntityKind::creature && !entity.creature.dead && !entity.creature.immune()) {
                candidates.push_back(entity.id);
            }
        });
        std::optional<float> first;
        EntityId struck = 0;
        for (const EntityId id : candidates) {
            ++m_counts.looked;
            const Entity* creature = m_world.find(id);
            if (creature == nullptr) {
                continue;
            }
            const std::optional<float> touch = sweep(projectile.position, end, capsule_of(*creature), attack.radius + projectile_rules::kHitTolerance);
            if (touch && (!first || *touch < *first)) {
                first = touch;
                struck = id;
            }
        }
        if (first && (!wall || *first <= *wall)) {
            const state::WorldPosition point = along(projectile.position, end, *first);
            if (Entity* target = m_world.find(struck)) {
                hit(projectile, *target, point);
            }
            finish(projectile, ProjectileEnd::hit, point);
            return ProjectileEnd::hit;
        }
        if (wall) {
            const state::WorldPosition point = along(projectile.position, end, *wall);
            finish(projectile, ProjectileEnd::wall, point);
            return ProjectileEnd::wall;
        }
        if (limit) {
            finish(projectile, *limit, end);
            return limit;
        }
        projectile.travelled += segment;
        projectile.position = next;
        projectile.velocity = {projectile.velocity[0] + acceleration[0] * seconds, projectile.velocity[1] + acceleration[1] * seconds,
                               projectile.velocity[2] + acceleration[2] * seconds};
        projectile.at += step;
        if (!valid_position(projectile.position)) {
            finish(projectile, ProjectileEnd::range, projectile.position);
            return ProjectileEnd::range;
        }
    }
    return std::nullopt;
}

void Projectiles::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    for (auto entry = m_active.begin(); entry != m_active.end();) {
        entry = advance(entry->second, now) ? m_active.erase(entry) : std::next(entry);
    }
    for (auto entry = m_ledger.begin(); entry != m_ledger.end();) {
        entry = entry->second <= now ? m_ledger.erase(entry) : std::next(entry);
    }
}

void Projectiles::caster_left(EntityId caster) {
    for (auto entry = m_active.begin(); entry != m_active.end();) {
        if (entry->second.caster == caster) {
            finish(entry->second, ProjectileEnd::caster, entry->second.position);
            entry = m_active.erase(entry);
        } else {
            ++entry;
        }
    }
}

std::vector<ProjectileView> Projectiles::active() const {
    std::vector<ProjectileView> views;
    const WorldClock::time_point now = m_world.clock().now();
    for (const auto& [number, projectile] : m_active) {
        ProjectileView view;
        view.number = number;
        view.characterId = projectile.characterId;
        view.attack = projectile.rule->profile + "/" + projectile.rule->attack;
        view.row = projectile.rule->row;
        view.position = projectile.position;
        view.travelled = projectile.travelled;
        view.age = std::chrono::duration_cast<std::chrono::milliseconds>(now - projectile.launched);
        views.push_back(std::move(view));
    }
    return views;
}

} // namespace soulwarden::backend::world
