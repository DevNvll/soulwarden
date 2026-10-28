// The corrections of the code review of stage QG5b (~/Games/soulwarden/work/evidence/quests/qg5b/code-review/FINDINGS.md), in stage QG5c1.
// Each case fails on the tree of stage QG5b and passes after the correction (the proof cases of the review, adapted). Made-up accounts, items,
// quests, and achievements only.
//
//   QGR5B1  QG5BR-R1: a row of ItemRequired with an item and no member itemCount is refused (a count of 0 would need nothing): the empty bag
//           gets no gather and no per-character completion; a complete descriptor still refuses the empty bag; a row with no member item
//           stays refused; a negative count clears the loader
//   QGR5B2  QG5BR-R2: a TaskAchievement wait whose ObjectiveFlow names a task follows a change of that task by an item (no bit changes); the
//           wait of B stays (MP-10)
//   QGR5B3  QG5BR-R3: the manifest keeps the item need of a producer with a rank gate and an ItemRequired item (the rank alone is not enough)

#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "support/quest_person_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using questfix::json;
using interaction::kCharacterA;
using interaction::kCharacterB;
constexpr const char* kKey = "Review_Key";

json item_data() {
    json data = gather::gathering_document();
    json& row = data["rows"][gather::kOreRow];
    row["quest"] = true;
    row["type"] = "None";
    row["tradeskill"] = "";
    row["level"] = 0;
    row["time"] = 1.0;
    row["event"] = "";
    row["loot"] = "Empty";
    row["restriction"] = "ItemRequired";
    row["item"] = kKey;
    row["itemCount"] = 2;
    return data;
}

void load_item_shop(gather::GatherShop& shop, const json& data) {
    check(shop.host.gathering.set(data.dump(), {{gather::kRegion, gather::region_records()}}), "control: the gathering data loads");
    std::vector<gamedata::ItemDefinition> items = gather::gathering_items();
    items.push_back(chests::chest_item(kKey, "Resource", "Resource", 1, 1));
    shop.host.catalog.set(items);
}

/** QGR5B1: a missing itemCount. */
void missing_count(const Options&) {
    gather::GatherShop full;
    load_item_shop(full, item_data());
    full.enter();
    full.gather(full.clientA, full.capital_block(), 1500);
    check(full.gathering().counts().begins == 0 && full.gathering().counts().completions == 0,
          "control: a complete descriptor needs two items and refuses the empty bag");
    json old = item_data();
    old["rows"][gather::kOreRow].erase("item");
    gamedata::GatheringData loader;
    check(loader.set(old.dump(), {{gather::kRegion, gather::region_records()}}) && !loader.rows()[gather::kOreRow].unsupported.empty(),
          "control: a row with no member item stays refused");
    json bad = item_data();
    bad["rows"][gather::kOreRow]["itemCount"] = -1;
    check(!loader.set(bad.dump(), {{gather::kRegion, gather::region_records()}}) && !loader.loaded(), "control: a negative count clears the loader");
    json partial = item_data();
    partial["rows"][gather::kOreRow].erase("itemCount");
    gather::GatherShop shop;
    load_item_shop(shop, partial);
    shop.enter();
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    const gamedata::GatherableRow& row = shop.host.gathering.rows()[gather::kOreRow];
    note("QGR5B1 a missing itemCount: causes " + std::to_string(row.unsupported.size()) + ", completions "
         + std::to_string(shop.gathering().counts().completions));
    check(!row.unsupported.empty() && row.unsupported.front().find("itemCount") != std::string::npos,
          "a row with an item and no member itemCount is refused with its cause");
    check(shop.gathering().counts().completions == 0 && shop.entity(kCharacterA)->questGathers.empty(),
          "a missing itemCount does not turn the item gate into a gather with the empty bag");
}

