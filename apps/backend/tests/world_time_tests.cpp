// Tests of the world time (stage 2.2; docs/systems/world-time-and-weather.md, protocol notes 4.43, research world-time-stage.md 8).
//
//   WT1  the cycle and the wire: the 85-byte vector of the research and the offset-zero vector, each phase edge, the two rates, the
//        midnight wrap, the order of the edges, a nonzero anchor at early and large uptime, and the float model of the client
//   WT2  two sessions (and a third one): one world value with the ids of each own entity; a command reaches each client that asked;
//        a client that did not ask gets nothing until it asks; the clock correction 335 each 3 s from 3 s after the registration
//   WT3  requests that change nothing: a body of 15 or 17 bytes, a different route, the entity of a different character, a different
//        facet, an observed remote player, a request before the registration, a closing session; a repeated valid request
//   WT4  the store: a new world at noon, the save interval, the orderly-stop save, a restart with a different clock epoch (no old
//        anchor), a refused write of a command (nothing changes), a stored value that is not a position
//   WT5  the creature points with a time constraint: each phase mask, the default mask, an empty mask, a point that does not leave,
//        two observers, the phase removal with no reward, a death time across a phase that is not one of the point, a hold of a
//        command, a creature of a command, the cooldown flags, one creature for each point
//   WT6  the load: a world of 33,856 points with many far points with a time constraint and 50 players; a phase edge and a command
//        look only at the points near the players
//   WT7  the creature file of format 4: the masks, the flags, an older format (each phase), faults (all or nothing)
//   WT8  the check of the creature tool (check_creature_tool.py): the cases phase-*
//   WT9  the commands: the admin command and the development routes, wrong times, a character of a different account
//
// The accounts, the characters, the ids, the places, and the times are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/time_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/test_players.h"
#include "world/world_time.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using world::DayCycle;
using world::DayPhase;
namespace day_cycle = world::day_cycle;

constexpr const char* kCharacterA = "71a00000-0000-4000-8000-0000000000a1";
constexpr const char* kCharacterB = "71b00000-0000-4000-8000-0000000000b1";
constexpr const char* kCharacterC = "71c00000-0000-4000-8000-0000000000c1";
constexpr const char* kSteamIdC = "76561202255233103";
constexpr const char* kTokenA = "token-of-the-start-of-A-0123456789abcdef";
constexpr const char* kTokenB = "token-of-the-start-of-B-fedcba9876543210";
constexpr state::WorldPosition kPlaceA{100.0F, 200.0F, 30.0F};
constexpr state::WorldPosition kPlaceB{103.0F, 200.0F, 30.0F};
constexpr state::WorldPosition kPlaceC{5000.0F, 5000.0F, 30.0F};
constexpr std::uint64_t kSecond = 1'000'000'000ULL;

std::string hex(const std::vector<std::uint8_t>& bytes) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : bytes) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

std::uint64_t be64(const std::vector<std::uint8_t>& body, std::size_t at) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < 8; ++index) {
        value = (value << 8) | body[at + index];
    }
    return value;
}

std::uint32_t be32(const std::vector<std::uint8_t>& body, std::size_t at) {
    return (static_cast<std::uint32_t>(body[at]) << 24) | (static_cast<std::uint32_t>(body[at + 1]) << 16)
           | (static_cast<std::uint32_t>(body[at + 2]) << 8) | body[at + 3];
}

float bef32(const std::vector<std::uint8_t>& body, std::size_t at) {
    const std::uint32_t bits = be32(body, at);
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/** One 7036 of the server, read from its body (80 bytes). */
struct TimeReply {
    std::uint64_t target{0};
    std::uint64_t entity{0};
    world::TimeRecord record;
    std::uint32_t phase{0};
    std::size_t index{0};
};

std::vector<TimeReply> time_replies(const FakeClient& client, std::size_t from = 0) {
    std::vector<TimeReply> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        const Sent& sent = client.got[index];
        if (sent.type != server::rep::kTypeTimeAndPhase || sent.body.size() != 80) {
            continue;
        }
        TimeReply reply;
        reply.index = index;
        reply.target = be64(sent.body, 0);
        reply.entity = be64(sent.body, 8);
        reply.record.offset = bef32(sent.body, 24);
        reply.record.cycle.dawnTime = be32(sent.body, 28);
        reply.record.cycle.dawnDuration = be32(sent.body, 32);
        reply.record.cycle.duskTime = be32(sent.body, 36);
        reply.record.cycle.duskDuration = be32(sent.body, 40);
        reply.record.cycle.dayLength = be32(sent.body, 44);
        reply.record.cycle.nightLength = be32(sent.body, 48);
        reply.record.anchor = be64(sent.body, 52);
        for (std::size_t start = 0; start < 4; ++start) {
            reply.record.starts[start] = bef32(sent.body, 60 + 4 * start);
        }
        reply.phase = be32(sent.body, 76);
        found.push_back(reply);
    }
    return found;
}

std::size_t count_type(const FakeClient& client, std::uint32_t type, std::size_t from = 0) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        count += client.got[index].type == type ? 1 : 0;
    }
    return count;
}

/** @return The circular distance of two positions of a cycle (nanoseconds). */
std::uint64_t circular(std::uint64_t a, std::uint64_t b, std::uint64_t length) {
    const std::uint64_t difference = a > b ? a - b : b - a;
    return std::min(difference, length - difference);
}

/** The position that a client calculates from a record at a time of the server clock (the model of 0x6A0ADC0). */
std::uint64_t client_position(const world::TimeRecord& record, std::uint64_t serverNow) {
    return static_cast<std::uint64_t>(std::llround(day_cycle::client_part(record, serverNow)
                                                    * static_cast<double>(day_cycle::length_ns(record.cycle))));
}

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** A backend with three accounts (A and B near each other, C far away) and their clients. */
struct TimeHost {
    ServerHost host;
    Player a;
    Player b;
    Player c;
    FakeClient clientA{27000};
    FakeClient clientB{27001};
    FakeClient clientC{27002};

    explicit TimeHost(const std::string& database = ":memory:", bool existing = false)
        : host(observing(), database),
          a(make(host.store, existing, kSteamIdA, "Tester A", kCharacterA, "Timeone", kPlaceA)),
          b(make(host.store, existing, kSteamIdB, "Tester B", kCharacterB, "Timetwo", kPlaceB)),
          c(make(host.store, existing, kSteamIdC, "Tester C", kCharacterC, "Timethree", kPlaceC)) {}

