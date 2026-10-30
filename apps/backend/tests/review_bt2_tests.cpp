// Tests of stage BT3, part D: the findings of the code review of stage BT2 (~/Games/soulwarden/work/evidence/behavior-trees/bt2/code-review/
// FINDINGS.md). Each case fails on the tree of stage QG9e (after-qg9e) and passes now; the counts of the trees of the program come from the
// embedded files, so each case runs on both trees.
//
//   BT2R1  finding BTR2-R1: a folder file that makes a cycle with the default tree of the program (default/fight names default): the folder
//          file is refused, and the trees of the program stay (the default tree too)
//   BT2R2  finding BTR2-R2: a chain of 24 folder trees, each names the next, the last a tree that is not there: each one is refused, and the
//          trees of the program stay (the limit of the rounds comes from the files at the start)
//   BT2R3  finding BTR2-R3: a default of a folder that names a status action that the slice does not have: the creature has the default of
//          the program, and it fights
//
// Finding BTR2-R4 (the cases of two sessions did not decode the action records) has its checks in BT5 and BTB5 (trees::action_records): with
// the mutation of the review (no action record in the movement state) they fail. The files and the creatures are made up.

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/tree_fixture.h"
#include "world/behavior_tree.h"

namespace soulwarden::backend::tests {
namespace {

namespace tree = world::tree;
using trees::Arena;

std::string review_tree(const std::string& name, const std::string& root) {
    return "{\"format\":1,\"tree\":\"" + name + "\",\"root\":" + root + "}";
}

std::filesystem::path review_folder(const Options& options, const std::string& name) {
    const auto folder = options.workDirectory / name;
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    return folder;
}

/** The trees of the program: the embedded files but the binding file. */
std::size_t program_trees() {
    return tree::embedded_tree_files().size() - 1;
}

void cycle(const Options& options) {
    const auto folder = review_folder(options, "bt2r1-cycle");
    std::ofstream(folder / "fight.jsonc") << review_tree("default/fight", R"({"node":"subtree","tree":"default"})");
    tree::TreeSet set;
    tree::LoadReport report;
    const bool loaded = set.load(folder.string(), report);
    std::string refused;
    for (const std::string& line : report.refused) {
        refused += line + "; ";
    }
    note("BT2R1 loaded=" + std::to_string(loaded) + " compiled=" + std::to_string(set.trees().size()) + " refused: " + refused);
    const std::shared_ptr<const tree::CompiledTree> fight = set.find("default/fight");
    check(loaded && set.default_tree() != nullptr && set.trees().size() == program_trees() && report.refused.size() == 1
              && report.refused[0].find("fight.jsonc") == 0 && fight != nullptr && fight->source == "embedded:default-fight.jsonc",
          "a cyclic override: the folder file is refused (one line), the default tree of the program stays, and default/fight is the file of "
          "the program");
}

void chain(const Options& options) {
    for (const unsigned count : {16U, 24U}) {
        const auto folder = review_folder(options, "bt2r2-chain-" + std::to_string(count));
        for (unsigned index = 0; index < count; ++index) {
            const auto child = "review/chain_" + std::to_string(index + 1);
            std::ofstream(folder / ("chain_" + std::to_string(index) + ".jsonc"))
                << review_tree("review/chain_" + std::to_string(index), "{\"node\":\"subtree\",\"tree\":\"" + child + "\"}");
        }
        tree::TreeSet set;
        tree::LoadReport report;
        const bool loaded = set.load(folder.string(), report);
        note("BT2R2 chain " + std::to_string(count) + ": loaded=" + std::to_string(loaded) + ", refused=" + std::to_string(report.refused.size())
             + ", compiled=" + std::to_string(set.trees().size()));
        check(loaded && set.default_tree() != nullptr && set.trees().size() == program_trees() && report.refused.size() == count,
              "a chain of " + std::to_string(count) + " folder trees to a missing tree: each one is refused, the trees of the program stay");
    }
}

void incompatible_default(const Options& options) {
    const auto folder = review_folder(options, "bt2r3-default");
    std::ofstream(folder / "default.jsonc") << review_tree("default", R"({"node":"status_action","action":"Howl","repeat":20,"pause":1.5})");
    Arena arena;
    tree::LoadReport report;
    const bool loaded = arena.behavior().reload_trees(folder.string(), report);
    world::Entity* fighter = arena.creature("Test_Fighter", 3.0F);
    arena.behavior().hurt_by(*fighter, arena.character()->id);
    arena.step(300);
    const world::CreatureBrain& brain = fighter->creature.mind.brain;
    const bool missing = std::any_of(brain.actions.begin(), brain.actions.end(), [](std::int32_t action) { return action < 0; });
    std::string cause;
    const world::Behavior::TreeCommand command = arena.behavior().set_tree(*fighter, "default", cause);
    note("BT2R3 attacks=" + std::to_string(fighter->creature.mind.attacks) + " tree=" + (brain.tree ? brain.tree->source : std::string("(none)"))
         + " missing=" + std::to_string(missing) + " refusal=" + brain.refusal);
    check(loaded && report.refused.empty() && !brain.refusal.empty() && brain.tree && brain.tree->source == "embedded:default.jsonc" && !missing
              && fighter->creature.mind.attacks >= 2 && command == world::Behavior::TreeCommand::refused,
          "a default of a folder that the slice does not fit: the creature has the default of the program and fights; the tree command "
          "refuses the default of the folder too (one rule)");
}

} // namespace

std::vector<Case> review_bt2_cases() {
    return {{"BT2R1", "review of BT2 (BTR2-R1): a cyclic override does not remove the default tree of the program", cycle},
            {"BT2R2", "review of BT2 (BTR2-R2): the recovery of a long chain of folder trees keeps the trees of the program", chain},
            {"BT2R3", "review of BT2 (BTR2-R3): a refused default is not its own fallback: the default of the program", incompatible_default}};
}

} // namespace soulwarden::backend::tests
