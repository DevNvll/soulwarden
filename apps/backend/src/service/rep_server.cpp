#include "service/rep_server.h"

#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>

#include "core/logging/log.h"

namespace soulwarden::backend::service {
namespace {

/**
 * Datagrams and messages of one session after these counts are not logged and not captured:
 * input is 30 Hz. Each session has its own counts.
 */
constexpr unsigned kLoggedPackets = 96;
constexpr unsigned kCapturedPackets = 32768;

std::string hex(std::span<const std::uint8_t> data, std::size_t limit) {
    std::string text;
    char cell[4];
    for (std::size_t index = 0; index < data.size() && index < limit; ++index) {
        std::snprintf(cell, sizeof(cell), "%02x", data[index]);
        text += cell;
    }
    if (data.size() > limit) {
        text += "...";
    }
    return text;
}

unsigned port_of(PeerId peer) {
    return static_cast<unsigned>(peer & 0xFFFF);
}

SessionEnd end_of(server::rep::CloseCause cause) {
    switch (cause) {
    case server::rep::CloseCause::refused:
        return SessionEnd::refused;
    case server::rep::CloseCause::replaced:
        return SessionEnd::replaced;
    case server::rep::CloseCause::characterDeleted:
        return SessionEnd::characterDeleted;
    }
    return SessionEnd::error;
}

} // namespace

const char* session_end_text(SessionEnd cause) {
    switch (cause) {
    case SessionEnd::secureSessionClosed:
        return "secure session closed";
    case SessionEnd::carrierClosed:
        return "carrier close";
    case SessionEnd::idle:
        return "idle (no datagram for 30 s)";
    case SessionEnd::noRegistration:
        return "no registration (30 s)";
    case SessionEnd::error:
        return "error";
    case SessionEnd::refused:
        return "registration refused";
    case SessionEnd::replaced:
        return "replaced by a newer session of the character";
    case SessionEnd::characterDeleted:
        return "character deleted";
    case SessionEnd::backendStop:
        return "backend stop";
    }
    return "unknown";
}

RepServer::RepServer(DatagramTransport& transport, world::World& world, RepServerOptions options)
    : m_transport(transport), m_world(world), m_options(std::move(options)) {
    if (!m_options.captureDirectory.empty()) {
        std::filesystem::create_directories(m_options.captureDirectory);
    }
    if (m_options.research.active()) {
        log::write("rep: RESEARCH SWITCH is on. The game server does not work as a product.");
    }
}

void RepServer::stop() {
    for (auto& [peer, client] : m_clients) {
        end_session(client, SessionEnd::backendStop);
    }
    m_clients.clear();
}

void RepServer::capture(Client& client, const char* direction, std::span<const std::uint8_t> data) {
    if (m_options.captureDirectory.empty() || client.packets >= kCapturedPackets) {
        return;
    }
    if (client.captureDirectory.empty()) {
        char folder[16];
        std::snprintf(folder, sizeof(folder), "s%04u", client.id);
        const std::filesystem::path path = std::filesystem::path(m_options.captureDirectory) / folder;
        std::error_code error;
        std::filesystem::create_directories(path, error);
        client.captureDirectory = path.string();
    }
    char name[64];
    std::snprintf(name, sizeof(name), "%05u-%s-%zu.bin", client.packets, direction, data.size());
    std::ofstream output(std::filesystem::path(client.captureDirectory) / name, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
}

void RepServer::on_connected(PeerId peer) {
    const auto existing = m_clients.find(peer);
    if (existing != m_clients.end()) {
        // The transport gave no end for the earlier session of this peer.
        end_session(existing->second, SessionEnd::secureSessionClosed);
        m_clients.erase(existing);
    }
    Client& client = m_clients[peer];
    client.id = m_nextSession++;
    client.peer = peer;
    client.opened = m_world.clock().now();
    client.lastDatagram = client.opened;
    Client* self = &client;
    client.connection = std::make_unique<server::carrier::Connection>(
        [this, self](std::span<const std::uint8_t> datagram) {
            if (datagram.size() > 16 && self->packets < kLoggedPackets) {
                log::write("rep: out %u (%zu bytes) %s", self->packets, datagram.size(), hex(datagram, 96).c_str());
            }
            capture(*self, "out", datagram);
            ++self->packets;
            ++self->datagramsOut;
            self->bytesOut += datagram.size();
            m_transport.send(self->peer, datagram);
        },
        [this, self](std::uint8_t channel, std::span<const std::uint8_t> data) { on_message(*self, channel, data); });
    server::carrier::Connection* connection = client.connection.get();
    server::rep::SessionOptions sessionOptions;
    sessionOptions.serverVersion = m_options.serverVersion;
    sessionOptions.research = m_options.research;
    sessionOptions.observePlayers = m_options.observePlayers;
    sessionOptions.playerSlice = m_options.playerSlice;
    client.session = std::make_unique<server::rep::RepSession>(
        client.id,
        [connection](std::uint8_t channel, std::span<const std::uint8_t> data, server::carrier::Reliability reliability) {
            connection->send(channel, data, reliability);
        },
        m_world,
        std::move(sessionOptions));
    const log::Context context(client.session->log_mark());
    log::write("rep: secure session open, client port %u", port_of(peer));
}

void RepServer::end_session(Client& client, SessionEnd cause) {
    if (client.ended) {
        return;
    }
    client.ended = true;
    const log::Context context(client.session->log_mark());
    // The world writes the position of the character and removes its entity. A session that
    // lost its character to a newer session changes nothing in the world.
    m_world.leave(client.id);
    client.session->log_message_counts();
    log::write("rep: session ended, cause: %s; client port %u, %llu datagrams in, %llu datagrams out (%llu bytes)",
               session_end_text(cause),
               port_of(client.peer),
               static_cast<unsigned long long>(client.datagramsIn),
               static_cast<unsigned long long>(client.datagramsOut),
               static_cast<unsigned long long>(client.bytesOut));
    // The game server does not close the peer of the transport: a client that gets the close
    // notice of the secure session does a new handshake from the same port (live run). The
    // transport ends a secure session that has no datagram for some time.
}

void RepServer::on_datagram(PeerId peer, std::span<const std::uint8_t> data) {
    const auto found = m_clients.find(peer);
    if (found == m_clients.end() || found->second.ended) {
        return;
    }
    Client& client = found->second;
    const log::Context context(client.session->log_mark());
    client.lastDatagram = m_world.clock().now();
    ++client.datagramsIn;
    // Acknowledgement and keep-alive datagrams are short. They are not logged.
    if (data.size() > 18 && client.packets < kLoggedPackets) {
        log::write("rep: in %u (%zu bytes) %s", client.packets, data.size(), hex(data, 64).c_str());
    }
    capture(client, "in", data);
    ++client.packets;
    // One bad message must not stop the world thread: all sessions are on it.
    try {
        client.connection->receive(data);
    } catch (const std::exception& error) {
        log::write("rep: a message handler stopped with an exception: %s", error.what());
        end_session(client, SessionEnd::error);
        return;
    } catch (...) {
        log::write("rep: a message handler stopped with an unknown exception");
        end_session(client, SessionEnd::error);
        return;
    }
    if (client.connection->state() == server::carrier::State::disconnected) {
        // A client that the session sent away closes its connection itself.
        const auto& closing = client.session->closing();
        end_session(client, closing ? end_of(closing->cause) : SessionEnd::carrierClosed);
    }
}

const server::rep::RepSession* RepServer::session(std::uint32_t id) const {
    for (const auto& [peer, client] : m_clients) {
        if (client.id == id) {
            return client.session.get();
        }
    }
    return nullptr;
}

void RepServer::on_message(Client& client, std::uint8_t channel, std::span<const std::uint8_t> data) {
    if (client.packets < kLoggedPackets) {
        log::write("rep: message on channel %u (%zu bytes) %s", channel, data.size(), hex(data, 96).c_str());
    }
    capture(client, "msg", data);
    ++client.packets;
    if (client.ended) {
        return;
    }
    if (m_options.messageTap) {
        m_options.messageTap(client.id, channel, data);
    }
    // The datagram of the message (code review of stage 2.5b, finding ST-R2): the order of the messages of different channels.
    client.session->on_message(channel, data, client.connection->delivered_datagram());
}

void RepServer::on_disconnected(PeerId peer) {
    const auto found = m_clients.find(peer);
    if (found == m_clients.end()) {
        return; // The session of this peer ended before its secure session.
    }
    Client& client = found->second;
    const log::Context context(client.session->log_mark());
    log::write("rep: secure session closed, client port %u", port_of(peer));
    end_session(client, SessionEnd::secureSessionClosed);
}

void RepServer::on_tick() {
    // The real time of the tick of the game server, with the tick of the world and of each
    // session: a measurement for the log and for the development tools.
    const world::LoadTimer timer(m_world.load().serverTick);
    // A session that ended in the last tick, or in a call of the transport after it, goes
    // away now: no call of its carrier connection is active here.
    for (auto client = m_clients.begin(); client != m_clients.end();) {
        client = client->second.ended ? m_clients.erase(client) : std::next(client);
    }
    m_world.tick();
    const auto now = m_world.clock().now();
    for (auto& [peer, client] : m_clients) {
        if (client.ended) {
            continue;
        }
        const log::Context context(client.session->log_mark());
        if (now - client.lastDatagram >= kSessionIdleLimit) {
            end_session(client, SessionEnd::idle);
            continue;
        }
        if (!client.session->registered() && now - client.opened >= kRegistrationLimit) {
            end_session(client, SessionEnd::noRegistration);
            continue;
        }
        if (const auto& closing = client.session->closing(); closing && now - closing->since >= kCloseLimit) {
            // The carrier of this session must run until here, so that the client gets the
            // message. A session that the world sent away in this tick ends in a later one.
            end_session(client, end_of(closing->cause));
            continue;
        }
        try {
            client.session->tick(now);
            client.connection->tick(now);
        } catch (const std::exception& error) {
            log::write("rep: the tick of the session stopped with an exception: %s", error.what());
            end_session(client, SessionEnd::error);
        } catch (...) {
            log::write("rep: the tick of the session stopped with an unknown exception");
            end_session(client, SessionEnd::error);
        }
    }
}

} // namespace soulwarden::backend::service
