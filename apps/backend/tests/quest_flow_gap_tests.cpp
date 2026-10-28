// The cases of stage QG5b (docs/systems/quests.md, the rules of stage QG5b): the ObjectiveFlow of a non-persisted achievement, the restriction
// ItemRequired of a gatherable row, the categorical progression reward of a success event (the AzothStaff rank), and the manifest. Made-up
// accounts, characters, ids, items, and objects only.
//
//   QG5BA  the ObjectiveFlow (static read A of stage QG5b): the parser (u and l, objective and task, the modifiers a, c, r, a clause of an
//          unknown prefix left out); the ordered overrides (the last matching clause decides); HasObjective, the task states Active and
//          Complete, the objective completion; a flow with no clause passes; the conditional and the flow with AND; A with the quest and B
//          with none (MP-10); the presence of a person with a flow condition for A only, and its end at the task completion
//   QG5BB  the restriction ItemRequired: no key in the bag refuses the begin and changes nothing; two keys give the gather, the event, and the
//          per-character state, and the keys stay; a key taken during the work refuses the completion; a quantity of 0 needs nothing; B with
//          no key is refused while A gathers (MP-10)
//   QG5BC  the categorical progression reward: the loader (an id and a whole amount; no id: unpaid); the join pays a trade skill of the
//          tables and refuses a faction progression; the hand-in gives the rank (1 experience is 1 rank) in its transaction; the rank of a
//          glyph opens after it (rank 4 refused, rank 5 accepted); at the cap no change; the categorical state of A only to A (MP-10)
//   QG5BD  the manifest: a gather with an AzothStaff rank gate is a producer with a rank need; a gather with ItemRequired needs its item (a
//          quest item: the history decides; no source: blocked)

#include <string>
#include <vector>

#include "gamedata/game_event.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "support/quest_person_fixture.h"
#include "support/test_progression.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using questfix::json;
using interaction::kCharacterA;
using interaction::kCharacterB;

constexpr const char* kKey = "Qg5b_Key";
constexpr const char* kStaff = "Qg5b_Staff";
constexpr std::uint32_t kStaffSlot = 38;

/** @return The number of fragments of a slot of an entity that is not the own one (interest 1) that a client got from `from` on. */
std::size_t foreign_fragments(const FakeClient& client, std::uint32_t slot, std::size_t from) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const tests::Record& record : read_bundle(client.got[index].body).records) {
            for (const tests::Fragment& fragment : record.fragments) {
                count += record.interest != 1 && fragment.slot == slot ? 1 : 0;
            }
        }
    }
    return count;
}

// ----- QG5BA: the ObjectiveFlow -----

