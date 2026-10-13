#include "core/logging/log.h"

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <mutex>

namespace soulwarden::backend::log {
namespace {

std::mutex g_mutex;
std::FILE* g_file = nullptr;
std::function<void(const std::string&)> g_sink;
/** Mark of the session whose call runs in this thread, or nothing. */
thread_local const std::string* t_mark = nullptr;

} // namespace

void open(const std::string& path) {
    std::lock_guard lock(g_mutex);
    if (g_file != nullptr) {
        std::fclose(g_file);
    }
    g_file = std::fopen(path.c_str(), "ab");
}

void write(const char* format, ...) {
    char body[4096];
    va_list arguments;
    va_start(arguments, format);
    const int length = std::vsnprintf(body, sizeof(body), format, arguments);
    va_end(arguments);
    if (t_mark != nullptr && length >= 0) {
        // The mark is at the end of the line: tools find the start of a line by its text.
        const std::size_t used = std::min(static_cast<std::size_t>(length), sizeof(body) - 1);
        std::snprintf(body + used, sizeof(body) - used, "%s", t_mark->c_str());
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &seconds);
#else
    localtime_r(&seconds, &local);
#endif
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%H:%M:%S", &local);

    std::lock_guard lock(g_mutex);
    if (g_sink) {
        g_sink(body);
        return;
    }
    for (std::FILE* output : {g_file, stderr}) {
        if (output != nullptr) {
            std::fprintf(output, "%s.%03d %s\n", stamp, static_cast<int>(millis), body);
            std::fflush(output);
        }
    }
}

Context::Context(const std::string& mark) : m_previous(t_mark) {
    t_mark = &mark;
}

Context::~Context() {
    t_mark = m_previous;
}

std::string session_mark(std::uint32_t session, const std::string& characterId) {
    if (session == 0) {
        return {};
    }
    std::string mark = " [s" + std::to_string(session);
    if (!characterId.empty()) {
        mark += " " + characterId.substr(0, 8);
    }
    return mark + "]";
}

void set_sink(std::function<void(const std::string& line)> sink) {
    std::lock_guard lock(g_mutex);
    g_sink = std::move(sink);
}

} // namespace soulwarden::backend::log
