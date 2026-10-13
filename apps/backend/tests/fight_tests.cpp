// Tests of stage 3.4b: a creature fights (protocol notes 4.17 and 4.18).
//
//   FT1   the creature file with the behavior data, a file whose parts do not fit together, and
//         a file of the form before the stage (the creatures stand)
//   FT2   sense: the vision of a creature (distance and angle), and a target by a hit
//   FT3   the way to the target: the speed, the direction, the height of the ground, the
//         position index, and the movement states of an observer
//   FT4   the attack: the action, the contact at its time, a target that went away
//   FT5   the damage rule with the worked example, the health of the character, and the bytes
//         for the owner (the vitals state and the damage mark)
//   FT6   the death of a character, the respawn request, and a dead character that leaves the world
//   FT7   the way back: the leash, a target that is gone, the health at the home
//   FT8   the spawn point rule for a creature that is not at its spawn point
//   FT9   the load: only the creatures near a player need time, with a time limit for 50 players
//   FT10  two sessions near one creature, a session far away, and a death while a second player fights
//   FT11  the switches: the behavior off, the creatures ignore a character, the heal command
//   FT12  a creature whose spawn point is not at the terrain height does not leave its place
//   FT13  a slice with a spawn action and with animation tags: the walk and the attack
//   FT14  the reader of the terrain height files, and the two functions of the navigation
//
// The accounts, the characters, the creature kinds, and the places are made up.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/height_maps.h"
#include "middleware/compression/zlib.h"
#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/routes.h"
#include "server/rep/health_messages.h"
#include "server/rep/travel_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"
#include "support/test_progression.h"
#include "world/behavior.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using world::CreatureMode;

constexpr const char* kCharacterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* kCharacterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr const char* kCharacterC = "cccccccc-0000-4000-8000-00000000000c";
constexpr const char* kSteamIdC = "76561202255233010";
constexpr const char* kTokenA = "token-of-the-start-of-A-0123456789abcdef";
constexpr const char* kTokenB = "token-of-the-start-of-B-fedcba9876543210";
constexpr float kHeight = 50.0F;
constexpr state::WorldPosition kPlace{1000.0F, 1000.0F, kHeight};
constexpr state::WorldPosition kFar{3000.0F, 1000.0F, kHeight};
constexpr std::chrono::microseconds kTick{33333};
constexpr std::uint32_t kWolfSlots[] = {5, 9, 15};
constexpr std::uint32_t kRabbitSlots[] = {2, 5, 17};
constexpr std::uint32_t kGuardSlots[] = {4, 8, 12};
/** Indexes of the actions of the test slices. */
constexpr std::uint32_t kReaction = 1;
constexpr std::uint32_t kBite = 3;
constexpr std::uint32_t kSpawn = 5;
/** The values of the test wolf: the level table of the game for the level 18. */
constexpr float kWolfHealth = 1690.875977F;
constexpr float kWolfDamage = 616.41F;
constexpr float kWolfGearScore = 180.0F;
/** The bite of the test wolf. */
constexpr float kBiteTime = 0.5F;
constexpr float kBiteReach = 2.2F;
constexpr float kBiteFactor = 0.2F;
constexpr float kBiteLength = 1.4F;
constexpr float kRunSpeed = 10.0F;
/** Field numbers of the movement state and of the vitals state. */
constexpr unsigned kFieldRotation = 11;
constexpr unsigned kFieldPosition = 27;
constexpr unsigned kFieldTeleport = 31;
constexpr unsigned kFieldScopeInfo = 33;
constexpr unsigned kFieldTags = 34;
constexpr unsigned kFieldHealth = 0;
constexpr unsigned kFieldVitalsData = 2;
constexpr unsigned kFieldHealthFlags = 3;
constexpr std::uint32_t kPlayerVitalsSlot = 41;

gamedata::CreatureSlice test_slice(const char* path, std::uint8_t mark, const std::uint32_t slots[3]) {
    gamedata::CreatureSlice slice;
    slice.path = path;
    slice.asset.fill(mark);
    slice.subId = 2;
    slice.root = 0x1000000000000000ULL + mark;
    slice.interestRadius = 60.0F;
    slice.movementSlot = slots[0];
    slice.vitalsSlot = slots[1];
    slice.markerSlot = slots[2];
    slice.slots = {{slots[0], "ActionList", 11}, {slots[1], "Vitals", 15}, {slots[2], "Marker", 5399}};
    slice.components = {{slice.root, 0x0000000000000A01ULL, "Root/Vitals"}};
    slice.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "r_R2_F", 5, true}, {"Death", "r_Death", 4, true},
                     {"Attack_Bite", "Attack_Bite", 7, false}, {"Attack_Lunge", "Attack_Lunge", 8, false},
                     {"Spawn", "Spawn", 9, false}};
    return slice;
}

gamedata::CreatureBehavior test_behavior() {
    gamedata::CreatureBehavior values;
    values.sightAngle = 75.0F;
    values.sightDistance = 12.0F;
    values.combatAngle = 180.0F;
    values.combatDistance = 30.0F;
    values.restLeash = 20.0F;
    values.combatLeash = 70.0F;
    values.walkSpeed = 4.0F;
    values.runSpeed = kRunSpeed;
    return values;
}

gamedata::CreatureAttack bite() {
    gamedata::CreatureContact contact;
    contact.time = kBiteTime;
    contact.reach = kBiteReach;
    contact.factor = kBiteFactor;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Test_Bite";
    return gamedata::CreatureAttack{kBite, kBiteLength, {contact}};
}

gamedata::CreatureKind test_kind(const char* id, std::uint32_t vitalsId, float factor, std::uint32_t level, std::uint32_t slice) {
    gamedata::CreatureKind kind;
    kind.id = id;
    kind.vitals = id;
    kind.category = std::string(id) + "_Category";
    kind.vitalsId = vitalsId;
    kind.categoryId = vitalsId + 1;
    kind.name = id;
    kind.vitalsName = id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = factor;
    kind.level = level;
    kind.bodies = {{slice, 100}};
    return kind;
}

/** The slices of the cases: a wolf that fights, a rabbit that does not, and a guard with a spawn action and with tags. */
std::vector<gamedata::CreatureSlice> test_slices() {
    gamedata::CreatureSlice wolf = test_slice("slices/test/wolf.dynamicslice", 0x11, kWolfSlots);
    wolf.behavior = test_behavior();
    wolf.attacks = {bite()};
    gamedata::CreatureSlice rabbit = test_slice("slices/test/rabbit.dynamicslice", 0x22, kRabbitSlots);
    rabbit.behavior = test_behavior();
    rabbit.behavior->runSpeed = 0.0F;
    gamedata::CreatureSlice guard = test_slice("slices/test/guard.dynamicslice", 0x33, kGuardSlots);
    guard.behavior = test_behavior();
    guard.attacks = {bite()};
    guard.spawnActionName = "Spawn";
    guard.tags = {0x24, 0x04};
    return {wolf, rabbit, guard};
}

std::vector<gamedata::CreatureLevel> test_levels() {
    return {{1, 337.5F, 145.52F, 50.0F}, {18, kWolfHealth, kWolfDamage, kWolfGearScore}, {40, 5000.0F, 2000.0F, 400.0F}};
}

bool set_fight_data(ServerHost& host, std::vector<gamedata::SpawnPoint> points = {}) {
    return host.creatures.set_full(test_slices(),
                                   {test_kind("Test_Wolf", 0x11112222, 1.0F, 18, 0), test_kind("Test_Rabbit", 0x55556666, 0.05F, 1, 1),
                                    test_kind("Test_Guard", 0x77778888, 1.0F, 18, 2)},
                                   std::move(points), test_levels());
}

/** The terrain of most cases: one height at each place. */
void flat_terrain(ServerHost& host, float height = kHeight) {
    host.terrain.source = [height](float, float) { return std::optional<float>(height); };
}

/** The combat tables of the cases: one weapon (the loader needs one), two armor items, and two level differences. */
gamedata::CombatData test_combat() {
    gamedata::CombatData data;
    data.constants = {0.025F, 0.0112F, 0.6667F, 5, 100, 500};
    data.damageTypes = {{"True", 1, gamedata::DamageCategory::direct}, {"Slash", 4, gamedata::DamageCategory::physical}};
    data.attributeMinimum = 5;
    for (const unsigned attribute : {world::attribute::kStrength, world::attribute::kDexterity, world::attribute::kIntelligence,
                                     world::attribute::kFocus}) {
        data.attributeDamage[attribute] = {0.0F, 1.25F};
    }
    data.damageRows = {{"Test_Sword_Light", "Light", 1, 1.0F, 0.0F}};
    gamedata::WeaponRow sword;
    sword.id = "TestSwordRow";
    sword.melee = true;
    sword.baseDamage = 64.0F;
    sword.lightRow = 0;
    data.weapons = {sword};
    data.items = {{"testsword", 0}};
    data.armor = {{"testchest", {0.25F, 0.25F, 0.3F}}, {"testlegs", {0.25F, 0.25F, 0.2F}}};
    data.levelDamage = {{6, 0.2F}, {22, 1.5F}};
    return data;
}

/** Puts an armor item into a slot of the paperdoll of an entity (the inventory system is not a part of these cases). */
void wear(world::Entity& entity, std::uint32_t slot, const char* item, std::uint32_t gearScore) {
    world::ItemInstance instance;
    instance.itemId = item;
    instance.quantity = 1;
    instance.gearScore = gearScore;
    entity.equipment.slots[slot].item = instance;
}

/** `count` ticks of the game server, 10 ms each, with the exchange of each client. */
void run(ServerHost& host, int count, std::initializer_list<FakeClient*> clients = {}) {
    host.steps(count, clients);
}

/** Ticks of 10 ms until the behavior did its next step (one step for each 200 ms of the world clock). */
void to_step(ServerHost& host, std::initializer_list<FakeClient*> clients = {}) {
    const std::uint64_t steps = host.world.creatures().behavior().counts().steps;
    for (int index = 0; index < 40 && host.world.creatures().behavior().counts().steps == steps; ++index) {
        host.steps(1, clients);
    }
}

/** The time `seconds` in ticks of 10 ms. */
void pass(ServerHost& host, float seconds, std::initializer_list<FakeClient*> clients = {}) {
    run(host, static_cast<int>(std::lround(seconds * 100.0F)), clients);
}

void enter_at(ServerHost& host, FakeClient& client, const Player& player, const state::WorldPosition& position, std::uint16_t& input) {
    host.enter(client, player.registration());
    for (int index = 0; index < 20; ++index) {
        host.clock.advance(kTick);
        host.datagram(client.peer, client.report_at(++input, position.x, position.y, position.z));
        host.tick();
        client.exchange(host);
    }
}

/** One position report of a client, in the time of one input step. */
void report(ServerHost& host, FakeClient& client, std::uint16_t input, const state::WorldPosition& position) {
    host.clock.advance(kTick);
    host.datagram(client.peer, client.report_at(input, position.x, position.y, position.z));
    host.tick();
    client.exchange(host);
}

std::uint32_t interest_of(ServerHost& host, std::uint32_t session, world::EntityId id) {
    const auto& table = host.session(session).observer().known();
    const auto found = table.find(id);
    return found == table.end() ? 0 : found->second.interest;
}

/** @return The fragments of one slot that a client got for an interest number, from the message `from` on. */
std::vector<Fragment> fragments_of(const FakeClient& client, std::size_t from, std::uint32_t interest, std::uint32_t slot) {
    std::vector<Fragment> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            if (record.interest != interest) {
                continue;
            }
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot == slot) {
                    found.push_back(fragment);
                }
            }
        }
    }
    return found;
}

