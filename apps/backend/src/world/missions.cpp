#include "world/missions.h"

#include <algorithm>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "gamedata/quest_data.h"
#include "world/interactions.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The service kind of a board (Interactions::grant_service). */
constexpr const char* kBoardService = "missionBoard";
/** The option of a board (InteractionUIActions 51, OpenObjectiveProvider; research mission-boards.md 2.1). */
constexpr const char* kBoardOption = "OpenMissionBoardOption";
/** The tag of the return task of the mission templates (CM_ReturnToGiver, InteractTag ReturnToGiver; factory 0x4461150, case 23). */
constexpr const char* kReturnTag = "returntogiver";
constexpr std::uint64_t kMinuteNs = 60'000'000'000ULL;

/** @return The interval of the client: the float clamped to 1 or more (MAXSS), then truncated to a whole number of minutes (CVTTSS2SI). */
std::int64_t interval_minutes(float interval) {
    const float clamped = !(interval >= 1.0F) ? 1.0F : interval;
    return clamped < 2147483648.0F ? static_cast<std::int64_t>(clamped) : 1;
}

/** The town project table of the mission data (MissionTable::town) is the only source of the offers of a town board. */
bool town_row(const gamedata::MissionRow& row) {
    return row.table == gamedata::MissionTable::town;
}

} // namespace

std::uint32_t town_seed(std::string_view text, std::uint16_t territory, std::uint64_t serverNs, float intervalMinutes, std::uint32_t addition) {
    // 0x39D2A70: the server clock minute (signed division of the nanoseconds), plus half of the interval, minus its remainder (the signed
    // 32-bit arithmetic of the client), plus the territory and the FNV-1a 64 of the text; 0x3A36220 adds the seed addition (u32 wrap).
    const std::int64_t p = interval_minutes(intervalMinutes);
    const std::int64_t minute = static_cast<std::int64_t>(serverNs) / static_cast<std::int64_t>(kMinuteNs);
    const auto phase = static_cast<std::int32_t>(static_cast<std::uint32_t>(minute + p / 2));
    const std::int32_t remainder = static_cast<std::int32_t>(phase % static_cast<std::int32_t>(p));
    const auto rounded = static_cast<std::int32_t>(static_cast<std::uint32_t>(phase) - static_cast<std::uint32_t>(remainder));
    std::uint64_t hash = 0xCBF29CE484222325ULL;
    for (const char letter : text) {
        hash = (hash ^ static_cast<std::uint8_t>(letter)) * 0x100000001B3ULL;
    }
    return static_cast<std::uint32_t>(hash + territory + static_cast<std::uint64_t>(static_cast<std::int64_t>(rounded)) + addition);
}

