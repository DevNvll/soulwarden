#include <cstdio>
#include <string>

#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the gathering (stage 3.6a, world/gathering.h, docs/systems/gathering.md), in the common layer of the development routes
// and of the admin routes (character_commands.h): the work of a character, its speed and luck for each trade skill of gathering, the nodes
// with a state of the server near it, and a development aid: the respawn of a depleted node after a short time (the development routes
// only).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

json parts_json(const world::GatherParts& parts) {
    return {{"toolSpeed", parts.toolSpeed},     {"rankEfficiency", parts.rankEfficiency}, {"effects", parts.effects},
            {"speed", parts.speed},             {"rollBonus", parts.rollBonus},           {"rollEffects", parts.rollEffects},
            {"multiplier", parts.multiplier},   {"instant", parts.instant},               {"tool", parts.toolItem},
            {"toolGearScore", parts.toolGearScore}};
}

} // namespace

json gathering_view(world::World& target, const std::string& character, float radius) {
    world::Gathering& gathering = target.gathering();
    const world::GatheringView view = gathering.view(character, radius);
    const world::GatheringCounts& counts = gathering.counts();
    json refusals = json::object();
    for (std::size_t index = 0; index < world::kGatherRefusalCount; ++index) {
        if (counts.refusals[index] != 0) {
            refusals[world::gather_refusal_text(static_cast<world::GatherRefusal>(index))] = counts.refusals[index];
        }
    }
    json reply{{"inWorld", view.inWorld},
               {"data", gathering.data() != nullptr},
               {"radius", radius},
               {"work", nullptr},
               {"nodes", json::array()},
               {"skills", json::object()},
               {"counts",
                {{"begins", counts.begins}, {"completions", counts.completions}, {"cancels", counts.cancels}, {"refusals", refusals},
                 {"contested", counts.contested}, {"fullBags", counts.fullBags}, {"storeFailures", counts.storeFailures},
                 {"depletions", counts.depletions}, {"respawns", counts.respawns}, {"itemsGiven", counts.itemsGiven},
                 {"experience", counts.experience}, {"rolls", counts.rolls}, {"resolved", counts.resolved},
                 {"storedNodes", counts.storedNodes}, {"staleRows", counts.staleRows}, {"nodesInMemory", gathering.node_count()},
                 {"depleted", gathering.depleted_count()}, {"dueRespawns", gathering.due_count()}, {"works", gathering.work_count()}}}};
    if (const gamedata::GatheringData* data = gathering.data()) {
        reply["rows"] = data->rows().size();
        reply["naturalRecords"] = data->natural_count();
        reply["capitalNodes"] = data->capital_count();
        reply["regionsLoaded"] = data->regions_loaded();
    }
    if (view.work) {
        const world::GatherWork& work = *view.work;
        const gamedata::GatheringData* data = gathering.data();
        reply["work"] = {{"node", work.node},
                         {"kind", world::node_kind_text(work.kind)},
                         {"row", data != nullptr ? data->rows()[work.row].id : std::string()},
                         {"amount", work.amount},
                         {"seconds", work.seconds},
                         {"elapsed", std::chrono::duration<double>(target.clock().now() - work.started).count()},
                         {"parts", parts_json(work.parts)}};
    }
    for (const world::NodeView& node : view.nodes) {
        reply["nodes"].push_back({{"node", node.key},
                                  {"kind", world::node_kind_text(node.kind)},
                                  {"row", node.row},
                                  {"depleted", node.depleted},
                                  {"respawnSeconds", node.respawnSeconds},
                                  {"distance", node.distance},
                                  {"generation", node.generation}});
    }
    for (const auto& [skill, parts] : view.skills) {
        reply["skills"][skill] = parts_json(parts);
    }
    return reply;
}

WorldCommand set_node_respawn(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    (void)character;
    const auto node = arguments.text("node");
    const auto seconds = arguments.integer("seconds", 1, 7 * 24 * 3600);
    if (!node || node->empty() || !seconds) {
        refusal = message(400, "give node (a placement UUID, a key REGION#RECORD, or a G in 16 hexadecimal digits) and seconds (1 to 604800)");
        return {};
    }
    return [node = *node, seconds = std::chrono::seconds(*seconds)](world::World& target) {
        const world::NodeError error = target.gathering().set_respawn(node, seconds);
        switch (error) {
        case world::NodeError::none:
            return json_reply(200, json{{"applied", "world"},
                                        {"message", "DEVELOPMENT COMMAND: the depleted node respawns after the given time (not the rule of the data)"}}
                                       .dump());
        case world::NodeError::noData:
        case world::NodeError::unknownNode:
        case world::NodeError::notDepleted:
            return message(404, world::node_error_text(error));
        case world::NodeError::badTime:
            return message(400, world::node_error_text(error));
        case world::NodeError::storeFailed:
            return message(500, world::node_error_text(error));
        }
        return message(500, "?");
    };
}

} // namespace soulwarden::backend::server::http::commands
