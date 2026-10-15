#include "world/statuses.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "gamedata/progression_tables.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

std::string lower(std::string_view text) {
    std::string result(text);
    for (char& letter : result) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + 32);
        }
    }
    return result;
}

bool same_text(std::string_view left, std::string_view right) {
    return left.size() == right.size() && lower(left) == lower(right);
}

/** @return The parts of a list of the data ("Buff+ArmorFortify", "A,B"), with no space around them. */
std::vector<std::string_view> parts_of(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t end = text.find_first_of("+,", start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view part = text.substr(start, end - start);
        while (!part.empty() && part.front() == ' ') {
            part.remove_prefix(1);
        }
        while (!part.empty() && part.back() == ' ') {
            part.remove_suffix(1);
        }
        if (!part.empty()) {
            parts.push_back(part);
        }
        start = end + 1;
    }
    return parts;
}

/**
 * @return A time of the data in milliseconds. A positive time is 1 ms at least (code review of stage 3.3b-S1, finding F9):
 *         a TickRate or an end that rounds to 0 would not advance the due list, and the tick would not end.
 */
std::chrono::milliseconds milliseconds_of(float seconds) {
    if (!(seconds > 0.0F)) {
        return std::chrono::milliseconds(0);
    }
    return std::chrono::milliseconds(std::max<long long>(static_cast<long long>(std::lround(seconds * 1000.0F)), 1));
}

/** @return True when the row refuses a dead character (no AllowAddIfDead). */
bool refuses_dead(const gamedata::StatusRow& row) {
    return row.cells.number("AllowAddIfDead") == 0.0F;
}

/** The value names of the five attributes in the data (MOD<name>), in the sequence of the attribute numbers. */
constexpr std::array<const char*, attribute::kCount> kAttributeColumns = {"MODConstitution", "MODFocus", "MODStrength",
                                                                          "MODDexterity", "MODIntelligence"};

bool ticks(const gamedata::StatusRow& row) {
    return row.cells.number("TickRate") > 0.0F
           && (row.cells.number("Health") != 0.0F || row.cells.number("HealthModifierPercent") != 0.0F
               || row.cells.number("Mana") != 0.0F || row.cells.number("ManaModifierPercent") != 0.0F);
}

} // namespace

const char* status_end_text(StatusEnd cause) {
    switch (cause) {
    case StatusEnd::time:
        return "its time ended";
    case StatusEnd::death:
        return "the death of the character";
    case StatusEnd::hits:
        return "the hits of StopOnHitCount";
    case StatusEnd::replaced:
        return "a newer status of its source";
    case StatusEnd::source:
        return "its source went away";
    case StatusEnd::leave:
        return "the character left the world";
    case StatusEnd::command:
        return "a command";
    }
    return "unknown";
}

const char* status_verdict_text(StatusVerdict verdict) {
    switch (verdict) {
    case StatusVerdict::applied:
        return "applied";
    case StatusVerdict::renewed:
        return "a new stack or a new end";
    case StatusVerdict::instant:
        return "one application now";
    case StatusVerdict::unknown:
        return "no such status row";
    case StatusVerdict::dead:
        return "the character is dead";
    case StatusVerdict::cooldown:
        return "the ApplicationCooldown of the row runs";
    case StatusVerdict::full:
        return "DontReplaceStack at StackMax";
    }
    return "unknown";
}

const gamedata::StatusRow* Statuses::row_of(std::string_view status) const {
    const gamedata::AbilityData* data = m_world.options().abilityData;
    return data != nullptr ? data->status(status) : nullptr;
}

StatusVerdict Statuses::apply(Entity& character, std::string_view status, const StatusApply& how) {
    const gamedata::StatusRow* row = row_of(status);
    if (row == nullptr) {
        return counted(StatusVerdict::unknown);
    }
    return apply(character, *row, how);
}

