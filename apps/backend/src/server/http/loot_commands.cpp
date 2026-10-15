#include <map>
#include <set>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands for the rewards of a kill (world/rewards.h), in the common layer of the
// development routes and of the admin routes (character_commands.h).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** Largest number of rolls of one roll command. */
constexpr std::uint64_t kLargestRolls = 10000;

} // namespace

WorldCommand set_force_loot(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto text = arguments.text("force");
    std::optional<bool> force;
    if (text && (*text == "1" || *text == "true" || *text == "on")) {
        force = true;
    } else if (text && (*text == "0" || *text == "false" || *text == "off")) {
        force = false;
    }
    if (!force) {
        refusal = message(400, "give force: 1 (each kill of the character drops loot and coin) or 0");
        return {};
    }
    return [character, force = *force](world::World& target) {
        if (target.find_character(character) == nullptr && !target.store().find_character(character)) {
            return message(404, "no character with this id");
        }
        // A value of the world with the character id as its key: it stays when the session of the
        // character ends, until the backend stops.
        target.rewards().set_forced(character, force);
        log::write("world: command: the kills of character %s %s", character.c_str(),
                   force ? "drop loot and coin each time (the loot switch)" : "have the loot chances of the game data");
        json reply = health_json(target, character);
        reply["applied"] = target.find_character(character) != nullptr ? "world" : "stored";
        reply["message"] = force ? "each kill of the character drops loot and coin" : "the kills have the chances of the game data";
        return json_reply(200, reply.dump());
    };
}

WorldCommand roll_loot(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto kind = arguments.text("kind");
    const auto level = arguments.has("level") ? arguments.integer("level", 1, 1000) : std::optional<std::uint64_t>(0);
    const auto count = arguments.has("count") ? arguments.integer("count", 1, kLargestRolls) : std::optional<std::uint64_t>(1);
    const auto forced = arguments.text("forced");
    if (!kind || kind->empty() || !level || !count || (forced && *forced != "1" && *forced != "0")) {
        refusal = message(400, "give kind=KIND (a kind of the creature data), and when necessary level (1 to 1000), count (1 to "
                               "10000 rolls), forced (1: the outer chance passes), character=ID (its level)");
        return {};
    }
    return [character, kind = *kind, level = static_cast<std::uint32_t>(*level), count = *count,
            forced = forced && *forced == "1"](world::World& target) {
        const gamedata::CreatureData* data = target.creatures().data();
        const gamedata::CreatureKind* found = data != nullptr && data->loaded() ? data->find_kind(kind) : nullptr;
        if (found == nullptr) {
            return message(404, "the creature data has no kind with this id");
        }
        const gamedata::LootTables* tables = target.rewards().tables();
        if (tables == nullptr) {
            return message(409, "the backend has no loot tables: make the file with tools/gamedata/loot.sh");
        }
        const gamedata::LootVitals* vitals = tables->vitals_of(found->vitals);
        if (vitals == nullptr) {
            return message(404, "the loot tables have no vitals row of this kind");
        }
        const world::Entity* reference = character.empty() ? nullptr : target.find_character(character);
        world::LootContext context;
        context.creatureLevel = level != 0 ? level : found->level;
        context.characterLevel = reference != nullptr ? reference->progression.level : context.creatureLevel;
        context.tags = vitals->tags;
        std::map<std::string, std::pair<std::uint64_t, std::uint64_t>> items;
        std::uint64_t withLoot = 0;
        for (std::uint64_t roll = 0; roll < count; ++roll) {
            const std::vector<world::LootDrop> drops = target.rewards().roll(*vitals, context, forced);
            withLoot += drops.empty() ? 0 : 1;
            // "rolls" counts each roll that gave the item one time, also when the roll gave it more
            // than one time (code review of stage 3.5a, finding F10); "quantity" counts each drop.
            std::set<std::string> counted;
            for (const world::LootDrop& drop : drops) {
                auto& entry = items[drop.itemId];
                entry.first += counted.insert(drop.itemId).second ? 1 : 0;
                entry.second += drop.quantity;
            }
        }
        json list = json::array();
        for (const auto& [item, value] : items) {
            list.push_back({{"item", item}, {"rolls", value.first}, {"quantity", value.second}});
        }
        return json_reply(200,
                          json{{"kind", found->id},
                               {"vitals", vitals->id},
                               {"table", vitals->table ? tables->table(*vitals->table).id : std::string()},
                               {"tags", vitals->tags},
                               {"canDropLoot", vitals->canDropLoot},
                               {"dropChance", vitals->dropChance},
                               {"creatureLevel", context.creatureLevel},
                               {"characterLevel", context.characterLevel},
                               {"rolls", count},
                               {"forced", forced},
                               {"rollsWithLoot", withLoot},
                               {"items", list}}
                              .dump());
    };
}

} // namespace soulwarden::backend::server::http::commands
