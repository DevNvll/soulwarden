// Cases of the code review of stage 3.4b (a creature fights, and a character can die). The
// review found nine faults; stage 3.4d corrected them. Each case fails with no correction:
//
//   RF1   a dead character keeps the place of its death: its client cannot move it (finding F1)
//   RF2   a creature with a spawn action and no death action that dies in its reaction: a new
//         observer gets the spawn action at once (finding F2)
//   RF3   a creature file whose damage values give a product that is not finite is refused (F3)
//   RF4   a contact of an attack hurts only the character that the attack started at (F4)
//   RF5   an attack whose length is 0 on the world clock is refused (F5)
//   RF6   a second session that takes a dead character over gets the death notice (F6)
//   RF7   a height file with a scale that is too large gives no ground (F7)
//   RF8   a creature of a spawn point with no ground attacks a character in its reach (F8)
//   RF9   controls: the life of a contact (a clock jump, a creature or a target that is gone)
//
// Finding F9 (the terrain tool at the east edge of the world) has its check in
// tools/gamedata/check_heights_tool.py, which case RF10 runs.
//
// All ids, entities, rows, tables, and height samples are made up.
#include <cmath>
#include <cstdlib>
#include <cstring>
#include "middleware/compression/zlib.h"
#include <filesystem>
#include <fstream>
#include <limits>
#include <nlohmann/json.hpp>
#include "server/rep/health_messages.h"
#include "server/rep/states/state_codecs.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {
namespace {
using nlohmann::json;
constexpr const char* characterA = "aaaaaaaa-0000-4000-8000-00000000000a";
constexpr const char* characterB = "bbbbbbbb-0000-4000-8000-00000000000b";
constexpr state::WorldPosition place{1000.0F, 1000.0F, 50.0F};

struct Sink final : world::SessionSink {
    unsigned deaths{0};
    void character_lost(world::LostCause) override {}
    void died(float) override { ++deaths; }
};

gamedata::CreatureSlice slice(bool spawned = false) {
    gamedata::CreatureSlice s;
    s.path = "slices/review.dynamicslice";
    s.asset.fill(0x11);
    s.subId = 2;
    s.root = 0x1000000000000011ULL;
    s.interestRadius = 60.0F;
    s.movementSlot = 5;
    s.vitalsSlot = 9;
    s.slots = {{5, "ActionList", 11}, {9, "Vitals", 15}};
    s.actions = {{"Reaction", "Reaction", 1, true}, {"Attack", "Attack", 2, false}, {"Spawn", "Spawn", 3, false}};
    s.behavior = gamedata::CreatureBehavior{180.0F, 12.0F, 180.0F, 30.0F, 20.0F, 70.0F, 4.0F, 10.0F};
    gamedata::CreatureContact c;
    c.time = 0.5F;
    c.reach = 2.2F;
    c.factor = 1.0F;
    c.type = "Slash";
    c.wire = 4;
    c.category = gamedata::DamageCategory::physical;
    c.row = "review_contact";
    s.attacks = {{1, 1.0F, {c}}};
    if (spawned) s.spawnActionName = "Spawn";
    return s;
}

gamedata::CreatureKind kind() {
    gamedata::CreatureKind k;
    k.id = "review_kind";
    k.vitals = "review_vitals";
    k.name = "review_kind";
    k.healthFactor = 1.0F;
    k.level = 1;
    k.bodies = {{0, 100}};
    return k;
}

bool data(ServerHost& h, gamedata::CreatureSlice s = slice(), float base = 10.0F) {
    h.terrain.source = [](float, float) { return std::optional<float>(50.0F); };
    return h.creatures.set_full({s}, {kind()}, {}, {{1, 1000.0F, base, 60.0F}});
}

Player player(ServerHost& h, const char* id, const std::string& identity) {
    return add_player(h.store, identity, "", id, id, place);
}

world::Entity* enter(ServerHost& h, Sink& sink, const Player& p, unsigned session) {
    return h.world.enter(session, sink, p.characterId, p.account.personaId).entity;
}

int action_of(const world::Entity& entity, const world::WorldClock& clock, world::Revision since = 0) {
    const Bytes bytes = server::rep::encode_creature_movement_state(entity, clock, since);
    Fragment f;
    f.type = 11;
    std::size_t position = 0;
    if (!detail::read_state(bytes, position, f)) throw std::runtime_error("movement decode failed");
    const auto found = f.fields.find({0, 33});
    if (found == f.fields.end()) return -1;
    server::hub::Reader reader(found->second);
    reader.size(); reader.u16(); reader.size();
    return static_cast<int>(reader.size()) - 1;
}

json document(float base = 10.0F) {
    auto d = json::parse(R"({
      "format":2,"levels":[[1,1000,10,60]],"spawnPoints":[],
      "slices":[{"path":"slices/review.dynamicslice","asset":"11111111-1111-1111-1111-111111111111",
        "sub":2,"root":"1000000000000011","interestRadius":60,"movement":5,"vitals":9,"marker":0,
        "slots":[[5,"ActionList",11],[9,"Vitals",15]],"components":[],"actions":[["Attack","Attack",2,false]],
        "behavior":{"sight":[180,12],"combatSight":[180,30],"leash":[20,70],"speed":[4,10]},
        "attacks":[[0,1,[[0.5,2.2,10,0,"Slash",4,"Physical","review_contact"]]]]}],
      "kinds":[{"id":"review_kind","vitals":"review_vitals","category":"review_category","vitalsId":1,
        "categoryId":2,"name":"review_kind","vitalsName":"review_kind","family":"review_family",
        "creatureType":"Solo","health":1,"damage":10,"level":1,"bodies":[[0,100]]}]
    })");
    d["levels"][0][2] = base;
    return d;
}

