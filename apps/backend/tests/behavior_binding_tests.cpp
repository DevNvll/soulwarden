// Tests of stage BT2: the binding of a behavior tree to a creature, the subtrees of the default tree, the refusal of a tree that names an
// action that the slice does not have, and the tree command (world/tree_binding.h, world/behavior.h, docs/systems/creature-behavior.md,
// section "Behavior trees").
//
//   BTB1  the binding file: each key (kind, vitals, slice, serverTree, creatureType, family, the binding with no key, no binding), the
//         order of the keys, the first server tree of a slice, no difference of case, the place in the file does not count; the files that
//         are refused (a tie, two keys, a tree that is not in the set, ...); the member serverTrees of creatures.json
//   BTB2  the bindings in a world: a folder with a binding file and a test tree; the creature of a kind and of a server tree gets the test
//         tree, the others the tree default; the view of the route and of the admin state; a binding file that is not correct keeps the
//         bindings of the program
//   BTB3  the refusal: a tree that names a status action or an area attack that the slice does not have is refused for that creature (one
//         log line for each tree and slice; the creature has the tree default); a named action is the only action of its node
//   BTB4  the command tree/set: the harness gate, a name that is not a tree, a refused tree, the tree default again, a reload
//   BTB5  two sessions (MP-10): two creatures with two trees of the binding file, two targets; each session decodes the records of both
//   BTB6  the subtrees: the shot of an archer and the cast of a caster in their own subtrees, a node with no band, the waits of the roam as
//         node parameters, a folder that replaces one subtree
//
// The accounts, the characters, the creature kinds, the slices, the trees, and the places are made up.

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "server/http/character_commands.h"
#include "server/http/request_handler.h"
#include "server/http/routes.h"
#include "server/http/services.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/crafting_fixture.h"
#include "support/tree_fixture.h"
#include "world/behavior.h"
#include "world/behavior_tree.h"
#include "world/tree_binding.h"

namespace soulwarden::backend::tests {
namespace {

using nlohmann::json;
using namespace trees;
namespace tree = world::tree;

/** A file of one tree with a root node (JSON text). */
std::string tree_text(const std::string& name, const std::string& root) {
    return "// a test tree\n{ \"format\": 1, \"tree\": \"" + name + "\", \"root\": " + root + " }";
}

/** A binding file with these bindings (JSON text of the list). */
std::string binding_text(const std::string& bindings) {
    return "// a test binding file\n{ \"format\": 1, \"bindings\": [ " + bindings + " ] }";
}

/** A tree that fights with no attack: the target checks, then it turns to its target; at rest the roam of the program. */
const char* const kStillRoot = R"({ "node": "selector", "children": [
    { "node": "sequence", "children": [ { "node": "in_fight" }, { "node": "keep_target", "lose_distance": 60 }, { "node": "turn_to_target" } ] },
    { "node": "subtree", "tree": "default/rest" } ] })";

/** A folder of the cases with the files (name, text); an earlier folder of the name goes first. */
std::filesystem::path folder_with(const Options& options, const char* name, const std::vector<std::pair<std::string, std::string>>& files) {
    const std::filesystem::path folder = options.workDirectory / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    for (const auto& [file, text] : files) {
        std::ofstream(folder / file, std::ios::binary) << text;
    }
    return folder;
}

/** The routes of the development commands of a world of a case. */
struct Routes {
    explicit Routes(Arena& arena)
        : services{arena.host.store, "127.0.0.1:32210", "test-version", "", arena.host.tables, arena.host.world} {
        server::http::register_dev_routes(router, services);
    }
    service::HttpResponse send(const char* method, const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + (query.empty() ? "" : "?" + query);
        return server::http::handle_request(router, request);
    }
    server::http::Router router;
    server::http::Services services;
};

std::string id_query(const world::Entity& creature) {
    char text[40];
    std::snprintf(text, sizeof(text), "creature=%016llx", static_cast<unsigned long long>(creature.id));
    return text;
}

// ---- BTB1: the binding file ------------------------------------------------------------------------------------------------------------

gamedata::CreatureKind binding_kind(const char* id, const char* vitals, const char* type, const char* family) {
    gamedata::CreatureKind kind;
    kind.id = id;
    kind.vitals = vitals;
    kind.creatureType = type;
    kind.family = family;
    return kind;
}

gamedata::CreatureSlice binding_slice(const char* path, std::vector<std::string> serverTrees) {
    gamedata::CreatureSlice slice;
    slice.path = path;
    slice.serverTrees = std::move(serverTrees);
    return slice;
}

