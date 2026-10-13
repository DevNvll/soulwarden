// Cases for the exact position of a character (stage 2.3a): the reader of the position field
// of an input report, the check of the world for a position report, the two position sources
// of a session, two sessions, and the id of the movement state. Each fixture is made from the
// layout of the protocol notes (4.7), with made-up ids: no capture is a source of these bytes.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

#include "server/rep/client_input.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "world/position_report.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

using server::rep::InputTail;
using server::rep::read_client_input;
using world::PositionReport;
using world::PositionTrack;
using world::PositionVerdict;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr float kHeight = 50.0F;
/** One simulation step of the client: 30 each second. */
constexpr std::chrono::microseconds kTick{33333};
/** Distance of one step of a sprint (5.5 m/s). */
constexpr float kSprintStep = 5.5F / 30.0F;

/** The height that the backend reads for a height that a client sends. */
float sent_height(float height) {
    return height_of_wire(wire_height(height));
}

/**
 * @return True when two heights are the same value of the wire. The compiler can compute a
 *         constant height with a fused multiply and add, which gives a value that is one
 *         float step from the value of the backend at run time.
 */
bool same_height(float left, float right) {
    return std::abs(left - right) < 0.0001F;
}

std::string position_text(const state::WorldPosition& position) {
    char text[96];
    std::snprintf(text, sizeof(text), "%.3f %.3f %.3f", position.x, position.y, position.z);
    return text;
}

std::string row_text(ServerHost& host, const char* characterId) {
    const auto row = saved_position(host.store, characterId);
    return row ? position_text(*row) : "no position";
}

/** One simulation step of a client: the clock goes one step on, the report arrives, and the server ticks. */
void step(ServerHost& host, FakeClient& client, const InputEntry& entry) {
    host.clock.advance(kTick);
    host.datagram(client.peer, client.input_report(entry));
    host.tick();
    client.exchange(host);
}

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** @return The number of state fragments that a client got for an interest number and a slot. */
std::size_t fragments_for(const FakeClient& client, std::uint32_t interest, std::uint32_t slot) {
    std::size_t count = 0;
    for (const Sent& sent : client.got) {
        if (sent.type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(sent.body).records) {
            count += record.interest == interest && fragment_of(record, slot) != nullptr ? 1 : 0;
        }
    }
    return count;
}

/** @return The newest movement state (slot 16) that a client got for its own character, or null. */
std::optional<Fragment> newest_movement(const FakeClient& client) {
    std::optional<Fragment> newest;
    for (const Sent& sent : client.got) {
        if (sent.type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(sent.body).records) {
            const Fragment* fragment = record.interest == 1 ? fragment_of(record, 16) : nullptr;
            if (fragment != nullptr) {
                newest = *fragment;
            }
        }
    }
    return newest;
}

/** @return The id of a movement state: idAbs has the high 8 bits and idRel the low 8 bits. -1: the state has no id. */
int state_id(const std::optional<Fragment>& movement) {
    if (!movement || field_of(*movement, 0, 0).size() != 1 || field_of(*movement, 0, 25).size() != 1) {
        return -1;
    }
    return (field_of(*movement, 0, 25)[0] << 8) | field_of(*movement, 0, 0)[0];
}

/** PR1: the reader of an input report, on fixtures with and without the position field. */
void parser_fixtures(const Options&) {
    // The entry of a live client with the position report of the game hook, at rest: the mask
    // 0x0014B3 and the flags 0x20084F0 (live captures of stage 2.3a).
    const InputEntry live = report_entry(4321, 8861.25F, 4252.75F, 138.2F);
    const Bytes liveBody = input_entry_body(live);
    auto input = read_client_input(liveBody);
    check(hex_text(liveBody).find("b3140010e1") != std::string::npos,
          "the fixture of a client with the hook has the mask of a live entry (b3 14 00: bits 0, 1, 4, 5, 7, 10, 12) "
          "before its input number");
    check(input && input->sequence == 4321 && input->flags == 0x20084F0ULL && input->hasCamera && !input->hasLook
              && input->cameraX == 8861.75F && input->cameraY == 4248.75F && input->targetKind == 3,
          "entry with the position flag: the reader gives the input number, the flags 0x20084F0, and the camera");
    check(input && input->tail == InputTail::read && input->hasCharacter && input->characterX == 8861.25F
              && input->characterY == 4252.75F && input->characterHeight == wire_height(138.2F)
              && same_height(input->characterZ, sent_height(138.2F)) && input->characterZ <= 138.2F
              && 138.2F - input->characterZ < 0.017F,
          "entry with the position flag: the position of the character is x and y of the entry, and the height is 0 m to "
          "0.017 m below the height of the client (the client cuts the 16 bits off)");
    note("height 138.2 on the wire: " + std::to_string(wire_height(138.2F)) + ", read as "
         + std::to_string(input ? input->characterZ : 0.0F));

    // The same entry of a client with no hook: flags 0x20004F0, and mask bit 11 is the byte of entry +0x470.
    InputEntry plain = live;
    plain.flags = kInputFlagsWorld;
    plain.hasCharacter = false;
    input = read_client_input(input_entry_body(plain));
    check(hex_text(input_entry_body(plain)).find("b3080010e1") != std::string::npos && input && input->flags == 0x20004F0ULL
              && input->tail == InputTail::read && !input->hasCharacter && input->hasCamera && input->cameraX == 8861.75F,
          "entry of a client with no hook (flags 0x20004F0, mask b3 08 00): the reader gets to the place of the position, "
          "and the entry has none");

    // The flag with no field: a simulation step of the client with no physics step.
    InputEntry noField = live;
    noField.hasCharacter = false;
    input = read_client_input(input_entry_body(noField));
    check(input && (input->flags & server::rep::kInputFlagPosition) != 0 && input->tail == InputTail::read
              && !input->hasCharacter,
          "entry with the position flag and with no position field: no position, and it is not an error");

    // Fields before the position: each one moves the place or the mask bit of the position.
    InputEntry target = live;
    target.targetKind = 1;
    input = read_client_input(input_entry_body(target));
    check(input && input->targetKind == 1 && input->hasCharacter && input->characterX == 8861.25F
              && input->characterY == 4252.75F,
          "entry with target data (a kind that is not 3): the reader passes its 17 bytes and reads the position");
    InputEntry action = live;
    action.flags |= 0x1;
    input = read_client_input(input_entry_body(action));
    check(input && input->flags == 0x20084F1ULL && input->hasCharacter && input->characterX == 8861.25F,
          "entry with flag bit 0 (the action report, three numbers): the position is read");
    InputEntry many = live;
    many.flags |= 0x800 | 0x100 | 0x4000;
    many.position360 = true;
    many.positionPair = true;
    many.value3F8 = true;
    many.value380 = 0;
    input = read_client_input(input_entry_body(many));
    check(input && input->hasCharacter && input->characterX == 8861.25F && input->characterY == 4252.75F
              && input->characterHeight == wire_height(138.2F),
          "entry with the fields of the flag bits 11, 8, and 14 and the byte of entry +0x380: the position has mask bit 14, "
          "and it is read");
    InputEntry bitsOnly = live;
    bitsOnly.flags |= 0x800 | 0x100 | 0x4000;
    input = read_client_input(input_entry_body(bitsOnly));
    check(input && input->hasCharacter && input->characterX == 8861.25F,
          "entry with the flag bits 11, 8, and 14 and none of their fields: each flag takes its mask bits, and the "
          "position is read");

    // Blocks with a layout that is not known: the reader stops, and the entry keeps its camera.
    InputEntry block = live;
    block.block140 = true;
    input = read_client_input(input_entry_body(block));
    const bool blockStop = input && input->tail == InputTail::unknownBlock && !input->hasCharacter && input->hasCamera
                           && input->cameraX == 8861.75F;
    InputEntry list = live;
    list.lists[1] = 2;
    input = read_client_input(input_entry_body(list));
    const bool listStop = input && input->tail == InputTail::unknownBlock && !input->hasCharacter && input->hasCamera;
    InputEntry value = live;
    value.value380 = 1;
    input = read_client_input(input_entry_body(value));
    const bool valueStop = input && input->tail == InputTail::unknownBlock && !input->hasCharacter && input->hasCamera;
    check(blockStop && listStop && valueStop,
          "entry with the block of mask bit 8, with a list that is not empty, or with a value of entry +0x380 that is not "
          "0: the reader stops, the entry gives no position and keeps its camera");

    // A body that is cut.
    InputEntry cut = live;
    cut.value470 = false;
    cut.cut = 3;
    input = read_client_input(input_entry_body(cut));
    check(input && input->tail == InputTail::shortBody && !input->hasCharacter && input->hasCamera && input->sequence == 4321,
          "a body that ends in the position field: no position, and the entry keeps its input number and its camera");
    cut.cut = 40;
    check(!read_client_input(input_entry_body(cut)), "a body that ends before the camera position does not parse");

    // The entry of the older tests: the look direction and the camera only.
    input = read_client_input(input_body(7, 120.5F, 196.0F, 51.65F));
    check(input && input->sequence == 7 && input->flags == 0 && input->hasLook && input->hasCamera
              && input->tail == InputTail::read && !input->hasCharacter,
          "entry with no flags: no position");
}

