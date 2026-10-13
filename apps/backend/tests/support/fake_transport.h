#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <vector>

#include "server/hub/wire.h"
#include "service/datagram_transport.h"

namespace soulwarden::backend::tests {

/** A transport with no socket: it keeps each datagram that the game server sends. */
class FakeTransport final : public service::DatagramTransport {
public:
    bool send(service::PeerId peer, std::span<const std::uint8_t> data) override {
        if (throwing.count(peer) != 0) {
            throw std::runtime_error("test transport: send failed");
        }
        pending[peer].emplace_back(data.begin(), data.end());
        bytes[peer] += data.size();
        ++datagrams[peer];
        return true;
    }

    void close(service::PeerId peer) override {
        closed.push_back(peer);
    }

    /** @return The datagrams for one peer since the last call. */
    std::vector<server::hub::Bytes> take(service::PeerId peer) {
        std::vector<server::hub::Bytes> result;
        result.swap(pending[peer]);
        return result;
    }

    [[nodiscard]] std::size_t close_count(service::PeerId peer) const {
        std::size_t count = 0;
        for (const service::PeerId value : closed) {
            count += value == peer ? 1 : 0;
        }
        return count;
    }

    std::map<service::PeerId, std::vector<server::hub::Bytes>> pending;
    /** All bytes and datagrams for a peer from the start of the test. */
    std::map<service::PeerId, std::uint64_t> bytes;
    std::map<service::PeerId, std::uint64_t> datagrams;
    std::vector<service::PeerId> closed;
    /** A send to one of these peers throws an exception. */
    std::set<service::PeerId> throwing;
};

} // namespace soulwarden::backend::tests
