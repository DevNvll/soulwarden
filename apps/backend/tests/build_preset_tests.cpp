// Stage B0 (the bow build of the user): the build presets and the gear score limit of a give.
//
//   BP1  the item catalog of format 3: the tool check (the column maxCraftUpgradeGS, the member artifactObjectives, the stops),
//        the loader (format 3, format 2 with no upgrade, a column count that is not the format, an objective of an unknown item)
//   BP2  the gear score of a give: the upgrade limit of the item to the MaxEquipGS of the owned expansion; a value outside the range
//        gets the nearest value; an item with no upgrade keeps its range; no expansion data: no limit of the expansion; the equip at 800
//   BP3  a preset for a character in the world: the equipment (the old items go into the bag), the quick slot, the bag, the level, the
//        attributes, the trees (the old nodes go), the bars, the achievement of the artifact objective; the rows; the owner states
//   BP4  a refused preset changes nothing: a full bag, a disabled preset (a level above the cap), an unknown preset or variant, and a
//        refused write in the transaction (a trigger): no row, no entity change
//   BP5  two sessions (rule MP-10): the observer gets the paperdoll fragment with the visible slots of the preset and no owner state
//   BP6  the check at the start: no game data (the project preset is disabled, one log line, the backend works), an id that the data
//        does not have, a gear score outside the range of its item; the content of the project preset
//   BP7  the routes: the admin command (a character of a different account: 404, no change), the catalog route, the development route
//   BP8  a character not in the world: the rows only; the next world entry has the build and the achievement bit
//   BP9  the list of the passive rules: a target condition with no event and a cooldown of a fixed row are "not applied" (the row of
//        a fixed value stays applied)
//
// All ids, names, and values are made up, with the id texts of the game where a rule of the backend names them. No value of a capture.
//
// The proof that a case fails on the tree before the stage (user test 2, 8514129): that tree builds this file with SOULWARDEN_B0_BASELINE
// (the members of the stage count as absent: no upgrade gear score, no objective, the limit of a give is the item table). BP1, BP2, and
// BP9 run there; BP3 to BP8 test the build preset, which that tree does not have (no world::BuildPresets, no route).

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/progress_decoder.h"
#include "support/shop_fixture.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#ifndef SOULWARDEN_B0_BASELINE
#include "world/build_presets.h"
#endif

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using shop::kCharacterA;
using shop::kCharacterB;

constexpr const char* kBow = "TestArtifactBow";
constexpr const char* kRing = "TestPlainRing";
constexpr const char* kFiller = "TestFillerSword";
constexpr const char* kObjectiveAchievement = "Test_Objective_Achievement";
constexpr std::uint32_t kObjectiveBit = 77;
constexpr const char* kExpansionAlias = "Test_Expansion_Owner";
constexpr const char* kExpansionAchievement = "Test_Expansion_Unlock";
constexpr std::uint32_t kExpansionBit = 12;
constexpr std::uint32_t kExpansionAchievementBit = 33;
constexpr const char* kTokenA = "token-of-the-start-of-A-preset-0123456789";
constexpr const char* kTokenB = "token-of-the-start-of-B-preset-9876543210";
constexpr std::uint32_t kPaperdollSlot = 62;
constexpr std::uint32_t kContainerSlot = 61;
constexpr std::uint32_t kAchievementSlot = 66;
const std::uint32_t kSwordTable = gamedata::progression_id("SwordAbilityTable");

#ifndef SOULWARDEN_B0_BASELINE
std::uint32_t upgrade_of(const gamedata::ItemDefinition& item) {
    return item.maxCraftUpgradeGS;
}
void set_upgrade(gamedata::ItemDefinition& item, std::uint32_t upgrade) {
    item.maxCraftUpgradeGS = upgrade;
}
std::string objective_of(const gamedata::ItemCatalog& catalog, const char* item) {
    const gamedata::ArtifactObjective* objective = catalog.artifact_objective(item);
    return objective != nullptr ? objective->achievementId : std::string{};
}
std::optional<std::uint32_t> objective_bit(const gamedata::ItemCatalog& catalog, const char* achievement) {
    return catalog.objective_achievement_index(achievement);
}
std::uint32_t limit_of(world::Inventory& inventory, const gamedata::ItemDefinition& item) {
    return inventory.give_limit(item);
}
#else
std::uint32_t upgrade_of(const gamedata::ItemDefinition&) {
    return 0;
}
void set_upgrade(gamedata::ItemDefinition&, std::uint32_t) {}
std::string objective_of(const gamedata::ItemCatalog&, const char*) {
    return {};
}
std::optional<std::uint32_t> objective_bit(const gamedata::ItemCatalog&, const char*) {
    return std::nullopt;
}
std::uint32_t limit_of(world::Inventory&, const gamedata::ItemDefinition& item) {
    return item.maxGearScore;
}
#endif

gamedata::ItemDefinition upgrade_bow(std::uint32_t upgrade = 800) {
    gamedata::ItemDefinition item = shop::definition(kBow, "Weapon", "Artifact+Weapon+EquippableTwoHand+Bow+Ranged", 5, 700, 700);
    set_upgrade(item, upgrade);
    return item;
}

