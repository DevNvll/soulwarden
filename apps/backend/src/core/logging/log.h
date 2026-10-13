#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace soulwarden::backend::log {

/** Opens the backend log file. Lines also go to standard error. */
void open(const std::string& path);

/** Appends one formatted line with a wall-clock time. */
void write(const char* format, ...) __attribute__((format(printf, 1, 2)));

/**
 * Mark of a session for the log lines of the current thread. While an object of this class
 * lives, each line that the thread writes ends with the text of the mark, for example
 * " [s1 4aae16c4]". The game server makes one for each call that belongs to one session, so
 * that each line of a session names the session. The text can change while the object lives.
 */
class Context {
public:
    /** @param mark The text. It must stay valid while this object lives. */
    explicit Context(const std::string& mark);
    ~Context();
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;

private:
    const std::string* m_previous;
};

/**
 * @return The mark of a session for a log line: " [s1]", and " [s1 4aae16c4]" with a character
 *         id (its first 8 characters). An empty text for session 0 (no session).
 */
[[nodiscard]] std::string session_mark(std::uint32_t session, const std::string& characterId = {});

/**
 * Test seam. With a sink, each line goes to the sink with no time, and not to the log file and
 * not to standard error. An empty function sets the normal outputs again. The sink runs with
 * the log mutex held: it must not write a log line.
 */
void set_sink(std::function<void(const std::string& line)> sink);

} // namespace soulwarden::backend::log