    /** The player of an account: a new one, or the one of a database file of an earlier host. */
    static Player make(state::Store& store, bool existing, const char* steamId, const char* persona, const char* character,
                       const char* name, const state::WorldPosition& place) {
        if (!existing) {
            return add_player(store, steamId, persona, character, name, place);
        }
        Player player;
        player.steamId = steamId;
        player.personaName = persona;
        player.characterId = character;
        state::AccountOrigin origin{};
        const auto account = store.login_account(steamId, persona, false, origin);
        if (account) {
            player.account = *account;
        }
        return player;
    }
    /** Input reports of a client at the place of its character: the session sees the camera, and its interest scans start. */
    void reports(FakeClient& client, const state::WorldPosition& place, int count = 3) {
        for (int index = 0; index < count; ++index) {
            host.datagram(client.peer, client.input_report(report_entry(sequence++, place.x, place.y, place.z)));
        }
    }
    std::uint16_t sequence{1};

    std::vector<FakeClient*> all() {
        return {&clientA, &clientB, &clientC};
    }
    void steps(int count) {
        for (int index = 0; index < count; ++index) {
            host.advance(std::chrono::milliseconds(10));
            host.tick();
            for (FakeClient* client : all()) {
                client->exchange(host);
            }
        }
    }
    /** Time passes in steps of 1 s: each client acknowledges in each step (no session ends for the idle limit of 30 s). */
    void pass(int seconds) {
        for (int index = 0; index < seconds; ++index) {
            host.advance(std::chrono::milliseconds(1000));
            host.tick();
            for (FakeClient* client : all()) {
                client->exchange(host);
            }
        }
    }
    world::Entity& entity(const char* character) {
        return *host.world.find_character(character);
    }
    /** The client sends a time request with a route, a target, an entity field, and a body size. */
    void request(FakeClient& client, const world::Entity& route, std::uint64_t target, std::uint64_t group, std::size_t size = 16) {
        std::vector<std::uint8_t> body;
        for (int shift = 56; shift >= 0; shift -= 8) {
            body.push_back(static_cast<std::uint8_t>(target >> shift));
        }
        for (int shift = 56; shift >= 0; shift -= 8) {
            body.push_back(static_cast<std::uint8_t>(group >> shift));
        }
        body.resize(size, 0);
        host.datagram(client.peer, client.component_message(server::rep::kTypeTimeRequest, route.actor.bytes, body));
    }
    /** The valid request of the own entity of a character. */
    void request(FakeClient& client, const char* character) {
        const world::Entity& own = entity(character);
        request(client, own, server::rep::time_target(own.id), own.id);
    }
};

/** WT1: the cycle and the wire. */
void cycle_and_wire(const Options&) {
    const DayCycle cycle;
    const std::uint64_t length = day_cycle::length_ns(cycle);
    check(day_cycle::valid(cycle) && length == 2700 * kSecond, "the client cycle (decision D1): 1,800 s of day and 900 s of night, 45 minutes");
    // The vector of the research (world-time-stage.md 7.3): G 0x1000, server time 1e14, noon.
    const std::uint64_t noon = day_cycle::position_of(cycle, 43200.0);
    const world::TimeRecord record = day_cycle::record(cycle, 100'000'000'000'000ULL, noon);
    const std::string body = hex(server::rep::encode_time_and_phase(0x1000, record, day_cycle::phase_of(cycle, noon)));
    check(body == "d41eb32c0f1550040000000000001000bf800000bf8000003aafc8ad000054600000003c00010b300000003c000007080000038400"
                  "00587e6befc8003e3a2e8c3e50eff83f5385f33f59364e00000002",
          "the body of the noon vector of the research: the target G + D41EB32C0F154004, the anchor one cycle back, the offset "
          "3aafc8ad, the four phase edges, the phase day (80 bytes; the frame of 85 bytes is the prefix 54 00 01 bc 6d)");
    const world::TimeRecord midnight = day_cycle::record(cycle, 100'000'000'000'000ULL, 0);
    check(midnight.offset == 0.0F && midnight.anchor == 97'300'000'000'000ULL,
          "the offset-zero vector of the old proposal: midnight at the anchor (the anchor 97,300,000,000,000, the offset 0)");
    check(hex(server::rep::encode_day_phase(0x1000, DayPhase::dusk)) == "d41eb32c0f155004000000000000100000000003"
              && hex(server::rep::encode_clock_sync(0x0102030405060708ULL)) == "0102030405060708",
          "the bodies of 4247 (target, entity, phase) and 335 (the server clock): big-endian");
    // The edges and the phases (0x6A08270 with exact positions).
    const day_cycle::Edges edge = day_cycle::edges(cycle);
    check(edge.dawn < edge.day && edge.day < edge.dusk && edge.dusk < edge.night && edge.night < length
              && edge.day - edge.dawn == 60 * kSecond && edge.night - edge.dusk == 60 * kSecond && edge.night - edge.dawn == 1800 * kSecond,
          "the edges in order: dawn at 06:00, day 60 s later, dusk 60 s before 19:00, night at 19:00 (decision D4)");
    const struct {
        std::uint64_t position;
        DayPhase phase;
    } points[] = {{0, DayPhase::night},          {edge.dawn - 1, DayPhase::night}, {edge.dawn, DayPhase::dawn},
                  {edge.day - 1, DayPhase::dawn}, {edge.day, DayPhase::day},        {edge.dusk - 1, DayPhase::day},
                  {edge.dusk, DayPhase::dusk},    {edge.night, DayPhase::dusk},     {edge.night + 1, DayPhase::night},
                  {length - 1, DayPhase::night}};
    bool phases = true;
    for (const auto& point : points) {
        phases = phases && day_cycle::phase_of(cycle, point.position) == point.phase;
    }
    check(phases, "each phase edge: night before dawn, dawn, day, dusk to the night edge (included), night after it and to midnight");
    check(std::abs(day_cycle::game_second(cycle, edge.dawn) - 21600.0) < 1e-6 && std::abs(day_cycle::game_second(cycle, edge.night) - 68400.0) < 1e-6
              && std::abs(day_cycle::game_second(cycle, noon) - 43200.0) < 1e-3,
          "the game seconds of the edges: 06:00 at dawn, 19:00 at night, 12:00 at the noon position");
    const double day = day_cycle::game_second(cycle, noon + kSecond) - day_cycle::game_second(cycle, noon);
    const std::uint64_t one = day_cycle::position_of(cycle, 3600.0);
    const double night = day_cycle::game_second(cycle, one + kSecond) - day_cycle::game_second(cycle, one);
    check(std::abs(day - 26.0) < 1e-6 && std::abs(night - 44.0) < 1e-6, "the two rates: 26 game seconds in one real second by day, 44 by night");
    check(day_cycle::game_second(cycle, length - 1) > 86399.99 && day_cycle::position_of(cycle, 0.0) == 0
              && day_cycle::phase_of(cycle, length) == DayPhase::night,
          "the midnight wrap: the last nanosecond of the cycle is 23:59:59.99, and the next one is 00:00 (night)");
    bool roundTrip = true;
    for (double second = 0.0; second < 86400.0; second += 597.0) {
        roundTrip = roundTrip && std::abs(day_cycle::game_second(cycle, day_cycle::position_of(cycle, second)) - second) < 1e-3;
    }
    check(roundTrip, "a game second gives a position and the same game second back (each 597 s of the day)");
    check(day_cycle::to_next_edge(cycle, noon) == edge.dusk - noon && day_cycle::to_next_edge(cycle, edge.night) == 1
              && day_cycle::to_next_edge(cycle, length - kSecond) == kSecond + edge.dawn,
          "the time to the next edge: from noon to dusk, from the night edge 1 ns, from 23:59 over midnight to dawn");
    DayCycle broken = cycle;
    broken.dayLength = 0;
    DayCycle reversed = cycle;
    reversed.dawnTime = 70000;
    check(!day_cycle::valid(broken) && !day_cycle::valid(reversed), "a day length of 0 (the client divides by it) and dawn after dusk are not cycles");
    // The anchor: never 0, never later than the clock; one cycle back at a large uptime.
    const world::TimeRecord early = day_cycle::record(cycle, 1, noon);
    const world::TimeRecord edgeCase = day_cycle::record(cycle, length, noon);
    const world::TimeRecord late = day_cycle::record(cycle, length + 1, noon);
    check(early.anchor == 1 && edgeCase.anchor == 1 && late.anchor == 1 && day_cycle::record(cycle, 5 * length, noon).anchor == 4 * length,
          "the anchor at an early uptime is 1 (not 0, not after the clock), and one cycle back at a large uptime");
    // The float model of the client (0x6A0ADC0): the error stays below 1 ms for each uptime, position, and time after the send.
    std::uint64_t largest = 0;
    unsigned compares = 0;
    const std::uint64_t uptimes[] = {1, 2, 1'000'000, kSecond, length - 1, length, length + 1, 100'000'000'000'000ULL,
                                     1'000'000'000'000'000'000ULL, (1ULL << 63) - 1};
    const std::uint64_t afters[] = {0, 1, 10 * kSecond, 60 * kSecond, length, 3600 * kSecond};
    for (const std::uint64_t now : uptimes) {
        for (std::uint64_t position = 0; position < length; position += length / 97 + 12345) {
            const world::TimeRecord sent = day_cycle::record(cycle, now, position);
            for (const std::uint64_t after : afters) {
                const std::uint64_t error = circular(client_position(sent, now + after), (position + after) % length, length);
                largest = std::max(largest, error);
                ++compares;
            }
        }
    }
    note("the float model of the client: " + std::to_string(compares) + " compares, the largest error " + std::to_string(largest) + " ns");
    check(largest < 1'000'000, "the client model gives the position of the server with an error below 1 ms (0.044 game seconds at night)");
}