StatusVerdict Statuses::apply(Entity& character, const gamedata::StatusRow& row, const StatusApply& how) {
    if (character.kind != EntityKind::player) {
        return counted(StatusVerdict::unknown);
    }
    if (character.life.dead && refuses_dead(row)) {
        return counted(StatusVerdict::dead);
    }
    const WorldClock::time_point now = m_world.clock().now();
    CharacterState& state = m_characters[character.characterId];
    // ApplicationCooldown: one application of the row on the character in that time. The new end of the cooldown is
    // written only for an application that changes the character (code review of stage 3.3b-S1, finding F7).
    const std::string key = lower(row.id);
    const float wait = row.cells.number("ApplicationCooldown");
    if (wait > 0.0F) {
        if (const auto found = state.applied.find(key); found != state.applied.end() && now < found->second) {
            return counted(StatusVerdict::cooldown);
        }
    }
    const auto start_cooldown = [&] {
        if (wait > 0.0F) {
            state.applied[key] = now + milliseconds_of(wait);
        }
    };
    // The duration: of the source, else BaseDuration; not more than DurationMax.
    float seconds = how.seconds > 0.0F ? how.seconds : row.duration;
    const float most = row.cells.number("DurationMax");
    if (most > 0.0F && seconds > most) {
        seconds = most;
    }
    if (!how.permanent && seconds <= 0.0F) {
        // A row with no duration: one application now.
        start_cooldown();
        give(character, row, how.potency);
        cast(character, row, how.source);
        return counted(StatusVerdict::instant);
    }
    const auto stackMax = static_cast<std::uint8_t>(std::clamp(row.cells.number("StackMax"), 1.0F, 127.0F));
    const auto found = std::find_if(state.statuses.begin(), state.statuses.end(),
                                    [&](const ActiveStatus& status) { return same_text(status.status, row.id); });
    if (found != state.statuses.end()) {
        ActiveStatus& status = *found;
        if (row.cells.number("DontReplaceStack") != 0.0F && status.stacks >= stackMax) {
            return counted(StatusVerdict::full);
        }
        start_cooldown();
        status.stacks = static_cast<std::uint8_t>(std::min<unsigned>(status.stacks + 1U, stackMax));
        status.potency = std::max(status.potency, how.potency);
        if (how.permanent) {
            status.permanent = true;
        } else if (!status.permanent) {
            const WorldClock::time_point fresh = now + milliseconds_of(seconds);
            if (row.cells.number("RefreshDuration") != 0.0F) {
                // RefreshDuration: the time starts again from now, and a shorter refresh keeps the later end (code review
                // of stage 3.3b-S1, finding F6).
                status.ends = std::max(status.ends, fresh);
            } else if (most > 0.0F && row.duration > 0.0F && most > row.duration) {
                // DurationMax above the duration: the time adds up, to DurationMax from now.
                status.ends = std::min(status.ends + milliseconds_of(seconds), now + milliseconds_of(most));
            } else {
                status.ends = std::max(status.ends, fresh);
            }
        }
        status.hitsLeft = row.stopOnHit;
        // A status of the gear keeps its source and its life (code review of stage 3.3b-S1, finding F4): the item that
        // keeps it ends it, and an application of a different source does not make it a status of that source.
        if (status.source.kind != StatusSourceKind::gear || how.source.kind == StatusSourceKind::gear) {
            status.source = how.source;
        }
        status.serial = m_nextSerial++;
        schedule(character.characterId, status);
        publish(character, state);
        return counted(StatusVerdict::renewed);
    }
    start_cooldown();
    ActiveStatus status;
    status.status = row.id;
    status.index = row.index;
    status.trayKey = gamedata::progression_id(lower(row.id));
    status.potency = how.potency;
    status.stacks = static_cast<std::uint8_t>(std::clamp<unsigned>(static_cast<unsigned>(std::max(row.cells.number("InitialStackSize"), 1.0F)), 1U, stackMax));
    status.started = now;
    status.permanent = how.permanent;
    status.ends = how.permanent ? now : now + milliseconds_of(seconds);
    status.hitsLeft = row.stopOnHit;
    status.source = how.source;
    status.serial = m_nextSerial++;
    status.tray = row.cells.number("ShowInUITray") != 0.0F;
    status.nameplate = row.nameplate;
    status.observed = row.nameplate || row.cells.number("AlwaysReplicate") != 0.0F || row.cells.number("ForceReplicateToRemotes") != 0.0F;
    status.replicated = how.source.kind != StatusSourceKind::gear;
    if (ticks(row)) {
        const float tick = row.cells.number("TickRate");
        status.nextTick = row.cells.number("DelayInitialTick") != 0.0F ? now + milliseconds_of(tick) : now;
    }
    state.statuses.push_back(status);
    schedule(character.characterId, status);
    publish(character, state);
    {
        const std::string mark = log::session_mark(character.owner, character.characterId);
        const log::Context context(mark);
        log::write("world: status: %s gets %s (%s), %s, potency %.2f",
                   character.name.c_str(),
                   row.id.c_str(),
                   how.source.id.empty() ? "no source" : how.source.id.c_str(),
                   how.permanent ? "no end" : (std::to_string(static_cast<int>(std::lround(seconds))) + " s").c_str(),
                   static_cast<double>(how.potency));
    }
    if (Entity* again = m_world.find_character(character.characterId)) {
        cast(*again, row, how.source);
    }
    return counted(StatusVerdict::applied);
}