void objective_flow(const Options&) {
    const std::vector<gamedata::QuestFlowClause> parsed = gamedata::parse_objective_flow("u:Q_A, l:Q_A/T_1:c,x:Q_B,u:Q_C:r,l:Q_D/T_2:r,u:Q_E:z");
    const auto same = [&](std::size_t index, bool unlock, char modifier, const char* objective, const char* task) {
        return index < parsed.size() && parsed[index].unlock == unlock && parsed[index].modifier == modifier && parsed[index].objective == objective
               && parsed[index].task == task;
    };
    check(parsed.size() == 5 && same(0, true, 'a', "Q_A", "") && same(1, false, 'c', "Q_A", "T_1") && same(2, true, 'r', "Q_C", "")
              && same(3, false, 'a', "Q_D", "T_2") && same(4, true, 'a', "Q_E", ""),
          "the parser of the client: u and l, OBJECTIVE/TASK, :c, :r only with no task, an unknown modifier keeps a, a clause x: is left out");
    questperson::QuestShop shop;
    json quests = questperson::quest_document_18b();
    // The form of the data: NPAId, ReqAchievementConditional, ObjectiveFlow, ReqStatusEffectId.
    for (const auto& [id, conditional, flow] : std::vector<std::tuple<const char*, const char*, const char*>>{
             {"*F_Window", "", "u:Qb_Next,l:Qb_Next/Qb_Next_Whisper:c"},
             {"*F_Relic", "", "u:Qb_Next/Qb_Next_Relic"},
             {"*F_RelicDone", "", "u:Qb_Next/Qb_Next_Relic:c"},
             {"*F_ObjectiveDone", "", "u:Qb_Next:c"},
             {"*F_Order", "", "l:Qb_Next,u:Qb_Next/Qb_Next_Relic"},
             {"*F_Reverse", "", "u:Qb_Next/Qb_Next_Relic,l:Qb_Next"},
             {"*F_Unknown", "", "u:Qb_Nowhere/Qb_Nowhere_Task"},
             {"*F_NoClause", "", "x:Qb_Next"},
             {"*F_Ready", "", "u:Qb_Pouch:r"},
             {"*F_Both", "Qb_Keep", "u:Qb_Next"}}) {
        quests["npa"].push_back(json::array({id, conditional, flow, ""}));
    }
    check(shop.host.quests.load_text(quests.dump()), "setup: the NPA rows with a flow load");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    world::Quests& quests_ = shop.host.world.quests();
    const auto value = [&](world::Entity& who, const char* id) { return quests_.conditional(who, id); };
    check(!value(a, "*F_Window") && !value(a, "*F_Relic") && value(a, "*F_NoClause") && value(b, "*F_NoClause"),
          "with no quest no clause matches (false); a flow with no clause passes (true) for each character");
    check(quests_.grant(a, questperson::kNext, "command", 0).ok(), "setup: A gets Qb_Next (the task Qb_Next_Relic is Active)");
    note("QG5BA with Qb_Next: window " + std::to_string(value(a, "*F_Window")) + ", relic " + std::to_string(value(a, "*F_Relic")) + ", relic done "
         + std::to_string(value(a, "*F_RelicDone")) + ", order " + std::to_string(value(a, "*F_Order")) + ", reverse "
         + std::to_string(value(a, "*F_Reverse")));
    check(value(a, "*F_Window") && value(a, "*F_Relic") && !value(a, "*F_RelicDone") && !value(a, "*F_ObjectiveDone"),
          "HasObjective matches u:OBJ, an Active task matches OBJ/TASK, not OBJ/TASK:c; OBJ:c matches only the completion");
    check(value(a, "*F_Order") && !value(a, "*F_Reverse"), "the clauses are ordered overrides: the last clause that matches decides");
    check(!value(a, "*F_Unknown"), "an objective that the data does not have matches nothing");
    check(!value(b, "*F_Window") && !value(b, "*F_Relic") && !value(b, "*F_Order"), "B has no instance: none of its flows passes (MP-10)");
    check(!value(a, "*F_Both"), "the conditional and the flow with AND: no bit Qb_Keep, false");
    const std::optional<std::uint32_t> keep = shop.host.world.ownership().achievement_bit("Qb_Keep");
    check(keep && shop.host.world.ownership().unlock_achievement(a, "Qb_Keep", *keep) && value(a, "*F_Both"), "with the bit both gates pass");
    // The presence of a person with a flow condition (the persons of Nighthaven: showWhen *SwapActive_18007_Cecilia).
    gamedata::PersonBinding binding = *shop.host.interactions.objects()[1].person;
    binding.condition = "*F_Window";
    binding.presence = gamedata::PersonPresence::showWhen;
    check(shop.persons().present(a, binding) && !shop.persons().present(b, binding), "the person with the flow condition is there for A only");
    // The progress of A: the relic task completes (2 relics); the whisper task (TaskTriggerGameEvent) completes at its activation and
    // closes the window.
    check(shop.host.world.inventory().give(kCharacterA, "QbRelic", 2).ok(), "setup: A gets the two relics");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    note("QG5BA after the relics: relic done " + std::to_string(value(a, "*F_RelicDone")) + ", window " + std::to_string(value(a, "*F_Window")));
    check(value(a, "*F_RelicDone") && value(a, "*F_Relic"), "the relic task is Complete: OBJ/TASK:c and OBJ/TASK match");
    check(!value(a, "*F_Window") && !shop.persons().present(a, binding) && !shop.persons().present(b, binding),
          "the whisper task is Complete: the lock clause (the last one) matches, and the person goes for A");
    const std::optional<std::uint32_t> done = shop.host.world.ownership().achievement_bit("Qb_Next_Done");
    check(done && shop.host.world.ownership().unlock_achievement(a, "Qb_Next_Done", *done) && value(a, "*F_ObjectiveDone") && !value(b, "*F_ObjectiveDone"),
          "the completion achievement of the objective matches OBJ:c (IsObjectiveComplete), for A only");
    check(quests_.grant(a, "Qb_Pouch", "command", 0).ok() && value(a, "*F_Ready") && !value(b, "*F_Ready"),
          "OBJ:r matches an instance that is ready for completion (the root is done)");
}

