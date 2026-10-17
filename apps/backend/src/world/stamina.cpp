#include "world/stamina.h"

#include <algorithm>
#include <cmath>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

using Duration = WorldClock::time_point::duration;

Duration seconds_of(float seconds) {
    return std::chrono::duration_cast<Duration>(std::chrono::duration<double>(std::max(seconds, 0.0F)));
}

float seconds_between(WorldClock::time_point from, WorldClock::time_point to) {
    return std::chrono::duration<float>(to - from).count();
}

/** @return The load class of a character for the stamina data (the gear summary: 0 light, 1 medium, 2 heavy, 3 overburdened). */
gamedata::LoadClass load_of(const Entity& character) {
    return static_cast<gamedata::LoadClass>(std::min<std::uint8_t>(character.gear.loadClass, 3));
}

} // namespace

const char* dodge_verdict_text(DodgeVerdict verdict) {
    switch (verdict) {
    case DodgeVerdict::accepted:
        return "accepted";
    case DodgeVerdict::buffered:
        return "accepted, after the buffered window of the dodge that plays";
    case DodgeVerdict::noData:
        return "the backend has no stamina data";
    case DodgeVerdict::dead:
        return "the character is dead or not in the world";
    case DodgeVerdict::locked:
        return "the input of the character is locked (a travel)";
    case DodgeVerdict::overburdened:
        return "the character is overburdened";
    case DodgeVerdict::exhausted:
        return "the character is exhausted";
    case DodgeVerdict::noStamina:
        return "the character has no stamina";
    case DodgeVerdict::dodging:
        return "a dodge plays (not in a window CanDodgeCancel)";
    case DodgeVerdict::encumbered:
        return "the bag of the character is over its weight limit (encumbered)";
    }
    return "unknown";
}

bool Stamina::has_data() const {
    const gamedata::StaminaData* data = m_world.options().staminaData;
    return data != nullptr && data->loaded();
}

float Stamina::maximum(const Entity& character) const {
    // m_initMax of the Stamina component of the player slice (game data). The MaxStamina multiplier of the speed table has no source in
    // this backend (the affixes with MaxStaminaMod are not in the perk export): 1.
    return has_data() ? m_world.options().staminaData->tables().maximum : character.vitals.staminaMax;
}

float Stamina::regeneration_multiplier(const Entity& character) {
    // The stat key StaminaRate (0x0B) of the speed table: 1 + the StaminaRate of the statuses (Status values; client code 0x6888790 reads
    // the key into the regeneration multiplier of its Stamina component).
    return std::max(1.0F + character.gear.staminaRate, 0.0F);
}

float Stamina::regeneration(const Entity& character) const {
    if (!has_data()) {
        return 0.0F;
    }
    const gamedata::StaminaTables& tables = m_world.options().staminaData->tables();
    return tables.regenRate * tables.regenMult[static_cast<std::size_t>(load_of(character))] * regeneration_multiplier(character);
}

bool Stamina::exhausted(const Entity& character) const {
    return character.vitals.exhausted;
}

float Stamina::change(Entity& character, float amount, const char* what) {
    if (character.kind != EntityKind::player || character.life.dead || !std::isfinite(amount) || amount == 0.0F) {
        return 0.0F;
    }
    const WorldClock::time_point now = m_world.clock().now();
    const float maximum = this->maximum(character);
    const float before = character.vitals.stamina;
    character.vitals.staminaMax = maximum;
    character.vitals.stamina = std::clamp(before + amount, 0.0F, maximum);
    const float changed = character.vitals.stamina - before;
    if (has_data() && amount < 0.0F) {
        const gamedata::StaminaTables& tables = m_world.options().staminaData->tables();
        // A payment sets the delay of the regeneration (live check LS5: 1 s; the base vitals StaminaRegenDelay, or
        // StaminaRegenDelayOnEmpty at 0). The value stops at 0 (no debt: live check LS6).
        const bool empty = character.vitals.stamina <= 0.0F;
        character.vitals.staminaRegenFrom = now + seconds_of(empty ? tables.regenDelayOnEmpty : tables.regenDelay);
        ++m_counts.payments;
        if (empty && !character.vitals.exhausted) {
            // At 0: winded for m_maxWindedTime (live check LS6: 2 s), and exhausted until the stamina is full (the action Exhausted).
            character.vitals.exhausted = true;
            character.vitals.windedUntil = now + seconds_of(tables.maxWindedTime);
            ++m_counts.emptied;
        }
    } else if (amount > 0.0F) {
        ++m_counts.gains;
    }
    if (character.vitals.stamina >= maximum) {
        character.vitals.exhausted = false;
        character.vitals.staminaRegenFrom.reset();
        m_regenerating.erase(character.characterId);
    } else {
        watch(character);
    }
    m_world.touch(character.id, ComponentKey::stamina);
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: stamina: %s %s %.1f stamina (%s), stamina %.1f to %.1f of %.1f%s",
               character.name.c_str(),
               amount < 0.0F ? "pays" : "gets",
               static_cast<double>(std::fabs(amount)),
               what,
               static_cast<double>(before),
               static_cast<double>(character.vitals.stamina),
               static_cast<double>(maximum),
               character.vitals.exhausted ? ", exhausted" : "");
    return changed;
}