/** PR2: the check of a position report: the sequence, the numbers, the two limit values of the height. */
void check_sequence_and_numbers(const Options&) {
    const state::WorldPosition spawn{100.0F, 200.0F, sent_height(kHeight)};
    const auto report = [](std::uint16_t input, float x, float y = 200.0F) {
        return PositionReport{input, state::WorldPosition{x, y, sent_height(kHeight)}, wire_height(kHeight)};
    };
    const auto start = world::WorldClock::time_point(std::chrono::seconds(1000));

    {
        PositionTrack track;
        auto now = start;
        const bool first = track.judge(report(10, 100.0F), spawn, now) == PositionVerdict::accepted;
        now += kTick;
        const bool same = track.judge(report(10, 100.1F), spawn, now) == PositionVerdict::outOfSequence;
        const bool older = track.judge(report(9, 100.1F), spawn, now) == PositionVerdict::outOfSequence;
        const bool halfRange = track.judge(report(10 + 0x8000, 100.1F), spawn, now) == PositionVerdict::outOfSequence;
        const bool newer = track.judge(report(11, 100.1F), spawn, now) == PositionVerdict::accepted;
        now += kTick;
        const bool gap = track.judge(report(15, 100.2F), spawn, now) == PositionVerdict::accepted;
        check(first && same && older && halfRange && newer && gap && track.count(PositionVerdict::outOfSequence) == 3
                  && track.count(PositionVerdict::accepted) == 3 && track.refused() == 0,
              "sequence: a newer input number is accepted (also after a gap); the same number, an older number, and a "
              "number that is 0x8000 ahead are not used, and they are not counted as refused");
    }
    {
        PositionTrack track;
        auto now = start;
        bool wrap = true;
        float x = 100.0F;
        for (const std::uint16_t number : {std::uint16_t{0xFFFE}, std::uint16_t{0xFFFF}, std::uint16_t{0}, std::uint16_t{1}}) {
            wrap = wrap && track.judge(report(number, x), spawn, now) == PositionVerdict::accepted;
            now += kTick;
            x += kSprintStep;
        }
        check(wrap && track.restarts() == 0, "sequence: the wrap of the 16 bits (0xFFFF, then 0) is a newer number");
    }
    {
        // The input numbers of the client start again: 3000, then 0xFFF0 and on.
        PositionTrack track;
        auto now = start;
        bool ok = track.judge(report(3000, 100.0F), spawn, now) == PositionVerdict::accepted;
        unsigned notUsed = 0;
        unsigned accepted = 0;
        std::chrono::nanoseconds firstAccepted{0};
        for (unsigned index = 0; index < 90; ++index) {
            now += kTick;
            const PositionVerdict verdict =
                track.judge(report(static_cast<std::uint16_t>(0xFFF0 + index), 100.0F), spawn, now);
            notUsed += verdict == PositionVerdict::outOfSequence ? 1 : 0;
            if (verdict == PositionVerdict::accepted && accepted++ == 0) {
                firstAccepted = now - start;
            }
        }
        check(ok && notUsed == 60 && accepted == 30 && track.restarts() == 1
                  && firstAccepted >= std::chrono::milliseconds(2000) && firstAccepted < std::chrono::milliseconds(2040),
              "sequence: when each report of 2 s has a number that is not newer, the session takes the new sequence (60 "
              "reports not used, then each report accepted)");
        note("first accepted report " + std::to_string(std::chrono::duration<double>(firstAccepted).count())
             + " s after the last newer number");
    }
    {
        PositionTrack track;
        auto now = start;
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const float infinity = std::numeric_limits<float>::infinity();
        bool ok = track.judge(report(1, 100.0F), spawn, now) == PositionVerdict::accepted;
        now += kTick;
        const bool notNumber = track.judge(report(2, nan), spawn, now) == PositionVerdict::notFinite;
        now += kTick;
        const bool endless = track.judge(report(3, 100.0F, infinity), spawn, now) == PositionVerdict::notFinite;
        now += kTick;
        PositionReport low = report(4, 100.0F);
        low.height = 0;
        low.position.z = -100.0F;
        const bool lowLimit = track.judge(low, spawn, now) == PositionVerdict::heightLimit;
        now += kTick;
        PositionReport high = report(5, 100.0F);
        high.height = 0xFFFF;
        high.position.z = 1000.0F;
        const bool highLimit = track.judge(high, spawn, now) == PositionVerdict::heightLimit;
        now += kTick;
        // The value next to a limit is a height of the range: a character at that height.
        PositionTrack lowTrack;
        PositionReport nearLow = report(6, 100.0F);
        nearLow.height = 1;
        nearLow.position.z = height_of_wire(1);
        const bool nearLimit =
            lowTrack.judge(nearLow, state::WorldPosition{100.0F, 200.0F, -99.9F}, now) == PositionVerdict::accepted;
        now += kTick;
        const bool outside = track.judge(report(7, 100.0F, 2.0e6F), spawn, now) == PositionVerdict::outsideWorld;
        now += kTick;
        ok = ok && track.judge(report(8, 100.1F), spawn, now) == PositionVerdict::accepted;
        check(notNumber && endless, "numbers: a position with a number that is not finite is refused");
        check(lowLimit && highLimit && nearLimit,
              "numbers: the two limit values of the height (0x0000 and 0xFFFF) are refused, and the value next to a limit "
              "is accepted");
        check(outside && ok && track.refused() == 5 && track.count(PositionVerdict::notFinite) == 2
                  && track.count(PositionVerdict::heightLimit) == 2 && track.count(PositionVerdict::outsideWorld) == 1,
              "numbers: a position outside the limits of the world is refused; the track counts each cause, and a correct "
              "report after them is accepted");
    }
}

