#include "world/chests.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "core/logging/log.h"
#include "world/interactions.h"
#include "world/rewards.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

/** The option of a chest (uidatabase.uidb: action 14 GatherResource, committed; live check L1 of stage 3.5b.1). */
constexpr const char* kChestOption = "GatherOption";
/** The Gathering status values (enum 0x49024D0). */
constexpr std::uint8_t kGatherSuccess = 1;
constexpr std::uint8_t kGatherFailure = 2;
constexpr std::uint8_t kGatherDone = 3;
constexpr std::uint64_t kNoTarget = 0x00000000FFFFFFFFULL;

std::string lower_ascii(std::string text) {
    for (char& letter : text) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return text;
}

std::string item_list(const std::vector<GrantItem>& items) {
    std::string text;
    for (const GrantItem& item : items) {
        text += (text.empty() ? "" : ", ") + std::to_string(item.quantity) + " " + item.itemId;
        if (item.gearScore) {
            text += " (gear score " + std::to_string(*item.gearScore) + ")";
        }
    }
    return text.empty() ? "no item" : text;
}

/** The handler of GatherOption of an object of the kind "gathering": the chests (world/interactions.h). */
class ChestHandler final : public InteractionHandler {
public:
    explicit ChestHandler(Chests& chests) : m_chests(chests) {}

    [[nodiscard]] const char* name() const override {
        return "chest";
    }
    [[nodiscard]] InteractionVerdict begin(Entity& character, const InteractionContext& context) override {
        return m_chests.begin(character, context);
    }
    /** The open time of the gatherable row (code review of stage 3.5b.1, finding CH-R2): the timed use of the base. */
    [[nodiscard]] float use_time(const Entity& character, const InteractionContext& context) const override {
        (void)context;
        return m_chests.open_seconds(character);
    }
    void complete(Entity& character, const InteractionContext& context) override {
        m_chests.complete(character, context);
    }
    void end(Entity& character, const InteractionContext& context, InteractionEnd cause) override {
        m_chests.ended(character, context, cause);
    }
    /**
     * The sight of a chest (m_requiresLineOfSight of each chest bound): the client checks it before it shows the prompt (its
     * LOSRayCastCheck entity); the server has no ray of the physics of the client, and the navigation cells of the crate itself stop a
     * line to it. A decision of an agent: the server checks the 3D range of the data, not the sight (docs/systems/chests.md).
     */
    [[nodiscard]] bool owns_sight() const override {
        return true;
    }
    /** The action Gather of the client ends at a hit (game data: playeractions_gather.actionlist, IfDamagedByPowerLevel >= 0). */
    [[nodiscard]] bool ends_at_damage() const override {
        return true;
    }

private:
    Chests& m_chests;
};

} // namespace

const char* claim_state_text(ClaimState state) {
    return state == ClaimState::rolled ? "rolled" : "looted";
}

const char* chest_refusal_text(ChestRefusal refusal) {
    switch (refusal) {
    case ChestRefusal::noData:
        return "the backend has no chest data";
    case ChestRefusal::notChest:
        return "the object is not a chest of the chest data (a gathering node: stage 3.6)";
    case ChestRefusal::unavailable:
        return "the chest is unavailable";
    case ChestRefusal::noLoot:
        return "the backend has no loot table of the root of the chest";
    case ChestRefusal::claimed:
        return "the character has a claim on the chest";
    }
    return "?";
}

const char* claim_error_text(ClaimError error) {
    switch (error) {
    case ClaimError::none:
        return "done";
    case ClaimError::notInWorld:
        return "the character is not in the world";
    case ClaimError::noClaim:
        return "the character has no such claim";
    case ClaimError::unknownChest:
        return "the chest data has no chest with this key";
    case ClaimError::badTime:
        return "the time is not in the range of the override (1 s to a week)";
    case ClaimError::storeFailed:
        return "the store did not write the change";
    }
    return "?";
}

Chests::Chests(World& world, const gamedata::ChestData* data)
    : m_world(world), m_data(data), m_handler(std::make_unique<ChestHandler>(*this)) {
    world.interactions().register_handler(gamedata::InteractionKind::gathering, gamedata::interaction_option_id(kChestOption), *m_handler);
}

Chests::~Chests() = default;

std::int64_t Chests::wall_ms(WorldClock::time_point time) const {
    return static_cast<std::int64_t>(m_world.clock().wall_time_ns(time) / 1000000ULL);
}

std::uint64_t Chests::micros(WorldClock::time_point time) const {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(time.time_since_epoch()).count());
}

std::size_t Chests::claim_count() const {
    std::size_t count = 0;
    for (const auto& [id, own] : m_own) {
        count += own.claims.size();
    }
    return count;
}

std::size_t Chests::open_count() const {
    std::size_t count = 0;
    for (const auto& [id, own] : m_own) {
        count += own.open ? 1 : 0;
    }
    return count;
}

std::optional<std::uint32_t> Chests::chest_of(const InteractionContext& context) const {
    if (data() == nullptr || context.object == nullptr || context.instance != 0) {
        return std::nullopt;
    }
    return m_data->find(context.object->key);
}

state::LootClaimRow Chests::row_of(const std::string& key, const ChestClaim& claim) const {
    state::LootClaimRow row;
    row.objectKey = key;
    row.state = static_cast<std::uint32_t>(claim.state);
    row.generation = claim.generation;
    row.rolledAt = claim.rolledAt;
    row.lootedAt = claim.lootedAt;
    row.resetAt = claim.resetAt;
    for (const GrantItem& item : claim.items) {
        state::ClaimItemRow entry;
        entry.itemId = item.itemId;
        entry.quantity = item.quantity;
        entry.gearScore = item.gearScore;
        entry.perks = item.perks;
        row.items.push_back(std::move(entry));
    }
    return row;
}

std::chrono::milliseconds Chests::reset_time(const gamedata::ChestDefinition& chest) {
    // The reset values of the gatherable row (the elite chests: 82,800 s), else the reset of an instanced chest of the base attributes of
    // a player (60 minutes; research Q1 "Resets and counts"). A range: a time from the smallest to the largest value with the generator of
    // the world (the rule of the respawn of a spawn point, world/rewards.h).
    const gamedata::ChestGatherable* gatherable = m_data->gatherable_of(chest);
    if (gatherable != nullptr && gatherable->resetMax > 0.0F) {
        float seconds = gatherable->resetMin;
        if (gatherable->resetMax > gatherable->resetMin) {
            seconds += m_world.random_unit() * (gatherable->resetMax - gatherable->resetMin);
        }
        return std::chrono::milliseconds(static_cast<long long>(std::lround(std::max(seconds, 1.0F) * 1000.0F)));
    }
    return std::chrono::minutes(m_data->rules().resetMinutes);
}

void Chests::entered(Entity& character) {
    if (character.kind != EntityKind::player) {
        return;
    }
    Own& own = m_own[character.characterId];
    own = Own{};
    const WorldClock::time_point now = m_world.clock().now();
    const std::int64_t wallNow = wall_ms(now);
    std::vector<std::string> expired;
    for (state::LootClaimRow& row : m_world.store().list_loot_claims(character.characterId)) {
        own.generation = std::max(own.generation, row.generation);
        const std::optional<std::uint32_t> chest = data() != nullptr ? m_data->find(row.objectKey) : std::nullopt;
        if (row.state == static_cast<std::uint32_t>(ClaimState::looted) && row.resetAt <= wallNow) {
            expired.push_back(row.objectKey); // The reset came while the character was away (the wall clock of the row).
            continue;
        }
        if (!chest || (row.state != static_cast<std::uint32_t>(ClaimState::looted) && row.state != static_cast<std::uint32_t>(ClaimState::rolled))) {
            continue; // A claim on a chest that the data does not have: the row stays, with no effect.
        }
        ChestClaim claim;
        claim.chest = *chest;
        claim.state = static_cast<ClaimState>(row.state);
        claim.generation = row.generation;
        claim.rolledAt = row.rolledAt;
        claim.lootedAt = row.lootedAt;
        claim.resetAt = row.resetAt;
        if (claim.state == ClaimState::looted) {
            claim.reset = now + std::chrono::milliseconds(row.resetAt - wallNow);
            m_due.emplace(claim.reset, DueReset{character.characterId, row.objectKey, claim.generation});
        }
        for (const state::ClaimItemRow& item : row.items) {
            claim.items.push_back(GrantItem{item.itemId, item.quantity, item.gearScore, item.perks});
        }
        own.claims.emplace(lower_ascii(row.objectKey), std::move(claim));
    }
    if (!expired.empty()) {
        if (m_world.store().delete_loot_claims(character.characterId, expired)) {
            m_counts.resets += expired.size();
        }
    }
    if (!own.claims.empty()) {
        log::write("world: chests: character %s has %zu loot claim(s) (%zu reset while it was away)", character.characterId.c_str(),
                   own.claims.size(), expired.size());
    }
    refresh_tracker(character, own);
}

