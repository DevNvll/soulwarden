// The cases of the code review of stage QG5c1 (evidence/quests/qg5c1/code-review/FINDINGS.md), from the review cases with their proofs. Each
// case fails on the tree of stage QG5c1 (trees/after-qg5c1). Made-up accounts, characters, slices, rows, and walls.
//
//   QGR5C1A  QG5C1R-R1: a hit ends a next-hit stun (StopOnHitCount) on a stationary target too; it still gives no reaction and no target
//   QGR5C1B  QG5C1R-R2: a melee contact through a wall (the navigation line) is refused, for a stationary target and for a moving creature;
//            the range, death, and free-line controls stay
//   QGR5C1C  QG5C1R-R3: a ranked producer with no item gate stays a free alternative of its event in the support manifest
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"
#include "support/shop_fixture.h"
#include "support/test_navigation.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::json;

constexpr std::uint32_t kTotemVitals = 1;
constexpr std::uint32_t kTotemPlace = 3;
constexpr std::uint32_t kTotemMarker = 5;
constexpr std::uint32_t kTotemStatus = 7;
constexpr state::WorldPosition kTotemAt{100.0F, 206.0F, 30.0F};
constexpr float kTotemRespawn = 30.0F;

gamedata::CreatureSlice totem_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/totem.dynamicslice";
    slice.asset.fill(0x7A);
    slice.subId = 2;
    slice.root = 0x100000000000007AULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = 0;
    slice.placeSlot = kTotemPlace;
    slice.vitalsSlot = kTotemVitals;
    slice.markerSlot = kTotemMarker;
    slice.slots = {{kTotemVitals, "Vitals", 15}, {2, "DamageReceiver", 1528}, {kTotemPlace, "PositionInTheWorld", 13},
                   {kTotemMarker, "Marker", 5399}, {kTotemStatus, "StatusEffects", 4236}};
    slice.components = {{slice.root, 0x0000000000000A7AULL, "Totem/Vitals"}};
    return slice;
}

gamedata::CreatureKind totem_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Totem_60";
    kind.vitals = "Test_Totem_60";
    kind.category = "Test_Totem_Category";
    kind.vitalsId = 0x7A7A7A01;
    kind.categoryId = 0x7A7A7A02;
    kind.name = "Test Totem";
    kind.vitalsName = "Test Totem";
    kind.creatureType = "Solo";  // no family: a structure of the data (Lost_Totem_60)
    kind.healthFactor = 1.0F;
    kind.level = 20;
    kind.bodies = {{1, 100}};
    return kind;
}

gamedata::SpawnPoint totem_point() {
    gamedata::SpawnPoint point;
    point.id = "Test_Totem_Point";
    point.kind = 1;
    point.level = 20;
    point.position = kTotemAt;
    point.yaw = 90.0F;
    point.respawnMin = kTotemRespawn;
    point.respawnMax = kTotemRespawn;
    return point;
}

json target_quests() {
    json document{{"format", 1},           {"objectives", json::array()}, {"tasks", json::object()}, {"achievements", json::array()},
                  {"npa", json::array()}, {"territories", json::array()}, {"backstories", json::array()}};
    const auto add = [&](const char* objective, const char* task, const char* category) {
        document["tasks"][gamedata::lower_text(task)] = questfix::task(task, "taskkillcontribution", 13, {{"KillEnemyType", category}, {"TargetQty", "2"}});
        document["objectives"].push_back({{"id", objective},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                          {"tree", json::array({json::array({0, gamedata::lower_text(task), json::array()})})}});
    };
    add("Test_TotemHunt", "Test_TotemHunt_Kill", "Test_Totem_Category");
    add("Test_NoTotem", "Test_NoTotem_Kill", "Test_Absent_Category");
    return document;
}