/** WT2: two sessions and a third one: one world value, the ids of each own entity, the deliveries, the clock correction. */
void two_sessions(const Options&) {
    TimeHost shop;
    shop.host.enter(shop.clientA, shop.a.registration());
    const auto registeredA = shop.host.clock.now();
    shop.host.enter(shop.clientB, shop.b.registration());
    shop.host.enter(shop.clientC, shop.c.registration());
    shop.steps(5);
    world::Entity& a = shop.entity(kCharacterA);
    world::Entity& b = shop.entity(kCharacterB);
    check(a.id != b.id, "setup: two own entities with different ids");
    check(time_replies(shop.clientA).empty() && count_type(shop.clientA, server::rep::kTypeDayPhase) == 0,
          "no time message before the request of the client (the registration sequence has no time message)");
    shop.request(shop.clientA, kCharacterA);
    shop.request(shop.clientB, kCharacterB);
    shop.steps(1);
    const auto repliesA = time_replies(shop.clientA);
    const auto repliesB = time_replies(shop.clientB);
    check(repliesA.size() == 1 && repliesB.size() == 1 && time_replies(shop.clientC).empty(),
          "each client that asked gets one record (7036); the client that did not ask gets none");
    if (repliesA.size() != 1 || repliesB.size() != 1) {
        return;
    }
    check(repliesA[0].entity == a.id && repliesA[0].target == server::rep::time_target(a.id) && repliesB[0].entity == b.id
              && repliesB[0].target == server::rep::time_target(b.id),
          "each record has the ids of the own entity of its session (rule MP-4), not of the other one");
    const std::uint64_t length = day_cycle::length_ns(repliesA[0].record.cycle);
    const std::uint64_t now = shop.host.clock.server_time_ns();
    check(circular(client_position(repliesA[0].record, now), client_position(repliesB[0].record, now), length) < 1'000'000
              && circular(client_position(repliesA[0].record, now), shop.host.world.world_time().position(), length) < 1'000'000
              && repliesA[0].phase == static_cast<std::uint32_t>(DayPhase::day),
          "the two records give the same world time (noon of a new world, day): one value of the world (rule MP-1)");
    check(shop.host.session(1).time_requested() && shop.host.session(2).time_requested() && !shop.host.session(3).time_requested(),
          "the sessions of A and B have the request mark; C has none");
    // A command for the whole world: each client that asked gets the new record in the next tick.
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    const std::size_t fromC = shop.clientC.got.size();
    check(shop.host.world.world_time().set(0.0, "the test") == world::TimeSetError::none, "the command sets 00:00");
    shop.steps(1);
    const auto newA = time_replies(shop.clientA, fromA);
    const auto newB = time_replies(shop.clientB, fromB);
    const std::uint64_t later = shop.host.clock.server_time_ns();
    check(newA.size() == 1 && newB.size() == 1 && newA[0].phase == 0 && newB[0].phase == 0
              && circular(client_position(newA[0].record, later), shop.host.world.world_time().position(), length) < 1'000'000
              && circular(client_position(newB[0].record, later), shop.host.world.world_time().position(), length) < 1'000'000,
          "a command reaches each client that asked, with the new time and the phase night (one change, one path: rule MP-2)");
    check(time_replies(shop.clientC, fromC).empty() && count_type(shop.clientC, server::rep::kTypeDayPhase, fromC) == 0,
          "the client that did not ask gets no record and no phase (far from A and B: the distance does not matter)");
    // C asks now: it gets the latest value.
    shop.request(shop.clientC, kCharacterC);
    shop.steps(1);
    const auto newC = time_replies(shop.clientC, fromC);
    check(newC.size() == 1 && newC[0].phase == 0
              && circular(client_position(newC[0].record, shop.host.clock.server_time_ns()), shop.host.world.world_time().position(),
                          length) < 1'000'000,
          "a client that asks later gets the time of the world now");
    // A natural phase change: 4247 to each client that asked, with no new record. The command sets 05:59:16 (1 s before dawn at 44
    // game seconds in a real second); the cycle then passes the edge.
    check(shop.host.world.world_time().set(21600.0 - 44.0, "the test") == world::TimeSetError::none, "the command sets 05:59:16");
    shop.steps(1);
    const std::size_t phaseA = shop.clientA.got.size();
    shop.steps(110);
    std::size_t dawnMessages = 0;
    for (std::size_t index = phaseA; index < shop.clientA.got.size(); ++index) {
        const Sent& sent = shop.clientA.got[index];
        if (sent.type == server::rep::kTypeDayPhase && sent.body.size() == 20 && be32(sent.body, 16) == 1 && be64(sent.body, 8) == a.id) {
            ++dawnMessages;
        }
    }
    check(shop.host.world.world_time().phase() == DayPhase::dawn && dawnMessages == 1 && time_replies(shop.clientA, phaseA).empty(),
          "the natural change to dawn: one phase message (4247, phase 1) to the client, and no new record");
    // The clock correction: each 3 s, with the server clock.
    std::vector<std::pair<std::chrono::nanoseconds, std::uint64_t>> syncs;
    const std::size_t syncFrom = shop.clientA.got.size();
    for (int step = 0; step < 700; ++step) {
        const std::size_t before = shop.clientA.got.size();
        shop.steps(1);
        for (std::size_t index = before; index < shop.clientA.got.size(); ++index) {
            const Sent& sent = shop.clientA.got[index];
            if (sent.type == server::rep::kTypeClockSync && sent.body.size() == 8) {
                syncs.emplace_back(shop.host.clock.now() - registeredA, be64(sent.body, 0));
            }
        }
    }
    bool spaced = syncs.size() >= 2;
    for (std::size_t index = 1; index < syncs.size(); ++index) {
        const auto gap = syncs[index].first - syncs[index - 1].first;
        spaced = spaced && gap >= std::chrono::milliseconds(3000) && gap <= std::chrono::milliseconds(3020);
    }
    const std::size_t total = count_type(shop.clientA, server::rep::kTypeClockSync, syncFrom);
    bool clocks = true;
    for (const auto& sync : syncs) {
        clocks = clocks && sync.second == static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                                (registeredA + sync.first).time_since_epoch()).count());
    }
    note("clock corrections of A in 7 s: " + std::to_string(total));
    check(spaced && clocks && total >= 2 && total <= 3,
          "the clock correction (335) goes each 3 s (the sender interval of the client image) with the server clock of its tick");
    bool firstAtThree = false;
    {
        TimeHost fresh;
        fresh.host.enter(fresh.clientA, fresh.a.registration());
        const auto start = fresh.host.clock.now();
        std::chrono::nanoseconds first{0};
        for (int step = 0; step < 400 && first.count() == 0; ++step) {
            const std::size_t before = fresh.clientA.got.size();
            fresh.steps(1);
            for (std::size_t index = before; index < fresh.clientA.got.size(); ++index) {
                if (fresh.clientA.got[index].type == server::rep::kTypeClockSync) {
                    first = fresh.host.clock.now() - start;
                    firstAtThree = be64(fresh.clientA.got[index].body, 0) == fresh.host.clock.server_time_ns();
                }
            }
        }
        firstAtThree = firstAtThree && first >= std::chrono::milliseconds(2980) && first <= std::chrono::milliseconds(3030);
        note("the first clock correction came " + std::to_string(first.count() / 1'000'000) + " ms after the registration");
    }
    check(firstAtThree, "the first clock correction comes 3 s after the registration, with the server clock of the tick (no request needed)");
}

