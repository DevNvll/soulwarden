#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "gamedata/combat_tables.h"
#include "state/store.h"

// The creature data: the usual spawn points of the creatures in an area of the world, and for
// each kind of creature what the backend needs for its record (protocol notes 4.14).
//
// A kind is one row of the vitals tables of the game with one category row and one alias: its
// ids on the wire, its name, its health, and its bodies. A body is one slice of the game: the
// asset id that a client instantiates, and the slot of each component that has a replicated
// state. An alias can have more than one slice (a left arm or a right arm, a man or a woman):
// the world selects one for each creature.
//
// The data also has what the behavior of a creature needs (protocol notes 4.17): the sense
// distances, the leash distances, and the speeds of each slice, the attacks of each slice (an
// action with its contact times and damage rows), the damage of each kind and of each level,
// and the terrain height at each spawn point. A file of a tool from before stage 3.4b (format
// 1) has none of these: the creatures of such a file stand at their spawn points.
//
// The data is game data: the tool tools/gamedata/creatures.sh makes the file from the game
// copy of the user, and the file stays outside the repository. Without the file the backend
// starts and works, and the world has no creature.

namespace soulwarden::backend::gamedata {

/** File name of the creature data in the game data directory of the backend. */
inline constexpr const char* kCreatureDataFile = "creatures.json";

/**
 * The tag state of the animation fragments of a creature on the wire: 12 bytes (the field
 * globalFragTags of a movement state). The tags of a creature (its weapons) select the variant
 * of each fragment: a fragment of a creature with a weapon has no variant with no tag.
 */
inline constexpr std::size_t kFragmentTagBytes = 12;
using FragmentTags = std::array<std::uint8_t, kFragmentTagBytes>;

/**
 * The behavior values of a slice: fields of its Perception and Blackboard components (game
 * data). The meaning of each value in the behavior is a rule of this project (world/behavior.h).
 */
struct CreatureBehavior {
    /** The vision of a creature at rest: the angle to each side of its direction (degrees) and the distance (meters). */
    float sightAngle{0.0F};
    float sightDistance{0.0F};
    /** The vision of a creature in a fight. */
    float combatAngle{0.0F};
    float combatDistance{0.0F};
    /** The largest distance of a creature from its home, at rest and in a fight (meters). 0: the slice has no value. */
    float restLeash{0.0F};
    float combatLeash{0.0F};
    /** The speed of a walk and of a run in a fight (meters in one second). */
    float walkSpeed{0.0F};
    float runSpeed{0.0F};
};

/** One contact of an attack: the moment at which the attack can hurt its target. */
struct CreatureContact {
    /** The time of the contact from the start of the action, in seconds. */
    float time{0.0F};
    /** The largest distance of a target that the contact can hurt, in meters (a value of the tool). */
    float reach{0.0F};
    /** The damage as a part of the damage of the creature, and the damage that the row adds. */
    float factor{0.0F};
    float added{0.0F};
    /** The damage type: its name, its number on the wire, and its category. */
    std::string type;
    std::uint8_t wire{0};
    DamageCategory category{DamageCategory::direct};
    /** Name of the damage row of the game. For a log line only. */
    std::string row;
    /**
     * True: the contact is `kContactGap` or less after the contact before it. The two are one
     * hit (two hit volumes of one swing), and the world does not test this one. The backend
     * sets it at the load of the data.
     */
    bool repeat{false};
};

/**
 * Rules of this project for the attacks of a slice (the game files have no rule for them; the
 * behavior of the creatures uses them, world/behavior.h).
 *
 * Two contacts of one attack that are this near in time are one hit (seconds): 242 of the
 * 2,091 attacks of the supported build have two contacts with the same damage row at the same
 * time, for the two hit volumes of one swing.
 */
inline constexpr float kContactGap = 0.15F;
/**
 * A heavy attack: the sum of the damage parts of its hits is more than `kHeavyRatio` times the
 * smallest sum of the attacks of its slice, and more than `kHeavyWeight`.
 */
inline constexpr float kHeavyRatio = 1.5F;
inline constexpr float kHeavyWeight = 0.5F;

/** One attack of a slice: an action of its action table with one contact or more. */
struct CreatureAttack {
    /** Index of the action in CreatureSlice::actions. */
    std::uint32_t action{0};
    /** The time of the action, in seconds: the creature starts no other action before its end. */
    float length{0.0F};
    /** The contacts, in the sequence of their times (the backend sorts them at the load of the data). */
    std::vector<CreatureContact> contacts;
    /** The sum of the damage parts of its hits (a contact with `repeat` is no hit), and the number of the hits. */
    float weight{0.0F};
    std::uint32_t hits{0};
    /** True for a heavy attack of its slice (`kHeavyRatio`, `kHeavyWeight`). */
    bool heavy{false};
};

/** One slice of a creature. */
struct CreatureSlice {
    /** One component of the slice that has a facet: a target of a client message (protocol notes 4.2). */
    struct Component {
        /** Id of the entity of the component in the slice file. */
        std::uint64_t entity{0};
        std::uint64_t component{0};
        /** Name of the entity and of the component class. For a log line only. */
        std::string name;
    };
    /**
     * One action of the action table of the slice. The position of an action in the table is
     * its index in the scope record of a movement state (protocol notes 4.14).
     */
    struct Action {
        std::string name;
        /** Name of the animation fragment of the action, and its index in a scope record. -1: no fragment. */
        std::string fragment;
        std::int32_t fragmentIndex{-1};
        /** True: the action starts only when the server starts it (the mark ServerStarted of the game files). */
        bool serverStarted{false};
    };
    /** One component of the slice that has a replicated state. */
    struct Slot {
        /** Index of the component in a state record (`m_replicationIndex`). */
        std::uint32_t slot{0};
        /** Name of the component class, or its type id when the tool does not know the class. */
        std::string component;
        /** Type index of the state type in the supported client build. 0: not known. */
        std::uint32_t stateType{0};
    };

    /** Path of the slice in the game files. For a log line only. */
    std::string path;
    /** Asset id and sub id of the slice: the metadata state of a creature has them. */
    std::array<std::uint8_t, 16> asset{};
    std::uint32_t subId{0};
    /** Id of the root entity of the slice file. */
    std::uint64_t root{0};
    /**
     * `m_sliceSpawnRadius` of the Aoi component of the slice, in meters: the distance at which
     * a session starts to observe a creature with this body (a rule of this project).
     */
    float interestRadius{0.0F};
    /** Slots of the movement component (action list), of the vitals component, and of the marker component. 0: none. */
    std::uint32_t movementSlot{0};
    std::uint32_t vitalsSlot{0};
    std::uint32_t markerSlot{0};
    std::vector<Slot> slots;
    std::vector<Component> components;
    /** The action table of the slice. Empty: the tool found no grid file for the slice. */
    std::vector<Action> actions;
    /**
     * Index of the action that a creature with this slice plays when it dies: the action
     * "Death", or "Death_FB" for a slice with no such action. No value: the slice has none with
     * a fragment, and a dead creature keeps its pose.
     */
    std::optional<std::uint32_t> deathAction;
    /**
     * Index of the action that a creature with this slice plays when it takes a hit: the action
     * "Reaction". No value: the slice has none with a fragment, and the creature keeps its pose.
     */
    std::optional<std::uint32_t> hitAction;
    /**
     * Index of the action that a creature with this slice plays while it walks or runs: the
     * action "Move_Combat" (410 of the 412 slices that fight have it). Its fragment is a blend
     * of the locomotion of the slice that the client selects from the speed of the creature: a
     * Grey Wolf gallops with the speed of its run, and stands at the speed 0. The client plays
     * no walk with no such action: the creature slides in its pose (live proof, user test 1).
     * No value: the slice has none with a fragment, and the creature slides.
     */
    std::optional<std::uint32_t> moveAction;
    /**
     * Name of the action that the server starts when it makes a creature with this slice (the
     * field m_spawnAction of the action list component, game data). The file has it only for
     * a slice whose body and name plate show in the client only after that action (the
     * picture tours of the slices: protocol notes 4.14). Empty: the creature starts with no
     * action of the server.
     */
    std::string spawnActionName;
    /** Index of that action in the action table. No value: the slice needs no spawn action. */
    std::optional<std::uint32_t> spawnAction;
    /**
     * The tag state of the animation fragments of a creature with this slice: the tags of its
     * weapons, as the bits of the tag definition file of its controller (the tool computes
     * them). All 0: the slice has no tag.
     */
    FragmentTags tags{};
    /** The behavior values of the slice. No value: a creature with this slice stands. */
    std::optional<CreatureBehavior> behavior;
    /** The attacks of the slice. Empty: a creature with this slice does not attack. */
    std::vector<CreatureAttack> attacks;
    /** The indexes of its attacks that are not heavy, and of its heavy attacks, in the sequence of the data. */
    std::vector<std::uint32_t> lightAttacks;
    std::vector<std::uint32_t> heavyAttacks;

    /** @return True when a creature with this slice gets a target and attacks it: the slice has a sense, a speed, and an attack. */
    [[nodiscard]] bool fights() const {
        return behavior && behavior->sightDistance > 0.0F && behavior->runSpeed > 0.0F && !attacks.empty();
    }

    /** @return The index of the action with this name that has a fragment, or no value. */
    [[nodiscard]] std::optional<std::uint32_t> find_action(std::string_view name) const;

    /** @return The facet target of one component for an entity group id. */
    [[nodiscard]] std::uint64_t target(std::uint64_t group, const Component& component) const {
        return (component.entity ^ root ^ group) + component.component;
    }
    /** @return The component whose target for this entity group id is `target`, or null. */
    [[nodiscard]] const Component* find(std::uint64_t group, std::uint64_t target) const;
};

/** One slice of the alias of a kind. */
struct CreatureBody {
    /** Index into CreatureData::slices. */
    std::uint32_t slice{0};
    /** Weight of this slice in the selection. */
    std::uint32_t percent{0};
};

struct CreatureKind {
    /** Id of the kind for a command: the name of its vitals row, with a number when more than one kind has the row. */
    std::string id;
    /** Names of the vitals row and of the category row, and their ids on the wire (CRC-32 of the lower-case name). */
    std::string vitals;
    std::string category;
    std::uint32_t vitalsId{0};
    std::uint32_t categoryId{0};
    /** English name of the category row: the name that the client shows. For a list only. */
    std::string name;
    /** English name of the vitals row. */
    std::string vitalsName;
    std::string family;
    std::string creatureType;
    /** The health of the kind as a part of the health of its level. */
    float healthFactor{1.0F};
    /** The damage of the kind as a part of the base damage of its level. */
    float damageFactor{1.0F};
    /** The level of the most spawn points of the kind. */
    std::uint32_t level{1};
    std::vector<CreatureBody> bodies;
    /** Number of spawn points of the kind in the file. */
    std::uint32_t points{0};
};

/** One place of the world where the game has a creature. */
struct SpawnPoint {
    /** Id of the placement in the game files: the stable key of the spawn point. */
    std::string id;
    /** Index into CreatureData::kinds. */
    std::uint32_t kind{0};
    std::uint32_t level{1};
    state::WorldPosition position;
    /** Direction of the creature: the angle about the vertical axis, in degrees. */
    float yaw{0.0F};
    /** The two respawn values of the spawner, in seconds. Stage 3.5a uses them. */
    float respawnMin{0.0F};
    float respawnMax{0.0F};
    /**
     * The terrain height at the place of the point, in meters. No value: the file does not have
     * it (a file of format 1, or a place with no height map).
     */
    std::optional<float> ground;
};

/** The values of one level of the level table of the game. */
struct CreatureLevel {
    std::uint32_t level{1};
    /** The health of a creature of the level, before the factor of its kind. */
    float health{0.0F};
    /** The base damage of a creature of the level, before the factor of its kind and of its attack. 0: the file has none. */
    float damage{0.0F};
    /** The gear score of a creature of the level: the armor of a player works against it. 0: the file has none. */
    float gearScore{0.0F};
};

/**
 * A creature that walks is at the terrain height. A creature whose spawn point is farther than
 * this from the terrain height (a floor of a building, a bridge, a cave) does not leave its
 * place: the terrain is not its floor (a rule of this project, protocol notes 4.17).
 */
inline constexpr float kTerrainTolerance = 1.0F;

class CreatureData {
public:
    /**
     * Reads the file of the tool. A file that is not complete (a member that is not there, an
     * index outside its list, a slice with no movement slot, a position that is not a
     * number) is not used: the data then stays empty. The same for a file whose parts do not
     * fit together: a kind or a spawn point with a level that the level table does not have, a
     * movement, vitals, or marker slot that is not the slot of that component in the slot list
     * of its slice, a slice with the null asset id, a kind whose health (the health of a level
     * times its factor) is 0 or not finite for a level of the table, a spawn action that the
     * action table of its slice does not have with a fragment, and a member "counts" with a
     * wrong form or with a number that is not the size of its list. The same for the behavior
     * data: an attack whose action the action table does not have with a fragment, an attack
     * with no contact, a contact after the end of its attack, a reach, a speed, or a distance
     * that is not a number of its range, and a damage type with no number or no category.
     * @return False when the file is not there or is not complete. `error()` then has the cause.
     */
    [[nodiscard]] bool load(const std::string& path);