std::vector<gamedata::ItemDefinition> preset_catalog() {
    std::vector<gamedata::ItemDefinition> items = shop::shop_catalog();
    items.push_back(upgrade_bow());
    items.push_back(shop::definition(kRing, "Armor", "EquippableRing+Jewelry", 5, 725, 725));
    items.push_back(shop::definition(kFiller, "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 1, 100, 100));
    return items;
}

/** The tables of the tests with an owned expansion: the largest level 7 (the base 6), MaxEquipGS 800 (`equip`). */
gamedata::ProgressionData preset_progression(std::uint32_t equip = 800, bool expansion = true) {
    gamedata::ProgressionData data = test_progression();
    if (!expansion) {
        return data;
    }
    data.expansions = {{"None", 6, 625, 625, 200, ""}, {"Expansion2023", 7, 700, equip, 250, kExpansionAlias}};
    data.entitlements = {{kExpansionAlias, kExpansionBit, "Expansion2023", "", kExpansionAchievement}};
    data.achievements = {{kExpansionAchievement, kExpansionAchievementBit}};
    return data;
}

#ifndef SOULWARDEN_B0_BASELINE
world::PresetItem slot_item(std::uint32_t slot, const char* item, std::uint32_t gearScore, world::PerkIds perks = {}, std::uint32_t quantity = 1) {
    world::PresetItem entry;
    entry.itemId = item;
    entry.gearScore = gearScore;
    entry.perks = std::move(perks);
    entry.quantity = quantity;
    entry.slot = slot;
    return entry;
}

/** A made-up preset: two variants of the sword tree, the same bow tree. */
world::BuildPreset test_preset() {
    world::BuildPreset preset;
    preset.name = "test-build";
    preset.title = "A test build";
    preset.level = 7;
    preset.attributes = {5, 5, 15, 25, 5}; // constitution, focus, strength, dexterity, intelligence: 30 points
    preset.items = {slot_item(world::paperdoll::kMainHand1, kBow, 800, {"PerkID_StatSwap_BaseDamage", "", "PerkID_Gem_VoidDMG4", "", ""}),
                    slot_item(world::paperdoll::kChest, "TestChest", 600, {"PerkID_StatSwap_HP", "", "", "", ""}),
                    slot_item(world::paperdoll::kRing, kRing, 725),
                    slot_item(world::paperdoll::kHeartRune, "TestRune", 625),
                    slot_item(world::paperdoll::kQuickFirst, "TestPotion", 0, {}, 5)};
    world::PresetItem food;
    food.itemId = "TestFood";
    food.quantity = 3;
    preset.items.push_back(food);
    const world::TreeBuild first{"SwordAbilityTable",
                                 14,
                                 {"Test_Sword_Slash", "Test_Sword_P0a", "Test_Sword_P0b", "Test_Sword_SlashUp", "Test_Sword_Stab", "Test_Sword_P1a",
                                  "Test_Sword_P2a", "Test_Sword_Leap", "Test_Sword_P3a", "Test_Sword_P4a", "Test_Sword_Ultimate"},
                                 {"Test_Sword_Slash", "Test_Sword_Stab", "Test_Sword_Leap"}};
    const world::TreeBuild second{"SwordAbilityTable",
                                  14,
                                  {"Test_Sword_Slash", "Test_Sword_P0a", "Test_Sword_Stab", "Test_Sword_Bash", "Test_Sword_D0a", "Test_Sword_D1a"},
                                  {"Test_Sword_Bash", "Test_Sword_Slash", ""}};
    preset.variants = {{"first", "First tree", {first}}, {"second", "Second tree", {second}}};
    return preset;
}

#endif

/** The world of a case: A in the world (B with `enter_b`), the made-up catalog with an artifact and its objective, the expansion. */
struct PresetShop {
    ServerHost host;
    FakeClient a{27300};
    FakeClient b{27301};
    Player alpha;
    Player beta;
    shop::Walker walker{host, a, shop::kPlace};

    explicit PresetShop(const std::string& database = ":memory:", bool enter = true, bool observe = true)
        : host(shop::shop_options(observe), database) {
        host.catalog.set(preset_catalog());
#ifndef SOULWARDEN_B0_BASELINE
        host.catalog.set_artifact_objectives({{kBow, "Test_Objective_Bow", kObjectiveAchievement, kObjectiveBit}});
#endif
        host.progression.set(preset_progression());
        if (!host.progression.loaded() || !host.perks.set(shop::shop_perks()) || !host.abilities.set(shop::shop_abilities())) {
            throw std::runtime_error("preset test: the game data is not valid");
        }
        host.terrain.source = [](float, float) { return std::optional<float>(shop::kHeight); };
        host.world.set_creature_behavior(false);
#ifndef SOULWARDEN_B0_BASELINE
        host.world.presets().set({test_preset()});
#endif
        alpha = add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", shop::kPlace);
        if (enter) {
            host.enter(a, alpha.registration());
            walker.idle(20);
        } else {
            host.tick();
        }
    }
    world::World& world() {
        return host.world;
    }
    world::Entity* character() {
        return host.world.find_character(kCharacterA);
    }
    void enter_b() {
        beta = add_player(host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", shop::at(shop::kPlace.x - 1.0F, shop::kPlace.y + 1.0F));
        host.enter(b, beta.registration());
        for (int index = 0; index < 20; ++index) {
            host.clock.advance(shop::kTick);
            host.datagram(b.peer, b.report_at(static_cast<std::uint16_t>(100 + index), shop::kPlace.x - 1.0F, shop::kPlace.y + 1.0F, shop::kHeight));
            host.datagram(a.peer, walker.client.input_report({walker.entry()}));
            host.tick();
            b.exchange(host);
            a.exchange(host);
        }
    }
    /** The state of A before a preset: an axe in weapon slot 1, a chest armor, level 3, two sword nodes at rank 14. */
    void prepare() {
        world::GiveOptions options;
        options.gearScore = 300;
        auto& inventory = host.world.inventory();
        const auto axe = inventory.give(kCharacterA, "TestAxe", 1, options);
        const auto chest = inventory.give(kCharacterA, "TestChest", 1, options);
        check(axe.ok() && chest.ok() && inventory.equip(kCharacterA, axe.slots.front(), world::paperdoll::kMainHand1).ok()
                  && inventory.equip(kCharacterA, chest.slots.front(), world::paperdoll::kChest).ok(),
              "setup: A has an axe in weapon slot 1 and a chest armor of gear score 300");
        check(host.world.progress().set_level(kCharacterA, 3).ok()
                  && host.world.skills().set_rank(kCharacterA, "SwordAbilityTable", gamedata::ProgressionKind::weapon, 14).ok()
                  && host.world.skills().assign(kCharacterA, kSwordTable, node_id("Test_Sword_Bash"), 1).ok()
                  && host.world.skills().assign(kCharacterA, kSwordTable, node_id("Test_Sword_D0a"), 1).ok(),
              "setup: A has level 3, the sword at rank 14, and the nodes Bash and D0a");
    }
};

#ifndef SOULWARDEN_B0_BASELINE
/** The rows of a character: the items, the progress, the skills, the achievements (a text for a compare). */
std::string rows_of(state::Store& store, const std::string& character) {
    std::string text;
    for (const state::ItemRow& row : store.list_items(character)) {
        text += std::to_string(row.container) + ":" + std::to_string(row.slot) + ":" + row.itemId + ":" + std::to_string(row.quantity) + ":"
                + std::to_string(row.gearScore) + " ";
    }
    const state::ProgressRow progress = store.progress(character);
    text += "| level " + std::to_string(progress.level) + " spent";
    for (const std::uint32_t points : progress.spent) {
        text += " " + std::to_string(points);
    }
    const state::SkillRows skills = store.list_skills(character);
    text += " | ranks";
    for (const auto& rank : skills.ranks) {
        text += " " + rank.name + ":" + std::to_string(rank.rank);
    }
    text += " | nodes";
    for (const auto& node : skills.nodes) {
        text += " " + node.name;
    }
    text += " | bars";
    for (const auto& bar : skills.bars) {
        text += " " + std::to_string(bar.slot) + ":" + bar.name;
    }
    text += " | achievements";
    for (const std::string& id : store.list_achievements(character)) {
        text += " " + id;
    }
    return text;
}

/** The values of an entity that a preset changes (a text for a compare). */
std::string entity_of(const world::Entity& entity) {
    std::string text;
    for (const auto& [slot, content] : entity.equipment.slots) {
        if (content.item) {
            text += std::to_string(slot) + ":" + content.item->itemId + ":" + std::to_string(content.item->gearScore) + " ";
        }
    }
    text += "| bag";
    for (std::size_t slot = 0; slot < entity.bag.slots.size(); ++slot) {
        if (entity.bag.slots[slot].item) {
            text += " " + entity.bag.slots[slot].item->itemId;
        }
    }
    text += " | level " + std::to_string(entity.progression.level);
    for (unsigned type = 0; type < world::attribute::kCount; ++type) {
        text += " " + std::to_string(entity.attributes.spent[type]);
    }
    text += " | nodes";
    for (const auto& [pool, nodes] : entity.skills.pools) {
        for (const auto& [node, state] : nodes) {
            if (state.level != 0) {
                text += " " + std::to_string(node);
            }
        }
    }
    text += " | achievements";
    for (const std::uint32_t bit : entity.achievements) {
        text += " " + std::to_string(bit);
    }
    return text;
}

/** @return The slots of the fragments that a client got in the records of an interest other than its own entity (1). */
std::set<std::uint32_t> observed_slots(const FakeClient& client, std::size_t from) {
    std::set<std::uint32_t> slots;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest == 1) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                slots.insert(fragment.slot);
            }
        }
    }
    return slots;
}

