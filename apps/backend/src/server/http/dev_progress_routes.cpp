#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "server/http/dev_support.h"
#include "server/http/routes.h"

// Development commands for the level, the attributes, and the skills of a character
// (world/progress.h, world/skills.h). They are management routes of the backend: a request
// with a service host gets 403. Each command names a character by its id (roadmap, rule MP-9)
// and runs as a task of the world thread. A command for a character that is not in the world
// changes its rows, and the reply has "applied":"stored". The commands are the commands of
// the common layer (character_commands.h), which the admin routes use too.
//
//   GET  /v1/dev/progress?character=ID                    the level, the attributes, the weapon
//                                                         masteries, and the trade skills
//   POST /v1/dev/progress/level?character=ID&level=N      set the level (a level of the screen)
//   POST /v1/dev/progress/experience?character=ID&amount=N
//                                                         give experience: the level goes up at
//                                                         the limit of a level
//   POST /v1/dev/progress/attribute?character=ID&attribute=NAME&value=N
//                                                         set the value of one attribute (NAME:
//                                                         constitution, focus, strength,
//                                                         dexterity, intelligence, or 0 to 4)
//   POST /v1/dev/progress/mastery?character=ID&weapon=NAME&level=N[&experience=N]
//                                                         set the mastery level of a weapon (1 to
//                                                         20) and the experience in that level.
//                                                         NAME: the id of the game table
//                                                         ("SwordAbilityTable"), or its first
//                                                         part ("sword")
//   POST /v1/dev/progress/tradeskill?character=ID&skill=NAME&level=N[&experience=N]
//                                                         set the level of a trade skill
//                                                         ("Mining") and the experience in it
//   POST /v1/dev/progress/skill-experience?character=ID&id=NAME&amount=N
//                                                         give experience to a weapon mastery or
//                                                         to a trade skill
//   POST /v1/dev/progress/node?character=ID&weapon=NAME&node=ID
//                                                         spend one mastery point on a node of a
//                                                         weapon tree (ID: the id text of the node)
//   POST /v1/dev/progress/bar?character=ID&weapon=NAME&slot=N&ability=ID
//                                                         put an active ability that the character
//                                                         has on a place (0 to 2) of the bar of a
//                                                         weapon (ID "none": an empty place)
//   GET  /v1/dev/presets                                  the build presets (stage B0) with the check of the start
//   POST /v1/dev/presets/apply?character=ID&preset=NAME[&variant=NAME]
//                                                         the whole build of a preset (the command apply_preset of the
//                                                         admin interface)
//   GET  /v1/dev/quests?character=ID                     the quests of a character (stage 1.8a): each quest with its tasks, its pin,
//                                                         the one-time grants, and the counts of the quest system
//   GET  /v1/dev/quests/templates                         the backstory templates of the quest data (decision D2 option C)
//   POST /v1/dev/quests/template?character=ID&template=ID the achievements and the start objectives of a template (the command
//                                                         apply_backstory of the admin interface)
//   POST /v1/dev/quests/grant?character=ID&objective=ID   a new instance of an objective for a character in the world (development
//                                                         only: a live check)
//   POST /v1/dev/quests/achievement?character=ID&id=ID&on=1|0
//                                                         unlock or lock an achievement of a character in the world (development
//                                                         only: the change event of the quest system, the empty list fragment)
//   POST /v1/dev/quests/abandon?character=ID&instance=HEX the abandon of a quest (stage 1.8c; the command abandon_quest of the admin
//                                                         interface)
//   POST /v1/dev/quests/reset?character=ID&instance=HEX   the reset of a quest (stage 1.8c; the command reset_quest)
// tools/world sends these requests to the backend of a development run.