/** WT3: requests that change nothing, and a repeated valid request. */
void refused_requests(const Options&) {
    TimeHost shop;
    // Before the registration: the connection is open, the client has no character in the world.
    shop.clientA.connect(shop.host);
    shop.steps(2);
    world::Entity probe;
    probe.actor.bytes.fill(0x02);
    shop.request(shop.clientA, probe, server::rep::time_target(0x1234), 0x1234);
    shop.steps(1);
    check(time_replies(shop.clientA).empty(), "a request before the registration gets no reply");
    shop.host.datagram(shop.clientA.peer, shop.clientA.registration(shop.a.registration()));
    shop.steps(2);
    shop.host.enter(shop.clientB, shop.b.registration());
    shop.reports(shop.clientA, kPlaceA);
    shop.reports(shop.clientB, kPlaceB);
    shop.steps(60);
    world::Entity& a = shop.entity(kCharacterA);
    world::Entity& b = shop.entity(kCharacterB);
    const std::uint64_t revision = shop.host.world.world_time().revision();
    shop.request(shop.clientA, a, server::rep::time_target(a.id), a.id, 15);
    shop.request(shop.clientA, a, server::rep::time_target(a.id), a.id, 17);
    shop.request(shop.clientA, b, server::rep::time_target(b.id), b.id);           // the route of the observed remote player B
    shop.request(shop.clientA, a, server::rep::time_target(b.id), b.id);           // the entity of B on the route of A
    shop.request(shop.clientA, a, server::rep::time_target(a.id) + 1, a.id);       // a different facet of A
    shop.request(shop.clientA, a, server::rep::time_target(a.id), a.id + 0x100);   // the entity field is not the own entity
    shop.steps(2);
    const bool observed = shop.host.session(1).observer().known().count(b.id) != 0;
    check(observed, "setup: the session of A observes B (a remote player near A)");
    check(time_replies(shop.clientA).empty() && !shop.host.session(1).time_requested() && shop.host.world.world_time().revision() == revision,
          "a body of 15 or 17 bytes, the route of the remote player B, the entity of B, a different facet, and a different entity "
          "field get no reply, no request mark, and no change of the world time");
    check(log_count("time request (type 4248, 15 bytes) refused") == 1 && log_count("refused: the route is not the actor") == 1
              && log_count("refused: the entity field is not") == 1 && log_count("refused: the target is not the Time facet") == 1,
          "one log line for each cause of a refusal");
    shop.request(shop.clientA, kCharacterA);
    shop.steps(1);
    shop.request(shop.clientA, kCharacterA);
    shop.steps(1);
    check(time_replies(shop.clientA).size() == 2 && shop.host.session(1).time_requested(),
          "a valid request gets the record, and a repeated valid request gets it again");
    // A closing session: B enters with a second client; the first session of B gets "kicked" and ends later.
    FakeClient second{27005};
    shop.host.enter(second, shop.b.registration());
    shop.steps(2);
    const std::size_t from = shop.clientB.got.size();
    shop.request(shop.clientB, b, server::rep::time_target(b.id), b.id);
    shop.steps(2);
    check(time_replies(shop.clientB, from).empty(), "the old session of a character that a newer session took (closing) gets no reply");
}

