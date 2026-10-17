#include <algorithm>
#include <chrono>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/world.h"

// The commands of the crafting (stage 3.7a, world/crafting.h, docs/systems/crafting.md), in the common layer of the development routes and
// of the admin routes (character_commands.h): the crafting of a character, the plan of a recipe for it (a diagnostic with no change), and
// the stop of its craft batch. Stage 3.7b: the bonus items, the experience, and the cooldown in the plan (a seeded plan gives the extra
// units of each job of a batch), the cooldowns of the character, and two harness commands: the cooldown of a recipe and the seed of the
// generator of the world.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

json station_json(const world::StationContext& station, const gamedata::CraftingStation* row) {
    json reply{{"assemblyKey", station.assemblyKey},
               {"row", row != nullptr ? json(row->name) : json(nullptr)},
               {"types", row != nullptr ? json(row->types) : json::array()},
               {"camp", station.isCamp},
               {"chargeFee", station.chargeCraftingFee},
               {"object", station.objectKey},
               {"generation", station.objectGeneration}};
    return reply;
}

} // namespace

json crafting_view(world::World& target, const std::string& character) {
    world::Crafting& crafting = target.crafting();
    const world::CraftingView view = crafting.view(character);
    const world::CraftingCounts& counts = crafting.counts();
    json verdicts = json::object();
    for (std::size_t index = 0; index < world::kCraftVerdictCount; ++index) {
        if (counts.verdicts[index] != 0) {
            verdicts[world::craft_verdict_text(static_cast<world::CraftVerdict>(index))] = counts.verdicts[index];
        }
    }
    json ends = json::object();
    for (std::size_t index = 0; index < counts.ends.size(); ++index) {
        if (counts.ends[index] != 0) {
            ends[world::craft_end_text(static_cast<world::CraftEnd>(index))] = counts.ends[index];
        }
    }
    json reply{{"inWorld", view.inWorld},
               {"data", crafting.data() != nullptr},
               {"station", nullptr},
               {"batch", nullptr},
               {"counts",
                {{"requests", counts.requests}, {"verdicts", verdicts}, {"batches", counts.batches}, {"jobs", counts.jobs},
                 {"ends", ends}, {"itemsCreated", counts.itemsCreated}, {"feesPaid", counts.feesPaid},
                 {"clientStops", counts.clientStops}, {"idleStops", counts.idleStops}, {"jobVisits", counts.jobVisits},
                 {"slotVisits", counts.slotVisits}, {"feeClaimsDiffer", counts.feeClaimsDiffer}, {"activeBatches", crafting.batch_count()},
                 {"bonusUnits", counts.bonusUnits}, {"experience", counts.experience}, {"firstCrafts", counts.firstCrafts},
                 {"cooldownWrites", counts.cooldownWrites}, {"cooldownLoads", counts.cooldownLoads}}},
               {"cooldowns", json::array()}};
    if (const world::Entity* entity = target.find_character(character)) {
        for (const auto& [crc, cooldown] : entity->craftCooldowns) {
            const double left = std::chrono::duration<double>(cooldown.end - target.clock().now()).count();
            const gamedata::Recipe* recipe = crafting.data() != nullptr ? crafting.data()->recipe(crc) : nullptr;
            reply["cooldowns"].push_back({{"recipe", cooldown.recipe}, {"used", cooldown.used},
                                          {"quantity", recipe != nullptr ? recipe->cooldownQuantity : 0U},
                                          {"secondsLeft", std::max(0.0, left)}, {"active", left > 0.0}});
        }
    }
    if (const gamedata::CraftingData* data = crafting.data()) {
        reply["recipes"] = data->recipes().size();
        reply["supportedRecipes"] = data->supported_count();
        reply["stationRows"] = data->stations().size();
    }
    if (view.station) {
        reply["station"] = station_json(*view.station, view.stationRow);
    }
    if (view.job) {
        const world::CraftJob& job = *view.job;
        reply["batch"] = {{"recipe", job.recipe},         {"done", job.done},
                          {"total", job.total},           {"station", job.stationKey},
                          {"camp", job.camp},             {"output", job.output},
                          {"outputQty", job.outputQty},   {"feePaid", job.feePaid},
                          {"extraUnits", job.extraUnits}, {"bonusGiven", job.bonusGiven},
                          {"experience", job.experience}, {"experienceGiven", job.experienceGiven},
                          {"firstCraft", job.firstCraft}, {"craftAll", job.craftAll},
                          {"seconds", std::chrono::duration<double>(target.clock().now() - job.accepted).count()}};
    }
    return reply;
}