/** @return The action index of the scope record of a creature movement state (the wire has the index plus 1), or -1. */
int scope_action(const Fragment& movement) {
    if (movement.fields.count({0, kFieldScopeInfo}) == 0) {
        return -1;
    }
    server::hub::Reader reader(movement.fields.at({0, kFieldScopeInfo}));
    reader.size();
    reader.u16();
    reader.size();
    return static_cast<int>(reader.size()) - 1;
}

/** @return The horizontal position of a movement state. */
std::pair<float, float> place_of(const Fragment& movement) {
    server::hub::Reader reader(movement.fields.at({0, kFieldPosition}));
    const float x = reader.f32();
    const float y = reader.f32();
    return {x, y};
}

/** @return The height of a movement state: 16 bits of the range -100 m to 1000 m. */
float height_of(const Fragment& movement) {
    const Bytes& value = movement.fields.at({0, kFieldPosition});
    const unsigned raw = (static_cast<unsigned>(value[8]) << 8) | value[9];
    return static_cast<float>(raw) / 65535.0F * 1100.0F - 100.0F;
}

float health_of(const Fragment& vitals) {
    server::hub::Reader reader(vitals.fields.at({0, kFieldHealth}));
    return reader.f32();
}

bool near(float value, float expected, float tolerance = 0.01F) {
    return std::fabs(value - expected) <= tolerance;
}

/** A host with the fight data, a flat terrain, and one character with no session at the place of the cases. */
struct FightHost {
    ServerHost host;
    Player alpha;

    explicit FightHost(const service::RepServerOptions& options = {})
        : host(options), alpha(add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace)) {
        if (!set_fight_data(host)) {
            throw std::runtime_error("fight test: the creature data is not valid");
        }
        flat_terrain(host);
    }

    world::Creatures& creatures() {
        return host.world.creatures();
    }
    world::Entity* wolf(float x, float y, float yaw, float z = kHeight) {
        return creatures().spawn("Test_Wolf", state::WorldPosition{x, y, z}, yaw).entity;
    }
};

service::RepServerOptions observing() {
    service::RepServerOptions options;
    options.serverVersion = "test-version";
    options.observePlayers = true;
    return options;
}

/** FT1: the creature file with the behavior data. */
void behavior_data_file(const Options& options) {
    const auto slice = [](const char* path, const char* asset, const char* root, int movement, int vitals, int marker) {
        return json{{"path", path}, {"asset", asset}, {"sub", 2}, {"root", root}, {"interestRadius", 60.0},
                    {"movement", movement}, {"vitals", vitals}, {"marker", marker},
                    {"slots", {{movement, "ActionList", 11}, {vitals, "Vitals", 15}, {marker, "Marker", 5399}}},
                    {"components", json::array({json::array({root, "0000000000000A01", "Root/Vitals"})})},
                    {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true},
                                 {"Attack_Bite", "Attack_Bite", 7, false}, {"Sound", "", -1, false}}}};
    };
    const auto kind = [](const char* id, std::uint32_t vitals, double health, int level, int body) {
        return json{{"id", id}, {"vitals", id}, {"category", std::string(id) + "_Category"}, {"vitalsId", vitals},
                    {"categoryId", vitals + 1}, {"name", id}, {"vitalsName", id}, {"family", "Wildlife"},
                    {"creatureType", "Solo"}, {"health", health}, {"level", level}, {"bodies", json::array({json::array({body, 100})})},
                    {"points", 0}};
    };
    json document{{"format", 2},
                  {"levels", {{1, 337.5, 145.52, 50.0}, {18, 1690.875977, 616.41, 180.0}}},
                  {"slices",
                   {slice("slices/test/wolf.dynamicslice", "11111111-1111-1111-1111-111111111111", "1000000000000011", 5, 9, 15),
                    slice("slices/test/rabbit.dynamicslice", "22222222-2222-2222-2222-222222222222", "1000000000000022", 2, 5, 17)}},
                  {"kinds", {kind("Test_Wolf", 0x11112222, 1.0, 18, 0), kind("Test_Rabbit", 0x55556666, 0.05, 1, 1)}},
                  {"spawnPoints",
                   {{"p-wolf-on-terrain", 0, 18, 1010.0, 1000.0, 50.2, 90.0, 360.0, 360.0, 50.0},
                    {"p-wolf-on-a-floor", 0, 18, 1100.0, 1000.0, 53.5, 0.0, 360.0, 360.0, 50.0},
                    {"p-wolf-no-height-map", 0, 18, 1200.0, 1000.0, 50.0, 0.0, 360.0, 360.0, nullptr},
                    {"p-rabbit", 1, 1, 1300.0, 1000.0, 50.0, 0.0, 360.0, 360.0, 50.0}}}};
    document["slices"][0]["behavior"] = {{"sight", {75.0, 12.0}}, {"combatSight", {180.0, 30.0}}, {"leash", {20.0, 70.0}},
                                         {"speed", {4.75, 9.23}}};
    document["slices"][0]["tags"] = "240400000000000000000000";
    document["slices"][0]["attacks"] = json::array(
        {json::array({2, 1.43333, json::array({json::array({0.566667, 2.18, 0.2, 0.0, "Slash", 4, "Physical", "Attack_01_Bite"})})})});
    document["slices"][1]["behavior"] = {{"sight", {65.0, 15.0}}, {"combatSight", {0.0, 0.0}}, {"leash", {75.0, 70.0}},
                                         {"speed", {0.0, 0.0}}};
    document["kinds"][0]["damage"] = 0.85;
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "fight-creatures.json";
    const auto load = [&path](const json& content, gamedata::CreatureData& data) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << content.dump();
        return data.load(path.string());
    };
    gamedata::CreatureData data;
    check(load(document, data) && data.loaded() && data.slices().size() == 2 && data.spawn_points().size() == 4,
          "a creature file of the format 2 loads");
    const gamedata::CreatureSlice& wolf = data.slices()[0];
    check(wolf.behavior && wolf.behavior->sightAngle == 75.0F && wolf.behavior->sightDistance == 12.0F
              && wolf.behavior->combatDistance == 30.0F && wolf.behavior->restLeash == 20.0F && wolf.behavior->combatLeash == 70.0F
              && wolf.behavior->walkSpeed == 4.75F && near(wolf.behavior->runSpeed, 9.23F),
          "the slice has its behavior values: the two vision profiles, the two leash distances, and the two speeds");
    check(wolf.attacks.size() == 1 && wolf.attacks[0].action == 2 && near(wolf.attacks[0].length, 1.43333F)
              && wolf.attacks[0].contacts.size() == 1 && near(wolf.attacks[0].contacts[0].time, 0.566667F)
              && near(wolf.attacks[0].contacts[0].reach, 2.18F) && near(wolf.attacks[0].contacts[0].factor, 0.2F)
              && wolf.attacks[0].contacts[0].type == "Slash" && wolf.attacks[0].contacts[0].wire == 4
              && wolf.attacks[0].contacts[0].category == gamedata::DamageCategory::physical
              && wolf.attacks[0].contacts[0].row == "Attack_01_Bite" && wolf.actions[wolf.attacks[0].action].fragmentIndex == 7,
          "the slice has its attack: the action with its fragment index, the time, and one contact with its time, reach, "
          "damage factor, damage type, and damage row");
    check(wolf.tags[0] == 0x24 && wolf.tags[1] == 0x04 && wolf.tags[11] == 0 && wolf.fights() && !data.slices()[1].fights()
              && data.slices()[1].attacks.empty(),
          "the slice has the tag state of its animation fragments; a slice with an attack and a speed fights, and a slice "
          "with no attack does not");
    check(near(data.damage(data.kinds()[0], 18), 616.41F * 0.85F) && data.gear_score(18) == 180.0F && data.damage(data.kinds()[0], 7) == 0.0F
              && data.kinds()[1].damageFactor == 1.0F,
          "the damage of a kind at a level is the base damage of the level times the factor of the kind, and a level has "
          "its gear score");
    check(data.spawn_points()[0].ground && *data.spawn_points()[0].ground == 50.0F && !data.spawn_points()[2].ground
              && gamedata::CreatureData::on_terrain(data.spawn_points()[0]) && !gamedata::CreatureData::on_terrain(data.spawn_points()[1])
              && !gamedata::CreatureData::on_terrain(data.spawn_points()[2]),
          "a spawn point has the terrain height of its place: a point 0.2 m from it is on the terrain, and a point 3.5 m "
          "above it and a point with no height are not");
    check(data.fighting_slices() == 1 && data.fighting_kinds() == 1 && data.fighting_points() == 3 && data.rooted_points() == 2
              && data.largest_sight() == 12.0F,
          "the counts for the log of the backend: 1 slice and 1 kind fight, 3 spawn points have such a kind, and the "
          "creatures of 2 of them do not leave their place");

    // A file whose parts do not fit together is not used, as a whole.
    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json wrong = document;
        change(wrong);
        gamedata::CreatureData other;
        const bool loaded = load(wrong, other);
        if (loaded || other.loaded()) {
            note(std::string("accepted: ") + what);
        }
        return !loaded && !other.loaded() && !other.error().empty();
    };
    bool all = true;
    all = refused("an attack whose action the action table does not have", [](json& d) { d["slices"][0]["attacks"][0][0] = 9; }) && all;
    all = refused("an attack whose action has no fragment", [](json& d) { d["slices"][0]["attacks"][0][0] = 3; }) && all;
    all = refused("an attack with no contact", [](json& d) { d["slices"][0]["attacks"][0][2] = json::array(); }) && all;
    all = refused("a contact after the end of its attack", [](json& d) { d["slices"][0]["attacks"][0][2][0][0] = 2.5; }) && all;
    all = refused("a contact with the reach 0", [](json& d) { d["slices"][0]["attacks"][0][2][0][1] = 0.0; }) && all;
    all = refused("a damage factor below 0", [](json& d) { d["slices"][0]["attacks"][0][2][0][2] = -0.5; }) && all;
    all = refused("a damage type number 0", [](json& d) { d["slices"][0]["attacks"][0][2][0][5] = 0; }) && all;
    all = refused("a damage category that is not known", [](json& d) { d["slices"][0]["attacks"][0][2][0][6] = "Physicl"; }) && all;
    all = refused("an attack time of 0", [](json& d) { d["slices"][0]["attacks"][0][1] = 0.0; }) && all;
    all = refused("a speed below 0", [](json& d) { d["slices"][0]["behavior"]["speed"][1] = -1.0; }) && all;
    all = refused("a vision distance of 5 km", [](json& d) { d["slices"][0]["behavior"]["sight"][1] = 5000.0; }) && all;
    all = refused("behavior values with no leash", [](json& d) { d["slices"][0]["behavior"].erase("leash"); }) && all;
    all = refused("tags with 10 digits", [](json& d) { d["slices"][0]["tags"] = "2404000000"; }) && all;
    all = refused("a level row with no gear score", [](json& d) { d["levels"][0] = {1, 337.5, 145.52}; }) && all;
    all = refused("a level with a damage below 0", [](json& d) { d["levels"][0][2] = -1.0; }) && all;
    all = refused("a spawn point with no terrain height value", [](json& d) { d["spawnPoints"][0].erase(9); }) && all;
    all = refused("a terrain height that is a text", [](json& d) { d["spawnPoints"][0][9] = "50"; }) && all;
    all = refused("a damage factor of a kind below 0", [](json& d) { d["kinds"][0]["damage"] = -1.0; }) && all;
    all = refused("the format 3", [](json& d) { d["format"] = 3; }) && all;
    check(all, "a file whose parts do not fit together gives no data, as a whole: 19 forms of a wrong attack, behavior value, "
               "tag state, level row, spawn point, and kind (a note above names each form that was accepted)");

    // A file of the form before the stage: the creatures are there, and they stand.
    json old = document;
    old["format"] = 1;
    old["levels"] = {{1, 337.5}, {18, 1690.875977}};
    for (json& point : old["spawnPoints"]) {
        point.erase(9);
    }
    for (json& entry : old["slices"]) {
        entry.erase("behavior");
        entry.erase("attacks");
        entry.erase("tags");
    }
    old["kinds"][0].erase("damage");
    ServerHost host;
    flat_terrain(host);
    add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    std::ofstream(path, std::ios::binary | std::ios::trunc) << old.dump();
    check(host.creatures.load(path.string()) && host.creatures.fighting_slices() == 0 && host.creatures.fighting_points() == 0
              && !host.creatures.spawn_points()[0].ground,
          "a file of the format 1 (a tool from before stage 3.4b) loads, with no behavior data");
    check(host.world.spawn_character(kCharacterA, kPlace) == world::SpawnError::none, "a character is 10 m from the first spawn point");
    pass(host, 3.0F);
    const world::Entity* stands = host.world.creatures().find(host.world.creatures().spawn_points()[0].entity);
    check(stands != nullptr && stands->creature.mind.mode == CreatureMode::rest && stands->position.x == 1010.0F
              && host.world.creatures().behavior().active_count() == 0 && host.world.creatures().behavior().counts().targetsBySight == 0,
          "with the old file the creature of the spawn point stands: it takes no target, and it does not move");
    std::filesystem::remove(path);
}

