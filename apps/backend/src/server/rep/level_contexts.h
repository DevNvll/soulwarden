#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

// The account rule of the level information (stage DG1, docs/systems/instances.md; research dungeons.md 2.4, open problem P1).
//
// The client does nothing for a level information (message 1635) whose version is the version of the last one that its game process
// got, and it makes no new client context for a context instance that is the instance of its last one (client code at image RVA
// 0x6446800: the equality branches at 0x64468B1 and 0x6446B47; the process keeps both values over a new connection, protocol notes 3.5).
// The client then stays in WaitingForPlayerSpawn. The backend cannot see the game process: it keeps, for each account, the last version
// and the last context instance that it gave, and each new level information of that account (the world entry of a session, a level
// change in one connection) gets the next ones. The first level information of an account in a backend process has the version and the
// instance of the session id, as before this stage (the stored bytes of case T2).
//
// The trap of the session rule (research dungeons.md 2.4): session 1 moves to instance 2 with a level change, and session 2 starts with
// the instance of its id, 2: the client keeps its context. With this table session 2 gets instance 3.
//
// A table of the game server (RepServer): one thread (the world thread) reads and changes it.

namespace soulwarden::backend::server::rep {

/** The largest client context instance: one byte, and the client uses 0 for "no instance". */
inline constexpr std::uint32_t kContextInstanceCount = 255;

/** @return The context instance of the session rule (session 1 has instance 1, the value of each session before the sessions had an id). */
[[nodiscard]] inline std::uint8_t session_context_instance(std::uint32_t sessionId) {
    return static_cast<std::uint8_t>((sessionId + kContextInstanceCount - 1) % kContextInstanceCount + 1);
}

struct LevelContext {
    std::uint64_t version{0};
    std::uint8_t instance{0};
};

class LevelContexts {
public:
    /**
     * @return The version and the context instance of the next level information of a session for an account, and keeps them as the last
     *         ones of the account. The first one of an account: the session id and its instance (session_context_instance); after that:
     *         one above the last version, and the instance after the last one (1 to 255, never 0).
     */
    [[nodiscard]] LevelContext next(const std::string& personaId, std::uint32_t sessionId) {
        const auto last = m_last.find(personaId);
        const LevelContext context = last == m_last.end()
                                         ? LevelContext{sessionId, session_context_instance(sessionId)}
                                         : LevelContext{last->second.version + 1,
                                                        static_cast<std::uint8_t>(last->second.instance % kContextInstanceCount + 1)};
        m_last[personaId] = context;
        return context;
    }

    /** @return The last values that the table gave for an account, or no value. */
    [[nodiscard]] std::optional<LevelContext> last(const std::string& personaId) const {
        const auto found = m_last.find(personaId);
        return found == m_last.end() ? std::nullopt : std::optional<LevelContext>(found->second);
    }

private:
    std::map<std::string, LevelContext> m_last;
};

} // namespace soulwarden::backend::server::rep
