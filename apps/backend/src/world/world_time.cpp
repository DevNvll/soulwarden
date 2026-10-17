#include "world/world_time.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The longest part of a cycle that the backend accepts (real seconds): the integer products of the edges stay in 64 bits. */
constexpr std::uint32_t kLongestPart = 86400;
/** The noon of a new world (decision D2). */
constexpr double kNoon = 43200.0;

/** @return The game seconds of the night part (duskTime to dawnTime over midnight). */
std::uint64_t night_game_seconds(const DayCycle& cycle) {
    return day_cycle::kDaySeconds - cycle.duskTime + cycle.dawnTime;
}

/** @return The real time from 00:00 to dawnTime, in nanoseconds (a part of the night part). */
std::uint64_t before_dawn_ns(const DayCycle& cycle) {
    return static_cast<std::uint64_t>(cycle.dawnTime) * cycle.nightLength * day_cycle::kSecondNs / night_game_seconds(cycle);
}

std::string clock_text(double gameSecond) {
    const auto whole = static_cast<unsigned>(std::llround(gameSecond)) % day_cycle::kDaySeconds;
    char text[16];
    std::snprintf(text, sizeof(text), "%02u:%02u:%02u", whole / 3600, whole / 60 % 60, whole % 60);
    return text;
}

} // namespace

const char* day_phase_text(DayPhase phase) {
    switch (phase) {
    case DayPhase::night:
        return "night";
    case DayPhase::dawn:
        return "dawn";
    case DayPhase::day:
        return "day";
    case DayPhase::dusk:
        return "dusk";
    }
    return "unknown";
}