/** The gathering world (two sessions, the wolf and the loot of the gathering cases) with one totem of a spawn point and its quests. */
struct TargetShop : GatherShop {
    TargetShop() {
        gamedata::LootData loot = gathering_loot();
        gamedata::LootVitals totem;
        totem.id = "Test_Totem_60";
        totem.creatureType = "Solo";
        totem.categories = {"test_totem_60", "structure"};
        loot.bases.push_back(totem);
        loot.vitals["test_totem_60"] = static_cast<std::uint32_t>(loot.bases.size() - 1);
        // The credit data of the game: 10 receivers (the gathering cases have 1).
        loot.credit.receivers = 10;
        loot.credit.modifiers.assign(10, {1.0F, 1.0F, 1.0F});
        if (!host.loot.set(loot)) {
            throw std::runtime_error("target test: the loot data was not set: " + host.loot.error());
        }
        if (!host.creatures.set_full({bags::bag_wolf_slice(), totem_slice()}, {bags::bag_wolf_kind(), totem_kind()}, {totem_point()},
                                     {{20, bags::kWolfHealth, 0.0F, 0.0F}})) {
            throw std::runtime_error("target test: the creature data was not set: " + host.creatures.error());
        }
        if (!host.quests.load_text(target_quests().dump())) {
            throw std::runtime_error("target test: the quest data did not load: " + host.quests.error());
        }
        host.world.set_creature_behavior(true);
    }
    world::Entity* totem() {
        for (const world::EntityId id : host.world.creatures().ids()) {
            world::Entity* entity = host.world.find(id);
            if (entity != nullptr && entity->creature.kind != nullptr && entity->creature.kind->id == "Test_Totem_60") {
                return entity;
            }
        }
        return nullptr;
    }
    /** The world goes on by `seconds` in steps of 100 ms; the clients report their place each step. */
    void wait(float seconds) {
        for (float passed = 0.0F; passed < seconds; passed += 0.1F) {
            report(clientA, std::nullopt);
            report(clientB, std::nullopt);
            host.steps(10, {&clientA, &clientB});
        }
    }
};


void stop_on_hit(const Options&) {
    TargetShop shop;
    shop.enter();
    shop.wait(0.5F);
    world::Entity* target = shop.totem();
    world::Entity* wolf = shop.host.world.creatures().spawn("Test_BagWolf", {103.0F, 206.0F, 30.0F}, 0.0F).entity;
    check(target != nullptr && wolf != nullptr, "control: a target and a moving-template creature exist");
    if (!target || !wolf) return;
    gamedata::StatusRow row;
    row.id = "Review_StopOnHit_Stun";
    row.index = 71;
    row.duration = 10.0F;
    row.nameplate = true;
    row.stunned = true;
    row.stopOnHit = 1;
    auto& creatures = shop.host.world.creatures();
    const auto attacker = shop.entity(kCharacterA)->id;
    check(creatures.give_status(*target, row, attacker) && creatures.give_status(*wolf, row, attacker),
          "control: the same next-hit stun is on each template");
    check(target->creature.held(shop.host.clock.now()) && wolf->creature.held(shop.host.clock.now()),
          "control: both stuns are active before their time ends");
    check(creatures.hurt(wolf->id, 1.0F, attacker).error == world::CreatureError::none && wolf->creature.effects.empty(),
          "control: a nonlethal hit ends this stun on the moving template");
    const float before = target->vitals.health;
    check(creatures.hurt(target->id, 1.0F, attacker).error == world::CreatureError::none && target->vitals.health == before - 1.0F,
          "control: the target takes the same nonlethal hit");
    note("QG5C1RV1 after hit: target effects=" + std::to_string(target->creature.effects.size())
         + ", moving effects=" + std::to_string(wolf->creature.effects.size())
         + ", target held=" + std::to_string(target->creature.held(shop.host.clock.now())));
    check(target->creature.effects.empty() && !target->creature.held(shop.host.clock.now()),
          "a next-hit stun must end on a stationary target too");
    gamedata::CellValues condition;
    condition.texts["TargetStatusEffect"] = row.id;
    world::PassiveRule rule;
    rule.id = "Review_Target_Stunned";
    rule.cells = &condition;
    world::HitContext hit;
    const auto& player = *shop.entity(kCharacterA);
    const bool targetPasses = shop.host.world.passives().conditions_pass(rule, player, *target, hit);
    const bool wolfPasses = shop.host.world.passives().conditions_pass(rule, player, *wolf, hit);
    note("QG5C1RV1 next-hit passive condition: target=" + std::to_string(targetPasses) + ", moving=" + std::to_string(wolfPasses));
    check(!targetPasses && !wolfPasses, "a later hit cannot use a stun that the preceding hit should have removed");
    check(!target->creature.action && !target->creature.moving && target->creature.mind.target == 0,
          "control: clearing the effect needs no target, reaction, or movement");
}


