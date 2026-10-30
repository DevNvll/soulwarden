#include "world/levels.h"

#include "core/logging/log.h"
#include "world/instances.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

std::string part_text(const gamedata::LevelPart& part) {
    if (part.loaded) {
        return std::to_string(part.bytes) + " bytes";
    }
    return "none (" + part.problem + ")";
}

} // namespace

Levels::Levels(World& world, const gamedata::LevelSource* source, bool navigationThread)
    : m_world(world), m_source(source), m_navigationThread(navigationThread) {}

Levels::~Levels() = default;

Level& Levels::read(const std::string& level) {
    auto found = m_levels.find(level);
    if (found != m_levels.end()) {
        return found->second;
    }
    // The first use of the level: the world thread reads its set now (the load case LV5), one time for each backend process.
    Level entry;
    entry.name = level;
    const auto started = std::chrono::steady_clock::now();
    if (m_source != nullptr) {
        entry.data = m_source->open(level);
    }
    entry.loadTime = std::chrono::steady_clock::now() - started;
    entry.loadedAt = m_world.clock().now();
    const double milliseconds = std::chrono::duration<double, std::milli>(entry.loadTime).count();
    if (entry.data == nullptr) {
        ++m_counts.noSet;
        log::write("world: level %s has no data set (make it with tools/gamedata/level.sh): its instances have no content", level.c_str());
    } else if (!entry.data->accepted()) {
        ++m_counts.reads;
        ++m_counts.refused;
        log::write("world: level %s: its data set is refused (%s): its instances have no content", level.c_str(), entry.data->problem().c_str());
    } else {
        ++m_counts.reads;
        const gamedata::LevelData& data = *entry.data;
        log::write("world: level %s: its data set is read in %.1f ms: creatures %s (%zu spawn points, %zu kinds, %zu slices), navigation %s "
                   "(%zu regions), heights %s, interactions %s (%zu objects, %zu checkpoints)",
                   level.c_str(), milliseconds, part_text(data.creature_part()).c_str(), data.creatures().spawn_points().size(),
                   data.creatures().kinds().size(), data.creatures().slices().size(), part_text(data.navigation_part()).c_str(),
                   data.navigation().regions(), part_text(data.height_part()).c_str(), part_text(data.interaction_part()).c_str(),
                   data.interactions().objects().size(), data.interactions().checkpoints().size());
        if (data.navigation_part().loaded || data.height_part().loaded) {
            entry.navigation = std::make_unique<Navigation>(m_world, data.navigation_part().loaded ? &entry.data->navigation() : nullptr,
                                                            data.height_part().loaded ? &entry.data->heights() : nullptr, m_navigationThread,
                                                            level);
        }
    }
    return m_levels.emplace(level, std::move(entry)).first->second;
}

bool Levels::available(const std::string& level) {
    if (level.empty() || level == kMainLevel) {
        return false;
    }
    const Level& entry = read(level);
    return entry.data != nullptr && entry.data->accepted();
}

void Levels::space_started(SpaceId space, const std::string& level) {
    if (space == kMainSpace || level.empty() || level == kMainLevel) {
        return;
    }
    m_spaces[space] = level;
    ++m_counts.spacesStarted;
    Level& found = read(level);
    found.spaces.insert(space);
    const gamedata::LevelData* data = found.data.get();
    if (data != nullptr && data->accepted() && data->creature_part().loaded) {
        if (m_world.creatures().has_data()) {
            m_world.creatures().level_space_started(space, found.data->creatures());
        } else {
            // [agent] The creatures of a level use the rules of the creatures of the backend (its level table, its behavior): with no
            // creature data of the backend the world has no creature.
            log::write("world: level %s: the backend has no creature data: the spawn points of the level make no creature", level.c_str());
        }
    }
}

void Levels::space_ended(SpaceId space) {
    const auto found = m_spaces.find(space);
    if (found == m_spaces.end()) {
        return;
    }
    const auto level = m_levels.find(found->second);
    if (level != m_levels.end()) {
        level->second.spaces.erase(space);
    }
    m_world.creatures().level_space_ended(space);
    m_spaces.erase(found);
    ++m_counts.spacesEnded;
}

const Level* Levels::level_of_space(SpaceId space) const {
    const auto found = m_spaces.find(space);
    if (found == m_spaces.end()) {
        return nullptr;
    }
    const auto level = m_levels.find(found->second);
    return level != m_levels.end() ? &level->second : nullptr;
}

const gamedata::LevelData* Levels::data_of(SpaceId space) const {
    const Level* level = level_of_space(space);
    return level != nullptr && level->data != nullptr && level->data->accepted() ? level->data.get() : nullptr;
}

Navigation* Levels::navigation_of(SpaceId space) {
    const Level* level = level_of_space(space);
    return level != nullptr ? level->navigation.get() : nullptr;
}

const Navigation* Levels::navigation_of(SpaceId space) const {
    const Level* level = level_of_space(space);
    return level != nullptr ? level->navigation.get() : nullptr;
}

void Levels::tick() {
    for (auto& [name, level] : m_levels) {
        // A level with no space keeps its tiles until their drop time; its paths end with their creatures. Code review of stage DG2,
        // finding DG2R-R5: a level with no space and no work (no tile, no route, no search) gets no tick: its tick would scan the characters
        // of the world each 500 ms for no tile. The data of the level stays.
        if (level.navigation != nullptr && (!level.spaces.empty() || level.navigation->busy())) {
            level.navigation->tick();
        }
    }
}

} // namespace soulwarden::backend::world