/** FT2: the sense of a creature. */
void sense_of_a_creature(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    world::Behavior& behavior = f.creatures().behavior();
    check(host.world.spawn_character(kCharacterA, kPlace) == world::SpawnError::none, "a character is in the world");
    const world::EntityId player = host.world.find_character(kCharacterA)->id;
    // The direction 90 degrees is west: these creatures look at the character, or away from it.
    const world::Entity* sees = f.wolf(1011.0F, 1000.0F, 90.0F);
    const world::Entity* far = f.wolf(1000.0F, 1013.0F, 180.0F);
    const world::Entity* behind = f.wolf(992.0F, 1000.0F, 90.0F);
    const world::Entity* edge = f.wolf(1000.0F - 8.0F * std::sin(74.0F * 3.14159265F / 180.0F),
                                       1000.0F - 8.0F * std::cos(74.0F * 3.14159265F / 180.0F), 0.0F);
    const world::Entity* outside = f.wolf(1000.0F + 8.0F * std::sin(77.0F * 3.14159265F / 180.0F),
                                          1000.0F - 8.0F * std::cos(77.0F * 3.14159265F / 180.0F), 0.0F);
    const world::Entity* rabbit = f.creatures().spawn("Test_Rabbit", state::WorldPosition{1003.0F, 1003.0F, kHeight}, 225.0F).entity;
    check(sees && far && behind && edge && outside && rabbit && sees->creature.mind.mode == CreatureMode::rest,
          "six creatures at rest: 11 m in front, 13 m in front, 8 m with the character behind, 8 m with the character 74 "
          "and 77 degrees from the direction, and a rabbit");
    run(host, 1);
    check(sees->creature.mind.mode == CreatureMode::fight && sees->creature.mind.target == player,
          "a creature gets the character as its target when the character is in its vision distance (12 m) and in its "
          "angle (75 degrees to each side)");
    check(far->creature.mind.mode == CreatureMode::rest && far->creature.mind.target == 0,
          "a character 13 m away is not in the vision distance: no target");
    check(behind->creature.mind.mode == CreatureMode::rest, "a character behind the creature is not in the angle: no target");
    check(edge->creature.mind.mode == CreatureMode::fight && outside->creature.mind.mode == CreatureMode::rest,
          "the angle of the slice is the angle to each side of the direction: 74 degrees is in it, and 77 degrees is not");
    check(rabbit->creature.mind.mode == CreatureMode::rest && !rabbit->creature.body->fights(),
          "a creature of a kind with no attack takes no target");
    check(behavior.counts().targetsBySight == 2 && behavior.active_count() == 2 && log_count("has a target: Alpha (in its vision") == 2,
          "the log has one line for each new target, with the cause");
    // A hit: the character is the target for each direction and distance.
    const world::EntityId behindId = behind->id;
    check(f.creatures().hurt(behindId, 10.0F, player).error == world::CreatureError::none
              && behind->creature.mind.mode == CreatureMode::fight && behind->creature.mind.target == player
              && behavior.counts().targetsByHit == 1 && log_count("has a target: Alpha (it did hurt the creature") == 1,
          "a creature that a character hurts gets that character as its target, also from behind");
    check(f.creatures().hurt(rabbit->id, 1.0F, player).error == world::CreatureError::none
              && rabbit->creature.mind.mode == CreatureMode::rest && behavior.counts().targetsByHit == 1,
          "a creature of a kind with no attack that is hurt takes no target: it stands");
    check(f.creatures().hurt(far->id, 10.0F).error == world::CreatureError::none && far->creature.mind.mode == CreatureMode::rest,
          "damage of a command (no attacker) gives no target");
    // One target: a second character that hurts the creature does not change it.
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kPlace);
    check(host.world.spawn_character(kCharacterB, state::WorldPosition{1020.0F, 1000.0F, kHeight}) == world::SpawnError::none,
          "a second character is in the world");
    const world::EntityId second = host.world.find_character(kCharacterB)->id;
    check(f.creatures().hurt(sees->id, 10.0F, second).error == world::CreatureError::none && sees->creature.mind.target == player,
          "a creature has one target: a hit of a second character does not change it");
}

/** FT3: the way of a creature to its target. */
void way_to_the_target(const Options&) {
    FightHost f(observing());
    ServerHost& host = f.host;
    // A slope: the ground goes up 0.1 m for each meter to the east.
    host.terrain.source = [](float x, float) { return std::optional<float>(kHeight + (x - 1000.0F) * 0.1F); };
    const state::WorldPosition place{1019.0F, 1000.0F, kHeight + 1.9F};
    FakeClient a(27000);
    std::uint16_t input = 0;
    (void)host.store.set_character_position(kCharacterA, place);
    enter_at(host, a, f.alpha, place, input);
    const world::Entity* player = host.world.find_character(kCharacterA);
    // The creature is 11 m to the east, in a different cell of the position index (the cells have 64 m: a limit is at x 1024).
    const world::Entity* wolf = f.wolf(1030.0F, 1000.0F, 0.0F, kHeight + 3.0F);
    check(player != nullptr && wolf != nullptr && wolf->creature.mind.walks && wolf->creature.mind.home.x == 1030.0F,
          "a creature whose place is at the terrain height can walk, and its home is the place where it was made");
    const world::EntityId wolfId = wolf->id;
    // The character is not in the angle of the creature. The session observes the creature first.
    pass(host, 0.6F, {&a});
    check(wolf->creature.mind.mode == CreatureMode::rest && interest_of(host, 1, wolfId) >= 2,
          "the session observes the creature, which is at rest");
    to_step(host, {&a});
    f.creatures().hurt(wolfId, 1.0F, player->id);
    std::size_t from = a.got.size();
    // The steps of the behavior: one step for each 200 ms of the world clock.
    std::vector<float> xs;
    std::vector<float> zs;
    for (int step = 0; step < 6; ++step) {
        pass(host, 0.2F, {&a});
        xs.push_back(wolf->position.x);
        zs.push_back(wolf->position.z);
    }
    note("the place of the creature after each 200 ms: x " + std::to_string(xs[0]) + " " + std::to_string(xs[1]) + " "
         + std::to_string(xs[2]) + " " + std::to_string(xs[3]) + " " + std::to_string(xs[4]) + " " + std::to_string(xs[5]));
    check(near(xs[0], 1028.0F) && near(xs[1], 1026.0F) && near(xs[2], 1024.0F) && near(xs[3], 1022.0F),
          "the creature goes to its target with the run speed of its slice: 2 m in each step of 200 ms at 10 m/s");
    const float stop = 1019.0F + kBiteReach - world::behavior_rules::kReachMargin;
    check(near(xs[4], stop) && near(xs[5], stop) && wolf->position.y == 1000.0F,
          "it stops where the target is in the reach of its attack: the reach minus 0.6 m");
    check(near(zs[0], kHeight + 2.8F) && near(zs[2], kHeight + 2.4F) && near(zs[4], kHeight + (stop - 1000.0F) * 0.1F),
          "the height of each step is the height of the ground at that place (a slope of the test terrain)");
    check(near(wolf->creature.yaw, 90.0F, 0.5F), "the creature looks at its target: west is the direction 90 degrees");
    bool found = false;
    host.world.for_each_near(state::WorldPosition{stop + 0.2F, 1000.0F, kHeight}, 0.5F,
                             [&](world::Entity& entity) { found = found || entity.id == wolfId; });
    bool old = false;
    host.world.for_each_near(state::WorldPosition{1030.0F, 1000.0F, kHeight}, 3.0F,
                             [&](world::Entity& entity) { old = old || entity.id == wolfId; });
    check(found && !old, "the position index of the world follows the creature into a different cell");
    // What the session of an observer got: one movement state for each step, with no action.
    const std::uint32_t number = interest_of(host, 1, wolfId);
    const std::vector<Fragment> states = fragments_of(a, from, number, kWolfSlots[0]);
    std::size_t moves = 0;
    std::size_t reactions = 0;
    std::size_t others = 0;
    for (const Fragment& state : states) {
        const int action = scope_action(state);
        moves += action == -1 ? 1 : 0;
        reactions += action == static_cast<int>(kReaction) ? 1 : 0;
        others += action != -1 && action != static_cast<int>(kReaction) && action != static_cast<int>(kBite) ? 1 : 0;
    }
    note("the movement states of the creature for the session: " + std::to_string(states.size()) + ", " + std::to_string(moves)
         + " of them with no action, " + std::to_string(reactions) + " with the reaction to the hit");
    check(number >= 2 && moves >= 4 && reactions == 1 && others == 0,
          "the session that observes the creature gets a movement state for each step, with no action of the server: the "
          "client plays the walk itself. The reaction to the hit goes out one time, in the state after its start");
    const auto walk = std::find_if(states.begin(), states.end(), [](const Fragment& state) {
        return scope_action(state) == -1 && near(place_of(state).first, 1024.0F);
    });
    check(walk != states.end() && near(place_of(*walk).second, 1000.0F) && near(height_of(*walk), kHeight + 2.4F, 0.02F)
              && walk->fields.count({0, kFieldRotation}) == 1 && walk->fields.count({0, kFieldTags}) == 0,
          "a state of the walk has the new position with the height of the ground and the direction, and no scope record");
    check(f.creatures().behavior().counts().moves == 5, "the count of the position changes: 5 for the way of 9.4 m");
}

