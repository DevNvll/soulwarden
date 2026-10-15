// Cases of stage 3.3b-S4: the forced movement of a creature (the pull and the push of a hit, world/forced_motion.h), the
// movement of the caster of a dash (CasterMotion, World::report_position, the state `motion` of the observers), and the
// counter that the server starts (Riposte, Abilities::countered). The damage rows have made-up impacts with the values of
// the rule of the game data (Maelstrom: Y -1.5 and the decay rate 0.8; the vortex: Y -7 and no decay rate). All ids, names,
// and values are made up.
//
//   FM1  the impacts of the combat tables: the member `impacts`, a file with none, a row that is not there, a decay above 1
//   FM2  the rule of a decay rate (client code 0x48A0D30): the travel of the time of the reaction, toward the source, the
//        steps on the world clock, the movement state of each step, the "_AI" values for a creature
//   FM3  the rule of no decay rate (client code 0x488B8F0): 7 m/s to the point and a stop 0.1 m from it; away from it
//   FM4  the navigation: a wall and a drop stop the movement; the height of the floor
//   FM5  the rules: no movement of a creature on its way back or of a dead creature, a new impact replaces the movement, the
//        behavior waits for its end and goes on after it
//   FM6  the abilities: the vortex of Gravity Well pulls to its center and the burst pushes; Path of Destiny pushes from the
//        caster (UseAttackerPosForReaction); Wrecking Ball moves nothing
//   FM7  two sessions (MP-10): each one gets the movement of the creature; a dash of B is observed by A, and B gets no
//        position of the server; each one keeps its own position and resources
//   FM8  the load: 200 moving creatures near the characters in a world of 4,200 creatures, with a time limit
//   CM1  the envelope of a dash: the position check accepts the dash of Charge in the cast, not without it, and not after it
//   CM2  the dash of the server for a character with no client: the line, the creature in front, the wall, the observer
//   RC1  the counter of Riposte: the window takes the hit, the server starts the counter, one hit with the stun and the push
//   RC2  a refused counter: a hit after the window, a press in the cooldown, and two sessions

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "support/cases.h"
#include "support/check.h"
#include "support/shop_fixture.h"
#include "support/test_navigation.h"
#include "world/forced_motion.h"

