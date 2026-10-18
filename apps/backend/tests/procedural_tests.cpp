// The cases of stage 3.7c (world/crafting.h, world/perk_roll.h, docs/systems/crafting.md, protocol notes 4.52): the procedural results,
// the perk items and the Azoth, the currency ingredients, and the craft bonus of the statuses. All ids, items, and values are made up.
//
//   CRAFTC1     the craft gear score: BaseGearScore, the rank of the trade skill for the tier, the materials of a procedural recipe, the
//               status bonus to its cap 50, the override, the MaxCraftGS of the item and of the expansion; the claim of the request; no
//               extra unit for a recipe that is not craft-all and not refining
//   CRAFTC2     the generator with the inputs of a craft: the resources in their order, G, the push flag, the count target, one Inherent
//               perk, one gem, a resource that can give no perk; the loot path keeps its result
//   CRAFTC3     the perk items and the Azoth at the station: the charm, the upgrade, the two-step rounding, the refusals with no change,
//               the live bytes of 2997 of both push values
//   CRAFTC4     the craft bonus of the statuses (state 205 field 1): a food and its end, the status of a perk of an equipped item, a
//               trophy with stacks
//   CRAFTC5     a currency ingredient (Azoth_Currency): the entry of the client (type Item, item 0), the balance, the refusals
//   CRAFTC6     a T5 item with five perks and a gem slot, the swap, the Azoth of G = 4, and the persistence after a restart
//   CRAFTC-DATA the loader of format 3 and its rules
//   CRAFTC-MP   two sessions: the perk items, the Azoth, and the bonus map of A do not change B; B gets no state 205 of A
//   CRAFTC-LOAD the perk rolls visit only the buckets of the recipe: 20,000 more buckets and 100 more characters give the same visits
//   CRAFTC-CMD  the commands: the preview with the craft gear score, the perk plan, and the seeded perks; the balance command

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/crafting_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/procedural_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace procedural;
using nlohmann::json;

constexpr std::uint32_t kCraftingSlot = 63;
constexpr std::uint32_t kCraftingType = 205;
constexpr std::uint32_t kEngineeringCode = 6;

std::uint64_t verdicts(world::Crafting& crafting, world::CraftVerdict verdict) {
    return crafting.counts().verdicts[static_cast<std::size_t>(verdict)];
}

/** The bonus map (field 1) of the fragments of state 205 that a client got, the last one first; empty: no fragment. */
std::vector<std::vector<std::pair<std::uint8_t, std::uint16_t>>> bonus_maps(const FakeClient& client, std::size_t from = 0) {
    std::vector<std::vector<std::pair<std::uint8_t, std::uint16_t>>> maps;
    for (const Fragment& fragment : slot_updates(client, kCraftingSlot, from).fragments) {
        if (fragment.type != kCraftingType) {
            continue;
        }
        server::hub::Reader bonuses(field_of(fragment, 0, 1));
        if (bonuses.size() != 0 || bonuses.u8() != 1) {
            continue;
        }
        (void)bonuses.size64();
        const std::uint32_t count = bonuses.size();
        std::vector<std::pair<std::uint8_t, std::uint16_t>> map;
        for (std::uint32_t index = 0; index < count; ++index) {
            const std::uint8_t code = bonuses.u8();
            map.emplace_back(code, bonuses.u16());
        }
        if (bonuses.ok() && bonuses.left() == 0) {
            maps.push_back(map);
        }
    }
    std::reverse(maps.begin(), maps.end());
    return maps;
}

world::RandomUnit constant(float value) {
    return [value] { return value; };
}

std::size_t count_perks(const world::PerkIds& perks) {
    return static_cast<std::size_t>(std::count_if(perks.begin(), perks.end(), [](const std::string& perk) { return !perk.empty(); }));
}

