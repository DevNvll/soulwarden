// The corrections of the code review of stage U4 (~/Games/soulwarden/work/evidence/menu-equipment/u4/code-review/FINDINGS.md), corrected in
// stage 3.7b. Each case fails on the tree before the correction and passes after it. The values are made up.
//
//   CRU1  finding U4-R1 (the request): a recipe with no cause in the crafting file whose output has a gear score range and no override in
//         the item catalog (the gear score rule of stage 3.7c) is refused at the request: no cost, no fee, no plain item of gear score 100
//   CRU2  finding U4-R1 (the load): the crafting file and the item catalog do not agree: the file is refused as a whole

#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/crafting_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace crafting;

/** The items of the crafting tests with the knife as an item with a gear score range (100 to 300) and no override. */
std::vector<gamedata::ItemDefinition> ranged_items() {
    std::vector<gamedata::ItemDefinition> items = craft_items();
    for (gamedata::ItemDefinition& item : items) {
        if (item.id == "TestKnifeT1") {
            item.gearScoreOverride = 0;
            item.minGearScore = 100;
            item.maxGearScore = 300;
        }
    }
    return items;
}

void ranged_request(const Options&) {
    Crafter shop(true);
    shop.host.catalog.set(ranged_items());
    shop.enter(true);
    for (const char* character : {kCharacterA, kCharacterB}) {
        shop.host.world.inventory().set_coin(character, 1000);
        shop.give(character, "TestFlintT1", 1);
        shop.give(character, "TestWoodT1", 1);
    }
    shop.use_station(shop.clientA);
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")});
    shop.finish();
    const world::Entity* a = shop.entity(kCharacterA);
    check(shop.count(kCharacterA, "TestKnifeT1") == 0 && shop.count(kCharacterA, "TestFlintT1") == 1 && shop.count(kCharacterA, "TestWoodT1") == 1
              && a->wallet.coin == 1000
              && shop.crafting().counts().verdicts[static_cast<std::size_t>(world::CraftVerdict::unsupported)] == 1,
          "a recipe with no cause whose output has a gear score range and no override in the catalog: refused (the rule of stage 3.7c), no "
          "cost, no fee, no item");
    check(shop.count(kCharacterB, "TestKnifeT1") == 0 && shop.count(kCharacterB, "TestFlintT1") == 1 && shop.entity(kCharacterB)->wallet.coin == 1000,
          "the second character: no change");
}

void ranged_load(const Options&) {
    gamedata::ItemCatalog ranged;
    ranged.set(ranged_items());
    gamedata::ItemCatalog fixed;
    fixed.set(craft_items());
    gamedata::CraftingData data;
    const bool loaded = data.set(craft_document());
    const bool refused = !data.match_catalog(ranged);
    check(loaded && refused && !data.loaded() && data.error().find("TestKnifeT1") != std::string::npos,
          "the file and a catalog with a ranged output of a recipe with no cause: the file is refused as a whole (" + data.error() + ")");
    gamedata::CraftingData good;
    check(good.set(craft_document()) && good.match_catalog(fixed) && good.loaded(),
          "the file and a catalog with a fixed output (the override 100): the file stays");
}

} // namespace

std::vector<Case> review_u4_cases() {
    return {{"CRU1", "review of stage U4, U4-R1: a no-cause recipe whose output has a gear score range is refused at the request", ranged_request},
            {"CRU2", "review of stage U4, U4-R1: a crafting file that does not agree with the catalog is refused as a whole", ranged_load}};
}

} // namespace soulwarden::backend::tests