/** @return The visible slots (group 2, field 3) of the last paperdoll fragment of an interest other than the own entity. */
std::vector<std::uint32_t> observed_visible(const FakeClient& client, std::size_t from) {
    std::vector<std::uint32_t> visible;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (record.interest == 1 || fragment.slot != kPaperdollSlot) {
                    continue;
                }
                const auto found = fragment.fields.find({2, 3});
                if (found == fragment.fields.end()) {
                    continue;
                }
                visible.clear();
                server::hub::Reader reader(found->second);
                const auto count = progress_wire::read_full_form(reader);
                for (std::uint32_t item = 0; count && item < *count; ++item) {
                    visible.push_back(reader.u32());
                }
            }
        }
    }
    return visible;
}

#endif

std::vector<std::string> run_check(const Options& options, const std::string& script, const std::vector<std::string>& arguments) {
    const std::filesystem::path path = options.goldenDirectory / ".." / ".." / ".." / ".." / script;
    check(std::filesystem::is_regular_file(path), "the check script " + script + " is in the tree");
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / (std::filesystem::path(script).stem().string() + "-bp.log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \"" + path.lexically_normal().string() + "\"";
    for (const std::string& argument : arguments) {
        command += " \"" + argument + "\"";
    }
    command += " > \"" + output.string() + "\" 2>&1";
    (void)std::system(command.c_str());
    std::ifstream log(output);
    std::vector<std::string> lines;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 200));
        lines.push_back(line);
    }
    std::filesystem::remove(output);
    return lines;
}

bool passed(const std::vector<std::string>& lines, const std::string& name) {
    const auto starts = [&](const std::string& line, const char* mark) {
        if (line.rfind(mark, 0) != 0) {
            return false;
        }
        const std::size_t start = line.find_first_not_of(' ', std::string(mark).size());
        return start != std::string::npos && line.compare(start, name.size() + 1, name + ":") == 0;
    };
    return std::any_of(lines.begin(), lines.end(), [&](const std::string& line) { return starts(line, "ok"); })
           && std::none_of(lines.begin(), lines.end(), [&](const std::string& line) { return starts(line, "FAIL"); });
}

