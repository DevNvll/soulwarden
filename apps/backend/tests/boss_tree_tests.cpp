// Tests of stage BT3: the boss nodes of the behavior trees and the first mini boss (world/behavior_tree.h, world/behavior.h,
// docs/systems/creature-behavior.md, section "Behavior trees, stage BT3").
//
//   BT3A  the loader of the boss nodes: each node kind and its members, the files that are refused, the lists of the compiled tree (tags,
//         statuses, events, node states), the tree data of the slice (treeActions of creatures.json)
//   BT3B  the node action: an attack of the slice by its name in its reach, a combo of two actions (a sequence with memory), a tree that
//         names an action that the slice does not have is refused for that creature
//   BT3C  the node phase: a health threshold that fires one time in a fight, the transition (a tree action with its statuses: the heal of its
//         ticks, the ABS parts on the damage of a hit, the exit), no reaction to a hit in the transition, the tag of the phase, the
//         conditions tag_is, has_status, health_below, and the timers of the fight and of the phase
//   BT3D  the nodes cooldown, once, and weighted_random (a running combo keeps its choice)
//   BT3E  the target policy (threat, nearest, random) and the arena of keep_target with two characters (MP-10: both sessions decode the
//         records of the boss)
//   BT3F  the end of a fight: the reset at the anchor gives the tree its first state (the phase, the tags, the once nodes, the statuses of
//         the fight) and the full health
//   BT3G  the tree boss/thorpe of the program: the binding by the kind of Thorpe, the refusal for a slice with no tree action (an older
//         creatures.json, the slice of the other commanders), the fight with the combos and the transition at 60 percent
//   BT3L  the load: 400 creatures of a boss tree in a fight near 50 characters, with the time limit of case BT4
//
// The accounts, the characters, the creature kinds, the slices, the status rows, the trees, and the places are made up.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "gamedata/creature_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/tree_fixture.h"
#include "world/behavior.h"
#include "world/behavior_tree.h"
#include "world/combat.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace trees;
namespace tree = world::tree;

// The boss slice: the fighter slice with three attacks of their own names, and a tree action with the statuses of the transition of the
// commander of the damned (the form of the game data: a heal of 2 percent each 0.1 s and ABS 0.6 in the transition, ABS 0.1 in the phase 2).
constexpr std::uint32_t kAttackA = 6;
constexpr std::uint32_t kAttackB = 7;
constexpr std::uint32_t kAttackC = 8;
constexpr std::uint32_t kTransition = 9;
constexpr float kTransitionLength = 3.0F;
constexpr const char* kTransitionStatus = "Test_Boss_Transition";
constexpr const char* kPhaseStatus = "Test_Boss_Phase_2";
constexpr const char* kFloorStatus = "Test_Boss_Phase_1_MinHealth";
constexpr std::uint32_t kBossBody = 11;
constexpr std::uint32_t kPlainBody = 12;

gamedata::CreatureContact boss_contact(float reach) {
    gamedata::CreatureContact contact;
    contact.time = 0.5F;
    contact.reach = reach;
    contact.factor = 0.3F;
    contact.type = "Slash";
    contact.wire = 4;
    contact.category = gamedata::DamageCategory::physical;
    contact.row = "Test_Boss";
    return contact;
}

gamedata::CreatureSlice boss_slice(bool transition) {
    gamedata::CreatureSlice slice = fighter_slice();
    slice.path = transition ? "slices/test/boss.dynamicslice" : "slices/test/boss_plain.dynamicslice";
    slice.asset.fill(transition ? 0x5A : 0x5B);
    slice.root = transition ? 0x100000000000005AULL : 0x100000000000005BULL;
    slice.actions.push_back({"Attack_A", "Attack_A", 40, false});
    slice.actions.push_back({"Attack_B", "Attack_B", 41, false});
    slice.actions.push_back({"Attack_C", "Attack_C", 42, false});
    slice.actions.push_back({"Phase_Transition", "Phase_Transition", 43, false});
    slice.attacks = {gamedata::CreatureAttack{kAttackA, 1.0F, {boss_contact(2.2F)}}, gamedata::CreatureAttack{kAttackB, 1.2F, {boss_contact(2.6F)}},
                     gamedata::CreatureAttack{kAttackC, 0.8F, {boss_contact(2.0F)}}};
    if (transition) {
        slice.treeActions = {gamedata::CreatureTreeAction{kTransition, kTransitionLength, {kTransitionStatus, kPhaseStatus},
                                                           {kFloorStatus, kTransitionStatus}, std::nullopt}};
        // The tag state of the variant: the weapon tags and a default tag of the slice (the bit of `thorpe` of the game data).
        slice.treeActions[0].tags = gamedata::FragmentTags{0x08, 0x01, 0x01};
    }
    return slice;
}

void boss_world(std::vector<gamedata::CreatureSlice>& slices, std::vector<gamedata::CreatureKind>& kinds) {
    slices.push_back(boss_slice(true));
    slices.push_back(boss_slice(false));
    kinds.push_back(area::area_kind("Test_Boss", kBossBody, 0x6B112222));
    kinds.push_back(area::area_kind("Test_Plain_Boss", kPlainBody, 0x6C112222));
}

/** The ability data of the support world and the two status rows of the transition (the cells of the rows of the game data). */
void boss_statuses(Arena& arena) {
    gamedata::AbilityTables tables = support::support_abilities();
    std::vector<gamedata::StatusRow> rows = tables.statusEffects;
    rows.push_back(shop::status(kTransitionStatus, -1.0F,
                                shop::cells({{"TickRate", 0.1F}, {"HealthModifierPercent", 0.02F}, {"ABSSlash", 0.6F}, {"ABSStrike", 0.6F},
                                             {"StackMax", 1.0F}, {"IgnoreInvulnerable", 1.0F}},
                                            {{"OnEndStatusEffect", kPhaseStatus}})));
    rows.push_back(shop::status(kPhaseStatus, -1.0F, shop::cells({{"ABSSlash", 0.1F}, {"ABSStrike", 0.1F}, {"StackMax", 1.0F}}), true));
    tables.statusEffects = shop::numbered(std::move(rows));
    if (!arena.host.abilities.set(std::move(tables))) {
        throw std::runtime_error("boss test: the ability data is not valid: " + arena.host.abilities.error());
    }
}

std::string tree_file(const std::string& name, const std::string& root, const std::string& board = "") {
    return "// a test tree\n{ \"format\": 1, \"tree\": \"" + name + "\"" + (board.empty() ? "" : ", \"blackboard\": " + board)
           + ", \"root\": " + root + " }";
}

