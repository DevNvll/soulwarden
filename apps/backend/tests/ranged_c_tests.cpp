// Tests of stage 3.4f-C: the ground areas of the creatures and the hostile statuses that they put on a player (research
// creature-ranged.md Q4 and part C; live check L8 of the stage; static checks STATIC-AREAS.md and STATIC-CAST.md of the evidence).
//
//   CRA1  the mushroom of the Dryad Shaman: the action Shaman_Bomb, its three casts at their times, one area at the target at each cast,
//         the record of each session (the metadata, the PositionInTheWorld state at slot 1, the delayed events; no Spell state), a late
//         observer (the cloud activation completed), the end at 26 s (the stop for each session)
//   CRA2  the poison of the area: the first application 0.5 s after the entry into the active cloud (1 s), the next ones each 1 s, the
//         ticks of 2 s (2 % of the maximum health and 4 % of the core damage of the creature), the HUD status of the owner, the exit
//         (the status goes), the entry again (a new first delay); two characters
//   CRA3  the lightning strike of the Undead Shaman: one hit of the row in the window 0.9 s to 0.95 s for each character in its sphere,
//         none before and none after, the Spell state with the caster of each session (the same bytes), the end at 5 s
//   CRA4  the ends: the death of the caster ends the strike (EndOnCasterDeath) and not the mushroom, the leave of the caster ends both, a
//         wall does not stop a status area (no line test in the server facet) or a strike with no CheckObstructions
//   CRA5  two characters and two areas that overlap: one status of the row, a second area keeps it, the exit of both ends it
//   CRA6  the behavior: the cast in the CastDistance, the repeat time, the walk of a creature with only an area attack, the cancels of a
//         cast (a knockdown, the death of the creature), the place (the CastDistance, the aim angle, the floor)
//   CRA7  the loaders: the area attacks of the creature data, the format 4 of the spell data, a cast whose spell has no data
//   CRA8  the dodge: a strike in the immune window does no damage; a poison tick in the immune window does no damage (a row with
//         IgnoreInvulnerable does); the application of the area is not stopped
//   CRA9  the load: 32,840 far spawn points, 50 characters, a shaman and a striker near each, 200 areas more; the steps of the areas look
//         only near each area (the same looks with 840 points), with a time limit
//
// The accounts, the characters, the creature kinds, the rows, and the places are made up; the values of the rows and slices are the values
// of the retail data (support/area_fixture.h).

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "server/rep/health_messages.h"
#include "support/area_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/test_navigation.h"
#include "support/test_progression.h"
#include "world/stamina.h"

