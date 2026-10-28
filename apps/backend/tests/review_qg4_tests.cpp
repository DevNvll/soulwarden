// The corrections of the code review of stage QG4 (~/Games/soulwarden/work/evidence/quests/qg4/code-review/FINDINGS.md), in stage QG5a.
// Each case fails on the tree of stage QG4 and passes after the correction. Made-up accounts, characters, ids, items, and volumes only.
//
//   QGR4A1  QG4R-R1: a perk edit of an equipped artifact (the slot keeps the action ItemEquipped of its equip) is no equip: after an
//           abandon of the artifact quest the edit grants nothing; a new equip of the item (unequip, equip) grants it again (control);
//           the other character gets nothing (MP-10)
//   QGR4A2  QG4R-R2: a non-persisted achievement with an ObjectiveFlow is false, also when it has a bit conditional that is true (its
//           gate does not go); an NPA with only a bit conditional keeps its rule (control); the presence of a person with such a
//           condition: no character sees it
//   QGR4A3  QG4R-R3: a trigger box whose center is far outside the coordinates of the grid (1e30 m: the span of its cells looks like one
//           cell) and a point of interest with such a shape point refuse their files before a cell number is made of the coordinate; a
//           file of normal volumes loads (control); a far volume of `set` goes into the list of each lookup

#include <string>

#include "gamedata/poi_data.h"
#include "gamedata/trigger_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/quest_giver_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace questperson;
using namespace questgiver;
using questfix::json;

/** QGR4A1: a perk edit is no equip. */
void perk_edit_is_no_equip(const Options&) {
    GiverShop shop;
    shop.enter();
    check(shop.host.world.progress().set_level(kCharacterA, 6).ok() && shop.host.world.inventory().give(kCharacterA, kArtifactItem, 1).ok()
              && shop.equip_artifact(kCharacterA) && shop.artifact(kCharacterA) != nullptr,
          "setup: A equips the artifact at its item level and gets its quest");
    const std::uint64_t first = shop.artifact(kCharacterA)->instance;
    check(shop.quests().abandon(*shop.entity(kCharacterA), first).ok(), "setup: A abandons the artifact quest");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(shop.artifact(kCharacterA) == nullptr, "setup: the quest is gone after the abandon");
    const std::uint64_t checks = shop.quests().counts().equipChecks;
    check(shop.host.world.inventory().set_perks(kCharacterA, true, world::paperdoll::kRing, {}).ok(), "setup: the perk edit of the ring commits");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    note("QGR4A1 after the perk edit: equip checks +" + std::to_string(shop.quests().counts().equipChecks - checks) + ", quest "
         + std::to_string(shop.artifact(kCharacterA) != nullptr));
    check(shop.artifact(kCharacterA) == nullptr && shop.quests().counts().equipChecks == checks,
          "a perk edit of an equipped artifact is no equip: no event and no artifact quest");
    check(shop.unequip_artifact(kCharacterA) && shop.equip_artifact(kCharacterA) && shop.artifact(kCharacterA) != nullptr,
          "control: a new equip of the same item (unequip, equip) grants the quest again");
    check(shop.artifact(kCharacterB) == nullptr, "the other character gets no artifact quest (MP-10)");
}