float Stamina::pay_cost(Entity& character, std::string_view costId, float multiplier, const char* what) {
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    const std::optional<float> base = abilities != nullptr && !costId.empty() ? abilities->stamina_cost(costId) : std::nullopt;
    if (!base || !has_data()) {
        return 0.0F;
    }
    const gamedata::StaminaTables& tables = m_world.options().staminaData->tables();
    const float cost = *base * multiplier * tables.costMult[static_cast<std::size_t>(load_of(character))];
    // A cost of 0 changes nothing (live check LS7: a light attack of the great axe does not stop the regeneration).
    return cost > 0.0F ? -change(character, -cost, what) : 0.0F;
}

void Stamina::watch(Entity& character) {
    if (character.kind != EntityKind::player || !has_data()) {
        return;
    }
    if (!character.vitals.staminaRegenFrom) {
        character.vitals.staminaRegenFrom = m_world.clock().now();
    }
    m_regenerating.emplace(character.characterId, m_world.clock().now());
}

bool Stamina::regenerate(Entity& character, WorldClock::time_point now) {
    const float maximum = this->maximum(character);
    if (character.life.dead) {
        return true;
    }
    if (character.vitals.stamina >= maximum) {
        character.vitals.exhausted = false;
        character.vitals.staminaRegenFrom.reset();
        return false;
    }
    WorldClock::time_point& last = m_regenerating[character.characterId];
    const WorldClock::time_point from =
        std::max({last, character.vitals.staminaRegenFrom.value_or(now), character.vitals.staminaHeldUntil});
    if (now <= from) {
        return true;
    }
    const float rate = regeneration(character);
    last = now;
    if (rate <= 0.0F) {
        return true;
    }
    character.vitals.stamina = std::min(character.vitals.stamina + rate * seconds_between(from, now), maximum);
    ++m_counts.regenerationSteps;
    if (character.vitals.stamina >= maximum) {
        // Full: the exhausted state ends (the action Exhausted ends at 100 percent), and the owner gets the state of a full stamina.
        character.vitals.exhausted = false;
        character.vitals.staminaRegenFrom.reset();
        m_world.touch(character.id, ComponentKey::stamina);
        return false;
    }
    return true;
}

DodgeVerdict Stamina::start_checks(const Entity& character, bool locked) const {
    if (!has_data()) {
        return DodgeVerdict::noData;
    }
    if (character.kind != EntityKind::player || character.life.dead) {
        return DodgeVerdict::dead;
    }
    if (locked) {
        return DodgeVerdict::locked;
    }
    if (load_of(character) == gamedata::LoadClass::overburdened) {
        return DodgeVerdict::overburdened;
    }
    if (m_world.inventory().encumbrance(character).encumbered) {
        return DodgeVerdict::encumbered; // Stage U3: the bag over its weight limit (research Q4).
    }
    if (character.vitals.exhausted) {
        return DodgeVerdict::exhausted;
    }
    if (!(character.vitals.stamina > 0.0F)) {
        return DodgeVerdict::noStamina;
    }
    return DodgeVerdict::accepted;
}

bool Stamina::can_dodge(const Entity& character, bool locked) const {
    const DodgeVerdict verdict = start_checks(character, locked);
    return verdict == DodgeVerdict::accepted || verdict == DodgeVerdict::noData;
}