namespace soulwarden::backend::tests {
namespace {

using namespace shop;
using std::chrono::milliseconds;

const std::uint32_t kRapier = gamedata::progression_id("RapierAbilityTable");
const std::uint32_t kRiposte = gamedata::progression_id("Ability_Rapier_Riposte");
const std::uint32_t kCharge = gamedata::progression_id("Ability_Greataxe_Charge");
const std::uint32_t kMaelstrom = gamedata::progression_id("Ability_Greataxe_Maelstorm");
const std::uint32_t kGravityWell = gamedata::progression_id("Ability_Greataxe_GravityWell");

/** An impact of the form of the tool: the normal values, and the values for a creature (0: none). */
gamedata::DamageImpact impact_of(float y, float decay, float creatureY = 0.0F, bool fromAttacker = false) {
    gamedata::DamageImpact impact;
    impact.distance = {0.0F, y, 0.0F};
    impact.decay = decay;
    impact.creatureDistance = {0.0F, creatureY, 0.0F};
    impact.fromAttacker = fromAttacker;
    return impact;
}

/** The combat tables of the cases: the tables of the shop with impacts, the rapier and its counter row. */
gamedata::CombatData motion_combat() {
    gamedata::CombatData data = shop_combat();
    const auto add = [&](const char* id, std::uint32_t type, float factor, std::optional<gamedata::DamageImpact> impact) {
        gamedata::DamageRow row{id, "Ability", type, factor, 0.0F};
        row.impact = impact;
        data.damageRows.push_back(row);
    };
    add("Test_Pull", 1, 0.1F, impact_of(-1.5F, 0.8F));
    add("Test_PullAI", 1, 0.1F, impact_of(0.5F, 0.85F, 1.0F));
    add("Test_Vortex", 3, 0.0F, impact_of(-7.0F, 0.0F));
    add("Test_Out", 3, 0.0F, impact_of(7.0F, 0.0F));
    add("Test_Push", 1, 0.1F, impact_of(3.0F, 0.9F));
    add("1H_Rapier_Riposte_Stun", 2, 0.5F, impact_of(1.5F, 0.85F));
    add("GreatAxe_Engage1", 1, 1.2F, impact_of(0.25F, 0.85F));
    for (gamedata::DamageRow& row : data.damageRows) {
        if (row.id == "GreatAxe_GravityWell_Vortex") {
            row.impact = impact_of(-7.0F, 0.0F);
        } else if (row.id == "GreatAxe_GravityWell_Burst") {
            row.impact = impact_of(0.5F, 0.3F);
        } else if (row.id == "Warhammer_Damage_PathOfDestiny") {
            row.impact = impact_of(2.0F, 0.9F, 0.0F, true);
        }
    }
    gamedata::WeaponRow rapier = data.weapons.front();
    rapier.id = "TestRapierRow";
    rapier.mastery = "RapierAbilityTable";
    data.weapons.push_back(rapier);
    data.items["testrapier"] = static_cast<std::uint32_t>(data.weapons.size() - 1);
    return data;
}

/** The ability data of the shop with the rows of Gravity Well and of Riposte (the values of the game data). */
gamedata::AbilityTables motion_abilities() {
    gamedata::AbilityTables tables = shop_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    gamedata::StatusRow root = status("GreatAxe_Vortex_Root", 1.0F, {}, true);
    root.root = true;
    rows.push_back(root);
    gamedata::StatusRow slow = status("GreatAxe_Vortex", 3.0F, {});
    slow.moveSpeed = -0.6F;
    rows.push_back(slow);
    // Rapier_Riposte_Timer: 1 s, the absorption 2 of each damage type (game data); Rapier_Riposte_Stun: 2 s, a stun that the
    // next hit ends.
    rows.push_back(status("Rapier_Riposte_Timer", 1.0F, cells({{"ABSSlash", 2.0F}, {"ABSStrike", 2.0F}, {"ABSStandard", 2.0F}})));
    gamedata::StatusRow stun = status("Rapier_Riposte_Stun", 2.0F, {}, true, 1);
    stun.stunned = true;
    rows.push_back(stun);
    tables.statusEffects = numbered(std::move(rows));
    tables.cooldowns.push_back({"Ability_Rapier_Riposte", 18.0F, 0.0F});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Projectile", 0.0F, 0.0F, 10.0F, 0, 0.0F, 2000.0F, {}});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    tables.spells.push_back({"Spell_GreatAxe_GravityWell_Vortex_Upgrade", 4.0F, 0.0F, 0.0F, 0, 0.0F, -1.0F, {}});
    return tables;
}

/** The world of a case: the shop with the impacts, the rapier, Riposte, and the nodes and bars of the three weapons. */
struct MotionShop : Shop {
    explicit MotionShop(bool observe = false) : Shop(false, false, observe) {
        if (!host.combat.set(motion_combat())) {
            throw std::runtime_error("motion test: the combat tables are not valid: " + host.combat.error());
        }
        std::vector<gamedata::ItemDefinition> items = shop_catalog();
        items.push_back(definition("TestRapier", "Weapon", "Weapon+EquippableMainHand+Rapier+Melee", 5, 100, 600));
        host.catalog.set(items);
        if (!host.abilities.set(motion_abilities())) {
            throw std::runtime_error("motion test: the ability data is not valid: " + host.abilities.error());
        }
        give_skills(*character());
    }
    /** The nodes and the bars of the axe, the hammer, and the rapier. */
    static void give_skills(world::Entity& character) {
        for (const std::uint32_t node : {kCharge, kMaelstrom, kGravityWell}) {
            character.skills.pools[kGreatAxe][node].level = 1;
        }
        character.skills.bars[kGreatAxe] = {kCharge, kMaelstrom, kGravityWell};
        std::array<std::uint32_t, 3> hammer{};
        std::size_t place = 0;
        for (const char* node : {"Ability_WarHammer_WreckingBall", "Ability_WarHammer_Shockwave", "Ability_WarHammer_PathOfDestiny"}) {
            character.skills.pools[kWarHammer][gamedata::progression_id(node)].level = 1;
            hammer[place++] = gamedata::progression_id(node);
        }
        character.skills.bars[kWarHammer] = hammer;
        character.skills.pools[kRapier][kRiposte].level = 1;
        character.skills.bars[kRapier] = {kRiposte, 0, 0};
    }
    /** Puts an item of the shop into the main hand of A. */
    void hold(const char* item) {
        world::ItemResult worn = wear(item, world::paperdoll::kMainHand1);
        if (!worn.ok()) {
            throw std::runtime_error(std::string("motion test: the item ") + item);
        }
        character()->combat.selectedWeapon = world::paperdoll::kMainHand1;
    }
    world::Entity* wolf_at(float east, float north) {
        return host.world.creatures().spawn("Test_Wolf", at(kPlace.x + east, kPlace.y + north), 90.0F).entity;
    }
    world::Entity* creature(world::EntityId id) {
        return host.world.creatures().find(id);
    }
    const world::ForcedCounts& forced() {
        return host.world.creatures().forced().counts();
    }
    /** Ticks of the world of `seconds` with idle reports of A. */
    void run(float seconds) {
        walker.idle(static_cast<int>(std::lround(seconds * 30.0F)));
    }
};

float be_f32(const Bytes& bytes, std::size_t offset) {
    std::uint32_t bits = 0;
    for (std::size_t index = 0; index < 4; ++index) {
        bits = (bits << 8) | bytes[offset + index];
    }
    float value = 0.0F;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}

/** One movement state of an entity that a client got: its interest, and its position when the state has one. */
struct Movement {
    std::uint32_t interest{0};
    std::optional<state::WorldPosition> position;
    /** The action of layer 0 (fields 13 and 15) when the state has it. */
    std::optional<std::uint32_t> actionState;
    std::optional<std::uint32_t> sequence;
};

std::uint32_t size_value(const Bytes& bytes) {
    server::hub::Reader reader(bytes);
    return reader.size();
}

/** @return The movement states (type 11) of a slot that a client got from the message `from` on. */
std::vector<Movement> movements(const FakeClient& client, std::uint32_t slot, std::size_t from = 0) {
    std::vector<Movement> found;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const Record& record : read_bundle(client.got[index].body).records) {
            for (const Fragment& fragment : record.fragments) {
                if (fragment.slot != slot || fragment.type != 11) {
                    continue;
                }
                Movement movement;
                movement.interest = record.interest;
                if (const auto field = fragment.fields.find({0, 27}); field != fragment.fields.end() && field->second.size() == 10) {
                    const Bytes& bytes = field->second;
                    const float height = static_cast<float>((bytes[8] << 8) | bytes[9]) / 65535.0F * 1100.0F - 100.0F;
                    movement.position = at(be_f32(bytes, 0), be_f32(bytes, 4), height);
                }
                if (const auto field = fragment.fields.find({0, 13}); field != fragment.fields.end()) {
                    movement.actionState = size_value(field->second) - 1;
                }
                if (const auto field = fragment.fields.find({0, 15}); field != fragment.fields.end()) {
                    movement.sequence = size_value(field->second) - 1;
                }
                found.push_back(movement);
            }
        }
    }
    return found;
}

/** The travel of the rule of a decay rate at the time t (client code 0x48A0D30). */
float decay_travel(float distance, float decay, float seconds) {
    return distance * (1.0F - std::pow(decay, 30.0F * seconds));
}

