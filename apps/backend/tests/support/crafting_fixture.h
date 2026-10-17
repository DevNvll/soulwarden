#pragma once

// The world of the cases of stage 3.7a (the crafting, tests/crafting_tests.cpp): the station of the fixture of stage 2.5b (Blacksmith5, its
// fee flag), the camp fixture (Camp5, no fee), made-up items and recipes, the ids of the Crafting and GlobalStorage components of a made-up
// player slice, and the helpers of request 2997, stop 2999, the storage search 7044, and progress 2993. All ids, keys, and items are made up.

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/crafting_data.h"
#include "gamedata/item_catalog.h"
#include "gamedata/slice_ids.h"
#include "server/rep/crafting_messages.h"
#include "support/chest_fixture.h"
#include "support/item_decoder.h"
#include "support/station_fixture.h"

namespace soulwarden::backend::tests::crafting {

using namespace stations;

/** The made-up player slice: its root and the Crafting and GlobalStorage components (their names are the names of the backend). */
inline constexpr std::uint64_t kSliceRoot = 0x51CE000000000001ULL;
inline constexpr std::uint64_t kCraftingEntity = 0x51CE0000000000C1ULL;
inline constexpr std::uint64_t kCraftingComponent = 0x065458AB26042E65ULL;
inline constexpr std::uint64_t kStorageEntity = 0x51CE0000000000D1ULL;
inline constexpr std::uint64_t kStorageComponent = 0x7CF8F5029CC7D817ULL;

inline const gamedata::SliceIds& craft_slice() {
    static const gamedata::SliceIds ids = [] {
        gamedata::SliceIds value;
        value.set(kSliceRoot, {{kCraftingEntity, kCraftingComponent, server::rep::kCraftingComponentName},
                               {kStorageEntity, kStorageComponent, server::rep::kStorageSearchComponentName}});
        return value;
    }();
    return ids;
}

/** @return The facet target of the Crafting component for an entity (the XOR rule of protocol notes 4.2). */
inline std::uint64_t crafting_target(std::uint64_t entity) {
    return (kCraftingEntity ^ kSliceRoot ^ entity) + kCraftingComponent;
}
inline std::uint64_t storage_target(std::uint64_t entity) {
    return (kStorageEntity ^ kSliceRoot ^ entity) + kStorageComponent;
}

inline std::vector<gamedata::ItemDefinition> craft_items() {
    using chests::chest_item;
    gamedata::ItemDefinition knife = chest_item("TestKnifeT1", "Weapon", "EquippableTool+SkinningKnife", 1, 1, 100, 100);
    knife.gearScoreOverride = 100;
    knife.bindOnPickup = true;
    return {chest_item("TestFlintT1", "Resource", "Resource", 1, 1000), chest_item("TestWoodT1", "Resource", "Resource", 1, 1000),
            knife,
            chest_item("TestIngot", "Resource", "Resource", 2, 1000), chest_item("TestIngotB", "Resource", "Resource", 2, 1000),
            chest_item("TestBar", "Resource", "Resource", 2, 1000), chest_item("TestFiller", "Resource", "Resource", 1, 1),
            chest_item("TestPotionT1", "Consumable", "Consumable", 1, 50)};
}

/** One recipe of the made-up crafting data (the members of the file of tools/gamedata/crafting.py). */
inline nlohmann::json recipe_json(const std::string& id, std::vector<std::string> stations, nlohmann::json ingredients, const std::string& item,
                                  std::uint32_t outputQty, std::uint32_t fee = 0, std::vector<std::string> causes = {},
                                  const std::string& skill = "", std::uint32_t level = 0) {
    return {{"id", id},          {"crc", gamedata::item_id_crc(id)}, {"table", "TestRecipes"}, {"category", "Tools"}, {"group", "Test"},
            {"known", true},     {"listed", true},                   {"skill", skill},         {"level", level},      {"stations", stations},
            {"ingredients", ingredients}, {"item", item},            {"outputQty", outputQty}, {"procedural", false}, {"refining", false},
            {"craftAll", false}, {"temporary", false},               {"fee", fee},             {"tax", true},         {"baseGearScore", 0},
            {"baseTier", 0},     {"disallowBonuses", false},         {"perkItems", 0},         {"attributeSlots", 0}, {"push", true},
            {"perkCost", 0},     {"event", ""},                      {"requiredAchievement", ""}, {"hiddenByAchievement", ""},
            {"unlockedAchievement", ""}, {"firstCraftAchievement", ""}, {"expansion", ""}, {"worldTags", ""}, {"cooldownSeconds", 0},
            {"cooldownQuantity", 0}, {"skipGrant", false},          {"bonusChance", 0.0},     {"unsupported", causes}};
}

/**
 * The made-up crafting data: the station rows Blacksmith5 (blacksmith1 to 5) and Camp5 (camp1 to 5); the recipes TestKnifeT1 (Flint and
 * Wood: one knife), TestBarRecipe (2 of the category TestMetal and 1 Wood: 2 bars, the fee 5), TestDouble (Flint 1 and Flint 2: one cost
 * of 3), TestTonic (2 potions: a bar), TestEvent (a cause), TestEngineering (only engineering2), TestRank (Engineering 50).
 */
inline std::string craft_document() {
    using nlohmann::json;
    const json items = json::array({json::array({"Item", "TestFlintT1", 1}), json::array({"Item", "TestWoodT1", 1})});
    json document{{"format", 1},
                  {"stations", json::array({{{"name", "Blacksmith5"}, {"types", {"blacksmith1", "blacksmith2", "blacksmith3", "blacksmith4", "blacksmith5"}},
                                             {"discount", 0.45}, {"mannequin", "Blacksmithing_Craft"}},
                                            {{"name", "Camp5"}, {"types", {"camp1", "camp2", "camp3", "camp4", "camp5"}}, {"discount", 0.0},
                                             {"mannequin", ""}}})},
                  {"missingStations", {{"Carpentry2", {"slices/test/norow.dynamicslice"}}}},
                  {"categories", {{"testmetal", {"TestIngot", "TestIngotB"}}}},
                  {"recipes",
                   json::array({recipe_json("TestKnifeT1", {"blacksmith2", "camp1"}, items, "TestKnifeT1", 1),
                                recipe_json("TestBarRecipe", {"blacksmith2"},
                                            json::array({json::array({"Category_Only", "TestMetal", 2}), json::array({"Item", "TestWoodT1", 1})}),
                                            "TestBar", 2, 5),
                                recipe_json("TestDouble", {"blacksmith2"},
                                            json::array({json::array({"Item", "TestFlintT1", 1}), json::array({"Item", "TestFlintT1", 2})}),
                                            "TestBar", 1),
                                recipe_json("TestTonic", {"blacksmith2"}, json::array({json::array({"Item", "TestPotionT1", 2})}), "TestBar", 1),
                                recipe_json("TestEvent", {"blacksmith2"}, items, "TestBar", 1, 0, {"reward event (stage 3.7b)"}),
                                recipe_json("TestEngineering", {"engineering2"}, items, "TestBar", 1),
                                recipe_json("TestRank", {"blacksmith2"}, items, "TestBar", 1, 0, {}, "Engineering", 50)})}};
    return document.dump();
}

/** One ingredient entry of request 2997 (the client sends type 1, quantity 1, and the item CRC: 0x5BA9FB0). */
struct Entry {
    std::uint8_t type{1};
    std::uint32_t quantity{1};
    std::uint32_t item{0};
};

inline Entry entry(const char* item, std::uint32_t quantity = 1) {
    return Entry{1, quantity, gamedata::item_id_crc(item)};
}

/** The body of request 2997 (an independent writer of the layout of protocol notes 4.50). */
inline Bytes craft_body(std::uint64_t target, std::uint64_t entity, std::uint32_t recipe, std::uint32_t quantity, std::uint32_t fee,
                        std::uint16_t gearScore, const std::vector<Entry>& entries, const std::vector<std::uint32_t>& perks = {},
                        std::uint8_t upgrade = 0) {
    Bytes body;
    const auto put = [&](std::uint64_t value, int bytes) {
        for (int shift = (bytes - 1) * 8; shift >= 0; shift -= 8) {
            body.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };
    put(target, 8);
    put(entity, 8);
    put(recipe, 4);
    put(quantity, 4);
    put(fee, 4);
    put(gearScore, 2);
    body.push_back(static_cast<std::uint8_t>(perks.size()));
    for (const std::uint32_t perk : perks) {
        put(perk, 4);
    }
    body.push_back(upgrade);
    body.push_back(static_cast<std::uint8_t>(entries.size()));
    for (const Entry& item : entries) {
        body.push_back(item.type);
        put(item.quantity, 4);
        put(item.item, 4);
    }
    return body;
}

/** One progress 2993 that a client got: its message index and its fields. */
struct Progress {
    std::size_t index{0};
    std::uint64_t target{0};
    std::uint64_t entity{0};
    std::string recipe;
    std::uint32_t completed{0};
    std::uint32_t total{0};
    std::size_t size{0};
};

inline std::vector<Progress> progresses(const FakeClient& client, std::size_t from = 0) {
    std::vector<Progress> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != server::rep::kTypeCraftProgress) {
            continue;
        }
        server::hub::Reader reader(client.got[index].body);
        Progress progress;
        progress.index = index;
        progress.size = client.got[index].body.size();
        progress.target = reader.u64();
        progress.entity = reader.u64();
        const std::uint32_t length = reader.size();
        const auto text = reader.raw(length);
        progress.recipe.assign(text.begin(), text.end());
        progress.completed = reader.u32();
        progress.total = reader.u32();
        if (reader.ok() && reader.left() == 0) {
            found.push_back(progress);
        }
    }
    return found;
}