// ----- QG5BB: the restriction ItemRequired -----

/** The gather shop with a quest object of the restriction ItemRequired (the key, `count`) and the key in the catalog. */
void item_shop(gather::GatherShop& shop, std::uint32_t count) {
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
    row["itemCount"] = count;
    check(shop.host.gathering.set(data.dump(), {{gather::kRegion, gather::region_records()}}), "setup: the row of ItemRequired loads (" + shop.host.gathering.error() + ")");
    std::vector<gamedata::ItemDefinition> items = gather::gathering_items();
    items.push_back(chests::chest_item(kKey, "Resource", "Resource", 1, 100));
    shop.host.catalog.set(items);
}

void item_required(const Options&) {
    gather::GatherShop shop;
    item_shop(shop, 2);
    check(shop.host.gathering.rows()[gather::kOreRow].unsupported.empty() && shop.host.gathering.rows()[gather::kOreRow].itemCount == 2,
          "the loader takes ItemRequired with an item (no refusal cause), with its count");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    const auto refused = [&]() { return shop.gathering().counts().refusals[static_cast<std::size_t>(world::GatherRefusal::missingItem)]; };
    check(shop.host.world.inventory().give(kCharacterA, kKey, 1).ok(), "setup: A has one key of two");
    const world::Bag before = a.bag;
    shop.request(shop.clientA, shop.capital_block());
    shop.end_block(shop.clientA);
    check(refused() == 1 && shop.gathering().counts().begins == 0 && a.questGathers.empty() && a.bag.slots.size() == before.slots.size()
              && world::Inventory::bag_count(a, kKey) == 1,
          "one key of two: the begin is refused (missingItem), and nothing changes");
    check(shop.host.world.inventory().give(kCharacterA, kKey, 1).ok(), "setup: A has two keys");
    shop.request(shop.clientB, shop.capital_block());
    shop.end_block(shop.clientB);
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    note("QG5BB: completions " + std::to_string(shop.gathering().counts().completions) + ", refusals " + std::to_string(refused()) + ", A keys "
         + std::to_string(world::Inventory::bag_count(a, kKey)));
    check(shop.gathering().counts().completions == 1 && a.questGathers.size() == 1 && world::Inventory::bag_count(a, kKey) == 2,
          "two keys: the gather completes and gives the per-character state; the keys stay in the bag (no removal: an agent rule)");
    check(refused() == 2 && b.questGathers.empty(), "B with no key is refused, and the gather of A gives B nothing (MP-10)");

    // A key that goes during the work: the completion checks the gates again.
    gather::GatherShop second;
    item_shop(second, 1);
    second.enter();
    world::Entity& a2 = *second.entity(kCharacterA);
    check(second.host.world.inventory().give(kCharacterA, kKey, 1).ok(), "setup: A has the key");
    second.request(second.clientA, second.capital_block());
    second.hold(second.clientA, second.capital_block(), 300);
    std::uint32_t slot = 0;
    for (; slot < a2.bag.slots.size() && !(a2.bag.slots[slot].item && a2.bag.slots[slot].item->itemId == kKey); ++slot) {
    }
    check(second.gathering().counts().begins == 1 && second.host.world.inventory().remove(kCharacterA, slot).ok(), "setup: the work runs; the key goes");
    second.hold(second.clientA, second.capital_block(), 1500);
    second.end_block(second.clientA);
    check(second.gathering().counts().completions == 0 && a2.questGathers.empty(), "a key that goes during the work refuses the completion");

    // A quantity of 0 needs nothing (the client check 0x48D626D).
    gather::GatherShop third;
    item_shop(third, 0);
    third.enter();
    third.gather(third.clientA, third.capital_block(), 1500);
    check(third.gathering().counts().completions == 1, "ItemRestrictionQuantity 0: no item needed");
}

