#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "gamedata/gathering_data.h"
#include "world/entity.h"
#include "world/interactions.h"
#include "world/inventory.h"
#include "world/world_clock.h"

// The gathering of the resources of the world (stage 3.6a, docs/systems/gathering.md, protocol notes 4.44): the natural nodes of the
// distribution files, the capital nodes of the layers, and the skinning of the body of a creature.
//
// The client makes each node from its level data and shows its prompt; with the tool of the node in its paperdoll slot (33 axe, 34
// sickle, 35 skinning knife, 36 pickaxe) the key E sends GatherOption (action 14, committed) as the interaction block of its input
// report (live checks L2, L6 of stage 3.6a). The interaction base (world/interactions.h) checks the request (the reference, the option,
// the 3D range of the bound; a natural node and a creature body through the resolver of this system) and calls the handler of this
// system. The rules, in this sequence (each refusal changes nothing; the source of each rule is in the document):
//   1. The node: a capital node (the placement UUID), a natural node (its offline id: a region and a record of its distribution file),
//      or the body of a dead creature with a skinning row. Its gatherable row is supported (no item, song, status, faction, quest, or FFA
//      rule of the data that the backend cannot check). The node is not depleted.
//   2. The tool: a row of a gathering type (Chopping, Cutting, Dressing, Mining) needs an item of that type in its paperdoll slot (the
//      weapon row of the item: GatheringTypes), not broken (durability 0 of a maximum above 0). A row of the type None needs no tool.
//   3. The rank: the trade skill of the row at RequiredTradeskillLevel or more.
//   4. The work: the amount BaseGatherTime (or the rest of an earlier work on the node when the row does not reset the work at an end),
//      the speed (the tool, the rank, the EFF parts), and the time amount / speed: a timed use of the base. The Gathering state of the
//      owner gets Success, the amount, the speed (field 2), the G of the node (live check L3: the ring of the prompt).
//   5. At the end of the time (the checks of the base again): the node is still not depleted (the first valid completion of two
//      characters wins), the tool and the rank again, the roll of the loot root of the row (the roller of the rewards, with the luck of
//      the rank and the ROL parts), the trade skill experience of the event of the row. A full bag refuses the gather before the commit:
//      no item, no experience, no depletion. One transaction of the store: the items, the experience, and the row of the depleted node.
//   6. The node is Depleted for each observer (the node state of its entity, slot of its GatherableController), the Gathering state Done
//      (the client ends its use), and the respawn is one uniform draw of the respawn bounds of the row, on the run clock of the world.
// A cancel before the end (the release, 2473, a walk out of the range, a travel, the death, damage) gives nothing and the state Failure
// (live check L4: Failure ends the use of the client, None does not).
//
// The nodes are values of the world with their key (MP-1): a node is shared by its observers (research Q1). The work of a character is a
// value of the world with the character id as its key; the store has only the depleted nodes (table gathering_nodes). A restart keeps the
// remaining respawn time of a depleted node (the run clock: the downtime does not count), and a work in progress ends.
//
// The cost depends on the requests, the works, and the depleted nodes, not on the size of the world (MP-8): a request finds its node by
// its key or its G (a hash, the region file of a natural node at its first use), a work is one timer of the base, a respawn is one entry
// of the due list, and the depleted nodes are in the grid of the object entities of the base. World thread only.

