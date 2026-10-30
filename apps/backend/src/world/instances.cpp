#include "world/instances.h"

#include <algorithm>
#include <cctype>

#include "core/logging/log.h"
#include "gamedata/gathering_data.h"
#include "gamedata/interaction_data.h"
#include "gamedata/quest_data.h"
#include "world/encounters.h"
#include "world/gathering.h"
#include "world/position_index.h"
#include "world/interactions.h"
#include "world/levels.h"
#include "world/quests.h"
#include "world/world.h"

namespace soulwarden::backend::world {

namespace {

/** The interaction actions of an entrance and of an exit portal (the client enum 0x35E99B0; research expeditions.md). */
constexpr std::uint8_t kOpenDungeonMenu = 71;
constexpr std::uint8_t kEnterDungeon = 72;
constexpr std::uint8_t kExitDungeon = 74;

bool same_text(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

/**
 * Stage QG9a: the entrance of a game mode (docs/systems/instances.md). OpenDungeonMenu (action 71, committed): the client opens its window
 * itself (dungeonenterscreen); the backend accepts the use. An Enter option (action 72, one time; the option of the Medea entrance has the
 * text EnterQuestCaerDun: the mode comes from m_gameMode of the entrance): the checks of Instances::entrance_result, the result 3094, and the
 * entry in the next tick of the world (the reply 3743 of the request goes first; a move inside the request would end its committed use).
 */
class EntranceHandler final : public InteractionHandler {
public:
    explicit EntranceHandler(World& world) : m_world(world) {}
    [[nodiscard]] const char* name() const override {
        return "the entrance of a game mode";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        if (context.option->action == kOpenDungeonMenu) {
            return InteractionVerdict::accepted;
        }
        if (context.option->action != kEnterDungeon || !context.object->entrance) {
            return InteractionVerdict::refused;
        }
        const gamedata::DungeonEntrance& entrance = *context.object->entrance;
        const std::uint32_t result = m_world.instances().entrance_result(character, entrance, context.difficulty);
        if (result != dungeon_result::kSuccess) {
            m_world.dungeon_result(character.owner, entrance.target, context.object->group, result);
            return InteractionVerdict::refused;
        }
        // Code review of stage QG9a, finding QG9AR-R6: the result 3094 comes from the entry of the next tick: Success after the commit of its
        // rows and before the level change; EntranceActivationFailure when the entry fails (a store that refused the rows).
        m_world.after(std::chrono::milliseconds(0), [characterId = character.characterId, owner = character.owner, mode = entrance.mode,
                                                     target = entrance.target, group = context.object->group](World& world) {
            const InstanceResult entered = world.instances().enter_mode(characterId, mode, [&world, owner, target, group]() {
                world.dungeon_result(owner, target, group, dungeon_result::kSuccess);
            });
            if (entered.error != InstanceError::none) {
                world.dungeon_result(owner, target, group, dungeon_result::kEntranceActivationFailure);
            }
        });
        return InteractionVerdict::accepted;
    }

private:
    World& m_world;
};

/**
 * Stage QG9a: the exit portal of an instance (ExitDungeon, action 74, one time): a server child of the space of the instance. The leave in
 * the next tick of the world (the reply 3743 goes first), with the rows of an exit.
 */
class ExitHandler final : public InteractionHandler {
public:
    explicit ExitHandler(World& world) : m_world(world) {}
    [[nodiscard]] const char* name() const override {
        return "the exit portal of an instance";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        const Instance* instance = m_world.instances().instance_of(character.characterId);
        // Stage QG9d: a portal of the instance (a server child that the instance system made), or the exit portal of the mode root of its
        // mode that the client makes from the level data (InteractionObject::exitMode). Stage DG3: also the exit stone of the level set of an
        // own level (Amrine: a capital of the level next to the start that the client makes). A level object in an instance has the state
        // of the space of the instance (InteractionContext::levelSpace; one rule, docs/systems/interactions.md).
        const bool own = instance != nullptr
                         && std::find(instance->objects.begin(), instance->objects.end(), context.instance) != instance->objects.end();
        const bool level = instance != nullptr && context.levelSpace == instance->space && context.object != nullptr
                           && (context.catalog != nullptr
                               || (!context.object->exitMode.empty() && same_text(context.object->exitMode, instance->mode)));
        if (context.option->action != kExitDungeon || instance == nullptr || instance->space != character.space || (!own && !level)) {
            return InteractionVerdict::refused;
        }
        m_world.after(std::chrono::milliseconds(0), [characterId = character.characterId](World& world) {
            (void)world.instances().leave(characterId, InstanceLeave::exitPortal);
        });
        return InteractionVerdict::accepted;
    }

private:
    World& m_world;
};

} // namespace

const char* instance_leave_text(InstanceLeave cause) {
    switch (cause) {
    case InstanceLeave::command:
        return "a command";
    case InstanceLeave::exitPortal:
        return "the exit portal";
    case InstanceLeave::request:
        return "a leave request of the client (3309, 1980, or 926)";
    }
    return "unknown";
}

const char* instance_error_text(InstanceError error) {
    switch (error) {
    case InstanceError::none:
        return "none";
    case InstanceError::noData:
        return "the backend has no game modes (make game-modes.json with tools/gamedata/game-modes.sh)";
    case InstanceError::unknownMap:
        return "the game modes have no map with this id (a mode with more than one map needs its map id)";
    case InstanceError::notInWorld:
        return "the character is not in the world";
    case InstanceError::inInstance:
        return "the character is in an instance (instance return first)";
    case InstanceError::notInInstance:
        return "the character is in no instance";
    case InstanceError::unknownInstance:
        return "no instance with this id";
    case InstanceError::wrongMap:
        return "the instance is of a different map";
    case InstanceError::full:
        return "the instance has its largest number of members";
    case InstanceError::storage:
        return "the database did not accept the rows: nothing changed";
    case InstanceError::badReturnPlace:
        return "the place of the character is not a place of a travel: it cannot be its return place";
    }
    return "unknown";
}

Instances::Instances(World& world, state::Store& store, const gamedata::GameModes* data)
    : m_world(world), m_store(store), m_data(data), m_entranceHandler(std::make_unique<EntranceHandler>(world)),
      m_exitHandler(std::make_unique<ExitHandler>(world)) {
    // Stage QG9a: each option of an entrance (OpenDungeonMenu and the Enter options of action 72) and of an exit portal (ExitDungeon). The
    // handlers test the action of the option.
    world.interactions().register_kind_handler(gamedata::InteractionKind::dungeonEntrance, *m_entranceHandler);
    world.interactions().register_kind_handler(gamedata::InteractionKind::dungeonExit, *m_exitHandler);
}

Instances::~Instances() = default;

bool Instances::has_space(SpaceId space) const {
    return space == kMainSpace || m_bySpace.contains(space);
}

const SpaceLevel* Instances::level_of(SpaceId space) const {
    if (space == kMainSpace) {
        return &m_mainLevel;
    }
    const Instance* instance = instance_of_space(space);
    return instance != nullptr ? &instance->level : nullptr;
}

const Instance* Instances::find(std::uint64_t id) const {
    const auto found = m_instances.find(id);
    return found == m_instances.end() ? nullptr : &found->second;
}

const Instance* Instances::instance_of_space(SpaceId space) const {
    const auto found = m_bySpace.find(space);
    return found == m_bySpace.end() ? nullptr : find(found->second);
}

const Instance* Instances::instance_of(const std::string& characterId) const {
    const auto found = m_byCharacter.find(characterId);
    return found == m_byCharacter.end() ? nullptr : find(found->second);
}

void Instances::tick() {
    recover();
    // Stage QG9a: the empty instances with content whose timer came (only the instances: MP-8).
    const WorldClock::time_point now = m_world.clock().now();
    std::vector<std::uint64_t> due;
    for (const auto& [id, instance] : m_instances) {
        if (instance.members.empty() && instance.emptyUntil && *instance.emptyUntil <= now) {
            due.push_back(id);
        }
    }
    for (const std::uint64_t id : due) {
        ++m_counts.emptyEnded;
        end_instance(id, "it was empty for the time of its mode");
    }
    // Stage QG9d: the quest objects of the instances (only the instances with quest objects: MP-8).
    for (auto& [id, instance] : m_instances) {
        if (!instance.questObjects.empty()) {
            look_quest_objects(instance);
        }
        // Stage DG3: the activation of the checkpoints (only the instances with checkpoints and their members: MP-8).
        if (!instance.checkpoints.empty()) {
            look_checkpoints(instance);
        }
    }
}

std::uint32_t Instances::group_size(const Entity& character) const {
    (void)character;
    // [agent] The backend has no group system (roadmap 6.3, stage DG6): the group of an entry is the character.
    return 1;
}

bool Instances::mode_runs(std::string_view mode) const {
    const gamedata::GameModes* modes = data();
    const gamedata::GameMap* map = modes != nullptr ? modes->find(std::string(mode)) : nullptr;
    if (map == nullptr || !same_text(map->mode, mode)) {
        return false;
    }
    if (map->level != kMainLevel && map->soloTrial) {
        // Stage QG9e: a solo mode of an own level runs with an accepted data set of its level (the first read of the set gives its instance
        // recipes to the encounter system, Encounters::add_level) and the runner takes each instance recipe of the mode (the rule of stage
        // QG9a: Encounters::runs_mode). The solo modes have IsDungeon 1 too [data: QuestLiang, QuestAdiana]: before this stage the rule of an
        // expedition below took them with the set alone (an instance with no encounter).
        return m_world.levels().available(map->level) && m_world.encounters().runs_mode(std::string(mode));
    }
    if (map->dungeon && map->level != kMainLevel) {
        // Stage DG3 [orchestrator scope: the encounters of an expedition are stage DG5]: an expedition runs with the content of the data set
        // of its level (its creatures, its navigation, its objects, its checkpoints).
        return m_world.levels().available(map->level);
    }
    return m_world.encounters().runs_mode(std::string(mode));
}

std::uint32_t Instances::entrance_result(const Entity& character, const gamedata::DungeonEntrance& entrance, std::uint32_t difficulty) const {
    // The checks of an entry, in this order (docs/systems/instances.md, "The entry at the entrance of an expedition"); a refusal changes
    // nothing (rule MP-5). The result values: eDungeonActivateResponse (research dungeons.md 3.3).
    const gamedata::GameModes* modes = data();
    const gamedata::GameMap* map = modes != nullptr ? modes->find(entrance.mode) : nullptr;
    const char* cause = nullptr;
    std::uint32_t result = dungeon_result::kSuccess;
    const std::uint32_t group = group_size(character);
    if (map == nullptr) {
        cause = "the game modes have no map of the mode (or more than one)";
        result = dungeon_result::kEntranceActivationFailure;
    } else if (!same_text(map->mode, entrance.mode)) {
        cause = "the map is of a different mode";
        result = dungeon_result::kEntranceActivationFailure;
    } else if (character.space != kMainSpace || m_byCharacter.contains(character.characterId)) {
        cause = "the character is in an instance";
        result = dungeon_result::kGroupAlreadyInDungeon;
    } else if (difficulty != 0) {
        // Stage DG3 [data: IsMutable, the tiers 1 to 3 of DifficultyProgressionId; agent: the mutations are stage DG7]: the normal mode only.
        cause = map->mutableMode ? "a mutation tier: the backend has no mutation (the tiers and their ranks are a later stage)"
                                 : "a mutation tier of a mode that is not mutable (IsMutable 0)";
        result = dungeon_result::kMutationDifficultyReq;
    } else if (character.progression.level < map->requiredLevel) {
        cause = "the level of the character is below RequiredLevel of the mode";
        result = dungeon_result::kEntranceActivationFailure;
    } else if (!map->entryConditional.empty() && !m_world.quests().conditional(character, map->entryConditional)) {
        // Stage DG3 [data: EntryAchievementConditional; the queue HUD of the client tests it too (CanEnterDungeon)].
        cause = "the character does not have the achievements of EntryAchievementConditional of the mode";
        result = dungeon_result::kEntranceActivationFailure;
    } else if (!map->requiredItemClasses.empty()) {
        // Stage DG3 [data: RequiredConsumableItemClasses; agent: no row of the supported build has one, and the backend has no check of an
        // item class yet].
        cause = "the mode needs an item of RequiredConsumableItemClasses (the backend has no check of an item class)";
        result = dungeon_result::kMissingReqItem;
    } else if (group < kMinimumGroupSize) {
        cause = "the group is smaller than the minimum group size (decision D7: 1)";
        result = dungeon_result::kNotEnoughMembers;
    } else if (map->teamCapacity != 0 && group > map->teamCapacity) {
        cause = "the group is larger than TeamCapacity of the mode";
        result = dungeon_result::kTooManyMembers;
    } else if (const std::string_view off = encounter_rules::mode_off(entrance.mode, m_world.options().questMedusaRuns); !off.empty()) {
        // Stage QG9b: the setting of the mode (encounter_rules::kQuestMedusaRuns) turns it off; the reason is a literal (it ends with NUL).
        cause = off.data();
        result = dungeon_result::kEntranceActivationFailure;
    } else if (!mode_runs(entrance.mode)) {
        cause = map->level != kMainLevel && !m_world.levels().available(map->level)
                    ? "the backend has no accepted data set of the level of the mode (tools/gamedata/level.sh)"
                    : "the backend does not run the content of the mode (docs/systems/instances.md)";
        result = dungeon_result::kEntranceActivationFailure;
    }
    const_cast<InstanceCounts&>(m_counts).entranceRequests += 1;
    if (cause != nullptr) {
        const_cast<InstanceCounts&>(m_counts).entranceRefused += 1;
        log::write("world: the entry of character %s at the entrance of the mode %s (difficulty %u) is refused: %s (result %u)",
                   character.characterId.c_str(), entrance.mode.c_str(), static_cast<unsigned>(difficulty), cause,
                   static_cast<unsigned>(result));
    } else {
        log::write("world: the entry of character %s at the entrance of the mode %s is accepted (result 0, map %s, group %u)",
                   character.characterId.c_str(), entrance.mode.c_str(), map->id.c_str(), static_cast<unsigned>(group));
    }
    return result;
}

InstanceResult Instances::enter_mode(const std::string& characterId, const std::string& mode, const std::function<void()>& committed) {
    InstanceResult result;
    const gamedata::GameModes* modes = data();
    const gamedata::GameMap* map = modes != nullptr ? modes->find(mode) : nullptr;
    if (map == nullptr) {
        ++m_counts.refused;
        result.error = InstanceError::unknownMap;
        return result;
    }
    // The empty instance of the mode that the character left (its timer runs) and whose content did not succeed: the character joins it
    // again [agent: the content stays for the timeout of the mode, and a character that left it by mistake comes back to its fight; after a
    // success an entry is a new run].
    std::uint64_t join = 0;
    for (const auto& [id, instance] : m_instances) {
        if (instance.map == map->id && instance.members.empty() && instance.emptyUntil && !instance.succeeded
            && instance.former.contains(characterId)) {
            join = id;
            break;
        }
    }
    result = enter(characterId, map->id, join, committed);
    if (result.error == InstanceError::none && join != 0) {
        ++m_counts.rejoined;
    }
    return result;
}

bool Instances::in_mode(const Entity& character, std::string_view mode) const {
    const Instance* instance = instance_of(character.characterId);
    return instance != nullptr && character.space == instance->space && !mode.empty() && same_text(instance->mode, mode);
}

void Instances::content_succeeded(SpaceId space) {
    const auto found = m_bySpace.find(space);
    if (found == m_bySpace.end()) {
        return;
    }
    Instance& instance = m_instances.at(found->second);
    if (m_world.encounters().space_done(space)) {
        instance.succeeded = true;
    }
    log::write("world: the content of instance %llu (%s) succeeded%s", static_cast<unsigned long long>(instance.id), instance.map.c_str(),
               instance.succeeded ? "" : " in part (an other encounter of the mode waits)");
}

void Instances::membership_changed(const std::string& characterId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return;
    }
    // The GameModeParticipant state (static read E2 of stage QG9a): the entry of the instance of the character, or the empty map.
    const Instance* instance = instance_of(characterId);
    if (instance != nullptr && character->space == instance->space) {
        GameModeMembership membership;
        membership.actor = instance->modeActor;
        membership.group = instance->modeEntity;
        membership.facet = instance->modeEntity;
        membership.modeCrc = gamedata::quest_crc(instance->mode);
        membership.mapCrc = gamedata::quest_crc(instance->map);
        const auto member = instance->members.find(characterId);
        membership.enteredNs = m_world.clock().wall_time_ns(member != instance->members.end() ? member->second.entered : instance->started);
        const gamedata::GameMap* map = data() != nullptr ? data()->find(instance->map) : nullptr;
        membership.dungeon = map != nullptr && map->dungeon;
        character->gameModeMembership = membership;
        character->gameModeKnown = true;
    } else {
        character->gameModeMembership.reset();
    }
    if (character->gameModeKnown) {
        m_world.touch(character->id, ComponentKey::gameModeParticipant);
        // Stage QG9e: the PlayerArena state (isInArena while the membership is in a dungeon mode, false after the leave).
        m_world.touch(character->id, ComponentKey::playerArena);
    }
    // Stage DG3: the home point list (the checkpoints of the instance, or the empty list after the leave).
    home_points_changed(characterId);
    m_world.quests().mode_changed(*character);
}

std::size_t Instances::make_checkpoints(Instance& instance) {
    const gamedata::LevelData* level = m_world.levels().data_of(instance.space);
    if (level == nullptr || !level->interaction_part().loaded) {
        return 0;
    }
    const gamedata::InteractionData& rows = level->interactions();
    for (std::uint32_t index = 0; index < rows.checkpoints().size(); ++index) {
        const gamedata::ModeCheckpoint& data = rows.checkpoints()[index];
        if (!same_text(data.mode, instance.mode) || !same_text(data.map, instance.map) || data.templateIndex >= rows.templates().size()) {
            continue;
        }
        // The server child of the checkpoint (the crystal; the client shows it only after the record of the server: m_isClientAuthoratative
        // false) [data; agent: the mode script that spawns it at the start of the instance is not in the client].
        const Interactions::SpawnedContainer made = m_world.interactions().spawn_container(
            data.templateIndex, data.position, data.yaw, data.spawner + "/instance/" + std::to_string(instance.id), instance.space, &rows);
        InstanceCheckpoint checkpoint;
        checkpoint.index = index;
        checkpoint.center = data.triggerCenter;
        checkpoint.radius = data.triggerRadius;
        checkpoint.cooldown = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::duration<double>(data.cooldown));
        HomePoint& point = checkpoint.point;
        if (made.entity != nullptr) {
            checkpoint.entity = made.entity->id;
            instance.objects.push_back(made.entity->id);
            // The reference of the point: the actor of the child, its G, and the facet target of its HomeComponent (K = root XOR G; the
            // instance id of an entity of the slice is its id XOR K; the target is the instance id plus the component id: protocol notes 4.2).
            point.actor = made.entity->actor.bytes;
            point.group = made.entity->id;
            point.facet = (data.homeEntity ^ (rows.templates()[data.templateIndex].root ^ made.entity->id)) + data.homeComponent;
        }
        point.name = data.name;
        point.position = data.respawn;
        point.cooldownNs = static_cast<std::uint64_t>(checkpoint.cooldown.count());
        point.type = data.type;
        point.id = data.spawner;
        point.unique = index + 1;
        instance.checkpoints.push_back(std::move(checkpoint));
        ++m_counts.checkpointsMade;
    }
    return instance.checkpoints.size();
}

