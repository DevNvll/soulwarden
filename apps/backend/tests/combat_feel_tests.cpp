// Tests of stage U3: the corrections of the creature combat feel (research creature-combat-feel.md, Q1 to Q6). Made-up accounts, ids,
// names, and data only. Each case of a correction fails on the tree of the commit before the stage (5633c84; the fixture builds there with
// SOULWARDEN_U3_BASELINE) and passes after it.
//
//   CF1   Q1, the homing of an attack: the Homing clip moves the creature with the time rule of the client (no move before its start,
//         the smoothstep at half time, MoveToDistance from the target at its end); the record of the attack stays; A and B get the same
//         places (MP-10)
//   CF2   Q2, the side of the reaction: the first row of the table that holds for the angle of the source (front, right, left, back; the
//         tolerance at 45 degrees), a power above the largest power of the table, AlignToDirection; A and B get the same fragment
//   CF3   Q3, the length of a reaction with the clip HitStun: HitStun / 30 s (22 frames: 0.733 s), the lower limit of the speed 3.0, a row
//         with no clip keeps its length; the push lasts that time
//   CF4   Q3, the chain: each full reaction since the last reset counts (three light hits 1.5 s apart start the limit); a hit in an attack
//         of the creature resets the count; the end of the limit resets it
//   CF5   Q5, the push scales: a promoted elite moves 0.15 of a push in a full reaction and keeps the length of a pull; a hit with no full
//         reaction plays the additive reaction in its own scope (the record and the 16-bit time on the wire, A and B the same) and pushes
//         with the scale of the data (1.0; 0.0 for a promoted elite); the end of the additive reaction
//   CF6   Q4, the weight: over the limit an ability press and a dodge are refused (encumbered: no cast, no cooldown, no stamina); over 1.5 x
//         the limit a press at rest starts no attack; B at the limit is accepted (MP-10)
//   CF7   Q6, the weapon switch: a key in a swing waits for its switch mark, the old swing hits until then, a press in the time of the switch
//         starts the first swing of the new weapon 3 steps after the switch, an ability press is refused in that time; a key in a swing with
//         no mark is lost
//   CF8   Q6, a chain press at or after CanTransition gets the first fragment as its other fragment (the chain or a new attack)
//   CF9   the load: 200 creatures with a homing at one time in a world of 4,200 creatures (the time limit of FM8)
//   CF10  the loaders: the members of stage U3 of the creature data, the weapon actions, and the perk data; a file from before; values
//         that the loaders refuse
//   CF11  the tool: the cases u3-* of the check of the creature tool (the Homing clips, the reaction table, the additive scope, the kind
//         marks of the buff buckets)
//   CF12  Q5, live check LC-Q5-2: the push of an additive reaction does not hold the run of the creature (the push adds to the run); the
//         push of a full reaction holds it; a light hit in the push of a full reaction does not replace that push
//   CF13  Q4, the live check of the weight: the item catalog tool takes the WeightOverride of the stats row of a weapon or an armor item
//         (the cases weight and weight-missing of check_item_catalog_tool.py)

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/perk_data.h"
#include "gamedata/weapon_actions.h"
#include "support/attack_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/feel_fixture.h"
#include "support/gawh_fixture.h"
#include "support/stamina_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace feel;

/** The movement states of a creature that one client got from the message `from` on (the fragments of slot of the wolf, type 11). */
std::vector<Fragment> movements_of(const FakeClient& client, std::size_t from, std::uint32_t interest) {
    std::vector<Fragment> out;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == shop::kWolfSlots[0] && fragment.type == 11) {
                    out.push_back(fragment);
                }
            }
        }
    }
    return out;
}

std::uint32_t interest_of(FeelShop& shop, std::uint32_t session, world::EntityId id) {
    const auto& known = shop.host.session(session).observer().known();
    return known.count(id) != 0 ? known.at(id).interest : 0;
}

/** @return The fields of a movement state: one field (group 0), or an empty value. */
Bytes field_of(const Fragment& fragment, unsigned field) {
    const auto found = fragment.fields.find({0, field});
    return found != fragment.fields.end() ? found->second : Bytes{};
}

/** @return The fragment index of the first record of a movement state (the scope record of scope 0, or the first scope of the mask). */
std::optional<std::uint32_t> first_fragment(const Fragment& fragment) {
    const Bytes blob = field_of(fragment, 33);
    if (blob.empty()) {
        return std::nullopt;
    }
    server::hub::Reader reader(blob);
    if (reader.size() == 0) {
        return std::nullopt;
    }
    (void)reader.u16();
    return reader.size() - 1;
}

float distance_of(const world::Entity& one, const state::WorldPosition& two) {
    return std::hypot(one.position.x - two.x, one.position.y - two.y);
}

// ---- CF1: the homing -----------------------------------------------------------------------------------------------------------

/**
 * CF1 (Q1): a wolf 2.4 m in front of A (its bite reach 3.0 m: it attacks from its place) bites; the Homing clip of the bite (from 0.2 s, to
 * 0.75 m from the target in 0.3 s, at most 6 m) moves it on the world clock with the time rule of the client. No move before 0.2 s; at
 * 0.35 s (half of the time: the smoothstep 0.5) about half of the way; at the end 0.75 m from A. The attack record stays (the action of the
 * bite in each state). A and B get the same places in the same states.
 */
