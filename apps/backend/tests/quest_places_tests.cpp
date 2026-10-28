// The cases of stage QG3 (docs/systems/quests.md, the places of the quests; research quest-gaps.md sections 1 and 2): the area shapes and
// the origins of each layer and region in the POI data, the causes of the tags with no shape, the trigger volumes of the capitals with
// their world, the volumes of an instance that the open world never tests, the place view, and the reasons of the manifest. The world is
// the fixture of the gathering cases (two characters A and B with their clients); the quest data, the POI data, and the trigger data are
// made up in the form of the tools. Made-up accounts, characters, ids, and names only.
//
//   QG3A  the POI loader: an area shape (kind, source), a shape of another world is not used, the causes of the member `unshaped` (a tag
//         of no row, a tag with a shape, a file with no member), the grid of the place view
//   QG3B  the trigger loader: a volume of another world is not known (its world for the cause), the member `elsewhere`, the slice and the
//         placement of a volume, the grid of the place view
//   QG3C  two sessions (MP-10): A goes into an area shape and into the trigger volume of a capital: its tasks complete; B stays at the place
//         of a shape and of a volume of another world: its tasks wait; the Objectives state of A does not reach B; a move visits only the
//         volumes of the tags of the active tasks
//   QG3D  the manifest: a place task with no tag, a pin task, a place of a template, a tag with no shape with the cause of the data, a tag
//         of no row, the location rule with its cause, a trigger volume of an instance; the kinds of the first reasons
//   QG3E  the place view (the command quest_places of the admin and development routes): the places of A at its position; B sees only its
//         own (none); a character that is not in the world

#include <string>
#include <vector>

#include "gamedata/poi_data.h"
#include "gamedata/trigger_data.h"
#include "server/http/character_commands.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"
#include "support/quest_fixture.h"
#include "support/quest_person_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace gather;
using questfix::json;
using questfix::quest_of;
using questfix::states_text;
using world::quest_state::kActive;
using world::quest_state::kComplete;

constexpr std::uint32_t kObjectivesSlot = 22;
constexpr std::uint32_t kAreaId = 9301;
constexpr const char* kInstanceWorld = "nw_dungeon_test";
/** The place of the area shape and of the trigger volume of a capital (far from the start places of A and B: 100, 200 and 101, 200). */
const state::WorldPosition kInArea{1050.0F, 1050.0F, 30.0F};

json square(float x, float y, float half) {
    return json::array({{x - half, y - half}, {x + half, y - half}, {x + half, y + half}, {x - half, y + half}});
}

/** The POI data of the cases (the form of tools/gamedata/pois.py of stage QG3, format 2). */
json place_poi_document(bool withCauses = true) {
    json document{
        {"format", 2},
        {"world", gamedata::kOpenWorld},
        {"pois",
         json::array({{{"id", kAreaId}, {"tags", {"test_area"}}, {"territory", 6}, {"center", {0.0, 0.0, 0.0}}, {"radius", 0.0},
                       {"shape", square(1050.0F, 1050.0F, 100.0F)}, {"kind", "area"}, {"source", "objects/worldprisms/areas/testland/area3.vshapec"},
                       {"origin", {{"world", true}}}, {"world", gamedata::kOpenWorld}},
                      // A shape of another world at the start place of B: the open world never tests it.
                      {{"id", 9302}, {"tags", {"test_area"}}, {"territory", 6}, {"center", {101.0, 200.0, 30.0}}, {"radius", 0.0},
                       {"shape", square(101.0F, 200.0F, 20.0F)}, {"kind", "poi"}, {"source", "objects/worldprisms/pois/r_+00_+00/poi_9302_x.vshapec"},
                       {"world", kInstanceWorld}}})},
        {"left", json::array()},
        {"territories", json::array({{{"id", 6}, {"name", "@test"}, {"shape", square(1500.0F, 1500.0F, 1500.0F)}}})}};
    if (withCauses) {
        document["unshaped"] = json::array({{{"tag", "Test_NoShape"}, {"ids", {40906}}, {"cause", "40906: no shape file of the id"}}});
    }
    return document;
}