std::filesystem::path folder_with(const Options& options, const char* name, const std::vector<std::pair<std::string, std::string>>& files) {
    const std::filesystem::path folder = options.workDirectory / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    for (const auto& [file, text] : files) {
        std::ofstream(folder / file, std::ios::binary) << text;
    }
    return folder;
}

/** An always-success wrapper: the child runs, and the selector goes on to the next node of the fight. */
std::string always(const std::string& child) {
    return R"({ "node": "selector", "children": [ )" + child + R"(, { "node": "set", "key": "none", "value": 0 } ] })";
}

// The test boss tree: the conditions and the timers write keys of the blackboard (always success), then the phase 2 at 60 percent with the
// transition and its tag, then the combo A then B, then the default melee.
const char* const kBossBoard = R"({ "phase": { "type": "int", "default": 1 }, "none": { "type": "int" }, "seen": { "type": "int" },
                                    "fightLong": { "type": "int" }, "phaseLong": { "type": "int" } })";

std::string boss_root() {
    const std::string seen = always(R"({ "node": "sequence", "children": [ { "node": "tag_is", "tag": "Phase_2" },
        { "node": "has_status", "status": "Test_Boss_Phase_2" }, { "node": "health_below", "percent": 100 }, { "node": "set", "key": "seen", "value": 1 } ] })");
    const std::string fightTimer = R"({ "node": "selector", "children": [ { "node": "sequence", "children": [ { "node": "timer", "seconds": 2 },
        { "node": "set", "key": "fightLong", "value": 1 } ] }, { "node": "set", "key": "fightLong", "value": 0 } ] })";
    const std::string phaseTimer = R"({ "node": "selector", "children": [ { "node": "sequence", "children": [ { "node": "timer", "seconds": 2, "since": "phase" },
        { "node": "set", "key": "phaseLong", "value": 1 } ] }, { "node": "set", "key": "phaseLong", "value": 0 } ] })";
    return R"({ "node": "selector", "children": [
      { "node": "sequence", "name": "fight", "children": [ { "node": "in_fight" }, { "node": "keep_target", "lose_distance": 60 }, )"
           + seen + ", " + fightTimer + ", " + phaseTimer + R"(,
        { "node": "selector", "children": [
          { "node": "phase", "name": "phase 2", "health_below": 60, "key": "phase", "to": 2, "child": { "node": "sequence", "children": [
              { "node": "action", "action": "Phase_Transition", "pause": 0.2 }, { "node": "tag_set", "tag": "Phase_2" } ] } },
          { "node": "sequence", "name": "combo", "memory": true, "children": [
              { "node": "action", "action": "Attack_A", "pause": 0.2 }, { "node": "action", "action": "Attack_B", "pause": 0.2 } ] },
          { "node": "subtree", "tree": "default/melee" } ] } ] },
      { "node": "subtree", "tree": "default/rest" } ] })";
}

const char* const kBossBindings = R"(// test bindings
{ "format": 1, "bindings": [ { "kind": "Test_Boss", "tree": "test/boss" }, { "kind": "Test_Plain_Boss", "tree": "test/boss" }, { "tree": "default" } ] })";

/** The arena of the boss cases: the support world with the boss slices, the status rows, and the test boss tree of a folder. */
struct BossArena : Arena {
    BossArena(const Options& options, const char* folder, std::vector<std::pair<std::string, std::string>> more = {}) : Arena(true, boss_world) {
        boss_statuses(*this);
        more.emplace_back("boss.jsonc", tree_file("test/boss", boss_root(), kBossBoard));
        if (std::none_of(more.begin(), more.end(), [](const auto& file) { return file.first == "bindings.jsonc"; })) {
            more.emplace_back("bindings.jsonc", kBossBindings);
        }
        tree::LoadReport report;
        loaded = behavior().reload_trees(folder_with(options, folder, more).string(), report) && report.refused.empty();
        refused = report.refused;
    }
    bool loaded{false};
    std::vector<std::string> refused;
    [[nodiscard]] float key(const world::Entity& creature, const char* name) const {
        const world::CreatureBrain& brain = creature.creature.mind.brain;
        for (std::size_t slot = 0; slot < brain.tree->keys.size(); ++slot) {
            if (brain.tree->keys[slot].name == name) {
                return brain.board[slot];
            }
        }
        return -1.0F;
    }
    [[nodiscard]] static bool has_effect(const world::Entity& creature, const char* status) {
        return std::any_of(creature.creature.effects.begin(), creature.creature.effects.end(),
                           [&](const world::CreatureEffect& effect) { return effect.status == status; });
    }
};

/** @return The names of the attacks that a creature started from log line `from` on ("starts the attack NAME"). */
std::vector<std::string> attacks_after(std::size_t from, const world::Entity& creature) {
    char id[24];
    std::snprintf(id, sizeof(id), "%016llx", static_cast<unsigned long long>(creature.id));
    std::vector<std::string> names;
    for (std::size_t index = from; index < log_lines().size(); ++index) {
        const std::string& line = log_lines()[index];
        const std::size_t at = line.find("starts the attack ");
        if (at != std::string::npos && line.find(id) != std::string::npos) {
            const std::size_t start = at + 18;
            names.push_back(line.substr(start, line.find(' ', start) - start));
        }
    }
    return names;
}

// ---- BT3A: the loader ------------------------------------------------------------------------------------------------------------------

void loader(const Options&) {
    const std::string board = R"({ "phase": { "type": "int", "default": 1 }, "rate": { "type": "float" } })";
    // Each boss node kind loads with its members.
    const std::string good = R"({ "node": "selector", "children": [
        { "node": "phase", "health_below": 50, "key": "phase", "to": 2, "child": { "node": "action", "action": "Attack_A", "pause": 0.5 } },
        { "node": "sequence", "memory": true, "children": [ { "node": "tag_set", "tag": "Hot" }, { "node": "tag_set", "tag": "Cold", "value": false } ] },
        { "node": "sequence", "children": [ { "node": "tag_is", "tag": "Hot" }, { "node": "health_below", "percent": 30 }, { "node": "has_status", "status": "Test_Boss_Phase_2" } ] },
        { "node": "weighted_random", "children": [ { "weight": 3, "node": "cooldown", "seconds": 8, "child": { "node": "action", "action": "Attack_B" } },
                                                   { "weight": 1, "node": "once", "child": { "node": "event", "event": "Phase1_started" } } ] },
        { "node": "sequence", "children": [ { "node": "timer", "seconds": 180 }, { "node": "timer", "seconds": 20, "since": "phase" } ] },
        { "node": "target", "policy": "threat" },
        { "node": "keep_target", "lose_distance": 60, "arena": 40 } ] })";
    tree::TreeFile file;
    std::string error;
    const bool loaded = tree::load_tree_file(tree_file("test/good", good, board), "good.jsonc", file, error);
    check(loaded, "a file with each boss node loads (" + error + ")");
    std::map<std::string, tree::TreeFile, std::less<>> files{{file.name, file}};
    const std::shared_ptr<const tree::CompiledTree> compiled = loaded ? tree::compile(file, files, error) : nullptr;
    check(compiled != nullptr && compiled->tags == std::vector<std::string>{"Hot", "Cold"} && compiled->statuses == std::vector<std::string>{"Test_Boss_Phase_2"}
              && compiled->events == std::vector<std::string>{"Phase1_started"} && compiled->states == 5 && compiled->actions.size() == 2
              && compiled->actions[0].kind == tree::NodeKind::action && compiled->sends("Phase1_started") && !compiled->sends("Enc_Complete"),
          "the compiled tree has its tags (Hot, Cold), its named status, its named event, five node states (the sequence with memory, the "
          "weighted choice, the cooldown, the once node, the target), and the two named actions");
    if (compiled != nullptr) {
        std::size_t weighted = 0;
        float weights = 0.0F;
        for (const tree::Node& node : compiled->nodes) {
            weighted += node.weight > 0.0F ? 1 : 0;
            weights += node.weight;
        }
        const auto keep = tree::parameters(*compiled, static_cast<std::size_t>(compiled->firstOfKind[static_cast<std::size_t>(tree::NodeKind::keep_target)]));
        check(weighted == 2 && weights == 4.0F && keep.size() == 2 && keep[1].first == "arena" && keep[1].second == 40.0F,
              "the weights of the two children of the weighted choice (3 and 1), and the arena 40 of keep_target");
    }
    struct Bad {
        std::string root;
        std::string cause;
    };
    const std::vector<Bad> bad = {
        {R"({ "node": "action" })", "root/action: the node action must have \"action\""},
        {R"({ "node": "action", "action": "Bad name" })", "root/action: the node action must have"},
        {R"({ "node": "phase", "health_below": 50, "key": "rate", "to": 2, "child": { "node": "roam" } })", "root/key: the key of a phase must be a key of the type int"},
        {R"({ "node": "phase", "health_below": 50, "key": "phase", "child": { "node": "roam" } })", "root/to: the node phase must have \"to\""},
        {R"({ "node": "phase", "health_below": 150, "key": "phase", "to": 2, "child": { "node": "roam" } })", "root/health_below: 150 is not in the range 0 to 100"},
        {R"({ "node": "weighted_random", "children": [ { "node": "roam" } ] })", "root/children/0: the parameter \"weight\" is not there"},
        {R"({ "node": "selector", "children": [ { "node": "roam", "weight": 2 } ] })", "root/children/0/weight: the node roam has no member \"weight\""},
        {R"({ "node": "weighted_random", "children": [ { "node": "roam", "weight": 0 } ] })", "root/children/0/weight: 0 is not in the range"},
        {R"({ "node": "target", "policy": "farthest" })", "root/policy: the node target must have \"policy\""},
        {R"({ "node": "timer", "seconds": 5, "since": "spawn" })", "root/since: the member since of a timer must be"},
        {R"({ "node": "tag_set", "tag": "Hot", "value": 1 })", "root/value: the value of a tag must be true or false"},
        {R"({ "node": "sequence", "memory": 1, "children": [ { "node": "roam" } ] })", "root/memory: the member memory of a sequence must be"},
        {R"({ "node": "cooldown", "seconds": 5 })", "root: the node cooldown must have one \"child\""},
        {R"({ "node": "event" })", "root/event: the node event must have \"event\""},
        {R"({ "node": "keep_target", "lose_distance": 60, "arena": 5000 })", "root/arena: 5000 is not in the range 0 to 1000"},
    };
    std::size_t refused = 0;
    std::string wrong;
    for (const Bad& item : bad) {
        tree::TreeFile one;
        std::string cause;
        if (!tree::load_tree_file(tree_file("test/bad", item.root, board), "bad.jsonc", one, cause) && cause.find(item.cause) != std::string::npos) {
            ++refused;
        } else {
            wrong += "[" + item.cause + "] gave [" + cause + "] ";
        }
    }
    check(refused == bad.size(), std::to_string(bad.size()) + " files with a fault of a boss node are refused with the place and the cause"
                                     + (wrong.empty() ? std::string() : ": " + wrong));
    std::string children = R"({ "node": "weighted_random", "children": [ )";
    for (int index = 0; index < 65; ++index) {
        children += std::string(index == 0 ? "" : ", ") + R"({ "weight": 1, "node": "roam" })";
    }
    tree::TreeFile many;
    const bool tooMany = tree::load_tree_file(tree_file("test/many", children + " ] }"), "many.jsonc", many, error);
    check(!tooMany && error.find("the node weighted_random has 64 children at most") != std::string::npos,
          "a weighted choice with 65 children is refused (" + error + ")");
    // The tree data of a slice (creatures.json member treeActions): [action, length, [enter], [exit]]; a value that does not fit is refused.
    gamedata::CreatureData data;
    std::vector<gamedata::CreatureSlice> slices{boss_slice(true)};
    const bool set = data.set_full(slices, {area::area_kind("Test_Boss", 0, 0x6B112222)}, {}, range::archer_levels());
    slices[0].treeActions[0].length = 0.0F;
    gamedata::CreatureData wrongData;
    const bool wrongSet = wrongData.set_full(slices, {area::area_kind("Test_Boss", 0, 0x6B112222)}, {}, range::archer_levels());
    check(set && data.slices()[0].treeActions.size() == 1 && data.slices()[0].treeActions[0].enter.size() == 2 && !wrongSet
              && wrongData.error().find("has a tree action whose action or length does not fit") != std::string::npos,
          "the tree action of a slice: its action, its length, its statuses; a length of 0 refuses the data (" + wrongData.error() + ")");
}

// ---- BT3B: the node action -------------------------------------------------------------------------------------------------------------

