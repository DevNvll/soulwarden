#include "world/position_report.h"

#include <algorithm>
#include <cmath>

#include "world/position_index.h"

namespace soulwarden::backend::world {
namespace {

/** The two limit values of the height on the wire. */
constexpr std::uint16_t kHeightLow = 0x0000;
constexpr std::uint16_t kHeightHigh = 0xFFFF;

float seconds(std::chrono::nanoseconds duration) {
    return std::chrono::duration<float>(duration).count();
}

double exact_seconds(std::chrono::nanoseconds duration) {
    return std::chrono::duration<double>(duration).count();
}

} // namespace

const char* position_verdict_text(PositionVerdict verdict) {
    switch (verdict) {
    case PositionVerdict::accepted:
        return "accepted";
    case PositionVerdict::outOfSequence:
        return "not in sequence";
    case PositionVerdict::notFinite:
        return "a number is not finite";
    case PositionVerdict::heightLimit:
        return "the height is a limit value of the range";
    case PositionVerdict::outsideWorld:
        return "outside the limits of the world";
    case PositionVerdict::tooFast:
        return "too fast";
    case PositionVerdict::noEntity:
        return "no entity";
    case PositionVerdict::beforeTravel:
        return "not near the place of the travel";
    }
    return "not known";
}

void PositionTrack::traveled() {
    m_window.clear();
    m_firstReport.reset();
    m_traveled = true;
}

float PositionTrack::seconds_from(const Sample& sample, std::uint16_t input, WorldClock::time_point now) {
    const float clock = seconds(now - sample.time);
    const float ticks = static_cast<float>(static_cast<std::uint16_t>(input - sample.input))
                        / position_limits::kTicksInSecond;
    return std::min(std::max(ticks, clock), clock + seconds(position_limits::kClockSlack));
}

PositionVerdict PositionTrack::judge(const PositionReport& report,
                                     const state::WorldPosition& known,
                                     WorldClock::time_point now) {
    using namespace position_limits;
    // 1. Sequence.
    if (m_hasInput) {
        const auto ahead = static_cast<std::uint16_t>(report.input - m_lastInput);
        if (ahead == 0 || ahead > kNewerRange) {
            if (now - m_lastNewer < kSequenceRestart) {
                return counted(PositionVerdict::outOfSequence);
            }
            ++m_restarts;
        }
    }
    m_hasInput = true;
    m_lastInput = report.input;
    m_lastNewer = now;

    // 2. Numbers.
    const state::WorldPosition& position = report.position;
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
        return counted(PositionVerdict::notFinite);
    }
    if (report.height == kHeightLow || report.height == kHeightHigh) {
        return counted(PositionVerdict::heightLimit);
    }
    if (!valid_position(position)) {
        return counted(PositionVerdict::outsideWorld);
    }