void ranked_alternative(const Options&) {
    gather::GatherShop shop;
    json data=gather::gathering_document();
    for (auto index : {gather::kOreRow, gather::kStoneRow}) {
        auto& row=data["rows"][index];
        row["quest"]=true;
        row["type"]="None";
        row["tradeskill"]="AzothStaff";
        row["level"]=2;
        row["time"]=1.0;
        row["loot"]="Empty";
        row["event"]="Review_Shared_Event";
        row["restriction"]=index==gather::kOreRow ? "ItemRequired" : "NoToolNecessary";
        row["item"]=index==gather::kOreRow ? "Review_Absent_Key" : "";
        row["itemCount"]=index==gather::kOreRow ? 1 : 0;
    }
    check(shop.host.gathering.set(data.dump(), {{gather::kRegion,gather::region_records()}}),
          "control: both ranked producers load");
    json document=questfix::quest_document();
    document["tasks"]["review_shared_task"]=questfix::task("Review_Shared_Task","taskgameevent",14,{{"KillEnemyType","Review_Shared_Event"}});
    document["objectives"].push_back({{"id","Review_Shared"},{"cells",{{"Type","Journey"}}},
           {"tree",json::array({json::array({0,"review_shared_task",json::array()})})}});
    check(shop.host.quests.load_text(document.dump()), "control: the event task loads");
    world::QuestSupport support(shop.host.world);
    json fact;
    const json manifest=support.manifest({});
    for(const auto& o:manifest["objectives"]) if(o["id"]=="Review_Shared") fact=o["tasks"][0];
    note("QG5C1RV3 shared ranked event: supported="+std::to_string(fact.value("supported",false))+", reason="+fact.value("reason",std::string{}));
    check(fact.value("supported",false), "a ranked producer with no item gate remains a valid alternative");
    shop.enter();
    auto& a=*shop.entity(kCharacterA);
    a.skills.progressions[gamedata::progression_id("AzothStaff")].rank = 2;
    check(shop.host.world.quests().grant(a,"Review_Shared","command",0).ok(), "control: A has the event task");
    shop.gather(shop.clientA,shop.natural_block(gather::kStone),1500);
    note("QG5C1RV3 free producer: completions="+std::to_string(shop.gathering().counts().completions));
    check(shop.gathering().counts().completions == 1
          && questfix::quest_of(a,"Review_Shared")->tasks[0].progress == 1,
          "control: the free ranked producer completes the runtime task without the other producer's key");
}



