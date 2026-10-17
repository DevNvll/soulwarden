#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <map>
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
//
// Stage 2.5b (format 2): the server children of the placements (the stations of a settlement, the camps), the templates of their
// slices, and the development fixtures (the powder keg of the timed option Trigger, the camps). The client shows a server child
// only after a record of the server (live check L4 of stage 2.5b). The ids of a child come from its actor of the id allocator:
// a template has the ids of its slice. A third grid of 64 m has the server children (the spawn rule of world/interactions.h).

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

/**
 * The Assembly component of a station or a camp (stage 2.5b, research K9; client 0x2E61E00 copies the fee flag and looks up the key
 * at 0x2E6A440: live check L4). The authored key is a default: BuildableController state 1594 field 6 can override it by a CRC, and
 * the server source of that override is not known (research Q1). The backend keeps the authored key.
 */
struct InteractionAssembly {
    /** m_craftingStationReference.m_craftingStationEntry ("Blacksmith5", "Camp5"). */
    std::string key;
    /** m_chargeCraftingFee. */
    bool fee{false};
    /** The entity and the component of the Assembly (a template: the ids of the slice; a level object: the instance id). */
    std::uint64_t entity{0};
    std::uint64_t component{0};
    /** True: the key is a row of craftingstations.craftstationdb (Carpentry2 and Smelting2 of some settlements are not). */
    bool resolved{false};
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
    /** A bound of a template (stage 2.5b): the entity of the Interact component in the slice. 0 for a level object. */
    std::uint64_t entity{0};
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
    /** The Assembly of the slice (a station or a camp; stage 2.5b). No value: none. */
    std::optional<InteractionAssembly> assembly;
};

/** A unit quaternion (x, y, z, w): the rotation of a server child. */
using Rotation = std::array<float, 4>;
inline constexpr Rotation kNoRotation{0.0F, 0.0F, 0.0F, 1.0F};

/** A server child of a template: the slice that the server makes with it (the forge of a station: live check L4 of stage 2.5b). */
struct TemplateChild {
    std::uint32_t templateIndex{0};
    /** The place and the rotation of the child in the frame of the template (the m_worldTM of its spawner entity). */
    state::WorldPosition position;
    Rotation rotation{kNoRotation};
};

/**
 * The slice of a server child or of a fixture (stage 2.5b): what the backend needs for its record and its use. The bounds have
 * the positions in the frame of the slice and the ids of the slice: the backend makes the instance ids and the facet targets
 * from G of the actor of the child (protocol notes 4.2, 4.35: K = root XOR G).
 */
struct InteractionTemplate {
    std::string slice;
    InteractionKind kind{InteractionKind::other};
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t sub{2};
    /** The root entity of the slice (the XOR rule of the ids). */
    std::uint64_t root{0};
    /** The slots of the replicated components of the slice (m_replicationIndex; 0: the slice has none). */
    std::uint32_t positionSlot{0};
    std::uint32_t buildableSlot{0};
    std::uint32_t ownershipSlot{0};
    std::uint32_t vitalsSlot{0};
    /** m_initialState of the BuildableController (5: Complete) and m_initialPermissionRule of the Ownership. -1: none. */
    std::int32_t buildableState{-1};
    std::int32_t permissionRule{-1};
    std::optional<InteractionAssembly> assembly;
    std::vector<InteractionBound> bounds;
    std::vector<TemplateChild> children;
};

/**
 * A loot container that the server makes (stage 3.5b.2, docs/systems/loot-bags.md; `lootContainers` of format 3): the personal bag of a
 * kill (AILoot) and of a discard. The values come from its slice (tools/gamedata/interactions.py; research chests-ground-loot.md Q3, K7).
 */
struct LootContainer {
    /** The template of the slice (the record and the ids of a server child). */
    std::uint32_t templateIndex{0};
    /** m_instancedLootType of its InstancedLoot component: 1 (AILoot). */
    std::uint32_t kind{1};
    /** The slot of its LootDrop component (the LootDrop state, type 2027). */
    std::uint32_t lootDropSlot{0};
    /** The lifetime (s): the DelayedEvent that starts at the init and ends the slice (KillSelfDelay, 180 s in the supported build). */
    float lifetime{0.0F};
    /** The end after the last item (s): the DelayedEvent of m_completelyLootedEvents (FastKillSelfDelay, 1.25 s). */
    float emptyLifetime{0.0F};
    /** The hide of the owner after Looted (s): the DelayedEvent of m_lootedEvents (HideMeshDelay, 1.5 s); the client runs it. */
    float hideDelay{0.0F};
    /** The number of the rarity particles of the slice (m_rarityParticleEntityRef, 5): the selector of a LootTracker entry is below it. */
    std::uint32_t rarityParticles{0};
};

/** A server child of a placement (stage 2.5b): a station of a settlement, a camp of a permanent camp. */
struct SpawnedPlacement {
    /** The durable key: the placement UUID and the entity id of the spawner ("UUID/ENTITY"). */
    std::string key;
    /** The placement (the settlement), its managed actor id, and its slice. */
    std::string parent;
    std::array<std::uint8_t, 16> parentActor{};
    std::string parentSlice;
    /** The name of the spawner entity ("Public_Blacksmith"). */
    std::string spawner;
    std::uint32_t templateIndex{0};
    bool spawnOnInitialize{false};
    std::string region;
    state::WorldPosition position;
    Rotation rotation{kNoRotation};
    /**
     * Decision D1 of stage 3.7a (a decision of the orchestrator; tools/gamedata/interactions.py): a station whose authored key ends in 2 and
     * has no row of the station database gets the _t2 variant of its family (key ending in 3) as `templateIndex`. The authored template, the
     * two keys, and the slice of the variant. No value: no override (also each file of before stage 3.7a).
     */
    std::optional<std::uint32_t> authoredTemplate;
    std::string overrideFrom;
    std::string overrideTo;
    std::string overrideSlice;
};

