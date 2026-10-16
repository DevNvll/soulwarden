#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "state/store.h"

// The interaction objects of the world (stage 2.5a, docs/systems/interactions.md, protocol notes 4.35): each object of the
// level data that a player can use with the key of an interaction, with its id on the wire, its place, its Interact
// components ("bounds"), and its options with their rows of the user interface database of the game.
//
// The data is game data: the tool tools/gamedata/interactions.sh makes the file from the game copy of the user, and the file
// stays outside the repository. Without the file the backend starts with an empty catalog: it refuses each request of an
// interaction (3743 result 2), and the lock of a conversation (6705/6709) works as before.
//
// Two indexes, so that the cost of a request and of a lookup near a character does not depend on the size of the world
// (roadmap, rule MP-8): the exact reference of the client (G and the facet target of a bound) to its bound, and a grid of
// cells of 64 m with the bounds of each cell.

namespace soulwarden::backend::gamedata {

/** File name of the interaction objects in the game data directory of the backend. */
inline constexpr const char* kInteractionDataFile = "interaction-data.json";

/** The kind of an object (a table of the tool from the option names of the data; research interaction-stage.md, Q4). */
enum class InteractionKind : std::uint8_t {
    lorePage,
    npc,
    storage,
    tradingPost,
    shrine,
    craftingStation,
    camp,
    lootDrop,
    gathering,
    door,
    restFurniture,
    other,
};

[[nodiscard]] const char* interaction_kind_text(InteractionKind kind);
/** @return The option id on the wire: the CRC-32 (IEEE 802.3) of the lower-case option name (client hash 0x12F4730). */
[[nodiscard]] std::uint32_t interaction_option_id(std::string_view name);
/** @return The kind of a text of the file ("lorePage"), or no value. */
[[nodiscard]] std::optional<InteractionKind> interaction_kind_of(std::string_view text);

/** One option row of the user interface database (sharedassets/genericassets/ui/uidatabase.uidb). */
struct InteractionOption {
    std::string name;
    /** The option id on the wire: the CRC-32 of the lower-case name (client hash 0x12F4730). */
    std::uint32_t id{0};
    /** Interact Action Type: the action of the request (47 OpenRead, 50 AddLoreToJournal, 63 OpenConversation). */
    std::uint8_t action{0};
    /** Is Committed Interaction: the client keeps the interaction until its end (a window, a conversation). */
    bool committed{false};
    /** Delay Time and Hold action press time (seconds). */
    float delay{0.0F};
    float hold{0.0F};
    bool instantCancel{false};
    std::uint32_t cooldown{0};
    /** Committed Interaction Max Usage Timeout and Inactive Timeout (seconds; 0: none). */
    float maxUsage{0.0F};
    float inactive{0.0F};
    std::uint32_t requiredLevel{0};
    std::string requiredItem;
    std::uint32_t requiredItemCount{0};
    std::uint32_t requiredCurrency{0};
    /** An index into InteractionData::achievements. -1: none. */
    std::int32_t requiredAchievement{-1};
    /** A required achievement with no row of the game data: the backend refuses the option. Empty: none. */
    std::string unresolved;
    /** The option has status rules (required, excluded, added, or removed status effects), a faction rule, or a PVP rule. */
    bool statusRules{false};
};

/** One achievement that an object names: its id and its bit in the achievement state of a character (AchievementIndex). */
struct InteractionAchievement {
    std::string id;
    std::uint32_t index{0};
};

/** One option of a bound: the option row and the instance id of the entity of the option (the option entity of a request). */
struct BoundOption {
    std::uint32_t option{0};
    std::uint64_t entity{0};
};

/** One Interact component of an object (a "bound": the client shows its prompt). */
struct InteractionBound {
    /** The slot of its state 2930 in a record of the object (m_replicationIndex). */
    std::uint32_t slot{0};
    std::uint64_t component{0};
    /** The facet target of the component: the target of the reference of a request. */
    std::uint64_t target{0};
    /** The place of its entity in the world (the range check, research K4). */
    state::WorldPosition position;
    /** m_interactRangeSq (square meters): the check of the 3D distance (client code 0x35C2E60: greater is refused). */
    float rangeSq{0.0F};
    /** m_numberOfInteractorsAllowed. -1: no limit. */
    std::int32_t capacity{-1};
    bool lineOfSight{false};
    bool arcRequired{false};
    float arc{0.0F};
    std::vector<BoundOption> options;
    /** The achievement of a lore page (an index into InteractionData::achievements). -1: not a lore page, or no row. */
    std::int32_t lore{-1};
    /** The requirements of the component: an achievement (an index; -1: none), a progression id and rank. */
    std::int32_t requiredAchievement{-1};
    std::string progression;
    std::uint32_t progressionRank{0};
    bool onlyOnQuest{false};
    bool condition{false};
    /** Names of the data with no row (a required achievement, a lore id): the backend refuses each use of the bound. */
    std::vector<std::string> unresolved;
    /** m_interactTagString: the tag of a quest task. Not a requirement. */
    std::string tag;
};

/** One object: a placement of a layer whose slice has Interact components. */
struct InteractionObject {
    /** The placement UUID: the durable key of the object. */
    std::string key;
    InteractionKind kind{InteractionKind::other};
    std::string slice;
    /** The slice asset id and its sub id: the metadata state of the record of the object. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t sub{0};
    /** The managed actor id of the placement, and G (its first 8 bytes, little-endian). */
    std::array<std::uint8_t, 16> actor{};
    std::uint64_t group{0};
    std::string region;
    state::WorldPosition position;
    std::vector<InteractionBound> bounds;
};

/** A bound of the catalog: an object and the place of the bound in it. */
struct BoundRef {
    std::uint32_t object{0};
    std::uint32_t bound{0};

