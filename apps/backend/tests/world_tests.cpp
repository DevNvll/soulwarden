// Cases for the world object: the task queue, the timers, the guard for exceptions, and the
// thread of a world with no game server listener.

#include <atomic>
#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/manual_clock.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "world/world.h"

namespace soulwarden::backend::tests {
namespace {

using std::chrono::milliseconds;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";

/** W1: a task from a second thread runs in the world thread, and its caller gets the value. */
void task_from_second_thread(const Options&) {
    ServerHost host;
    host.tick(); // The thread of this case is the world thread: the game server ticks the world.
    const std::thread::id worldThread = std::this_thread::get_id();
    std::thread::id taskThread;
    std::thread::id callerThread;
    std::optional<int> value;
    std::atomic<bool> done{false};
    std::thread caller([&] {
        callerThread = std::this_thread::get_id();
        value = host.world.call<int>(
            [&](world::World& world) {
                taskThread = std::this_thread::get_id();
                return world.in_world_thread() ? 42 : -1;
            },
            milliseconds(5000));
        done = true;
    });
    int ticks = 0;
    while (!done && ticks < 5000) {
        host.tick();
        ++ticks;
        std::this_thread::sleep_for(milliseconds(1));
    }
    caller.join();
    check(value.has_value() && *value == 42, "the caller in the second thread gets the value of the task (42)");
    check(taskThread == worldThread && callerThread != worldThread, "the task ran in the world thread, not in the thread of the caller");

    int posted = 0;
    std::thread poster([&] { host.world.post([&](world::World&) { ++posted; }); });
    poster.join();
    check(posted == 0, "a posted task does not run in the thread that posts it");
    host.tick();
    check(posted == 1, "the next tick of the game server runs the posted task one time");
    host.tick();
    check(posted == 1, "a task runs one time only");
    check(host.world.call<int>([](world::World&) { return 7; }, milliseconds(0)) == 7, "a call in the world thread runs at once");
}

/** W2: a timer runs at its time on the world clock. */
void timers(const Options&) {
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    world::World world(clock, store, tables);
    std::vector<std::string> order;
    world.after(milliseconds(500), [&](world::World&) { order.push_back("500"); });
    world.after(milliseconds(200), [&](world::World&) { order.push_back("200"); });
    world.after(milliseconds(0), [&](world::World& inner) {
        order.push_back("0");
        inner.after(milliseconds(0), [&](world::World&) { order.push_back("0b"); });
    });
    check(order.empty(), "no timer runs before a tick");
    world.tick();
    check(order == std::vector<std::string>{"0"}, "a timer with no delay runs in the next tick; a timer that it adds waits for one more tick");
    world.tick();
    check(order == std::vector<std::string>{"0", "0b"}, "the timer that a timer added runs in the tick after it");
    clock.advance(milliseconds(199));
    world.tick();
    check(order.size() == 2, "after 199 ms the timer of 200 ms did not run");
    clock.advance(milliseconds(1));
    world.tick();
    check(order.size() == 3 && order.back() == "200", "at 200 ms the timer of 200 ms runs");
    clock.advance(milliseconds(10'000));
    world.tick();
    world.tick();
    check(order == std::vector<std::string>{"0", "0b", "200", "500"}, "each timer runs one time, in the sequence of the times");
    // Real time does not move a timer: only the world clock does.
    world.after(milliseconds(20), [&](world::World&) { order.push_back("late"); });
    std::this_thread::sleep_for(milliseconds(40));
    world.tick();
    check(order.size() == 4, "40 ms of real time with no change of the world clock: the timer of 20 ms does not run");
}

/** W3: an exception in a tick does not stop the process. */
void exception_in_tick(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, 50.0F});
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());

    int after = 0;
    host.world.post([](world::World&) { throw std::runtime_error("task fault"); });
    host.world.post([&](world::World&) { ++after; });
    host.world.after(milliseconds(0), [](world::World&) { throw std::runtime_error("timer fault"); });
    host.world.after(milliseconds(0), [&](world::World&) { ++after; });
    host.steps(1, {&a, &b});
    check(after == 2, "a task and a timer that throw do not stop the tick: the next task and the next timer run");
    check(log_count("world: a task stopped with an exception: task fault") == 1
              && log_count("world: a timer stopped with an exception: timer fault") == 1,
          "the log has one line for each exception");
    check(host.server->session_count() == 2, "the two sessions are alive after the exceptions of the world tick");

