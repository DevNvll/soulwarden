// The cases of the code review of the corrections after user test 1 (stage 3.4e). The
// reviewer wrote them (cases RU1 to RU10 of its copy of the tree); each one of RU1 to RU6
// failed before its correction, and RU7 to RU9 are controls of the review.
//
//   RU1   a short line of sight over the edge of a cell does not cross a ceiling (finding F1)
//   RU2   the death of a creature with no death action ends the kept locomotion of a session (F2)
//   RU3   a first record with the spawn action is followed by the action that plays (F3)
//   RU4   a step ends a reaction also for a slice with no Move_Combat (F7)
//   RU5   a travel before the first camera report has the fallback of 20 s (F4)
//   RU6   a goal near the edge of a loaded tile waits for its own tile (F5)
//   RU7   controls: the lock of the input with many ends, wrong owners, and a disconnect
//   RU8   controls: the mana with no tables, and two travels that wait for the client
//   RU9   controls: the scope fields of a first record, an action that a session did not see
//   RU10  the spawn action of a first record has the time 0 (F9: the documents now say so)
//
// The accounts, the characters, the creature kinds, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <thread>
#include <vector>

#include "server/rep/lock_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_navigation.h"
#include "support/test_players.h"
#include "world/behavior.h"

namespace soulwarden::backend::tests {
namespace {

constexpr float ground = 50.0F;
constexpr unsigned movementSlot = 5;
constexpr unsigned spawnIndex = 4;
constexpr unsigned moveIndex = 5;
constexpr unsigned attackIndex = 3;
constexpr unsigned reactionIndex = 1;
const state::WorldPosition place{1000.0F, 1000.0F, ground};

struct Sink final : world::SessionSink {
    void character_lost(world::LostCause) override {}
};

gamedata::CreatureSlice slice(bool spawn, bool death, bool move) {
    gamedata::CreatureSlice body;
    body.path = "slices/test/review.dynamicslice";
    body.asset.fill(0x11);
    body.subId = 2;
    body.root = 0x1000000000000011ULL;
    body.interestRadius = 60.0F;
    body.movementSlot = movementSlot;
    body.vitalsSlot = 9;
    body.markerSlot = 15;
    body.slots = {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}};
    body.components = {{body.root, 0xA01ULL, "Root/Vitals"}};
    body.actions = {{"Idle_No_Task", "Idle", 0, false}, {"Reaction", "Reaction", 5, true},
                    {"Death", "Death", death ? 4 : -1, true}, {"Attack_Bite", "Attack_Bite", 7, false},
                    {"Spawn", "Spawn", 9, false}, {"Move_Combat", "Move_Combat", move ? 11 : -1, false}};
    if (spawn) body.spawnActionName = "Spawn";
    gamedata::CreatureBehavior behavior;
    behavior.sightAngle = 180.0F;
    behavior.sightDistance = 12.0F;
    behavior.combatAngle = 180.0F;
    behavior.combatDistance = 30.0F;
    behavior.restLeash = 20.0F;
    behavior.combatLeash = 70.0F;
    behavior.walkSpeed = 4.0F;
    behavior.runSpeed = 10.0F;
    body.behavior = behavior;
    gamedata::CreatureContact contact;
    contact.time = 0.5F;
    contact.reach = 2.2F;
    contact.factor = 0.2F;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Review_Bite";
    body.attacks = {gamedata::CreatureAttack{attackIndex, 1.4F, {contact}}};
    return body;
}

void data(ServerHost& host, bool spawn = false, bool death = true, bool move = true) {
    gamedata::CreatureKind kind;
    kind.id = "Review_Creature";
    kind.vitals = kind.id;
    kind.category = "Review_Category";
    kind.vitalsId = 0x11112222;
    kind.categoryId = 0x11112223;
    kind.name = kind.id;
    kind.vitalsName = kind.id;
    kind.family = "Wildlife";
    kind.creatureType = "Solo";
    kind.healthFactor = 1.0F;
    kind.level = 18;
    kind.bodies = {{0, 100}};
    check(host.creatures.set_full({slice(spawn, death, move)}, {kind}, {}, {{18, 1690.875977F, 616.41F, 180.0F}}),
          "setup: the synthetic creature data loads");
    host.terrain.source = [](float, float) { return std::optional<float>(ground); };
}

Player player(ServerHost& host, bool second = false, state::WorldPosition position = place) {
    return add_player(host.store, second ? kSteamIdB : kSteamIdA, "", second ? "review-b" : "review-a", second ? "FixtureB" : "FixtureA", position);
}

void enter(ServerHost& host, FakeClient& client, const Player& owner, state::WorldPosition position = place) {
    host.enter(client, owner.registration());
    host.advance(std::chrono::milliseconds(10));
    host.datagram(client.peer, client.report_at(1, position.x, position.y, position.z));
    host.steps(60, {&client});
}

world::Entity* creature(ServerHost& host, state::WorldPosition position = {1004.0F, 1000.0F, ground}) {
    return host.world.creatures().spawn("Review_Creature", position, 0.0F).entity;
}

std::uint32_t interest(ServerHost& host, unsigned session, world::EntityId id) {
    const auto& known = host.session(session).observer().known();
    const auto found = known.find(id);
    return found == known.end() ? 0 : found->second.interest;
}

std::vector<Fragment> fragments(const FakeClient& client, std::size_t from, unsigned number, unsigned slot = movementSlot) {
    std::vector<Fragment> result;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) continue;
        for (const auto& record : read_bundle(client.got[index].body).records) {
            if (record.interest != number) continue;
            for (const auto& part : record.fragments) if (part.slot == slot) result.push_back(part);
        }
    }
    return result;
}

int action(const Fragment& part) {
    const auto field = part.fields.find({0, 33});
    if (field == part.fields.end()) return -2;
    server::hub::Reader reader(field->second);
    const unsigned bytes = reader.size();
    if (bytes == 0) return -1;
    reader.u16();
    reader.size();
    return static_cast<int>(reader.size()) - 1;
}

void ceiling_at_cell_edge(const Options& options) {
    ServerHost host;
    data(host);
    TestNavRegion region;
    region.tiles[{15, 15}] = [](int, int, std::vector<TestFloor>& floors) {
        floors[0].sightTo = 2.0F;
        floors.push_back({ground + 2.5F, 0});
    };
    const auto folder = options.workDirectory / "review-cell-edge-ceiling";
    region.write(folder);
    check(host.navigation.set_directory(folder.string()), "setup: the two-floor navigation file loads");
    const state::WorldPosition below{1000.24F, 1000.125F, ground};
    const state::WorldPosition above{1000.26F, 1000.125F, ground + 2.5F};
    const auto owner = player(host, false, above);
    Sink sink;
    auto* character = host.world.enter(1, sink, owner.characterId, owner.account.personaId).entity;
    check(character && host.world.travel(owner.characterId, above, world::TravelScreen::off).error == world::TravelError::none,
          "setup: the character is above the ceiling");
    host.steps(60, {});
    auto& navigation = host.world.navigation();
    check(!navigation.sees(below, {below.x, below.y, above.z}), "control: the vertical line in one cell is blocked");
    check(!navigation.sees(below, {1001.24F, below.y, above.z}), "control: a longer slanted line is blocked");
    const bool seen = navigation.sees(below, above);
    note("sight across a cell edge through the ceiling=" + std::to_string(seen));
    check(!seen, "the short line through the ceiling is blocked across a cell edge");
    character->vitals.healthMax = character->vitals.health = 10000.0F;
    auto* attacker = creature(host, below);
    host.steps(80, {});
    note("attacks=" + std::to_string(attacker->creature.mind.attacks) + "; health=" + std::to_string(character->vitals.health));
    check(attacker->creature.mind.attacks == 0 && character->vitals.health == 10000.0F,
          "the creature cannot acquire or damage a character through this ceiling");
}

void death_ends_locomotion(const Options&) {
    ServerHost host;
    data(host, false, false);
    host.world.set_creature_behavior(false);
    const auto owner = player(host);
    FakeClient client(29000);
    enter(host, client, owner);
    auto* body = creature(host);
    const auto id = body->id;
    host.steps(60, {&client});
    const auto number = interest(host, 1, id);
    check(number >= 2 && !body->creature.body->deathAction, "setup: the session observes a slice without a death action");
    std::size_t from = client.got.size();
    host.world.creatures().move(*body, {1005.0F, 1000.0F, ground});
    host.steps(3, {&client});
    const auto walk = fragments(client, from, number);
    check(!walk.empty() && action(walk.back()) == static_cast<int>(moveIndex), "setup: the session has the locomotion scope");
    from = client.got.size();
    check(host.world.creatures().set_health(id, 0.0F).error == world::CreatureError::none, "setup: the creature dies");
    host.steps(60, {&client});
    const auto after = fragments(client, from, number);
    const auto sent = host.session(1).observer().known().at(id).scope;
    note("dead=" + std::to_string(body->creature.dead) + "; moving=" + std::to_string(body->creature.moving.has_value())
         + "; movement states after death=" + std::to_string(after.size()) + "; retained scope=" + std::to_string(sent));
    check(!after.empty() && action(after.back()) == -1 && sent == server::rep::kScopeNone,
          "death without a death action clears the session's kept locomotion scope");
}

void first_spawn_hides_current_action(const Options&) {
    ServerHost host;
    data(host, true);
    host.world.set_creature_behavior(false);
    FakeClient a(29000);
    enter(host, a, player(host));
    auto* body = creature(host);
    const auto id = body->id;
    host.steps(60, {&a});
    const auto numberA = interest(host, 1, id);
    check(numberA >= 2, "setup: the first session already observes the creature");
    const auto fromA = a.got.size();
    check(host.world.creatures().play(id, "Attack_Bite", 5.0F).error == world::CreatureError::none, "setup: a five-second action starts");
    host.steps(10, {&a});
    const auto current = fragments(a, fromA, numberA);
    check(!current.empty() && action(current.back()) == static_cast<int>(attackIndex), "control: the existing session gets the active action");
    FakeClient b(29001);
    enter(host, b, player(host, true));
    const auto numberB = interest(host, 2, id);
    const auto first = fragments(b, 0, numberB);
    check(numberB >= 2 && !first.empty() && action(first.front()) == static_cast<int>(spawnIndex),
          "setup: the later session starts the required spawn action");
    host.steps(200, {&a, &b});
    const auto later = fragments(b, 0, numberB);
    const auto currentKey = server::rep::creature_scope(body->creature, false).key;
    const auto sent = host.session(2).observer().known().at(id).scope;
    note("active action=" + std::to_string(body->creature.action ? body->creature.action->action : 999)
         + "; later-session movement states=" + std::to_string(later.size()) + "; retained scope=" + std::to_string(sent)
         + "; active scope=" + std::to_string(currentKey));
    check(body->creature.action && sent == currentKey && std::any_of(later.begin(), later.end(), [](const auto& state) {
              return action(state) == static_cast<int>(attackIndex);
          }), "the later session receives the active action after its required spawn record");
    host.steps(500, {&a, &b});
    const auto ended = fragments(b, 0, numberB);
    const auto attacks = std::count_if(ended.begin(), ended.end(), [](const auto& state) {
        return action(state) == static_cast<int>(attackIndex);
    });
    note("after the whole action: movement states=" + std::to_string(ended.size()) + "; attack records=" + std::to_string(attacks));
    check(!body->creature.action && attacks > 0, "the later session does not miss the whole five-second action");
}

void step_without_move_action(const Options&) {
    ServerHost host;
    data(host, false, true, false);
    host.world.set_creature_behavior(false);
    FakeClient client(29000);
    enter(host, client, player(host));
    auto* body = creature(host);
    const auto id = body->id;
    host.steps(60, {&client});
    const auto number = interest(host, 1, id);
    check(number >= 2 && !body->creature.body->moveAction, "setup: the session observes a supported slice without Move_Combat");
    check(host.world.creatures().hurt(id, 1.0F).error == world::CreatureError::none, "setup: a reaction starts");
    host.steps(3, {&client});
    check(body->creature.action && body->creature.action->action == reactionIndex, "setup: the creature is in its reaction");
    const auto from = client.got.size();
    host.world.creatures().move(*body, {1005.0F, 1000.0F, ground});
    host.steps(3, {&client});
    const auto moved = fragments(client, from, number);
    const auto sent = host.session(1).observer().known().at(id).scope;
    note("action after step=" + std::to_string(body->creature.action ? body->creature.action->action : 999)
         + "; movement states=" + std::to_string(moved.size())
         + "; last wire action=" + std::to_string(moved.empty() ? -3 : action(moved.back())));
    check(!body->creature.action && !moved.empty() && action(moved.back()) == -1 && sent == server::rep::kScopeNone,
          "a step ends the reaction even when the slice has no locomotion action");
}

