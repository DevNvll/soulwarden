#include <string>

#include <nlohmann/json.hpp>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/build_presets.h"
#include "world/world.h"

// The commands of the build presets (stage B0): the common layer of the admin routes (the command apply_preset, the route
// GET /admin/v1/catalog/presets) and of the development routes (POST /v1/dev/presets/apply, GET /v1/dev/presets). The world does the
// apply (world/build_presets.h): a route has no logic of its own for a character.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** Longest name of a preset or a variant in a command. */
constexpr std::size_t kNameLimit = 64;

int status_of(world::PresetError error) {
    switch (error) {
    case world::PresetError::unknownPreset:
    case world::PresetError::unknownVariant:
    case world::PresetError::unknownCharacter:
        return 404;
    case world::PresetError::storeFailed:
        return 500;
    default:
        return 409;
    }
}

} // namespace

WorldCommand apply_preset(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto preset = arguments.text("preset");
    const auto variant = arguments.has("variant") ? arguments.text("variant") : std::optional<std::string>(std::string{});
    if (!preset || preset->empty() || preset->size() > kNameLimit || !variant || variant->size() > kNameLimit) {
        refusal = message(400, "give preset=NAME (bow-boltcaster) and, when necessary, variant=NAME (pve or pvp)");
        return {};
    }
    return [character, preset = *preset, variant = *variant](world::World& target) {
        const world::PresetResult result = target.presets().apply(character, preset, variant);
        if (!result.ok()) {
            const std::string text = std::string(world::preset_error_text(result.error)) + (result.cause.empty() ? "" : ": " + result.cause);
            return json_reply(status_of(result.error), json{{"message", text}}.dump());
        }
        return json_reply(200,
                          json{{"message", "done"},
                               {"applied", result.inWorld ? "world" : "stored"},
                               {"preset", preset},
                               {"variant", variant},
                               {"changes", result.changes}}
                              .dump());
    };
}

json presets_json(world::World& target) {
    json presets = json::array();
    for (const world::PresetStatus& status : target.presets().presets()) {
        const world::BuildPreset& preset = status.preset;
        json variants = json::array();
        for (const world::PresetVariant& variant : preset.variants) {
            json trees = json::array();
            for (const world::TreeBuild& tree : variant.trees) {
                trees.push_back({{"table", tree.table}, {"level", tree.rank + 1}, {"nodes", tree.nodes}, {"bar", tree.bar}});
            }
            variants.push_back({{"name", variant.name}, {"label", variant.label}, {"trees", trees}});
        }
        json items = json::array();
        for (const world::PresetItem& item : preset.items) {
            items.push_back({{"slot", item.slot ? json(*item.slot) : json("bag")},
                             {"item", item.itemId},
                             {"quantity", item.quantity},
                             {"gearScore", item.gearScore},
                             {"perks", item.perks}});
        }
        json attributes = json::object();
        attributes["strength"] = preset.attributes[world::attribute::kStrength];
        attributes["dexterity"] = preset.attributes[world::attribute::kDexterity];
        attributes["intelligence"] = preset.attributes[world::attribute::kIntelligence];
        attributes["focus"] = preset.attributes[world::attribute::kFocus];
        attributes["constitution"] = preset.attributes[world::attribute::kConstitution];
        presets.push_back({{"name", preset.name},
                           {"title", preset.title},
                           {"enabled", status.enabled},
                           {"cause", status.cause},
                           {"level", preset.level},
                           {"attributes", attributes},
                           {"variants", variants},
                           {"items", items}});
    }
    return json{{"presets", presets}};
}

} // namespace soulwarden::backend::server::http::commands