void homing_time_rule(const Options&) {
    FeelShop shop(true);
    shop.enter_b();
    gamedata::CreatureSlice slice = feel_slice();
    slice.attacks.front().contacts.front().reach = 3.0F;
    gamedata::CreatureKind kind = kind_of("Test_Wolf", "Solo", 0x21112222);
    check(shop.host.creatures.set_full({slice}, {kind}, {}, {{1, shop::kWolfHealth * 100.0F, 100.0F, 50.0F}}), "setup: the slice with the long bite");
    shop.host.world.set_creature_behavior(true);
    world::Entity* wolf = shop.spawn("Test_Wolf", 0.0F, 2.4F, 180.0F);
    const world::EntityId id = wolf->id;
    const state::WorldPosition player = shop.character()->position;
    // The wolf sees A, takes it, and bites from its place; the steps of the world, with the reports of A and B.
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    std::optional<world::WorldClock::time_point> started;
    std::uint16_t generation = 0;
    bool recordStays = true;
    std::vector<std::pair<double, float>> track;
    for (int step = 0; step < 60; ++step) {
        shop.walker.idle(1);
        shop.b.exchange(shop.host);
        world::Entity* now = shop.creature(id);
        if (!started && now->creature.mind.attack) {
            started = now->creature.action->started;
            generation = now->creature.action->generation;
        }
        if (started) {
            const double time = std::chrono::duration<double>(shop.host.clock.now() - *started).count();
            track.emplace_back(time, distance_of(*now, player));
            if (time < 0.55 && (!now->creature.action || now->creature.action->generation != generation)) {
                recordStays = false;
            }
        }
    }
    check(started.has_value(), "setup: the wolf bites A");
    const auto at = [&](double seconds) {
        float found = -1.0F;
        for (const auto& [time, away] : track) {
            if (time <= seconds + 1.0e-6) {
                found = away;
            }
        }
        return found;
    };
    std::string trace;
    for (const auto& [time, away] : track) {
        if (time < 0.7) {
            trace += " " + std::to_string(time).substr(0, 5) + ":" + std::to_string(away).substr(0, 5);
        }
    }
    note("the track:" + trace);
    note("the distance to A: 0.15 s " + std::to_string(at(0.15)) + ", 0.36 s " + std::to_string(at(0.36)) + ", 0.6 s " + std::to_string(at(0.6)));
    check(std::fabs(at(0.15) - 2.4F) < 0.02F, "no move before the start of the clip (0.2 s)");
    // The steps of 50 ms come with the ticks of 33 ms: the step at 0.366 s has the clip time 0.166 s (the smoothstep 0.584: 1.436 m).
    check(at(0.37) > 1.3F && at(0.37) < 1.6F, "about half of the way at about half of the time (the smoothstep)");
    check(std::fabs(at(0.6) - kHomingDistance) < 0.05F, "at the end of the clip the wolf is MoveToDistance (0.75 m) from A");
    check(recordStays, "the attack record stays while the homing moves the wolf (Gait::place)");
    const std::vector<Fragment> a = movements_of(shop.a, fromA, interest_of(shop, 1, id));
    const std::vector<Fragment> b = movements_of(shop.b, fromB, interest_of(shop, 2, id));
    // The sessions send at their own times: each place that B got is a place that A got, and the last places are the same.
    bool same = !a.empty() && !b.empty() && field_of(a.back(), 27) == field_of(b.back(), 27);
    for (const Fragment& state : b) {
        const Bytes place = field_of(state, 27);
        same = same && (place.empty() || std::any_of(a.begin(), a.end(), [&](const Fragment& seen) { return field_of(seen, 27) == place; }));
    }
    check(same, "A and B get the same places (" + std::to_string(a.size()) + " and " + std::to_string(b.size()) + " states)");
}

// ---- CF2: the side of the reaction ---------------------------------------------------------------------------------------------

/**
 * CF2 (Q2): a light hit (power 2) on a wolf that looks north from a source in front (0 degrees), to its right (+90), left (-90), back
 * (180), and at 44.99 and 45.02 degrees (the tolerance 0.01) plays the fragment of the first row that holds; a power above the largest
 * power of the table (5) takes the largest (4); a row whose name the slice does not have keeps the fragment of the action; a heavy hit
 * from the right turns the wolf by AlignOffsetAngle - angle (45 - 90). A and B get the same fragment in the scope record.
 */
void reaction_side(const Options&) {
    FeelShop shop(true);
    shop.enter_b();
    const auto react = [&](const char* row, float degrees, float yaw = 0.0F) -> const world::Entity* {
        world::Entity* wolf = shop.spawn("Test_Wolf", 0.0F, 3.0F, yaw);
        const float radians = degrees / 57.2957795F;
        (void)shop.hit_from(*wolf, row, 2.0F * std::sin(radians), 3.0F + 2.0F * std::cos(radians));
        return shop.creature(wolf->id);
    };
    const auto fragment = [](const world::Entity* wolf) { return wolf->creature.action ? static_cast<int>(wolf->creature.action->fragment) : -1; };
    const int frontHit = fragment(react("Test_FeelLight", 0.0F));
    const int rightHit = fragment(react("Test_FeelLight", 90.0F));
    const int leftHit = fragment(react("Test_FeelLight", -90.0F));
    const int backHit = fragment(react("Test_FeelLight", 180.0F));
    note("front " + std::to_string(frontHit) + ", right " + std::to_string(rightHit) + ", left " + std::to_string(leftHit) + ", back "
         + std::to_string(backHit));
    check(frontHit == static_cast<int>(kFront2) && rightHit == static_cast<int>(kRight2) && leftHit == static_cast<int>(kLeft2)
              && backHit == static_cast<int>(kBack2),
          "a light hit from the front, the right, the left, and the back plays r_R2_F, r_R2_R, r_R2_L, r_R2_B");
    check(fragment(react("Test_FeelLight", 44.99F)) == static_cast<int>(kFront2) && fragment(react("Test_FeelLight", 45.02F)) == static_cast<int>(kRight2),
          "the tolerance of the side conditions: 44.99 degrees is the front, 45.02 degrees the right (the first row that holds)");
    check(fragment(react("Test_FeelPower5", 0.0F)) == 79, "a power above the largest power of the table (5 > 4) takes the rows of power 4");
    check(fragment(react("Test_FeelPower4", 180.0F)) == static_cast<int>(kFront2),
          "a row whose fragment the slice does not have keeps the fragment of the action Reaction (r_R2_F)");
    const world::Entity* turned = react("Test_FeelHeavy", 90.0F);
    check(turned->creature.action && turned->creature.action->fragment == kRight3 && std::fabs(turned->creature.yaw - 315.0F) < 0.5F,
          "AlignToDirection: a heavy hit from the right (+90 degrees) turns the wolf by 45 - 90 degrees (its yaw 315)");
    // The wire: a wolf of A and B gets the hit from its right; each session gets the same fragment of the record.
    world::Entity* seen = shop.spawn("Test_Wolf", 0.0F, 3.0F, 0.0F);
    shop.walker.idle(20);
    shop.b.exchange(shop.host);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    (void)shop.hit_from(*seen, "Test_FeelLight", 2.0F, 3.0F);
    shop.walker.idle(3);
    shop.b.exchange(shop.host);
    const std::vector<Fragment> a = movements_of(shop.a, fromA, interest_of(shop, 1, seen->id));
    const std::vector<Fragment> b = movements_of(shop.b, fromB, interest_of(shop, 2, seen->id));
    check(!a.empty() && !b.empty() && first_fragment(a.front()) == kRight2 && first_fragment(b.front()) == kRight2,
          "A and B get the record of r_R2_R in the scope fields of the movement state");
}