/** PR3: the speed check: the peak speed of a step, the speed of the time window, the first report, the time rule. */
void check_speed(const Options&) {
    const state::WorldPosition spawn{100.0F, 200.0F, sent_height(kHeight)};
    const auto report = [](std::uint16_t input, float x, float y = 200.0F) {
        return PositionReport{input, state::WorldPosition{x, y, sent_height(kHeight)}, wire_height(kHeight)};
    };
    const auto start = world::WorldClock::time_point(std::chrono::seconds(1000));

    {
        // A sprint: 5.5 m/s for 3 s.
        PositionTrack track;
        auto now = start;
        bool sprint = true;
        float x = 100.0F;
        for (std::uint16_t number = 1; number <= 90; ++number) {
            sprint = sprint && track.judge(report(number, x), spawn, now) == PositionVerdict::accepted;
            now += kTick;
            x += kSprintStep;
        }
        check(sprint && track.refused() == 0 && std::abs(track.largest().tick - kSprintStep) < 0.001F
                  && std::abs(track.largest().stepSpeed - 5.5F) < 0.05F && std::abs(track.largest().window - 5.5F) < 0.2F,
              "speed: each report of a sprint of 3 s (5.5 m/s) is accepted; the track has the largest distance for one "
              "tick (0.183 m) and for one window (5.5 m in 1 s)");
        note("largest tick " + std::to_string(track.largest().tick) + " m, largest step speed "
             + std::to_string(track.largest().stepSpeed) + " m/s, largest window " + std::to_string(track.largest().window)
             + " m in " + std::to_string(track.largest().windowSeconds) + " s");

        // A jump of 100 m in one tick, then the sprint again.
        const float last = x - kSprintStep;
        const bool jump = track.judge(report(91, last + 100.0F), spawn, now) == PositionVerdict::tooFast;
        now += kTick;
        const bool back = track.judge(report(92, last + 2.0F * kSprintStep), spawn, now) == PositionVerdict::accepted;
        check(jump && back && track.count(PositionVerdict::tooFast) == 1,
              "speed: a jump of 100 m in one tick is refused, and the next report on the path of the sprint is accepted");
    }
    {
        // The peak speed of one step.
        PositionTrack track;
        auto now = start;
        bool ok = track.judge(report(1, 100.0F), spawn, now) == PositionVerdict::accepted;
        now += kTick;
        const float below = world::position_limits::kPeakSpeed / 30.0F * 0.9F;
        const float above = world::position_limits::kPeakSpeed / 30.0F * 1.1F;
        const bool peakRefused = track.judge(report(2, 100.0F + above), spawn, now) == PositionVerdict::tooFast;
        const bool peakAccepted = track.judge(report(3, 100.0F + below), spawn, now) == PositionVerdict::accepted;
        check(ok && peakRefused && peakAccepted,
              "speed: one step above the peak speed is refused, and one step below it is accepted (a dodge is faster "
              "than a sprint for some ticks)");
    }
    {
        // A speed between the two limits for a long time: the time window stops it.
        PositionTrack track;
        auto now = start;
        bool ok = track.judge(report(1, 100.0F), spawn, now) == PositionVerdict::accepted;
        const float fast = (world::position_limits::kWindowSpeed + world::position_limits::kPeakSpeed) / 2.0F;
        float claimed = 100.0F;
        float acceptedAt = 100.0F;
        unsigned refused = 0;
        for (std::uint16_t number = 2; number <= 91; ++number) {
            now += kTick;
            claimed += fast / 30.0F;
            if (track.judge(report(number, claimed), spawn, now) == PositionVerdict::accepted) {
                acceptedAt = claimed;
            } else {
                ++refused;
            }
        }
        const float limit = world::position_limits::kWindowSpeed * 3.0F + world::position_limits::kWindowMargin
                            + world::position_limits::kWindowSpeed * 0.25F;
        check(ok && refused > 0 && acceptedAt - 100.0F <= limit && claimed - 100.0F > limit,
              "speed: a client that moves faster than the window speed for 3 s gets refusals: its accepted position is "
              "not farther than the window speed permits for that time, plus the margin");
        note("claimed " + std::to_string(claimed - 100.0F) + " m in 3 s at " + std::to_string(fast) + " m/s; accepted "
             + std::to_string(acceptedAt - 100.0F) + " m; " + std::to_string(refused) + " of 90 reports refused");
    }
    {
        // The first report of a session.
        PositionTrack nearTrack;
        const bool nearSpawn = nearTrack.judge(report(200, 104.0F), spawn, start) == PositionVerdict::accepted;
        PositionTrack farTrack;
        auto now = start;
        unsigned refused = 0;
        std::chrono::nanoseconds acceptedAfter{0};
        for (std::uint16_t number = 200; number < 200 + 300; ++number) {
            if (farTrack.judge(report(number, 150.0F), spawn, now) == PositionVerdict::accepted) {
                acceptedAfter = now - start;
                break;
            }
            ++refused;
            now += kTick;
        }
        const double seconds = std::chrono::duration<double>(acceptedAfter).count();
        check(nearSpawn && refused > 0 && seconds > 5.9 && seconds < 6.2,
              "speed: the first report is accepted when it is 5 m or less from the spawn position; a first report 50 m "
              "away is refused until the window speed permits the distance (after 6.0 s)");
        note("first report 50 m from the spawn: " + std::to_string(refused) + " reports refused, accepted after "
             + std::to_string(seconds) + " s");
    }
    {
        // The height: a jump, a fall, and a step that is too large.
        const auto at_height = [](std::uint16_t input, float z) {
            return PositionReport{input, state::WorldPosition{100.0F, 200.0F, z}, wire_height(z)};
        };
        PositionTrack track;
        auto now = start;
        const float top = 250.0F;
        bool ok = track.judge(at_height(1, top), state::WorldPosition{100.0F, 200.0F, top}, now) == PositionVerdict::accepted;
        float z = top;
        // A jump: 0.22 m up in the first tick (the largest rise of the live session).
        now += kTick;
        z += 0.22F;
        ok = ok && track.judge(at_height(2, z), spawn, now) == PositionVerdict::accepted;
        // A step up that is too large: 2 m in one tick.
        now += kTick;
        const bool riseRefused = track.judge(at_height(3, z + 2.0F), spawn, now) == PositionVerdict::tooFast;
        // A fall with more speed in each tick, to 1.8 m in one tick (54 m/s, the largest fall of the live session).
        bool fall = true;
        std::uint16_t number = 3;
        for (float speed = 0.33F; speed <= 54.0F; speed += 0.33F) {
            now += kTick;
            z -= speed / 30.0F;
            fall = fall && track.judge(at_height(++number, z), spawn, now) == PositionVerdict::accepted;
        }
        // A step down that is too large: 10 m in one tick.
        now += kTick;
        const bool fallRefused = track.judge(at_height(++number, z - 10.0F), spawn, now) == PositionVerdict::tooFast;
        check(ok && fall && track.largest().fall > 1.7F && track.largest().fall < 1.9F,
              "height: a jump (0.22 m up in one tick) and a fall of 150 m (to 1.8 m down in one tick) are accepted");
        check(riseRefused && fallRefused && track.count(PositionVerdict::tooFast) == 2,
              "height: a rise of 2 m in one tick and a fall of 10 m in one tick are refused");
        note("the fall: " + std::to_string(top + 0.22F - z) + " m, largest fall for one tick "
             + std::to_string(track.largest().fall) + " m (" + std::to_string(track.largest().fallSpeed) + " m/s)");
    }
    {
        // The time between two reports.
        PositionTrack together;
        auto now = start;
        bool ok = together.judge(report(1, 100.0F), spawn, now) == PositionVerdict::accepted;
        float x = 100.0F;
        // 7 reports arrive at the same time of the world clock (a delay of the network).
        for (std::uint16_t number = 2; number <= 8; ++number) {
            x += kSprintStep;
            ok = ok && together.judge(report(number, x), spawn, now) == PositionVerdict::accepted;
        }
        check(ok, "time: reports of a sprint that arrive together are accepted (the time is the time of their input numbers)");

        PositionTrack jump;
        now = start;
        ok = jump.judge(report(1, 100.0F), spawn, now) == PositionVerdict::accepted;
        now += kTick;
        // The input number says 100 s, the world clock says one tick.
        const bool largeNumber = jump.judge(report(3001, 120.0F), spawn, now) == PositionVerdict::tooFast;
        check(ok && largeNumber,
              "time: a large step of the input number gives a client no more time than the world clock plus 0.25 s (20 m "
              "in one tick with the input number 3000 ahead is refused)");

        PositionTrack slow;
        now = start;
        ok = slow.judge(report(1, 100.0F), spawn, now) == PositionVerdict::accepted;
        x = 100.0F;
        // A client with 5 simulation steps each second: each step has 0.2 s and 1.1 m.
        for (std::uint16_t number = 2; number <= 16; ++number) {
            now += std::chrono::milliseconds(200);
            x += 1.1F;
            ok = ok && slow.judge(report(number, x), spawn, now) == PositionVerdict::accepted;
        }
        check(ok, "time: a client with a slow picture (one step of 1.1 m each 0.2 s, 5.5 m/s) is accepted (the time is "
                  "the time of the world clock when it is longer)");
    }
}

