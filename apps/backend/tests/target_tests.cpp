// The cases of stage QG5c1 (docs/systems/creatures.md, the stationary targets; docs/systems/rewards.md): a totem or a nest is a creature
// of the same table with a PositionInTheWorld component and no movement component. Made-up slices, kinds, places, quests, and accounts.
//
//   QG5C1A  the creature file: a stationary slice (the member `place`, no movement slot) loads; a file of before (no member) loads; the
//           loader refuses a stationary slice with an action, a place slot of a wrong type, a slice with a movement slot and a place slot,
//           and a slice with neither
//   QG5C1B  (I4, I7, MP-10) two sessions near one totem of a spawn point: the first record of each observer (the metadata, the place
//           state of type 13 at the place slot, the vitals, the marker, no movement state); the hits of A and B with the behavior on (no
//           reaction, no target, no push, no knockdown, no movement); one kill result with the credit of both (the quest kill of each one
//           time, the reward counts); the dead vitals to each observer; no second kill; the respawn time of the data and a new totem
//   QG5C1C  (I5, I6) the support manifest: a kill task of the category of a totem of a spawn point is supported; a category with no
//           spawn point stays blocked, and its reason names the kept-out cause of the creature data

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::json;

constexpr std::uint32_t kTotemVitals = 1;
constexpr std::uint32_t kTotemPlace = 3;
constexpr std::uint32_t kTotemMarker = 5;
constexpr std::uint32_t kTotemStatus = 7;
constexpr std::uint32_t kTypeMetadata = 10;
constexpr std::uint32_t kTypeMovement = 11;
constexpr std::uint32_t kTypePlace = 13;
constexpr std::uint32_t kTypeVitals = 15;
constexpr std::uint32_t kTypeMarker = 5399;
constexpr state::WorldPosition kTotemAt{100.0F, 206.0F, 30.0F};
constexpr float kTotemRespawn = 30.0F;

gamedata::CreatureSlice totem_slice() {
    gamedata::CreatureSlice slice;
    slice.path = "slices/test/totem.dynamicslice";
    slice.asset.fill(0x7A);
    slice.subId = 2;
    slice.root = 0x100000000000007AULL;
    slice.interestRadius = 60.0F;
    slice.movementSlot = 0;
    slice.placeSlot = kTotemPlace;
    slice.vitalsSlot = kTotemVitals;
    slice.markerSlot = kTotemMarker;
    slice.slots = {{kTotemVitals, "Vitals", 15}, {2, "DamageReceiver", 1528}, {kTotemPlace, "PositionInTheWorld", 13},
                   {kTotemMarker, "Marker", 5399}, {kTotemStatus, "StatusEffects", 4236}};
    slice.components = {{slice.root, 0x0000000000000A7AULL, "Totem/Vitals"}};
    return slice;
}

gamedata::CreatureKind totem_kind() {
    gamedata::CreatureKind kind;
    kind.id = "Test_Totem_60";
    kind.vitals = "Test_Totem_60";
    kind.category = "Test_Totem_Category";
    kind.vitalsId = 0x7A7A7A01;
    kind.categoryId = 0x7A7A7A02;
    kind.name = "Test Totem";
    kind.vitalsName = "Test Totem";
    kind.creatureType = "Solo";  // no family: a structure of the data (Lost_Totem_60)
    kind.healthFactor = 1.0F;
    kind.level = 20;
    kind.bodies = {{1, 100}};
    return kind;
}

gamedata::SpawnPoint totem_point() {
    gamedata::SpawnPoint point;
    point.id = "Test_Totem_Point";
    point.kind = 1;
    point.level = 20;
    point.position = kTotemAt;
    point.yaw = 90.0F;
    point.respawnMin = kTotemRespawn;
    point.respawnMax = kTotemRespawn;
    return point;
}

json target_quests() {
    json document{{"format", 1},           {"objectives", json::array()}, {"tasks", json::object()}, {"achievements", json::array()},
                  {"npa", json::array()}, {"territories", json::array()}, {"backstories", json::array()}};
    const auto add = [&](const char* objective, const char* task, const char* category) {
        document["tasks"][gamedata::lower_text(task)] = questfix::task(task, "taskkillcontribution", 13, {{"KillEnemyType", category}, {"TargetQty", "2"}});
        document["objectives"].push_back({{"id", objective},
                                          {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}}},
                                          {"tree", json::array({json::array({0, gamedata::lower_text(task), json::array()})})}});
    };
    add("Test_TotemHunt", "Test_TotemHunt_Kill", "Test_Totem_Category");
    add("Test_NoTotem", "Test_NoTotem_Kill", "Test_Absent_Category");
    return document;
}