/** FM1: the impacts of the combat tables (the member `impacts` of the tool). */
void impact_tables(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path path = options.workDirectory / "motion-combat-tables.json";
    nlohmann::json document = {
        {"format", 1},
        {"constants", {{"levelDamage", 0.025}, {"gearScoreRate", 0.0112}, {"gearScoreSmallerRate", 0.6667}, {"gearScoreInterval", 5},
                       {"gearScoreMinimum", 100}, {"gearScoreLimit", 500}}},
        {"damageTypes", {{"Slash", 4, "Physical"}}},
        {"attributes", {{"minimum", 5}, {"strength", {0.0, 1.0}}, {"dexterity", {0.0, 1.0}}, {"intelligence", {0.0, 1.0}}, {"focus", {0.0, 1.0}}}},
        {"damageRows", {{"Test_Light", "Sword", "Light", "Slash", 1.0, 0.0, 0.0, 0.0}, {"Test_Pull", "Sword", "Ability", "Slash", 1.0, 0.0, 0.0, 0.0}}},
        {"weapons", {{"Sword", "Combat Melee", "Sword", 64.0, 0.0, 0.0, 0.0, 0.0, "Test_Light", "", 0.0, "SwordAbilityTable"}}},
        {"items", {{"testsword", 0}}},
        {"defenses", {{0.1, 0.0, nlohmann::json::object(), nlohmann::json::object()}}},
        {"vitals", {{"testwolf", 0}}}};
    const auto load = [&](const nlohmann::json& file) {
        std::ofstream(path) << file.dump();
        gamedata::CombatTables tables;
        const bool loaded = tables.load(path.string());
        return std::make_pair(loaded, tables);
    };
    {
        const auto [loaded, tables] = load(document);
        const auto row = tables.row_index("Test_Pull");
        check(loaded && row && !tables.damage_row(*row).impact, "a file with no member impacts loads: a hit moves no target (a tool from before stage 3.3b-S4)");
    }
    nlohmann::json with = document;
    with["impacts"] = {{"test_pull", 0.0, -1.5, 0.0, 0.8, 0.0, -2.0, 0.0, 0.0, true}};
    {
        const auto [loaded, tables] = load(with);
        const auto row = tables.row_index("Test_Pull");
        const gamedata::DamageRow* pulled = row ? &tables.damage_row(*row) : nullptr;
        check(loaded && pulled != nullptr && pulled->impact && pulled->impact->distance[1] == -1.5F && pulled->impact->decay == 0.8F
                  && pulled->impact->creatureDistance[1] == -2.0F && pulled->impact->fromAttacker,
              "the impact of a row: the distances, the decay rate, the values for a creature, UseAttackerPosForReaction");
        check(!tables.damage_row(*tables.row_index("Test_Light")).impact, "a row with no entry has no impact");
    }
    for (const auto& [what, entry] : std::vector<std::pair<const char*, nlohmann::json>>{
             {"an impact of a row that the file does not have", {"No_Row", 0.0, 1.0, 0.0, 0.8, 0.0, 0.0, 0.0, 0.0, false}},
             {"a decay rate above 1", {"Test_Pull", 0.0, 1.0, 0.0, 1.5, 0.0, 0.0, 0.0, 0.0, false}},
             {"a distance of more than 100 m", {"Test_Pull", 0.0, 500.0, 0.0, 0.8, 0.0, 0.0, 0.0, 0.0, false}},
             {"an entry with 9 values", {"Test_Pull", 0.0, 1.0, 0.0, 0.8, 0.0, 0.0, 0.0, 0.0}}}) {
        nlohmann::json bad = document;
        bad["impacts"] = {entry};
        check(!load(bad).first, std::string("the file is refused: ") + what);
    }
    std::filesystem::remove(path);
}

/** FM2: the rule of a decay rate, its steps on the world clock, the movement states, and the values for a creature. */
void decay_rule(const Options&) {
    MotionShop shop;
    world::Entity* wolf = shop.wolf_at(3.0F, 0.0F);
    check(wolf != nullptr, "setup: a wolf 3 m to the east of A");
    if (wolf == nullptr) {
        return;
    }
    const world::EntityId id = wolf->id;
    shop.run(0.2F);
    const std::size_t from = shop.a.got.size();
    const world::HitResult hit = shop.strike(*wolf, "Test_Pull");
    wolf = shop.creature(id);
    check(hit.verdict == world::HitVerdict::accepted && wolf->creature.forced && !wolf->creature.forced->constantSpeed
              && near(wolf->creature.forced->distance, 1.5F) && near(wolf->creature.forced->direction[0], -1.0F),
          "the hit of a row with Y -1.5 and the decay rate 0.8 starts a pull of 1.5 m toward the attacker");
    // Each step puts the creature at the travel of the time of its step from its start (client code 0x48A0D30).
    bool exact = true;
    float largestGap = 0.0F;
    float lastStep = 0.0F;
    for (int tick = 0; tick < 20 && shop.creature(id)->creature.forced; ++tick) {
        shop.walker.idle(1);
        const world::Entity* moved = shop.creature(id);
        if (!moved->creature.forced) {
            break;
        }
        const world::ForcedMove& move = *moved->creature.forced;
        const float t = std::chrono::duration<float>(move.stepped - move.started).count();
        const float expected = kPlace.x + 3.0F - decay_travel(1.5F, 0.8F, t);
        exact = exact && near(moved->position.x, expected, 0.002F) && near(moved->position.z, kHeight, 0.001F);
        if (t > lastStep) {
            largestGap = std::max(largestGap, t - lastStep);
            lastStep = t;
        }
    }
    note("the largest time between two steps " + std::to_string(largestGap) + " s");
    check(exact, "each step: the place 1.5 * (1 - 0.8^(30 t)) m from the start toward the attacker, on the floor (the terrain height)");
    check(largestGap <= 0.067F, "a step at least each 50 ms of the world clock (with the ticks of 33 ms of the case: 67 ms at most)");
    shop.run(1.6F);
    wolf = shop.creature(id);
    check(!wolf->creature.forced && near(wolf->position.x, kPlace.x + 1.5F, 0.002F), "at its end the wolf is 1.5 m nearer: at 1.5 m from A");
    const std::vector<Movement> states = movements(shop.a, kWolfSlots[0], from);
    std::size_t withPlace = 0;
    for (const Movement& state : states) {
        withPlace += state.position ? 1 : 0;
    }
    check(withPlace >= 5 && withPlace <= 32 && states.back().position && near(states.back().position->x, kPlace.x + 1.5F, 0.002F),
          "the observer gets a movement state with each new place (5 or more, 32 at most for 1.5 s), the last one at the end place");
    check(shop.forced().started >= 1 && shop.forced().ended >= 1 && shop.host.world.creatures().forced().moving_count() == 0,
          "the counts: one movement started and ended; no creature moves now");
    // The values for a creature: the "_AI" value of the row (1.0) in the place of the normal value (0.5).
    world::Entity* second = shop.wolf_at(-3.0F, 0.0F);
    shop.run(0.1F);
    const world::EntityId secondId = second->id;
    (void)shop.strike(*second, "Test_PullAI");
    second = shop.creature(secondId);
    check(second->creature.forced && near(second->creature.forced->distance, 1.0F) && near(second->creature.forced->direction[0], -1.0F),
          "a row with an \"_AI\" value: a creature takes that value (1.0 m away from the attacker), not the normal value (0.5 m)");
}

