// Tests of stage BT1: the behavior trees of the creatures (world/behavior_tree.h, docs/systems/creature-behavior.md, section "Behavior
// trees").
//
//   BT1   the default tree: its files in the program, its nodes, its parameters (the values of behavior_rules before the trees), and the
//         schema file (each node kind and each parameter)
//   BT2   the loader: a file that is not correct is refused with its cause; a folder with a bad file (the world starts with the default
//         tree, one log line, the creatures fight); a good file of the folder replaces a tree; a folder that is not there
//   BT3   the blackboard and the general nodes (set, check, invert), the routes tree and tree/reload (the harness gate)
//   BTD1  the differential test: the trace of the decisions of the trees on six scripted fights is the stored trace of the decision code
//         before the trees (tests/golden/behavior_trace.txt; stage BT1 ran the two side by side, step by step, before that code left)
//   BT4   the load: 400 creatures in a fight near 50 characters with a time limit
//   BT5   two sessions (MP-10): two creatures with two targets, each one with its own tree state; each session observes both fights
//
// The accounts, the characters, the creature kinds, the rows, and the places are made up.

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/request_handler.h"
#include "server/http/routes.h"
#include "server/http/services.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/crafting_fixture.h"
#include "support/support_fixture.h"
#include "support/tree_fixture.h"
#include "world/behavior.h"
#include "world/behavior_tree.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace support;
using namespace trees;
namespace tree = world::tree;

// ---- BT1: the default tree ---------------------------------------------------------------------------------------------------------

void default_tree(const Options& options) {
    const std::vector<tree::EmbeddedFile>& files = tree::embedded_tree_files();
    tree::TreeSet set;
    tree::LoadReport report;
    const bool loaded = set.load("", report);
    // Stage BT2: the default tree is a set of subtrees, and the program has the binding file.
    bool subtrees = true;
    for (const char* name : {"default/rest", "default/fight", "default/status", "default/area", "default/shot", "default/cast", "default/melee"}) {
        subtrees = subtrees && set.find(name) != nullptr;
    }
    check(files.size() == 9 && loaded && report.refused.empty() && set.trees().size() == 8 && subtrees,
          "the program has the nine files of apps/backend/behavior (the tree default, its subtrees rest, fight, status, area, shot, cast, "
          "melee, and bindings.jsonc); the eight trees load and compile");
    const std::shared_ptr<const tree::CompiledTree> root = set.default_tree();
    std::string kinds;
    for (const tree::Node& node : root->nodes) {
        kinds += std::string(node.depth, ' ') + std::string(tree::kind_name(node.kind)) + (node.kind == tree::NodeKind::subtree ? " " + node.label : "")
                 + "\n";
    }
    check(kinds == "selector\n sequence\n  in_fight\n  subtree default/fight\n   sequence\n    keep_target\n    selector\n"
                   "     subtree default/status\n      status_action\n     subtree default/area\n      area_attack\n"
                   "     subtree default/shot\n      basic_shot\n     subtree default/cast\n      basic_shot\n"
                   "     subtree default/melee\n      melee\n     turn_to_target\n subtree default/rest\n  roam\n",
          "the default tree: a fight (the subtree default/fight: the target checks, then the status action, the area attack, the shot of an "
          "archer, the cast of a caster, the melee attack, the turn: the order of think before the trees), else the roam (default/rest)\n"
              + kinds);
    // The values of the namespace behavior_rules before the trees (commit 5174ff8), now node parameters.
    const auto values = [&](tree::NodeKind kind) { return root->first(kind); };
    const float* keep = values(tree::NodeKind::keep_target);
    const float* status = values(tree::NodeKind::status_action);
    const float* area = values(tree::NodeKind::area_attack);
    // The first basic_shot node has the band of an archer (default/shot), the second one the band of a caster (default/cast).
    const float* shot = values(tree::NodeKind::basic_shot);
    const float* cast = nullptr;
    for (std::size_t index = 0; index < root->nodes.size(); ++index) {
        if (root->nodes[index].kind == tree::NodeKind::basic_shot && root->values(index) != shot) {
            cast = root->values(index);
        }
    }
    const float* melee = values(tree::NodeKind::melee);
    const float* roam = values(tree::NodeKind::roam);
    using namespace tree::param;
    check(keep[kLoseDistance] == 60.0F && status[kStatusRepeat] == 20.0F && status[kStatusPause] == 1.5F && area[kAreaRepeat] == 10.0F
              && area[kAreaOnlyRepeat] == 5.0F && area[kAreaHoldMargin] == 2.0F && area[kAreaPause] == 1.5F,
          "keep_target: lose_distance 60 (kLoseDistance); status_action: repeat 20 (kStatusRepeat); area_attack: repeat 10, "
          "repeat_area_only 5, hold_margin 2 (kAreaRepeat, kAreaOnlyRepeat, kAreaHoldMargin); each pause 1.5 (kAttackPause)");
    check(cast != nullptr && shot[kArcher + kBandFar] == 20.0F && shot[kArcher + kBandHold] == 18.0F && shot[kArcher + kBandNear] == 8.0F
              && shot[kArcher + kBandMiddle] == 14.0F && shot[kArcher + kBandRepeat] == 3.0F && shot[kCaster + kBandFar] == kNoBand
              && cast[kArcher + kBandFar] == kNoBand && cast[kCaster + kBandFar] == 16.0F && cast[kCaster + kBandHold] == 14.0F
              && cast[kCaster + kBandNear] == 6.0F && cast[kCaster + kBandMiddle] == 11.0F && cast[kCaster + kBandRepeat] == 2.5F
              && shot[kRetreatPause] == 6.0F && shot[kShotPause] == 1.5F && cast[kRetreatPause] == 6.0F && cast[kShotPause] == 1.5F,
          "basic_shot: default/shot has the band of an archer 20, 18, 8, 14, repeat 3 (kShot*); default/cast the band of a caster 16, 14, 6, "
          "11, repeat 2.5 (kCast*); each one retreat_pause 6, pause 1.5");
    check(roam != nullptr && roam[kWaitShortest] == 4.0F && roam[kWaitLongest] == 12.0F && roam[kNoWayPause] == 3.0F,
          "roam (default/rest): wait_shortest 4, wait_longest 12, no_way_pause 3 (kWanderWaitShortest, kWanderWaitLongest, kRoamNoWayPause "
          "before stage BT2)");
    check(melee[kLightBeforeHeavy] == 3.0F
              && melee[kMeleePause] == std::chrono::duration<float>(world::behavior_rules::kAttackPause).count()
              && melee[kHeavyPause] == std::chrono::duration<float>(world::behavior_rules::kHeavyPause).count()
              && root->lightBeforeHeavy == 3,
          "melee: light_before_heavy 3 (kLightAttacks), pause 1.5 and heavy_pause 2.5 (behavior_rules::kAttackPause, kHeavyPause)");
    // The schema file has each node kind and each parameter of the loader (an editor checks a file with it).
    const std::filesystem::path schemaPath = options.goldenDirectory.parent_path().parent_path() / "behavior" / "tree.schema.json";
    std::ifstream input(schemaPath);
    const json schema = json::parse(input, nullptr, false);
    bool complete = schema.is_object() && schema.contains("$defs");
    std::string missing;
    for (std::size_t kind = 0; complete && kind < tree::kNodeKinds; ++kind) {
        const std::string name(tree::kind_name(static_cast<tree::NodeKind>(kind)));
        const json* def = schema["$defs"].contains(name) ? &schema["$defs"][name] : nullptr;
        if (def == nullptr) {
            missing += name + " ";
            continue;
        }
    }
    for (const char* parameter : {"lose_distance", "repeat", "pause", "repeat_area_only", "hold_margin", "retreat_pause", "light_before_heavy",
                                  "heavy_pause", "wait_shortest", "wait_longest", "no_way_pause", "action"}) {
        bool found = false;
        for (const auto& [name, def] : schema["$defs"].items()) {
            found = found || (def.contains("properties") && def["properties"].contains(parameter));
        }
        if (!found) {
            missing += parameter;
            missing += " ";
        }
    }
    check(complete && missing.empty(), "tree.schema.json has a definition of each node kind of the loader and each parameter" +
                                            (missing.empty() ? std::string() : ": missing " + missing));
}

