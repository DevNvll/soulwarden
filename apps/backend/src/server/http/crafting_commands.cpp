#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/world.h"

// The commands of the crafting (stage 3.7a, world/crafting.h, docs/systems/crafting.md), in the common layer of the development routes and
// of the admin routes (character_commands.h): the crafting of a character, the plan of a recipe for it (a diagnostic with no change), and
// the stop of its craft batch. Stage 3.7b: the bonus items, the experience, and the cooldown in the plan (a seeded plan gives the extra
// units of each job of a batch), the cooldowns of the character, and two harness commands: the cooldown of a recipe and the seed of the
// generator of the world (code review of stage 3.7b, finding CR7B-R2: they need the harness gate of the process also through the admin
// routes). Stage 3.7c: the craft gear score, the perk options, and the perks of each job in the plan; the balance of a currency (Azoth).

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

json perks_json(const world::PerkIds& perks) {
    json list = json::array();
    for (const std::string& perk : perks) {
        list.push_back(perk);
    }
    return list;
}

std::vector<std::string> split_list(const std::string& text) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find(',', start);
        if (end == std::string::npos) {
            end = text.size();
        }
        if (end > start) {
            parts.push_back(text.substr(start, end - start));
        }
        start = end + 1;
    }
    return parts;
}

} // namespace

bool harness_gate() {
    const char* value = std::getenv("SOULWARDEN_HARNESS");
    return value != nullptr && std::strcmp(value, "1") == 0;
}

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
    const auto perkItems = arguments.text("perks");
    const auto upgrade = arguments.integer("upgrade", 0, world::crafting_rules::kMaxUpgrade);
    const auto materialList = arguments.text("materials");
    if (!recipe || recipe->empty() || (arguments.has("quantity") && !quantity) || (arguments.has("seed") && !seed)
        || (arguments.has("upgrade") && !upgrade)) {
        refusal = message(400, "give recipe (a RecipeID of the crafting data) and, if you want, quantity (1 to 10000), seed (a whole number: "
                               "the extra units and the perks of each job with the generator of the world at that seed), perks (resource "
                               "item ids with \",\" between them), upgrade (0 to 5), and materials (an item id for each ingredient with "
                               "\",\" between them; \"-\": the first member)");
        return {};
    }
    std::vector<std::string> items = perkItems ? split_list(*perkItems) : std::vector<std::string>{};
    if (items.size() > world::crafting_rules::kMaxPerkItems) {
        refusal = message(400, "give 4 perk items at most");
        return {};
    }
    std::vector<std::string> materials = materialList ? split_list(*materialList) : std::vector<std::string>{};
    return [character, recipe = *recipe, quantity = static_cast<std::uint32_t>(quantity.value_or(1)), seed, items = std::move(items),
            upgrade = static_cast<std::uint32_t>(upgrade.value_or(0)), materials = std::move(materials)](world::World& target) {
        const world::CraftPreview preview = target.crafting().preview(character, recipe, quantity, seed, items, upgrade, materials);
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
                               {"raw", experience.raw},
                               {"batch", experience.total(quantity)}};
        reply["cooldown"] = preview.cooldownLeft ? json{{"left", *preview.cooldownLeft}, {"secondsLeft", preview.cooldownSeconds}} : json(nullptr);
        // Stage 3.7c: the craft gear score with its parts, the perk plan, the Azoth and the currencies of one job.
        const world::CraftGearScore& score = preview.craftGearScore;
        reply["craftGearScore"] = {{"rule", score.rule},           {"value", score.value},     {"base", score.base},
                                   {"rank", score.rank},           {"materials", score.materials}, {"status", score.status},
                                   {"statusSum", score.statusSum}, {"override", score.override}, {"cap", score.cap}};
        const world::CraftPerkPlan& plan = preview.perks;
        json currencies = json::object();
        for (const auto& [currency, amount] : plan.currencies) {
            currencies[currency] = amount;
        }
        reply["perkPlan"] = {{"roll", plan.roll},
                             {"perkItems", plan.resources},
                             {"upgrade", plan.upgrade},
                             {"fixedPerks", plan.fixedPerks},
                             {"buckets", plan.buckets},
                             {"maxPerks", plan.maxPerks},
                             {"maximumPerks", plan.maximumPerks},
                             {"push", preview.recipe != nullptr && preview.recipe->push},
                             {"azothPerJob", plan.azothPerJob},
                             {"currenciesPerJob", currencies}};
        if (seed) {
            std::uint64_t extra = 0;
            for (const std::uint32_t units : preview.draws) {
                extra += units;
            }
            reply["seeded"] = {{"seed", *seed}, {"draws", preview.draws}, {"bonus", extra},
                               {"total", static_cast<std::uint64_t>(preview.output.quantity) * quantity + extra}};
            json jobs = json::array();
            for (std::size_t index = 0; index < preview.rolls.size(); ++index) {
                const world::CraftPerkRoll& roll = preview.rolls[index];
                json chances = json::array();
                for (const auto& [bucket, chance] : roll.chances) {
                    chances.push_back({{"bucket", bucket}, {"chance", chance}});
                }
                jobs.push_back({{"job", index + 1},
                                {"resourcePerks", roll.resourcePerks},
                                {"target", roll.target},
                                {"chances", chances},
                                {"perks", perks_json(roll.perks)},
                                {"afterSwap", perks_json(preview.swapped[index])}});
            }
            reply["seeded"]["perks"] = jobs;
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
    if (!harness_gate()) {
        refusal = message(403, "set_craft_cooldown is a harness command: the backend needs SOULWARDEN_HARNESS=1");
        return {};
    }
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
    if (!harness_gate()) {
        refusal = message(403, "set_craft_seed is a harness command: the backend needs SOULWARDEN_HARNESS=1");
        return {};
    }
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

WorldCommand set_currency(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto amount = arguments.integer("amount", 0, 1000000000ULL);
    const std::string currency = arguments.text("currency").value_or(world::kAzothCurrency);
    if (!amount) {
        refusal = message(400, "give amount (0 to the cap of the currency) and, if you want, currency (default Azoth_Currency)");
        return {};
    }
    return [character, currency, amount = *amount](world::World& target) {
        const gamedata::CraftingData* data = target.crafting().data();
        const gamedata::CraftCurrency* row = data != nullptr ? data->currency(currency) : nullptr;
        if (row == nullptr) {
            return message(400, "the crafting data has no such currency (no cap of its balance)");
        }
        if (amount > row->cap) {
            return message(400, "the amount is above the cap " + std::to_string(row->cap) + " of " + row->id);
        }
        const world::SkillResult result = target.skills().set_balance(character, row->id, amount, row->cap);
        if (!result.ok()) {
            return message(result.error == world::ProgressError::unknownCharacter ? 404 : result.error == world::ProgressError::storeFailed ? 500 : 400,
                           world::progress_error_text(result.error));
        }
        log::write("world: the balance of %s of character %s is %llu (a command)", row->id.c_str(), character.c_str(),
                   static_cast<unsigned long long>(amount));
        return json_reply(200, json{{"message", "the balance is set"}, {"currency", row->id}, {"amount", amount}, {"inWorld", result.inWorld}}.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands
