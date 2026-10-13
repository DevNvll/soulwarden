#include "world/progress.h"

#include <algorithm>
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
    const std::uint32_t health = data.baseHealth + tables()->level(sheet.progression.level)->health
                                 + tables()->constitution_health(sheet.attributes.value(attribute::kConstitution));
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
        vitals.mana = scale(old.mana, old.manaMax, static_cast<float>(data.baseMana));
        vitals.manaMax = static_cast<float>(data.baseMana);
    }
    return vitals;
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
ProgressResult Progress::change(const std::string& characterId, const char* what, bool needsTables, Change&& apply) {
    ProgressResult result;
    Entity* entity = m_world.find_character(characterId);
    // The log line names the session that plays the character.
    const std::string mark = entity != nullptr ? log::session_mark(entity->owner, characterId) : std::string{};
    const log::Context context(mark);
    Sheet sheet;
    if (entity != nullptr) {
        sheet.progression = entity->progression;
        sheet.attributes = entity->attributes;
        result.inWorld = true;
    } else if (m_store.find_character(characterId)) {
        sheet = read_sheet(characterId, false);
    } else {
        result.error = ProgressError::unknownCharacter;
    }
    if (result.ok() && needsTables && tables() == nullptr) {
        result.error = ProgressError::noTables;
    }
    // The change is made on a copy: the values of the entity change only after the row.
    const Sheet before = sheet;
    std::uint64_t cost = 0;
    if (result.ok()) {
        result.error = apply(sheet, cost);
    }
    if (result.ok()) {
        settle(sheet);
        state::ProgressRow row;
        row.level = sheet.progression.level;
        row.experience = sheet.progression.experience;
        row.spent = sheet.attributes.spent;
        if (cost != 0) {
            // The coin of the cost and the row are one transaction. The inventory system
            // changes the wallet.
            const ItemResult paid = m_world.inventory().pay(characterId, cost, [&](std::uint64_t coin) {
                return m_store.set_progress(characterId, row, coin);
            });
            if (paid.error == ItemError::notEnoughCoin) {
                result.error = ProgressError::notEnoughCoin;
            } else if (!paid.ok()) {
                result.error = ProgressError::storeFailed;
            }
        } else if (!m_store.set_progress(characterId, row)) {
            result.error = ProgressError::storeFailed;
        }
    }
    const Sheet& now = result.ok() ? sheet : before;
    result.level = now.progression.level;
    result.experience = now.progression.experience;
    result.unspent = now.attributes.unspent;
    if (!result.ok()) {
        log::write("progress: %s of character %s refused: %s", what, characterId.c_str(), progress_error_text(result.error));
        return result;
    }
    result.levelsGained = sheet.progression.level > before.progression.level
                              ? sheet.progression.level - before.progression.level
                              : 0;
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
        entity->vitals = vitals;
        entity->progression = sheet.progression;
        entity->attributes = sheet.attributes;
    }
    log::write("progress: %s of character %s: level %u, experience %u, %u spent and %u free attribute point(s) (%s)",
               what,
               characterId.c_str(),
               sheet.progression.level,
               sheet.progression.experience,
               spent_total(sheet.attributes),
               sheet.attributes.unspent,
               entity != nullptr ? "the character is in the world" : "the row only");
    return result;
}

ProgressResult Progress::set_level(const std::string& characterId, std::uint32_t level) {
    const std::string what = "level " + std::to_string(level);
    return change(characterId, what.c_str(), true, [&](Sheet& sheet, std::uint64_t&) {
        if (level == 0 || level > tables()->level_cap()) {
            return ProgressError::badLevel;
        }
        sheet.progression.level = level;
        sheet.progression.experience = tables()->level(level)->experience;
        return ProgressError::none;
    });
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
    return change(characterId, what.c_str(), true, [&](Sheet& sheet, std::uint64_t&) {
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
    });
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
