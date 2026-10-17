// The cases of the code review of stage 2.2 (findings WT-R1 to WT-R5), corrected in stage 3.6a. Each case fails on the tree of the commit
// of stage 2.2 and passes with the correction. The cases come from the proof tests of the review (WTP1 to WTP5). Each case uses only
// interfaces of the tree of stage 2.2, so that the same file proves the fault on that tree. Made-up accounts, ids, names, and data only.
//
//   RWT1  WT-R1: a kept roll of 20,001 units (an item that the catalog did not have at the kill) keeps its whole quantity at a restart;
//         with the item in the catalog the load makes its 21 stacks, and the owner takes all 20,001
//   RWT2  WT-R2: a stored stack beyond the 500 stacks of a part (a part of a backend before stage 2.2) keeps its instance id, its
//         durability, and its bind flag through a take and a restart
//   RWT3  WT-R3: the item catalog tool stops at a master table with no protection column or with a protection cell that is not a flag
//         (the old file stays); a store table can have no column NotDroppable (check_item_catalog_tool.py)
//   RWT4  WT-R4: a catalog of format 2 must have the protection columns; a file of format 1 can have the 12 old columns
//   RWT5  WT-R5: a world time text with a delimiter after the seconds changes nothing

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "server/http/character_commands.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/loot_bag_fixture.h"