// ----- QG5BC: the categorical progression reward -----

json staff_quests() {
    json document = questfix::quest_document();
    document["tasks"]["qg5b_ready"] = questfix::task("Qg5b_Ready", "taskunlockachievement", 20, {{"ItemName", "Qg5b_Ready_Bit"}});
    document["achievements"].push_back(json::array({"Qg5b_Ready_Bit", 3901}));
    for (const auto& [id, event] : std::vector<std::pair<const char*, const char*>>{
             {"Qg5b_StaffQuest", "Qg5b_Staff_Success"}, {"Qg5b_StaffQuest2", "Qg5b_Staff_Success"}, {"Qg5b_FactionQuest", "Qg5b_Faction_Success"}}) {
        document["objectives"].push_back({{"id", id},
                                          {"cells", {{"Type", "Journey"}, {"SuccessGameEventId", event}}},
                                          {"tree", json::array({json::array({0, "qg5b_ready", json::array()})})}});
    }
    document["events"]["qg5b_staff_success"] = {{"id", "Qg5b_Staff_Success"}, {"CategoricalProgressionId", "AzothStaff"},
                                                {"CategoricalProgressionReward", "1"}};
    document["events"]["qg5b_faction_success"] = {{"id", "Qg5b_Faction_Success"}, {"CategoricalProgressionId", "Covenant"},
                                                  {"CategoricalProgressionReward", "100"}};
    return document;
}

