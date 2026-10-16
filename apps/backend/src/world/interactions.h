#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "gamedata/interaction_data.h"
#include "world/entity.h"
#include "world/ids.h"

// The interaction system (stage 2.5a, docs/systems/interactions.md, protocol notes 4.35): the use of an object of the level data
// by a character. The client sends its request as the interaction block of its input report (server/rep/client_input.h,
// InputInteraction); the client shows the prompt and opens a window itself, and the reply 3743 of the server confirms or
// refuses the request (client handler 0x49175F0). This system checks each request, keeps the committed interaction of each
// character, and keeps the state of each object (Enabled, HasInteractors of each Interact component).
//
// The checks of a request, in this sequence (research interaction-stage.md, Q3; each refusal changes nothing):
//   1. The character of the session is in the world and lives (MP-5: the character comes from the session).
//   2. The reference (G and the facet target) is a bound of the interaction data, and the actor id is the actor of its object.
//   3. The option id, the option entity, and the action are an option of that bound (the data, not the values of the block).
//   4. A bound with an unresolved name of the data, a status rule, a quest rule, a condition, a line of sight, or an arc: the
//      backend cannot check it, and it refuses (an object is not open because a check is missing).
//   5. The bound is enabled (the server state).
//   6. The character has an exact position (an accepted report), and the 3D distance to the bound is not more than the range
//      of the data (client code 0x35C2E60: the squared distance; greater is refused, equal passes).
//   7. The requirements of the data: the level of the option, the achievement of the option and of the bound. An item, coin,
//      or progression requirement has no check yet: refused.
//   8. The capacity of the bound (m_numberOfInteractorsAllowed; -1: no limit). A character that uses the bound already passes.
//   9. A handler for the kind and the option. With no handler: refused (the domain is not in the backend).
// A one-shot request whose base action is not its own action is a part of the committed interaction of the character: it
// needs that interaction on the same object with that action (research Q4). A committed request replaces the committed
// interaction of the character (the old one ends with the cause `replaced`).
//
// A handler gets the begin of an accepted use (one time for each use) and the end of a committed use. Stage 2.5a has two:
//   - the lore page (ReadLore 47, committed; AddLoreToJournal 50, one-shot): the achievement of the page goes to the
//     character through the ownership system (one row in the database, then the bit of the entity; live check L3: the journal
//     of the client shows the pages of the bits of the achievement state);
//   - the presentation of the client (OpenConversation, the animations of a chair, a bed, a pet, PlayInteractionAnimation,
//     PlayCinematicVideo): the client plays it from its own data, and the use has no effect on the server. A decision of an agent
//     from live check L1: a refusal closes a conversation that the client opened (6705 comes at once).
//
// The state of an object goes to each session near it as the record of an object entity (EntityKind::object): the metadata
// state with the managed actor id of the placement, and the state 2930 of each bound (live check L2: the client applies it to
// the object that it made from the level data). The system makes the entity when the state first differs from the level data
// (a user, a disabled bound) and keeps it while the backend runs: the client keeps the last state of a record after its stop
// (live check L2), and a session that observes the entity again gets its state of now.
//
// The cost depends on the requests and on the active interactions, not on the size of the world (MP-8): a request finds its
// bound in the reference map of the data, a list near a character looks at the cells of the grid near it, and the ends of
// the interactions of a character look only at that character.
//
// The temporary state of a use (the active interaction, the users of an object, a disabled bound) is a value of the world: a
// restart has none (research: no resume contract). The read lore pages are rows of the database (the ownership system).
// World thread only.

