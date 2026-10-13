#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <list>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "service/network.h"

namespace soulwarden::backend::service {

using HeaderList = std::vector<std::pair<std::string, std::string>>;

/** One decoded HTTP request. Header names keep their wire case. */
struct HttpRequest {
    std::string method;
    std::string target;
    std::string path;
    std::string query;
    /** Service host that the game addressed. The hook supplies it in X-Soulwarden-Host. */
    std::string host;
    HeaderList headers;
    std::string body;

    /** @return The first header value with a case-insensitive name match, or empty. */
    [[nodiscard]] std::string header(const std::string& name) const;
};

struct HttpResponse {
    int status{200};
    std::string contentType{"application/json"};
    HeaderList headers;
    std::string body;
};

using HttpHandler = std::function<HttpResponse(const HttpRequest&)>;

/** Loopback HTTP/1.1 listener with one thread per connection. */
class HttpServer {
public:
    ~HttpServer();

    /** Binds and starts the accept thread. @return False when the port is not available. */
    [[nodiscard]] bool start(const std::string& host, std::uint16_t port, HttpHandler handler);

    /**
     * Closes the listener and each open connection, and waits until each connection thread
     * ended: a handler of a request uses this object and objects that the caller destroys
     * after the stop (the world object and its task queue). The wait has no time limit: a
     * handler that runs gets the time to end, and the log gets a line at an interval while a
     * thread still runs. The function returns only when no thread uses this object.
     */
    void stop();

    /** @return The number of connection threads that run (a read function for the tests). */
    [[nodiscard]] std::size_t connection_count();

private:
    void accept_loop();
    /** Reads and answers the requests of one connection. It does not close the socket. */
    void serve(net::Socket socket);

    net::Socket m_listener{net::kInvalidSocket};
    std::atomic<bool> m_running{false};
    std::thread m_acceptThread;
    HttpHandler m_handler;
    /** One connection thread. It stays joinable until the accept loop or the stop joins it. */
    struct Connection {
        std::thread thread;
        /** True when the thread did its last use of this object. Protected by the mutex. */
        bool done{false};
    };

    /** Joins and removes the threads that ended. The caller holds the lock. */
    void reap(std::unique_lock<std::mutex>& lock);

    /** Sockets of the connection threads that run. Protected by the mutex. */
    std::mutex m_connectionsMutex;
    std::condition_variable m_connectionsChanged;
    std::set<net::Socket> m_connections;
    /** Each connection thread that was not joined. Protected by the mutex. */
    std::list<Connection> m_threads;
};

} // namespace soulwarden::backend::service