// ---- BT2: the loader ---------------------------------------------------------------------------------------------------------------

/** A file of one tree with a root node (JSON text). */
std::string tree_file(const std::string& name, const std::string& root, const std::string& blackboard = "") {
    return "// a test tree\n{ \"format\": 1, \"tree\": \"" + name + "\"," + (blackboard.empty() ? "" : " \"blackboard\": " + blackboard + ",")
           + " \"root\": " + root + " }";
}

void loader(const Options& options) {
    struct Bad {
        std::string text;
        std::string cause;
    };
    const std::string melee = R"({ "node": "melee", "light_before_heavy": 3, "pause": 1.5, "heavy_pause": 2.5 })";
    const std::vector<Bad> bad = {
        {"{ \"format\": 1, ", "not JSON with comments"},
        {tree_file("t", R"({ "node": "selector", "children": [ { "node": "roam" }, ] })"), "not JSON with comments"},
        {"{ \"format\": 2, \"tree\": \"t\", \"root\": { \"node\": \"roam\" } }", "format: the file must have \"format\": 1"},
        {"{ \"format\": 1, \"tree\": \"bad name\", \"root\": { \"node\": \"roam\" } }", "tree: the file must have \"tree\""},
        {"{ \"format\": 1, \"tree\": \"t\", \"root\": { \"node\": \"roam\" }, \"extra\": 1 }", "a tree file has no member \"extra\""},
        {tree_file("t", R"({ "node": "dance" })"), "root/node: \"dance\" is not a node kind"},
        {tree_file("t", R"({ "node": "melee", "light_before_heavy": 3, "pause": 1.5 })"), "root: the parameter \"heavy_pause\" is not there"},
        {tree_file("t", R"({ "node": "melee", "light_before_heavy": 2.5, "pause": 1.5, "heavy_pause": 2.5 })"),
         "root/light_before_heavy: the parameter must be a whole number"},
        {tree_file("t", R"({ "node": "keep_target", "lose_distance": 5000 })"), "root/lose_distance: 5000 is not in the range 1 to 1000"},
        {tree_file("t", R"({ "node": "melee", "light_before_heavy": 3, "pause": 1.5, "heavy_pause": 2.5, "speed": 1 })"),
         "root/speed: the node melee has no member \"speed\""},
        {tree_file("t", R"({ "node": "basic_shot", "archer": { "far": 20, "hold": 18, "near": 15, "middle": 14, "repeat": 3 },
                            "caster": { "far": 16, "hold": 14, "near": 6, "middle": 11, "repeat": 2.5 }, "retreat_pause": 6, "pause": 1.5 })"),
         "root/archer: the distances of a band must be near <= middle <= hold <= far"},
        {tree_file("t", R"({ "node": "sequence", "children": [] })"), "root: the node sequence must have \"children\""},
        {tree_file("t", R"({ "node": "invert" })"), "root: the node invert must have one \"child\""},
        {tree_file("t", R"({ "node": "target_is", "kind": "wolf" })"), "root/kind: the node target_is must have \"kind\""},
        {tree_file("t", R"({ "node": "check", "key": "phase", "op": "eq", "value": 1 })"),
         "root/key: \"phase\" is not a declared key of the blackboard of the file"},
        {tree_file("t", R"({ "node": "set", "key": "phase", "value": 1.5 })", R"({ "phase": { "type": "int", "default": 1 } })"),
         "root/value: the value does not fit the type of the key \"phase\""},
        {tree_file("t", R"({ "node": "roam" })", R"({ "phase": { "type": "text" } })"), "blackboard/phase: a key must have \"type\""},
    };
    std::size_t refused = 0;
    std::string wrong;
    for (const Bad& item : bad) {
        tree::TreeFile file;
        std::string error;
        if (!tree::load_tree_file(item.text, "test.jsonc", file, error) && error.find(item.cause) != std::string::npos) {
            ++refused;
        } else {
            wrong += "[" + item.cause + "] gave [" + error + "] ";
        }
    }
    check(refused == bad.size(), std::to_string(bad.size()) + " files that are not correct are refused, each with the place in the file "
                                                                "and its cause (a trailing comma too: the library has no option for it)"
                                     + (wrong.empty() ? std::string() : ": " + wrong));
    // The compiler: a cycle of subtrees, a subtree that is not there, a tree that is too deep.
    tree::TreeSet set;
    tree::LoadReport report;
    (void)set.load("", report);
    std::string cycle;
    std::string absent;
    std::string deep;
    const bool first = set.add(tree_file("loop/a", R"({ "node": "selector", "children": [ { "node": "roam" } ] })"), "a.jsonc", cycle);
    const bool loop = set.add(tree_file("loop/a", R"({ "node": "subtree", "tree": "loop/b" })"), "a.jsonc", absent);
    const bool linked = set.add(tree_file("loop/b", R"({ "node": "subtree", "tree": "loop/a" })"), "b.jsonc", cycle);
    const bool second = set.add(tree_file("loop/a", R"({ "node": "subtree", "tree": "loop/b" })"), "a.jsonc", cycle);
    std::string nested = R"({ "node": "roam" })";
    for (int level = 0; level < 33; ++level) {
        nested = R"({ "node": "invert", "child": )" + nested + " }";
    }
    const bool tooDeep = set.add(tree_file("deep", nested), "deep.jsonc", deep);
    check(first && !loop && linked && absent.find("the subtree \"loop/b\" is not a tree of the set") != std::string::npos && !second
              && cycle.find("uses itself (a cycle") != std::string::npos && !tooDeep
              && deep.find("deeper than 32 levels") != std::string::npos && set.find("loop/a") != nullptr,
          "the compiler refuses a subtree that is not there, a cycle of subtrees (" + cycle + "), and a tree deeper than 32 levels; the set keeps "
          "its trees");
    // A folder with a file that is not correct, a good file that replaces the subtree default/fight, and a file of a different type.
    const std::filesystem::path folder = options.workDirectory / "bt2-trees";
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "broken.jsonc") << tree_file("default", R"({ "node": "melee" })");
    std::ofstream(folder / "notes.txt") << "not a tree";
    // The good file: the fight with the melee attack only, and a longer pause (a test of the parameters of a file).
    std::ofstream(folder / "fight.jsonc") << tree_file("default/fight", R"({ "node": "sequence", "children": [
        { "node": "keep_target", "lose_distance": 60 },
        { "node": "melee", "light_before_heavy": 3, "pause": 4, "heavy_pause": 4 } ] })");
    log_lines().clear();
    Arena arena;
    tree::LoadReport folderReport;
    world::Behavior& behavior = arena.behavior();
    const bool reloaded = behavior.reload_trees(folder.string(), folderReport);
    const std::shared_ptr<const tree::CompiledTree> fight = behavior.trees().find("default/fight");
    check(reloaded && folderReport.refused.size() == 1 && folderReport.refused[0].find("broken.jsonc: root: the parameter") == 0
              && log_count("world: behavior tree refused: broken.jsonc: root: the parameter \"light_before_heavy\" is not there") == 1
              && fight != nullptr && fight->source == "fight.jsonc" && behavior.trees().default_tree()->source == "embedded:default.jsonc",
          "a folder: the file that is not correct is refused with one log line (" + (folderReport.refused.empty() ? std::string() : folderReport.refused[0])
              + "), the tree default of the program stays, the good file replaces default/fight, a file of a different type is not read");
    // The creatures fight with the trees of the folder: a fighter attacks A, and its pause is the pause of the file (4 s).
    world::Entity* fighter = arena.creature("Test_Fighter", 1.5F);
    std::vector<world::WorldClock::time_point> starts;
    for (int tick = 0; tick < 400 && starts.size() < 3; ++tick) {
        const std::uint32_t before = fighter->creature.mind.attacks;
        arena.step();
        if (fighter->creature.mind.attacks != before) {
            starts.push_back(arena.now());
        }
    }
    const double gap = starts.size() == 3 ? std::chrono::duration<double>(starts[2] - starts[1]).count() : 0.0;
    check(starts.size() == 3 && gap > 5.4 && gap < 6.05 && fighter->creature.mind.brain.tree == behavior.trees().default_tree(),
          "the fighter has the tree default and fights with the subtree of the folder: 1.4 s of the attack, the pause 4 s of the file, and "
          "the steps of 200 ms (" + std::to_string(gap) + " s from one attack to the next; 3.1 s to 3.5 s with the pause 1.5 s of the program)");
    // A folder that is not there: the trees of the program.
    tree::LoadReport noFolder;
    const bool fallback = behavior.reload_trees((options.workDirectory / "no-such-folder").string(), noFolder);
    check(fallback && noFolder.refused.size() == 1 && noFolder.refused[0].find("not a folder") != std::string::npos
              && behavior.trees().find("default/fight")->source == "embedded:default-fight.jsonc"
              && fighter->creature.mind.brain.tree == behavior.trees().default_tree(),
          "a folder that is not there: one refusal, the trees of the program, and each creature has its new tree");
    // A world whose options name the folder: it starts with the default of the program and the subtree of the folder.
    world::WorldOptions worldOptions;
    worldOptions.behaviorDirectory = folder.string();
    log_lines().clear();
    ManualClock clock;
    state::Store store;
    gamedata::AppearanceTables tables;
    world::World started(clock, store, tables, worldOptions);
    check(started.creatures().behavior().trees().find("default/fight")->source == "fight.jsonc"
              && log_count("world: behavior tree refused: broken.jsonc") == 1,
          "a world with the folder in its options starts: the bad file is refused with one log line, the good file is used");
}

