#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

// The real time of a part of the work of the backend: a measurement for the log, for the
// development tools, and for the tests of the load. It is not a time of the world: no rule of
// the world reads it (the rules read the world clock, world/world_clock.h).

namespace soulwarden::backend::world {

struct LoadMeter {
    std::uint64_t count{0};
    std::chrono::nanoseconds total{0};
    std::chrono::nanoseconds largest{0};

    void add(std::chrono::nanoseconds time) {
        ++count;
        total += time;
        largest = std::max(largest, time);
    }
};

/** Adds the real time from its start to its end to a meter. */
class LoadTimer {
public:
    explicit LoadTimer(LoadMeter& meter) : m_meter(meter), m_started(std::chrono::steady_clock::now()) {}
    ~LoadTimer() {
        m_meter.add(std::chrono::steady_clock::now() - m_started);
    }
    LoadTimer(const LoadTimer&) = delete;
    LoadTimer& operator=(const LoadTimer&) = delete;

private:
    LoadMeter& m_meter;
    std::chrono::steady_clock::time_point m_started;
};

} // namespace soulwarden::backend::world