void binding_file(const Options& options) {
    tree::TreeSet set;
    tree::LoadReport report;
    bool added = set.load("", report);
    std::string error;
    for (const char* name : {"t/kind", "t/vitals", "t/slice", "t/server", "t/bomb", "t/type", "t/family", "t/each"}) {
        added = set.add(tree_text(name, R"({ "node": "roam" })"), std::string(name) + ".jsonc", error) && added;
    }
    const std::string bindings = R"(
        { "family": "Wolf", "tree": "t/family" },
        { "tree": "t/each" },
        { "creatureType": ["EliteBoss", "EliteMiniBoss"], "tree": "t/type" },
        { "serverTree": "bomber/bomber_explode", "tree": "t/bomb" },
        { "serverTree": "wolf/wolf_base", "tree": "t/server" },
        { "slice": "slices/test/wolf.dynamicslice", "tree": "t/slice" },
        { "vitals": "Test_Wolf", "tree": "t/vitals" },
        { "kind": ["Test_Wolf_7", "Test_Wolf_9"], "tree": "t/kind" })";
    tree::BindingSet file;
    const bool parsed = tree::BindingSet::parse(binding_text(bindings), "bindings.jsonc", set, file, error);
    check(added && parsed && file.bindings().size() == 8, "a binding file with each key loads (" + error + ")");
    // The place in the file does not change a selection: the same bindings in the other sequence.
    std::string reversed;
    {
        const json list = json::parse("[" + bindings + "]");
        for (auto item = list.rbegin(); item != list.rend(); ++item) {
            reversed += (reversed.empty() ? "" : ", ") + item->dump();
        }
    }
    tree::BindingSet backwards;
    const bool parsedBack = tree::BindingSet::parse(binding_text(reversed), "back.jsonc", set, backwards, error);
    struct Row {
        gamedata::CreatureKind kind;
        gamedata::CreatureSlice slice;
        const char* tree;
        const char* text;
    };
    const gamedata::CreatureSlice wolf = binding_slice("slices/test/wolf.dynamicslice", {"wolf/wolf_base"});
    const std::vector<Row> rows = {
        {binding_kind("Test_Wolf_7", "Test_Wolf", "EliteMiniBoss", "Wolf"), wolf, "t/kind", "kind Test_Wolf_7 (binding 8)"},
        {binding_kind("Test_Wolf_8", "Test_Wolf", "EliteMiniBoss", "Wolf"), wolf, "t/vitals", "vitals Test_Wolf (binding 7)"},
        {binding_kind("Test_Wolf_8", "Test_Alpha", "EliteMiniBoss", "Wolf"), wolf, "t/slice", "slice slices/test/wolf.dynamicslice (binding 6)"},
        {binding_kind("Test_Wolf_8", "Test_Alpha", "EliteMiniBoss", "Wolf"), binding_slice("slices/test/alpha.dynamicslice", {"WOLF/Wolf_Base"}),
         "t/server", "serverTree WOLF/Wolf_Base (binding 5)"},
        // The first server tree of the slice that has a binding: the base tree, not the explode tree (a rule of this project).
        {binding_kind("Test_Bomber", "Test_Bomber", "Solo", "Undead"),
         binding_slice("slices/test/bomber.dynamicslice", {"wolf/wolf_base", "bomber/bomber_explode"}), "t/server",
         "serverTree wolf/wolf_base (binding 5)"},
        {binding_kind("Test_Bomber", "Test_Bomber", "Solo", "Undead"),
         binding_slice("slices/test/bomber.dynamicslice", {"bomber/bomber_base", "bomber/bomber_explode"}), "t/bomb",
         "serverTree bomber/bomber_explode (binding 4)"},
        {binding_kind("Test_Wolf_8", "Test_Alpha", "EliteMiniBoss", "Wolf"), binding_slice("slices/test/alpha.dynamicslice", {}), "t/type",
         "creatureType EliteMiniBoss (binding 3)"},
        {binding_kind("Test_Wolf_8", "Test_Alpha", "Solo", "wolf"), binding_slice("slices/test/alpha.dynamicslice", {}), "t/family",
         "family wolf (binding 1)"},
        {binding_kind("Test_Boar", "Test_Boar", "Solo", "Boar"), binding_slice("slices/test/boar.dynamicslice", {}), "t/each",
         "each creature (binding 2)"},
    };
    std::string wrong;
    for (const Row& row : rows) {
        const tree::Selection one = file.select(row.kind, row.slice);
        const tree::Selection two = backwards.select(row.kind, row.slice);
        if (one.tree() != row.tree || one.text() != row.text || two.tree() != row.tree) {
            wrong += "[" + row.kind.id + " " + row.slice.path + ": " + std::string(one.tree()) + ", " + one.text() + ", backwards "
                     + std::string(two.tree()) + "] ";
        }
    }
    check(parsedBack && wrong.empty(),
          "the narrowest key that matches selects the tree: kind, vitals, slice, serverTree (the first one of the slice with a binding), "
          "creatureType (a list), family (no difference of case), the binding with no key; the same in the other sequence of the file "
              + wrong);
    tree::BindingSet none;
    const bool noFallback = tree::BindingSet::parse(binding_text(R"({ "family": "Wolf", "tree": "t/family" })"), "f.jsonc", set, none, error);
    const tree::Selection nothing = none.select(rows.back().kind, rows.back().slice);
    check(noFallback && nothing.binding == nullptr && nothing.tree() == "default" && nothing.text() == "no binding"
              && tree::BindingSet().select(rows.front().kind, rows.front().slice).tree() == "default",
          "no binding that matches and no binding with no key, or no binding file: the tree default");
    // The files that are refused, each with its place and cause.
    struct Bad {
        std::string text;
        std::string cause;
    };
    const std::vector<Bad> bad = {
        {binding_text(R"({ "family": "Wolf", "tree": "t/family" }, { "family": ["Boar", "WOLF"], "tree": "t/each" })"),
         "bindings/1/family: \"WOLF\" is in the binding 1 too"},
        {binding_text(R"({ "family": "Wolf", "kind": "Test_Wolf_7", "tree": "t/family" })"), "a binding has one key"},
        {binding_text(R"({ "species": "Wolf", "tree": "t/family" })"), "bindings/0/species: a binding has no member \"species\""},
        {binding_text(R"({ "family": "Wolf", "tree": "t/none" })"), "bindings/0/tree: \"t/none\" is not a tree of the set"},
        {binding_text(R"({ "family": "Wolf" })"), "bindings/0: a binding must have \"tree\""},
        {binding_text(R"({ "tree": "t/each" }, { "tree": "default" })"), "the file has two bindings with no key"},
        {binding_text(R"({ "family": [], "tree": "t/family" })"), "the value of a key must be a text or a list"},
        {binding_text(R"({ "family": [7], "tree": "t/family" })"), "each value must be a text"},
        {"{ \"format\": 2, \"bindings\": [] }", "format: the file must have \"format\": 1"},
        {"{ \"format\": 4294967297, \"bindings\": [] }", "format: the file must have \"format\": 1"},
        {"{ \"format\": 1e400, \"bindings\": [] }", "not JSON with comments"},
        {binding_text(R"({ "tree": "t/each" },)"), "not JSON with comments"},
        {"{ \"format\": 1, \"bindings\": [], \"extra\": 1 }", "a binding file has no member \"extra\""},
    };
    std::size_t refused = 0;
    std::string causes;
    for (const Bad& item : bad) {
        tree::BindingSet out;
        std::string cause;
        if (!tree::BindingSet::parse(item.text, "bad.jsonc", set, out, cause) && cause.find(item.cause) != std::string::npos) {
            ++refused;
        } else {
            causes += "[" + item.cause + "] gave [" + cause + "] ";
        }
    }
    check(refused == bad.size(), std::to_string(bad.size()) + " binding files that are not correct are refused with their cause: a value in "
                                                                "two bindings of one key (a tie, no difference of case), two keys, a member that "
                                                                "is not a key, a tree that is not in the set, ... "
                                     + causes);
    // The member serverTrees of creatures.json (stage BT2): a list of names; a member with a wrong form refuses the file.
    const std::filesystem::path folder = options.workDirectory / "btb1-creatures";
    std::filesystem::create_directories(folder);
    json document = {{"format", 1},
                     {"levels", {{1, 337.5}}},
                     {"slices",
                      {{{"path", "slices/test/wolf.dynamicslice"},
                        {"asset", "11111111-1111-1111-1111-111111111111"},
                        {"sub", 2},
                        {"root", "1000000000000011"},
                        {"interestRadius", 100.0},
                        {"movement", 5},
                        {"vitals", 9},
                        {"marker", 15},
                        {"slots", {{5, "ActionList", 11}, {9, "Vitals", 15}, {15, "Marker", 5399}}},
                        {"components", json::array()},
                        {"actions", {{"Idle_No_Task", "Idle", 0, false}, {"Death", "r_Death", 4, true}}},
                        {"serverTrees", {"wolf/wolf_base", "bomber/bomber_explode"}}}}},
                     {"kinds",
                      {{{"id", "Test_Wolf"}, {"vitals", "Test_Wolf"}, {"category", "Test_Wolf_Category"}, {"vitalsId", 0x11112222},
                        {"categoryId", 0x33334444}, {"name", "Test Wolf"}, {"vitalsName", "Test Wolf"}, {"family", "Wildlife"},
                        {"creatureType", "Solo"}, {"health", 1.0}, {"level", 1}, {"bodies", json::array({json::array({0, 100})})},
                        {"points", 1}}}},
                     {"spawnPoints", {{"00000000-0000-4000-8000-0000000000p1", 0, 1, 1100.0, 1000.0, 50.0, 90.0, 360.0, 0.0}}}};
    const auto load = [&](const char* name, const json& text, gamedata::CreatureData& data) {
        std::ofstream(folder / name, std::ios::binary) << text.dump();
        return data.load((folder / name).string());
    };
    gamedata::CreatureData good;
    const bool goodLoaded = load("trees.json", document, good);
    json noMember = document;
    noMember["slices"][0].erase("serverTrees");
    gamedata::CreatureData older;
    const bool olderLoaded = load("older.json", noMember, older);
    json badForm = document;
    badForm["slices"][0]["serverTrees"] = "wolf/wolf_base";
    gamedata::CreatureData broken;
    const bool brokenLoaded = load("broken.json", badForm, broken);
    json emptyName = document;
    emptyName["slices"][0]["serverTrees"] = json::array({""});
    gamedata::CreatureData empty;
    const bool emptyLoaded = load("empty.json", emptyName, empty);
    check(goodLoaded && good.slices().front().serverTrees == std::vector<std::string>{"wolf/wolf_base", "bomber/bomber_explode"} && olderLoaded
              && older.slices().front().serverTrees.empty() && !brokenLoaded && !emptyLoaded,
          "creatures.json: the member serverTrees of a slice (a list of names); a file of an older tool has none (each binding of a server "
          "tree then does not match); a text, or an empty name, refuses the file (" + broken.error() + ")");
}