/** CRAFTC1: the craft gear score. */
void gear_score(const Options&) {
    Procedural shop(false);
    shop.enter(false);
    world::Crafting& crafting = shop.crafting();
    const gamedata::CraftingData& data = shop.host.crafting;
    const gamedata::Recipe& knife = *data.recipe_of("TestKnifeT2Recipe");
    const gamedata::ItemDefinition& out = *shop.host.catalog.find("TestSkinKnifeT2");
    const std::vector<std::string> plain{"TestMetal2", "TestTimber2", "TestHide2"};
    const auto score = [&](const gamedata::Recipe& recipe, const gamedata::ItemDefinition& output, const std::vector<std::string>& selected) {
        return crafting.gear_score_of(*shop.entity(kCharacterA), recipe, output, selected);
    };
    const world::CraftGearScore base = score(knife, out, plain);
    check(base.rule && base.base == 200 && base.rank == 5 && base.materials == 0 && base.status == 0 && base.value == 205,
          "the knife of tier 2 at Engineering 0 with plain materials: 200 + 5 (MaxGearScoreBonusT2 of rank 0) = 205 (live check L5: 205)");
    check(score(knife, out, {"TestMetal2Fae", "TestTimber2", "TestHide2"}).value == 210
              && score(knife, out, {"TestMetal2", "TestTimber3", "TestHide2"}).value == 205,
          "a member with IngredientGearScoreMaxBonus 5 gives 210 (live: Fae Iron 210); a timber of tier 3 changes nothing (live: Lumber 210)");
    shop.rank(kCharacterA, "Engineering", 50);
    const std::uint32_t at50 = score(knife, out, plain).value;
    shop.rank(kCharacterA, "Engineering", 100);
    const std::uint32_t at100 = score(knife, out, plain).value;
    shop.rank(kCharacterA, "Engineering", 0);
    check(at50 == 255 && at100 == 275, "Engineering 50: 255 (live: 255); Engineering 100: 275 (" + std::to_string(at50) + ", " +
                                           std::to_string(at100) + ")");
    world::StatusApply food;
    food.seconds = 30.0F;
    shop.host.world.statuses().apply(*shop.entity(kCharacterA), "FoodTestEngineer", food);
    shop.host.steps(2, {&shop.clientA});
    const world::CraftGearScore fed = score(knife, out, plain);
    shop.host.world.statuses().apply(*shop.entity(kCharacterA), "FoodTestEngineerBig", food);
    shop.host.steps(2, {&shop.clientA});
    const world::CraftGearScore capped = score(knife, out, plain);
    check(fed.status == 5 && fed.value == 210 && capped.statusSum == 65 && capped.status == 50 && capped.value == 255,
          "the food of Engineering (5): 210; a sum of 65 counts 50 (the cap of the client; live: the entry 60 gave 255)");
    const gamedata::Recipe& crown = *data.recipe_of("TestCrownRecipe");
    check(score(crown, *shop.host.catalog.find("TestCrown"), {"TestMetal5"}).value == 600, "an output with an override: its override (600)");
    shop.rank(kCharacterA, "Engineering", 50);
    check(score(*data.recipe_of("TestKnifeCapRecipe"), out, plain).value == 207, "the MaxCraftGS of the item (207) caps the score (305)");
    check(!score(*data.recipe_of("TestKnifeT1"), *shop.host.catalog.find("TestKnifeT1"), {"TestFlintT1", "TestWoodT1"}).rule,
          "a recipe with no BaseGearScore has no craft gear score rule");
    shop.rank(kCharacterA, "Engineering", 0);
    shop.host.world.statuses().remove(*shop.entity(kCharacterA), [](const world::ActiveStatus&) { return true; }, world::StatusEnd::command);
    shop.host.steps(2, {&shop.clientA});
    // The request: the claim of the client must be 0 or the craft gear score of the server.
    shop.host.world.inventory().set_coin(kCharacterA, 100000);
    shop.give_knife(kCharacterA, 3);
    shop.use_station(shop.clientA);
    const std::size_t from = shop.clientA.got.size();
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 206);
    shop.finish();
    const bool refused = verdicts(crafting, world::CraftVerdict::gearScore) == 1 && shop.count(kCharacterA, "TestMetal2") == 21
                         && shop.item(kCharacterA, "TestSkinKnifeT2") == nullptr;
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205);
    shop.finish();
    const world::ItemInstance* made = shop.item(kCharacterA, "TestSkinKnifeT2");
    bool created = false;
    for (const SeenItem& seen : bag_elements(shop.clientA, from)) {
        created = created || (seen.item.gearScore == 205 && seen.item.action == world::sync::kItemCreated && seen.item.index != 0);
    }
    check(refused && made != nullptr && made->gearScore == 205 && created && shop.count(kCharacterA, "TestMetal2") == 14,
          "the claim 206 is refused with no change (client 0x2E41E10); the claim 205 gives a knife of 205 with the cause ItemCreated");
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 0);
    shop.finish();
    check(shop.count(kCharacterA, "TestMetal2") == 7 && crafting.counts().jobs == 2, "the claim 0 is accepted");
    // A procedural recipe is not craft-all and not refining: a material with roll bonus points gives a chance, but no extra unit (the
    // screen of the client shows no bonus chance for it; live: Fae Iron on the knife). The preview takes the selection of the client.
    shop.give(kCharacterA, "TestMetal2Fae", 7);
    const world::CraftPreview fae = crafting.preview(kCharacterA, "TestKnifeT2Recipe", 1, 7, {}, 0, {"TestMetal2Fae", "-", "-"});
    note("CRAFTC1 fae: verdict " + std::string(world::craft_verdict_text(fae.verdict)) + ", chance " + std::to_string(fae.yield.chance) +
         ", extra " + std::to_string(fae.yield.extraUnits) + ", gear score " + std::to_string(fae.craftGearScore.value));
    check(fae.verdict == world::CraftVerdict::accepted && fae.craftGearScore.value == 210 && fae.yield.chance > 0.0
              && fae.yield.extraUnits == 0.0 && fae.yield.maxExtra == 0 && fae.draws == std::vector<std::uint32_t>{0},
          "the knife with the Fae metal: the chance 0.1 percent, no extra unit (a recipe that is not craft-all and not refining)");
    // The cap of the expansion (a made-up MaxCraftGS 590 of the row of no expansion): the axe of 600 gets 590.
    Procedural capped590(false, ":memory:", false, true);
    capped590.enter(false);
    capped590.rank(kCharacterA, "Weaponsmithing", 200);
    world::StatusApply trophy;
    trophy.seconds = 600.0F;
    for (int stack = 0; stack < 3; ++stack) {
        capped590.host.world.statuses().apply(*capped590.entity(kCharacterA), "TestTrophyEngineer", trophy);
    }
    capped590.host.steps(2, {&capped590.clientA});
    const world::CraftGearScore axe = capped590.crafting().gear_score_of(*capped590.entity(kCharacterA), *capped590.host.crafting.recipe_of("TestAxeT5Recipe"),
                                                                          *capped590.host.catalog.find("TestGreatAxeT5"),
                                                                          {"TestMetal5", "TestTimber5", "TestHide5"});
    check(axe.rank == 25 && axe.materials == 60 && axe.status == 15 && axe.cap == 590 && axe.value == 590,
          "the axe: 500 + 25 + 60 + 15 (a trophy of 3 stacks) = 600, the cap of the expansion 590 (" + std::to_string(axe.value) + ")");
}