void action_node(const Options& options) {
    log_lines().clear();
    BossArena arena(options, "bt3b-trees");
    check(arena.loaded, "setup: the test boss tree and the bindings load");
    world::Entity* alpha = arena.character();
    world::Entity* boss = arena.creature("Test_Boss", 2.0F, 0.0F, 270.0F);
    world::Entity* plain = arena.creature("Test_Plain_Boss", -12.0F, 8.0F, 90.0F);
    check(boss != nullptr && plain != nullptr && boss->creature.mind.brain.tree->name == "test/boss" && plain->creature.mind.brain.tree->name == "default"
              && plain->creature.mind.brain.refusal.find("names the action \"Phase_Transition\"") != std::string::npos
              && plain->creature.mind.brain.refusal.find("has no attack, status action, area attack, or tree action of this name") != std::string::npos
              && log_count("world: behavior tree refused for creature") == 1,
          "the tree that names the tree action Phase_Transition: the boss slice fits it; the plain slice (no tree action) is refused with one "
          "log line, and it has the tree default");
    if (boss == nullptr) {
        return;
    }
    const std::size_t from = log_lines().size();
    arena.behavior().hurt_by(*boss, alpha->id);
    arena.until([&] { return attacks_after(from, *boss).size() >= 6; }, 900);
    Arena::hardy(*alpha);
    const std::vector<std::string> names = attacks_after(from, *boss);
    std::string text;
    for (const std::string& name : names) {
        text += name + " ";
    }
    check(names.size() >= 6 && names[0] == "Attack_A" && names[1] == "Attack_B" && names[2] == "Attack_A" && names[3] == "Attack_B",
          "the combo (a sequence with memory): Attack_A, then Attack_B in the next decision after its end, again and again: " + text);
    check(std::none_of(names.begin(), names.end(), [](const std::string& name) { return name == "Attack_C" || name == "Attack_Bash"; })
              && arena.behavior().counts().namedActions >= 6,
          "the named actions only: the default melee does not attack while the combo can start (the target is in the reach)");
    // Out of the reach: the action fails, and the default melee walks the boss to the target.
    arena.jump(state::WorldPosition{alpha->position.x - 9.0F, alpha->position.y, alpha->position.z});
    const float away = world::horizontal_distance(boss->position, alpha->position);
    const std::size_t walked = arena.behavior().counts().moves;
    arena.step(45);
    check(away > 5.0F && arena.behavior().counts().moves > walked && world::horizontal_distance(boss->position, alpha->position) < away,
          "a target out of the reach of Attack_A: the action fails and the default melee walks the boss to it");
}

// ---- BT3C: the phase ------------------------------------------------------------------------------------------------------------------

void phase_node(const Options& options) {
    log_lines().clear();
    BossArena arena(options, "bt3c-trees");
    world::Entity* alpha = arena.character();
    world::Entity* boss = arena.creature("Test_Boss", 2.0F, 0.0F, 270.0F);
    check(arena.loaded && boss != nullptr, "setup: the boss");
    if (boss == nullptr) {
        return;
    }
    arena.behavior().hurt_by(*boss, alpha->id);
    arena.step(15);
    check(arena.key(*boss, "phase") == 1.0F && arena.key(*boss, "fightLong") == 0.0F && arena.behavior().counts().phases == 0,
          "the phase 1 at the start of the fight; the timer of 2 s of the fight is not over");
    // The tree runs between the attacks: the keys of the timers come in the first decision after 2 s.
    const bool timers = arena.until([&] { return arena.key(*boss, "fightLong") == 1.0F; }, 150);
    check(timers && arena.key(*boss, "phaseLong") == 1.0F && arena.now() - boss->creature.mind.brain.fightAt >= std::chrono::seconds(2),
          "after 2 s: the timers of the fight and of the phase (the phase 1 starts with the fight) are over");
    // The combat path of a hit before the phase (a damage over time of 1000 Slash, the one damage path of a strike).
    world::Combat& combat = arena.host.world.combat();
    const world::HitResult before = combat.strike_amount(*alpha, *boss, 1000.0F, "Slash", "a test strike");
    // 55 percent: the phase 2 starts in the next decision, after the attack that plays.
    boss->vitals.health = 0.55F * boss->vitals.healthMax;
    const bool started = arena.until([&] { return boss->creature.mind.treeAction.has_value(); }, 120);
    const world::WorldClock::time_point at = arena.now();
    const float transitionHealth = boss->vitals.health;
    check(started && arena.key(*boss, "phase") == 2.0F && arena.behavior().counts().phases == 1 && boss->creature.action
              && boss->creature.action->action == kTransition && boss->creature.mind.shooting && boss->creature.mind.brain.phaseAt == at
              && log_count("starts the phase 2 of its tree test/boss") == 1 && log_count("starts the tree action Phase_Transition (3.00 s; statuses: "
                                                                                              "Test_Boss_Transition, Test_Boss_Phase_2)") == 1,
          "at 55 percent the phase 2 fires: the key, the transition (the tree action of the slice, 3 s) with its two statuses");
    check(boss->creature.action && boss->creature.action->tags == *boss->creature.body->treeActions[0].tags
              && boss->creature.action->tags != boss->creature.body->tags,
          "the record of the transition has the tag state of the tree action (the default tags of the slice too), not the tags of the slice");
    check(boss->creature.reactionsOffUntil && *boss->creature.reactionsOffUntil == boss->creature.mind.attackEnds
              && BossArena::has_effect(*boss, kTransitionStatus) && BossArena::has_effect(*boss, kPhaseStatus)
              && std::fabs(boss->creature.absorb_part(arena.now(), "Slash") - 0.7F) < 0.001F && boss->creature.absorb_part(arena.now(), "Fire") == 0.0F,
          "in the transition: no reaction until its end; the ABS parts of the two statuses (Slash 0.6 + 0.1, Fire 0)");
    // A hit with a reaction in the transition: no reaction (the empty reaction table of the data), the damage counts.
    const std::uint64_t reactions = arena.host.world.creatures().counts().reactions;
    const std::uint64_t off = arena.host.world.creatures().counts().reactionsOff;
    world::HitReaction reaction;
    reaction.plays = true;
    reaction.ruled = true;
    reaction.attack = world::HitReaction::Attack::heavy;
    reaction.power = 2;
    (void)arena.host.world.creatures().hurt(boss->id, 10.0F, alpha->id, 0, reaction);
    check(arena.host.world.creatures().counts().reactions == reactions && arena.host.world.creatures().counts().reactionsOff == off + 1
              && boss->creature.action && boss->creature.action->action == kTransition,
          "a hit in the transition plays no reaction (the count reactionsOff), and the transition goes on");
    const world::HitResult during = combat.strike_amount(*alpha, *boss, 1000.0F, "Slash", "a test strike");
    check(before.damage.dealt > 0.0F && std::fabs(during.damage.dealt - before.damage.dealt * 0.3F) < 0.01F * before.damage.dealt,
          "the damage of a hit of Slash in the transition is 0.3 of the damage before (ABS 0.7): " + std::to_string(before.damage.dealt) + " and "
              + std::to_string(during.damage.dealt));
    arena.until([&] { return !boss->creature.mind.treeAction.has_value(); }, 200);
    const float healed = boss->vitals.health;
    check(arena.now() - at >= std::chrono::milliseconds(2900) && healed >= transitionHealth + 0.44F * boss->vitals.healthMax
              && !BossArena::has_effect(*boss, kTransitionStatus) && BossArena::has_effect(*boss, kPhaseStatus),
          "the end of the transition (3 s): the heal of its ticks (2 percent each 0.1 s), its status goes at the exit, the status of the phase "
          "stays (health " + std::to_string(transitionHealth) + " to " + std::to_string(healed) + ")");
    arena.step(12);
    const world::HitResult after = combat.strike_amount(*alpha, *boss, 1000.0F, "Slash", "a test strike");
    check(arena.key(*boss, "seen") == 1.0F && std::fabs(after.damage.dealt - before.damage.dealt * 0.9F) < 0.01F * before.damage.dealt,
          "in the phase 2: tag_is Phase_2, has_status Test_Boss_Phase_2, and health_below 100 hold; the damage is 0.9 of the damage before");
    // One time in a fight: 20 percent again does not start the phase again.
    boss->vitals.health = 0.2F * boss->vitals.healthMax;
    arena.step(60);
    check(arena.behavior().counts().phases == 1 && arena.behavior().counts().treeActions == 1 && arena.key(*boss, "phaseLong") == 1.0F,
          "the phase fires one time in a fight (20 percent: no second transition); the timer of the phase counts from the phase 2");
}