/** The trigger data of the cases: a volume of a capital of the open world, a volume of an instance, a tag of another instance. */
json place_trigger_document() {
    const json axes = json::array({1, 0, 0, 0, 1, 0, 0, 0, 1});
    return {{"format", 1},
            {"world", gamedata::kOpenWorld},
            {"volumes",
             json::array({{{"tag", "Test_Capital_Trigger"}, {"shape", "box"}, {"center", {kInArea.x, kInArea.y, kInArea.z}}, {"axes", axes},
                           {"half", {10.0, 10.0, 5.0}}, {"source", "outpost_test.dynamicslice"},
                           {"via", "capitals r_+00_+00/capitals/test_persistence/test_persistence.capitals.json row 7"},
                           {"world", gamedata::kOpenWorld}},
                          {{"tag", "Test_Instance_Trigger"}, {"shape", "sphere"}, {"center", {101.0, 200.0, 30.0}}, {"axes", axes}, {"radius", 30.0},
                           {"source", "levelscript_test.dynamicslice"}, {"world", kInstanceWorld}}})},
            {"elsewhere", json::array({{{"tag", "Test_Elsewhere_Only"}, {"world", "nw_dungeon_two"}, {"source", "x.dynamicslice"}}})}};
}

/** The quest data of the cases, in the form of the tool. */
json place_quests() {
    json tasks = json::object();
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id, type, code, cells);
    };
    add("Qg3_GoToArea", "taskgotopoitag", 6, {{"POITag", "test_area"}});
    add("Qg3_EnterCapital", "tasktriggerarea", 3, {{"TP_TargetTag", "Test_Capital_Trigger"}, {"TriggerAreaType", "Enter"}, {"TargetQty", "1"}});
    add("Qg3_Root", "consecutivetaskcontainer", 1);
    add("Qg3_EnterInstance", "tasktriggerarea", 3, {{"TP_TargetTag", "Test_Instance_Trigger"}, {"TriggerAreaType", "Enter"}, {"TargetQty", "1"}});
    json objectives = json::array();
    objectives.push_back({{"id", "Qg3_PlaceQuest"},
                          {"cells", {{"Type", "Journey"}}},
                          {"tree", json::array({json::array({0, "qg3_gotoarea", json::array()}), json::array({1, "qg3_entercapital", json::array()}),
                                                json::array({2, "qg3_root", json::array({0, 1})})})}});
    objectives.push_back({{"id", "Qg3_InstanceQuest"},
                          {"cells", {{"Type", "Journey"}}},
                          {"tree", json::array({json::array({0, "qg3_enterinstance", json::array()})})}});
    return {{"format", 1},           {"objectives", objectives},     {"tasks", tasks},           {"achievements", json::array()},
            {"npa", json::array()}, {"territories", json::array()}, {"backstories", json::array()}};
}

/** The gathering world with the places of the cases (loaded before the entry of the characters). */
struct PlaceShop : GatherShop {
    PlaceShop() {
        if (!host.quests.load_text(place_quests().dump())) {
            throw std::runtime_error("quest place test: the quest data did not load: " + host.quests.error());
        }
        if (!host.pois.load_text(place_poi_document().dump())) {
            throw std::runtime_error("quest place test: the POI data did not load: " + host.pois.error());
        }
        if (!host.triggers.load_text(place_trigger_document().dump())) {
            throw std::runtime_error("quest place test: the trigger data did not load: " + host.triggers.error());
        }
    }
    world::Quests& quests() {
        return host.world.quests();
    }
    const world::QuestInstance* quest(const char* character, const char* objective) {
        return quest_of(*entity(character), objective);
    }
    /** A travel to a place, then the report of the client there (the accepted position: World::place), and some ticks. */
    void move(const char* character, const state::WorldPosition& place) {
        const bool isA = std::string(character) == kCharacterA;
        (void)host.world.travel(character, place, world::TravelScreen::off);
        (isA ? placeA : placeB) = place;
        report(isA ? clientA : clientB, std::nullopt);
        host.steps(3, {&clientA, &clientB});
    }
};

std::uint8_t state_of(const world::QuestInstance* quest, std::size_t index) {
    return quest != nullptr && index < quest->tasks.size() ? quest->tasks[index].state : 255;
}

/** @return The number of fragments of a slot of an entity that is not the own one (interest 1) that a client got from `from` on. */
std::size_t foreign_slot(const FakeClient& client, std::uint32_t slot, std::size_t from) {
    std::size_t count = 0;
    for (std::size_t index = from; index < client.got.size(); ++index) {
        if (client.got[index].type != 8) {
            continue;
        }
        for (const tests::Record& record : read_bundle(client.got[index].body).records) {
            for (const tests::Fragment& fragment : record.fragments) {
                count += record.interest != 1 && fragment.slot == slot ? 1 : 0;
            }
        }
    }
    return count;
}

/** QG3A: the POI loader of stage QG3. */
void poi_loader(const Options&) {
    gamedata::PoiData data;
    check(data.load_text(place_poi_document().dump()), "the POI data of the cases loads (" + data.error() + ")");
    check(data.pois().size() == 1 && data.other_world() == 1 && data.poi(9302) == nullptr,
          "a shape of another world (an instance) is not used: one point of interest, one of another world");
    const gamedata::Poi* area = data.poi(kAreaId);
    check(area != nullptr && area->area && area->source == "objects/worldprisms/areas/testland/area3.vshapec" && area->contains(1050.0F, 1050.0F)
              && data.inside({"test_area"}, 0, 1050.0F, 1050.0F) && !data.inside({"test_area"}, 0, 101.0F, 200.0F),
          "the area shape (world coordinates) has its kind and its source; the tag of the other world is not inside at its place");
    check(data.cause("test_noshape") == "40906: no shape file of the id" && data.cause("never_seen") == "no row of the territory tables has the tag"
              && data.cause("test_area").empty(),
          "the cause of a tag with no shape (the member unshaped), of a tag of no row, and none for a tag with a shape");
    const std::vector<const gamedata::Poi*> here = data.at(1050.0F, 1050.0F);
    check(here.size() == 1 && here[0]->id == kAreaId && data.at(101.0F, 200.0F).empty() && data.at(5000.0F, 5000.0F).empty(),
          "the grid of the place view: the area at its place; nothing at the place of the shape of the other world; nothing far away");
    gamedata::PoiData before;
    check(before.load_text(place_poi_document(false).dump()) && before.cause("test_noshape").empty() && before.cause("never_seen").empty(),
          "a file with no member unshaped (stage 1.8c, QG1): no cause");
    json bad = place_poi_document();
    bad["unshaped"] = "x";
    gamedata::PoiData refused;
    check(!refused.load_text(bad.dump()) && !refused.loaded(), "a member unshaped that is not a list refuses the file");
}