namespace soulwarden::backend::tests {
namespace review_world_time {

using namespace bags;
using nlohmann::json;

/** The made-up item of the protection cases (RWT4). */
constexpr const char* kProtectedOre = "ReviewProtectedOre";

/** @return The quantity of an item in the part of a character: its instances and its kept rolls. */
std::uint32_t part_quantity(const world::LootBag& bag, const std::string& character, const char* id) {
    std::uint32_t total = 0;
    const auto part = bag.parts.find(character);
    if (part == bag.parts.end()) {
        return 0;
    }
    for (const auto& item : part->second.items) {
        total += item.itemId == id ? item.quantity : 0;
    }
    for (const auto& item : part->second.kept) {
        total += item.itemId == id ? item.quantity : 0;
    }
    return total;
}

/** RWT1 (WT-R1): a kept roll of more than one stack keeps its whole quantity at a restart. */
void large_kept_roll(const Options& options) {
    const auto path = options.workDirectory / "review-world-time-large-kept.sqlite3";
    std::filesystem::remove(path);
    std::string key;
    {
        BagShop shop(path.string());
        auto items = bag_items();
        std::erase_if(items, [](const auto& item) { return item.id == "TestOre"; });
        shop.host.catalog.set(std::move(items));
        auto data = bag_loot();
        data.tables[0].entries[1].quantityMin = 20001;
        data.tables[0].entries[1].quantityMax = 20001;
        check(shop.host.loot.set(std::move(data)), "setup: a roll of 20,001 ore is valid loot data");
        shop.enter();
        shop.kill({kCharacterA});
        const auto* bag = shop.bag_of(kCharacterA);
        check(bag != nullptr, "the kill makes a bag");
        if (bag == nullptr) {
            return;
        }
        key = bag->key;
        check(part_quantity(*bag, kCharacterA, "TestOre") == 20001, "the part keeps all 20,001 ore (the catalog does not have the item)");
    }
    BagShop shop(path.string(), true);
    shop.enter();
    const auto* bag = shop.bags().find(key);
    const std::uint32_t restored = bag != nullptr ? part_quantity(*bag, kCharacterA, "TestOre") : 0;
    note("RWT1 restored " + std::to_string(restored) + " of 20001");
    check(bag != nullptr && restored == 20001, "a restart with the item in the catalog keeps the whole roll: 20,001 ore");
    if (bag == nullptr) {
        return;
    }
    std::size_t stacks = 0;
    for (const auto& item : bag->parts.at(kCharacterA).items) {
        stacks += item.itemId == "TestOre" ? 1 : 0;
    }
    check(stacks == 21, "the load makes the instances: 21 stacks of 1,000 at most (" + std::to_string(stacks) + ")");
    shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
    world::Entity& a = *shop.entity(kCharacterA);
    shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
    note("RWT1 taken " + std::to_string(held(a.bag, "TestOre")));
    check(held(a.bag, "TestOre") == 20001, "the owner takes all 20,001 ore");
    check(held(shop.entity(kCharacterB)->bag, "TestOre") == 0, "the second session gets no ore");
}

/** RWT2 (WT-R2): a stored stack beyond 500 keeps all its fields through a take and a restart. */
void overflow_instance(const Options& options) {
    const auto path = options.workDirectory / "review-world-time-overflow.sqlite3";
    std::filesystem::remove(path);
    const std::string key = "review-world-time-overflow";
    world::InstanceId original{};
    {
        BagShop shop(path.string());
        auto instances = shop.host.world.inventory().make_instances({{"TestStone", 500, std::nullopt, {}}, {"TestSword", 1, 220, {}}}, nullptr);
        check(instances.size() == 501, "setup: 501 stacks (the old give path of a backend before stage 2.2 accepted them)");
        if (instances.size() != 501) {
            return;
        }
        instances.back().durability = 37;
        instances.back().flags = world::kItemBound;
        original = instances.back().instance;
        state::LootBagRow row;
        row.bagKey = key;
        row.kind = world::loot_bag_rules::kDiscarded;
        row.source = "review";
        row.position = kKillPlace;
        row.createdAt = static_cast<std::int64_t>(shop.host.clock.wall_time_ns(shop.host.clock.now()) / 1000000ULL);
        row.expiresAt = row.createdAt + 180000;
        for (const auto& instance : instances) {
            state::ItemRow stored;
            stored.slot = static_cast<std::uint32_t>(row.items.size());
            stored.instanceId = world::instance_text(instance.instance);
            stored.itemId = instance.itemId;
            stored.quantity = instance.quantity;
            stored.gearScore = instance.gearScore;
            stored.durability = instance.durability;
            stored.maxDurability = instance.maxDurability;
            stored.flags = instance.flags;
            stored.chrono = instance.chrono;
            stored.perks = instance.perks;
            row.items.push_back(std::move(stored));
        }
        check(shop.host.store.write_loot_bag(kCharacterA, row), "setup: the store has the 501 stacks");
    }
    {
        // The schema of stage 3.5b.2 (10): the columns and the tables of later stages are not there; the load migrates it.
        sqlite3* connection = nullptr;
        bool ok = sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK;
        ok = ok && sqlite3_exec(connection,
                                "DROP INDEX loot_bags_bag_key; DROP INDEX loot_bag_items_bag_key; ALTER TABLE loot_bags DROP COLUMN emptied_at;"
                                "DROP TABLE world_time; DROP TABLE IF EXISTS gathering_nodes; PRAGMA user_version=10;",
                                nullptr, nullptr, nullptr)
                        == SQLITE_OK;
        sqlite3_close(connection);
        check(ok, "setup: the database has the schema 10");
    }
    {
        BagShop shop(path.string(), true);
        shop.enter();
        const auto* bag = shop.bags().find(key);
        check(bag != nullptr && bag->parts.at(kCharacterA).items.size() == 500, "the load shows 500 stacks (the Container of the client)");
        if (bag == nullptr) {
            return;
        }
        bool swordShown = false;
        for (const auto& item : bag->parts.at(kCharacterA).items) {
            swordShown = swordShown || item.itemId == "TestSword";
        }
        check(!swordShown, "the sword (stack 501) waits: the client does not see it");
        shop.request(shop.clientA, shop.bag_block(*bag, kOpenRow));
        world::Entity& a = *shop.entity(kCharacterA);
        shop.send(shop.clientA, server::rep::kTypeTakeAllItems, BagShop::bag_body(a, a));
        check(held(a.bag, "TestStone") == 500, "the owner takes the 500 stones");
        check(held(shop.entity(kCharacterB)->bag, "TestStone") == 0, "the second session gets no stone");
    }
    BagShop shop(path.string(), true);
    shop.enter();
    const auto* bag = shop.bags().find(key);
    const world::ItemInstance* sword = nullptr;
    if (bag != nullptr) {
        for (const auto& item : bag->parts.at(kCharacterA).items) {
            sword = item.itemId == "TestSword" ? &item : sword;
        }
    }
    note("RWT2 sword " + std::to_string(sword != nullptr) + ", same instance " + std::to_string(sword != nullptr && sword->instance == original)
         + ", durability " + std::to_string(sword != nullptr ? sword->durability : 0) + ", bound "
         + std::to_string(sword != nullptr && (sword->flags & world::kItemBound) != 0));
    check(sword != nullptr && sword->instance == original && sword->durability == 37 && sword->flags == world::kItemBound,
          "after the take and a restart the sword shows with its instance id, its durability 37, and its bind flag");
}

/** RWT3 (WT-R3): the check of the item catalog tool with the source cases of the finding. */
void catalog_tool_sources(const Options& options) {
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_item_catalog_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the item catalog tool is in the tree");
    const std::filesystem::path work = options.workDirectory / "review-world-time-catalog";
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / "review-world-time-catalog.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::vector<std::string> cases{"missing-nonremovable", "missing-notdroppable", "invalid-flag", "store-table"};
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                          + script.lexically_normal().string() + "\" \"" + work.string() + "\"";
    for (const std::string& name : cases) {
        command += " " + name;
    }
    command += " > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        note(line);
        if (line.rfind("ok  ", 0) == 0) {
            const std::size_t start = line.find_first_not_of(' ', 2);
            passed.insert(line.substr(start, line.find(':') - start));
        }
    }
    for (const std::string& name : cases) {
        check(passed.count(name) == 1, "the case " + name + " of the check of the item catalog tool passes");
    }
    check(status == 0, "the check of the item catalog tool gives status 0");
}

/** @return A catalog document with the 14 columns and a made-up item with Nonremovable. */
json protected_catalog() {
    const json columns = {"id",           "name",         "type",       "class",        "tier",
                          "weight",       "maxStack",     "durability", "minGearScore", "maxGearScore",
                          "gearScoreOverride", "bindOnPickup", "nonRemovable", "notDroppable"};
    json items = json::array();
    for (const auto& item : bag_items()) {
        items.push_back(json::array({item.id, item.nameKey, item.type, item.itemClass, item.tier, item.weight, item.maxStack, item.durability,
                                     item.minGearScore, item.maxGearScore, item.gearScoreOverride, item.bindOnPickup, false, false}));
    }
    items.push_back(json::array({kProtectedOre, "@review_ore", "Resource", "Resource", 1, 1, 1000, 0, 0, 0, 0, false, true, false}));
    return json{{"format", 2}, {"count", items.size() + 1}, {"columns", columns}, {"items", items}};
}

/** Sends a discard (1838) of the protected ore of character A. */
void discard_ore(BagShop& shop) {
    world::Entity& a = *shop.entity(kCharacterA);
    for (std::uint32_t slot = 0; slot < a.bag.slots.size(); ++slot) {
        if (!a.bag.slots[slot].item || a.bag.slots[slot].item->itemId != kProtectedOre) {
            continue;
        }
        server::hub::Writer body;
        body.u64(0x2FEF73D6DC525917ULL);
        body.u64(a.id);
        body.size(1);
        body.u32(slot);
        body.raw(a.bag.slots[slot].item->instance);
        body.u32(0xFFFFFFFFU);
        body.u32(1);
        shop.send(shop.clientA, server::rep::kTypeDropInventoryItems, body.data());
        return;
    }
    check(false, "setup: the protected ore is in the bag before the discard");
}

/** RWT4 (WT-R4): the format of a catalog file selects its columns. */
void catalog_format(const Options& options) {
    const json full = protected_catalog();
    const auto write = [&](const json& document, const char* name) {
        const auto path = options.workDirectory / name;
        std::ofstream(path, std::ios::binary) << document.dump();
        return path.string();
    };
    {
        BagShop shop;
        check(shop.host.catalog.load(write(full, "review-world-time-catalog-full.json")),
              "control: a file of format 2 with the 14 columns loads");
    }
    for (int format : {1, 2}) {
        json document = full;
        document["format"] = format;
        document["columns"].erase(document["columns"].begin() + 12, document["columns"].end());
        for (auto& row : document["items"]) {
            row.erase(row.begin() + 12, row.end());
        }
        BagShop shop;
        const bool loaded = shop.host.catalog.load(write(document, format == 1 ? "review-world-time-catalog-1.json" : "review-world-time-catalog-2.json"));
        note("RWT4 format " + std::to_string(format) + " with 12 columns: " + (loaded ? "loaded" : "refused"));
        check(format == 1 ? loaded : !loaded, format == 1 ? "a file of format 1 with the 12 old columns loads"
                                                          : "a file of format 2 with no protection columns does not load");
        if (format == 2 && loaded) {
            shop.enter();
            check(shop.host.world.inventory().give(kCharacterA, kProtectedOre, 1).ok(), "setup: A has the item of the file");
            discard_ore(shop);
            check(held(shop.entity(kCharacterA)->bag, kProtectedOre) == 1, "the item of a format 2 file that is not complete stays with A");
        }
    }
    json other = full;
    other["format"] = 3;
    BagShop shop;
    check(!shop.host.catalog.load(write(other, "review-world-time-catalog-3.json")), "a file of an unknown format does not load");
}

/** RWT5 (WT-R5): a time text with a delimiter after the seconds changes nothing. */
void time_text_delimiter(const Options&) {
    BagShop shop;
    shop.enter();
    world::WorldTime& time = shop.host.world.world_time();
    const auto before = time.revision();
    const auto position = time.position();
    for (const char* value : {"01:02:03:", "23:59:59:", "01:02:03:04"}) {
        const json object{{"time", value}};
        const server::http::commands::JsonArguments args(object);
        service::HttpResponse refusal;
        auto command = server::http::commands::set_world_time(kCharacterA, args, refusal);
        int status = refusal.status;
        if (command) {
            status = command(shop.host.world).status;
        }
        note(std::string("RWT5 ") + value + ": status " + std::to_string(status));
        check(!command && status == 400 && time.revision() == before && time.position() == position,
              std::string("the text ") + value + " is refused and changes nothing");
    }
    for (const char* value : {"04:05:06", "04:05"}) {
        const json object{{"time", value}};
        const server::http::commands::JsonArguments args(object);
        service::HttpResponse refusal;
        auto command = server::http::commands::set_world_time(kCharacterA, args, refusal);
        check(command && command(shop.host.world).status == 200, std::string("control: the text ") + value + " is accepted");
    }
}

} // namespace review_world_time

std::vector<Case> review_world_time_cases() {
    using namespace review_world_time;
    return {{"RWT1", "review of stage 2.2, WT-R1: a kept roll of 20,001 units keeps its whole quantity at a restart", large_kept_roll},
            {"RWT2", "review of stage 2.2, WT-R2: a stored stack beyond 500 keeps its instance id, durability, and bind flag", overflow_instance},
            {"RWT3", "review of stage 2.2, WT-R3: the item catalog tool stops at a source with no protection column or a bad flag", catalog_tool_sources},
            {"RWT4", "review of stage 2.2, WT-R4: a catalog of format 2 must have the protection columns", catalog_format},
            {"RWT5", "review of stage 2.2, WT-R5: a time text with a delimiter after the seconds changes nothing", time_text_delimiter}};
}

} // namespace soulwarden::backend::tests