/** The gathering world (two sessions, the wolf and the loot of the gathering cases) with one totem of a spawn point and its quests. */
struct TargetShop : GatherShop {
    TargetShop() {
        gamedata::LootData loot = gathering_loot();
        gamedata::LootVitals totem;
        totem.id = "Test_Totem_60";
        totem.creatureType = "Solo";
        totem.categories = {"test_totem_60", "structure"};
        loot.bases.push_back(totem);
        loot.vitals["test_totem_60"] = static_cast<std::uint32_t>(loot.bases.size() - 1);
        // The credit data of the game: 10 receivers (the gathering cases have 1).
        loot.credit.receivers = 10;
        loot.credit.modifiers.assign(10, {1.0F, 1.0F, 1.0F});
        if (!host.loot.set(loot)) {
            throw std::runtime_error("target test: the loot data was not set: " + host.loot.error());
        }
        if (!host.creatures.set_full({bags::bag_wolf_slice(), totem_slice()}, {bags::bag_wolf_kind(), totem_kind()}, {totem_point()},
                                     {{20, bags::kWolfHealth, 0.0F, 0.0F}})) {
            throw std::runtime_error("target test: the creature data was not set: " + host.creatures.error());
        }
        if (!host.quests.load_text(target_quests().dump())) {
            throw std::runtime_error("target test: the quest data did not load: " + host.quests.error());
        }
        host.world.set_creature_behavior(true);
    }
    world::Entity* totem() {
        for (const world::EntityId id : host.world.creatures().ids()) {
            world::Entity* entity = host.world.find(id);
            if (entity != nullptr && entity->creature.kind != nullptr && entity->creature.kind->id == "Test_Totem_60") {
                return entity;
            }
        }
        return nullptr;
    }
    /** The world goes on by `seconds` in steps of 100 ms; the clients report their place each step. */
    void wait(float seconds) {
        for (float passed = 0.0F; passed < seconds; passed += 0.1F) {
            report(clientA, std::nullopt);
            report(clientB, std::nullopt);
            host.steps(10, {&clientA, &clientB});
        }
    }
};

bool close_to(float left, float right, float tolerance = 0.01F) {
    return std::abs(left - right) <= tolerance;
}

float f32(const Bytes& bytes, std::size_t offset = 0) {
    std::uint32_t bits = 0;
    for (std::size_t index = 0; index < 4 && offset + index < bytes.size(); ++index) {
        bits = (bits << 8U) | bytes[offset + index];
    }
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/** What one client got for the totem: its interest, the slots and types of its fragments, its vitals health values. */
struct Seen {
    std::uint32_t interest{0};
    std::vector<std::pair<std::uint32_t, std::uint32_t>> first;
    std::size_t movement{0};
    std::size_t places{0};
    float x{0.0F};
    float y{0.0F};
    std::vector<float> health;
    std::size_t stops{0};
};

Seen seen(const FakeClient& client, const std::array<std::uint8_t, 16>& asset, std::size_t from = 0) {
    Seen out;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        const tests::Bundle bundle = read_bundle(client.got[index].body);
        for (const tests::Record& record : bundle.records) {
            if (out.interest == 0) {
                for (const tests::Fragment& fragment : record.fragments) {
                    const auto field = fragment.fields.find({0, 0});
                    if (fragment.type == kTypeMetadata && field != fragment.fields.end() && field->second.size() >= 16
                        && std::equal(asset.begin(), asset.end(), field->second.begin())) {
                        out.interest = record.interest;
                        for (const tests::Fragment& part : record.fragments) {
                            out.first.emplace_back(part.slot, part.type);
                        }
                    }
                }
            }
            if (record.interest != out.interest || out.interest == 0) {
                continue;
            }
            for (const tests::Fragment& fragment : record.fragments) {
                out.movement += fragment.type == kTypeMovement ? 1 : 0;
                if (fragment.type == kTypePlace) {
                    ++out.places;
                    const auto place = fragment.fields.find({0, 0});
                    if (place != fragment.fields.end()) {
                        out.x = f32(place->second, 0);
                        out.y = f32(place->second, 4);
                    }
                }
                if (fragment.type == kTypeVitals) {
                    const auto amount = fragment.fields.find({0, 0});
                    if (amount != fragment.fields.end()) {
                        out.health.push_back(f32(amount->second));
                    }
                }
            }
        }
        for (const std::uint32_t stop : bundle.stops) {
            out.stops += out.interest != 0 && stop == out.interest ? 1 : 0;
        }
    }
    return out;
}