/** QG3B: the trigger loader of stage QG3. */
void trigger_loader(const Options&) {
    gamedata::TriggerData data;
    check(data.load_text(place_trigger_document().dump()), "the trigger data of the cases loads (" + data.error() + ")");
    check(data.volume_count() == 1 && data.knows("test_capital_trigger") && !data.knows("test_instance_trigger") && data.other_world() == 1,
          "a volume of another world (an instance) is not known in the open world");
    const std::vector<std::string>* instance = data.elsewhere("test_instance_trigger");
    const std::vector<std::string>* other = data.elsewhere("test_elsewhere_only");
    check(instance != nullptr && *instance == std::vector<std::string>{kInstanceWorld} && other != nullptr
              && *other == std::vector<std::string>{"nw_dungeon_two"} && data.elsewhere("test_capital_trigger") == nullptr && data.elsewhere_tags() == 2,
          "the worlds of the tags of the other worlds (a volume with its world and the member elsewhere)");
    const std::vector<const gamedata::TriggerVolume*> here = data.at(kInArea.x, kInArea.y, kInArea.z);
    check(here.size() == 1 && here[0]->tag == "test_capital_trigger" && here[0]->source == "outpost_test.dynamicslice"
              && here[0]->via == "capitals r_+00_+00/capitals/test_persistence/test_persistence.capitals.json row 7"
              && data.at(101.0F, 200.0F, 30.0F).empty() && data.at(kInArea.x, kInArea.y, kInArea.z + 50.0F).empty(),
          "the grid of the place view: the volume of the capital with its slice and its placement; nothing at the instance place; not above");
    json bad = place_trigger_document();
    bad["elsewhere"] = json::object();
    gamedata::TriggerData refused;
    check(!refused.load_text(bad.dump()) && !refused.loaded(), "a member elsewhere that is not a list refuses the file");
}

/** QG3C: two sessions in the places of stage QG3. */
void two_sessions(const Options&) {
    PlaceShop shop;
    shop.enter();
    for (const char* character : {kCharacterA, kCharacterB}) {
        check(shop.quests().grant(*shop.entity(character), "Qg3_PlaceQuest", "command", 0).ok()
                  && shop.quests().grant(*shop.entity(character), "Qg3_InstanceQuest", "command", 0).ok(),
              std::string("setup: the place quests of ") + (character == kCharacterA ? "A" : "B"));
    }
    shop.move(kCharacterB, state::WorldPosition{101.0F, 200.0F, 30.0F});
    const std::size_t fromB = shop.clientB.got.size();
    const std::uint64_t visits = shop.quests().counts().triggerVisits;
    shop.move(kCharacterA, kInArea);
    const world::QuestInstance* placeA = shop.quest(kCharacterA, "Qg3_PlaceQuest");
    const world::QuestInstance* placeB = shop.quest(kCharacterB, "Qg3_PlaceQuest");
    note("QG3C A: " + states_text(*placeA) + "; B: " + states_text(*placeB));
    check(state_of(placeA, 0) == kComplete && state_of(placeA, 1) == kComplete,
          "A in the area shape (world coordinates): TaskGoToPOITag completes; then it is in the volume of the capital: TaskTriggerArea completes");
    check(state_of(placeB, 0) == kActive && state_of(shop.quest(kCharacterB, "Qg3_InstanceQuest"), 0) == kActive
              && state_of(shop.quest(kCharacterA, "Qg3_InstanceQuest"), 0) == kActive,
          "B at the place of a shape and of a volume of another world: its tasks wait (an instance is never tested in the open world)");
    check(shop.quests().counts().triggerVisits - visits <= 2,
          "the move of A visits only the volumes of the tags of its active tasks (one volume of the open world; MP-8)");
    check(foreign_slot(shop.clientB, kObjectivesSlot, fromB) == 0, "the Objectives state of A does not reach the session of B (owner only, MP-10)");
}