/** CRAFTC2: the generator with the inputs of a craft. */
void generator(const Options&) {
    Procedural shop(false);
    const gamedata::PerkData& perks = shop.host.perks;
    const gamedata::ItemDefinition& axe = *shop.host.catalog.find("TestGreatAxeT5");
    const gamedata::ItemDefinition& bag = *shop.host.catalog.find("TestBagT5");
    const auto inputs = [&](std::vector<const char*> resources, std::uint32_t guaranteed, bool keep, std::uint32_t maxPerks) {
        world::CraftPerkInputs value;
        for (const char* bucket : resources) {
            value.resources.push_back(perks.bucket(bucket));
        }
        value.guaranteed = guaranteed;
        value.keepBuckets = keep;
        value.maxPerks = maxPerks;
        return value;
    };
    // The resources in the order of the request, before the count roll (chance bonus 0, chance 1).
    const world::CraftPerkRoll order = world::roll_craft_perks(perks, axe, 600, inputs({"Test_AxeCharmBucket", "Test_GemCutBucket"}, 0, true, 5),
                                                               constant(0.99F));
    const world::CraftPerkRoll swapped = world::roll_craft_perks(perks, axe, 600, inputs({"Test_GemCutBucket", "Test_AxeCharmBucket"}, 0, true, 5),
                                                                 constant(0.99F));
    check(order.perks == world::PerkIds{"PerkID_TestCraft_D", "PerkID_Gem_VoidDMG4", "", "", ""}
              && swapped.perks == world::PerkIds{"PerkID_Gem_VoidDMG4", "PerkID_TestCraft_D", "", "", ""} && order.target == 5,
          "the resources first, in the order of the request; with 0.99 no ordinary bucket passes the count roll (target 5)");
    // G = 1: the first ordinary bucket that gets a chance gets the modifier 1 (1.9): it rolls.
    const world::CraftPerkRoll one = world::roll_craft_perks(perks, axe, 600, inputs({"Test_AxeCharmBucket", "Test_GemCutBucket"}, 1, true, 5),
                                                             constant(0.99F));
    check(one.perks == world::PerkIds{"PerkID_TestCraft_D", "PerkID_Gem_VoidDMG4", "PerkID_Stat_TwoHandSoldier", "", ""}
              && std::fabs(one.chances[0].second - 1.9F) < 1e-6F && std::fabs(one.chances[1].second - 0.5F) < 1e-6F
              && one.chances[3].second == 0.0F,
          "G = 1: the attribute bucket has 1.9 and rolls; the count stops the chances at the target (2 resource perks + 3 buckets)");
    // The push flag: false takes the first ordinary bucket of the type of a resource that gave a perk out (the bag), true keeps it.
    const world::CraftPerkRoll pushed = world::roll_craft_perks(perks, bag, 520, inputs({"Test_BagCharmBucket"}, 0, false, 4), constant(0.0F));
    const world::CraftPerkRoll kept = world::roll_craft_perks(perks, bag, 520, inputs({"Test_BagCharmBucket"}, 0, true, 4), constant(0.0F));
    check(pushed.chances.size() == 3 && pushed.chances[0].first == "Test_BagB"
              && pushed.perks == world::PerkIds{"PerkID_TestBag_E", "PerkID_TestBag_B", "PerkID_TestBag_C", "PerkID_TestBag_D", ""}
              && kept.chances.size() == 4
              && kept.perks == world::PerkIds{"PerkID_TestBag_E", "PerkID_TestBag_A", "PerkID_TestBag_B", "PerkID_TestBag_C", ""},
          "push false: the charm takes the bucket Test_BagA out (E, B, C, D); push true: the buckets stay, and the target stops at C");
    // One Inherent perk: an attribute charm leaves the attribute bucket with no chance.
    const world::CraftPerkRoll attribute = world::roll_craft_perks(perks, axe, 600, inputs({"Test_AttrCharmBucket"}, 0, true, 5), constant(0.0F));
    check(attribute.perks[0] == "PerkID_Stat_TwoHandSoldier" && attribute.chances[0].second == 0.0F && count_perks(attribute.perks) == 5,
          "an attribute charm: the Inherent bucket gets no chance; the other buckets give the rest of the 5 perks");
    // One gem: a cut gem and the gem bucket of the item give one gem (a decision of an agent from the instance rule of stage 1.3b).
    const world::CraftPerkRoll gem = world::roll_craft_perks(perks, axe, 600, inputs({"Test_GemCutBucket"}, 3, true, 5), constant(0.0F));
    const auto gems = std::count_if(gem.perks.begin(), gem.perks.end(), [&](const std::string& id) {
        return !id.empty() && perks.find(id) != nullptr && perks.find(id)->gem();
    });
    check(gems == 1 && gem.perks[0] == "PerkID_Gem_VoidDMG4", "a cut gem: the gem bucket of the axe gives no second gem");
    // A resource that can give the item no perk gives none and takes no bucket out.
    const world::CraftPerkRoll none = world::roll_craft_perks(perks, axe, 600, inputs({"Test_SwordCharmBucket"}, 0, false, 5), constant(0.99F));
    check(none.resourcePerks == std::vector<std::string>{""} && none.chances.size() == 5 && count_perks(none.perks) == 0,
          "a resource with no perk for the class of the item: no perk, the 5 buckets stay");
    // The largest count: 4 at 520 (the bag), and the count roll of 0.0 at each place gives the target only.
    const world::CraftPerkRoll full = world::roll_craft_perks(perks, axe, 600, inputs({}, 5, true, 5), constant(0.0F));
    check(count_perks(full.perks) == 5 && count_perks(world::roll_craft_perks(perks, bag, 520, inputs({}, 0, true, 4), constant(0.0F)).perks) == 4,
          "the count target: 5 perks of the axe at 600, 4 of the bag at 520");
    // The loot path keeps its rule: a craft roll with no resource, G 0, and the general count has the same perks.
    std::uint64_t state = 77;
    std::uint64_t again = 77;
    const world::PerkIds loot = world::roll_perks(perks, axe, 600, 60, [&state] { return world::World::next_unit(state); });
    const world::PerkIds craft = world::roll_craft_perks(perks, axe, 600, inputs({}, 0, true, perks.max_perks(5, 600)),
                                                         [&again] { return world::World::next_unit(again); })
                                     .perks;
    check(loot == craft && state == again, "the craft roll with no input is the loot roll (same perks, same draws)");
}

