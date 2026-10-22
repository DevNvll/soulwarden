#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "gamedata/quest_object_data.h"
#include "world/entity.h"

// The destructible quest objects (stage 1.8e, docs/systems/quests.md; the static read research/scratch/quests-18e/destructible and the
// live check I0 of stage 1.8e): the corruption nodules of 06_survivors and the monolith of 06_demonstorm2, and each other placement of the
// variation table Gatherable_Quest_Damageable (game data quest-objects.json).
//
// The client makes each object from its level data. Its slice has a DamageReceiver and no HitVolume: the client puts no melee contact on it
// into its input report (collector 0x485FBA0 needs a HitVolume), and the key E has no option. The server finds the hit itself: each strike
// of a melee swing or of an ability of a character (a strike window of the attack graph that the combat system follows: AttackSequences)
// hits each object of the character in its reach. The rules, in this sequence (each refusal changes nothing):
//   1. The character lives, and its exact position is known (no camera estimate).
//   2. The object is 5.0 m or less from the position of the character (3D: the reach of a melee weapon and its tolerance, the rule of a
//      creature hit) and in front of the camera of the character (90 degrees or less from its look).
//   3. An object with InteractOnlyOnQuest: an active task of the character has its InteractTag (client 0x8261FC0; the client shows the
//      object only then).
//   4. The object is not destroyed for the character, and the strike did not hit it before (one strike of a visit hits an object one time).
//   5. The strike is one unit of the work of the gatherable row of the object (BaseGatherTime, rounded up, at least 1: a rule of this
//      project; the data has no health of the object, and the client shows a full health bar of its own).
//   6. At the last unit the object is destroyed for the character: its LootTracker entry Looted (the claim of an instanced object, type 982:
//      live check I0, the client removes the object, its marker, and its corruption), the quest drops of the row (one transaction), then the
//      game event of the row (TaskGameEvent: MSQ_CorruptedNodule_1_interact) and its InteractTag (TaskInteract). After a uniform draw of the
//      respawn bounds of the row the entry goes (the map with no entry: the object is back for the character).
//
// The state is a value of the character (Entity::questObjects, rule MP-1): each character has its own objects (InstancedLootType 4 of the
// data), and its entries go only to its own sessions (the LootTracker state is owner only). The store has none: a new world entry has the
// objects again (the quest rows keep the progress).
//
// The cost depends on the strikes and the objects near the character (a grid of 64 m: rule MP-8), not on the size of the world. World
// thread only.

namespace soulwarden::backend::world {

class World;

namespace quest_object_rules {
/** The reach of a strike (meters, 3D): the reach of a melee weapon and its tolerance (combat_limits::kMeleeReach, kReachTolerance). */
inline constexpr float kReach = 5.0F;
/** The InstancedLootType of the entry (0: the value of the live check I0 of stage 1.8e; the data of the objects names 4). */
inline constexpr std::uint32_t kEntryKind = 0;
/** InstancedLootState Looted (protocol notes 4.41). */
inline constexpr std::uint32_t kLooted = 2;
} // namespace quest_object_rules

/** Why a strike did not hit an object (the counts and the log). */
enum class QuestObjectVerdict : std::uint8_t { hit, destroyed, tooFar, behind, notOnQuest, alreadyDestroyed, sameStrike };

[[nodiscard]] const char* quest_object_verdict_text(QuestObjectVerdict verdict);

/** The counts of the system (tools/world quest objects, the load case). */
struct QuestObjectCounts {
    std::uint64_t strikes{0};
    /** The objects that the strikes looked at (the cells of the grid near the character). */
    std::uint64_t visits{0};
    std::uint64_t hits{0};
    std::uint64_t destroyed{0};
    std::uint64_t respawns{0};
    std::uint64_t storeFailures{0};
    std::uint64_t refused[7]{};
};

class QuestObjects {
public:
    /** @param data The destructible quest objects (game data), or null: no object can be destroyed. */
    QuestObjects(World& world, const gamedata::QuestObjectData* data);

    [[nodiscard]] const gamedata::QuestObjectData* data() const {
        return m_data != nullptr && m_data->loaded() ? m_data : nullptr;
    }
    /**
     * A strike of a character passed its time (Combat::run_event: a strike window of a basic attack or of an ability): the objects of the
     * character in its reach take one unit of work each. `visit` and `strike` are the identity of the strike (a second call changes nothing).
     */
    void strike(Entity& character, std::uint32_t visit, std::uint32_t strike);
    /** The character leaves the world: its objects go with its entity (the respawn timers end themselves). */
    void left(const Entity& character);
    /** The respawn timer of a destroyed object of a character (World::after): the entry goes. An old generation changes nothing. */
    void respawn_due(const std::string& characterId, std::uint32_t index, std::uint64_t generation);

    [[nodiscard]] const QuestObjectCounts& counts() const {
        return m_counts;
    }

private:
    void destroy(Entity& character, std::uint32_t index);
    /** The LootTracker entries of the destroyed objects of a character (Entity::questObjectTracker). */
    void publish(Entity& character);
    void refuse(QuestObjectVerdict verdict);

    World& m_world;
    const gamedata::QuestObjectData* m_data;
    QuestObjectCounts m_counts;
    std::uint64_t m_generation{0};
};

} // namespace soulwarden::backend::world