WorldCommand craft_preview(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto recipe = arguments.text("recipe");
    const auto quantity = arguments.integer("quantity", 1, world::crafting_rules::kMaxQuantity);
    const auto seed = arguments.integer("seed", 0, 0x7FFFFFFFFFFFFFFFULL);
    if (!recipe || recipe->empty() || (arguments.has("quantity") && !quantity) || (arguments.has("seed") && !seed)) {
        refusal = message(400, "give recipe (a RecipeID of the crafting data) and, if you want, quantity (1 to 10000) and seed (a whole number: "
                               "the extra units of each job with the generator of the world at that seed)");
        return {};
    }
    return [character, recipe = *recipe, quantity = static_cast<std::uint32_t>(quantity.value_or(1)), seed](world::World& target) {
        const world::CraftPreview preview = target.crafting().preview(character, recipe, quantity, seed);
        if (preview.verdict == world::CraftVerdict::notInWorld) {
            return message(404, "the character is not in the world");
        }
        json costs = json::array();
        for (const world::CraftCost& cost : preview.costs) {
            costs.push_back({{"item", cost.itemId}, {"quantity", cost.quantity}});
        }
        json reply{{"recipe", preview.recipe != nullptr ? json(preview.recipe->id) : json(recipe)},
                   {"quantity", quantity},
                   {"verdict", world::craft_verdict_text(preview.verdict)},
                   {"accepted", preview.verdict == world::CraftVerdict::accepted},
                   {"detail", preview.detail},
                   {"costs", costs},
                   {"output", {{"item", preview.output.itemId}, {"quantity", preview.output.quantity}}},
                   {"fee", preview.fee},
                   {"gearScore", preview.gearScore},
                   {"slots", preview.slots},
                   {"station", preview.station ? station_json(*preview.station, preview.stationRow) : json(nullptr)}};
        if (preview.recipe != nullptr) {
            reply["causes"] = preview.recipe->unsupported;
            reply["stations"] = preview.recipe->stations;
            reply["refining"] = preview.recipe->refining;
            reply["craftAll"] = preview.recipe->craftAll;
        }
        // Stage 3.7b: the bonus items (the chance that the client shows, the expected extra units of one job: decision D2), the experience,
        // and the cooldown.
        const world::CraftYield& yield = preview.yield;
        reply["bonus"] = {{"chance", yield.chance},
                          {"percent", yield.chance * 100.0},
                          {"materialChance", yield.materialChance},
                          {"materialPoints", yield.materialPoints},
                          {"rankPoints", yield.rankPoints},
                          {"effectPoints", yield.effectPoints},
                          {"extraUnits", yield.extraUnits},
                          {"maxExtra", yield.maxExtra}};
        const world::CraftExperience& experience = preview.experience;
        reply["experience"] = {{"progression", experience.progression},
                               {"perJob", experience.perJob},
                               {"firstCraft", experience.firstCraft},
                               {"firstCraftExtra", experience.firstCraftExtra},
                               {"batch", experience.perJob * quantity + experience.firstCraftExtra}};
        reply["cooldown"] = preview.cooldownLeft ? json{{"left", *preview.cooldownLeft}, {"secondsLeft", preview.cooldownSeconds}} : json(nullptr);
        if (seed) {
            std::uint64_t extra = 0;
            for (const std::uint32_t units : preview.draws) {
                extra += units;
            }
            reply["seeded"] = {{"seed", *seed}, {"draws", preview.draws}, {"bonus", extra},
                               {"total", static_cast<std::uint64_t>(preview.output.quantity) * quantity + extra}};
        }
        return json_reply(200, reply.dump());
    };
}

WorldCommand stop_craft(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    (void)arguments;
    (void)refusal;
    return [character](world::World& target) {
        if (!target.crafting().stop(character)) {
            return message(404, "the character has no craft batch");
        }
        return json_reply(200, json{{"message", "the craft batch stopped (the completed jobs stay)"}}.dump());
    };
}

WorldCommand set_craft_cooldown(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto recipe = arguments.text("recipe");
    const auto used = arguments.integer("used", 0, 255);
    const auto seconds = arguments.number("seconds", 0.0F, 1.0e8F);
    if (!recipe || recipe->empty() || !used || !seconds) {
        refusal = message(400, "give recipe (a RecipeID with a cooldown), used (0 to its CooldownQuantity), and seconds (0: no window)");
        return {};
    }
    return [character, recipe = *recipe, used = static_cast<std::uint32_t>(*used), seconds = *seconds](world::World& target) {
        if (!target.store().find_character(character)) {
            return message(404, "the store has no such character");
        }
        const std::string error = target.crafting().set_cooldown(character, recipe, used, seconds);
        if (!error.empty()) {
            return message(error == "the database did not take the row" ? 500 : 400, error);
        }
        return json_reply(200, json{{"message", "the cooldown of the recipe is set"}, {"used", used}, {"seconds", seconds}}.dump());
    };
}

WorldCommand set_craft_seed(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto seed = arguments.integer("seed", 0, 0x7FFFFFFFFFFFFFFFULL);
    if (!seed) {
        refusal = message(400, "give seed (a whole number: the next state of the generator of the world)");
        return {};
    }
    return [character, seed = *seed](world::World& target) {
        // The generator of the world is one value of the world (MP-1): the character names who gives the command (MP-9).
        target.set_random_seed(seed);
        log::write("world: the generator of the world has the seed %llu (a command of character %s)", static_cast<unsigned long long>(seed),
                   character.c_str());
        return json_reply(200, json{{"message", "the generator of the world has the seed"}, {"seed", seed}}.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands
