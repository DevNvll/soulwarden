#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// Ids of the faceted components of the player slice (slices/player.dynamicslice). With them
// the backend computes the facet target of each component of an entity, and it checks the
// target of each component call of a client (protocol notes 4.2).
//
//   K               = id of the root entity of the slice XOR entity group id
//   instance entity = entity id in the slice file XOR K
//   target          = (instance entity + component id) mod 2^64
//
// The ids are game data: the tool tools/gamedata/slice_ids.sh makes the file from the game
// copy of the user, and the file stays outside the repository. Without the file the backend
// does not check a target.

namespace soulwarden::backend::gamedata {

/** File name of the table in the game data directory of the backend. */
inline constexpr const char* kSliceIdsFile = "slice-ids.json";

class SliceIds {
public:
    struct Component {
        /** Id of the entity of the component in the slice file. */
        std::uint64_t entity{0};
        std::uint64_t component{0};
        /** Name of the type of the component, when the tool knows it. For a log line only. */
        std::string name;
    };

    /** Reads the file of the tool. @return False when the file is not there or has a different form. */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the table with no file (tests). */
    void set(std::uint64_t root, std::vector<Component> components) {
        m_root = root;
        m_components = std::move(components);
    }

    [[nodiscard]] bool loaded() const {
        return !m_components.empty();
    }
    [[nodiscard]] std::size_t component_count() const {
        return m_components.size();
    }

    /** @return The facet target of one component for an entity group id. */
    [[nodiscard]] std::uint64_t target(std::uint64_t group, const Component& component) const {
        return (component.entity ^ m_root ^ group) + component.component;
    }

    /**
     * @return The component of the slice whose target for this entity group id is `target`, or
     *         null when the slice has no such component.
     */
    [[nodiscard]] const Component* find(std::uint64_t group, std::uint64_t target) const;
    /** @return The component with this name of the tool ("ENTITY/CLASS-UUID"), or null (stage 3.5b.2: the selected Container). */
    [[nodiscard]] const Component* find_named(std::string_view name) const;

private:
    std::uint64_t m_root{0};
    std::vector<Component> m_components;
};

} // namespace soulwarden::backend::gamedata
