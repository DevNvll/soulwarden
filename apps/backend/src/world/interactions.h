#pragma once

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
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
// Two kinds of usable objects (stage 2.5b):
//   - a level object of the interaction data: the client makes it from its level data; its managed actor id comes from the
//     placement UUID; the system makes its entity when its state first differs from the level data (stage 2.5a);
//   - a server child: a station of a settlement, a camp, or a development fixture (the powder keg): the client shows it only after
//     a record of the server (live check L4 of stage 2.5b). Its entity has an actor of the id allocator, and its template (game data)
//     gives the ids: the instance ids and the facet targets come from G of that actor (the XOR rule of protocol notes 4.2). The spawn
//     rule makes the station children of the placements near the characters (150 m; removal when no character is 200 m or less from
//     it, the distances of the spawn points of the creatures); a template child (the forge of a station) comes and goes with its
//     parent. The camp children of the placements stay in the data (stage 2.6).
//
// The checks of a request, in this sequence (research interaction-stage.md, Q3; each refusal changes nothing):
//   1. The character of the session is in the world and lives (MP-5: the character comes from the session).
//   2. The reference (G and the facet target) is a bound of the interaction data or of a server child, and the actor id is the actor
//      of its object.
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
// interaction of the character: the old one ends (cause `replaced`) only after the handler of the new one accepted it (code review
// of stage 2.5a, finding INT-R1: a refused write changes nothing).
//
// A handler gets the begin of an accepted use (one time for each use), the completion of a timed use, and the end of a committed
// use. The handlers:
//   - the lore page (ReadLore 47, committed; AddLoreToJournal 50, one-shot; stage 2.5a);
//   - the presentation of the client (OpenConversation, the animations, PlayCinematicVideo; stage 2.5a);
//   - the station (stage 2.5b: OpenCraftingOption and OpenCraftingOptionLevel60 of a crafting station or a camp): the StationContext of
//     the use, from the data of the object (the Assembly key, the fee flag, the Assembly reference, the parent settlement); a key with
//     no row of the crafting station database is refused. The recipes and the crafts are stage 3.7;
//   - the timed fixture (stage 2.5b: Trigger of the powder keg): the completion counts on the object; no explosion and no reward.
//
// A timed use (an option with a Delay Time: Trigger 5 s; live check L6 of stage 2.5b): the client counts the Delay Time from its
// request (0x49106F0, 0x49146F0) and executes the object at zero (0x4910750); a release before sends 2473. The server counts the full
// Delay Time of the option row on the world clock from the accepted request (a decision of an agent from the research: the client
// timer has no factor 0.6, and the original server rule is not known), checks the use again at the end (the source, Enabled, the
// range, the life), and completes it one time. The use stays until the client ends it (its release sends 2473).
//
// The ends of a committed use: the end of the block, 2473 with the reference, a new committed request, a command (3746), the death
// (3746), a travel or a respawn (3746 and 6709), the leave, the removal of the source (3746), the loss of the range (3746: an accepted
// position of the character farther than the range of the bound), and damage for a use whose option ends at damage (3746):
//   - the action PlayInteractionAnimation (64: a chair, a bed, an animation): the action InteractionAnimation of the client ends
//     at IfDamaged (game data: playeractions_interact.actionlist);
//   - a timed option (Delay Time above 0): a decision of an agent (stage 2.5b, live check L7: the client continues its timed use at a
//     hit; the action list has no damage rule for DelayedInteraction).
// The client ends a conversation at 3746 itself (it sends 6705; the session answers 6709: live check L7).
//
// The service handoff (stage 2.5b; the base of the shops 5.1 and the transmog 5.6; research Q4): a handler can give the character of
// an accepted use a service of its source (grant_service). The grant stays after the end of the use (a conversation that the client
// closes with 2473), so that a later operation with no source id (a purchase 3741) can check it (require_service): the same world
// entry, the same source and generation, the source near (its range), the service and the id in the offer. A new source use, the
// removal of the source, the death, a travel, the leave, and close_service end it. No time limit (the option data has none).
//
// The state of an object goes to each session near it as the record of an object entity (EntityKind::object). Each session observes an
// object at the distance of a character from its nearest bound (finding INT-R3: a bound can be more than 100 m from its placement
// root), with the grid of this system: for_each_entity_near.
//
// The cost depends on the requests, the active uses, and the objects near the players, not on the size of the world (MP-8): a
// request finds its bound in the reference map of the data or in the map of the server children (by G), a list near a character
// looks at the cells of the grid near it, the spawn rule looks at the cells near each character and at the live children, a timed
// use has a timer of the world, and the ends of a character look only at that character.
//
// The temporary state of a use (the active interaction, the users of an object, a disabled bound, a service grant, the server
// children) is a value of the world: a restart has none (research: no resume contract). The read lore pages are rows of the database.
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
    /** No bound of the data or of a server child has this reference, or the actor id is not the actor of its object. */
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
    /** The handler refused (the database did not take the row, a station key with no row). */
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
    /** The source went away (stage 2.5b: the removal of a server child; the client gets 3746). */
    sourceRemoved,
    /** An accepted position of the character farther than the range of the bound (stage 2.5b; the client gets 3746). */
    rangeLost,
    /** Damage for a use whose option ends at damage (stage 2.5b; the client gets 3746). */
    damage,
    /** The check at the end of a timed use failed (stage 2.5b: the bound is disabled; the client gets 3746). */
    policyFailed,
};