/** FM3: the rule of no decay rate: a constant speed to the point, a stop 0.1 m from it; away from the point. */
void constant_rule(const Options&) {
    MotionShop shop;
    world::Entity* wolf = shop.wolf_at(3.0F, 0.0F);
    const world::EntityId id = wolf->id;
    shop.run(0.2F);
    const std::optional<std::uint32_t> vortex = shop.host.combat.row_index("Test_Vortex");
    const gamedata::DamageRow& row = shop.host.combat.damage_row(*vortex);
    check(shop.world().creatures().impact(*wolf, *row.impact, kPlace, kPlace, row.id), "the impact of Y -7 with no decay rate moves the wolf");
    const world::WorldClock::time_point start = shop.host.clock.now();
    bool speed = true;
    for (int tick = 0; tick < 30 && shop.creature(id)->creature.forced; ++tick) {
        shop.walker.idle(1);
        const world::Entity* moved = shop.creature(id);
        if (moved->creature.forced) {
            const float t = std::chrono::duration<float>(moved->creature.forced->stepped - start).count();
            speed = speed && near(moved->position.x, kPlace.x + 3.0F - 7.0F * t, 0.002F);
        }
    }
    wolf = shop.creature(id);
    const float took = std::chrono::duration<float>(shop.host.clock.now() - start).count();
    note("the pull took " + std::to_string(took) + " s, the wolf is " + std::to_string(wolf->position.x - kPlace.x) + " m from the point");
    check(speed, "7 m in each second to the point (client code 0x488B8F0)");
    check(!wolf->creature.forced && near(wolf->position.x, kPlace.x + 0.1F, 0.002F) && took < 0.5F,
          "the pull ends 0.1 m from the point, in 0.42 s (not at the end of the reaction)");
    // A positive Y with no decay rate goes away from the point for the length of the reaction.
    world::Entity* other = shop.wolf_at(0.0F, 2.0F);
    const world::EntityId otherId = other->id;
    shop.run(0.1F);
    const gamedata::DamageRow& out = shop.host.combat.damage_row(*shop.host.combat.row_index("Test_Out"));
    check(shop.world().creatures().impact(*other, *out.impact, kPlace, kPlace, out.id), "the impact of Y +7 with no decay rate moves the wolf away");
    shop.run(1.7F);
    other = shop.creature(otherId);
    check(!other->creature.forced && near(other->position.x, kPlace.x, 0.002F) && other->position.y > kPlace.y + 2.0F + 9.0F
              && other->position.y < kPlace.y + 2.0F + 10.6F,
          "away from the point at 7 m/s, to the end of the reaction (1.5 s: 10.5 m)");
}

constexpr int kTile = 31;

/** A folder with one region file: tile (31, 31) of region (0, 0) with a wall and a drop, the flat terrain elsewhere. */
std::filesystem::path nav_folder(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "motion-navigation";
    std::filesystem::remove_all(folder);
    TestNavRegion region;
    region.ground = kHeight;
    region.tiles[{kTile, kTile}] = [](int cellX, int cellY, std::vector<TestFloor>& floors) {
        const float x = static_cast<float>(kTile) * 64.0F + (static_cast<float>(cellX) + 0.5F) * 0.25F;
        (void)cellY;
        if (x >= 2003.0F && x < 2003.5F) {
            floors.clear();
            floors.push_back({kHeight + 3.0F, gamedata::NavFloor::kBlocked}); // A wall from north to south.
        } else if (x >= 2010.0F) {
            floors.clear();
            floors.push_back({kHeight - 2.0F, 0}); // A floor 2 m lower: a drop at x = 2010.
        }
    };
    region.write(folder);
    return folder;
}

/** FM4: a wall and a drop stop the movement; the movement keeps the height of the floor. */
void navigation_stops(const Options& options) {
    MotionShop shop;
    check(shop.host.navigation.set_directory(nav_folder(options).string()), "setup: the navigation data with a wall at x 2003 and a drop at x 2010");
    world::Entity* wolf = shop.wolf_at(1.5F, 0.0F);
    shop.run(0.5F);
    const world::EntityId id = wolf->id;
    const std::uint64_t blocked = shop.forced().blocked;
    (void)shop.strike(*shop.creature(id), "Test_Push");
    shop.run(1.7F);
    wolf = shop.creature(id);
    note("the wolf stops at x " + std::to_string(wolf->position.x - kPlace.x) + " m east of A");
    check(!wolf->creature.forced && wolf->position.x > kPlace.x + 2.7F && wolf->position.x < kPlace.x + 3.0F
              && shop.forced().blocked == blocked + 1,
          "a push of 3 m toward the wall stops in front of it (the last free cell), not through it");
    // A drop: the floor beyond x 2010 is 2 m lower (more than a step of 0.35 m).
    world::Entity* edge = shop.wolf_at(8.6F, 2.0F);
    shop.run(0.5F);
    const world::EntityId edgeId = edge->id;
    const gamedata::DamageRow& push = shop.host.combat.damage_row(*shop.host.combat.row_index("Test_Push"));
    const state::WorldPosition source = at(kPlace.x + 7.0F, kPlace.y + 2.0F);
    check(shop.world().creatures().impact(*shop.creature(edgeId), *push.impact, source, source, push.id), "setup: a push of 3 m to the east");
    shop.run(1.7F);
    edge = shop.creature(edgeId);
    check(!edge->creature.forced && edge->position.x < kPlace.x + 10.0F && edge->position.x > kPlace.x + 9.5F && near(edge->position.z, kHeight, 0.05F),
          "a push to a drop stops at its edge, on the upper floor");
}

