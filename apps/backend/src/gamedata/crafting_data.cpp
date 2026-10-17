#include "gamedata/crafting_data.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>

#include <nlohmann/json.hpp>

#include "gamedata/item_catalog.h"

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The forms of the file that this code reads (tools/gamedata/crafting.py): 1 (stage 3.7a) and 2 (stage 3.7b). */
constexpr std::uint64_t kFirstFormat = 1;
constexpr std::uint64_t kFormat = 2;
/** The limit of a RollBonus and of a bonus item value of an item (the tables have 0 to 2,500; the tool refuses more than 1,000,000). */
constexpr double kLargestBonus = 1000000.0;
/** The client reads an achievement vector of 0x2000000 bytes at most (gamedata/progression_tables.h, kAchievementBitLimit). */
constexpr std::uint64_t kLargestAchievementBit = 0x2000000ULL * 8 - 1;
/** The limits of the request reader of the client (research crafting.md 4.1) and of the tables. */
constexpr std::uint64_t kLargestQuantity = 50000;
constexpr std::uint64_t kLargestLevel = 1000;
constexpr std::uint64_t kLargestGearScore = 10000;
constexpr std::uint64_t kLargestFee = 100000000;
constexpr std::size_t kLargestIngredients = 7;
constexpr std::size_t kLargestStationKeys = 4;

std::string lower_ascii(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

bool whole_in(const json& value, std::uint64_t high, std::uint64_t& out) {
    if (!value.is_number_unsigned()) {
        return false;
    }
    out = value.get<std::uint64_t>();
    return out <= high;
}

bool text_in(const json& object, const char* member, std::string& out) {
    if (!object.contains(member) || !object[member].is_string()) {
        return false;
    }
    out = object[member].get<std::string>();
    return true;
}

bool flag_in(const json& object, const char* member, bool& out) {
    if (!object.contains(member) || !object[member].is_boolean()) {
        return false;
    }
    out = object[member].get<bool>();
    return true;
}

bool number_in(const json& object, const char* member, std::uint64_t high, std::uint32_t& out) {
    std::uint64_t value = 0;
    if (!object.contains(member) || !whole_in(object[member], high, value)) {
        return false;
    }
    out = static_cast<std::uint32_t>(value);
    return true;
}

/**
 * @return The first rule of a recipe that the backend does not apply (each one is a cause of the tool, crafting.py recipe_entry), or null.
 *         The checks of the request do not test these rules: the cause list of the file is the gate. A file of format 1 (stage 3.7a) has
 *         the rules of stage 3.7a: refining, craft-all, an event, a first-craft achievement, and a cooldown are not applied in it.
 */
const char* unapplied_rule(const Recipe& recipe, std::uint64_t format) {
    if (recipe.procedural) {
        return "IsProcedural";
    }
    if (format < kFormat) {
        if (recipe.refining) {
            return "IsRefining";
        }
        if (recipe.craftAll) {
            return "CraftAll";
        }
        if (!recipe.event.empty()) {
            return "GameEventID";
        }
        if (!recipe.firstCraftAchievement.empty()) {
            return "FirstCraftAchievementId";
        }
        if (recipe.cooldownSeconds > 0 || recipe.cooldownQuantity > 0) {
            return "CooldownSeconds or CooldownQuantity";
        }
    }
    if (recipe.perkItems > 0 || recipe.attributeSlots > 0 || recipe.perkCost > 0) {
        return "MaxPerkItemsAllowed, AttributeOnlyPerkItemSlotCount, or PerkCost";
    }
    if (recipe.baseGearScore > 0) {
        return "BaseGearScore";
    }
    if (!recipe.known || !recipe.requiredAchievement.empty() || !recipe.hiddenByAchievement.empty() || !recipe.unlockedAchievement.empty()) {
        return "an unlock (bKnownByDefault false or an achievement)";
    }
    if (recipe.skipGrant) {
        return "SkipGrantItems";
    }
    if (!recipe.worldTags.empty()) {
        return "RequiredWorldTags";
    }
    if (recipe.temporary) {
        return "IsTemporary";
    }
    return nullptr;
}

std::optional<IngredientType> ingredient_type(std::string_view text) {
    if (text == "Item") {
        return IngredientType::item;
    }
    if (text == "Currency") {
        return IngredientType::currency;
    }
    if (text == "Category_Only") {
        return IngredientType::categoryOnly;
    }
    if (text == "Category_Any") {
        return IngredientType::categoryAny;
    }
    return std::nullopt;
}

} // namespace