void staff_reward(const Options&) {
    const gamedata::QuestGameEvent staff =
        gamedata::read_game_event("S", {{"EventID", "S"}, {"CategoricalProgressionId", "AzothStaff"}, {"CategoricalProgressionReward", "1"}});
    const gamedata::QuestGameEvent noId = gamedata::read_game_event("N", {{"EventID", "N"}, {"CategoricalProgressionReward", "5"}});
    const gamedata::QuestGameEvent fraction =
        gamedata::read_game_event("F", {{"EventID", "F"}, {"CategoricalProgressionId", "Riding"}, {"CategoricalProgressionReward", "15299.5"}});
    check(staff.progression == "AzothStaff" && staff.progressionAmount == 1 && staff.unpaid.empty() && noId.progression.empty()
              && !noId.unpaid.empty() && !fraction.unpaid.empty(),
          "the loader: the id and a whole amount; no id or a fraction stays unpaid");
    gather::GatherShop shop;
    json data = gather::gathering_document();
    json& row = data["rows"][gather::kOreRow];
    row["quest"] = true;
    row["type"] = "AzothStaff";
    row["tradeskill"] = "AzothStaff";
    row["level"] = 5;
    row["time"] = 1.0;
    row["event"] = "";
    row["loot"] = "Empty";
    data["tools"][kStaff] = {{"types", {"AzothStaff"}}, {"min", 1.0}, {"max", 1.0}, {"efficiency", 0.0}, {"multiplier", 0.0}};
    check(shop.host.gathering.set(data.dump(), {{gather::kRegion, gather::region_records()}}), "setup: the glyph row of rank 5 loads");
    std::vector<gamedata::ItemDefinition> items = gather::gathering_items();
    items.push_back(chests::chest_item(kStaff, "Weapon", "AzothStaff", 1, 1, 100, 200));
    shop.host.catalog.set(items);
    gamedata::ProgressionData progression = gather::gathering_progression();
    gamedata::ProgressionRow skill = progression.progressions.front();
    skill.id = "AzothStaff";
    skill.wireId = gamedata::progression_id("AzothStaff");
    skill.kind = gamedata::ProgressionKind::tradeSkill;
    skill.maxRank = 5;
    skill.expansionMaxRank = 0;
    skill.ranks.experience = {1, 1, 1, 1, 1, 0};
    progression.progressions.push_back(skill);
    shop.host.progression.set(progression);
    check(shop.host.quests.load_text(staff_quests().dump()), "setup: the quests of the reward load (" + shop.host.quests.error() + ")");
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    world::Quests& quests = shop.host.world.quests();
    world::RewardJoin join;
    std::string detail;
    check(quests.reward_join(a, *shop.host.quests.objective("Qg5b_StaffQuest"), 0, join, detail) == world::QuestError::none
              && join.progression == "AzothStaff" && join.progressionAmount == 1,
          "the join pays AzothStaff 1 (a trade skill of the progression tables)");
    const world::QuestError faction = quests.reward_join(a, *shop.host.quests.objective("Qg5b_FactionQuest"), 0, join, detail);
    note("QG5BC the faction reward: " + detail);
    check(faction == world::QuestError::cannotPay && detail.find("Covenant") != std::string::npos,
          "a faction progression (not a trade skill of the tables) refuses the join with its cause");
    check(shop.equip(kCharacterA, kStaff, kStaffSlot) && shop.set_rank(kCharacterA, "AzothStaff", 4), "setup: the staff in slot 38, rank 4");
    shop.request(shop.clientA, shop.capital_block());
    shop.end_block(shop.clientA);
    check(shop.gathering().counts().begins == 0 && shop.gathering().counts().refusals[static_cast<std::size_t>(world::GatherRefusal::rank)] == 1,
          "rank 4: the glyph of rank 5 is refused");
    const world::QuestResult granted = quests.grant(a, "Qg5b_StaffQuest", "command", 0);
    check(granted.ok() && quests.ready(*questfix::quest_of(a, "Qg5b_StaffQuest")), "setup: the staff quest is ready");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    const world::HandInResult paid = quests.hand_in(a, granted.instance);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    note("QG5BC the hand-in: " + paid.progression + " " + std::to_string(paid.progressionAmount) + ", rank " + std::to_string(paid.progressionRank));
    check(paid.ok() && paid.progression == "AzothStaff" && paid.progressionRank == 5 && shop.skill(kCharacterA, "AzothStaff").first == 5,
          "the hand-in gives 1 experience: the rank 4 becomes 5 (AutoRankUp: 1 experience for each rank)");
    check(!slot_updates(shop.clientA, kCategoricalSlot, fromA).fragments.empty() && foreign_fragments(shop.clientB, kCategoricalSlot, fromB) == 0
              && shop.skill(kCharacterB, "AzothStaff").first == 0,
          "the categorical state of A goes to A only; B has no rank (MP-10)");
    shop.gather(shop.clientA, shop.capital_block(), 1500);
    check(shop.gathering().counts().completions == 1 && a.questGathers.size() == 1 && b.questGathers.empty(),
          "rank 5 from the hand-in: the glyph gather completes for A only");
    const world::QuestResult again = quests.grant(a, "Qg5b_StaffQuest2", "command", 0);
    const world::HandInResult capped = quests.hand_in(a, again.instance);
    check(again.ok() && capped.ok() && shop.skill(kCharacterA, "AzothStaff") == std::pair<std::uint32_t, std::uint64_t>{5, 0},
          "at the cap (5) a second reward is paid and changes nothing (the experience at the largest rank is 0)");
}

// ----- QG5BD: the manifest -----