/** QGR5B2: a flow wait after a change of an item task. */
void flow_after_items(const Options&) {
    questperson::QuestShop shop;
    json document = questperson::quest_document_18b();
    document["tasks"]["review_source_task"] = questfix::task("Review_Source_Task", "taskhaveitems", 25, {{"ItemName", "QbRelic"}, {"TargetQty", "1"}});
    document["tasks"]["review_wait_task"] = questfix::task("Review_Wait_Task", "taskachievement", 18, {{"ItemName", "*Review_Flow"}});
    for (const auto& [id, task] : std::vector<std::pair<const char*, const char*>>{{"Review_Source", "review_source_task"}, {"Review_Wait", "review_wait_task"}}) {
        document["objectives"].push_back(
            {{"id", id}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, task, json::array()})})}});
    }
    document["npa"].push_back(json::array({"*Review_Flow", "", "u:Review_Source/Review_Source_Task:c", ""}));
    check(shop.host.quests.load_text(document.dump()), "control: the source and the wait load");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    world::Quests& quests = shop.host.world.quests();
    check(quests.grant(a, "Review_Source", "command", 0).ok() && quests.grant(a, "Review_Wait", "command", 0).ok()
              && quests.grant(b, "Review_Wait", "command", 0).ok(),
          "control: A has the source and the wait; B has its own wait");
    check(!quests.conditional(a, "*Review_Flow") && questfix::quest_of(a, "Review_Wait")->tasks[0].state == world::quest_state::kActive,
          "control: the flow and the wait are false before the item");
    const auto bits = a.achievements;
    check(shop.host.world.inventory().give(kCharacterA, "QbRelic", 1).ok(), "control: the item changes the source task of A");
    shop.host.steps(4, {&shop.clientA, &shop.clientB});
    check(questfix::quest_of(a, "Review_Source")->tasks[0].state == world::quest_state::kComplete && quests.conditional(a, "*Review_Flow")
              && a.achievements == bits,
          "control: the source is Complete and its flow is true with no change of a bit");
    note("QGR5B2 after the item: the wait of A " + std::to_string(questfix::quest_of(a, "Review_Wait")->tasks[0].state));
    check(questfix::quest_of(a, "Review_Wait")->tasks[0].state == world::quest_state::kComplete,
          "the TaskAchievement wait follows a flow that an item task changed");
    check(questfix::quest_of(b, "Review_Wait")->tasks[0].state == world::quest_state::kActive, "the wait of B stays Active (MP-10)");
}

/** QGR5B3: a producer with a rank gate and an item gate in the manifest. */
void combined_manifest(const Options&) {
    gather::GatherShop shop;
    json data = item_data();
    json& row = data["rows"][gather::kOreRow];
    row["type"] = "AzothStaff";
    row["tradeskill"] = "AzothStaff";
    row["level"] = 2;
    row["event"] = "Review_Glyph_Event";
    load_item_shop(shop, data);
    json document = questfix::quest_document();
    document["tasks"]["review_glyph_task"] = questfix::task("Review_Glyph_Task", "taskgameevent", 14, {{"KillEnemyType", "Review_Glyph_Event"}});
    document["objectives"].push_back(
        {{"id", "Review_Glyph"}, {"cells", {{"Type", "Journey"}}}, {"tree", json::array({json::array({0, "review_glyph_task", json::array()})})}});
    check(shop.host.quests.load_text(document.dump()), "control: the quest loads; no source gives Review_Key");
    const auto fact = [&]() {
        world::QuestSupport support(shop.host.world);
        const json manifest = support.manifest({});
        for (const json& objective : manifest["objectives"]) {
            if (objective["id"] == "Review_Glyph") {
                return objective["tasks"][0];
            }
        }
        return json();
    };
    const json combined = fact();
    row["level"] = 0;
    load_item_shop(shop, data);
    const json itemOnly = fact();
    check(!itemOnly.value("supported", true) && itemOnly.value("reason", std::string{}).find("review_key") != std::string::npos,
          "control: the producer with only the item gate is blocked (its key has no source)");
    note("QGR5B3 the rank and the item: supported " + std::to_string(combined.value("supported", false)) + ", "
         + combined.value("reason", std::string{}));
    check(!combined.value("supported", true) && combined.value("reason", std::string{}).find("review_key") != std::string::npos,
          "a rank gate does not remove the item gate of the producer: the task is blocked for its key");
}

} // namespace

std::vector<Case> review_qg5b_cases() {
    return {{"QGR5B1", "review of QG5b (QG5BR-R1): a missing itemCount of an ItemRequired row is refused", missing_count},
            {"QGR5B2", "review of QG5b (QG5BR-R2, MP-10): a flow wait follows a change of an item task", flow_after_items},
            {"QGR5B3", "review of QG5b (QG5BR-R3): the manifest keeps the item need of a ranked producer", combined_manifest}};
}

} // namespace soulwarden::backend::tests