[[nodiscard]] const char* interaction_end_text(InteractionEnd cause);

/** The values of one accepted use for a handler: the object, the bound, the option of the data. */
struct InteractionContext {
    /** The object of the data (a level object) or the template (a server child: `instance` is its entity). */
    std::uint32_t objectIndex{0};
    std::uint32_t boundIndex{0};
    const gamedata::InteractionObject* object{nullptr};
    const gamedata::InteractionBound* bound{nullptr};
    const gamedata::InteractionOption* option{nullptr};
    EntityId instance{0};
    std::uint64_t generation{0};
    /** True: an object that the data does not have (stage 3.6a: a natural node, a creature body); `instance` is its G. */
    bool external{false};
};

/**
 * The facts of a use of a crafting station or a camp (stage 2.5b, research Q1 "StationContext"): what stage 3.7 needs, from the data
 * of the registered object, never from the block of the client.
 */
struct StationContext {
    /** The durable key of the object (a placement UUID; a server child: "UUID/ENTITY"; a fixture: "fixture/NAME/N"). */
    std::string objectKey;
    std::uint64_t objectGeneration{0};
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t group{0};
    /** The facet target of the Interact bound of the use, and the instance id of its option entity. */
    std::uint64_t interactTarget{0};
    std::uint64_t optionEntity{0};
    /** The Assembly: the facet target (instance id + component id), the instance id of its entity, the key, and the fee flag. */
    std::uint64_t assemblyTarget{0};
    std::uint64_t assemblyEntity{0};
    std::string assemblyKey;
    bool chargeCraftingFee{false};
    /** True: the key is a row of the crafting station database (stage 3.7 must not craft at a key with no row). */
    bool keyResolved{false};
    /** The parent placement (the settlement) and its managed actor id. Empty: no parent (a level object, a fixture). */
    std::string parentKey;
    std::array<std::uint8_t, 16> parentActor{};
    std::string region;
    state::WorldPosition position;
    float rangeSq{0.0F};
    bool isCamp{false};
};

/**
 * An object that the interaction data does not have (stage 3.6a, docs/systems/gathering.md): a natural gathering node of a distribution
 * file (the client makes it from its level data and gives it an offline id: live check L2 of stage 3.6a), or the body of a creature
 * (skinning). A resolver of a later stage gives it from the reference of a request. The base checks its use as the use of a level object:
 * the option, the range, the requirements, the handler.
 */
struct ExternalObject {
    /** The key (a durable key of the node), the kind, the slice, the asset, the actor, G (`group`), the place, the bounds (world places,
     *  facet targets, options with their entity). */
    gamedata::InteractionObject object;
    /** The generation of the source (a creature body: a new entity has a new one). */
    std::uint64_t generation{0};
    /** The entity that carries the state of the source: 0 for a natural node (the base makes an object entity), or a creature. */
    EntityId body{0};
};

/** A resolver of the objects that the interaction data does not have (stage 3.6a: the gathering system). */
class ObjectResolver {
public:
    virtual ~ObjectResolver() = default;
    /** @return The object of a reference (G and the facet target) that the data does not have, or no value. */
    [[nodiscard]] virtual std::optional<ExternalObject> resolve(const InteractionReference& reference) = 0;
    /** @return True while the body of an external object is still its source (a creature body that went away is not). */
    [[nodiscard]] virtual bool alive(const ExternalObject& object) const = 0;
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
    /**
     * The base registered an accepted use (stage 3.5b.2): a committed use is the interaction of the character now, so the handler can end
     * it (end_use). One call for each accepted use, after begin. The loot bags take the items of Take All here.
     */
    virtual void started(Entity& character, const InteractionContext& context) {
        (void)character;
        (void)context;
    }
    /**
     * @return The time of a use of this handler (seconds; code review of stage 3.5b.1, finding CH-R2: the open of a chest), or 0: the
     *         Delay Time of the option. A use with a time is a timed use of the base: the timer, complete() after the checks again, and
     *         the completion before the end of a late release (finding ST-R1). Called after begin accepted it.
     */
    [[nodiscard]] virtual float use_time(const Entity& character, const InteractionContext& context) const {
        (void)character;
        (void)context;
        return 0.0F;
    }
    /** A timed use reached its end, and the checks again passed (stage 2.5b): one call for each use. */
    virtual void complete(Entity& character, const InteractionContext& context) {
        (void)character;
        (void)context;
    }
    /** A committed use ends. */
    virtual void end(Entity& character, const InteractionContext& context, InteractionEnd cause) {
        (void)character;
        (void)context;
        (void)cause;
    }
    /**
     * True: the handler owns the line-of-sight rule of its bounds (stage 3.5b.1: a chest; docs/systems/chests.md), and the base does
     * not refuse a bound with m_requiresLineOfSight. False (the default): such a bound is refused (`unsupported`).
     */
    [[nodiscard]] virtual bool owns_sight() const {
        return false;
    }
    /** True: a use of this handler ends at damage (stage 3.5b.1: the Gather action of the client ends at a hit, game data). */
    [[nodiscard]] virtual bool ends_at_damage() const {
        return false;
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
    /** A server child (stage 2.5b): its generation, its parent (the key of the placement), its fixture name, the completions. */
    bool spawned{false};
    std::uint64_t generation{0};
    std::string parentKey;
    std::string fixture;
    std::uint32_t completions{0};
};

/** The interactions of a character and the objects near it, for a reader (a command). */
struct InteractionsView {
    bool inWorld{false};
    std::optional<ActiveInteraction> active;
    /** The object of the active use (no value: none), its station facts (a station), and the service grant of the character. */
    const gamedata::InteractionObject* activeObject{nullptr};
    std::optional<StationContext> station;
    std::optional<ServiceGrant> grant;
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
    /** Stage 2.5b: the completions of the timed uses, the server children made and removed, the scans of the spawn rule. */
    std::uint64_t completions{0};
    std::uint64_t spawned{0};
    std::uint64_t removed{0};
    std::uint64_t scans{0};
    /** The work of the last scan of the spawn rule (case IT-LOAD). */
    gamedata::LookupWork scanWork;
};

/** Why a fixture was not made (stage 2.5b, a development command). */
enum class FixtureError {
    none,
    /** The backend has no interaction data, or the data has no fixture of this name. */
    unknownFixture,
    notInWorld,
    /** The place is not a position of the world. */
    badPosition,
};

struct FixtureResult {
    FixtureError error{FixtureError::none};
    Entity* entity{nullptr};
};

/** Why require_service refused (stage 2.5b). */
enum class ServiceError {
    none,
    notInWorld,
    /** The character has no grant, or a grant of a different kind. */
    noGrant,
    /** The grant is of an older world entry. */
    oldEntry,
    /** The source went away, or it is a new child (a different generation). */
    sourceGone,
    /** The character is farther from the source than the range of its bound. */
    tooFar,
    /** The id is not in the offer of the source. */
    notOffered,
    /** The deadline of the grant passed. */
    expired,
};

[[nodiscard]] const char* service_error_text(ServiceError error);

class Interactions {
public:
    /** Interval of the spawn rule of the server children on the world clock (the interval of the spawn points of the creatures). */
    static constexpr std::chrono::milliseconds kSpawnInterval{500};
    /** A server child is made when a character is this near (horizontal), and removed when no character is this near (meters). */
    static constexpr float kSpawnEnter = 150.0F;
    static constexpr float kSpawnLeave = 200.0F;

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
    /** @return The handler of an option of a kind, or null (stage 3.6a: the gathering handler wraps the chest handler). */
    [[nodiscard]] InteractionHandler* handler(gamedata::InteractionKind kind, std::uint32_t optionId) const {
        return handler_of(kind, optionId);
    }
    /** Registers the resolver of the objects that the data does not have (stage 3.6a). It must live as long as the world. */
    void register_resolver(ObjectResolver& resolver) {
        m_resolver = &resolver;
    }
    /**
     * @return The entity of a level object of the data (stage 3.6a: the node state of a capital gathering node): the base makes it when
     *         it has none (as for a state that differs from the level data). Null: no such object.
     */
    Entity* level_entity(std::uint32_t objectIndex);
    /**
     * Registers an object that the data does not have (stage 3.6a) and @return the entity that carries its state: an object entity with
     * its managed actor (a natural node; the base makes it once, and each session near it observes it), or the body of a creature.
     */
    Entity* external_entity(const ExternalObject& object);
    /** @return The external object of a G (registered by a request or by external_entity), or null. */
    [[nodiscard]] const ExternalObject* external(EntityId group) const;
    /**
     * The world calls it at the removal of a creature: the external object of its body goes (a use of it ends at the next check: the
     * source is gone). The entries of the bodies do not grow with the kills.
     */
    void body_removed(EntityId creature);

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
    /** The health system calls it for each accepted damage of a character that lives (stage 2.5b): the damage rule of its use. */
    void damaged(Entity& character);
    /** The world calls it after an accepted position of a character (stage 2.5b): the range of its use. O(1). */
    void moved(Entity& character);
    /**
     * @return True when the committed use of a character still passes the checks of its source (stage 3.5b.1: the end of the open of a
     *         chest): the source, Enabled, the life, the exact position, and the range. False sets `cause`.
     */
    [[nodiscard]] bool use_valid(const Entity& character, InteractionEnd& cause) const;
    /** Ends the committed use of a character with a cause (stage 3.5b.1: a handler of a timed use): its client gets 3746. */
    void end_use(Entity& character, InteractionEnd cause);