/** BP1: the item catalog of format 3. */
void catalog_format(const Options& options) {
    const std::filesystem::path work = options.workDirectory / "build-preset-catalog";
    const std::vector<std::string> lines =
        run_check(options, "tools/gamedata/check_item_catalog_tool.py", {work.string(), "upgrade", "upgrade-invalid", "upgrade-missing", "objective-broken", "protections"});
    for (const char* name : {"upgrade", "upgrade-invalid", "upgrade-missing", "objective-broken", "protections"}) {
        check(passed(lines, name), std::string("the item catalog tool: case ") + name);
    }
    gamedata::ItemCatalog catalog;
    const bool loaded = catalog.load((work / "item-catalog-upgrade.json").string());
    const gamedata::ItemDefinition* bow = catalog.find("ReviewArtifactBow");
    const gamedata::ItemDefinition* ring = catalog.find("ReviewPlainRing");
    check(loaded && bow != nullptr && ring != nullptr && upgrade_of(*bow) == 800 && upgrade_of(*ring) == 0
              && objective_of(catalog, "reviewartifactbow") == "Achievement_Objective_ReviewArtifactBow"
              && objective_bit(catalog, "Achievement_Objective_ReviewArtifactBow") == std::optional<std::uint32_t>(4321)
              && objective_of(catalog, "ReviewPlainRing").empty(),
          "the loader reads format 3: the upgrade gear score of each item and the objective of the artifact (its achievement bit)");

    // The forms of the file: format 2 (14 columns) loads with no upgrade; a format that does not fit its columns does not load.
    std::ifstream input(work / "item-catalog-upgrade.json");
    const json document = json::parse(input);
    const auto load = [&](const json& changed) {
        const auto path = work / "item-catalog-form.json";
        std::ofstream(path) << changed.dump();
        gamedata::ItemCatalog other;
        return std::make_pair(other.load(path.string()), other.find("ReviewArtifactBow") != nullptr ? upgrade_of(*other.find("ReviewArtifactBow")) : 99U);
    };
    json old = document;
    old["format"] = 2;
    old["columns"].erase(old["columns"].begin() + 14);
    for (json& row : old["items"]) {
        row.erase(row.begin() + 14);
    }
    old.erase("artifactObjectives");
    check(load(old) == std::make_pair(true, 0U), "a file of format 2 (stage 2.2) loads: no item has an upgrade gear score");
    json short_columns = old;
    short_columns["format"] = 3;
    check(!load(short_columns).first, "a file of format 3 with the 14 columns of format 2 does not load");
    json unknown = document;
    unknown["artifactObjectives"].push_back(json::array({"TestNotAnItem", "Objective_X", "Achievement_X", 5}));
    check(!load(unknown).first, "an artifact objective of an item that the list does not have: the file does not load");
    json wrong = document;
    wrong["items"][0][14] = "800";
    check(!load(wrong).first, "an upgrade gear score that is not a number: the file does not load");
}

/** BP2: the gear score limit of a give. */
void gear_score_limit(const Options&) {
    PresetShop shop;
    auto& inventory = shop.world().inventory();
    const auto given = [&](const char* item, std::uint32_t gearScore) {
        world::GiveOptions options;
        options.gearScore = gearScore;
        const world::ItemResult result = inventory.give(kCharacterA, item, 1, options);
        const world::Entity* entity = shop.character();
        return result.ok() && entity != nullptr ? entity->bag.slots[result.slots.front()].item->gearScore : 0U;
    };
    check(given(kBow, 800) == 800, "a give of the artifact at 800 keeps 800 (MaxCraftUpgradeGS 800, MaxEquipGS 800)");
    check(given(kBow, 801) == 800 && given(kBow, 650) == 700, "801 gets 800 and 650 gets 700: the nearest value of the range 700 to 800");
    check(given(kRing, 796) == 725, "the ring has no upgrade: 796 gets 725, the largest gear score of its item table");
    const std::uint32_t bowSlot = [&] {
        for (std::size_t slot = 0; slot < shop.character()->bag.slots.size(); ++slot) {
            const auto& item = shop.character()->bag.slots[slot].item;
            if (item && item->itemId == kBow && item->gearScore == 800) {
                return static_cast<std::uint32_t>(slot);
            }
        }
        return 0U;
    }();
    check(inventory.equip(kCharacterA, bowSlot, world::paperdoll::kMainHand1).ok()
              && shop.character()->equipment.item(world::paperdoll::kMainHand1)->gearScore == 800,
          "the equip of the artifact at 800 is accepted (not above MaxEquipGS 800)");
    // The expansion limit: MaxEquipGS 750 is the limit of the upgrade; the range of the item table stays.
    shop.host.progression.set(preset_progression(750));
    check(limit_of(inventory, upgrade_bow()) == 750 && given(kBow, 800) == 750, "with MaxEquipGS 750 the upgrade limit is 750");
    shop.host.progression.set(preset_progression(650));
    check(limit_of(inventory, upgrade_bow()) == 700, "with MaxEquipGS 650 the limit is the largest gear score of the item table (700)");
    shop.host.progression.set(preset_progression(800, false));
    check(limit_of(inventory, upgrade_bow()) == 800, "with no expansion data the upgrade has no limit of an expansion (800)");
    check(limit_of(inventory, upgrade_bow(0)) == 700, "an item with no upgrade (0, a file of format 2) keeps its range");
}

