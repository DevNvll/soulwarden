#pragma once

// The world of the cases of stage 3.4f-C (tests/ranged_c_tests.cpp): the ground areas of the creatures. The slices have the structure of
// the slices of the game (dryad_mushroom_bomb: a PositionInTheWorld slot, the delayed events of the tell, the cloud, and the end, the
// AreaStatusEffectComponent of the enemies with its cylinder and its delays; undead_shaman_lightning_strike: a Spell slot, the damage window
// of 0.9 s to 0.95 s of a volume with no shape), and the creatures have the casts of the data (Shaman_Bomb: three casts at 1.2 s, 2.2 s,
// 3.13 s; Shaman_Pirate_Lightning_Strike: one cast at 0.93 s). The ids, the names, the assets, the rows, and the places are made up.

#include <chrono>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/range_fixture.h"
#include "support/stamina_fixture.h"
#include "world/spells.h"
#include "world/statuses.h"

namespace soulwarden::backend::tests::area {

using namespace range;

inline constexpr const char* kBombSpell = "Test_Bomb";
inline constexpr const char* kStrikeSpell = "Test_Strike";
inline constexpr const char* kPoison = "Test_Poison_55";
/** The actions of the shaman slice: the melee bash (3), Shaman_Bomb (6), and Shaman_Pirate_Lightning_Strike (7). */
inline constexpr std::uint32_t kBombAction = 6;
inline constexpr std::uint32_t kStrikeAction = 7;
inline constexpr float kBombLength = 4.03333F;
inline constexpr float kStrikeLength = 3.33333F;
inline constexpr float kStrikeCast = 0.933333F;
inline constexpr std::uint32_t kMushroomPlaceSlot = 1;
inline constexpr std::uint32_t kStrikeSpellSlot = 1;
/** The values of the retail rows (AI_Poison_55, Dryad_Bomb, Undead_Shaman_Lightning_Strike, the slices). */
inline constexpr float kPoisonPercent = -0.02F;
inline constexpr float kPoisonDamageBased = -0.04F;
inline constexpr float kPoisonTick = 2.0F;
inline constexpr float kPoisonDuration = 20.0F;
inline constexpr float kCloudAt = 1.0F;
inline constexpr float kCloudEnd = 26.0F;
inline constexpr float kInitialDelay = 0.5F;
inline constexpr float kReapply = 1.0F;
inline constexpr float kCloudRadius = 1.5F;
inline constexpr float kCloudHeight = 4.0F;
inline constexpr float kStrikeRadius = 2.5F;
inline constexpr float kStrikeOn = 0.9F;
inline constexpr float kStrikeOff = 0.95F;
inline constexpr float kStrikeFactor = 0.85F;
/** The core damage of the shaman of the cases: the base damage of its level (archer_levels: 100) times the factor of its kind (1). */
inline constexpr float kCoreDamage = kArcherDamage;

/** The mushroom slice: the structure of dryad_mushroom_bomb (live check L8: the tell, the cloud at 1 s, the end at 26 s). */
inline gamedata::SpellSlice mushroom_slice() {
    gamedata::SpellSlice slice;
    slice.path = "slices/test/mushroom.dynamicslice";
    slice.asset.fill(0x71);
    slice.subId = 2;
    slice.root = 0x000000C600000071ULL;
    slice.interestRadius = 50.5F;
    slice.spellSlot = 0;
    slice.positionSlot = kMushroomPlaceSlot;
    slice.slots = {{1, "PositionInTheWorld", 13}, {2, "DelayedEvent", 3139}, {3, "DelayedEvent", 3139}, {4, "DelayedEvent", 3139},
                   {5, "TemporaryAffiliation", 3563}};
    slice.events = {{2, kCloudEnd, gamedata::spell_event::kTrigger, "Destroy", "", true, "Mushroom", true, true},
                    {3, kCloudAt, gamedata::spell_event::kActivate, "Cloud", "", false, "Mushroom", true, true},
                    {4, kCloudAt, gamedata::spell_event::kDeactivate, "Tell", "", false, "Mushroom", true, true}};
    gamedata::SpellVolume volume;
    volume.entity = "Cloud";
    volume.radius = 0.0F;
    volume.activeAfter = kCloudAt;
    volume.shape = gamedata::VolumeShape::cylinder;
    volume.shapeRadius = kCloudRadius;
    volume.height = kCloudHeight;
    volume.offset = -0.5F;
    volume.inactiveAfter = -1.0F;
    slice.volumes = {volume};
    gamedata::SpellAreaStatus area;
    area.entity = "Cloud";
    area.detection = "Cloud";
    area.statuses = {kPoison};
    area.removeOnExit = true;
    area.targets = 2;
    area.initialDelay = kInitialDelay;
    area.reapplyDelay = kReapply;
    area.inherit = true;
    slice.areaStatuses = {area};
    return slice;
}

/** The strike slice: the structure of undead_shaman_lightning_strike (live check L8: the omen, the bolt at 0.9 s). */
inline gamedata::SpellSlice strike_slice() {
    gamedata::SpellSlice slice;
    slice.path = "slices/test/strike.dynamicslice";
    slice.asset.fill(0x72);
    slice.subId = 2;
    slice.root = 0x0000038D00000072ULL;
    slice.interestRadius = 50.0F;
    slice.spellSlot = kStrikeSpellSlot;
    slice.slots = {{1, "Spell", 2912},        {2, "Ownership", 3217},    {3, "DelayedEvent", 3139}, {4, "DelayedEvent", 3139},
                   {5, "DelayedEvent", 3139}, {6, "DelayedEvent", 3139}, {7, "DelayedEvent", 3139}, {8, "TemporaryAffiliation", 3563}};
    slice.events = {{3, 0.65F, gamedata::spell_event::kActivate, "Audio", "", false, "Strike", true, false},
                    {4, kStrikeOn, gamedata::spell_event::kActivate, "Damage", "", false, "Strike", true, false},
                    {5, kStrikeOff, gamedata::spell_event::kDeactivate, "Damage", "", false, "Strike", true, false},
                    {6, 0.85F, gamedata::spell_event::kDeactivate, "VFXArea", "", false, "Strike", true, false},
                    {7, kStrikeOn, gamedata::spell_event::kActivate, "VFXDamage", "", false, "Strike", true, false}};
    gamedata::SpellVolume volume;
    volume.entity = "Damage";
    volume.activeAfter = kStrikeOn;
    volume.shape = gamedata::VolumeShape::none;
    volume.inactiveAfter = kStrikeOff;
    slice.volumes = {volume};
    return slice;
}

inline gamedata::SpellTables area_spells() {
    gamedata::SpellTables tables;
    tables.format = 4;
    tables.slices = {mushroom_slice(), strike_slice()};
    tables.spells = {{kBombSpell, 0}, {kStrikeSpell, 1}};
    return tables;
}

/** The poison of the cases: the cells of AI_Poison_55 of the game data. */
inline gamedata::StatusRow poison_row() {
    return shop::status(kPoison, kPoisonDuration,
                        shop::cells({{"TickRate", kPoisonTick}, {"HealthModifierPercent", kPoisonPercent},
                                     {"HealthModifierDamageBased", kPoisonDamageBased}, {"RemoveOnDeath", 1.0F}, {"IsNegative", 1.0F},
                                     {"StackMax", 1.0F}, {"ShowInUITray", 1.0F}, {"ShowTextInDamageNumbers", 1.0F}},
                                    {{"DamageType", "Nature"}, {"EffectCategories", "Debuff+Poison+Dot"}}));
}

/** The ability data of the cases: the rows of the shop, the poison, and the spell rows of the two areas (the cells of the game data). */
inline gamedata::AbilityTables area_abilities() {
    gamedata::AbilityTables tables = shop::shop_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    rows.push_back(poison_row());
    tables.statusEffects = shop::numbered(std::move(rows));
    // The two rows have the ranged attack data of the game rows (RangedAttackProfile AI_Ranged_AOE): the client puts the spell at the aim
    // point of the target (stage 3.4f-D, the place rule of 0x679DBB0).
    tables.spells.push_back({kBombSpell, 3.0F, 5.0F, 30.0F, 0, 0.0F, 25000.0F,
                             shop::cells({{"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}},
                                         {{"TargetTypes", "Enemy"}, {"DamageTable", "TestShamanDamageTable"}, {"DamageTableRow", "Mushroom"},
                                          {"RangedAttackProfile", "AI_Ranged_AOE"}, {"RangedAttackName", "Basic"}})});
    tables.spells.push_back({kStrikeSpell, kStrikeRadius, 5.0F, 12.0F, 0, 0.0F, 5000.0F,
                             shop::cells({{"EndOnCasterDeath", 1.0F}, {"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}, {"CastHeight", 20.0F}},
                                         {{"TargetTypes", "Enemy"}, {"ShapeTypes", "Sphere"}, {"DamageTable", "TestShamanDamageTable"},
                                          {"DamageTableRow", "Test_Strike"}, {"RangedAttackProfile", "AI_Ranged_AOE"},
                                          {"RangedAttackName", "AlignSpawnsWithTerrain"}})});
    return tables;
}

inline gamedata::AreaCast bomb_cast(float time) {
    gamedata::AreaCast cast;
    cast.time = time;
    cast.spell = kBombSpell;
    cast.shape = gamedata::AreaShape::status;
    cast.aimAtTarget = true;
    cast.lead = true;
    cast.aimSpeed = 100.0F;
    cast.leadAngle = 10.0F;
    cast.aimAngle = 45.0F;
    cast.missMin = 0.5F;
    cast.missMax = 1.0F;
    cast.allTargets = true;
    return cast;
}

inline gamedata::AreaCast strike_cast() {
    gamedata::AreaCast cast;
    cast.time = kStrikeCast;
    cast.spell = kStrikeSpell;
    cast.shape = gamedata::AreaShape::strike;
    cast.aimAtTarget = true;
    cast.aimSpeed = 100.0F;
    cast.leadAngle = 20.0F;
    cast.aimAngle = 45.0F;
    cast.missMax = 2.0F;
    cast.allTargets = true;
    cast.damage.time = kStrikeCast;
    cast.damage.factor = kStrikeFactor;
    cast.damage.type = "Arcane";
    cast.damage.wire = 7;
    cast.damage.category = gamedata::DamageCategory::elemental;
    cast.damage.row = "Test_Strike";
    return cast;
}

/** The shaman of the cases: the archer slice with no shot, the bash, and the area attack Shaman_Bomb (the Dryad Shaman). */
inline gamedata::CreatureSlice shaman_slice() {
    gamedata::CreatureSlice slice = archer_slice(true);
    slice.path = "slices/test/shaman.dynamicslice";
    slice.asset.fill(0x33);
    slice.root = 0x1000000000000033ULL;
    slice.actions.push_back({"Shaman_Bomb", "Shaman_Bomb", 120, false});
    slice.actions.push_back({"Shaman_Pirate_Lightning_Strike", "Shaman_Pirate_Lightning_Strike", 121, false});
    slice.ranged.clear();
    slice.areas = {gamedata::CreatureAreaAttack{kBombAction, kBombLength, {bomb_cast(1.2F), bomb_cast(2.2F), bomb_cast(3.13333F)}}};
    return slice;
}

/** The striker of the cases: the same slice with no melee attack and only the lightning strike (the Undead Shaman). */
inline gamedata::CreatureSlice striker_slice() {
    gamedata::CreatureSlice slice = shaman_slice();
    slice.path = "slices/test/striker.dynamicslice";
    slice.asset.fill(0x34);
    slice.root = 0x1000000000000034ULL;
    slice.attacks.clear();
    slice.areas = {gamedata::CreatureAreaAttack{kStrikeAction, kStrikeLength, {strike_cast()}}};
    slice.behavior->sightDistance = 18.0F;
    return slice;
}

/** The thrower of the cases: the shaman slice with the basic shot of the archer too (the Dryad Shaman shoots and casts its area). */
inline gamedata::CreatureSlice thrower_slice() {
    gamedata::CreatureSlice slice = shaman_slice();
    slice.path = "slices/test/thrower.dynamicslice";
    slice.asset.fill(0x35);
    slice.root = 0x1000000000000035ULL;
    slice.ranged = archer_slice(true).ranged;
    return slice;
}

inline gamedata::CreatureKind area_kind(const char* id, std::uint32_t body, std::uint32_t mark) {
    gamedata::CreatureKind kind = archer_kind();
    kind.id = id;
    kind.vitals = kind.id;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = mark;
    kind.categoryId = mark + 1;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.bodies = {{body, 100}};
    return kind;
}

/** The world of a case: the range world with the shaman (slice 0), the striker (slice 1), the thrower (slice 2), the spell data, the ability data, and the stamina data. */
struct Areas : Range {
    explicit Areas(bool observe = true, std::vector<gamedata::SpawnPoint> points = {}) : Range(observe) {
        if (!host.creatures.set_full({shaman_slice(), striker_slice(), thrower_slice()},
                                     {area_kind("Test_Shaman", 0, 0x33112222), area_kind("Test_Striker", 1, 0x34112222),
                                      area_kind("Test_Thrower", 2, 0x35112222)},
                                     std::move(points),
                                     archer_levels(), {}, {arrow_slice()},
                                     gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}})) {
            throw std::runtime_error("area test: the creature data is not valid: " + host.creatures.error());
        }
        if (!host.abilities.set(area_abilities())) {
            throw std::runtime_error("area test: the ability data is not valid: " + host.abilities.error());
        }
        if (!host.spells.set(area_spells())) {
            throw std::runtime_error("area test: the spell data is not valid: " + host.spells.error());
        }
        stamina::set_stamina(host);
        walker.idle(2);
    }
    world::Spells& spells() {
        return host.world.spells();
    }
    /** A shaman `east` m east of A (and `north` m to the north), its face to the west (yaw 90: it sees A), or with a different yaw (270: away). */
    world::Entity* shaman(float east, float north = 0.0F, float yaw = 90.0F) {
        return host.world.creatures().spawn("Test_Shaman", at(kPlace.x + east, kPlace.y + north), yaw).entity;
    }
    world::Entity* striker(float east, float north = 0.0F, float yaw = 90.0F) {
        return host.world.creatures().spawn("Test_Striker", at(kPlace.x + east, kPlace.y + north), yaw).entity;
    }
    world::Entity* thrower(float east, float north = 0.0F, float yaw = 90.0F) {
        return host.world.creatures().spawn("Test_Thrower", at(kPlace.x + east, kPlace.y + north), yaw).entity;
    }
    /** Makes an area of a cast of a creature at a place now (the spell system, as the behavior does at a cast). */
    world::Entity* area_at(world::Entity& creature, const gamedata::AreaCast& cast, const state::WorldPosition& place) {
        return spells().make_area(world::AreaRequest{creature.id, &cast, place});
    }
    /** The spell entities of a role. */
    std::vector<world::EntityId> areas_of(world::SpellRole role) {
        std::vector<world::EntityId> found;
        for (const auto& [id, entity] : host.world.entities()) {
            if (entity.kind == world::EntityKind::spell && entity.spell.role == role) {
                found.push_back(id);
            }
        }
        return found;
    }
    bool has_poison(const char* character) {
        const world::Entity* entity = host.world.find_character(character);
        return entity != nullptr && host.world.statuses().has(*entity, kPoison);
    }
};

/** The cast of the shaman slice of the data of the host (a pointer that stays valid while the data is set). */
inline const gamedata::AreaCast& data_cast(Areas& world, std::uint32_t slice, std::uint32_t cast = 0) {
    return world.host.creatures.slices()[slice].areas.front().casts[cast];
}

} // namespace soulwarden::backend::tests::area