/** WT4: the store of the world time. */
void stored_time(const Options& options) {
    const std::filesystem::path path = options.workDirectory / "world-time.sqlite3";
    std::filesystem::remove(path);
    std::filesystem::remove(path.string() + "-wal");
    std::filesystem::remove(path.string() + "-shm");
    const DayCycle cycle;
    const std::uint64_t length = day_cycle::length_ns(cycle);
    std::uint64_t stopped = 0;
    {
        TimeHost shop(path.string());
        shop.host.tick();
        world::WorldTime& time = shop.host.world.world_time();
        check(!time.counts().loaded && time.phase() == DayPhase::day && std::abs(time.game_second() - 43200.0) < 0.01
                  && shop.host.store.world_time(world::kWorldTimeKey) == day_cycle::position_of(cycle, 43200.0),
              "a new world starts at noon (decision D2), and the store has its row at once");
        check(time.set(6.5 * 3600.0, "the test") == world::TimeSetError::none
                  && shop.host.store.world_time(world::kWorldTimeKey) == day_cycle::position_of(cycle, 6.5 * 3600.0),
              "a command writes the row first: 06:30");
        const std::uint64_t saves = time.counts().saves;
        shop.host.advance(std::chrono::seconds(59));
        shop.host.tick();
        const bool noSave = time.counts().saves == saves;
        shop.host.advance(std::chrono::seconds(2));
        shop.host.tick();
        const auto stored = shop.host.store.world_time(world::kWorldTimeKey);
        check(noSave && time.counts().saves == saves + 1 && stored && circular(*stored, time.position(), length) <= 10'000'000,
              "the save interval: no save 59 s after the command, one save at 60 s with the position of the cycle");
        shop.host.advance(std::chrono::seconds(17));
        check(time.save("orderly stop") && shop.host.store.world_time(world::kWorldTimeKey) == time.position(),
              "the save of an orderly stop writes the position now");
        stopped = time.position();
        // A refused write of a command: nothing changes.
        sqlite3* connection = nullptr;
        sqlite3_open(path.string().c_str(), &connection);
        const int made = sqlite3_exec(connection,
                                      "CREATE TRIGGER refuse_insert BEFORE INSERT ON world_time BEGIN SELECT RAISE(ABORT, 'refused'); END;"
                                      "CREATE TRIGGER refuse_update BEFORE UPDATE ON world_time BEGIN SELECT RAISE(ABORT, 'refused'); END;",
                                      nullptr, nullptr, nullptr);
        const std::uint64_t revision = time.revision();
        const std::uint64_t before = time.position();
        check(made == SQLITE_OK && time.set(0.0, "the test") == world::TimeSetError::storage && time.revision() == revision
                  && circular(time.position(), before, length) < 1'000 && shop.host.store.world_time(world::kWorldTimeKey) == stopped,
              "a command whose write the database refuses: the error storage, the same revision, the same position, the same row");
        sqlite3_exec(connection, "DROP TRIGGER refuse_insert; DROP TRIGGER refuse_update;", nullptr, nullptr, nullptr);
        sqlite3_close(connection);
    }
    {
        // A restart: a different clock epoch (10 hours later), and the time from the stop to the start adds nothing (decision D3).
        TimeHost shop(path.string(), true);
        shop.host.clock.advance(std::chrono::hours(10));
        shop.host.tick();
        world::WorldTime& time = shop.host.world.world_time();
        check(time.counts().loaded && circular(time.position(), stopped, length) < 1'000,
              "a restart with a different epoch of the clock continues from the stored position (the stop adds no time)");
        shop.host.enter(shop.clientA, shop.a.registration());
        shop.steps(2);
        shop.request(shop.clientA, kCharacterA);
        shop.steps(1);
        const auto replies = time_replies(shop.clientA);
        const std::uint64_t now = shop.host.clock.server_time_ns();
        check(replies.size() == 1 && replies[0].record.anchor + length <= now + 30'000'000 && replies[0].record.anchor < now
                  && circular(client_position(replies[0].record, now), time.position(), length) < 1'000'000,
              "the record after the restart has an anchor of the new clock (one cycle back): no anchor of the old start");
    }
    {
        // A stored value that is not a position of the cycle: the world starts at noon.
        state::Store store;
        check(store.open(path.string()) && store.set_world_time(world::kWorldTimeKey, length + 5), "setup: a stored value above the cycle");
    }
    {
        TimeHost shop(path.string(), true);
        shop.host.tick();
        check(!shop.host.world.world_time().counts().loaded && std::abs(shop.host.world.world_time().game_second() - 43200.0) < 0.01,
              "a stored value that is not a position of the cycle: the world starts at noon");
    }
}

// The creature data of WT5 to WT7.
constexpr std::uint32_t kSlots[] = {5, 9, 15};

gamedata::CreatureSlice test_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/time_wolf.dynamicslice";
    slice.asset.fill(0x71);
    slice.subId = 2;
    slice.root = 0x1000000000000071ULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = kSlots[0];
    slice.vitalsSlot = kSlots[1];
    slice.markerSlot = kSlots[2];
    slice.slots = {{kSlots[0], "ActionList", 11}, {kSlots[1], "Vitals", 15}, {kSlots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    return slice;
}

gamedata::CreatureKind test_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Time_Wolf";
    kind.vitals = kind.id;
    kind.category = "Test_Time_Wolf_Category";
    kind.vitalsId = 0x71717171;
    kind.categoryId = 0x71717172;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 18;
    kind.bodies = {{0, 100}};
    return kind;
}

gamedata::SpawnPoint phase_point(const std::string& id, float x, float y, std::uint8_t phases, bool leaves = true) {
    gamedata::SpawnPoint point;
    point.id = id;
    point.kind = 0;
    point.level = 18;
    point.position = state::WorldPosition{x, y, 30.0F};
    point.respawnMin = 360.0F;
    point.phases = phases;
    point.leavesAtPhaseEnd = leaves;
    return point;
}

/** @return The creature of a spawn point, or null. */
const world::Entity* creature_of(world::World& world, std::uint32_t index) {
    const auto& points = world.creatures().spawn_points();
    return index < points.size() && points[index].entity != 0 ? world.find(points[index].entity) : nullptr;
}