void Chests::left(Entity& character) {
    m_own.erase(character.characterId); // The due resets of its claims find no claim: the rows stay for the next entry.
}

void Chests::moved(Entity& character) {
    const auto found = m_own.find(character.characterId);
    if (found == m_own.end() || data() == nullptr) {
        return;
    }
    const std::int64_t cell = gamedata::ChestData::cell_of(character.position.x, character.position.y);
    if (!found->second.hasCell || cell != found->second.cell) {
        refresh_tracker(character, found->second);
    }
}

void Chests::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const DueReset due = m_due.begin()->second;
        m_due.erase(m_due.begin());
        ++m_counts.dueLooks;
        const auto own = m_own.find(due.characterId);
        if (own == m_own.end()) {
            continue; // The character left: the row stays, and the next entry removes it.
        }
        const auto claim = own->second.claims.find(lower_ascii(due.key));
        if (claim == own->second.claims.end() || claim->second.generation != due.generation || claim->second.state != ClaimState::looted
            || claim->second.reset > now) {
            continue; // A claim that a command changed (a new reset time, a removal).
        }
        if (!m_world.store().delete_loot_claims(due.characterId, {due.key})) {
            ++m_counts.storeFailures;
            m_due.emplace(now + chest_rules::kResetRetry, due);
            continue;
        }
        own->second.claims.erase(claim);
        ++m_counts.resets;
        Entity* character = m_world.find_character(due.characterId);
        if (character != nullptr) {
            const std::string mark = log::session_mark(character->owner, character->characterId);
            const log::Context context(mark);
            log::write("world: chests: the claim of character %s on the chest %s reset", due.characterId.c_str(), due.key.c_str());
            refresh_tracker(*character, own->second);
        }
    }
}

InteractionVerdict Chests::refuse(ChestRefusal refusal, const Entity& character, const std::string& key, const char* detail) {
    ++m_counts.refusals[static_cast<std::size_t>(refusal)];
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: chests: the open of %s by character %s refused: %s%s%s", key.c_str(), character.characterId.c_str(),
               chest_refusal_text(refusal), detail != nullptr && *detail != '\0' ? ": " : "", detail != nullptr ? detail : "");
    return InteractionVerdict::refused;
}

InteractionVerdict Chests::begin(Entity& character, const InteractionContext& context) {
    const std::string key = context.object != nullptr ? context.object->key : std::string("?");
    if (data() == nullptr) {
        return refuse(ChestRefusal::noData, character, key, "make the file with tools/gamedata/chests.sh");
    }
    const std::optional<std::uint32_t> index = chest_of(context);
    if (!index) {
        return refuse(ChestRefusal::notChest, character, key, nullptr);
    }
    const gamedata::ChestDefinition& chest = m_data->chests()[*index];
    if (!chest.available) {
        return refuse(ChestRefusal::unavailable, character, key, chest.reason.c_str());
    }
    const gamedata::LootTables* tables = m_world.rewards().tables();
    if (tables == nullptr || !tables->table_index(chest.root)) {
        return refuse(ChestRefusal::noLoot, character, key, chest.root.c_str());
    }
    Own& own = m_own[character.characterId];
    const WorldClock::time_point now = m_world.clock().now();
    const auto claim = own.claims.find(lower_ascii(chest.key));
    if (claim != own.claims.end() && claim->second.state == ClaimState::looted) {
        char detail[96];
        std::snprintf(detail, sizeof(detail), "looted, the reset in %.0f s",
                      std::chrono::duration<double>(claim->second.reset - now).count());
        return refuse(ChestRefusal::claimed, character, key, detail);
    }
    const gamedata::ChestGatherable& gatherable = *m_data->gatherable_of(chest);
    const auto time = std::chrono::duration_cast<WorldClock::time_point::duration>(std::chrono::duration<float>(gatherable.openSeconds));
    // The time of the open is the time of the timed use of the base (use_time; code review of stage 3.5b.1, finding CH-R2): its timer,
    // its checks again, and the completion of a release at or after the time (finding ST-R1). The chest has no timer of its own.
    ChestOpen open;
    open.chest = *index;
    open.seconds = gatherable.openSeconds;
    open.started = now;
    open.due = now + time;
    own.open = open;
    set_gather(character, kGatherSuccess, gatherable.openSeconds, true, context.object->group);
    ++m_counts.begins;
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    log::write("world: chests: character %s opens the %s chest %s (%s, %s, root %s): %.1f s%s", character.characterId.c_str(),
               gamedata::chest_kind_text(chest.kind), chest.key.c_str(), chest.variant.c_str(), gatherable.id.c_str(), chest.root.c_str(),
               static_cast<double>(gatherable.openSeconds), claim != own.claims.end() ? " (the kept roll of its claim)" : "");
    return InteractionVerdict::accepted;
}

void Chests::ended(Entity& character, const InteractionContext& context, InteractionEnd cause) {
    const auto found = m_own.find(character.characterId);
    if (found == m_own.end() || !found->second.open) {
        return; // No open (a completed open: the client ends its use after Done).
    }
    const std::optional<std::uint32_t> index = chest_of(context);
    if (!index || *index != found->second.open->chest) {
        return; // The end of a different use (a new open replaced it).
    }
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context logContext(mark);
    log::write("world: chests: the open of %s by character %s ended before its time: %s", m_data->chests()[*index].key.c_str(),
               character.characterId.c_str(), interaction_end_text(cause));
    ++m_counts.cancels;
    fail_open(character, found->second);
}

void Chests::fail_open(Entity& character, Own& own) {
    own.open.reset();
    set_gather(character, kGatherFailure, 0.0F, false, kNoTarget);
}

void Chests::set_gather(Entity& character, std::uint8_t status, float remaining, bool active, std::uint64_t target) {
    GatherProgress& gather = character.gathering;
    gather.status = status;
    gather.remaining = remaining;
    gather.speed = chest_rules::kGatherSpeed;
    gather.gatherers = active ? 1 : 0;
    gather.active = active;
    gather.target = target;
    // The client applies the fields only at a change of their reference counters (apply 0x4905130).
    ++gather.reference;
    ++gather.syncReference;
    m_world.touch(character.id, ComponentKey::gathering);
}

float Chests::open_seconds(const Entity& character) const {
    const auto found = m_own.find(character.characterId);
    return found != m_own.end() && found->second.open ? found->second.open->seconds : 0.0F;
}