/** QG3E: the place view of the admin and development routes. */
void place_view(const Options&) {
    PlaceShop shop;
    shop.enter();
    shop.move(kCharacterA, kInArea);
    shop.move(kCharacterB, state::WorldPosition{101.0F, 200.0F, 30.0F});
    const auto view = [&](const char* character) {
        service::HttpResponse refusal;
        const json arguments = json::object();
        const server::http::commands::JsonArguments args(arguments);
        const server::http::commands::WorldCommand command = server::http::commands::quest_places(character, args, refusal);
        const service::HttpResponse reply = command ? command(shop.host.world) : refusal;
        return std::make_pair(reply.status, json::parse(reply.body, nullptr, false));
    };
    const auto [statusA, bodyA] = view(kCharacterA);
    note("QG3E A: " + bodyA.dump());
    check(statusA == 200 && bodyA["pois"].size() == 1 && bodyA["pois"][0]["id"] == kAreaId && bodyA["pois"][0]["kind"] == "area"
              && bodyA["volumes"].size() == 1 && bodyA["volumes"][0]["tag"] == "test_capital_trigger"
              && bodyA["volumes"][0]["via"] == "capitals r_+00_+00/capitals/test_persistence/test_persistence.capitals.json row 7"
              && bodyA["territory"] == 6 && bodyA["estimated"] == false,
          "the view of A: the area shape and the volume of the capital at its accepted position, its territory");
    const auto [statusB, bodyB] = view(kCharacterB);
    check(statusB == 200 && bodyB["pois"].empty() && bodyB["volumes"].empty(),
          "the view of B: none of the places of A, and not the shape and the volume of the other world at its place");
    check(view("cccccccc-1111-4000-8000-0000000000c1").first == 409, "a character that is not in the world: 409");
}