DodgeVerdict Stamina::dodge(Entity& character, WorldClock::time_point at, bool locked) {
    const auto found = m_dodges.find(character.characterId);
    if (found != m_dodges.end() && found->second.entity == character.id && found->second.fragment != nullptr) {
        DodgeState& playing = found->second;
        const float t = seconds_between(playing.start, at);
        if (playing.next) {
            // A second press in the buffered window: the next dodge waits already (one dodge for each window).
            return counted(DodgeVerdict::buffered);
        }
        const gamedata::DodgeCancel* window = nullptr;
        for (const gamedata::DodgeCancel& cancel : playing.fragment->cancel) {
            if (t >= cancel.start && (!cancel.end || t < *cancel.end)) {
                window = &cancel;
                break;
            }
        }
        if (window == nullptr && t < playing.fragment->canTransition) {
            return counted(DodgeVerdict::dodging);
        }
        if (window != nullptr && window->buffered) {
            const DodgeVerdict checks = start_checks(character, locked);
            if (checks != DodgeVerdict::accepted) {
                return counted(checks);
            }
            // CAGE-TransitionBuffered: the next dodge starts at the end of the window.
            playing.next = playing.start + seconds_of(*window->end);
            playing.nextLocked = locked;
            schedule(character.characterId, *playing.next);
            return counted(DodgeVerdict::buffered);
        }
    }
    const DodgeVerdict checks = start_checks(character, locked);
    if (checks != DodgeVerdict::accepted) {
        return counted(checks);
    }
    start(character, at);
    return counted(DodgeVerdict::accepted);
}

void Stamina::start(Entity& character, WorldClock::time_point at) {
    const gamedata::StaminaData& data = *m_world.options().staminaData;
    const gamedata::DodgeClass* dodge = data.dodge_of(load_of(character));
    const gamedata::DodgeFragment* fragment = dodge != nullptr ? data.fragment_of(*dodge) : nullptr;
    if (fragment == nullptr) {
        return;
    }
    // A buffered next dodge replaces the dodge that plays: the immune window of that dodge stays for the contacts of its time that the world
    // did not test yet (code review of stage 3.4f-B, finding CS-R1).
    if (const auto playing = m_dodges.find(character.characterId); playing != m_dodges.end() && playing->second.fragment != nullptr) {
        keep_window(character.characterId, playing->second, playing->second.immuneEnd);
        // A payment that is due and that the tick did not take yet is not lost at the replacement (finding CS-R5).
        if (!playing->second.paid && at >= playing->second.payAt && playing->second.entity == character.id) {
            playing->second.paid = true;
            change(character, -playing->second.cost, "a dodge");
        }
    }
    DodgeState state;
    state.generation = ++m_generation;
    state.entity = character.id;
    state.fragment = fragment;
    state.costId = dodge->costId;
    state.cost = dodge->cost * data.tables().costMult[static_cast<std::size_t>(load_of(character))];
    state.start = at;
    state.payAt = at + seconds_of(fragment->pay);
    state.immuneEnd = at + seconds_of(fragment->immuneEnd);
    state.transitionAt = at + seconds_of(fragment->canTransition);
    // DisableStaminaRegen of the action Dodge: no regeneration from the start of the dodge to its mark CanTransition.
    character.vitals.staminaHeldUntil = std::max(character.vitals.staminaHeldUntil, state.transitionAt);
    m_dodges[character.characterId] = state;
    schedule(character.characterId, state.payAt);
    schedule(character.characterId, state.transitionAt);
    // The status actions with the tag Dodging (Guarded Sprint) look at the dodge in this tick.
    m_world.passives().recheck(character.characterId);
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: dodge: %s starts a dodge (%s, cost %.1f at %.2f s, immune to %.2f s), stamina %.1f of %.1f",
               character.name.c_str(),
               fragment->name.c_str(),
               static_cast<double>(state.cost),
               static_cast<double>(fragment->pay),
               static_cast<double>(fragment->immuneEnd),
               static_cast<double>(character.vitals.stamina),
               static_cast<double>(maximum(character)));
}

void Stamina::schedule(const std::string& characterId, WorldClock::time_point at) {
    m_due.emplace(at, characterId);
}