/** The fields of the live 2997 requests of the live checks L5, L5b, and L5c (after the facet prefix), with a made-up target and G. */
Bytes live_body(const std::string& fields) {
    Bytes body{0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x4C, 0x55, 0x4F, 0x53, 0x00, 0x00, 0x00, 0x02};
    for (std::size_t index = 0; index + 1 < fields.size(); index += 2) {
        body.push_back(static_cast<std::uint8_t>(std::stoul(fields.substr(index, 2), nullptr, 16)));
    }
    return body;
}

/** CRAFTC3: the perk items and the Azoth at the station. */
void perk_items(const Options&) {
    // The live bytes: the knife with the Steel Skinners Charm (upgrade 0) and with Azoth 1; the axe (push on) with the Gem Setting Pins
    // and with Azoth 1; the bag (push off) with the Silver of Adderstone.
    struct Live {
        const char* fields;
        std::vector<std::uint32_t> perks;
        std::uint8_t upgrade;
        std::uint16_t gearScore;
    };
    const std::vector<Live> lives{
        {"fedd0895000000010000001900d2014f660ed8000301000000019bd77f6d0100000001ab8017770100000001086caab6", {0x4F660ED8U}, 0, 210},
        {"fedd0895000000010000001900cd00010301000000012997d9940100000001dc8727e10100000001086caab6", {}, 1, 205},
        {"9f832bda00000001000001c1025801b3bc2b6100030100000001d5d8d8d80100000001f5424d9501000000010099624f", {0xB3BC2B61U}, 0, 600},
        {"9f832bda00000001000001c102580001030100000001d5d8d8d80100000001f5424d9501000000010099624f", {}, 1, 600},
        {"5c7d4f8b00000001000001c1023501d19ba570000401000000010099624f0100000001322b19e00100000001d5d8d8d8010000000107edd97f", {0xD19BA570U}, 0, 565}};
    bool parsed = true;
    for (const Live& live : lives) {
        std::uint64_t entity = 0;
        const std::optional<world::CraftRequest> request = server::rep::read_craft_request(live_body(live.fields), entity);
        parsed = parsed && request && request->perkItems == live.perks && request->upgrade == live.upgrade && request->gearScore == live.gearScore;
    }
    check(parsed && gamedata::item_id_crc("SkinnerCharmT1") == 0x4F660ED8U && gamedata::item_id_crc("GemSocketPinsT1") == 0xB3BC2B61U
              && gamedata::item_id_crc("AdderStoneT1") == 0xD19BA570U,
          "the live 2997 of L5 and L5b: the perk item CRCs (resource ids) in order, then the upgrade byte, for both push values");
    check(world::Crafting::azoth_of(15, 1, 0.45F) == 8 && world::Crafting::azoth_of(15, 2, 0.45F) == 16 && world::Crafting::azoth_of(15, 3, 0.45F) == 24
              && world::Crafting::azoth_of(15, 4, 0.45F) == 33 && world::Crafting::azoth_of(15, 5, 0.45F) == 41
              && world::Crafting::azoth_of(15, 1, 0.0F) == 15 && world::Crafting::azoth_of(15, 2, 1.0F) == 0
              && world::Crafting::azoth_of(0, 3, 0.45F) == 0,
          "the Azoth of the levels 1 to 5 at a station of tier 5 (0.45): 8, 16, 24, 33, 41 (live L5: the same); no discount 15; 1.0: 0");
    Procedural shop(false);
    shop.enter(false);
    world::Crafting& crafting = shop.crafting();
    shop.host.world.inventory().set_coin(kCharacterA, 100000);
    shop.give_knife(kCharacterA, 4);
    shop.give(kCharacterA, "TestToolCharm", 3);
    shop.give(kCharacterA, "TestSwordCharm", 1);
    shop.azoth(kCharacterA, 500);
    shop.use_station(shop.clientA);
    const std::uint32_t charm = gamedata::item_id_crc("TestToolCharm");
    // The refusals: no change.
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {gamedata::item_id_crc("TestSwordCharm")});
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {0x12345678U});
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {charm, charm});
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {charm}, 1);
    shop.give(kCharacterA, "TestFlintT1", 1);
    shop.give(kCharacterA, "TestWoodT1", 1);
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT1", 1, {entry("TestFlintT1"), entry("TestWoodT1")}, 1, 100, {}, 1);
    shop.finish();
    check(verdicts(crafting, world::CraftVerdict::perkItems) == 3 && verdicts(crafting, world::CraftVerdict::upgrade) == 2
              && shop.count(kCharacterA, "TestToolCharm") == 3 && shop.count(kCharacterA, "TestMetal2") == 28 && shop.balance(kCharacterA) == 500
              && crafting.counts().jobs == 0,
          "refused with no change: a charm with no perk for the class, an unknown resource, 2 charms for 1, a charm and G 1 above the 1 perk "
          "of the knife, an upgrade of a recipe that is not procedural");
    // The charm with G 0: one unit of the charm, its perk, no Azoth.
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {charm});
    shop.finish();
    const world::ItemInstance* charmed = shop.item(kCharacterA, "TestSkinKnifeT2");
    check(charmed != nullptr && charmed->perks == world::PerkIds{"PerkID_TestTool_XP", "", "", "", ""} && shop.count(kCharacterA, "TestToolCharm") == 2
              && shop.balance(kCharacterA) == 500,
          "the charm: the knife has its perk, one charm is gone, no Azoth (the charm alone is not an upgrade)");
    // G 1 with no charm, a batch of 2: 8 Azoth for each job (16), each knife gets the perk of its bucket (1.15).
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 2, knife_entries(), 50, 205, {}, 1);
    shop.finish(20);
    std::size_t perked = 0;
    std::size_t knives = 0;
    for (const world::BagSlot& slot : shop.entity(kCharacterA)->bag.slots) {
        if (slot.item && slot.item->itemId == "TestSkinKnifeT2") {
            ++knives;
            perked += count_perks(slot.item->perks) == 1 ? 1 : 0;
        }
    }
    check(knives == 3 && perked == 3 && shop.balance(kCharacterA) == 484 && crafting.counts().azothPaid == 16,
          "G 1, a batch of 2: 8 Azoth for each job (500 to 484), each knife has one perk");
    // Not enough Azoth: refused, no change.
    shop.azoth(kCharacterA, 7);
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {}, 1);
    shop.finish();
    check(verdicts(crafting, world::CraftVerdict::notEnoughCurrency) == 1 && shop.balance(kCharacterA) == 7 && knives == 3,
          "7 Azoth for a cost of 8: refused (notEnoughCurrency), no change");
}