/** QG3D: the reasons of the manifest of stage QG3. */
void place_manifest(const Options&) {
    questperson::QuestShop shop;
    json tasks = json::object();
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id, type, code, cells);
    };
    add("Gp_NoTag", "taskgotopoitag", 6);
    add("Gp_Pin", "taskgotopoitag", 6, {{"OverrideLocation", "1.0 2.0 3.0"}});
    add("Gp_NoShape", "taskgotopoitag", 6, {{"POITag", "Test_NoShape"}});
    add("Gp_NoRow", "taskexitpoitag", 7, {{"POITag", "Never_Seen"}});
    add("Gp_Area", "taskgotopoitag", 6, {{"POITag", "test_area"}});
    add("Gp_Location", "taskopenuiscreen", 62,
        {{"UIScreenTag", "Inventory"}, {"TargetQty", "1"}, {"EnforceLocationRequirement", "1"}, {"POITag", "Test_NoShape"}});
    add("Gp_Instance", "tasktriggerarea", 3, {{"TP_TargetTag", "Test_Instance_Trigger"}, {"TriggerAreaType", "Enter"}});
    add("Gp_Capital", "tasktriggerarea", 3, {{"TP_TargetTag", "Test_Capital_Trigger"}, {"TriggerAreaType", "Enter"}});
    const auto one = [](const char* id, json cells, const char* task) {
        cells["NpcDestinationId"] = "Test_Artificer";
        if (!cells.contains("Type")) {
            cells["Type"] = "Journey";
        }
        return json{{"id", id}, {"cells", cells}, {"tree", json::array({json::array({0, task, json::array()})})}};
    };
    json objectives = json::array();
    for (const auto& [id, task] : std::vector<std::pair<const char*, const char*>>{{"Gp_NoTag", "gp_notag"},
                                                                                   {"Gp_Pin", "gp_pin"},
                                                                                   {"Gp_NoShape", "gp_noshape"},
                                                                                   {"Gp_NoRow", "gp_norow"},
                                                                                   {"Gp_Area", "gp_area"},
                                                                                   {"Gp_Location", "gp_location"},
                                                                                   {"Gp_Instance", "gp_instance"},
                                                                                   {"Gp_Capital", "gp_capital"}}) {
        objectives.push_back(one(id, json::object(), task));
    }
    objectives.push_back(one("Gp_Template", {{"Type", "Mission"}}, "gp_notag"));
    const json document{{"format", 1},           {"objectives", objectives},     {"tasks", tasks},
                        {"achievements", json::array()}, {"territories", json::array()},
                        {"backstories", json::array({{{"id", "Archetype_Soldier"}, {"objectives", json::array()}, {"achievements", json::array()},
                                                      {"archetype", true}}})}};
    check(shop.host.quests.load_text(document.dump()), "control: the manifest data loads (" + shop.host.quests.error() + ")");
    check(shop.host.pois.load_text(place_poi_document().dump()) && shop.host.triggers.load_text(place_trigger_document().dump()),
          "control: the POI data and the trigger data of the cases");
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest(support.default_profiles());
    const auto row = [&](const char* id) -> const json& {
        for (const char* list : {"objectives", "missionTemplates"}) {
            for (const json& entry : manifest[list]) {
                if (entry["id"] == id) {
                    return entry;
                }
            }
        }
        throw std::runtime_error("no row " + std::string(id));
    };
    const auto first = [&](const char* id) { return row(id)["blocked"].empty() ? std::string{} : row(id)["blocked"][0].get<std::string>(); };
    const auto has = [&](const char* id, const char* text) { return first(id).find(text) != std::string::npos; };
    note("QG3D reasons: " + first("Gp_NoTag") + " | " + first("Gp_Pin") + " | " + first("Gp_Template") + " | " + first("Gp_NoShape") + " | "
         + first("Gp_NoRow") + " | " + first("Gp_Location") + " | " + first("Gp_Instance"));
    check(has("Gp_NoTag", "the task has no POITag: a blank cell of the data") && has("Gp_Pin", "the task has no POITag: a pin task (OverrideLocation)")
              && has("Gp_Template", "the task has no POITag: the mission instance of a template gives the place"),
          "a place task with no tag, a pin task, and a place of a template have their own reasons (not a tag with no shape)");
    check(has("Gp_NoShape", "no point of interest shape of the tags test_noshape in the POI data (test_noshape: 40906: no shape file of the id)")
              && has("Gp_NoRow", "(never_seen: no row of the territory tables has the tag)"),
          "a tag with no shape has the cause of the data; a tag of no row has its own cause");
    const auto any = [&](const char* id, const char* text) {
        for (const json& reason : row(id)["blocked"]) {
            if (reason.get<std::string>().find(text) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    check(any("Gp_Location", "the location rule (EnforceLocationRequirement) needs a point of interest shape of the tags test_noshape: the POI data "
                             "has none (the task never progresses) (test_noshape: 40906: no shape file of the id)"),
          "the location rule names the cause of the tag");
    check(has("Gp_Instance", "the volume of the tag test_instance_trigger is in the instance nw_dungeon_test, not in the open world")
              && row("Gp_Capital")["tasks"][0]["supplier"] == "the trigger volume test_capital_trigger"
              && row("Gp_Area")["tasks"][0]["supplier"] == "the point of interest test_area",
          "a trigger volume of an instance has its own reason; the volume of a capital and the area shape supply their tasks");
    const json& reasons = manifest["counts"]["firstReasonOfTheOthers"];
    check(reasons.contains("task: a trigger volume of an instance (its instance stage)") && reasons.contains("task: a place task with no POITag (never completes)")
              && reasons.contains("task: a pin task with no POITag (never completes)")
              && reasons.contains("task: no point of interest shape of its tags"),
          "the counts of the first reasons have the new kinds (the place of a template is a mission template row)");
}

} // namespace

std::vector<Case> quest_place_cases() {
    return {{"QG3A", "quests QG3 (I1, I2): the POI loader: the area shape, a shape of another world, the causes of the tags, the grid", poi_loader},
            {"QG3B", "quests QG3 (I3): the trigger loader: a volume of another world, the member elsewhere, the placement, the grid", trigger_loader},
            {"QG3C", "quests QG3 (I6): two sessions in an area shape and the volume of a capital; the places of another world wait", two_sessions},
            {"QG3D", "quests QG3 (I2, I3, I5): the manifest: the place tasks with no tag, the causes of the tags, a volume of an instance",
             place_manifest},
            {"QG3E", "quests QG3 (I6): the place view of the admin and development routes for A and B", place_view}};
}

} // namespace soulwarden::backend::tests