void Chests::complete(Entity& entity, const InteractionContext& context) {
    Entity* character = &entity;
    const std::string& characterId = entity.characterId;
    const auto found = m_own.find(characterId);
    const std::optional<std::uint32_t> openChest = chest_of(context);
    if (found == m_own.end() || !found->second.open || !openChest || *openChest != found->second.open->chest) {
        return; // No open of this chest (a different use).
    }
    // The base checked the use again before this call (the source, the life, the exact position, the range: still_valid).
    Own& own = found->second;
    const std::uint32_t index = own.open->chest;
    const gamedata::ChestDefinition& chest = m_data->chests()[index];
    const std::string mark = log::session_mark(character->owner, character->characterId);
    const log::Context logContext(mark);
    const std::string claimKey = lower_ascii(chest.key);
    auto existing = own.claims.find(claimKey);
    if (existing != own.claims.end() && existing->second.state == ClaimState::looted) {
        ++m_counts.cancels;
        fail_open(*character, own);
        m_world.interactions().end_use(*character, InteractionEnd::policyFailed);
        return;
    }
    // The roll: the kept roll of the claim, or a new roll of the root with the context of the chest.
    const WorldClock::time_point now = m_world.clock().now();
    const std::int64_t wallNow = wall_ms(now);
    const bool kept = existing != own.claims.end() && existing->second.state == ClaimState::rolled;
    std::vector<GrantItem> items;
    if (kept) {
        items = existing->second.items;
    } else {
        LootContext context;
        context.characterLevel = character->progression.level;
        context.creatureLevel = chest.contentLevel.value_or(character->progression.level);
        context.tags = chest.tags;
        context.container = true;
        const std::uint32_t root = *m_world.rewards().tables()->table_index(chest.root);
        items = m_world.rewards().grant_items(m_world.rewards().roll_root(root, context), character->progression.level);
        ++m_counts.rolls;
    }
    // One transaction: the items and the claim (MP-7). A full bag keeps the roll in the claim (Rolled) and gives no item.
    ChestClaim next;
    next.chest = index;
    next.generation = kept ? existing->second.generation : ++own.generation;
    next.rolledAt = kept ? existing->second.rolledAt : wallNow;
    const Bag bagBefore = character->bag;
    const Wallet walletBefore = character->wallet;
    state::Store::Batch batch(m_world.store());
    bool written = batch.open();
    ItemResult given;
    if (written && !items.empty()) {
        given = m_world.inventory().grant(characterId, items, 0, nullptr, true);
        written = given.error != ItemError::storeFailed;
    }
    // A full bag, or an item of the roll that the catalog does not have (finding CH-R3): no change, and the claim keeps the complete
    // roll (Rolled). A later open gives it when the bag has place and the catalog has each item.
    const bool unresolved = given.error == ItemError::unknownItem || given.error == ItemError::noItemType;
    const bool full = given.error == ItemError::bagFull || unresolved;
    if (full) {
        next.state = ClaimState::rolled;
        next.items = items;
    } else {
        next.state = ClaimState::looted;
        next.lootedAt = wallNow;
        const std::chrono::milliseconds reset = reset_time(chest);
        next.reset = now + reset;
        next.resetAt = wallNow + reset.count();
    }
    if (written && !(full && kept)) {
        written = m_world.store().write_loot_claim(characterId, row_of(chest.key, next));
    }
    written = written && batch.commit();
    if (!written) {
        character->bag = bagBefore;
        character->wallet = walletBefore;
        m_world.touch(character->id, ComponentKey::container);
        m_world.touch(character->id, ComponentKey::currency);
        if (!kept) {
            --own.generation;
        }
        ++m_counts.storeFailures;
        log::write("world: chests: the store did not write the open of %s by character %s: no item and no claim", chest.key.c_str(),
                   characterId.c_str());
        fail_open(*character, own);
        m_world.interactions().end_use(*character, InteractionEnd::policyFailed);
        return;
    }
    own.open.reset();
    own.claims[claimKey] = next;
    if (full) {
        ++m_counts.kept;
        log::write("world: chests: %s character %s for the roll of %s (%s): the roll stays in its claim, the chest stays closed",
                   unresolved ? "the item catalog has no item of the roll of" : "the bag has no place for", characterId.c_str(),
                   chest.key.c_str(), item_list(items).c_str());
        set_gather(*character, kGatherFailure, 0.0F, false, kNoTarget);
        refresh_tracker(*character, own);
        m_world.interactions().end_use(*character, InteractionEnd::policyFailed);
        return;
    }
    m_due.emplace(next.reset, DueReset{characterId, chest.key, next.generation});
    ++m_counts.completions;
    m_counts.itemsGiven += items.size();
    set_gather(*character, kGatherDone, 0.0F, false, kNoTarget);
    refresh_tracker(*character, own);
    log::write("world: chests: character %s opened the %s chest %s: %s; the reset in %.0f s (claim %llu)", characterId.c_str(),
               gamedata::chest_kind_text(chest.kind), chest.key.c_str(), item_list(items).c_str(),
               std::chrono::duration<double>(next.reset - now).count(), static_cast<unsigned long long>(next.generation));
}