// ---- BT3: the blackboard, the general nodes, the routes ----------------------------------------------------------------------------

void blackboard_and_routes(const Options& options) {
    const std::filesystem::path folder = options.workDirectory / "bt3-trees";
    std::filesystem::create_directories(folder);
    // The default tree with a blackboard: the key "seen" is true after the first step of a fight, and "steps" counts nothing (a check
    // with invert); the fight is the subtree of the program.
    std::ofstream(folder / "default.jsonc") << tree_file("default", R"({ "node": "selector", "children": [
        { "node": "sequence", "name": "first", "children": [
            { "node": "in_fight" },
            { "node": "check", "key": "seen", "op": "eq", "value": false },
            { "node": "set", "key": "seen", "value": true },
            { "node": "set", "key": "level", "value": 2 },
            { "node": "subtree", "tree": "default/fight" } ] },
        { "node": "sequence", "name": "later", "children": [
            { "node": "in_fight" },
            { "node": "invert", "child": { "node": "check", "key": "level", "op": "lt", "value": 2 } },
            { "node": "subtree", "tree": "default/fight" } ] },
        { "node": "roam" } ] })",
                                                         R"({ "seen": { "type": "bool", "default": false }, "level": { "type": "int", "default": 1 } })");
    const crafting::HarnessGate gate(true);
    Arena arena;
    server::http::Router router;
    const server::http::Services services{arena.host.store, "127.0.0.1:32210", "test-version", "", arena.host.tables, arena.host.world};
    server::http::register_dev_routes(router, services);
    const auto send = [&](const char* method, const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + (query.empty() ? "" : "?" + query);
        return server::http::handle_request(router, request);
    };
    // The reload route reads the folder of the world options: a world of a test has none, thus the route reads the program; the test
    // then loads the folder with the function of the route.
    tree::LoadReport report;
    const bool loaded = arena.behavior().reload_trees(folder.string(), report);
    world::Entity* fighter = arena.creature("Test_Fighter", 4.0F);
    const world::CreatureBrain& brain = fighter->creature.mind.brain;
    // The slots of the keys: the keys of a file in the sequence of their names (level, seen).
    const auto slot = [&](const char* name) {
        const std::vector<tree::Key>& keys = brain.tree->keys;
        return static_cast<std::size_t>(std::find_if(keys.begin(), keys.end(), [&](const tree::Key& key) { return key.name == name; })
                                        - keys.begin());
    };
    const std::size_t seen = slot("seen");
    const std::size_t level = slot("level");
    check(loaded && brain.board.size() == 2 && seen < 2 && level < 2 && brain.board[seen] == 0.0F && brain.board[level] == 1.0F,
          "a tree with two declared keys: the creature has their defaults (seen false, level 1) before its first step");
    arena.until([&] { return fighter->creature.mind.attacks > 0; }, 200);
    char query[64];
    std::snprintf(query, sizeof(query), "creature=%016llx", static_cast<unsigned long long>(fighter->id));
    const service::HttpResponse viewReply = send("GET", "/v1/dev/creatures/tree", query);
    const json view = json::parse(viewReply.body, nullptr, false);
    std::string path;
    if (view.is_object() && view.contains("nodes")) {
        for (const json& node : view["nodes"]) {
            if (node["status"] != "idle") {
                path += node["node"].get<std::string>() + "=" + node["status"].get<std::string>() + " ";
            }
        }
    }
    note("the path of the last step: " + path);
    check(brain.board[seen] == 1.0F && brain.board[level] == 2.0F && fighter->creature.mind.attacks > 0,
          "the first step of the fight sets seen and level (set), the later steps take the branch later (invert of a check), and the "
          "fighter attacks");
    check(viewReply.status == 200 && view["tree"] == "default" && view["source"] == "default.jsonc" && view["mode"] == "fight"
              && view["blackboard"]["keys"]["seen"] == 1.0 && view["blackboard"]["keys"]["level"] == 2.0
              && view["blackboard"]["facts"]["Has_Target"] == true && view["blackboard"]["facts"]["Target_Kind"] == "player"
              && view["nodes"].size() == brain.last.size() && view["nodes"][0]["node"] == "selector"
              && (view["step"]["cause"] == "tree" || view["step"]["cause"] == "an action plays"),
          "the route tree: the tree and its file, the mode, the last step, each node with its result and parameters, the declared keys, "
          "and the facts (Has_Target, Target_Kind)");
    check(path.find("check=failure") != std::string::npos || path.find("invert=success") != std::string::npos
              || view["step"]["cause"] == "an action plays",
          "the path of a later step: the branch first fails at its check, the branch later runs");
    const service::HttpResponse wrong = send("GET", "/v1/dev/creatures/tree", "creature=00000000000000ff");
    const service::HttpResponse reload = send("POST", "/v1/dev/creatures/tree/reload", "");
    const json reloadReply = json::parse(reload.body, nullptr, false);
    // The trees of the program: each embedded file but the binding file (stage BT2: the default tree and its seven subtrees).
    check(wrong.status == 404 && reload.status == 200 && reloadReply["refused"].empty()
              && reloadReply["loaded"].size() == tree::embedded_tree_files().size() - 1
              && reloadReply["folder"] == false && fighter->creature.mind.brain.tree->source == "embedded:default.jsonc"
              && fighter->creature.mind.brain.board.empty(),
          "the route tree/reload with the harness gate: the trees of the program again (no folder in the options of the test world), and "
          "the fighter has the new tree with no keys; a creature that is not there: 404");
    {
        const crafting::HarnessGate off(false);
        const service::HttpResponse refused = send("POST", "/v1/dev/creatures/tree/reload", "");
        check(refused.status == 403 && refused.body.find("SOULWARDEN_HARNESS=1") != std::string::npos,
              "with no harness gate the reload route is refused (403)");
    }
}

// ---- BTD1: the differential test -----------------------------------------------------------------------------------------------------

long long ms_of(world::WorldClock::time_point time) {
    return static_cast<long long>(std::chrono::duration_cast<std::chrono::milliseconds>(time.time_since_epoch()).count());
}

/** The state of each creature and the counts of the behavior: two worlds with the same decisions have the same text. */
std::string fingerprint(world::World& world) {
    std::string out;
    char line[640];
    for (const auto& [id, entity] : world.entities()) {
        if (entity.kind != world::EntityKind::creature) {
            continue;
        }
        const world::Creature& creature = entity.creature;
        const world::CreatureMind& mind = creature.mind;
        const world::Entity* target = mind.target != 0 ? world.find(mind.target) : nullptr;
        std::snprintf(line, sizeof(line),
                      "%016llx %s %.9g %.9g %.9g yaw %.9g mode %d target %s action %d/%u attack %d shooting %d closing %d retreating %d "
                      "attacks %u next %u ready %lld shot %lld area %lld status %lld ends %lld retreat %lld hold %lld blocked %u trail %zu "
                      "waypoint %u goal %d wait %lld health %.9g effects %zu cause %u decided %u\n",
                      static_cast<unsigned long long>(id), creature.kind->id.c_str(), static_cast<double>(entity.position.x),
                      static_cast<double>(entity.position.y), static_cast<double>(entity.position.z), static_cast<double>(creature.yaw),
                      static_cast<int>(mind.mode), target != nullptr ? target->name.c_str() : "-", creature.action ? static_cast<int>(creature.action->action) : -1,
                      creature.action ? creature.action->generation : 0U, mind.attack ? static_cast<int>(*mind.attack) : -1, mind.shooting ? 1 : 0,
                      mind.closing ? 1 : 0, mind.retreating ? 1 : 0, mind.attacks, mind.nextArea, ms_of(mind.ready), ms_of(mind.shotReady),
                      ms_of(mind.areaReady), ms_of(mind.statusReady), ms_of(mind.attackEnds), ms_of(mind.retreatPauseUntil),
                      mind.holdUntil ? ms_of(*mind.holdUntil) : -1LL, mind.blockedSteps, mind.trail.size(), mind.waypoint,
                      mind.roamGoal ? 1 : 0, ms_of(mind.roamWaitUntil), static_cast<double>(entity.vitals.health), creature.effects.size(),
                      static_cast<unsigned>(mind.brain.cause), static_cast<unsigned>(mind.brain.decided));
        out += line;
    }
    const world::BehaviorCounts& k = world.creatures().behavior().counts();
    std::snprintf(line, sizeof(line),
                  "counts steps %llu sensed %llu sight %llu hit %llu lost %llu noway %llu crowded %llu attacks %llu contacts %llu/%llu moves %llu "
                  "returns %llu roam %llu waypoints %llu wander %llu roamNoWay %llu holds %llu retraces %llu jumps %llu shots %llu releases %llu/%llu "
                  "retreats %llu areas %llu casts %llu/%llu/%llu status %llu marks %llu/%llu\n",
                  (unsigned long long)k.creatureSteps, (unsigned long long)k.sensed, (unsigned long long)k.targetsBySight,
                  (unsigned long long)k.targetsByHit, (unsigned long long)k.targetsLost, (unsigned long long)k.targetsNoWay,
                  (unsigned long long)k.crowded, (unsigned long long)k.attacks, (unsigned long long)k.contactsHit,
                  (unsigned long long)k.contactsMissed, (unsigned long long)k.moves, (unsigned long long)k.returns,
                  (unsigned long long)k.roamSteps, (unsigned long long)k.waypoints, (unsigned long long)k.wanderPlaces,
                  (unsigned long long)k.roamNoWay, (unsigned long long)k.holds, (unsigned long long)k.retraces, (unsigned long long)k.jumps,
                  (unsigned long long)k.shots, (unsigned long long)k.releases, (unsigned long long)k.releasesCancelled,
                  (unsigned long long)k.retreats, (unsigned long long)k.areaAttacks, (unsigned long long)k.casts,
                  (unsigned long long)k.castsCancelled, (unsigned long long)k.castsAtCaster, (unsigned long long)k.statusActions,
                  (unsigned long long)k.statusMarks, (unsigned long long)k.statusMarksCancelled);
    out += line;
    world.for_each_character([&](const world::Entity& character) {
        std::snprintf(line, sizeof(line), "character %s %.9g %.9g health %.9g\n", character.name.c_str(), static_cast<double>(character.position.x),
                      static_cast<double>(character.position.y), static_cast<double>(character.vitals.health));
        out += line;
    });
    return out;
}