/** FM5: the rules of the creature: its way back, its death, a second impact, and the behavior. */
void creature_rules(const Options&) {
    MotionShop shop;
    const gamedata::DamageRow& pull = shop.host.combat.damage_row(*shop.host.combat.row_index("Test_Pull"));
    world::Entity* home = shop.wolf_at(3.0F, 0.0F);
    const world::EntityId homeId = home->id;
    shop.run(0.1F);
    home->creature.mind.mode = world::CreatureMode::home; // On its way back (the reset): immune.
    const std::uint64_t refused = shop.forced().refused;
    check(!shop.world().creatures().impact(*home, *pull.impact, kPlace, kPlace, pull.id) && shop.forced().refused == refused + 1,
          "a creature on its way back (immune) does not move");
    home->creature.mind.mode = world::CreatureMode::rest;
    (void)shop.world().creatures().kill(homeId);
    check(!shop.world().creatures().impact(*shop.creature(homeId), *pull.impact, kPlace, kPlace, pull.id), "a dead creature does not move");
    // A second impact replaces the first one, from the place that the creature has.
    world::Entity* wolf = shop.wolf_at(0.0F, 3.0F);
    const world::EntityId id = wolf->id;
    shop.run(0.1F);
    (void)shop.world().creatures().impact(*wolf, *pull.impact, kPlace, kPlace, pull.id);
    shop.run(0.1F);
    wolf = shop.creature(id);
    const state::WorldPosition middle = wolf->position;
    (void)shop.world().creatures().impact(*wolf, *pull.impact, kPlace, kPlace, pull.id);
    check(shop.forced().replaced >= 1 && near(shop.creature(id)->creature.forced->start.y, middle.y), "a second impact replaces the movement");
    shop.run(1.7F);
    check(near(shop.creature(id)->position.y, middle.y - 1.5F, 0.003F), "the second movement goes its full distance from its start");
    // The behavior: a wolf that fights A does not run while the force moves it, and goes on after the end.
    shop.world().set_creature_behavior(true);
    world::Entity* fighter = shop.wolf_at(-8.0F, 0.0F);
    const world::EntityId fighterId = fighter->id;
    shop.run(0.6F);
    fighter = shop.creature(fighterId);
    check(fighter->creature.mind.mode == world::CreatureMode::fight, "setup: the wolf fights A");
    const gamedata::DamageRow& out = shop.host.combat.damage_row(*shop.host.combat.row_index("Test_Push"));
    (void)shop.world().creatures().impact(*fighter, *out.impact, kPlace, kPlace, out.id);
    bool rule = true;
    while (shop.creature(fighterId)->creature.forced) {
        shop.walker.idle(1);
        const world::Entity* moved = shop.creature(fighterId);
        if (moved->creature.forced) {
            const float t = std::chrono::duration<float>(moved->creature.forced->stepped - moved->creature.forced->started).count();
            rule = rule && near(moved->position.x, moved->creature.forced->start.x - decay_travel(3.0F, 0.9F, t), 0.003F);
        }
    }
    const float after = shop.creature(fighterId)->position.x;
    check(rule, "while the push moves it, the wolf has only the places of the push (the behavior does not step it)");
    shop.run(1.0F);
    check(shop.creature(fighterId)->position.x > after + 1.0F, "after the push the wolf runs to A again (a new path from its new place)");
    shop.world().set_creature_behavior(false);
}

/** FM6: the abilities: the vortex and the burst of Gravity Well, Path of Destiny, Wrecking Ball. */
void ability_motion(const Options&) {
    MotionShop shop;
    shop.hold("TestAxe");
    // The press of the server throws the projectile 10 m to the west (the yaw of the walker); the vortex has its center there.
    world::Entity* north = shop.wolf_at(-10.0F, 2.0F);
    world::Entity* south = shop.wolf_at(-10.0F, -3.0F);
    const world::EntityId northId = north->id;
    const world::EntityId southId = south->id;
    shop.run(0.3F);
    shop.walker.press(2);
    std::optional<state::WorldPosition> center;
    for (int step = 0; step < 60 && !center; ++step) {
        shop.walker.idle(1);
        for (const auto& [entityId, entity] : shop.world().entities()) {
            if (entity.kind == world::EntityKind::spell) {
                center = entity.position;
            }
        }
    }
    check(center.has_value() && near(center->x, kPlace.x - 10.0F, 0.05F), "setup: the vortex 10 m to the west of A");
    if (!center) {
        return;
    }
    check(shop.creature(northId)->creature.forced && shop.creature(southId)->creature.forced
              && shop.creature(northId)->creature.forced->constantSpeed && near(shop.creature(northId)->creature.forced->speed, 7.0F),
          "the start of the vortex pulls each wolf of its sphere (the row GreatAxe_GravityWell_Vortex: 7 m/s to the center)");
    shop.run(0.6F);
    const float northAway = world::horizontal_distance(shop.creature(northId)->position, *center);
    const float southAway = world::horizontal_distance(shop.creature(southId)->position, *center);
    note("after 0.6 s: the wolves are " + std::to_string(northAway) + " m and " + std::to_string(southAway) + " m from the center");
    check(northAway < 0.11F && southAway < 0.11F, "the two wolves are at the center (0.1 m) after 0.6 s");
    // The burst (3 s): its damage and its push of 0.5 m (decay rate 0.3) away from the center.
    shop.run(2.6F);
    const float pushed = world::horizontal_distance(shop.creature(northId)->position, *center);
    check(pushed > 0.45F && pushed < 0.65F, "the burst pushes a wolf 0.5 m out of the center");
    // Path of Destiny: the push comes from the caster (UseAttackerPosForReaction), not from the circle.
    shop.hold("TestHammer");
    shop.run(0.3F);
    world::Entity* inPath = shop.wolf_at(-4.0F, 1.0F);
    const world::EntityId pathId = inPath->id;
    const state::WorldPosition before = inPath->position;
    shop.run(0.3F);
    shop.walker.press(2);
    shop.run(2.5F);
    const world::Entity* hit = shop.creature(pathId);
    const float dx = before.x - kPlace.x;
    const float dy = before.y - kPlace.y;
    const float length = std::hypot(dx, dy);
    note("Path of Destiny: the wolf moved from " + std::to_string(before.x - kPlace.x) + " " + std::to_string(before.y - kPlace.y) + " to "
         + std::to_string(hit->position.x - kPlace.x) + " " + std::to_string(hit->position.y - kPlace.y));
    check(near(hit->position.x, before.x + dx / length * 2.0F, 0.02F) && near(hit->position.y, before.y + dy / length * 2.0F, 0.02F),
          "Path of Destiny pushes the wolf 2 m away from the caster (UseAttackerPosForReaction), not from the center of its circle");
    // Wrecking Ball: its row has no impact distance in the game data; it moves nothing.
    shop.world().abilities().reset_cooldowns(kCharacterA, true);
    world::Entity* wrecked = shop.wolf_at(2.0F, 0.0F);
    const world::EntityId wreckedId = wrecked->id;
    shop.run(0.3F);
    const std::uint64_t started = shop.forced().started;
    (void)shop.strike(*wrecked, "Warhammer_Damage_WreckingBall");
    check(shop.forced().started == started && !shop.creature(wreckedId)->creature.forced, "a row with no impact (Wrecking Ball) moves nothing");
}