#ifndef SOULWARDEN_B0_BASELINE
/** BP3: a preset for a character in the world. */
void preset_in_world(const Options&) {
    PresetShop shop;
    shop.prepare();
    const std::string chestBefore = world::instance_text(shop.character()->equipment.item(world::paperdoll::kChest)->instance);
    const std::size_t from = shop.a.got.size();
    const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-build", "");
    shop.walker.idle(3);
    const world::Entity& a = *shop.character();
    check(result.ok() && result.inWorld && !result.changes.empty(), "the preset (the first variant with no name) is applied: " + result.cause);
    check(std::find(result.changes.begin(), result.changes.end(), "2 old item(s) of the paperdoll went into the bag") != result.changes.end(),
          "the reply names the 2 old items that went into the bag");
    const world::ItemInstance* bow = a.equipment.item(world::paperdoll::kMainHand1);
    check(bow != nullptr && bow->itemId == kBow && bow->gearScore == 800 && bow->perks[0] == "PerkID_StatSwap_BaseDamage"
              && bow->perks[2] == "PerkID_Gem_VoidDMG4" && bow->perkIndex[0] != 0,
          "weapon slot 1 has the artifact at 800 with its perks (the stored ids and their indexes)");
    const world::ItemInstance* chest = a.equipment.item(world::paperdoll::kChest);
    const world::ItemInstance* potion = a.equipment.item(world::paperdoll::kQuickFirst);
    check(chest != nullptr && chest->gearScore == 600 && world::instance_text(chest->instance) != chestBefore
              && a.equipment.item(world::paperdoll::kRing)->gearScore == 725 && a.equipment.item(world::paperdoll::kHeartRune) != nullptr
              && potion != nullptr && potion->itemId == "TestPotion" && potion->quantity == 5,
          "the chest armor is a new instance at 600; the ring, the heart rune, and 5 potions in quick slot 14");
    std::multiset<std::string> bag;
    for (const world::BagSlot& slot : a.bag.slots) {
        if (slot.item) {
            bag.insert(slot.item->itemId + ":" + std::to_string(slot.item->gearScore) + ":" + std::to_string(slot.item->quantity));
        }
    }
    check(bag == std::multiset<std::string>{"TestAxe:300:1", "TestChest:300:1", "TestFood:0:3"},
          "the old axe and the old chest armor went into the bag; the bag has 3 food");
    check(a.progression.level == 7 && a.attributes.value(world::attribute::kStrength) == 15
              && a.attributes.value(world::attribute::kDexterity) == 25 && a.attributes.unspent + 30 == shop.host.progression.attribute_points(7),
          "level 7, strength 15, dexterity 25 (30 of the 36 points of the level)");
    const auto& sword = a.skills.pools.at(kSwordTable);
    std::size_t learned = 0;
    for (const auto& [node, state] : sword) {
        learned += state.level != 0 ? 1 : 0;
    }
    check(learned == 11 && sword.at(node_id("Test_Sword_Bash")).level == 0 && sword.at(node_id("Test_Sword_Ultimate")).level == 1
              && a.skills.rank(kSwordTable) == 14 && a.skills.free_points(kSwordTable) == 3,
          "the sword tree has the 11 nodes of the preset (the old nodes Bash and D0a are gone: the tree is reset with no cost)");
    check(a.skills.bars.at(kSwordTable) == std::array<std::uint32_t, 3>{node_id("Test_Sword_Slash"), node_id("Test_Sword_Stab"), node_id("Test_Sword_Leap")},
          "the bar of the sword");
    check(std::find(a.achievements.begin(), a.achievements.end(), kObjectiveBit) != a.achievements.end(),
          "the character has the bit of the objective of the artifact (77)");
    const std::string rows = rows_of(shop.host.store, kCharacterA);
    check(rows.find("2:24:TestArtifactBow:1:800") != std::string::npos && rows.find("2:14:TestPotion:5") != std::string::npos
              && rows.find("1:2:TestFood:3") != std::string::npos
              && rows.find("| level 7 spent 0 0 10 20 0") != std::string::npos
              && rows.find("Test_Sword_Ultimate") != std::string::npos && rows.find("Test_Sword_Bash") == std::string::npos
              && rows.find(kObjectiveAchievement) != std::string::npos,
          "the rows: the items, the progress, the skills, and the achievement");
    note(rows.substr(0, 400));
    for (const std::uint32_t slot : {kPaperdollSlot, kContainerSlot, kAttributeSlot, kPointsSlot, kAbilitySlot, kAchievementSlot}) {
        check(!slot_updates(shop.a, slot, from).fragments.empty(), "the owner gets the state of slot " + std::to_string(slot));
    }
    // The second variant: only the sword tree changes. The items of the preset are in their slots already: they stay (the same
    // instances), and the bag has the food of the preset already (no second stack).
    const std::string bowInstance = world::instance_text(a.equipment.item(world::paperdoll::kMainHand1)->instance);
    const std::string itemRows = rows.substr(0, rows.find('|'));
    const world::PresetResult second = shop.world().presets().apply(kCharacterA, "test-build", "second");
    check(second.ok() && shop.character()->skills.bars.at(kSwordTable)[0] == node_id("Test_Sword_Bash")
              && shop.character()->skills.pools.at(kSwordTable).at(node_id("Test_Sword_Ultimate")).level == 0,
          "the second variant: the sword tree of the variant (Bash on place 0, no Ultimate)");
    const std::string after = rows_of(shop.host.store, kCharacterA);
    check(world::instance_text(shop.character()->equipment.item(world::paperdoll::kMainHand1)->instance) == bowInstance
              && after.substr(0, after.find('|')) == itemRows,
          "the second apply changes no item: the items of the preset stay in their slots, and the bag is the same");
    // A stack of the preset with fewer items (a potion used) gets the quantity of the preset again: the same instance.
    const std::string potionInstance = world::instance_text(shop.character()->equipment.item(world::paperdoll::kQuickFirst)->instance);
    check(shop.world().inventory().take_one(kCharacterA, true, world::paperdoll::kQuickFirst, [](const world::ItemInstance&) {
              return world::ItemError::none;
          }).ok() && shop.character()->equipment.item(world::paperdoll::kQuickFirst)->quantity == 4,
          "setup: one potion of quick slot 14 is used (4 left)");
    const world::PresetResult third = shop.world().presets().apply(kCharacterA, "test-build", "second");
    const world::ItemInstance* stack = shop.character()->equipment.item(world::paperdoll::kQuickFirst);
    check(third.ok() && stack->quantity == 5 && world::instance_text(stack->instance) == potionInstance
              && std::find(third.changes.begin(), third.changes.end(), "0 old item(s) of the paperdoll went into the bag") != third.changes.end(),
          "an apply again: the stack of quick slot 14 has the 5 potions of the preset again (the same instance), no item moves");
}