void dead_position(const Options&) {
    ServerHost h;
    Sink sink;
    auto p = player(h, characterA, kSteamIdA);
    auto* e = enter(h, sink, p, 1);
    check(e != nullptr, "setup: a character entered");
    world::PositionTrack track;
    check(h.world.report_position(1, {1, place, 10000}, track) == world::PositionVerdict::accepted, "setup: position anchor accepted");
    check(h.places.set({{"near_death", "shrine", "", "", 1, {1000, 1000, 50}},
                       {"near_moved_corpse", "shrine", "", "", 1, {1020, 1000, 50}}}), "setup: two respawn places");
    h.world.health().hurt(characterA, 1000000.0F);
    check(e->life.dead && e->position.x == place.x, "setup: death at the original place");
    for (unsigned input = 2; input <= 21; ++input) {
        h.clock.advance(std::chrono::milliseconds(100));
        h.world.report_position(1, {static_cast<std::uint16_t>(input), {1000.0F + static_cast<float>(input - 1), 1000, 50}, 10000}, track);
    }
    note("corpse x=" + std::to_string(e->position.x) + "; death x=1000; accepted reports=" + std::to_string(track.count(world::PositionVerdict::accepted)));
    check(e->position.x == place.x, "a dead character keeps the death position despite client movement reports");
    auto result = h.world.health().respawn(1);
    note("respawn x=" + std::to_string(e->position.x));
    check(result.revived && e->position.x == 1000.0F, "respawn uses the shrine nearest the death position");
}

void immediate_dead_spawn(const Options&) {
    ServerHost h;
    check(data(h, slice(true)), "setup: a spawn-action slice without a death action loads");
    auto* c = h.world.creatures().spawn("review_kind", place, 0).entity;
    check(c != nullptr && c->creature.spawnAction, "setup: the creature has its spawn action");
    h.world.creatures().hurt(c->id, 1.0F);
    h.world.creatures().hurt(c->id, 1000000.0F);
    // Before the correction the reaction stayed as the action of the body for 1.5 s.
    check(c->creature.dead && !c->creature.action, "the death of a creature with no death action ends the reaction that played");
    const int sent = action_of(*c, h.clock);
    note("first observer action=" + std::to_string(sent) + "; required Spawn=2");
    check(sent == 2, "a new observer gets Spawn immediately for a corpse with no death action");
    h.clock.advance(std::chrono::milliseconds(1600)); h.world.tick();
    check(action_of(*c, h.clock) == 2, "control: after the reaction expires the first state does contain Spawn");
}

void overflow_damage(const Options& o) {
    const auto path = o.workDirectory / "review-overflow.json";
    auto d = document();
    std::ofstream(path) << d.dump();
    gamedata::CreatureData control;
    check(control.load(path.string()), "control: ordinary format-2 damage loads");
    d["levels"][0][2] = 1.0e38;
    std::ofstream(path) << d.dump();
    ServerHost h;
    const bool loaded = h.creatures.load(path.string());
    note("overflow file loaded=" + std::to_string(loaded));
    check(!loaded && !h.creatures.loaded(), "a damage product outside the finite range refuses the whole file");
    if (!loaded) return;
    auto p = player(h, characterA, kSteamIdA);
    (void)h.world.spawn_character(p.characterId, place);
    auto* e = h.world.find_character(p.characterId);
    auto* c = h.world.creatures().spawn("review_kind", {1001, 1000, 50}, 90).entity;
    const auto damage = h.world.combat().creature_damage(*c, c->creature.body->attacks[0].contacts[0], *e);
    note("base finite=" + std::to_string(std::isfinite(damage.base)) + "; dealt=" + std::to_string(damage.dealt));
    check(std::isfinite(damage.base) && damage.dealt > 0, "loaded positive damage does not overflow and turn into zero damage");
}