/** FM7 (MP-10): two sessions get the movement of a creature; the dash of B; each keeps its own position and resources. */
void two_sessions(const Options&) {
    MotionShop shop(true);
    shop.enter_b();
    world::Entity* wolf = shop.wolf_at(3.0F, 2.0F);
    const world::EntityId id = wolf->id;
    shop.run(0.5F);
    const std::size_t fromA = shop.a.got.size();
    const std::size_t fromB = shop.b.got.size();
    world::Entity* beta = shop.world().find_character(kCharacterB);
    const state::WorldPosition betaPlace = beta->position;
    const float betaHealth = beta->vitals.health;
    const float betaMana = beta->vitals.mana;
    (void)shop.strike(*wolf, "Test_Pull");
    for (int index = 0; index < 50; ++index) {
        shop.host.clock.advance(kTick);
        shop.host.datagram(shop.b.peer, shop.b.report_at(static_cast<std::uint16_t>(200 + index), betaPlace.x, betaPlace.y, betaPlace.z));
        shop.host.tick();
        shop.b.exchange(shop.host);
        shop.walker.send(shop.walker.entry());
    }
    const std::vector<Movement> forA = movements(shop.a, kWolfSlots[0], fromA);
    const std::vector<Movement> forB = movements(shop.b, kWolfSlots[0], fromB);
    const auto last_place = [](const std::vector<Movement>& states) {
        std::optional<state::WorldPosition> place;
        for (const Movement& state : states) {
            place = state.position ? state.position : place;
        }
        return place;
    };
    const state::WorldPosition end = shop.creature(id)->position;
    check(last_place(forA) && last_place(forB) && near(last_place(forA)->x, end.x, 0.002F) && near(last_place(forB)->x, end.x, 0.002F),
          "A (the caster) and B (an observer) each get the movement states of the pulled wolf, with its end place");
    beta = shop.world().find_character(kCharacterB);
    check(near(beta->position.x, betaPlace.x) && near(beta->position.y, betaPlace.y) && beta->vitals.health == betaHealth
              && beta->vitals.mana == betaMana,
          "B keeps its own position, health, and mana");
    // B makes the dash of Charge with its client: A observes it (the state `motion`), B gets no position of the server.
    MotionShop::give_skills(*beta);
    beta->equipment.slots[world::paperdoll::kMainHand1].item = shop.character()->equipment.slots[world::paperdoll::kMainHand1].item;
    world::ItemResult axe = shop.host.world.inventory().give(kCharacterB, "TestAxe", 1);
    if (axe.ok() && !axe.slots.empty()) {
        (void)shop.host.world.inventory().equip(kCharacterB, axe.slots.front(), world::paperdoll::kMainHand1);
    }
    beta->combat.selectedWeapon = world::paperdoll::kMainHand1;
    const std::size_t dashA = shop.a.got.size();
    const std::size_t dashB = shop.b.got.size();
    const state::WorldPosition alphaPlace = shop.character()->position;
    std::uint16_t input = 300;
    const auto b_entry = [&](std::vector<unsigned> bits, float east) {
        InputEntry entry = report_entry(input++, betaPlace.x + east, betaPlace.y, betaPlace.z);
        entry.actionBits = std::move(bits);
        return entry;
    };
    std::deque<InputEntry> history;
    const auto b_send = [&](InputEntry entry) {
        history.push_front(entry);
        if (history.size() > 5) {
            history.pop_back();
        }
        shop.host.clock.advance(kTick);
        shop.host.datagram(shop.b.peer, shop.b.input_report(std::vector<InputEntry>(history.begin(), history.end())));
        shop.host.tick();
        shop.b.exchange(shop.host);
        shop.walker.send(shop.walker.entry());
    };
    b_send(b_entry({kAbilityBits[0], kVirtualBit}, 0.0F));
    b_send(b_entry({kHoldBit}, 0.0F));
    b_send(b_entry({kReleaseBit}, 0.0F));
    const server::rep::RepSession* sessionB = shop.host.server->session(2);
    const std::uint64_t refusedBefore = sessionB != nullptr ? sessionB->position_track().refused() : 0;
    for (int step = 1; step <= 30; ++step) {
        b_send(b_entry({}, -13.8F * static_cast<float>(step) / 30.0F));
    }
    for (int step = 0; step < 20; ++step) {
        b_send(b_entry({}, -13.8F));
    }
    const std::uint64_t refusedAfter = sessionB != nullptr ? sessionB->position_track().refused() : 0;
    check(shop.world().abilities().counts().presses[0] >= 1 && refusedAfter == refusedBefore,
          "B's Charge is accepted, and its dash of 13.8 m in 1 s is accepted in the envelope");
    const std::vector<Movement> ofB = movements(shop.a, server::rep::kMovementSlot, dashA);
    std::size_t places = 0;
    bool dashAction = false;
    for (const Movement& state : ofB) {
        if (state.interest != 1 && state.position) {
            ++places;
        }
        dashAction = dashAction || (state.interest != 1 && state.actionState == 7U && state.sequence == 59U);
    }
    check(places >= 5 && dashAction, "A gets the movement states of B (its places and the action 7/59 of the dash)");
    bool ownPlace = false;
    for (const Movement& state : movements(shop.b, server::rep::kMovementSlot, dashB)) {
        ownPlace = ownPlace || (state.interest == 1 && state.position.has_value());
    }
    check(!ownPlace, "B gets no movement state with a position for its own character (no second displacement)");
    check(near(shop.character()->position.x, alphaPlace.x) && near(shop.world().find_character(kCharacterB)->position.x, betaPlace.x - 13.8F, 0.01F),
          "A keeps its place; B is at the end of its dash (the place of its client)");
}