void melee_checks(const Options& options) {
    TargetShop shop;
    check(shop.host.combat.set(shop::shop_combat()),"control: the combat tables load");
    shop.host.catalog.set(shop::shop_catalog());
    shop.enter();
    const auto given=shop.host.world.inventory().give(kCharacterA,"TestAxe",1);
    check(given.ok() && !given.slots.empty(), "control: A gets a melee weapon");
    if (!given.ok() || given.slots.empty()) return;
    const auto equipped=shop.host.world.inventory().equip(kCharacterA,given.slots.front(),world::paperdoll::kMainHand1);
    note("QG5C1RV5 equip: "+std::string(world::item_error_text(equipped.error)));
    check(equipped.ok(), "control: A holds a melee weapon");
    check(shop.host.world.entity_of(1)==shop.entity(kCharacterA),"control: the claim session owns A");
    auto& creatures=shop.host.world.creatures();
    auto* near=creatures.spawn("Test_Totem_60",{100.0F,202.0F,30.0F},0.0F).entity;
    auto* far=creatures.spawn("Test_Totem_60",{100.0F,210.0F,30.0F},0.0F).entity;
    auto* dead=creatures.spawn("Test_Totem_60",{101.0F,201.0F,30.0F},0.0F).entity;
    check(near && far && dead,"control: the three target records exist");
    if (!near || !far || !dead) return;
    creatures.kill(dead->id);
    unsigned number=100;
    const auto claim=[&](world::Entity& target){
        world::AttackTrack track;
        track.step(number,true,false,false,shop.host.clock.now());
        world::HitClaim hit;
        hit.input=number+12;
        number+=30;
        hit.target=target.actor;
        hit.targetId=target.id;
        hit.group=target.actor;
        hit.groupId=target.id;
        hit.facet=target.creature.body->target(target.id,target.creature.body->components.front());
        hit.point=target.position;
        hit.observed=true;
        return shop.host.world.combat().claim(1,hit,track);
    };
    check(claim(*far).verdict==world::HitVerdict::tooFar,"a far target is refused for its range");
    check(claim(*dead).verdict==world::HitVerdict::dead,"a dead target is refused for its state");
    const float full=near->vitals.health;
    check(claim(*near).verdict==world::HitVerdict::accepted && near->vitals.health<full,
          "control: an ordinary near target takes the validated melee contact");
    const auto folder=options.workDirectory/"review-target-wall";
    TestNavRegion region;
    region.ground=30.0F;
    region.tiles[{1,3}]=[](int x,int y,std::vector<TestFloor>& floors){
        const float wx=64.0F+(x+0.5F)*0.25F;
        const float wy=192.0F+(y+0.5F)*0.25F;
        if(wx>90.0F && wx<110.0F && wy>201.0F && wy<201.5F){
            floors.clear();
            floors.push_back({33.0F,gamedata::NavFloor::kBlocked});
        }
    };
    region.write(folder);
    check(shop.host.navigation.set_directory(folder.string()),"control: the made-up wall data loads");
    shop.wait(1.0F);
    check(!shop.host.world.navigation().sees(shop.entity(kCharacterA)->position,near->position),
          "control: the navigation line from A to the target is blocked");
    const float before=near->vitals.health;
    const auto result=claim(*near);
    note("QG5C1RV5 wall contact: verdict="+std::string(world::hit_verdict_text(result.verdict))
         +", damage="+std::to_string(before-near->vitals.health));
    check(result.verdict!=world::HitVerdict::accepted && near->vitals.health==before,
          "a target behind a wall must refuse a melee contact");
    // The same for a creature that moves (the shared claim path; the orchestrator: check the creatures too).
    auto* wolf=creatures.spawn("Test_BagWolf",{101.0F,202.0F,30.0F},0.0F).entity;
    check(wolf!=nullptr && !shop.host.world.navigation().sees(shop.entity(kCharacterA)->position,wolf->position),
          "control: a moving creature behind the same wall");
    if (!wolf) return;
    const float wolfBefore=wolf->vitals.health;
    const auto wolfResult=claim(*wolf);
    note("QGR5C1B wall contact on a moving creature: verdict="+std::string(world::hit_verdict_text(wolfResult.verdict)));
    check(wolfResult.verdict!=world::HitVerdict::accepted && wolf->vitals.health==wolfBefore,
          "a moving creature behind a wall refuses a melee contact too");
}

} // namespace

std::vector<Case> review_qg5c1_cases() {
    return {{"QGR5C1A", "review QG5c1, finding QG5C1R-R1: a hit ends a next-hit stun on a stationary target too", stop_on_hit},
            {"QGR5C1B", "review QG5c1, finding QG5C1R-R2: no melee contact through a wall (a stationary target and a moving creature)",
             melee_checks},
            {"QGR5C1C", "review QG5c1, finding QG5C1R-R3: a ranked producer with no item gate stays a free alternative in the manifest",
             ranked_alternative}};
}

} // namespace soulwarden::backend::tests