void retarget_contact(const Options&) {
    ServerHost h;
    Sink a, b;
    check(data(h), "setup: ordinary attack data loads");
    auto pa = player(h, characterA, kSteamIdA);
    auto pb = player(h, characterB, kSteamIdB);
    auto* first = enter(h, a, pa, 1);
    auto* second = enter(h, b, pb, 2);
    auto* c = h.world.creatures().spawn("review_kind", {1001, 1000, 50}, 90).entity;
    h.world.tick();
    check(c->creature.mind.attack && c->creature.mind.target == first->id, "setup: attack started at the first character");
    h.clock.advance(std::chrono::milliseconds(100));
    h.world.health().hurt(characterA, 1000000.0F);
    const float before = second->vitals.health;
    h.world.creatures().hurt(c->id, 1.0F, second->id);
    note("target changed before the contact=" + std::to_string(c->creature.mind.target == second->id));
    h.clock.advance(std::chrono::milliseconds(400)); h.world.tick();
    note("second health before=" + std::to_string(before) + "; after old contact=" + std::to_string(second->vitals.health) +
         "; attacks started=" + std::to_string(c->creature.mind.attacks));
    check(second->vitals.health == before, "a pending contact of the old attack does not switch to a new target");
}

void zero_time_contact(const Options& o) {
    auto d = document();
    d["slices"][0]["attacks"][0][1] = 0.0001;
    d["slices"][0]["attacks"][0][2][0][0] = 0.0;
    const auto path = o.workDirectory / "review-short-attack.json";
    std::ofstream(path) << d.dump();
    ServerHost h;
    const bool loaded = h.creatures.load(path.string());
    note("positive sub-millisecond attack loaded=" + std::to_string(loaded));
    if (!loaded) {
        check(!h.creatures.loaded() && !h.creatures.error().empty(), "a sub-millisecond attack can be refused as unusable data");
        return;
    }
    check(loaded, "setup: positive sub-millisecond attack with a contact at zero loads");
    h.terrain.source = [](float, float) { return std::optional<float>(50); };
    auto p = player(h, characterA, kSteamIdA);
    (void)h.world.spawn_character(p.characterId, place);
    auto* e = h.world.find_character(p.characterId);
    auto* c = h.world.creatures().spawn("review_kind", {1001, 1000, 50}, 90).entity;
    const float before = e->vitals.health;
    h.world.tick();
    check(c->creature.mind.attacks == 1, "setup: one attack started");
    h.clock.advance(std::chrono::milliseconds(10)); h.world.tick();
    note("contact hits=" + std::to_string(h.world.creatures().behavior().counts().contactsHit) + "; health=" + std::to_string(e->vitals.health));
    check(e->vitals.health < before, "a zero-time contact in an accepted attack causes damage");
}

void height_file(const std::filesystem::path& path, float scale, unsigned fault = 0) {
    auto u32 = [](Bytes& b, std::uint32_t n) {
        for (unsigned i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(n >> (8 * i)));
    };
    Bytes bytes{'S', 'W', 'H', 'T'};
    for (std::uint32_t n : {1U, 0U, 0U, 2048U, 64U}) u32(bytes, n);
    std::uint32_t bits;
    std::memcpy(&bits, &scale, sizeof(bits));
    u32(bytes, bits); u32(bytes, 0);
    Bytes plain(65 * 65 * 2 + (fault == 2 ? 2 : 0), 0);
    plain[0] = 255; plain[1] = 255;
    const Bytes stream = compression::zlib_store(plain);
    for (unsigned i = 0; i < 1024; ++i) {
        u32(bytes, fault == 1 ? 0xFFFFFFFFU : 32 + 1024 * 8);
        u32(bytes, static_cast<std::uint32_t>(stream.size()));
    }
    bytes.insert(bytes.end(), stream.begin(), stream.end());
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}