std::uint64_t seconds_to_next_seed(std::uint64_t serverNs, float intervalMinutes) {
    // 0x4957F20: whole minutes m and the seconds s in the minute; 60 * (P - ((m + P/2) % P)) - s. At a boundary it gives P minutes.
    const std::int64_t p = interval_minutes(intervalMinutes);
    const std::int64_t minute = static_cast<std::int64_t>(serverNs / kMinuteNs);
    const std::int64_t second = static_cast<std::int64_t>((serverNs % kMinuteNs) / 1'000'000'000ULL);
    return static_cast<std::uint64_t>(60 * (p - ((minute + p / 2) % p)) - second);
}

std::uint64_t next_seed_ns(std::uint64_t serverNs, float intervalMinutes) {
    const std::int64_t p = interval_minutes(intervalMinutes);
    const std::uint64_t minute = serverNs / kMinuteNs;
    const std::uint64_t phase = (minute + static_cast<std::uint64_t>(p / 2)) % static_cast<std::uint64_t>(p);
    return (minute + static_cast<std::uint64_t>(p) - phase) * kMinuteNs;
}

const char* mission_error_text(MissionError error) {
    switch (error) {
    case MissionError::none:
        return "none";
    case MissionError::noData:
        return "no mission data or no quest data";
    case MissionError::notInWorld:
        return "the character is not in the world";
    case MissionError::noProvider:
        return "no service of this board (no open, another board, the range, a new entry, or the board went away)";
    case MissionError::notOffered:
        return "not an offer of the current seed of the board";
    case MissionError::unavailable:
        return "an offer that a hand-in of this seed made unavailable";
    case MissionError::alreadyActive:
        return "the character has an active instance of this mission";
    case MissionError::unsupported:
        return "the mission has a rule that the backend cannot run";
    case MissionError::unknownInstance:
        return "no active mission of the character with this board as its provider";
    case MissionError::questRefused:
        return "the quest system refused";
    case MissionError::storeFailed:
        return "the database did not take the change";
    }
    return "unknown";
}

/** The handler of OpenMissionBoardOption of the kind missionBoard, and the observer of the spawns of the boards. */
class Missions::BoardHandler final : public InteractionHandler, public SpawnObserver {
public:
    BoardHandler(World& world, Missions& missions) : m_world(world), m_missions(missions) {}

    [[nodiscard]] const char* name() const override {
        return "mission board";
    }

    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        (void)character;
        // A board is a level object that the client makes (its entity has its TerritoryInterface slot: Interactions::make_boards); with no
        // mission data the board offers nothing: refused.
        const Entity* board = m_world.interactions().source_entity(context);
        if (m_missions.data() == nullptr || board == nullptr || board->kind != EntityKind::object || context.object == nullptr) {
            return InteractionVerdict::refused;
        }
        return InteractionVerdict::accepted;
    }

    void started(Entity& character, const InteractionContext& context) override {
        const Entity* board = m_world.interactions().source_entity(context);
        if (board != nullptr && context.object != nullptr) {
            (void)m_missions.opened(character, context.object->key, *board);
        }
    }

    void child_spawned(const std::string& key, Entity& entity) override {
        m_missions.child_spawned(key, entity);
    }

private:
    World& m_world;
    Missions& m_missions;
};

Missions::Missions(World& world, state::Store& store, const gamedata::MissionData* data)
    : m_world(world), m_store(store), m_data(data), m_handler(std::make_unique<BoardHandler>(world, *this)) {
    m_world.interactions().register_handler(gamedata::InteractionKind::missionBoard, gamedata::interaction_option_id(kBoardOption), *m_handler);
    m_world.interactions().register_spawn_observer(*m_handler);
}

Missions::~Missions() = default;

const Entity* Missions::board_of(const std::string& key) const {
    const EntityId id = key.empty() ? 0 : m_world.interactions().board_entity(key);
    return id != 0 ? m_world.find(id) : nullptr;
}

std::uint16_t Missions::territory_of(const Entity& board) const {
    return board.object.territory != 0 ? board.object.territory : m_world.territory_of(board);
}

void Missions::entered(Entity& entity) {
    MissionBoardState state;
    if (data() != nullptr) {
        for (const state::MissionOfferRow& row : m_store.list_mission_offers(entity.characterId)) {
            state.towns[row.territory] = MissionTownEntry{row.seed, row.unavailable, row.previousProjects};
        }
        state.loaded = true;
    }
    entity.missionBoard = std::move(state);
    // The destinations of the missions of the character: the live boards of their providers (a value of the world).
    for (QuestInstance& quest : entity.quests.active) {
        if (quest.mission == 0 || quest.provider.empty()) {
            continue;
        }
        m_byProvider[quest.provider].insert(entity.id);
        const Entity* board = board_of(quest.provider);
        quest.destination = board != nullptr ? board->object.providerIdentity : std::array<std::uint8_t, 16>{};
    }
}

void Missions::left(const Entity& entity) {
    for (auto it = m_byProvider.begin(); it != m_byProvider.end();) {
        it->second.erase(entity.id);
        it = it->second.empty() ? m_byProvider.erase(it) : std::next(it);
    }
}