bool has(const Seen& view, std::uint32_t slot, std::uint32_t type) {
    return std::find(view.first.begin(), view.first.end(), std::make_pair(slot, type)) != view.first.end();
}

std::uint32_t progress(world::Entity& entity, const char* objective) {
    const world::QuestInstance* quest = questfix::quest_of(entity, objective);
    return quest != nullptr && !quest->tasks.empty() ? quest->tasks[0].progress : 9999;
}

/** QG5C1A: the loader of a stationary target. */
void target_file(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "targets";
    std::filesystem::create_directories(folder);
    const json totem{{"path", "slices/test/totem.dynamicslice"},
                     {"asset", "7a7a7a7a-7a7a-7a7a-7a7a-7a7a7a7a7a7a"},
                     {"sub", 2},
                     {"root", "100000000000007A"},
                     {"interestRadius", 60.0},
                     {"movement", 0},
                     {"vitals", 1},
                     {"marker", 5},
                     {"place", 3},
                     {"slots", {{1, "Vitals", 15}, {2, "DamageReceiver", 1528}, {3, "PositionInTheWorld", 13}, {5, "Marker", 5399}}},
                     {"components", json::array()},
                     {"actions", json::array()}};
    const json wolf{{"path", "slices/test/wolf.dynamicslice"},
                    {"asset", "11111111-1111-1111-1111-111111111111"},
                    {"sub", 2},
                    {"root", "1000000000000011"},
                    {"interestRadius", 100.0},
                    {"movement", 5},
                    {"vitals", 9},
                    {"marker", 15},
                    {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                    {"components", json::array()},
                    {"actions", {{"Idle_No_Task", "Idle", 0, false}}}};
    const auto document = [&](const json& slice) {
        return json{{"format", 1},
                    {"counts", {{"spawnPoints", 1}, {"kinds", 2}, {"slices", 2}}},
                    {"levels", {{20, 1000.0}}},
                    {"slices", json::array({wolf, slice})},
                    {"kinds",
                     {{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 1}, {"categoryId", 2},
                       {"name", "Test Wolf"}, {"vitalsName", "Test Wolf"}, {"family", "Wildlife"}, {"creatureType", "Solo"}, {"health", 1.0},
                       {"level", 20}, {"bodies", json::array({json::array({0, 100})})}, {"points", 0}},
                      {{"id", "Test_Totem_60"}, {"vitals", "Test_Totem_60"}, {"category", "Test_Totem_Category"}, {"vitalsId", 3},
                       {"categoryId", 4}, {"name", "Test Totem"}, {"vitalsName", "Test Totem"}, {"family", ""}, {"creatureType", "Solo"},
                       {"health", 1.0}, {"level", 20}, {"bodies", json::array({json::array({1, 100})})}, {"points", 1}}}},
                    {"spawnPoints", {{"Test_Totem_Point", 1, 20, 100.0, 206.0, 30.0, 90.0, 30.0, 30.0}}}};
    };
    const auto load = [&](const char* name, const json& slice, gamedata::CreatureData& data) {
        const std::filesystem::path path = folder / name;
        std::ofstream(path, std::ios::binary) << document(slice).dump();
        return data.load(path.string());
    };
    gamedata::CreatureData data;
    check(load("totem.json", totem, data) && data.slices().size() == 2 && data.slices()[1].stationary() && data.slices()[1].placeSlot == 3
              && data.slices()[1].movementSlot == 0 && !data.slices()[0].stationary() && data.slices()[0].placeSlot == 0,
          "a stationary slice loads: its place slot 3, no movement slot; a slice with a movement slot is not stationary");
    json before = wolf;
    before["path"] = "slices/test/wolf2.dynamicslice";
    gamedata::CreatureData old;
    check(load("before.json", before, old) && !old.slices()[1].stationary(), "a file with no member place (a tool of before) loads");
    const auto refused = [&](const char* name, const json& slice) {
        gamedata::CreatureData broken;
        return !load(name, slice, broken) && !broken.loaded() && !broken.error().empty();
    };
    json acting = totem;
    acting["actions"] = {{"Idle_No_Task", "Idle", 0, false}};
    json wrongType = totem;
    wrongType["slots"] = {{1, "Vitals", 15}, {3, "Marker", 5399}, {5, "Marker", 5399}};
    json both = totem;
    both["movement"] = 2;
    both["slots"] = {{1, "Vitals", 15}, {2, "ActionList", 11}, {3, "PositionInTheWorld", 13}, {5, "Marker", 5399}};
    json neither = totem;
    neither.erase("place");
    check(refused("acting.json", acting), "a stationary slice with an action is refused (a target never moves or attacks)");
    check(refused("wrong-type.json", wrongType), "a place slot whose component has not the type 13 is refused");
    check(refused("both.json", both) && refused("neither.json", neither),
          "a slice with a movement slot and a place slot, and a slice with neither, are refused");
}