void Statuses::schedule(const std::string& characterId, const ActiveStatus& status) {
    if (!status.permanent) {
        m_due.emplace(status.ends, Due{characterId, status.serial, true});
    }
    if (status.nextTick) {
        m_due.emplace(*status.nextTick, Due{characterId, status.serial, false});
    }
}

void Statuses::give(Entity& character, const gamedata::StatusRow& row, float amount) {
    if (character.life.dead) {
        return;
    }
    const float health = (row.cells.number("Health") + row.cells.number("HealthModifierPercent") * character.vitals.healthMax) * amount;
    if (health > 0.0F) {
        m_world.health().heal_amount(character, health * m_world.gear().heal_factor(character), row.id.c_str());
        ++m_counts.heals;
    }
    const float mana = (row.cells.number("Mana") + row.cells.number("ManaModifierPercent") * character.vitals.manaMax) * amount;
    if (mana != 0.0F) {
        m_world.abilities().change_mana(character, mana, row.id.c_str());
    }
}

void Statuses::cast(Entity& character, const gamedata::StatusRow& row, const StatusSource& source) {
    const std::string_view spell = row.cells.text("CastSpell");
    if (spell.empty()) {
        return;
    }
    if (!m_world.runes().status_spell(character, spell, source)) {
        ++m_counts.spellsNotDone;
    }
}

void Statuses::publish(Entity& character, CharacterState& state, bool refresh) {
    character.statuses = state.statuses;
    m_world.touch(character.id, ComponentKey::status);
    if (refresh) {
        m_world.gear().refresh(character);
    }
}

unsigned Statuses::end_statuses(Entity* character, const std::string& characterId, CharacterState& state,
                                const std::function<bool(const ActiveStatus&)>& select, StatusEnd cause) {
    std::vector<ActiveStatus> ended;
    for (auto status = state.statuses.begin(); status != state.statuses.end();) {
        if (select(*status)) {
            ended.push_back(std::move(*status));
            status = state.statuses.erase(status);
        } else {
            ++status;
        }
    }
    if (ended.empty()) {
        return 0;
    }
    m_counts.ended[static_cast<std::size_t>(cause)] += ended.size();
    // An activation of a heart rune that ends before its time makes no explosion (a death, a leave): the rune system counts it.
    if (cause != StatusEnd::time) {
        for (const ActiveStatus& status : ended) {
            if (status.source.kind == StatusSourceKind::rune) {
                m_world.runes().activation_cancelled();
            }
        }
    }
    if (character != nullptr) {
        publish(*character, state);
        const std::string mark = log::session_mark(character->owner, characterId);
        const log::Context context(mark);
        for (const ActiveStatus& status : ended) {
            log::write("world: status: %s of %s ends: %s", status.status.c_str(), character->name.c_str(), status_end_text(cause));
        }
    }
    // The end status of each status (OnEndStatusEffect): at its time, and at a removal of a row with no
    // DontApplyOnEndEffectOnRemove. A character that is not in the world gets none.
    for (const ActiveStatus& status : ended) {
        const gamedata::StatusRow* row = row_of(status.status);
        Entity* own = m_world.find_character(characterId);
        if (row == nullptr || own == nullptr || (cause != StatusEnd::time && row->cells.number("DontApplyOnEndEffectOnRemove") != 0.0F)) {
            continue;
        }
        for (const std::string_view next : parts_of(row->cells.text("OnEndStatusEffect"))) {
            StatusApply how;
            how.potency = status.potency;
            how.source = StatusSource{StatusSourceKind::status, status.status, 0, status.source.entity};
            apply(*own, next, how);
        }
    }
    return static_cast<unsigned>(ended.size());
}