std::vector<std::uint32_t> Missions::town_projects(std::uint16_t territory) const {
    (void)territory;
    // [agent] docs/systems/mission-boards.md, the offer rule: the backend has no town project state yet (roadmap 5.4); the board of each
    // town gives the offers of the always available project (ProjectType AlwaysAvailable: CommunityProjectId, cost 0, progress 0).
    std::vector<std::uint32_t> projects;
    if (const gamedata::MissionData* missions = data()) {
        for (const gamedata::MissionProject& project : missions->projects()) {
            if (project.type == "AlwaysAvailable") {
                projects.push_back(project.crc);
            }
        }
    }
    return projects;
}

bool Missions::candidate(const gamedata::MissionRow& row, const TownInputs& inputs, const std::set<std::uint32_t>& allowed,
                         const Entity* character) const {
    // The provider filter of the initialization (0x3A34140 -> 0x3A25210 -> 0x39D3EC0: the territory list and its exclusion flag) and the
    // candidate filter 0x3A09900: the project of the row is an admitted project, the level bounds (0: no bound), the trade skill rank, and
    // the achievement conditional.
    if (!town_row(row) || !row.enabled || allowed.count(row.projectCrc) == 0) {
        return false;
    }
    if (!row.territories.empty()) {
        const bool listed = std::find(row.territories.begin(), row.territories.end(), inputs.territory) != row.territories.end();
        if (listed == row.excludeTerritories) {
            return false;
        }
    }
    if ((row.minLevel != 0 && row.minLevel > inputs.level) || (row.maxLevel != 0 && row.maxLevel < inputs.level)) {
        return false;
    }
    if (!row.tradeskill.empty()
        && (character == nullptr || character->skills.rank(gamedata::progression_id(row.tradeskill)) < row.tradeskillLevel)) {
        return false;
    }
    if (!row.achievementConditional.empty() && (character == nullptr || !m_world.quests().conditional(*character, row.achievementConditional))) {
        return false;
    }
    return true;
}

TownOffers Missions::town_offers(const TownInputs& inputs, const MissionTownEntry* entry, const Entity* character) const {
    TownOffers result;
    const gamedata::MissionData* missions = data();
    result.seed = town_seed(inputs.seedText, inputs.territory, inputs.serverNs, town_rule::kIntervalMinutes, town_rule::kSeedAddition);
    if (missions == nullptr) {
        return result;
    }
    ++m_counts.offerRuns;
    for (const std::uint32_t projectCrc : inputs.projects) {
        const gamedata::MissionProject* active = missions->project(projectCrc);
        if (active == nullptr) {
            continue;
        }
        // 0x3A362A0, step 1: the project definitions of the same type with a level no greater than the level of the active project.
        std::set<std::uint32_t> allowed;
        for (const gamedata::MissionProject& project : missions->projects()) {
            if (project.type == active->type && project.level <= active->level) {
                allowed.insert(project.crc);
            }
        }
        // Step 4: the LCG from projectId + currentSeed (u32), xor the multiplier (the seed of java.util.Random).
        std::uint64_t random = ((static_cast<std::uint64_t>(static_cast<std::uint32_t>(projectCrc + result.seed))) ^ kTownLcgMultiplier) & kTownLcgMask;
        for (const gamedata::TownBucket& bucket : missions->town_buckets()) {
            std::vector<std::uint32_t> candidates;
            for (const std::uint32_t mission : bucket.missions) {
                ++m_counts.candidateVisits;
                const gamedata::MissionRow* row = missions->mission(mission);
                if (row != nullptr && candidate(*row, inputs, allowed, character)) {
                    candidates.push_back(mission);
                }
            }
            if (candidates.empty()) {
                continue;
            }
            random = (random * kTownLcgMultiplier + 11) & kTownLcgMask;
            const auto index = static_cast<std::uint32_t>((random >> 16) % candidates.size());
            TownOffer offer;
            offer.mission = candidates[index];
            offer.bucket = bucket.key;
            offer.index = index;
            offer.candidates = static_cast<std::uint32_t>(candidates.size());
            offer.project = projectCrc;
            // 0x3A3684C: unavailable only when the stored seed is the current seed and the mission is in the list.
            offer.available = entry == nullptr || entry->seed != result.seed
                              || std::find(entry->unavailable.begin(), entry->unavailable.end(), offer.mission) == entry->unavailable.end();
            result.offers.push_back(offer);
        }
    }
    return result;
}