namespace soulwarden::backend::world {

class World;
class Chests;

/** Rules of this project and of the client for the gathering (docs/systems/gathering.md). */
namespace gathering_rules {
/** The node states (client enum 0x3378D60). */
inline constexpr std::uint8_t kHealthy = 4;
inline constexpr std::uint8_t kDepleted = 8;
/** The Gathering status values (client enum 0x49024D0; live check L4: Failure ends a use, None does not). */
inline constexpr std::uint8_t kStatusSuccess = 1;
inline constexpr std::uint8_t kStatusFailure = 2;
inline constexpr std::uint8_t kStatusDone = 3;
/** The speed of a gather with no tool (field 2 of the Gathering state; live check L3 of stage 3.5b.1: 1 runs the time of the data). */
inline constexpr float kNoToolSpeed = 1.0F;
/** A respawn that the store did not write comes again after this time. */
inline constexpr std::chrono::seconds kRespawnRetry{10};
/** The development override of a respawn: from 1 s to a week. */
inline constexpr std::chrono::seconds kLongestOverride{7 * 24 * 3600};
/**
 * A skinned body keeps its entry while the creature system has it (code review of stage 3.6a, finding GA-R1): the due list looks at the
 * body again after this time, and the entry goes only when the body is gone.
 */
inline constexpr std::chrono::seconds kBodyCheck{10};
} // namespace gathering_rules

enum class NodeKind : std::uint8_t {
    /** A placement of a layer (capitals): an object of the interaction data. */
    capital,
    /** A record of a distribution file: its offline id. */
    natural,
    /** The body of a dead creature (skinning). */
    body,
};

[[nodiscard]] const char* node_kind_text(NodeKind kind);

/** A node with a state of the server (sparse: a node that was depleted while the backend ran, or that the store had). */
struct GatherNode {
    std::string key;
    NodeKind kind{NodeKind::capital};
    std::uint32_t row{0};
    std::uint32_t templateIndex{0};
    /** A capital node: the index of its object in the interaction data. */
    std::uint32_t objectIndex{0};
    /** A natural node: its G (its offline id); a body: the entity of the creature. */
    EntityId group{0};
    /** The source of its identity and its version ("capital", "distribution:VERSION", "body"). */
    std::string source;
    state::WorldPosition position;
    bool depleted{false};
    std::uint64_t generation{0};
    /** The respawn on the world clock and on the run clock of the world. */
    WorldClock::time_point respawn{};
    std::uint64_t respawnRunNs{0};
    std::int64_t depletedAt{0};
    /** A body: the creation time of its creature (microseconds of the world clock; a new creature is a new body). */
    std::uint64_t bodyMade{0};
};

/** The parts of the speed and of the luck of a gather (the log, the view, and the tests). */
struct GatherParts {
    /** The speed of the tool (live check L7: MinGatherEFF to MaxGatherEFF over the gear score range of the item); 1 with no tool. */
    float toolSpeed{gathering_rules::kNoToolSpeed};
    /** The rank part (GatheringEfficiency of the rank row) and the EFF parts of the statuses, the attribute thresholds, the tool perks. */
    float rankEfficiency{0.0F};
    float effects{0.0F};
    /** The speed of the Gathering state: toolSpeed * (1 + rankEfficiency + effects) (a decision of an agent: docs/systems/gathering.md). */
    float speed{gathering_rules::kNoToolSpeed};
    /** The luck: RollBonus of the rank row and the ROL parts. */
    float rollBonus{0.0F};
    float rollEffects{0.0F};
    /** Kept, not used (no rule of the client for them): GatheringMultiplier and the MULT parts, the INS parts. */
    float multiplier{0.0F};
    float instant{0.0F};
    /** The tool item and its gear score (empty: no tool). */
    std::string toolItem;
    std::uint32_t toolGearScore{0};
};

/** The work of a character on a node. */
struct GatherWork {
    std::string node;
    std::uint64_t nodeGeneration{0};
    NodeKind kind{NodeKind::capital};
    std::uint32_t row{0};
    /** The work amount at the start (BaseGatherTime, or the rest of an earlier work). */
    float amount{0.0F};
    GatherParts parts;
    /** The time of the use: amount / speed (seconds). */
    float seconds{0.0F};
    WorldClock::time_point started{};
    WorldClock::time_point due{};
    /** The G of the node (the Gathering state). */
    std::uint64_t group{0};
};

enum class GatherRefusal : std::uint8_t {
    noData,
    /** The object is not a node of the gathering data (a quest object, a different slice). */
    notNode,
    /** A rule of the row that the backend has no predicate for. */
    unsupported,
    depleted,
    /** The body of a creature that lives. */
    alive,
    noTool,
    brokenTool,
    rank,
    /** The loot tables have no table of the root of the row. */
    noLoot,
    /** The trade skill of the event of the row is not a trade skill of the progression tables. */
    noSkill,
    /**
     * Stage 1.8c: a quest object (InteractOnlyOnQuest) and no active task of the character with its InteractTag (client 0x8261FC0: the
     * client shows no prompt then; live check G1).
     */
    notOnQuest,
};
inline constexpr std::size_t kGatherRefusalCount = 11;

[[nodiscard]] const char* gather_refusal_text(GatherRefusal refusal);

struct GatheringCounts {
    std::uint64_t begins{0};
    std::uint64_t completions{0};
    std::uint64_t cancels{0};
    std::array<std::uint64_t, kGatherRefusalCount> refusals{};
    /** Completions that found the node depleted by a different character (the first valid completion wins). */
    std::uint64_t contested{0};
    /** Completions that found no place in the bag for the whole result: nothing changed. */
    std::uint64_t fullBags{0};
    std::uint64_t storeFailures{0};
    std::uint64_t depletions{0};
    std::uint64_t respawns{0};
    std::uint64_t itemsGiven{0};
    std::uint64_t experience{0};
    std::uint64_t rolls{0};
    /** The due respawns that the tick looked at, the stored rows read at the start, the rows of an older source version. */
    std::uint64_t dueLooks{0};
    std::uint64_t storedNodes{0};
    std::uint64_t staleRows{0};
    /** The stored rows that the backend could not check at the start (a region file that is not there or not the file of the data, no
     *  interaction data): they stay in the store (code review of stage 3.6a, finding GA-R2). */
    std::uint64_t keptRows{0};
    /** The requests that the resolver answered (natural nodes and bodies). */
    std::uint64_t resolved{0};
};

/** A node near a character, for a reader. */
struct NodeView {
    std::string key;
    NodeKind kind{NodeKind::capital};
    std::string row;
    bool depleted{false};
    double respawnSeconds{0.0};
    float distance{0.0F};
    std::uint64_t generation{0};
};

struct GatheringView {
    bool inWorld{false};
    std::optional<GatherWork> work;
    /** The nodes with a state of the server `radius` or less from the character. */
    std::vector<NodeView> nodes;
    /** The speed and the luck of the character for each trade skill of gathering (with its tools in its paperdoll). */
    std::map<std::string, GatherParts> skills;
};

enum class NodeError {
    none,
    noData,
    /** No node with this key or G. */
    unknownNode,
    /** The node is not depleted (a respawn override). */
    notDepleted,
    badTime,
    storeFailed,
};

[[nodiscard]] const char* node_error_text(NodeError error);

class Gathering final : public ObjectResolver {
public:
    /** @param data The gathering data (game data), or null: each gather is refused. */
    Gathering(World& world, const gamedata::GatheringData* data);
    ~Gathering() override;
    Gathering(const Gathering&) = delete;
    Gathering& operator=(const Gathering&) = delete;