void Instances::look_checkpoints(Instance& instance) {
    bool changed = false;
    for (const auto& [characterId, member] : instance.members) {
        const Entity* character = m_world.find_character(characterId);
        if (character == nullptr || character->space != instance.space || character->life.dead) {
            continue;
        }
        for (InstanceCheckpoint& checkpoint : instance.checkpoints) {
            if (checkpoint.active) {
                continue;
            }
            const float dx = character->position.x - checkpoint.center.x;
            const float dy = character->position.y - checkpoint.center.y;
            const float dz = character->position.z - checkpoint.center.z;
            // [data: the sphere of the detection volume of the server facet of the HomeComponent (5 m for a crystal, 30 m for the entrance)]
            if (dx * dx + dy * dy + dz * dz <= checkpoint.radius * checkpoint.radius) {
                checkpoint.active = true;
                changed = true;
                ++m_counts.checkpointsActivated;
                log::write("world: character %s activated the checkpoint %s of instance %llu (%s) at %.2f %.2f %.2f", characterId.c_str(),
                           checkpoint.point.id.c_str(), static_cast<unsigned long long>(instance.id), instance.map.c_str(),
                           static_cast<double>(checkpoint.center.x), static_cast<double>(checkpoint.center.y),
                           static_cast<double>(checkpoint.center.z));
            }
        }
    }
    if (changed) {
        // [agent] A checkpoint is of the instance: each member gets it (the group of a later stage shares it).
        for (const auto& [characterId, member] : instance.members) {
            home_points_changed(characterId);
        }
    }
}

void Instances::home_points_changed(const std::string& characterId) {
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return;
    }
    std::vector<HomePoint> points;
    const Instance* instance = instance_of(characterId);
    if (instance != nullptr && character->space == instance->space) {
        const auto cooldowns = instance->checkpointCooldowns.find(characterId);
        for (const InstanceCheckpoint& checkpoint : instance->checkpoints) {
            if (!checkpoint.active) {
                continue;
            }
            HomePoint point = checkpoint.point;
            if (cooldowns != instance->checkpointCooldowns.end()) {
                const auto used = cooldowns->second.find(point.id);
                if (used != cooldowns->second.end() && used->second > m_world.clock().now()) {
                    point.cooldownEndNs = m_world.clock().wall_time_ns(used->second);
                }
            }
            points.push_back(std::move(point));
        }
    }
    // The first list of the world entry comes with the first checkpoint of an instance; a leave sends the empty list.
    if (!character->homePointsKnown && points.empty() && (instance == nullptr || instance->checkpoints.empty())) {
        return;
    }
    const bool same = character->homePointsKnown && points.size() == character->homePoints.size()
                      && std::equal(points.begin(), points.end(), character->homePoints.begin(), [](const HomePoint& a, const HomePoint& b) {
                             return a.id == b.id && a.group == b.group && a.cooldownEndNs == b.cooldownEndNs;
                         });
    if (same) {
        return;
    }
    character->homePoints = std::move(points);
    character->homePointsKnown = true;
    m_world.touch(character->id, ComponentKey::playerHome);
}

std::optional<state::WorldPosition> Instances::use_checkpoint(const Entity& character, const std::string& id) {
    const auto owned = m_byCharacter.find(character.characterId);
    if (owned == m_byCharacter.end() || id.empty()) {
        return std::nullopt;
    }
    Instance& instance = m_instances.at(owned->second);
    if (character.space != instance.space) {
        return std::nullopt;
    }
    // Code review of stage DG3, finding DG3R-R1: the cooldowns of the character are of the instance (they stay over a leave and a disconnect).
    std::map<std::string, WorldClock::time_point>& cooldowns = instance.checkpointCooldowns[character.characterId];
    const WorldClock::time_point now = m_world.clock().now();
    std::erase_if(cooldowns, [&](const auto& entry) { return entry.second <= now; });
    for (const InstanceCheckpoint& checkpoint : instance.checkpoints) {
        if (!checkpoint.active || checkpoint.point.id != id) {
            continue;
        }
        const auto used = cooldowns.find(id);
        if (used != cooldowns.end() && used->second > now) {
            // [agent; research death-travel.md 4.5: a request of a dead character is not refused] the start of the map.
            log::write("world: the checkpoint %s of instance %llu has its cooldown for character %s: the respawn is at the start of the map",
                       id.c_str(), static_cast<unsigned long long>(instance.id), character.characterId.c_str());
            return std::nullopt;
        }
        cooldowns[id] = now + std::chrono::duration_cast<WorldClock::time_point::duration>(checkpoint.cooldown);
        ++m_counts.checkpointRespawns;
        home_points_changed(character.characterId);
        return checkpoint.point.position;
    }
    return std::nullopt;
}

Instances::ModeRootFiles Instances::mode_root_files(SpaceId space) const {
    // Stage QG9e: the objects of the mode root of an instance of an own level are in the set of its level (its encounter-data.json and the
    // templates of its interaction-data.json); of the main world level in the files of the backend start.
    if (const gamedata::LevelData* level = m_world.levels().data_of(space); level != nullptr) {
        return {level->encounter_part().loaded ? &level->encounters() : nullptr,
                level->interaction_part().loaded ? &level->interactions() : nullptr,
                level->interaction_part().loaded ? &level->interactions() : nullptr};
    }
    return {m_world.encounters().data(), m_world.interactions().data(), nullptr};
}

void Instances::start_content(Instance& instance) {
    const std::size_t encounters = m_world.encounters().start_space(instance.space, instance.mode);
    instance.content = encounters > 0;
    // The exit portal of the mode (a server child of the mode root: interactobject_portalstone of Medea, 6 m from the start of the map) at
    // the start of the instance [agent: the mode script that spawns it is not in the client]. Stage QG9e: of an own level from its set.
    const ModeRootFiles files = mode_root_files(instance.space);
    const gamedata::EncounterData* recipes = files.recipes;
    const gamedata::InteractionData* catalog = files.catalog;
    std::size_t portals = 0;
    if (recipes != nullptr && catalog != nullptr) {
        for (const gamedata::InstanceObject& object : recipes->instance_objects()) {
            // Stage QG9d [data: m_spawnOnInitialize of the spawner of the mode root; live check of stage QG9c: the client sent ExitDungeon
            // to its own portal of QuestSeraphim]: the client makes a portal whose spawner spawns at the init from the level data (the
            // object with the member mode of interaction-data.json); the instance system makes no second one.
            if (!same_text(object.mode, instance.mode) || !same_text(object.map, instance.map) || object.onInit) {
                continue;
            }
            const std::optional<std::uint32_t> templateIndex = catalog->instance_object(object.slice);
            if (!templateIndex || catalog->templates()[*templateIndex].kind != gamedata::InteractionKind::dungeonExit) {
                continue;
            }
            const Interactions::SpawnedContainer made =
                m_world.interactions().spawn_container(*templateIndex, object.position, object.yaw,
                                                       object.key + "/instance/" + std::to_string(instance.id), instance.space, files.own);
            if (made.entity != nullptr) {
                instance.objects.push_back(made.entity->id);
                ++portals;
            }
        }
    }
    instance.content = instance.content || portals > 0;
    // Stage DG3: the checkpoints of the mode root of an own level, and the content of its level set (its creatures): the instance then waits
    // for the timer of its mode when it is empty, and a former member joins it again [data: TimerThresholdDefault; agent].
    const std::size_t checkpoints = make_checkpoints(instance);
    const gamedata::LevelData* level = m_world.levels().data_of(instance.space);
    instance.content = instance.content || checkpoints > 0
                       || (level != nullptr && level->creature_part().loaded && !level->creatures().spawn_points().empty());
    // Stage QG9d: the quest objects of the mode root (the heartgems of QuestCaerDun): an object of the mode with a template of the kind
    // gathering, a row of the gathering data (gathering-data.json, member encounterObjects), and a game event that a task of the mode names
    // [data; agent: the mode script that makes them is not in the client]. The instance makes each one while a member has its task active
    // (look_quest_objects). The encounters of the mode wait: the tasks of the quest objects come before the tasks of the encounter (Caer
    // Dun: PlaceHeartgem, SmashHeartgem, then the kill of Umberto and the final seal).
    const gamedata::GatheringData* gathering = m_world.gathering().data();
    const gamedata::QuestData* quests = m_world.quests().data();
    if (recipes != nullptr && catalog != nullptr && gathering != nullptr && quests != nullptr) {
        for (std::uint32_t index = 0; index < recipes->instance_objects().size(); ++index) {
            const gamedata::InstanceObject& object = recipes->instance_objects()[index];
            if (!same_text(object.mode, instance.mode) || !same_text(object.map, instance.map)) {
                continue;
            }
            const std::optional<std::uint32_t> templateIndex = catalog->instance_object(object.slice);
            const std::optional<gamedata::GatheringLink> link = gathering->encounter_object(object.slice);
            if (!templateIndex || catalog->templates()[*templateIndex].kind != gamedata::InteractionKind::gathering || !link
                || link->row >= gathering->rows().size() || gathering->rows()[link->row].event.empty()) {
                continue;
            }
            const std::string event = gamedata::lower_text(gathering->rows()[link->row].event);
            const bool named = std::any_of(quests->tasks().begin(), quests->tasks().end(), [&](const gamedata::QuestTask& task) {
                return same_text(task.gameMode, instance.mode) && std::find(task.targets.begin(), task.targets.end(), event) != task.targets.end();
            });
            if (named) {
                instance.questObjects.push_back(InstanceQuestObject{index, "e:" + event, 0, false});
            }
        }
    }
    if (!instance.questObjects.empty() && encounters > 0) {
        instance.held = true;
        m_world.encounters().hold_space(instance.space, true);
    }
    instance.content = instance.content || !instance.questObjects.empty();
    log::write("world: the content of instance %llu (%s): %zu encounter(s), %zu exit portal(s), %zu quest object(s) of the mode root%s, %zu "
               "checkpoint(s)%s",
               static_cast<unsigned long long>(instance.id), instance.map.c_str(), encounters, portals, instance.questObjects.size(),
               instance.held ? " (the encounters wait for them)" : "", checkpoints,
               instance.content ? "" : "; no content: the instance ends when its last member goes");
}

