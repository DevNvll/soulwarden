#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

#include "world/world_clock.h"

namespace soulwarden::backend::tests {

/** A world clock that changes only when the test changes it. A second thread can read it. */
class ManualClock final : public world::WorldClock {
public:
    /** The start value is not zero: a time point of zero means "never" in the backend. */
    explicit ManualClock(std::chrono::nanoseconds start = std::chrono::seconds(1'000'000))
        : m_nanoseconds(start.count()) {}

    [[nodiscard]] time_point now() const override {
        return time_point(std::chrono::nanoseconds(m_nanoseconds.load()));
    }

    void advance(std::chrono::nanoseconds duration) {
        m_nanoseconds += duration.count();
    }

    /** The made-up Unix time of the time 0 of this clock (the wall times of the tests): 2026-09-21. */
    static constexpr std::int64_t kWallOffset = 1'790'000'000'000'000'000;

protected:
    [[nodiscard]] std::int64_t wall_offset_ns() const override {
        return kWallOffset;
    }

private:
    std::atomic<std::int64_t> m_nanoseconds;
};

} // namespace soulwarden::backend::tests