void Chests::refresh_tracker(Entity& character, Own& own) {
    own.cell = gamedata::ChestData::cell_of(character.position.x, character.position.y);
    own.hasCell = true;
    std::vector<LootTrackerEntry> entries;
    if (data() != nullptr) {
        ++m_counts.trackers;
        const float rangeSq = chest_rules::kTrackerRange * chest_rules::kTrackerRange;
        for (const auto& [key, claim] : own.claims) {
            ++m_counts.trackerClaims;
            const gamedata::ChestDefinition& chest = m_data->chests()[claim.chest];
            const float dx = chest.position.x - character.position.x;
            const float dy = chest.position.y - character.position.y;
            if (dx * dx + dy * dy > rangeSq) {
                continue;
            }
            LootTrackerEntry entry;
            entry.actor = chest.actor;
            entry.state = claim.state == ClaimState::looted ? chest_rules::kLooted : chest_rules::kRolled;
            entry.resetTime = claim.state == ClaimState::looted ? micros(claim.reset) : 0;
            entry.lootedTime = claim.state == ClaimState::looted ? micros(claim.reset) - static_cast<std::uint64_t>(
                                                                        std::max<std::int64_t>(0, claim.resetAt - claim.lootedAt) * 1000)
                                                                 : 0;
            entry.position = chest.position;
            entries.push_back(entry);
        }
        // The unavailable chests near the character (live check L9: the state of an unmet gate).
        m_data->for_each_near(
            character.position, chest_rules::kTrackerRange,
            [&](std::uint32_t index) {
                const gamedata::ChestDefinition& chest = m_data->chests()[index];
                if (chest.available || own.claims.count(lower_ascii(chest.key)) != 0) {
                    return;
                }
                LootTrackerEntry entry;
                entry.actor = chest.actor;
                entry.state = chest_rules::kUnavailableState;
                entry.position = chest.position;
                entries.push_back(entry);
            },
            &m_counts.lookups);
        std::sort(entries.begin(), entries.end(), [](const LootTrackerEntry& a, const LootTrackerEntry& b) { return a.actor < b.actor; });
    }
    if (entries != character.lootTracker) {
        character.lootTracker = std::move(entries);
        m_world.touch(character.id, ComponentKey::lootTracker);
    }
}

ChestsView Chests::view(const std::string& characterId, float radius) const {
    ChestsView view;
    const Entity* character = m_world.find_character(characterId);
    const auto found = m_own.find(characterId);
    if (character == nullptr || found == m_own.end()) {
        return view;
    }
    view.inWorld = true;
    view.open = found->second.open;
    view.trackerEntries = character->lootTracker.size();
    const WorldClock::time_point now = m_world.clock().now();
    for (const auto& [key, claim] : found->second.claims) {
        ClaimView entry;
        entry.chest = data() != nullptr ? &m_data->chests()[claim.chest] : nullptr;
        entry.key = entry.chest != nullptr ? entry.chest->key : key;
        entry.state = claim.state;
        entry.generation = claim.generation;
        entry.resetSeconds = claim.state == ClaimState::looted ? std::chrono::duration<double>(claim.reset - now).count() : 0.0;
        entry.lootedAt = claim.lootedAt;
        entry.resetAt = claim.resetAt;
        entry.items = claim.items;
        view.claims.push_back(std::move(entry));
    }
    if (data() != nullptr && std::isfinite(radius) && radius >= 0.0F) {
        m_data->for_each_near(character->position, std::min(radius, 2000.0F), [&](std::uint32_t index) {
            ChestNearView entry;
            entry.index = index;
            entry.chest = &m_data->chests()[index];
            entry.distance = std::hypot(entry.chest->position.x - character->position.x, entry.chest->position.y - character->position.y);
            const auto claim = found->second.claims.find(lower_ascii(entry.chest->key));
            if (claim != found->second.claims.end()) {
                entry.claim = claim->second.state;
            }
            view.nearby.push_back(entry);
        });
        std::sort(view.nearby.begin(), view.nearby.end(), [](const ChestNearView& a, const ChestNearView& b) {
            return a.distance != b.distance ? a.distance < b.distance : a.index < b.index;
        });
    }
    return view;
}

