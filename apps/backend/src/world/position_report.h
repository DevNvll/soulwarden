#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>

#include "state/store.h"
#include "world/world_clock.h"

// The position that a client reports for its character, and the check of the world for it.
//
// The input report of a client (message type 1589) has the position of its character after
// the physics step of the client (protocol notes 4.7). The report is a claim of the client
// (roadmap, rule MP-5): the world checks each one before it changes the entity
// (World::report_position), and a report that fails changes nothing.
//
// The check, in this sequence:
//   1. Sequence. The input number of the report is newer than the last one of the session
//      (the reports are on the unreliable channel).
//   2. Numbers. Each number is finite, the height is not one of the two limit values of its
//      range on the wire, and the position is a position of the world (valid_position).
//   3. Speed. The horizontal distance from the last accepted position is not more than the
//      peak speed permits for the time between the two reports, and the horizontal distance
//      from the accepted position at the start of the time window is not more than the window
//      speed permits for that time, plus one margin. The margin thus applies one time for a
//      window, not one time for each report. The height has a limit for a rise and a larger
//      one for a fall, each one for the time between two reports, and the same two limits for
//      the total rise and the total fall of the time window: that time is the time of the
//      world clock plus the delay value, one time for a window. The total rise is the sum of
//      the rises of the accepted reports of the window, and the total fall is the sum of their
//      falls: a path that goes up and down has the two sums, not the difference of two
//      heights. The first report of a session has the position of the entity as its
//      reference, for the horizontal distance and for the height.
//
// The check is a design of this project: the client has no copy of the check of the original
// server. It finds a position that is too far from the last one. It cannot find a position
// in a wall or below the ground: that needs the collision data of the world.
//
// The world keeps the accepted position in the entity (rule MP-1). The values that describe
// the input of one client are in a PositionTrack, which the session of that client owns: the
// last input number, the accepted positions of the time window, and the counts for the log.

namespace soulwarden::backend::world {

/** One position that a client reports for its character. */
struct PositionReport {
    /** Input number of the report. The client adds one for each simulation step (30 each second). */
    std::uint16_t input{0};
    state::WorldPosition position;
    /** The height of the position as the 16 bits of the wire (0 = -100 m, 0xFFFF = 1000 m). */
    std::uint16_t height{0};
};

enum class PositionVerdict : std::uint8_t {
    /** The entity has the position of the report. */
    accepted,
    /** The input number is not newer than the last one of the session: an old or a second datagram. */
    outOfSequence,
    /** A number of the position is not finite. */
    notFinite,
    /** The height is one of the two limit values of its range: the real height is outside the range. */
    heightLimit,
    /** The position is outside the limits of the world (valid_position). */
    outsideWorld,
    /** The position is too far from the accepted positions for the time. */
    tooFast,
    /** The session is the owner of no entity. */
    noEntity,
};
inline constexpr std::size_t kPositionVerdictCount = 7;

/** @return A short text for a log line, for example "too fast". */
[[nodiscard]] const char* position_verdict_text(PositionVerdict verdict);

/**
 * Limits of the speed check. The numbers come from one live session of a character on foot
 * with a fall of 150 m, a sprint with a dodge in it, dodges, and jumps (stage 2.3a; protocol
 * notes 4.7): each limit is above the largest measured value. The speeds of the action lists
 * of the client are 5.5 m/s for a sprint, and 6 m/s at most for the velocity that a turn
 * keeps. A character on a mount will need larger limits.
 *
 *   Measured (largest value of the session)            Limit
 *   one tick, horizontal: 0.304 m (9.0 m/s, a dodge)   kPeakSpeed 20 m/s (0.667 m in one tick)
 *   one window of 1 s: 7.50 m (a dodge in a sprint)    kWindowSpeed 8 m/s and kWindowMargin 2 m (10 m)
 *   one tick, rise: 0.218 m (6.5 m/s, a jump)          kRiseSpeed 20 m/s (0.667 m in one tick)
 *   one tick, fall: 1.796 m (53.9 m/s after 150 m)     kFallSpeed 120 m/s (4 m in one tick)
 */
namespace position_limits {
/** Simulation steps of the client in one second. */
inline constexpr float kTicksInSecond = 30.0F;
/** Largest horizontal speed between two reports (meters in a second). */
inline constexpr float kPeakSpeed = 20.0F;
/** Largest horizontal speed over the time window (meters in a second), and the margin of one window (meters). */
inline constexpr float kWindowSpeed = 8.0F;
inline constexpr float kWindowMargin = 2.0F;
/**
 * Largest speed of the height between two reports (meters in a second). A fall has no speed
 * limit in the client (9.8 m/s more in each second): the limit is the speed after a fall of
 * 730 m.
 */
inline constexpr float kRiseSpeed = 20.0F;
inline constexpr float kFallSpeed = 120.0F;
/** Length of the time window. */
inline constexpr std::chrono::milliseconds kWindow{1000};
/**
 * The first report of a session is accepted when it is this near to the spawn position
 * (horizontal, meters): the client can move its character out of an object at the spawn.
 */
inline constexpr float kSpawnRange = 5.0F;
/**
 * The same for the height (meters, up and down): the spawn is some distance above the saved
 * height (kSpawnLift), and the character then falls to the ground. A first report that is
 * farther gets the rise or the fall that the time from the first report permits.
 */
inline constexpr float kSpawnHeightRange = 5.0F;
/**
 * The time between two reports is the time of their input numbers, but not less than the
 * time of the world clock (a client with a slow picture makes larger steps), and not more
 * than that time plus this value (reports can come together after a delay; a client must not
 * get more time from large input numbers).
 */
inline constexpr std::chrono::milliseconds kClockSlack{250};
/**
 * A report is newer when its input number is 1 to this value above the last one (16 bits,
 * with wrap).
 */
inline constexpr std::uint16_t kNewerRange = 0x7FFF;
/**
 * When each report of this time had an input number that is not newer, the input numbers of
 * the client started again: the session takes the new sequence.
 */
inline constexpr std::chrono::milliseconds kSequenceRestart{2000};
} // namespace position_limits

/**
 * The values of one client for the check of its position reports. A session owns one, and it
 * gives it to the world with each report.
 */
class PositionTrack {
public:
    /** The largest values of the accepted reports of the session. */
    struct Largest {
        /** Horizontal distance (meters) between two accepted reports whose input numbers differ by 1, and the newer number. */
        float tick{0.0F};
        std::uint16_t tickInput{0};
        /** Horizontal speed (meters in a second) between two accepted reports. */
        float stepSpeed{0.0F};
        /** Horizontal distance (meters) from the start of the time window, and the time of that window (seconds). */
        float window{0.0F};
        float windowSeconds{0.0F};
        /** Rise and fall (meters) between two accepted reports whose input numbers differ by 1. */
        float rise{0.0F};
        float fall{0.0F};
        /** Speed of the height (meters in a second) between two accepted reports: up, and down. */
        float riseSpeed{0.0F};
        float fallSpeed{0.0F};
    };

