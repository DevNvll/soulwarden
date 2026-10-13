#include "service/http_server.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "core/logging/log.h"

namespace soulwarden::backend::service {
namespace {

constexpr std::size_t kMaxHeaderBytes = 64 * 1024;
constexpr std::size_t kMaxBodyBytes = 64 * 1024 * 1024;
/** The stop waits this long for the connection threads. A handler has its own, shorter limits. */
constexpr std::chrono::seconds kConnectionStopLimit{10};

bool equals_ignore_case(const std::string& left, const std::string& right) {
    return left.size() == right.size()
           && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
                  return std::tolower(static_cast<unsigned char>(a))
                         == std::tolower(static_cast<unsigned char>(b));
              });
}

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(begin, end - begin);
}

/** Reads more bytes into the buffer. @return False at end of stream or on error. */
bool fill(net::Socket socket, std::string& buffer) {
    char chunk[16384];
    const auto received = recv(socket, chunk, sizeof(chunk), 0);
    if (received <= 0) {
        return false;
    }
    buffer.append(chunk, static_cast<std::size_t>(received));
    return true;
}

bool send_all(net::Socket socket, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const auto result = send(socket,
                                 data.data() + sent,
                                 static_cast<int>(std::min<std::size_t>(data.size() - sent, 1 << 20)),
                                 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(result);
    }
    return true;
}

const char* reason(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 204: return "No Content";
    case 400: return "Bad Request";
    case 401: return "Unauthorized";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 500: return "Internal Server Error";
    default: return "Status";
    }
}

/** Decodes a chunked body that is complete in the buffer. @return False when more data is needed. */
bool decode_chunked(const std::string& buffer, std::size_t offset, std::string& body, std::size_t& consumed) {
    body.clear();
    std::size_t position = offset;
    for (;;) {
        const std::size_t lineEnd = buffer.find("\r\n", position);
        if (lineEnd == std::string::npos) {
            return false;
        }
        const std::size_t size = std::strtoul(buffer.c_str() + position, nullptr, 16);
        position = lineEnd + 2;
        if (size == 0) {
            const std::size_t trailerEnd = buffer.find("\r\n", position);
            if (trailerEnd == std::string::npos) {
                return false;
            }
            consumed = trailerEnd + 2;
            return true;
        }
        if (buffer.size() < position + size + 2) {
            return false;
        }
        body.append(buffer, position, size);
        position += size + 2;
    }
}

} // namespace

std::string HttpRequest::header(const std::string& name) const {
    for (const auto& [key, value] : headers) {
        if (equals_ignore_case(key, name)) {
            return value;
        }
    }
    return {};
}

HttpServer::~HttpServer() {
    stop();
}

bool HttpServer::start(const std::string& host, std::uint16_t port, HttpHandler handler) {
    m_handler = std::move(handler);
    m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (m_listener == net::kInvalidSocket) {
        return false;
    }
    int enabled = 1;
    setsockopt(m_listener, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &address.sin_addr) != 1) {
        return false;
    }
    if (bind(m_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
        || listen(m_listener, 64) != 0) {
        net::close_socket(m_listener);
        m_listener = net::kInvalidSocket;
        return false;
    }
    m_running = true;
    m_acceptThread = std::thread([this] { accept_loop(); });
    return true;
}

void HttpServer::stop() {
    if (!m_running.exchange(false)) {
        return;
    }
#if defined(_WIN32)
    net::close_socket(m_listener);
#else
    shutdown(m_listener, SHUT_RDWR);
    net::close_socket(m_listener);
#endif
    if (m_acceptThread.joinable()) {
        m_acceptThread.join();
    }
    // No new connection comes now. A connection thread waits in recv between two requests:
    // the shutdown of its socket ends that wait. A thread that is in a handler ends after its
    // reply. The thread closes its socket itself.
    std::unique_lock lock(m_connectionsMutex);
    for (const net::Socket socket : m_connections) {
        net::shutdown_socket(socket);
    }
    // The stop waits for each thread: a thread that runs uses this object, and the caller
    // destroys the object after the stop. A time limit only writes a line.
    while (!m_connectionsChanged.wait_for(lock, kConnectionStopLimit, [this] { return m_connections.empty(); })) {
        log::write("http: %zu connection thread(s) still run after %lld s: the stop waits for them",
                   m_connections.size(),
                   static_cast<long long>(kConnectionStopLimit.count()));
    }
    // Each thread did its last use of this object. The join waits for the end of the thread
    // function, which uses nothing of this object after it set its mark.
    std::list<Connection> threads;
    threads.swap(m_threads);
    lock.unlock();
    for (Connection& connection : threads) {
        if (connection.thread.joinable()) {
            connection.thread.join();
        }
    }
}