    /**
     * Sets the data with no file (tests), with the same check as the data of a file.
     * @return False when the data is not complete: the object then has no data.
     */
    [[nodiscard]] bool set(std::vector<CreatureSlice> slices,
                           std::vector<CreatureKind> kinds,
                           std::vector<SpawnPoint> spawnPoints,
                           std::vector<std::pair<std::uint32_t, float>> levels);
    /** The same, with the damage and the gear score of each level. */
    [[nodiscard]] bool set_full(std::vector<CreatureSlice> slices,
                                std::vector<CreatureKind> kinds,
                                std::vector<SpawnPoint> spawnPoints,
                                std::vector<CreatureLevel> levels);

    [[nodiscard]] bool loaded() const {
        return !m_kinds.empty();
    }
    /** @return Why the last load gave no data. Empty after a load that gave data. */
    [[nodiscard]] const std::string& error() const {
        return m_error;
    }
    [[nodiscard]] const std::vector<CreatureSlice>& slices() const {
        return m_slices;
    }
    [[nodiscard]] const std::vector<CreatureKind>& kinds() const {
        return m_kinds;
    }
    [[nodiscard]] const std::vector<SpawnPoint>& spawn_points() const {
        return m_spawnPoints;
    }

    /** @return The kind with this id (the compare ignores the case of the letters A to Z), or null. */
    [[nodiscard]] const CreatureKind* find_kind(std::string_view id) const;

    /**
     * @return The maximum health of a kind at a level: the health of the level (the level table
     *         of the game) times the health factor of the kind. This is a rule of this project
     *         (protocol notes 4.14). 0 when the level table has no row for the level.
     */
    [[nodiscard]] float health(const CreatureKind& kind, std::uint32_t level) const;
    /** @return True when the level table has a row for the level. */
    [[nodiscard]] bool has_level(std::uint32_t level) const {
        return m_levels.count(level) != 0;
    }
    /**
     * @return The damage of a kind at a level before the factor of an attack: the base damage of
     *         the level times the damage factor of the kind. 0 when the table has no such value.
     */
    [[nodiscard]] float damage(const CreatureKind& kind, std::uint32_t level) const;
    /** @return The gear score of a creature of a level. 0 when the table has no such value. */
    [[nodiscard]] float gear_score(std::uint32_t level) const;
    /** @return The number of slices that have an attack and the values for the way to a target. */
    [[nodiscard]] std::size_t fighting_slices() const {
        return m_fightingSlices;
    }
    /** @return The number of kinds with a slice that fights, and of spawn points of these kinds. */
    [[nodiscard]] std::size_t fighting_kinds() const {
        return m_fightingKinds;
    }
    [[nodiscard]] std::size_t fighting_points() const {
        return m_fightingPoints;
    }
    /**
     * @return The number of spawn points of a kind that fights whose creature does not leave
     *         its place: the point has no terrain height, or it is farther than
     *         kTerrainTolerance from it.
     */
    [[nodiscard]] std::size_t rooted_points() const {
        return m_rootedPoints;
    }
    /** @return The largest vision distance at rest of a slice that fights, in meters. 0: no slice fights. */
    [[nodiscard]] float largest_sight() const {
        return m_largestSight;
    }
    /** @return True when a creature of this spawn point can walk on the terrain. */
    [[nodiscard]] static bool on_terrain(const SpawnPoint& point) {
        return point.ground && std::abs(point.position.z - *point.ground) <= kTerrainTolerance;
    }

    /**
     * Finds the kinds for a search text. Each word of the text must be in the id, the name, the
     * category, or the family of a kind (the compare ignores the case of the letters A to Z).
     * An empty text finds each kind.
     * @param total When not null: the number of all kinds that the text finds.
     * @return The kinds in the sequence of the file.
     */
    [[nodiscard]] std::vector<const CreatureKind*> search(std::string_view text,
                                                          std::size_t offset,
                                                          std::size_t limit,
                                                          std::size_t* total = nullptr) const;

private:
    void clear();
    [[nodiscard]] bool check();
    void build_keys();

    std::vector<CreatureSlice> m_slices;
    std::vector<CreatureKind> m_kinds;
    std::vector<SpawnPoint> m_spawnPoints;
    /** Level to the values of the level. */
    std::unordered_map<std::uint32_t, CreatureLevel> m_levels;
    std::size_t m_fightingSlices{0};
    std::size_t m_fightingKinds{0};
    std::size_t m_fightingPoints{0};
    std::size_t m_rootedPoints{0};
    float m_largestSight{0.0F};
    /** Lower-case kind id to the position in the list. */
    std::unordered_map<std::string, std::size_t> m_byId;
    std::vector<std::string> m_searchTexts;
    std::string m_error;
};

} // namespace soulwarden::backend::gamedata