/** FM8: the load: 200 creatures that a force moves near the characters in a world of 4,200 creatures. */
void load(const Options&) {
    MotionShop shop;
    std::vector<world::EntityId> far;
    for (int index = 0; index < 4000; ++index) {
        world::Entity* creature = shop.world().creatures().spawn("Test_Wolf", at(5000.0F + static_cast<float>(index % 80) * 5.0F,
                                                                                   5000.0F + static_cast<float>(index / 80) * 5.0F),
                                                                 0.0F)
                                      .entity;
        if (creature != nullptr) {
            far.push_back(creature->id);
        }
    }
    std::vector<world::EntityId> near_;
    for (int index = 0; index < 200; ++index) {
        world::Entity* creature = shop.world().creatures().spawn("Test_Wolf", at(kPlace.x + 5.0F + static_cast<float>(index % 20) * 2.0F,
                                                                                   kPlace.y + 5.0F + static_cast<float>(index / 20) * 2.0F),
                                                                 0.0F)
                                      .entity;
        near_.push_back(creature->id);
    }
    const gamedata::DamageRow& pull = shop.host.combat.damage_row(*shop.host.combat.row_index("Test_Pull"));
    for (const world::EntityId id : near_) {
        (void)shop.world().creatures().impact(*shop.creature(id), *pull.impact, kPlace, kPlace, pull.id);
    }
    const std::uint64_t looked = shop.forced().looked;
    const auto start = std::chrono::steady_clock::now();
    for (int tick = 0; tick < 60; ++tick) {
        shop.host.clock.advance(kTick);
        shop.host.world.tick();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    const std::uint64_t steps = shop.forced().looked - looked;
    note("200 movements for 2 s of the world clock in a world of " + std::to_string(shop.world().creatures().count()) + " creatures: "
         + std::to_string(steps) + " looks at a moving creature, " + std::to_string(shop.forced().steps) + " steps, "
         + std::to_string(seconds) + " s");
    check(shop.world().creatures().count() >= 4200, "setup: 4,200 creatures in the world");
    check(steps <= 200U * 60U && shop.host.world.creatures().forced().moving_count() == 0, "the step looks only at the moving creatures (200 at most in each tick)");
    bool farStill = true;
    for (std::size_t index = 0; index < far.size(); index += 97) {
        const world::Entity* creature = shop.creature(far[index]);
        farStill = farStill && creature != nullptr && !creature->creature.forced;
    }
    check(farStill, "the far creatures have no movement");
    check(seconds < 2.0, "the time limit: 60 ticks with 200 movements in less than 2 s");
}

/** Sends A's reports of a dash of `distance` m to the west in `steps` input steps, with a track of the case. @return The refusals. */
unsigned dash(MotionShop& shop, world::PositionTrack& track, std::uint16_t first, float distance, unsigned steps) {
    unsigned refused = 0;
    const state::WorldPosition start = shop.character()->position;
    for (unsigned index = 0; index <= steps + 10; ++index) {
        const float part = std::min(static_cast<float>(index) / static_cast<float>(steps), 1.0F);
        const world::PositionReport report{static_cast<std::uint16_t>(first + index), at(start.x - distance * part, start.y), wire_height(kHeight)};
        if (shop.world().report_position(shop.character()->owner, report, track) != world::PositionVerdict::accepted) {
            ++refused;
        }
        shop.host.clock.advance(std::chrono::microseconds(33333));
        shop.host.world.tick();
    }
    return refused;
}

/** CM1: the envelope of the dash of Charge in the position check; no second displacement. */
void caster_envelope(const Options&) {
    MotionShop shop;
    shop.hold("TestAxe");
    shop.run(0.3F);
    // With no cast: the dash of the live check (13.8 m in 1 s) is faster than the window of a run.
    world::PositionTrack plain;
    const unsigned noCast = dash(shop, plain, 5000, 13.8F, 30);
    // With the cast: the same dash in its envelope.
    const std::size_t from = shop.a.got.size();
    shop.walker.position = shop.character()->position;
    shop.walker.press(0);
    world::PositionTrack charged;
    const unsigned inCast = dash(shop, charged, 6000, 13.8F, 30);
    const world::MotionAllowance allowance = shop.world().abilities().motion_allowance(*shop.character(), shop.host.clock.now());
    note("refusals of the dash: no cast " + std::to_string(noCast) + ", in the cast " + std::to_string(inCast));
    check(noCast > 0, "with no cast the dash of 13.8 m in 1 s is refused (too fast)");
    check(inCast == 0, "in the cast of Charge the dash is accepted (the envelope: 14.73 m x 1.3, the step speed 18.07 m/s x 1.3)");
    check(allowance.distance > 19.0F && allowance.peak > 23.0F, "1.5 s after the press the envelope still runs (the length 1.6 s and one window)");
    // After the envelope the same dash is refused again.
    shop.host.clock.advance(milliseconds(1200));
    check(shop.world().abilities().motion_allowance(*shop.character(), shop.host.clock.now()).distance == 0.0F,
          "the envelope ends one window after the length of the movement (2.6 s after the press)");
    world::PositionTrack late;
    check(dash(shop, late, 7000, 13.8F, 30) > 0, "a dash after the envelope is refused again");
    bool ownPlace = false;
    for (const Movement& state : movements(shop.a, server::rep::kMovementSlot, from)) {
        ownPlace = ownPlace || (state.interest == 1 && state.position.has_value());
    }
    check(!ownPlace, "the owner gets no movement state with a position of the server after the press (no second displacement)");
}

/** CM2: the dash of the server for a character with no client, and its observer. */
void server_dash(const Options&) {
    MotionShop shop(true);
    const auto beta = add_player(shop.host.store, "76561202255233019", "Tester B", kCharacterB, "Beta", at(kPlace.x - 4.0F, kPlace.y + 3.0F));
    check(shop.world().spawn_character(kCharacterB, at(kPlace.x - 4.0F, kPlace.y + 3.0F)) == world::SpawnError::none, "setup: B with no client");
    world::Entity& remote = *shop.world().find_character(kCharacterB);
    MotionShop::give_skills(remote);
    shop.hold("TestAxe");
    remote.equipment.slots[world::paperdoll::kMainHand1].item = shop.character()->equipment.slots[world::paperdoll::kMainHand1].item;
    remote.combat.selectedWeapon = world::paperdoll::kMainHand1;
    shop.run(1.0F);
    const std::size_t from = shop.a.got.size();
    const state::WorldPosition start = remote.position;
    world::AbilityPress press;
    press.place = 0;
    press.yaw = 1.5708F; // West.
    check(shop.world().abilities().press_character(kCharacterB, press) == world::CastVerdict::accepted, "B (no client) casts Charge");
    shop.run(0.5F);
    const float half = start.x - shop.world().find_character(kCharacterB)->position.x;
    shop.run(1.5F);
    const world::Entity* moved = shop.world().find_character(kCharacterB);
    note("the dash of the server: " + std::to_string(half) + " m after 0.5 s, " + std::to_string(start.x - moved->position.x) + " m at its end");
    check(half > 6.0F && half < 7.9F && near(start.x - moved->position.x, 13.77F, 0.01F) && near(moved->position.y, start.y, 0.01F),
          "the server moves B on the line of the press: 13.77 m in 1 s");
    bool places = false;
    bool dashAction = false;
    bool rest = false;
    for (const Movement& state : movements(shop.a, server::rep::kMovementSlot, from)) {
        if (state.interest == 1) {
            continue;
        }
        places = places || state.position.has_value();
        dashAction = dashAction || (state.actionState == 7U && state.sequence == 59U);
        rest = rest || (dashAction && state.actionState == 30U && state.sequence == 1080U);
    }
    check(places && dashAction && rest, "A observes the places of B, the action of the dash (7/59), and the action at rest after it (30/1080)");
    // A creature on the line: the dash stops 1 m in front of it.
    shop.world().abilities().reset_cooldowns(kCharacterB, true);
    world::Entity* wolf = shop.wolf_at(moved->position.x - kPlace.x - 6.0F, moved->position.y - kPlace.y);
    const float wolfX = wolf->position.x;
    shop.run(0.3F);
    check(shop.world().abilities().press_character(kCharacterB, press) == world::CastVerdict::accepted, "B casts Charge again");
    shop.run(2.0F);
    check(near(shop.world().find_character(kCharacterB)->position.x, wolfX + 1.0F, 0.02F), "the dash ends 1 m in front of a creature on its line");
    (void)beta;
}

/** Ticks with a wolf that fights A until it starts a new attack (not the attack that plays now). @return False: none in 5 s. */
bool wait_attack(MotionShop& shop, world::EntityId wolf) {
    const world::Entity* before = shop.creature(wolf);
    const std::uint32_t attacks = before != nullptr ? before->creature.mind.attacks : 0;
    for (int step = 0; step < 150; ++step) {
        shop.walker.idle(1);
        const world::Entity* creature = shop.creature(wolf);
        if (creature != nullptr && creature->creature.mind.attack && creature->creature.mind.attacks != attacks) {
            return true;
        }
    }
    return false;
}

/** RC1: the counter of Riposte: the window takes the hit, the server starts the counter, one hit with the stun and the push. */
void riposte(const Options&) {
    MotionShop shop;
    shop.hold("TestRapier");
    shop.world().set_creature_behavior(true);
    world::Entity* wolf = shop.wolf_at(1.8F, 0.0F);
    const world::EntityId id = wolf->id;
    check(wait_attack(shop, id), "setup: the wolf starts an attack on A");
    const float health = shop.character()->vitals.health;
    const float wolfHealth = shop.creature(id)->vitals.health;
    const std::size_t from = shop.a.got.size();
    shop.walker.press(0);
    check(std::any_of(shop.character()->statuses.begin(), shop.character()->statuses.end(),
                      [](const world::ActiveStatus& status) { return status.status == "Rapier_Riposte_Timer"; }),
          "the press puts the window (Rapier_Riposte_Timer, 1 s) on A");
    shop.run(1.2F);
    const world::AbilityCounts& counts = shop.world().abilities().counts();
    note("health of A " + std::to_string(health) + " to " + std::to_string(shop.character()->vitals.health) + "; counters "
         + std::to_string(counts.counters) + ", counter hits " + std::to_string(counts.counterHits));
    check(counts.counters == 1 && shop.character()->vitals.health == health, "the bite in the window is countered: its absorption takes the damage (0)");
    check(std::none_of(shop.character()->statuses.begin(), shop.character()->statuses.end(),
                       [](const world::ActiveStatus& status) { return status.status == "Rapier_Riposte_Timer"; }),
          "the counter ends the window");
    const world::Entity* countered = shop.creature(id);
    check(counts.counterHits == 1 && countered->vitals.health < wolfHealth, "the counter hits the attacker one time (the row of the counter action)");
    check(std::any_of(countered->creature.effects.begin(), countered->creature.effects.end(),
                      [](const world::CreatureEffect& effect) { return effect.status == "Rapier_Riposte_Stun" && effect.stun; }),
          "the attacker has the stun of the row (Rapier_Riposte_Stun)");
    check(shop.forced().started >= 1, "the counter row pushes the attacker (its impact)");
    check(!damage_entries(shop.a, from).empty(), "A gets the damage number of the counter");
    shop.run(3.0F);
    check(shop.world().abilities().counts().counterHits == 1, "one counter hit for the cast");
}

/** RC2: a refused counter: a hit after the window, a press in the cooldown; and two sessions. */
void riposte_refused(const Options&) {
    MotionShop shop(true);
    shop.enter_b();
    shop.hold("TestRapier");
    shop.walker.press(0);
    shop.run(1.2F); // The window ends with no hit.
    shop.world().set_creature_behavior(true);
    world::Entity* wolf = shop.wolf_at(1.8F, 0.0F);
    const world::EntityId id = wolf->id;
    const float health = shop.character()->vitals.health;
    check(wait_attack(shop, id), "setup: the wolf attacks A after the window");
    shop.run(1.0F);
    check(shop.world().abilities().counts().counters == 0 && shop.character()->vitals.health < health,
          "a hit after the window is not countered: it does its damage, and no counter starts");
    // A press in the cooldown (18 s) opens no window.
    shop.walker.press(0);
    check(std::none_of(shop.character()->statuses.begin(), shop.character()->statuses.end(),
                       [](const world::ActiveStatus& status) { return status.status == "Rapier_Riposte_Timer"; }),
          "a press in the cooldown is refused: no window");
    const float before = shop.character()->vitals.health;
    (void)wait_attack(shop, id);
    shop.run(1.0F);
    check(shop.world().abilities().counts().counters == 0 && shop.character()->vitals.health < before, "no counter with no window");
    world::Entity* beta = shop.world().find_character(kCharacterB);
    check(beta != nullptr && beta->vitals.health == beta->vitals.healthMax, "B keeps its health");
}

} // namespace

