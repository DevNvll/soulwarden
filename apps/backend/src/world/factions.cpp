#include "world/factions.h"

#include <algorithm>
#include <cctype>

#include "core/logging/log.h"
#include "world/interactions.h"
#include "world/persons.h"
#include "world/quests.h"
#include "world/skills.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The service Faction_Selection of a conversation state (the client enum of the services: 4; protocol notes 4.55). */
constexpr std::uint32_t kServiceFactionSelection = 4;
constexpr std::uint64_t kNanosecondsPerSecond = 1000000000ULL;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

const char* faction_error_text(FactionError error) {
    switch (error) {
    case FactionError::none:
        return "no error";
    case FactionError::unknownCharacter:
        return "the store has no character with this id";
    case FactionError::notInWorld:
        return "the character is not in the world";
    case FactionError::noData:
        return "the backend has no faction data (the member factions of progression-tables.json)";
    case FactionError::badFaction:
        return "the faction is not Syndicate (1), Marauder (2), or Covenant (3)";
    case FactionError::sameFaction:
        return "the character has this faction already";
    case FactionError::notOffered:
        return "the data does not offer the faction (no active recruitment task of the faction with the Faction_Selection service, and no "
               "recruitment achievement of the faction)";
    case FactionError::onCooldown:
        return "the wait after the last change is not over";
    case FactionError::notEnoughAzoth:
        return "the Azoth balance is below the cost of the change";
    case FactionError::storeFailed:
        return "the database did not take the change";
    }
    return "unknown error";
}

const char* faction_name(std::uint8_t faction) {
    switch (faction) {
    case 1:
        return "Syndicate";
    case 2:
        return "Marauder";
    case 3:
        return "Covenant";
    default:
        return "None";
    }
}

std::optional<std::uint8_t> faction_number(const std::string& text) {
    const std::string key = lower(text);
    if (key == "0" || key == "none") {
        return std::uint8_t{0};
    }
    for (std::uint8_t faction = 1; faction <= gamedata::kFactionCount; ++faction) {
        if (key == std::to_string(faction) || key == lower(faction_name(faction)) || key == "faction" + std::to_string(faction)) {
            return faction;
        }
    }
    // The data names the faction of a story with an "s" (FactionStory_Marauders, FactionIntro_Marauders_01).
    if (key == "marauders") {
        return std::uint8_t{2};
    }
    return std::nullopt;
}

Factions::Factions(World& world, state::Store& store, const gamedata::ProgressionTables* tables)
    : m_world(world), m_store(store), m_source(tables) {}

void Factions::load(Entity& entity) {
    const state::CharacterFactionRow row = m_store.faction(entity.characterId);
    entity.faction.faction = row.faction;
    entity.faction.changedNs = row.changedNs;
    entity.faction.changes = row.changes;
    entity.faction.publicChanged = 0;
}

std::optional<FactionState> Factions::view(const std::string& characterId) {
    if (const Entity* entity = m_world.find_character(characterId)) {
        return entity->faction;
    }
    if (!m_store.find_character(characterId)) {
        return std::nullopt;
    }
    const state::CharacterFactionRow row = m_store.faction(characterId);
    return FactionState{row.faction, row.changedNs, row.changes, 0};
}

std::uint64_t Factions::now_ns() const {
    return m_world.clock().wall_time_ns(m_world.clock().now());
}

const gamedata::FactionRow* Factions::row(std::uint8_t faction) const {
    const gamedata::ProgressionTables* data = tables();
    return data != nullptr ? data->faction(faction) : nullptr;
}

std::uint8_t Factions::recruitment_faction(const std::string& achievement) const {
    const gamedata::ProgressionTables* data = tables();
    if (data == nullptr || achievement.empty()) {
        return gamedata::kNoFaction;
    }
    const std::string key = lower(achievement);
    for (const gamedata::FactionRow& faction : data->factions()) {
        if (lower(faction.achievement) == key) {
            return faction.faction;
        }
    }
    return gamedata::kNoFaction;
}

std::uint64_t Factions::wait_left(const FactionState& state) const {
    // GetCooldownEndTime (0x30B1DE0): lastFactionChangeTimepoint plus Faction Change Cooldown Seconds; no branch for the count. A time of 0
    // (the join, a character with no change) ends in 1970: no wait.
    const gamedata::ProgressionTables* data = tables();
    if (data == nullptr || state.changedNs == 0) {
        return 0;
    }
    const std::uint64_t end = state.changedNs + static_cast<std::uint64_t>(data->faction_change().cooldownSeconds) * kNanosecondsPerSecond;
    const std::uint64_t now = now_ns();
    return end > now ? end - now : 0;
}