/** PR4: a session whose client sends its position: the entity has the exact value, and a refused report changes nothing. */
void exact_position(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, kHeight});
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    const world::Entity* entity = host.world.find_character(kCharacterA);
    check(entity != nullptr && entity->positionEstimated && entity->position.x == 100.0F,
          "before the first report: the entity has the position of the row, as an estimate");

    // The character falls 0.2 m to the ground and stands.
    step(host, a, report_entry(1, 100.0F, 200.0F, 49.8F));
    check(entity->position.x == 100.0F && entity->position.y == 200.0F
              && same_height(entity->position.z, sent_height(49.8F)) && !entity->positionEstimated,
          "the first report: the entity has the position of the report, and the position is not an estimate");
    check(log_count("rep: position source: the position field of the input reports, the exact position (input 1, flags "
                    "0x20084f0")
              == 1,
          "the log names the source: the position field of the input reports");

    // A walk of 2 s to the east. The camera of each report is at a different place: it has no effect.
    float x = 100.0F;
    bool follows = true;
    for (std::uint16_t number = 2; number <= 61; ++number) {
        x += kSprintStep;
        InputEntry entry = report_entry(number, x, 200.0F, 49.8F);
        entry.cameraX = 500.0F;
        entry.cameraY = 900.0F;
        step(host, a, entry);
        follows = follows && entity->position.x == x && entity->position.y == 200.0F;
    }
    check(follows && std::abs(x - 111.0F) < 0.01F,
          "a walk of 2 s: the entity has the position of each report (11 m), and the camera of the reports does not move "
          "it");
    check(log_count("(client report)") >= 2 && log_count("(camera estimate)") == 0,
          "the log line of each second names the source \"client report\"");

    // A report that the world refuses changes nothing.
    const state::WorldPosition before = entity->position;
    const world::Revision positionRevision = entity->positionRevision;
    const world::Revision revision = host.world.revision();
    const std::string row = row_text(host, kCharacterA);
    const std::size_t messages = a.got.size();
    step(host, a, report_entry(62, x + 100.0F, 200.0F, 49.8F));
    InputEntry lowHeight = report_entry(63, x, 200.0F, 49.8F);
    lowHeight.characterHeight = 0;
    step(host, a, lowHeight);
    InputEntry notNumber = report_entry(64, std::numeric_limits<float>::quiet_NaN(), 200.0F, 49.8F);
    notNumber.cameraX = 100.0F;
    step(host, a, notNumber);
    const server::rep::RepSession& session = host.session(1);
    check(entity->position.x == before.x && entity->position.y == before.y && entity->position.z == before.z
              && entity->positionRevision == positionRevision && host.world.revision() == revision
              && row_text(host, kCharacterA) == row && !entity->positionEstimated,
          "a jump of 100 m in one tick, a height at the limit, and a number that is not finite: the entity, the "
          "revision of the world, and the row do not change");
    std::size_t bundles = 0;
    for (std::size_t index = messages; index < a.got.size(); ++index) {
        bundles += a.got[index].type == 8 ? 1 : 0;
    }
    check(session.position_track().refused() == 3 && session.position_track().count(PositionVerdict::tooFast) == 1
              && session.position_track().count(PositionVerdict::heightLimit) == 1
              && session.position_track().count(PositionVerdict::notFinite) == 1
              && log_count("rep: position report refused (too fast): input 62") == 1
              && log_count("rep: position report refused (the height is a limit value of the range): input 63") == 1
              && log_count("rep: position report refused (a number is not finite): input 64") == 1 && bundles <= 1,
          "the session counts each refused report, the log has one line for the first one of each cause, and the client "
          "gets no reply for a refused report");

    // The walk continues from the accepted position. The row gets the exact value.
    step(host, a, report_entry(65, x + kSprintStep, 200.0F, 49.8F));
    const float last = x + kSprintStep;
    check(entity->position.x == last, "the next report on the path is accepted");
    host.steps(520, {&a});
    const auto saved = saved_position(host.store, kCharacterA);
    check(saved && saved->x == last && saved->y == 200.0F && saved->z == entity->position.z
              && same_height(saved->z, sent_height(49.8F)),
          "5 s later the row has the exact position of the entity");

    // The session ends: the log has the counts. The next session gets the row, 0.5 m higher.
    host.datagram(a.peer, a.carrier_close());
    host.steps(2, {&a});
    check(log_count("rep: position reports of the session: 62 accepted, 3 refused (1 too fast, 1 not finite, 1 at a "
                    "height limit, 0 outside the world), 0 not in sequence")
              == 1,
          "the end of the session writes the counts of its position reports to the log");
    FakeClient again(27001);
    host.enter(again, alpha.registration());
    const auto movement = newest_movement(again);
    const world::Entity* second = host.world.find_character(kCharacterA);
    // The encoder rounds the height of a spawn to the nearest step.
    const auto spawnHeight =
        static_cast<std::uint16_t>(std::lround((second->position.z + 0.5F + 100.0F) / 1100.0F * 65535.0F));
    check(second != nullptr && second->positionEstimated && movement && float_at(movement->fields.at({0, 27}), 0) == last
              && float_at(movement->fields.at({0, 27}), 4) == 200.0F
              && movement->fields.at({0, 27})[8] == static_cast<std::uint8_t>(spawnHeight >> 8)
              && movement->fields.at({0, 27})[9] == static_cast<std::uint8_t>(spawnHeight),
          "the next session of the character gets the saved position as its spawn position, 0.5 m higher");
}