    /** The world tick: the spawn rule of the server children (each kSpawnInterval of the world clock). */
    void tick();

    /**
     * Enables or disables each bound of an object (a command; research I2): each session near it gets the state, and a
     * disabled bound refuses each request. A character that uses a bound that goes off keeps its use (the end of a use has
     * its own causes). `key`: the placement UUID of a level object, or the key of a live server child.
     */
    [[nodiscard]] ObjectError set_enabled(const std::string& key, bool enabled);

    /**
     * Development aid (stage 2.5b, live checks L5 and L6): makes a fixture of the data near a character (`offset` from it, turned
     * by `yawDegrees` about the vertical axis). `permissionRule`: the permission rule of its Ownership state (-1: the rule of the
     * data). The fixture stays until remove_object.
     */
    [[nodiscard]] FixtureResult make_fixture(const std::string& characterId, const std::string& name, const state::WorldPosition& offset,
                                             float yawDegrees, int permissionRule);
    /**
     * Removes a server child (a fixture or a child of a placement) and its template children: each use of it ends (sourceRemoved,
     * 3746), each grant of it ends, and each session that observes it gets the stop. @return False: no server child with this id.
     */
    bool remove_object(EntityId entity);

    /** A server child of a loot container (stage 3.5b.2): its entity and its generation. Null entity: not made. */
    struct SpawnedContainer {
        Entity* entity{nullptr};
        std::uint64_t generation{0};
    };
    /**
     * Makes a server child of a template at a place (stage 3.5b.2: a loot bag of world/loot_bags.h): the record and the ids of a server
     * child, its bounds, its uses. It stays until remove_object. `key`: its durable key (the key of the bag).
     */
    SpawnedContainer spawn_container(std::uint32_t templateIndex, const state::WorldPosition& place, float yawDegrees, const std::string& key);

    /** @return The interactions of a character and the objects that are `radius` or less from it (horizontal). */
    [[nodiscard]] InteractionsView view(const std::string& characterId, float radius) const;
    /** @return The station facts of the committed use of a character (a crafting station or a camp), or no value. */
    [[nodiscard]] std::optional<StationContext> station(const Entity& character) const;

    /**
     * Gives a character a service of the source of its committed use (stage 2.5b, a handler of a later stage). It replaces the grant
     * of a different source. @return False: the character has no committed use.
     */
    bool grant_service(Entity& character, const std::string& kind, std::vector<std::uint32_t> ids, std::uint64_t revision,
                       std::optional<WorldClock::time_point> deadline = std::nullopt);
    /**
     * @return `none` when the character has a grant of this kind that offers `id` and is still valid: the same world entry, the same
     *         source and generation, the source near (the 3D range of its bound), and no deadline passed. A grant that is not valid
     *         any more ends.
     */
    [[nodiscard]] ServiceError require_service(const std::string& characterId, const std::string& kind, std::uint32_t id);
    /** Ends the service grant of a character (an authenticated close of a later stage). @return False: no grant. */
    bool close_service(const std::string& characterId);