/** CRAFTC4: the craft bonus of the statuses. */
void status_bonus(const Options&) {
    Procedural shop(true);
    shop.enter(true);
    world::Entity* a = shop.entity(kCharacterA);
    check(bonus_maps(shop.clientA).empty() && a->craftGearBonus.empty(), "no status bonus: no state 205 in the first record");
    world::StatusApply food;
    food.seconds = 30.0F;
    std::size_t from = shop.clientA.got.size();
    shop.host.world.statuses().apply(*a, "FoodTestEngineer", food);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    const auto fed = bonus_maps(shop.clientA, from);
    const gamedata::Recipe& knife = *shop.host.crafting.recipe_of("TestKnifeT2Recipe");
    const gamedata::ItemDefinition& out = *shop.host.catalog.find("TestSkinKnifeT2");
    const std::vector<std::string> plain{"TestMetal2", "TestTimber2", "TestHide2"};
    check(!fed.empty() && fed.front() == std::vector<std::pair<std::uint8_t, std::uint16_t>>{{kEngineeringCode, 5}}
              && shop.crafting().gear_score_of(*a, knife, out, plain).value == 210 && bonus_maps(shop.clientB, 0).empty(),
          "the food: field 1 of state 205 has the entry {6 (Engineering), 5} for the owner; the knife 210; the observer gets no state 205");
    // The end of the food after its 30 s: the map is empty again (a fragment with an empty map), the knife 205.
    from = shop.clientA.got.size();
    for (int second = 0; second < 32; ++second) {
        shop.host.advance(std::chrono::milliseconds(1000));
        shop.host.steps(2, {&shop.clientA, &shop.clientB});
    }
    const auto ended = bonus_maps(shop.clientA, from);
    check(a->craftGearBonus.empty() && !ended.empty() && ended.front().empty() && shop.crafting().gear_score_of(*a, knife, out, plain).value == 205,
          "the end of the food (30 s): an empty bonus map goes to the owner, and the knife has 205 again");
    // The status of a perk of an equipped item: the chest with PerkID_TestEngineer (its affix keeps Status_Test_Engineer, 2).
    world::GiveOptions options;
    options.perks = world::PerkIds{"PerkID_TestEngineer", "", "", "", ""};
    const world::ItemResult given = shop.host.world.inventory().give(kCharacterA, "TestEngineChest", 1, options);
    const world::ItemResult worn = given.ok() ? shop.host.world.inventory().equip(kCharacterA, given.slots.front()) : given;
    shop.host.steps(2, {&shop.clientA});
    const auto equipped = a->craftGearBonus;
    const world::ItemResult off = shop.host.world.inventory().unequip(kCharacterA, 1);
    shop.host.steps(2, {&shop.clientA});
    check(worn.ok() && off.ok() && equipped == std::map<std::uint8_t, std::uint16_t>{{kEngineeringCode, 2}} && a->craftGearBonus.empty(),
          "the perk of an equipped chest: Engineering 2 while it is on, nothing after the unequip");
    // A trophy with 3 stacks: 15 for Weaponsmithing (0) and Engineering (6).
    world::StatusApply trophy;
    trophy.seconds = 600.0F;
    for (int stack = 0; stack < 3; ++stack) {
        shop.host.world.statuses().apply(*a, "TestTrophyEngineer", trophy);
    }
    shop.host.steps(2, {&shop.clientA});
    check(a->craftGearBonus == std::map<std::uint8_t, std::uint16_t>{{0, 15}, {kEngineeringCode, 15}},
          "a status of 3 stacks of 5: 15 for each of its trade skills");
}

/** CRAFTC5: a currency ingredient. */
void currency(const Options&) {
    Procedural shop(false);
    shop.enter(false);
    world::Crafting& crafting = shop.crafting();
    shop.host.world.inventory().set_coin(kCharacterA, 1000);
    shop.give(kCharacterA, "TestOre", 5);
    shop.azoth(kCharacterA, 300);
    shop.use_station(shop.clientA);
    const std::vector<Entry> live{entry("TestOre"), Entry{1, 1, 0}};
    shop.craft(shop.clientA, kCharacterA, "TestAzothRecipe", 1, {entry("TestOre"), Entry{1, 1, gamedata::item_id_crc("Azoth_Currency")}}, 0, 0);
    shop.craft(shop.clientA, kCharacterA, "TestAzothRecipe", 2, live, 0, 0);
    shop.finish();
    const bool refused = verdicts(crafting, world::CraftVerdict::badIngredients) == 1
                         && verdicts(crafting, world::CraftVerdict::notEnoughCurrency) == 1 && shop.balance(kCharacterA) == 300
                         && shop.count(kCharacterA, "TestOre") == 5;
    shop.craft(shop.clientA, kCharacterA, "TestAzothRecipe", 1, live, 0, 0);
    shop.finish();
    check(refused && shop.balance(kCharacterA) == 50 && shop.count(kCharacterA, "TestOre") == 4 && shop.count(kCharacterA, "TestRefined") == 1,
          "the Azoth ingredient: the entry of the client is type Item with the item 0 (live L5c); an other item is refused; 2 jobs need 500 "
          "(refused); one job: 300 to 50 and the output");
}