std::uint64_t Factions::change_cost(const Entity& character) const {
    // The cost getter (0x30B1EC0): 0 with no faction, and 0 when the level index (the level of the screen minus 1) is below Faction Change
    // Cost Level (no plus 1: the first cost is at the level 21 of the screen); else the provider of PlayerDataManager (virtual +0x518) with
    // factionChangeCount.
    const gamedata::ProgressionTables* data = tables();
    if (data == nullptr || character.faction.faction == gamedata::kNoFaction || character.progression.level == 0
        || character.progression.level - 1 < data->faction_change().costLevel) {
        return 0;
    }
    // The provider (0x67F8670 -> 0x5C41E30): unsigned min(Max, Min + u16(count) x Increment). Max 0 of the data gives 0 at each level and
    // count (static read S4 of stage QG7).
    const gamedata::FactionChange& change = data->faction_change();
    const std::uint64_t raw = static_cast<std::uint64_t>(change.costMin)
                              + static_cast<std::uint64_t>(change.costIncrement) * static_cast<std::uint64_t>(character.faction.changes);
    return std::min<std::uint64_t>(change.costMax, raw);
}

bool Factions::offered(const Entity& character, std::uint8_t faction) const {
    const gamedata::FactionRow* data = row(faction);
    if (data == nullptr) {
        return false;
    }
    const auto service = [&] {
        return m_world.interactions().require_service(character.characterId, kConversationServiceKind, kServiceFactionSelection)
               == ServiceError::none;
    };
    // The recruitment achievement of the faction (the Bio change panel: CanSetFactionWithResults with the achievement check, 0x30AD140
    // result 10; the states 98xx_Rep*_01_b offer Faction_Selection with it). A character with no faction has no Bio change button
    // (UpdateFactionButton of skills/bio.luac): it needs the service of the conversation.
    const Quests& quests = m_world.quests();
    const bool achievement = (!data->achievement.empty() && quests.conditional(character, data->achievement))
                             || (!data->legacyAchievement.empty() && quests.conditional(character, data->legacyAchievement));
    if (achievement && (character.faction.faction != gamedata::kNoFaction || service())) {
        return true;
    }
    // The recruitment: the active objective whose completion achievement is the RequiredAchievementId of the faction, with its
    // TaskChooseFaction task active (the states 98xx_Rep*_01 need RequiredActiveObjectiveId and RequiredActiveTaskId and offer
    // Faction_Selection), and the service of that conversation.
    return quests.choose_faction_active(character, faction) && service();
}

FactionError Factions::check(const Entity& character, std::uint8_t faction, std::uint64_t* cost, std::uint64_t* waitNs) const {
    if (tables() == nullptr) {
        return FactionError::noData;
    }
    if (faction == gamedata::kNoFaction || faction > gamedata::kFactionCount) {
        return FactionError::badFaction;
    }
    if (faction == character.faction.faction) {
        return FactionError::sameFaction;
    }
    if (!offered(character, faction)) {
        return FactionError::notOffered;
    }
    // A change (the character has a faction): the wait, then the cost. The join of a character with no faction has neither (the getters
    // return no wait for the time 0, and 0 Azoth with no faction).
    const std::uint64_t left = wait_left(character.faction);
    if (waitNs != nullptr) {
        *waitNs = left;
    }
    if (left != 0) {
        return FactionError::onCooldown;
    }
    const std::uint64_t price = change_cost(character);
    if (cost != nullptr) {
        *cost = price;
    }
    if (price != 0 && Skills::balance(character.skills, kAzothCurrency) < price) {
        return FactionError::notEnoughAzoth;
    }
    return FactionError::none;
}

FactionResult Factions::request(Entity& character, std::uint8_t faction) {
    ++m_counts.requests;
    FactionResult result;
    result.inWorld = true;
    result.before = character.faction.faction;
    result.state = character.faction;
    std::uint64_t cost = 0;
    std::uint64_t wait = 0;
    result.error = check(character, faction, &cost, &wait);
    result.waitNs = wait;
    if (!result.ok()) {
        ++m_counts.refusals;
        // The client published the faction of the request on its HUD (0x30C2E50) and its conversation waits for a faction field: the owner
        // gets the faction of the server again (the apply 0x30B6210 republishes a received field also when it is the same value).
        m_world.touch(character.id, ComponentKey::faction);
        log::write("factions: the request of character %s for the faction %s (%u) is refused: %s; the faction stays %s",
                   character.characterId.c_str(), faction_name(faction), faction, faction_error_text(result.error),
                   faction_name(character.faction.faction));
        return result;
    }
    FactionState after = character.faction;
    after.faction = faction;
    if (character.faction.faction != gamedata::kNoFaction) {
        // A change: the wait starts now, and the count goes up (factionChangeCount). The join of a character with no faction keeps the time
        // 0: the first change from the Bio screen has no wait (owg_faction_select_confirm: "you can change your faction from the Bio screen
        // once. After that, you can change it again every 30 days").
        after.changedNs = now_ns();
        after.changes = static_cast<std::uint16_t>(std::min<std::uint32_t>(after.changes + 1U, 0xFFFF));
    }
    return apply(&character, character.characterId, character.faction, after, cost, "the request of the client");
}

