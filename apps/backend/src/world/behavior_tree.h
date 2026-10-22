#pragma once

#include <array>
#include <cstdint>
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

namespace soulwarden::backend::world::tree {

/** The version of the format of a tree file. */
inline constexpr int kFormat = 1;
/** The limits of a compiled tree: its nodes and the depth of its node table. */
inline constexpr std::size_t kMaxNodes = 1024;
inline constexpr std::size_t kMaxDepth = 32;
/** The limit of a blackboard: the declared keys of a compiled tree. */
inline constexpr std::size_t kMaxKeys = 64;
/** The name of the tree of each creature in stage BT1. */
inline constexpr std::string_view kDefaultTree = "default";

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
    roam,           ///< the patrol or the wander of a creature at rest (stage 3.4e)
    set,            ///< a key of the blackboard gets a value (`key`, `value`)
};
inline constexpr std::size_t kNodeKinds = static_cast<std::size_t>(NodeKind::set) + 1;

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
inline constexpr std::size_t kStatusRepeat = 0, kStatusPause = 1;
inline constexpr std::size_t kAreaRepeat = 0, kAreaOnlyRepeat = 1, kAreaHoldMargin = 2, kAreaPause = 3;
// basic_shot: the band of an archer (0 to 4), the band of a caster (5 to 9), the retreat pause, the pause after the shot.
inline constexpr std::size_t kBandFar = 0, kBandHold = 1, kBandNear = 2, kBandMiddle = 3, kBandRepeat = 4;
inline constexpr std::size_t kArcher = 0, kCaster = 5, kRetreatPause = 10, kShotPause = 11;
inline constexpr std::size_t kLightBeforeHeavy = 0, kMeleePause = 1, kHeavyPause = 2;
inline constexpr std::size_t kCheckKey = 0, kCheckOp = 1, kCheckValue = 2;
inline constexpr std::size_t kSetKey = 0, kSetValue = 1;
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
    /** The name of the node for a view (the field "name" of the file, or the name of the tree of a subtree). Empty: none. */
    std::string label;
};

/** A declared key of the blackboard of a compiled tree: its slot is its place in `CompiledTree::keys`. */
struct Key {
    std::string name;
    KeyType type{KeyType::real};
    float initial{0.0F};
};

/** A tree with its subtrees in one flat node table: what the runtime of world/behavior.h runs. */
struct CompiledTree {
    std::string name;
    /** The file of the tree (a file name of the folder, or "embedded:NAME"). */
    std::string source;
    std::vector<Node> nodes;
    std::vector<float> params;
    std::vector<Key> keys;
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
};

/** @return The name of a node kind in a file ("basic_shot"). */
[[nodiscard]] std::string_view kind_name(NodeKind kind);
/** @return The name of a status ("success"). */
[[nodiscard]] std::string_view status_name(Status status);
/**
 * @return The parameters of a node of a compiled tree as (name, value) pairs in the names of the file ("archer.far", "light_before_heavy";
 *         the key of a check or a set node as its name: "key", with the slot in `CompiledTree::keys`): the view of a development command.
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

/** The files of apps/backend/behavior that the build put into the program: (file name, text). */
struct EmbeddedFile {
    const char* name;
    const char* text;
};
[[nodiscard]] const std::vector<EmbeddedFile>& embedded_tree_files();

/** The result of a load of a set of files: one text for each tree that was loaded, and one for each file that was refused. */
struct LoadReport {
    std::vector<std::string> loaded;
    std::vector<std::string> refused;
};

/**
 * The trees of the world: the files by tree name and the compiled trees. World thread only (the behavior of world/behavior.h owns it). A
 * compiled tree stays valid while a creature has it (a shared pointer): a reload makes new compiled trees.
 */
class TreeSet {
public:
    /**
     * Loads the embedded files, then the `*.jsonc` files of a folder (empty: none) that replace the trees of the same name, and compiles
     * each tree. A file that is not correct, and a file whose tree (or a tree that uses it) does not compile, are refused (`report`); the
     * earlier tree of that name stays. @return False when the set has no tree "default" that compiles.
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