/** CRAFTC6: a T5 item with five perks and a gem slot, the swap, the Azoth of G 4, the persistence. */
void five_perks(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::string path = (options.workDirectory / "crafting-c6.sqlite3").string();
    std::filesystem::remove(path);
    world::InstanceId instance{};
    world::PerkIds made{};
    {
        Procedural shop(false, path);
        shop.enter(false);
        shop.host.world.inventory().set_coin(kCharacterA, 100000);
        shop.rank(kCharacterA, "Weaponsmithing", 200);
        shop.give_axe(kCharacterA, 1);
        shop.give(kCharacterA, "TestAxeCharm", 1);
        shop.azoth(kCharacterA, 500);
        world::StatusApply trophy;
        trophy.seconds = 600.0F;
        for (int stack = 0; stack < 3; ++stack) {
            shop.host.world.statuses().apply(*shop.entity(kCharacterA), "TestTrophyEngineer", trophy);
        }
        shop.use_station(shop.clientA);
        const std::size_t from = shop.clientA.got.size();
        shop.craft(shop.clientA, kCharacterA, "TestAxeT5Recipe", 1, axe_entries(), 449, 600, {gamedata::item_id_crc("TestAxeCharm")}, 4);
        shop.finish();
        const world::ItemInstance* axe = shop.item(kCharacterA, "TestGreatAxeT5");
        if (axe != nullptr) {
            instance = axe->instance;
            made = axe->perks;
        }
        std::size_t wirePerks = 0;
        for (const SeenItem& seen : bag_elements(shop.clientA, from)) {
            if (seen.item.gearScore == 600) {
                wirePerks = static_cast<std::size_t>(std::count_if(seen.item.perks.begin(), seen.item.perks.end(), [](std::uint32_t perk) { return perk != 0; }));
            }
        }
        // G 4 with the charm: the attribute (1.9), Test_Craft3 (1.5), the gem slot (1.6), Test_Craft4 (1.35): each rolls; the swap of the
        // game makes the Soldier perk of a weapon BaseDamage.
        check(axe != nullptr && axe->gearScore == 600 && count_perks(made) == 5 && wirePerks == 5
                  && std::find(made.begin(), made.end(), "PerkID_Gem_EmptyGemSlot") != made.end()
                  && std::find(made.begin(), made.end(), "PerkID_StatSwap_BaseDamage") != made.end()
                  && std::find(made.begin(), made.end(), "PerkID_Stat_TwoHandSoldier") == made.end() && made[0] == "PerkID_TestCraft_D"
                  && shop.balance(kCharacterA) == 467 && shop.count(kCharacterA, "TestAxeCharm") == 0,
              "the axe of 600 with the charm and G 4: 5 perks (the charm first, a gem slot, the Soldier perk swapped to BaseDamage), 33 Azoth");
    }
    {
        Procedural shop(false, path, true);
        shop.enter(false);
        const world::ItemInstance* axe = shop.item(kCharacterA, "TestGreatAxeT5");
        check(axe != nullptr && axe->instance == instance && axe->perks == made && axe->gearScore == 600 && shop.balance(kCharacterA) == 467,
              "after a restart: the same instance, gear score, and perks; the Azoth balance");
    }
}

/** CRAFTC-DATA: the loader of format 3. */
void data_rules(const Options&) {
    gamedata::CraftingData data;
    check(data.set(procedural_document()) && data.format() == 3 && data.rank_gear_score("Engineering", 0, 2) == 5
              && data.rank_gear_score("Engineering", 60, 2) == 55 && data.rank_gear_score("Engineering", 999, 2) == 75
              && data.rank_gear_score("Weaponsmithing", 200, 5) == 25 && data.rank_gear_score("Weaponsmithing", 200, 1) == 0
              && data.currency("azoth_currency") != nullptr && data.currency("Azoth_Currency")->cap == 2500
              && data.resource(gamedata::item_id_crc("TestToolCharm")) != nullptr && data.resource_of("testtoolcharm")->bucket == "Test_ToolCharmBucket"
              && data.craft_max_perks(5, 600) == 5 && data.craft_max_perks(5, 640) == 4 && data.craft_max_perks(2, 205) == 2
              && data.material("TestMetal2Fae").gearScoreMax == 5 && data.chance_modifier("Gem") == 1.0F && data.chance_modifier("Other") == 0.0F,
          "format 3: the rank gear score (the last row above the table), the cap of Azoth, the resources, the perk counts of crafting, the "
          "gear score of a material, the chance modifiers");
    const auto refused = [](const std::function<void(json&)>& change, const char* what) {
        json document = json::parse(procedural_document());
        change(document);
        gamedata::CraftingData file;
        check(!file.set(document.dump()), std::string("refused as a whole: ") + what + " (" + file.error() + ")");
    };
    refused([](json& document) { document.erase("currencies"); }, "no member currencies");
    refused([](json& document) { document["currencies"] = json::object(); }, "a recipe with no cause and a currency with no cap");
    refused([](json& document) { document["perkGeneration"]["tiers"][5][0][1][0] = {500, 400}; }, "a gear score range that ends below its start");
    refused([](json& document) { document["rankGearScore"]["Engineering"][0] = {0, 5}; }, "a rank of the gear score table with 2 values");
    refused([](json& document) { document["materials"]["TestMetal5"] = {0, 0, 20}; }, "a material of format 3 with 3 values");
    refused(
        [](json& document) {
            for (json& recipe : document["recipes"]) {
                if (recipe["id"] == "TestKnifeT2Recipe") {
                    recipe["perkItems"] = 4;
                    recipe["attributeSlots"] = 1;
                }
            }
        },
        "more perk items than the 4 controls of the client");
    // A file of format 2 keeps the rules of stage 3.7b: a procedural recipe with no cause is not consistent.
    json old = json::parse(procedural_document());
    old["format"] = 2;
    for (auto& [item, values] : old["materials"].items()) {
        values = {values[0], values[1], values[2]};
    }
    gamedata::CraftingData format2;
    check(!format2.set(old.dump()) && format2.error().find("IsProcedural") != std::string::npos,
          "a file of format 2 with a procedural recipe with no cause is refused (" + format2.error() + ")");
    // Code review of stage 3.7b, finding CR7B-R3: a first-craft bit that the progression tables give an other value refuses the file.
    gamedata::CraftingData joined;
    check(joined.set(procedural_document())
              && joined.match_achievements([](std::string_view id) {
                     return id == "fc_testrefine" ? std::optional<std::uint32_t>(kFirstCraftBit) : std::nullopt;
                 })
              && !joined.match_achievements([](std::string_view id) {
                     return id == "fc_testrefine" ? std::optional<std::uint32_t>(kFirstCraftBit + 1) : std::nullopt;
                 })
              && !joined.loaded(),
          "the bits of the first-craft achievements against the progression tables: the same bit keeps the file, an other bit refuses it");
}

