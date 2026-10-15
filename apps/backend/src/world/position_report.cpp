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
    case PositionVerdict::dead:
        return "the character is dead";
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
                                     WorldClock::time_point now,
                                     float speedFactor,
                                     MotionAllowance allowance) {
    using namespace position_limits;
    const float windowSpeed = kWindowSpeed * std::max(speedFactor, 1.0F);
    // The envelope of a dash of an accepted cast (stage 3.3b-S4): more distance in the window, and a faster step.
    const float extra = std::isfinite(allowance.distance) ? std::max(allowance.distance, 0.0F) : 0.0F;
    const float peakSpeed = std::isfinite(allowance.peak) ? std::max(kPeakSpeed, allowance.peak) : kPeakSpeed;
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
    // The time that the client has for a rise and for a fall after this report (the two
    // budgets of the height), and the client time of this report.
    double riseTime = 0.0;
    double fallTime = 0.0;
    double clientTime = 0.0;
    if (m_window.empty()) {
        // No report was accepted: the reference is the position that the world has (the spawn
        // position), from the time of the first report. The loading screen of the client is
        // not a time in which the character moves.
        if (!m_firstReport) {
            m_firstReport = now;
        }
        const float waited = seconds(now - *m_firstReport);
        const float allowed = std::max(kSpawnRange, windowSpeed * waited + kWindowMargin);
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
        // The client time starts at the first accepted report, and each budget of the height
        // starts with the delay value.
        m_clientStart = now;
        riseTime = exact_seconds(kClockSlack);
        fallTime = exact_seconds(kClockSlack);
    } else {
        const Sample& last = m_window.back();
        const float step = horizontal_distance(position, last.position);
        const float stepSeconds = seconds_from(last, report.input, now);
        if (step > peakSpeed * stepSeconds) {
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
        if (distance > windowSpeed * windowSeconds + kWindowMargin + extra) {
            return counted(PositionVerdict::tooFast);
        }
        // The budget of the dash: one cast gives its extra distance one time (finding F1 of the review of stage 3.3b-S4).
        if (allowance.from && std::isfinite(allowance.since)
            && horizontal_distance(position, *allowance.from) > windowSpeed * std::max(allowance.since, 0.0F) + kWindowMargin + extra) {
            return counted(PositionVerdict::tooFast);
        }
        // The height over more than one report. The rises of the accepted reports have one
        // budget of time, and the falls have a second one: an accepted report takes the time of
        // its rise at the rise speed from the first one, and the time of its fall at the fall
        // speed from the second one. A report that needs more time than its budget has is
        // refused. The time between two reports (seconds_from) is not sufficient for this: it
        // comes from the input numbers too, and each report gets the delay value again, thus a
        // client with larger input number steps could rise without a limit.
        //
        // The world clock fills the two budgets, and it does this one time: each budget gets
        // the time from the last accepted report to this one. The first accepted report gives
        // each budget the delay value. Thus the rises of a session are never more than the
        // rise speed times the time of the world clock from the first accepted report plus the
        // delay value, for each sequence of input numbers. The rule before this one compared
        // the totals of a time window with the client time of the window, or with its time of
        // the world clock when that was longer: a client with slow input numbers got its rise
        // for the time of the world clock, and then a second rise for the same time when its
        // input numbers went on at one clock value (code review of stage 3.4c, finding C2).
        //
        // Two budgets, not one: a path that goes up and down has its rises and its falls in
        // the same time. The difference of two heights is not sufficient: a client that goes
        // up and down in turns stays near its first height (400 m up and 400 m down at one
        // clock value).
        //
        // The limit of a budget. A budget keeps the time that the client did not use, but not
        // more than the time of one window, or than the time that the client has in store when
        // that is more. The time in store is the time of the world clock that the input numbers
        // of the client did not cover (the top of position_report.h): reports that come late
        // and together have the times of their input numbers, and the time of the world clock
        // before them covers these times. It is 5.25 s at most (the backlog value and the delay
        // value), and it goes down with each input number that the client sends.
        //
        // Usual movement stays far below the two limits, also with its changes of direction.
        // The sum of the rises of a character in a time is not more than its largest rise speed
        // times that time, for each path: that speed is 6.5 m/s (the start of a jump, see
        // position_limits), and the limit is 20 m/s. A character that jumps 1.2 m each 0.8 s on
        // a slope goes less than 4 m up in 1 s, and the budget gets 20 m in that time. The
        // fastest fall of the live session is 54 m in 1 s, and the budget gets 120 m.
        const double ticks = static_cast<double>(static_cast<std::uint16_t>(report.input - last.input))
                             / static_cast<double>(kTicksInSecond);
        const double highest = exact_seconds(now - m_clientStart) + exact_seconds(kClockSlack);
        clientTime = std::clamp(last.clientTime + ticks, highest - exact_seconds(kReportBacklog), highest);
        const double store =
            std::min(highest - last.clientTime, exact_seconds(kReportBacklog) + exact_seconds(kClockSlack));
        const double limit = std::max(exact_seconds(kWindow), store);
        const double filled = exact_seconds(now - last.time);
        riseTime = std::min(last.riseTime + filled, limit);
        fallTime = std::min(last.fallTime + filled, limit);
        const double riseNeeds = static_cast<double>(std::max(rise, 0.0F)) / static_cast<double>(kRiseSpeed);
        const double fallNeeds = static_cast<double>(std::max(-rise, 0.0F)) / static_cast<double>(kFallSpeed);
        if (riseNeeds > riseTime || fallNeeds > fallTime) {
            return counted(PositionVerdict::tooFast);
        }
        riseTime -= riseNeeds;
        fallTime -= fallNeeds;
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
    m_window.push_back(Sample{report.input, now, position, riseTime, fallTime, clientTime});
    return counted(PositionVerdict::accepted);
}

void PositionTrack::set_client_speed(float factor, WorldClock::time_point now) {
    if (!std::isfinite(factor) || factor <= 0.0F) {
        factor = 1.0F;
    }
    if (factor == m_clientSpeed) {
        return;
    }
    // The value before the change still counts for one window from this change, and for no longer: each older value keeps
    // its own end (code review of stage 3.3b-S2a, finding F1: a sequence of lower tables kept an old haste for all time).
    while (!m_previousSpeeds.empty() && m_previousSpeeds.front().second <= now) {
        m_previousSpeeds.pop_front();
    }
    const WorldClock::time_point ends = now + position_limits::kWindow;
    // An older value that is not larger than this one ends before it: it adds nothing to the limit.
    while (!m_previousSpeeds.empty() && m_previousSpeeds.back().first <= m_clientSpeed) {
        m_previousSpeeds.pop_back();
    }
    m_previousSpeeds.emplace_back(m_clientSpeed, ends);
    m_clientSpeed = factor;
}

float PositionTrack::client_speed(WorldClock::time_point now) const {
    float factor = m_clientSpeed;
    for (const auto& [value, ends] : m_previousSpeeds) {
        if (now < ends) {
            factor = std::max(factor, value);
        }
    }
    return factor;
}

} // namespace soulwarden::backend::world