namespace soulwarden::backend::world {

class World;

/** A request of a client: the block of a new input step (server/rep/client_input.h). Each value is a claim of the client. */
struct InteractionRequest {
    InteractionReference object;
    std::uint8_t action{0};
    std::uint8_t baseAction{0};
    std::uint64_t optionEntity{0};
    std::uint32_t optionId{0};
    bool committed{false};
    /** The input number of the step (the log). */
    std::uint16_t input{0};
};

/** The result of a request. `accepted` gets the reply 3743 with result 1, `same` and `notInWorld` no reply, each other result 2. */
enum class InteractionVerdict : std::uint8_t {
    accepted,
    /** The request of the committed interaction of the character again (the client restored it after a one-shot): no change. */
    same,
    notInWorld,
    dead,
    /** The backend has no interaction data. */
    noData,
    /** No bound of the data has this reference, or the actor id is not the actor of its object. */
    unknownObject,
    /** The option id, the option entity, or the action is not an option of the bound. */
    unknownOption,
    /** A rule of the data that the backend cannot check: an unresolved name, a status rule, a quest, a condition, a sight, an arc. */
    unsupported,
    disabled,
    /** The character has no exact position (no accepted position report). */
    noPosition,
    tooFar,
    /** A requirement of the data that the character does not have (the level, an achievement), or an item or coin requirement. */
    requirement,
    /** The bound has its number of users. */
    full,
    /** The backend has no handler for this kind and option. */
    noHandler,
    /** A part of a committed interaction (a one-shot with a base action) with no such interaction on the object. */
    noMainInteraction,
    /** The handler refused (the database did not take the row). */
    refused,
};
inline constexpr std::size_t kInteractionVerdictCount = 16;

[[nodiscard]] const char* interaction_verdict_text(InteractionVerdict verdict);

/** Why a committed interaction ended. */
enum class InteractionEnd : std::uint8_t {
    /** The client ended its request: its block went away (the close of the journal, live check L1). */
    clientEnd,
    /** The client sent 2473 with the reference of the interaction (the end of a conversation, an answer to 3746). */
    clientCancel,
    /** A new committed request of the character. */
    replaced,
    /** A command of the admin interface or of the development routes (the client gets 3746). */
    command,
    /** A travel or a respawn (the session sends 3746 and 6709: RepSession::end_input_lock). */
    travel,
    death,
    /** The character left the world. */
    left,
};

[[nodiscard]] const char* interaction_end_text(InteractionEnd cause);

/** The values of one accepted use for a handler: the object, the bound, the option of the data. */
struct InteractionContext {
    std::uint32_t objectIndex{0};
    std::uint32_t boundIndex{0};
    const gamedata::InteractionObject* object{nullptr};
    const gamedata::InteractionBound* bound{nullptr};
    const gamedata::InteractionOption* option{nullptr};
};

/** A handler of a kind and its options. A later stage registers its own one (research Q4: no switch of action numbers). */
class InteractionHandler {
public:
    virtual ~InteractionHandler() = default;
    /** @return A name for the log. */
    [[nodiscard]] virtual const char* name() const = 0;
    /**
     * An accepted use begins (the checks of the base passed). One call for each use: a request again is no new use.
     * @return `accepted`, or `refused` when the domain refuses (it changed nothing).
     */
    [[nodiscard]] virtual InteractionVerdict begin(Entity& character, const InteractionContext& context) = 0;
    /** A committed use ends. */
    virtual void end(Entity& character, const InteractionContext& context, InteractionEnd cause) {
        (void)character;
        (void)context;
        (void)cause;
    }
};

/** One object near a character, for a reader (a command). */
struct InteractionObjectView {
    std::uint32_t index{0};
    const gamedata::InteractionObject* object{nullptr};
    /** The horizontal distance of the nearest bound (meters). */
    float distance{0.0F};
    /** The state of each bound: enabled and the number of users. */
    std::vector<ObjectBound> bounds;
    /** The entity of the object (0: the object has the state of the level data, and no entity). */
    EntityId entity{0};
};

/** The interactions of a character and the objects near it, for a reader (a command). */
struct InteractionsView {
    bool inWorld{false};
    std::optional<ActiveInteraction> active;
    std::vector<InteractionObjectView> objects;
    /** The work of the lookup (case IT-LOAD). */
    gamedata::LookupWork work;
};

enum class ObjectError {
    none,
    /** The backend has no interaction data, or the data has no object with this key. */
    unknownObject,
};

/** The counts of the system: for the log and for the tests. */
struct InteractionCounts {
    std::array<std::uint64_t, kInteractionVerdictCount> verdicts{};
    std::uint64_t begins{0};
    std::uint64_t ends{0};
};

class Interactions {
public:
    /** @param data The interaction data (game data), or null: the system refuses each request. */
    Interactions(World& world, const gamedata::InteractionData* data);
    ~Interactions();
    Interactions(const Interactions&) = delete;
    Interactions& operator=(const Interactions&) = delete;