TownOffers Missions::offers_of(const Entity& character, std::uint16_t territory) const {
    TownInputs inputs;
    inputs.seedText = character.characterId;
    inputs.territory = territory;
    inputs.serverNs = m_world.clock().server_time_ns();
    inputs.projects = town_projects(territory);
    inputs.level = character.progression.level == 0 ? 0 : character.progression.level - 1;
    const auto entry = character.missionBoard.towns.find(territory);
    return town_offers(inputs, entry != character.missionBoard.towns.end() ? &entry->second : nullptr, &character);
}

bool Missions::opened(Entity& character, const std::string& boardKey, const Entity& board) {
    if (data() == nullptr || !character.missionBoard.loaded) {
        return false;
    }
    ++m_counts.opens;
    const std::uint64_t now = m_world.clock().server_time_ns();
    const std::uint16_t territory = territory_of(board);
    const TownOffers offers = offers_of(character, territory);
    MissionBoardState& state = character.missionBoard;
    state.opened = true;
    // The provider identity: the Claim landmark of the territory of the board (the provider component keeps it at +0x108, 0x3A47881;
    // CanCompleteMission 0x4954180 compares it with the destination of the return task) [client: DESTINATION.md].
    state.provider = board.object.providerIdentity;
    state.providerKey = boardKey;
    state.territory = territory;
    // [agent] the expiration of the provider data: the start of the next seed (the offers of the provider change there).
    state.expirationNs = next_seed_ns(now, town_rule::kIntervalMinutes);
    // The town entry of the territory: the stored seed and its unavailable missions; a town with no entry gets one of the current seed with
    // empty lists (the client generates the offers of a territory from its entry: 0x4955A90).
    if (state.towns.find(territory) == state.towns.end()) {
        state.towns[territory] = MissionTownEntry{offers.seed, {}, town_projects(territory)};
    }
    m_world.touch(character.id, ComponentKey::objectiveInteractor);
    std::vector<std::uint32_t> ids;
    for (const TownOffer& offer : offers.offers) {
        ids.push_back(offer.mission);
    }
    std::sort(ids.begin(), ids.end());
    if (ids.empty()) {
        ids.push_back(0); // A grant needs an id; 0 is no mission.
    }
    (void)m_world.interactions().grant_service(character, kBoardService, ids, offers.seed);
    std::string list;
    for (const TownOffer& offer : offers.offers) {
        const gamedata::MissionRow* row = m_data->mission(offer.mission);
        list += (list.empty() ? "" : ", ") + (row != nullptr ? row->id : std::to_string(offer.mission)) + (offer.available ? "" : " (unavailable)");
    }
    log::write("missions: character %s opened the board %s (territory %u): seed %08x, %llu s to the next seed, offers [%s]",
               character.characterId.c_str(), boardKey.c_str(), static_cast<unsigned>(territory), offers.seed,
               static_cast<unsigned long long>(seconds_to_next_seed(now, town_rule::kIntervalMinutes)), list.c_str());
    return true;
}