// ---- BTB2: the bindings in a world -----------------------------------------------------------------------------------------------------

/** The kinds and the slices of BTB2 to BTB5: the caster slice names a server tree, the hound is of the family Hounds. */
void binding_world(std::vector<gamedata::CreatureSlice>& slices, std::vector<gamedata::CreatureKind>& kinds) {
    slices[2].serverTrees = {"test/caster_base"};
    kinds[7].family = "Hounds";
}

void world_bindings(const Options& options) {
    const std::filesystem::path folder = folder_with(options, "btb2-trees",
                                                     {{"still.jsonc", tree_text("test/still", kStillRoot)},
                                                      {"bindings.jsonc", binding_text(R"({ "kind": "Test_Fighter", "tree": "test/still" },
                                                                                          { "serverTree": "test/caster_base", "tree": "test/still" },
                                                                                          { "tree": "default" })")}});
    const crafting::HarnessGate gate(true);
    log_lines().clear();
    Arena arena(true, binding_world);
    Routes routes(arena);
    world::Behavior& behavior = arena.behavior();
    tree::LoadReport report;
    const bool loaded = behavior.reload_trees(folder.string(), report);
    check(loaded && report.refused.empty() && report.bindings == "bindings.jsonc, 3 binding(s)"
              && log_count("world: behavior tree bindings: bindings.jsonc, 3 binding(s)") == 1,
          "a folder with a binding file: it replaces the binding file of the program (" + report.bindings + ")");
    world::Entity* fighter = arena.creature("Test_Fighter", 3.0F);
    world::Entity* caster = arena.creature("Test_Caster", 9.0F, 3.0F);
    world::Entity* archer = arena.creature("Test_Archer", 10.0F, -3.0F);
    const world::CreatureBrain& one = fighter->creature.mind.brain;
    const world::CreatureBrain& two = caster->creature.mind.brain;
    const world::CreatureBrain& three = archer->creature.mind.brain;
    check(one.tree->name == "test/still" && one.binding == "kind Test_Fighter (binding 1)" && two.tree->name == "test/still"
              && two.binding == "serverTree test/caster_base (binding 2)" && three.tree->name == "default"
              && three.binding == "each creature (binding 3)" && one.refusal.empty(),
          "the fighter has the tree of its kind, the caster the tree of the server tree of its slice, the archer the binding with no key ("
              + one.binding + "; " + two.binding + "; " + three.binding + ")");
    // The fighter of the test tree takes its target and turns to it, and it does not attack; the archer of the tree default shoots.
    arena.until([&] { return archer->creature.mind.attacks >= 2; }, 400);
    check(fighter->creature.mind.mode == world::CreatureMode::fight && fighter->creature.mind.attacks == 0 && caster->creature.mind.attacks == 0
              && archer->creature.mind.attacks >= 2 && one.decided == 1 + static_cast<unsigned>(tree::NodeKind::turn_to_target),
          "the creatures of the test tree fight with no attack (the leaf turn_to_target decides); the archer of the tree default shoots ("
              + std::to_string(archer->creature.mind.attacks) + " shots)");
    const service::HttpResponse view = routes.send("GET", "/v1/dev/creatures/tree", id_query(*fighter));
    const json reply = json::parse(view.body, nullptr, false);
    const json state = server::http::commands::creatures_view(arena.host.world, arena.character()->characterId, 150.0F);
    std::string rows;
    for (const json& row : state["creatures"]) {
        rows += row.value("kind", std::string()) + "=" + row.value("tree", std::string()) + " (" + row.value("treeBinding", std::string()) + ") ";
    }
    check(view.status == 200 && reply["tree"] == "test/still" && reply["binding"] == "kind Test_Fighter (binding 1)" && !reply.contains("refused")
              && rows.find("Test_Fighter=test/still (kind Test_Fighter (binding 1))") != std::string::npos
              && rows.find("Test_Archer=default (each creature (binding 3))") != std::string::npos,
          "the route tree and the creature rows of the admin state (the tab Creatures of the tool window) have the tree and its binding: " + rows);
    // A binding file that is not correct (a tie): one log line, and the binding file of the program (each creature: the tree default).
    const std::filesystem::path broken = folder_with(options, "btb2-broken",
                                                     {{"still.jsonc", tree_text("test/still", kStillRoot)},
                                                      {"bindings.jsonc", binding_text(R"({ "kind": "Test_Fighter", "tree": "test/still" },
                                                                                          { "kind": "test_fighter", "tree": "default" })")}});
    tree::LoadReport again;
    const bool reloaded = behavior.reload_trees(broken.string(), again);
    check(reloaded && again.refused.size() == 1 && again.refused[0].find("bindings.jsonc: bindings/1/kind: \"test_fighter\" is in the binding 1 too") == 0
              && log_count("world: behavior tree refused: bindings.jsonc: bindings/1/kind") == 1
              && again.bindings == "embedded:bindings.jsonc, 3 binding(s)" && fighter->creature.mind.brain.tree->name == "default"
              && fighter->creature.mind.brain.binding == "each creature (binding 3)",
          "a binding file with a tie is refused with one log line; the binding file of the program is used, and the reload binds each "
          "creature again (" + (again.refused.empty() ? std::string() : again.refused[0]) + ")");
}

// ---- BTB3: the refusal of a tree that names an action that the slice does not have ------------------------------------------------------

/** The cleric gets a second status action, the Howl of the hound (a buff): a tree that names its heal does not start the Howl. */
void refusal_world(std::vector<gamedata::CreatureSlice>& slices, std::vector<gamedata::CreatureKind>& kinds) {
    binding_world(slices, kinds);
    gamedata::CreatureSlice& cleric = slices[6];
    cleric.actions.push_back({"Howl", "Howl", 126, false});
    cleric.statusActions.push_back(gamedata::CreatureStatusAction{
        static_cast<std::uint32_t>(cleric.actions.size() - 1), kHowlLength, {gamedata::StatusMark{kHowlMark, {kHaste}}}});
}

void refusal(const Options& options) {
    // test/howl names the status action Howl, test/rekindle the heal of the cleric, test/nova the area attack of the ghost.
    const char* const fight = R"({ "node": "selector", "children": [
        { "node": "sequence", "children": [ { "node": "in_fight" }, { "node": "keep_target", "lose_distance": 60 },
          { "node": "selector", "children": [ %s, { "node": "subtree", "tree": "default/melee" }, { "node": "turn_to_target" } ] } ] },
        { "node": "subtree", "tree": "default/rest" } ] })";
    const auto with = [&](const char* leaf) {
        char text[1024];
        std::snprintf(text, sizeof(text), fight, leaf);
        return std::string(text);
    };
    const std::filesystem::path folder = folder_with(
        options, "btb3-trees",
        {{"howl.jsonc", tree_text("test/howl", with(R"({ "node": "status_action", "action": "Howl", "repeat": 20, "pause": 1.5 })"))},
         {"rekindle.jsonc",
          tree_text("test/rekindle", with(R"({ "node": "status_action", "action": "InqCleric_Rekindle_Self", "repeat": 20, "pause": 1.5 })"))},
         {"nova.jsonc", tree_text("test/nova", with(R"({ "node": "area_attack", "action": "Spell_Plagued_Plague_Nova", "repeat": 10,
                                                        "repeat_area_only": 5, "hold_margin": 2, "pause": 1.5 })"))},
         {"bindings.jsonc", binding_text(R"({ "family": "Human", "tree": "test/howl" }, { "family": "Hounds", "tree": "test/howl" })")}});
    const crafting::HarnessGate gate(true);
    log_lines().clear();
    Arena arena(true, refusal_world);
    world::Behavior& behavior = arena.behavior();
    tree::LoadReport report;
    const bool loaded = behavior.reload_trees(folder.string(), report);
    // Two fighters (one slice) and the cleric: their slices have no status action Howl. The hound has it.
    world::Entity* first = arena.creature("Test_Fighter", 3.0F, 0.0F, 270.0F);
    world::Entity* second = arena.creature("Test_Fighter", 3.5F, 1.0F, 270.0F);
    world::Entity* cleric = arena.creature("Test_Cleric", 5.0F, -2.0F, 270.0F);
    world::Entity* hound = arena.creature("Test_Hound", 6.0F, 2.0F, 270.0F);
    const std::string cause = first->creature.mind.brain.refusal;
    check(loaded && report.refused.empty() && first->creature.mind.brain.tree->name == "default" && second->creature.mind.brain.tree->name == "default"
              && cause == "the tree \"test/howl\" names the status action \"Howl\" (node 5): the slice slices/test/fighter.dynamicslice has no "
                          "status action of this name"
              && first->creature.mind.brain.binding == "family Human (binding 1)" && cleric->creature.mind.brain.tree->name == "test/howl"
              && hound->creature.mind.brain.tree->name == "test/howl" && hound->creature.mind.brain.actions == std::vector<std::int32_t>{0}
              && cleric->creature.mind.brain.actions == std::vector<std::int32_t>{1},
          "the fighters (no status action Howl) are refused the tree of their binding and have the tree default (" + cause
              + "); the hound and the cleric (with the Howl) have it, with the place of the Howl in their status actions");
    check(behavior.counts().treeRefusals == 2
              && log_count("world: behavior tree refused for creature") == 1
              && log_count("names the status action \"Howl\" (node 5): the slice slices/test/fighter.dynamicslice") == 1,
          "two refusals of one tree and one slice: one log line (one line for each tree and slice)");
    // A named action is the only action of its node: the hound howls; the cleric with the tree test/rekindle at full health does not howl.
    std::string error;
    const bool set = behavior.set_tree(*cleric, "test/rekindle", error) == world::Behavior::TreeCommand::done;
    behavior.hurt_by(*hound, arena.character()->id);
    behavior.hurt_by(*cleric, arena.character()->id);
    arena.until([&] { return behavior.counts().statusActions >= 1 && cleric->creature.mind.attacks >= 2; }, 600);
    const bool howled = Support::effect(*hound, kHaste) != nullptr || hound->creature.mind.statusReady > arena.now();
    check(set && behavior.counts().statusActions == 1 && howled && Support::effect(*cleric, kHaste) == nullptr && cleric->creature.mind.attacks >= 2,
          "the hound starts its Howl (the named status action); the cleric of the tree that names its heal does not start its Howl at full "
          "health and attacks (" + std::to_string(behavior.counts().statusActions) + " status action)");
    // An area attack (a command): the ghost has the nova; the knight has not: refused, it keeps its tree (the tree default: its slice has no
    // Howl either).
    world::Entity* ghost = arena.creature("Test_Ghost", 4.0F, -4.0F, 270.0F);
    world::Entity* knight = arena.creature("Test_Knight", 3.0F, 4.0F, 270.0F);
    std::string knightCause;
    const world::Behavior::TreeCommand forGhost = behavior.set_tree(*ghost, "test/nova", error);
    const world::Behavior::TreeCommand forKnight = behavior.set_tree(*knight, "test/nova", knightCause);
    check(forGhost == world::Behavior::TreeCommand::done && ghost->creature.mind.brain.actions == std::vector<std::int32_t>{0}
              && forKnight == world::Behavior::TreeCommand::refused && knight->creature.mind.brain.tree->name == "default"
              && knightCause.find("names the area attack \"Spell_Plagued_Plague_Nova\"") != std::string::npos
              && log_count("world: command: behavior tree refused for creature") == 1,
          "an area attack: the ghost gets the tree that names its nova; the knight (no such area attack) is refused and keeps its tree ("
              + knightCause + ")");
}

// ---- BTB4: the command tree/set ------------------------------------------------------------------------------------------------------

void tree_command(const Options& options) {
    const std::filesystem::path folder = folder_with(
        options, "btb4-trees",
        {{"still.jsonc", tree_text("test/still", kStillRoot)},
         {"howl.jsonc", tree_text("test/howl", R"({ "node": "status_action", "action": "Howl", "repeat": 20, "pause": 1.5 })")}});
    const crafting::HarnessGate gate(true);
    Arena arena(true, binding_world);
    Routes routes(arena);
    tree::LoadReport report;
    (void)arena.behavior().reload_trees(folder.string(), report);
    world::Entity* fighter = arena.creature("Test_Fighter", 3.0F);
    const std::string query = id_query(*fighter);
    const service::HttpResponse unknown = routes.send("POST", "/v1/dev/creatures/tree/set", query + "&tree=test/none");
    const service::HttpResponse badName = routes.send("POST", "/v1/dev/creatures/tree/set", query);
    const service::HttpResponse refused = routes.send("POST", "/v1/dev/creatures/tree/set", query + "&tree=test/howl");
    check(unknown.status == 404 && unknown.body.find("the set has no tree \\\"test/none\\\"") != std::string::npos && badName.status == 400
              && refused.status == 409 && refused.body.find("names the status action \\\"Howl\\\"") != std::string::npos
              && fighter->creature.mind.brain.tree->name == "default",
          "tree/set: a name that is not a tree 404, no name 400, a tree that names an action that the slice does not have 409 (the creature "
          "keeps its tree)");
    const service::HttpResponse done = routes.send("POST", "/v1/dev/creatures/tree/set", query + "&tree=test/still");
    const std::uint32_t before = fighter->creature.mind.attacks;
    arena.step(150);
    const bool still = fighter->creature.mind.attacks == before && fighter->creature.mind.mode == world::CreatureMode::fight;
    const service::HttpResponse back = routes.send("POST", "/v1/dev/creatures/tree/set", query + "&tree=default");
    arena.until([&] { return fighter->creature.mind.attacks > before; }, 200);
    const json view = json::parse(routes.send("GET", "/v1/dev/creatures/tree", query).body, nullptr, false);
    check(done.status == 200 && still && back.status == 200 && fighter->creature.mind.attacks > before && view["tree"] == "default"
              && view["binding"] == "a command" && arena.behavior().counts().treeCommands == 2,
          "tree/set test/still: the fighter fights with no attack for 5 s; tree/set default: it attacks again (the binding: a command)");
    tree::LoadReport again;
    (void)arena.behavior().reload_trees(folder.string(), again);
    check(fighter->creature.mind.brain.binding == "each creature (binding 3)",
          "a reload binds each creature to the tree of its binding again (the command tree ends)");
    const crafting::HarnessGate off(false);
    const service::HttpResponse gated = routes.send("POST", "/v1/dev/creatures/tree/set", query + "&tree=test/still");
    check(gated.status == 403 && gated.body.find("SOULWARDEN_HARNESS=1") != std::string::npos && fighter->creature.mind.brain.tree->name == "default",
          "with no harness gate the route tree/set is refused (403)");
}

// ---- BTB5: two sessions ----------------------------------------------------------------------------------------------------------------

void two_sessions(const Options& options) {
    const std::filesystem::path folder =
        folder_with(options, "btb5-trees",
                    {{"still.jsonc", tree_text("test/still", kStillRoot)},
                     {"bindings.jsonc", binding_text(R"({ "kind": "Test_Fighter", "tree": "test/still" }, { "tree": "default" })")}});
    Arena arena;
    arena.enter_both();
    tree::LoadReport report;
    (void)arena.behavior().reload_trees(folder.string(), report);
    world::Entity* alpha = arena.character();
    world::Entity* beta = arena.host.world.find_character(kCharacterB);
    Arena::hardy(*beta);
    // A fighter of the test tree (its target A) and an archer of the tree default (its target B).
    world::Entity* fighter = arena.creature("Test_Fighter", 3.0F, 0.0F, 270.0F);
    world::Entity* archer = arena.creature("Test_Archer", -11.0F, 1.0F, 90.0F);
    const std::size_t fromA = arena.a.got.size();
    const std::size_t fromB = arena.b.got.size();
    arena.behavior().hurt_by(*fighter, alpha->id);
    arena.behavior().hurt_by(*archer, beta->id);
    arena.until([&] { return archer->creature.mind.attacks >= 2; }, 600);
    Arena::hardy(*alpha);
    Arena::hardy(*beta);
    check(fighter->creature.mind.brain.tree->name == "test/still" && archer->creature.mind.brain.tree->name == "default"
              && fighter->creature.mind.target == alpha->id && archer->creature.mind.target == beta->id && fighter->creature.mind.attacks == 0
              && archer->creature.mind.attacks >= 2,
          "two creatures with two trees of the binding file and two targets: the fighter of the test tree does not attack A, the archer of "
          "the tree default shoots at B (values of the creatures in the world, not of a session)");
    const std::size_t aFighter = movement_records(arena, arena.a, 1, *fighter, fromA);
    const std::size_t aArcher = movement_records(arena, arena.a, 1, *archer, fromA);
    const std::size_t bFighter = movement_records(arena, arena.b, 2, *fighter, fromB);
    const std::size_t bArcher = movement_records(arena, arena.b, 2, *archer, fromB);
    check(aFighter >= 1 && bFighter >= 1 && aArcher >= 2 && bArcher >= 2,
          "each session decodes the movement records of both creatures (A: fighter " + std::to_string(aFighter) + ", archer "
              + std::to_string(aArcher) + "; B: fighter " + std::to_string(bFighter) + ", archer " + std::to_string(bArcher) + ")");
    // Code review of stage BT2, finding BTR2-R4: the records of the archer have the action of its shot in each session.
    const std::size_t aShot = action_records(arena, arena.a, 1, *archer, fromA, range::kFireBow);
    const std::size_t bShot = action_records(arena, arena.b, 2, *archer, fromB, range::kFireBow);
    check(aShot >= 2 && bShot >= 2, "each session decodes the action records of the shots of the archer (A " + std::to_string(aShot) + ", B "
                                        + std::to_string(bShot) + ")");
}

// ---- BTB6: the subtrees ----------------------------------------------------------------------------------------------------------------

/** @return The result of the first node with this label (a subtree) in the last step of a creature. */
tree::Status status_of(const world::Entity& creature, const char* label) {
    const world::CreatureBrain& brain = creature.creature.mind.brain;
    for (std::size_t index = 0; index < brain.tree->nodes.size(); ++index) {
        if (brain.tree->nodes[index].label == label) {
            return static_cast<tree::Status>(brain.last[index]);
        }
    }
    return tree::Status::idle;
}

void subtrees(const Options& options) {
    // The shot of an archer in default/shot, the cast of a caster in default/cast.
    {
        Arena arena;
        world::Entity* archer = arena.creature("Test_Archer", 12.0F);
        world::Entity* caster = arena.creature("Test_Caster", 12.0F, 2.0F);
        arena.behavior().hurt_by(*archer, arena.character()->id);
        arena.behavior().hurt_by(*caster, arena.character()->id);
        bool archerPath = false;
        bool casterPath = false;
        for (int tick = 0; tick < 400 && !(archerPath && casterPath); ++tick) {
            arena.step();
            archerPath = archerPath
                         || (archer->creature.mind.brain.cause == world::StepCause::tree && status_of(*archer, "default/shot") == tree::Status::success
                             && status_of(*archer, "default/cast") == tree::Status::idle);
            casterPath = casterPath
                         || (caster->creature.mind.brain.cause == world::StepCause::tree && status_of(*caster, "default/shot") == tree::Status::failure
                             && status_of(*caster, "default/cast") == tree::Status::success);
        }
        check(archerPath && casterPath && archer->creature.mind.attacks >= 1 && caster->creature.mind.attacks >= 1,
              "the archer shoots in the subtree default/shot (default/cast does not run); the caster fails default/shot (no band of a caster) "
              "and casts in default/cast");
    }
    // A node with each band (the format of stage BT1) loads; a node with no band does not; the member action must be a name.
    tree::TreeFile file;
    std::string both;
    std::string none;
    std::string action;
    const bool bothBands = tree::load_tree_file(tree_text("t", R"({ "node": "basic_shot",
        "archer": { "far": 20, "hold": 18, "near": 8, "middle": 14, "repeat": 3 },
        "caster": { "far": 16, "hold": 14, "near": 6, "middle": 11, "repeat": 2.5 }, "retreat_pause": 6, "pause": 1.5 })"),
                                                "t.jsonc", file, both);
    const bool noBand = tree::load_tree_file(tree_text("t", R"({ "node": "basic_shot", "retreat_pause": 6, "pause": 1.5 })"), "t.jsonc", file, none);
    const bool badAction =
        tree::load_tree_file(tree_text("t", R"({ "node": "status_action", "action": "a b", "repeat": 20, "pause": 1.5 })"), "t.jsonc", file, action);
    std::string waits;
    const bool badWaits =
        tree::load_tree_file(tree_text("t", R"({ "node": "roam", "wait_shortest": 9, "wait_longest": 2 })"), "t.jsonc", file, waits);
    check(bothBands && !noBand && none.find("the node basic_shot must have a band") != std::string::npos && !badAction
              && action.find("root/action: the action must be the name of an action") == 0 && !badWaits
              && waits.find("wait_shortest <= wait_longest") != std::string::npos,
          "a basic_shot with both bands (stage BT1) loads; with no band, a named action that is not a name, and waits of a roam in the wrong "
          "sequence are refused");
    // The waits of the roam are node parameters: a folder replaces default/rest with waits of 1 s; the wanderer reaches more places.
    const auto places = [&](const std::string& folder) {
        Arena arena;
        tree::LoadReport report;
        (void)arena.behavior().reload_trees(folder, report);
        (void)arena.creature("Test_Wanderer", 15.0F, 15.0F);
        arena.step(1800); // 60 s
        return arena.behavior().counts().wanderPlaces;
    };
    const std::filesystem::path fast =
        folder_with(options, "btb6-rest", {{"rest.jsonc", tree_text("default/rest", R"({ "node": "roam", "wait_shortest": 1, "wait_longest": 1 })")}});
    const std::uint64_t usual = places("");
    const std::uint64_t quick = places(fast.string());
    check(usual >= 2 && quick >= usual + 3,
          "the waits of the roam come from its node: 4 s to 12 s (the program: " + std::to_string(usual) + " places in 60 s), 1 s (a folder: "
              + std::to_string(quick) + " places)");
}

} // namespace

std::vector<Case> behavior_binding_cases() {
    return {{"BTB1", "behavior trees (BT2): the binding file, the order of its keys, the refused files, the server trees of creatures.json",
             binding_file},
            {"BTB2", "behavior trees (BT2): the bindings in a world, the view of the tree and its binding, a binding file that is refused",
             world_bindings},
            {"BTB3", "behavior trees (BT2): a tree that names an action that the slice does not have is refused for that creature", refusal},
            {"BTB4", "behavior trees (BT2): the command tree/set (the harness gate, 404, 409, the tree default again, a reload)", tree_command},
            {"BTB5", "behavior trees (BT2, MP-10): two sessions, two trees of the binding file, the records of both creatures", two_sessions},
            {"BTB6", "behavior trees (BT2): the subtrees shot and cast, the bands of a node, the waits of the roam as node parameters", subtrees}};
}

} // namespace soulwarden::backend::tests