namespace soulwarden::backend::tests {
namespace {

using namespace area;
using std::chrono::milliseconds;

/** @return The fragments of a slot and a type of the records of an entity in a session from the message `from` on. */
std::vector<Fragment> entity_fragments(Areas& world, const FakeClient& client, std::uint32_t session, world::EntityId id, std::uint32_t slot,
                                       std::uint32_t type, std::size_t from = 0) {
    std::vector<Fragment> found;
    const std::uint32_t interest = interest_in(world.host, session, id);
    if (interest == 0) {
        return found;
    }
    for (const Fragment& fragment : fragments_for(client, interest, from)) {
        if (fragment.slot == slot && fragment.type == type) {
            found.push_back(fragment);
        }
    }
    return found;
}

/** @return The completed byte of a delayed event state (field 1). */
std::uint8_t completed_of(const Fragment& fragment) {
    const auto found = fragment.fields.find({0, 1});
    return found != fragment.fields.end() && !found->second.empty() ? found->second[0] : 0xFF;
}

/** @return The time that is left of a delayed event state (field 0, a half float). */
float remaining_of(const Fragment& fragment) {
    const auto found = fragment.fields.find({0, 0});
    if (found == fragment.fields.end() || found->second.size() != 2) {
        return -1.0F;
    }
    const auto bits = static_cast<std::uint16_t>((found->second[0] << 8) | found->second[1]);
    const int exponent = (bits >> 10) & 0x1F;
    const int mantissa = bits & 0x3FF;
    const float value = exponent == 0 ? std::ldexp(static_cast<float>(mantissa), -24)
                                      : std::ldexp(static_cast<float>(mantissa | 0x400), exponent - 25);
    return (bits & 0x8000) != 0 ? -value : value;
}

/** Moves a character with no walker of its place (B) to a place: a travel with no screen and one position report. */
void move_b(Areas& world, float east, float north) {
    (void)world.host.world.travel(kCharacterB, at(kPlace.x + east, kPlace.y + north), world::TravelScreen::off);
    static std::uint16_t input = 400;
    world.host.datagram(world.b.peer, world.b.report_at(input++, kPlace.x + east, kPlace.y + north, kHeight));
    world.step();
}

/** CRA1: the mushroom of the Dryad Shaman. */
void mushroom(const Options&) {
    Areas world(true);
    world.enter_both();
    world::Entity& a = *world.character();
    // The shaman looks away from A (no vision): the command gives it A as its target; the behavior runs the casts of the action.
    world::Entity* shaman = world.shaman(10.0F, 0.0F, 270.0F);
    world.step(5);
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    const auto start = world.now();
    check(world.behavior().command_area(*shaman, a.id) && shaman->creature.action && shaman->creature.action->action == kBombAction,
          "the shaman starts its area attack Shaman_Bomb (the action of the data) at A");
    world.to(start + milliseconds(1150));
    check(world.areas_of(world::SpellRole::statusArea).empty(), "no area before the first cast of the data (1.2 s)");
    world.to(start + milliseconds(1250));
    const std::vector<world::EntityId> first = world.areas_of(world::SpellRole::statusArea);
    const world::Entity* cloud = first.size() == 1 ? world.host.world.find(first.front()) : nullptr;
    check(cloud != nullptr && world::horizontal_distance(cloud->position, a.position) < 0.3F && std::fabs(cloud->position.z - kHeight) < 0.01F
              && cloud->spell.caster == shaman->id && cloud->spell.casterActor == shaman->actor,
          "one area at 1.2 s at the place of A, on its floor, with the shaman as its caster");
    world.to(start + milliseconds(3300));
    check(world.areas_of(world::SpellRole::statusArea).size() == 3, "three areas after the three casts (1.2 s, 2.2 s, 3.13 s)");
    world.step(3);
    // The record of each session: the metadata, the PositionInTheWorld state at slot 1 of the slice, and the three delayed events (live
    // check L8); no Spell state.
    const world::EntityId id = first.front();
    const auto places = entity_fragments(world, world.a, 1, id, kMushroomPlaceSlot, 13, fromA);
    const auto placesB = entity_fragments(world, world.b, 2, id, kMushroomPlaceSlot, 13, fromB);
    const auto events = entity_fragments(world, world.a, 1, id, 3, 3139, fromA);
    check(places.size() == 1 && placesB.size() == 1 && places[0].fields == placesB[0].fields && events.size() == 1
              && completed_of(events[0]) == 0 && std::fabs(remaining_of(events[0]) - kCloudAt) < 0.05F
              && entity_fragments(world, world.a, 1, id, kStrikeSpellSlot, 2912, fromA).empty(),
          "each session gets the place of the mushroom at slot 1 (the same bytes) and the cloud event of 1 s; no Spell state");
    // A late observer: B goes far before the cast of a new mushroom and comes back 8 s later.
    move_b(world, -500.0F, 0.0F);
    world.step(10);
    const world::Entity* late = world.area_at(*shaman, data_cast(world, 0), at(kPlace.x + 3.0F, kPlace.y));
    const world::EntityId lateId = late->id;
    const auto lateStart = world.now();
    world.to(lateStart + milliseconds(8000));
    const std::size_t fromLate = world.b.got.size();
    move_b(world, 2.0F, 3.0F);
    world.step(4);
    const auto cloudLate = entity_fragments(world, world.b, 2, lateId, 3, 3139, fromLate);
    const auto tellLate = entity_fragments(world, world.b, 2, lateId, 4, 3139, fromLate);
    const auto endLate = entity_fragments(world, world.b, 2, lateId, 2, 3139, fromLate);
    check(cloudLate.size() == 1 && completed_of(cloudLate[0]) == 1 && tellLate.size() == 1 && completed_of(tellLate[0]) == 1
              && endLate.size() == 1 && completed_of(endLate[0]) == 0 && std::fabs(remaining_of(endLate[0]) - 18.0F) < 0.6F,
          "a late observer (8 s): the cloud activation and the tell deactivation are completed (live check L8, M4b), the end 18 s later");
    // The end at 26 s: the stop for each session.
    const std::uint32_t interestA = interest_in(world.host, 1, id);
    const std::uint32_t interestB = interest_in(world.host, 2, id);
    world.to(start + milliseconds(27500)); // The first cast at 1.2 s, its end 26 s later.
    world.step(3);
    note("the end: A interest " + std::to_string(interestA) + " stops " + std::to_string(stops_of(world.a, interestA, fromA)) + ", B interest "
         + std::to_string(interestB) + " stops " + std::to_string(stops_of(world.b, interestB, fromLate)) + ", entity "
         + (world.host.world.find(id) == nullptr ? "gone" : "there"));
    check(world.host.world.find(id) == nullptr && stops_of(world.a, interestA, fromA) == 1 && interestB != 0
              && stops_of(world.b, interestB, fromLate) == 1,
          "the first mushroom ends 26 s after its cast (the destroy event of the slice): the stop for each session");
}

/** CRA2: the poison of the area. */
void poison(const Options&) {
    Areas world(true);
    world.enter_both();
    world.host.world.set_creature_behavior(false);
    world::Entity& a = *world.character();
    world::Entity& b = *world.host.world.find_character(kCharacterB);
    world::Entity* shaman = world.shaman(10.0F);
    world.step(3);
    // The area 1 m east of A: A is in the cylinder (1.5 m and the capsule 0.41 m), B (1 m west and 1 m north of A) is not.
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    world::Entity* cloud = world.area_at(*shaman, data_cast(world, 0), at(kPlace.x + 1.0F, kPlace.y));
    check(cloud != nullptr, "setup: a mushroom 1 m east of A");
    const auto start = world.now();
    world.to(start + milliseconds(1450));
    check(!world.has_poison(kCharacterA), "no status before the first application: the cloud at 1 s and the initial delay 0.5 s");
    world.to(start + milliseconds(1560));
    check(world.has_poison(kCharacterA) && !world.has_poison(kCharacterB), "A gets the poison 0.5 s after its entry into the active cloud; B not");
    check(!fragments_of(world.a, 44, 4236, fromA).empty(), "the owner A gets its status state (the HUD icon of the poison)");
    const float before = a.vitals.health;
    world.step(2);
    const float tick = a.vitals.health;
    const float expected = -kPoisonPercent * a.vitals.healthMax - kPoisonDamageBased * kCoreDamage;
    note("one tick: " + std::to_string(before - tick) + " damage, expected " + std::to_string(expected) + " (maximum "
         + std::to_string(a.vitals.healthMax) + ")");
    check(std::fabs((before - tick) - expected) < 0.5F,
          "the first tick: 2 percent of the maximum health and 4 percent of the core damage of the creature (the damage source)");
    check(messages_of(world.a, server::rep::kTypeDamageTaken, fromA).size() == 1 && messages_of(world.b, server::rep::kTypeDamageTaken, fromB).empty(),
          "the owner gets one damage mark for the tick; B gets none");
    world.to(start + milliseconds(3500));
    const float second = a.vitals.health;
    world.to(start + milliseconds(3700));
    check(std::fabs((second - a.vitals.health) - expected) < 0.5F || std::fabs((tick - second) - expected) < 0.5F,
          "the next tick 2 s later (TickRate 2)");
    const std::uint64_t applications = world.spells().counts().areaApplications;
    world.to(start + milliseconds(5600));
    check(world.spells().counts().areaApplications >= applications + 2, "the area applies the poison again each 1 s while A is in it");
    // B comes into the cloud: its first application 0.5 s later.
    move_b(world, 1.5F, 0.5F);
    const auto entry = world.now();
    world.to(entry + milliseconds(400));
    check(!world.has_poison(kCharacterB), "B has no poison 0.4 s after its entry");
    world.to(entry + milliseconds(700));
    check(world.has_poison(kCharacterB), "B gets the poison about 0.5 s after its entry");
    // B goes out: its poison goes at once (m_removeOnExit).
    move_b(world, -6.0F, 0.0F);
    world.step(4);
    check(!world.has_poison(kCharacterB) && world.has_poison(kCharacterA), "B leaves the cloud: its poison ends; A keeps it");
    // B comes in again: a new initial delay.
    move_b(world, 1.5F, 0.5F);
    const auto again = world.now();
    world.to(again + milliseconds(350));
    check(!world.has_poison(kCharacterB), "B in the cloud again: no poison before the initial delay");
    world.to(again + milliseconds(700));
    check(world.has_poison(kCharacterB), "B in the cloud again: the poison comes again after the initial delay");
    (void)b;
}

/** CRA3: the lightning strike. */
void strike(const Options&) {
    Areas world(true);
    world.enter_both();
    world.host.world.set_creature_behavior(false);
    world::Entity& a = *world.character();
    world::Entity& b = *world.host.world.find_character(kCharacterB);
    world::Entity* striker = world.striker(10.0F);
    world.step(3);
    const std::size_t fromA = world.a.got.size();
    const std::size_t fromB = world.b.got.size();
    const float healthA = a.vitals.health;
    const float healthB = b.vitals.health;
    world::Entity* bolt = world.area_at(*striker, data_cast(world, 1), at(kPlace.x, kPlace.y));
    check(bolt != nullptr && bolt->spell.role == world::SpellRole::strike && bolt->spell.endsOnCasterDeath, "setup: a strike at the place of A");
    const world::EntityId id = bolt->id;
    const auto start = world.now();
    world.to(start + milliseconds(850));
    check(a.vitals.health == healthA && b.vitals.health == healthB, "no damage before the damage window (0.9 s)");
    world.to(start + milliseconds(1000));
    const float lossA = healthA - a.vitals.health;
    const float lossB = healthB - b.vitals.health;
    note("the strike: A " + std::to_string(lossA) + ", B " + std::to_string(lossB));
    check(lossA > 0.0F && lossB > 0.0F && world.spells().counts().strikeHits == 2, "one hit of the row for A and for B in the window");
    world.to(start + milliseconds(3000));
    check(healthA - a.vitals.health == lossA && healthB - b.vitals.health == lossB && world.spells().counts().strikeHits == 2,
          "no second hit after the window");
    check(messages_of(world.a, server::rep::kTypeDamageTaken, fromA).size() == 1 && messages_of(world.b, server::rep::kTypeDamageTaken, fromB).size() == 1,
          "each owner gets one damage mark");
    const auto spellA = entity_fragments(world, world.a, 1, id, kStrikeSpellSlot, 2912, fromA);
    const auto spellB = entity_fragments(world, world.b, 2, id, kStrikeSpellSlot, 2912, fromB);
    check(spellA.size() == 1 && spellB.size() == 1 && spellA[0].fields == spellB[0].fields && spellA[0].fields.count({0, 7}) == 1,
          "each session gets the Spell state at slot 1 with the caster reference (the same bytes)");
    const std::uint32_t interestA = interest_in(world.host, 1, id);
    world.to(start + milliseconds(5100));
    world.step(2);
    check(world.host.world.find(id) == nullptr && stops_of(world.a, interestA, fromA) == 1, "the strike ends at 5 s (the Duration of the row)");
    // A character that comes into the sphere after the window gets no hit.
    world::Entity* second = world.area_at(*striker, data_cast(world, 1), at(kPlace.x - 6.0F, kPlace.y));
    const float healthLate = b.vitals.health;
    world.to(world.now() + milliseconds(1000));
    move_b(world, -6.0F, 0.0F);
    world.step(10);
    check(second != nullptr && b.vitals.health == healthLate, "a character that comes into the sphere after the window gets no hit");
}

/** A navigation folder with a wall cell 3 m high from x 2000.5 to 2000.75 (0.5 m east of A). */
std::string wall_folder(const Options& options, const char* name) {
    TestNavRegion region;
    region.tiles[{31, 31}] = [](int x, int, std::vector<TestFloor>& floors) {
        const float px = 1984.0F + (static_cast<float>(x) + 0.5F) * 0.25F;
        if (px >= 2000.5F && px < 2000.75F) {
            floors = {{kHeight + 3.0F, gamedata::NavFloor::kBlocked}};
        }
    };
    const auto folder = options.workDirectory / name;
    region.write(folder);
    return folder.string();
}

/** CRA4: the ends and the walls. */
void ends(const Options& options) {
    Areas world(false);
    world.host.world.set_creature_behavior(false);
    world::Entity& a = *world.character();
    world::Entity* striker = world.striker(10.0F);
    world::Entity* shaman = world.shaman(-10.0F);
    world.step(3);
    const float health = a.vitals.health;
    world::Entity* bolt = world.area_at(*striker, data_cast(world, 1), at(kPlace.x, kPlace.y));
    world::Entity* cloud = world.area_at(*shaman, data_cast(world, 0), at(kPlace.x + 0.5F, kPlace.y));
    const world::EntityId boltId = bolt->id;
    const world::EntityId cloudId = cloud->id;
    world.step(10);
    (void)world.host.world.creatures().kill(striker->id);
    (void)world.host.world.creatures().kill(shaman->id);
    world.step(2);
    check(world.host.world.find(boltId) == nullptr, "the death of the caster ends the strike (EndOnCasterDeath of the row)");
    world.to(world.now() + milliseconds(1500));
    check(world.host.world.find(cloudId) != nullptr && world.spells().counts().strikeHits == 0,
          "the mushroom stays after the death of its caster (no EndOnCasterDeath); the strike did no hit");
    (void)health;
    world.to(world.now() + milliseconds(1000));
    check(world.has_poison(kCharacterA), "the mushroom of a dead caster still gives its poison");
    // The leave of the caster (the creature goes out of the world) ends its areas.
    world::Entity* other = world.shaman(-12.0F);
    world::Entity* cloud2 = world.area_at(*other, data_cast(world, 0), at(kPlace.x + 8.0F, kPlace.y));
    const world::EntityId cloud2Id = cloud2->id;
    world.step(2);
    (void)world.host.world.creatures().remove(other->id);
    world.step(2);
    check(world.host.world.find(cloud2Id) == nullptr, "the leave of the caster ends its areas");
    // A wall between the center and the character: the status area has no line test (client code: the server facet of the
    // AreaStatusEffectComponent has none), and the strike has none with no CheckObstructions in its row.
    for (const bool obstructions : {false, true}) {
        Areas walled(false);
        walled.host.world.set_creature_behavior(false);
        if (obstructions) {
            gamedata::AbilityTables tables = area_abilities();
            for (gamedata::SpellRow& row : tables.spells) {
                if (row.id == kStrikeSpell) {
                    row.cells.numbers["CheckObstructions"] = 1.0F;
                }
            }
            check(walled.host.abilities.set(tables), "setup: a strike row with CheckObstructions (a made-up row for the control)");
        }
        check(walled.host.navigation.set_directory(wall_folder(options, obstructions ? "area-wall-check" : "area-wall")),
              "setup: the navigation data with a wall 0.5 m east of A");
        walled.step(20);
        world::Entity& own = *walled.character();
        world::Entity* caster = walled.striker(10.0F);
        world::Entity* mage = walled.shaman(12.0F);
        const float before = own.vitals.health;
        (void)walled.area_at(*caster, data_cast(walled, 1), at(kPlace.x + 1.2F, kPlace.y));
        if (!obstructions) {
            (void)walled.area_at(*mage, data_cast(walled, 0), at(kPlace.x + 1.2F, kPlace.y));
        }
        walled.to(walled.now() + milliseconds(1700));
        if (obstructions) {
            check(walled.spells().counts().strikeHits == 0 && own.vitals.health == before,
                  "control: a strike row with CheckObstructions does not hit through the wall");
        } else {
            check(walled.spells().counts().strikeHits == 1 && walled.has_poison(kCharacterA),
                  "a wall between the center and A: the strike (no CheckObstructions) hits, and the cloud gives its poison");
        }
    }
}

/** CRA5: two characters and two areas that overlap. */
void overlap(const Options&) {
    Areas world(true);
    world.enter_both();
    world.host.world.set_creature_behavior(false);
    world::Entity* shaman = world.shaman(10.0F);
    world::Entity* second = world.shaman(-10.0F);
    world.step(3);
    // Two mushrooms over A (1 m east and 1 m west); B (1 m west, 1 m north) is in the west one.
    world::Entity* east = world.area_at(*shaman, data_cast(world, 0), at(kPlace.x + 1.0F, kPlace.y));
    world::Entity* west = world.area_at(*second, data_cast(world, 0), at(kPlace.x - 1.0F, kPlace.y));
    check(east != nullptr && west != nullptr, "setup: two mushrooms of two creatures that overlap at A");
    const world::EntityId eastId = east->id;
    world.to(world.now() + milliseconds(1700));
    const auto* statuses = world.host.world.statuses().statuses_of(kCharacterA);
    const std::size_t rows = statuses == nullptr ? 0 : static_cast<std::size_t>(std::count_if(statuses->begin(), statuses->end(), [](const world::ActiveStatus& s) {
                                                         return s.status == kPoison;
                                                     }));
    check(rows == 1 && world.has_poison(kCharacterB), "A in two clouds has one status of the row (StackMax 1); B in one cloud has it too");
    // The east area ends (a command): A is still in the west one, and the poison is there one application later.
    (void)world.spells().end(eastId, world::SpellEnd::command);
    world.to(world.now() + milliseconds(1100));
    check(world.has_poison(kCharacterA), "the end of one area: the other one gives the poison again (one application later)");
    // B leaves the west area: its poison goes.
    move_b(world, -8.0F, 6.0F);
    world.step(3);
    check(!world.has_poison(kCharacterB), "B out of each area: no poison");
}

/** CRA6: the behavior. */
void behavior(const Options&) {
    {
        Areas world(false);
        world::Entity& a = *world.character();
        world::Entity* shaman = world.shaman(10.0F);
        world.behavior().hurt_by(*shaman, a.id);
        check(world.until([&] { return world.behavior().counts().areaAttacks >= 1; }, 30), "a shaman with a target in 30 m starts its area attack");
        const auto first = world.now();
        world.until([&] { return world.behavior().counts().areaAttacks >= 2; }, 600);
        const double gap = std::chrono::duration<double>(world.now() - first).count();
        note("the next area attack " + std::to_string(gap) + " s later");
        check(gap >= 9.9 && gap < 15.0,
              "a creature with a different attack: the next area attack not before kAreaRepeat (10 s), after its melee attacks");
    }
    {
        // A creature with a basic shot: the shot repeat (kShotRepeat) ends before the pause of a melee attack (kAttackPause after the
        // end of the shot). The area attack waits for the earlier of the two (live run of stage 3.4f-C: the Dryad Shaman shot for 40 s
        // and did not cast its area again).
        Areas world(false);
        world::Entity& a = *world.character();
        world::Entity* thrower = world.thrower(10.0F);
        world.behavior().hurt_by(*thrower, a.id);
        check(world.until([&] { return world.behavior().counts().areaAttacks >= 1; }, 30), "a shaman with a shot starts its area attack");
        const auto first = world.now();
        const std::uint64_t shots = world.behavior().counts().releases;
        world.until([&] { return world.behavior().counts().areaAttacks >= 2; }, 600);
        const double gap = std::chrono::duration<double>(world.now() - first).count();
        note("the next area attack of the shaman with a shot " + std::to_string(gap) + " s later, after "
             + std::to_string(world.behavior().counts().releases - shots) + " shot(s)");
        check(world.behavior().counts().areaAttacks >= 2 && gap >= 9.9 && gap < 15.0 && world.behavior().counts().releases > shots,
              "a creature with a shot: its shots, then the next area attack after kAreaRepeat (10 s)");
    }
    {
        Areas world(false);
        world::Entity& a = *world.character();
        world::Entity* striker = world.striker(16.0F);
        world.behavior().hurt_by(*striker, a.id);
        world.until([&] { return world.behavior().counts().areaAttacks >= 1; }, 200);
        const float away = world::horizontal_distance(striker->position, a.position);
        note("the striker casts at " + std::to_string(away) + " m");
        check(world.behavior().counts().areaAttacks == 1 && away <= 12.05F && away >= 9.5F,
              "a creature with only an area attack walks into the CastDistance (12 m, to 10 m) and casts");
        const auto first = world.now();
        world.until([&] { return world.behavior().counts().areaAttacks >= 2; }, 400);
        const double gap = std::chrono::duration<double>(world.now() - first).count();
        check(gap >= 4.9 && gap < 5.6, "the next strike about kAreaOnlyRepeat (5 s) later");
    }
    {
        // The cancels: a knockdown before the cast, the death of the creature before the cast.
        Areas world(false);
        world::Entity& a = *world.character();
        world::Entity* striker = world.striker(8.0F, 0.0F, 270.0F);
        check(world.behavior().command_area(*striker, a.id), "setup: the striker starts its strike");
        world.step(10);
        check(world.host.world.creatures().knock(*striker, 4, milliseconds(2000)), "setup: a knockdown before the cast (0.93 s)");
        world.to(world.now() + milliseconds(1500));
        check(world.areas_of(world::SpellRole::strike).empty() && world.behavior().counts().castsCancelled == 1,
              "a knockdown before the cast: no area (a cast of a different action)");
        world::Entity* doomed = world.striker(-8.0F, 0.0F, 90.0F + 180.0F);
        check(world.behavior().command_area(*doomed, a.id), "setup: a second striker starts its strike");
        world.step(10);
        (void)world.host.world.creatures().kill(doomed->id);
        world.to(world.now() + milliseconds(1500));
        check(world.areas_of(world::SpellRole::strike).empty() && world.behavior().counts().castsCancelled == 2,
              "the death of the creature before the cast: no area");
    }
    {
        // The place: the CastDistance of the row (30 m) from the creature, the floor of the place.
        Areas world(false);
        world::Entity& a = *world.character();
        world::Entity* shaman = world.shaman(40.0F);
        shaman->creature.yaw = 90.0F;
        const gamedata::AreaCast& cast = data_cast(world, 0);
        const state::WorldPosition place = world.behavior().area_place(*shaman, a, cast);
        check(std::fabs(world::horizontal_distance(place, shaman->position) - 30.0F) < 0.05F && std::fabs(place.z - kHeight) < 0.01F,
              "a target at 40 m: the area at 30 m (the CastDistance), on the floor");
        shaman->creature.yaw = 0.0F; // The shaman looks north: A to the west is 90 degrees from its front.
        const state::WorldPosition turned = world.behavior().area_place(*shaman, a, cast);
        const float angle = std::fabs(std::atan2(turned.y - shaman->position.y, turned.x - shaman->position.x) * 180.0F / 3.14159265F - 90.0F);
        check(std::fabs(angle - 45.0F) < 0.5F, "a target out of the aim angle (45 degrees): the place at the limit angle");
    }
}

/** CRA7: the loaders. */
void loaders(const Options&) {
    Areas world(false);
    // The creature data: an area attack with a cast after the end of its action, or with an action with no fragment, is refused.
    gamedata::CreatureSlice late = shaman_slice();
    late.areas.front().casts.back().time = kBombLength + 1.0F;
    gamedata::CreatureData data;
    check(!data.set_full({late}, {area_kind("Test_Shaman", 0, 0x33112222)}, {}, archer_levels()), "a cast after the end of its action is refused");
    gamedata::CreatureSlice noFragment = shaman_slice();
    noFragment.actions[kBombAction].fragmentIndex = -1;
    check(!data.set_full({noFragment}, {area_kind("Test_Shaman", 0, 0x33112222)}, {}, archer_levels()), "an area action with no fragment is refused");
    gamedata::CreatureSlice noType = striker_slice();
    noType.areas.front().casts.front().damage.type.clear();
    check(!data.set_full({noType}, {area_kind("Test_Striker", 0, 0x34112222)}, {}, archer_levels()), "a strike with no damage type is refused");
    check(data.set_full({shaman_slice()}, {area_kind("Test_Shaman", 0, 0x33112222)}, {}, archer_levels()) && data.slices()[0].fights()
              && data.area_slices() == 1,
          "the area attacks load; a slice with an area attack fights");
    // The spell data of format 4: a negative delay of an area status is refused.
    gamedata::SpellTables tables = area_spells();
    tables.slices[0].areaStatuses[0].reapplyDelay = -1.0F;
    gamedata::SpellData spells;
    check(!spells.set(tables), "an area status with a negative delay is refused");
    tables = area_spells();
    tables.slices[0].volumes[0].height = -4.0F;
    check(!spells.set(tables), "a volume with a negative height is refused");
    // A cast whose spell has no slice in the spell data is not usable: the creature makes no area, and no other rule changes.
    gamedata::SpellTables none = area_spells();
    none.slices.erase(none.slices.begin());
    none.spells = {{kStrikeSpell, 0}};
    check(world.host.spells.set(none), "setup: spell data with no slice of the mushroom");
    check(!world.spells().area_usable(data_cast(world, 0)) && world.spells().area_usable(data_cast(world, 1)),
          "a cast whose spell has no slice is not usable; the strike is");
}

/** CRA8: the dodge with the areas. */
void dodge(const Options&) {
    Areas world(false);
    world.host.world.set_creature_behavior(false);
    world::Entity& a = *world.character();
    world::Entity* striker = world.striker(10.0F);
    world.step(3);
    const float health = a.vitals.health;
    (void)world.area_at(*striker, data_cast(world, 1), at(kPlace.x, kPlace.y));
    world.to(world.now() + milliseconds(700));
    world.walker.bits({322});
    world.to(world.now() + milliseconds(400));
    check(a.vitals.health == health && world.host.world.stamina().counts().evaded == 1 && world.spells().counts().strikeHits == 1,
          "a strike at 0.9 s in the immune window of a dodge (from 0.7 s): no damage (the hit is evaded)");
    // The poison tick in the immune window: no damage; out of the window: damage. The application is not stopped.
    world::Entity* shaman = world.shaman(-10.0F);
    (void)world.area_at(*shaman, data_cast(world, 0), at(kPlace.x + 0.5F, kPlace.y));
    const auto cloud = world.now();
    world.to(cloud + milliseconds(1420));
    const float before = a.vitals.health;
    world.walker.bits({322});
    world.to(cloud + milliseconds(1650));
    check(world.has_poison(kCharacterA), "the application of the area in the window of a dodge is not stopped (the poison comes)");
    check(a.vitals.health == before, "the first poison tick in the immune window of a dodge does no damage (no IgnoreInvulnerable in the row)");
    world.to(cloud + milliseconds(3800));
    check(a.vitals.health < before, "the next tick out of the window does damage");
}

/** CRA9: the load. */
void area_load(const Options&) {
    const auto points = [](std::size_t count) {
        std::vector<gamedata::SpawnPoint> list;
        for (std::size_t index = 0; index < count; ++index) {
            gamedata::SpawnPoint point;
            point.id = "p" + std::to_string(index);
            point.kind = 0;
            point.level = 1;
            point.position = state::WorldPosition{10000.0F + static_cast<float>(index % 200) * 20.0F,
                                                  10000.0F + static_cast<float>(index / 200) * 20.0F, kHeight};
            point.ground = kHeight;
            list.push_back(point);
        }
        return list;
    };
    struct Result {
        double milliseconds{0.0};
        world::SpellCounts spells;
        std::uint64_t areaAttacks{0};
        std::uint64_t damageTicks{0};
    };
    const auto run = [&](std::size_t count) {
        ServerHost host;
        host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
        check(host.creatures.set_full({shaman_slice(), striker_slice()},
                                      {area_kind("Test_Shaman", 0, 0x33112222), area_kind("Test_Striker", 1, 0x34112222)}, points(count),
                                      archer_levels(), {}, {arrow_slice()},
                                      gamedata::CreatureCapsule{kPlayerRadius, kPlayerSegment, {0.0F, 0.0F, 1.0F}, {0.0F, 0.0F, 0.0F}}),
              "setup: the shaman and the striker with " + std::to_string(count) + " spawn points");
        check(host.combat.set(shop::shop_combat()) && host.abilities.set(area_abilities()) && host.spells.set(area_spells()),
              "setup: the combat tables, the ability data, and the spell data");
        stamina::set_stamina(host);
        const unsigned kPlayers = 50;
        for (unsigned index = 0; index < kPlayers; ++index) {
            char id[40];
            std::snprintf(id, sizeof(id), "%08x-3333-4000-8000-%012x", 0x30000000U + index, index);
            add_player(host.store, "765612022553" + std::to_string(30000 + index), "Tester " + std::to_string(index), id,
                       "Area" + std::to_string(index), std::nullopt);
            const state::WorldPosition place{500.0F + 120.0F * static_cast<float>(index % 10), 500.0F + 120.0F * static_cast<float>(index / 10),
                                             kHeight};
            (void)host.world.spawn_character(id, place);
            // A shaman 10 m east and a striker 10 m west of each character, their faces to it: they see it and cast again and again.
            world::Entity* shaman = host.world.creatures().spawn("Test_Shaman", {place.x + 10.0F, place.y, kHeight}, 90.0F).entity;
            (void)host.world.creatures().spawn("Test_Striker", {place.x - 10.0F, place.y, kHeight}, 270.0F);
            // Four more mushrooms near each character: 200 areas more at the start.
            const gamedata::AreaCast& cast = host.creatures.slices()[0].areas.front().casts.front();
            for (int extra = 0; extra < 4; ++extra) {
                (void)host.world.spells().make_area(
                    world::AreaRequest{shaman->id, &cast, {place.x + 2.0F * static_cast<float>(extra), place.y + 3.0F, kHeight}});
            }
        }
        Result result;
        const auto started = std::chrono::steady_clock::now();
        for (int tick = 0; tick < 1000; ++tick) {
            host.clock.advance(std::chrono::milliseconds(33));
            host.world.tick();
        }
        result.milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        result.spells = host.world.spells().counts();
        result.areaAttacks = host.world.creatures().behavior().counts().areaAttacks;
        result.damageTicks = host.world.statuses().counts().damageTicks;
        return result;
    };
    const Result full = run(32840);
    const Result few = run(840);
    note("32,840 points, 50 characters, 50 shamans and 50 strikers: 1000 ticks need " + std::to_string(full.milliseconds) + " ms; "
         + std::to_string(full.spells.areasMade) + " areas, " + std::to_string(full.spells.largest) + " spells at most, "
         + std::to_string(full.spells.areaSteps) + " area steps, " + std::to_string(full.spells.looked) + " entities looked at, "
         + std::to_string(full.spells.strikeHits) + " strike hits, " + std::to_string(full.damageTicks) + " poison ticks, "
         + std::to_string(full.areaAttacks) + " area attacks; with 840 points: " + std::to_string(few.milliseconds) + " ms, "
         + std::to_string(few.spells.looked) + " looks");
    check(full.spells.largest >= 200 && full.areaAttacks >= 100 && full.spells.strikeHits > 0 && full.damageTicks > 0,
          "200 areas or more at one time; the creatures cast again and again; the strikes hit and the poison ticks");
    check(full.spells.looked == few.spells.looked && full.spells.areaSteps == few.spells.areaSteps && full.areaAttacks == few.areaAttacks,
          "the work depends on the areas and the characters near them, not on the far spawn points: the same steps and looks with 840 and "
          "with 32,840 points");
    check(full.milliseconds < 3000.0, "1000 ticks with 100 casting creatures and 200 areas or more need less than 3 s of real time");
}

} // namespace

std::vector<Case> ranged_c_cases() {
    return {{"CRA1", "areas (3.4f-C): the mushroom, its casts, the record of each session, a late observer, the end at 26 s", mushroom},
            {"CRA2", "areas (3.4f-C): the poison: the first application, the next ones, the ticks, the exit, the entry again; two characters", poison},
            {"CRA3", "areas (3.4f-C): the lightning strike: one hit in its window, the Spell state with the caster, the end", strike},
            {"CRA4", "areas (3.4f-C): the death and the leave of the caster, the walls", ends},
            {"CRA5", "areas (3.4f-C): two characters and two areas that overlap", overlap},
            {"CRA6", "areas (3.4f-C): the behavior: the cast, the repeat, the walk, the cancels, the place", behavior},
            {"CRA7", "areas (3.4f-C): the loaders of the creature data and of the spell data of format 4", loaders},
            {"CRA8", "areas (3.4f-C): the dodge: a strike and a poison tick in the immune window", dodge},
            {"CRA9", "areas (3.4f-C): the load with 32,840 points, 100 casting creatures, 200 areas, a time limit", area_load}};
}

} // namespace soulwarden::backend::tests