// ---- BT3D: cooldown, once, weighted_random --------------------------------------------------------------------------------------------

void decorators(const Options& options) {
    log_lines().clear();
    // The dice: a child that always fails (weight 100), then two events by weight 3 and 1; a cooldown of an event; a once of an event.
    const std::string dice = R"({ "node": "selector", "children": [ { "node": "sequence", "children": [ { "node": "in_fight" },
        { "node": "keep_target", "lose_distance": 60 },
        { "node": "selector", "children": [ { "node": "cooldown", "seconds": 3, "child": { "node": "event", "event": "Cool" } }, { "node": "set", "key": "none", "value": 0 } ] },
        { "node": "selector", "children": [ { "node": "once", "child": { "node": "event", "event": "Once" } }, { "node": "set", "key": "none", "value": 0 } ] },
        { "node": "weighted_random", "children": [ { "weight": 100, "node": "tag_is", "tag": "Never" },
          { "weight": 3, "node": "event", "event": "Dice_A" }, { "weight": 1, "node": "event", "event": "Dice_B" } ] } ] },
      { "node": "subtree", "tree": "default/rest" } ] })";
    BossArena arena(options, "bt3d-trees", {{"dice.jsonc", tree_file("test/dice", dice, R"({ "none": { "type": "int" } })")}});
    world::Entity* alpha = arena.character();
    world::Entity* boss = arena.creature("Test_Boss", 2.0F, 0.0F, 270.0F);
    std::string cause;
    const world::Behavior::TreeCommand set = boss != nullptr ? arena.behavior().set_tree(*boss, "test/dice", cause) : world::Behavior::TreeCommand::unknownTree;
    check(arena.loaded && set == world::Behavior::TreeCommand::done, "setup: the boss with the tree test/dice (" + cause + ")");
    if (boss == nullptr) {
        return;
    }
    arena.behavior().hurt_by(*boss, alpha->id);
    const std::uint64_t treeSteps = arena.behavior().counts().treeSteps;
    arena.step(1800); // 60 s: a step of the behavior in the first tick of 33 ms 200 ms or more after the last one (233 ms)
    const std::uint64_t steps = arena.behavior().counts().treeSteps - treeSteps;
    const std::size_t a = log_count("sends the event Dice_A");
    const std::size_t b = log_count("sends the event Dice_B");
    const std::size_t cool = log_count("sends the event Cool");
    const std::size_t once = log_count("sends the event Once");
    note("BT3D dice A=" + std::to_string(a) + " B=" + std::to_string(b) + " cool=" + std::to_string(cool) + " once=" + std::to_string(once));
    check(a + b + 1 >= steps && a + b <= steps && steps > 250 && a > 2 * b && b > steps / 6 && a < steps * 9 / 10,
          "the weighted choice: one event in each step; the child that fails (weight 100) gives the step to an other child; Dice_A 3 to 1 "
          "against Dice_B (" + std::to_string(a) + " and " + std::to_string(b) + ")");
    check(cool >= 19 && cool <= 21 && once == 1, "the cooldown of 3 s: about 20 events in 60 s; the once node: one event in the fight");
    // A combo in a weighted choice keeps the choice: the next decision runs it again (its second action), not a new random child.
    const std::string combos = R"({ "node": "selector", "children": [ { "node": "sequence", "children": [ { "node": "in_fight" },
        { "node": "keep_target", "lose_distance": 60 },
        { "node": "weighted_random", "children": [
          { "weight": 1, "node": "sequence", "memory": true, "children": [ { "node": "action", "action": "Attack_A", "pause": 0.2 }, { "node": "action", "action": "Attack_B", "pause": 0.2 } ] },
          { "weight": 1, "node": "sequence", "memory": true, "children": [ { "node": "action", "action": "Attack_C", "pause": 0.2 }, { "node": "action", "action": "Attack_C", "pause": 0.2 } ] } ] } ] },
      { "node": "subtree", "tree": "default/rest" } ] })";
    BossArena second(options, "bt3d-combos", {{"combos.jsonc", tree_file("test/combos", combos)}});
    world::Entity* other = second.creature("Test_Boss", 2.0F, 0.0F, 270.0F);
    const world::Behavior::TreeCommand combo = other != nullptr ? second.behavior().set_tree(*other, "test/combos", cause) : world::Behavior::TreeCommand::unknownTree;
    check(combo == world::Behavior::TreeCommand::done, "setup: the tree test/combos");
    if (other == nullptr) {
        return;
    }
    const std::size_t from = log_lines().size();
    second.behavior().hurt_by(*other, second.character()->id);
    second.until([&] { return attacks_after(from, *other).size() >= 20; }, 3000);
    Arena::hardy(*second.character());
    const std::vector<std::string> names = attacks_after(from, *other);
    bool pairs = names.size() >= 20;
    for (std::size_t index = 0; pairs && index + 1 < 20; index += 2) {
        pairs = (names[index] == "Attack_A" && names[index + 1] == "Attack_B") || (names[index] == "Attack_C" && names[index + 1] == "Attack_C");
    }
    std::string text;
    for (const std::string& name : names) {
        text += name + " ";
    }
    check(pairs, "each choice of the weighted node ends its combo: the attacks come in pairs A B or C C: " + text);
}

