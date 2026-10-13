#include "service/dtls_server.h"

#include <chrono>
#include <cstring>
#include <exception>

#include <mbedtls/ctr_drbg.h>
#include <mbedtls/entropy.h>
#include <mbedtls/error.h>
#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cookie.h>
#include <mbedtls/x509_crt.h>
#include <psa/crypto.h>

#include "core/logging/log.h"

#if !defined(_WIN32)
#include <poll.h>
#endif

namespace soulwarden::backend::service {
namespace {

using Clock = std::chrono::steady_clock;

/** A peer with no complete handshake goes away after this time with no datagram. */
constexpr std::chrono::seconds kHandshakeIdleLimit{30};
/**
 * A connected peer goes away after this time with no datagram, with no close notice. The
 * handler ends its session earlier (30 s): the secure session stays for the datagrams that a
 * client sends after the end of its session.
 */
constexpr std::chrono::seconds kConnectedIdleLimit{60};

std::string error_text(int code) {
    char text[128];
    mbedtls_strerror(code, text, sizeof(text));
    return text;
}

} // namespace

struct DtlsServer::Shared {
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context random;
    mbedtls_ssl_config config;
    mbedtls_ssl_cookie_ctx cookies;
    mbedtls_x509_crt certificate;
    mbedtls_pk_context key;

    Shared() {
        mbedtls_entropy_init(&entropy);
        mbedtls_ctr_drbg_init(&random);
        mbedtls_ssl_config_init(&config);
        mbedtls_ssl_cookie_init(&cookies);
        mbedtls_x509_crt_init(&certificate);
        mbedtls_pk_init(&key);
    }
    ~Shared() {
        mbedtls_pk_free(&key);
        mbedtls_x509_crt_free(&certificate);
        mbedtls_ssl_cookie_free(&cookies);
        mbedtls_ssl_config_free(&config);
        mbedtls_ctr_drbg_free(&random);
        mbedtls_entropy_free(&entropy);
    }
};

struct DtlsServer::Session {
    DtlsServer* owner{};
    PeerId peer{};
    sockaddr_in address{};
    mbedtls_ssl_context ssl;
    bool connected{false};
    bool closed{false};
    /** Datagram that the record layer reads next. Empty when none is pending. */
    const std::uint8_t* input{nullptr};
    std::size_t inputSize{0};
    Clock::time_point timerStart{};
    std::uint32_t intermediateMs{0};
    std::uint32_t finalMs{0};
    Clock::time_point lastActivity{Clock::now()};

    Session() {
        mbedtls_ssl_init(&ssl);
    }
    ~Session() {
        mbedtls_ssl_free(&ssl);
    }

    static int send_callback(void* context, const unsigned char* data, std::size_t size) {
        auto* session = static_cast<Session*>(context);
        const auto sent = sendto(session->owner->m_socket,
                                 reinterpret_cast<const char*>(data),
                                 static_cast<int>(size),
                                 0,
                                 reinterpret_cast<const sockaddr*>(&session->address),
                                 sizeof(session->address));
        return sent < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : static_cast<int>(sent);
    }

    static int receive_callback(void* context, unsigned char* data, std::size_t size) {
        auto* session = static_cast<Session*>(context);
        if (session->input == nullptr) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        const std::size_t count = session->inputSize < size ? session->inputSize : size;
        std::memcpy(data, session->input, count);
        session->input = nullptr;
        session->inputSize = 0;
        return static_cast<int>(count);
    }

    static void set_timer(void* context, std::uint32_t intermediateMs, std::uint32_t finalMs) {
        auto* session = static_cast<Session*>(context);
        session->intermediateMs = intermediateMs;
        session->finalMs = finalMs;
        if (finalMs != 0) {
            session->timerStart = Clock::now();
        }
    }

    static int get_timer(void* context) {
        auto* session = static_cast<Session*>(context);
        if (session->finalMs == 0) {
            return -1;
        }
        const auto elapsed =
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - session->timerStart).count();
        if (elapsed >= session->finalMs) {
            return 2;
        }
        return elapsed >= session->intermediateMs ? 1 : 0;
    }
};

DtlsServer::DtlsServer() = default;

DtlsServer::~DtlsServer() {
    stop();
}