/** PR5: the camera estimate is the fallback: a client with no position field, and a client whose field stops. */
void camera_fallback(const Options&) {
    {
        // A client with no position report of the game hook.
        ServerHost host;
        const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                        state::WorldPosition{100.0F, 200.0F, kHeight});
        FakeClient a(27000);
        host.enter(a, alpha.registration());
        const world::Entity* entity = host.world.find_character(kCharacterA);
        InputEntry entry;
        entry.flags = kInputFlagsWorld;
        entry.sequence = 1;
        entry.cameraX = 120.5F;
        entry.cameraY = 196.0F;
        entry.cameraZ = kHeight + 1.65F;
        step(host, a, entry);
        check(std::abs(entity->position.x - 120.0F) < 0.01F && std::abs(entity->position.y - 200.0F) < 0.01F
                  && entity->positionEstimated && host.session(1).position_track().count(PositionVerdict::accepted) == 0,
              "a client with no position field: the entity gets the estimate from the camera (120, 200), as before the "
              "stage");
        check(log_count("rep: position source: the camera estimate (input 1, flags 0x20004f0: the input report has no "
                        "position flag")
                  == 1
                  && log_count("(camera estimate)") == 1,
              "the log names the source: the camera estimate, and the cause (no position flag)");
        host.datagram(a.peer, a.carrier_close());
        host.steps(2, {&a});
        check(log_count("rep: position reports of the session: none (the client sent no position field)") == 1
                  && row_text(host, kCharacterA).rfind("120.000 200.000", 0) == 0,
              "the end of the session saves the estimate, and the log says that the client sent no position field");
    }
    {
        // A client with the hook whose reports lose the field.
        ServerHost host;
        const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                        state::WorldPosition{100.0F, 200.0F, kHeight});
        FakeClient a(27000);
        host.enter(a, alpha.registration());
        const world::Entity* entity = host.world.find_character(kCharacterA);
        std::uint16_t number = 0;
        for (int index = 0; index < 10; ++index) {
            step(host, a, report_entry(++number, 100.0F, 200.0F, kHeight));
        }
        // For 0.5 s the entries have the flag and no field, and the camera moves 3 m.
        bool kept = true;
        for (int index = 0; index < 15; ++index) {
            InputEntry entry = report_entry(++number, 100.0F, 200.0F, kHeight);
            entry.hasCharacter = false;
            entry.cameraX += 0.2F * static_cast<float>(index + 1);
            step(host, a, entry);
            kept = kept && entity->position.x == 100.0F && !entity->positionEstimated;
        }
        check(kept && log_count("rep: position source changed") == 0,
              "reports with no position field for 0.5 s: the entity keeps its last position, and the camera does not "
              "move it");
        // The field stops for 2 s: the reader stops at a block that it does not know.
        for (int index = 0; index < 60; ++index) {
            InputEntry entry = report_entry(++number, 100.0F, 200.0F, kHeight);
            entry.block140 = true;
            // The camera of a character that walks east, 0.18 m in each step.
            entry.cameraX = 100.5F + kSprintStep * static_cast<float>(index + 1);
            entry.actionWords = 0;
            step(host, a, entry);
        }
        const float estimate = 100.0F + kSprintStep * 60.0F;
        check(log_count("rep: position source changed: the camera estimate") == 1
                  && log_count("the reader of the input report stops before the position field") == 1
                  && entity->positionEstimated && std::abs(entity->position.x - estimate) < 0.3F,
              "no position field for more than 1 s: the source changes to the camera estimate with one log line, and "
              "the entity follows the camera");
        note("entity at x " + std::to_string(entity->position.x) + " after a camera path to " + std::to_string(estimate));
        // The field comes back.
        const float x = entity->position.x;
        step(host, a, report_entry(++number, x + 0.1F, 200.0F, kHeight));
        check(log_count("rep: position source changed: the position field of the input reports") == 1
                  && entity->position.x == x + 0.1F && !entity->positionEstimated,
              "the position field comes again: the source changes back with one log line, and the entity has the exact "
              "value");
        check(log_count("the entry has a block with a layout that is not known before the position of the character") == 1,
              "the log has one line for the first report whose reader stopped");
    }
    {
        // Target data: the log shows the first report of a session that has it.
        ServerHost host;
        const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                        state::WorldPosition{100.0F, 200.0F, kHeight});
        FakeClient a(27000);
        host.enter(a, alpha.registration());
        InputEntry entry = report_entry(1, 100.0F, 200.0F, kHeight);
        entry.targetKind = 0;
        step(host, a, entry);
        entry.sequence = 2;
        step(host, a, entry);
        check(log_count("has target data of the kind 0: the first report of the session with target data") == 1
                  && host.world.find_character(kCharacterA)->position.x == 100.0F
                  && host.session(1).position_track().count(PositionVerdict::accepted) == 2,
              "a report with target data: the log has one line for the first one, and its position is used");
    }
}