// ---- BT3E: the target policy and the arena ---------------------------------------------------------------------------------------------

void policies(const Options& options) {
    const auto root = [](const char* policy, const char* keep) {
        return std::string(R"({ "node": "selector", "children": [ { "node": "sequence", "children": [ { "node": "in_fight" }, )") + keep
               + R"(, { "node": "target", "policy": ")" + policy + R"(", "range": 30, "every": 1 }, { "node": "turn_to_target" } ] },
                  { "node": "subtree", "tree": "default/rest" } ] })";
    };
    const char* keep = R"({ "node": "keep_target", "lose_distance": 60 })";
    BossArena arena(options, "bt3e-trees",
                    {{"threat.jsonc", tree_file("test/threat", root("threat", keep))},
                     {"nearest.jsonc", tree_file("test/nearest", root("nearest", keep))},
                     {"random.jsonc", tree_file("test/random", root("random", keep))},
                     {"arena.jsonc", tree_file("test/arena", root("threat", R"({ "node": "keep_target", "lose_distance": 60, "arena": 15 })"))}});
    arena.enter_both();
    world::Entity* alpha = arena.character();
    world::Entity* beta = arena.host.world.find_character(kCharacterB);
    Arena::hardy(*beta);
    world::Entity* boss = arena.creature("Test_Boss", 4.0F, 0.0F, 270.0F);
    std::string cause;
    check(arena.loaded && boss != nullptr && arena.behavior().set_tree(*boss, "test/threat", cause) == world::Behavior::TreeCommand::done,
          "setup: A and B, the boss with the tree test/threat");
    if (boss == nullptr) {
        return;
    }
    const std::size_t fromA = arena.a.got.size();
    const std::size_t fromB = arena.b.got.size();
    // A hits first (the target), then B does more damage: the policy threat takes B in at most 1 s.
    (void)arena.host.world.creatures().hurt(boss->id, 10.0F, alpha->id);
    arena.step(3);
    const bool first = boss->creature.mind.target == alpha->id;
    (void)arena.host.world.creatures().hurt(boss->id, 50.0F, beta->id);
    const bool switched = arena.until([&] { return boss->creature.mind.target == beta->id; }, 40);
    check(first && switched && arena.behavior().counts().targetChanges == 1,
          "threat: A hurt the boss first (its target); B did more damage, and the boss takes B in 1 s (the credit of the kill is the threat)");
    arena.step(40);
    check(movement_records(arena, arena.a, 1, *boss, fromA) >= 1 && movement_records(arena, arena.b, 2, *boss, fromB) >= 1,
          "both sessions decode the movement records of the boss (the turn to its new target; values of the creature, not of a session)");
    // nearest: A goes 1 m from the boss.
    (void)arena.behavior().set_tree(*boss, "test/nearest", cause);
    arena.behavior().hurt_by(*boss, beta->id);
    arena.jump(state::WorldPosition{boss->position.x - 1.5F, boss->position.y, boss->position.z});
    const bool nearest = arena.until([&] { return boss->creature.mind.target == alpha->id; }, 60);
    check(nearest, "nearest: the boss takes A, the nearer character");
    // random: both characters are chosen in 60 choices.
    (void)arena.behavior().set_tree(*boss, "test/random", cause);
    arena.behavior().hurt_by(*boss, alpha->id);
    std::size_t onA = 0;
    std::size_t onB = 0;
    for (int choice = 0; choice < 60; ++choice) {
        arena.step(31);
        (boss->creature.mind.target == alpha->id ? onA : onB) += 1;
        Arena::hardy(*alpha);
        Arena::hardy(*beta);
    }
    check(onA >= 10 && onB >= 10, "random: in 60 choices the boss has A " + std::to_string(onA) + " times and B " + std::to_string(onB) + " times");
    // The arena of 15 m around the home of the boss: a target in it is kept, a target out of it is lost.
    (void)arena.behavior().set_tree(*boss, "test/arena", cause);
    (void)arena.host.world.creatures().hurt(boss->id, 1000.0F, alpha->id); // A is the threat now
    arena.step(40);
    arena.jump(state::WorldPosition{boss->creature.mind.home.x - 12.0F, boss->creature.mind.home.y, boss->position.z});
    arena.step(12);
    const bool kept = boss->creature.mind.mode == world::CreatureMode::fight && boss->creature.mind.target == alpha->id;
    arena.jump(state::WorldPosition{boss->creature.mind.home.x - 18.0F, boss->creature.mind.home.y, boss->position.z});
    arena.step(12);
    // The boss stands near its home (this tree does not walk): its way back ends at once.
    check(kept && boss->creature.mind.target != alpha->id && log_count("lost its target (the target left the arena)") == 1,
          "the arena: a target 12 m from the home of the boss is kept, a target 18 m from it is lost (the target left the arena)");
}

// ---- BT3F: the reset ------------------------------------------------------------------------------------------------------------------

void reset(const Options& options) {
    log_lines().clear();
    BossArena arena(options, "bt3f-trees");
    world::Entity* alpha = arena.character();
    world::Entity* boss = arena.creature("Test_Boss", 2.0F, 0.0F, 270.0F);
    if (boss == nullptr) {
        check(false, "setup: the boss");
        return;
    }
    arena.behavior().hurt_by(*boss, alpha->id);
    arena.step(15);
    boss->vitals.health = 0.5F * boss->vitals.healthMax;
    arena.until([&] { return !boss->creature.mind.brain.fightStatuses.empty() && !boss->creature.mind.treeAction; }, 400);
    boss->vitals.health = 0.4F * boss->vitals.healthMax;
    const bool phase = arena.key(*boss, "phase") == 2.0F && boss->creature.mind.brain.tags != 0 && BossArena::has_effect(*boss, kPhaseStatus);
    check(phase, "setup: the boss in the phase 2 (its key, its tag, the status of the phase)");
    // The boss loses its target (the creatures ignore A), goes back to its anchor, and the fight ends there.
    arena.behavior().set_ignored(alpha->characterId, true);
    const bool home = arena.until([&] { return boss->creature.mind.mode == world::CreatureMode::rest; }, 600);
    arena.behavior().set_ignored(alpha->characterId, false);
    check(home && arena.key(*boss, "phase") == 1.0F && boss->creature.mind.brain.tags == 0 && !BossArena::has_effect(*boss, kPhaseStatus)
              && boss->creature.mind.brain.fightStatuses.empty() && boss->vitals.health == boss->vitals.healthMax
              && arena.behavior().counts().fightResets >= 1
              && std::all_of(boss->creature.mind.brain.states.begin(), boss->creature.mind.brain.states.end(),
                             [](const world::CreatureBrain::NodeState& state) { return state.index == 0; }),
          "the end of the fight at the anchor: the phase 1 again, no tag, the status of the phase goes, the node states start again, the full "
          "health");
    // A new fight has the phase again.
    arena.step(330); // the shelter of the ignore value does not apply; the vision of the boss takes A again
    arena.behavior().hurt_by(*boss, alpha->id);
    arena.step(9);
    boss->vitals.health = 0.5F * boss->vitals.healthMax;
    const bool again = arena.until([&] { return arena.behavior().counts().phases == 2; }, 200);
    check(again, "a new fight: the phase 2 fires again (one time in each fight)");
}