void Missions::complete_return(Entity& character, const QuestInstance& quest) {
    const gamedata::QuestData* quests = m_world.quests().data();
    const gamedata::QuestObjective* objective = quests != nullptr ? quests->objective_by_crc(quest.objectiveCrc, quest.mission) : nullptr;
    QuestEvent event;
    event.kind = QuestEventKind::interact;
    event.keys = {kReturnTag};
    event.restricted = true;
    for (std::size_t index = 0; objective != nullptr && index < quest.tasks.size(); ++index) {
        const gamedata::QuestNode& node = objective->nodes[index];
        if (node.task != gamedata::kNoTask && quests->task(node.task).type == gamedata::task_type::kInteract
            && quests->task(node.task).interactTag == kReturnTag && quest.tasks[index].state == quest_state::kActive) {
            event.matched.emplace_back(quest.instance, static_cast<std::uint8_t>(index));
        }
    }
    if (!event.matched.empty()) {
        const QuestEventOutcome outcome = m_world.quests().event(character, event);
        m_counts.returns += outcome.result == QuestEventResult::credited ? event.matched.size() : 0;
    }
}

MissionResult Missions::accept(Entity& character, std::uint32_t mission, std::uint32_t destination) {
    MissionResult result;
    const auto refuse = [&](MissionError error, std::string detail) {
        result.error = error;
        result.detail = std::move(detail);
        ++m_counts.acceptRefusals;
        log::write("missions: the accept of the mission %08x by character %s is refused: %s (%s)", mission, character.characterId.c_str(),
                   mission_error_text(error), result.detail.c_str());
        return result;
    };
    const gamedata::QuestData* quests = m_world.quests().data();
    if (data() == nullptr || quests == nullptr || !character.missionBoard.loaded) {
        return refuse(MissionError::noData, "no data");
    }
    const MissionBoardState& state = character.missionBoard;
    if (!state.opened || !character.serviceGrant || character.serviceGrant->kind != kBoardService) {
        return refuse(MissionError::noProvider, "no open board");
    }
    // The offers of the current seed (the client computes the same list; a seed change since the open gives the service the new offers).
    const TownOffers offers = offers_of(character, state.territory);
    const auto offer = std::find_if(offers.offers.begin(), offers.offers.end(), [&](const TownOffer& value) { return value.mission == mission; });
    if (offer == offers.offers.end()) {
        return refuse(MissionError::notOffered, "the seed " + std::to_string(offers.seed) + " of the territory " + std::to_string(state.territory)
                                                    + " does not offer it");
    }
    if (!offer->available) {
        return refuse(MissionError::unavailable, "handed in for this seed");
    }
    if (character.serviceGrant->revision != offers.seed) {
        std::vector<std::uint32_t> ids;
        for (const TownOffer& value : offers.offers) {
            ids.push_back(value.mission);
        }
        std::sort(ids.begin(), ids.end());
        m_world.interactions().update_service(character, ids);
    }
    if (const ServiceError error = m_world.interactions().require_service(character.characterId, kBoardService, mission);
        error != ServiceError::none) {
        return refuse(MissionError::noProvider, service_error_text(error));
    }
    if (destination != 0 && destination != offer->project) {
        // The second argument of 3819: the card of the town screen sends the project of the card (townproject_activeproject.luac
        // OnTaskStart: RequestCommunityGoalSelection(missionId, projectId); live: 0x2591C412, CommunityProjectId), and the detail popup 0
        // (townprojects.luac OnTaskDetailCallback) [client]. Another value is not the project of the offer: refused [agent].
        return refuse(MissionError::notOffered, "the project argument " + std::to_string(destination) + " is not the project of the offer");
    }
    const gamedata::MissionBinding* binding = quests->mission(mission);
    if (binding == nullptr || binding->objective == nullptr || !binding->refusals.empty()) {
        return refuse(MissionError::unsupported, binding == nullptr ? "no binding" : binding->refusals.empty() ? "no template" : binding->refusals.front());
    }
    const Entity* board = board_of(state.providerKey);
    MissionGrant grant;
    grant.mission = mission;
    grant.communityGoal = town_row(*binding->row);
    grant.provider = state.providerKey;
    // The destination of a town mission: the claim of its settlement (the town screen keeps a community goal only when its destination is
    // the id of a claim of the map whose settlement is the territory: GetObjectivesForTerritory of territorydatahandler.luac) [client].
    grant.destination = board != nullptr ? board->object.providerIdentity : state.provider;
    const QuestResult granted = m_world.quests().grant_mission(character, *binding, state.territory, grant);
    if (granted.error == QuestError::alreadyActive) {
        return refuse(MissionError::alreadyActive, "instance " + std::to_string(granted.instance));
    }
    if (!granted.ok()) {
        return refuse(granted.error == QuestError::storeFailed ? MissionError::storeFailed : MissionError::questRefused,
                      quest_error_text(granted.error));
    }
    m_byProvider[state.providerKey].insert(character.id);
    ++m_counts.accepts;
    result.instance = granted.instance;
    log::write("missions: character %s accepted %s at the board %s (instance %016llx)", character.characterId.c_str(), binding->row->id.c_str(),
               state.providerKey.c_str(), static_cast<unsigned long long>(granted.instance));
    return result;
}