bool CraftingData::load(const std::string& directory) {
    std::ifstream input(std::filesystem::path(directory) / kCraftingDataFile, std::ios::binary);
    if (!input) {
        *this = CraftingData{};
        m_error = "the file is not there";
        return false;
    }
    std::ostringstream text;
    text << input.rdbuf();
    return set(text.str());
}

bool CraftingData::set(const std::string& document) {
    *this = CraftingData{};
    if (!parse(document)) {
        const std::string error = m_error;
        *this = CraftingData{};
        m_error = error;
        return false;
    }
    m_loaded = true;
    return true;
}

bool CraftingData::parse(const std::string& text) {
    const json document = json::parse(text, nullptr, false);
    const auto fail = [&](std::string cause) {
        m_error = std::move(cause);
        return false;
    };
    if (!document.is_object()) {
        return fail("the file is not a JSON object");
    }
    std::uint64_t format = 0;
    if (!document.contains("format") || !whole_in(document["format"], 1000, format) || (format != kFirstFormat && format != kFormat)) {
        return fail("the file does not have the format 1 or 2");
    }
    m_format = format;
    for (const char* member : {"stations", "missingStations", "recipes", "categories"}) {
        if (!document.contains(member)) {
            return fail(std::string("the file has no member ") + member);
        }
    }
    if (format == kFormat) {
        for (const char* member : {"events", "ranks", "achievements", "materials"}) {
            if (!document.contains(member) || !document[member].is_object()) {
                return fail(std::string("the file of format 2 has no object ") + member);
            }
        }
        // The events of the recipes: the trade skill and the reward of one unit, the character experience, the standing.
        for (const auto& [id, value] : document["events"].items()) {
            CraftEvent event;
            if (!value.is_object() || !text_in(value, "progression", event.progression)) {
                return fail("the event " + id + " has no progression text");
            }
            for (const auto& [member, target] : {std::pair<const char*, float*>{"reward", &event.reward}, {"xp", &event.experience},
                                                 {"standing", &event.standing}}) {
                if (!value.contains(member) || !value[member].is_number() || !std::isfinite(value[member].get<double>())
                    || value[member].get<double>() < 0.0 || value[member].get<double>() > kLargestBonus) {
                    return fail("the event " + id + ": " + member + " is not a number of 0 to 1000000");
                }
                *target = static_cast<float>(value[member].get<double>());
            }
            m_events[lower_ascii(id)] = event;
        }
        // The RollBonus of each rank of each trade skill (the rows 0 to N).
        for (const auto& [skill, values] : document["ranks"].items()) {
            if (!values.is_array() || values.empty()) {
                return fail("the rank table " + skill + " is not a list of ranks");
            }
            std::vector<float>& ranks = m_ranks[lower_ascii(skill)];
            for (const json& value : values) {
                if (!value.is_number() || !std::isfinite(value.get<double>()) || value.get<double>() < 0.0 || value.get<double>() > kLargestBonus) {
                    return fail("the rank table " + skill + " has a RollBonus that is not a number of 0 to 1000000");
                }
                ranks.push_back(static_cast<float>(value.get<double>()));
            }
        }
        for (const auto& [id, value] : document["achievements"].items()) {
            std::uint64_t index = 0;
            if (!whole_in(value, kLargestAchievementBit, index)) {
                return fail("the achievement " + id + " has no bit of the achievement vector");
            }
            m_achievements[lower_ascii(id)] = static_cast<std::uint32_t>(index);
        }
        for (const auto& [item, values] : document["materials"].items()) {
            std::array<std::uint64_t, 3> parts{};
            if (!values.is_array() || values.size() != 3) {
                return fail("the material " + item + " is not [EXTRA, PRIMARY, SECONDARY]");
            }
            for (std::size_t index = 0; index < 3; ++index) {
                if (!whole_in(values[index], static_cast<std::uint64_t>(kLargestBonus), parts[index])) {
                    return fail("the material " + item + " has a value that is not a whole number of 0 to 1000000");
                }
            }
            m_materials[lower_ascii(item)] = MaterialBonus{static_cast<std::int32_t>(parts[0]), static_cast<std::int32_t>(parts[1]),
                                                           static_cast<std::int32_t>(parts[2])};
        }
    }
    // The station rows.
    const json& stations = document["stations"];
    if (!stations.is_array() || stations.empty()) {
        return fail("the member stations is not a list of rows");
    }
    for (const json& row : stations) {
        CraftingStation station;
        if (!row.is_object() || !text_in(row, "name", station.name) || station.name.empty() || !row.contains("types")
            || !row["types"].is_array() || row["types"].empty()) {
            return fail("a station row has no name or no type");
        }
        for (const json& type : row["types"]) {
            if (!type.is_string() || type.get<std::string>().empty()) {
                return fail("the station row " + station.name + " has a type that is not a text");
            }
            station.types.push_back(lower_ascii(type.get<std::string>()));
        }
        if (row.contains("discount") && row["discount"].is_number()) {
            const double discount = row["discount"].get<double>();
            if (!std::isfinite(discount) || discount < 0.0 || discount > 1.0) {
                return fail("the station row " + station.name + " has a discount outside 0 to 1");
            }
            station.discount = static_cast<float>(discount);
        }
        if (row.contains("mannequin") && row["mannequin"].is_string()) {
            station.mannequin = row["mannequin"].get<std::string>();
        }
        if (!m_stationByName.emplace(lower_ascii(station.name), m_stations.size()).second) {
            return fail("two station rows have the name " + station.name);
        }
        m_stations.push_back(std::move(station));
    }
    const json& missing = document["missingStations"];
    if (!missing.is_object()) {
        return fail("the member missingStations is not an object");
    }
    for (const auto& [key, slices] : missing.items()) {
        if (!slices.is_array()) {
            return fail("the missing station " + key + " has no slice list");
        }
        std::vector<std::string>& list = m_missing[key];
        for (const json& slice : slices) {
            if (slice.is_string()) {
                list.push_back(slice.get<std::string>());
            }
        }
    }
    // The categories: the members by their CRC-32.
    const json& categories = document["categories"];
    if (!categories.is_object()) {
        return fail("the member categories is not an object");
    }
    for (const auto& [name, members] : categories.items()) {
        if (!members.is_array()) {
            return fail("the category " + name + " has no member list");
        }
        auto& table = m_categories[lower_ascii(name)];
        for (const json& member : members) {
            if (!member.is_string() || member.get<std::string>().empty()) {
                return fail("the category " + name + " has a member that is not an item id");
            }
            const std::string item = member.get<std::string>();
            m_firstMembers.emplace(lower_ascii(name), item);
            if (!table.emplace(item_id_crc(item), item).second) {
                return fail("the category " + name + " has two items with one CRC-32");
            }
        }
    }
    // The recipes.
    const json& recipes = document["recipes"];
    if (!recipes.is_array()) {
        return fail("the member recipes is not a list");
    }
    m_recipes.reserve(recipes.size());
    for (const json& row : recipes) {
        Recipe recipe;
        std::uint32_t crc = 0;
        if (!row.is_object() || !text_in(row, "id", recipe.id) || recipe.id.empty() || !number_in(row, "crc", 0xFFFFFFFFULL, crc)) {
            return fail("a recipe has no id or no CRC-32");
        }
        const std::string what = "the recipe " + recipe.id;
        recipe.crc = crc;
        if (recipe.crc != item_id_crc(recipe.id)) {
            return fail(what + ": the CRC-32 is not the CRC-32 of its lower-case id");
        }
        if (!text_in(row, "table", recipe.table) || !text_in(row, "category", recipe.category) || !text_in(row, "group", recipe.group)
            || !flag_in(row, "known", recipe.known) || !flag_in(row, "listed", recipe.listed) || !text_in(row, "skill", recipe.skill)
            || !number_in(row, "level", kLargestLevel, recipe.level) || !text_in(row, "item", recipe.item)
            || !flag_in(row, "procedural", recipe.procedural) || !flag_in(row, "refining", recipe.refining)
            || !flag_in(row, "craftAll", recipe.craftAll) || !flag_in(row, "temporary", recipe.temporary)
            || !number_in(row, "fee", kLargestFee, recipe.fee) || !flag_in(row, "tax", recipe.tax)
            || !number_in(row, "baseGearScore", kLargestGearScore, recipe.baseGearScore)
            || !number_in(row, "baseTier", 100, recipe.baseTier) || !number_in(row, "perkItems", 100, recipe.perkItems)
            || !number_in(row, "attributeSlots", 100, recipe.attributeSlots) || !flag_in(row, "push", recipe.push)
            || !number_in(row, "perkCost", 1000000, recipe.perkCost) || !text_in(row, "event", recipe.event)
            || !text_in(row, "requiredAchievement", recipe.requiredAchievement)
            || !text_in(row, "hiddenByAchievement", recipe.hiddenByAchievement)
            || !text_in(row, "unlockedAchievement", recipe.unlockedAchievement)
            || !text_in(row, "firstCraftAchievement", recipe.firstCraftAchievement) || !text_in(row, "worldTags", recipe.worldTags)
            || !text_in(row, "expansion", recipe.expansion)
            || !number_in(row, "cooldownSeconds", 100000000, recipe.cooldownSeconds)
            || !number_in(row, "cooldownQuantity", 255, recipe.cooldownQuantity) || !flag_in(row, "skipGrant", recipe.skipGrant)) {
            return fail(what + ": a member is not there or has a wrong form");
        }
        if (row.contains("disallowBonuses") && !flag_in(row, "disallowBonuses", recipe.disallowBonuses)) {
            return fail(what + ": disallowBonuses is not a flag");
        }
        // The bonus item values (format 2; a file of format 1 can have bonusChance).
        if (row.contains("bonusChance")) {
            if (!row["bonusChance"].is_number() || !std::isfinite(row["bonusChance"].get<double>())
                || std::fabs(row["bonusChance"].get<double>()) > kLargestBonus) {
                return fail(what + ": bonusChance is not a finite number");
            }
            recipe.bonusChance = static_cast<float>(row["bonusChance"].get<double>());
        }
        for (const auto& [member, target] : {std::pair<const char*, std::vector<float>*>{"bonusIncrease", &recipe.bonusIncrease},
                                             {"bonusDecrease", &recipe.bonusDecrease}}) {
            if (!row.contains(member)) {
                if (format == kFormat) {
                    return fail(what + ": the member " + member + " is not there");
                }
                continue;
            }
            if (!row[member].is_array()) {
                return fail(what + ": " + member + " is not a list");
            }
            for (const json& value : row[member]) {
                if (!value.is_number() || !std::isfinite(value.get<double>()) || std::fabs(value.get<double>()) > kLargestBonus) {
                    return fail(what + ": " + member + " has a value that is not a finite number");
                }
                target->push_back(static_cast<float>(value.get<double>()));
            }
        }
        if (row.contains("outputQty") && !row["outputQty"].is_null()) {
            std::uint64_t value = 0;
            if (!whole_in(row["outputQty"], kLargestQuantity, value)) {
                return fail(what + ": outputQty is not a whole number of 0 to 50000");
            }
            recipe.outputQty = static_cast<std::uint32_t>(value);
        } else if (!row.contains("outputQty")) {
            return fail(what + ": the member outputQty is not there");
        }
        if (!row.contains("stations") || !row["stations"].is_array() || row["stations"].size() > kLargestStationKeys) {
            return fail(what + ": the station keys are not a list of 4 keys at most");
        }
        for (const json& key : row["stations"]) {
            if (!key.is_string() || key.get<std::string>().empty()) {
                return fail(what + ": a station key is not a text");
            }
            recipe.stations.push_back(lower_ascii(key.get<std::string>()));
        }
        if (!row.contains("unsupported") || !row["unsupported"].is_array()) {
            return fail(what + ": the member unsupported is not a list");
        }
        for (const json& cause : row["unsupported"]) {
            if (!cause.is_string()) {
                return fail(what + ": a cause is not a text");
            }
            recipe.unsupported.push_back(cause.get<std::string>());
        }
        if (!row.contains("ingredients") || !row["ingredients"].is_array() || row["ingredients"].size() > kLargestIngredients) {
            return fail(what + ": the ingredients are not a list of 7 entries at most");
        }
        for (const json& entry : row["ingredients"]) {
            std::uint64_t quantity = 0;
            if (!entry.is_array() || entry.size() != 3 || !entry[0].is_string() || !entry[1].is_string()
                || !whole_in(entry[2], kLargestQuantity, quantity)) {
                return fail(what + ": an ingredient is not [TYPE, ID, QUANTITY]");
            }
            const std::optional<IngredientType> type = ingredient_type(entry[0].get<std::string>());
            if (!type) {
                return fail(what + ": the ingredient type " + entry[0].get<std::string>() + " is not a type of the client");
            }
            recipe.ingredients.push_back(RecipeIngredient{*type, entry[1].get<std::string>(), static_cast<std::uint32_t>(quantity)});
        }
        // A recipe that the backend crafts must be complete: an output, a quantity, an ingredient with a quantity, a station key.
        if (recipe.unsupported.empty()) {
            // Code review of stage 3.7a, finding CR7A-R2: a recipe with no cause has no rule that this stage does not apply (the rules of
            // the causes of the tool, crafting.py recipe_entry). A file whose causes and rules do not agree is not consistent: an empty
            // cause list does not enable a recipe with such a rule.
            if (const char* rule = unapplied_rule(recipe, format)) {
                return fail(what + ": a recipe with no cause has the rule " + rule + ", which the backend does not apply");
            }
            // Stage 3.7b (format 2): the rules of the event, the first craft, the cooldown, and refining need their data (the tool gives a
            // recipe with incomplete data a cause): an event of the file with no character experience and a trade skill with a rank table,
            // the bit of the first-craft achievement, a cooldown with a time and a count, the rank table of the trade skill of refining.
            if (format == kFormat) {
                if (!recipe.event.empty()) {
                    const CraftEvent* reward = event(recipe.event);
                    if (reward == nullptr || reward->experience != 0.0F || (!reward->progression.empty() && !has_ranks(reward->progression))) {
                        return fail(what + ": a recipe with no cause has the event " + recipe.event
                                    + " with no row, with character experience, or with a progression with no rank table");
                    }
                }
                if (!recipe.firstCraftAchievement.empty() && !achievement_index(recipe.firstCraftAchievement)) {
                    return fail(what + ": a recipe with no cause has the first-craft achievement " + recipe.firstCraftAchievement + " with no bit");
                }
                if ((recipe.cooldownSeconds > 0) != (recipe.cooldownQuantity > 0)) {
                    return fail(what + ": a recipe with no cause has a cooldown with only a time or only a count");
                }
                if (recipe.refining && !has_ranks(recipe.skill)) {
                    return fail(what + ": a refining recipe with no cause has no rank table of its trade skill " + recipe.skill);
                }
            }
            if (recipe.item.empty() || recipe.outputQty == 0 || recipe.ingredients.empty() || recipe.stations.empty()) {
                return fail(what + ": a recipe with no cause has no output, no output quantity, no ingredient, or no station key");
            }
            for (const RecipeIngredient& ingredient : recipe.ingredients) {
                if (ingredient.quantity == 0 || ingredient.type == IngredientType::currency) {
                    return fail(what + ": a recipe with no cause has an ingredient with the quantity 0 or a currency ingredient");
                }
                if ((ingredient.type == IngredientType::categoryOnly || ingredient.type == IngredientType::categoryAny)
                    && member_count(ingredient.id) == 0) {
                    return fail(what + ": a recipe with no cause has a category ingredient with no item");
                }
            }
            ++m_supported;
        }
        if (!m_byCrc.emplace(recipe.crc, m_recipes.size()).second) {
            return fail(what + ": two recipes have one CRC-32");
        }
        m_recipes.push_back(std::move(recipe));
    }
    return true;
}

const Recipe* CraftingData::recipe(std::uint32_t crc) const {
    const auto found = m_byCrc.find(crc);
    return found != m_byCrc.end() ? &m_recipes[found->second] : nullptr;
}

const Recipe* CraftingData::recipe_of(std::string_view id) const {
    const Recipe* found = recipe(item_id_crc(id));
    return found != nullptr && lower_ascii(found->id) == lower_ascii(id) ? found : nullptr;
}

const CraftingStation* CraftingData::station(std::string_view name) const {
    const auto found = m_stationByName.find(lower_ascii(name));
    return found != m_stationByName.end() ? &m_stations[found->second] : nullptr;
}

bool CraftingData::permits(const CraftingStation& station, const Recipe& recipe) {
    for (const std::string& key : recipe.stations) {
        if (std::find(station.types.begin(), station.types.end(), key) != station.types.end()) {
            return true;
        }
    }
    return false;
}

std::string CraftingData::member(std::string_view category, std::uint32_t itemCrc) const {
    const auto found = m_categories.find(lower_ascii(category));
    if (found == m_categories.end()) {
        return {};
    }
    const auto item = found->second.find(itemCrc);
    return item != found->second.end() ? item->second : std::string{};
}

std::string CraftingData::first_member(std::string_view category) const {
    const auto found = m_firstMembers.find(lower_ascii(category));
    return found != m_firstMembers.end() ? found->second : std::string{};
}