    // The tick of one session throws: the transport fails for the peer of A.
    host.datagram(a.peer, a.input_at(1, 120.0F, 200.0F, 50.0F));
    host.datagram(a.peer, a.input_at(31, 123.0F, 200.0F, 50.0F));
    host.transport.throwing.insert(a.peer);
    const std::size_t bMessages = b.got.size();
    host.steps(30, {&b});
    check(log_count("rep: the tick of the session stopped with an exception: test transport: send failed [s1 aaaaaaaa]") == 1,
          "an exception in the tick of session 1 goes to the log with the mark of the session");
    check(log_count("rep: session ended, cause: error") == 1 && host.server->session_count() == 1, "session 1 ends with the cause 'error'");
    const auto row = saved_position(host.store, kCharacterA);
    check(row && std::abs(row->x - 123.0F) < 0.05F, "the end of session 1 saves its last position (123, 200)");
    check(b.got.size() > bMessages, "session 2 continues to get its movement states: the process did not stop");
}

/** W4: an exception in a message handler does not stop the process. */
void exception_in_message_handler(const Options&) {
    bool fault = false;
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.messageTap = [&](std::uint32_t session, std::uint8_t, std::span<const std::uint8_t>) {
        if (fault && session == 1) {
            throw std::runtime_error("handler fault");
        }
    };
    ServerHost host(options);
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, 50.0F});
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    host.datagram(a.peer, a.input_at(1, 120.0F, 200.0F, 50.0F));
    host.steps(100, {&a, &b});
    host.datagram(a.peer, a.input_at(31, 123.0F, 200.0F, 50.0F));
    fault = true;
    host.datagram(a.peer, a.other_message(4242));
    check(log_count("rep: a message handler stopped with an exception: handler fault [s1 aaaaaaaa]") == 1,
          "an exception in a message handler of session 1 goes to the log with the mark of the session");
    check(log_count("rep: session ended, cause: error") == 1, "session 1 ends with the cause 'error'");
    const auto row = saved_position(host.store, kCharacterA);
    check(row && std::abs(row->x - 123.0F) < 0.05F, "the end of session 1 saves its last position (123, 200)");
    const std::size_t bMessages = b.got.size();
    host.datagram(b.peer, b.other_message(4242));
    host.steps(30, {&b});
    check(host.server->session_count() == 1 && b.got.size() > bMessages,
          "session 2 is alive and gets its movement states: the process did not stop");
    check(log_count("rep: message type 4242 (4 bytes) has no handler [s2 bbbbbbbb]") == 1,
          "a message of session 2 goes to its session after the exception");
}

/** W5: with no game server listener, the world has its own thread for the task queue. */
void world_thread(const Options&) {
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    world::World world(clock, store, tables);
    check(!world.call<int>([](world::World&) { return 1; }, milliseconds(50)).has_value(),
          "a world with no thread: a call of a different thread gets no value after its time limit");
    check(log_count("world: a task of a different thread did not start in 50 ms: it was cancelled") == 1,
          "the log has a line for the call with no value");
    world.start_thread(milliseconds(1));
    std::thread::id taskThread;
    const std::optional<int> value = world.call<int>(
        [&](world::World& inner) {
            taskThread = std::this_thread::get_id();
            return inner.in_world_thread() ? 42 : -1;
        },
        milliseconds(5000));
    check(value.has_value() && *value == 42, "after start_thread the caller gets the value of the task (42)");
    check(taskThread != std::this_thread::get_id() && taskThread != std::thread::id{}, "the task ran in the thread of the world");
    // The task that waited for the first call ran too, one time.
    std::atomic<int> timerRuns{0};
    world.post([&](world::World& inner) { inner.after(milliseconds(300), [&](world::World&) { ++timerRuns; }); });
    check(world.call<int>([](world::World&) { return 0; }, milliseconds(5000)).has_value(), "the task queue continues");
    std::this_thread::sleep_for(milliseconds(30));
    check(timerRuns == 0, "a timer of the world thread waits for the world clock");
    clock.advance(milliseconds(300));
    for (int attempt = 0; attempt < 2000 && timerRuns == 0; ++attempt) {
        std::this_thread::sleep_for(milliseconds(1));
    }
    check(timerRuns == 1, "the thread of the world runs the timer when the world clock is at its time");
    const std::optional<int> failed = world.call<int>(
        [](world::World&) -> int { throw std::runtime_error("call fault"); }, milliseconds(5000));
    check(!failed.has_value() && world.call<int>([](world::World&) { return 5; }, milliseconds(5000)) == 5,
          "a task that throws gives its caller no value, and the thread of the world continues");
    world.stop_thread();
    check(!world.call<int>([](world::World&) { return 1; }, milliseconds(30)).has_value(), "after stop_thread no task runs");
}