ClaimError Chests::clear(const std::string& characterId, const std::string& key) {
    const std::optional<std::uint32_t> index = data() != nullptr ? m_data->find(key) : std::nullopt;
    if (!index) {
        return ClaimError::unknownChest;
    }
    const std::string& chestKey = m_data->chests()[*index].key;
    const auto found = m_own.find(characterId);
    if (found != m_own.end()) {
        if (found->second.claims.count(lower_ascii(chestKey)) == 0) {
            return ClaimError::noClaim;
        }
        if (!m_world.store().delete_loot_claims(characterId, {chestKey})) {
            return ClaimError::storeFailed;
        }
        found->second.claims.erase(lower_ascii(chestKey));
        if (Entity* character = m_world.find_character(characterId)) {
            refresh_tracker(*character, found->second);
        }
        log::write("world: chests: COMMAND: the claim of character %s on the chest %s removed", characterId.c_str(), chestKey.c_str());
        return ClaimError::none;
    }
    // A character that is not in the world: its rows.
    const std::vector<state::LootClaimRow> rows = m_world.store().list_loot_claims(characterId);
    if (std::none_of(rows.begin(), rows.end(), [&](const state::LootClaimRow& row) { return lower_ascii(row.objectKey) == lower_ascii(chestKey); })) {
        return ClaimError::noClaim;
    }
    return m_world.store().delete_loot_claims(characterId, {chestKey}) ? ClaimError::none : ClaimError::storeFailed;
}

ClaimError Chests::set_reset(const std::string& characterId, const std::string& key, std::chrono::seconds seconds) {
    if (seconds.count() < 1 || seconds > chest_rules::kLongestOverride) {
        return ClaimError::badTime;
    }
    const std::optional<std::uint32_t> index = data() != nullptr ? m_data->find(key) : std::nullopt;
    if (!index) {
        return ClaimError::unknownChest;
    }
    const auto found = m_own.find(characterId);
    if (found == m_own.end()) {
        return ClaimError::notInWorld;
    }
    const auto claim = found->second.claims.find(lower_ascii(m_data->chests()[*index].key));
    if (claim == found->second.claims.end() || claim->second.state != ClaimState::looted) {
        return ClaimError::noClaim;
    }
    ChestClaim changed = claim->second;
    const WorldClock::time_point now = m_world.clock().now();
    changed.reset = now + seconds;
    changed.resetAt = wall_ms(now) + std::chrono::duration_cast<std::chrono::milliseconds>(seconds).count();
    if (!m_world.store().write_loot_claim(characterId, row_of(m_data->chests()[*index].key, changed))) {
        return ClaimError::storeFailed;
    }
    claim->second = changed;
    m_due.emplace(changed.reset, DueReset{characterId, m_data->chests()[*index].key, changed.generation});
    if (Entity* character = m_world.find_character(characterId)) {
        refresh_tracker(*character, found->second);
    }
    log::write("world: chests: DEVELOPMENT COMMAND: the claim of character %s on the chest %s resets in %lld s (not the rule of the data)",
               characterId.c_str(), m_data->chests()[*index].key.c_str(), static_cast<long long>(seconds.count()));
    return ClaimError::none;
}

} // namespace soulwarden::backend::world
