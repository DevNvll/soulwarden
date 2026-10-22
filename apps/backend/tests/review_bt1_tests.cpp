// Tests of the corrections of the code review of stage BT1 (findings R1 to R8; the review: evidence/behavior-trees/bt1/code-review). Each
// case failed on the code of stage BT1 and passes with the correction of stage BT2. Finding R3 (a link in the folder of a development run)
// was not accepted, and finding R8 is in case BT5 (the records of both fights in both sessions).
//
//   RBT1  R1: a number that is too large for the JSON library (1e400) is a refusal with its cause, not an exception
//   RBT2  R2: a set of trees whose subtree is in a later file loads in each sequence of the file names
//   RBT4  R4: a command (shoot, area, status action) after a reload of a tree default with no node of its kind does nothing (no crash)
//   RBT5  R5: a file that is larger than 1 MiB is refused with a bounded read (the peak memory grows less than 8 MiB for a 64 MiB file)
//   RBT6  R6: an int key of the blackboard takes values from -16777216 to 16777216 (exact in a 32-bit float); a larger value is refused
//   RBT7  R7: a format number that is not 1 (4294967297) is refused
//
// The trees, the files, and the places are made up.

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/range_fixture.h"
#include "world/behavior.h"
#include "world/behavior_tree.h"

namespace soulwarden::backend::tests {
namespace {

namespace tree = world::tree;

std::string tree_file(const std::string& name, const std::string& root, const std::string& board = "") {
    return "{ \"format\": 1, \"tree\": \"" + name + "\", \"root\": " + root + (board.empty() ? "" : ", \"blackboard\": " + board) + " }";
}

std::filesystem::path empty_folder(const Options& options, const char* name) {
    const std::filesystem::path folder = options.workDirectory / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

void number_overflow(const Options&) {
    tree::TreeSet set;
    tree::LoadReport report;
    const bool loaded = set.load("", report);
    const std::shared_ptr<const tree::CompiledTree> before = set.default_tree();
    std::string error;
    bool refused = false;
    bool threw = false;
    try {
        refused = !set.add(tree_file("default", R"({ "node": "keep_target", "lose_distance": 1e400 })"), "overflow.jsonc", error);
    } catch (const std::exception& failure) {
        threw = true;
        note(std::string("the loader threw: ") + failure.what());
    }
    check(loaded && !threw && refused && error.find("not JSON with comments") == 0 && set.default_tree() == before,
          "a number that is too large (1e400) is refused with its cause (" + error + "), with no exception; the tree default stays");
}

void file_order(const Options& options) {
    // The same two files in two sequences of their names: the parent first, and the subtree first.
    const std::string parent = tree_file("default", R"({ "node": "subtree", "tree": "review/fight" })");
    const std::string child = tree_file("review/fight", R"({ "node": "roam" })");
    const std::filesystem::path parentFirst = empty_folder(options, "rbt2-parent-first");
    const std::filesystem::path childFirst = empty_folder(options, "rbt2-child-first");
    std::ofstream(parentFirst / "00-parent.jsonc") << parent;
    std::ofstream(parentFirst / "99-child.jsonc") << child;
    std::ofstream(childFirst / "99-parent.jsonc") << parent;
    std::ofstream(childFirst / "00-child.jsonc") << child;
    tree::TreeSet one;
    tree::TreeSet two;
    tree::LoadReport first;
    tree::LoadReport second;
    const bool loadedOne = one.load(parentFirst.string(), first);
    const bool loadedTwo = two.load(childFirst.string(), second);
    for (const std::string& line : first.refused) {
        note("parent first: refused " + line);
    }
    check(loadedOne && loadedTwo && first.refused.empty() && second.refused.empty() && one.default_tree()->source == "00-parent.jsonc"
              && two.default_tree()->source == "99-parent.jsonc" && one.default_tree()->nodes.size() == 2
              && two.default_tree()->nodes.size() == 2,
          "a tree whose subtree is in a later file loads in each sequence of the file names (the set compiles after the last file)");
}

void command_with_no_node(const Options& options) {
    const std::filesystem::path folder = empty_folder(options, "rbt4-trees");
    std::ofstream(folder / "default.jsonc") << tree_file("default", R"({ "node": "roam" })");
    range::Range arena;
    world::Entity* archer = arena.archer(10.0F);
    arena.behavior().hurt_by(*archer, arena.character()->id);
    arena.step(3);
    tree::LoadReport report;
    const bool reloaded = arena.behavior().reload_trees(folder.string(), report);
    const std::uint32_t attacks = archer->creature.mind.attacks;
    // The shoot command: no tree of the archer has a node basic_shot (before the correction: a null pointer, the end of the program).
    const bool shot = arena.behavior().command_shot(*archer, arena.character()->id);
    const bool status = arena.behavior().command_status_action(*archer, "");
    check(reloaded && report.refused.empty() && !shot && !status && archer->creature.mind.attacks == attacks,
          "after a reload of a tree default with no node basic_shot, the commands shoot and status-action do nothing and the program goes on");
}

/** @return A value of /proc/self/status in KiB ("VmHWM", "VmRSS"), or -1. */
long status_kib(const char* name) {
    std::ifstream input("/proc/self/status");
    std::string line;
    const std::string prefix = std::string(name) + ":";
    while (std::getline(input, line)) {
        if (line.rfind(prefix, 0) == 0) {
            std::istringstream words(line.substr(prefix.size()));
            long value = -1;
            words >> value;
            return value;
        }
    }
    return -1;
}

void large_file(const Options& options) {
    const std::filesystem::path folder = empty_folder(options, "rbt5-trees");
    const std::filesystem::path path = folder / "large.jsonc";
    std::ofstream(path) << " ";
    std::filesystem::resize_file(path, 64ULL * 1024 * 1024);
    // The peak of the resident memory starts again at the resident memory now (Linux: 5 in /proc/self/clear_refs).
    std::ofstream("/proc/self/clear_refs") << "5";
    const long before = status_kib("VmRSS");
    tree::TreeSet set;
    tree::LoadReport report;
    const bool loaded = set.load(folder.string(), report);
    const long peak = status_kib("VmHWM");
    std::filesystem::remove(path);
    note("the peak of the resident memory for a file of 64 MiB: " + std::to_string(peak - before) + " KiB more");
    check(loaded && report.refused.size() == 1 && report.refused[0].find("large.jsonc: the file is larger than 1 MiB") == 0 && before > 0
              && peak - before < 8 * 1024,
          "a file of 64 MiB is refused, and the read is bounded: the peak memory grows less than 8 MiB");
}

void integer_keys(const Options&) {
    tree::TreeFile file;
    std::string tooLarge;
    std::string defaultTooLarge;
    std::string exact;
    const bool value = tree::load_tree_file(tree_file("t", R"({ "node": "check", "key": "phase", "op": "eq", "value": 16777217 })",
                                                      R"({ "phase": { "type": "int", "default": 16777216 } })"),
                                            "t.jsonc", file, tooLarge);
    const bool initial = tree::load_tree_file(tree_file("t", R"({ "node": "roam" })", R"({ "phase": { "type": "int", "default": -16777217 } })"),
                                              "t.jsonc", file, defaultTooLarge);
    const bool largest = tree::load_tree_file(tree_file("t", R"({ "node": "set", "key": "phase", "value": -16777216 })",
                                                        R"({ "phase": { "type": "int", "default": 16777216 } })"),
                                              "t.jsonc", file, exact);
    check(!value && tooLarge.find("root/value: an int value must be from -16777216 to 16777216") == 0 && !initial
              && defaultTooLarge.find("blackboard/phase/default") == 0 && largest,
          "an int key: the values and defaults -16777216 to 16777216 load (exact in the 32-bit float of the blackboard); 16777217 and "
          "-16777217 are refused (" + tooLarge + ")");
}

void format_number(const Options&) {
    tree::TreeFile file;
    std::string error;
    const bool accepted = tree::load_tree_file(R"({ "format": 4294967297, "tree": "review", "root": { "node": "roam" } })", "f.jsonc", file, error);
    check(!accepted && error.find("format: the file must have \"format\": 1") == 0,
          "the format number 4294967297 is refused (it is not narrowed to 1): " + error);
}

} // namespace

std::vector<Case> review_bt1_cases() {
    return {{"RBT1", "code review of BT1 (R1): a JSON number that is too large is a refusal of the file", number_overflow},
            {"RBT2", "code review of BT1 (R2): a subtree in a later file; the set loads in each sequence of the file names", file_order},
            {"RBT4", "code review of BT1 (R4): a command after a reload of a tree with no node of its kind does nothing", command_with_no_node},
            {"RBT5", "code review of BT1 (R5): a file larger than 1 MiB is refused with a bounded read", large_file},
            {"RBT6", "code review of BT1 (R6): the values of an int key of the blackboard are exact", integer_keys},
            {"RBT7", "code review of BT1 (R7): a format number that is not 1 is refused", format_number}};
}

} // namespace soulwarden::backend::tests
