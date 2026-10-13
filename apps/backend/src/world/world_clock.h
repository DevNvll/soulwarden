#pragma once

#include <chrono>
#include <cstdint>

// The clock of the world. All timers of the game server and of the world read this clock, and
// no code of a session reads the clock of the computer. A test gives the backend a clock that
// the test sets.

namespace soulwarden::backend::world {

class WorldClock {
public:
    using time_point = std::chrono::steady_clock::time_point;

    virtual ~WorldClock() = default;

    /** Monotonic time for timers. */
    [[nodiscard]] virtual time_point now() const = 0;

    /**
     * Server time in nanoseconds: the value of the registration reply and of each time anchor
     * of a movement state. The client computes a clock offset from it. All sessions have the
     * same time base.
     */
    [[nodiscard]] std::uint64_t server_time_ns() const {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(now().time_since_epoch()).count());
    }
};

/** The monotonic clock of the computer. */
class SystemClock final : public WorldClock {
public:
    [[nodiscard]] time_point now() const override {
        return std::chrono::steady_clock::now();
    }
};

} // namespace soulwarden::backend::world