/** PR6: two sessions: each entity has only the position of its own session (rule MP-10). */
void two_sessions(const Options&) {
    ServerHost host(observing());
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, kHeight});
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta",
                                   state::WorldPosition{110.0F, 200.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, alpha.registration());
    host.enter(b, beta.registration());
    const world::Entity* entityA = host.world.find_character(kCharacterA);
    const world::Entity* entityB = host.world.find_character(kCharacterB);

    // The two characters walk in different directions for 1 s. The interest scan runs: each
    // client observes the other character.
    float ax = 100.0F;
    float by = 200.0F;
    for (std::uint16_t number = 1; number <= 30; ++number) {
        ax -= kSprintStep;
        by += kSprintStep;
        host.clock.advance(kTick);
        host.datagram(a.peer, a.report_at(number, ax, 200.0F, kHeight));
        host.datagram(b.peer, b.report_at(static_cast<std::uint16_t>(number + 500), 110.0F, by, kHeight));
        host.tick();
        a.exchange(host);
        b.exchange(host);
    }
    host.steps(60, {&a, &b});
    check(entityA->position.x == ax && entityA->position.y == 200.0F && entityB->position.x == 110.0F
              && entityB->position.y == by && !entityA->positionEstimated && !entityB->positionEstimated,
          "two sessions: each entity has the position that its own session reported, and no value of the other session");
    check(fragments_for(a, 2, 16) == 1 && fragments_for(b, 2, 16) == 1,
          "each client observes the other character: one movement state for it (the record of a remote player)");

    // A report of A that the world refuses changes nothing: not for A, and not for B.
    const state::WorldPosition beforeA = entityA->position;
    const state::WorldPosition beforeB = entityB->position;
    const world::Revision revision = host.world.revision();
    const std::string rowA = row_text(host, kCharacterA);
    const std::string rowB = row_text(host, kCharacterB);
    const std::size_t messagesB = b.got.size();
    host.clock.advance(kTick);
    host.datagram(a.peer, a.report_at(31, ax - 100.0F, 200.0F, kHeight));
    host.tick();
    a.exchange(host);
    b.exchange(host);
    host.steps(30, {&a, &b});
    std::size_t recordsOfA = 0;
    for (std::size_t index = messagesB; index < b.got.size(); ++index) {
        if (b.got[index].type == 8) {
            for (const Record& record : read_bundle(b.got[index].body).records) {
                recordsOfA += record.interest == 2 ? 1 : 0;
            }
        }
    }
    check(entityA->position.x == beforeA.x && entityA->position.y == beforeA.y && entityB->position.x == beforeB.x
              && entityB->position.y == beforeB.y && host.world.revision() == revision && row_text(host, kCharacterA) == rowA
              && row_text(host, kCharacterB) == rowB && recordsOfA == 0,
          "a jump of 100 m of A is refused: the entity of A, the entity of B, the two rows, and the revision of the world "
          "do not change, and the client of B gets no record for A");
    check(host.session(1).position_track().refused() == 1 && host.session(2).position_track().refused() == 0
              && host.session(1).position_track().count(PositionVerdict::accepted) == 30
              && host.session(2).position_track().count(PositionVerdict::accepted) == 30
              && log_count("rep: position report refused (too fast): input 31") == 1,
          "each session has its own counts: one refused report for A, and none for B");

    // B continues: its reports are accepted with no effect of the refusal of A. The input
    // numbers of the two clients have no relation.
    for (std::uint16_t number = 31; number <= 40; ++number) {
        by += kSprintStep;
        host.clock.advance(kTick);
        host.datagram(b.peer, b.report_at(static_cast<std::uint16_t>(number + 500), 110.0F, by, kHeight));
        host.tick();
        b.exchange(host);
    }
    check(entityB->position.y == by && entityA->position.x == beforeA.x && host.session(2).position_track().refused() == 0,
          "B walks on: each report of B is accepted, and the entity of A stays");

    // The rows: each character has its own exact position.
    host.steps(520, {&a, &b});
    const auto savedA = saved_position(host.store, kCharacterA);
    const auto savedB = saved_position(host.store, kCharacterB);
    check(savedA && savedB && savedA->x == beforeA.x && savedA->y == 200.0F && savedB->x == 110.0F && savedB->y == by,
          "each row has the position of its own character");
}

