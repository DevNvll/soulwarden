#include <cstdio>
#include <string>

#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the loot bags (stage 3.5b.2, world/loot_bags.h, docs/systems/loot-bags.md), in the common layer of the development
// routes and of the admin routes (character_commands.h): the bags of a character, with the items of its own part only.

namespace soulwarden::backend::server::http::commands {

using nlohmann::json;

json loot_bags_view(world::World& target, const std::string& character) {
    const world::LootBagsView view = target.loot_bags().view(character);
    const world::LootBagCounts& counts = target.loot_bags().counts();
    json refusals = json::object();
    for (std::size_t index = 0; index < world::kBagRefusalCount; ++index) {
        if (counts.refusals[index] != 0) {
            refusals[world::bag_refusal_text(static_cast<world::BagRefusal>(index))] = counts.refusals[index];
        }
    }
    json reply{{"inWorld", view.inWorld},
               {"lootContainer", view.data},
               {"open", view.open.empty() ? json(nullptr) : json(view.open)},
               {"bags", json::array()},
               {"counts",
                {{"bags", counts.bags}, {"parts", counts.parts}, {"discards", counts.discards}, {"takes", counts.takes},
                 {"itemsTaken", counts.itemsTaken}, {"itemsGiven", counts.itemsGiven}, {"expired", counts.expired},
                 {"emptied", counts.emptied}, {"loaded", counts.loaded}, {"storeFailures", counts.storeFailures},
                 {"refusals", refusals}, {"bagsInWorld", target.loot_bags().bag_count()}, {"dueEnds", target.loot_bags().due_count()}}}};
    if (const gamedata::LootContainer* container = target.loot_bags().container()) {
        reply["lifetime"] = container->lifetime;
        reply["emptyLifetime"] = container->emptyLifetime;
    }
    for (const world::BagView& bag : view.bags) {
        json items = json::array();
        for (const world::ItemInstance& item : bag.items) {
            json perks = json::array();
            for (const std::string& perk : item.perks) {
                perks.push_back(perk);
            }
            items.push_back({{"item", item.itemId}, {"quantity", item.quantity}, {"instance", world::instance_text(item.instance)},
                             {"gearScore", item.gearScore}, {"durability", item.durability}, {"bound", (item.flags & world::kItemBound) != 0},
                             {"perks", perks}});
        }
        char entity[17];
        std::snprintf(entity, sizeof(entity), "%016llx", static_cast<unsigned long long>(bag.entity));
        reply["bags"].push_back({{"bag", bag.key},
                                 {"kind", bag.kind == world::loot_bag_rules::kDiscarded ? "discarded" : "kill"},
                                 {"source", bag.source},
                                 {"entity", entity},
                                 {"position", {bag.position.x, bag.position.y, bag.position.z}},
                                 {"distance", bag.distance},
                                 {"secondsLeft", bag.secondsLeft},
                                 {"emptied", bag.emptied},
                                 {"receivers", bag.receivers},
                                 {"items", items}});
    }
    return reply;
}

} // namespace soulwarden::backend::server::http::commands
