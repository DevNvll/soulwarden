#include <cstdio>
#include <string>

#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the chests (stage 3.5b.1, world/chests.h, docs/systems/chests.md), in the common layer of the development routes and
// of the admin routes (character_commands.h): the claims of a character and the chests near it, the removal of a claim, and a
// development aid: the reset of a claim after a short time (live check L8; the development routes only).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

std::string hex_bytes(const std::array<std::uint8_t, 16>& bytes) {
    std::string text;
    for (const std::uint8_t byte : bytes) {
        char digits[3];
        std::snprintf(digits, sizeof(digits), "%02x", byte);
        text += digits;
    }
    return text;
}

json items_json(const std::vector<world::GrantItem>& items) {
    json list = json::array();
    for (const world::GrantItem& item : items) {
        json perks = json::array();
        for (const std::string& perk : item.perks) {
            perks.push_back(perk);
        }
        list.push_back({{"item", item.itemId}, {"quantity", item.quantity},
                        {"gearScore", item.gearScore ? json(*item.gearScore) : json(nullptr)}, {"perks", perks}});
    }
    return list;
}

service::HttpResponse claim_reply(world::ClaimError error, const char* done) {
    switch (error) {
    case world::ClaimError::none:
        return json_reply(200, json{{"applied", "world"}, {"message", done}}.dump());
    case world::ClaimError::notInWorld:
    case world::ClaimError::noClaim:
    case world::ClaimError::unknownChest:
        return message(404, world::claim_error_text(error));
    case world::ClaimError::badTime:
        return message(400, world::claim_error_text(error));
    case world::ClaimError::storeFailed:
        return message(500, world::claim_error_text(error));
    }
    return message(500, "?");
}

} // namespace

json chests_view(world::World& target, const std::string& character, float radius) {
    const world::ChestsView view = target.chests().view(character, radius);
    const gamedata::ChestData* data = target.chests().data();
    const world::ChestCounts& counts = target.chests().counts();
    json refusals = json::object();
    for (std::size_t index = 0; index < world::kChestRefusalCount; ++index) {
        if (counts.refusals[index] != 0) {
            refusals[world::chest_refusal_text(static_cast<world::ChestRefusal>(index))] = counts.refusals[index];
        }
    }
    json reply{{"inWorld", view.inWorld},
               {"data", data != nullptr},
               {"radius", radius},
               {"open", nullptr},
               {"claims", json::array()},
               {"near", json::array()},
               {"trackerEntries", view.trackerEntries},
               {"counts",
                {{"begins", counts.begins}, {"completions", counts.completions}, {"cancels", counts.cancels}, {"refusals", refusals},
                 {"rolls", counts.rolls}, {"kept", counts.kept}, {"itemsGiven", counts.itemsGiven}, {"questDrops", counts.questDrops}, {"resets", counts.resets},
                 {"storeFailures", counts.storeFailures}, {"claimsInMemory", target.chests().claim_count()},
                 {"dueResets", target.chests().due_count()}, {"opens", target.chests().open_count()}}}};
    if (data != nullptr) {
        reply["chests"] = data->chests().size();
        reply["ordinary"] = data->ordinary();
        reply["resetMinutes"] = data->rules().resetMinutes;
    }
    if (view.open && data != nullptr) {
        const gamedata::ChestDefinition& chest = data->chests()[view.open->chest];
        reply["open"] = {{"chest", chest.key},
                         {"seconds", std::chrono::duration<double>(target.clock().now() - view.open->started).count()},
                         {"secondsLeft", std::max(0.0, std::chrono::duration<double>(view.open->due - target.clock().now()).count())}};
    }
    for (const world::ClaimView& claim : view.claims) {
        json entry{{"chest", claim.key},
                   {"state", world::claim_state_text(claim.state)},
                   {"generation", claim.generation},
                   {"resetSeconds", claim.resetSeconds},
                   {"lootedAt", claim.lootedAt},
                   {"resetAt", claim.resetAt},
                   {"items", items_json(claim.items)}};
        if (claim.chest != nullptr) {
            entry["kind"] = gamedata::chest_kind_text(claim.chest->kind);
            entry["variant"] = claim.chest->variant;
            entry["position"] = {claim.chest->position.x, claim.chest->position.y, claim.chest->position.z};
        }
        reply["claims"].push_back(std::move(entry));
    }
    for (const world::ChestNearView& entry : view.nearby) {
        const gamedata::ChestDefinition& chest = *entry.chest;
        const gamedata::ChestGatherable* gatherable = data != nullptr ? data->gatherable_of(chest) : nullptr;
        reply["near"].push_back({{"chest", chest.key},
                                 {"actor", hex_bytes(chest.actor)},
                                 {"kind", gamedata::chest_kind_text(chest.kind)},
                                 {"variant", chest.variant},
                                 {"name", chest.name},
                                 {"root", chest.root},
                                 {"gatherable", gatherable != nullptr ? gatherable->id : std::string()},
                                 {"openSeconds", gatherable != nullptr ? gatherable->openSeconds : 0.0F},
                                 {"tags", chest.tags},
                                 {"contentLevel", chest.contentLevel ? json(*chest.contentLevel) : json(nullptr)},
                                 {"available", chest.available},
                                 {"reason", chest.reason},
                                 {"position", {chest.position.x, chest.position.y, chest.position.z}},
                                 {"distance", entry.distance},
                                 {"claim", entry.claim ? json(world::claim_state_text(*entry.claim)) : json(nullptr)}});
    }
    return reply;
}

WorldCommand clear_chest_claim(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto chest = arguments.text("chest");
    if (!chest || chest->empty()) {
        refusal = message(400, "give chest: the placement key of a chest (a UUID)");
        return {};
    }
    return [character, chest = *chest](world::World& target) {
        if (!target.store().find_character(character)) {
            return message(404, "no character with this id");
        }
        return claim_reply(target.chests().clear(character, chest), "the claim of the character on the chest is removed: it can open the chest");
    };
}

WorldCommand set_chest_reset(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto chest = arguments.text("chest");
    const auto seconds = arguments.integer("seconds", 1, 7 * 24 * 3600);
    if (!chest || chest->empty() || !seconds) {
        refusal = message(400, "give chest (the placement key of a chest) and seconds (1 to 604800)");
        return {};
    }
    return [character, chest = *chest, seconds = std::chrono::seconds(*seconds)](world::World& target) {
        return claim_reply(target.chests().set_reset(character, chest, seconds),
                           "DEVELOPMENT COMMAND: the looted claim resets after the given time (not the rule of the data)");
    };
}

} // namespace soulwarden::backend::server::http::commands
