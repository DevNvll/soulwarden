#include <cstdio>
#include <string>

#include <nlohmann/json.hpp>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/quests.h"
#include "world/world.h"

// The commands of the quests (stage 1.8a, world/quests.h, docs/systems/quests.md): the common layer of the admin routes (the command
// apply_backstory, the route GET /admin/v1/catalog/backstories, the member "quests" of the state) and of the development routes
// (/v1/dev/quests...). The quest system does the change: a route has no logic of its own for a character.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** Longest id of a template, an objective, or an achievement in a command. */
constexpr std::size_t kIdLimit = 128;

int status_of(world::QuestError error) {
    switch (error) {
    case world::QuestError::unknownCharacter:
    case world::QuestError::unknownObjective:
    case world::QuestError::unknownTemplate:
    case world::QuestError::unknownInstance:
        return 404;
    case world::QuestError::storeFailed:
        return 500;
    default:
        return 409;
    }
}

std::string hex(std::uint64_t value) {
    char text[20];
    std::snprintf(text, sizeof(text), "%016llx", static_cast<unsigned long long>(value));
    return text;
}

std::optional<std::string> id_argument(const Arguments& arguments, const char* name) {
    const auto text = arguments.text(name);
    if (!text || text->empty() || text->size() > kIdLimit) {
        return std::nullopt;
    }
    return text;
}

} // namespace

WorldCommand apply_backstory(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto id = id_argument(arguments, "template");
    if (!id) {
        refusal = message(400, "give template=ID (a backstory template of the quest data: GET /admin/v1/catalog/backstories)");
        return {};
    }
    return [character, id = *id](world::World& target) {
        const world::TemplateResult result = target.quests().apply_template(character, id);
        if (!result.ok()) {
            return json_reply(status_of(result.error), json{{"message", world::quest_error_text(result.error)}}.dump());
        }
        return json_reply(200, json{{"message", "done"},
                                    {"applied", result.inWorld ? "world" : "stored"},
                                    {"template", id},
                                    {"achievements", result.achievements},
                                    {"achievementsWithNoBit", result.achievementsWithNoBit},
                                    {"objectives", result.objectives},
                                    {"objectivesWithNoRow", result.objectivesWithNoRow}}
                                   .dump());
    };
}

WorldCommand grant_objective(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto id = id_argument(arguments, "objective");
    if (!id) {
        refusal = message(400, "give objective=ID (an ObjectiveID of the quest data)");
        return {};
    }
    return [character, id = *id](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(409, "the character is not in the world");
        }
        log::write("quests: DEVELOPMENT COMMAND: grant of the objective %s to character %s", id.c_str(), character.c_str());
        const world::QuestResult result = target.quests().grant(*entity, id, "command", 0);
        if (!result.ok()) {
            return json_reply(status_of(result.error), json{{"message", world::quest_error_text(result.error)}}.dump());
        }
        return json_reply(200, json{{"message", "done"}, {"objective", id}, {"instance", hex(result.instance)}}.dump());
    };
}

WorldCommand quest_rewards(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto id = id_argument(arguments, "objective");
    const auto territory = arguments.integer("territory", 0, 65535);
    if (!id) {
        refusal = message(400, "give objective=ID (an ObjectiveID of the quest data) and maybe territory=N (the origin territory; default its "
                               "ExclusiveTerritory)");
        return {};
    }
    return [character, id = *id, territory](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(409, "the character is not in the world");
        }
        const gamedata::QuestData* data = target.quests().data();
        const gamedata::QuestObjective* objective = data != nullptr ? data->objective(id) : nullptr;
        if (objective == nullptr) {
            return message(404, "the quest data has no such objective");
        }
        const std::uint16_t origin = territory ? static_cast<std::uint16_t>(*territory) : objective->exclusiveTerritory.value_or(0);
        world::RewardJoin join;
        std::string detail;
        const world::QuestError error = target.quests().reward_join(*entity, *objective, origin, join, detail);
        json items = json::array();
        for (const world::RewardItem& item : join.items) {
            json entry{{"item", item.itemId}, {"quantity", item.quantity}, {"container", target.item_containers().openable(item.itemId)}};
            if (item.gearScoreRange) {
                entry["gearScoreRange"] = {item.gearScoreRange->first, item.gearScoreRange->second};
            }
            items.push_back(entry);
        }
        const world::QuestError availability = target.quests().available(*entity, *objective);
        return json_reply(200, json{{"objective", objective->id},
                                    {"payable", error == world::QuestError::none},
                                    {"refusal", error == world::QuestError::none ? "" : detail},
                                    {"experience", join.eventExperience + join.rowExperience},
                                    {"coin", join.coin},
                                    {"coinChance", join.coinChance},
                                    {"items", items},
                                    {"achievements", join.eventAchievements},
                                    {"completionAchievement", join.achievement},
                                    {"standing", join.standing},
                                    {"standingTerritory", join.standingTerritory},
                                    {"next", join.next != nullptr ? join.next->id : ""},
                                    {"repeat", join.schedule == gamedata::ScheduleKind::none ? "none"
                                               : join.schedule == gamedata::ScheduleKind::hourly ? "hourly"
                                               : join.schedule == gamedata::ScheduleKind::daily ? "daily" : "weekly"},
                                    {"available", world::quest_error_text(availability)}}
                                   .dump());
    };
}

WorldCommand set_achievement(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto id = id_argument(arguments, "id");
    const auto on = arguments.integer("on", 0, 1);
    if (!id || !on) {
        refusal = message(400, "give id=ACHIEVEMENT and on=1 (unlock) or on=0 (lock)");
        return {};
    }
    return [character, id = *id, on = *on == 1](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(409, "the character is not in the world");
        }
        const std::optional<std::uint32_t> bit = target.ownership().achievement_bit(id);
        if (!bit) {
            return message(404, "the game data has no bit for this achievement");
        }
        log::write("ownership: DEVELOPMENT COMMAND: %s of the achievement %s of character %s", on ? "unlock" : "lock", id.c_str(),
                   character.c_str());
        const bool ok = on ? target.ownership().unlock_achievement(*entity, id, *bit) : target.ownership().lock_achievement(*entity, id);
        return ok ? json_reply(200, json{{"message", "done"}, {"id", id}, {"bit", *bit}, {"on", on}}.dump())
                  : message(500, "the database did not take the change: nothing changed");
    };
}

namespace {

WorldCommand end_quest(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal, bool reset) {
    const auto text = arguments.text("instance");
    std::uint64_t instance = 0;
    if (!text || text->size() != 16 || std::sscanf(text->c_str(), "%16llx", reinterpret_cast<unsigned long long*>(&instance)) != 1 || instance == 0) {
        refusal = message(400, "give instance=HEX (16 hexadecimal digits: an active quest of the character, GET the quests of the character)");
        return {};
    }
    return [character, instance, reset](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(409, "the character is not in the world");
        }
        const world::QuestResult result = reset ? target.quests().reset(*entity, instance) : target.quests().abandon(*entity, instance);
        if (!result.ok()) {
            return json_reply(status_of(result.error), json{{"message", world::quest_error_text(result.error)}}.dump());
        }
        json reply = {{"message", "done"}, {"instance", hex(instance)}};
        if (reset) {
            reply["newInstance"] = hex(result.instance);
        }
        return json_reply(200, reply.dump());
    };
}

} // namespace

WorldCommand abandon_quest(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    return end_quest(character, arguments, refusal, false);
}

WorldCommand reset_quest(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    return end_quest(character, arguments, refusal, true);
}