/** FT4: the attack of a creature and its contact. */
void attack_and_contact(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, f.alpha, kPlace, input);
    world::Entity* player = host.world.find_character(kCharacterA);
    // The creature is in its reach at once: 1.5 m to the east, with its face to the character.
    const world::Entity* wolf = f.wolf(1001.5F, 1000.0F, 90.0F);
    const world::EntityId wolfId = wolf->id;
    std::size_t from = a.got.size();
    to_step(host, {&a});
    check(wolf->creature.mind.mode == CreatureMode::fight && wolf->creature.mind.attack && *wolf->creature.mind.attack == 0
              && wolf->creature.action && wolf->creature.action->action == kBite && wolf->creature.action->ends
              && log_count("starts the attack Attack_Bite at Alpha") == 1,
          "a creature whose target is in its reach starts an attack of its slice: an action with an end");
    const float before = player->vitals.health;
    // The contact is at 0.5 s of the action, on the world clock.
    pass(host, 0.49F, {&a});
    const std::uint32_t number = interest_of(host, 1, wolfId);
    const std::vector<Fragment> first = fragments_of(a, from, number, kWolfSlots[0]);
    check(number >= 2 && !first.empty() && scope_action(first.back()) == static_cast<int>(kBite)
              && first.back().fields.count({0, kFieldTags}) == 1,
          "the session that observes the creature gets the attack as the scope record of its movement state");
    check(player->vitals.health == before && f.creatures().behavior().counts().contactsHit == 0,
          "0.49 s after the start of the action the character has its health: the contact is not due");
    run(host, 1, {&a});
    const float bite = kWolfDamage * kBiteFactor;
    check(near(player->vitals.health, before - bite) && f.creatures().behavior().counts().contactsHit == 1
              && log_count("hit: creature") == 1,
          "at 0.5 s the contact is due: the target is in the reach and in front, and it takes the damage one time");
    pass(host, 0.6F, {&a});
    check(near(player->vitals.health, before - bite) && wolf->creature.action && wolf->creature.action->action == kBite,
          "one contact is one damage result: the health does not change again while the action plays");
    // After the action and the pause the creature attacks again. The character goes away in time.
    pass(host, 0.5F, {&a});
    check(!wolf->creature.action && !wolf->creature.mind.attack, "after its time the action is over");
    pass(host, 1.0F, {&a});
    check(wolf->creature.mind.attack && wolf->creature.mind.attacks == 2 && f.creatures().behavior().counts().attacks == 2,
          "after the pause the creature starts its next attack");
    const world::CreatureAction second = *wolf->creature.action;
    const float health = player->vitals.health;
    // A walk of 3 m to the west in 0.3 s (the position reports of the client).
    for (int index = 1; index <= 9; ++index) {
        report(host, a, ++input, state::WorldPosition{kPlace.x - 0.33F * static_cast<float>(index), kPlace.y, kPlace.z});
    }
    pass(host, 0.4F, {&a});
    check(player->position.x < 997.5F && player->vitals.health == health && f.creatures().behavior().counts().contactsMissed == 1
              && log_count("miss: the attack Attack_Bite") == 1,
          "a character that went away before the contact gets no damage");
    check(wolf->creature.action && wolf->creature.action->generation == second.generation && wolf->position.x == 1001.5F,
          "the action still plays to its end, and the creature stays at its place while it plays");
    // A dead creature does no damage: the contact of its attack does nothing.
    pass(host, 2.5F, {&a});
    const std::uint64_t hits = f.creatures().behavior().counts().contactsHit;
    check(wolf->creature.mind.attack.has_value(), "the creature went to the character and plays an attack again");
    f.creatures().kill(wolfId);
    const float last = player->vitals.health;
    pass(host, 1.0F, {&a});
    check(player->vitals.health == last && f.creatures().behavior().counts().contactsHit == hits
              && wolf->creature.mind.mode == CreatureMode::rest && f.creatures().behavior().active_count() == 0,
          "a creature that dies while its attack plays does no damage, and it has no target");
}

/** FT5: the damage of an attack of a creature on a character. */
void damage_to_a_character(const Options&) {
    FightHost f(observing());
    ServerHost& host = f.host;
    check(host.combat.set(test_combat()), "the combat tables of the case are valid");
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1004.0F, 1004.0F, kHeight});
    FakeClient a(27000);
    FakeClient b(27001);
    std::uint16_t inputA = 0;
    std::uint16_t inputB = 0;
    enter_at(host, a, f.alpha, kPlace, inputA);
    enter_at(host, b, beta, state::WorldPosition{1004.0F, 1004.0F, kHeight}, inputB);
    run(host, 60, {&a, &b});
    world::Entity* player = host.world.find_character(kCharacterA);
    world::Combat& combat = host.world.combat();
    // The rule, with the values of the game for a Grey Wolf of the level 18 and a character
    // with the chest armor and the leg armor of a new character (gear score 60).
    wear(*player, world::paperdoll::kChest, "TestChest", 60);
    wear(*player, world::paperdoll::kLegs, "TestLegs", 60);
    const world::Entity* wolf = f.wolf(1001.5F, 1000.0F, 90.0F);
    const gamedata::CreatureContact contact = wolf->creature.body->attacks[0].contacts[0];
    const world::CreatureDamage damage = combat.creature_damage(*wolf, contact, *player);
    note("the worked example: damage of the creature " + std::to_string(damage.base) + ", of the attack " + std::to_string(damage.raw)
         + ", armor " + std::to_string(damage.armor) + ", mitigation " + std::to_string(damage.mitigation) + ", dealt "
         + std::to_string(damage.dealt));
    check(near(damage.base, 616.41F) && near(damage.raw, 123.282F) && damage.levelFactor == 1.0F,
          "the damage of the attack is the base damage of the level of the creature (616.41) times the factor of its kind "
          "(1) times the factor of the contact (0.2): 123.28");
    check(near(damage.armor, 17.0F) && near(combat.armor_of(*player, gamedata::DamageCategory::physical), 17.0F)
              && combat.armor_of(*player, gamedata::DamageCategory::direct) == 0.0F,
          "the armor of the character: 0.3 x 34.0 for the chest and 0.2 x 34.0 for the legs (60 ^ 1.2 x 0.25 = 34.0), 17.0");
    const float against = std::pow(180.0F, 1.2F);
    check(near(damage.mitigation, 17.0F / (17.0F + against), 0.0001F) && near(damage.dealt, 123.282F * (1.0F - 17.0F / (17.0F + against)), 0.01F)
              && near(damage.dealt, 119.29F, 0.02F),
          "the armor stops the part armor / (armor + gear score of the creature ^ 1.2) = 17 / (17 + 508.5): the character "
          "takes 119.29");
    // The level difference, a damage with no armor, and a contact that adds damage.
    player->progression.level = 12;
    check(combat.creature_damage(*wolf, contact, *player).levelFactor == 1.2F,
          "a creature 6 levels above the character does 1.2 times the damage (the table of the game)");
    player->progression.level = 13;
    check(combat.creature_damage(*wolf, contact, *player).levelFactor == 1.0F, "a level difference with no row changes no damage");
    gamedata::CreatureContact direct = contact;
    direct.category = gamedata::DamageCategory::direct;
    direct.added = 10.0F;
    const world::CreatureDamage pure = combat.creature_damage(*wolf, direct, *player);
    check(pure.armor == 0.0F && pure.mitigation == 0.0F && near(pure.dealt, 133.282F),
          "a damage of the category True takes no armor, and a contact adds its own damage");
    player->progression.level = 26;

    // One hit through the world: the health, and what each session gets.
    const world::EntityId wolfId = wolf->id;
    const world::EntityId playerId = player->id;
    const float before = player->vitals.health;
    const std::size_t fromA = a.got.size();
    const std::size_t fromB = b.got.size();
    pass(host, 0.7F, {&a, &b});
    check(near(player->vitals.health, before - 119.29F, 0.02F) && log_count("hurts Alpha with Test_Bite: 119.3 damage of the type Slash") == 1,
          "one contact of the bite lowers the health of the character by the damage of the rule, and the log has the "
          "values of each step");
    const std::vector<Fragment> own = fragments_of(a, fromA, 1, kPlayerVitalsSlot);
    check(!own.empty() && near(health_of(own.back()), before - 119.29F, 0.02F) && own.back().fields.count({0, kFieldVitalsData}) == 1
              && own.back().fields.at({0, kFieldVitalsData})[0] == 0 && own.back().fields.at({0, kFieldHealthFlags})[0] == 1,
          "the owner gets its vitals state with the new health, the life data with no dead mark, and the mark \"the health "
          "changed\": the health bar of its client goes down");
    const std::vector<Bytes> marks = messages_of(a, server::rep::kTypeDamageTaken, fromA);
    server::hub::Writer expected;
    expected.u64(server::rep::player_vitals_target(playerId));
    expected.u64(playerId);
    expected.u64(wolfId);
    expected.u8(0x02);
    expected.u8(0x00);
    expected.f32(damage.dealt / player->vitals.healthMax);
    expected.f32(1001.5F);
    expected.f32(1000.0F);
    expected.f32(kHeight);
    expected.size(1);
    expected.u8(4);
    expected.f32(damage.dealt);
    expected.f32(1.0F);
    check(marks.size() == 1 && marks[0] == expected.data(),
          "the owner gets one message \"on damage\" (type 3601) for the vitals facet of its character: the source, the flag "
          "\"the damage has a place\", the part of the health, the place of the creature, and one entry with the damage "
          "type and the damage");
    const std::uint32_t seen = interest_of(host, 2, playerId);
    const std::vector<Fragment> other = fragments_of(b, fromB, seen, kPlayerVitalsSlot);
    check(seen >= 2 && !other.empty() && near(health_of(other.back()), before - 119.29F, 0.02F)
              && other.back().fields.count({1, 0}) == 0 && messages_of(b, server::rep::kTypeDamageTaken, fromB).empty(),
          "a session that observes the character gets the vitals state of the observer view with the new health, and no "
          "damage mark");
    // A backend with no combat tables: the damage with no armor.
    ServerHost bare;
    check(set_fight_data(bare) && bare.world.spawn_character(kCharacterA, kPlace) == world::SpawnError::unknownCharacter,
          "a second host with no combat tables");
    add_player(bare.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    (void)bare.world.spawn_character(kCharacterA, kPlace);
    world::Entity* alone = bare.world.find_character(kCharacterA);
    wear(*alone, world::paperdoll::kChest, "TestChest", 60);
    const world::Entity* second = bare.world.creatures().spawn("Test_Wolf", state::WorldPosition{1001.5F, 1000.0F, kHeight}, 90.0F).entity;
    const world::CreatureDamage plain = bare.world.combat().creature_damage(*second, contact, *alone);
    check(plain.armor == 0.0F && plain.levelFactor == 1.0F && near(plain.dealt, 123.282F),
          "with no combat tables a character has no armor in the rule: it takes the damage of the attack");
}