/** BP4: a refused preset changes nothing. */
void refused_preset(const Options& options) {
    {
        PresetShop shop;
        shop.prepare();
        const auto used = [&] {
            std::uint32_t count = 0;
            for (const world::BagSlot& slot : shop.character()->bag.slots) {
                count += slot.item ? 1 : 0;
            }
            return count;
        };
        // Each slot of the bag but one has an item: the preset needs a slot for each of its items of the paperdoll.
        check(shop.world().inventory().give(kCharacterA, kFiller, world::kMaxContainerSlots - used() - 1).ok(), "setup: the bag has one free slot");
        const std::string rows = rows_of(shop.host.store, kCharacterA);
        const std::string entity = entity_of(*shop.character());
        const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-build", "first");
        check(result.error == world::PresetError::items && result.cause.find("bag") != std::string::npos,
              "a bag with one free slot refuses the preset: " + result.cause);
        check(rows_of(shop.host.store, kCharacterA) == rows && entity_of(*shop.character()) == entity, "nothing changed: the rows and the entity");
        check(shop.world().presets().apply(kCharacterA, "no-such-preset", "").error == world::PresetError::unknownPreset
                  && shop.world().presets().apply(kCharacterA, "test-build", "third").error == world::PresetError::unknownVariant
                  && rows_of(shop.host.store, kCharacterA) == rows,
              "an unknown preset or variant changes nothing");
    }
    {
        PresetShop shop;
        world::BuildPreset high = test_preset();
        high.level = 8;
        shop.world().presets().set({high});
        shop.world().presets().check();
        const auto& list = shop.world().presets().presets();
        const std::string rows = rows_of(shop.host.store, kCharacterA);
        const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-build", "");
        check(!list.front().enabled && list.front().cause.find("level") != std::string::npos && result.error == world::PresetError::disabled
                  && rows_of(shop.host.store, kCharacterA) == rows,
              "a level above the cap (8, the cap 7) disables the preset at the check; its apply changes nothing: " + list.front().cause);
    }
    {
        // A refused write in the transaction (the last write: the achievement row): no row of the other systems, and no change of the
        // entity (the rows of the items, the progress, and the skills were savepoints of the one transaction).
        const auto path = options.workDirectory / "build-preset-refused.sqlite3";
        std::filesystem::create_directories(options.workDirectory);
        for (const char* suffix : {"", "-wal", "-shm"}) {
            std::filesystem::remove(path.string() + suffix);
        }
        PresetShop shop(path.string());
        shop.prepare();
        sqlite3* connection = nullptr;
        const bool trigger = sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK
                             && sqlite3_exec(connection,
                                             "CREATE TRIGGER refuse_objective BEFORE INSERT ON achievements "
                                             "WHEN NEW.achievement = 'Test_Objective_Achievement' BEGIN SELECT RAISE(ABORT, 'refused'); END;",
                                             nullptr, nullptr, nullptr)
                                    == SQLITE_OK;
        sqlite3_close(connection);
        check(trigger, "setup: the database refuses the row of the achievement of the objective");
        const std::string rows = rows_of(shop.host.store, kCharacterA);
        const std::string entity = entity_of(*shop.character());
        const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-build", "");
        check(result.error == world::PresetError::storeFailed && rows_of(shop.host.store, kCharacterA) == rows
                  && entity_of(*shop.character()) == entity,
              "a refused write of the transaction: no row changed (the items, the progress, the skills) and the entity is the same");
    }
}

/** BP5: two sessions (rule MP-10). */
void two_sessions(const Options&) {
    PresetShop shop;
    shop.prepare();
    shop.enter_b();
    check(shop.host.session(1).observer().known().size() == 2 && shop.host.session(2).observer().known().size() == 2,
          "setup: each session observes the entity of the other character");
    const std::size_t fromB = shop.b.got.size();
    const std::string rowsB = rows_of(shop.host.store, kCharacterB);
    const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-build", "");
    for (int index = 0; index < 4; ++index) {
        shop.host.clock.advance(shop::kTick);
        shop.host.datagram(shop.b.peer, shop.b.report_at(static_cast<std::uint16_t>(200 + index), shop::kPlace.x - 1.0F, shop::kPlace.y + 1.0F,
                                                         shop::kHeight));
        shop.host.tick();
        shop.b.exchange(shop.host);
        shop.a.exchange(shop.host);
    }
    const std::set<std::uint32_t> seen = observed_slots(shop.b, fromB);
    std::string text;
    for (const std::uint32_t slot : seen) {
        text += std::to_string(slot) + " ";
    }
    note("the slots of the records of A in session B after the preset: " + text);
    const std::vector<std::uint32_t> visible = observed_visible(shop.b, fromB);
    check(result.ok() && seen.count(kPaperdollSlot) == 1 && std::find(visible.begin(), visible.end(), world::paperdoll::kMainHand1) != visible.end()
              && std::find(visible.begin(), visible.end(), world::paperdoll::kChest) != visible.end(),
          "session B gets the paperdoll of A with the visible slots of the preset (weapon slot 1, the chest)");
    check(std::find(visible.begin(), visible.end(), world::paperdoll::kRing) == visible.end()
              && std::find(visible.begin(), visible.end(), world::paperdoll::kHeartRune) == visible.end(),
          "the observer view has no ring and no heart rune");
    bool owner = false;
    for (const std::uint32_t slot : {kContainerSlot, kAttributeSlot, kPointsSlot, kAbilitySlot, kAchievementSlot, kCategoricalSlot}) {
        owner = owner || seen.count(slot) != 0;
    }
    check(!owner, "session B gets no owner state of A (the bag, the attributes, the skills, the bars, the achievements)");
    check(rows_of(shop.host.store, kCharacterB) == rowsB && shop.host.world.find_character(kCharacterB)->progression.level == 1,
          "the preset of A changes nothing of B");
}