FactionResult Factions::set(const std::string& characterId, const FactionCommand& command) {
    FactionResult result;
    if (command.faction > gamedata::kFactionCount) {
        result.error = FactionError::badFaction;
        return result;
    }
    Entity* character = m_world.find_character(characterId);
    const std::optional<FactionState> before = character != nullptr ? std::optional<FactionState>(character->faction) : view(characterId);
    if (!before) {
        result.error = FactionError::unknownCharacter;
        return result;
    }
    FactionState after = *before;
    after.faction = command.faction;
    if (command.changedNs) {
        after.changedNs = *command.changedNs;
    }
    if (command.changes) {
        after.changes = *command.changes;
    }
    return apply(character, characterId, *before, after, 0, "a command");
}

FactionResult Factions::apply(Entity* character, const std::string& characterId, const FactionState before, FactionState after,
                              std::uint64_t cost, const char* what) {
    // `before` is a copy: the caller can give the state of the entity, which this function changes.
    FactionResult result;
    result.inWorld = character != nullptr;
    result.before = before.faction;
    result.state = before;
    result.cost = cost;
    const auto refuse = [&](FactionError error) {
        result.error = error;
        if (character != nullptr) {
            m_world.touch(character->id, ComponentKey::faction);
        }
        log::write("factions: the change of character %s to %s (%s) is refused: %s; no change", characterId.c_str(), faction_name(after.faction),
                   what, faction_error_text(error));
        return result;
    };
    // One transaction: the faction row and the Azoth of the cost (the skills rows).
    Skills::Plan pay = m_world.skills().plan_none(characterId);
    if (cost != 0) {
        const ProgressError error = pay.ok() ? m_world.skills().add_balance(pay, kAzothCurrency, -static_cast<std::int64_t>(cost), 0)
                                             : pay.error;
        if (error != ProgressError::none) {
            return refuse(error == ProgressError::notEnoughBalance ? FactionError::notEnoughAzoth : FactionError::storeFailed);
        }
    }
    // Code review of stage QG7, finding QG7R-R1 (MP-7): the quest ends of the change go into the same transaction (the quest plan with the
    // faction row and the cost as its hook); a refused part changes nothing.
    QuestCommitHook hook;
    hook.write = [&]() {
        return m_store.write_faction(characterId, state::CharacterFactionRow{after.faction, after.changedNs, after.changes})
               && (cost == 0 || m_world.skills().write(pay));
    };
    const std::optional<QuestError> ended =
        character != nullptr && before.faction != after.faction ? m_world.quests().faction_ends(*character, after.faction, hook) : std::nullopt;
    if (ended) {
        if (*ended != QuestError::none) {
            return refuse(FactionError::storeFailed);
        }
    } else {
        state::Store::Batch batch(m_store);
        const bool written = batch.open() && hook.write() && batch.commit();
        if (!written) {
            return refuse(FactionError::storeFailed);
        }
    }
    if (cost != 0) {
        (void)m_world.skills().finish(pay, "the cost of a faction change");
    }
    if (before.faction == gamedata::kNoFaction && after.faction != gamedata::kNoFaction) {
        ++m_counts.joins;
    } else if (before.faction != after.faction) {
        ++m_counts.changes;
    }
    result.state = after;
    log::write("factions: character %s: %s -> %s (%s; last change %llu, %u change(s), %llu Azoth)", characterId.c_str(),
               faction_name(before.faction), faction_name(after.faction), what, static_cast<unsigned long long>(after.changedNs),
               after.changes, static_cast<unsigned long long>(cost));
    if (character == nullptr) {
        return result;
    }
    const Revision revision = m_world.touch(character->id, ComponentKey::faction);
    after.publicChanged = before.faction != after.faction ? revision : before.publicChanged;
    character->faction = after;
    result.state = after;
    // The quests of the character: the gates, TaskChooseFaction, the objectives with ShouldResetOnFactionChange, the rank-up rows
    // (docs/systems/factions.md, the quests).
    if (before.faction != after.faction) {
        m_world.quests().faction_changed(*character, before.faction);
        // Code review of stage QG6b, finding QG6BR-R5: an open faction board of the old faction closes (a new open checks the faction).
        m_world.missions().faction_changed(*character);
    }
    return result;
}

} // namespace soulwarden::backend::world