namespace day_cycle {

bool valid(const DayCycle& cycle) {
    return cycle.dayLength > 0 && cycle.nightLength > 0 && cycle.dayLength <= kLongestPart && cycle.nightLength <= kLongestPart
           && cycle.dawnTime < cycle.duskTime && cycle.duskTime < kDaySeconds
           && static_cast<std::uint64_t>(cycle.dawnDuration) + cycle.duskDuration <= cycle.dayLength;
}

std::uint64_t length_ns(const DayCycle& cycle) {
    return (static_cast<std::uint64_t>(cycle.dayLength) + cycle.nightLength) * kSecondNs;
}

Edges edges(const DayCycle& cycle) {
    Edges result;
    result.dawn = before_dawn_ns(cycle);
    result.night = result.dawn + static_cast<std::uint64_t>(cycle.dayLength) * kSecondNs;
    result.day = result.dawn + static_cast<std::uint64_t>(cycle.dawnDuration) * kSecondNs;
    result.dusk = result.night - static_cast<std::uint64_t>(cycle.duskDuration) * kSecondNs;
    return result;
}

std::uint64_t position_of(const DayCycle& cycle, double gameSecond) {
    // The inverse of the formula of the client (0x6A09E60): the night part before dawnTime, the day part, the night part after
    // duskTime.
    const double night = static_cast<double>(night_game_seconds(cycle));
    const double day = static_cast<double>(kDaySeconds) - night;
    const double before = static_cast<double>(before_dawn_ns(cycle));
    double position = 0.0;
    if (gameSecond < cycle.dawnTime) {
        position = gameSecond / night * cycle.nightLength * static_cast<double>(kSecondNs);
    } else if (gameSecond <= cycle.duskTime) {
        position = before + (gameSecond - cycle.dawnTime) / day * cycle.dayLength * static_cast<double>(kSecondNs);
    } else {
        position = before + static_cast<double>(cycle.dayLength) * static_cast<double>(kSecondNs)
                   + (gameSecond - cycle.duskTime) / night * cycle.nightLength * static_cast<double>(kSecondNs);
    }
    const std::uint64_t length = length_ns(cycle);
    const auto rounded = static_cast<std::uint64_t>(std::llround(std::max(position, 0.0)));
    return rounded % length;
}

double game_second(const DayCycle& cycle, std::uint64_t position) {
    const Edges edge = edges(cycle);
    const double night = static_cast<double>(night_game_seconds(cycle));
    const double day = static_cast<double>(kDaySeconds) - night;
    const double x = static_cast<double>(position % length_ns(cycle));
    const double second = static_cast<double>(kSecondNs);
    if (position <= edge.dawn) {
        return x / (cycle.nightLength * second) * night;
    }
    if (position <= edge.night) {
        return (x - static_cast<double>(edge.dawn)) / (cycle.dayLength * second) * day + cycle.dawnTime;
    }
    return (x - static_cast<double>(edge.night)) / (cycle.nightLength * second) * night + cycle.duskTime;
}

DayPhase phase_of(const DayCycle& cycle, std::uint64_t position) {
    const Edges edge = edges(cycle);
    position %= length_ns(cycle);
    if (position < edge.dawn || position > edge.night) {
        return DayPhase::night;
    }
    if (position < edge.day) {
        return DayPhase::dawn;
    }
    if (position < edge.dusk) {
        return DayPhase::day;
    }
    return DayPhase::dusk;
}

std::uint64_t to_next_edge(const DayCycle& cycle, std::uint64_t position) {
    const Edges edge = edges(cycle);
    const std::uint64_t length = length_ns(cycle);
    position %= length;
    // The night edge belongs to dusk (phase_of): the phase changes one nanosecond after it.
    for (const std::uint64_t next : {edge.dawn, edge.day, edge.dusk, edge.night + 1}) {
        if (position < next) {
            return next - position;
        }
    }
    return length - position + edge.dawn;
}

TimeRecord record(const DayCycle& cycle, std::uint64_t serverNow, std::uint64_t position) {
    const std::uint64_t length = length_ns(cycle);
    TimeRecord result;
    result.cycle = cycle;
    result.anchor = serverNow > length ? serverNow - length : 1;
    const std::uint64_t elapsed = (serverNow > result.anchor ? serverNow - result.anchor : 0) % length;
    const std::uint64_t part = (position % length + length - elapsed) % length;
    result.offset = static_cast<float>(static_cast<double>(part) / (365.0 * static_cast<double>(length)));
    const Edges edge = edges(cycle);
    const double whole = static_cast<double>(length);
    result.starts = {static_cast<float>(static_cast<double>(edge.dawn) / whole), static_cast<float>(static_cast<double>(edge.day) / whole),
                     static_cast<float>(static_cast<double>(edge.dusk) / whole),
                     static_cast<float>(static_cast<double>(edge.night) / whole)};
    return result;
}

double client_part(const TimeRecord& record, std::uint64_t serverNow) {
    // 0x6A0ADC0: CVTSI2SS of the absolute time from the anchor, MULSS of the float year and the offset, ADDSS, a whole number, and
    // the remainders of the year and of the cycle. Each step in its own statement: no fused multiply and add.
    const std::uint64_t cycleNs = length_ns(record.cycle);
    const std::uint64_t year = cycleNs * 365;
    const std::uint64_t difference = serverNow >= record.anchor ? serverNow - record.anchor : record.anchor - serverNow;
    const auto elapsed = static_cast<float>(difference);
    const auto yearFloat = static_cast<float>(year);
    const float moved = yearFloat * record.offset;
    const float sum = elapsed + moved;
    const auto whole = static_cast<std::uint64_t>(sum);
    return static_cast<double>((whole % year) % cycleNs) / static_cast<double>(cycleNs);
}

} // namespace day_cycle

WorldTime::WorldTime(World& world) : m_world(world) {}