    /**
     * Checks one report. An accepted report goes into the time window.
     * @param known The position that the entity has now: the reference of the first report.
     */
    [[nodiscard]] PositionVerdict judge(const PositionReport& report,
                                        const state::WorldPosition& known,
                                        WorldClock::time_point now);

    /** @return The number of reports with this verdict. */
    [[nodiscard]] std::uint64_t count(PositionVerdict verdict) const {
        return m_counts[static_cast<std::size_t>(verdict)];
    }
    /** @return The number of reports that the check refused for their numbers or for their speed. */
    [[nodiscard]] std::uint64_t refused() const {
        return count(PositionVerdict::notFinite) + count(PositionVerdict::heightLimit)
               + count(PositionVerdict::outsideWorld) + count(PositionVerdict::tooFast);
    }
    /** @return The number of times that the input numbers of the client started again. */
    [[nodiscard]] std::uint32_t restarts() const {
        return m_restarts;
    }
    [[nodiscard]] const Largest& largest() const {
        return m_largest;
    }

private:
    struct Sample {
        std::uint16_t input{0};
        WorldClock::time_point time{};
        state::WorldPosition position;
        /**
         * The sum of the rises and the sum of the falls of the accepted reports of the session
         * up to this one (meters). The difference of two samples is the total of the reports
         * between them. Double precision: the sums of a long session stay exact to less than
         * one step of the height on the wire.
         */
        double totalRise{0.0};
        double totalFall{0.0};
    };

    PositionVerdict counted(PositionVerdict verdict) {
        ++m_counts[static_cast<std::size_t>(verdict)];
        return verdict;
    }
    /** @return The time from a sample to a report, in seconds (position_limits::kClockSlack). */
    [[nodiscard]] static float seconds_from(const Sample& sample, std::uint16_t input, WorldClock::time_point now);

    bool m_hasInput{false};
    std::uint16_t m_lastInput{0};
    /** Time of the last report whose input number was newer. */
    WorldClock::time_point m_lastNewer{};
    std::uint32_t m_restarts{0};
    /** Time of the first report that got to the speed check. The reference time until a report is accepted. */
    std::optional<WorldClock::time_point> m_firstReport;
    /** The accepted reports of the time window, oldest first. The newest one stays also when it is older. */
    std::deque<Sample> m_window;
    std::array<std::uint64_t, kPositionVerdictCount> m_counts{};
    Largest m_largest;
};

} // namespace soulwarden::backend::world