void manifest_rules(const Options&) {
    gather::GatherShop shop;
    json data = gather::gathering_document();
    json& glyph = data["rows"][gather::kOreRow];
    glyph["quest"] = true;
    glyph["type"] = "AzothStaff";
    glyph["tradeskill"] = "AzothStaff";
    glyph["level"] = 2;
    glyph["loot"] = "Empty";
    glyph["event"] = "Qg5b_Glyph_Interact";
    json& lock = data["rows"][gather::kStoneRow];
    lock["quest"] = true;
    lock["loot"] = "Empty";
    lock["event"] = "Qg5b_Lock_Interact";
    lock["restriction"] = "ItemRequired";
    lock["item"] = kKey;
    lock["itemCount"] = 1;
    data["capitals"][interaction::object_of(0x22, gamedata::InteractionKind::gathering, {120.0F, 198.0F, 30.0F}, {gather::kGatherOptionIndex}).key] =
        json::array({gather::kStoneSlice, gather::kStoneRow});
    check(shop.host.gathering.set(data.dump(), {{gather::kRegion, gather::region_records()}}), "setup: the glyph and the lock load (" + shop.host.gathering.error() + ")");
    std::vector<gamedata::ItemDefinition> items = gather::gathering_items();
    items.push_back(chests::chest_item(kKey, "Resource", "Resource", 1, 100));
    shop.host.catalog.set(items);
    json quests = questfix::quest_document();
    quests["tasks"]["qg5b_glyph"] = questfix::task("Qg5b_Glyph", "taskgameevent", 14, {{"KillEnemyType", "Qg5b_Glyph_Interact"}, {"TargetQty", "1"}});
    quests["tasks"]["qg5b_lock"] = questfix::task("Qg5b_Lock", "taskgameevent", 14, {{"KillEnemyType", "Qg5b_Lock_Interact"}, {"TargetQty", "1"}});
    quests["tasks"]["qg5b_give_key"] = questfix::task("Qg5b_Give_Key", "taskgiveitem", 33, {{"ItemName", kKey}, {"TargetQty", "1"}});
    quests["objectives"].push_back({{"id", "Qg5b_GlyphQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qg5b_glyph", json::array()})})}});
    quests["tasks"]["qg5b_lock_root"] = questfix::task("Qg5b_Lock_Root", "consecutivetaskcontainer", 1);
    quests["objectives"].push_back({{"id", "Qg5b_LockQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qg5b_give_key", json::array()}), json::array({1, "qg5b_lock", json::array()}),
                                                          json::array({2, "qg5b_lock_root", json::array({0, 1})})})}});
    check(shop.host.quests.load_text(quests.dump()), "setup: the quests of the manifest load (" + shop.host.quests.error() + ")");
    world::QuestSupport support(shop.host.world);
    const json result = support.manifest({});
    const auto task_of = [&](const char* objective, std::size_t index) {
        for (const json& row : result["objectives"]) {
            if (row["id"] == objective) {
                for (const json& task : row["tasks"]) {
                    if (task["index"] == index) {
                        return task;
                    }
                }
            }
        }
        return json();
    };
    const json glyphTask = task_of("Qg5b_GlyphQuest", 0);
    const json lockTask = task_of("Qg5b_LockQuest", 1);
    note("QG5BD the glyph: " + glyphTask.value("supplier", std::string{}) + glyphTask.value("reason", std::string{}) + "; the lock: "
         + lockTask.value("supplier", std::string{}) + lockTask.value("reason", std::string{}) + " (" + lockTask.value("questItem", std::string{}) + ")");
    check(glyphTask.value("supported", false) && glyphTask.value("supplier", std::string{}).find("a gather with the rank 2 of the trade skill AzothStaff") == 0,
          "a gather with an AzothStaff rank gate is a producer with a rank need (the history decides)");
    check(lockTask.value("supported", false) && lockTask.value("questItem", std::string{}) == "qg5b_key"
              && lockTask.value("supplier", std::string{}).find("with the item qg5b_key of a quest") != std::string::npos,
          "a gather with ItemRequired needs its item: a quest give (the history must have it)");
}

} // namespace

std::vector<Case> quest_flow_gap_cases() {
    return {{"QG5BA", "quests QG5b (I1, MP-10): the ObjectiveFlow of an NPA: the parser, the overrides, the quest state, the presence", objective_flow},
            {"QG5BB", "quests QG5b (I2, MP-10): the restriction ItemRequired: the bag check at the begin and the completion, no removal", item_required},
            {"QG5BC", "quests QG5b (I3, MP-10): the categorical progression reward: the AzothStaff rank of a hand-in and the glyph", staff_reward},
            {"QG5BD", "quests QG5b (I5): the manifest: the rank need of a glyph producer, the item need of an ItemRequired producer", manifest_rules}};
}

} // namespace soulwarden::backend::tests