/** One scripted fight: its creatures, and what A (and the commands) do in each step of 33 ms. */
struct Script {
    const char* name;
    int ticks;
    std::function<void(Arena&)> setup;
    std::function<void(Arena&, int)> tick;
};

std::vector<Script> scripts() {
    const auto knock = [](Arena& arena, std::size_t number, bool knockdown) {
        std::size_t seen = 0;
        for (auto& [id, entity] : arena.host.world.entities()) {
            if (entity.kind == world::EntityKind::creature && seen++ == number) {
                (void)arena.host.world.creatures().knock(*arena.host.world.creatures().find(id), knockdown ? 2 : 1,
                                                         std::chrono::milliseconds(knockdown ? 2000 : 700));
            }
        }
    };
    const auto health = [](Arena& arena, std::size_t number, float part) {
        std::size_t seen = 0;
        for (auto& [id, entity] : arena.host.world.entities()) {
            if (entity.kind == world::EntityKind::creature && seen++ == number) {
                world::Entity* creature = arena.host.world.creatures().find(id);
                creature->vitals.health = creature->vitals.healthMax * part;
            }
        }
    };
    return {
        {"melee: three fighters, the light and heavy attacks, the crowd, a knockdown, a walk, a target that goes too far", 900,
         [](Arena& arena) {
             arena.creature("Test_Fighter", 6.0F);
             arena.creature("Test_Fighter", 6.5F, 1.0F);
             arena.creature("Test_Fighter", 7.0F, -1.0F);
         },
         [knock](Arena& arena, int tick) {
             if (tick >= 300 && tick < 360) {
                 arena.move(-0.15F, 0.0F);
             } else if (tick == 450) {
                 knock(arena, 0, true);
                 arena.step();
             } else if (tick == 520) {
                 knock(arena, 1, false);
                 arena.step();
             } else if (tick == 600) {
                 arena.jump(at(kPlace.x - 80.0F, kPlace.y));
             } else {
                 arena.step();
             }
         }},
        {"archer: the band, the shot, the walk back, the melee attack in its reach, the walk into the band", 1100,
         [](Arena& arena) { arena.creature("Test_Archer", 10.0F); },
         [](Arena& arena, int tick) {
             if (tick >= 200 && tick < 230) {
                 arena.move(0.15F, 0.0F); // to 5.5 m: the walk back
             } else if (tick >= 450 && tick < 520) {
                 arena.move(0.2F, 0.0F); // into the reach of the bash
             } else if (tick >= 700 && tick < 800) {
                 arena.move(-0.3F, 0.0F); // away: the walk to the hold distance
             } else {
                 arena.step();
             }
         }},
        {"caster: the band of a caster, its repeat, the walk back", 800, [](Arena& arena) { arena.creature("Test_Caster", 9.0F); },
         [](Arena& arena, int tick) {
             if (tick >= 200 && tick < 230) {
                 arena.move(0.15F, 0.0F);
             } else if (tick >= 500 && tick < 580) {
                 arena.move(-0.3F, 0.0F);
             } else {
                 arena.step();
             }
         }},
        {"areas: a shaman (area and bash), a striker (only an area), a thrower (area and shot)", 1200,
         [](Arena& arena) {
             arena.creature("Test_Shaman", 9.0F);
             arena.creature("Test_Striker", 11.0F, 3.0F);
             arena.creature("Test_Thrower", 10.0F, -3.0F);
         },
         [](Arena& arena, int tick) {
             if (tick >= 400 && tick < 460) {
                 arena.move(0.0F, 0.2F);
             } else if (tick >= 800 && tick < 900) {
                 arena.move(-0.25F, 0.0F);
             } else {
                 arena.step();
             }
         }},
        {"status actions: the heal of a cleric at low health, the buff of a hound; the ghost and the knight cast at their place", 1000,
         [](Arena& arena) {
             arena.creature("Test_Cleric", 5.0F);
             arena.creature("Test_Hound", 6.0F, 2.0F);
             arena.creature("Test_Ghost", 4.0F, -2.0F);
             arena.creature("Test_Knight", 3.0F, 3.0F);
         },
         [health](Arena& arena, int tick) {
             if (tick == 150) {
                 health(arena, 0, 0.1F);
             } else if (tick == 700) {
                 health(arena, 0, 0.12F);
             }
             if (tick >= 400 && tick < 440) {
                 arena.move(-0.2F, 0.0F);
             } else {
                 arena.step();
             }
         }},
        {"roam: a wanderer near A, and a fighter that sees A later", 900,
         [](Arena& arena) {
             arena.creature("Test_Wanderer", 15.0F, 15.0F);
             arena.creature("Test_Fighter", 25.0F, 0.0F, 270.0F);
         },
         [](Arena& arena, int tick) {
             if (tick >= 300 && tick < 380) {
                 arena.move(0.2F, 0.0F); // the fighter sees A at 12 m (its back to A: a wide angle of 180 degrees)
             } else {
                 arena.step();
             }
         }},
    };
}

