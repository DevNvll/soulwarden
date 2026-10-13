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

} // namespace

void register_dev_progress_routes(Router& router, const Services& services) {
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
}

} // namespace soulwarden::backend::server::http