MissionResult Missions::complete(Entity& character, std::uint64_t instance) {
    MissionResult result;
    result.instance = instance;
    const auto refuse = [&](MissionError error, std::string detail) {
        result.error = error;
        result.detail = std::move(detail);
        ++m_counts.handInRefusals;
        log::write("missions: the hand-in of the instance %016llx by character %s is refused: %s (%s)", static_cast<unsigned long long>(instance),
                   character.characterId.c_str(), mission_error_text(error), result.detail.c_str());
        return result;
    };
    if (data() == nullptr || m_world.quests().data() == nullptr || !character.missionBoard.loaded) {
        return refuse(MissionError::noData, "no data");
    }
    const auto quest = std::find_if(character.quests.active.begin(), character.quests.active.end(),
                                    [&](const QuestInstance& value) { return value.instance == instance; });
    if (quest == character.quests.active.end() || quest->mission == 0) {
        return refuse(MissionError::unknownInstance, "no active mission of this instance (a second hand-in, or a forged instance)");
    }
    // The board of the service is the provider of the instance: the same key and the live child of the grant.
    const MissionBoardState& state = character.missionBoard;
    const Entity* board = board_of(quest->provider);
    if (!state.opened || state.providerKey != quest->provider || board == nullptr || !character.serviceGrant
        || character.serviceGrant->kind != kBoardService || m_world.interactions().grant_source(*character.serviceGrant) != board->id) {
        return refuse(MissionError::noProvider, "the provider of the instance is " + quest->provider);
    }
    if (const ServiceError error = m_world.interactions().require_source(character.characterId, kBoardService); error != ServiceError::none) {
        return refuse(MissionError::noProvider, service_error_text(error));
    }
    // The return task (CM_ReturnToGiver: TaskInteract with the tag ReturnToGiver) stays pending until this request: the client offers the
    // completion only while the progress of the container is its child count minus one, with the return as the last child (the ready
    // check 0x3A3CC50, 0x3A3CDC2 to 0x3A3CDEC; DESTINATION.md section 3) [client; live: after a return at the open the card showed "Start
    // Mission"]. The request at the provider board is the return: then the hand-in [agent].
    complete_return(character, *quest);
    // The town entry: the mission is unavailable for the current seed (0x495DEB0 keeps the exclusions of a seed and clears them at a new
    // seed). Its row goes into the transaction of the hand-in (MP-7).
    const std::uint32_t mission = quest->mission;
    const std::uint16_t territory = state.territory;
    const std::uint32_t seed = offers_of(character, territory).seed;
    MissionTownEntry entry;
    if (const auto found = state.towns.find(territory); found != state.towns.end() && found->second.seed == seed) {
        entry = found->second;
    } else {
        entry.seed = seed;
    }
    // [agent] the projects of the seed: the active projects of the town now (the client regenerates its cached offers for a current project
    // that the list does not have: OFFERS.md section 4).
    entry.previousProjects = town_projects(territory);
    if (std::find(entry.unavailable.begin(), entry.unavailable.end(), mission) == entry.unavailable.end()) {
        entry.unavailable.push_back(mission);
    }
    QuestCommitHook hook;
    hook.write = [&]() {
        return m_store.write_mission_offer(character.characterId,
                                           state::MissionOfferRow{territory, entry.seed, entry.unavailable, entry.previousProjects});
    };
    hook.apply = [&]() {
        character.missionBoard.towns[territory] = entry;
        m_world.touch(character.id, ComponentKey::objectiveInteractor);
    };
    result.handIn = m_world.quests().hand_in(character, instance, &hook);
    if (!result.handIn.ok()) {
        return refuse(result.handIn.error == QuestError::storeFailed ? MissionError::storeFailed : MissionError::questRefused,
                      std::string(quest_error_text(result.handIn.error)) + (result.handIn.detail.empty() ? "" : ": " + result.handIn.detail));
    }
    ++m_counts.handIns;
    log::write("missions: character %s handed in the mission %08x at the board %s: %llu experience, %llu coin units, standing %llu",
               character.characterId.c_str(), mission, state.providerKey.c_str(), static_cast<unsigned long long>(result.handIn.experience),
               static_cast<unsigned long long>(result.handIn.coin), static_cast<unsigned long long>(result.handIn.standing));
    return result;
}