    /** @return The interaction data, or null when the backend has none. */
    [[nodiscard]] const gamedata::InteractionData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /**
     * Registers a handler for the options of a kind (a later stage: a chest, a station). A second handler for one option
     * replaces the first one. The handler must live as long as the world.
     */
    void register_handler(gamedata::InteractionKind kind, std::uint32_t optionId, InteractionHandler& handler);

    /**
     * The request of a new input step of the client of a session (its block changed). The world finds the character from the
     * session (MP-5). An accepted committed request begins the interaction of the character; an accepted one-shot runs its
     * handler one time.
     */
    [[nodiscard]] InteractionVerdict request(SessionId session, const InteractionRequest& request);

    /** The block of the client of a session went away: its committed interaction ends (cause clientEnd). */
    void request_ended(SessionId session);

    /**
     * The message 2473 of the client of a session (a full body: the facet, G, the 32 bytes of the reference). A reference that
     * is not the reference of the committed interaction of the character changes nothing (a late or an echo cancel).
     * @return True when an interaction ended.
     */
    bool client_cancel(SessionId session, const InteractionReference& reference);

    /**
     * Ends the committed interaction of a character (a command): the session of the character sends 3746.
     * @return False when the character is not in the world or has no committed interaction.
     */
    bool cancel(const std::string& characterId);

    /** The world calls these: the end of each interaction of a character that travels, dies, or leaves. */
    void traveled(Entity& character);
    void died(Entity& character);
    void left(Entity& character);

    /**
     * Enables or disables each bound of an object (a command; research I2): each session near it gets the state, and a
     * disabled bound refuses each request. A character that uses a bound that goes off keeps its use (the end of a use has
     * its own causes).
     */
    [[nodiscard]] ObjectError set_enabled(const std::string& key, bool enabled);

    /** @return The interactions of a character and the objects that are `radius` or less from it (horizontal). */
    [[nodiscard]] InteractionsView view(const std::string& characterId, float radius) const;

    [[nodiscard]] const InteractionCounts& counts() const {
        return m_counts;
    }
    /** @return The number of object entities that the system made. */
    [[nodiscard]] std::size_t object_entities() const {
        return m_objects.size();
    }
    /** @return The number of characters with a committed interaction. */
    [[nodiscard]] std::size_t active_count() const {
        return m_active.size();
    }

private:
    struct BoundState {
        bool enabled{true};
        std::vector<EntityId> users;
    };
    /** The state of an object that differs from the level data, and its entity. */
    struct ObjectState {
        std::vector<BoundState> bounds;
        EntityId entity{0};
    };

    [[nodiscard]] InteractionVerdict check(Entity& character, const InteractionRequest& request, InteractionContext& context,
                                           InteractionHandler*& handler);
    [[nodiscard]] InteractionVerdict refuse(InteractionVerdict verdict, const Entity& character, const InteractionRequest& request);
    /** Ends the committed interaction of a character. @param notify True: the session of the character sends 3746. */
    void end(Entity& character, InteractionEnd cause, bool notify);
    [[nodiscard]] ObjectState& state_of(std::uint32_t objectIndex);
    /** Gives the entity of an object the state of now and marks its state (a new entity for an object with no entity). */
    void publish(std::uint32_t objectIndex);
    [[nodiscard]] InteractionHandler* handler_of(gamedata::InteractionKind kind, std::uint32_t optionId) const;

    World& m_world;
    const gamedata::InteractionData* m_data;
    /** The handlers of stage 2.5a: the lore page, and the presentation of the client. */
    std::unique_ptr<InteractionHandler> m_lore;
    std::unique_ptr<InteractionHandler> m_presentation;
    std::map<std::pair<gamedata::InteractionKind, std::uint32_t>, InteractionHandler*> m_handlers;
    /** The objects whose state differs from the level data, or that have an entity, by their index. */
    std::map<std::uint32_t, ObjectState> m_objects;
    /** The characters with a committed interaction (MP-8: an end visits only them). */
    std::set<EntityId> m_active;
    std::uint64_t m_serial{0};
    InteractionCounts m_counts;
    std::array<bool, kInteractionVerdictCount> m_logged{};
};

} // namespace soulwarden::backend::world