void HttpServer::reap(std::unique_lock<std::mutex>& lock) {
    std::list<Connection> ended;
    for (auto connection = m_threads.begin(); connection != m_threads.end();) {
        const auto next = std::next(connection);
        if (connection->done) {
            ended.splice(ended.end(), m_threads, connection);
        }
        connection = next;
    }
    // A thread with the mark does not take the mutex again: the join needs no lock, and it
    // ends at once.
    lock.unlock();
    for (Connection& connection : ended) {
        connection.thread.join();
    }
    lock.lock();
}

std::size_t HttpServer::connection_count() {
    std::lock_guard lock(m_connectionsMutex);
    return m_connections.size();
}

void HttpServer::accept_loop() {
    while (m_running) {
        const net::Socket client = accept(m_listener, nullptr, nullptr);
        if (client == net::kInvalidSocket) {
            if (!m_running) {
                break;
            }
            continue;
        }
        int enabled = 1;
        setsockopt(client, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
        std::unique_lock lock(m_connectionsMutex);
        reap(lock);
        m_connections.insert(client);
        // The thread stays joinable: the stop waits for it before the caller destroys this
        // object. The thread reads its list entry only with the mutex, which this function
        // holds until the entry has the thread.
        m_threads.emplace_back();
        const auto entry = std::prev(m_threads.end());
        entry->thread = std::thread([this, client, entry] {
            serve(client);
            // The socket leaves the list before its close: the number of a closed socket can
            // go to a new connection.
            std::lock_guard guard(m_connectionsMutex);
            m_connections.erase(client);
            net::close_socket(client);
            entry->done = true;
            // The last use of this object: the stop can return after this call.
            m_connectionsChanged.notify_all();
        });
    }
}

void HttpServer::serve(net::Socket socket) {
    std::string buffer;
    for (;;) {
        std::size_t headerEnd;
        while ((headerEnd = buffer.find("\r\n\r\n")) == std::string::npos) {
            if (buffer.size() > kMaxHeaderBytes || !fill(socket, buffer)) {
                return;
            }
        }
        HttpRequest request;
        std::size_t lineStart = 0;
        bool first = true;
        while (lineStart < headerEnd) {
            std::size_t lineEnd = buffer.find("\r\n", lineStart);
            const std::string line = buffer.substr(lineStart, lineEnd - lineStart);
            lineStart = lineEnd + 2;
            if (first) {
                first = false;
                const std::size_t methodEnd = line.find(' ');
                const std::size_t targetEnd = line.rfind(' ');
                if (methodEnd == std::string::npos || targetEnd == methodEnd) {
                    return;
                }
                request.method = line.substr(0, methodEnd);
                request.target = line.substr(methodEnd + 1, targetEnd - methodEnd - 1);
                continue;
            }
            const std::size_t colon = line.find(':');
            if (colon != std::string::npos) {
                request.headers.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
            }
        }
        const std::size_t bodyStart = headerEnd + 4;
        std::size_t consumed = bodyStart;
        if (equals_ignore_case(request.header("Transfer-Encoding"), "chunked")) {
            while (!decode_chunked(buffer, bodyStart, request.body, consumed)) {
                if (buffer.size() > kMaxBodyBytes || !fill(socket, buffer)) {
                    return;
                }
            }
        } else {
            const std::size_t length = std::strtoull(request.header("Content-Length").c_str(), nullptr, 10);
            if (length > kMaxBodyBytes) {
                return;
            }
            while (buffer.size() < bodyStart + length) {
                if (!fill(socket, buffer)) {
                    return;
                }
            }
            request.body = buffer.substr(bodyStart, length);
            consumed = bodyStart + length;
        }
        buffer.erase(0, consumed);

        const std::size_t question = request.target.find('?');
        request.path = request.target.substr(0, question);
        request.query = question == std::string::npos ? std::string{} : request.target.substr(question + 1);
        request.host = request.header("X-Soulwarden-Host");

        // A handler that throws must not stop the process: this thread has no other handler
        // for an exception.
        HttpResponse response;
        try {
            response = m_handler(request);
        } catch (const std::exception& error) {
            log::write("http: %s %s failed: %s", request.method.c_str(), request.path.c_str(), error.what());
            response = HttpResponse{};
            response.status = 400;
            response.body = R"({"message":"Bad Request"})";
        }
        std::string head = "HTTP/1.1 " + std::to_string(response.status) + " " + reason(response.status) + "\r\n";
        if (!response.contentType.empty()) {
            head += "Content-Type: " + response.contentType + "\r\n";
        }
        for (const auto& [key, value] : response.headers) {
            head += key + ": " + value + "\r\n";
        }
        head += "Content-Length: " + std::to_string(response.body.size()) + "\r\nConnection: keep-alive\r\n\r\n";
        if (!send_all(socket, head + response.body)) {
            return;
        }
    }
}

} // namespace soulwarden::backend::service