/** @return The 64-bit FNV-1a hash of a text. */
std::uint64_t hash_of(const std::string& text) {
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    for (const char letter : text) {
        hash = (hash ^ static_cast<unsigned char>(letter)) * 0x100000001b3ULL;
    }
    return hash;
}

/**
 * @return The trace lines of one step: for each creature whose behavior made a step now, the script, the step of 33 ms, the number of the
 *         creature, the cause and the leaf of its decision, and the hash of its full state line and of the counts of the behavior.
 */
std::string trace_of(world::World& world, std::size_t script, int tick, const std::string& fingerprintText) {
    std::string out;
    std::istringstream lines(fingerprintText);
    std::string line;
    std::string counts;
    for (std::string rest; std::getline(lines, rest);) {
        if (rest.rfind("counts ", 0) == 0) {
            counts = rest;
        }
    }
    lines.clear();
    lines.seekg(0);
    const long long now = ms_of(world.clock().now());
    std::size_t number = 0;
    char text[128];
    for (const auto& [id, entity] : world.entities()) {
        if (entity.kind != world::EntityKind::creature || std::getline(lines, line).fail()) {
            continue;
        }
        const world::CreatureBrain& brain = entity.creature.mind.brain;
        if (ms_of(brain.at) == now) {
            std::snprintf(text, sizeof(text), "%zu %d %zu %u %u %016llx\n", script, tick, number, static_cast<unsigned>(brain.cause),
                          static_cast<unsigned>(brain.decided), static_cast<unsigned long long>(hash_of(line + counts)));
            out += text;
        }
        ++number;
    }
    return out;
}

void differential(const Options& options) {
    // Stage BT1 ran the decision code before the trees and the trees side by side in two worlds on these scripts and compared the state
    // of each creature after each step of 33 ms (5,900 steps, no difference: evidence/behavior-trees/bt1/implement). The decision code
    // before the trees then left the program; the stored trace is its trace (one line for each step of the behavior of a creature: the
    // cause, the leaf, and the hash of the state of the creature and of the counts of the behavior).
    const std::filesystem::path tracePath = options.goldenDirectory / "behavior_trace.txt";
    std::string trace;
    std::size_t steps = 0;
    std::set<unsigned> decided;
    const std::vector<Script> all = scripts();
    for (const Script& script : all) {
        Arena trees;
        script.setup(trees);
        for (int tick = 0; tick < script.ticks; ++tick) {
            script.tick(trees, tick);
            Arena::hardy(*trees.character());
            const std::string mine = fingerprint(trees.host.world);
            const std::string lines = trace_of(trees.host.world, static_cast<std::size_t>(&script - all.data()), tick, mine);
            trace += lines;
            steps += static_cast<std::size_t>(std::count(lines.begin(), lines.end(), '\n'));
            for (const auto& [id, entity] : trees.host.world.entities()) {
                if (entity.kind == world::EntityKind::creature && ms_of(entity.creature.mind.brain.at) == ms_of(trees.now())) {
                    decided.insert(entity.creature.mind.brain.decided + 100U * static_cast<unsigned>(entity.creature.mind.brain.cause));
                }
            }
        }
    }
    std::string kinds;
    for (const unsigned value : decided) {
        kinds += std::to_string(value) + " ";
    }
    note(std::to_string(steps) + " steps of the behavior of a creature in the trace; the causes and leaves (100 x cause + 1 + leaf): " + kinds);
    if (options.writeTrace) {
        std::ofstream(tracePath, std::ios::binary) << trace;
    }
    std::ifstream stored(tracePath, std::ios::binary);
    std::stringstream text;
    text << stored.rdbuf();
    std::string difference;
    if (text.str() != trace) {
        std::istringstream a(text.str());
        std::istringstream b(trace);
        std::string lineA;
        std::string lineB;
        for (std::size_t number = 1; difference.empty(); ++number) {
            const bool hasA = static_cast<bool>(std::getline(a, lineA));
            const bool hasB = static_cast<bool>(std::getline(b, lineB));
            if (!hasA && !hasB) {
                break;
            }
            if (!hasA || !hasB || lineA != lineB) {
                difference = "line " + std::to_string(number) + " (script, step, creature, cause, leaf, hash):\n  stored: " + (hasA ? lineA : "(end)")
                             + "\n  now:    " + (hasB ? lineB : "(end)");
            }
        }
    }
    check(stored.good() && text.str() == trace,
          "the trace of the decisions of the trees on six scripted fights is the stored trace of the decision code before the trees ("
              + tracePath.filename().string() + ", " + std::to_string(steps) + " lines)" + (difference.empty() ? std::string() : ": " + difference));
    // 100 x the cause (1 tree, 2 held, 4 an action plays, 5 the way back) + 1 + the leaf (7 keep_target, 8 status_action, 9 area_attack,
    // 10 basic_shot, 11 melee, 13 roam). The turn of a creature with no melee attack at a summon, and the hit stun, have their cases
    // (CRG, RT5) and are not in the scripts.
    check(kinds == "108 109 110 111 112 114 200 400 500 ",
          "the scripts make each leaf decide (the target checks, the status action, the area attack, the shot, the melee attack, the roam) "
          "and each rule of the engine (held, an action plays, the way back)");
}

