// Cases for the sessions of the game server: two sessions with two characters (rule MP-10 of
// the roadmap), and the stored bytes of one session.

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/golden_scenario.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data) {
    static const char* digits = "0123456789abcdef";
    std::string text;
    for (const std::uint8_t byte : data) {
        text += digits[byte >> 4];
        text += digits[byte & 15];
    }
    return text;
}

namespace {

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kSessionTypes = "3 1628 1635 1617 8 8 ";

bool near(float value, float expected) {
    return std::abs(value - expected) < 0.05F;
}

/** T1: A registers, then B registers with a different character. */
void two_sessions(const Options&) {
    ServerHost host;
    const Player alpha =
        add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", state::WorldPosition{100.0F, 200.0F, 50.0F});
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt);

    FakeClient a(27000);
    a.connect(host);
    host.steps(2, {&a});
    host.datagram(a.peer, a.registration(alpha.registration()));
    host.steps(2, {&a});
    note("A got the types: " + a.types());
    check(a.types() == kSessionTypes, "A gets 6 messages: 3, 1628, 1635, 1617, 8, 8");
    {
        // RegistrationResponseMsg: result:u32 serverTime:u64 text text flag x4.
        server::hub::Reader reader(a.got.at(0).body);
        const std::uint32_t result = reader.u32();
        const std::uint64_t serverTime = reader.u64();
        const std::string version = reader.string();
        const std::string second = reader.string();
        const auto flags = reader.raw(4);
        check(reader.ok() && reader.left() == 0 && result == 0 && version == "test-version" && second.empty()
                  && flags.size() == 4 && flags[0] + flags[1] + flags[2] + flags[3] == 0,
              "registration reply of A: result 0, version text, empty text, 4 flags 0, no more bytes");
        check(serverTime == host.clock.server_time_ns() - 20'000'000ULL,
              "registration reply of A: the server time is the time of the world clock at the registration");
    }
    const std::size_t aMessages = a.got.size();

    FakeClient b(27001);
    b.connect(host);
    host.steps(2, {&a, &b});
    host.datagram(b.peer, b.registration(beta.registration()));
    host.steps(2, {&a, &b});
    note("B got the types: " + b.types());
    check(b.types() == kSessionTypes, "B gets 6 messages: 3, 1628, 1635, 1617, 8, 8");
    check(a.got.size() == aMessages, "A gets no message when B connects and registers");

    const Bundle emptyA = read_bundle(a.got.at(4).body);
    const Bundle recordA = read_bundle(a.got.at(5).body);
    const Bundle emptyB = read_bundle(b.got.at(4).body);
    const Bundle recordB = read_bundle(b.got.at(5).body);
    check(emptyA.complete && emptyA.records.empty() && emptyA.sequence == 1 && emptyB.complete && emptyB.records.empty()
              && emptyB.sequence == 1,
          "the first bundle of each session is empty and has the sequence 1");
    check(recordA.complete && recordB.complete && recordA.sequence == 2 && recordB.sequence == 2
              && recordA.records.size() == 1 && recordB.records.size() == 1,
          "the second bundle of each session has one record, decodes to its last byte, and has the sequence 2");
    if (recordA.complete && recordB.complete && recordA.records.size() == 1 && recordB.records.size() == 1) {
        const Record& ra = recordA.records[0];
        const Record& rb = recordB.records[0];
        note("fragments (slot, type) of A: " + order_of(ra));
        check(ra.interest == 1 && rb.interest == 1, "each session uses interest 1 for its own player");
        check(order_of(ra) == "(0,10) (9,3935) (16,11) (41,15) (12,1195) (70,899) (27,129) (62,3183) (60,3786) (61,1755) (65,911) (67,3681) "
                  && order_of(rb) == order_of(ra),
              "12 fragments in the order metadata 0, player 9, movement 16, vitals 41, appearance 12, progression 70, "
              "attribute 27, paperdoll 62, currency 60, container 61, categorical progression 65, progression points 67 "
              "(a character with no ability on a bar gets no ability state)");
        const std::string idA = text_of(ra.fragments[1].fields.at({2, 0}), 1);
        const std::string idB = text_of(rb.fragments[1].fields.at({2, 0}), 1);
        const std::string nameA = text_of(ra.fragments[1].fields.at({2, 1}), 0);
        const std::string nameB = text_of(rb.fragments[1].fields.at({2, 1}), 0);
        check(idA == alpha.characterId && nameA == "Alpha", "the player state of A has the character id and the name of A");
        check(idB == beta.characterId && nameB == "Beta", "the player state of B has the character id and the name of B");
        const Bytes& positionA = ra.fragments[2].fields.at({0, 27});
        const Bytes& positionB = rb.fragments[2].fields.at({0, 27});
        check(float_at(positionA, 0) == 100.0F && float_at(positionA, 4) == 200.0F,
              "A spawns at the position of its row (100, 200)");
        check(float_at(positionB, 0) == 8880.0F && float_at(positionB, 4) == 4250.0F,
              "B has no saved position and spawns at the default position (8880, 4250)");
    }
    check(!a.received_text(beta.characterId) && !a.received_text("Beta"),
          "no datagram for A has the character id or the name of B");
    check(!b.received_text(alpha.characterId) && !b.received_text("Alpha"),
          "no datagram for B has the character id or the name of A");

