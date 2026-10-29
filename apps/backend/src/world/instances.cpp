#include "world/instances.h"

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {

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
    }
    return "unknown";
}

Instances::Instances(World& world, state::Store& store, const gamedata::GameModes* data) : m_world(world), m_store(store), m_data(data) {}

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
}

void Instances::recover() {
    if (m_recovered) {
        return;
    }
    m_recovered = true;
    const std::vector<state::InstanceMemberRow> rows = m_store.list_instance_members();
    if (rows.empty()) {
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
        log::write("world: the member rows of the instances of an earlier start (%zu) were not removed: the database did not accept the "
                   "change",
                   rows.size());
        return;
    }
    for (const state::InstanceMemberRow& row : rows) {
        ++m_counts.recovered;
        log::write("world: instance %llu (%s) of an earlier start ended with the backend stop: character %s is at its return place %.2f %.2f "
                   "%.2f",
                   static_cast<unsigned long long>(row.instanceId), row.map.c_str(), row.characterId.c_str(),
                   static_cast<double>(row.returnPlace.x), static_cast<double>(row.returnPlace.y), static_cast<double>(row.returnPlace.z));
    }
}

InstanceResult Instances::enter(const std::string& characterId, const std::string& mapId, std::uint64_t join) {
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
    Instance* instance = nullptr;
    if (joined != nullptr) {
        instance = &m_instances.at(joined->id);
    } else {
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
        instance = &m_instances.emplace(id, std::move(made)).first->second;
        m_bySpace[instance->space] = id;
        ++m_counts.made;
        log::write("world: instance %llu of map %s (mode %s, level %s) made: space %u, start %.2f %.2f %.2f, %u member place(s)",
                   static_cast<unsigned long long>(id), instance->map.c_str(), instance->mode.c_str(), instance->level.level.c_str(),
                   instance->space, static_cast<double>(instance->start.x), static_cast<double>(instance->start.y),
                   static_cast<double>(instance->start.z), instance->capacity);
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
    return result;
}

bool Instances::write_exit(const InstanceMember& member) {
    state::Store::Batch batch(m_store);
    return batch.open() && m_store.set_character_position(member.characterId, member.returnPlace)
           && m_store.delete_instance_member(member.characterId) && batch.commit();
}

InstanceResult Instances::leave(const std::string& characterId) {
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
    result.instance = instance;
    result.place = member.returnPlace;
    {
        const std::string mark = log::session_mark(character->owner, characterId);
        const log::Context context(mark);
        log::write("world: character %s leaves instance %llu (%s) to its return place %.2f %.2f %.2f, %zu member(s) stay", characterId.c_str(),
                   static_cast<unsigned long long>(id), instance.map.c_str(), static_cast<double>(member.returnPlace.x),
                   static_cast<double>(member.returnPlace.y), static_cast<double>(member.returnPlace.z), instance.members.size());
    }
    const SpaceMoveError moved = m_world.move_to_space(characterId, kMainSpace, member.returnPlace);
    if (moved != SpaceMoveError::none) {
        log::write("world: the move of character %s back to the main world failed (%d)", characterId.c_str(), static_cast<int>(moved));
    }
    if (instance.members.empty()) {
        end_instance(id, "its last member left");
        result.ended = true;
    }
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
    const std::uint64_t id = instance.id;
    instance.members.erase(character.characterId);
    m_byCharacter.erase(owned);
    ++m_counts.departures;
    log::write("world: character %s left the world in instance %llu (%s): its next world entry is at its return place %.2f %.2f %.2f%s",
               character.characterId.c_str(), static_cast<unsigned long long>(id), instance.map.c_str(),
               static_cast<double>(member.returnPlace.x), static_cast<double>(member.returnPlace.y), static_cast<double>(member.returnPlace.z),
               written ? "" : " (the rows were not written: the next start removes the member row)");
    if (instance.members.empty()) {
        end_instance(id, "its last member left the world");
    }
}

void Instances::end_instance(std::uint64_t id, const char* cause) {
    const auto found = m_instances.find(id);
    if (found == m_instances.end()) {
        return;
    }
    // This stage: an instance has no content; an entity of its space that is not a member is a probe of a command or a spell (each member
    // left, and the spells and projectiles of a member end with its move). The space goes with the instance: a later instance gets a new one.
    const SpaceId space = found->second.space;
    const std::size_t removed = m_world.clear_space(space);
    log::write("world: instance %llu (%s) ended: %s; space %u is free (%zu other entit%s removed)", static_cast<unsigned long long>(id),
               found->second.map.c_str(), cause, space, removed, removed == 1 ? "y" : "ies");
    m_bySpace.erase(space);
    m_instances.erase(found);
    ++m_counts.ended;
}

} // namespace soulwarden::backend::world