void Instances::look_quest_objects(Instance& instance) {
    const ModeRootFiles files = mode_root_files(instance.space);
    const gamedata::EncounterData* recipes = files.recipes;
    const gamedata::InteractionData* catalog = files.catalog;
    if (recipes == nullptr || catalog == nullptr) {
        return;
    }
    // The members in the space of the instance (a member that left has no entity there).
    std::vector<const Entity*> present;
    for (const auto& [characterId, member] : instance.members) {
        const Entity* character = m_world.find_character(characterId);
        if (character != nullptr && character->space == instance.space) {
            present.push_back(character);
        }
    }
    bool waiting = false;
    for (InstanceQuestObject& quest : instance.questObjects) {
        const auto wanted_now = [&] {
            return std::any_of(present.begin(), present.end(), [&](const Entity* character) {
                return m_world.quests().mode_task_active(*character, instance.mode, {quest.key});
            });
        };
        if (quest.gathered) {
            if (quest.entity != 0) {
                (void)m_world.interactions().remove_object(quest.entity);
                instance.objects.erase(std::remove(instance.objects.begin(), instance.objects.end(), quest.entity), instance.objects.end());
                quest.entity = 0;
                continue;
            }
            // Code review of stage QG9d, finding QG9DR-R3: the gather ended the use of the object, not its task. When its object is gone
            // and a member has its task active again (a reset of the quest), the object comes again [agent: the gather of the object
            // completes its one task; the mode script that makes it is not in the client].
            if (!wanted_now()) {
                continue;
            }
            quest.gathered = false;
            ++m_counts.questObjectsAgain;
        }
        waiting = true;
        if (quest.entity != 0 || quest.object >= recipes->instance_objects().size()) {
            continue;
        }
        const bool wanted = wanted_now();
        const gamedata::InstanceObject& object = recipes->instance_objects()[quest.object];
        const std::optional<std::uint32_t> templateIndex = catalog->instance_object(object.slice);
        if (!wanted || !templateIndex) {
            continue;
        }
        const Interactions::SpawnedContainer made = m_world.interactions().spawn_container(
            *templateIndex, object.position, object.yaw, object.key + "/instance/" + std::to_string(instance.id), instance.space, files.own);
        if (made.entity == nullptr) {
            continue;
        }
        quest.entity = made.entity->id;
        instance.objects.push_back(made.entity->id);
        m_world.gathering().encounter_object_made(*made.entity, object.slice);
        ++m_counts.questObjectsMade;
        log::write("world: instance %llu (%s) makes its quest object %s (%s) at %.2f %.2f %.2f: a member has the task of its event",
                   static_cast<unsigned long long>(instance.id), instance.map.c_str(), object.spawner.c_str(), quest.key.c_str(),
                   static_cast<double>(object.position.x), static_cast<double>(object.position.y), static_cast<double>(object.position.z));
    }
    if (!instance.held) {
        return;
    }
    const bool later = std::any_of(present.begin(), present.end(), [&](const Entity* character) {
        return m_world.encounters().mode_recipe_task(*character, instance.space);
    });
    if (!waiting || later) {
        instance.held = false;
        m_world.encounters().hold_space(instance.space, false);
        log::write("world: the encounters of instance %llu (%s) start now: %s", static_cast<unsigned long long>(instance.id),
                   instance.map.c_str(), waiting ? "a member has an active task of a recipe of the mode" : "each quest object is gathered");
    }
}

const std::string* Instances::quest_object(EntityId object) const {
    const Entity* entity = object != 0 ? m_world.find(object) : nullptr;
    const Instance* instance = entity != nullptr ? instance_of_space(entity->space) : nullptr;
    const gamedata::EncounterData* recipes = instance != nullptr ? mode_root_files(instance->space).recipes : nullptr;
    if (instance == nullptr || recipes == nullptr) {
        return nullptr;
    }
    for (const InstanceQuestObject& quest : instance->questObjects) {
        if (quest.entity == object && !quest.gathered && quest.object < recipes->instance_objects().size()) {
            return &recipes->instance_objects()[quest.object].slice;
        }
    }
    return nullptr;
}

void Instances::quest_object_gathered(EntityId object) {
    const Entity* entity = m_world.find(object);
    const auto found = entity != nullptr ? m_bySpace.find(entity->space) : m_bySpace.end();
    if (found == m_bySpace.end()) {
        return;
    }
    Instance& instance = m_instances.at(found->second);
    for (InstanceQuestObject& quest : instance.questObjects) {
        if (quest.entity == object && !quest.gathered) {
            quest.gathered = true;
            ++m_counts.questObjectsGathered;
        }
    }
}

void Instances::member_gone(Instance& instance, const std::string& characterId, const char* cause) {
    instance.former.insert(characterId);
    if (!instance.members.empty()) {
        return;
    }
    if (instance.content) {
        const gamedata::GameMap* map = data() != nullptr ? data()->find(instance.map) : nullptr;
        const std::uint32_t timeout = map != nullptr ? map->timeoutSeconds : 0;
        if (timeout > 0) {
            // Stage QG9a [data: TimerThresholdDefault of the mode; agent: the content stays for that time]: the empty instance waits.
            instance.emptyUntil = m_world.clock().now() + std::chrono::seconds(timeout);
            log::write("world: instance %llu (%s) is empty: it ends in %u s (TimerThresholdDefault of the mode) unless a former member enters "
                       "again",
                       static_cast<unsigned long long>(instance.id), instance.map.c_str(), static_cast<unsigned>(timeout));
            return;
        }
    }
    end_instance(instance.id, cause);
}

void Instances::recover() {
    // Code review of stage DG1, finding DG1R-R5: a departure whose exit rows the database refused tries again (here: each tick and each
    // world entry), before the character can play in the main world again.
    for (auto pending = m_pendingExits.begin(); pending != m_pendingExits.end();) {
        if (write_exit(pending->second)) {
            log::write("world: the exit rows of character %s (a departure from an instance) are written now", pending->first.c_str());
            pending = m_pendingExits.erase(pending);
        } else {
            ++pending;
        }
    }
    if (m_recovered) {
        return;
    }
    const std::vector<state::InstanceMemberRow> rows = m_store.list_instance_members();
    if (rows.empty()) {
        m_recovered = true;
        return;
    }
    // One transaction: each character of a row at its return place, and no row. With no commit each row stays, and the next start tries
    // again; the position row of each character has its return place since the entry.
    state::Store::Batch batch(m_store);
    bool ok = batch.open();
    for (const state::InstanceMemberRow& row : rows) {
        // A row of a character that the store does not have (it was deleted) has no position row: its member row goes alone.
        ok = ok && (!m_store.find_character(row.characterId) || m_store.set_character_position(row.characterId, row.returnPlace))
             && m_store.delete_instance_member(row.characterId);
    }
    if (!ok || !batch.commit()) {
        // Code review of stage DG1, finding DG1R-R5: the recovery is not complete: the next tick (or world entry) tries again. Code review
        // of stage QG9a, finding QG9AR-R1: the characters of the rows do not enter the world until then (World::enter, exit_pending).
        m_unrecovered.clear();
        for (const state::InstanceMemberRow& row : rows) {
            m_unrecovered.insert(row.characterId);
        }
        log::write("world: the member rows of the instances of an earlier start (%zu) were not removed: the database did not accept the "
                   "change (the next tick tries again)",
                   rows.size());
        return;
    }
    m_recovered = true;
    m_unrecovered.clear();
    for (const state::InstanceMemberRow& row : rows) {
        ++m_counts.recovered;
        log::write("world: instance %llu (%s) of an earlier start ended with the backend stop: character %s is at its return place %.2f %.2f "
                   "%.2f",
                   static_cast<unsigned long long>(row.instanceId), row.map.c_str(), row.characterId.c_str(),
                   static_cast<double>(row.returnPlace.x), static_cast<double>(row.returnPlace.y), static_cast<double>(row.returnPlace.z));
    }
}