    [[nodiscard]] const gamedata::GatheringData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }

    /** The resolver of the base: a natural node from its offline id, the body of a creature from its entity. */
    [[nodiscard]] std::optional<ExternalObject> resolve(const InteractionReference& reference) override;
    [[nodiscard]] bool alive(const ExternalObject& object) const override;

    /** @return True when the object of a use is a node of the gathering data (a capital node, a natural node, a creature body). */
    [[nodiscard]] bool owns(const InteractionContext& context) const;
    /**
     * The handler (world/interactions.h): an accepted use of a node prepares its work. The work begins at started(), after the base ended
     * the use that the new use replaces (code review of stage 3.6a, finding GA-R3: the end of the old use keeps the rest of its work).
     */
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context);
    /** The handler: the base registered the use: the work that begin prepared begins (the Gathering state Success). */
    void started(Entity& character, const InteractionContext& context);
    /** @return The time of the work of a character (the timed use of the base), or 0. */
    [[nodiscard]] float work_seconds(const Entity& character) const;
    /** The handler: the timed use reached its end, and the checks of the base passed. */
    /** @return True when the gather completed (code review of stage 1.8c, finding QR8C-R12). */
    bool complete(Entity& character, const InteractionContext& context);
    /** The handler: the use ends (before its completion: a cancel). */
    void ended(Entity& character, const InteractionContext& context, InteractionEnd cause);

    /** The world tick: the stored nodes at the first tick, the respawns whose time came (the due list). */
    void tick();
    /** The world calls it at the leave of a character: its work ends (the rest of its work stays only while it is in the world). */
    void left(Entity& character);
    /** The creature system calls it at the death of a creature: a body with a skinning row gets the node state Healthy. */
    void creature_died(Entity& creature);
    /**
     * The world removed a creature (code review of stage 3.6a, finding GA-R1): the entry of its skinned body goes now. The due list also
     * removes an entry whose body is gone (kBodyCheck), and a new creature has a new entity id.
     */
    void body_removed(EntityId creature);

    /** @return The work and the nodes near a character (a command). */
    [[nodiscard]] GatheringView view(const std::string& characterId, float radius);
    /** @return The speed and the luck of a character for a row (also a command; no work). */
    [[nodiscard]] GatherParts parts_of(const Entity& character, const gamedata::GatherableRow& row, std::string* toolError = nullptr) const;
    /**
     * Development aid (only the development routes, which need SOULWARDEN_HARNESS=1): the depleted node `key` (a placement UUID, a key
     * "REGION#RECORD", or a G in 16 hexadecimal digits) respawns after `seconds` (1 s to a week), not at its time of the data.
     */
    NodeError set_respawn(const std::string& key, std::chrono::seconds seconds);

    [[nodiscard]] const GatheringCounts& counts() const {
        return m_counts;
    }
    [[nodiscard]] std::size_t node_count() const {
        return m_nodes.size();
    }
    [[nodiscard]] std::size_t depleted_count() const;
    [[nodiscard]] std::size_t due_count() const {
        return m_due.size();
    }
    [[nodiscard]] std::size_t work_count() const {
        return m_work.size();
    }
    /** @return A node with a state of the server, or null. */
    [[nodiscard]] const GatherNode* node(const std::string& key) const;

    /**
     * @return The sum of a column of the status rows of a character: its statuses that run (a food, a potion, the statuses of its gear),
     *         times the potency and the stacks, and the threshold abilities of its attributes. A gathering uses EFF, ROL, MULT, INS of its
     *         trade skill; a craft (stage 3.7b) uses ROL of the trade skill of the recipe (the roll bonus of the bonus items).
     */
    [[nodiscard]] float effect_sum(const Entity& character, const std::string& column) const;