    bool operator==(const BoundRef&) const = default;
};

/** The work of one lookup near a place (InteractionData::for_each_near; case IT-LOAD). */
struct LookupWork {
    std::uint64_t cells{0};
    std::uint64_t bounds{0};
};

class InteractionData {
public:
    /** Edge of a cell of the grid (meters). */
    static constexpr float kCellSize = 64.0F;

    /**
     * Reads the file of the tool. A file that is not complete is not used, as a whole: a member with a wrong form, an index
     * of an option or of an achievement that the lists do not have, two bounds with one reference, a position that is not a
     * position of the world, and a member "counts" whose numbers are not the sizes of the lists.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /** Sets the data with no file (tests), with the same checks. @return False when a part is not correct: no object then. */
    [[nodiscard]] bool set(std::vector<InteractionOption> options,
                           std::vector<InteractionAchievement> achievements,
                           std::vector<InteractionObject> objects);

    [[nodiscard]] bool loaded() const {
        return !m_objects.empty();
    }
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<InteractionObject>& objects() const {
        return m_objects;
    }
    [[nodiscard]] const std::vector<InteractionOption>& options() const {
        return m_options;
    }
    [[nodiscard]] const std::vector<InteractionAchievement>& achievements() const {
        return m_achievements;
    }
    [[nodiscard]] std::size_t bound_count() const {
        return m_references.size();
    }

    /** @return The bound of a reference of the client (G and the facet target), or no value. */
    [[nodiscard]] std::optional<BoundRef> find(std::uint64_t group, std::uint64_t target) const;
    /** @return The object with this key (the placement UUID, any case), or null. */
    [[nodiscard]] const InteractionObject* find_key(std::string_view key) const;
    /** @return The index of the object with this key, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> index_of(std::string_view key) const;
    /** @return The index of the object of a group id (G of its actor), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> index_of_group(std::uint64_t group) const;
    /** @return The bit of an achievement id (any case) of the list, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> achievement_index(std::string_view id) const;

    /**
     * Calls `visit(BoundRef)` for each bound whose position is `range` or less from `center` (horizontal), once for each
     * bound. It looks only at the cells near the center. @param work When not null: gets the work of the lookup.
     */
    template <class Visit>
    void for_each_near(const state::WorldPosition& center, float range, Visit&& visit, LookupWork* work = nullptr) const;

private:
    [[nodiscard]] static std::int64_t cell_key(std::int32_t x, std::int32_t y) {
        return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
    }

    std::vector<InteractionOption> m_options;
    std::vector<InteractionAchievement> m_achievements;
    std::vector<InteractionObject> m_objects;
    std::unordered_map<std::string, std::uint32_t> m_byKey;
    std::unordered_map<std::uint64_t, std::uint32_t> m_byGroup;
    std::unordered_map<std::string, std::uint32_t> m_achievementByLowerId;
    /** (G, target) to the bound: the key is G XOR the target turned by 17 bits, then a check of both values. */
    std::unordered_multimap<std::uint64_t, BoundRef> m_references;
    std::unordered_map<std::int64_t, std::vector<BoundRef>> m_cells;
    std::string m_error;
};

template <class Visit>
void InteractionData::for_each_near(const state::WorldPosition& center, float range, Visit&& visit, LookupWork* work) const {
    if (m_cells.empty() || !(range >= 0.0F)) {
        return;
    }
    const auto low = [](float value) { return static_cast<std::int32_t>(std::floor(value / kCellSize)); };
    const std::int32_t x0 = low(center.x - range);
    const std::int32_t x1 = low(center.x + range);
    const std::int32_t y0 = low(center.y - range);
    const std::int32_t y1 = low(center.y + range);
    const float rangeSq = range * range;
    for (std::int32_t x = x0; x <= x1; ++x) {
        for (std::int32_t y = y0; y <= y1; ++y) {
            if (work != nullptr) {
                ++work->cells;
            }
            const auto found = m_cells.find(cell_key(x, y));
            if (found == m_cells.end()) {
                continue;
            }
            for (const BoundRef& ref : found->second) {
                if (work != nullptr) {
                    ++work->bounds;
                }
                const state::WorldPosition& place = m_objects[ref.object].bounds[ref.bound].position;
                const float dx = place.x - center.x;
                const float dy = place.y - center.y;
                if (dx * dx + dy * dy <= rangeSq) {
                    visit(ref);
                }
            }
        }
    }
}

} // namespace soulwarden::backend::gamedata