void Stamina::step_dodge(const std::string& characterId, WorldClock::time_point now) {
    const auto found = m_dodges.find(characterId);
    if (found == m_dodges.end()) {
        return;
    }
    DodgeState& state = found->second;
    Entity* character = m_world.find_character(characterId);
    if (character == nullptr || character->id != state.entity || character->life.dead) {
        if (!state.paid) {
            ++m_counts.paymentsCancelled;
        }
        m_dodges.erase(found);
        return;
    }
    if (!state.paid && now >= state.payAt) {
        // PayStaminaCost at its mark (0.1 s): the cost of the class through the stamina path.
        state.paid = true;
        change(*character, -state.cost, "a dodge");
        // The delay of the payment counts from the mark, not from the tick that ran it.
        if (character->vitals.staminaRegenFrom && *character->vitals.staminaRegenFrom > state.payAt) {
            const gamedata::StaminaTables& tables = m_world.options().staminaData->tables();
            character->vitals.staminaRegenFrom =
                state.payAt + seconds_of(character->vitals.stamina <= 0.0F ? tables.regenDelayOnEmpty : tables.regenDelay);
        }
    }
    if (!state.transitioned && now >= state.transitionAt) {
        // The mark CanTransition: the regeneration is on again, and SetStaminaRegenDelay of the action sets the delay of its exit (1 s).
        state.transitioned = true;
        m_world.passives().recheck(characterId);
        if (character->vitals.stamina < maximum(*character)) {
            character->vitals.staminaRegenFrom = state.transitionAt + seconds_of(m_world.options().staminaData->tables().dodgeExitDelay);
            watch(*character);
            m_world.touch(character->id, ComponentKey::stamina);
        }
    }
    if (state.next && now >= *state.next) {
        const WorldClock::time_point at = *state.next;
        const bool locked = state.nextLocked;
        state.next.reset();
        const DodgeVerdict checks = start_checks(*character, locked);
        if (checks == DodgeVerdict::accepted) {
            start(*character, at);
        } else {
            ++m_counts.dodges[static_cast<std::size_t>(checks)];
        }
    }
}

bool Stamina::immune(const Entity& character, WorldClock::time_point at) const {
    return immune_since(character, at).has_value();
}

std::optional<WorldClock::time_point> Stamina::immune_since(const Entity& character, WorldClock::time_point at) const {
    const auto found = m_dodges.find(character.characterId);
    if (found != m_dodges.end() && found->second.entity == character.id && at >= found->second.start && at < found->second.immuneEnd) {
        return found->second.start;
    }
    // The window of an earlier dodge of the same entity (finding CS-R1): the contact time selects the window.
    if (const auto past = m_pastWindows.find(character.characterId); past != m_pastWindows.end()) {
        for (const PastWindow& window : past->second) {
            if (window.entity == character.id && at >= window.start && at < window.end) {
                return window.start;
            }
        }
    }
    return std::nullopt;
}

void Stamina::keep_window(const std::string& characterId, const DodgeState& state, WorldClock::time_point end) {
    std::vector<PastWindow>& windows = m_pastWindows[characterId];
    const WorldClock::time_point now = m_world.clock().now();
    std::erase_if(windows, [&](const PastWindow& window) { return window.end + stamina_rules::kPastWindowKeep < now; });
    const WorldClock::time_point last = std::min(end, state.immuneEnd);
    if (last > state.start) {
        windows.push_back(PastWindow{state.entity, state.start, last});
    }
    while (windows.size() > stamina_rules::kPastWindows) {
        windows.erase(windows.begin());
    }
    if (windows.empty()) {
        m_pastWindows.erase(characterId);
    }
}

bool Stamina::dodging(const Entity& character, WorldClock::time_point at) const {
    const auto found = m_dodges.find(character.characterId);
    return found != m_dodges.end() && found->second.entity == character.id && at >= found->second.start && at < found->second.transitionAt;
}

void Stamina::evaded(Entity& character, const Entity& creature, float wouldDeal, const char* what, std::optional<WorldClock::time_point> at) {
    ++m_counts.evaded;
    const WorldClock::time_point when = at.value_or(m_world.clock().now());
    const std::optional<WorldClock::time_point> since = immune_since(character, when);
    const float t = since ? seconds_between(*since, when) : 0.0F;
    const std::string mark = log::session_mark(character.owner, character.characterId);
    const log::Context context(mark);
    log::write("world: dodge: %s avoids the hit of creature %016llx (%s, %.1f damage) at %.2f s of its dodge: no damage",
               character.name.c_str(),
               static_cast<unsigned long long>(creature.id),
               what,
               static_cast<double>(wouldDeal),
               static_cast<double>(t));
    m_world.passives().hit_evaded(character, wouldDeal);
}