namespace soulwarden::backend::server::http {
namespace {

using namespace dev;

service::HttpResponse show_progress(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = character_of(context, refusal);
    if (!character) {
        return refusal;
    }
    return in_world(services, [character = *character](world::World& target) {
        const auto view = commands::progress_view(target, character);
        return view ? json_reply(200, view->dump()) : message(404, "the store has no character with this id");
    });
}

/** GET /v1/dev/ownership?character=ID: the ownership of the DLC of a character (stage 1.1b). */
service::HttpResponse show_ownership(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = character_of(context, refusal);
    if (!character) {
        return refusal;
    }
    return in_world(services, [character = *character](world::World& target) {
        const auto view = commands::ownership_view(target, character);
        return view ? json_reply(200, view->dump()) : message(404, "the store has no character with this id");
    });
}

/** GET /v1/dev/quests?character=ID: the quests of a character (stage 1.8a). */
service::HttpResponse show_quests(const Services& services, const RouteContext& context) {
    service::HttpResponse refusal;
    const auto character = character_of(context, refusal);
    if (!character) {
        return refusal;
    }
    return in_world(services, [character = *character](world::World& target) {
        const auto view = commands::quests_view(target, character);
        return view ? json_reply(200, view->dump()) : message(404, "the store has no character with this id");
    });
}

} // namespace

void register_dev_progress_routes(Router& router, const Services& services) {
    router.add("GET", "", "/v1/dev/ownership", "dev.ownership", [&services](const RouteContext& context) {
        return show_ownership(services, context);
    });
    router.add("GET", "", "/v1/dev/progress", "dev.progress", [&services](const RouteContext& context) {
        return show_progress(services, context);
    });
    router.add("POST", "", "/v1/dev/progress/level", "dev.progress_level", command_route(services, commands::set_level));
    router.add("POST", "", "/v1/dev/progress/experience", "dev.progress_experience",
               command_route(services, commands::add_experience));
    router.add("POST", "", "/v1/dev/progress/attribute", "dev.progress_attribute",
               command_route(services, commands::set_attribute));
    router.add("POST", "", "/v1/dev/progress/mastery", "dev.progress_mastery",
               command_route(services, commands::set_weapon_mastery));
    router.add("POST", "", "/v1/dev/progress/tradeskill", "dev.progress_tradeskill",
               command_route(services, commands::set_trade_skill));
    router.add("POST", "", "/v1/dev/progress/skill-experience", "dev.progress_skill_experience",
               command_route(services, commands::add_skill_experience));
    router.add("POST", "", "/v1/dev/progress/node", "dev.progress_node", command_route(services, commands::assign_node));
    router.add("POST", "", "/v1/dev/progress/bar", "dev.progress_bar", command_route(services, commands::set_ability_bar));
    router.add("GET", "", "/v1/dev/presets", "dev.presets", [&services](const RouteContext& context) {
        if (!context.request.host.empty()) {
            return message(403, "forbidden");
        }
        return in_world(services, [](world::World& target) { return json_reply(200, commands::presets_json(target).dump()); });
    });
    router.add("POST", "", "/v1/dev/presets/apply", "dev.presets_apply", command_route(services, commands::apply_preset));
    router.add("GET", "", "/v1/dev/quests", "dev.quests", [&services](const RouteContext& context) {
        return show_quests(services, context);
    });
    router.add("GET", "", "/v1/dev/quests/templates", "dev.quest_templates", [&services](const RouteContext& context) {
        if (!context.request.host.empty()) {
            return message(403, "forbidden");
        }
        return in_world(services, [](world::World& target) { return json_reply(200, commands::backstories_json(target).dump()); });
    });
    router.add("POST", "", "/v1/dev/quests/template", "dev.quest_template", command_route(services, commands::apply_backstory));
    router.add("POST", "", "/v1/dev/quests/grant", "dev.quest_grant", command_route(services, commands::grant_objective));
    router.add("POST", "", "/v1/dev/quests/achievement", "dev.quest_achievement", command_route(services, commands::set_achievement));
    router.add("POST", "", "/v1/dev/quests/abandon", "dev.quest_abandon", command_route(services, commands::abandon_quest));
    router.add("POST", "", "/v1/dev/quests/reset", "dev.quest_reset", command_route(services, commands::reset_quest));
}

} // namespace soulwarden::backend::server::http