std::vector<Case> motion_cases() {
    return {{"FM1", "forced movement: the impacts of the combat tables, a file with none, the refusals", impact_tables},
            {"FM2", "forced movement: the rule of a decay rate, the steps on the world clock, the states, the values for a creature", decay_rule},
            {"FM3", "forced movement: the rule of no decay rate: 7 m/s to the point, the stop at 0.1 m; away from the point", constant_rule},
            {"FM4", "forced movement: a wall and a drop stop the movement", navigation_stops},
            {"FM5", "forced movement: the way back, the death, a second impact, the behavior during and after the movement", creature_rules},
            {"FM6", "forced movement: the vortex and the burst of Gravity Well, Path of Destiny from the caster, Wrecking Ball", ability_motion},
            {"FM7", "forced movement: two sessions (MP-10): the moved creature, the dash of B, own positions and resources", two_sessions},
            {"FM8", "forced movement: the load of 200 moving creatures in a world of 4,200 creatures, with a time limit", load},
            {"CM1", "caster motion: the envelope of the dash of Charge in the position check, no second displacement", caster_envelope},
            {"CM2", "caster motion: the dash of the server for a character with no client, and its observer", server_dash},
            {"RC1", "counter: Riposte counters a hit in its window, the server starts the counter, one hit", riposte},
            {"RC2", "counter: a refused counter (after the window, in the cooldown), two sessions", riposte_refused}};
}

} // namespace soulwarden::backend::tests