void WorldTime::start() {
    if (m_started) {
        return;
    }
    m_started = true;
    m_reference = m_world.clock().now();
    const std::uint64_t length = day_cycle::length_ns(m_cycle);
    const std::optional<std::uint64_t> stored = m_world.store().world_time(kWorldTimeKey);
    if (stored && *stored < length) {
        m_base = *stored;
        m_counts.loaded = true;
        log::write("world: the world time continues from its stored position: %s (%s), a cycle of %u s of day and %u s of night",
                   clock_text(day_cycle::game_second(m_cycle, m_base)).c_str(),
                   day_phase_text(day_cycle::phase_of(m_cycle, m_base)),
                   m_cycle.dayLength,
                   m_cycle.nightLength);
    } else {
        if (stored) {
            log::write("world: the stored world time %llu is not a position of the cycle: the world starts at noon",
                       static_cast<unsigned long long>(*stored));
        }
        m_base = day_cycle::position_of(m_cycle, kNoon);
        log::write("world: a new world time starts at noon (12:00:00, day), a cycle of %u s of day and %u s of night",
                   m_cycle.dayLength,
                   m_cycle.nightLength);
        save("new world");
    }
    m_phase = day_cycle::phase_of(m_cycle, m_base);
    m_lastSave = m_reference;
}

std::uint64_t WorldTime::position() {
    start();
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(m_world.clock().now() - m_reference).count();
    const std::uint64_t length = day_cycle::length_ns(m_cycle);
    return (m_base + static_cast<std::uint64_t>(std::max<std::int64_t>(elapsed, 0)) % length) % length;
}

DayPhase WorldTime::phase() {
    start();
    return m_phase;
}

double WorldTime::game_second() {
    return day_cycle::game_second(m_cycle, position());
}

std::uint64_t WorldTime::to_next_edge() {
    return day_cycle::to_next_edge(m_cycle, position());
}

std::uint64_t WorldTime::revision() {
    start();
    return m_revision;
}

TimeRecord WorldTime::record(std::uint64_t serverNow) {
    return day_cycle::record(m_cycle, serverNow, position());
}

void WorldTime::tick() {
    start();
    const DayPhase now = day_cycle::phase_of(m_cycle, position());
    if (now != m_phase) {
        phase_changed(now, "the cycle");
    }
    if (!m_lastSave || m_world.clock().now() - *m_lastSave >= kSaveInterval) {
        save("interval");
    }
}

void WorldTime::phase_changed(DayPhase phase, const char* cause) {
    log::write("world: the world time phase changes from %s to %s (%s) at %s",
               day_phase_text(m_phase),
               day_phase_text(phase),
               cause,
               clock_text(game_second()).c_str());
    m_phase = phase;
    ++m_counts.phaseChanges;
    // The points with a time constraint near the characters: the normal scan in this tick (no walk of the points of the world).
    m_world.creatures().scan_soon();
}

TimeSetError WorldTime::set(double gameSecond, const std::string& cause) {
    start();
    if (!std::isfinite(gameSecond) || gameSecond < 0.0 || gameSecond >= day_cycle::kDaySeconds) {
        ++m_counts.refusedSets;
        return TimeSetError::badTime;
    }
    const std::uint64_t position = day_cycle::position_of(m_cycle, gameSecond);
    // The store first: a command that the database refuses changes nothing.
    if (!m_world.store().set_world_time(kWorldTimeKey, position)) {
        ++m_counts.refusedSets;
        log::write("world: the world time stays: the database did not accept the time %s (%s)", clock_text(gameSecond).c_str(),
                   cause.c_str());
        return TimeSetError::storage;
    }
    m_base = position;
    m_reference = m_world.clock().now();
    m_lastSave = m_reference;
    ++m_revision;
    ++m_counts.sets;
    log::write("world: the world time is %s for the whole world (%s)", clock_text(gameSecond).c_str(), cause.c_str());
    const DayPhase now = day_cycle::phase_of(m_cycle, position);
    if (now != m_phase) {
        phase_changed(now, "a command");
    }
    return TimeSetError::none;
}

bool WorldTime::save(const char* cause) {
    const std::uint64_t now = position();
    m_lastSave = m_world.clock().now();
    if (!m_world.store().set_world_time(kWorldTimeKey, now)) {
        if (m_counts.failedSaves++ == 0) {
            log::write("world: the world time was not saved (%s): the next interval tries again", cause);
        }
        return false;
    }
    ++m_counts.saves;
    return true;
}

} // namespace soulwarden::backend::world