/** A bound of the catalog: an object and the place of the bound in it. */
struct BoundRef {
    std::uint32_t object{0};
    std::uint32_t bound{0};

    bool operator==(const BoundRef&) const = default;
};

/** @return The point `local` of a frame with this place and rotation, in the world (q * local * q^-1 + place). */
[[nodiscard]] state::WorldPosition transform_point(const state::WorldPosition& place, const Rotation& rotation,
                                                   const state::WorldPosition& local);
/** @return The rotation a * b (the frame b in the frame a). */
[[nodiscard]] Rotation compose_rotation(const Rotation& a, const Rotation& b);

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
                           std::vector<InteractionObject> objects,
                           std::vector<InteractionTemplate> templates = {},
                           std::vector<SpawnedPlacement> spawned = {},
                           std::map<std::string, std::uint32_t> fixtures = {});

    [[nodiscard]] bool loaded() const {
        return !m_objects.empty() || !m_templates.empty();
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
    /** The templates of the server children and of the fixtures, the server children of the placements (stage 2.5b). */
    [[nodiscard]] const std::vector<InteractionTemplate>& templates() const {
        return m_templates;
    }
    [[nodiscard]] const std::vector<SpawnedPlacement>& spawned() const {
        return m_spawned;
    }
    [[nodiscard]] const std::map<std::string, std::uint32_t>& fixtures() const {
        return m_fixtures;
    }
    /** @return The template of a fixture name ("powderKeg"), or no value. */
    [[nodiscard]] std::optional<std::uint32_t> fixture(std::string_view name) const;
    /**
     * Sets the loot containers (stage 3.5b.2; the loader calls it for a file of format 3, a test with no file). Each one must name a
     * template with a bound, have the type AILoot, a LootDrop slot, a lifetime above 0, and a finite end time not below 0.
     * @return False (no change) when one is not correct.
     */
    [[nodiscard]] bool set_loot_containers(std::map<std::string, LootContainer> containers);
    /** @return The loot container of a name ("personal"), or null (a file of format 1 or 2: no loot bag). */
    [[nodiscard]] const LootContainer* loot_container(std::string_view name) const;
    [[nodiscard]] const std::map<std::string, LootContainer>& loot_containers() const {
        return m_lootContainers;
    }
    /** @return The largest horizontal distance of a bound or of a child from the origin of a template (meters). */
    [[nodiscard]] float template_spread(std::uint32_t templateIndex) const;

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

    /**
     * Calls `visit(std::uint32_t index)` for each server child (an index into spawned()) whose place is `range` or less from
     * `center` (horizontal). It looks only at the cells near the center. @param work When not null: gets the work.
     */
    template <class Visit>
    void for_each_spawned_near(const state::WorldPosition& center, float range, Visit&& visit, LookupWork* work = nullptr) const;

    /** @return The cell key of a place (the grids of 64 m; a caller with its own grid of the same cells). */
    [[nodiscard]] static std::int64_t cell_of(const state::WorldPosition& place) {
        const auto cell = [](float value) { return static_cast<std::int32_t>(std::floor(value / kCellSize)); };
        return cell_key(cell(place.x), cell(place.y));
    }
    [[nodiscard]] static std::int64_t cell_key(std::int32_t x, std::int32_t y) {
        return (static_cast<std::int64_t>(x) << 32) ^ static_cast<std::uint32_t>(y);
    }

private:
    std::vector<InteractionOption> m_options;
    std::vector<InteractionAchievement> m_achievements;
    std::vector<InteractionObject> m_objects;
    std::unordered_map<std::string, std::uint32_t> m_byKey;
    std::unordered_map<std::uint64_t, std::uint32_t> m_byGroup;
    std::unordered_map<std::string, std::uint32_t> m_achievementByLowerId;
    /** (G, target) to the bound: the key is G XOR the target turned by 17 bits, then a check of both values. */
    std::unordered_multimap<std::uint64_t, BoundRef> m_references;
    std::unordered_map<std::int64_t, std::vector<BoundRef>> m_cells;
    std::vector<InteractionTemplate> m_templates;
    std::vector<SpawnedPlacement> m_spawned;
    std::map<std::string, std::uint32_t> m_fixtures;
    std::map<std::string, LootContainer> m_lootContainers;
    std::vector<float> m_templateSpread;
    std::unordered_map<std::int64_t, std::vector<std::uint32_t>> m_spawnedCells;
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

template <class Visit>
void InteractionData::for_each_spawned_near(const state::WorldPosition& center, float range, Visit&& visit, LookupWork* work) const {
    if (m_spawnedCells.empty() || !(range >= 0.0F)) {
        return;
    }
    const auto low = [](float value) { return static_cast<std::int32_t>(std::floor(value / kCellSize)); };
    const float rangeSq = range * range;
    for (std::int32_t x = low(center.x - range); x <= low(center.x + range); ++x) {
        for (std::int32_t y = low(center.y - range); y <= low(center.y + range); ++y) {
            if (work != nullptr) {
                ++work->cells;
            }
            const auto found = m_spawnedCells.find(cell_key(x, y));
            if (found == m_spawnedCells.end()) {
                continue;
            }
            for (const std::uint32_t index : found->second) {
                if (work != nullptr) {
                    ++work->bounds;
                }
                const state::WorldPosition& place = m_spawned[index].position;
                const float dx = place.x - center.x;
                const float dy = place.y - center.y;
                if (dx * dx + dy * dy <= rangeSq) {
                    visit(index);
                }
            }
        }
    }
}

} // namespace soulwarden::backend::gamedata
