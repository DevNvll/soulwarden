#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// The behavior trees of the creatures (stage BT1, docs/systems/creature-behavior.md, section "Behavior trees").
//
// A tree decides what a creature does next. The engine of world/behavior.h keeps the rules that no tree can change (death, a stun, a
// knockdown, a stagger, a forced movement, the hit stun, an action that plays to its end, the shelter, the immune way back, the contacts,
// the releases, the casts, and the marks with their cancels, the world clock, the one random source). Each leaf of a tree calls a
// function of the behavior that existed before the trees; a leaf fails when the slice of the creature has no such ability, thus one tree
// fits each creature.
//
// The format: JSON with comments (`//` and `/* */`; the vendored JSON library 3.12.0 reads comments and has no option for a trailing
// comma). One file has one tree:
//
//   { "format": 1, "tree": "default/fight", "blackboard": { "phase": { "type": "int", "default": 1 } }, "root": { "node": "selector", ... } }
//
// The JSON Schema of the format is apps/backend/behavior/tree.schema.json (for an editor). The loader (`load_tree_file`) checks each file:
// the format, the node names, the parameters and their ranges, the children, the declared keys of the blackboard. The compiler
// (`compile`) puts a tree and its subtrees into one flat node table (no cycle, at most kMaxNodes nodes and kMaxDepth levels). A file that
// is not correct is refused with one log line, and the tree of that name stays as it was (the embedded file of the build).
//
// The files of the repository (apps/backend/behavior/*.jsonc) are in the program: the build puts their text into a generated source file
// (`embedded_tree_files`). A development run can give a folder of files (WorldOptions::behaviorDirectory) that replace the embedded
// trees of the same name, and reload it with a development route. The tree "default" must be there.
//
// Stage BT2: the default tree is a set of subtrees (default/rest, default/fight, default/status, default/area, default/shot, default/cast,
// default/melee), and the binding file (kBindingFile, world/tree_binding.h) selects the tree of each creature. A `status_action` and an
// `area_attack` can name the action of the slice that they use (`action`): the behavior refuses such a tree for a creature whose slice does
// not have the action (one log line), and the creature has the tree "default".
//
// Stage BT3: the boss nodes (research BT0 section 4; docs/systems/creature-behavior.md, "Behavior trees, stage BT3"): `action` (an action of
// the slice by its name), `phase` (a health threshold that fires one time in a fight, with its transition), `tag_set` and `tag_is` (the
// tags of the creature), `health_below`, `has_status`, `cooldown`, `weighted_random`, `sequence` with `memory` (a combo of separate
// actions), `once` (one time in a fight), `timer` (the time of the fight or of the phase), `target` (the target policy), the arena of
// `keep_target`, and `event` (a named event of the creature for the encounter runner). The state of these nodes is in the creature
// (CreatureBrain::states, tags, board), and the end of a fight (the reset at the anchor) gives it its first values again.

