#include "world/progress.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <set>

#include "core/logging/log.h"
#include "world/world.h"

namespace soulwarden::backend::world {
namespace {

std::uint32_t spent_total(const Attributes& attributes) {
    return std::accumulate(attributes.spent.begin(), attributes.spent.end(), 0U);
}

} // namespace

const char* progress_error_text(ProgressError error) {
    switch (error) {
    case ProgressError::none:
        return "no error";
    case ProgressError::unknownCharacter:
        return "the store has no character with this id";
    case ProgressError::noTables:
        return "the backend has no progression tables (make them with tools/gamedata/progression.sh)";
    case ProgressError::badLevel:
        return "the level is not a level of a player";
    case ProgressError::badAmount:
        return "the amount is not correct";
    case ProgressError::badAttribute:
        return "the attribute or its value is not correct";
    case ProgressError::notEnoughPoints:
        return "the request spends more points than the character has";
    case ProgressError::badRequest:
        return "the request is not correct";
    case ProgressError::notEnoughCoin:
        return "the character does not have the coin for the cost";
    case ProgressError::unknownProgression:
        return "the game data has no weapon mastery and no trade skill with this id";
    case ProgressError::wrongKind:
        return "the id is not of the kind that the request needs (a weapon or a trade skill)";
    case ProgressError::badRank:
        return "the level or the experience is not in the range of this weapon mastery or trade skill";
    case ProgressError::unknownNode:
        return "the node is not a node of the tree of this weapon";
    case ProgressError::nodeOwned:
        return "the character has the node already";
    case ProgressError::nodeLocked:
        return "a condition of the node is not met";
    case ProgressError::badSlot:
        return "the place is not a place of the ability bar";
    case ProgressError::notActive:
        return "the ability is not an active ability of the tree of this weapon";
    case ProgressError::notOwned:
        return "the character does not have the node of the ability";
    case ProgressError::costNotPaid:
        return "the reset has a cost in a currency that the backend does not have";
    case ProgressError::storeFailed:
        return "the database did not accept the change";
    }
    return "unknown error";
}

Progress::Progress(World& world, state::Store& store, const gamedata::ProgressionTables* tables)
    : m_world(world), m_store(store), m_source(tables) {}

void Progress::settle(Sheet& sheet) const {
    Progression& progression = sheet.progression;
    Attributes& attributes = sheet.attributes;
    progression.level = std::max(progression.level, 1U);
    if (tables() == nullptr) {
        // No rule of a table: the stored values go to the client as they are, and no point is free.
        attributes.minimum = gamedata::kDefaultAttributeMinimum;
        attributes.unspent = 0;
        return;
    }
    progression.level = std::min(progression.level, tables()->level_cap());
    // The experience of a level is in the range of the level. At the largest level it stays
    // at the start of the level: the client then shows no experience for a next level.
    const std::uint32_t start = tables()->level(progression.level)->experience;
    const gamedata::LevelRow* next =
        progression.level < tables()->level_cap() ? tables()->level(progression.level + 1) : nullptr;
    if (next == nullptr || progression.experience < start || progression.experience >= next->experience) {
        progression.experience = start;
    }
    attributes.minimum = tables()->attribute_minimum();
    const std::uint32_t range = tables()->attribute_maximum() - attributes.minimum;
    for (std::uint32_t& points : attributes.spent) {
        points = std::min(points, range);
    }
    // A character does not have more spent points than its level gives: then all are free again.
    const std::uint32_t total = tables()->attribute_points(progression.level);
    if (spent_total(attributes) > total) {
        attributes.spent.fill(0);
    }
    attributes.unspent = total - spent_total(attributes);
}

Vitals Progress::vitals_of(const Sheet& sheet, const Vitals& old) const {
    Vitals vitals = old;
    if (tables() == nullptr) {
        return vitals;
    }
    const gamedata::ProgressionData& data = tables()->data();
    // The health of the perks and the effects (stage 1.3b, world/gear.h): a part of the health of the base, the level,
    // and the constitution (a rule of this project: "of the base value" of the tooltip of the client).
    const std::uint32_t plain = data.baseHealth + tables()->level(sheet.progression.level)->health
                                + tables()->constitution_health(sheet.attributes.value(attribute::kConstitution));
    const auto health = static_cast<std::uint32_t>(std::lround(static_cast<float>(plain) * std::max(1.0F + sheet.healthPart, 0.0F)));
    // The amount keeps its part of the maximum: full health stays full health.
    const auto scale = [](float amount, float oldMaximum, float maximum) {
        return oldMaximum > 0.0F ? std::min(maximum, amount * maximum / oldMaximum) : maximum;
    };
    if (health != 0) {
        vitals.health = scale(old.health, old.healthMax, static_cast<float>(health));
        vitals.healthMax = static_cast<float>(health);
    }
    if (data.baseStamina != 0) {
        vitals.stamina = scale(old.stamina, old.staminaMax, static_cast<float>(data.baseStamina));
        vitals.staminaMax = static_cast<float>(data.baseStamina);
    }
    if (data.baseMana != 0) {
        // The mana of the client (stage 3.5b.2, live check of the mana meter): the maximum is the base times 1 plus the MaxMana parts of
        // the threshold abilities, the perks, and the effects (the client computes the same maximum from the ManaMax of the vitals state
        // and its stat MaxMana, 0x676B310, and ignores the maximum of the mana state), and the regeneration is the base rate times 1 plus
        // the ManaRate parts. The amount keeps its part of the maximum (the client does the same at a change of its maximum).
        const auto [maxMana, manaRate] = threshold_mana(sheet);
        const float maximum = static_cast<float>(data.baseMana) * std::max(1.0F + sheet.manaPart + maxMana, 0.0F);
        vitals.mana = scale(old.mana, old.manaMax, maximum);
        vitals.manaMax = maximum;
        vitals.manaBase = static_cast<float>(data.baseMana);
        vitals.manaRateFactor = std::max(1.0F + sheet.manaRatePart + manaRate, 0.0F);
    }
    return vitals;
}

std::pair<float, float> Progress::threshold_mana(const Sheet& sheet) const {
    std::pair<float, float> parts{0.0F, 0.0F};
    const gamedata::AbilityData* abilities = m_world.options().abilityData;
    if (tables() == nullptr || abilities == nullptr || !abilities->loaded()) {
        return parts;
    }
    static constexpr std::array<std::pair<unsigned, const char*>, attribute::kCount> kNames{
        {{attribute::kStrength, "strength"}, {attribute::kDexterity, "dexterity"}, {attribute::kIntelligence, "intelligence"},
         {attribute::kFocus, "focus"}, {attribute::kConstitution, "constitution"}}};
    const auto& thresholds = tables()->data().attributeAbilities;
    for (const auto& [type, name] : kNames) {
        const auto found = thresholds.find(name);
        if (found == thresholds.end()) {
            continue;
        }
        const std::uint32_t value = sheet.attributes.value(type);
        for (const gamedata::AttributeThreshold& threshold : found->second) {
            if (threshold.value > value) {
                break;
            }
            for (const std::string& id : threshold.abilities) {
                if (const gamedata::AbilityRow* row = abilities->ability_row(id)) {
                    parts.first += row->cells.number("MaxMana");
                    parts.second += row->cells.number("ManaRate");
                }
            }
        }
    }
    return parts;
}

Progress::Sheet Progress::read_sheet(const std::string& characterId, bool log) const {
    const state::ProgressRow row = m_store.progress(characterId);
    Sheet sheet;
    sheet.progression.level = row.level;
    sheet.progression.experience = row.experience;
    sheet.attributes.spent = row.spent;
    settle(sheet);
    if (log && (sheet.progression.level != row.level || sheet.progression.experience != row.experience
                || sheet.attributes.spent != row.spent)) {
        log::write("progress: the row of character %s does not agree with the progression tables: level %u, "
                   "experience %u, and %u spent attribute point(s) are in use (the row stays)",
                   characterId.c_str(),
                   sheet.progression.level,
                   sheet.progression.experience,
                   spent_total(sheet.attributes));
    }
    return sheet;
}

void Progress::load(Entity& entity) {
    const Sheet sheet = read_sheet(entity.characterId, true);
    entity.progression = sheet.progression;
    entity.attributes = sheet.attributes;
    entity.vitals = vitals_of(sheet, entity.vitals);
    log::write("progress: character %s has level %u, experience %u, %u spent and %u free attribute point(s), "
               "maximum health %.0f%s",
               entity.characterId.c_str(),
               entity.progression.level,
               entity.progression.experience,
               spent_total(entity.attributes),
               entity.attributes.unspent,
               entity.vitals.healthMax,
               tables() == nullptr ? " (no progression tables)" : "");
}

void Progress::refresh_vitals(Entity& entity) {
    Sheet sheet;
    sheet.progression = entity.progression;
    sheet.attributes = entity.attributes;
    sheet.healthPart = entity.gear.healthPart;
    sheet.manaPart = entity.gear.manaPart;
    sheet.manaRatePart = entity.gear.manaRatePart;
    const Vitals vitals = vitals_of(sheet, entity.vitals);
    const bool health = vitals.healthMax != entity.vitals.healthMax || vitals.health != entity.vitals.health;
    const bool mana = vitals.manaMax != entity.vitals.manaMax || vitals.mana != entity.vitals.mana
                      || vitals.manaRateFactor != entity.vitals.manaRateFactor;
    if (health || mana) {
        entity.vitals = vitals;
        m_world.touch(entity.id, ComponentKey::vitals);
    }
    if (mana) {
        // The mana state and the regeneration of the one mana path (world/abilities.h: the new maximum or rate).
        m_world.abilities().mana_changed(entity);
    }
}

std::optional<ProgressView> Progress::view(const std::string& characterId) {
    ProgressView view;
    if (const Entity* entity = m_world.find_character(characterId)) {
        view.progression = entity->progression;
        view.attributes = entity->attributes;
        view.vitals = entity->vitals;
        view.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        const Sheet sheet = read_sheet(characterId, false);
        view.progression = sheet.progression;
        view.attributes = sheet.attributes;
        view.vitals = vitals_of(sheet, Vitals{});
    } else {
        return std::nullopt;
    }
    if (tables() != nullptr) {
        view.levelExperience = tables()->level(view.progression.level)->experience;
        if (view.progression.level < tables()->level_cap()) {
            view.nextLevelExperience = tables()->level(view.progression.level + 1)->experience;
        }
    }
    return view;
}

template <class Change>
Progress::Plan Progress::stage(const std::string& characterId, bool needsTables, Change&& apply) {
    Plan plan;
    plan.characterId = characterId;
    Entity* entity = m_world.find_character(characterId);
    if (entity != nullptr) {
        plan.sheet.progression = entity->progression;
        plan.sheet.attributes = entity->attributes;
        plan.sheet.healthPart = entity->gear.healthPart;
        plan.sheet.manaPart = entity->gear.manaPart;
        plan.sheet.manaRatePart = entity->gear.manaRatePart;
        plan.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        plan.sheet = read_sheet(characterId, false);
    } else {
        plan.error = ProgressError::unknownCharacter;
    }
    if (plan.ok() && needsTables && tables() == nullptr) {
        plan.error = ProgressError::noTables;
    }
    // The change is made on a copy: the values of the entity change only after the row.
    plan.before = plan.sheet;
    if (plan.ok()) {
        plan.error = apply(plan.sheet, plan.cost);
    }
    if (plan.ok()) {
        settle(plan.sheet);
    }
    return plan;
}

bool Progress::write(const Plan& plan) {
    state::ProgressRow row;
    row.level = plan.sheet.progression.level;
    row.experience = plan.sheet.progression.experience;
    row.spent = plan.sheet.attributes.spent;
    return m_store.set_progress(plan.characterId, row);
}

ProgressResult Progress::finish(Plan& plan, const char* what) {
    ProgressResult result;
    result.inWorld = plan.inWorld;
    const Sheet& sheet = plan.sheet;
    const Sheet& before = plan.before;
    result.level = sheet.progression.level;
    result.experience = sheet.progression.experience;
    result.unspent = sheet.attributes.unspent;
    result.levelsGained = sheet.progression.level > before.progression.level ? sheet.progression.level - before.progression.level : 0;
    Entity* entity = plan.inWorld ? m_world.find_character(plan.characterId) : nullptr;
    if (entity != nullptr) {
        // One mark for each state that changed.
        if (sheet.progression.level != before.progression.level
            || sheet.progression.experience != before.progression.experience) {
            m_world.touch(entity->id, ComponentKey::progression);
        }
        if (sheet.attributes.spent != before.attributes.spent || sheet.attributes.unspent != before.attributes.unspent) {
            m_world.touch(entity->id, ComponentKey::attribute);
        }
        const Vitals vitals = vitals_of(sheet, entity->vitals);
        if (vitals.healthMax != entity->vitals.healthMax || vitals.health != entity->vitals.health) {
            m_world.touch(entity->id, ComponentKey::vitals);
        }
        const bool manaChanged = vitals.manaMax != entity->vitals.manaMax || vitals.mana != entity->vitals.mana
                                 || vitals.manaRateFactor != entity->vitals.manaRateFactor;
        if (manaChanged) {
            // The vitals state has the mana too: the two states keep the same values.
            m_world.touch(entity->id, ComponentKey::vitals);
        }
        entity->vitals = vitals;
        if (manaChanged) {
            m_world.abilities().mana_changed(*entity);
        }
        entity->progression = sheet.progression;
        entity->attributes = sheet.attributes;
        // Code review of stage B4, finding B4-R1: the threshold abilities of the attributes are values of the gear (Con_Bonus_100_1:
        // MaxHealth 0.03): after the stored change the gear and the vitals come again from the new attributes (the vitals above have the
        // health part of the old attributes).
        if (sheet.attributes.spent != before.attributes.spent || sheet.progression.level != before.progression.level) {
            m_world.gear().refresh(*entity);
        }
    }
    log::write("progress: %s of character %s: level %u, experience %u, %u spent and %u free attribute point(s) (%s)",
               what,
               plan.characterId.c_str(),
               sheet.progression.level,
               sheet.progression.experience,
               spent_total(sheet.attributes),
               sheet.attributes.unspent,
               entity != nullptr ? "the character is in the world" : "the row only");
    return result;
}

template <class Change>
ProgressResult Progress::change(const std::string& characterId, const char* what, bool needsTables, Change&& apply) {
    Entity* entity = m_world.find_character(characterId);
    // The log line names the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    Plan plan = stage(characterId, needsTables, std::forward<Change>(apply));
    if (plan.ok() && plan.cost != 0) {
        // The coin of the cost and the row are one transaction. The inventory system changes the wallet.
        state::ProgressRow row;
        row.level = plan.sheet.progression.level;
        row.experience = plan.sheet.progression.experience;
        row.spent = plan.sheet.attributes.spent;
        const ItemResult paid = m_world.inventory().pay(characterId, plan.cost, [&](std::uint64_t coin) {
            return m_store.set_progress(characterId, row, coin);
        });
        if (paid.error == ItemError::notEnoughCoin) {
            plan.error = ProgressError::notEnoughCoin;
        } else if (!paid.ok()) {
            plan.error = ProgressError::storeFailed;
        }
    } else if (plan.ok() && !write(plan)) {
        plan.error = ProgressError::storeFailed;
    }
    if (!plan.ok()) {
        ProgressResult result;
        result.error = plan.error;
        result.inWorld = plan.inWorld;
        result.level = plan.before.progression.level;
        result.experience = plan.before.progression.experience;
        result.unspent = plan.before.attributes.unspent;
        log::write("progress: %s of character %s refused: %s", what, characterId.c_str(), progress_error_text(result.error));
        return result;
    }
    return finish(plan, what);
}

ProgressError Progress::level_into(Sheet& sheet, std::uint32_t level) const {
    if (level == 0 || level > tables()->level_cap()) {
        return ProgressError::badLevel;
    }
    sheet.progression.level = level;
    sheet.progression.experience = tables()->level(level)->experience;
    return ProgressError::none;
}

ProgressError Progress::attribute_into(Sheet& sheet, unsigned type, std::uint32_t value) const {
    Attributes& attributes = sheet.attributes;
    if (type >= attribute::kCount || value < tables()->attribute_minimum() || value > tables()->attribute_maximum()) {
        return ProgressError::badAttribute;
    }
    const std::uint32_t points = value - tables()->attribute_minimum();
    const std::uint32_t others = spent_total(attributes) - attributes.spent[type];
    if (others + points > tables()->attribute_points(sheet.progression.level)) {
        return ProgressError::notEnoughPoints;
    }
    attributes.spent[type] = points;
    return ProgressError::none;
}

ProgressResult Progress::set_level(const std::string& characterId, std::uint32_t level) {
    const std::string what = "level " + std::to_string(level);
    return change(characterId, what.c_str(), true, [&](Sheet& sheet, std::uint64_t&) { return level_into(sheet, level); });
}

Progress::Plan Progress::plan_sheet(const std::string& characterId,
                                    std::uint32_t level,
                                    const std::array<std::uint32_t, attribute::kCount>& values) {
    return stage(characterId, true, [&](Sheet& sheet, std::uint64_t&) { return sheet_into(sheet, level, values); });
}

ProgressError Progress::sheet_into(Sheet& sheet, std::uint32_t level, const std::array<std::uint32_t, attribute::kCount>& values) const {
    if (const ProgressError error = level_into(sheet, level); error != ProgressError::none) {
        return error;
    }
    // The new values together: the spent points of the old values do not count (a value that goes down first frees its points).
    sheet.attributes.spent.fill(0);
    for (unsigned type = 0; type < attribute::kCount; ++type) {
        if (const ProgressError error = attribute_into(sheet, type, values[type]); error != ProgressError::none) {
            return error;
        }
    }
    return ProgressError::none;
}

ProgressError Progress::check_sheet(std::uint32_t level, const std::array<std::uint32_t, attribute::kCount>& values) const {
    if (tables() == nullptr) {
        return ProgressError::noTables;
    }
    Sheet sheet;
    return sheet_into(sheet, level, values);
}

ProgressResult Progress::add_experience(const std::string& characterId, std::uint64_t amount) {
    const std::string what = "gain of " + std::to_string(amount) + " experience";
    return change(characterId, what.c_str(), true, [&](Sheet& sheet, std::uint64_t&) {
        if (amount == 0 || amount > kExperienceLimit) {
            return ProgressError::badAmount;
        }
        Progression& progression = sheet.progression;
        const std::uint32_t cap = tables()->level_cap();
        const std::uint64_t total =
            std::min<std::uint64_t>(progression.experience + amount, tables()->level(cap)->experience);
        progression.experience = static_cast<std::uint32_t>(total);
        while (progression.level < cap && progression.experience >= tables()->level(progression.level + 1)->experience) {
            ++progression.level;
        }
        return ProgressError::none;
    });
}

ProgressResult Progress::set_attribute(const std::string& characterId, unsigned type, std::uint32_t value) {
    const std::string what = "attribute " + std::to_string(type) + " to " + std::to_string(value);
    return change(characterId, what.c_str(), true, [&](Sheet& sheet, std::uint64_t&) { return attribute_into(sheet, type, value); });
}

ProgressResult Progress::spend(const std::string& characterId, std::span<const AttributeSpend> entries) {
    const ProgressResult result = change(characterId, "spend of attribute points", true, [&](Sheet& sheet, std::uint64_t&) {
        Attributes& attributes = sheet.attributes;
        std::set<std::uint32_t> types;
        std::uint64_t sum = 0;
        for (const AttributeSpend& entry : entries) {
            if (entry.type >= attribute::kCount) {
                return ProgressError::badAttribute;
            }
            if (entry.points > kSpendLimit || !types.insert(entry.type).second) {
                return ProgressError::badRequest;
            }
            sum += entry.points;
        }
        if (entries.empty()) {
            return ProgressError::badRequest;
        }
        // The rule of the client for its own values: a request for more points than the
        // free points changes nothing.
        if (sum > attributes.unspent) {
            return ProgressError::notEnoughPoints;
        }
        const std::uint32_t range = tables()->attribute_maximum() - tables()->attribute_minimum();
        for (const AttributeSpend& entry : entries) {
            attributes.spent[entry.type] = std::min(attributes.spent[entry.type] + entry.points, range);
        }
        return ProgressError::none;
    });
    if (!result.ok()) {
        // The client has its own result of the request already: the state puts its values back.
        if (const Entity* entity = m_world.find_character(characterId)) {
            m_world.touch(entity->id, ComponentKey::attribute);
        }
    }
    return result;
}

ProgressResult Progress::reset_attributes(const std::string& characterId) {
    const ProgressResult result = change(characterId, "reset of the attribute points", true, [&](Sheet& sheet, std::uint64_t& cost) {
        if (spent_total(sheet.attributes) == 0) {
            return ProgressError::none; // Nothing to reset, and no cost.
        }
        cost = tables()->level(sheet.progression.level)->resetCost;
        sheet.attributes.spent.fill(0);
        return ProgressError::none;
    });
    if (!result.ok()) {
        if (const Entity* entity = m_world.find_character(characterId)) {
            m_world.touch(entity->id, ComponentKey::attribute);
        }
    }
    return result;
}

} // namespace soulwarden::backend::world
