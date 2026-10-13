// Cases for the session ids (capture folders, log marks, client context instance), for the
// session end causes, and for the research switch.

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <regex>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {

std::string hex_text(const Bytes& data);

namespace {

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr float kHeight = 50.0F;

struct TwoPlayers {
    Player alpha;
    Player beta;
    explicit TwoPlayers(ServerHost& host)
        : alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha",
                           state::WorldPosition{100.0F, 200.0F, kHeight})),
          beta(add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", std::nullopt)) {}
};

bool row_is(ServerHost& host, const std::string& characterId, float x, float y) {
    const auto row = saved_position(host.store, characterId);
    return row && std::abs(row->x - x) < 0.05F && std::abs(row->y - y) < 0.05F;
}

std::string row_text(ServerHost& host, const std::string& characterId) {
    const auto row = saved_position(host.store, characterId);
    if (!row) {
        return "no position";
    }
    char text[64];
    std::snprintf(text, sizeof(text), "(%.2f, %.2f, %.2f)", row->x, row->y, row->z);
    return text;
}

/** @return The x and y of the spawn position in the player record of a client. */
std::pair<float, float> spawn_of(const FakeClient& client) {
    for (const Sent& sent : client.got) {
        if (sent.type != 8) {
            continue;
        }
        const Bundle bundle = read_bundle(sent.body);
        for (const Record& record : bundle.records) {
            for (const Fragment& fragment : record.fragments) {
                const auto position = fragment.fields.find({0, 27});
                if (fragment.type == 11 && position != fragment.fields.end()) {
                    return {float_at(position->second, 0), float_at(position->second, 4)};
                }
            }
        }
    }
    return {0.0F, 0.0F};
}

std::size_t end_lines(const std::string& cause) {
    return log_count("rep: session ended, cause: " + cause);
}

/**
 * A walk of a character in the world: the first input report puts it at (x, y) and the row
 * gets this position. One second later a second report puts it `step` meters to the east:
 * the save timer of 5 s did not come, thus this position is not in the row.
 */
void walk(ServerHost& host, FakeClient& client, float x, float y, float step) {
    host.datagram(client.peer, client.input_at(1, x, y, kHeight));
    host.steps(100, {&client});
    host.datagram(client.peer, client.input_at(31, x + step, y, kHeight));
    host.steps(1, {&client});
}

std::vector<std::string> file_names(const std::filesystem::path& directory) {
    std::vector<std::string> names;
    if (std::filesystem::is_directory(directory)) {
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            names.push_back(entry.path().filename().string());
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

/** @return True when the names are capture files with a counter from 0 with no gap. */
bool one_counter(const std::vector<std::string>& names) {
    const std::regex form("^[0-9]{5}-(in|out|msg)-[0-9]+\\.bin$");
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (!std::regex_match(names[index], form) || std::stoul(names[index].substr(0, 5)) != index) {
            return false;
        }
    }
    return !names.empty();
}

std::string file_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

/** T8: the capture files of each session are in its own folder with its own counter. */
void capture_folders(const Options& options) {
    const std::filesystem::path captures = options.workDirectory / "captures-t8";
    service::RepServerOptions serverOptions;
    serverOptions.serverVersion = "test-version";
    serverOptions.captureDirectory = captures.string();
    ServerHost host(serverOptions);
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    host.steps(30, {&a, &b});

    check(file_names(captures) == std::vector<std::string>{"s0001", "s0002"},
          "the capture directory has the folders s0001 and s0002 and no other file");
    const std::vector<std::string> first = file_names(captures / "s0001");
    const std::vector<std::string> second = file_names(captures / "s0002");
    note("s0001 has " + std::to_string(first.size()) + " files, s0002 has " + std::to_string(second.size()) + " files");
    check(one_counter(first) && one_counter(second), "each folder has its own file counter from 00000 with no gap");
    const auto registration_of = [&](const std::filesystem::path& folder, const std::vector<std::string>& names) {
        std::string text;
        for (const std::string& name : names) {
            if (name.find("-msg-") != std::string::npos) {
                text += file_text(folder / name);
            }
        }
        return text;
    };
    const std::string messagesA = registration_of(captures / "s0001", first);
    const std::string messagesB = registration_of(captures / "s0002", second);
    check(messagesA.find(kCharacterA) != std::string::npos && messagesA.find(kCharacterB) == std::string::npos,
          "the messages of s0001 have the registration of A and nothing of B");
    check(messagesB.find(kCharacterB) != std::string::npos && messagesB.find(kCharacterA) == std::string::npos,
          "the messages of s0002 have the registration of B and nothing of A");
}

/** M1: each log line of a session names the session, and each session has its own log limit. */
void log_marks(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    // More packets of A than the limit of the hexadecimal log lines (96 for each session).
    for (std::uint16_t number = 1; number <= 120; ++number) {
        host.datagram(a.peer, a.input_at(number, 100.0F, 200.0F, kHeight));
        host.steps(1, {&a});
    }
    FakeClient b(27001);
    host.enter(b, players.beta.registration());

    const std::regex mark(" \\[s[0-9]+( [0-9a-f]{8})?\\]$");
    std::size_t sessionLines = 0;
    std::size_t withMark = 0;
    std::string firstWithout;
    for (const std::string& line : log_lines()) {
        if (line.rfind("rep: ", 0) != 0 && line.rfind("carrier: ", 0) != 0) {
            continue;
        }
        ++sessionLines;
        if (std::regex_search(line, mark)) {
            ++withMark;
        } else if (firstWithout.empty()) {
            firstWithout = line;
        }
    }
    note(std::to_string(sessionLines) + " log lines of the game server");
    if (!firstWithout.empty()) {
        note("line with no mark: " + firstWithout);
    }
    check(sessionLines > 20 && withMark == sessionLines, "each log line 'rep:' and 'carrier:' ends with a session mark");
    check(log_count("rep: secure session open, client port 27000 [s1]") == 1
              && log_count("rep: secure session open, client port 27001 [s2]") == 1,
          "before the registration the mark is the session id: [s1] and [s2]");
    check(log_count("accepted [s1 aaaaaaaa]") == 1 && log_count("accepted [s2 bbbbbbbb]") == 1,
          "from the registration on, the mark has the first 8 characters of the character id");
    check(log_count("rep: local player record sent [s1 aaaaaaaa]") == 1 && log_count("carrier: connection established") == 2,
          "the lines of the carrier and of the session have the mark of their session");
    const auto hexLines = [](const std::string& markText) {
        std::size_t count = 0;
        for (const std::string& line : log_lines()) {
            const bool hexLine = line.rfind("rep: in ", 0) == 0 || line.rfind("rep: out ", 0) == 0
                                 || line.rfind("rep: message on channel", 0) == 0;
            count += hexLine && line.find(markText) != std::string::npos ? 1 : 0;
        }
        return count;
    };
    note("hexadecimal log lines: " + std::to_string(hexLines("[s1")) + " of session 1, " + std::to_string(hexLines("[s2"))
         + " of session 2");
    check(hexLines("[s1") <= 96 && hexLines("[s1") > 10, "session 1 has hexadecimal log lines for its first 96 packets only");
    check(log_count("rep: message on channel 0") >= 2 && hexLines("[s2") >= 3,
          "session 2 has its own limit: its registration has a hexadecimal log line after 120 packets of session 1");
}

/** C1: each session has its own client context instance. */
void context_instance(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    const Bundle bundleA = read_bundle(a.got.at(5).body);
    const Bundle bundleB = read_bundle(b.got.at(5).body);
    check(bundleA.complete && bundleA.instance == 1, "session 1 has the client context instance 1 (the value of the stored bytes)");
    check(bundleB.complete && bundleB.instance == 2, "session 2 has the client context instance 2");
    // LevelInfoChangedMsg ends with instance:u8 flag flag version:u64.
    const Bytes& levelA = a.got.at(2).body;
    const Bytes& levelB = b.got.at(2).body;
    std::vector<std::size_t> where;
    for (std::size_t index = 0; index < levelA.size() && index < levelB.size(); ++index) {
        if (levelA[index] != levelB[index]) {
            where.push_back(index);
        }
    }
    const std::size_t size = levelA.size();
    check(a.got.at(2).type == 1635 && size == levelB.size() && where == std::vector<std::size_t>{size - 11, size - 1},
          "the level information of the two sessions is different only in the instance and in the version");
    check(levelA[size - 11] == 1 && levelB[size - 11] == 2 && levelA[size - 1] == 1 && levelB[size - 1] == 2,
          "session 1 has instance 1 and version 1 (the values of the stored bytes), session 2 has instance 2 and version 2");
    const Bytes& selfA = a.got.at(1).body;
    const Bytes& selfB = b.got.at(1).body;
    // identity (flags, 16 bytes), 3 actor references (36 bytes each), level block (instance:u32 ...), identity.
    std::vector<std::size_t> selfWhere;
    for (std::size_t index = 0; index < selfA.size() && index < selfB.size(); ++index) {
        if (selfA[index] != selfB[index]) {
            selfWhere.push_back(index);
        }
    }
    const std::size_t levelBlock = 17 + 3 * 36;
    const std::size_t lastIdentity = selfA.size() - 16;
    check(a.got.at(1).type == 1628 && selfA.size() == selfB.size()
              && selfWhere == std::vector<std::size_t>{1, 17 + 36 + 4, levelBlock + 3, lastIdentity},
          "the self identification of the two sessions is different only in the actor id of the session (the two "
          "identities and reference 2) and in the instance of its level block");
    check(selfA[1] == 0x02 && selfB[1] == 0x04 && hex_text(Bytes(selfA.begin() + 1, selfA.begin() + 17))
                                                      == "02000020415a4f5402000000415a4f54",
          "the session actor of session 1 is 02000020 415a4f54 02000000 415a4f54, and session 2 has the next serial");
    check(hex_text(Bytes(selfA.begin() + 17, selfA.begin() + 17 + 36))
                  == "00000001" "00000020415a4f5401000000415a4f54" "38a737ff6cad4792a2f9324736e0ee02"
              && hex_text(Bytes(selfA.begin() + 17 + 72, selfA.begin() + 17 + 108)) == std::string(72, '0'),
          "reference 1 is the player manager of the world (hub 1), and reference 3 is null");
    // A second connection of one client: the same peer address with the next port.
    host.datagram(a.peer, a.carrier_close());
    host.steps(2, {&b});
    FakeClient again(27002);
    host.enter(again, players.alpha.registration());
    const Bundle bundleAgain = read_bundle(again.got.at(5).body);
    check(bundleAgain.complete && bundleAgain.instance == 3,
          "the client of session 1 connects again: its new session has instance 3, not 1");
}

/** E1: a session with no registration request ends 30 s after its start. */
void end_no_registration(const Options&) {
    ServerHost host;
    FakeClient a(27000);
    a.connect(host);
    // The client sends acknowledgements, thus the session is not idle. It does not register.
    host.steps(2990, {&a});
    check(end_lines("") == 0 && host.server->session_count() == 1, "after 29.9 s the session is alive");
    host.steps(12, {&a});
    check(end_lines("no registration") == 1, "after 30 s the log has the end with the cause 'no registration'");
    check(host.transport.close_count(a.peer) == 0, "the game server sends no close notice to the client");
    check(log_count("position") == 0, "a session with no character saves no position");
    host.steps(100, {&a});
    check(end_lines("") == 1 && host.server->session_count() == 0, "the session ends one time and goes away");
}

/** E2: a session with no datagram ends after 30 s, saves its position, and gets no more data. */
void end_idle(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    walk(host, a, 120.0F, 200.0F, 3.0F);
    check(row_is(host, kCharacterA, 120.0F, 200.0F), "before the stop of the client the row has (120, 200): " + row_text(host, kCharacterA));
    check(!row_is(host, kCharacterA, 123.0F, 200.0F), "the last position (123, 200) is not in the row: the save timer of 5 s did not come");
    // The client stops: no datagram and no acknowledgement. The server ticks each 10 ms.
    std::vector<std::uint64_t> bytes;
    std::uint64_t last = host.transport.bytes[a.peer];
    while (host.silence(a.peer) < std::chrono::milliseconds(29990)) {
        host.steps(1, {});
        if (host.silence(a.peer) == std::chrono::seconds(10) || host.silence(a.peer) == std::chrono::seconds(20)) {
            bytes.push_back(host.transport.bytes[a.peer] - last);
            last = host.transport.bytes[a.peer];
        }
    }
    check(end_lines("") == 0, "29.99 s after the last datagram the session is alive");
    check(row_is(host, kCharacterA, 123.0F, 200.0F) && log_count("rep: position 123.00 200.00 50.00 saved [s1 aaaaaaaa]") == 1,
          "the save timer of the world wrote the last position (123, 200) with no input of the client");
    check(host.world.find_character(kCharacterA) != nullptr, "the entity of the character is in the world");
    host.steps(1, {});
    check(end_lines("idle") == 1, "30 s after the last datagram the log has the end with the cause 'idle'");
    check(host.world.find_character(kCharacterA) == nullptr && host.world.entities().empty(),
          "the end takes the entity of the character out of the world");
    check(row_is(host, kCharacterA, 123.0F, 200.0F), "the row has the last position (123, 200): " + row_text(host, kCharacterA));
    check(log_count("rep: position 123.00 200.00 50.00 saved") == 1, "the end does not write the same position a second time");
    check(host.transport.close_count(a.peer) == 0, "the game server sends no close notice to the client");
    check(host.server->session_count() == 1, "the end only marks the session: it is in the table in the same tick");
    const std::uint64_t atEnd = host.transport.bytes[a.peer];
    host.steps(1, {});
    check(host.server->session_count() == 0, "the next tick erases the session");
    host.disconnect(a.peer);
    host.steps(1000, {});
    note("bytes for the client that stopped, for each 10 s before the end: " + std::to_string(bytes.at(0)) + ", "
         + std::to_string(bytes.at(1)) + "; in 10 s after the end: " + std::to_string(host.transport.bytes[a.peer] - atEnd));
    check(bytes.at(1) > bytes.at(0), "before the end the bytes for a client that stopped increase");
    check(host.transport.bytes[a.peer] == atEnd, "after the end the client gets no byte in 10 s");
    check(end_lines("") == 1 && log_count("saved") == 2, "no second end and no second save");
}

/** E3: the client closes the carrier connection (it leaves the world). */
void end_carrier_close(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    walk(host, a, 120.0F, 200.0F, 3.0F);
    check(row_is(host, kCharacterA, 120.0F, 200.0F), "before the close the row has (120, 200), and the character is at (123, 200)");
    host.datagram(a.peer, a.carrier_close());
    check(end_lines("carrier close") == 1, "the log has the end with the cause 'carrier close' at once");
    check(row_is(host, kCharacterA, 123.0F, 200.0F), "the row has the position of that moment (123, 200): " + row_text(host, kCharacterA));
    check(log_count("rep: position 123.00 200.00 50.00 saved [s1 aaaaaaaa]") == 1, "the end saves the position one time");
    check(host.server->session_count() == 2, "the end only marks the session: the erase is not in the call of the carrier");
    const std::uint64_t atEnd = host.transport.bytes[a.peer];
    const std::size_t bMessages = b.got.size();
    host.steps(1, {&b});
    check(host.server->session_count() == 1, "the next tick erases the session");
    host.disconnect(a.peer);
    host.steps(100, {&b});
    check(host.transport.bytes[a.peer] == atEnd, "the client that left gets no byte after its end");
    check(b.got.size() > bMessages, "the other session continues to get its movement states");
    check(end_lines("") == 1 && log_count("123.00 200.00 50.00 saved") == 1, "no second end and no second save");
    check(!saved_position(host.store, kCharacterB).has_value(), "the row of the other character did not change");
}

/** E4: the secure session closes (DTLS close notice, read error, or a new handshake from the port). */
void end_secure_session(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    host.enter(a, players.alpha.registration());
    walk(host, a, 120.0F, 200.0F, 3.0F);
    host.disconnect(a.peer);
    check(end_lines("secure session closed") == 1, "the log has the end with the cause 'secure session closed'");
    check(row_is(host, kCharacterA, 123.0F, 200.0F) && log_count("123.00 200.00 50.00 saved") == 1,
          "the end saves the last position one time: " + row_text(host, kCharacterA));
    check(host.transport.close_count(a.peer) == 0, "the game server does not close the peer of the transport");
    host.steps(1, {});
    check(host.server->session_count() == 0, "the next tick erases the session");
}

/** E5: the backend stops. */
void end_backend_stop(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    host.enter(a, players.alpha.registration());
    host.enter(b, players.beta.registration());
    walk(host, a, 120.0F, 200.0F, 3.0F);
    walk(host, b, 300.0F, 400.0F, 2.0F);
    host.server->stop();
    check(end_lines("backend stop") == 2, "each session ends with the cause 'backend stop'");
    check(row_is(host, kCharacterA, 123.0F, 200.0F) && row_is(host, kCharacterB, 302.0F, 400.0F),
          "each row has the last position of its character: " + row_text(host, kCharacterA) + " and " + row_text(host, kCharacterB));
    check(log_count("123.00 200.00 50.00 saved") == 1 && log_count("302.00 400.00 50.00 saved") == 1,
          "each end saves the position one time");
    check(host.server->session_count() == 0, "no session stays");
    host.server.reset();
    check(log_count("saved") == 4, "the destructor of a session saves nothing");
}

/**
 * E6: the fault of the backend before stage 0.6a. The user leaves the world, enters again,
 * walks, and stops the game: the position of the first session came back at the stop.
 */
void old_position_does_not_come_back(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient first(27000);
    host.enter(first, players.alpha.registration());
    walk(host, first, 120.0F, 200.0F, 3.0F);
    host.datagram(first.peer, first.carrier_close());
    check(row_is(host, kCharacterA, 123.0F, 200.0F), "session 1 leaves the world at (123, 200), and the row has this position");
    host.steps(1900, {});
    FakeClient second(27001);
    host.enter(second, players.alpha.registration());
    const auto spawn = spawn_of(second);
    check(spawn.first == 123.0F && spawn.second == 200.0F, "session 2 of the same character spawns at (123, 200)");
    host.datagram(second.peer, second.input_at(1, 300.0F, 400.0F, kHeight));
    host.steps(700, {&second});
    check(row_is(host, kCharacterA, 300.0F, 400.0F), "session 2 walks to (300, 400), and the row has this position");
    host.server->stop();
    check(row_is(host, kCharacterA, 300.0F, 400.0F),
          "after the stop of the backend the row has the last position (300, 400): " + row_text(host, kCharacterA));
    check(end_lines("carrier close") == 1 && end_lines("backend stop") == 1, "session 1 ended with its cause before the stop");
}

/**
 * E7: two sessions of one character at the same time. The newer session wins: the older one
 * loses the character, gets the message "kicked", and ends. Only the newest one moves the
 * character.
 */
void newest_session_writes(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient first(27000);
    host.enter(first, players.alpha.registration());
    walk(host, first, 120.0F, 200.0F, 3.0F);
    // The client of session 1 goes away with no close. A new session registers the character.
    FakeClient second(27001);
    host.enter(second, players.alpha.registration());
    const auto spawn = spawn_of(second);
    check(spawn.first == 123.0F && spawn.second == 200.0F,
          "session 2 gets the position of the entity, which session 1 moved last: it spawns at (123, 200)");
    check(log_count("world: character " + std::string(kCharacterA) + " is in session s1") == 1,
          "the log names the older session of the character");
    first.read(host.take(first.peer));
    check(first.types().find("271") != std::string::npos, "session 1 gets the message 'kicked' (type 271)");
    host.datagram(first.peer, first.input_at(40, 700.0F, 800.0F, kHeight));
    host.datagram(second.peer, second.input_at(1, 300.0F, 400.0F, kHeight));
    // The change of the owner wrote the row: the save timer of the world comes 5 s later.
    host.steps(510, {&second});
    check(row_is(host, kCharacterA, 300.0F, 400.0F),
          "session 2 walks to (300, 400), and the row has this position. An input of session 1 changes nothing");
    check(end_lines("replaced by a newer session") == 1, "session 1 ended with the cause 'replaced by a newer session'");
    host.disconnect(first.peer);
    check(row_is(host, kCharacterA, 300.0F, 400.0F) && host.world.entities().size() == 1,
          "the end of session 1 changes no row and no entity: " + row_text(host, kCharacterA));
    host.server->stop();
    check(row_is(host, kCharacterA, 300.0F, 400.0F), "after the stop the row has the position of session 2");
}

/**
 * E8: three sessions of one character, one after the other (case RV2 of the review of stage
 * 0.6a). Each new session takes the entity with its last position, and a session that lost
 * the character changes nothing after that.
 */
void three_sessions_of_one_character(const Options&) {
    ServerHost host;
    const TwoPlayers players(host);
    FakeClient a(27000);
    FakeClient b(27001);
    FakeClient c(27002);
    host.enter(a, players.alpha.registration());
    host.datagram(a.peer, a.input_at(1, 120.0F, 200.0F, kHeight));
    host.steps(100, {&a});
    host.datagram(a.peer, a.input_at(2, 123.0F, 200.0F, kHeight));
    host.enter(b, players.alpha.registration());
    check(row_is(host, kCharacterA, 123.0F, 200.0F), "B takes the entity: the row gets the last position of A (123, 200)");
    host.datagram(b.peer, b.input_at(1, 300.0F, 400.0F, kHeight));
    host.steps(100, {&a, &b});
    host.datagram(b.peer, b.input_at(2, 303.0F, 400.0F, kHeight));
    host.enter(c, players.alpha.registration());
    check(row_is(host, kCharacterA, 303.0F, 400.0F), "C takes the entity: the row gets the last position of B (303, 400)");
    host.datagram(c.peer, c.input_at(1, 500.0F, 600.0F, kHeight));
    host.steps(100, {&a, &b, &c});
    host.datagram(c.peer, c.input_at(2, 503.0F, 600.0F, kHeight));
    host.datagram(c.peer, c.carrier_close());
    host.datagram(a.peer, a.input_at(3, 700.0F, 800.0F, kHeight));
    host.datagram(b.peer, b.input_at(3, 900.0F, 1000.0F, kHeight));
    host.steps(600, {&a, &b});
    host.disconnect(b.peer);
    host.disconnect(a.peer);
    host.server->stop();
    check(row_is(host, kCharacterA, 503.0F, 600.0F),
          "after C ends, the inputs and the ends of A and B do not change the row of C: " + row_text(host, kCharacterA));
    check(log_count("rep: position 503.00 600.00 50.00 saved") == 1, "C saves its last position one time");
    check(end_lines("replaced by a newer session") == 2 && host.world.entities().empty(),
          "A and B ended with the cause 'replaced', and the world has no entity");
}

/** R1: the research switch is off by default, and its three messages have the bytes of the protocol notes. */
void research_switch(const Options&) {
    {
        server::rep::ResearchSwitch research;
        check(!research.active() && !service::RepServerOptions{}.research.active(), "the research switch is off by default");
        check(server::rep::read_research_option("registration-result=-160", research) && research.registrationResult == -160
                  && server::rep::read_research_option("reject=2", research) && research.rejectReason == 2U
                  && server::rep::read_research_option("kick=1", research) && research.kickReason == 1U
                  && server::rep::read_research_option("kick-delay=3", research)
                  && research.kickDelay == std::chrono::seconds(3) && !server::rep::read_research_option("kick", research)
                  && !server::rep::read_research_option("other=1", research)
                  && !server::rep::read_research_option("kick=x", research)
                  && !research.refuseEquipment && server::rep::read_research_option("refuse-equipment=1", research)
                  && research.refuseEquipment && !server::rep::read_research_option("refuse-equipment=2", research),
              "the option text of the research switch is read, and a text of a different form is refused");
    }
    {
        service::RepServerOptions options;
        options.serverVersion = "v";
        options.research.registrationResult = -160;
        ServerHost host(options);
        const TwoPlayers players(host);
        FakeClient a(27000);
        host.enter(a, players.alpha.registration());
        host.steps(50, {&a});
        check(a.types() == "3 " && a.got.at(0).body.size() >= 4 && hex_text(a.got.at(0).body).rfind("ffffff60", 0) == 0,
              "registration-result=-160: the client gets only the reply (type 3), and its result is FF FF FF 60");
    }
    {
        service::RepServerOptions options;
        options.research.rejectReason = 2;
        ServerHost host(options);
        const TwoPlayers players(host);
        FakeClient a(27000);
        host.enter(a, players.alpha.registration());
        host.steps(50, {&a});
        check(a.types() == "3 1630 " && hex_text(a.got.at(1).frame) == "0900019e190000000200",
              "reject=2: after the reply the client gets type 1630 with the frame 09 00 01 9E 19 00 00 00 02 00, and no more");
    }
    {
        service::RepServerOptions options;
        options.research.kickReason = 1;
        options.research.kickDelay = std::chrono::seconds(10);
        ServerHost host(options);
        const TwoPlayers players(host);
        FakeClient a(27000);
        host.enter(a, players.alpha.registration());
        host.steps(300, {&a});
        check(a.types().find("271") == std::string::npos, "kick=1: no kick message before the first camera position");
        host.datagram(a.peer, a.input_at(1, 100.0F, 200.0F, kHeight));
        host.steps(990, {&a});
        check(a.types().find("271") == std::string::npos, "kick=1: no kick message 9.9 s after the first camera position");
        host.steps(20, {&a});
        const auto kick = std::find_if(a.got.begin(), a.got.end(), [](const Sent& sent) { return sent.type == 271; });
        check(kick != a.got.end() && hex_text(kick->frame) == "1100018f04000000010000000000000000" "00",
              "kick=1: 10 s after the first camera position the client gets type 271 with the frame "
              "11 00 01 8F 04 00 00 00 01, 8 zero bytes, 00");
        host.steps(500, {&a});
        check(std::count_if(a.got.begin(), a.got.end(), [](const Sent& sent) { return sent.type == 271; }) == 1,
              "the kick message goes out one time");
    }
}

} // namespace

std::vector<Case> session_end_cases() {
    return {
        {"T8", "capture folders: each session has its own folder and its own file counter", capture_folders},
        {"M1", "log marks: each log line of a session names the session, with a log limit for each session", log_marks},
        {"C1", "client context instance: each session has its own number", context_instance},
        {"E1", "session end: no registration for 30 s", end_no_registration},
        {"E2", "session end: no datagram for 30 s (idle); the position is saved, and no more data goes out", end_idle},
        {"E3", "session end: carrier close; the position of that moment is saved one time, erase in the next tick", end_carrier_close},
        {"E4", "session end: the secure session closes", end_secure_session},
        {"E5", "session end: the backend stops; each session saves one time", end_backend_stop},
        {"E6", "leave the world, enter again, stop: the position of the first session does not come back", old_position_does_not_come_back},
        {"E7", "two sessions of one character: the newer session wins, and only it moves the character", newest_session_writes},
        {"E8", "three sessions of one character: each new one takes the entity (review case RV2 of stage 0.6a)", three_sessions_of_one_character},
        {"R1", "research switch: off by default; registration result, message 'rejected', message 'kicked'", research_switch},
    };
}

} // namespace soulwarden::backend::tests