// ---- CF3: the length of the reaction with the hit stun --------------------------------------------------------------------------

/**
 * CF3 (Q3): a heavy hit (power 3, HitStun_AI 22 frames) on a row of 1.333 s with the clip HitStun at 0.183 s plays the reaction for
 * 22 / 30 = 0.733 s (the clip 0x2559640 sets the speed of the rest of the fragment); its push (1 m/s, no decay) ends at that time
 * (0.73 m; before: 0.9 s of the length of the slice, 0.9 m). A row of 5 frames: the speed limit 3.0 (0.183 + 1.15 / 3 = 0.567 s). A row
 * with no clip (power 2) keeps the length of its fragment (0.9 s).
 */
void hit_stun_length(const Options&) {
    FeelShop shop;
    const auto length_of = [&](const char* row) {
        world::Entity* wolf = shop.spawn("Test_Wolf", 0.0F, 3.0F, 180.0F);
        (void)shop.hit_from(*wolf, row, 0.0F, 0.0F);
        const world::Entity* hit = shop.creature(wolf->id);
        return hit->creature.action && hit->creature.action->ends
                   ? std::chrono::duration<float>(*hit->creature.action->ends - shop.host.clock.now()).count()
                   : -1.0F;
    };
    const float heavy = length_of("Test_FeelHeavy");
    const float shortStun = length_of("Test_FeelShort");
    const float light = length_of("Test_FeelLight");
    note("heavy " + std::to_string(heavy) + " s, 5 frames " + std::to_string(shortStun) + " s, light " + std::to_string(light) + " s");
    check(std::fabs(heavy - 22.0F / 30.0F) < 0.01F, "a heavy hit of 22 frames on a row with the clip HitStun plays 0.733 s");
    check(std::fabs(shortStun - (kClip3 + (kLength3 - kClip3) / 3.0F)) < 0.01F, "a hit of 5 frames: the speed limit 3.0 (0.567 s)");
    check(std::fabs(light - kLength2) < 0.01F, "a row with no clip keeps the length of its fragment (0.9 s)");
    // The push of the heavy hit ends with the reaction: 0.733 m at 1 m/s.
    world::Entity* pushed = shop.spawn("Test_Wolf", 0.0F, 3.0F, 180.0F);
    const state::WorldPosition before = pushed->position;
    (void)shop.hit_from(*pushed, "Test_FeelHeavy", 0.0F, 0.0F);
    shop.wait(1.5F);
    const float travel = distance_of(*shop.creature(pushed->id), before);
    note("the push of the heavy hit: " + std::to_string(travel) + " m");
    check(std::fabs(travel - 22.0F / 30.0F) < 0.06F, "the push of the heavy hit lasts the reaction: 0.73 m at 1 m/s");
}

// ---- CF4: the chain ------------------------------------------------------------------------------------------------------------

/**
 * CF4 (Q3): three light hits 1.5 s apart on a Solo wolf (each reaction of 0.9 s ends before the next hit) start the reaction limit at the
 * third (each full reaction since the last reset counts; before: a reaction counted only while the one before it played). A hit in an attack
 * of the wolf resets the count (the additive reaction with grit): two hits, the reset, and two more hits start no limit. The end of the
 * limit resets the count.
 */
void reaction_chain(const Options&) {
    FeelShop shop;
    world::Creatures& creatures = shop.world().creatures();
    world::Entity* wolf = shop.spawn("Test_Wolf", 0.0F, 3.0F, 180.0F);
    const world::EntityId id = wolf->id;
    const std::uint64_t limits = limits_of(creatures);
    for (int hit = 0; hit < 3; ++hit) {
        (void)shop.hit_from(*shop.creature(id), "Test_FeelLight", 0.0F, 0.0F);
        shop.wait(1.5F);
    }
    check(limits_of(creatures) == limits + 1, "three light hits 1.5 s apart: the third reaction starts the limit");
    const std::uint64_t reactions = reactions_of(creatures);
    (void)shop.hit_from(*shop.creature(id), "Test_FeelLight", 0.0F, 0.0F);
    check(reactions_of(creatures) == reactions, "in the limit a light hit plays no full reaction");
    // The end of the limit (2 s after the exit of the third reaction, and the additive hit gives the limiter again): the count starts
    // again, and two more hits start no new limit.
    shop.wait(4.0F);
    for (int hit = 0; hit < 2; ++hit) {
        (void)shop.hit_from(*shop.creature(id), "Test_FeelLight", 0.0F, 0.0F);
        shop.wait(1.5F);
    }
    check(limits_of(creatures) == limits + 1 && reactions_of(creatures) == reactions + 2,
          "after the end of the limit the count starts again: two reactions, no new limit");
    // A hit in an attack of the creature (grit) resets the count: two reactions, the reset, two reactions: no limit.
    world::Entity* other = shop.spawn("Test_Wolf", 0.0F, -3.0F, 0.0F);
    const world::EntityId second = other->id;
    const std::uint64_t before = limits_of(creatures);
    for (int hit = 0; hit < 2; ++hit) {
        (void)shop.hit_from(*shop.creature(second), "Test_FeelLight", 0.0F, 0.0F);
        shop.wait(1.5F);
    }
    shop.creature(second)->creature.mind.attack = 0U; // The grit of an attack that plays.
    (void)shop.hit_from(*shop.creature(second), "Test_FeelLight", 0.0F, 0.0F);
    shop.creature(second)->creature.mind.attack.reset();
    shop.wait(1.5F);
    for (int hit = 0; hit < 2; ++hit) {
        (void)shop.hit_from(*shop.creature(second), "Test_FeelLight", 0.0F, 0.0F);
        shop.wait(1.5F);
    }
    check(limits_of(creatures) == before && additive_count(creatures) >= 1,
          "a hit in an attack of the wolf plays the additive reaction and resets the count: four reactions with it start no limit");
}

// ---- CF5: the push scales and the additive reaction ------------------------------------------------------------------------------