/** PR7: the id of the movement state of the owner (the heartbeat state). */
void movement_id(const Options&) {
    using server::rep::movement_state_id;
    bool twoValues = true;
    unsigned nearest = 0x10000;
    for (unsigned number = 0; number < 0x10000; ++number) {
        const std::uint16_t id = movement_state_id(static_cast<std::uint16_t>(number));
        twoValues = twoValues && (id == 0x8000 || id == 0x0000);
        const auto ahead = static_cast<std::uint16_t>(id - number);
        const auto behind = static_cast<std::uint16_t>(number - id);
        nearest = std::min<unsigned>(nearest, std::min(ahead, behind));
    }
    check(twoValues && nearest == 0x4000,
          "id rule: for each of the 65,536 input numbers the id is 0x8000 or 0x0000, and it is 0x4000 (16,384 steps, 9 "
          "minutes) or more from the number: the client has no predicted state with it (it keeps 60)");
    check(movement_state_id(0) == 0x8000 && movement_state_id(1) == 0x8000 && movement_state_id(0x3FFF) == 0x8000
              && movement_state_id(0x4000) == 0x0000 && movement_state_id(0xBFFF) == 0x0000
              && movement_state_id(0xC000) == 0x8000 && movement_state_id(0xFFFF) == 0x8000,
          "id rule: 0x8000 for the numbers 0xC000 to 0xFFFF and 0x0000 to 0x3FFF, and 0x0000 for 0x4000 to 0xBFFF");

    ManualClock clock;
    const std::uint64_t anchor = clock.server_time_ns() / 1000;
    char anchorText[17];
    std::snprintf(anchorText, sizeof(anchorText), "%016llx", static_cast<unsigned long long>(anchor));
    check(hex_text(server::rep::encode_movement_heartbeat(clock, 0)) == std::string("01f90000800040") + "00" + "80" + anchorText
              && hex_text(server::rep::encode_movement_heartbeat(clock, 0x4000)) == std::string("01f90000800040") + "00" + "00" + anchorText,
          "heartbeat state: group 0 with the fields idRel (0), idAbs (25), and timeAnchor (40): 01 f9 00 00 80 00 40, "
          "idRel 00, idAbs 80 (or 00), and the time anchor");

    // A session: the heartbeat follows the newest input number of its client.
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, kHeight});
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    const auto spawnState = newest_movement(a);
    check(spawnState && spawnState->fields.count({0, 27}) == 1 && state_id(spawnState) == -1,
          "the full movement state of the owner (the spawn) has no id");
    host.steps(30, {&a});
    const int before = state_id(newest_movement(a));
    const auto id_after = [&](std::uint16_t number) {
        host.clock.advance(kTick);
        host.datagram(a.peer, a.report_at(number, 100.0F, 200.0F, kHeight));
        host.steps(30, {&a});
        const auto movement = newest_movement(a);
        return movement && movement->fields.size() == 3 ? state_id(movement) : -2;
    };
    const int early = id_after(1);
    const int low = id_after(0x3FFF);
    const int middle = id_after(0x4000);
    const int high = id_after(0xBFFF);
    const int wrap = id_after(0xC000);
    const int last = id_after(0xFFFF);
    const int again = id_after(0x0005);
    check(before == 0x8000, "before the first input report, the heartbeat of a session has the id 0x8000");
    check(early == 0x8000 && low == 0x8000 && middle == 0x0000 && high == 0x0000 && wrap == 0x8000 && last == 0x8000
              && again == 0x8000,
          "the heartbeat of a session has the id of the rule for the newest input number of its client (1, 0x3FFF, "
          "0x4000, 0xBFFF, 0xC000, 0xFFFF, and 5 after the wrap), and only the three fields");
}

/**
 * PR8: the height of the first report of a session (code review of stage 2.3a, finding 1). The
 * first report has the position of the entity as its reference for the height too.
 */
void first_report_height(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, kHeight});
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    const world::Entity* entity = host.world.find_character(kCharacterA);
    const float before = entity->position.z;
    step(host, a, report_entry(1, 100.0F, 200.0F, 900.0F));
    const world::PositionTrack& track = host.session(1).position_track();
    note("first report 850 m above the saved height: height of the entity before " + std::to_string(before) + ", after "
         + std::to_string(entity->position.z) + "; accepted " + std::to_string(track.count(PositionVerdict::accepted))
         + ", too fast " + std::to_string(track.count(PositionVerdict::tooFast)));
    check(entity->position.z == before && track.count(PositionVerdict::accepted) == 0
              && track.count(PositionVerdict::tooFast) == 1,
          "a first report 850 m above the saved position is refused (too fast), and the entity keeps its height");
    host.datagram(a.peer, a.carrier_close());
    host.steps(2, {&a});
    check(saved_position(host.store, kCharacterA)->z == before, "the refused height is not in the saved row after the session end");

    // The limits of the first report, with no session: 5 m up and down at the time of the
    // first report, and the rise or the fall of the time after it.
    const state::WorldPosition known{100.0F, 200.0F, sent_height(kHeight)};
    const auto first = world::WorldClock::time_point(std::chrono::seconds(1000));
    const auto verdict = [&](float height, std::chrono::milliseconds later) {
        PositionTrack fresh;
        // A report that the check refuses sets the time of the first report.
        const PositionReport far{1, state::WorldPosition{100.0F, 200.0F, sent_height(900.0F)}, wire_height(900.0F)};
        const bool refused = fresh.judge(far, known, first) == PositionVerdict::tooFast;
        const PositionReport report{2, state::WorldPosition{100.0F, 200.0F, sent_height(height)}, wire_height(height)};
        return refused ? fresh.judge(report, known, first + later) : PositionVerdict::noEntity;
    };
    using std::chrono::milliseconds;
    check(verdict(kHeight + 4.9F, milliseconds(0)) == PositionVerdict::accepted
              && verdict(kHeight - 4.9F, milliseconds(0)) == PositionVerdict::accepted
              && verdict(kHeight + 5.2F, milliseconds(0)) == PositionVerdict::tooFast
              && verdict(kHeight - 5.2F, milliseconds(0)) == PositionVerdict::tooFast,
          "at the time of the first report: 4.9 m above or below the saved height is accepted, and 5.2 m is refused");
    check(verdict(kHeight + 24.0F, milliseconds(1000)) == PositionVerdict::accepted
              && verdict(kHeight + 26.0F, milliseconds(1000)) == PositionVerdict::tooFast
              && verdict(kHeight - 120.0F, milliseconds(1000)) == PositionVerdict::accepted
              && verdict(kHeight - 126.0F, milliseconds(1000)) == PositionVerdict::tooFast,
          "1 s after the first report: a rise of 24 m and a fall of 120 m are accepted (20 m/s and 120 m/s plus 5 m), "
          "and a rise of 26 m and a fall of 126 m are refused");
}