private:
    struct Due {
        std::string key;
        std::uint64_t generation{0};
    };
    /** The rest of an earlier work of a character on a node (a row that does not reset the work at an end). */
    struct Rest {
        std::string node;
        std::uint64_t generation{0};
        float amount{0.0F};
    };
    /**
     * A roll that found no place in the bag (or an item that the catalog does not have): the next completion on the node gives it. A
     * character has one for each node (code review of stage 3.6a, finding GA-R7: a gather of another node does not replace it).
     */
    struct KeptRoll {
        std::string node;
        std::uint64_t generation{0};
        std::vector<GrantItem> items;
    };
    /** The values of a node of a use: its key, its kind, its row, its template, its G, its place. */
    struct Target {
        std::string key;
        NodeKind kind{NodeKind::capital};
        std::uint32_t row{0};
        std::uint32_t templateIndex{0};
        std::uint32_t objectIndex{0};
        EntityId group{0};
        std::string source;
        state::WorldPosition position;
        /** Stage 1.8c: the InteractTag of a quest object (lower case), and InteractOnlyOnQuest. */
        std::string interactTag;
        bool onlyOnQuest{false};
    };

    [[nodiscard]] std::optional<Target> target_of(const InteractionContext& context) const;
    [[nodiscard]] InteractionVerdict refuse(GatherRefusal refusal, const Entity& character, const std::string& key, const std::string& detail);
    /** @return The refusal of the tool rule of a row for a character, or no value (and the parts). */
    [[nodiscard]] std::optional<GatherRefusal> check_tool_and_rank(const Entity& character, const gamedata::GatherableRow& row,
                                                                   std::string& detail) const;
    void set_gather(Entity& character, std::uint8_t status, float remaining, float speed, bool active, std::uint64_t target);
    void fail_work(Entity& character);
    /** Stage 1.8c: the events of the quests after a gather (the items of the roll, the game event of the row, the tag of a quest object). */
    void quest_events(Entity& character, const gamedata::GatherableRow& row, const std::vector<GrantItem>& items, const std::string& interactTag,
                      const std::string& boundTag);
    [[nodiscard]] bool is_depleted(const std::string& key) const;
    /** Gives a node its state on its entity (the node state of its GatherableController): each observer gets it. */
    void publish(GatherNode& node);
    [[nodiscard]] std::optional<ExternalObject> natural_object(const gamedata::NaturalNode& node) const;
    [[nodiscard]] std::optional<ExternalObject> body_object(const Entity& creature) const;
    [[nodiscard]] std::optional<std::uint32_t> gather_option() const;
    [[nodiscard]] std::chrono::milliseconds respawn_time(const gamedata::GatherableRow& row);
    void load_stored();
    void respawn(GatherNode& node);
    /** @return True when a kept roll is for the node as it is now (its generation, not depleted). */
    [[nodiscard]] bool roll_current(const KeptRoll& roll) const;
    [[nodiscard]] std::int64_t wall_ms(WorldClock::time_point time) const;

    World& m_world;
    const gamedata::GatheringData* m_data;
    std::unique_ptr<InteractionHandler> m_handler;
    std::unordered_map<std::string, GatherNode> m_nodes;
    std::unordered_map<std::string, GatherWork> m_work;
    /** The work that begin prepared, by character: started() makes it the work of the character (finding GA-R3). */
    std::unordered_map<std::string, GatherWork> m_pending;
    std::unordered_map<std::string, std::vector<Rest>> m_rests;
    std::unordered_map<std::string, std::vector<KeptRoll>> m_kept;
    /** The regions whose file cannot be read: the log named each one (finding GA-R2). */
    std::unordered_set<std::uint32_t> m_badRegions;
    std::multimap<WorldClock::time_point, Due> m_due;
    bool m_loaded{false};
    GatheringCounts m_counts;
};

} // namespace soulwarden::backend::world
