#pragma once

// The world of the cases of stage 3.4f-D (tests/creature_support_tests.cpp): the heals and the buffs of the creatures, and the ground areas at
// the caster. The rows and the slices have the structure of the game data: the self heal InqCleric_Rekindle_Self of the Inquisitor Cleric
// (the mark Inquisitor_Cleric_Rekindle_Self at 0.633 s gives AI_InqCleric_Rekindle: 8 s, each 1 s 4.5 percent of the maximum health, an
// OnTickStatusEffect), the Howl of the Evil Knight hound (the mark ApplyHaste at 1.2 s gives AI_Evil_Knight_Wolf_Haste: 10 s, DMG 0.1 of each
// type, MoveSpeedMod 0.2, the category Buff), the dispel of the Null Chamber (RemoveStatusEffectCategories Buff), the plague nova of the
// ghost (CastDistance 0, no ranged attack data: at the caster), and the shockwave of the Evil Knight (AIAimAtTarget false: at the caster). The
// ids, the names, the assets, and the places are made up.

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/area_fixture.h"

namespace soulwarden::backend::tests::support {

using namespace area;

inline constexpr const char* kRekindle = "Test_InqCleric_Rekindle";
inline constexpr const char* kRampant = "Test_Inquisitor_RampantFlame";
inline constexpr const char* kHaste = "Test_Evil_Knight_Wolf_Haste";
inline constexpr const char* kNullify = "Test_NullChamber_Nullify";
inline constexpr const char* kNovaSpell = "Test_Plague_Nova";
inline constexpr const char* kWaveSpell = "Test_Shockwave";
inline constexpr const char* kShotSpell = "Test_Ranged_Self";
/** The status actions and the area attacks of the slices: the action index 6 of each slice (after the six actions of the archer). */
inline constexpr std::uint32_t kSupportAction = 6;
inline constexpr float kRekindleMark = 0.633333F;
inline constexpr float kRekindleLength = 1.533333F;
inline constexpr float kHowlMark = 1.2F;
inline constexpr float kHowlLength = 2.1F;
inline constexpr float kHealPart = 0.045F;
inline constexpr float kHealTime = 8.0F;
inline constexpr float kHastePart = 0.1F;
inline constexpr float kHasteSpeed = 0.2F;
inline constexpr float kHasteTime = 10.0F;
inline constexpr float kLowHealth = 0.15F;
inline constexpr float kNovaRadius = 4.0F;
inline constexpr float kWaveRadius = 3.0F;
inline constexpr std::uint32_t kNovaSpellSlot = 1;
inline constexpr std::uint32_t kWaveSpellSlot = 1;

/** The status rows of the cases (the cells of the rows of the game data). */
inline std::vector<gamedata::StatusRow> support_rows() {
    std::vector<gamedata::StatusRow> rows;
    rows.push_back(shop::status(kRekindle, kHealTime,
                                shop::cells({{"TickRate", 1.0F}, {"HealthModifierPercent", kHealPart}, {"KeepOnTickEffectOnEnd", 1.0F},
                                             {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}},
                                            {{"OnTickStatusEffect", kRampant}}),
                                true));
    rows.push_back(shop::status(kRampant, 20.0F, shop::cells({{"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}}, {{"EffectCategories", "RampantFlame"}}),
                                true));
    gamedata::StatusRow haste = shop::status(kHaste, kHasteTime,
                                             shop::cells({{"TickRate", 1.0F}, {"StaminaRate", 5.0F}, {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F},
                                                          {"SprintSpeedMod", kHasteSpeed}, {"DMGSiege", kHastePart}, {"DMGStrike", kHastePart},
                                                          {"DMGSlash", kHastePart}, {"DMGThrust", kHastePart}, {"DMGArcane", kHastePart},
                                                          {"DMGFire", kHastePart}, {"DMGIce", kHastePart}, {"DMGNature", kHastePart},
                                                          {"DMGLightning", kHastePart}, {"DMGCorruption", kHastePart}},
                                                         {{"EffectCategories", "Buff"}}),
                                             true);
    haste.moveSpeed = kHasteSpeed;
    haste.damage = kHastePart;
    rows.push_back(haste);
    rows.push_back(shop::status(kNullify, 0.1F,
                                shop::cells({{"TickRate", 1.0F}, {"RemoveOnDeath", 1.0F}, {"StackMax", 1.0F}},
                                            {{"RemoveStatusEffectCategories", "Buff"}})));
    return rows;
}

inline gamedata::AbilityTables support_abilities() {
    gamedata::AbilityTables tables = area_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    for (gamedata::StatusRow& row : support_rows()) {
        rows.push_back(std::move(row));
    }
    tables.statusEffects = shop::numbered(std::move(rows));
    tables.spells.push_back({kNovaSpell, kNovaRadius, 5.0F, 0.0F, 0, 0.0F, 5000.0F,
                             shop::cells({{"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}},
                                         {{"TargetTypes", "Enemy"}, {"ShapeTypes", "Sphere"}, {"DamageTable", "TestShamanDamageTable"},
                                          {"DamageTableRow", "Test_Nova"}})});
    tables.spells.push_back({kWaveSpell, kWaveRadius, 0.5F, 0.0F, 0, 0.0F, 6000.0F,
                             shop::cells({{"ClearOnExit", 1.0F}, {"ClearOnEnd", 1.0F}, {"CheckObstructions", 1.0F}},
                                         {{"TargetTypes", "Enemy"}, {"ShapeTypes", "Sphere"}, {"DamageTable", "TestShamanDamageTable"},
                                          {"DamageTableRow", "Test_Wave"}})});
    // A row with ranged attack data and a clip with AIAimAtTarget false: a shot of the ranged attack data (no rule of the backend).
    tables.spells.push_back({kShotSpell, kWaveRadius, 0.5F, 12.0F, 0, 0.0F, 6000.0F,
                             shop::cells({}, {{"TargetTypes", "Enemy"}, {"ShapeTypes", "Sphere"}, {"DamageTable", "TestShamanDamageTable"},
                                              {"DamageTableRow", "Test_Wave"}, {"RangedAttackProfile", "AI_Ranged_AOE"},
                                              {"RangedAttackName", "Basic"}})});
    return tables;
}

/** A strike slice that is active from the start of the spell to `off` (the plague nova: 0.5 s; the shockwave: 0.2 s). */
inline gamedata::SpellSlice window_slice(const char* path, std::uint8_t mark, float off, bool ownership) {
    gamedata::SpellSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x0000038D00000000ULL | mark;
    slice.interestRadius = 50.0F;
    slice.spellSlot = 1;
    if (ownership) {
        slice.slots = {{1, "Spell", 2912}, {2, "Ownership", 3217}, {3, "DelayedEvent", 3139}, {4, "DelayedEvent", 3139},
                       {5, "TemporaryAffiliation", 3563}};
        slice.events = {{3, off, gamedata::spell_event::kDeactivate, "Damage", "", false, "Wave", true, true},
                        {4, 2.0F, gamedata::spell_event::kTrigger, "Destroy", "", true, "Wave", true, true}};
    } else {
        slice.slots = {{1, "Spell", 2912}, {2, "DelayedEvent", 3139}, {3, "TemporaryAffiliation", 3563}};
        slice.events = {{2, off, gamedata::spell_event::kDeactivate, "Damage", "", false, "Nova", true, false}};
    }
    gamedata::SpellVolume volume;
    volume.entity = "Damage";
    volume.activeAfter = 0.0F;
    volume.shape = gamedata::VolumeShape::none;
    volume.inactiveAfter = off;
    slice.volumes = {volume};
    return slice;
}

inline gamedata::SpellTables support_spells() {
    gamedata::SpellTables tables = area_spells();
    tables.slices.push_back(window_slice("slices/test/nova.dynamicslice", 0x73, 0.5F, false));
    tables.slices.push_back(window_slice("slices/test/wave.dynamicslice", 0x74, 0.2F, true));
    tables.spells.push_back({kNovaSpell, 2});
    tables.spells.push_back({kWaveSpell, 3});
    tables.spells.push_back({kShotSpell, 3});
    return tables;
}

/** A cast of a strike at the caster: `aim` is AIAimAtTarget (the nova: true with UseAllAvailableTargets; the shockwave: false). */
inline gamedata::AreaCast caster_cast(const char* spell, float time, bool aim, float factor, const char* type, std::uint8_t wire,
                                      gamedata::DamageCategory category) {
    gamedata::AreaCast cast;
    cast.time = time;
    cast.spell = spell;
    cast.shape = gamedata::AreaShape::strike;
    cast.aimAtTarget = aim;
    cast.aimSpeed = 100.0F;
    cast.leadAngle = 10.0F;
    cast.aimAngle = 45.0F;
    cast.missMin = 0.5F;
    cast.missMax = 1.0F;
    cast.allTargets = true;
    cast.damage.time = time;
    cast.damage.factor = factor;
    cast.damage.type = type;
    cast.damage.wire = wire;
    cast.damage.category = category;
    cast.damage.row = spell;
    return cast;
}

/** A slice of the archer of the cases with no shot, its bash, and one more action (index 6). */
inline gamedata::CreatureSlice support_slice(const char* path, std::uint8_t mark, const char* action, std::int32_t fragment) {
    gamedata::CreatureSlice slice = range::archer_slice(true);
    slice.path = path;
    slice.asset.fill(mark);
    slice.root = 0x1000000000000000ULL | mark;
    slice.ranged.clear();
    slice.actions.push_back({action, action, fragment, false});
    return slice;
}

/** The slices of the cases: the cleric (0), the hound (1), the ghost (2), and the knight (3). */
inline std::vector<gamedata::CreatureSlice> support_slices() {
    gamedata::CreatureSlice cleric = support_slice("slices/test/cleric.dynamicslice", 0x41, "InqCleric_Rekindle_Self", 122);
    cleric.statusActions = {gamedata::CreatureStatusAction{kSupportAction, kRekindleLength, {gamedata::StatusMark{kRekindleMark, {kRekindle}}}}};
    cleric.lowHealth = kLowHealth;
    gamedata::CreatureSlice hound = support_slice("slices/test/hound.dynamicslice", 0x42, "Howl", 123);
    hound.statusActions = {gamedata::CreatureStatusAction{kSupportAction, kHowlLength, {gamedata::StatusMark{kHowlMark, {kHaste}}}}};
    hound.lowHealth = 0.85F;
    gamedata::CreatureSlice ghost = support_slice("slices/test/ghost.dynamicslice", 0x43, "Spell_Plagued_Plague_Nova", 124);
    ghost.areas = {gamedata::CreatureAreaAttack{
        kSupportAction, 1.766667F, {caster_cast(kNovaSpell, 0.866667F, true, 1.0F, "Nature", 14, gamedata::DamageCategory::elemental)}}};
    gamedata::CreatureSlice knight = support_slice("slices/test/knight.dynamicslice", 0x44, "Attack_Shockwave", 125);
    knight.areas = {gamedata::CreatureAreaAttack{
        kSupportAction, 1.9F, {caster_cast(kWaveSpell, 1.0F, false, 0.4F, "Strike", 6, gamedata::DamageCategory::physical)}}};
    return {cleric, hound, ghost, knight};
}

inline std::vector<gamedata::CreatureKind> support_kinds() {
    return {area_kind("Test_Cleric", 0, 0x41112222), area_kind("Test_Hound", 1, 0x42112222), area_kind("Test_Ghost", 2, 0x43112222),
            area_kind("Test_Knight", 3, 0x44112222)};
}

/** The world of a case: the area world (A with a client, B next to A) with the four slices of stage 3.4f-D and their data. */
struct Support : Areas {
    explicit Support(bool observe = true, std::vector<gamedata::SpawnPoint> points = {}) : Areas(observe) {
        if (!host.creatures.set_full(support_slices(), support_kinds(), std::move(points), archer_levels(), {}, {arrow_slice()},
                                     gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}})) {
            throw std::runtime_error("support test: the creature data is not valid: " + host.creatures.error());
        }
        if (!host.abilities.set(support_abilities())) {
            throw std::runtime_error("support test: the ability data is not valid: " + host.abilities.error());
        }
        if (!host.spells.set(support_spells())) {
            throw std::runtime_error("support test: the spell data is not valid: " + host.spells.error());
        }
    }
    world::Entity* make(const char* kind, float east, float north = 0.0F, float yaw = 90.0F) {
        return host.world.creatures().spawn(kind, at(kPlace.x + east, kPlace.y + north), yaw).entity;
    }
    const world::CreatureCounts& counts() {
        return host.world.creatures().counts();
    }
    /** @return The effect of a status on a creature, or null. */
    static const world::CreatureEffect* effect(const world::Entity& creature, const char* status) {
        for (const world::CreatureEffect& item : creature.creature.effects) {
            if (item.status == status) {
                return &item;
            }
        }
        return nullptr;
    }
};

} // namespace soulwarden::backend::tests::support
