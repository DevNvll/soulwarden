#include <algorithm>
#include <chrono>
#include <string>

#include "core/logging/log.h"
#include "server/http/character_commands.h"
#include "server/http/router.h"

// The commands of the status effects of a character (stage 3.3b-S1: world/statuses.h), in the common layer of the
// development routes and of the admin routes (character_commands.h).

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** Largest number of applications of one command (a stack test). */
constexpr std::uint64_t kLargestCount = 25;

const char* source_name(world::StatusSourceKind kind) {
    switch (kind) {
    case world::StatusSourceKind::command:
        return "command";
    case world::StatusSourceKind::consumable:
        return "consumable";
    case world::StatusSourceKind::rune:
        return "rune";
    case world::StatusSourceKind::gear:
        return "gear";
    case world::StatusSourceKind::ability:
        return "ability";
    case world::StatusSourceKind::status:
        return "status";
    case world::StatusSourceKind::spell:
        return "spell";
    }
    return "unknown";
}

} // namespace

WorldCommand apply_status(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto status = arguments.text("status");
    const auto seconds = arguments.has("seconds") ? arguments.number("seconds", 0.0F, 86400.0F) : std::optional<float>(0.0F);
    const auto potency = arguments.has("potency") ? arguments.number("potency", 0.01F, 100.0F) : std::optional<float>(1.0F);
    const auto count = arguments.has("count") ? arguments.integer("count", 1, kLargestCount) : std::optional<std::uint64_t>(1);
    if (!status || status->empty() || status->size() > 200 || !seconds || !potency || !count) {
        refusal = message(400, "give status (the id of a status row), and when necessary seconds (0 to 86400; 0: the duration of the "
                               "row), potency, and count (1 to 25 applications)");
        return {};
    }
    return [character, status = *status, seconds = *seconds, potency = *potency, count = *count](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(target.store().find_character(character) ? 409 : 404, "the character is not in the world");
        }
        world::StatusApply how;
        how.seconds = seconds;
        how.potency = potency;
        how.source = world::StatusSource{world::StatusSourceKind::command, "command", 0, 0};
        world::StatusVerdict verdict = world::StatusVerdict::unknown;
        for (std::uint64_t index = 0; index < count; ++index) {
            entity = target.find_character(character);
            if (entity == nullptr) {
                break;
            }
            verdict = target.statuses().apply(*entity, status, how);
        }
        log::write("world: command: the status %s on character %s (%llu time(s)): %s", status.c_str(), character.c_str(),
                   static_cast<unsigned long long>(count), world::status_verdict_text(verdict));
        json reply = status_json(target, character);
        reply["message"] = world::status_verdict_text(verdict);
        const bool done = verdict == world::StatusVerdict::applied || verdict == world::StatusVerdict::renewed
                          || verdict == world::StatusVerdict::instant;
        return json_reply(done ? 200 : 409, reply.dump());
    };
}

WorldCommand remove_status(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto status = arguments.text("status");
    if (!status || status->empty() || status->size() > 200) {
        refusal = message(400, "give status (the id of a status row, or all)");
        return {};
    }
    return [character, status = *status](world::World& target) {
        world::Entity* entity = target.find_character(character);
        if (entity == nullptr) {
            return message(target.store().find_character(character) ? 409 : 404, "the character is not in the world");
        }
        const bool all = status == "all";
        const unsigned ended = target.statuses().remove(
            *entity,
            [&](const world::ActiveStatus& entry) {
                return entry.source.kind != world::StatusSourceKind::gear && (all || entry.status == status);
            },
            world::StatusEnd::command);
        log::write("world: command: %u status(es) %s of character %s end", ended, status.c_str(), character.c_str());
        json reply = status_json(target, character);
        reply["message"] = std::to_string(ended) + " status(es) ended";
        return json_reply(200, reply.dump());
    };
}

json status_json(world::World& target, const std::string& character) {
    json reply{{"inWorld", false}};
    const world::WorldClock::time_point now = target.clock().now();
    json list = json::array();
    const world::Entity* entity = target.find_character(character);
    const std::vector<world::ActiveStatus>* statuses = entity != nullptr ? &entity->statuses : target.statuses().statuses_of(character);
    if (statuses != nullptr) {
        for (const world::ActiveStatus& status : *statuses) {
            if (!status.permanent && status.ends <= now) {
                continue;
            }
            list.push_back({{"status", status.status},
                            {"index", status.index},
                            {"seconds", status.permanent ? -1.0 : std::chrono::duration<double>(status.ends - now).count()},
                            {"stacks", status.stacks},
                            {"potency", status.potency},
                            {"source", source_name(status.source.kind)},
                            {"sourceId", status.source.id},
                            {"tray", status.tray},
                            {"public", status.observed},
                            {"wire", status.replicated},
                            {"hitsLeft", status.hitsLeft}});
        }
    }
    reply["statuses"] = list;
    if (entity != nullptr) {
        reply["inWorld"] = true;
        reply["moveSpeedPart"] = entity->gear.moveSpeed;
        reply["sprintSpeedPart"] = entity->gear.sprintSpeed;
        reply["physicalArmorPart"] = entity->gear.physicalArmor;
        reply["elementalArmorPart"] = entity->gear.elementalArmor;
        reply["attributeBonus"] = entity->attributes.bonus;
    }
    const world::StatusCounts& counts = target.statuses().counts();
    json applications = json::object();
    for (std::size_t verdict = 0; verdict < world::kStatusVerdictCount; ++verdict) {
        applications[world::status_verdict_text(static_cast<world::StatusVerdict>(verdict))] = counts.applications[verdict];
    }
    json ended = json::object();
    for (std::size_t cause = 0; cause < world::kStatusEndCount; ++cause) {
        ended[world::status_end_text(static_cast<world::StatusEnd>(cause))] = counts.ended[cause];
    }
    reply["counts"] = {{"applications", applications}, {"ended", ended}, {"ticks", counts.ticks}, {"heals", counts.heals},
                       {"pulsesWithNoHeal", counts.pulsesWithNoHeal},
                       {"dueTaken", counts.dueTaken}, {"dueWaiting", target.statuses().due_size()},
                       {"spellsNotDone", counts.spellsNotDone}};
    return reply;
}

} // namespace soulwarden::backend::server::http::commands
