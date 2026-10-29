#include <cstdio>

#include <nlohmann/json.hpp>

#include "gamedata/mission_data.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/missions.h"
#include "world/world.h"

// The command of the mission boards (stage QG6a, world/missions.h, docs/systems/mission-boards.md): a reader of the board state of a
// character, its town offers of now, its missions, and the next seed windows in which a mission is an offer.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

std::string hex32(std::uint32_t value) {
    char text[9];
    std::snprintf(text, sizeof text, "%08x", value);
    return text;
}

std::string actor_hex(const std::array<std::uint8_t, 16>& actor) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : actor) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

std::string mission_name(const world::World& world, std::uint32_t mission) {
    const gamedata::MissionData* data = world.missions().data();
    const gamedata::MissionRow* row = data != nullptr ? data->mission(mission) : nullptr;
    return row != nullptr ? row->id : hex32(mission);
}

json offers_json(const world::World& world, const world::TownOffers& offers) {
    json list = json::array();
    for (const world::TownOffer& offer : offers.offers) {
        list.push_back({{"mission", mission_name(world, offer.mission)},
                        {"missionId", hex32(offer.mission)},
                        {"bucket", offer.bucket},
                        {"index", offer.index},
                        {"candidates", offer.candidates},
                        {"available", offer.available}});
    }
    return list;
}

/** Stage QG6b: the explicit offers of a faction board (field 2 of the provider state) in the order of the vector that 3816 names. */
json faction_offers_json(const world::World& world, const world::MissionBoardState& board) {
    json list = json::array();
    for (std::size_t index = 0; board.faction && index < board.offers.size(); ++index) {
        const world::MissionOffer& offer = board.offers[index];
        list.push_back({{"index", index},
                        {"mission", mission_name(world, offer.mission)},
                        {"missionId", hex32(offer.mission)},
                        {"slot", offer.slot},
                        {"goal", offer.goal},
                        {"available", offer.available},
                        {"instance", offer.instance}});
    }
    return list;
}

} // namespace

WorldCommand mission_board(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    // Code review of stage QG6a, finding QG6AR-R6: the board state and the search of the future offers are a diagnostic: the process gate
    // of the harness (AGENTS.md), also on the admin path.
    if (!harness_gate()) {
        refusal = message(403, "the mission board diagnostic is a harness command: the backend needs SOULWARDEN_HARNESS=1");
        return {};
    }
    const auto mission = arguments.text("mission");
    const auto windows = arguments.integer("windows", 1, 400);
    const auto territory = arguments.integer("territory", 1, 65535);
    if (mission && (mission->empty() || mission->size() > 200)) {
        refusal = message(400, "give mission=ID (a MissionID of the mission data), and maybe windows=N (1 to 400) and territory=N");
        return {};
    }
    return [character, mission, windows, territory](world::World& target) {
        const std::optional<world::MissionView> view = target.missions().view(character);
        world::Entity* entity = target.find_character(character);
        if (!view || !view->inWorld || entity == nullptr) {
            return message(409, "the character is not in the world");
        }
        const world::MissionBoardState& board = view->board;
        const std::uint16_t town = territory ? static_cast<std::uint16_t>(*territory) : board.territory != 0 ? board.territory : 6;
        json towns = json::array();
        for (const auto& [key, entry] : board.towns) {
            json unavailable = json::array();
            for (const std::uint32_t id : entry.unavailable) {
                unavailable.push_back(mission_name(target, id));
            }
            towns.push_back({{"territory", key}, {"seed", entry.seed}, {"unavailable", unavailable}, {"projects", entry.previousProjects.size()}});
        }
        json missions = json::array();
        for (const world::QuestInstance& quest : view->missions) {
            json tasks = json::array();
            for (const world::QuestTaskState& task : quest.tasks) {
                tasks.push_back({{"state", task.state}, {"progress", task.progress}});
            }
            char instance[17];
            std::snprintf(instance, sizeof instance, "%016llx", static_cast<unsigned long long>(quest.instance));
            missions.push_back({{"instance", instance},
                                {"mission", mission_name(target, quest.mission)},
                                {"template", quest.objective},
                                {"provider", quest.provider},
                                {"destination", actor_hex(quest.destination)},
                                {"origin", quest.originTerritory},
                                {"ready", target.quests().ready(quest)},
                                {"tasks", tasks}});
        }
        const world::TownOffers now = target.missions().offers_of(*entity, town);
        json document{{"character", character},
                      {"territory", town},
                      {"seed", now.seed},
                      {"secondsToNextSeed", view->secondsToNextSeed},
                      {"serverNs", target.clock().server_time_ns()},
                      {"offers", offers_json(target, now)},
                      {"board", {{"opened", board.opened},
                                 {"provider", board.providerKey},
                                 {"providerActor", actor_hex(board.provider)},
                                 {"territory", board.territory},
                                 {"expirationNs", board.expirationNs},
                                 {"faction", board.faction},
                                 {"providerFaction", board.providerFaction},
                                 {"factionOffers", faction_offers_json(target, board)}}},
                      {"dailyBonusesLeft", target.missions().daily_bonuses_left(*entity)},
                      {"towns", towns},
                      {"missions", missions},
                      {"counts", {{"opens", target.missions().counts().opens},
                                  {"accepts", target.missions().counts().accepts},
                                  {"acceptRefusals", target.missions().counts().acceptRefusals},
                                  {"handIns", target.missions().counts().handIns},
                                  {"handInRefusals", target.missions().counts().handInRefusals},
                                  {"returns", target.missions().counts().returns}}}};
        if (mission) {
            // The next seed windows from now on in which the mission is an offer of the character (the rule of the client; no change).
            const gamedata::MissionData* data = target.missions().data();
            const gamedata::MissionRow* row = data != nullptr ? data->mission(*mission) : nullptr;
            if (row == nullptr) {
                return message(404, "the mission data has no such mission");
            }
            json found = json::array();
            world::TownInputs inputs;
            inputs.seedText = entity->characterId;
            inputs.territory = town;
            inputs.projects = target.missions().town_projects(town);
            inputs.level = entity->progression.level; // Finding QG6AR-R1: the level of the screen (0x3CEC5C0 adds 1 to the raw level).
            std::uint64_t at = target.clock().server_time_ns();
            const std::uint64_t count = windows.value_or(100);
            for (std::uint64_t window = 0; window < count; ++window) {
                inputs.serverNs = at;
                const auto entry = entity->missionBoard.towns.find(town);
                const world::TownOffers offers =
                    target.missions().town_offers(inputs, entry != entity->missionBoard.towns.end() ? &entry->second : nullptr, entity);
                for (const world::TownOffer& offer : offers.offers) {
                    if (offer.mission == row->crc) {
                        found.push_back({{"window", window}, {"startNs", window == 0 ? target.clock().server_time_ns() : at},
                                         {"inSeconds", (at - target.clock().server_time_ns()) / 1'000'000'000ULL},
                                         {"seed", offers.seed}, {"available", offer.available}});
                    }
                }
                at = world::next_seed_ns(at, world::town_rule::kIntervalMinutes);
            }
            document["windows"] = {{"mission", row->id}, {"searched", count}, {"offered", found}};
        }
        return json_reply(200, document.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands
