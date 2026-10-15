#include "gamedata/perk_data.h"

#include <algorithm>
#include <cmath>
#include <fstream>

#include <nlohmann/json.hpp>

namespace soulwarden::backend::gamedata {
namespace {

using nlohmann::json;

/** The form of the file that this code reads (tools/gamedata/perks.py). */
constexpr std::uint64_t kFormat = 1;
/** The text before the id of a bucket in an entry of a bucket. */
constexpr std::string_view kBucketPrefix = "[PBID]";

std::string key_of(std::string_view text) {
    std::string lower(text);
    for (char& letter : lower) {
        if (letter >= 'A' && letter <= 'Z') {
            letter = static_cast<char>(letter + ('a' - 'A'));
        }
    }
    return lower;
}

bool finite_number(const json& value, float& out) {
    if (!value.is_number()) {
        return false;
    }
    out = value.get<float>();
    return std::isfinite(out);
}

bool texts_of(const json& value, std::vector<std::string>& out) {
    if (!value.is_array()) {
        return false;
    }
    for (const json& entry : value) {
        if (!entry.is_string()) {
            return false;
        }
        out.push_back(entry.get<std::string>());
    }
    return true;
}

template <std::size_t N>
bool fixed_texts(const json& value, std::array<std::string, N>& out) {
    if (!value.is_array() || value.size() != N) {
        return false;
    }
    for (std::size_t index = 0; index < N; ++index) {
        if (!value[index].is_string()) {
            return false;
        }
        out[index] = value[index].get<std::string>();
    }
    return true;
}

bool cells_of(const json& value, CellValues& out) {
    if (!value.is_object()) {
        return false;
    }
    for (const auto& [column, cell] : value.items()) {
        if (cell.is_string()) {
            out.texts.emplace(column, cell.get<std::string>());
        } else {
            float number = 0.0F;
            if (!finite_number(cell, number)) {
                return false;
            }
            out.numbers.emplace(column, number);
        }
    }
    return true;
}

bool read_perk(const json& row, PerkRow& perk) {
    if (!row.is_array() || row.size() != 11 || !row[9].is_number_unsigned() || !row[10].is_boolean()) {
        return false;
    }
    perk.channel = static_cast<std::uint32_t>(row[9].get<std::uint64_t>());
    perk.deprecated = row[10].get<bool>();
    for (std::size_t column : {0U, 1U, 2U, 3U, 4U, 5U}) {
        if (!row[column].is_string()) {
            return false;
        }
    }
    perk.id = row[0].get<std::string>();
    perk.type = row[1].get<std::string>();
    perk.condition = row[2].get<std::string>();
    perk.itemClass = row[3].get<std::string>();
    perk.excludeItemClass = row[4].get<std::string>();
    perk.labels = row[5].get<std::string>();
    if (perk.id.empty() || !row[6].is_array()) {
        return false;
    }
    for (const json& step : row[6]) {
        ScalingStep value;
        if (!step.is_array() || step.size() != 2 || !finite_number(step[0], value.gearScore) || !finite_number(step[1], value.slope)) {
            return false;
        }
        perk.scaling.push_back(value);
    }
    return texts_of(row[7], perk.affixes) && texts_of(row[8], perk.abilities);
}

bool read_bucket(const std::string& id, const json& row, PerkBucket& bucket) {
    bucket.id = id;
    if (!row.is_array() || row.size() != 5 || !row[0].is_string() || !finite_number(row[1], bucket.chance) || !row[2].is_array()
        || bucket.chance < 0.0F || bucket.chance > 1.0F || !row[3].is_boolean() || !row[4].is_boolean()) {
        return false;
    }
    bucket.type = row[0].get<std::string>();
    bucket.ignoreLabelWeights = row[3].get<bool>();
    bucket.disableBiasing = row[4].get<bool>();
    for (const json& entry : row[2]) {
        BucketEntry value;
        if (!entry.is_array() || entry.size() != 2 || !entry[0].is_string() || !finite_number(entry[1], value.weight)
            || value.weight < 0.0F) {
            return false;
        }
        value.name = entry[0].get<std::string>();
        if (value.name.rfind(kBucketPrefix, 0) == 0) {
            value.bucket = true;
            value.name = value.name.substr(kBucketPrefix.size());
        }
        if (value.name.empty()) {
            return false;
        }
        bucket.entries.push_back(std::move(value));
    }
    return true;
}

bool read_layout(const json& row, PerkLayout& layout) {
    return row.is_array() && row.size() == 5 && fixed_texts(row[0], layout.slots) && fixed_texts(row[1], layout.fixed)
           && fixed_texts(row[2], layout.buckets) && row[3].is_number_integer() && row[4].is_boolean()
           && (layout.forceRarity = row[3].get<int>(), layout.canHavePerks = row[4].get<bool>(), true);
}

bool read_consumable(const json& row, ConsumableRow& consumable) {
    if (!row.is_object() || !row.contains("id") || !row["id"].is_string() || !row.contains("effects")
        || !texts_of(row["effects"], consumable.effects) || !row.contains("durations") || !row["durations"].is_array()
        || !row.contains("cooldown") || !row["cooldown"].is_string() || !row.contains("cooldownTime")
        || !finite_number(row["cooldownTime"], consumable.cooldownTime) || consumable.cooldownTime < 0.0F
        || !row.contains("removeEffects") || !texts_of(row["removeEffects"], consumable.removeEffects)
        || !row.contains("removeCategories") || !texts_of(row["removeCategories"], consumable.removeCategories)
        || !row.contains("noCombat") || !row["noCombat"].is_boolean() || !row.contains("shared") || !row["shared"].is_boolean()) {
        return false;
    }
    consumable.id = row["id"].get<std::string>();
    consumable.shared = row["shared"].get<bool>();
    consumable.cooldown = row["cooldown"].get<std::string>();
    consumable.noCombat = row["noCombat"].get<bool>();
    for (const json& entry : row["durations"]) {
        float value = 0.0F;
        if (!finite_number(entry, value) || value < 0.0F) {
            return false;
        }
        consumable.durations.push_back(value);
    }
    return !consumable.id.empty();
}

bool read_equip_load(const json& row, EquipLoadData& load) {
    const auto member = [&](const char* name, float& value) { return row.contains(name) && finite_number(row[name], value); };
    return row.is_object() && member("equipLoadMax", load.max) && member("ratioMedium", load.ratioMedium)
           && member("ratioHeavy", load.ratioHeavy) && member("ratioOverburdened", load.ratioOverburdened)
           && member("damageLight", load.damage[0]) && member("damageMedium", load.damage[1])
           && member("damageHeavy", load.damage[2]) && member("healLight", load.heal[0])
           && member("healMedium", load.heal[1]) && member("healHeavy", load.heal[2]) && load.max > 0.0F
           && load.ratioMedium > 0.0F && load.ratioHeavy >= load.ratioMedium;
}

bool read_generation(const json& value, PerkTables& tables) {
    if (!value.is_object() || !value.contains("minLevelRollPerks") || !value["minLevelRollPerks"].is_number_unsigned()
        || !value.contains("tiers") || !value["tiers"].is_array()) {
        return false;
    }
    tables.minLevelRollPerks = static_cast<std::uint32_t>(value["minLevelRollPerks"].get<std::uint64_t>());
    for (const json& tier : value["tiers"]) {
        PerkTier entry;
        if (!tier.is_array() || tier.size() != 2 || !tier[0].is_number_unsigned() || !tier[1].is_array()) {
            return false;
        }
        entry.maxChannel = static_cast<std::uint32_t>(tier[0].get<std::uint64_t>());
        for (const json& count : tier[1]) {
            if (!count.is_array() || count.size() != 2 || !count[0].is_number_unsigned() || !count[1].is_array()) {
                return false;
            }
            auto& ranges = entry.counts.emplace_back(static_cast<std::uint32_t>(count[0].get<std::uint64_t>()),
                                                     std::vector<std::pair<std::uint32_t, std::uint32_t>>{}).second;
            for (const json& range : count[1]) {
                if (!range.is_array() || range.size() != 2 || !range[0].is_number_unsigned() || !range[1].is_number_unsigned()
                    || range[1].get<std::uint64_t>() < range[0].get<std::uint64_t>()) {
                    return false;
                }
                ranges.emplace_back(static_cast<std::uint32_t>(range[0].get<std::uint64_t>()),
                                    static_cast<std::uint32_t>(range[1].get<std::uint64_t>()));
            }
        }
        tables.tiers.push_back(std::move(entry));
    }
    return true;
}

} // namespace

bool PerkData::load(const std::string& path) {
    m_tables = {};
    build_keys();
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        m_error = "the file is not there";
        return false;
    }
    const json document = json::parse(input, nullptr, false);
    if (!document.is_object()) {
        m_error = "the file is not a JSON object";
        return false;
    }
    if (!document.contains("format") || !document["format"].is_number_unsigned() || document["format"].get<std::uint64_t>() != kFormat) {
        m_error = "the file does not have the format 1";
        return false;
    }
    const auto fail = [&](std::string cause) {
        m_error = std::move(cause);
        return false;
    };
    PerkTables tables;
    for (const char* name : {"perks", "affixes", "abilities", "buckets", "layouts", "items", "consumables", "encumbrance", "equipLoad",
                             "labels", "swaps", "generation"}) {
        if (!document.contains(name)) {
            return fail(std::string("the file does not have the member ") + name);
        }
    }
    if (!document["perks"].is_array()) {
        return fail("the member perks is not a list");
    }
    for (const json& row : document["perks"]) {
        if (!read_perk(row, tables.perks.emplace_back())) {
            return fail("perk " + std::to_string(tables.perks.size()) + " does not have the form of the tool");
        }
    }
    if (!document.contains("count") || !document["count"].is_number_unsigned()
        || document["count"].get<std::uint64_t>() != tables.perks.size() + 1) {
        return fail("the member count is not the length of the perk list");
    }
    for (const char* name : {"affixes", "abilities"}) {
        if (!document[name].is_object()) {
            return fail(std::string("the member ") + name + " is not an object");
        }
        auto& target = std::string_view(name) == "affixes" ? tables.affixes : tables.abilities;
        for (const auto& [id, cells] : document[name].items()) {
            if (!cells_of(cells, target[key_of(id)])) {
                return fail(std::string("the row ") + id + " of the member " + name + " does not have the form of the tool");
            }
        }
    }
    if (!document["buckets"].is_object() || !document["layouts"].is_array() || !document["items"].is_object()
        || !document["consumables"].is_object() || !document["equipLoad"].is_object()) {
        return fail("a member does not have the form of the tool");
    }
    for (const auto& [id, row] : document["buckets"].items()) {
        if (!read_bucket(id, row, tables.buckets.emplace_back())) {
            return fail("the perk bucket " + id + " does not have the form of the tool");
        }
    }
    for (const json& row : document["layouts"]) {
        if (!read_layout(row, tables.layouts.emplace_back())) {
            return fail("layout " + std::to_string(tables.layouts.size() - 1) + " does not have the form of the tool");
        }
    }
    for (const auto& [id, layout] : document["items"].items()) {
        if (!layout.is_number_unsigned() || layout.get<std::uint64_t>() >= tables.layouts.size()) {
            return fail("the item " + id + " names a layout that the file does not have");
        }
        tables.items.emplace(key_of(id), static_cast<std::size_t>(layout.get<std::uint64_t>()));
    }
    for (const auto& [id, row] : document["consumables"].items()) {
        if (!read_consumable(row, tables.consumables.emplace_back())) {
            return fail("the consumable " + id + " does not have the form of the tool");
        }
    }
    if (!read_equip_load(document["encumbrance"], tables.equipLoad)) {
        return fail("the member encumbrance does not have the form of the tool");
    }
    if (!document["labels"].is_object() || !document["swaps"].is_array() || !read_generation(document["generation"], tables)) {
        return fail("the labels, the swaps, or the perk generation data do not have the form of the tool");
    }
    for (const auto& [id, row] : document["labels"].items()) {
        ExclusiveLabel label;
        if (!row.is_array() || row.size() != 2 || !finite_number(row[0], label.weight) || !row[1].is_object()) {
            return fail("the exclusive label " + id + " does not have the form of the tool");
        }
        for (const auto& [itemClass, weight] : row[1].items()) {
            float value = 0.0F;
            if (!finite_number(weight, value)) {
                return fail("the exclusive label " + id + " has a weight that is not a number");
            }
            label.itemClasses.emplace_back(itemClass, value);
        }
        tables.labels.emplace(id, std::move(label));
    }
    for (const json& row : document["swaps"]) {
        PerkSwap swap;
        if (!row.is_array() || row.size() != 5 || !row[0].is_number_unsigned() || !row[1].is_string() || !row[2].is_string()
            || !row[3].is_string() || !row[4].is_string()) {
            return fail("a perk swap does not have the form of the tool");
        }
        swap.id = static_cast<std::uint32_t>(row[0].get<std::uint64_t>());
        swap.byItem = row[1].get<std::string>() == "ItemId";
        swap.key = row[2].get<std::string>();
        swap.oldPerk = row[3].get<std::string>();
        swap.newPerk = row[4].get<std::string>();
        tables.swaps.push_back(std::move(swap));
    }
    if (document.contains("runes")) {
        if (!document["runes"].is_object()) {
            return fail("the member runes does not have the form of the tool");
        }
        for (const auto& [id, list] : document["runes"].items()) {
            std::vector<std::string> abilities;
            if (!texts_of(list, abilities)) {
                return fail("the heart rune " + id + " does not have the form of the tool");
            }
            tables.runes.emplace(key_of(id), std::move(abilities));
        }
    }
    for (const auto& [id, load] : document["equipLoad"].items()) {
        float value = 0.0F;
        if (!finite_number(load, value) || value < 0.0F) {
            return fail("the equip load of the item " + id + " is not valid");
        }
        tables.itemLoads.emplace(key_of(id), value);
    }
    return set(std::move(tables));
}