/** CRAFTC-MP: two sessions. */
void two_sessions(const Options&) {
    Procedural shop(true);
    shop.enter(true);
    for (const char* character : {kCharacterA, kCharacterB}) {
        shop.host.world.inventory().set_coin(character, 100000);
        shop.give_knife(character, 1);
        shop.azoth(character, 100);
    }
    world::StatusApply food;
    food.seconds = 30.0F;
    shop.host.world.statuses().apply(*shop.entity(kCharacterA), "FoodTestEngineer", food);
    shop.use_station(shop.clientA);
    const std::size_t fromB = shop.clientB.got.size();
    shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 1, knife_entries(), 25, 210, {}, 1);
    shop.finish();
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.item(kCharacterA, "TestSkinKnifeT2") != nullptr && shop.item(kCharacterA, "TestSkinKnifeT2")->gearScore == 210
              && shop.balance(kCharacterA) == 92 && shop.balance(kCharacterB) == 100 && shop.count(kCharacterB, "TestMetal2") == 7
              && b->craftGearBonus.empty() && bonus_maps(shop.clientB, fromB).empty() && shop.item(kCharacterB, "TestSkinKnifeT2") == nullptr,
          "A crafts a knife of 210 with its food and 8 Azoth; B keeps its Azoth, its materials, and an empty bonus map, and gets no state 205");
    // A request of B names its own character only: a request on the entity of A from the session of B changes nothing of A.
    shop.use_station(shop.clientB);
    shop.craft(shop.clientB, kCharacterB, "TestKnifeT2Recipe", 1, knife_entries(), 25, 205, {}, 1, crafting_target(shop.entity(kCharacterA)->id),
               shop.entity(kCharacterA)->id);
    shop.finish();
    check(shop.balance(kCharacterA) == 92 && shop.count(kCharacterA, "TestMetal2") == 0, "a facet of A in a request of B changes nothing of A");
}