namespace soulwarden::backend::world::tree {

/** The version of the format of a tree file. */
inline constexpr int kFormat = 1;
/** The limits of a compiled tree: its nodes and the depth of its node table. */
inline constexpr std::size_t kMaxNodes = 1024;
inline constexpr std::size_t kMaxDepth = 32;
/** The limit of a blackboard: the declared keys of a compiled tree. */
inline constexpr std::size_t kMaxKeys = 64;
/** Stage BT3: the limits of the tags, the named statuses, and the named events of a compiled tree. */
inline constexpr std::size_t kMaxTags = 64;
inline constexpr std::size_t kMaxNamed = 64;
/** Stage BT3: the place of a node in CompiledTree::states for a node with no state (Node::state). */
inline constexpr std::uint16_t kNoState = 0xFFFF;
/** The largest file of a folder of trees, and of a binding file (bytes). */
inline constexpr std::size_t kMaxFileBytes = 1024 * 1024;
/** The tree of a creature with no binding, and of a creature whose slice does not fit the tree of its binding. */
inline constexpr std::string_view kDefaultTree = "default";
/** The name of the binding file in the program and in a folder (stage BT2, world/tree_binding.h): the tree loader does not read it. */
inline constexpr std::string_view kBindingFile = "bindings.jsonc";

/** The kinds of the nodes of a tree. The names of the file are in `kind_name`. */
enum class NodeKind : std::uint8_t {
    // Composites and decorators.
    selector,  ///< the children in turn until one succeeds (or runs)
    sequence,  ///< the children in turn until one fails (or runs)
    invert,    ///< one child: success and failure change places
    subtree,   ///< one child: the root of a different tree (`tree`), put into this table by the compiler
    // Conditions.
    in_fight,  ///< the creature has a target of a fight (not on its way back, not at rest)
    target_is, ///< the target is a character (`kind` "player") or a summon (`kind` "summon")
    check,     ///< a key of the blackboard compares with a value (`key`, `op`, `value`)
    // Actions: each one calls a function of the behavior (world/behavior.h).
    keep_target,    ///< the target checks: a target that is gone, the end of the leash, a target that is too far (`lose_distance`)
    status_action,  ///< a status action of the slice: its heal at low health, its buff (stage 3.4f-D)
    area_attack,    ///< an area attack of the slice (stage 3.4f-C)
    basic_shot,     ///< the basic shot of an archer or a caster (stages 3.4f-A and 3.4f-B)
    melee,          ///< the melee attacks of the slice, the way to the target, the crowd, and the wait with no way (stages 3.4b to 3.4e)
    turn_to_target, ///< the creature turns to its target
    roam,           ///< the patrol or the wander of a creature at rest (stage 3.4e; its waits are parameters from stage BT2)
    set,            ///< a key of the blackboard gets a value (`key`, `value`)
    // Stage BT3: the boss nodes.
    action,          ///< an action of the slice by its name (`action`): an attack, a status action, an area attack, or a tree action
    phase,           ///< one child (the transition): a health threshold (`health_below`) that fires one time in a fight; the key (`key`, `to`)
    tag_set,         ///< a tag of the creature gets a value (`tag`, `value`)
    tag_is,          ///< the creature has a tag (`tag`)
    health_below,    ///< the health of the creature is at `percent` of its maximum or below
    has_status,      ///< a status effect of the creature (`status`)
    cooldown,        ///< one child: it fails for `seconds` after its child succeeded
    weighted_random, ///< the children in a random order by their `weight`, until one does not fail
    once,            ///< one child: it runs until its child succeeded one time in the fight
    timer,           ///< `seconds` passed since the start of the fight or of the phase (`since`)
    target,          ///< the target policy (`policy`: nearest, random, threat) among the characters in `range`, each `every` seconds
    event,           ///< a named event of the creature for its encounter (`event`)
};
inline constexpr std::size_t kNodeKinds = static_cast<std::size_t>(NodeKind::event) + 1;

/** @return True for a node that does something in a step (the view "decided": the last one that succeeded). Stage BT3. */
[[nodiscard]] constexpr bool acts(NodeKind kind) {
    return (kind >= NodeKind::keep_target && kind <= NodeKind::roam) || kind == NodeKind::action || kind == NodeKind::tag_set
           || kind == NodeKind::target || kind == NodeKind::event;
}

/** Stage BT3: the reference time of a `timer` node. */
enum class Since : std::uint8_t { fight, phase };
/** Stage BT3: the target policies of a `target` node. */
enum class Policy : std::uint8_t { nearest, random, threat };

/** The result of a node in one step. `idle`: the step did not run the node. */
enum class Status : std::uint8_t { idle, success, failure, running };

/** The comparisons of a `check` node. */
enum class Compare : std::uint8_t { eq, ne, lt, le, gt, ge };

/** The type of a key of the blackboard. Each value is a number of the creature (a bool is 0 or 1). */
enum class KeyType : std::uint8_t { boolean, integer, real };

/** The parameters of the nodes: the place of each value in the parameters of a node (`Node::param`). Times in seconds, distances in meters. */
namespace param {
inline constexpr std::size_t kLoseDistance = 0;
inline constexpr std::size_t kTargetPlayer = 0; // target_is: 1 for "player", 0 for "summon"
// status_action and area_attack (stage BT2): the last value is the place of the named action in `CompiledTree::actions`, or -1 (none: each
// status action or area attack of the slice).
inline constexpr std::size_t kStatusRepeat = 0, kStatusPause = 1, kStatusAction = 2;
inline constexpr std::size_t kAreaRepeat = 0, kAreaOnlyRepeat = 1, kAreaHoldMargin = 2, kAreaPause = 3, kAreaAction = 4;
// basic_shot: the band of an archer (0 to 4), the band of a caster (5 to 9), the retreat pause, the pause after the shot. A band that the
// node does not have (stage BT2: the subtrees default/shot and default/cast have one band each) has kNoBand as its `far`: the node then
// fails for a slice of that band.
inline constexpr std::size_t kBandFar = 0, kBandHold = 1, kBandNear = 2, kBandMiddle = 3, kBandRepeat = 4;
inline constexpr std::size_t kArcher = 0, kCaster = 5, kRetreatPause = 10, kShotPause = 11;
inline constexpr float kNoBand = -1.0F;
inline constexpr std::size_t kLightBeforeHeavy = 0, kMeleePause = 1, kHeavyPause = 2;
// roam (stage BT2): the wait after a walk of the wander (a time from the first to the second value), and the pause after a goal with no way.
// A roam node with no such parameter (a file of stage BT1) has the values of stage 3.4e (project rules): kWanderWaitShortest 4 s,
// kWanderWaitLongest 12 s, kRoamNoWayPause 3 s. The subtree default/rest writes them.
inline constexpr std::size_t kWaitShortest = 0, kWaitLongest = 1, kNoWayPause = 2;
inline constexpr float kDefaultWaitShortest = 4.0F, kDefaultWaitLongest = 12.0F, kDefaultNoWayPause = 3.0F;
inline constexpr std::size_t kCheckKey = 0, kCheckOp = 1, kCheckValue = 2;
inline constexpr std::size_t kSetKey = 0, kSetValue = 1;
// Stage BT3. keep_target: the radius of the arena (0: no arena; the leash of the slice from the anchor). sequence: 1 with memory.
inline constexpr std::size_t kArena = 1;
inline constexpr std::size_t kMemory = 0;
// action: the pause after it (seconds), then the place of its name in CompiledTree::actions.
inline constexpr std::size_t kActionPause = 0, kActionSlot = 1;
inline constexpr float kDefaultActionPause = 1.5F;
// phase: the health part (percent), the slot of its key, the value of the key in the phase.
inline constexpr std::size_t kPhaseHealth = 0, kPhaseKey = 1, kPhaseTo = 2;
// tag_set and tag_is: the bit of the tag in CompiledTree::tags, then (tag_set) the value 0 or 1.
inline constexpr std::size_t kTagBit = 0, kTagValue = 1;
inline constexpr std::size_t kHealthPercent = 0;
// has_status: the place of its name in CompiledTree::statuses. event: the place of its name in CompiledTree::events.
inline constexpr std::size_t kStatusSlot = 0, kEventSlot = 0;
inline constexpr std::size_t kCooldownSeconds = 0;
inline constexpr std::size_t kTimerSeconds = 0, kTimerSince = 1;
// target: the policy, the range (meters), the time between two choices (seconds).
inline constexpr std::size_t kTargetRange = 0, kTargetEvery = 1, kTargetPolicy = 2;
inline constexpr float kDefaultTargetRange = 30.0F, kDefaultTargetEvery = 1.0F;
} // namespace param

/** One node of a compiled tree: its children are the nodes after it up to `end` (a pre-order table). */
struct Node {
    NodeKind kind{NodeKind::selector};
    /** The depth of the node (the root: 0). */
    std::uint8_t depth{0};
    /** One past the last node of its subtree. */
    std::uint16_t end{0};
    /** The first value of its parameters in `CompiledTree::params`. */
    std::uint32_t param{0};
    /** Stage BT3: the place of the state of the node in CompiledTree::states (cooldown, once, a sequence with memory, target), or kNoState. */
    std::uint16_t state{kNoState};
    /** Stage BT3: the weight of a child of a `weighted_random` node (0 for each other node). */
    float weight{0.0F};
    /** The name of the node for a view (the field "name" of the file, or the name of the tree of a subtree). Empty: none. */
    std::string label;
};

/** A declared key of the blackboard of a compiled tree: its slot is its place in `CompiledTree::keys`. */
struct Key {
    std::string name;
    KeyType type{KeyType::real};
    float initial{0.0F};
};

/**
 * An action of the slice that a node of a compiled tree names (stage BT2): the member `action` of a `status_action` (a status action of the
 * slice) or of an `area_attack` (an area attack of the slice). The behavior binds it to the slice of each creature.
 */
struct NamedAction {
    /** The node that names it. */
    std::uint32_t node{0};
    NodeKind kind{NodeKind::status_action};
    std::string name;
};

/** A tree with its subtrees in one flat node table: what the runtime of world/behavior.h runs. */
struct CompiledTree {
    std::string name;
    /** The file of the tree (a file name of the folder, or "embedded:NAME"). */
    std::string source;
    std::vector<Node> nodes;
    std::vector<float> params;
    std::vector<Key> keys;
    /** The actions of the slice that its nodes name (stage BT2): the parameter kStatusAction or kAreaAction of a node is a place here. */
    std::vector<NamedAction> actions;
    /**
     * Stage BT3: the tags of its `tag_set` and `tag_is` nodes (a bit of CreatureBrain::tags each), the status rows of its `has_status` nodes
     * (the behavior finds each row when it binds a creature), the events of its `event` nodes, and the number of node states.
     */
    std::vector<std::string> tags;
    std::vector<std::string> statuses;
    std::vector<std::string> events;
    std::uint16_t states{0};
    /** @return True when an `event` node of the tree sends the event (the encounter runner: a stage that waits for it). */
    [[nodiscard]] bool sends(std::string_view event) const;
    /** The first node of each kind (a development command takes its values from it), or -1. */
    std::array<std::int32_t, kNodeKinds> firstOfKind{};
    /** The cadence of the light and heavy attacks of the first melee node (the melee rule of a shot needs the next melee attack). */
    std::uint32_t lightBeforeHeavy{0};