bool PerkData::set(PerkTables tables) {
    m_tables = {};
    build_keys();
    std::string error;
    if (tables.perks.empty()) {
        error = "the data has no perk";
    } else if (tables.perks.size() + 1 > 0xFFFF) {
        error = "the perk list has more than 65,535 entries";
    }
    std::unordered_map<std::string, std::size_t> perks;
    for (std::size_t position = 0; error.empty() && position < tables.perks.size(); ++position) {
        tables.perks[position].index = static_cast<std::uint16_t>(position + 1);
        if (!perks.emplace(key_of(tables.perks[position].id), position).second) {
            error = "two perks have the id " + tables.perks[position].id;
        }
        for (std::size_t step = 1; error.empty() && step < tables.perks[position].scaling.size(); ++step) {
            if (tables.perks[position].scaling[step].gearScore <= tables.perks[position].scaling[step - 1].gearScore) {
                error = "the scaling of the perk " + tables.perks[position].id + " does not go up";
            }
        }
    }
    std::unordered_map<std::string, std::size_t> buckets;
    for (std::size_t position = 0; error.empty() && position < tables.buckets.size(); ++position) {
        if (!buckets.emplace(key_of(tables.buckets[position].id), position).second) {
            error = "two perk buckets have the id " + tables.buckets[position].id;
        }
    }
    const auto known = [&](const std::string& name, bool bucket) {
        return bucket ? buckets.count(key_of(name)) != 0 : perks.count(key_of(name)) != 0;
    };
    for (const PerkBucket& bucket : tables.buckets) {
        for (const BucketEntry& entry : bucket.entries) {
            if (error.empty() && !known(entry.name, entry.bucket)) {
                error = "the perk bucket " + bucket.id + " names " + entry.name + ", which the data does not have";
            }
        }
    }
    for (const PerkLayout& layout : tables.layouts) {
        for (std::size_t place = 0; error.empty() && place < kPerkPlaces; ++place) {
            for (const std::string* name : {&layout.slots[place], &layout.fixed[place], &layout.buckets[place]}) {
                if (error.empty() && !name->empty() && !known(*name, false) && !known(*name, true)) {
                    error = "an item names the perk or the bucket " + *name + ", which the data does not have";
                }
            }
        }
    }
    if (!error.empty()) {
        m_error = error;
        return false;
    }
    m_tables = std::move(tables);
    build_keys();
    m_error.clear();
    return true;
}

void PerkData::build_keys() {
    m_byId.clear();
    m_buckets.clear();
    m_consumables.clear();
    m_swaps.clear();
    m_classSwaps.clear();
    m_itemSwaps.clear();
    for (const PerkSwap& swap : m_tables.swaps) {
        m_swaps[key_of(swap.oldPerk)].push_back(&swap);
        if (swap.newPerk.empty()) {
            continue;
        }
        if (swap.byItem) {
            // The first row of a pair of an old perk and an item stays (loader 0x4431E80).
            m_itemSwaps.emplace(key_of(swap.oldPerk) + '\n' + key_of(swap.key), &swap);
            continue;
        }
        // A class row whose classes are a part of, or contain, the classes of an earlier row of the same old perk is not
        // loaded (loader 0x4431E80).
        std::vector<const PerkSwap*>& rows = m_classSwaps[key_of(swap.oldPerk)];
        const bool covered = std::any_of(rows.begin(), rows.end(), [&](const PerkSwap* earlier) {
            return class_has_tags(earlier->key, swap.key) || class_has_tags(swap.key, earlier->key);
        });
        if (!covered) {
            rows.push_back(&swap);
        }
    }
    for (std::size_t position = 0; position < m_tables.perks.size(); ++position) {
        m_byId.emplace(key_of(m_tables.perks[position].id), position);
    }
    for (std::size_t position = 0; position < m_tables.buckets.size(); ++position) {
        m_buckets.emplace(key_of(m_tables.buckets[position].id), position);
    }
    for (std::size_t position = 0; position < m_tables.consumables.size(); ++position) {
        m_consumables.emplace(key_of(m_tables.consumables[position].id), position);
    }
}