/**
 * W6: a task of the world thread stops that thread. Review finding 1 of stage 0.6a: the thread
 * then waited for itself, and the destructor of the world ended the process.
 */
void task_stops_world_thread(const Options&) {
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    {
        world::World world(clock, store, tables);
        world.start_thread(milliseconds(1));
        const std::optional<int> value = world.call<int>(
            [](world::World& inner) {
                inner.stop_thread();
                return 1;
            },
            milliseconds(5000));
        check(value.has_value() && *value == 1, "a task of the world thread asks for the stop, and its caller gets the value");
        check(log_count("stopped with an exception") == 0, "the stop from the task gives no exception");
        world.stop_thread();
        check(!world.call<int>([](world::World&) { return 1; }, milliseconds(30)).has_value(),
              "a stop from a different thread waits for the thread: after it no task runs");
        world.start_thread(milliseconds(1));
        check(world.call<int>([](world::World&) { return 7; }, milliseconds(5000)) == 7,
              "a new thread of the world starts after the stop and runs a task");
        const std::optional<int> second = world.call<int>(
            [](world::World& inner) {
                inner.stop_thread();
                return 2;
            },
            milliseconds(5000));
        check(second.has_value() && *second == 2, "a task stops the second thread of the world");
        // The destructor of the world comes next, with a thread that a task stopped.
    }
    check(true, "the destructor of the world waits for a thread that a task stopped: the process continues");
}

/**
 * W7: after the stop of the world thread, a new thread cannot run a task directly. Review
 * finding 2 of stage 0.6a: the world kept the id of its thread, and a new thread can get the
 * id of a thread that ended.
 */
void stopped_world_new_thread(const Options&) {
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    world::World world(clock, store, tables);
    world.start_thread(milliseconds(1));
    const auto owner =
        world.call<std::thread::id>([](world::World&) { return std::this_thread::get_id(); }, milliseconds(5000));
    world.stop_thread();
    bool ran = false;
    bool reused = false;
    std::optional<int> result;
    std::thread caller([&] {
        reused = owner && std::this_thread::get_id() == *owner;
        result = world.call<int>(
            [&](world::World&) {
                ran = true;
                return 42;
            },
            milliseconds(30));
    });
    caller.join();
    note(std::string("the new thread has the id of the thread of the world that ended: ") + (reused ? "yes" : "no"));
    check(!ran && !result, "a new thread cannot run a task of the world after stop_thread");
    check(!world.in_world_thread(), "after the stop the world has no world thread");

    // The same for a thread that ticked the world and ended (the listener thread of the game
    // server): its owner tells the world.
    world::World second(clock, store, tables);
    std::thread ticker([&] { second.tick(); });
    ticker.join();
    second.forget_thread();
    bool secondRan = false;
    std::thread late([&] {
        const auto value = second.call<int>(
            [&](world::World&) {
                secondRan = true;
                return 1;
            },
            milliseconds(30));
        (void)value;
    });
    late.join();
    check(!secondRan, "after forget_thread, a new thread cannot run a task of a world whose listener thread ended");
}

} // namespace

std::vector<Case> world_cases() {
    return {
        {"W1", "world: a task from a second thread runs in the world thread, and its caller gets the value", task_from_second_thread},
        {"W2", "world: a timer runs at its time on the world clock", timers},
        {"W3", "world: an exception in a tick does not stop the process", exception_in_tick},
        {"W4", "world: an exception in a message handler does not stop the process", exception_in_message_handler},
        {"W5", "world: with no listener, the thread of the world runs the task queue and the timers", world_thread},
        {"W6", "world: a task of the world thread can stop that thread (review finding 1 of stage 0.6a)", task_stops_world_thread},
        {"W7", "world: after the stop of its thread, a new thread runs no task directly (review finding 2)", stopped_world_new_thread},
    };
}

} // namespace soulwarden::backend::tests