void travel_before_first_camera(const Options&) {
    ServerHost host;
    data(host, true);
    host.world.set_creature_behavior(false);
    FakeClient client(29000);
    const auto owner = player(host);
    host.enter(client, owner.registration());
    const state::WorldPosition far{1400.0F, 1300.0F, ground};
    auto* body = creature(host, {1404.0F, 1300.0F, ground});
    const auto id = body->id;
    check(host.world.travel(owner.characterId, far).error == world::TravelError::none,
          "setup: a registered character travels before its first camera report");
    host.steps(1900, {&client});
    check(interest(host, 1, id) == 0, "control: the creature waits for the first 19 seconds");
    host.steps(150, {&client});
    const auto number = interest(host, 1, id);
    note("interest after 20.5 seconds with no camera=" + std::to_string(number));
    check(number >= 2, "the twenty-second travel fallback works without any previous camera report");
    host.datagram(client.peer, client.report_at(1, far.x, far.y, far.z));
    host.steps(60, {&client});
    check(interest(host, 1, id) >= 2, "control: a camera report admits the creature");
}

void cold_goal_near_tile_edge(const Options& options) {
    const auto folder = options.workDirectory / "review-cold-edge-goal";
    TestNavRegion region;
    region.tiles[{16, 15}] = [](int, int, std::vector<TestFloor>& floors) {
        for (unsigned index = 1; index < 16; ++index) floors.push_back({ground + index * 3.0F, 0});
    };
    region.write(folder);
    gamedata::NavigationData navigation;
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    check(store.open(":memory:") && navigation.set_directory(folder.string()), "setup: a navigation region loads");
    world::WorldOptions settings;
    settings.navigationData = &navigation;
    settings.navigationThread = true;
    world::World host(clock, store, tables, settings);
    const state::WorldPosition from{1023.0F, 1000.0F, ground};
    const state::WorldPosition goal{1024.7F, 1000.0F, ground};
    auto& nav = host.navigation();
    check(nav.next_step(1234, from, goal, 0.2F).kind == world::StepKind::waiting, "setup: the source tile gets a request");
    for (unsigned count = 0; count < 5000 && nav.footing(from) == world::Footing::unknown; ++count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        clock.advance(std::chrono::milliseconds(1));
        host.tick();
    }
    check(nav.footing(from) == world::Footing::walks && nav.footing(goal) == world::Footing::unknown,
          "setup: the source tile is ready while the adjacent goal tile is absent");
    const auto step = nav.next_step(1234, from, goal, 0.2F);
    note("step with a missing adjacent goal tile=" + std::to_string(static_cast<unsigned>(step.kind))
         + "; x=" + std::to_string(step.position.x));
    check(step.kind == world::StepKind::waiting, "a goal near a loaded tile cannot be snapped out of its missing tile");
    for (unsigned count = 0; count < 5000 && nav.footing(goal) == world::Footing::unknown; ++count) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        clock.advance(std::chrono::milliseconds(1));
        host.tick();
    }
    nav.forget(1234);
    check(nav.footing(goal) == world::Footing::walks && nav.next_step(1234, from, goal, 0.2F).kind == world::StepKind::moved,
          "control: the same goal has a route once its tile is ready");
}

void lock_controls(const Options&) {
    service::RepServerOptions settings;
    settings.observePlayers = true;
    ServerHost host(settings);
    FakeClient a(29000), b(29001);
    const auto ownerA = player(host);
    enter(host, a, ownerA);
    enter(host, b, player(host, true));
    const auto* own = host.world.find_character(ownerA.characterId);
    const auto id = own->id;
    const auto other = host.session(2).observer().own();
    const auto fromA = a.got.size(), fromB = b.got.size();
    const auto revision = host.world.revision();
    for (unsigned repeat = 0; repeat < 8; ++repeat) {
        host.datagram(a.peer, a.component_call(server::rep::kTypeCutsceneEnded, own->actor.bytes,
                                              id + server::rep::kPlayerCutsceneComponent, id));
        host.steps(1, {&a, &b});
    }
    const auto count = [](const FakeClient& client, std::size_t from, unsigned type) {
        return std::count_if(client.got.begin() + static_cast<std::ptrdiff_t>(from), client.got.end(),
                             [=](const auto& item) { return item.type == type; });
    };
    check(count(a, fromA, 6709) == 8 && count(b, fromB, 6709) == 0 && host.world.revision() == revision,
          "eight ends without server conversation state get eight owner replies and change no world state");
    const auto refused = a.got.size();
    host.datagram(a.peer, a.component_call(6705, own->actor.bytes, other + server::rep::kPlayerCutsceneComponent, other));
    host.datagram(a.peer, a.component_message(6705, own->actor.bytes, Bytes{1, 2, 3}));
    host.datagram(a.peer, a.component_call(2473, own->actor.bytes, id + server::rep::kInteractorComponent, id));
    host.steps(3, {&a, &b});
    check(count(a, refused, 6709) == 0 && count(a, refused, 3746) == 0 && host.world.revision() == revision,
          "a different entity, a short cutscene call, and a stateless interaction cancel have no reply or world change");
    host.disconnect(a.peer);
    host.steps(2, {&b});
    check(host.world.find_character(ownerA.characterId) == nullptr && host.world.find(other) != nullptr
              && count(b, fromB, 6709) == 0 && count(b, fromB, 3746) == 0,
          "ending the requesting session removes its character and sends no lock reply to a different owner");
}

void mana_and_second_travel_controls(const Options&) {
    service::RepServerOptions settings;
    settings.observePlayers = true;
    ServerHost host(settings);
    data(host, true);
    host.world.set_creature_behavior(false);
    FakeClient a(29000), b(29001);
    const auto ownerA = player(host);
    enter(host, a, ownerA);
    enter(host, b, player(host, true));
    auto* own = host.world.find_character(ownerA.characterId);
    const auto ownId = own->id;
    const auto mana = fragments(a, 0, 1, 47);
    const auto value = [](const Fragment& part, unsigned group, unsigned field) {
        server::hub::Reader reader(part.fields.at({group, field}));
        return reader.f32();
    };
    check(!host.progression.loaded() && mana.size() == 1 && mana.front().type == 1652 && mana.front().fields.size() == 4
              && value(mana.front(), 0, 0) == 100.0F && value(mana.front(), 0, 1) == 100.0F
              && value(mana.front(), 0, 2) == 0.0F && value(mana.front(), 0, 3) == 0.0F,
          "without progression tables the owner receives mana 100 of 100 with zero rates");
    const auto ownForB = interest(host, 2, ownId);
    check(ownForB >= 2 && fragments(b, 0, ownForB, 47).empty(), "the nearby observer receives no mana component");
    auto from = a.got.size();
    own->vitals.mana = 25.0F;
    host.world.touch(ownId, world::ComponentKey::vitals);
    host.world.touch(ownId, world::ComponentKey::mana);
    host.steps(3, {&a, &b});
    const auto changedMana = fragments(a, from, 1, 47);
    const auto changedVitals = fragments(a, from, 1, 41);
    check(changedMana.size() == 1 && !changedVitals.empty() && value(changedMana.front(), 0, 0) == 25.0F
              && value(changedVitals.front(), 1, 0) == 25.0F && fragments(b, 0, ownForB, 47).empty(),
          "a marked mana change keeps both owner states equal and sends no mana state to the observer");
    const state::WorldPosition first{1400.0F, 1300.0F, ground}, second{1800.0F, 1300.0F, ground};
    const auto firstId = creature(host, {1404.0F, 1300.0F, ground})->id;
    const auto secondId = creature(host, {1804.0F, 1300.0F, ground})->id;
    check(host.world.travel(ownerA.characterId, first).error == world::TravelError::none, "setup: the first travel starts");
    host.steps(1900, {&a, &b});
    check(host.world.travel(ownerA.characterId, second).error == world::TravelError::none, "setup: another travel replaces the pending place");
    host.steps(300, {&a, &b});
    check(interest(host, 1, firstId) == 0 && interest(host, 1, secondId) == 0,
          "the old twenty-second deadline does not admit creatures after a second travel");
    host.datagram(a.peer, a.report_at(2, first.x, first.y, first.z));
    host.steps(3, {&a, &b});
    check(interest(host, 1, secondId) == 0 && world::horizontal_distance(own->position, second) < 0.1F,
          "a report from the first place cannot confirm the second travel");
    host.datagram(a.peer, a.report_at(3, second.x, second.y, second.z));
    host.steps(3, {&a, &b});
    check(interest(host, 1, secondId) >= 2 && interest(host, 1, firstId) == 0,
          "the report from the latest place admits its creature");
}

Fragment decode_movement(const Bytes& bytes) {
    Fragment part;
    part.type = 11;
    std::size_t consumed = 0;
    if (!detail::read_state(bytes, consumed, part) || consumed != bytes.size()) throw std::runtime_error("review: movement state did not decode");
    return part;
}