/** QG5C1B: two sessions near one totem of a spawn point. */
void target_sessions(const Options&) {
    TargetShop shop;
    shop.enter();
    shop.wait(1.0F);
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(a, "Test_TotemHunt", "command", 0).ok() && shop.host.world.quests().grant(b, "Test_TotemHunt", "command", 0).ok(),
          "control: A and B have the totem quest");
    world::Entity* totem = shop.totem();
    check(totem != nullptr && !totem->creature.dead && totem->creature.spawnPoint == 0U,
          "the spawn point makes its totem when a player is near (the distance rule of the creatures)");
    if (totem == nullptr) {
        return;
    }
    const world::EntityId first = totem->id;
    const float health = totem->vitals.healthMax;
    const std::array<std::uint8_t, 16> asset = totem_slice().asset;
    const Seen beforeA = seen(shop.clientA, asset);
    const Seen beforeB = seen(shop.clientB, asset);
    for (const Seen* view : {&beforeA, &beforeB}) {
        check(view->interest != 0 && has(*view, 0, kTypeMetadata) && has(*view, kTotemPlace, kTypePlace) && has(*view, kTotemVitals, kTypeVitals)
                  && has(*view, kTotemMarker, kTypeMarker) && view->movement == 0,
              std::string("the first record of ") + (view == &beforeA ? "A" : "B")
                  + ": the metadata, the place (type 13, slot 3), the vitals (slot 1), the marker (slot 5), no movement state");
        check(close_to(view->x, kTotemAt.x) && close_to(view->y, kTotemAt.y), "the place state has the place of the spawn point");
    }
    note("QG5C1B first record of A: interest " + std::to_string(beforeA.interest) + ", fragments " + std::to_string(beforeA.first.size()));

    // The hits of A and of B (each one more than 1 percent of the health), with the behavior on.
    world::HitPush push;
    world::Creatures& creatures = shop.host.world.creatures();
    check(creatures.hurt(first, health * 0.3F, a.id, 0, {}, &push).error == world::CreatureError::none && !push.length,
          "A hurts the totem: no push of a reaction");
    shop.wait(3.0F);
    totem = shop.host.world.find(first);
    check(totem != nullptr && totem->position.x == kTotemAt.x && totem->position.y == kTotemAt.y && totem->position.z == kTotemAt.z
              && !totem->creature.action && !totem->creature.moving && totem->creature.mind.mode == world::CreatureMode::rest
              && totem->creature.mind.target == 0,
          "with the behavior on the totem keeps its place, plays no reaction, takes no target, and does not walk");
    gamedata::DamageImpact impact;
    impact.distance = {0.0F, 3.0F, 0.0F};
    check(!creatures.impact(*totem, impact, a.position, a.position, "Test_Push") && !creatures.knock(*totem, 4, std::chrono::seconds(2)),
          "the totem takes no push and no knockdown");
    const std::uint64_t killsBefore = shop.host.world.rewards().counts().kills;
    check(creatures.hurt(first, health, b.id).error == world::CreatureError::none && totem->creature.dead,
          "B kills the totem: one path with the death of a creature");
    shop.wait(1.0F);
    check(shop.host.world.rewards().counts().kills == killsBefore + 1, "the death gives one kill result (Rewards::killed)");
    const world::QuestInstance* questA = questfix::quest_of(a, "Test_TotemHunt");
    note("QG5C1B after the kill: credited " + std::to_string(shop.host.world.rewards().counts().credited) + ", no credit "
         + std::to_string(shop.host.world.rewards().counts().killsWithNoCredit) + ", the quest of A "
         + (questA != nullptr ? questfix::states_text(*questA) : std::string("none")));
    check(progress(a, "Test_TotemHunt") == 1 && progress(b, "Test_TotemHunt") == 1,
          "A and B have the credit: the kill task of each one counts one time (the category of the spawn point)");
    check(creatures.hurt(first, 10.0F, a.id).error == world::CreatureError::dead && shop.host.world.rewards().counts().kills == killsBefore + 1
              && progress(a, "Test_TotemHunt") == 1,
          "a hit on the dead totem changes nothing: no second kill result");
    const Seen afterA = seen(shop.clientA, asset, 0);
    const Seen afterB = seen(shop.clientB, asset, 0);
    for (const Seen* view : {&afterA, &afterB}) {
        check(!view->health.empty() && view->health.back() == 0.0F && view->movement == 0 && view->places == 1,
              std::string("observer ") + (view == &afterA ? "A" : "B") + " gets the vitals of the hits and the death; one place state, no movement");
        check(std::find_if(view->health.begin(), view->health.end(), [&](float value) { return close_to(value, health * 0.7F, 1.0F); }) != view->health.end(),
              "the vitals of the first hit (70 percent) went to the observer");
    }

    // The respawn: the time of the spawner (30 s), then a new totem with a new entity id at the place of the point.
    const auto& points = creatures.spawn_points();
    check(!points.empty() && points[0].respawnAt && *points[0].respawnAt - totem->creature.diedAt == std::chrono::seconds(30),
          "the death gives the spawn point the respawn time of its spawner (30 s)");
    shop.wait(25.0F);
    check(shop.host.world.find(first) != nullptr && shop.host.world.find(first)->creature.dead, "before the respawn time the body stays");
    shop.wait(6.0F);
    world::Entity* again = shop.totem();
    check(again != nullptr && again->id != first && !again->creature.dead && again->vitals.health == health && again->position.x == kTotemAt.x
              && shop.host.world.find(first) == nullptr,
          "at the respawn time the point removes the body and makes a new totem (a new entity id, full health)");
    const Seen stopA = seen(shop.clientA, asset, 0);
    check(stopA.stops == 1, "observer A gets the stop of the old totem");
}