// ---- BT4: the load ------------------------------------------------------------------------------------------------------------------

/** @return The real time of the steps of the behavior (ms) of 400 creatures in a fight near 50 characters for 1,000 ticks of 33 ms. */
double fight_load(std::size_t& active, std::uint64_t& creatureSteps, std::uint64_t& attacks, std::uint64_t& nodes, double& total) {
    ServerHost host;
    host.terrain.source = [](float, float) { return std::optional<float>(kHeight); };
    if (!host.creatures.set_full({fighter_slice()}, {area::area_kind("Test_Fighter", 0, 0x61112222)}, {}, range::archer_levels())) {
        throw std::runtime_error("tree test: the load data is not valid: " + host.creatures.error());
    }
    host.world.set_creature_behavior(true);
    std::vector<world::Entity*> characters;
    for (unsigned index = 0; index < 50; ++index) {
        char id[40];
        std::snprintf(id, sizeof(id), "%08x-0000-4000-8000-%012x", 0x20000000U + index, index);
        add_player(host.store, "765612022553" + std::to_string(20000 + index), "Load " + std::to_string(index), id,
                   "Load" + std::to_string(index), std::nullopt);
        const float x = 1000.0F + 300.0F * static_cast<float>(index % 10);
        const float y = 1000.0F + 300.0F * static_cast<float>(index / 10);
        (void)host.world.spawn_character(id, state::WorldPosition{x, y, kHeight});
        characters.push_back(host.world.find_character(id));
    }
    for (world::Entity* character : characters) {
        Arena::hardy(*character);
        // Eight creatures around each character, 4 m to 8 m from it: each one sees the character and fights it.
        for (unsigned index = 0; index < 8; ++index) {
            const float angle = 0.785398F * static_cast<float>(index);
            const float away = 4.0F + 0.5F * static_cast<float>(index);
            const state::WorldPosition place{character->position.x + std::cos(angle) * away, character->position.y + std::sin(angle) * away, kHeight};
            (void)host.world.creatures().spawn("Test_Fighter", place, 0.0F);
        }
    }
    world::Behavior& behavior = host.world.creatures().behavior();
    for (int tick = 0; tick < 30; ++tick) {
        host.clock.advance(std::chrono::microseconds(33333));
        host.world.tick();
    }
    active = behavior.active_count();
    const std::uint64_t stepsBefore = behavior.counts().creatureSteps;
    const std::uint64_t attacksBefore = behavior.counts().attacks;
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
    total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    creatureSteps = behavior.counts().creatureSteps - stepsBefore;
    attacks = behavior.counts().attacks - attacksBefore;
    nodes = behavior.counts().treeNodes - nodesBefore;
    return std::chrono::duration<double, std::milli>(behavior.load().total - loadBefore).count();
}

/** The best of three runs of fight_load (the real time of a run changes with the load of the computer). */
struct LoadRun {
    std::size_t active{0};
    std::uint64_t steps{0};
    std::uint64_t attacks{0};
    std::uint64_t nodes{0};
    double behavior{0.0};
    double ticks{0.0};
};

LoadRun best_load() {
    LoadRun best;
    for (int run = 0; run < 3; ++run) {
        LoadRun item;
        item.behavior = fight_load(item.active, item.steps, item.attacks, item.nodes, item.ticks);
        if (run == 0 || item.behavior < best.behavior) {
            best = item;
        }
    }
    return best;
}

void load(const Options&) {
    const LoadRun trees = best_load();
    const auto each = [](const LoadRun& run) { return run.behavior * 1000.0 / static_cast<double>(std::max<std::uint64_t>(run.steps, 1)); };
    note("the trees: 400 creatures, " + std::to_string(trees.active) + " in a fight; 1,000 ticks of 33 ms (33 s of the world), the best of 3 "
         "runs: the steps of the behavior " + std::to_string(trees.behavior) + " ms, the ticks of the world " + std::to_string(trees.ticks)
         + " ms; " + std::to_string(trees.steps) + " creature steps (" + std::to_string(each(trees)) + " us each), " + std::to_string(trees.nodes)
         + " nodes, " + std::to_string(trees.attacks) + " attacks");
    // A step of the behavior comes in the first tick of 33 ms that is 200 ms or more after the last one: 143 steps in 1,000 ticks.
    check(trees.active == 400 && trees.steps == 400ULL * 143ULL && trees.attacks > 400 * 8,
          "400 creatures in a fight: each one in each step of the behavior (143 steps in 1,000 ticks), and each one attacks again and again");
    // Stage BT1 measured the decision code before the trees in this world too (the same steps and attacks; the cost before and after is
    // in evidence/behavior-trees/bt1/implement): the trees need about 0.05 us more for each step of a creature.
    check(trees.ticks < 4000.0 && trees.behavior < 2000.0,
          "the time limit: 1,000 ticks of the world with 400 fights need less than 4 s, the steps of the behavior less than 2 s");
}