std::size_t CraftingData::member_count(std::string_view category) const {
    const auto found = m_categories.find(lower_ascii(category));
    return found != m_categories.end() ? found->second.size() : 0;
}

const CraftEvent* CraftingData::event(std::string_view id) const {
    const auto found = m_events.find(lower_ascii(id));
    return found != m_events.end() ? &found->second : nullptr;
}

std::optional<float> CraftingData::roll_bonus(std::string_view tradeskill, std::uint32_t rank) const {
    const auto found = m_ranks.find(lower_ascii(tradeskill));
    if (found == m_ranks.end() || found->second.empty()) {
        return std::nullopt;
    }
    return found->second[std::min<std::size_t>(rank, found->second.size() - 1)];
}

bool CraftingData::has_ranks(std::string_view tradeskill) const {
    return m_ranks.contains(lower_ascii(tradeskill));
}

std::optional<std::uint32_t> CraftingData::achievement_index(std::string_view id) const {
    const auto found = m_achievements.find(lower_ascii(id));
    return found != m_achievements.end() ? std::optional<std::uint32_t>(found->second) : std::nullopt;
}

MaterialBonus CraftingData::material(std::string_view itemId) const {
    const auto found = m_materials.find(lower_ascii(itemId));
    return found != m_materials.end() ? found->second : MaterialBonus{};
}

bool CraftingData::match_catalog(const ItemCatalog& catalog) {
    // Code review of stage U4, finding U4-R1 (the rule of the tool, crafting.py CAUSE_RANGE): an output with a gear score range and no
    // override needs the craft gear score rule of stage 3.7c. A recipe with no cause must not have such an output.
    if (!m_loaded || !catalog.loaded()) {
        return true;
    }
    for (const Recipe& recipe : m_recipes) {
        if (!recipe.unsupported.empty()) {
            continue;
        }
        const ItemDefinition* output = catalog.find(recipe.item);
        if (output != nullptr && output->maxGearScore != 0 && output->gearScoreOverride == 0) {
            const std::string cause = "the recipe " + recipe.id + " has no cause, and its output " + output->id
                                      + " has a gear score range and no override in the item catalog (the rule of stage 3.7c)";
            *this = CraftingData{};
            m_error = cause;
            return false;
        }
    }
    return true;
}

} // namespace soulwarden::backend::gamedata
