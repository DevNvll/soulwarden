// The corrections of the code review of stage QG3 (~/Games/soulwarden/work/evidence/quests/qg3/code-review/FINDINGS.md), in stage QG4.
// Each case fails on the tree of stage QG3 and passes after the correction. Made-up volumes and shapes only.
//
//   QGR3A1  QG3R-R1: a trigger file whose root names an instance world (the form of the tool of before stage QG3) and a volume with no
//           world of its own: the volume is of that world, never tested in the open world (an open-world file and a volume with its own
//           world: controls)
//   QGR3A2  QG3R-R2: a turned box with no height limit (its local height axis along world X): the place view finds it far along that axis,
//           as the predicate of the runtime does (a point outside its local Y limit: outside)
//   QGR3A3  QG3R-R3: a point of interest of 2,000 km and a box of 2,000 km: the loaders refuse the files before a grid of millions of
//           cells (a file of normal shapes loads); a lookup far outside the world converts no huge value
//   QGR3A4  QG3R-R4: the trigger tool keeps two volumes of one slice with one tag and one center and different axes
//           (check_triggers_tool.py)

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <tuple>

#include "gamedata/poi_data.h"
#include "gamedata/trigger_data.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/quest_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using questfix::json;

json review_box() {
    return {{"tag", "review_trigger"}, {"shape", "box"}, {"center", {0.0, 0.0, 0.0}}, {"axes", {1, 0, 0, 0, 1, 0, 0, 0, 1}}, {"half", {10.0, 10.0, 10.0}}};
}

/** QGR3A1: the world of the file. */
void file_world(const Options&) {
    gamedata::TriggerData data;
    json document = {{"format", 1}, {"world", "nw_dungeon_review"}, {"volumes", json::array({review_box()})}};
    const bool loaded = data.load_text(document.dump());
    note("QGR3A1 an instance file: loaded " + std::to_string(loaded) + ", known " + std::to_string(data.knows("review_trigger")) + ", inside "
         + std::to_string(data.inside("review_trigger", 0.0F, 0.0F, 0.0F)));
    check(loaded && !data.knows("review_trigger") && !data.inside("review_trigger", 0.0F, 0.0F, 0.0F) && data.other_world() == 1
              && data.elsewhere("review_trigger") != nullptr && data.elsewhere("review_trigger")->front() == "nw_dungeon_review",
          "a volume with no world of its own in a file of an instance world is of that world: never tested in the open world (elsewhere)");
    document["world"] = gamedata::kOpenWorld;
    check(data.load_text(document.dump()) && data.inside("review_trigger", 0.0F, 0.0F, 0.0F), "control: a file of the open world supplies its volume");
    document["world"] = "nw_dungeon_review";
    document["volumes"][0]["world"] = gamedata::kOpenWorld;
    check(data.load_text(document.dump()) && data.inside("review_trigger", 0.0F, 0.0F, 0.0F),
          "control: the world of a volume comes before the world of the file");
    document.erase("world");
    document["volumes"][0].erase("world");
    check(data.load_text(document.dump()) && data.inside("review_trigger", 0.0F, 0.0F, 0.0F), "control: no world member: the open world");
}

/** QGR3A2: a turned volume with no height limit in the place view. */
void unbounded_view(const Options&) {
    gamedata::TriggerData data;
    json volume = review_box();
    volume["axes"] = {0, 0, 1, 0, 1, 0, -1, 0, 0};
    volume["ignoreHeight"] = true;
    check(data.load_text(json{{"format", 1}, {"volumes", json::array({volume})}}.dump()), "control: a turned volume with no height limit loads");
    const bool inside = data.inside("review_trigger", 500.0F, 0.0F, 0.0F);
    const std::size_t found = data.at(500.0F, 0.0F, 0.0F).size();
    note("QGR3A2 at (500, 0, 0): the predicate " + std::to_string(inside) + ", the place view " + std::to_string(found) + " volume(s)");
    check(inside && found == 1, "the place view finds the volume where the predicate of the runtime holds (500 m along its local height axis)");
    check(data.at(0.0F, 0.0F, 0.0F).size() == 1 && data.at(500.0F, 20.0F, 0.0F).empty(), "control: the center is inside; a point outside local Y is not");
}

