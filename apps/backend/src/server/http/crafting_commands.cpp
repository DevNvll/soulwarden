#include <chrono>
#include <string>

#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/world.h"

// The commands of the crafting (stage 3.7a, world/crafting.h, docs/systems/crafting.md), in the common layer of the development routes and
// of the admin routes (character_commands.h): the crafting of a character, the plan of a recipe for it (a diagnostic with no change), and
// the stop of its craft batch.

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
                 {"slotVisits", counts.slotVisits}, {"feeClaimsDiffer", counts.feeClaimsDiffer}, {"activeBatches", crafting.batch_count()}}}};
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
                          {"seconds", std::chrono::duration<double>(target.clock().now() - job.accepted).count()}};
    }
    return reply;
}

WorldCommand craft_preview(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto recipe = arguments.text("recipe");
    const auto quantity = arguments.integer("quantity", 1, world::crafting_rules::kMaxQuantity);
    if (!recipe || recipe->empty() || (arguments.has("quantity") && !quantity)) {
        refusal = message(400, "give recipe (a RecipeID of the crafting data) and, if you want, quantity (1 to 10000)");
        return {};
    }
    return [character, recipe = *recipe, quantity = static_cast<std::uint32_t>(quantity.value_or(1))](world::World& target) {
        const world::CraftPreview preview = target.crafting().preview(character, recipe, quantity);
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

} // namespace soulwarden::backend::server::http::commands
