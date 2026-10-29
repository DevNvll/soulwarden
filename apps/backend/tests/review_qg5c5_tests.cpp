// The cases of the corrections of the code review of stage QG5c5 (findings QG5C5R-R1 to QG5C5R-R5,
// ~/Games/soulwarden/work/evidence/quests/qg5c5/code-review/FINDINGS.md), corrected in stage QG7. The cases are the proof cases of the review
// (its work folder, qg5c5_review_tests.cpp) with the ids of this tree. Each one of QGR5C5A to QGR5C5E fails on the tree of stage QG5c5 and
// passes after the correction; QGR5C5F is the control of the review (a required waiting child). Made-up ids and recipes.
//
//   QGR5C5A  QG5C5R-R1: a part of a group member is a kill source in the support manifest
//   QGR5C5B  QG5C5R-R2: DestroySliceAction removes a part whose body alias selected the named slice
//   QGR5C5C  QG5C5R-R3: a later count whose only earlier object an action between removes is refused
//   QGR5C5D  QG5C5R-R4: the end of a parent runs the end actions of its waiting child
//   QGR5C5E  QG5C5R-R5: two gathers of one encounter object with an ordinary row give one reward
//   QGR5C5F  control: a required waiting child ends one time and its delayed spawn does not come

#include <cstdarg>
#include <cstdio>
#include <string>
#include "support/cases.h"
#include "support/check.h"
#include "support/encounter_pattern_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {
using namespace encfix;
using questfix::json;

/** A note of the log of a case with the values of the review (printf form). */
void report(const char* format, ...) {
    char text[1024];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof text, format, arguments);
    va_end(arguments);
    std::string line = text;
    while (!line.empty() && line.back() == '\n') {
        line.pop_back();
    }
    note(line);
}

constexpr const char* kGroup = "slices/test/review_group.dynamicslice";
constexpr const char* kBody = "slices/test/bag-wolf.dynamicslice";
constexpr const char* kBodyAlias = "sharedassets/test/review_body.aliasasset";

json group_members(bool bodyAlias = false) {
    json members = pattern_members();
    json part{{"name", "Review part"}, {"vitals", "Test_Member"}, {"category", "Test_Member_Category"},
              {"body", bodyAlias ? kBodyAlias : kBody}, {"bodySlices", {kBody}}, {"offset", {0,0,0}}, {"yaw", 0}};
    members[kGroup] = {{"kind", "creature"}, {"vitals", "Test_Member"}, {"body", kBody}, {"group", {part,part}}};
    return members;
}

struct ReviewShop : PatternShop {
    ReviewShop(const std::vector<json>& recipes, json members = group_members()) : PatternShop(recipes, std::move(members)) {
        host.creatures.set_members({{kMemberSlice,{1,""}}, {kBossSlice,{1,""}},
                                   {std::string(kGroup)+"#0",{1,""}}, {std::string(kGroup)+"#1",{1,""}}});
    }
};

json destroy_action(const char* slice) {
    return {{"type","DestroySliceAction"},{"slice",slice},{"alias",nullptr},{"aliasSlices",json::array()},{"localOnly",false}};
}

void group_manifest(const Options&) {
    json recipe = pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Group fight",0x100,kKillSlot,true,destroyed(2,kGroup),
                               json::array({spawn_json(kGroup,0,1,kMemberAt)})),
                     stage_json("Cleanup",0x200,kCleanupSlot,false,timed(100),json::array())}), {0,1});
    ReviewShop shop(std::vector<json>{recipe});
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty() && shop.members(false).size()==2,
          "control: the group-only recipe is taken and both parts live");
    const auto& placement = shop.encounters().placements()[0];
    const auto* member = shop.host.encounters.member(kGroup);
    const auto at = static_cast<std::size_t>(member-shop.host.encounters.members().data());
    check(placement.partKinds[at][0] != nullptr && placement.partKinds[at][1] != nullptr && placement.kinds[at] == nullptr,
          "control: the new part-kind table has both parts, and the old kind table has no group kind");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    json fact;
    for (const json& objective : manifest["objectives"]) {
        if (objective["id"] == "Test_Member_Hunt") fact=objective["tasks"][0];
    }
    report("QGR5C5A task=%s\n",fact.dump().c_str());
    check(fact.value("supported",false),"a live group part is a kill source in the support manifest");
}

void destroy_body_alias(const Options&) {
    json recipe = pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Make group",0x100,kSetupSlot,false,timed(4),json::array({spawn_json(kGroup,0,1,kMemberAt)})),
                     stage_json("Remove selected body",0x200,kCleanupSlot,false,timed(100),json::array())}), {0,1});
    recipe["stages"][1]["startActions"] = json::array({destroy_action(kBody)});
    ReviewShop shop({recipe},group_members(true));
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty() && shop.members(false).size()==2,
          "control: two live group parts have a body alias containing the action's slice");
    shop.wait(5.0F);
    report("QGR5C5B remaining=%zu removed=%llu\n",shop.members(false).size(),
                static_cast<unsigned long long>(shop.encounters().counts().slicesDestroyed));
    check(shop.members(false).empty() && shop.encounters().counts().slicesDestroyed==2,
          "DestroySliceAction removes parts whose body alias contains the named slice");
    // A direct body is the positive control for the action timing and instance membership.
    ReviewShop direct(std::vector<json>{recipe});
    begin(direct);
    direct.wait(5.0F);
    check(direct.members(false).empty() && direct.encounters().counts().slicesDestroyed==2,
          "control: the same action removes both parts when their body directly names the slice");
}

void canceled_earlier_object(const Options&) {
    json recipe = pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Make earlier object",0x100,kSetupSlot,false,timed(3),json::array({spawn_json(kStartSlice,0,1,kStartAt)})),
                     stage_json("Remove before use",0x200,kChildSlotA,false,timed(1),json::array()),
                     stage_json("Use earlier object",0x300,kInteractSlot,true,destroyed(1,kStartSlice),json::array()),
                     stage_json("Cleanup",0x400,kCleanupSlot,false,timed(100),json::array())}), {0,1,2,3});
    recipe["stages"][1]["startActions"] = json::array({destroy_action(kStartSlice)});
    ReviewShop shop(std::vector<json>{recipe});
    begin(shop);
    const auto& placement = shop.encounters().placements()[0];
    report("QGR5C5C refused=%zu\n",placement.refused.size());
    check(!placement.refused.empty(),"refuse a required later count whose only earlier object an intervening action destroys");
    if (!placement.refused.empty()) return;
    check(shop.members(true).size()==1,"control: the only object exists before the action");
    shop.wait(5.0F);
    const auto* run=shop.instance();
    report("QGR5C5C laterStarted=%d laterEnded=%d count=%u objects=%zu destroyed=%llu\n",
                run && run->stages[2].started, run && run->stages[2].ended,
                run ? run->stages[2].counts[0] : 999U,shop.members(true).size(),
                static_cast<unsigned long long>(shop.encounters().counts().slicesDestroyed));
    check(run && run->stages[2].started && !run->stages[2].ended && shop.members(true).empty(),
          "control: the accepted stage waits forever with no object to gather");
}

void canceled_child_actions(const Options&) {
    json wait{{"type","WaitForParentObjective"},{"timeLimit",0},{"succeedOnTimeout",false},{"failureCondition",false}};
    json recipe = pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Timed parent",0x100,kSetupSlot,false,timed(4),json::array()),
                     stage_json("Waiting child",0x200,kChildSlotA,false,wait,json::array({spawn_json(kGroup,0,1,kMemberAt)})),
                     stage_json("Cleanup",0x300,kCleanupSlot,false,timed(100),json::array())}), {0,2});
    recipe["stages"][0]["children"]={1};
    recipe["stages"][1]["endActions"]=json::array({destroy_action(kGroup)});
    ReviewShop shop(std::vector<json>{recipe});
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty() && shop.members(false).size()==2,
          "control: the waiting child and its end action are accepted");
    shop.wait(5.0F);
    const auto* run=shop.instance();
    check(run && run->stages[0].ended && run->stages[1].ended && run->stages[2].started,
          "control: the parent ends the waiting child and starts cleanup");
    report("QGR5C5D remaining=%zu destroyed=%llu\n",shop.members(false).size(),
                static_cast<unsigned long long>(shop.encounters().counts().slicesDestroyed));
    check(shop.members(false).empty() && shop.encounters().counts().slicesDestroyed==2,
          "the waiting child's end runs its DestroySliceAction");
    for (const FakeClient* client : {&shop.clientA,&shop.clientB}) {
        check(statuses(root_seen(*client),kChildSlotA)==std::vector<std::uint32_t>{2,3},
              "control: each observer gets exactly one start and one end for the waiting child");
    }
}

void concurrent_gather(const Options&) {
    json recipe=pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Gather ordinary object",0x100,kInteractSlot,true,destroyed(1,kStartSlice),
                               json::array({spawn_json(kStartSlice,0,1,kStartAt)})),
                     stage_json("Cleanup",0x200,kCleanupSlot,false,timed(100),json::array())}),{0,1});
    ReviewShop shop(std::vector<json>{recipe});
    json data=gathering_document();
    json row=row_json("Review_Ordinary","Gatherables","None","NoToolNecessary","",0,1.0,0,0,
                      "Test_GatherStone","Test_GatherMining",true);
    row["quest"]=false;
    data["rows"].push_back(row);
    json tmpl=template_json(kOreSlice,"review_ordinary","Gatherables");
    tmpl["slice"]=kStartSlice;
    data["templates"].push_back(tmpl);
    data["encounterObjects"]={{kStartSlice,{data["templates"].size()-1,data["rows"].size()-1,"IO_Test_EncStart",false}}};
    check(shop.host.gathering.set(data.dump(),{{kRegion,region_records()}}),"control: an ordinary encounter row with loot and experience loads");
    begin(shop);
    const auto objects=shop.members(true);
    check(shop.encounters().placements()[0].refused.empty() && objects.size()==1,"control: one shared start object waits");
    if (objects.empty()) return;
    const auto block=EncounterShop::start_block(*objects.front());
    shop.request(shop.clientA,block,0);
    shop.request(shop.clientB,block,0);
    check(shop.gathering().work_count()==2,"control: both gatherers begin at the same world time");
    for (int i=0;i<15;++i) {
        shop.report(shop.clientA,block);
        shop.report(shop.clientB,block);
        shop.host.steps(10,{&shop.clientA,&shop.clientB});
    }
    const auto total=chests::bag_count(*shop.entity(kCharacterA),"TestStoneT1")+chests::bag_count(*shop.entity(kCharacterB),"TestStoneT1");
    const auto xp=shop.skill(kCharacterA,"Mining").second+shop.skill(kCharacterB,"Mining").second;
    report("QGR5C5E completions=%llu items=%u xp=%llu contested=%llu\n",
                static_cast<unsigned long long>(shop.gathering().counts().completions),total,
                static_cast<unsigned long long>(xp),static_cast<unsigned long long>(shop.gathering().counts().contested));
    check(total==1 && xp==40 && shop.gathering().counts().completions==1,
          "only the first committed gather of one encounter object grants its loot and experience");
}

void required_waiting_child(const Options&) {
    json wait{{"type","WaitForParentObjective"},{"timeLimit",0},{"succeedOnTimeout",false},{"failureCondition",false}};
    json recipe=pattern_recipe(kKey,kRootAt,
        json::array({stage_json("Required parent",0x100,kSetupSlot,true,destroyed(2,kGroup),json::array()),
                     stage_json("Attack child",0x200,kChildSlotA,false,destroyed(2,kGroup),json::array({spawn_json(kGroup,0,1,kMemberAt)})),
                     stage_json("Waiting child",0x300,kChildSlotB,false,wait,json::array({spawn_json(kMemberSlice,2,1,kMemberAt)})),
                     stage_json("Cleanup",0x400,kCleanupSlot,false,timed(100),json::array())}),{0,3});
    recipe["stages"][0]["children"]={1,2};
    recipe["stages"][0]["childrenOptional"]=false;
    ReviewShop shop(std::vector<json>{recipe});
    begin(shop);
    check(shop.encounters().placements()[0].refused.empty() && shop.members(false).size()==2,
          "control: a required attack followed by a waiting child is taken");
    for (auto* member:shop.members(false)) shop.kill(*member,kCharacterA);
    shop.wait(3.0F);
    const auto* run=shop.instance();
    check(run && run->stages[0].ended && run->stages[1].ended && run->stages[2].ended && run->stages[3].started,
          "a parent with its count does not wait for its required WaitForParent child");
    check(shop.members(false).empty(),"the waiting child's delayed spawn cannot run after the parent ends");
    for (const FakeClient* client:{&shop.clientA,&shop.clientB}) {
        check(statuses(root_seen(*client),kChildSlotB)==std::vector<std::uint32_t>{2,3},
              "both observers get exactly one start and one end of the required waiting child");
    }
}

}
std::vector<Case> review_qg5c5_cases() {
    return {{"QGR5C5A", "review of QG5c5, R1: a part of a group member is a kill source in the support manifest",group_manifest},
            {"QGR5C5B", "review of QG5c5, R2: DestroySliceAction removes a group part through its body alias",destroy_body_alias},
            {"QGR5C5C", "review of QG5c5, R3: refuse a count whose earlier object an action removes before its stage",canceled_earlier_object},
            {"QGR5C5D", "review of QG5c5, R4: the end of a parent runs the end actions of its waiting child",canceled_child_actions},
            {"QGR5C5E", "review of QG5c5, R5: one reward for two gathers of an encounter object with an ordinary row",concurrent_gather},
            {"QGR5C5F", "review of QG5c5 (control): a required waiting child ends one time and cancels its delayed spawn",required_waiting_child}};
}
}