unsigned Statuses::remove(Entity& character, const std::function<bool(const ActiveStatus&)>& select, StatusEnd cause) {
    const auto found = m_characters.find(character.characterId);
    if (found == m_characters.end()) {
        return 0;
    }
    return end_statuses(&character, character.characterId, found->second, select, cause);
}

unsigned Statuses::remove_rows(Entity& character, const std::vector<std::string>& ids, const std::vector<std::string>& categories,
                               StatusEnd cause) {
    if (ids.empty() && categories.empty()) {
        return 0;
    }
    return remove(
        character,
        [&](const ActiveStatus& status) {
            if (std::any_of(ids.begin(), ids.end(), [&](const std::string& id) { return same_text(id, status.status); })) {
                return true;
            }
            const gamedata::StatusRow* row = row_of(status.status);
            if (row == nullptr) {
                return false;
            }
            const std::vector<std::string_view> own = parts_of(row->cells.text("EffectCategories"));
            return std::any_of(categories.begin(), categories.end(), [&](const std::string& category) {
                return std::any_of(own.begin(), own.end(), [&](std::string_view part) { return same_text(part, category); });
            });
        },
        cause);
}

unsigned Statuses::remove_source(Entity& character, const StatusSource& source, StatusEnd cause) {
    return remove(character, [&](const ActiveStatus& status) { return status.source == source; }, cause);
}

void Statuses::set_gear(Entity& character, const std::vector<GearStatus>& wanted) {
    if (character.kind != EntityKind::player) {
        return;
    }
    const auto known = m_characters.find(character.characterId);
    if (wanted.empty() && (known == m_characters.end()
                           || std::none_of(known->second.statuses.begin(), known->second.statuses.end(),
                                           [](const ActiveStatus& status) { return status.source.kind == StatusSourceKind::gear; }))) {
        return;
    }
    CharacterState& state = m_characters[character.characterId];
    const WorldClock::time_point now = m_world.clock().now();
    // One status of a row: the larger potency of the items that keep it.
    std::vector<GearStatus> rows;
    for (const GearStatus& entry : wanted) {
        if (entry.row == nullptr) {
            continue;
        }
        const auto same = std::find_if(rows.begin(), rows.end(), [&](const GearStatus& other) { return other.row == entry.row; });
        if (same == rows.end()) {
            rows.push_back(entry);
        } else if (entry.potency > same->potency) {
            *same = entry;
        }
    }
    bool changed = false;
    // The statuses of the gear that no item keeps now end (cause: their source went away).
    for (auto status = state.statuses.begin(); status != state.statuses.end();) {
        const bool kept = std::any_of(rows.begin(), rows.end(), [&](const GearStatus& entry) { return same_text(entry.row->id, status->status); });
        if (status->source.kind == StatusSourceKind::gear && !kept) {
            ++m_counts.ended[static_cast<std::size_t>(StatusEnd::source)];
            status = state.statuses.erase(status);
            changed = true;
        } else {
            ++status;
        }
    }
    for (const GearStatus& entry : rows) {
        // The rules of the life state apply to the gear too (code review of stage 3.3b-S1, finding F5): a dead character
        // gets no status that its row refuses for the dead; the revival gives it again (Health::revive refreshes the gear).
        if (character.life.dead && refuses_dead(*entry.row)) {
            continue;
        }
        const auto found = std::find_if(state.statuses.begin(), state.statuses.end(),
                                        [&](const ActiveStatus& status) { return same_text(status.status, entry.row->id); });
        if (found != state.statuses.end()) {
            // A status of a different source with the same row stays (one status of a row): the gear changes nothing.
            if (found->source.kind == StatusSourceKind::gear && (found->potency != entry.potency || !(found->source == entry.source))) {
                found->potency = entry.potency;
                found->source = entry.source;
                changed = true;
            }
            continue;
        }
        ActiveStatus status;
        status.status = entry.row->id;
        status.index = entry.row->index;
        status.trayKey = gamedata::progression_id(lower(entry.row->id));
        status.potency = entry.potency;
        status.started = now;
        status.ends = now;
        status.permanent = true;
        status.source = entry.source;
        status.serial = m_nextSerial++;
        status.tray = entry.row->cells.number("ShowInUITray") != 0.0F;
        status.nameplate = entry.row->nameplate;
        status.replicated = false;
        if (ticks(*entry.row)) {
            status.nextTick = entry.row->cells.number("DelayInitialTick") != 0.0F ? now + milliseconds_of(entry.row->cells.number("TickRate")) : now;
        }
        state.statuses.push_back(status);
        schedule(character.characterId, status);
        ++m_counts.applications[static_cast<std::size_t>(StatusVerdict::applied)];
        changed = true;
    }
    if (changed) {
        publish(character, state, false);
    }
}