std::optional<json> quests_view(world::World& target, const std::string& character) {
    const auto view = target.quests().view(character);
    if (!view) {
        return std::nullopt;
    }
    const gamedata::QuestData* data = target.quests().data();
    json quests = json::array();
    for (const state::QuestRow& quest : view->quests) {
        json tasks = json::array();
        for (const state::QuestTaskRow& task : quest.tasks) {
            json entry = {{"index", task.index}, {"task", task.task}, {"state", task.state}, {"progress", task.progress}};
            if (task.startNs != 0) {
                entry["startNs"] = task.startNs;
            }
            tasks.push_back(entry);
        }
        const gamedata::QuestObjective* objective = data != nullptr ? data->objective(quest.objective) : nullptr;
        quests.push_back({{"instance", hex(quest.instance)},
                          {"objective", quest.objective},
                          {"title", objective != nullptr ? objective->title : ""},
                          {"source", quest.source},
                          {"territory", quest.originTerritory},
                          {"pinned", std::find(view->tracked.begin(), view->tracked.end(), quest.instance) != view->tracked.end()},
                          {"tasks", tasks}});
    }
    json grants = json::array();
    for (const state::QuestGrantRow& grant : view->grants) {
        grants.push_back({{"source", grant.source}, {"key", grant.key}, {"objective", grant.objective}, {"state", grant.state}});
    }
    json awards = json::array();
    for (const state::QuestAwardRow& award : view->awards) {
        awards.push_back({{"objective", award.objective}, {"index", award.index}, {"amount", award.amount}});
    }
    const world::QuestCounts& counts = target.quests().counts();
    return json{{"character", character},
                {"inWorld", view->inWorld},
                {"data", data != nullptr},
                {"quests", quests},
                {"grants", grants},
                {"awards", awards},
                {"counts",
                 {{"grants", counts.grants},
                  {"plans", counts.plans},
                  {"steps", counts.steps},
                  {"events", counts.events},
                  {"waitsVisited", counts.waitsVisited},
                  {"timersStarted", counts.timersStarted},
                  {"timersDone", counts.timersDone},
                  {"trackedUpdates", counts.trackedUpdates},
                  {"storeFailures", counts.storeFailures},
                  {"worldEvents", counts.worldEvents},
                  {"subscribersVisited", counts.subscribersVisited},
                  {"progressChanges", counts.progressChanges},
                  {"repeatedResults", counts.repeatedResults},
                  {"drops", counts.drops},
                  {"dropMisses", counts.dropMisses},
                  {"abandons", counts.abandons},
                  {"resets", counts.resets},
                  {"failures", counts.failures},
                  {"timerRetries", counts.timerRetries},
                  {"itemLooks", counts.itemLooks},
                  {"placeLooks", counts.placeLooks},
                  {"accepts", counts.accepts},
                  {"handIns", counts.handIns},
                  {"handInRefusals", counts.handInRefusals},
                  {"cutsceneGrants", counts.cutsceneGrants},
                  {"cutsceneRefusals", counts.cutsceneRefusals},
                  {"cutsceneCompletions", counts.cutsceneCompletions},
                  {"whispers", counts.whispers},
                  {"gameEventsApplied", counts.gameEventsApplied},
                  {"triggerVisits", counts.triggerVisits},
                  {"topicEffects", counts.topicEffects}}}};
}

json backstories_json(world::World& target) {
    const gamedata::QuestData* data = target.quests().data();
    json templates = json::array();
    if (data == nullptr) {
        return json{{"data", false}, {"templates", templates}};
    }
    for (const gamedata::QuestBackstory& backstory : data->backstories()) {
        if (backstory.archetype) {
            continue; // An archetype row of the creation is not a template of decision D2.
        }
        json objectives = json::array();
        for (const std::string& id : backstory.objectives) {
            const gamedata::QuestObjective* objective = data->objective(id);
            objectives.push_back({{"id", id}, {"inData", objective != nullptr}, {"title", objective != nullptr ? objective->title : ""}});
        }
        templates.push_back({{"id", backstory.id},
                             {"name", backstory.name},
                             {"purpose", backstory.purpose},
                             {"playtest", backstory.playtest},
                             {"level", backstory.level},
                             {"faction", backstory.faction},
                             {"objectives", objectives},
                             {"achievements", backstory.achievements}});
    }
    return json{{"data", true}, {"templates", templates}};
}

} // namespace soulwarden::backend::server::http::commands