/**
 * PR9: the height over the time window (code review of stage 2.3a, finding 2). A client with
 * larger steps of its input number gets more time for each report from the time rule of two
 * reports: the total rise and the total fall of the time window have the time of the world
 * clock, with the delay value one time.
 */
void height_over_the_window(const Options&) {
    ServerHost host;
    const Player alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                                    state::WorldPosition{100.0F, 200.0F, kHeight});
    FakeClient a(27000);
    host.enter(a, alpha.registration());
    step(host, a, report_entry(1, 100.0F, 200.0F, kHeight));
    const world::Entity* entity = host.world.find_character(kCharacterA);
    const auto start = host.clock.now();
    const float before = entity->position.z;
    // 30 reports in 1 s. Each one has an input number that is 2 higher and a height that is
    // 1.2 m higher: 36 m in 1 s. The time rule of two reports permits 1.33 m for each one.
    for (unsigned index = 1; index <= 30; ++index) {
        step(host, a, report_entry(static_cast<std::uint16_t>(1 + 2 * index), 100.0F, 200.0F,
                                   kHeight + 1.2F * static_cast<float>(index)));
    }
    const float elapsed = std::chrono::duration<float>(host.clock.now() - start).count();
    const float rise = entity->position.z - before;
    const world::PositionTrack& track = host.session(1).position_track();
    note("input number steps of 2 with 1.2 m each: " + std::to_string(elapsed) + " s, rise of the entity "
         + std::to_string(rise) + " m, accepted " + std::to_string(track.count(PositionVerdict::accepted)) + ", refused "
         + std::to_string(track.refused()));
    check(rise <= world::position_limits::kRiseSpeed * (elapsed + 0.25F) + 0.017F && track.refused() > 0,
          "the entity does not rise faster than 20 m/s for the time of the world clock plus the delay value one time "
          "(25 m in 1 s), and reports above that are refused");

    // 101 reports at one value of the world clock, each one 4 m higher with an input number
    // that is 8 higher: a claim of 400 m in no time.
    PositionTrack burst;
    const state::WorldPosition known{100.0F, 200.0F, sent_height(kHeight)};
    const auto now = world::WorldClock::time_point(std::chrono::seconds(1000));
    unsigned accepted = 0;
    float highest = kHeight;
    for (unsigned index = 0; index <= 100; ++index) {
        const float z = kHeight + 4.0F * static_cast<float>(index);
        const PositionReport report{static_cast<std::uint16_t>(1 + 8 * index),
                                    state::WorldPosition{100.0F, 200.0F, sent_height(z)}, wire_height(z)};
        if (burst.judge(report, known, now) == PositionVerdict::accepted) {
            ++accepted;
            highest = z;
        }
    }
    note("101 reports at one clock value, 4 m higher each: accepted " + std::to_string(accepted) + ", highest accepted height "
         + std::to_string(highest - kHeight) + " m above the start");
    check(accepted == 2 && highest - kHeight <= world::position_limits::kRiseSpeed * 0.25F,
          "only the first report and one more are accepted: the total rise at one clock value is 5 m at most (20 m/s for "
          "the delay value of 0.25 s)");

    // The same rule for a fall: 120 m/s for the time of the world clock plus the delay value.
    PositionTrack fall;
    accepted = 0;
    float lowest = kHeight;
    for (unsigned index = 0; index <= 100; ++index) {
        const float z = kHeight - 25.0F * static_cast<float>(index);
        const PositionReport report{static_cast<std::uint16_t>(1 + 8 * index),
                                    state::WorldPosition{100.0F, 200.0F, sent_height(z)}, wire_height(z)};
        if (z > -90.0F && fall.judge(report, known, now) == PositionVerdict::accepted) {
            ++accepted;
            lowest = z;
        }
    }
    check(accepted == 2 && kHeight - lowest <= world::position_limits::kFallSpeed * 0.25F,
          "a fall at one clock value is 30 m at most (120 m/s for the delay value)");

    // A real fall still passes: 1.8 m in each tick (54 m/s, the largest value of the live
    // session of stage 2.3a), with one input number for each tick, for 2 s.
    PositionTrack real;
    auto time = now;
    bool all = real.judge(PositionReport{1, state::WorldPosition{100.0F, 200.0F, sent_height(600.0F)}, wire_height(600.0F)},
                          state::WorldPosition{100.0F, 200.0F, sent_height(600.0F)}, time)
               == PositionVerdict::accepted;
    for (unsigned index = 1; index <= 60; ++index) {
        time += kTick;
        const float z = 600.0F - 1.8F * static_cast<float>(index);
        all = all && real.judge(PositionReport{static_cast<std::uint16_t>(1 + index),
                                               state::WorldPosition{100.0F, 200.0F, sent_height(z)}, wire_height(z)},
                                known, time)
                         == PositionVerdict::accepted;
    }
    check(all, "a fall of 108 m in 2 s (1.8 m in each tick) is accepted: each report passes the check of the time window");
}

} // namespace

std::vector<Case> position_cases() {
    return {
        {"PR1", "position report: the reader of the input report, with and without the position field", parser_fixtures},
        {"PR2", "position report: the check of the sequence, of the numbers, and of the two limit values of the height", check_sequence_and_numbers},
        {"PR3", "position report: the speed check (a sprint, a jump of 100 m, the time window, the first report, the height, the time rule)", check_speed},
        {"PR4", "position report: the entity and the row have the exact value; a refused report changes nothing", exact_position},
        {"PR5", "position report: the camera estimate is the fallback (no position field, a field that stops)", camera_fallback},
        {"PR6", "position report: two sessions have their own positions, and a refused report of one changes nothing for the other", two_sessions},
        {"PR7", "movement state: the id rule of the heartbeat state", movement_id},
        {"PR8", "position report: the height of the first report of a session (review of stage 2.3a)", first_report_height},
        {"PR9", "position report: the total rise and fall of the time window (review of stage 2.3a)", height_over_the_window},
    };
}

} // namespace soulwarden::backend::tests