void Statuses::died(Entity& character) {
    remove(
        character,
        [&](const ActiveStatus& status) {
            const gamedata::StatusRow* row = row_of(status.status);
            return row != nullptr && row->cells.number("RemoveOnDeath") != 0.0F;
        },
        StatusEnd::death);
}

void Statuses::hit_taken(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found == m_characters.end()) {
        return;
    }
    bool counted_ = false;
    for (ActiveStatus& status : found->second.statuses) {
        if (status.hitsLeft > 0) {
            --status.hitsLeft;
            counted_ = true;
        }
    }
    if (!counted_) {
        return;
    }
    const unsigned ended = end_statuses(
        &character, character.characterId, found->second,
        [&](const ActiveStatus& status) {
            const gamedata::StatusRow* row = row_of(status.status);
            return row != nullptr && row->stopOnHit > 0 && status.hitsLeft == 0;
        },
        StatusEnd::hits);
    if (ended == 0) {
        character.statuses = found->second.statuses;
    }
}

void Statuses::entered(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found == m_characters.end()) {
        return;
    }
    const WorldClock::time_point now = m_world.clock().now();
    // The ticks of a character that was not in the world wait: they start again now (the time of the status runs).
    for (ActiveStatus& status : found->second.statuses) {
        if (status.nextTick && *status.nextTick < now) {
            status.nextTick = now;
            m_due.emplace(now, Due{character.characterId, status.serial, false});
        }
    }
    character.statuses = found->second.statuses;
}

void Statuses::left(Entity& character) {
    const auto found = m_characters.find(character.characterId);
    if (found == m_characters.end()) {
        return;
    }
    end_statuses(nullptr, character.characterId, found->second,
                 [](const ActiveStatus& status) { return status.source.kind == StatusSourceKind::gear; }, StatusEnd::leave);
    character.statuses = found->second.statuses;
}

void Statuses::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const WorldClock::time_point at = m_due.begin()->first;
        const Due due = std::move(m_due.begin()->second);
        m_due.erase(m_due.begin());
        ++m_counts.dueTaken;
        const auto state = m_characters.find(due.characterId);
        if (state == m_characters.end()) {
            continue;
        }
        const auto status = std::find_if(state->second.statuses.begin(), state->second.statuses.end(),
                                         [&](const ActiveStatus& entry) { return entry.serial == due.serial; });
        if (status == state->second.statuses.end()) {
            continue;
        }
        Entity* character = m_world.find_character(due.characterId);
        if (due.end) {
            if (status->permanent || status->ends != at) {
                continue;
            }
            const std::uint64_t serial = due.serial;
            end_statuses(character, due.characterId, state->second, [serial](const ActiveStatus& entry) { return entry.serial == serial; },
                         StatusEnd::time);
            continue;
        }
        if (!status->nextTick || *status->nextTick != at || character == nullptr) {
            // A character that is not in the world: its ticks start again at its next entry (Statuses::entered).
            continue;
        }
        const gamedata::StatusRow* row = row_of(status->status);
        const float rate = row != nullptr ? row->cells.number("TickRate") : 0.0F;
        if (row == nullptr || rate <= 0.0F) {
            continue;
        }
        const float amount = status->potency * static_cast<float>(status->stacks);
        const std::uint64_t serial = status->serial;
        WorldClock::time_point next = *status->nextTick;
        bool alive = true;
        while (alive && next <= now && (status->permanent || next < status->ends)) {
            give(*character, *row, amount);
            ++m_counts.ticks;
            next += milliseconds_of(rate);
            // The heal can change the gear values; the status stays with its number.
            const auto again = std::find_if(state->second.statuses.begin(), state->second.statuses.end(),
                                            [&](const ActiveStatus& entry) { return entry.serial == serial; });
            alive = again != state->second.statuses.end();
        }
        const auto again = std::find_if(state->second.statuses.begin(), state->second.statuses.end(),
                                        [&](const ActiveStatus& entry) { return entry.serial == serial; });
        if (again != state->second.statuses.end() && (again->permanent || next < again->ends)) {
            again->nextTick = next;
            m_due.emplace(next, Due{due.characterId, serial, false});
        } else if (again != state->second.statuses.end()) {
            again->nextTick.reset();
        }
    }
}