bool DtlsServer::start(const std::string& host, std::uint16_t port, const TlsIdentity& identity, DtlsHandler* handler) {
    m_handler = handler;
    m_shared = std::make_unique<Shared>();
    Shared& shared = *m_shared;
    static constexpr unsigned char kPersonalization[] = "soulwarden-dtls-server";
    psa_crypto_init();
    int result = mbedtls_ctr_drbg_seed(
        &shared.random, mbedtls_entropy_func, &shared.entropy, kPersonalization, sizeof(kPersonalization) - 1);
    if (result == 0) {
        result = mbedtls_x509_crt_parse(&shared.certificate,
                                        reinterpret_cast<const unsigned char*>(identity.certificatePem.c_str()),
                                        identity.certificatePem.size() + 1);
    }
    if (result == 0) {
        result = mbedtls_pk_parse_key(&shared.key,
                                      reinterpret_cast<const unsigned char*>(identity.privateKeyPem.c_str()),
                                      identity.privateKeyPem.size() + 1,
                                      nullptr,
                                      0,
                                      mbedtls_ctr_drbg_random,
                                      &shared.random);
    }
    if (result == 0) {
        result = mbedtls_ssl_config_defaults(
            &shared.config, MBEDTLS_SSL_IS_SERVER, MBEDTLS_SSL_TRANSPORT_DATAGRAM, MBEDTLS_SSL_PRESET_DEFAULT);
    }
    if (result == 0) {
        mbedtls_ssl_conf_rng(&shared.config, mbedtls_ctr_drbg_random, &shared.random);
        mbedtls_ssl_conf_authmode(&shared.config, MBEDTLS_SSL_VERIFY_NONE);
        mbedtls_ssl_conf_min_tls_version(&shared.config, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_max_tls_version(&shared.config, MBEDTLS_SSL_VERSION_TLS1_2);
        mbedtls_ssl_conf_handshake_timeout(&shared.config, 500, 8000);
        result = mbedtls_ssl_conf_own_cert(&shared.config, &shared.certificate, &shared.key);
    }
    if (result == 0) {
        result = mbedtls_ssl_cookie_setup(&shared.cookies, mbedtls_ctr_drbg_random, &shared.random);
    }
    if (result != 0) {
        log::write("dtls: setup failed: %s", error_text(result).c_str());
        return false;
    }
    mbedtls_ssl_conf_dtls_cookies(
        &shared.config, mbedtls_ssl_cookie_write, mbedtls_ssl_cookie_check, &shared.cookies);

    m_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (m_socket == net::kInvalidSocket) {
        return false;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1
        || bind(m_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        net::close_socket(m_socket);
        m_socket = net::kInvalidSocket;
        return false;
    }
    m_running = true;
    m_thread = std::thread([this] { run(); });
    return true;
}

void DtlsServer::stop() {
    if (!m_running.exchange(false)) {
        return;
    }
    if (m_thread.joinable()) {
        m_thread.join();
    }
    m_sessions.clear();
    net::close_socket(m_socket);
    m_socket = net::kInvalidSocket;
}

DtlsServer::Session* DtlsServer::create_session(PeerId peer, const sockaddr_in& address) {
    auto session = std::make_unique<Session>();
    session->owner = this;
    session->peer = peer;
    session->address = address;
    if (mbedtls_ssl_setup(&session->ssl, &m_shared->config) != 0) {
        return nullptr;
    }
    mbedtls_ssl_set_timer_cb(&session->ssl, session.get(), Session::set_timer, Session::get_timer);
    mbedtls_ssl_set_bio(&session->ssl, session.get(), Session::send_callback, Session::receive_callback, nullptr);
    mbedtls_ssl_set_client_transport_id(
        &session->ssl, reinterpret_cast<const unsigned char*>(&session->peer), sizeof(session->peer));
    Session* raw = session.get();
    m_sessions[peer] = std::move(session);
    return raw;
}

void DtlsServer::advance(Session& session) {
    if (session.closed) {
        return;
    }
    if (!session.connected) {
        const int result = mbedtls_ssl_handshake(&session.ssl);
        if (result == MBEDTLS_ERR_SSL_HELLO_VERIFY_REQUIRED) {
            mbedtls_ssl_session_reset(&session.ssl);
            mbedtls_ssl_set_client_transport_id(
                &session.ssl, reinterpret_cast<const unsigned char*>(&session.peer), sizeof(session.peer));
            return;
        }
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return;
        }
        if (result != 0) {
            log::write("dtls: handshake failed for port %u: %s",
                       static_cast<unsigned>(session.peer & 0xFFFF),
                       error_text(result).c_str());
            session.closed = true;
            return;
        }
        session.connected = true;
        log::write("dtls: connected port %u, cipher %s",
                   static_cast<unsigned>(session.peer & 0xFFFF),
                   mbedtls_ssl_get_ciphersuite(&session.ssl));
        m_handler->on_connected(session.peer);
    }
    unsigned char buffer[16384];
    for (;;) {
        const int result = mbedtls_ssl_read(&session.ssl, buffer, sizeof(buffer));
        if (result > 0) {
            m_handler->on_datagram(session.peer, std::span<const std::uint8_t>(buffer, static_cast<std::size_t>(result)));
            continue;
        }
        if (result == MBEDTLS_ERR_SSL_WANT_READ || result == MBEDTLS_ERR_SSL_WANT_WRITE) {
            return;
        }
        if (result == MBEDTLS_ERR_SSL_CLIENT_RECONNECT) {
            log::write("dtls: client reconnect on port %u", static_cast<unsigned>(session.peer & 0xFFFF));
        } else if (result != 0 && result != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            log::write("dtls: read failed for port %u: %s",
                       static_cast<unsigned>(session.peer & 0xFFFF),
                       error_text(result).c_str());
        }
        session.closed = true;
        return;
    }
}

void DtlsServer::receive(PeerId peer, const sockaddr_in& address, const std::uint8_t* data, std::size_t size) {
    Session* session = nullptr;
    const auto found = m_sessions.find(peer);
    if (found != m_sessions.end()) {
        session = found->second.get();
    } else {
        session = create_session(peer, address);
    }
    if (session == nullptr) {
        return;
    }
    session->lastActivity = Clock::now();
    session->input = data;
    session->inputSize = size;
    advance(*session);
    session->input = nullptr;
    session->inputSize = 0;
}

bool DtlsServer::send(PeerId peer, std::span<const std::uint8_t> data) {
    const auto found = m_sessions.find(peer);
    if (found == m_sessions.end() || !found->second->connected || found->second->closed) {
        return false;
    }
    const int result = mbedtls_ssl_write(&found->second->ssl, data.data(), data.size());
    return result == static_cast<int>(data.size());
}

void DtlsServer::close(PeerId peer) {
    const auto found = m_sessions.find(peer);
    if (found != m_sessions.end()) {
        mbedtls_ssl_close_notify(&found->second->ssl);
        found->second->closed = true;
    }
}

void DtlsServer::run() {
    std::uint8_t buffer[65536];
    auto lastTick = Clock::now();
    while (m_running) {
        bool readable = false;
#if defined(_WIN32)
        fd_set set;
        FD_ZERO(&set);
        FD_SET(m_socket, &set);
        timeval timeout{0, 5000};
        readable = select(0, &set, nullptr, nullptr, &timeout) > 0;
#else
        pollfd descriptor{m_socket, POLLIN, 0};
        readable = poll(&descriptor, 1, 5) > 0 && (descriptor.revents & POLLIN) != 0;
#endif
        if (readable) {
            sockaddr_in address{};
            socklen_t addressSize = sizeof(address);
            const auto received = recvfrom(m_socket,
                                           reinterpret_cast<char*>(buffer),
                                           sizeof(buffer),
                                           0,
                                           reinterpret_cast<sockaddr*>(&address),
                                           &addressSize);
            if (received > 0) {
                const PeerId peer =
                    (static_cast<PeerId>(ntohl(address.sin_addr.s_addr)) << 16) | ntohs(address.sin_port);
                // The handler has its own guard for each session. This guard is for a fault
                // that it does not catch: the listener thread must not end the process.
                try {
                    receive(peer, address, buffer, static_cast<std::size_t>(received));
                } catch (const std::exception& error) {
                    log::write("dtls: the handler stopped with an exception for port %u: %s",
                               static_cast<unsigned>(peer & 0xFFFF),
                               error.what());
                    close(peer);
                }
            }
        }
        const auto now = Clock::now();
        if (now - lastTick >= std::chrono::milliseconds(10)) {
            lastTick = now;
            try {
                tick_sessions(now);
                m_handler->on_tick();
            } catch (const std::exception& error) {
                log::write("dtls: a tick stopped with an exception: %s", error.what());
            }
        }
    }
}

void DtlsServer::tick_sessions(std::chrono::steady_clock::time_point now) {
    for (auto iterator = m_sessions.begin(); iterator != m_sessions.end();) {
        Session& session = *iterator->second;
        if (!session.closed && !session.connected && Session::get_timer(&session) == 2) {
            // Handshake retransmission timer.
            advance(session);
        }
        const auto silence = now - session.lastActivity;
        const bool stale = silence > (session.connected ? kConnectedIdleLimit : kHandshakeIdleLimit);
        if (stale && session.connected && !session.closed) {
            log::write("dtls: no datagram from port %u for %lld s: the secure session ends",
                       static_cast<unsigned>(session.peer & 0xFFFF),
                       static_cast<long long>(kConnectedIdleLimit.count()));
        }
        if (session.closed || stale) {
            if (session.connected) {
                m_handler->on_disconnected(session.peer);
            }
            iterator = m_sessions.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

} // namespace soulwarden::backend::service