// ---- BT3G: the tree of Thorpe ---------------------------------------------------------------------------------------------------------

/** The names of the actions of the tree boss/thorpe (the slice damned_commander_thorpe of the game data has each one). */
const char* const kThorpeAttacks[] = {"Attack_SwipeL_01", "Attack_SwipeR_01", "Attack_Stab_01", "Attack_Stab_02", "Attack_SwipeL_02",
                                      "Attack_SwipeR_02", "Attack_Shoulder_Charge_Loop", "Attack_Shoulder_Charge_Outro",
                                      "Attack_Overhead_Slash", "Attack_Double_Spin", "Attack_Flurry", "Attack_Core_Explosion"};

/** A slice of the form of damned_commander_thorpe: the 12 named attacks and (with `transition`) the tree action Phase_Transition_1_to_2. */
gamedata::CreatureSlice thorpe_slice(bool transition) {
    gamedata::CreatureSlice slice = fighter_slice();
    slice.path = transition ? "slices/test/thorpe.dynamicslice" : "slices/test/commander.dynamicslice";
    slice.asset.fill(transition ? 0x5C : 0x5D);
    slice.root = transition ? 0x100000000000005CULL : 0x100000000000005DULL;
    slice.attacks.clear();
    for (const char* name : kThorpeAttacks) {
        const auto action = static_cast<std::uint32_t>(slice.actions.size());
        slice.actions.push_back({name, name, static_cast<std::int32_t>(50 + action), false});
        slice.attacks.push_back(gamedata::CreatureAttack{action, 1.2F, {boss_contact(2.4F)}});
    }
    const auto phase = static_cast<std::uint32_t>(slice.actions.size());
    slice.actions.push_back({"Phase_Transition_1_to_2", "Phase_Transition_1_to_2", 90, false});
    if (transition) {
        slice.treeActions = {gamedata::CreatureTreeAction{phase, 11.5333F, {kTransitionStatus, kPhaseStatus}, {kFloorStatus, kTransitionStatus}, std::nullopt}};
    }
    return slice;
}

void thorpe_tree(const Options&) {
    const auto world_of = [](bool transition) {
        return [transition](std::vector<gamedata::CreatureSlice>& slices, std::vector<gamedata::CreatureKind>& kinds) {
            slices.push_back(thorpe_slice(transition));
            kinds.push_back(area::area_kind("worldevent_ai_thorpe_msq", 11, 0x6D112222));
            slices.push_back(thorpe_slice(false));
            kinds.push_back(area::area_kind("Damned_LongSwordsman_Commander_Named_03GC09_ELITE", 12, 0x6E112222));
        };
    };
    log_lines().clear();
    Arena arena(true, world_of(true));
    boss_statuses(arena);
    tree::LoadReport report;
    (void)arena.behavior().reload_trees("", report);
    world::Entity* alpha = arena.character();
    world::Entity* thorpe = arena.creature("worldevent_ai_thorpe_msq", 2.0F, 0.0F, 270.0F);
    world::Entity* patrick = arena.creature("Damned_LongSwordsman_Commander_Named_03GC09_ELITE", -14.0F, 9.0F, 90.0F);
    check(thorpe != nullptr && patrick != nullptr && thorpe->creature.mind.brain.tree->name == "boss/thorpe"
              && thorpe->creature.mind.brain.binding == "kind worldevent_ai_thorpe_msq (binding 1)" && patrick->creature.mind.brain.tree->name == "default"
              && patrick->creature.mind.brain.binding == "each creature (binding 3)" && patrick->creature.mind.brain.refusal.empty(),
          "the program binds the tree boss/thorpe to the kind of Thorpe; the other commanders keep the tree default (no refusal)");
    if (thorpe == nullptr) {
        return;
    }
    const std::size_t from = log_lines().size();
    arena.behavior().hurt_by(*thorpe, alpha->id);
    arena.until([&] { return attacks_after(from, *thorpe).size() >= 16; }, 4000);
    Arena::hardy(*alpha);
    const std::vector<std::string> names = attacks_after(from, *thorpe);
    std::size_t pairs = 0;
    for (std::size_t index = 0; index + 1 < names.size(); ++index) {
        pairs += (names[index] == "Attack_SwipeL_01" && names[index + 1] == "Attack_SwipeR_01")
                         || (names[index] == "Attack_Stab_01" && names[index + 1] == "Attack_Stab_02")
                 ? 1
                 : 0;
    }
    const bool explosion = std::find(names.begin(), names.end(), "Attack_Core_Explosion") != names.end();
    std::string text;
    for (const std::string& name : names) {
        text += name + " ";
    }
    check(names.size() >= 16 && pairs >= 1 && !explosion, "Thorpe attacks with the named attacks of his tree: combos, no Attack_Core_Explosion "
                                                            "in the phase 1: " + text);
    thorpe->vitals.health = 0.59F * thorpe->vitals.healthMax;
    const bool transition = arena.until([&] { return thorpe->creature.mind.treeAction.has_value(); }, 200);
    check(transition && thorpe->creature.action && thorpe->creature.action->action == thorpe->creature.body->treeActions[0].action
              && thorpe->creature.reactionsOffUntil && log_count("starts the tree action Phase_Transition_1_to_2 (11.53 s") == 1,
          "at 60 percent Thorpe plays the transition Phase_Transition_1_to_2 (11.53 s, no reaction in it)");
    arena.until([&] { return !thorpe->creature.mind.treeAction.has_value(); }, 600);
    const std::size_t afterPhase = log_lines().size();
    thorpe->vitals.health = 0.5F * thorpe->vitals.healthMax;
    arena.until([&] { return attacks_after(afterPhase, *thorpe).size() >= 30; }, 9000);
    Arena::hardy(*alpha);
    const std::vector<std::string> later = attacks_after(afterPhase, *thorpe);
    check(std::find(later.begin(), later.end(), "Attack_Core_Explosion") != later.end(),
          "in the phase 2 (the tag Phase_2) Thorpe uses Attack_Core_Explosion too (" + std::to_string(later.size()) + " attacks)");
    // An older creatures.json: the slice of Thorpe has no tree action. The tree is refused for him (one log line), he has the tree default.
    log_lines().clear();
    Arena old(true, world_of(false));
    boss_statuses(old);
    (void)old.behavior().reload_trees("", report);
    world::Entity* oldThorpe = old.creature("worldevent_ai_thorpe_msq", 2.0F, 0.0F, 270.0F);
    check(oldThorpe != nullptr && oldThorpe->creature.mind.brain.tree->name == "default"
              && oldThorpe->creature.mind.brain.refusal.find("the tree \"boss/thorpe\" names the action \"Phase_Transition_1_to_2\"") == 0
              && log_count("world: behavior tree refused for creature") == 1,
          "with a slice with no tree action (an older creatures.json) the tree of Thorpe is refused with one log line: the tree default");
}