/** FT6: the death of a character and its respawn. */
void death_and_respawn(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    check(host.places.set({{"shrine_101", "shrine", "Made-up Shrine", "East Land", 7, state::WorldPosition{1400.0F, 1300.0F, 80.0F}},
                           {"inn_202", "inn", "Inn of Nowhere", "West Land", 9, state::WorldPosition{700.0F, 9000.0F, 33.5F}}}),
          "the travel places of the case are valid");
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, f.alpha, kPlace, input);
    world::Entity* player = host.world.find_character(kCharacterA);
    const world::EntityId id = player->id;
    const world::Entity* wolf = f.wolf(1001.5F, 1000.0F, 90.0F);
    to_step(host, {&a});
    check(wolf->creature.mind.target == id, "a creature has the character as its target");
    // The health 0 is the death.
    std::size_t from = a.got.size();
    player->vitals.health = 100.0F;
    pass(host, 0.6F, {&a});
    check(player->life.dead && player->vitals.health == 0.0F && player->life.deaths == 1 && log_count("(Alpha) is dead") == 1,
          "a hit that takes the last health is the death of the character");
    const std::vector<Fragment> dead = fragments_of(a, from, 1, kPlayerVitalsSlot);
    check(!dead.empty() && health_of(dead.back()) == 0.0F && dead.back().fields.at({0, kFieldVitalsData}).size() == 18
              && dead.back().fields.at({0, kFieldVitalsData})[0] == 0x01,
          "the vitals state of the owner has the health 0 and the dead mark (bit 0 of the flags of the life data)");
    const std::vector<Bytes> notices = messages_of(a, server::rep::kTypePlayerDeath, from);
    server::hub::Writer notice;
    notice.u64(id + server::rep::kPlayerComponent);
    notice.u64(id);
    notice.f32(world::Health::kRespawnTime);
    check(notices.size() == 1 && notices[0] == notice.data() && world::Health::kRespawnTime == 5.0F,
          "the owner gets one message \"on player death\" (type 3950) for the player facet of its character, with the time "
          "5 s to the respawn screen");
    // The creature has no target, and it goes to its home.
    pass(host, 1.5F, {&a});
    check(wolf->creature.mind.mode == CreatureMode::rest && wolf->creature.mind.target == 0
              && log_count("lost its target (the target is dead)") == 1 && wolf->position.x == 1001.5F,
          "the creature loses its target when the target is dead, and it is at its home again");
    const std::uint64_t attacks = f.creatures().behavior().counts().attacks;
    pass(host, 3.0F, {&a});
    check(f.creatures().behavior().counts().attacks == attacks && wolf->creature.mind.mode == CreatureMode::rest,
          "a dead character is the target of no creature: the creature in whose vision it lies takes no target");
    check(host.world.health().hurt(kCharacterA, 10.0F).error == world::HealthError::dead && player->vitals.health == 0.0F,
          "a dead character takes no damage");

    // The respawn request of the client.
    const auto request = [&](world::EntityId entity) {
        server::hub::Writer body;
        body.u64(entity + server::rep::kPlayerComponent);
        body.u64(entity);
        body.u8(1);
        body.string("");
        host.datagram(a.peer, a.component_message(server::rep::kTypeRequestRespawn, player->actor.bytes, body.data()));
    };
    f.creatures().remove(wolf->id);
    from = a.got.size();
    request(id);
    run(host, 3, {&a});
    check(!player->life.dead && player->vitals.health == player->vitals.healthMax && player->position.x == 1400.0F
              && player->position.y == 1300.0F && player->position.z == 80.0F && player->teleport == 2
              && log_count("lives again with the health") == 1,
          "the answer to the respawn request: the character lives again with its full health at the travel place that is "
          "nearest to the place of its death");
    const std::vector<Fragment> alive = fragments_of(a, from, 1, kPlayerVitalsSlot);
    const std::vector<Fragment> moved = fragments_of(a, from, 1, 16);
    check(!alive.empty() && health_of(alive.back()) == player->vitals.healthMax && alive.back().fields.at({0, kFieldVitalsData})[0] == 0
              && !moved.empty() && moved.back().fields.count({0, kFieldTeleport}) == 1 && moved.back().fields.at({0, kFieldTeleport})[0] == 2
              && near(place_of(moved.back()).first, 1400.0F),
          "the owner gets the vitals state with no dead mark and the movement state with the new place and a new teleport "
          "number");
    check(messages_of(a, server::rep::kTypePlayerTeleport, from).size() == 1
              && messages_of(a, server::rep::kTypeTeleportWithLoadingScreen, from).empty(),
          "and the message \"on player teleport\" (type 3954) after it, with no loading screen message: the client shows "
          "its own loading screen for a respawn");
    // A request for a character that lives changes nothing, and the client gets the end of its loading screen.
    from = a.got.size();
    request(id);
    run(host, 3, {&a});
    check(player->teleport == 2 && messages_of(a, server::rep::kTypePlayerTeleport, from).size() == 1
              && fragments_of(a, from, 1, kPlayerVitalsSlot).empty() && log_count("respawn request refused (the character is not dead)") == 1,
          "a respawn request for a character that is not dead changes nothing: the client gets only the message that ends "
          "its loading screen");
    // A request that names a different entity changes nothing and gets no reply.
    from = a.got.size();
    host.world.health().hurt(kCharacterA, 100000.0F);
    run(host, 2, {&a});
    request(id + 2);
    run(host, 3, {&a});
    check(player->life.dead && messages_of(a, server::rep::kTypePlayerTeleport, from).empty(),
          "a respawn request that names the entity of a different character changes nothing");
    // A dead character that leaves the world is not dead at its next world entry.
    host.datagram(a.peer, a.carrier_close());
    run(host, 3, {&a});
    const std::optional<state::WorldPosition> row = saved_position(host.store, kCharacterA);
    check(host.world.find_character(kCharacterA) == nullptr && row && row->x == 1400.0F && row->y == 1300.0F && row->z == 80.0F
              && log_count("leaves the world dead: its row gets its respawn place (Made-up Shrine)") == 1,
          "a dead character that leaves the world: its row gets its respawn place");
    FakeClient again(27002);
    host.enter(again, f.alpha.registration());
    run(host, 3, {&again});
    const world::Entity* back = host.world.find_character(kCharacterA);
    check(back != nullptr && !back->life.dead && back->vitals.health == back->vitals.healthMax && back->position.x == 1400.0F,
          "at its next world entry the character lives, with its full health, at that place");
}

/** FT7: the way back of a creature. */
void way_back(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    check(host.world.spawn_character(kCharacterA, kPlace) == world::SpawnError::none, "a character is in the world");
    world::Entity* player = host.world.find_character(kCharacterA);
    const world::Entity* wolf = f.wolf(1010.0F, 1000.0F, 90.0F);
    const world::EntityId wolfId = wolf->id;
    run(host, 1);
    check(wolf->creature.mind.mode == CreatureMode::fight, "a creature has the character as its target");
    // The creature takes damage, and the character goes away to the west: 12 m in each second.
    f.creatures().hurt(wolfId, 500.0F, player->id);
    for (int step = 0; step < 40 && wolf->creature.mind.mode == CreatureMode::fight; ++step) {
        host.world.set_position(player->id, state::WorldPosition{player->position.x - 2.4F, 1000.0F, kHeight}, true);
        pass(host, 0.2F);
    }
    const float turned = wolf->position.x;
    note("the creature lost its target at x " + std::to_string(turned) + ", " + std::to_string(1010.0F - turned) + " m from its home");
    check(wolf->creature.mind.mode == CreatureMode::home && wolf->creature.mind.target == 0 && 1010.0F - turned > 70.0F
              && 1010.0F - turned < 73.0F && log_count("lost its target (the creature is at the end of its leash)") == 1,
          "a creature that is farther from its home than the leash distance of its slice (70 m) loses its target");
    check(near(wolf->vitals.health, kWolfHealth - 500.0F), "on the way back the creature has the health that it had");
    pass(host, 0.2F);
    check(wolf->position.x > turned + 1.9F && near(wolf->creature.yaw, 270.0F, 0.5F),
          "it goes to its home with its run speed, with its face to its home (east is the direction 270 degrees)");
    // On the way back it takes no target by its vision.
    host.world.set_position(player->id, state::WorldPosition{wolf->position.x + 6.0F, 1000.0F, kHeight}, false);
    pass(host, 0.4F);
    check(wolf->creature.mind.mode == CreatureMode::home, "on the way back the creature takes no target by its vision");
    host.world.set_position(player->id, state::WorldPosition{0.0F, 0.0F, kHeight}, false);
    pass(host, 9.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest && wolf->position.x == 1010.0F && wolf->position.y == 1000.0F
              && wolf->position.z == kHeight && wolf->creature.yaw == 90.0F && f.creatures().behavior().active_count() == 0
              && log_count("is at its home") == 1,
          "at its home the creature has the exact place and the direction of its home, and it is at rest");
    check(wolf->vitals.health == kWolfHealth && f.creatures().behavior().counts().returns == 1,
          "at its home the creature has its full health");
    // A target that travels away.
    host.world.set_position(player->id, state::WorldPosition{1004.0F, 1000.0F, kHeight}, false);
    run(host, 25);
    check(wolf->creature.mind.mode == CreatureMode::fight, "the creature takes the character as its target again");
    host.world.travel(kCharacterA, kFar);
    pass(host, 0.3F);
    check(wolf->creature.mind.mode == CreatureMode::home && log_count("lost its target (the target is too far)") == 1,
          "a creature loses a target that travelled away");
    pass(host, 3.0F);
    // A target that leaves the world.
    host.world.travel(kCharacterA, state::WorldPosition{1004.0F, 1000.0F, kHeight});
    pass(host, 0.5F);
    check(wolf->creature.mind.mode == CreatureMode::fight, "the creature has the target again after the travel back");
    host.world.remove_character(kCharacterA, world::LostCause::deleted);
    pass(host, 0.3F);
    check(wolf->creature.mind.mode != CreatureMode::fight && wolf->creature.mind.target == 0
              && log_count("lost its target (the target left the world)") == 1,
          "a creature loses a target that left the world");
    // A hit of a player on the way back gives the target again, with the reaction and the death of stage 3.2a.
    add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", kPlace);
    (void)host.world.spawn_character(kCharacterB, state::WorldPosition{1030.0F, 1000.0F, kHeight});
    const world::EntityId beta = host.world.find_character(kCharacterB)->id;
    pass(host, 2.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest, "the creature is at its home and does not see the second character 20 m away");
    check(f.creatures().hurt(wolfId, 100.0F, beta).error == world::CreatureError::none && wolf->creature.action
              && wolf->creature.action->action == kReaction && wolf->creature.mind.target == beta,
          "a creature at rest that takes a hit plays its reaction and gets the attacker as its target");
    check(f.creatures().hurt(wolfId, 100000.0F, beta).entity != nullptr && wolf->creature.dead && wolf->creature.mind.target == 0
              && wolf->creature.action && wolf->creature.action->action == 2,
          "a hit that takes its last health is its death: the dead mark, the death action, and no target");
}