InstanceResult Instances::enter(const std::string& characterId, const std::string& mapId, std::uint64_t join,
                                const std::function<void()>& committed) {
    InstanceResult result;
    const auto refuse = [&](InstanceError error) {
        ++m_counts.refused;
        result.error = error;
        log::write("world: no instance entry of character %s (map %s%s): %s", characterId.c_str(), mapId.c_str(),
                   join != 0 ? (", instance " + std::to_string(join)).c_str() : "", instance_error_text(error));
        return result;
    };
    recover();
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return refuse(InstanceError::notInWorld);
    }
    if (character->space != kMainSpace || m_byCharacter.contains(characterId)) {
        return refuse(InstanceError::inInstance);
    }
    const gamedata::GameModes* modes = data();
    if (modes == nullptr) {
        return refuse(InstanceError::noData);
    }
    const gamedata::GameMap* map = modes->find(mapId);
    if (map == nullptr) {
        return refuse(InstanceError::unknownMap);
    }
    const Instance* joined = nullptr;
    if (join != 0) {
        joined = find(join);
        if (joined == nullptr) {
            return refuse(InstanceError::unknownInstance);
        }
        if (joined->map != map->id) {
            return refuse(InstanceError::wrongMap);
        }
        if (joined->members.size() >= joined->capacity) {
            return refuse(InstanceError::full);
        }
    }
    // Code review of stage DG1, finding DG1R-R4: the return place must be a place of a travel (the move of the exit needs it): checked before
    // any row.
    if (!travel_position(character->position)) {
        return refuse(InstanceError::badReturnPlace);
    }
    // The return place: the place of the character now, in the main world.
    InstanceMember member;
    member.characterId = characterId;
    member.returnPlace = character->position;
    member.entered = m_world.clock().now();
    const std::uint64_t id = joined != nullptr ? joined->id : m_nextInstance;
    // One transaction (rule MP-7): the position row gets the return place, and the member row. With no commit nothing changes.
    {
        state::Store::Batch batch(m_store);
        state::InstanceMemberRow row;
        row.characterId = characterId;
        row.instanceId = id;
        row.map = map->id;
        row.returnPlace = member.returnPlace;
        row.enteredNs = m_world.clock().wall_time_ns(member.entered);
        if (!batch.open() || !m_store.set_character_position(characterId, member.returnPlace) || !m_store.write_instance_member(row)
            || !batch.commit()) {
            return refuse(InstanceError::storage);
        }
    }
    // Code review of stage QG9a, finding QG9AR-R6: the rows are in the store: the caller sends its result now (3094 Success of the entrance),
    // before the level change of the move.
    if (committed) {
        committed();
    }
    Instance* instance = nullptr;
    bool made = false;
    if (joined != nullptr) {
        instance = &m_instances.at(joined->id);
        // Stage QG9a: a join of an empty instance stops its timer.
        instance->emptyUntil.reset();
        instance->former.erase(characterId);
    } else {
        made = true;
        ++m_nextInstance;
        Instance made;
        made.id = id;
        made.space = m_nextSpace++;
        made.map = map->id;
        made.mode = map->mode;
        made.level = SpaceLevel{map->level, map->terrain};
        made.start = map->starts.front().position;
        made.heading = map->starts.front().heading;
        made.capacity = map->teamCapacity == 0 ? 1 : map->teamCapacity;
        made.started = member.entered;
        const ActorId modeActor = m_world.reserve_actor();
        made.modeActor = modeActor.bytes;
        made.modeEntity = entity_group_id(modeActor);
        instance = &m_instances.emplace(id, std::move(made)).first->second;
        m_bySpace[instance->space] = id;
        ++m_counts.made;
        log::write("world: instance %llu of map %s (mode %s, level %s) made: space %u, start %.2f %.2f %.2f, %u member place(s)",
                   static_cast<unsigned long long>(id), instance->map.c_str(), instance->mode.c_str(), instance->level.level.c_str(),
                   instance->space, static_cast<double>(instance->start.x), static_cast<double>(instance->start.y),
                   static_cast<double>(instance->start.z), instance->capacity);
        // Stage DG2: a space of an own level gets the content of the data set of its level (the first space reads the set), before the
        // move: the character comes to the floors of its level.
        m_world.levels().space_started(instance->space, instance->level.level);
    }
    instance->members[characterId] = member;
    m_byCharacter[characterId] = id;
    ++m_counts.entries;
    {
        const std::string mark = log::session_mark(character->owner, characterId);
        const log::Context context(mark);
        log::write("world: character %s enters instance %llu (%s): return place %.2f %.2f %.2f, %zu member(s)", characterId.c_str(),
                   static_cast<unsigned long long>(id), instance->map.c_str(), static_cast<double>(member.returnPlace.x),
                   static_cast<double>(member.returnPlace.y), static_cast<double>(member.returnPlace.z), instance->members.size());
    }
    result.instance = *instance;
    result.place = instance->start;
    const SpaceMoveError moved = m_world.move_to_space(characterId, instance->space, instance->start);
    if (moved != SpaceMoveError::none) {
        // Not possible after the checks above (the space is there, the start is a place of the data). The rows have the return place: the
        // exit puts the character back.
        log::write("world: the move of character %s into instance %llu failed (%d)", characterId.c_str(), static_cast<unsigned long long>(id),
                   static_cast<int>(moved));
    }
    // Stage QG9a: the content of the mode of a new instance (its encounters, its exit portal), after the move: the participants are in
    // the space. Then the membership of the character (TaskGameMode).
    if (made) {
        start_content(m_instances.at(id));
    }
    membership_changed(characterId);
    return result;
}