/** WT5: the creature points with a time constraint. */
void phase_points(const Options&) {
    TimeHost shop;
    // The masks of the data (night 1, dawn 2, day 4, dusk 8): the seven masks of the retail data, each phase, none, a point that does
    // not leave, and a point with the two cooldown flags. All 12 to 40 m from A and B.
    const std::uint8_t masks[] = {9, 6, 3, 11, 14, 4, 1, 15, 0};
    std::vector<gamedata::SpawnPoint> points;
    for (std::size_t index = 0; index < std::size(masks); ++index) {
        points.push_back(phase_point("t" + std::to_string(index), 110.0F + 3.0F * static_cast<float>(index), 210.0F, masks[index]));
    }
    const std::size_t stays = points.size();
    points.push_back(phase_point("t-stays", 110.0F, 220.0F, 4, false));
    const std::size_t cooled = points.size();
    points.push_back(phase_point("t-cooldown", 113.0F, 220.0F, 4));
    points.back().cooldownAfterPhaseEnd = true;
    points.back().cooldownOutOfPhase = true;
    check(shop.host.creatures.set({test_slice()}, {test_kind()}, points, {{18, 1690.875977F}}), "setup: the creature data");
    shop.host.enter(shop.clientA, shop.a.registration());
    shop.host.enter(shop.clientB, shop.b.registration());
    shop.reports(shop.clientA, kPlaceA);
    shop.reports(shop.clientB, kPlaceB);
    world::World& world = shop.host.world;
    world::Creatures& creatures = world.creatures();
    shop.steps(80);
    const auto present = [&](std::size_t index) { return creature_of(world, static_cast<std::uint32_t>(index)) != nullptr; };
    // Noon (day: bit 4).
    bool day = true;
    for (std::size_t index = 0; index < std::size(masks); ++index) {
        day = day && present(index) == ((masks[index] & 4) != 0);
    }
    check(world.world_time().phase() == DayPhase::day && day && present(stays) && present(cooled),
          "noon (day): the points whose mask has the day have their creature; the others (night, night and dusk, night and dawn, "
          "the empty mask) have none");
    check(creatures.counts().outOfPhase > 0, "the points out of their phase near the players count in outOfPhase");
    // A creature of a command stays through the phases.
    const world::CreatureResult commanded = creatures.spawn("Test_Time_Wolf", state::WorldPosition{105.0F, 205.0F, 30.0F}, 0.0F);
    // A night point: kill one at night later; now the day-only point with leaves=false stays at night.
    const std::uint64_t kills = world.rewards().counts().kills;
    const std::uint64_t deaths = creatures.counts().deaths;
    const world::EntityId dayOnly = creature_of(world, 5)->id;
    check(world.world_time().set(0.0, "the test") == world::TimeSetError::none, "the command sets 00:00");
    shop.steps(1);
    bool night = true;
    for (std::size_t index = 0; index < std::size(masks); ++index) {
        night = night && present(index) == ((masks[index] & 1) != 0);
    }
    check(night && world.find(dayOnly) == nullptr && present(stays) && !present(cooled),
          "in the tick after the command (night): the creatures of the day points left, the night points have their creature, the "
          "point with no despawn flag keeps its creature");
    check(creatures.counts().removedByPhase >= 4 && world.rewards().counts().kills == kills && creatures.counts().deaths == deaths,
          "the phase removal is not a kill: no death, no reward");
    check(shop.host.session(1).observer().known().count(dayOnly) == 0 && shop.host.session(2).observer().known().count(dayOnly) == 0,
          "the two observers A and B lose the creature that left (the stop of the record)");
    check(commanded.error == world::CreatureError::none && commanded.entity != nullptr && world.find(commanded.entity->id) != nullptr,
          "the creature of a command stays");
    // The cooldown flags: the end of its phases started the respawn time of the point (360 s); a look out of its phase too.
    check(creatures.spawn_points()[cooled].waiting && creatures.spawn_points()[cooled].respawnAt.has_value(),
          "the point with the cooldown flags waits its respawn time after the end of its phases");
    // A death across a phase that is not one of the point: kill the creature of the night point 6 at night.
    const world::EntityId nightCreature = creature_of(world, 6)->id;
    creatures.kill(nightCreature);
    const auto respawnAt = creatures.spawn_points()[6].respawnAt;
    check(creatures.spawn_points()[6].waiting && respawnAt.has_value(), "setup: the night point waits 360 s after the death");
    check(world.world_time().set(12 * 3600.0, "the test") == world::TimeSetError::none, "the command sets 12:00");
    shop.steps(1);
    check(!present(6) && creatures.spawn_points()[6].waiting, "by day the body leaves (the despawn flag), and the point still waits");
    shop.pass(400);
    shop.steps(60);
    check(!present(6) && creatures.spawn_points()[6].waiting, "after its respawn time, by day: no creature (not a phase of the point)");
    check(world.world_time().set(1 * 3600.0, "the test") == world::TimeSetError::none, "the command sets 01:00");
    shop.steps(60);
    check(present(6) && !creatures.spawn_points()[6].waiting, "at night again: the point makes its creature (the respawn time came)");
    // A hold of a command across the phases.
    const world::EntityId held = creature_of(world, 0)->id;
    check(creatures.remove(held).error == world::CreatureError::none, "setup: a command removes the creature of the point 0");
    world.world_time().set(12 * 3600.0, "the test");
    shop.steps(60);
    world.world_time().set(1 * 3600.0, "the test");
    shop.steps(60);
    check(!present(0) && creatures.spawn_points()[0].waiting, "the hold of a command stays across the phases");
    creatures.reset_spawn_points();
    shop.steps(60);
    check(present(0), "the reset frees the point, and it makes its creature at night");
    // The cooldown point: out of its phase it waits; by day after its time it makes its creature.
    world.world_time().set(12 * 3600.0, "the test");
    shop.steps(60);
    const bool waitsByDay = !present(cooled);
    shop.pass(400);
    shop.steps(60);
    check(waitsByDay && present(cooled), "the point with the cooldown flags: by day before its time no creature, after it one");
    // At most one creature for each point.
    std::map<std::uint32_t, unsigned> perPoint;
    for (const world::EntityId id : creatures.ids()) {
        const world::Entity* entity = world.find(id);
        if (entity != nullptr && entity->creature.spawnPoint) {
            ++perPoint[*entity->creature.spawnPoint];
        }
    }
    bool single = true;
    for (const auto& [point, count] : perPoint) {
        single = single && count == 1;
    }
    check(single, "one creature for each point at most");
}

/** WT6: the load of the phase rule at the size of the world. */
void phase_load(const Options&) {
    // A world of 33,856 points (the grid of WS2), each fourth one with a night mask, and 50 players.
    std::vector<gamedata::SpawnPoint> points;
    std::uint32_t index = 0;
    for (float x = 0.0F; x <= 14274.0F; x += 78.0F) {
        for (float y = 0.0F; y <= 14274.0F; y += 78.0F) {
            points.push_back(phase_point("w" + std::to_string(index), x, y, index % 4 == 0 ? 9 : 15));
            ++index;
        }
    }
    ServerHost host;
    check(host.creatures.set({test_slice()}, {test_kind()}, points, {{18, 1690.875977F}}), "the made-up world has 33,856 points");
    std::vector<std::string> ids;
    for (unsigned player = 0; player < 50; ++player) {
        char id[40];
        std::snprintf(id, sizeof(id), "%08x-0000-4000-8000-%012x", 0x71000000U + player, player);
        add_player(host.store, "765612022553" + std::to_string(20000 + player), "Tester " + std::to_string(player), id,
                   "Timer" + std::to_string(player), std::nullopt);
        ids.emplace_back(id);
        check(host.world.spawn_character(id, state::WorldPosition{700.0F + 271.0F * static_cast<float>(player),
                                                                   500.0F + 263.0F * static_cast<float>((player * 7) % 50), 30.0F})
                  == world::SpawnError::none,
              player == 0 ? "50 players are in the world" : "");
        if (player != 0) {
            --counters().checks;
        }
    }
    world::World& world = host.world;
    world::Creatures& creatures = world.creatures();
    host.clock.advance(world::Creatures::kScanInterval);
    world.tick();
    const std::size_t dayCreatures = creatures.count();
    const std::size_t dayLooked = creatures.counts().lastScanPoints;
    // A command to night: the tick after it.
    const auto commandStarted = std::chrono::steady_clock::now();
    world.world_time().set(0.0, "the test");
    world.tick();
    const double commandTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - commandStarted).count();
    const std::size_t nightLooked = creatures.counts().lastScanPoints;
    const std::size_t nightCreatures = creatures.count();
    // A natural edge: from 05:59:59 to dawn.
    world.world_time().set(5 * 3600.0 + 59 * 60.0 + 50.0, "the test");
    world.tick();
    const std::uint64_t edge = world.world_time().to_next_edge();
    host.clock.advance(std::chrono::nanoseconds(edge));
    const auto edgeStarted = std::chrono::steady_clock::now();
    world.tick();
    const double edgeTime = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - edgeStarted).count();
    note("day: " + std::to_string(dayCreatures) + " creatures, " + std::to_string(dayLooked) + " points looked at; night: "
         + std::to_string(nightCreatures) + " creatures, " + std::to_string(nightLooked) + " points; the command tick "
         + std::to_string(commandTime) + " ms, the dawn edge tick " + std::to_string(edgeTime) + " ms");
    check(world.world_time().phase() == DayPhase::dawn && dayLooked <= 50 * 49 && nightLooked <= 50 * 49
              && creatures.counts().lastScanPoints <= 50 * 49,
          "the scan of a command and of a phase edge looks at 49 points or less for each player, not at each point of the world");
    check(nightCreatures > dayCreatures && creatures.counts().removedByPhase > 0,
          "by night the night points near the players have their creature, and at dawn their creatures leave (the phase removal)");
    check(commandTime < 50.0 && edgeTime < 50.0, "the tick of a command and of a phase edge needs less than 50 ms");
}

/** WT7: the creature file of format 4. */
void creature_format(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "world-time-creatures";
    std::filesystem::create_directories(folder);
    const auto document = [](std::uint64_t format, const json& point) {
        json slice = {{"path", "slices/test/time_wolf.dynamicslice"}, {"asset", "71717171-7171-7171-7171-717171717171"}, {"sub", 2},
                      {"root", "1000000000000071"}, {"interestRadius", 60}, {"movement", 5}, {"vitals", 9}, {"marker", 15},
                      {"slots", json::array({json::array({5, "ActionList", 11}), json::array({9, "Vitals", 15}), json::array({15, "Marker", 5399})})},
                      {"components", json::array()}, {"actions", json::array()}};
        json kind = {{"id", "Test_Time_Wolf"}, {"vitals", "Test_Time_Wolf"}, {"category", ""}, {"vitalsId", 1}, {"categoryId", 2},
                     {"name", "Wolf"}, {"vitalsName", "Wolf"}, {"family", "Wildlife"}, {"creatureType", "Solo"}, {"health", 1.0},
                     {"level", 18}, {"bodies", json::array({json::array({0, 100})})}, {"points", 1}};
        json levels = format >= 2 ? json::array({json::array({18, 1690.9, 100.0, 180.0})}) : json::array({json::array({18, 1690.9})});
        json result = {{"format", format}, {"levels", levels}, {"slices", json::array({slice})}, {"kinds", json::array({kind})},
                       {"spawnPoints", json::array({point})}};
        if (format >= 3) {
            result["routes"] = json::array();
        }
        return result;
    };
    const auto load = [&](const std::string& name, const json& content, gamedata::CreatureData& data) {
        const std::filesystem::path file = folder / (name + ".json");
        std::ofstream(file) << content.dump();
        return data.load(file.string());
    };
    const json base = json::array({"pt-1", 0, 18, 1.0, 2.0, 3.0, 0.0, 360.0, 0.0, 3.0, nullptr});
    json four = base;
    four.push_back(9);
    four.push_back(true);
    four.push_back(3);
    gamedata::CreatureData data;
    const bool loaded = load("format4", document(4, four), data);
    check(loaded && data.spawn_points().size() == 1 && data.spawn_points()[0].phases == 9 && data.spawn_points()[0].leavesAtPhaseEnd
              && data.spawn_points()[0].cooldownAfterPhaseEnd && data.spawn_points()[0].cooldownOutOfPhase && data.phased_points() == 1,
          "format 4: the phase mask 9 (night, dusk), the despawn flag, and the two cooldown flags of a point " + (loaded ? std::string{} : data.error()));
    gamedata::CreatureData older;
    check(load("format3", document(3, base), older) && older.spawn_points()[0].phases == world::kAllPhases
              && !older.spawn_points()[0].leavesAtPhaseEnd && older.phased_points() == 0,
          "a file of format 3: each point in each phase, no despawn flag");
    json empty = base;
    empty.push_back(0);
    empty.push_back(true);
    empty.push_back(0);
    gamedata::CreatureData none;
    check(load("empty", document(4, empty), none) && none.spawn_points()[0].phases == 0, "an explicit empty mask loads (no phase)");
    const struct {
        const char* name;
        json mask;
        json leaves;
        json cooldown;
    } faults[] = {{"mask-16", 16, true, 0}, {"mask-text", "9", true, 0}, {"mask-negative", -1, true, 0}, {"leaves-number", 9, 1, 0},
                  {"cooldown-4", 9, true, 4}, {"cooldown-null", 9, true, nullptr}};
    for (const auto& fault : faults) {
        json point = base;
        point.push_back(fault.mask);
        point.push_back(fault.leaves);
        point.push_back(fault.cooldown);
        gamedata::CreatureData wrong;
        const bool accepted = load(fault.name, document(4, point), wrong);
        check(!accepted && !wrong.loaded() && wrong.spawn_points().empty(),
              std::string("a fault of a point (") + fault.name + ") refuses the whole file: no point is loaded");
    }
    gamedata::CreatureData missing;
    json short_point = base;
    short_point.push_back(9);
    short_point.push_back(true);
    check(!load("short", document(4, short_point), missing) && !missing.loaded(), "a point of format 4 with 13 values refuses the file");
    gamedata::CreatureData absent;
    check(!absent.load((folder / "no-file.json").string()) && !absent.loaded(), "no file: no creature data (the backend starts with no creature)");
}