/**
 * CF5 (Q5): a heavy hit (a full reaction of a Solo+ kind) pushes a promoted elite 0.15 of the push (0.11 m of 0.73 m), a normal Solo+ the
 * whole push; a pull (Y -2) moves the promoted elite 2 m (PushBackScale scales only a push). A light hit on a Solo+ kind plays no full
 * reaction: the additive reaction in scope 4 (the fragment of the side) with its push (0.5 m/s for 0.9 - 0.2 s: 0.35 m), and on a promoted
 * elite with the scale 0 (no move). The movement state has the record of scope 0 and of scope 4 (mask 0x11 with a walk, 0x10 with no
 * action), and one 16-bit time in scopeTimeBlobEx; A and B get the same bytes; after its end the mask has no bit 4.
 */
void push_scales(const Options&) {
    FeelShop shop(true);
    shop.enter_b();
    const auto travel_of = [&](const char* kind, const char* row, float wait) {
        world::Entity* wolf = shop.spawn(kind, 0.0F, 3.0F, 180.0F);
        const state::WorldPosition before = wolf->position;
        (void)shop.hit_from(*wolf, row, 0.0F, 0.0F);
        shop.wait(wait);
        return distance_of(*shop.creature(wolf->id), before);
    };
    const float promotedPush = travel_of("Test_Promoted", "Test_FeelHeavy", 1.5F);
    const float normalPush = travel_of("Test_Plus", "Test_FeelHeavy", 1.5F);
    const float promotedPull = travel_of("Test_Promoted", "Test_FeelPull", 1.5F);
    const float additivePush = travel_of("Test_Plus", "Test_FeelLight", 1.5F);
    const float promotedAdditive = travel_of("Test_Promoted", "Test_FeelLight", 1.5F);
    note("promoted push " + std::to_string(promotedPush) + ", normal push " + std::to_string(normalPush) + ", promoted pull "
         + std::to_string(promotedPull) + ", additive push " + std::to_string(additivePush) + ", promoted additive "
         + std::to_string(promotedAdditive));
    check(std::fabs(promotedPush - 0.15F * normalPush) < 0.03F && normalPush > 0.6F,
          "a full reaction pushes a promoted elite 0.15 of the push of a normal Solo+ (PushBackScale 0.15)");
    check(promotedPull > 1.8F, "a pull keeps its length on a promoted elite (PushBackScale scales only a push)");
    check(std::fabs(additivePush - 0.5F * (kAdditiveLength - 0.2F)) < 0.06F,
          "a light hit on a Solo+ kind: the additive reaction pushes 0.35 m (0.5 m/s for 0.9 - 0.2 s)");
    check(promotedAdditive < 0.01F, "the additive reaction of a promoted elite pushes nothing (PushBackScale 0.0)");
    // The wire of the additive reaction for A and B.
    world::Entity* seen = shop.spawn("Test_Plus", 0.0F, 3.0F, 0.0F);
    const world::EntityId id = seen->id;
    shop.walker.idle(20);
    shop.b.exchange(shop.host);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    (void)shop.hit_from(*seen, "Test_FeelLight", 2.0F, 3.0F);
    check(additive_of(*shop.creature(id)) == std::make_pair(kAdditiveScope, kAddRight),
          "a light hit from the right of a Solo+ kind plays the additive reaction of the right side in scope 4");
    shop.walker.idle(3);
    shop.b.exchange(shop.host);
    const std::vector<Fragment> a = movements_of(shop.a, fromA, interest_of(shop, 1, id));
    const std::vector<Fragment> b = movements_of(shop.b, fromB, interest_of(shop, 2, id));
    const auto mask_of = [](const Fragment& state) -> int {
        const Bytes bytes = field_of(state, 32);
        if (bytes.empty()) {
            return -1;
        }
        server::hub::Reader reader(bytes);
        return static_cast<int>(reader.size());
    };
    const auto time_of = [](const Fragment& state) -> int {
        const Bytes bytes = field_of(state, 29);
        if (bytes.empty()) {
            return -1;
        }
        server::hub::Reader reader(bytes);
        return reader.size() == 2 ? static_cast<int>(reader.u16()) : -2;
    };
    check(!a.empty() && !b.empty() && mask_of(a.front()) == static_cast<int>(1U << kAdditiveScope)
              && time_of(a.front()) >= 0 && time_of(a.front()) < 0x3C00 && field_of(a.front(), 33) == field_of(b.front(), 33),
          "the movement state has the bit of scope 4, its record, and its time as a 16-bit float; A and B get the same records");
    shop.wait(1.0F);
    shop.walker.idle(2);
    shop.b.exchange(shop.host);
    const std::vector<Fragment> after = movements_of(shop.a, fromA, interest_of(shop, 1, id));
    check(!after.empty() && mask_of(after.back()) >= 0 && (mask_of(after.back()) & static_cast<int>(1U << kAdditiveScope)) == 0
              && !additive_of(*shop.creature(id)),
          "at the end of the additive reaction the next scope fields have no bit of scope 4");
}

// ---- CF12: the push of an additive reaction in a run --------------------------------------------------------------------------------

/**
 * CF12 (Q5, live check LC-Q5-2): a Solo+ wolf 12 m from A takes A as its target with a first light hit and runs to it (5 m/s). A light hit
 * in the run plays the additive reaction and its push (0.5 m/s away from A), and the wolf keeps its run: 0.7 s after the hit (the push
 * time) it is more than 1.5 m nearer to A (the steps of the run are 0.23 s apart; before: the push held the run, and the wolf was 0.35 m
 * farther). A heavy hit in the run (a full reaction) holds it: 0.4 s after that hit it is not nearer. A light hit in the push of the full reaction plays the additive reaction but does not replace that push.
 */