bool Instances::write_exit(const InstanceMember& member) {
    state::Store::Batch batch(m_store);
    return batch.open() && m_store.set_character_position(member.characterId, member.returnPlace)
           && m_store.delete_instance_member(member.characterId) && batch.commit();
}

InstanceResult Instances::leave(const std::string& characterId, InstanceLeave cause) {
    InstanceResult result;
    recover();
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        ++m_counts.refused;
        result.error = InstanceError::notInWorld;
        return result;
    }
    const auto owned = m_byCharacter.find(characterId);
    if (owned == m_byCharacter.end()) {
        ++m_counts.refused;
        result.error = InstanceError::notInInstance;
        return result;
    }
    Instance& instance = m_instances.at(owned->second);
    const InstanceMember member = instance.members.at(characterId);
    // Code review of stage DG1, finding DG1R-R4: the move of the exit must be possible before its rows and the end of the membership (a
    // return place of a row of an earlier backend can be outside the travel limits).
    if (!travel_position(member.returnPlace) || !m_world.has_space(kMainSpace)) {
        ++m_counts.refused;
        result.error = InstanceError::badReturnPlace;
        log::write("world: no instance exit of character %s: %s", characterId.c_str(), instance_error_text(result.error));
        return result;
    }
    // One transaction (rule MP-7): the position row gets the return place, and the member row goes. With no commit nothing changes.
    if (!write_exit(member)) {
        ++m_counts.refused;
        result.error = InstanceError::storage;
        log::write("world: no instance exit of character %s: %s", characterId.c_str(), instance_error_text(result.error));
        return result;
    }
    const std::uint64_t id = instance.id;
    instance.members.erase(characterId);
    m_byCharacter.erase(owned);
    ++m_counts.exits;
    m_counts.portalLeaves += cause == InstanceLeave::exitPortal ? 1 : 0;
    m_counts.requestLeaves += cause == InstanceLeave::request ? 1 : 0;
    result.instance = instance;
    result.place = member.returnPlace;
    {
        const std::string mark = log::session_mark(character->owner, characterId);
        const log::Context context(mark);
        log::write("world: character %s leaves instance %llu (%s) by %s to its return place %.2f %.2f %.2f, %zu member(s) stay",
                   characterId.c_str(), static_cast<unsigned long long>(id), instance.map.c_str(), instance_leave_text(cause),
                   static_cast<double>(member.returnPlace.x), static_cast<double>(member.returnPlace.y),
                   static_cast<double>(member.returnPlace.z), instance.members.size());
    }
    const SpaceMoveError moved = m_world.move_to_space(characterId, kMainSpace, member.returnPlace);
    if (moved != SpaceMoveError::none) {
        log::write("world: the move of character %s back to the main world failed (%d)", characterId.c_str(), static_cast<int>(moved));
    }
    member_gone(instance, characterId, "its last member left");
    result.ended = !m_instances.contains(id);
    membership_changed(characterId);
    return result;
}

void Instances::character_left(const Entity& character) {
    const auto owned = m_byCharacter.find(character.characterId);
    if (owned == m_byCharacter.end()) {
        return;
    }
    Instance& instance = m_instances.at(owned->second);
    const InstanceMember member = instance.members.at(character.characterId);
    // Decision D9: the next world entry of the character is at its return place. A transaction that the database did not accept keeps the
    // member row: the recovery of the next start removes it (the position row has the return place since the entry).
    const bool written = write_exit(member);
    if (!written) {
        // Code review of stage DG1, finding DG1R-R5: the exit rows try again (recover) until the database takes them.
        m_pendingExits[character.characterId] = member;
    }
    const std::uint64_t id = instance.id;
    instance.members.erase(character.characterId);
    m_byCharacter.erase(owned);
    ++m_counts.departures;
    log::write("world: character %s left the world in instance %llu (%s): its next world entry is at its return place %.2f %.2f %.2f%s",
               character.characterId.c_str(), static_cast<unsigned long long>(id), instance.map.c_str(),
               static_cast<double>(member.returnPlace.x), static_cast<double>(member.returnPlace.y), static_cast<double>(member.returnPlace.z),
               written ? "" : " (the rows were not written: the next tick and the next world entry try again)");
    member_gone(instance, character.characterId, "its last member left the world");
}

void Instances::end_instance(std::uint64_t id, const char* cause) {
    const auto found = m_instances.find(id);
    if (found == m_instances.end()) {
        return;
    }
    // An entity of its space that is not a member and not content is a probe of a command or a spell (each member left, and the spells
    // and projectiles of a member end with its move). The space goes with the instance: a later instance gets a new one.
    const SpaceId space = found->second.space;
    // Stage QG9a: the content goes first, by its own systems (the encounters of the space and the objects of the instance).
    m_world.encounters().hold_space(space, false);
    m_world.encounters().end_space(space);
    for (const EntityId object : found->second.objects) {
        (void)m_world.interactions().remove_object(object);
    }
    const std::size_t removed = m_world.clear_space(space);
    // Stage DG3: the objects of the level set of the space go (their states are of this space).
    m_world.interactions().space_ended(space);
    // Stage DG2: the spawn points of the space go; the data of its level stays for a later instance.
    m_world.levels().space_ended(space);
    log::write("world: instance %llu (%s) ended: %s; space %u is free (%zu other entit%s removed)", static_cast<unsigned long long>(id),
               found->second.map.c_str(), cause, space, removed, removed == 1 ? "y" : "ies");
    m_bySpace.erase(space);
    m_instances.erase(found);
    ++m_counts.ended;
}

} // namespace soulwarden::backend::world
