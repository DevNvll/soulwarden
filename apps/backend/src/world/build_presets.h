#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "world/entity.h"
#include "world/inventory.h"
#include "world/item.h"
#include "world/skills.h"

// The build presets (stage B0, the bow build of the user): one command gives a character a whole build. A build is the equipment
// with the gear score and the perks of each item, the consumables of the bag and of the quick slots, the level, the value of each
// attribute, the weapon masteries, the nodes of the trees, and the ability bars. A preset has variants (the two trees of the bow:
// PvE and PvP).
//
// A preset is project data in the repository (project_presets): game ids and numbers only, no account, no character, and no Steam
// id. At the start of the backend each preset gets the check of its ids and numbers against the game data (check): a preset with an
// id that the data does not have, a gear score outside the range of its item, or a tree that the rules refuse, is disabled with one
// log line. The backend starts and works with no game data (each preset is disabled).
//
// The apply is one task of the world (apply):
// 1. The plan of each system on a copy, with the same checks as the single commands: the items (Inventory::plan_build: the give and
//    the equip; the old item of each slot goes into the bag), the level and the attributes (Progress::plan_sheet: set_level and
//    set_attribute), and the trees (Skills::plan_trees: set_rank, the reset of the tree, assign, set_bar). No write.
// 2. All rows of the plans in one SQLite transaction (state::Store::Batch), with the achievement of the objective of each artifact
//    of the build (the client shows the perks 2 to 5 of an artifact only with it: live check U1 of stage B0).
// 3. The entity and the marks of the component states (the finish of each plan), and the refresh of the gear.
// A refused preset (an unknown preset or variant, a disabled preset, a bag with no room for the old items, a level above the cap, a
// refused node) changes nothing. A character that is not in the world gets the rows only.
//
// World thread only, but for project_presets.

namespace soulwarden::backend::world {

class World;

/** One item of a preset: an item of a slot of the paperdoll, or a stack of the bag. */
struct PresetItem {
    /** The ItemID text of the item catalog. */
    std::string itemId;
    std::uint32_t quantity{1};
    /** The gear score of the new instance. 0: the value of the item table. */
    std::uint32_t gearScore{0};
    /** The perks of the five places (the stored ids, after the swap of the game; an empty text: no perk). */
    PerkIds perks{};
    /** The slot of the paperdoll. No value: the bag. */
    std::optional<std::uint32_t> slot;
};

/** One variant of a preset: the trees (the bow tree of PvE or of PvP). */
struct PresetVariant {
    /** The name of a command ("pve"). */
    std::string name;
    /** The text of the tool window ("PvE tree"). */
    std::string label;
    std::vector<TreeBuild> trees;
};

struct BuildPreset {
    /** The name of a command ("bow-boltcaster"). */
    std::string name;
    /** The text of the tool window. */
    std::string title;
    /** The level of the screen. */
    std::uint32_t level{1};
    /** The value of each attribute (index world::attribute::k*: constitution, focus, strength, dexterity, intelligence). */
    std::array<std::uint32_t, attribute::kCount> attributes{};
    /** The equipment and the bag, in this sequence. */
    std::vector<PresetItem> items;
    /** The first variant is the default of a command with no variant. */
    std::vector<PresetVariant> variants;
};

/** @return The build presets of the project (project data). Any thread. */
[[nodiscard]] std::vector<BuildPreset> project_presets();

enum class PresetError {
    none,
    /** No preset with this name. */
    unknownPreset,
    /** The preset has no variant with this name. */
    unknownVariant,
    /** The check of the start disabled the preset: an id or a number that the game data does not have (the cause has the text). */
    disabled,
    /** The store has no character with this id. */
    unknownCharacter,
    /** The inventory system refused the items (the cause has its text: a bag with no room, ...). */
    items,
    /** The progress system refused the level or the attributes (a level above the cap, ...). */
    progress,
    /** The skill system refused a tree (a node that its rules refuse, ...). */
    skills,
    /** The database did not take the change: nothing changed. */
    storeFailed,
};

/** @return A short text of the error. */
[[nodiscard]] const char* preset_error_text(PresetError error);

struct PresetResult {
    PresetError error{PresetError::none};
    /** The text of the cause of a refusal. */
    std::string cause;
    /** True: the character is in the world, and its session gets the change. False: only the rows changed. */
    bool inWorld{false};
    /** The changes, one text each (the reply of the command). */
    std::vector<std::string> changes;

    [[nodiscard]] bool ok() const {
        return error == PresetError::none;
    }
};

/** A preset with the result of its check. */
struct PresetStatus {
    BuildPreset preset;
    bool enabled{false};
    /** The cause of a disabled preset. */
    std::string cause;
};

class BuildPresets {
public:
    BuildPresets(World& world, std::vector<BuildPreset> presets);

    /** Sets the presets with no check (a test). The next use checks them. */
    void set(std::vector<BuildPreset> presets);
    /**
     * Checks each id and number of each preset against the game data (the item catalog, the perk data, the progression tables) with
     * the rules of the systems on a character with nothing: one log line for each disabled preset, and one line with the enabled
     * ones. The start of the backend calls it; a use calls it when no check ran.
     */
    void check();
    /** @return The presets with their check. */
    [[nodiscard]] const std::vector<PresetStatus>& presets();
    /**
     * Gives a character the build of a preset in one task of the world (the text at the top).
     * @param variant The name of a variant, or an empty text: the first variant.
     */
    PresetResult apply(const std::string& characterId, std::string_view preset, std::string_view variant);

private:
    [[nodiscard]] std::string cause_of(const BuildPreset& preset) const;

    World& m_world;
    std::vector<PresetStatus> m_presets;
    bool m_checked{false};
};

/** @return The items of a preset in the form of the inventory system. */
[[nodiscard]] std::vector<BuildItem> build_items(const BuildPreset& preset);

} // namespace soulwarden::backend::world