/** QGR3A3: the cells of a large shape are counted before the grid. */
void large_shapes(const Options&) {
    gamedata::PoiData pois;
    const json large = {{"format", 1},
                        {"pois", json::array({{{"id", 49001}, {"center", {0, 0, 0}}, {"tags", {"review_large"}},
                                               {"shape", {{-1000000, -1000000}, {1000000, -1000000}, {1000000, 1000000}, {-1000000, 1000000}}}}})}};
    const bool poiLoaded = pois.load_text(large.dump());
    note("QGR3A3 a point of interest of 2,000 km: loaded " + std::to_string(poiLoaded) + " (" + pois.error() + ")");
    check(!poiLoaded, "a point of interest that covers more cells than the bound refuses the file (no grid of 61 million cells)");
    const json normal = {{"format", 1},
                         {"pois", json::array({{{"id", 49002}, {"center", {0, 0, 0}}, {"tags", {"review_normal"}},
                                                {"shape", {{-500, -500}, {500, -500}, {500, 500}, {-500, 500}}}}})}};
    check(pois.load_text(normal.dump()) && pois.inside({"review_normal"}, 0, 0.0F, 0.0F) && pois.at(0.0F, 0.0F).size() == 1
              && pois.at(3.0e38F, 0.0F).empty(),
          "control: a shape of 1 km loads; a lookup far outside the world finds nothing (no conversion of a huge value)");
    gamedata::TriggerData triggers;
    json box = review_box();
    box["half"] = {1000000.0, 1000000.0, 10.0};
    const bool triggerLoaded = triggers.load_text(json{{"format", 1}, {"volumes", json::array({box})}}.dump());
    note("QGR3A3 a box of 2,000 km: loaded " + std::to_string(triggerLoaded) + " (" + triggers.error() + ")");
    check(!triggerLoaded, "a volume that covers more cells than the bound refuses the file");
    check(triggers.load_text(json{{"format", 1}, {"volumes", json::array({review_box()})}}.dump()) && triggers.at(3.0e38F, 0.0F, 0.0F).empty(),
          "control: a box of 20 m loads; a lookup far outside the world finds nothing");
    gamedata::TriggerVolume test;
    test.tag = "review_set";
    test.half = {1.0e6F, 1.0e6F, 10.0F};
    triggers.set({test});
    check(triggers.at(0.0F, 0.0F, 0.0F).size() == 1, "a large volume of `set` (a test) is in the list of each lookup, not in millions of cells");
}

/** QGR3A4: the duplicate key of the trigger tool. */
void trigger_tool(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata/check_triggers_tool.py";
    const std::filesystem::path output = options.workDirectory / "check-triggers-qg3r.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + script.lexically_normal().string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    bool review = false;
    std::size_t failures = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 160));
        review = review || line.rfind("ok   QG3R-R4", 0) == 0;
        failures += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    std::filesystem::remove(output);
    check(status == 0 && failures == 0 && review,
          "check_triggers_tool.py: the tool keeps two volumes with one tag and one center and different axes (QG3R-R4), and each case passes");
}

} // namespace

std::vector<Case> review_qg3_cases() {
    return {{"QGR3A1", "review of QG3 (QG3R-R1): a volume with no world in a file of an instance world is of that world", file_world},
            {"QGR3A2", "review of QG3 (QG3R-R2): the place view finds a turned volume with no height limit", unbounded_view},
            {"QGR3A3", "review of QG3 (QG3R-R3): the loaders count the cells of a large shape before the grid", large_shapes},
            {"QGR3A4", "review of QG3 (QG3R-R4): the trigger tool keeps two volumes with different axes", trigger_tool}};
}

} // namespace soulwarden::backend::tests