    // After 200 ms each session sends its own movement state with a new time anchor.
    const std::size_t aBefore = a.got.size();
    const std::size_t bBefore = b.got.size();
    host.steps(21, {&a, &b});
    check(a.got.size() == aBefore + 1 && b.got.size() == bBefore + 1, "after 210 ms each session gets one more message");
    if (a.got.size() == aBefore + 1) {
        const Bundle beat = read_bundle(a.got.back().body);
        check(a.got.back().type == 8 && beat.complete && beat.records.size() == 1 && beat.records[0].interest == 1
                  && order_of(beat.records[0]) == "(16,11) " && beat.records[0].fragments[0].fields.size() == 3
                  && beat.records[0].fragments[0].fields.count({0, 40}) == 1 && beat.records[0].fragments[0].fields.count({0, 0}) == 1
                  && beat.records[0].fragments[0].fields.count({0, 25}) == 1 && beat.sequence == 3,
              "the message is bundle 3: interest 1, one fragment (16, 11) with the time anchor and the id of the state");
    }

    // Input of A: only the position of A changes, and B gets nothing.
    const std::size_t bMessages = b.got.size();
    const std::uint64_t bDatagrams = host.transport.datagrams[b.peer];
    host.datagram(a.peer, a.input(1, 120.5F, 196.0F, 51.65F));
    const world::Entity* entityA = host.world.find_character(alpha.characterId);
    const world::Entity* entityB = host.world.find_character(beta.characterId);
    check(entityA != nullptr && near(entityA->position.x, 120.0F) && near(entityA->position.y, 200.0F)
              && near(entityA->position.z, 50.0F),
          "an input report of A moves the entity of A in the world to (120, 200, 50)");
    check(entityB != nullptr && entityB->position.x == 8880.0F && entityB->position.y == 4250.0F,
          "the entity of B does not move");
    // The world saves a position in its tick.
    host.tick();
    const auto rowA = saved_position(host.store, alpha.characterId);
    check(rowA && near(rowA->x, 120.0F) && near(rowA->y, 200.0F) && near(rowA->z, 50.0F),
          "the next tick of the world writes the row of A (120, 200, 50)");
    check(!saved_position(host.store, beta.characterId).has_value(), "the row of B has no position");
    check(b.got.size() == bMessages && host.transport.datagrams[b.peer] == bDatagrams, "the input of A sends nothing to B");
}

std::vector<Bytes> read_golden(const std::filesystem::path& path) {
    std::vector<Bytes> datagrams;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        Bytes data;
        for (std::size_t index = 0; index + 1 < line.size(); index += 2) {
            data.push_back(static_cast<std::uint8_t>(std::stoul(line.substr(index, 2), nullptr, 16)));
        }
        datagrams.push_back(std::move(data));
    }
    return datagrams;
}

/** T2: the bytes of one session are equal to the stored bytes. */
void golden_bytes(const Options& options) {
    ServerHost host(kGoldenVersion);
    const std::vector<Bytes> datagrams = run_golden_scenario(host);
    std::size_t total = 0;
    for (const Bytes& data : datagrams) {
        total += data.size();
    }
    // The content of the datagrams, for the reader of the output.
    FakeClient reader(27000);
    reader.read(datagrams);
    note("the server sent " + std::to_string(datagrams.size()) + " datagrams with " + std::to_string(total)
         + " bytes; message types: " + reader.types());
    const auto row = saved_position(host.store, kGoldenCharacterId);
    check(row && near(row->x, 120.0F) && near(row->y, 200.0F) && near(row->z, 50.0F),
          "the input report of the scenario was read: the row has the position (120, 200, 50)");

    if (options.goldenDirectory.empty()) {
        check(false, "no folder of the stored files (option --golden-dir)");
        return;
    }
    const std::filesystem::path path = options.goldenDirectory / "one_session.hex";
    if (options.writeGolden) {
        std::ofstream output(path);
        output << "# Stored bytes of one session (case T2 of soulwarden-backend-tests).\n"
               << "# Each line is one clear datagram that the game server sends to the client in the scenario\n"
               << "# of tests/support/golden_scenario.h, in sequence. All ids of the scenario are made up.\n"
               << "# The test program writes this file again only with the option --write-golden\n"
               << "# (tools/test-soulwarden-backend.sh --write-golden).\n";
        for (const Bytes& data : datagrams) {
            output << hex_text(data) << "\n";
        }
        check(output.good(), "the stored file was written again: " + path.filename().string());
        return;
    }
    const std::vector<Bytes> expected = read_golden(path);
    check(!expected.empty(), "the stored file " + path.filename().string() + " has datagrams");
    check(datagrams.size() == expected.size(),
          "the number of datagrams is equal: " + std::to_string(datagrams.size()) + " and " + std::to_string(expected.size()));
    std::size_t different = 0;
    for (std::size_t index = 0; index < datagrams.size() && index < expected.size(); ++index) {
        if (datagrams[index] != expected[index]) {
            if (different == 0) {
                note("first difference: datagram " + std::to_string(index));
                note("  now:    " + hex_text(datagrams[index]));
                note("  stored: " + hex_text(expected[index]));
            }
            ++different;
        }
    }
    check(different == 0 && datagrams.size() == expected.size(), "each datagram is equal to the stored datagram");
    check(reader.types().rfind(kSessionTypes, 0) == 0 && reader.got.size() > 6,
          "the datagrams have the 6 messages of a registration and then movement states");
}

} // namespace

std::vector<Case> session_cases() {
    return {
        {"T1", "two sessions: each one gets its own character and nothing of the other", two_sessions},
        {"T2", "stored bytes: the datagrams for one session are equal to the stored file", golden_bytes},
    };
}

} // namespace soulwarden::backend::tests