/** QGR4A2: an NPA with an ObjectiveFlow is false. */
void objective_flow_is_false(const Options&) {
    GiverShop shop;
    json quests = giver_quests();
    // The form of the data: NPAId, ReqAchievementConditional, ObjectiveFlow, ReqStatusEffectId.
    quests["npa"].push_back(json::array({"*Review_Flow", "", "u:Qg4_Caged,l:Qg4_Caged/Qg4_Wait:c", ""}));
    quests["npa"].push_back(json::array({"*Review_Both", "Test_Swap_Pirate", "u:Qg4_Caged", ""}));
    quests["npa"].push_back(json::array({"*Review_Bit", "Test_Swap_Pirate", "", ""}));
    check(shop.host.quests.load_text(quests.dump()), "setup: the NPA rows load");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit), "setup: A has the bit of the conditional");
    const bool both = shop.quests().conditional(*a, "*Review_Both");
    note("QGR4A2 the NPA with a bit conditional and an ObjectiveFlow: " + std::to_string(both));
    check(!both, "an NPA with an ObjectiveFlow is false also when its bit conditional is true (its gate does not go)");
    check(!shop.quests().conditional(*a, "*Review_Flow") && !shop.quests().conditional(*b, "*Review_Flow"),
          "an NPA with only an ObjectiveFlow is false for each character");
    check(shop.quests().conditional(*a, "*Review_Bit") && !shop.quests().conditional(*b, "*Review_Bit"),
          "control: an NPA with only a bit conditional keeps its rule (A has the bit, B does not)");
    gamedata::PersonBinding binding = *shop.host.interactions.objects()[1].person;
    binding.condition = "*Review_Both";
    check(!shop.persons().present(*a, binding) && !shop.persons().present(*b, binding),
          "a person whose presence condition names such an NPA is there for no character");
}

json far_box() {
    return {{"tag", "review_far"}, {"shape", "box"}, {"center", {1.0e30, 0.0, 0.0}}, {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}}, {"half", {10.0, 10.0, 10.0}}};
}

/** QGR4A3: the coordinates of the grid. */
void grid_coordinates(const Options&) {
    gamedata::TriggerData triggers;
    const bool far = triggers.load_text(json{{"format", 1}, {"volumes", json::array({far_box()})}}.dump());
    note("QGR4A3 a box with the center 1e30: loaded " + std::to_string(far) + " (" + triggers.error() + ")");
    check(!far, "a trigger volume with a corner outside the coordinates of the grid refuses the file (no cell number of 1e30 m)");
    json normal = far_box();
    normal["center"] = {100.0, 200.0, 0.0};
    check(triggers.load_text(json{{"format", 1}, {"volumes", json::array({normal})}}.dump()) && triggers.at(100.0F, 200.0F, 0.0F).size() == 1,
          "control: a box at a normal place loads and the place view finds it");
    gamedata::TriggerVolume test;
    test.tag = "review_set_far";
    test.center = {1.0e30F, 0.0F, 0.0F};
    test.half = {10.0F, 10.0F, 10.0F};
    triggers.set({test});
    check(triggers.at(0.0F, 0.0F, 0.0F).empty(), "a far volume of `set` (a test) is in the list of each lookup, and it does not hold the origin");
    gamedata::PoiData pois;
    const json farShape = {{"format", 1},
                           {"pois", json::array({{{"id", 49101}, {"center", {0, 0, 0}}, {"tags", {"review_far"}},
                                                  {"shape", {{1.0e30, 0}, {1.0e30, 1}, {1.0e30 + 1.0, 1}}}}})}};
    const bool poiFar = pois.load_text(farShape.dump());
    note("QGR4A3 a point of interest with a shape point of 1e30: loaded " + std::to_string(poiFar) + " (" + pois.error() + ")");
    check(!poiFar, "a point of interest with a shape point outside the coordinates of the grid refuses the file");
    const json normalShape = {{"format", 1},
                              {"pois", json::array({{{"id", 49102}, {"center", {0, 0, 0}}, {"tags", {"review_normal"}},
                                                     {"shape", {{-50, -50}, {50, -50}, {50, 50}, {-50, 50}}}}})}};
    check(pois.load_text(normalShape.dump()) && pois.inside({"review_normal"}, 0, 0.0F, 0.0F), "control: a normal point of interest loads");
}

} // namespace

std::vector<Case> review_qg4_cases() {
    return {{"QGR4A1", "review of QG4, QG4R-R1: a perk edit of an equipped artifact is no equip", perk_edit_is_no_equip},
            {"QGR4A2", "review of QG4, QG4R-R2: a non-persisted achievement with an ObjectiveFlow is false", objective_flow_is_false},
            {"QGR4A3", "review of QG4, QG4R-R3: the place loaders refuse a coordinate outside the grid", grid_coordinates}};
}

} // namespace soulwarden::backend::tests