/** BP6: the check at the start. */
void start_check(const Options&) {
    {
        // No game data: the project preset is disabled, the backend works.
        ServerHost host;
        host.tick();
        const auto& list = host.world.presets().presets();
        check(list.size() == 1 && list.front().preset.name == "bow-boltcaster" && !list.front().enabled
                  && list.front().cause.find("item catalog") != std::string::npos,
              "with no game data the project preset is disabled: " + (list.empty() ? std::string() : list.front().cause));
        check(host.world.presets().apply(kCharacterA, "bow-boltcaster", "pve").error == world::PresetError::disabled,
              "its apply is refused (disabled)");
    }
    PresetShop shop;
    world::BuildPreset unknown = test_preset();
    unknown.items[2].itemId = "TestNotInTheCatalog";
    world::BuildPreset gearScore = test_preset();
    gearScore.name = "test-gear-score";
    gearScore.items[2].gearScore = 796;
    world::BuildPreset node = test_preset();
    node.name = "test-node";
    node.variants[1].trees[0].nodes.push_back("Test_Sword_NotANode");
    world::BuildPreset order = test_preset();
    order.name = "test-order";
    std::swap(order.variants[0].trees[0].nodes[0], order.variants[0].trees[0].nodes[3]); // SlashUp before the Slash that it needs
    world::BuildPreset perk = test_preset();
    perk.name = "test-perk";
    perk.items[0].perks[1] = "PerkID_Test_Deprecated";
    shop.world().presets().set({unknown, gearScore, node, order, perk, test_preset()});
    shop.world().presets().check();
    const auto& list = shop.world().presets().presets();
    for (const auto& status : list) {
        note(status.preset.name + ": " + (status.enabled ? "enabled" : status.cause));
    }
    check(list.size() == 6 && !list[0].enabled && !list[1].enabled && !list[2].enabled && !list[3].enabled && !list[4].enabled && list[5].enabled,
          "an unknown item, a gear score outside the range of its item (796 for the ring), an unknown node, a node before the node that "
          "it needs, and a deprecated perk disable a preset; the correct preset stays enabled");
    check(list[1].cause.find("796") != std::string::npos, "the cause names the gear score");

    // The content of the project preset (project data: no game data needed).
    const std::vector<world::BuildPreset> project = world::project_presets();
    check(project.size() == 1 && project[0].name == "bow-boltcaster" && project[0].level == 70 && project[0].variants.size() == 2
              && project[0].variants[0].name == "pve" && project[0].variants[1].name == "pvp",
          "the project preset bow-boltcaster: level 70, the variants pve (the first: the default) and pvp");
    const auto& attributes = project[0].attributes;
    check(attributes[world::attribute::kStrength] == 50 && attributes[world::attribute::kDexterity] == 456 && attributes[world::attribute::kIntelligence] == 5
              && attributes[world::attribute::kFocus] == 5 && attributes[world::attribute::kConstitution] == 19,
          "the attributes STR 50, DEX 456, INT 5, FOC 5, CON 19 (510 points above the minimum 5)");
    bool trees = true;
    for (const world::PresetVariant& variant : project[0].variants) {
        trees = trees && variant.trees.size() == 2 && variant.trees[0].nodes.size() == 19 && variant.trees[1].nodes.size() == 19
                && variant.trees[0].rank == 19 && variant.trees[1].rank == 19;
    }
    check(trees && project[0].variants[0].trees[1].nodes == project[0].variants[1].trees[1].nodes
              && project[0].variants[0].trees[0].nodes != project[0].variants[1].trees[0].nodes
              && project[0].variants[0].trees[0].bar == project[0].variants[1].trees[0].bar,
          "each variant has the bow tree and the rapier tree with 19 nodes at the level 20; only the bow nodes differ");
    std::set<std::uint32_t> slots;
    std::size_t equipment = 0;
    std::string ring;
    std::string earring;
    for (const world::PresetItem& item : project[0].items) {
        if (item.slot) {
            ++equipment;
            slots.insert(*item.slot);
            if (*item.slot == world::paperdoll::kRing) {
                ring = item.itemId + ":" + std::to_string(item.gearScore);
            }
            if (*item.slot == world::paperdoll::kToken) {
                earring = item.perks[4];
            }
        }
    }
    check(equipment == 14 && slots.size() == 14 && ring == "RingT5_HercyneRaid_CraftedShard:725" && earring == "PerkID_Slottable_Common_DefBasic",
          "14 items of the paperdoll in 14 slots (11 of the gear, 3 quick slots); the ring at 725 (decision D3); place 5 of the earring "
          "Enchanted Ward II (decision D2)");
}

/** BP7: the routes. */
void routes(const Options&) {
    PresetShop shop;
    shop.enter_b();
    server::http::AdminAccess access;
    check(access.add(kTokenA, kSteamIdA) && access.add(kTokenB, "76561202255233019"), "setup: the tokens of the two accounts");
    server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::Router router;
    server::http::register_admin_routes(router, services, access);
    server::http::register_dev_routes(router, services);
    const auto send = [&](const char* method, const std::string& path, const std::string& query, const std::string& body, const char* token) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = query.empty() ? path : path + "?" + query;
        request.body = body;
        if (token != nullptr) {
            request.headers.emplace_back(server::http::kAdminTokenHeader, token);
        }
        return server::http::handle_request(router, request);
    };
    const std::string rows = rows_of(shop.host.store, kCharacterA);
    const service::HttpResponse foreign =
        send("POST", "/admin/v1/commands", "", json{{"command", "apply_preset"}, {"character", kCharacterA}, {"args", {{"preset", "test-build"}}}}.dump(), kTokenB);
    check(foreign.status == 404 && rows_of(shop.host.store, kCharacterA) == rows, "a token of a different account: 404, nothing changes");
    const service::HttpResponse catalog = send("GET", "/admin/v1/catalog/presets", "", "", kTokenA);
    const json list = json::parse(catalog.body, nullptr, false);
    check(catalog.status == 200 && list["presets"].size() == 1 && list["presets"][0]["name"] == "test-build" && list["presets"][0]["enabled"] == true
              && list["presets"][0]["variants"][1]["label"] == "Second tree",
          "the catalog route gives the presets with their variants");
    check(send("GET", "/admin/v1/catalog/presets", "", "", nullptr).status == 401, "the catalog route needs the token");
    const service::HttpResponse own =
        send("POST", "/admin/v1/commands", "", json{{"command", "apply_preset"}, {"character", kCharacterA}, {"args", {{"preset", "test-build"}, {"variant", "second"}}}}.dump(), kTokenA);
    const json reply = json::parse(own.body, nullptr, false);
    check(own.status == 200 && reply["applied"] == "world" && reply["changes"].is_array() && !reply["changes"].empty()
              && shop.character()->skills.bars.at(kSwordTable)[0] == node_id("Test_Sword_Bash"),
          "the admin command applies the variant for a character of the account of the token");
    const service::HttpResponse bad =
        send("POST", "/admin/v1/commands", "", json{{"command", "apply_preset"}, {"character", kCharacterA}, {"args", {{"preset", "test-build"}, {"variant", "x"}}}}.dump(), kTokenA);
    check(bad.status == 404, "an unknown variant: 404");
    const service::HttpResponse dev = send("POST", "/v1/dev/presets/apply", std::string("character=") + kCharacterB + "&preset=test-build&variant=first", "", nullptr);
    check(dev.status == 200 && shop.host.world.find_character(kCharacterB)->progression.level == 7, "the development route applies the preset for B");
    const service::HttpResponse devList = send("GET", "/v1/dev/presets", "", "", nullptr);
    check(devList.status == 200 && json::parse(devList.body, nullptr, false)["presets"].size() == 1, "the development list of the presets");
}

