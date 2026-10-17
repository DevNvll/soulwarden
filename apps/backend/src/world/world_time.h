#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

#include "world/world_clock.h"

// The world time (stage 2.2; protocol notes 4.43, docs/systems/world-time-and-weather.md).
//
// The world has one day and night cycle: one position in the cycle (nanoseconds from 00:00) that the world owns, with the world key
// as its key (roadmap, rule MP-1). No character and no session owns it. The position moves on the world clock (rule MP-6): its value
// is the stored position plus the time of the world clock from the reference time. The store keeps the position (table world_time):
// at a command, each kWorldTimeSaveInterval, and at an orderly stop. A stop of the backend stops the cycle (decision D3 of the
// orchestrator): the next start continues from the stored position. A new world starts at noon (decision D2).
//
// The cycle of the client (decision D1, client code 0x40718B0 and 0x407E7D0: the defaults of the Time component; no table of the
// game data has a different value): 1,800 real seconds from 06:00 to 19:00 (26 game seconds in one real second) and 900 real seconds
// from 19:00 to 06:00 (44 game seconds in one real second), one cycle of 45 minutes. The client calculates the hour itself in each
// frame from one record (message 7036: an anchor on the server clock and a part of the cycle) and its copy of the server clock
// (client code 0x6A0ADC0, 0x6A09E60): the server sends no hour stream.
//
// The phases (client code 0x6A08270; the numbers of the client): night 0, dawn 1, day 2, dusk 3. The phase edges of the record are
// parts of the cycle: dawnStart and nightStart come from the lengths (the client needs them for its hour formula), dayStart is
// dawnStart plus the dawn duration, and duskStart is nightStart minus the dusk duration (decision D4 of the orchestrator: gameplay
// windows of 60 s; the client constructors leave the four values at 0). The server phase selects the creature points with a time
// constraint (world/creatures.h).

namespace soulwarden::backend::world {

class World;

/** The phases of the day: the numbers of the client (client code 0x6A08270, the phase of the messages 7036 and 4247). */
enum class DayPhase : std::uint32_t {
    night = 0,
    dawn = 1,
    day = 2,
    dusk = 3,
};
inline constexpr std::size_t kDayPhaseCount = 4;
[[nodiscard]] const char* day_phase_text(DayPhase phase);

/** @return The bit of a phase in a phase mask (the creature data, format 4): bit 0 night, bit 1 dawn, bit 2 day, bit 3 dusk. */
[[nodiscard]] constexpr std::uint8_t phase_bit(DayPhase phase) {
    return static_cast<std::uint8_t>(1U << static_cast<unsigned>(phase));
}
/** The mask of a spawn point with no time constraint: each phase. */
inline constexpr std::uint8_t kAllPhases = 0x0F;

/** The key of the row of the main world in the table world_time (the level of the level information). */
inline constexpr const char* kWorldTimeKey = "newworld_vitaeeterna";

/**
 * The values of the day cycle of a time record. The default values are the defaults of the client (client code 0x40718B0 and
 * 0x407E7D0; decision D1).
 */
struct DayCycle {
    /** The game second of the day at which the day part starts (06:00). */
    std::uint32_t dawnTime{21600};
    /**
     * Real seconds. The client: its engine dawn lasts dawnDuration * 22 / 3600 game hours (client code 0x644ECC0: 06:00 to about
     * 06:22). The server: the gameplay phase dawn (decision D4).
     */
    std::uint32_t dawnDuration{60};
    /** The game second of the day at which the night part starts (19:00). */
    std::uint32_t duskTime{68400};
    std::uint32_t duskDuration{60};
    /** Real seconds of the day part (dawnTime to duskTime). Not 0: the client divides by it (0x644ECC0). */
    std::uint32_t dayLength{1800};
    /** Real seconds of the night part (duskTime to dawnTime). */
    std::uint32_t nightLength{900};
};

/** One record of the Time component: the 60 bytes of a 7036 (client reader 0x69F2530; the fields a and b are -1 on the wire). */
struct TimeRecord {
    /** A part of 365 cycles that the client adds to the time from the anchor (client code 0x6A0ADC0). */
    float offset{0.0F};
    DayCycle cycle;
    /** A time of the server clock (nanoseconds) not later than the clock of the client. Not 0. */
    std::uint64_t anchor{1};
    /** dawnStart, dayStart, duskStart, nightStart: parts of the cycle from 0 to 1. */
    std::array<float, 4> starts{};
};

namespace day_cycle {

inline constexpr std::uint64_t kSecondNs = 1'000'000'000ULL;
inline constexpr std::uint32_t kDaySeconds = 86400;

/** @return True for a cycle that the client can use: lengths above 0, the dawn and dusk times in the day, dawn before dusk. */
[[nodiscard]] bool valid(const DayCycle& cycle);
/** @return The length of one cycle in nanoseconds (the day part and the night part). */
[[nodiscard]] std::uint64_t length_ns(const DayCycle& cycle);

/** The four phase edges of a cycle: positions in nanoseconds from 00:00. */
struct Edges {
    /** The start of the day part (dawnTime): the start of the phase dawn. */
    std::uint64_t dawn{0};
    /** The start of the phase day (dawn plus the dawn duration). */
    std::uint64_t day{0};
    /** The start of the phase dusk (night minus the dusk duration). */
    std::uint64_t dusk{0};
    /** The start of the night part (duskTime): the start of the phase night. */
    std::uint64_t night{0};
};
[[nodiscard]] Edges edges(const DayCycle& cycle);

/** @return The position of a game second of the day (0 to 86,400), in nanoseconds from 00:00 (the inverse of the client formula). */
[[nodiscard]] std::uint64_t position_of(const DayCycle& cycle, double gameSecond);
/** @return The game second of the day (0 to 86,400) at a position (the formula of the client, 0x6A09E60, in double precision). */
[[nodiscard]] double game_second(const DayCycle& cycle, std::uint64_t position);
/**
 * @return The phase at a position: the comparisons of the client (0x6A08270) with the exact edges: before dawn and after night is
 *         night, dawn to day is dawn, day to dusk is day, dusk to night (night included) is dusk.
 */
[[nodiscard]] DayPhase phase_of(const DayCycle& cycle, std::uint64_t position);
/** @return The time from a position to the next phase edge (nanoseconds, above 0). */
[[nodiscard]] std::uint64_t to_next_edge(const DayCycle& cycle, std::uint64_t position);

/**
 * The record for a position of the cycle at a time of the server clock (research world-time-stage.md 7.1). The anchor is one cycle
 * before the time (1 at an earlier time: never 0, and never later than the clock of the client, which is late by the time of the
 * path), and the offset moves the cycle to the position: offset = ((position - (now - anchor)) mod cycle) / (365 * cycle). The
 * client then has a small time from the anchor (its float32 steps stay below 1 ms for two cycles) with no change of the clock epoch.
 */
[[nodiscard]] TimeRecord record(const DayCycle& cycle, std::uint64_t serverNow, std::uint64_t position);

/** The client calculation of the part of the cycle (0x6A0ADC0: float32 of the time from the anchor, the offset, two remainders). */
[[nodiscard]] double client_part(const TimeRecord& record, std::uint64_t serverNow);

} // namespace day_cycle

/** What became of a command that sets the world time. */
enum class TimeSetError {
    none,
    /** Not a game second of the day (0 to 86,399) or not a number. Nothing changed. */
    badTime,
    /** The database did not accept the row. Nothing changed: the cycle continues from its position. */
    storage,
};

/** The counts of the world time, for the development list and the tests. */
struct WorldTimeCounts {
    /** The phase changes of the cycle (natural and by a command). */
    std::uint64_t phaseChanges{0};
    std::uint64_t sets{0};
    std::uint64_t refusedSets{0};
    std::uint64_t saves{0};
    std::uint64_t failedSaves{0};
    /** True when the start read a stored position; false: a new world (noon). */
    bool loaded{false};
};

/** The world time: the position of the day cycle of the world. World thread only. */
class WorldTime {
public:
    explicit WorldTime(World& world);