// ---- BT3L: the load -------------------------------------------------------------------------------------------------------------------

void load(const Options& options) {
    // 400 creatures of the boss tree (phase, combo, timers, conditions) near 50 characters for 1,000 ticks of 33 ms: the time limit of BT4.
    const std::filesystem::path folder =
        folder_with(options, "bt3l-trees", {{"boss.jsonc", tree_file("test/boss", boss_root(), kBossBoard)},
                                            {"bindings.jsonc", R"({ "format": 1, "bindings": [ { "tree": "test/boss" } ] })"}});
    ServerHost host;
    host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
    if (!host.creatures.set_full({boss_slice(true)}, {area::area_kind("Test_Boss", 0, 0x6B112222)}, {}, range::archer_levels())) {
        throw std::runtime_error("boss test: the load data is not valid: " + host.creatures.error());
    }
    host.world.set_creature_behavior(true);
    world::Behavior& behavior = host.world.creatures().behavior();
    tree::LoadReport report;
    (void)behavior.reload_trees(folder.string(), report);
    std::vector<world::Entity*> characters;
    for (unsigned index = 0; index < 50; ++index) {
        char id[40];
        std::snprintf(id, sizeof(id), "%08x-0000-4000-8000-%012x", 0x30000000U + index, index);
        add_player(host.store, "765612022554" + std::to_string(20000 + index), "Boss Load " + std::to_string(index), id,
                   "BossLoad" + std::to_string(index), std::nullopt);
        const float x = 1000.0F + 300.0F * static_cast<float>(index % 10);
        const float y = 1000.0F + 300.0F * static_cast<float>(index / 10);
        (void)host.world.spawn_character(id, state::WorldPosition{x, y, kHeight});
        characters.push_back(host.world.find_character(id));
    }
    std::vector<world::Entity*> bosses;
    for (world::Entity* character : characters) {
        Arena::hardy(*character);
        for (unsigned index = 0; index < 8; ++index) {
            const float angle = 0.785398F * static_cast<float>(index);
            const float away = 4.0F + 0.5F * static_cast<float>(index);
            const state::WorldPosition place{character->position.x + std::cos(angle) * away, character->position.y + std::sin(angle) * away, kHeight};
            bosses.push_back(host.world.creatures().spawn("Test_Boss", place, 0.0F).entity);
        }
    }
    for (int tick = 0; tick < 30; ++tick) {
        host.clock.advance(std::chrono::microseconds(33333));
        host.world.tick();
    }
    // Half of the bosses at 50 percent: their phase 2 (the transition with its statuses) comes in the measured ticks.
    for (std::size_t index = 0; index < bosses.size(); index += 2) {
        if (bosses[index] != nullptr) {
            bosses[index]->vitals.health = 0.5F * bosses[index]->vitals.healthMax;
        }
    }
    const std::size_t active = behavior.active_count();
    const std::uint64_t stepsBefore = behavior.counts().creatureSteps;
    const std::uint64_t nodesBefore = behavior.counts().treeNodes;
    const auto loadBefore = behavior.load().total;
    const auto started = std::chrono::steady_clock::now();
    for (int tick = 0; tick < 1000; ++tick) {
        host.clock.advance(std::chrono::microseconds(33333));
        host.world.tick();
        for (world::Entity* character : characters) {
            Arena::hardy(*character);
        }
    }
    const double total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    const double steps = std::chrono::duration<double, std::milli>(behavior.load().total - loadBefore).count();
    const std::uint64_t creatureSteps = behavior.counts().creatureSteps - stepsBefore;
    note("the boss trees: " + std::to_string(active) + " creatures in a fight; 1,000 ticks: the steps of the behavior " + std::to_string(steps)
         + " ms, the ticks of the world " + std::to_string(total) + " ms; " + std::to_string(creatureSteps) + " creature steps, "
         + std::to_string(behavior.counts().treeNodes - nodesBefore) + " nodes, " + std::to_string(behavior.counts().phases) + " phases, "
         + std::to_string(behavior.counts().namedActions) + " named actions");
    check(active == 400 && behavior.counts().phases >= 200 && behavior.counts().namedActions > 400 * 4,
          "400 bosses in a fight: half of them start their phase 2, and each one starts its named actions again and again");
    check(total < 4000.0 && steps < 2000.0, "the time limit of case BT4: 1,000 ticks with 400 boss fights need less than 4 s, the steps of the "
                                            "behavior less than 2 s");
}

} // namespace

std::vector<Case> boss_tree_cases() {
    return {{"BT3A", "behavior trees (BT3): the loader of the boss nodes, the lists of a compiled tree, the tree actions of a slice", loader},
            {"BT3B", "behavior trees (BT3): the node action (an attack by name, a combo with memory, the refusal of a slice with no such action)",
             action_node},
            {"BT3C", "behavior trees (BT3): the node phase (one time, the transition with its statuses, no reaction, the ABS parts), the conditions "
                     "and the timers",
             phase_node},
            {"BT3D", "behavior trees (BT3): cooldown, once, weighted_random (a combo keeps its choice)", decorators},
            {"BT3E", "behavior trees (BT3, MP-10): the target policy (threat, nearest, random) and the arena with two characters", policies},
            {"BT3F", "behavior trees (BT3): the end of a fight resets the state of the tree at the anchor", reset},
            {"BT3G", "behavior trees (BT3): the tree boss/thorpe of the program, its binding, its phase, the refusal with an older slice", thorpe_tree},
            {"BT3L", "behavior trees (BT3): the load of 400 boss trees near 50 characters, with the time limit of BT4", load}};
}

} // namespace soulwarden::backend::tests
