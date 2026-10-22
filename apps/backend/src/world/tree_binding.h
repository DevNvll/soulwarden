#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "world/behavior_tree.h"

namespace soulwarden::backend::gamedata {
struct CreatureKind;
struct CreatureSlice;
} // namespace soulwarden::backend::gamedata

// The binding of a behavior tree to a creature (stage BT2, docs/systems/creature-behavior.md, section "Behavior trees").
//
// The binding file (tree::kBindingFile: apps/backend/behavior/bindings.jsonc in the program, or the file of the same name in the folder of a
// development run) selects the tree of each creature by its kind and its slice. JSON with comments, as a tree file:
//
//   { "format": 1, "bindings": [ { "family": "Wolf", "tree": "wolf" }, { "kind": ["Wolf_Grey_7", "Wolf_Grey_9"], "tree": "wolf/alpha" },
//                                { "tree": "default" } ] }
//
// A binding has one key and a tree, or only a tree (the binding of each other creature: at most one). The keys (a text, or a list of texts),
// from the narrowest to the widest: `kind` (the id of a kind of the creature data), `vitals` (its vitals row), `slice` (the path of its
// slice), `serverTree` (a server tree that its slice names in the client data, gamedata::CreatureSlice::serverTrees), `creatureType`
// (EliteMiniBoss), `family` (Wolf). The selection of a creature takes the binding of the narrowest key that matches; the place of a binding
// in the file does not change the selection. A text matches with no difference of upper and lower case. A value in two bindings of one key
// is an error of the file (a tie). For a slice with two server trees the first one of the slice that has a binding wins. A creature with no
// binding that matches has the binding with no key, else the tree "default". These are rules of this project (stage BT2): a binding does
// not depend on the order of the lines of the file, and a narrow binding (one kind) wins over a wide one (a family) in each file.
//
// The loader checks the form, each key and value, the tie, and the tree of each binding (it must be a tree of the set). A file that is not
// correct is refused with one log line, and the bindings stay as they were (the program file, or none: each creature has the tree default).
// A binding selects a tree; the behavior then checks that the slice of the creature has each action that the tree names (world/behavior.h).

namespace soulwarden::backend::world::tree {

/** The keys of a binding, from the narrowest to the widest: the order of the selection. `none`: the binding of each other creature. */
enum class BindingKey : std::uint8_t { kind, vitals, slice, serverTree, creatureType, family, none };
inline constexpr std::size_t kBindingKeys = static_cast<std::size_t>(BindingKey::none);
/** The version of the format of the binding file. */
inline constexpr int kBindingFormat = 1;
/** The limits of a binding file (rules of this project): the bindings, and the values of one binding. */
inline constexpr std::size_t kMaxBindings = 4096;
inline constexpr std::size_t kMaxBindingValues = 256;

/** @return The name of a key in the file ("creatureType"). */
[[nodiscard]] std::string_view key_name(BindingKey key);

/** One binding of the file. */
struct Binding {
    BindingKey key{BindingKey::none};
    /** The values of its key, as the file has them. */
    std::vector<std::string> values;
    std::string tree;
    /** Its place in the file (0: the first binding). */
    std::size_t place{0};
};

/** The binding that selected the tree of a creature: the binding (null: no binding, the tree default) and the value that matched. */
struct Selection {
    const Binding* binding{nullptr};
    std::string_view value;

    [[nodiscard]] std::string_view tree() const {
        return binding != nullptr ? std::string_view(binding->tree) : kDefaultTree;
    }
    /** @return A text for a view and a log line: "family Wolf (binding 3)", "each creature (binding 1)", "no binding". */
    [[nodiscard]] std::string text() const;
};

/** The bindings of the world: the behavior of world/behavior.h owns them (world thread). */
class BindingSet {
public:
    /**
     * Reads and checks a binding file; the tree of each binding must be a tree of `trees`. @return True with `out` set, or false with
     * `error` (a text for one log line: the place in the file and the cause).
     */
    static bool parse(std::string_view text, const std::string& source, const TreeSet& trees, BindingSet& out, std::string& error);
    /** @return The binding of a creature with this kind and this slice: one look-up for each key (the make of a creature, a reload). */
    [[nodiscard]] Selection select(const gamedata::CreatureKind& kind, const gamedata::CreatureSlice& slice) const;
    [[nodiscard]] const std::vector<Binding>& bindings() const {
        return m_bindings;
    }
    /** The file of the bindings ("embedded:bindings.jsonc", "bindings.jsonc"). Empty: no file (each creature has the tree default). */
    [[nodiscard]] const std::string& source() const {
        return m_source;
    }

private:
    /** @return The binding of a value of a key (lower case), or null. */
    [[nodiscard]] const Binding* find(BindingKey key, std::string_view value, std::string& lower) const;

    std::vector<Binding> m_bindings;
    /** For each key: the lower-case value and the place of its binding. */
    std::array<std::unordered_map<std::string, std::size_t>, kBindingKeys> m_index;
    std::optional<std::size_t> m_fallback;
    std::string m_source;
};

} // namespace soulwarden::backend::world::tree