const PerkRow* PerkData::find(std::string_view id) const {
    const auto found = m_byId.find(key_of(id));
    return found == m_byId.end() ? nullptr : &m_tables.perks[found->second];
}

const PerkRow* PerkData::by_index(std::uint32_t index) const {
    return index == 0 || index > m_tables.perks.size() ? nullptr : &m_tables.perks[index - 1];
}

const CellValues* PerkData::affix(std::string_view id) const {
    const auto found = m_tables.affixes.find(key_of(id));
    return found == m_tables.affixes.end() ? nullptr : &found->second;
}

const CellValues* PerkData::ability(std::string_view id) const {
    const auto found = m_tables.abilities.find(key_of(id));
    return found == m_tables.abilities.end() ? nullptr : &found->second;
}

const PerkBucket* PerkData::bucket(std::string_view id) const {
    const auto found = m_buckets.find(key_of(id));
    return found == m_buckets.end() ? nullptr : &m_tables.buckets[found->second];
}

const PerkLayout* PerkData::layout_of(std::string_view itemId) const {
    const auto found = m_tables.items.find(key_of(itemId));
    return found == m_tables.items.end() ? nullptr : &m_tables.layouts[found->second];
}

const ConsumableRow* PerkData::consumable(std::string_view itemId) const {
    const auto found = m_consumables.find(key_of(itemId));
    return found == m_consumables.end() ? nullptr : &m_tables.consumables[found->second];
}

float PerkData::item_load(std::string_view itemId) const {
    const auto found = m_tables.itemLoads.find(key_of(itemId));
    return found == m_tables.itemLoads.end() ? 0.0F : found->second;
}

const std::vector<std::string>& PerkData::rune_abilities(std::string_view weaponId) const {
    static const std::vector<std::string> none;
    const auto found = m_tables.runes.find(key_of(weaponId));
    return found == m_tables.runes.end() ? none : found->second;
}

const ExclusiveLabel* PerkData::label(std::string_view id) const {
    const auto found = m_tables.labels.find(std::string(id));
    return found == m_tables.labels.end() ? nullptr : &found->second;
}

const std::vector<const PerkSwap*>& PerkData::swaps_of(std::string_view perk) const {
    static const std::vector<const PerkSwap*> none;
    const auto found = m_swaps.find(key_of(perk));
    return found == m_swaps.end() ? none : found->second;
}

const PerkRow* PerkData::swap_of(const PerkRow& perk, std::string_view itemId, std::string_view itemClass) const {
    const std::string old = key_of(perk.id);
    if (const auto found = m_itemSwaps.find(old + '\n' + key_of(itemId)); found != m_itemSwaps.end()) {
        return find(found->second->newPerk);
    }
    const auto rows = m_classSwaps.find(old);
    if (rows == m_classSwaps.end()) {
        return nullptr;
    }
    for (const PerkSwap* swap : rows->second) {
        if (class_has_tags(itemClass, swap->key)) {
            return find(swap->newPerk);
        }
    }
    return nullptr;
}