void additive_push_in_run(const Options&) {
    FeelShop shop;
    world::Creatures& creatures = shop.world().creatures();
    shop.host.world.set_creature_behavior(true);
    world::Entity* wolf = shop.spawn("Test_Plus", 0.0F, 12.0F, 180.0F);
    const world::EntityId id = wolf->id;
    const state::WorldPosition player = at(kPlace.x, kPlace.y);
    const std::uint64_t additives = additive_count(creatures);
    (void)shop.hit_from(*wolf, "Test_FeelLight", 0.0F, 0.0F);
    for (int tick = 0; tick < 90 && distance_of(*shop.creature(id), player) > 9.5F; ++tick) {
        shop.wait(1.0F / 30.0F);
    }
    const float before = distance_of(*shop.creature(id), player);
    check(before < 9.6F && before > 8.0F, "setup: the wolf runs to A (" + std::to_string(before) + " m)");
    (void)shop.hit_from(*shop.creature(id), "Test_FeelLight", 0.0F, 0.0F);
    const bool pushed = additive_push(*shop.creature(id));
    shop.wait(0.7F);
    const float after = distance_of(*shop.creature(id), player);
    note("the light hit in the run: " + std::to_string(before) + " m, 0.7 s after it " + std::to_string(after) + " m");
    check(additive_count(creatures) >= additives + 2 && pushed, "the light hit in the run plays the additive reaction and its push");
    check(before - after > 1.5F, "the push of the additive reaction does not hold the run: the wolf is nearer to A");
    shop.wait(0.2F);
    const float heavyFrom = distance_of(*shop.creature(id), player);
    (void)shop.hit_from(*shop.creature(id), "Test_FeelHeavy", 0.0F, 0.0F);
    shop.wait(0.2F);
    const std::uint64_t kept = kept_full(creatures);
    (void)shop.hit_from(*shop.creature(id), "Test_FeelLight", 0.0F, 0.0F);
    const world::Entity& held = *shop.creature(id);
    check(held.creature.forced && held.creature.forced->row == "Test_FeelHeavy" && !additive_push(held) && kept_full(creatures) == kept + 1,
          "a light hit in the push of a full reaction does not replace that push");
    shop.wait(0.2F);
    const float heavyAfter = distance_of(*shop.creature(id), player);
    note("the heavy hit in the run: " + std::to_string(heavyFrom) + " m, 0.4 s after it " + std::to_string(heavyAfter) + " m");
    check(heavyFrom > 3.0F && heavyAfter > heavyFrom - 0.05F, "the push of a full reaction holds the run: the wolf is not nearer");
}

// ---- CF6: the weight -----------------------------------------------------------------------------------------------------------

/** @return The weight of the bag of a character (the catalog weight x the quantity of each slot; tenths). */
std::uint64_t bag_weight(ServerHost& host, const world::Entity& character) {
    std::uint64_t total = 0;
    for (const world::BagSlot& slot : character.bag.slots) {
        if (slot.item) {
            if (const gamedata::ItemDefinition* definition = host.catalog.find(slot.item->itemId)) {
                total += static_cast<std::uint64_t>(definition->weight) * slot.item->quantity;
            }
        }
    }
    return total;
}

/**
 * CF6 (Q4): A carries 200.2 (2002 tenths) and B 200.0 (the limit 2000 of the data). A ability press of A is refused (encumbered): no cast
 * and no cooldown; the same press of B is accepted. A dodge of A at rest is refused (no stamina payment). With 300.2 (above 1.5 x the limit)
 * a press of A at rest starts no attack; B at 300.0 starts its attack.
 */
void weight(const Options&) {
    gawh::GawhShop shop;
    stamina::set_stamina(shop.host);
    std::vector<gamedata::ItemDefinition> items = shop::shop_catalog();
    gamedata::ItemDefinition ore = shop::definition("TestOre", "Resource", "Resource+Ore", 1, 0, 0, 10000);
    ore.weight = 2;
    gamedata::ItemDefinition pebble = shop::definition("TestPebble", "Resource", "Resource+Ore", 1, 0, 0, 10000);
    pebble.weight = 1;
    items.push_back(ore);
    items.push_back(pebble);
    shop.host.catalog.set(items);
    shop.enter_b();
    shop.hold("TestHammer");
    shop.hold("TestHammer", kCharacterB);
    shop.bar(kWarHammer, {"Ability_WarHammer_WreckingBall", nullptr, nullptr});
    shop.bar(kWarHammer, {"Ability_WarHammer_WreckingBall", nullptr, nullptr}, kCharacterB);
    const auto fill = [&](const char* character, std::uint64_t target) {
        world::Entity* own = shop.world().find_character(character);
        const std::uint64_t now = bag_weight(shop.host, *own);
        if (target > now) {
            const std::uint64_t more = target - now;
            if (more / 2 > 0) {
                (void)shop.world().inventory().give(character, "TestOre", static_cast<std::uint32_t>(more / 2));
            }
            if (more % 2 != 0) {
                (void)shop.world().inventory().give(character, "TestPebble", 1);
            }
        }
        return bag_weight(shop.host, *own);
    };
    check(fill(kCharacterA, 2002) == 2002 && fill(kCharacterB, 2000) == 2000, "setup: A carries 200.2, B 200.0");
    shop.walker.idle(20);
    const std::uint64_t accepted = shop.accepted();
    (void)shop.press(0);
    shop.walker.idle(5);
    const auto* cooldownsA = shop.abilities().cooldowns_of(kCharacterA);
    check(shop.accepted() == accepted
              && (cooldownsA == nullptr || cooldownsA->count(gamedata::progression_id("Ability_WarHammer_WreckingBall")) == 0),
          "over the limit the press of A is refused: no cast and no cooldown");
    Walker walkerB{shop.host, shop.b, shop.world().find_character(kCharacterB)->position};
    walkerB.input = 600;
    walkerB.press(0);
    walkerB.idle(5);
    check(shop.accepted() == accepted + 1, "at the limit (200.0) the press of B is accepted");
    shop.walker.idle(90);
    // A dodge of A at rest: no dodge, no stamina payment.
    const float stamina = shop.character()->vitals.stamina;
    const std::uint64_t dodges = shop.world().stamina().counts().dodges[static_cast<std::size_t>(world::DodgeVerdict::accepted)];
    shop.step({attack_fixture::kActionDodge});
    shop.walker.idle(10);
    check(shop.world().stamina().counts().dodges[static_cast<std::size_t>(world::DodgeVerdict::accepted)] == dodges
              && shop.character()->vitals.stamina == stamina,
          "over the limit a dodge of A starts no dodge and pays no stamina");
    // Over 1.5 x the limit: a press at rest starts no attack (A); B at 1.5 x the limit starts its attack.
    check(fill(kCharacterA, 3002) == 3002 && fill(kCharacterB, 3000) == 3000, "setup: A carries 300.2, B 300.0");
    shop.walker.idle(60);
    walkerB.idle(60);
    const std::size_t visitsA = shop.visits().size();
    shop.step({kActionAttack});
    shop.step({kActionAttackRelease});
    shop.walker.idle(3);
    check(shop.visits().size() == visitsA, "over 1.5 x the limit a press of A at rest starts no attack");
    const std::size_t visitsB = shop.visits(kCharacterB).size();
    walkerB.bits({kActionAttack});
    walkerB.bits({kActionAttackRelease});
    walkerB.idle(3);
    check(shop.visits(kCharacterB).size() > visitsB, "B at 1.5 x the limit starts its attack");
}