/** FT8: the spawn point rule for a creature that is not at its spawn point. */
void spawn_rule_for_a_moved_creature(const Options&) {
    ServerHost host;
    flat_terrain(host);
    gamedata::SpawnPoint point;
    point.id = "p-wolf";
    point.kind = 0;
    point.level = 18;
    point.position = state::WorldPosition{2000.0F, 2000.0F, kHeight};
    point.yaw = 270.0F;
    point.ground = kHeight;
    check(set_fight_data(host, {point}), "the creature data has one spawn point");
    add_player(host.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    // The character is 140 m to the east of the point: the point makes its creature.
    (void)host.world.spawn_character(kCharacterA, state::WorldPosition{2140.0F, 2000.0F, kHeight});
    world::Creatures& creatures = host.world.creatures();
    const auto scan = [&] {
        host.clock.advance(world::Creatures::kScanInterval);
        host.world.tick();
    };
    scan();
    const world::EntityId id = creatures.spawn_points()[0].entity;
    world::Entity* wolf = creatures.find(id);
    check(wolf != nullptr && wolf->creature.mind.walks && creatures.count() == 1,
          "the spawn point makes its creature when a character is 150 m or less from the point");
    // The creature is 70 m to the west of its point: 210 m from the character.
    creatures.move(*wolf, state::WorldPosition{1930.0F, 2000.0F, kHeight});
    scan();
    scan();
    check(creatures.find(id) != nullptr && creatures.counts().removedByDistance == 0 && creatures.counts().spawnedByPoint == 1,
          "a creature that is more than 200 m from each character stays in the world while a character is 200 m or less "
          "from its spawn point: the rule looks at the place of the point");
    // The character goes to 195 m from the point: the creature stays. At 205 m it leaves.
    const world::EntityId player = host.world.find_character(kCharacterA)->id;
    host.world.set_position(player, state::WorldPosition{2195.0F, 2000.0F, kHeight}, false);
    scan();
    check(creatures.find(id) != nullptr, "with the character 195 m from the point the creature stays");
    creatures.move(*creatures.find(id), state::WorldPosition{2100.0F, 2000.0F, kHeight});
    host.world.set_position(player, state::WorldPosition{2205.0F, 2000.0F, kHeight}, false);
    scan();
    check(creatures.find(id) == nullptr && creatures.counts().removedByDistance == 1 && creatures.count() == 0,
          "with the character 205 m from the point the creature leaves the world, also when it is 105 m from the "
          "character");
    host.world.set_position(player, state::WorldPosition{2140.0F, 2000.0F, kHeight}, false);
    scan();
    const world::Entity* next = creatures.find(creatures.spawn_points()[0].entity);
    check(next != nullptr && next->id != id && next->position.x == 2000.0F && creatures.counts().spawnedByPoint == 2,
          "when a character is 150 m or less from the point again, the point makes a new creature at the point");
}

/** FT9: the load of the behavior. */
void load_of_the_behavior(const Options&) {
    ServerHost host;
    flat_terrain(host);
    // A world of the size of the game: one spawn point for each 78 m, 33,856 points, each one a wolf that fights.
    std::vector<gamedata::SpawnPoint> points;
    for (float x = 0.0F; x <= 14274.0F; x += 78.0F) {
        for (float y = 0.0F; y <= 14274.0F; y += 78.0F) {
            gamedata::SpawnPoint point;
            point.id = "p" + std::to_string(points.size());
            point.kind = 0;
            point.level = 18;
            point.position = state::WorldPosition{x, y, kHeight};
            point.ground = kHeight;
            points.push_back(point);
        }
    }
    const std::size_t count = points.size();
    check(set_fight_data(host, std::move(points)) && host.creatures.fighting_points() == count,
          "a world with 33,856 spawn points, each one with a creature that fights");
    world::Creatures& creatures = host.world.creatures();
    world::Behavior& behavior = creatures.behavior();
    const unsigned kPlayers = 50;
    std::vector<std::string> ids;
    for (unsigned index = 0; index < kPlayers; ++index) {
        char id[40];
        std::snprintf(id, sizeof(id), "%08x-0000-4000-8000-%012x", 0x10000000U + index, index);
        add_player(host.store, "765612022553" + std::to_string(10000 + index), "Tester " + std::to_string(index), id,
                   "Player" + std::to_string(index), std::nullopt);
        ids.emplace_back(id);
    }
    // First the characters are 30 m from each spawn point: no creature has a character in its vision.
    for (unsigned index = 0; index < kPlayers; ++index) {
        const float x = 780.0F + 234.0F * static_cast<float>(index) + 39.0F;
        const float y = 780.0F + 234.0F * static_cast<float>((index * 7) % kPlayers) + 39.0F;
        (void)host.world.spawn_character(ids[index], state::WorldPosition{x, y, kHeight});
    }
    const auto ticks = [&](int number) {
        const auto started = std::chrono::steady_clock::now();
        for (int index = 0; index < number; ++index) {
            host.clock.advance(std::chrono::milliseconds(10));
            host.world.tick();
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    };
    ticks(100);
    const std::size_t inWorld = creatures.count();
    const double restTime = ticks(1000);
    note(std::to_string(kPlayers) + " characters, " + std::to_string(inWorld) + " creatures in the world, no creature with a "
         "character in its vision: 1000 ticks (10 s of the world clock) need " + std::to_string(restTime) + " ms; "
         + std::to_string(behavior.counts().sensed) + " looks of the vision rule, " + std::to_string(behavior.counts().creatureSteps)
         + " creature steps");
    check(inWorld > 500 && behavior.active_count() == 0 && behavior.counts().creatureSteps == 0 && behavior.counts().sensed == 0,
          "creatures with no character in their vision distance need no time: no step looks at one of them");
    // Then each character is 8 m to the north of a spawn point: the creature of the point fights.
    for (unsigned index = 0; index < kPlayers; ++index) {
        const float x = 780.0F + 234.0F * static_cast<float>(index);
        const float y = 780.0F + 234.0F * static_cast<float>((index * 7) % kPlayers) + 8.0F;
        host.world.set_position(host.world.find_character(ids[index])->id, state::WorldPosition{x, y, kHeight}, false);
    }
    ticks(60);
    const std::uint64_t stepsBefore = behavior.counts().steps;
    const std::uint64_t creatureStepsBefore = behavior.counts().creatureSteps;
    const double fightTime = ticks(1000);
    const std::uint64_t steps = behavior.counts().steps - stepsBefore;
    const std::uint64_t creatureSteps = behavior.counts().creatureSteps - creatureStepsBefore;
    note(std::to_string(kPlayers) + " characters, each one with one creature that fights: 1000 ticks need "
         + std::to_string(fightTime) + " ms; " + std::to_string(steps) + " steps of the behavior, "
         + std::to_string(creatureSteps) + " creature steps, " + std::to_string(behavior.counts().attacks) + " attacks, "
         + std::to_string(behavior.counts().contactsHit) + " contacts; the largest step "
         + std::to_string(std::chrono::duration<double, std::micro>(behavior.load().largest).count()) + " us");
    check(behavior.active_count() == kPlayers && steps == 50 && creatureSteps == 50ULL * kPlayers,
          "only the creatures with a target are in the step: 50 creatures of " + std::to_string(creatures.count())
              + ", one step for each 200 ms");
    check(behavior.counts().attacks >= 3ULL * kPlayers && behavior.counts().contactsHit >= 3ULL * kPlayers,
          "each of the 50 creatures attacks its character again and again");
    check(behavior.load().count == behavior.counts().steps && behavior.load().total.count() > 0,
          "the world measures the real time of the steps of the behavior");
    // The time limit: 10 s of the world for 50 fights need less than 1 s of real time (the
    // measured value is in the note above; a step that looks at each creature of the world
    // for each character does not change this number much, thus the counts above are the proof
    // of the rule, and this limit finds a step that is slow).
    check(fightTime < 1000.0 && restTime < 1000.0, "1000 ticks with 50 fights need less than 1 s of real time");
}

/** FT10: two sessions near one creature, and a session far away. */
void two_sessions_and_one_creature(const Options&) {
    FightHost f(observing());
    ServerHost& host = f.host;
    const state::WorldPosition placeB{1002.0F, 1006.0F, kHeight};
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", placeB);
    const Player gamma = add_player(host.store, kSteamIdC, "Tester C", kCharacterC, "Gamma", kFar);
    FakeClient a(27000);
    FakeClient b(27001);
    FakeClient c(27002);
    std::uint16_t inputA = 0;
    std::uint16_t inputB = 0;
    std::uint16_t inputC = 0;
    enter_at(host, a, f.alpha, kPlace, inputA);
    enter_at(host, b, beta, placeB, inputB);
    enter_at(host, c, gamma, kFar, inputC);
    run(host, 60, {&a, &b, &c});
    world::Entity* alpha = host.world.find_character(kCharacterA);
    world::Entity* second = host.world.find_character(kCharacterB);
    // The creature is 9 m to the east of A and 10 m from B. First it looks away, and the two
    // sessions observe it. Then it turns: the two characters are in its vision.
    world::Entity* wolf = f.wolf(1009.0F, 1000.0F, 270.0F);
    const world::EntityId wolfId = wolf->id;
    pass(host, 0.7F, {&a, &b, &c});
    check(wolf->creature.mind.mode == CreatureMode::rest && interest_of(host, 1, wolfId) >= 2 && interest_of(host, 2, wolfId) >= 2,
          "the two sessions observe the creature, which is at rest");
    wolf->creature.yaw = 90.0F;
    std::size_t fromA = a.got.size();
    std::size_t fromB = b.got.size();
    const std::size_t fromC = c.got.size();
    const float healthA = alpha->vitals.health;
    const float healthB = second->vitals.health;
    pass(host, 2.0F, {&a, &b, &c});
    check(wolf->creature.mind.target == alpha->id && f.creatures().behavior().counts().targetsBySight == 1,
          "a creature with two characters in its vision has one target: a character of the world, not a session");
    const std::uint32_t wolfForA = interest_of(host, 1, wolfId);
    const std::uint32_t wolfForB = interest_of(host, 2, wolfId);
    const auto walked = [](const std::vector<Fragment>& states) {
        return std::count_if(states.begin(), states.end(), [](const Fragment& state) { return scope_action(state) == -1; });
    };
    const auto attacked = [](const std::vector<Fragment>& states) {
        return std::count_if(states.begin(), states.end(), [](const Fragment& state) { return scope_action(state) == static_cast<int>(kBite); });
    };
    const std::vector<Fragment> statesA = fragments_of(a, fromA, wolfForA, kWolfSlots[0]);
    const std::vector<Fragment> statesB = fragments_of(b, fromB, wolfForB, kWolfSlots[0]);
    check(wolfForA >= 2 && wolfForB >= 2 && walked(statesA) >= 3 && walked(statesA) == walked(statesB) && attacked(statesA) >= 1
              && attacked(statesA) == attacked(statesB),
          "the two sessions get the same movement states of the creature: its way to its target, and its attack");
    check(alpha->vitals.health < healthA && second->vitals.health == healthB,
          "the target takes the damage, and the character beside it takes none");
    const std::uint32_t alphaForB = interest_of(host, 2, alpha->id);
    const std::vector<Fragment> seen = fragments_of(b, fromB, alphaForB, kPlayerVitalsSlot);
    check(alphaForB >= 2 && !seen.empty() && near(health_of(seen.back()), alpha->vitals.health, 0.01F)
              && messages_of(a, server::rep::kTypeDamageTaken, fromA).size() >= 1
              && messages_of(b, server::rep::kTypeDamageTaken, fromB).empty(),
          "the second session gets the health of the character that was hit with its vitals state, and only the session "
          "of that character gets the damage mark");
    check(c.got.size() >= fromC && fragments_of(c, fromC, interest_of(host, 3, wolfId), kWolfSlots[0]).empty()
              && interest_of(host, 3, wolfId) == 0 && interest_of(host, 3, alpha->id) == 0
              && messages_of(c, server::rep::kTypeDamageTaken, fromC).empty(),
          "a session 2 km away gets nothing of the fight");
    // The second character fights the creature: a hit does not change the target.
    f.creatures().hurt(wolfId, 50.0F, second->id);
    check(wolf->creature.mind.target == alpha->id, "a hit of the second character does not change the target of the creature");
    // The target dies while the second character fights.
    fromA = a.got.size();
    fromB = b.got.size();
    host.world.health().hurt(kCharacterA, 100000.0F);
    pass(host, 0.5F, {&a, &b, &c});
    check(alpha->life.dead && wolf->creature.mind.target == 0 && wolf->creature.mind.mode == CreatureMode::home,
          "when its target dies the creature loses it");
    const std::vector<Fragment> death = fragments_of(b, fromB, alphaForB, kPlayerVitalsSlot);
    check(!death.empty() && health_of(death.back()) == 0.0F && death.back().fields.at({0, kFieldVitalsData})[0] == 0x01
              && messages_of(a, server::rep::kTypePlayerDeath, fromA).size() == 1
              && messages_of(b, server::rep::kTypePlayerDeath, fromB).empty(),
          "the second session gets the dead mark of that character with its vitals state, and only the session of the "
          "dead character gets the death notice");
    f.creatures().hurt(wolfId, 50.0F, second->id);
    check(wolf->creature.mind.mode == CreatureMode::fight && wolf->creature.mind.target == second->id,
          "the next hit of the second character gives the creature that character as its target");
    pass(host, 3.0F, {&a, &b, &c});
    check(second->vitals.health < healthB && alpha->life.dead && alpha->vitals.health == 0.0F,
          "the creature goes to the second character and hurts it, and the dead character takes no damage");
    // The session of the target ends: the creature stays in the world for the other session.
    host.datagram(b.peer, b.carrier_close());
    pass(host, 0.5F, {&a, &c});
    check(host.world.find_character(kCharacterB) == nullptr && f.creatures().find(wolfId) != nullptr
              && wolf->creature.mind.target == 0 && interest_of(host, 1, wolfId) != 0,
          "when the session of its target ends the creature loses the target and stays in the world for the other session");
}

/** FT11: the switches. */
void switches(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    const Player beta = add_player(host.store, kSteamIdB, "Tester B", kCharacterB, "Beta", state::WorldPosition{1300.0F, 1000.0F, kHeight});
    server::http::AdminAccess access;
    const server::http::Services services{host.store, "127.0.0.1:32210", "test-version", "", host.tables, host.world};
    server::http::Router router;
    check(access.add(kTokenA, kSteamIdA) && access.add(kTokenB, kSteamIdB), "the two accounts have a token");
    server::http::register_dev_routes(router, services);
    server::http::register_admin_routes(router, services, access);
    host.tick();
    const auto send = [&](const char* method, const std::string& path, const std::string& query, const std::string& body = {},
                          const char* token = nullptr) {
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
    const auto admin = [&](const char* command, const char* character, const json& arguments, const char* token = kTokenA) {
        return send("POST", "/admin/v1/commands", {},
                    json{{"command", command}, {"character", character}, {"args", arguments}}.dump(), token);
    };
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, f.alpha, kPlace, input);
    world::Entity* player = host.world.find_character(kCharacterA);
    world::Behavior& behavior = f.creatures().behavior();

    // (a) The behavior off: the creatures stand.
    host.world.set_creature_behavior(false);
    const std::uint64_t stepsBefore = behavior.counts().steps;
    const world::Entity* wolf = f.wolf(1006.0F, 1000.0F, 90.0F);
    const world::EntityId wolfId = wolf->id;
    pass(host, 2.0F, {&a});
    f.creatures().hurt(wolfId, 10.0F, player->id);
    pass(host, 1.0F, {&a});
    check(wolf->creature.mind.mode == CreatureMode::rest && wolf->position.x == 1006.0F && behavior.counts().steps == stepsBefore
              && behavior.counts().targetsByHit == 0 && player->vitals.health == player->vitals.healthMax,
          "with the behavior off a creature takes no target by its vision or by a hit, and it does not move");
    check(json::parse(send("GET", "/v1/dev/world", "").body)["load"]["behaviorOn"] == false,
          "the list of the world says that the behavior is off");
    host.world.set_creature_behavior(true);

    // (b) The creatures ignore a character.
    check(send("POST", "/v1/dev/world/ignore", std::string("character=") + kCharacterA + "&ignore=1").status == 200
              && behavior.ignores(kCharacterA),
          "the development command turns the value on for a character");
    pass(host, 2.0F, {&a});
    f.creatures().hurt(wolfId, 10.0F, player->id);
    pass(host, 1.0F, {&a});
    check(wolf->creature.mind.mode == CreatureMode::rest && behavior.counts().targetsBySight == 0 && behavior.counts().targetsByHit == 0
              && player->vitals.health == player->vitals.healthMax,
          "the creatures ignore the character: no target by the vision, and none by a hit");
    const json state = json::parse(send("GET", "/admin/v1/state", std::string("character=") + kCharacterA, {}, kTokenA).body);
    check(state["health"]["creaturesIgnore"] == true && state["health"]["dead"] == false
              && state["health"]["healthMax"] == player->vitals.healthMax && state["health"]["creatureBehavior"] == true,
          "the state of the character for the tool window has the value, and the health of the character");
    check(admin("set_creatures_ignore", kCharacterA, {{"ignore", false}}).status == 200 && !behavior.ignores(kCharacterA),
          "the admin command turns the value off");
    pass(host, 0.3F, {&a});
    check(wolf->creature.mind.mode == CreatureMode::fight, "the creature then takes the character as its target");
    check(admin("set_creatures_ignore", kCharacterA, {{"ignore", true}}).status == 200, "the admin command turns the value on");
    pass(host, 0.3F, {&a});
    check(wolf->creature.mind.target == 0 && log_count("lost its target (the creatures ignore the target)") == 1,
          "a creature that has the character as its target loses it");
    check(admin("set_creatures_ignore", kCharacterA, json::object()).status == 400
              && admin("set_creatures_ignore", kCharacterA, {{"ignore", "maybe"}}).status == 400
              && admin("set_creatures_ignore", kCharacterA, {{"ignore", true}}, kTokenB).status == 404
              && admin("set_creatures_ignore", kCharacterB, {{"ignore", true}}, kTokenB).status == 200
              && behavior.ignores(kCharacterB),
          "a command with no value or with a wrong value gives 400; a character of a different account gives 404; a "
          "character that is not in the world gets the value too (a value of the character, with its id as the key)");
    // The value stays when the session of the character ends.
    host.datagram(a.peer, a.carrier_close());
    run(host, 3, {&a});
    check(host.world.find_character(kCharacterA) == nullptr && behavior.ignores(kCharacterA),
          "the value stays when the session of the character ends");
    FakeClient again(27002);
    std::uint16_t next = 0;
    enter_at(host, again, f.alpha, kPlace, next);
    player = host.world.find_character(kCharacterA);
    pass(host, 1.0F, {&again});
    check(wolf->creature.mind.mode == CreatureMode::rest, "a new session of the character has the same value: no target");

    // (c) The heal command.
    host.world.health().hurt(kCharacterA, 300.0F);
    check(send("POST", "/v1/dev/world/heal", std::string("character=") + kCharacterA).status == 200
              && player->vitals.health == player->vitals.healthMax,
          "the development command gives a character its full health");
    std::size_t from = again.got.size();
    check(send("POST", "/v1/dev/world/hurt", std::string("character=") + kCharacterA + "&amount=5000").status == 200 && player->life.dead,
          "the development command hurt takes the health of a character: the death");
    run(host, 2, {&again});
    check(messages_of(again, server::rep::kTypePlayerDeath, from).size() == 1, "the client gets the death notice");
    from = again.got.size();
    const service::HttpResponse healed = admin("heal", kCharacterA, json::object());
    run(host, 2, {&again});
    const std::vector<Fragment> alive = fragments_of(again, from, 1, kPlayerVitalsSlot);
    check(healed.status == 200 && json::parse(healed.body)["revived"] == true && !player->life.dead
              && player->vitals.health == player->vitals.healthMax && player->position.x == kPlace.x && player->teleport == 1
              && !alive.empty() && alive.back().fields.at({0, kFieldVitalsData})[0] == 0
              && health_of(alive.back()) == player->vitals.healthMax,
          "the admin command heal makes a dead character live again at its place with its full health: the owner gets the "
          "vitals state with no dead mark, and no travel");
    check(admin("heal", kCharacterA, json::object(), kTokenB).status == 404
              && json::parse(admin("heal", kCharacterB, json::object(), kTokenB).body)["applied"] == "stored"
              && send("POST", "/v1/dev/world/hurt", std::string("character=") + kCharacterB + "&amount=10").status == 404
              && send("POST", "/v1/dev/world/hurt", std::string("character=") + kCharacterA + "&amount=abc").status == 400
              && admin("hurt_character", kCharacterA, {{"amount", 10}}).status != 200,
          "heal for a character of a different account gives 404; for a character that is not in the world nothing "
          "changes; the damage command is a development command only");
}

/** FT12: a creature whose spawn point is not at the terrain height. */
void creature_that_does_not_walk(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    check(host.world.spawn_character(kCharacterA, kPlace) == world::SpawnError::none, "a character is in the world");
    world::Entity* player = host.world.find_character(kCharacterA);
    // The floor of a building: 3 m above the terrain. The character is on that floor too.
    host.world.set_position(player->id, state::WorldPosition{1000.0F, 1000.0F, kHeight + 3.0F}, false);
    const world::Entity* wolf = f.wolf(1008.0F, 1000.0F, 0.0F, kHeight + 3.0F);
    const world::EntityId wolfId = wolf->id;
    check(!wolf->creature.mind.walks, "a creature whose place is 3 m above the terrain does not walk");
    f.creatures().hurt(wolfId, 1.0F, player->id);
    pass(host, 2.0F);
    check(wolf->creature.mind.mode == CreatureMode::fight && wolf->position.x == 1008.0F && wolf->position.z == kHeight + 3.0F
              && near(wolf->creature.yaw, 90.0F, 0.5F) && f.creatures().behavior().counts().moves == 0
              && f.creatures().behavior().counts().attacks == 0,
          "it has a target and turns to it, and it does not leave its place");
    // The character comes into its reach: it attacks.
    host.world.set_position(player->id, state::WorldPosition{1008.0F - kBiteReach + 0.1F, 1000.0F, kHeight + 3.0F}, false);
    const float before = player->vitals.health;
    pass(host, 1.0F);
    check(f.creatures().behavior().counts().attacks == 1 && player->vitals.health < before && wolf->position.x == 1008.0F,
          "it attacks a target that is in its reach, from its place");
    // A target that goes away: the creature loses it at the vision distance of a fight, and it is at its home at once.
    host.world.set_position(player->id, state::WorldPosition{1008.0F - 31.0F, 1000.0F, kHeight + 3.0F}, false);
    pass(host, 2.0F);
    check(wolf->creature.mind.mode == CreatureMode::rest && wolf->creature.yaw == 0.0F && wolf->vitals.health == kWolfHealth
              && log_count("lost its target (the target is too far)") == 1,
          "a target that is farther than its vision distance of a fight (30 m) is lost: the creature has the direction of "
          "its home and its full health again");
    // A backend with no terrain data: no creature walks.
    ServerHost bare;
    check(set_fight_data(bare), "a host with no terrain");
    add_player(bare.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    (void)bare.world.spawn_character(kCharacterA, kPlace);
    const world::Entity* still = bare.world.creatures().spawn("Test_Wolf", state::WorldPosition{1008.0F, 1000.0F, kHeight}, 90.0F).entity;
    pass(bare, 2.0F);
    check(!bare.world.navigation().ground_height(kPlace) && !still->creature.mind.walks && still->creature.mind.mode == CreatureMode::fight
              && still->position.x == 1008.0F,
          "with no terrain heights a creature takes its target and does not leave its place");
    // The spawn command on a slope: the creature stands on the terrain at its own place.
    ServerHost slope;
    check(set_fight_data(slope), "a host with a slope");
    slope.terrain.source = [](float x, float) { return std::optional<float>(kHeight + (x - 1000.0F) * 0.5F); };
    add_player(slope.store, kSteamIdA, "Tester A", kCharacterA, "Alpha", kPlace);
    (void)slope.world.spawn_character(kCharacterA, kPlace);
    slope.world.set_creature_behavior(false);
    const server::http::Services services{slope.store, "127.0.0.1:32210", "test-version", "", slope.tables, slope.world};
    server::http::Router router;
    server::http::register_dev_routes(router, services);
    slope.tick();
    const auto command = [&](const char* offset) {
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/v1/dev/creatures/spawn";
        request.query = std::string("character=") + kCharacterA + "&kind=Test_Wolf&" + offset;
        request.target = request.path + "?" + request.query;
        return json::parse(server::http::handle_request(router, request).body, nullptr, false);
    };
    const json uphill = command("dx=8&dy=0&dz=0");
    check(uphill["creature"]["walks"] == true && near(uphill["creature"]["position"][2].get<float>(), kHeight + 4.0F, 0.001F),
          "a creature of a command 8 m up a slope stands on the terrain at its place (4 m higher than the character), and it walks");
    // A character on a floor, 3 m above the terrain: the creature gets the height of the character.
    world::Entity* high = slope.world.find_character(kCharacterA);
    slope.world.set_position(high->id, state::WorldPosition{1000.0F, 1000.0F, kHeight + 3.0F}, false);
    const json floor = command("dx=0&dy=6&dz=0.5");
    check(floor["creature"]["walks"] == false && near(floor["creature"]["position"][2].get<float>(), kHeight + 3.5F, 0.001F),
          "a creature of a command near a character that is not on the terrain gets the height of the character, and it does not walk");
}

/** FT13: a slice with a spawn action and with animation tags. */
void spawn_action_slice_in_a_fight(const Options&) {
    FightHost f;
    ServerHost& host = f.host;
    FakeClient a(27000);
    std::uint16_t input = 0;
    enter_at(host, a, f.alpha, kPlace, input);
    world::Entity* player = host.world.find_character(kCharacterA);
    // The creature is behind the character of its vision: it has no target.
    const world::Entity* guard = f.creatures().spawn("Test_Guard", state::WorldPosition{1006.0F, 1000.0F, kHeight}, 270.0F).entity;
    const world::EntityId guardId = guard->id;
    std::size_t from = a.got.size();
    host.advance(std::chrono::milliseconds(600));
    run(host, 3, {&a});
    const std::uint32_t number = interest_of(host, 1, guardId);
    std::vector<Fragment> states = fragments_of(a, from, number, kGuardSlots[0]);
    const Bytes tags{0x24, 0x04, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    check(number >= 2 && states.size() == 1 && scope_action(states[0]) == static_cast<int>(kSpawn)
              && states[0].fields.at({0, kFieldTags}) == tags && guard->creature.mind.mode == CreatureMode::rest,
          "the first movement state of the session for a creature of a slice with a spawn action has that action, with "
          "the tag state of the slice");
    // The fight: the walk states have no action, and the attack replaces the spawn action.
    from = a.got.size();
    f.creatures().hurt(guardId, 1.0F, player->id);
    pass(host, 3.0F, {&a});
    states = fragments_of(a, from, number, kGuardSlots[0]);
    const auto count = [&states](int action) {
        return std::count_if(states.begin(), states.end(), [action](const Fragment& state) { return scope_action(state) == action; });
    };
    check(count(static_cast<int>(kSpawn)) == 0 && count(-1) >= 2 && count(static_cast<int>(kReaction)) == 1
              && count(static_cast<int>(kBite)) >= 1,
          "in the fight the session gets the walk with no action and the attack: the spawn action goes out one time, and "
          "the creature does not play it again");
    const auto attack = std::find_if(states.begin(), states.end(), [](const Fragment& state) { return scope_action(state) == static_cast<int>(kBite); });
    check(attack != states.end() && attack->fields.at({0, kFieldTags}) == tags,
          "the state of the attack has the tag state of the slice: the tags of the weapons select the variant of the fragment");
    check(guard->creature.spawnAction && guard->creature.spawnAction->action == kSpawn && guard->creature.spawnAction->generation == 1,
          "the spawn action is a value of the creature for its whole time in the world");
    // A session that observes the creature later gets the spawn action in its first record.
    host.world.set_creature_behavior(false);
    host.world.travel(kCharacterA, kFar);
    run(host, 3, {&a});
    host.advance(std::chrono::seconds(4));
    from = a.got.size();
    host.world.travel(kCharacterA, kPlace);
    run(host, 4, {&a});
    states = fragments_of(a, from, interest_of(host, 1, guardId), kGuardSlots[0]);
    check(!states.empty() && scope_action(states[0]) == static_cast<int>(kSpawn),
          "a session that observes the creature later gets the spawn action in its first record: its body shows");
}

/** Writes one height file of the form of tools/gamedata/height_maps.py, with the tiles as stored zlib streams. */
void write_height_file(const std::filesystem::path& path, std::uint32_t regionX, std::uint32_t regionY,
                       const std::function<std::uint16_t(std::uint32_t, std::uint32_t)>& sample, std::uint32_t badTile = 0xFFFFFFFFU) {
    constexpr std::uint32_t kSize = 2048;
    constexpr std::uint32_t kTile = 64;
    constexpr std::uint32_t kTiles = kSize / kTile;
    const auto u32 = [](Bytes& out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) {
            out.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    };
    Bytes header;
    header.insert(header.end(), {'S', 'W', 'H', 'T'});
    u32(header, 1);
    u32(header, regionX);
    u32(header, regionY);
    u32(header, kSize);
    u32(header, kTile);
    const float scale = 2048.0F;
    std::uint32_t bits = 0;
    std::memcpy(&bits, &scale, sizeof(bits));
    u32(header, bits);
    u32(header, 0);
    std::vector<Bytes> tiles;
    for (std::uint32_t tileY = 0; tileY < kTiles; ++tileY) {
        for (std::uint32_t tileX = 0; tileX < kTiles; ++tileX) {
            Bytes plain;
            std::uint16_t south = 0;
            for (std::uint32_t row = 0; row <= kTile; ++row) {
                std::uint16_t west = south;
                for (std::uint32_t column = 0; column <= kTile; ++column) {
                    const std::uint16_t value = sample(tileX * kTile + column, tileY * kTile + row);
                    const auto delta = static_cast<std::uint16_t>(value - west);
                    plain.push_back(static_cast<std::uint8_t>(delta));
                    plain.push_back(static_cast<std::uint8_t>(delta >> 8));
                    west = value;
                    if (column == 0) {
                        south = value;
                    }
                }
            }
            Bytes stream = compression::zlib_store(plain);
            if (tileY * kTiles + tileX == badTile) {
                stream.resize(stream.size() / 2);
            }
            tiles.push_back(std::move(stream));
        }
    }
    Bytes index;
    std::uint32_t offset = static_cast<std::uint32_t>(header.size() + 8 * tiles.size());
    for (const Bytes& tile : tiles) {
        u32(index, offset);
        u32(index, static_cast<std::uint32_t>(tile.size()));
        offset += static_cast<std::uint32_t>(tile.size());
    }
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(header.data()), static_cast<std::streamsize>(header.size()));
    file.write(reinterpret_cast<const char*>(index.data()), static_cast<std::streamsize>(index.size()));
    for (const Bytes& tile : tiles) {
        file.write(reinterpret_cast<const char*>(tile.data()), static_cast<std::streamsize>(tile.size()));
    }
}

/** FT14: the reader of the terrain height files, and the two functions of the navigation. */
void heights_and_navigation(const Options& options) {
    const auto folder = options.workDirectory / "fight-heights";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    gamedata::HeightMaps maps;
    check(!maps.set_directory(folder.string()) && !maps.available() && !maps.height(100.0F, 100.0F),
          "a folder with no height file: the reader has no height");
    // The region r_+00_+01: the sample at (x, y) is 1000 + 16 x + 32 y (a plane), in units of 2048 / 65535 m.
    const auto plane = [](std::uint32_t x, std::uint32_t y) { return static_cast<std::uint16_t>(1000 + 16 * (x % 512) + 32 * (y % 512)); };
    write_height_file(folder / "r_+00_+01.swh", 0, 1, plane, 5);
    check(maps.set_directory(folder.string()) && maps.available(), "a folder with one height file");
    const float unit = 2048.0F / 65535.0F;
    const std::optional<float> exact = maps.height(100.0F, 2048.0F + 70.0F);
    const std::optional<float> between = maps.height(100.5F, 2048.0F + 70.25F);
    check(exact && near(*exact, static_cast<float>(1000 + 16 * 100 + 32 * 70) * unit, 0.0005F) && between
              && near(*between, (1000.0F + 16.0F * 100.5F + 32.0F * 70.25F) * unit, 0.0005F),
          "the height at a sample, and between four samples: the samples are at whole meters");
    const std::optional<float> edge = maps.height(63.75F, 2048.0F + 127.5F);
    check(edge && near(*edge, (1000.0F + 16.0F * 63.75F + 32.0F * 127.5F) * unit, 0.0005F),
          "the height at the edge of a tile: a tile has the first samples of the next tile");
    check(maps.counts().filesOpened == 1 && maps.counts().tilesRead == 2 && maps.counts().tilesHeld == 2,
          "the reader opened one file and decoded only the two tiles of these places");
    check(!maps.height(5000.0F, 100.0F) && !maps.height(-5.0F, 2100.0F) && !maps.height(100.0F, std::nanf(""))
              && maps.counts().filesNotThere == 1,
          "a place in a region with no file, and a place that is not a place, have no height");
    check(!maps.height(64.0F * 5.0F + 3.0F, 2048.0F + 3.0F) && maps.counts().tilesBad == 1 && !maps.height(64.0F * 5.0F + 9.0F, 2048.0F + 9.0F)
              && maps.counts().tilesBad == 1,
          "a tile of the file that does not decode has no height, and the reader does not try it again");
    // The reader keeps a limited number of tiles.
    for (std::uint32_t tile = 0; tile < 700; ++tile) {
        (void)maps.height(static_cast<float>((tile % 32) * 64 + 5), 2048.0F + static_cast<float>(((tile / 32) % 32) * 64 + 5));
    }
    check(maps.counts().tilesHeld <= gamedata::HeightMaps::kMaxTiles && maps.counts().tilesRead > 600,
          "the reader keeps 512 tiles at most: it reads a tile again when it is necessary");
    // A file that is not a height file.
    std::ofstream(folder / "r_+03_+03.swh", std::ios::binary) << "not a height file";
    check(!maps.height(3.0F * 2048.0F + 10.0F, 3.0F * 2048.0F + 10.0F), "a file of a different form gives no height");

    // The two functions of the navigation: the ground, and the next step.
    const world::Navigation none(nullptr);
    check(!none.available() && !none.ground_height(kPlace) && !none.next_step(kPlace, kFar, 1.0F),
          "a navigation with no terrain has no ground and no step");
    TestTerrain slope;
    slope.source = [](float x, float y) { return x > 1500.0F ? std::nullopt : std::optional<float>(10.0F + x * 0.01F + y * 0.0F); };
    const world::Navigation navigation(&slope);
    const std::optional<float> ground = navigation.ground_height(state::WorldPosition{1000.0F, 1000.0F, 999.0F});
    check(ground && near(*ground, 20.0F), "the ground at a position is the terrain height at its place, for each height of the position");
    const std::optional<state::WorldPosition> step = navigation.next_step(kPlace, state::WorldPosition{1030.0F, 1040.0F, 0.0F}, 5.0F);
    check(step && near(step->x, 1003.0F, 0.001F) && near(step->y, 1004.0F, 0.001F) && near(step->z, 10.0F + 1003.0F * 0.01F, 0.001F),
          "one step: the place 5 m nearer to the goal on the straight line, with the height of the ground there");
    const std::optional<state::WorldPosition> last = navigation.next_step(kPlace, state::WorldPosition{1003.0F, 1000.0F, 0.0F}, 5.0F);
    check(last && last->x == 1003.0F && last->y == 1000.0F && near(last->z, 20.03F, 0.001F),
          "a step that is longer than the way ends at the goal, with the height of the ground");
    check(!navigation.next_step(state::WorldPosition{1499.0F, 0.0F, 0.0F}, state::WorldPosition{1600.0F, 0.0F, 0.0F}, 5.0F)
              && !navigation.next_step(kPlace, kFar, 0.0F),
          "a step to a place with no ground, and a step with no length, give no place");
    std::filesystem::remove_all(folder);
}

} // namespace

std::vector<Case> fight_cases() {
    return {
        {"FT1", "fight: the creature file with the behavior data, a file whose parts do not fit together, and an old file", behavior_data_file},
        {"FT2", "fight: the sense of a creature (the vision distance and angle, a target by a hit, one target)", sense_of_a_creature},
        {"FT3", "fight: the way to the target (the speed, the ground, the position index, the states of an observer)", way_to_the_target},
        {"FT4", "fight: the attack action and its contact at its time, and a target that went away", attack_and_contact},
        {"FT5", "fight: the damage rule with the worked example, and the bytes for the owner and for an observer", damage_to_a_character},
        {"FT6", "fight: the death of a character, the respawn request, and a dead character that leaves the world", death_and_respawn},
        {"FT7", "fight: the way back (the leash, a target that is gone, the health at the home)", way_back},
        {"FT8", "fight: the spawn point rule for a creature that is not at its spawn point", spawn_rule_for_a_moved_creature},
        {"FT9", "fight: the load of the behavior with 33,856 spawn points and 50 players, with a time limit", load_of_the_behavior},
        {"FT10", "fight: two sessions near one creature, a session far away, and a death while a second player fights",
         two_sessions_and_one_creature},
        {"FT11", "fight: the switches (the behavior off, the creatures ignore a character, the heal command)", switches},
        {"FT12", "fight: a creature whose spawn point is not at the terrain height does not leave its place", creature_that_does_not_walk},
        {"FT13", "fight: a slice with a spawn action and with animation tags in a fight", spawn_action_slice_in_a_fight},
        {"FT14", "fight: the reader of the terrain height files, and the two functions of the navigation", heights_and_navigation},
    };
}

} // namespace soulwarden::backend::tests