    [[nodiscard]] const float* values(std::size_t index) const {
        return params.data() + nodes[index].param;
    }
    /** @return The parameters of the first node of a kind, or null. */
    [[nodiscard]] const float* first(NodeKind kind) const {
        const std::int32_t index = firstOfKind[static_cast<std::size_t>(kind)];
        return index < 0 ? nullptr : values(static_cast<std::size_t>(index));
    }
};

/** One tree file as the loader read it: its name, its declared keys, and its root (the JSON text of the root that the loader checked). */
struct TreeFile {
    std::string name;
    std::string source;
    std::vector<Key> keys;
    std::string root;
    /** The trees that its subtree nodes name (the load compiles the set after the last file: code review of stage BT1, finding R2). */
    std::vector<std::string> subtrees;
};

/** @return The name of a node kind in a file ("basic_shot"). */
[[nodiscard]] std::string_view kind_name(NodeKind kind);
/** @return The name of a status ("success"). */
[[nodiscard]] std::string_view status_name(Status status);
/**
 * @return The parameters of a node of a compiled tree as (name, value) pairs in the names of the file ("archer.far", "light_before_heavy";
 *         the key of a check or a set node as its name: "key", with the slot in `CompiledTree::keys`; the named action of a node as
 *         "action", with its place in `CompiledTree::actions`; a band that the node does not have is not there): the view of a
 *         development command.
 */
[[nodiscard]] std::vector<std::pair<std::string, float>> parameters(const CompiledTree& tree, std::size_t index);

/**
 * Reads and checks one tree file. @return True with `out` set, or false with `error` set (a text for one log line: the place in the file
 * and the cause).
 */
bool load_tree_file(std::string_view text, const std::string& source, TreeFile& out, std::string& error);

/** @return A compiled tree from its file and the other files (its subtrees), or null with `error`. */
[[nodiscard]] std::shared_ptr<const CompiledTree> compile(const TreeFile& file, const std::map<std::string, TreeFile, std::less<>>& files,
                                                          std::string& error);

/**
 * Reads a text file of at most `limit` bytes: it reads at most `limit` + 1 bytes (code review of stage BT1, finding R5). @return False with
 * `error` for a file that cannot be read or is larger.
 */
bool read_text_file(const std::filesystem::path& path, std::size_t limit, std::string& text, std::string& error);

/** The files of apps/backend/behavior that the build put into the program: (file name, text). */
struct EmbeddedFile {
    const char* name;
    const char* text;
};
[[nodiscard]] const std::vector<EmbeddedFile>& embedded_tree_files();

/**
 * The result of a load of a set of files: one text for each tree that was loaded, and one for each file that was refused. `bindings`: the
 * binding file that the behavior uses and its bindings (stage BT2, world/tree_binding.h).
 */
struct LoadReport {
    std::vector<std::string> loaded;
    std::vector<std::string> refused;
    std::string bindings;
};

/**
 * The trees of the world: the files by tree name and the compiled trees. World thread only (the behavior of world/behavior.h owns it). A
 * compiled tree stays valid while a creature has it (a shared pointer): a reload makes new compiled trees.
 */
class TreeSet {
public:
    /**
     * Loads the embedded files, then the `*.jsonc` files of a folder (empty: none) that replace the trees of the same name, and compiles
     * the set after the last file (a subtree can be in a later file). The binding file (kBindingFile) is not a tree: world/tree_binding.h
     * reads it. A file that is not correct (or larger than kMaxFileBytes), and a file whose tree does not compile by a fault of its own, are
     * refused (`report`); the tree of the program of that name stays. @return False when the set has no tree "default" that compiles.
     */
    bool load(const std::string& folder, LoadReport& report);
    /** Adds one file: it replaces the tree of the same name when it is correct and each tree of the set compiles with it. */
    bool add(std::string_view text, const std::string& source, std::string& error);
    /** @return The compiled tree of a name, or null. */
    [[nodiscard]] std::shared_ptr<const CompiledTree> find(std::string_view name) const;
    [[nodiscard]] std::shared_ptr<const CompiledTree> default_tree() const {
        return find(kDefaultTree);
    }
    [[nodiscard]] const std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>>& trees() const {
        return m_compiled;
    }

private:
    /** Compiles each tree of `files`. @return False with `error` when one does not compile. */
    static bool compile_all(const std::map<std::string, TreeFile, std::less<>>& files,
                            std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>>& out, std::string& error);

    std::map<std::string, TreeFile, std::less<>> m_files;
    std::map<std::string, std::shared_ptr<const CompiledTree>, std::less<>> m_compiled;
};

} // namespace soulwarden::backend::world::tree