unsigned PerkData::apply_swaps(std::array<std::string, kPerkPlaces>& perks, std::string_view itemId, std::string_view itemClass) const {
    std::vector<std::string> list;
    for (const std::string& perk : perks) {
        if (!perk.empty()) {
            list.push_back(perk);
        }
    }
    unsigned changes = 0;
    // The client makes the pass up to three times (research of stage 1.3b, perk-swap): the data has chains of two steps.
    for (unsigned pass = 0; pass < 3; ++pass) {
        // The perks of the item that a swap does not give a second time: each perk that is not a gem and not a charm.
        std::vector<const PerkRow*> kept;
        for (const std::string& perk : list) {
            const PerkRow* row = find(perk);
            if (row != nullptr && row->type != "Gem" && row->type != "OffensiveCharm" && row->type != "DefensiveCharm"
                && row->type != "SkillCharm") {
                kept.push_back(row);
            }
        }
        unsigned changed = 0;
        for (std::size_t place = 0; place < list.size();) {
            const PerkRow* row = find(list[place]);
            const PerkRow* next = row != nullptr ? swap_of(*row, itemId, itemClass) : nullptr;
            if (next == nullptr) {
                ++place;
                continue;
            }
            ++changed;
            if (std::find(kept.begin(), kept.end(), next) != kept.end()) {
                list.erase(list.begin() + static_cast<std::ptrdiff_t>(place));
                continue;
            }
            list[place] = next->id;
            ++place;
        }
        changes += changed;
        if (changed == 0) {
            break;
        }
    }
    if (changes != 0) {
        perks = {};
        for (std::size_t place = 0; place < list.size(); ++place) {
            perks[place] = list[place];
        }
    }
    return changes;
}

bool PerkData::allowed(const PerkRow& perk, std::string_view itemClass) {
    if (!perk.itemClass.empty() && !class_has_any(itemClass, perk.itemClass)) {
        return false;
    }
    return perk.excludeItemClass.empty() || !class_has_any(itemClass, perk.excludeItemClass);
}

std::uint32_t PerkData::max_perks(std::uint32_t tier, std::uint32_t gearScore) const {
    if (gearScore == 0 || tier >= m_tables.tiers.size()) {
        return 0;
    }
    const PerkTier& data = m_tables.tiers[tier];
    std::uint32_t best = 0;
    bool found = false;
    for (const auto& [count, ranges] : data.counts) {
        for (const auto& [low, high] : ranges) {
            if (gearScore >= low && gearScore <= high && (!found || count > best)) {
                best = count;
                found = true;
            }
        }
    }
    if (found) {
        return best;
    }
    // No range has the gear score: the count of the nearest range (the largest count when two are as near).
    std::uint32_t nearest = 0xFFFFFFFFU;
    for (const auto& [count, ranges] : data.counts) {
        for (const auto& [low, high] : ranges) {
            const std::uint32_t away = gearScore < low ? low - gearScore : gearScore - high;
            if (away < nearest || (away == nearest && count > best)) {
                nearest = away;
                best = count;
            }
        }
    }
    return best;
}

float PerkData::potency(const PerkRow& perk, std::uint32_t gearScore, std::uint32_t lowest) {
    float result = 1.0F;
    const float score = static_cast<float>(gearScore);
    for (std::size_t step = 0; step < perk.scaling.size(); ++step) {
        const float from = std::max(perk.scaling[step].gearScore, static_cast<float>(lowest));
        const float to = step + 1 < perk.scaling.size() ? perk.scaling[step + 1].gearScore : score;
        const float covered = std::min(score, to) - from;
        if (covered > 0.0F) {
            result += covered * perk.scaling[step].slope;
        }
    }
    return result;
}

namespace {

/** @return The parts of a text with "+" or "," between them, with no space around them. */
std::vector<std::string_view> tags_of(std::string_view text) {
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

bool same_tag(std::string_view left, std::string_view right) {
    return key_of(left) == key_of(right);
}

} // namespace

bool class_has_tags(std::string_view itemClass, std::string_view key) {
    const std::vector<std::string_view> tags = tags_of(itemClass);
    for (const std::string_view wanted : tags_of(key)) {
        if (std::none_of(tags.begin(), tags.end(), [&](std::string_view tag) { return same_tag(tag, wanted); })) {
            return false;
        }
    }
    return true;
}

bool class_has_any(std::string_view itemClass, std::string_view tags) {
    const std::vector<std::string_view> wanted = tags_of(tags);
    if (wanted.empty()) {
        return true;
    }
    for (const std::string_view tag : tags_of(itemClass)) {
        if (std::any_of(wanted.begin(), wanted.end(), [&](std::string_view other) { return same_tag(tag, other); })) {
            return true;
        }
    }
    return false;
}

} // namespace soulwarden::backend::gamedata