    /**
     * One tick of the world: the start (the stored position, at the first tick), a phase change (the creature system scans its
     * points in the same tick), and the save of the position each kWorldTimeSaveInterval. The world calls it before the creature
     * system.
     */
    void tick();

    /** @return The position of the cycle now (nanoseconds from 00:00). */
    [[nodiscard]] std::uint64_t position();
    /** @return The phase now (the phase of the last tick or command: the phase that the creature system and the sessions use). */
    [[nodiscard]] DayPhase phase();
    /** @return The game second of the day now (0 to 86,400). */
    [[nodiscard]] double game_second();
    /** @return The time to the next phase edge (nanoseconds). */
    [[nodiscard]] std::uint64_t to_next_edge();
    /** @return A number that a command changes (a session sends each client a new record when it changes). */
    [[nodiscard]] std::uint64_t revision();
    [[nodiscard]] const DayCycle& cycle() const {
        return m_cycle;
    }
    /** @return The record of the position now for a time of the server clock (each session writes its own ids around it). */
    [[nodiscard]] TimeRecord record(std::uint64_t serverNow);

    /**
     * Sets the time of the world (a command for the whole world, rule MP-2): the store gets the new position first, then the world.
     * Each session whose client asked for the time gets the new record; the creature system scans its points at once when the phase
     * changed.
     * @param gameSecond A game second of the day, 0 to 86,399.
     */
    TimeSetError set(double gameSecond, const std::string& cause);

    /**
     * Writes the position now to the store (the save interval, an orderly stop of the backend). A failure goes to the log, and the
     * next interval tries again. @return True when the row was written.
     */
    bool save(const char* cause);

    [[nodiscard]] const WorldTimeCounts& counts() const {
        return m_counts;
    }

    /** The interval of the save of the position (decision D3: a crash loses this time at most). */
    static constexpr std::chrono::seconds kSaveInterval{60};

private:
    void start();
    /** A new phase: the count, a log line, and a scan of the creature system in this tick. */
    void phase_changed(DayPhase phase, const char* cause);

    World& m_world;
    DayCycle m_cycle;
    bool m_started{false};
    /** The position at the reference time. */
    std::uint64_t m_base{0};
    WorldClock::time_point m_reference{};
    DayPhase m_phase{DayPhase::day};
    std::uint64_t m_revision{1};
    std::optional<WorldClock::time_point> m_lastSave;
    WorldTimeCounts m_counts;
};

} // namespace soulwarden::backend::world