void malformed_height_scale(const Options& o) {
    const auto folder = o.workDirectory / "review-heights";
    std::filesystem::create_directories(folder);
    const auto path = folder / "r_+00_+00.swh";
    gamedata::HeightMaps maps;
    height_file(path, 2048);
    check(maps.set_directory(folder.string()) && maps.height(1, 1) && *maps.height(1, 1) == 2048,
          "control: a normal height file gives a finite height");
    height_file(path, 2048, 1); maps.set_directory(folder.string());
    check(!maps.height(1, 1), "control: a tile index past the file gives no height");
    height_file(path, 2048, 2); maps.set_directory(folder.string());
    check(!maps.height(1, 1), "control: a zlib tile with too much plain data gives no height");
    height_file(path, 1.0e38F); maps.set_directory(folder.string());
    const auto ground = maps.height(1, 1);
    note("malformed scale: height present=" + std::to_string(ground.has_value()) + "; finite=" +
         std::to_string(ground && std::isfinite(*ground)));
    check(!ground, "a file with a scale that overflows gives no ground height");
}

void lifecycle_controls(const Options&) {
    {
        ServerHost h;
        check(data(h), "setup: attack data for a clock jump");
        auto p = player(h, characterA, kSteamIdA);
        (void)h.world.spawn_character(p.characterId, place);
        auto* e = h.world.find_character(p.characterId);
        auto* c = h.world.creatures().spawn("review_kind", {1001, 1000, 50}, 90).entity;
        const float before = e->vitals.health;
        h.world.tick(); h.clock.advance(std::chrono::seconds(3)); h.world.tick();
        check(e->vitals.health == before - 10 && h.world.creatures().behavior().counts().contactsHit == 1,
              "a long tick applies the due contact once before the action expires");
        (void)c;
    }
    for (unsigned fault = 0; fault < 3; ++fault) {
        ServerHost h;
        check(data(h), "setup: attack data for a removed or dead participant");
        Sink sink;
        auto p = player(h, characterA, kSteamIdA);
        auto* e = enter(h, sink, p, 1);
        const auto oldId = e->id;
        const auto oldActor = e->actor;
        auto* c = h.world.creatures().spawn("review_kind", {1001, 1000, 50}, 90).entity;
        h.world.tick();
        if (fault == 0) h.world.creatures().remove(c->id);
        else if (fault == 1) h.world.creatures().hurt(c->id, 1000000.0F);
        else {
            h.world.leave(1);
            e = enter(h, sink, p, 2);
            check(e->id != oldId && e->actor.bytes != oldActor.bytes, "a character's new entry has new entity and actor ids");
        }
        const float before = e->vitals.health;
        h.clock.advance(std::chrono::milliseconds(500)); h.world.tick();
        check(e->vitals.health == before && h.world.creatures().behavior().counts().contactsHit == 0,
              "the pending contact skips a removed creature, dead creature, or old target entity");
    }
    {
        ServerHost h;
        auto s = slice(); s.attacks[0].contacts.push_back(s.attacks[0].contacts[0]);
        check(data(h, s), "setup: two contacts at the same time");
        auto p = player(h, characterA, kSteamIdA);
        (void)h.world.spawn_character(p.characterId, place);
        auto* e = h.world.find_character(p.characterId);
        h.world.creatures().spawn("review_kind", {1001, 1000, 50}, 90);
        const float before = e->vitals.health;
        h.world.tick(); h.clock.advance(std::chrono::milliseconds(500)); h.world.tick();
        // A rule of stage 3.4d (gamedata::kContactGap): two contacts of one attack at the same
        // time are the two hit volumes of one swing. The review case had two damage results here.
        check(e->vitals.health == before - 10 && h.world.creatures().behavior().counts().contactsHit == 1,
              "two contacts of one attack at the same time are one hit: one damage result");
        h.world.health().hurt(characterA, 1000000.0F);
        check(h.world.health().hurt(characterA, 1).error == world::HealthError::dead && e->vitals.health == 0,
              "damage to a dead character changes nothing");
        h.world.health().heal(characterA);
        check(!e->life.dead && e->vitals.health == e->vitals.healthMax, "heal revives through the health system");
    }
}

void missing_terrain_at_spawn(const Options&) {
    ServerHost h;
    gamedata::SpawnPoint point;
    point.id = "review_point";
    point.kind = 0;
    point.level = 1;
    point.position = {1002.0F, 1000.0F, 50.0F};
    point.yaw = 90;
    point.ground = 50;
    check(h.creatures.set_full({slice()}, {kind()}, {point}, {{1, 1000, 10, 60}}), "setup: format-2 spawn data says the point is on terrain");
    // The backend has no height files at this place. TestTerrain gives no height.
    auto p = player(h, characterA, kSteamIdA);
    (void)h.world.spawn_character(p.characterId, place);
    auto* e = h.world.find_character(p.characterId);
    const float before = e->vitals.health;
    for (unsigned i = 0; i < 200; ++i) {
        h.clock.advance(std::chrono::milliseconds(10)); h.world.tick();
    }
    const auto& behavior = h.world.creatures().behavior();
    check(h.world.creatures().count() == 1 && behavior.counts().targetsBySight == 1, "setup: one spawn-point creature saw the character at 2 m");
    note("no terrain: attacks=" + std::to_string(behavior.counts().attacks) + "; contact hits=" +
         std::to_string(behavior.counts().contactsHit) + "; character health=" + std::to_string(e->vitals.health));
    check(behavior.counts().attacks > 0 && e->vitals.health < before,
          "a creature with no terrain source stands and attacks a target within its 2.2 m reach");
}

