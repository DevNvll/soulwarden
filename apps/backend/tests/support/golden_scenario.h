#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "support/fake_client.h"
#include "support/test_players.h"

// The scenario of the stored bytes (case T2): one client connects, registers, stays in the
// world for 0.6 s, and sends one input report. The result is each datagram that the game
// server sends to this client, in sequence. The datagrams are the clear bytes before DTLS:
// carrier headers, acknowledgements, and the messages of the session.
//
// The file tests/golden/one_session.hex has the datagrams of the backend from before stage
// 0.6a (a program in the work folder of that stage ran this scenario with the sources of that
// backend), with one change of stage 0.6b: the ids. The self identification has the actor ids
// of the session and of the player manager and a null third reference, and the record of the
// player has the actor id of its entity, all from the id allocator of the world (145 bytes of
// one datagram; a live run proved the new ids). Thus a pass of case T2 proves that no other
// byte for one client changed.
//
// This file uses only the wire, the carrier, and the store headers of the backend
// (fake_client.h has the functions of a host).

namespace soulwarden::backend::tests {

inline constexpr const char* kGoldenCharacterId = "0a0a0a0a-1111-4222-8333-444444444444";
inline constexpr const char* kGoldenCharacterName = "Golden";
inline constexpr const char* kGoldenVersion = "golden-version";
/** Start time of the clock of the scenario, in nanoseconds. */
inline constexpr std::chrono::nanoseconds kGoldenStart = std::chrono::seconds(1'000'000);

/**
 * Runs the scenario. The store of the host must be open and empty, and the clock of the host
 * must be at kGoldenStart.
 * @return The datagrams that the server sent to the client.
 */
template <class Host>
std::vector<Bytes> run_golden_scenario(Host& host) {
    const Player player = add_player(host.store, kSteamIdA, "Tester", kGoldenCharacterId, kGoldenCharacterName,
                                     state::WorldPosition{100.0F, 200.0F, 50.0F});
    FakeClient client(27000);
    std::vector<Bytes> out;
    // One step: 10 ms pass, the server ticks, the client reads and acknowledges.
    const auto steps = [&](int count) {
        for (int index = 0; index < count; ++index) {
            host.advance(std::chrono::milliseconds(10));
            host.tick();
            const std::vector<Bytes> datagrams = client.exchange(host);
            out.insert(out.end(), datagrams.begin(), datagrams.end());
        }
    };
    client.connect(host);
    steps(2);
    host.datagram(client.peer, client.registration(player.registration()));
    steps(30);
    // The camera of a character at (120, 200, 50): the position estimate changes, and the
    // server sends nothing for it.
    host.datagram(client.peer, client.input(1, 120.5F, 196.0F, 51.65F));
    steps(28);
    return out;
}

} // namespace soulwarden::backend::tests