    // 3. Speed.
    // The totals of the rises and of the falls of the accepted reports, with this report, and
    // the client time of this report.
    double totalRise = 0.0;
    double totalFall = 0.0;
    double clientTime = 0.0;
    if (m_window.empty()) {
        // No report was accepted: the reference is the position that the world has (the spawn
        // position), from the time of the first report. The loading screen of the client is
        // not a time in which the character moves.
        if (!m_firstReport) {
            m_firstReport = now;
        }
        const float waited = seconds(now - *m_firstReport);
        const float allowed = std::max(kSpawnRange, kWindowSpeed * waited + kWindowMargin);
        // After a travel, a report that is far is a report from the place before it.
        const PositionVerdict far = m_traveled ? PositionVerdict::beforeTravel : PositionVerdict::tooFast;
        if (horizontal_distance(position, known) > allowed) {
            return counted(far);
        }
        // The height of the first report has the same reference. Without this check the first
        // report of a session could give its character each height of the range.
        const float rise = position.z - known.z;
        if (rise > kSpawnHeightRange + kRiseSpeed * waited || -rise > kSpawnHeightRange + kFallSpeed * waited) {
            return counted(far);
        }
        m_traveled = false;
        // The client time starts at the first accepted report.
        m_clientStart = now;
    } else {
        const Sample& last = m_window.back();
        const float step = horizontal_distance(position, last.position);
        const float stepSeconds = seconds_from(last, report.input, now);
        if (step > kPeakSpeed * stepSeconds) {
            return counted(PositionVerdict::tooFast);
        }
        const float rise = position.z - last.position.z;
        if (rise > kRiseSpeed * stepSeconds || -rise > kFallSpeed * stepSeconds) {
            return counted(PositionVerdict::tooFast);
        }
        // The start of the time window: the oldest accepted report that is not older than
        // the window. With no such report: the last accepted one, with its longer time.
        const Sample* start = &last;
        for (const Sample& sample : m_window) {
            if (now - sample.time <= kWindow) {
                start = &sample;
                break;
            }
        }
        const float distance = horizontal_distance(position, start->position);
        const float windowSeconds = seconds_from(*start, report.input, now);
        if (distance > kWindowSpeed * windowSeconds + kWindowMargin) {
            return counted(PositionVerdict::tooFast);
        }
        // The height over a time window: the total rise and the total fall of the window have
        // a limit for the time of the window. That time is the client time (the top of
        // position_report.h): the time of the input numbers, which is not more than the time of
        // the world clock from the first accepted report plus the delay value, one time for
        // the session. The time between two reports comes from their input numbers too
        // (seconds_from), and each report would get the delay value again: a client with larger
        // input number steps could rise without a limit. The time of arrival of a report is not
        // the time of its movement: reports that come late and together have the times of their
        // input numbers, and the time of the world clock before them covers these times.
        //
        // The two totals are sums: each rise of the accepted reports of the window and of this
        // report, and each fall. The difference of two heights is not sufficient: a client that
        // goes up and down in turns stays near its first height, and each change of its
        // direction would give it the limit again (400 m up and 400 m down at one clock value).
        //
        // Usual movement stays far below the two limits, also with its changes of direction.
        // The sum of the rises of a character in a time is not more than its largest rise speed
        // times that time, for each path: that speed is 6.5 m/s (the start of a jump, see
        // position_limits), and the limit is 20 m/s. A character that jumps 1.2 m each 0.8 s on
        // a slope goes less than 4 m up in a window of 1 s, and the limit of that window is
        // 20 m. The fastest fall of the live session is 54 m in 1 s, and the limit is 120 m.
        const double ticks = static_cast<double>(static_cast<std::uint16_t>(report.input - last.input))
                             / static_cast<double>(kTicksInSecond);
        const double highest = exact_seconds(now - m_clientStart) + exact_seconds(kClockSlack);
        clientTime = std::clamp(last.clientTime + ticks, highest - exact_seconds(kReportBacklog), highest);
        // The start of the window of the client time: the oldest accepted report that is not
        // older than the window in client time. With no such report: the last accepted one.
        const Sample* since = &last;
        for (const Sample& sample : m_window) {
            if (clientTime - sample.clientTime <= exact_seconds(kWindow)) {
                since = &sample;
                break;
            }
        }
        // The time of the window is not less than its time of the world clock: a client whose
        // input numbers go slower than the world clock has the time that passed.
        const float heightSeconds =
            std::max(static_cast<float>(clientTime - since->clientTime), seconds(now - since->time));
        totalRise = last.totalRise + static_cast<double>(std::max(rise, 0.0F));
        totalFall = last.totalFall + static_cast<double>(std::max(-rise, 0.0F));
        const float windowRise = static_cast<float>(totalRise - since->totalRise);
        const float windowFall = static_cast<float>(totalFall - since->totalFall);
        if (windowRise > kRiseSpeed * heightSeconds || windowFall > kFallSpeed * heightSeconds) {
            return counted(PositionVerdict::tooFast);
        }
        if (static_cast<std::uint16_t>(report.input - last.input) == 1) {
            if (step > m_largest.tick) {
                m_largest.tick = step;
                m_largest.tickInput = report.input;
            }
            m_largest.rise = std::max(m_largest.rise, rise);
            m_largest.fall = std::max(m_largest.fall, -rise);
        }
        if (stepSeconds > 0.0F) {
            m_largest.stepSpeed = std::max(m_largest.stepSpeed, step / stepSeconds);
            m_largest.riseSpeed = std::max(m_largest.riseSpeed, rise / stepSeconds);
            m_largest.fallSpeed = std::max(m_largest.fallSpeed, -rise / stepSeconds);
        }
        if (distance > m_largest.window) {
            m_largest.window = distance;
            m_largest.windowSeconds = windowSeconds;
        }
    }

    while (m_window.size() > 1 && now - m_window.front().time > kWindow) {
        m_window.pop_front();
    }
    m_window.push_back(Sample{report.input, now, position, totalRise, totalFall, clientTime});
    return counted(PositionVerdict::accepted);
}

} // namespace soulwarden::backend::world