StatusValues Statuses::values(const Entity& character) const {
    StatusValues values;
    const gamedata::AbilityData* data = m_world.options().abilityData;
    if (data == nullptr || character.statuses.empty()) {
        return values;
    }
    const WorldClock::time_point now = m_world.clock().now();
    std::vector<std::string> typeNames;
    if (const gamedata::CombatTables* combat = m_world.combat().tables()) {
        for (const gamedata::DamageType& type : combat->data().damageTypes) {
            typeNames.push_back(type.name);
        }
    }
    // The sum of each value with no limit, and of each value of each category that has a limit for it.
    std::unordered_map<std::string, float> free;
    std::map<std::pair<std::string, std::string>, std::pair<float, float>> limited;
    const auto add = [&](const gamedata::StatusRow& row, const std::string& name, float value) {
        if (value == 0.0F) {
            return;
        }
        for (const std::string_view part : parts_of(row.cells.text("EffectCategories"))) {
            const gamedata::StatusCategory* category = data->category(part);
            if (category == nullptr) {
                continue;
            }
            const auto limit = category->limits.find(name);
            if (limit != category->limits.end()) {
                auto& entry = limited[{lower(category->id), name}];
                entry.first += value;
                entry.second = limit->second;
                return;
            }
        }
        free[name] += value;
    };
    for (const ActiveStatus& status : character.statuses) {
        if (!status.permanent && now >= status.ends) {
            continue;
        }
        const gamedata::StatusRow* row = data->status(status.status);
        if (row == nullptr) {
            continue;
        }
        const float factor = status.potency * static_cast<float>(status.stacks);
        for (const char* column : kAttributeColumns) {
            add(*row, column, row->cells.number(column) * factor);
        }
        for (const char* column : {"MaxHealthMod", "PhysicalArmor", "ElementalArmor", "BaseDamageReduction", "SprintSpeedMod"}) {
            add(*row, column, row->cells.number(column) * factor);
        }
        add(*row, "MoveSpeedMod", row->moveSpeed * factor);
        add(*row, "DMGStandard", row->damage * factor);
        for (const std::string& name : typeNames) {
            if (name != "Standard") {
                add(*row, "DMG" + name, row->cells.number("DMG" + name) * factor);
            }
            add(*row, "ABS" + name, row->cells.number("ABS" + name) * factor);
        }
    }
    std::unordered_map<std::string, float> total = free;
    for (const auto& [key, entry] : limited) {
        const float sum = entry.second > 0.0F ? std::min(entry.first, entry.second) : std::max(entry.first, entry.second);
        total[key.second] += sum;
    }
    const auto value = [&](const std::string& name) {
        const auto found = total.find(name);
        return found == total.end() ? 0.0F : found->second;
    };
    for (unsigned type = 0; type < attribute::kCount; ++type) {
        values.attributes[type] = value(kAttributeColumns[type]);
    }
    values.maxHealth = value("MaxHealthMod");
    values.physicalArmor = value("PhysicalArmor");
    values.elementalArmor = value("ElementalArmor");
    values.damageReduction = value("BaseDamageReduction");
    values.moveSpeed = value("MoveSpeedMod");
    values.sprintSpeed = value("SprintSpeedMod");
    for (const std::string& name : typeNames) {
        if (const float part = value("DMG" + name); part != 0.0F) {
            values.damage[name] += part;
        }
        if (const float part = value("ABS" + name); part != 0.0F) {
            values.absorption[name] += part;
        }
    }
    return values;
}

const std::vector<ActiveStatus>* Statuses::statuses_of(const std::string& characterId) const {
    const auto found = m_characters.find(characterId);
    return found == m_characters.end() ? nullptr : &found->second.statuses;
}

bool Statuses::has(const Entity& character, std::string_view status) const {
    const WorldClock::time_point now = m_world.clock().now();
    return std::any_of(character.statuses.begin(), character.statuses.end(), [&](const ActiveStatus& entry) {
        return same_text(entry.status, status) && (entry.permanent || now < entry.ends);
    });
}

} // namespace soulwarden::backend::world
