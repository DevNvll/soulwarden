#include <cmath>
#include <cstdio>
#include <string>

#include "server/http/character_commands.h"
#include "server/http/router.h"
#include "world/world_time.h"

// The world time commands (stage 2.2, world/world_time.h), in the common layer of the development routes and of the admin routes
// (character_commands.h). The time is a value of the world: the character of a command names who gives it (rule MP-9), and the
// command changes the time of each player.

namespace soulwarden::backend::server::http::commands {
namespace {

using nlohmann::json;

/** @return The text "HH:MM:SS" of a game second of the day (the nearest second). */
std::string clock_text(double gameSecond) {
    const auto whole = static_cast<unsigned>(std::llround(gameSecond)) % world::day_cycle::kDaySeconds;
    char text[16];
    std::snprintf(text, sizeof(text), "%02u:%02u:%02u", whole / 3600, whole / 60 % 60, whole % 60);
    return text;
}

/** @return The game second of a text "HH:MM" or "HH:MM:SS" (00:00 to 23:59:59), or no value for a different text. */
std::optional<unsigned> read_clock(const std::string& text) {
    unsigned parts[3] = {0, 0, 0};
    unsigned count = 0;
    std::size_t index = 0;
    while (count < 3) {
        std::size_t digits = 0;
        unsigned value = 0;
        while (index < text.size() && text[index] >= '0' && text[index] <= '9' && digits < 2) {
            value = value * 10 + static_cast<unsigned>(text[index] - '0');
            ++index;
            ++digits;
        }
        // The hour has one or two digits; the minute and the second have two.
        if (digits == 0 || (count > 0 && digits != 2)) {
            return std::nullopt;
        }
        parts[count++] = value;
        if (index == text.size()) {
            break;
        }
        if (text[index] != ':') {
            return std::nullopt;
        }
        ++index;
    }
    if (index != text.size() || count < 2 || parts[0] > 23 || parts[1] > 59 || parts[2] > 59) {
        return std::nullopt;
    }
    return parts[0] * 3600 + parts[1] * 60 + parts[2];
}

} // namespace

json world_time_json(world::World& target) {
    world::WorldTime& time = target.world_time();
    const world::DayCycle& cycle = time.cycle();
    const double second = time.game_second();
    const world::DayPhase phase = time.phase();
    const std::uint64_t toEdge = time.to_next_edge();
    const world::DayPhase next = world::day_cycle::phase_of(cycle, time.position() + toEdge);
    const world::WorldTimeCounts& counts = time.counts();
    return json{{"time", clock_text(second)},
                {"hour", second / 3600.0},
                {"phase", world::day_phase_text(phase)},
                {"phaseNumber", static_cast<std::uint32_t>(phase)},
                {"nextPhase", world::day_phase_text(next)},
                {"secondsToNextPhase", static_cast<double>(toEdge) / 1e9},
                {"positionSeconds", static_cast<double>(time.position()) / 1e9},
                {"cycle",
                 {{"seconds", cycle.dayLength + cycle.nightLength},
                  {"dayLength", cycle.dayLength},
                  {"nightLength", cycle.nightLength},
                  {"dawn", clock_text(cycle.dawnTime)},
                  {"dusk", clock_text(cycle.duskTime)},
                  {"dawnDuration", cycle.dawnDuration},
                  {"duskDuration", cycle.duskDuration}}},
                {"counts",
                 {{"loaded", counts.loaded},
                  {"phaseChanges", counts.phaseChanges},
                  {"sets", counts.sets},
                  {"refusedSets", counts.refusedSets},
                  {"saves", counts.saves},
                  {"failedSaves", counts.failedSaves}}}};
}

WorldCommand set_world_time(const std::string& character, const Arguments& arguments, service::HttpResponse& refusal) {
    const auto text = arguments.text("time");
    const auto second = text ? read_clock(*text) : std::nullopt;
    if (!second) {
        refusal = message(400, "give time: HH:MM or HH:MM:SS, from 00:00 to 23:59:59");
        return {};
    }
    return [character, second = *second](world::World& target) {
        // The character names who gives the command (rule MP-9): a character that the store does not have gives no command.
        if (target.find_character(character) == nullptr && !target.store().find_character(character)) {
            return message(404, "no character with this id");
        }
        const world::TimeSetError error =
            target.world_time().set(static_cast<double>(second), "a command of character " + character.substr(0, 8));
        if (error == world::TimeSetError::storage) {
            return message(500, "the database did not accept the world time: the time did not change");
        }
        if (error != world::TimeSetError::none) {
            return message(400, "not a time of the day: the time did not change");
        }
        json reply{{"applied", "world"},
                   {"scope", "world"},
                   {"message", "the time of the world is " + clock_text(second) + " for each player"},
                   {"worldTime", world_time_json(target)}};
        return json_reply(200, reply.dump());
    };
}

} // namespace soulwarden::backend::server::http::commands
