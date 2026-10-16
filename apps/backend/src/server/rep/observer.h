#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <set>

#include "world/entity.h"
#include "world/ids.h"

// The interest table of one session: the entities that its client knows.
//
// An interest number is an index into a table of the client (65,534 places, client context
// +0x608). The server selects the number. Number 1 is the player of the client. The client
// stores 16 bits of the number and makes no range check, thus the numbers of other entities
// are 2 to 65,533 (protocol notes 4.2).

namespace soulwarden::backend::server::rep {

/** One entity that the client of the session has. */
struct Known {
    /** Number of the entity in the state records of this session. */
    std::uint32_t interest{0};
    /** World revision of the last record of the session for this entity. 0: no record. */
    world::Revision sent{0};
    /**
     * For a creature: the key of the scope fields of the last movement state that the session
     * sent (states/state_codecs.h, creature_scope). 0xFFFFFFFF: none yet.
     */
    std::uint32_t scope{0xFFFFFFFFU};
    /**
     * For a creature with a spawn action: the first record of the session had the spawn action
     * (creature_scope), and a different action of the server plays (an attack, the locomotion of
     * a way). At this time of the world clock the session sends the movement state with that
     * action, also with no new change of the creature (code review of the corrections after user
     * test 1, finding F3). No value: nothing waits.
     */
    std::optional<world::WorldClock::time_point> scopeDue;
    /** For the own entity: the health of the last vitals state that the session sent (finding S6-R7). Below 0: none. */
    float ownerHealth{-1.0F};
};

class Observer {
public:
    static constexpr std::uint32_t kOwnInterest = 1;
    static constexpr std::uint32_t kFirstOtherInterest = 2;
    static constexpr std::uint32_t kLastInterest = 65533;

    /** Sets the entity of the character of the session. It gets the interest number 1. */
    void set_own(world::EntityId id) {
        m_own = id;
        m_known[id] = Known{kOwnInterest, 0, 0xFFFFFFFFU, std::nullopt};
        m_used.insert(kOwnInterest);
    }

    /** @return The entity of the character of the session. 0: none. */
    [[nodiscard]] world::EntityId own() const {
        return m_own;
    }

    /**
     * Adds an entity of a different character. It gets the lowest free number from 2.
     * @return The number, or 0 when the table has the entity or has no free number.
     */
    std::uint32_t add(world::EntityId id) {
        if (m_known.count(id) != 0) {
            return 0;
        }
        std::uint32_t interest = kFirstOtherInterest;
        // The set is sorted: the first gap from 2 is the lowest free number.
        for (auto used = m_used.lower_bound(kFirstOtherInterest); used != m_used.end() && *used == interest; ++used) {
            ++interest;
        }
        if (interest > kLastInterest) {
            return 0;
        }
        m_known[id] = Known{interest, 0, 0xFFFFFFFFU, std::nullopt};
        m_used.insert(interest);
        return interest;
    }

    /**
     * Removes an entity. Its number stays in use until release: the client must get the stop
     * for the number before a new entity gets it.
     * @return The number of the entity, or 0 when the table does not have the entity.
     */
    std::uint32_t remove(world::EntityId id) {
        const auto found = m_known.find(id);
        if (found == m_known.end()) {
            return 0;
        }
        const std::uint32_t interest = found->second.interest;
        m_known.erase(found);
        if (id == m_own) {
            m_own = 0;
        }
        return interest;
    }

    /** The stop for a number went to the client: a new entity can get the number. */
    void release(std::uint32_t interest) {
        m_used.erase(interest);
    }

    /** The session has no character and no client that gets records. */
    void clear() {
        m_own = 0;
        m_known.clear();
        m_used.clear();
    }

    [[nodiscard]] Known* find(world::EntityId id) {
        const auto found = m_known.find(id);
        return found == m_known.end() ? nullptr : &found->second;
    }

    [[nodiscard]] const std::map<world::EntityId, Known>& known() const {
        return m_known;
    }

private:
    world::EntityId m_own{0};
    std::map<world::EntityId, Known> m_known;
    /** Numbers in use: the numbers of the table, and the numbers whose stop did not go out. */
    std::set<std::uint32_t> m_used;
};

} // namespace soulwarden::backend::server::rep