/** The elements of the bag that a client got from a message index on (each journal entry and each full vector), with their index. */
struct SeenItem {
    std::size_t index{0};
    std::uint32_t element{0};
    WireItem item;
};

inline std::vector<SeenItem> bag_elements(const FakeClient& client, std::size_t from = 0) {
    std::vector<SeenItem> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == 61 && fragment.type == 1755) {
                    const WireContainer container = read_container(field_of(fragment, 0, 0));
                    for (const auto& [element, item] : container.elements) {
                        found.push_back(SeenItem{index, element, item});
                    }
                }
            }
        }
    }
    return found;
}

/** A host with the stations, the items, and the crafting data; A (and B) near the station; the ids of the made-up player slice. */
struct Crafter : Shop {
    explicit Crafter(bool withB = true, bool data = true, const std::string& database = ":memory:", bool existing = false)
        : Shop(false, database, withB, existing, &craft_slice()) {
        if (!set_stations(host.interactions)) {
            throw std::runtime_error("crafting test: the interaction data was not set: " + host.interactions.error());
        }
        host.catalog.set(craft_items());
        if (data && !host.crafting.set(craft_document())) {
            throw std::runtime_error("crafting test: the crafting data was not set: " + host.crafting.error());
        }
    }
    [[nodiscard]] world::Crafting& crafting() {
        return host.world.crafting();
    }
    [[nodiscard]] world::Entity* station() {
        return host.world.find(host.world.interactions().spawned_entity(kStationKey));
    }
    /** The client uses the station (the committed use of action 20). */
    void use_station(FakeClient& client, world::Entity* object = nullptr) {
        world::Entity* source = object != nullptr ? object : station();
        if (source == nullptr) {
            throw std::runtime_error("crafting test: no station");
        }
        request(client, child_block(host.interactions, *source, kCraft));
    }
    /** The camp fixture near A (the rule of its data: Camp5, no fee). */
    world::Entity* camp() {
        const world::FixtureResult result = host.world.interactions().make_fixture(kCharacterA, "camp", {0.0F, 1.5F, 0.0F}, 0.0F, -1);
        host.steps(60, {&clientA, &clientB});
        return result.entity;
    }
    /** Request 2997 of a client for a character (the route is the actor of the character). */
    void craft(FakeClient& client, const char* character, const char* recipe, std::uint32_t quantity, const std::vector<Entry>& entries,
               std::uint32_t fee = 1, std::uint16_t gearScore = 100, const std::vector<std::uint32_t>& perks = {}, std::uint8_t upgrade = 0,
               std::optional<std::uint64_t> target = std::nullopt, std::optional<std::uint64_t> group = std::nullopt) {
        world::Entity* entity = this->entity(character);
        const std::uint64_t id = group.value_or(entity->id);
        const Bytes body = craft_body(target.value_or(crafting_target(id)), id, gamedata::item_id_crc(recipe), quantity, fee, gearScore,
                                      entries, perks, upgrade);
        host.datagram(client.peer, client.component_message(server::rep::kTypeCraftRequest, entity->actor.bytes, body));
        host.steps(2, {&clientA, &clientB});
    }
    /** Message 2999 of a client. */
    void stop(FakeClient& client, const char* character) {
        world::Entity* entity = this->entity(character);
        host.datagram(client.peer, client.component_call(server::rep::kTypeCraftCancel, entity->actor.bytes, crafting_target(entity->id),
                                                         entity->id));
        host.steps(2, {&clientA, &clientB});
    }
    /** The time of the first job (1 s) and some ticks: the jobs complete and the records and the progress go out. */
    void finish(int ticks = 10) {
        host.advance(std::chrono::milliseconds(1000));
        host.steps(ticks, {&clientA, &clientB});
    }
    /** @return The quantity of an item in the bag of a character (the entity, or the rows when it is not in the world). */
    std::uint32_t count(const char* character, const char* item) {
        std::uint32_t total = 0;
        const std::optional<world::Holdings> holdings = host.world.inventory().holdings(character);
        if (!holdings) {
            return 0;
        }
        for (const world::BagSlot& slot : holdings->bag.slots) {
            if (slot.item && slot.item->itemId == item) {
                total += slot.item->quantity;
            }
        }
        return total;
    }
    void give(const char* character, const char* item, std::uint32_t quantity) {
        if (!host.world.inventory().give(character, item, quantity).ok()) {
            throw std::runtime_error(std::string("crafting test: the give of ") + item + " failed");
        }
    }
};

} // namespace soulwarden::backend::tests::crafting