// ---- CF7: the weapon switch ----------------------------------------------------------------------------------------------------

/**
 * CF7 (Q6): A has the hammer in weapon slot 1 and the axe in slot 2. A swing of the hammer (its switch marks: the buffer from 0.4 s, the
 * switch at 1.133 s) and the key 2 at 0.133 s: the hammer stays in the hand until the step of 1.133 s, and its strike hits the wolf; a press
 * 0.3 s after the key starts nothing, and the first swing of the axe starts 3 steps after the switch; an ability press in that time is
 * refused. A key in a swing with no mark is lost (the weapon stays).
 */
void weapon_switch(const Options&) {
    gawh::GawhShop shop;
    gamedata::WeaponActionTables tables = gawh::gawh_actions();
#ifndef SOULWARDEN_U3_BASELINE
    for (gamedata::WeaponActionSet& set : tables.weapons) {
        if (std::find(set.tags.begin(), set.tags.end(), "Warhammer") != set.tags.end()) {
            for (gamedata::AttackFragment& fragment : set.fragments) {
                if (fragment.name == "Attack_Primary") {
                    fragment.switchBuffer = 0.4F;
                    fragment.switchAt = 1.133333F;
                }
            }
        }
    }
#endif
    check(shop.host.weaponActions.set(tables), "setup: the weapon actions with the switch marks of the hammer swing");
    shop.hold("TestHammer");
    {
        world::GiveOptions options;
        options.gearScore = 100;
        const world::ItemResult given = shop.world().inventory().give(kCharacterA, "TestAxe", 1, options);
        check(given.ok() && !given.slots.empty()
                  && shop.world().inventory().equip(kCharacterA, given.slots.front(), world::paperdoll::kMainHand1 + 1).ok(),
              "setup: the axe in weapon slot 2");
    }
    shop.bar(kWarHammer, {"Ability_WarHammer_WreckingBall", nullptr, nullptr});
    shop.bar(kGreatAxe, {"Ability_Greataxe_Reap", nullptr, nullptr});
    world::Entity* wolf = shop.wolf(-1.5F);
    shop.walker.idle(30);
    const std::string hammer = world::Combat::weapon_key(*shop.character());
    const std::uint16_t press = shop.step({kActionAttack});
    const std::uint16_t swing = shop.step({kActionAttackRelease});
    (void)press;
    shop.until(static_cast<std::uint16_t>(swing + 4));
    const std::uint16_t key = shop.step({kActionWeapon2});
    check(world::Combat::weapon_key(*shop.character()) == hammer, "at the key in the swing the hammer stays in the hand");
    shop.until(static_cast<std::uint16_t>(swing + 9));
    const std::uint64_t hitsBefore = shop.world().combat().sequences().counts().strikes;
    shop.step({}, {gawh::touch(*wolf)});
    check(shop.world().combat().sequences().counts().strikes == hitsBefore + 1, "the strike of the hammer swing hits after the key");
    shop.until(static_cast<std::uint16_t>(key + 9));
    shop.step({kActionAttack});
    shop.step({kActionAttackRelease});
    const std::uint64_t refusedBefore = shop.abilities().counts().presses[static_cast<std::size_t>(world::CastVerdict::accepted)];
    (void)shop.press(0);
    check(shop.abilities().counts().presses[static_cast<std::size_t>(world::CastVerdict::accepted)] == refusedBefore,
          "an ability press while the switch waits is refused");
    // The switch at 1.133 s of the swing (the step swing + 33), the first swing of the axe 3 steps later from the kept press.
    shop.until(static_cast<std::uint16_t>(swing + 33));
    shop.step();
    const bool axeAtSwitch = world::Combat::weapon_key(*shop.character()) != hammer;
    shop.step();
    shop.step();
    const std::vector<std::string> before = shop.visits();
    shop.step();
    const std::vector<std::string> after = shop.visits();
    note("visits after the switch: " + attack_fixture::joined(after));
    check(axeAtSwitch, "the weapon changes at the step of the switch mark (1.133 s), not at the key");
    check(after.size() > before.size() && after.back() == "Attack_Primary",
          "the kept press starts the first swing of the axe 3 steps after the switch");
    // A key in a swing of the axe (no switch mark): no window starts in the 20 steps of the key, which is lost (live check SW-L3); the
    // weapon stays after the end of the swing.
    shop.walker.idle(90);
    const std::string axe = world::Combat::weapon_key(*shop.character());
    shop.step({kActionAttack});
    const std::uint16_t axeSwing = shop.step({kActionAttackRelease});
    shop.until(static_cast<std::uint16_t>(axeSwing + 4));
    shop.step({kActionWeapon1});
    shop.walker.idle(90);
    check(world::Combat::weapon_key(*shop.character()) == axe, "a key in a swing with no switch window in its 20 steps is lost");
}

// ---- CF8: a press after CanTransition -------------------------------------------------------------------------------------------

/**
 * CF8 (Q6, UT2-12): a press at 1.2 s of the axe swing (its CanTransition at 1.167 s) takes the chain (Attack_Start_Charge_C2) with the
 * first fragment of the graph (Attack_Start_Charge) as its other fragment: the first contact selects the chain or a new attack.
 */
void after_transition(const Options&) {
    attack_fixture::Range range;
    gamedata::WeaponActionTables tables = attack_fixture::test_actions();
    tables.weapons.front().fragments[1].canTransition = 1.166667F;
    check(range.host.weaponActions.set(tables), "setup: the axe swing with its CanTransition mark");
    attack_fixture::Attacker attacker{range.host, range.a, attack_fixture::kPlace};
    attacker.idle(5);
    const std::uint16_t swing = attacker.click();
    attacker.until(static_cast<std::uint16_t>(swing + 36));
    attacker.click();
    const world::AttackVisit* visit = range.host.world.combat().sequences().newest(attack_fixture::kCharacterA);
    const world::AttackSequences::State* state = range.state();
    note("the visits: " + attack_fixture::joined(range.visits()) + "; the other fragment of the newest: "
         + std::to_string(visit != nullptr ? visit->alternate : -2));
    check(visit != nullptr && state != nullptr && state->actions->fragments[visit->fragment].name == "Attack_Primary_Strike2"
              && visit->alternate >= 0,
          "a press after CanTransition chains, with the start of a new attack as the other fragment of the visit");
}