/** QG5C1C: the support manifest uses the totem kinds of the spawn points. */
void target_manifest(const Options&) {
    TargetShop shop;
    shop.host.creatures.set_kept_out({{"test_absent_category", "the client does not show its slice test_hidden.dynamicslice (the list notShown)"}});
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest({});
    const auto row = [&](const char* id) -> json {
        for (const json& entry : manifest["objectives"]) {
            if (entry["id"] == id) {
                return entry;
            }
        }
        return json::object();
    };
    const json hunt = row("Test_TotemHunt");
    const json none = row("Test_NoTotem");
    check(!hunt.empty() && hunt["tasks"][0]["supported"] == true, "a kill task of the category of a totem of a spawn point is supported");
    check(!none.empty() && none["tasks"][0]["supported"] == false
              && none["tasks"][0]["reason"].get<std::string>().find("no creature of a spawn point") != std::string::npos,
          "a kill task of a category with no spawn point keeps its reason");
    check(none["tasks"][0]["reason"].get<std::string>().find("test_absent_category: the client does not show its slice test_hidden") != std::string::npos,
          "the reason names the cause of the creature data for a kept-out category (the member keptOut)");
}

} // namespace

std::vector<Case> target_cases() {
    return {{"QG5C1A", "stationary targets QG5c1 (I4): the creature file: the place slot, no movement slot, the refusals", target_file},
            {"QG5C1B", "stationary targets QG5c1 (I4, I7, MP-10): two sessions near one totem: the records, the hits, one kill, the respawn",
             target_sessions},
            {"QG5C1C", "stationary targets QG5c1 (I5, I6): the manifest takes the totems of the spawn points as kill sources; the kept-out causes", target_manifest}};
}

} // namespace soulwarden::backend::tests
