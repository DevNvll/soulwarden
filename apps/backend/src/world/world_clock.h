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

    /**
     * The wall clock at a time of the world clock: Unix time in nanoseconds (stage 3.3b-S1). The client computes its
     * synced wall clock as wallStart + (serverNow - serverStart) from one pair of the Player state (group 1, fields 6
     * and 7; static proof 0x6830A40, live check L1 of stage 3.3b-S1), and the end of a status effect is a time of that
     * wall clock. The world keeps each timer on the world clock and converts only the wire values here.
     */
    [[nodiscard]] std::uint64_t wall_time_ns(time_point time) const {
        const auto steady = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
        return static_cast<std::uint64_t>(wall_offset_ns() + steady);
    }

protected:
    /** The Unix time in nanoseconds at the time 0 of the world clock: one value for the life of the clock. */
    [[nodiscard]] virtual std::int64_t wall_offset_ns() const = 0;
};

/** The monotonic clock of the computer. */
class SystemClock final : public WorldClock {
public:
    SystemClock()
        : m_wallOffset(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count()
                       - std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) {}

    [[nodiscard]] time_point now() const override {
        return std::chrono::steady_clock::now();
    }

protected:
    [[nodiscard]] std::int64_t wall_offset_ns() const override {
        return m_wallOffset;
    }

private:
    /** One sample of the two clocks at the start: a change of the clock of the computer later does not move the wall times. */
    std::int64_t m_wallOffset;
};

} // namespace soulwarden::backend::world