void Missions::set_destinations(Entity& character, const std::string& key, const std::array<std::uint8_t, 16>& actor) {
    bool changed = false;
    for (QuestInstance& quest : character.quests.active) {
        if (quest.mission != 0 && quest.provider == key && quest.destination != actor) {
            quest.destination = actor;
            changed = true;
        }
    }
    if (changed) {
        ++m_counts.destinationUpdates;
        m_world.touch(character.id, ComponentKey::objectives);
    }
}

void Missions::child_spawned(const std::string& key, Entity& entity) {
    // Stage QG6a: the TerritoryInterface state of a new board: the active projects of its town (Missions::town_projects).
    if (entity.kind == EntityKind::object && entity.object.territorySlot != 0) {
        const std::uint16_t territory = territory_of(entity);
        entity.object.projects = town_projects(territory);
        m_world.touch(entity.id, ComponentKey::territoryInterface);
        // The claim of its town in the LandClaimManager state of the world entity: the town screen lists a project only when the claim of
        // the territory of the player has it (townprojects.luac; LANDCLAIM.md). One claim for each territory of a board.
        Entity& world = m_world.world_entity();
        std::vector<LandClaim>& claims = world.object.landClaims;
        const auto at = std::lower_bound(claims.begin(), claims.end(), territory,
                                         [](const LandClaim& claim, std::uint16_t value) { return claim.territory < value; });
        if (territory != 0 && (at == claims.end() || at->territory != territory)) {
            claims.insert(at, LandClaim{territory, entity.object.projects});
            m_world.touch(world.id, ComponentKey::landClaims);
        }
    }
    const auto found = m_byProvider.find(key);
    if (found == m_byProvider.end()) {
        return;
    }
    for (const EntityId id : found->second) {
        if (Entity* character = m_world.find(id); character != nullptr && character->kind == EntityKind::player) {
            set_destinations(*character, key, entity.object.providerIdentity);
        }
    }
}

std::optional<MissionView> Missions::view(const std::string& characterId) {
    MissionView view;
    const Entity* character = m_world.find_character(characterId);
    if (character == nullptr) {
        return view;
    }
    view.inWorld = true;
    view.board = character->missionBoard;
    if (character->missionBoard.opened) {
        view.offers = offers_of(*character, character->missionBoard.territory);
    }
    view.secondsToNextSeed = seconds_to_next_seed(m_world.clock().server_time_ns(), town_rule::kIntervalMinutes);
    for (const QuestInstance& quest : character->quests.active) {
        if (quest.mission != 0) {
            view.missions.push_back(quest);
        }
    }
    return view;
}

} // namespace soulwarden::backend::world