/** BP8: a character that is not in the world. */
void preset_stored(const Options&) {
    PresetShop shop(":memory:", false);
    const world::PresetResult result = shop.world().presets().apply(kCharacterA, "test-build", "first");
    check(result.ok() && !result.inWorld, "the preset for a character that is not in the world: the rows only");
    shop.host.enter(shop.a, shop.alpha.registration());
    shop.walker.idle(20);
    const world::Entity* a = shop.character();
    check(a != nullptr && a->equipment.item(world::paperdoll::kMainHand1) != nullptr
              && a->equipment.item(world::paperdoll::kMainHand1)->gearScore == 800 && a->progression.level == 7
              && a->skills.bars.at(kSwordTable)[2] == node_id("Test_Sword_Leap")
              && std::find(a->achievements.begin(), a->achievements.end(), kObjectiveBit) != a->achievements.end(),
          "the next world entry has the build and the bit of the objective (the row of the achievement)");
    const auto bits = slot_updates(shop.a, kAchievementSlot).fragments;
    check(!bits.empty(), "the first record has the achievement state");
}

#endif

/** BP9: the list of the passive rules (B0-I5). */
void passive_list(const Options&) {
    PresetShop shop;
    const world::Passives& passives = shop.world().passives();
    const gamedata::CellValues target = shop::cells({{"TargetHealthPercent", 50.0F}, {"BaseDamage", 0.15F}}, {{"WeaponTag", "Bow"}});
    const world::RuleView low = passives.describe_row("Test_Bow_FinishingShot", &target, world::RuleOrigin::node);
    check(!low.applied && low.note.find("TargetHealthPercent") != std::string::npos,
          "a node row with a target condition and no event (Finishing Shot) is not applied: " + low.note);
    const gamedata::CellValues refreshing =
        shop::cells({{"IsStackableAbility", 1.0F}, {"IsStackableMax", 3.0F}, {"CooldownTimer", -0.02F}}, {{"CDRImmediatelyOptions", "ActiveWeapon"}});
    const world::RuleView cdr = passives.describe_row("Test_Common_CDR", &refreshing, world::RuleOrigin::perk);
    check(!cdr.applied && cdr.note.find("CooldownTimer") != std::string::npos,
          "a perk row with a cooldown change and no event (Refreshing) is not applied: " + cdr.note);
    const gamedata::CellValues fixed = shop::cells({{"BaseDamage", 0.1F}}, {{"WeaponTag", "Bow"}});
    const world::RuleView value = passives.describe_row("Test_Bow_Fixed", &fixed, world::RuleOrigin::node);
    check(value.applied && value.note.find("fixed") != std::string::npos, "control: a fixed value with no condition stays applied: " + value.note);
    const gamedata::CellValues hit = shop::cells({{"TargetHealthPercent", 100.0F}, {"BaseDamage", 0.15F}}, {{"DamageTableRow", "Test_Heavy"}});
    const world::RuleView opening = passives.describe_row("Test_Bow_Opening", &hit, world::RuleOrigin::node);
    check(opening.applied, "control: a target condition with a damage row (Opening Strike) stays a rule of a hit: " + opening.note);
}

} // namespace

std::vector<Case> build_preset_cases() {
    return {{"BP1", "item catalog format 3: the upgrade gear score and the artifact objectives (tool and loader)", catalog_format},
            {"BP2", "the gear score limit of a give: the upgrade to the expansion limit, the nearest value, the equip at 800", gear_score_limit},
#ifndef SOULWARDEN_B0_BASELINE
            {"BP3", "a build preset for a character in the world: items, bag, level, attributes, trees, bars, achievement, rows, states",
             preset_in_world},
            {"BP4", "a refused build preset changes nothing: a full bag, a disabled preset, an unknown name, a refused write", refused_preset},
            {"BP5", "a build preset with two sessions: the observer gets the visible gear and no owner state (MP-10)", two_sessions},
            {"BP6", "the check of the build presets at the start, and the content of the project preset", start_check},
            {"BP7", "the routes of the build presets: the admin command and its owner check, the catalog, the development route", routes},
            {"BP8", "a build preset for a character that is not in the world: the rows, then the world entry", preset_stored},
#endif
            {"BP9", "the list of the passive rules: a target condition with no event and a cooldown of a fixed row are not applied", passive_list}};
}

} // namespace soulwarden::backend::tests