void enter_client(ServerHost& h, FakeClient& c, const Player& p) {
    h.enter(c, p.registration());
    for (std::uint16_t i = 1; i <= 20; ++i) {
        h.clock.advance(std::chrono::microseconds(33333));
        h.datagram(c.peer, c.report_at(i, place.x, place.y, place.z));
        h.tick(); c.exchange(h);
    }
}

void dead_session_replacement(const Options&) {
    ServerHost h;
    auto p = player(h, characterA, kSteamIdA);
    FakeClient a(27000), b(27001);
    enter_client(h, a, p);
    auto* e = h.world.find_character(characterA);
    check(e != nullptr && e->owner == 1, "setup: the first client owns the character");
    h.world.health().hurt(characterA, 1000000.0F);
    h.steps(3, {&a});
    check(e->life.dead, "setup: the first client died");
    enter_client(h, b, p);
    h.steps(3, {&b});
    e = h.world.find_character(characterA);
    unsigned notices = 0, deadStates = 0;
    for (const auto& m : b.got) {
        notices += m.type == server::rep::kTypePlayerDeath;
        if (m.type != 8) continue;
        for (const auto& r : read_bundle(m.body).records) {
            if (r.interest != 1) continue;
            for (const auto& f : r.fragments) {
                auto life = f.fields.find({0, 2});
                if (f.slot == 41 && life != f.fields.end() && !life->second.empty() && (life->second[0] & 1)) ++deadStates;
            }
        }
    }
    check(e != nullptr && e->owner == 2 && (!e->life.dead || deadStates > 0), "setup: replacement client received the character state");
    note("replacement dead states=" + std::to_string(deadStates) + "; death notices=" + std::to_string(notices));
    check((!e->life.dead && e->vitals.health == e->vitals.healthMax) || (e->life.dead && notices > 0),
          "a client entering a dead entity receives the death notice or starts alive with full health");
}
/** RF10: the check of the terrain height tool (finding F9). */
void height_tool_edge(const Options& options) {
    // The golden folder of the options is REPOSITORY/apps/backend/tests/golden.
    const std::filesystem::path script =
        options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_heights_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the terrain height tool is in the tree (the case needs --golden-dir)");
    if (!std::filesystem::is_regular_file(script)) {
        return;
    }
    std::filesystem::create_directories(options.workDirectory);
    const auto output = options.workDirectory / "heights-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + options.workDirectory.string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    unsigned correct = 0;
    unsigned wrong = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 160));
        correct += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        wrong += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && correct == 6 && wrong == 0,
          "the height for a spawn point and the height of the file are equal in the middle of a region, at its east edge and at its "
          "north edge with no region there, and at its corner; and the tool takes the terrain after the cookies when it is there");
    std::filesystem::remove(output);
}
}
std::vector<Case> review_fight_cases() {
    return {{"RF1", "review of 3.4b: a dead character keeps the place of its death, for each report of its client", dead_position},
            {"RF2", "review of 3.4b: a new observer of a creature that died in its reaction gets the spawn action at once", immediate_dead_spawn},
            {"RF3", "review of 3.4b: a creature file whose damage product is not finite is refused", overflow_damage},
            {"RF4", "review of 3.4b: a contact of an attack does not hurt a different character", retarget_contact},
            {"RF5", "review of 3.4b: an attack with a length of 0 on the world clock is refused", zero_time_contact},
            {"RF6", "review of 3.4b: a second session of a dead character gets the death notice", dead_session_replacement},
            {"RF7", "review of 3.4b: a height file with a scale that is too large gives no ground", malformed_height_scale},
            {"RF8", "review of 3.4b: a creature of a spawn point with no ground attacks in its reach", missing_terrain_at_spawn},
            {"RF9", "review of 3.4b, controls: the life of a contact with a clock jump and with a creature or a target that is gone",
             lifecycle_controls},
            {"RF10", "review of 3.4b: the terrain height tool at the edge of the world, and the terrain after the cookies", height_tool_edge}};
}
}