    /**
     * Calls `visit(Entity&)` for each object entity with an interest point `range` or less from `center` (horizontal): a bound, or
     * the place of a server child with no bound. It looks only at the cells near the center (MP-8). Each entity one time.
     */
    template <class Visit>
    void for_each_entity_near(const state::WorldPosition& center, float range, Visit&& visit);
    /** @return The horizontal distance of the nearest interest point of an object entity from a place (another entity: its place). */
    [[nodiscard]] float distance_to(const Entity& entity, const state::WorldPosition& place) const;

    [[nodiscard]] const InteractionCounts& counts() const {
        return m_counts;
    }
    /** @return The number of object entities that the system made (level objects and server children that live). */
    [[nodiscard]] std::size_t object_entities() const;
    /** @return The number of server children that live (stage 2.5b). */
    [[nodiscard]] std::size_t instance_count() const {
        return m_instances.size();
    }
    /** @return The number of characters with a committed interaction. */
    [[nodiscard]] std::size_t active_count() const {
        return m_active.size();
    }
    /** @return The number of timed uses that wait for their end (stage 2.5b). */
    [[nodiscard]] std::size_t timed_count() const {
        return m_timed;
    }
    /** @return The completions of the timed uses of a server child (a fixture). 0: none, or no such child. */
    [[nodiscard]] std::uint32_t completions_of(EntityId entity) const;
    /** @return The entity of the live server child of a placement child key ("UUID/ENTITY"), or 0. */
    [[nodiscard]] EntityId spawned_entity(const std::string& key) const;
    /** @return The object of a key: a level object of the data (its placement UUID) or a live server child (its key). Null: none. */
    [[nodiscard]] const gamedata::InteractionObject* object_of_key(const std::string& key) const;

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
    /** A server child (stage 2.5b): its runtime object (the ids from its G), its state, its links. */
    struct Instance {
        std::uint32_t templateIndex{0};
        std::uint64_t generation{0};
        /** The index of its placement child in the data (-1: a fixture or a template child). */
        std::int64_t spawned{-1};
        EntityId parent{0};
        std::vector<EntityId> children;
        /** The object of the requests: the key, the kind, the slice, the actor, G, the world places of the bounds, the targets. */
        gamedata::InteractionObject object;
        ObjectState state;
        std::string parentKey;
        std::array<std::uint8_t, 16> parentActor{};
        std::string fixture;
        std::uint32_t completions{0};
        /** The cells of the grid of the entities that have this child (its interest points). */
        std::vector<std::int64_t> cells;
    };
    /** An object that the data does not have (stage 3.6a): its object, its state, and the cells of its entity. */
    struct External {
        ExternalObject value;
        ObjectState state;
        std::vector<std::int64_t> cells;
    };
    /** A usable object: a level object of the data (instance 0), a server child, or an external object (stage 3.6a: its G). */
    struct Site {
        const gamedata::InteractionObject* object{nullptr};
        std::uint32_t index{0};
        EntityId instance{0};
        std::uint64_t generation{0};
        bool external{false};
    };