void Stamina::entered(Entity& character) {
    m_dodges.erase(character.characterId);
    m_pastWindows.erase(character.characterId);
    m_regenerating.erase(character.characterId);
    character.vitals.staminaMax = maximum(character);
    character.vitals.stamina = character.vitals.staminaMax;
    character.vitals.staminaRegenFrom.reset();
    character.vitals.staminaHeldUntil = {};
    character.vitals.windedUntil = {};
    character.vitals.exhausted = false;
}

void Stamina::interrupted(const std::string& characterId, const char* why) {
    const auto found = m_dodges.find(characterId);
    if (found == m_dodges.end()) {
        return;
    }
    if (!found->second.paid) {
        ++m_counts.paymentsCancelled;
        log::write("world: dodge: the dodge of character %s ends before its payment: %s", characterId.c_str(), why);
    }
    // The immune window to now stays for the contacts before the interruption (finding CS-R1).
    keep_window(characterId, found->second, m_world.clock().now());
    if (Entity* character = m_world.find_character(characterId); character != nullptr && character->id == found->second.entity) {
        // The exit of the action: the regeneration is on again with the delay of its exit.
        const WorldClock::time_point now = m_world.clock().now();
        character->vitals.staminaHeldUntil = now;
        if (character->vitals.stamina < maximum(*character) && has_data()) {
            character->vitals.staminaRegenFrom = now + seconds_of(m_world.options().staminaData->tables().dodgeExitDelay);
            watch(*character);
        }
    }
    m_dodges.erase(found);
}

void Stamina::refilled(Entity& character) {
    if (character.kind != EntityKind::player) {
        return;
    }
    m_dodges.erase(character.characterId);
    m_pastWindows.erase(character.characterId);
    m_regenerating.erase(character.characterId);
    const bool changed = character.vitals.stamina < maximum(character) || character.vitals.exhausted;
    character.vitals.staminaMax = maximum(character);
    character.vitals.stamina = character.vitals.staminaMax;
    character.vitals.staminaRegenFrom.reset();
    character.vitals.staminaHeldUntil = {};
    character.vitals.windedUntil = {};
    character.vitals.exhausted = false;
    if (changed) {
        m_world.touch(character.id, ComponentKey::stamina);
    }
}

void Stamina::refreshed(Entity& character, bool rateChanged) {
    if (character.kind != EntityKind::player || !has_data()) {
        return;
    }
    const float maximum = this->maximum(character);
    if (character.vitals.staminaMax != maximum) {
        character.vitals.staminaMax = maximum;
        character.vitals.stamina = std::min(character.vitals.stamina, maximum);
    }
    // A new regeneration multiplier or load class: the owner gets the state (the client predicts with it), when it has a fragment of the
    // state or the stamina is not full and default.
    if (character.vitals.stamina < maximum || rateChanged) {
        m_world.touch(character.id, ComponentKey::stamina);
    }
    if (character.vitals.stamina < maximum) {
        watch(character);
    }
}

void Stamina::tick() {
    const WorldClock::time_point now = m_world.clock().now();
    while (!m_due.empty() && m_due.begin()->first <= now) {
        const std::string characterId = m_due.begin()->second;
        m_due.erase(m_due.begin());
        step_dodge(characterId, now);
    }
    for (auto entry = m_regenerating.begin(); entry != m_regenerating.end();) {
        Entity* character = m_world.find_character(entry->first);
        if (character == nullptr) {
            entry = m_regenerating.erase(entry);
            continue;
        }
        const std::string characterId = entry->first;
        ++entry;
        if (!regenerate(*character, now)) {
            m_regenerating.erase(characterId);
        }
    }
}

std::optional<Stamina::DodgeView> Stamina::dodge_of(const std::string& characterId) const {
    const auto found = m_dodges.find(characterId);
    if (found == m_dodges.end() || found->second.fragment == nullptr) {
        return std::nullopt;
    }
    const DodgeState& state = found->second;
    return DodgeView{state.generation, state.start, state.immuneEnd, state.transitionAt, state.paid, state.fragment->name, state.cost};
}

} // namespace soulwarden::backend::world