/** CRAFTC-LOAD: the perk rolls visit only the buckets of the recipe. */
void load(const Options&) {
    const auto run = [](bool large, std::uint64_t& visits, std::uint64_t& rolls, long long& milliseconds) {
        Procedural shop(false);
        if (large) {
            gamedata::PerkTables tables = procedural_perks();
            for (int index = 0; index < 20000; ++index) {
                tables.buckets.push_back(bucket(("Test_Unrelated" + std::to_string(index)).c_str(), "Generated", 0.5F,
                                                {{"PerkID_TestCraft_A", false, 1.0F}}));
            }
            if (!shop.host.perks.set(tables)) {
                throw std::runtime_error("procedural load: the perk data");
            }
        }
        shop.enter(false);
        if (large) {
            for (int index = 0; index < 100; ++index) {
                const std::string id = "00000000-0000-4000-8000-0000000c" + std::to_string(1000 + index);
                const state::WorldPosition place{5000.0F + static_cast<float>(index), 5000.0F, 30.0F};
                (void)add_player(shop.host.store, std::to_string(76561202220000000ULL + static_cast<std::uint64_t>(index)), "Load", id,
                                 "Procedural" + std::to_string(index), place);
                (void)shop.host.world.spawn_character(id, place);
            }
        }
        shop.host.world.inventory().set_coin(kCharacterA, 1000000);
        shop.give_knife(kCharacterA, 200);
        shop.azoth(kCharacterA, 2500);
        shop.use_station(shop.clientA);
        const auto start = std::chrono::steady_clock::now();
        shop.craft(shop.clientA, kCharacterA, "TestKnifeT2Recipe", 200, knife_entries(), 5000, 205, {}, 1);
        for (int tick = 0; tick < 20; ++tick) {
            shop.finish(2);
        }
        milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        visits = shop.crafting().counts().bucketVisits;
        rolls = shop.crafting().counts().perkRolls;
    };
    std::uint64_t visitsSmall = 0, visitsLarge = 0, rollsSmall = 0, rollsLarge = 0;
    long long timeSmall = 0, timeLarge = 0;
    run(false, visitsSmall, rollsSmall, timeSmall);
    run(true, visitsLarge, rollsLarge, timeLarge);
    note("200 jobs with a perk roll: " + std::to_string(timeSmall) + " ms and " + std::to_string(timeLarge) + " ms; bucket visits " +
         std::to_string(visitsSmall) + " and " + std::to_string(visitsLarge));
    check(rollsSmall == 200 && rollsLarge == 200 && visitsSmall == 200 && visitsLarge == 200,
          "200 rolls: one bucket of the recipe for each roll, also with 20,000 more buckets and 100 more characters");
    check(timeLarge < 2000, "200 jobs with the large data in less than 2 s (" + std::to_string(timeLarge) + " ms)");
    // The roll at the largest depth (5 buckets, 4 resources, a nested bucket of 20,000 entries is not reached): 1,000 rolls, 2 s.
    Procedural shop(false);
    world::CraftPerkInputs inputs;
    for (const char* bucket : {"Test_AxeCharmBucket", "Test_PinsBucket", "Test_AttrCharmBucket", "Test_SwordCharmBucket"}) {
        inputs.resources.push_back(shop.host.perks.bucket(bucket));
    }
    inputs.guaranteed = 5;
    inputs.maxPerks = 5;
    std::uint64_t state = 5;
    const auto start = std::chrono::steady_clock::now();
    std::size_t four = 0;
    for (int roll = 0; roll < 1000; ++roll) {
        four += count_perks(world::roll_craft_perks(shop.host.perks, *shop.host.catalog.find("TestGreatAxeT5"), 600, inputs,
                                                    [&state] { return world::World::next_unit(state); })
                                .perks) == 4
                    ? 1
                    : 0;
    }
    const long long time = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    // The charm D, the pins (a gem slot), and the attribute charm give 3 perks; the sword charm gives none; G 5: Test_Craft3 (1.5) and the
    // gem bucket (1.6: no second gem) reach the target 5: 4 perks each time.
    check(four == 1000 && time < 2000, "1,000 rolls of 4 resources and 5 buckets: 4 perks each, " + std::to_string(time) + " ms");
}

/** CRAFTC-CMD: the commands. */
void commands(const Options&) {
    Procedural shop(false);
    shop.enter(false);
    server::http::Router router;
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_dev_routes(router, services);
    shop.host.tick();
    const auto send = [&](const char* method, const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        return server::http::handle_request(router, request);
    };
    shop.host.world.inventory().set_coin(kCharacterA, 100000);
    shop.give_axe(kCharacterA, 2);
    shop.give(kCharacterA, "TestAxeCharm", 2);
    shop.rank(kCharacterA, "Weaponsmithing", 200);
    shop.use_station(shop.clientA);
    const std::string character = std::string("character=") + kCharacterA;
    const service::HttpResponse set = send("POST", "/v1/dev/crafting/currency", character + "&amount=500");
    const service::HttpResponse above = send("POST", "/v1/dev/crafting/currency", character + "&amount=2501");
    const json plan = json::parse(
        send("POST", "/v1/dev/crafting/preview", character + "&recipe=TestAxeT5Recipe&quantity=2&seed=11&perks=TestAxeCharm&upgrade=2").body, nullptr,
        false);
    check(set.status == 200 && above.status == 400 && shop.balance(kCharacterA) == 500 && plan.is_object() && plan["accepted"] == true
              && plan["craftGearScore"]["value"] == 585 && plan["craftGearScore"]["rank"] == 25 && plan["craftGearScore"]["materials"] == 60
              && plan["perkPlan"]["azothPerJob"] == 16 && plan["perkPlan"]["maxPerks"] == 4 && plan["perkPlan"]["roll"] == true
              && plan["seeded"]["perks"].size() == 2 && plan["seeded"]["perks"][0]["perks"][0] == "PerkID_TestCraft_D"
              && plan["seeded"]["perks"][0]["afterSwap"].is_array(),
          "the balance command (2,501 above the cap: 400); the preview: 585 (500 + 25 + 60), 16 Azoth for G 2, 4 perks at 585, the "
          "seeded perks of 2 jobs before and after the swap");
}

} // namespace

std::vector<Case> procedural_cases() {
    return {{"CRAFTC1", "procedural (3.7c, I1): the craft gear score, its parts and caps, the claim of the request", gear_score},
            {"CRAFTC2", "procedural (3.7c, I2): the generator with the resources, G, the push flag, the target, one Inherent, one gem", generator},
            {"CRAFTC3", "procedural (3.7c, I3): the perk items and the Azoth at the station, the live bytes, the refusals", perk_items},
            {"CRAFTC4", "procedural (3.7c, I4): the craft bonus of the statuses (state 205 field 1): a food and its end, a perk, a trophy",
             status_bonus},
            {"CRAFTC5", "procedural (3.7c, I5): a currency ingredient (Azoth_Currency): the entry, the balance, the refusals", currency},
            {"CRAFTC6", "procedural (3.7c, I2): a T5 item with five perks and a gem slot, the swap, G 4, the restart", five_perks},
            {"CRAFTC-DATA", "procedural (3.7c, I7): the loader of format 3 and its rules", data_rules},
            {"CRAFTC-MP", "procedural (3.7c, I6, MP-10): the perk items, the Azoth, and the bonus map of A do not change B", two_sessions},
            {"CRAFTC-LOAD", "procedural (3.7c, I6): the perk rolls visit only the buckets of the recipe; 1,000 rolls; 2 s", load},
            {"CRAFTC-CMD", "procedural (3.7c): the preview with the craft gear score, the perk plan, the seeded perks; the balance command",
             commands}};
}

} // namespace soulwarden::backend::tests