    [[nodiscard]] InteractionVerdict check(Entity& character, const InteractionRequest& request, InteractionContext& context,
                                           InteractionHandler*& handler);
    [[nodiscard]] InteractionVerdict refuse(InteractionVerdict verdict, const Entity& character, const InteractionRequest& request);
    /** Ends the committed interaction of a character. @param notify True: the session of the character sends 3746. */
    void end(Entity& character, InteractionEnd cause, bool notify);
    /** @return The usable object of an active use or of a grant (null object: it went away, or a new generation). */
    [[nodiscard]] Site site_of(std::uint32_t object, EntityId instance, std::uint64_t generation) const;
    [[nodiscard]] ObjectState& state_of(std::uint32_t objectIndex);
    [[nodiscard]] ObjectState* state_of(const Site& site);
    /** Gives the entity of an object the state of now and marks its state (a new entity for a level object with no entity). */
    void publish(std::uint32_t objectIndex);
    void publish(Instance& instance);
    void publish(External& external);
    /** Publishes the state of the source of a site (a level object, a server child, or an external object). */
    void publish(const Site& site);
    [[nodiscard]] InteractionHandler* handler_of(gamedata::InteractionKind kind, std::uint32_t optionId) const;
    /** Makes a server child of a template with its template children. @return Its entity, or null (a place that is not of the world). */
    Entity* spawn(std::uint32_t templateIndex, const state::WorldPosition& place, const gamedata::Rotation& rotation,
                  const std::string& key, std::int64_t spawnedIndex, EntityId parent, const std::string& parentKey,
                  const std::array<std::uint8_t, 16>& parentActor, const std::string& region, int permissionRule, int depth);
    /** The spawn rule: makes the children of the placements near the characters, removes the ones with no character near. */
    void scan();
    /** The timer of a timed use: the end of its Delay Time. */
    void timed_due(const std::string& characterId, std::uint64_t serial);
    /** @return True when the use of a character still passes the checks of its source (the end of a timed use). */
    [[nodiscard]] bool still_valid(const Entity& character, const ActiveInteraction& active, InteractionEnd& cause) const;
    [[nodiscard]] std::optional<StationContext> station_of(const Site& site, std::uint32_t boundIndex, const InteractionReference& reference) const;
    /** Ends the grants of a source (the removal of a server child). */
    void end_grants_of(EntityId instance);
    void end_grant(Entity& character, const char* cause);
    void add_cells(std::vector<std::int64_t>& cells, EntityId entity, const gamedata::InteractionObject& object, const state::WorldPosition& place);
    [[nodiscard]] Entity* find_entity(EntityId id) const;

    World& m_world;
    const gamedata::InteractionData* m_data;
    /** The handlers of the stages 2.5a and 2.5b. */
    std::unique_ptr<InteractionHandler> m_lore;
    std::unique_ptr<InteractionHandler> m_presentation;
    std::unique_ptr<InteractionHandler> m_station;
    std::unique_ptr<InteractionHandler> m_fixture;
    std::map<std::pair<gamedata::InteractionKind, std::uint32_t>, InteractionHandler*> m_handlers;
    /** The level objects whose state differs from the level data, or that have an entity, by their index. */
    std::map<std::uint32_t, ObjectState> m_objects;
    /** The server children that live, by their entity id (= G of their actor). */
    std::map<EntityId, Instance> m_instances;
    /** The objects that the data does not have (stage 3.6a), by their G; the resolver of the gathering system. */
    std::map<EntityId, External> m_externals;
    ObjectResolver* m_resolver{nullptr};
    /** The object entities of the external objects (their G), for the interest of a session. */
    std::set<EntityId> m_externalEntities;
    /** The placement children that live: the index into the data to the entity. */
    std::map<std::uint32_t, EntityId> m_spawnedLive;
    /** The object entities by the cells of their interest points (64 m; finding INT-R3), for for_each_entity_near. */
    std::unordered_map<std::int64_t, std::vector<EntityId>> m_entityCells;
    /** The interest points of each level object entity (its bounds). */
    std::map<EntityId, std::uint32_t> m_levelEntities;
    /** The characters with a committed interaction (MP-8: an end visits only them). */
    std::set<EntityId> m_active;
    /** The characters with a service grant (MP-8: the removal of a source visits only them). */
    std::set<EntityId> m_granted;
    std::uint64_t m_serial{0};
    std::uint64_t m_generation{0};
    std::size_t m_timed{0};
    WorldClock::time_point m_lastScan{};
    bool m_scanned{false};
    InteractionCounts m_counts;
    std::array<bool, kInteractionVerdictCount> m_logged{};
};

template <class Visit>
void Interactions::for_each_entity_near(const state::WorldPosition& center, float range, Visit&& visit) {
    if (m_entityCells.empty() || !(range >= 0.0F)) {
        return;
    }
    const auto low = [](float value) { return static_cast<std::int32_t>(std::floor(value / gamedata::InteractionData::kCellSize)); };
    std::set<EntityId> seen;
    for (std::int32_t x = low(center.x - range); x <= low(center.x + range); ++x) {
        for (std::int32_t y = low(center.y - range); y <= low(center.y + range); ++y) {
            const auto found = m_entityCells.find(gamedata::InteractionData::cell_key(x, y));
            if (found == m_entityCells.end()) {
                continue;
            }
            for (const EntityId id : found->second) {
                if (!seen.insert(id).second) {
                    continue;
                }
                Entity* entity = find_entity(id);
                if (entity != nullptr && distance_to(*entity, center) <= range) {
                    visit(*entity);
                }
            }
        }
    }
}

} // namespace soulwarden::backend::world