// ---- CF9: the load -------------------------------------------------------------------------------------------------------------

/** CF9: 200 creatures with a homing at one time in a world of 4,200 creatures: 60 ticks in less than 2 s (the limit of FM8). */
void homing_load(const Options&) {
#ifndef SOULWARDEN_U3_BASELINE
    FeelShop shop;
    const state::WorldPosition player = shop.character()->position;
    std::vector<world::EntityId> moving;
    for (int index = 0; index < 4200; ++index) {
        const float angle = static_cast<float>(index) * 0.0015F;
        const float radius = index < 200 ? 4.0F : 300.0F + static_cast<float>(index % 50);
        world::Entity* wolf = shop.spawn("Test_Wolf", radius * std::sin(angle), radius * std::cos(angle), 180.0F);
        if (index < 200 && wolf != nullptr) {
            moving.push_back(wolf->id);
        }
    }
    world::Creatures& creatures = shop.world().creatures();
    for (const world::EntityId id : moving) {
        world::Entity* wolf = creatures.find(id);
        (void)creatures.start_action(*wolf, shop::kBite, std::chrono::milliseconds(1400));
        creatures.attack_motion().start(*wolf, shop.character()->id, 0);
    }
    check(creatures.attack_motion().moving_count() == moving.size(), "setup: 200 homings run");
    const auto started = std::chrono::steady_clock::now();
    for (int tick = 0; tick < 60; ++tick) {
        shop.host.clock.advance(attack_fixture::kTick);
        shop.host.world.tick();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    float nearest = 100.0F;
    for (const world::EntityId id : moving) {
        nearest = std::min(nearest, distance_of(*creatures.find(id), player));
    }
    note("60 ticks with 200 homings in a world of 4,200 creatures: " + std::to_string(seconds) + " s; the nearest wolf " + std::to_string(nearest) + " m");
    check(seconds < 2.0, "60 ticks in less than 2 s");
    check(creatures.attack_motion().counts().steps > 0, "the homings moved the creatures");
#else
    check(false, "the baseline tree has no homing");
#endif
}

// ---- CF10: the loaders ---------------------------------------------------------------------------------------------------------

/** CF10: the members of stage U3 of the creature data, the weapon actions, and the perk data; a file from before; refused values. */
void loaders(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "cf10-loaders";
    std::filesystem::create_directories(folder);
    const json homing = {0.2, 0.3, 1, 0.75, 0.3, 0.0, 6.0, 600.0, 0, 10.0, 90.0, 5.0, 0, 0, 0, 1, 0};
    const json table = {{"rows", {{2, {{0.0, 45.0, 0.01}, {-45.0, 0.0, 0.01}}, 5, 0.9, -1.0, 0, 0.0}, {3, nullptr, 3, 1.333, 0.183, 1, 45.0}}},
                        {"motion", {{"Default", 1.0, 1.0}, {"PromotedElite", 1.0, 0.15}}},
                        {"exit", 0.0}};
    json additive = {{"rows", {{-1, nullptr, 4, 0.9, -1.0, 0, 0.0}}}, {"motion", {{"Default", 1.0, 1.0}}}, {"exit", 0.2}, {"action", 3}, {"scope", 4}};
    const auto document = [&](bool members, const json& attackHoming, const json& tableMember, const json& additiveMember) {
        json attack = {2, 1.4, {{0.5, 2.0, 1.0, 0.0, "Slash", 4, "Physical", "Test_Bite"}}};
        json slice = {{"path", "slices/test/wolf.dynamicslice"},
                      {"asset", "11111111-1111-1111-1111-111111111111"},
                      {"sub", 2},
                      {"root", "1000000000000011"},
                      {"interestRadius", 100.0},
                      {"movement", 5},
                      {"vitals", 9},
                      {"marker", 15},
                      {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                      {"components", json::array()},
                      {"actions", {{"Idle", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}, {"Attack_Bite", "Attack_Bite", 7, false},
                                   {"Reaction_Additive", "r_R0_F", 4, true}}}};
        json kind = {{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 0x11112222},
                     {"categoryId", 0x33334444}, {"name", "Test Wolf"}, {"vitalsName", "Test Wolf"}, {"family", "Wildlife"},
                     {"creatureType", "Solo+"}, {"health", 1.0}, {"level", 18}, {"bodies", json::array({json::array({0, 100})})},
                     {"points", 0}};
        if (members) {
            // The forms of the retail data: an exponential clip with a TurnRate above 1, a clip that only turns with a MoveToDuration of 75 s,
            // and a clip that starts after the end of its attack.
            const json exponential = {0.0, 0.3, 0, 1.0, 0.25, 0.0, 0.0, 1080.0, 1, 10.0, 180.0, 5.0, 0, 0, 0, 1, 1};
            const json longTurn = {0.1, 0.2, 0, 0.5, 75.0, 0.0, 2.0, 0.3, 1, 5.0, 180.0, 5.0, 0, 0, 0, 0, 0};
            const json late = {2.0, 0.3, 1, 0.75, 0.3, 0.0, 6.0, 600.0, 0, 10.0, 90.0, 5.0, 0, 0, 0, 1, 0};
            attack.push_back(attackHoming.size() == 17 ? json::array({attackHoming, exponential, longTurn, late}) : json::array({attackHoming}));
            slice["reactionTable"] = tableMember;
            slice["additive"] = additiveMember;
            kind["promotedElite"] = true;
        }
        slice["attacks"] = json::array({attack});
        return json{{"format", 2},
                    {"counts", {{"spawnPoints", 0}, {"kinds", 1}, {"slices", 1}}},
                    {"levels", {{18, 1690.875977, 100.0, 50.0}}},
                    {"slices", {slice}},
                    {"kinds", {kind}},
                    {"spawnPoints", json::array()}};
    };
    int number = 0;
    const auto load = [&](gamedata::CreatureData& data, const json& file) {
        const std::filesystem::path path = folder / ("creatures-" + std::to_string(number++) + ".json");
        std::ofstream(path) << file.dump();
        return data.load(path.string());
    };
    gamedata::CreatureData data;
    const bool loaded = load(data, document(true, homing, table, additive));
#ifndef SOULWARDEN_U3_BASELINE
    check(loaded && data.slices()[0].attacks.size() == 1 && data.slices()[0].attacks[0].homing.size() == 4
              && std::fabs(data.slices()[0].attacks[0].homing[0].moveToDistance - 0.75F) < 1.0e-5F && data.slices()[0].reactionTable
              && data.slices()[0].reactionTable->rows.size() == 2 && data.slices()[0].reactionTable->largestPower == 3
              && data.slices()[0].additive && data.slices()[0].additive->scope == 4 && data.kinds()[0].promotedElite,
          "the creature data has the Homing clips (also the forms of the retail data: a TurnRate of 1080 of an exponential clip, a "
          "MoveToDuration of 75 s, a start after the end of the attack), the reaction table, the additive reaction, and the mark of a "
          "promoted elite: " + data.error());
#else
    check(loaded, "the creature data loads the members of stage U3: " + data.error());
#endif
    gamedata::CreatureData old;
    check(load(old, document(false, homing, table, additive)), "a file of a tool from before stage U3 loads");
    json badHoming = homing;
    badHoming.erase(badHoming.end() - 1);
    json badTable = table;
    badTable["rows"][0][0] = 10;
    json badScope = additive;
    badScope["scope"] = 1;
    json badMotion = table;
    badMotion["motion"] = {{"PromotedElite", 1.0, 0.15}};
    for (const auto& [what, file] : {std::pair<const char*, json>{"a Homing clip of 16 values", document(true, badHoming, table, additive)},
                                     std::pair<const char*, json>{"a row of power 10", document(true, homing, badTable, additive)},
                                     std::pair<const char*, json>{"an additive reaction in scope 1", document(true, homing, table, badScope)},
                                     std::pair<const char*, json>{"a motion with no Default first", document(true, homing, badMotion, additive)}}) {
        gamedata::CreatureData refused;
        check(!load(refused, file), std::string("the creature data refuses ") + what);
    }
#ifndef SOULWARDEN_U3_BASELINE
    // The weapon actions: the switch marks of a fragment, and a file with none.
    {
        gamedata::WeaponActionTables tables = attack_fixture::test_actions();
        tables.weapons.front().fragments[1].switchBuffer = 0.4F;
        tables.weapons.front().fragments[1].switchAt = 1.133F;
        gamedata::WeaponActions actions;
        check(actions.set(tables) && actions.tables().weapons.front().fragments[1].switchAt > 1.0F
                  && actions.tables().weapons.front().fragments[0].switchAt < 0.0F,
              "the weapon actions keep the switch marks of a fragment (none: -1)");
    }
    check(gamedata::EquipLoadData{}.encumbranceMax == 2000.0F, "the perk data has the limit of the bag weight 2000 with no file member");
#endif
}

// ---- CF11: the tool ------------------------------------------------------------------------------------------------------------

/** CF11: the cases u3-* of the check of the creature tool (made-up animation databases, reaction tables, and buff buckets). */
/** Runs the check of a game data tool (tools/gamedata/SCRIPT with a work folder) and checks its status and the line of each named case. */
void tool_check(const Options& options, const char* script, const char* what, const std::vector<const char*>& names) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / script;
    check(std::filesystem::is_regular_file(tool), std::string("the check of the ") + what + " is in the tree");
    const std::string stem = std::filesystem::path(script).stem().string() + "-u3";
    const std::filesystem::path work = options.workDirectory / stem;
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / (stem + ".log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream file(output);
    const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    check(status == 0, std::string("each case of the check of the ") + what + " passes");
    for (const char* name : names) {
        check(text.find(std::string("ok    ") + name + ":") != std::string::npos, std::string("the case ") + name + " passes");
    }
}

void creature_tool(const Options& options) {
    tool_check(options, "check_creature_tool.py", "creature tool", {"u3-homing", "u3-reaction-table", "u3-additive-scope", "u3-kind-marks"});
}

/**
 * CF13 (Q4, live check of the weight): the item catalog tool gives each weapon and armor item the WeightOverride of its stats row, cut to
 * whole tenths (the weight of the bag of the client), and stops for a stats table with no such column.
 */
void item_catalog_tool(const Options& options) {
    tool_check(options, "check_item_catalog_tool.py", "item catalog tool", {"weight", "weight-missing"});
}

} // namespace

std::vector<Case> combat_feel_cases() {
    return {{"CF1", "stage U3 (Q1): the homing of an attack: the time rule, the record, two sessions", homing_time_rule},
            {"CF2", "stage U3 (Q2): the side of the reaction, the tolerance, the largest power, AlignToDirection, two sessions", reaction_side},
            {"CF3", "stage U3 (Q3): the length of a reaction with the clip HitStun, and its push", hit_stun_length},
            {"CF4", "stage U3 (Q3): the chain of the reactions and its resets", reaction_chain},
            {"CF5", "stage U3 (Q5): the push scales of a promoted elite, the additive reaction in its scope, two sessions", push_scales},
            {"CF6", "stage U3 (Q4): the weight of the bag: the ability, the dodge, the attack from rest, two characters", weight},
            {"CF7", "stage U3 (Q6): the weapon switch in a swing, the kept press, the lost key", weapon_switch},
            {"CF8", "stage U3 (Q6): a press after CanTransition: the chain or a new attack", after_transition},
            {"CF9", "stage U3: the load of 200 homings in a world of 4,200 creatures", homing_load},
            {"CF10", "stage U3: the loaders of the creature data, the weapon actions, and the perk data", loaders},
            {"CF11", "stage U3: the cases u3-* of the check of the creature tool (runs check_creature_tool.py)", creature_tool},
            {"CF12", "stage U3 (Q5): the push of an additive reaction in a run, and of a full reaction", additive_push_in_run},
            {"CF13", "stage U3 (Q4): the weight of a weapon and an armor item in the item catalog (runs check_item_catalog_tool.py)",
             item_catalog_tool}};
}

} // namespace soulwarden::backend::tests