/** WT8: the cases phase-* of the check of the creature tool. */
void creature_tool(const Options& options) {
    const std::filesystem::path tool = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_creature_tool.py";
    check(std::filesystem::is_regular_file(tool), "the check of the creature tool is in the tree");
    const std::filesystem::path work = options.workDirectory / "check-creature-phases";
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / "check-creature-phases.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + tool.lexically_normal().string() + "\" \"" + work.string() + "\" > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        if (line.rfind("ok    phase-", 0) == 0) {
            passed.insert(line.substr(6, line.find(':') - 6));
            note(line.substr(0, 140));
        } else if (line.rfind("FAIL", 0) == 0) {
            note(line.substr(0, 200));
        }
    }
    check(status == 0, "each case of the check of the creature tool passes");
    for (const char* name : {"phase-none", "phase-night-dusk", "phase-empty", "phase-cooldowns", "phase-cut-flag", "phase-missing-flag",
                             "phase-two-constraints"}) {
        check(passed.contains(name), std::string("the case ") + name + " passes");
    }
}

/** WT9: the commands of the admin interface and of the development routes. */
void time_commands(const Options&) {
    TimeHost shop;
    server::http::AdminAccess access;
    server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::Router router;
    if (!access.add(kTokenA, kSteamIdA) || !access.add(kTokenB, kSteamIdB)) {
        check(false, "setup: the tokens");
        return;
    }
    server::http::register_admin_routes(router, services, access);
    server::http::register_dev_routes(router, services);
    shop.host.tick();
    const auto send = [&](const char* method, const std::string& path, const std::string& query, const std::string& body, const char* token) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = query.empty() ? path : path + "?" + query;
        request.body = body;
        if (token != nullptr) {
            request.headers.emplace_back(server::http::kAdminTokenHeader, token);
        }
        return server::http::handle_request(router, request);
    };
    const auto command = [&](const char* token, const char* character, const json& arguments) {
        return send("POST", "/admin/v1/commands", {}, json{{"command", "set_world_time"}, {"character", character}, {"args", arguments}}.dump(),
                    token);
    };
    world::WorldTime& time = shop.host.world.world_time();
    const service::HttpResponse state = send("GET", "/admin/v1/state", "character=" + std::string(kCharacterA), {}, kTokenA);
    const json stateBody = json::parse(state.body, nullptr, false);
    check(state.status == 200 && stateBody.contains("worldTime") && stateBody["worldTime"]["time"] == "12:00:00"
              && stateBody["worldTime"]["phase"] == "day" && stateBody["worldTime"]["cycle"]["dayLength"] == 1800,
          "the state of a character has the world time (the tool window shows it): 12:00:00, day, a day of 1,800 s");
    const service::HttpResponse set = command(kTokenA, kCharacterA, {{"time", "06:00"}});
    const json setBody = json::parse(set.body, nullptr, false);
    check(set.status == 200 && setBody["scope"] == "world" && setBody["worldTime"]["phase"] == "dawn" && time.phase() == DayPhase::dawn,
          "the admin command 06:00: the reply 200 with the scope world, the phase dawn");
    const std::uint64_t revision = time.revision();
    bool refused = true;
    for (const json& wrong : {json("24:00"), json("12:60"), json("noon"), json(""), json("1:2:3:4"), json("12:5"), json(43200)}) {
        const service::HttpResponse reply = command(kTokenA, kCharacterA, {{"time", wrong}});
        refused = refused && reply.status == 400;
    }
    const service::HttpResponse noTime = command(kTokenA, kCharacterA, json::object());
    check(refused && noTime.status == 400 && time.revision() == revision, "a time that is not a time of the day, and no time: the reply 400, no change");
    const service::HttpResponse other = command(kTokenB, kCharacterA, {{"time", "00:00"}});
    check(other.status == 404 && time.revision() == revision, "the token of B with the character of A: the reply 404, no change");
    const service::HttpResponse dev = send("POST", "/v1/dev/world/time", "character=" + std::string(kCharacterB) + "&time=19:00:30", {}, nullptr);
    const service::HttpResponse read = send("GET", "/v1/dev/world/time", {}, {}, nullptr);
    const json readBody = json::parse(read.body, nullptr, false);
    check(dev.status == 200 && read.status == 200 && readBody["phase"] == "night" && readBody["time"] == "19:00:30" && readBody["counts"]["sets"] == 2,
          "the development route sets 19:00:30 (night), and its readback has the time, the phase, and the counts");
}

} // namespace

std::vector<Case> world_time_cases() {
    return {{"WT1", "world time: the cycle and the wire (the vectors, the edges, the rates, the anchor, the client model)", cycle_and_wire},
            {"WT2", "world time: two sessions and a third one, the deliveries, the phase message, the clock correction", two_sessions},
            {"WT3", "world time: the requests that change nothing, and a repeated valid request", refused_requests},
            {"WT4", "world time: the store (noon, the interval, the stop, a restart, a refused write)", stored_time},
            {"WT5", "world time: the creature points with a time constraint, two observers", phase_points},
            {"WT6", "world time: the load of the phase rule at the size of the world (limit 50 ms for one tick)", phase_load},
            {"WT7", "world time: the creature file of format 4 and its faults", creature_format},
            {"WT8", "world time: the cases phase-* of the check of the creature tool (runs check_creature_tool.py)", creature_tool},
            {"WT9", "world time: the admin command, the development routes, the state of the tool window", time_commands}};
}

} // namespace soulwarden::backend::tests