void scope_controls(const Options&) {
    ServerHost host;
    data(host);
    host.world.set_creature_behavior(false);
    auto* body = creature(host);
    auto& system = host.world.creatures();
    const auto encode = [&](world::Revision since, std::uint32_t key) {
        return decode_movement(server::rep::encode_creature_movement_state(*body, host.clock, since, key));
    };
    check(action(encode(0, server::rep::kScopeUnknown)) == -1, "a first rest record has explicit empty scope fields");
    system.hurt(body->id, 1.0F);
    const auto reaction = server::rep::creature_scope(body->creature, false).key;
    check(action(encode(0, server::rep::kScopeUnknown)) == static_cast<int>(reactionIndex), "a first reaction record has the current reaction");
    check(action(encode(host.world.revision(), reaction)) == -2, "an unchanged reaction scope is omitted for a session that already has it");
    system.play(body->id, "Attack_Bite", 0.01F);
    host.steps(3, {});
    check(!body->creature.action && action(encode(1, server::rep::kScopeNone)) == -2,
          "an action that starts and ends before a session sees it needs no clear for that session");
    check(action(encode(1, reaction)) == -1, "the same end clears a different session's older kept reaction");
    system.move(*body, {1005.0F, 1000.0F, ground});
    check(action(encode(0, server::rep::kScopeUnknown)) == static_cast<int>(moveIndex), "a first way record has locomotion without a spawn requirement");
    system.stop(*body);
    check(action(encode(1, 0x10000U | body->creature.actions)) == -1, "the home stop has explicit empty scope fields");
    system.play(body->id, "Attack_Bite", 2.0F);
    host.advance(std::chrono::milliseconds(500));
    const auto attack = encode(0, server::rep::kScopeUnknown);
    check(action(attack) == static_cast<int>(attackIndex) && attack.fields.at({0, 28})[0] == 15,
          "a first attack record without a spawn requirement uses its current elapsed time");
    system.kill(body->id);
    check(action(encode(0, server::rep::kScopeUnknown)) == 2, "a first dead record has the death action");
    server::rep::Observer observer;
    auto number = observer.add(body->id);
    observer.find(body->id)->scope = 65539;
    observer.remove(body->id);
    observer.release(number);
    observer.add(body->id);
    check(observer.find(body->id)->scope == server::rep::kScopeUnknown && observer.find(body->id)->sent == 0,
          "leaving and re-entering interest discards the old scope and revision");
}

void stale_spawn_timing_contract(const Options&) {
    ServerHost host;
    data(host, true);
    host.world.set_creature_behavior(false);
    auto* body = creature(host);
    host.advance(std::chrono::seconds(80));
    const auto first = decode_movement(server::rep::encode_creature_movement_state(*body, host.clock, 0));
    check(action(first) == static_cast<int>(spawnIndex) && body->creature.spawnAction->started == body->creature.madeAt,
          "control: the first record has the required spawn action and its stored origin is creation time");
    std::uint32_t ticks = 0;
    const unsigned fields[] = {28, 38, 49, 50};
    for (unsigned index = 0; index < 4; ++index) ticks |= static_cast<std::uint32_t>(first.fields.at({0, fields[index]})[0]) << (index * 8);
    note("wire spawn time=" + std::to_string(ticks) + "; time from creation=2400");
    // The review (finding F9) found the old description (the time from the creation) in
    // AGENTS.md and entity.h: the two now say the time 0 of the wire, which this case checks.
    check(ticks == 0, "the spawn action of a first record has the time 0 on the wire, as AGENTS.md and entity.h say");
}

} // namespace

std::vector<Case> review_corrections_cases() {
    return {{"RU1", "review: a short line across a cell edge cannot cross a ceiling", ceiling_at_cell_edge},
            {"RU2", "review: death without a death action ends a kept locomotion scope", death_ends_locomotion},
            {"RU3", "review: a first spawn record is followed by the current action", first_spawn_hides_current_action},
            {"RU4", "review: a step ends a reaction without Move_Combat", step_without_move_action},
            {"RU5", "review: travel before the first camera still has a twenty-second fallback", travel_before_first_camera},
            {"RU6", "review: a missing goal tile still waits near a loaded tile edge", cold_goal_near_tile_edge},
            {"RU7", "review controls: repeated input locks, wrong owners, and disconnect", lock_controls},
            {"RU8", "review controls: mana without tables and two pending travels", mana_and_second_travel_controls},
            {"RU9", "review controls: first scope phases, skipped actions, and interest reset", scope_controls},
            {"RU10", "review: the spawn action of a first record has the time 0 (the documents say so)", stale_spawn_timing_contract}};
}

} // namespace soulwarden::backend::tests