// ---- BT5: two sessions --------------------------------------------------------------------------------------------------------------

void two_sessions(const Options&) {
    Arena arena;
    arena.enter_both();
    world::Entity* alpha = arena.character();
    world::Entity* beta = arena.host.world.find_character(kCharacterB);
    Arena::hardy(*beta);
    // A fighter and an archer that do not see A and B (their backs to them); a hit gives each one its target: the fighter A, the archer B.
    world::Entity* fighter = arena.creature("Test_Fighter", 3.0F, 0.0F, 270.0F);
    world::Entity* archer = arena.creature("Test_Archer", -11.0F, 1.0F, 90.0F);
    world::Behavior& behavior = arena.behavior();
    behavior.hurt_by(*fighter, alpha->id);
    behavior.hurt_by(*archer, beta->id);
    const std::size_t aBefore = arena.a.received.size();
    const std::size_t bBefore = arena.b.received.size();
    const std::size_t aFrom = arena.a.got.size();
    const std::size_t bFrom = arena.b.got.size();
    arena.until([&] { return fighter->creature.mind.attacks >= 2 && archer->creature.mind.attacks >= 1; }, 600);
    Arena::hardy(*alpha);
    Arena::hardy(*beta);
    const world::CreatureBrain& one = fighter->creature.mind.brain;
    const world::CreatureBrain& two = archer->creature.mind.brain;
    check(fighter->creature.mind.target == alpha->id && archer->creature.mind.target == beta->id && fighter->creature.mind.attacks >= 2
              && archer->creature.mind.attacks >= 1 && one.tree == two.tree && one.tree == behavior.trees().default_tree()
              && one.cause != world::StepCause::none && two.cause != world::StepCause::none && one.last.data() != two.last.data(),
          "two creatures fight two characters with one tree: each one has its own target and its own state of the nodes (values of the "
          "creature in the world, not of a session)");
    // Code review of stage BT1, finding R8: a count of packets passed with no creature record at all. Each session decodes the records of
    // the movement state (the actions of the attacks) of both creatures.
    const std::size_t aFighter = movement_records(arena, arena.a, 1, *fighter, aFrom);
    const std::size_t aArcher = movement_records(arena, arena.a, 1, *archer, aFrom);
    const std::size_t bFighter = movement_records(arena, arena.b, 2, *fighter, bFrom);
    const std::size_t bArcher = movement_records(arena, arena.b, 2, *archer, bFrom);
    check(arena.a.received.size() > aBefore + 10 && arena.b.received.size() > bBefore + 10 && aFighter >= 2 && aArcher >= 1 && bFighter >= 2
              && bArcher >= 1,
          "each session decodes the records of the movement state of both fights (A: fighter " + std::to_string(aFighter) + ", archer "
              + std::to_string(aArcher) + "; B: fighter " + std::to_string(bFighter) + ", archer " + std::to_string(bArcher)
              + "; one path: the encoders of the creatures; a tree has no state in a session)");
    const std::uint32_t bFighterInterest = range::interest_in(arena.host, 2, fighter->id);
    const std::uint32_t bArcherInterest = range::interest_in(arena.host, 2, archer->id);
    // B goes 200 m away: only the archer loses its target and goes back; the fighter fights A on.
    const std::uint64_t lostBefore = behavior.counts().targetsLost;
    const std::size_t aLater = arena.a.got.size();
    const std::size_t bLater = arena.b.got.size();
    (void)arena.host.world.set_position(beta->id, at(kPlace.x - 200.0F, kPlace.y), false);
    // The shot that plays ends first (2.47 s): the engine holds the archer, then its tree loses the target.
    arena.until([&] { return archer->creature.mind.mode != world::CreatureMode::fight; }, 150);
    arena.step(3);
    check(archer->creature.mind.mode != world::CreatureMode::fight && archer->creature.mind.target == 0
              && behavior.counts().targetsLost == lostBefore + 1 && fighter->creature.mind.mode == world::CreatureMode::fight
              && fighter->creature.mind.target == alpha->id && log_count("lost its target (the target is too far)") == 1,
          "B goes 200 m away: the archer loses its target (keep_target: too far) and goes back; the fighter keeps A");
    // The records after the travel: B decodes the stops of both creatures (they are not near it); A decodes the records of the next attack
    // of the fighter.
    const std::uint32_t attacks = fighter->creature.mind.attacks;
    arena.until([&] { return fighter->creature.mind.attacks > attacks; }, 200);
    arena.step(3);
    check(range::stops_of(arena.b, bFighterInterest, bLater) >= 1 && range::stops_of(arena.b, bArcherInterest, bLater) >= 1
              && fighter->creature.mind.attacks > attacks && movement_records(arena, arena.a, 1, *fighter, aLater) >= 1,
          "after the travel of B: B decodes the stops of both creatures, and A decodes the record of the next attack of the fighter");
}

} // namespace

std::vector<Case> behavior_tree_cases() {
    return {{"BT1", "behavior trees (BT1): the default tree in the program, its nodes and parameters (the values before the trees), the schema",
             default_tree},
            {"BT2", "behavior trees (BT1): the loader refuses a file that is not correct with its cause; a folder of a development run",
             loader},
            {"BT3", "behavior trees (BT1): the blackboard, set, check, invert, the routes tree and tree/reload (the harness gate)",
             blackboard_and_routes},
            {"BTD1", "behavior trees (BT1): the differential test of six scripted fights and the stored trace of the decisions", differential},
            {"BT4", "behavior trees (BT1): the load of 400 creatures in a fight near 50 characters, with a time limit", load},
            {"BT5", "behavior trees (BT1, MP-10): two sessions, two fights, the tree state of each creature", two_sessions}};
}

} // namespace soulwarden::backend::tests
