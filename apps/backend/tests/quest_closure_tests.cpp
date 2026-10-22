#include <chrono>
#include <cmath>
#include <string>
#include <vector>
#include "support/attack_fixture.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/quest_person_fixture.h"
#include "world/quest_objects.h"
#include "world/quest_support.h"

// The cases of stage 1.8e (docs/systems/quests.md): the destructible quest objects (QF1, QF2), the entry of a trigger area (QF3), the support
// manifest (QF4), two sessions across a relog (QF5, QG9 of the research), and the availability with many unrelated definitions (QF6, QG10).
// The data and the identities are made up.

namespace soulwarden::backend::tests {
namespace {

using namespace questperson;

constexpr float kPi = 3.14159265F;

/** The destructible objects of the cases: a nodule (work 4, respawn 33 s, only on a quest) north of a place, a far nodule, an altar. */
std::string object_document(const state::WorldPosition& nodule, const state::WorldPosition& altar) {
    const json rows = json::array({{{"id", "Qf_Nodule"}, {"table", "Gatherables"}, {"time", 4.0}, {"respawn", {33.0, 33.0}}, {"loot", "QuestInteractable"},
                                    {"event", "Qf_Nodule_interact"}},
                                   {{"id", "Qf_Altar"}, {"table", "QuestGatherables"}, {"time", 1.0}, {"respawn", {200.0, 300.0}}, {"loot", "Empty"},
                                    {"event", "Qf_Altar_interact"}}});
    const auto object = [](const char* key, const char* actor, std::uint32_t row, const char* tag, bool onlyOnQuest, const state::WorldPosition& at) {
        return json{{"key", key},      {"actor", actor},     {"asset", "00112233445566778899aabbccddeeff"}, {"sub", 2},
                    {"slot", 4},       {"variant", "Test"},  {"row", row},                                  {"tag", tag},
                    {"onlyOnQuest", onlyOnQuest}, {"position", {at.x, at.y, at.z}}};
    };
    const json objects = json::array({object("11111111-0000-4000-8000-000000000001", "0b0000000000000a0000000000000000", 0, "IO_Qf_Nodule", true, nodule),
                                      object("11111111-0000-4000-8000-000000000002", "1b0000000000000a0000000000000000", 0, "IO_Qf_Nodule", true,
                                             state::WorldPosition{nodule.x + 40.0F, nodule.y, nodule.z}),
                                      object("11111111-0000-4000-8000-000000000003", "2b0000000000000a0000000000000000", 1, "IO_Qf_Altar", false, altar)});
    return json{{"format", 1}, {"rows", rows}, {"objects", objects}}.dump();
}

/** The quest of the nodules: a TaskGameEvent of 2 with the event and the tag of the nodule. */
void add_nodule_quest(json& quests) {
    quests["tasks"]["qf_nodules"] = questfix::task("Qf_Nodules", "taskgameevent", 14,
                                                   {{"KillEnemyType", "Qf_Nodule_interact"}, {"TargetQty", "2"}, {"InteractTag", "IO_Qf_Nodule"}});
    quests["objectives"].push_back({{"id", "Qf_NoduleQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qf_nodules", json::array()})})}});
}

/** @return The entries (the first two bytes of the actor in hexadecimal, the state) of the last LootTracker map of a client after an index. */
std::vector<std::pair<std::string, std::uint32_t>> tracker_entries(const FakeClient& client, std::size_t from, bool& seen) {
    const std::vector<std::vector<chests::TrackerEntry>> maps = chests::tracker_maps(client, 1, from);
    seen = !maps.empty();
    std::vector<std::pair<std::string, std::uint32_t>> entries;
    for (const chests::TrackerEntry& entry : seen ? maps.back() : std::vector<chests::TrackerEntry>{}) {
        char text[8];
        std::snprintf(text, sizeof(text), "%02x%02x", entry.actor[0], entry.actor[1]);
        entries.emplace_back(text, entry.state);
    }
    return entries;
}

/** QF1: the destructible quest objects: the reach, the look, the quest gate, the work, the claim of the owner, the respawn, two sessions. */
void destructible_objects(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    add_nodule_quest(quests);
    check(shop.host.quests.load_text(quests.dump()), "control: the quest of the nodules loads");
    shop.enter();
    // The exact positions of the characters (a report of each client; the rule needs no camera estimate).
    shop.walk_to(kCharacterA, state::WorldPosition{shop.entity(kCharacterA)->position.x + 1.0F, shop.entity(kCharacterA)->position.y, shop.entity(kCharacterA)->position.z});
    shop.walk_to(kCharacterB, state::WorldPosition{shop.entity(kCharacterB)->position.x + 1.0F, shop.entity(kCharacterB)->position.y, shop.entity(kCharacterB)->position.z});
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(!a->positionEstimated && !b->positionEstimated, "control: A and B have exact positions");
    world::QuestObjects& objects = shop.host.world.quest_objects();
    // No data: a strike changes nothing.
    objects.strike(*a, 1, 0);
    check(objects.counts().strikes == 0 && a->questObjects.empty(), "no quest object data: a strike changes nothing");
    const state::WorldPosition nodule{a->position.x, a->position.y + 2.0F, a->position.z};
    const state::WorldPosition altar{nodule.x, nodule.y + 30.0F, nodule.z}; // 30 m from the nodule: out of the reach of a strike there.
    check(shop.host.questObjects.load_text(object_document(nodule, altar)), "control: the quest object data loads");
    world::Quests& system = shop.host.world.quests();
    check(system.grant(*a, "Qf_NoduleQuest", "command", 0).ok(), "control: A has the task of the nodules; B has none");
    // B at the nodule of A: no active task with the tag (InteractOnlyOnQuest).
    const state::WorldPosition placeOfB = b->position;
    b->position = state::WorldPosition{nodule.x, nodule.y - 2.0F, nodule.z};
    b->look = 0.0F;
    objects.strike(*b, 1, 0);
    check(b->questObjects.empty() && objects.counts().refused[static_cast<std::size_t>(world::QuestObjectVerdict::notOnQuest)] == 1,
          "a character with no active task of the tag: no hit (notOnQuest)");
    b->position = placeOfB;
    // A looks south: the nodule is behind it.
    a->look = kPi;
    objects.strike(*a, 1, 0);
    check(a->questObjects.empty() && objects.counts().refused[static_cast<std::size_t>(world::QuestObjectVerdict::behind)] == 1,
          "the object behind the look: no hit");
    a->look = 0.0F;
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    objects.strike(*a, 2, 0);
    objects.strike(*a, 2, 0);
    check(a->questObjects.size() == 1 && a->questObjects.begin()->second.hits == 1
              && objects.counts().refused[static_cast<std::size_t>(world::QuestObjectVerdict::sameStrike)] == 1,
          "one strike hits the object one time (a second call of the same strike: sameStrike)");
    objects.strike(*a, 3, 0);
    objects.strike(*a, 4, 0);
    check(a->questObjectTracker.empty() && questfix::quest_of(*a, "Qf_NoduleQuest")->tasks[0].progress == 0, "3 of the 4 units: not destroyed");
    objects.strike(*a, 5, 0);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    bool seenA = false;
    const auto entriesA = tracker_entries(shop.clientA, fromA, seenA);
    // Code review of stage 1.8e, finding QR8E-R9: each LootTracker fragment that B got in the window, of each interest (its own character
    // and the observer record of A), not only the interest of its own character.
    std::size_t foreignB = 0;
    std::size_t ownB = 0;
    for (const auto& seen : fragments_of(shop.clientB, chests::kTrackerSlot, chests::kTrackerType, fromB)) {
        ++(seen.interest == 1 ? ownB : foreignB);
    }
    note("QF1 the LootTracker fragments of B after the destruction by A: " + std::to_string(foreignB) + " of the observer record of A, " + std::to_string(ownB)
         + " of its own character");
    check(objects.counts().destroyed == 1 && a->questObjectTracker.size() == 1 && a->questObjectTracker[0].state == world::quest_object_rules::kLooted
              && questfix::quest_of(*a, "Qf_NoduleQuest")->tasks[0].progress == 1,
          "the 4th unit (BaseGatherTime 4) destroys the object: the Looted claim of A, the game event counts for the task (1 of 2)");
    check(seenA && entriesA.size() == 1 && entriesA[0].first == "0b00" && entriesA[0].second == 2 && foreignB == 0 && ownB == 0
              && b->questObjectTracker.empty(),
          "the LootTracker state of A (slot 33) has the Looted entry with the managed actor; B gets no LootTracker fragment (no observer bytes)");
    objects.strike(*a, 6, 0);
    check(objects.counts().refused[static_cast<std::size_t>(world::QuestObjectVerdict::alreadyDestroyed)] == 1, "a destroyed object takes no hit");
    // The respawn of the row (33 s) for the character: the entry goes.
    const std::size_t beforeRespawn = shop.clientA.got.size();
    for (int second = 0; second < 34; ++second) {
        shop.host.advance(std::chrono::milliseconds(1000));
        shop.host.steps(1, {&shop.clientA, &shop.clientB});
    }
    const auto afterRespawn = tracker_entries(shop.clientA, beforeRespawn, seenA);

    check(a->questObjects.empty() && a->questObjectTracker.empty() && objects.counts().respawns == 1 && seenA && afterRespawn.empty(),
          "after the respawn time of the row the object is back for A: the map with no entry");
    for (std::uint32_t visit = 7; visit < 11; ++visit) {
        objects.strike(*a, visit, 0);
    }
    check(questfix::quest_of(*a, "Qf_NoduleQuest") == nullptr || system.ready(*questfix::quest_of(*a, "Qf_NoduleQuest")),
          "the second destruction completes the task (2 of 2)");
    // The far nodule: not in the reach (the grid looks at the cells near the character only).
    a->position = state::WorldPosition{nodule.x + 40.0F, nodule.y - 4.5F, nodule.z - 3.0F};
    objects.strike(*a, 11, 0);
    check(objects.counts().refused[static_cast<std::size_t>(world::QuestObjectVerdict::tooFar)] == 1,
          "an object 4.5 m away in the plane and 3 m higher (5.4 m): too far");
    // The altar (no quest gate, work 1) of B: one strike; A keeps its own state.
    b->position = state::WorldPosition{altar.x, altar.y - 2.0F, altar.z};
    b->look = 0.0F;
    objects.strike(*b, 2, 0);
    check(b->questObjectTracker.size() == 1 && b->questObjectTracker[0].actor[0] == 0x2b && a->questObjectTracker.size() == 1
              && a->questObjectTracker[0].actor[0] == 0x0b,
          "B destroys the altar with one strike (BaseGatherTime 1): its own claim; A keeps only the claim of its nodule");
}

/** QF2: a strike of a basic attack (a click of the attack graph, with no contact of the client) reaches the destructible object. */
void basic_attack_strike(const Options&) {
    attack_fixture::Range range;
    json quests = questfix::quest_document();
    add_nodule_quest(quests);
    check(range.host.quests.load_text(quests.dump()), "control: the quest data loads");
    const state::WorldPosition nodule{attack_fixture::kPlace.x, attack_fixture::kPlace.y + 2.0F, attack_fixture::kPlace.z};
    check(range.host.questObjects.load_text(object_document(nodule, state::WorldPosition{0.0F, 0.0F, 0.0F})), "control: the quest objects load");
    world::Entity* a = range.host.world.find_character(attack_fixture::kCharacterA);
    check(range.host.world.quests().grant(*a, "Qf_NoduleQuest", "command", 0).ok(), "control: A has the task of the nodules");
    attack_fixture::Attacker attacker{range.host, range.a, attack_fixture::kPlace};
    for (int click = 0; click < 4; ++click) {
        attacker.click();
        attacker.idle(45);
    }
    const world::QuestObjectCounts& counts = range.host.world.quest_objects().counts();
    note("QF2: strikes " + std::to_string(counts.strikes) + ", hits " + std::to_string(counts.hits) + ", destroyed " + std::to_string(counts.destroyed));
    check(counts.strikes >= 4 && counts.hits == 4 && counts.destroyed == 1 && a->questObjectTracker.size() == 1,
          "four clicks (each a strike of the attack graph of the server, no contact item): four units, the nodule is destroyed");
}

/** QF3: an Enter trigger area stays satisfied when the character leaves; a Stay Inside task regresses. */
void trigger_entry(const Options&) {
    QuestShop shop;
    json quests = quest_document_18b();
    quests["tasks"]["qf_enter"] = questfix::task("Qf_Enter", "tasktriggerarea", 3,
                                                 {{"TP_TargetTag", "Qb_Trigger"}, {"TriggerAreaType", "Enter"}, {"StayActive", "1"}, {"TargetQty", "1"}});
    quests["tasks"]["qf_inside"] = questfix::task("Qf_Inside", "tasktriggerarea", 3,
                                                  {{"TP_TargetTag", "Qb_Trigger"}, {"TriggerAreaType", "Stay Inside"}, {"StayActive", "1"}, {"TargetQty", "1"}});
    quests["tasks"]["qf_after"] = questfix::task("Qf_After", "taskachievement", 18, {{"ItemName", "Qb_Ready_Bit"}});
    quests["tasks"]["qf_area_root"] = questfix::task("Qf_Area_Root", "consecutivetaskcontainer", 1);
    quests["objectives"].push_back({{"id", "Qf_EnterQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qf_enter", json::array()}), json::array({1, "qf_after", json::array()}),
                                                          json::array({2, "qf_area_root", json::array({0, 1})})})}});
    quests["objectives"].push_back({{"id", "Qf_InsideQuest"}, {"cells", {{"Type", "Journey"}}},
                                    {"tree", json::array({json::array({0, "qf_inside", json::array()}), json::array({1, "qf_after", json::array()}),
                                                          json::array({2, "qf_area_root", json::array({0, 1})})})}});
    check(shop.host.quests.load_text(quests.dump()), "control: the two area quests load");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    world::Quests& system = shop.host.world.quests();
    check(system.grant(*a, "Qf_EnterQuest", "command", 0).ok() && system.grant(*a, "Qf_InsideQuest", "command", 0).ok(), "control: both quests");
    shop.walk_to(kCharacterA, kTriggerPlace);
    const world::QuestInstance* enter = questfix::quest_of(*a, "Qf_EnterQuest");
    const world::QuestInstance* inside = questfix::quest_of(*a, "Qf_InsideQuest");
    check(enter->tasks[0].state == world::quest_state::kCompleteActive && inside->tasks[0].state == world::quest_state::kCompleteActive
              && enter->tasks[1].state == world::quest_state::kActive,
          "in the volume: both area tasks are satisfied (StayActive), the next task is Active");
    shop.walk_to(kCharacterA, state::WorldPosition{kTriggerPlace.x, kTriggerPlace.y + 30.0F, kTriggerPlace.z});
    enter = questfix::quest_of(*a, "Qf_EnterQuest");
    inside = questfix::quest_of(*a, "Qf_InsideQuest");
    note("QF3 out of the volume: Enter " + questfix::states_text(*enter) + ", Stay Inside " + questfix::states_text(*inside));
    check(enter->tasks[0].state == world::quest_state::kCompleteActive && enter->tasks[1].state == world::quest_state::kActive,
          "Enter: out of the volume the task stays satisfied, and the next task stays Active (the monolith of 06_demonstorm2)");
    check(inside->tasks[0].state == world::quest_state::kActive && inside->tasks[1].state != world::quest_state::kActive,
          "Stay Inside: out of the volume the task regresses, and the next task is not Active");
}

/** The made-up data of the manifest case: each kind of row of the support manifest. */
json manifest_document() {
    json tasks = json::object();
    const auto add = [&](const char* id, const char* type, std::uint32_t code, json cells = json::object()) {
        tasks[gamedata::lower_text(id)] = questfix::task(id, type, code, cells);
    };
    add("Qf_A_Unlock", "taskunlockachievement", 20, {{"ItemName", "Qf_A_Bit"}});
    add("Qf_B_Wait", "taskachievement", 18, {{"ItemName", "Qf_A_Bit"}});
    add("Qf_B_Child", "taskobjective", 52, {{"ItemName", "Qf_I"}});
    add("Qf_B_Root", "consecutivetaskcontainer", 1);
    add("Qf_C_Fish", "taskfishingcatchfish", 76, {{"TargetQty", "1"}});
    add("Qf_D_Area", "tasktriggerarea", 3, {{"TP_TargetTag", "Qf_No_Volume"}, {"TriggerAreaType", "Enter"}});
    add("Qf_E_Unlock", "taskunlockachievement", 20, {{"ItemName", "Qf_E_Bit"}});
    add("Qf_H_Wait", "taskachievement", 18, {{"ItemName", "Qf_Never_Bit"}});
    // Each objective has its hand-in person (stage 1.8f, code review of stage 1.8e, finding QR8E-R6: the history hands in a quest only at
    // the person of its NpcDestinationId).
    const auto one = [](const char* id, json cells, const char* task) {
        cells["NpcDestinationId"] = "Test_Artificer";
        return json{{"id", id}, {"cells", cells}, {"tree", json::array({json::array({0, task, json::array()})})}};
    };
    json objectives = json::array();
    objectives.push_back(one("Qf_A", {{"Type", "MainStoryQuest"}, {"AchievementId", "Qf_A_Done"}, {"ObjectiveReward", "Qf_B"}}, "qf_a_unlock"));
    objectives.push_back({{"id", "Qf_B"},
                          {"cells", {{"Type", "MainStoryQuest"}, {"AchievementId", "Qf_B_Done"}, {"NpcDestinationId", "Test_Artificer"}}},
                          {"tree", json::array({json::array({0, "qf_b_wait", json::array()}), json::array({1, "qf_b_child", json::array()}),
                                                json::array({2, "qf_b_root", json::array({0, 1})})})}});
    objectives.push_back(one("Qf_C", {{"Type", "Journey"}}, "qf_c_fish"));
    objectives.push_back(one("Qf_D", {{"Type", "Journey"}}, "qf_d_area"));
    objectives.push_back(one("Qf_E", {{"Type", "Journey"}, {"SuccessGameEventId", "Qf_E_Success"}}, "qf_e_unlock"));
    objectives.push_back(one("Qf_F", {{"Type", "Mission"}}, "qf_e_unlock"));
    objectives.push_back(one("Qf_G", {{"Type", "Journey"}}, "qf_a_unlock"));
    objectives.push_back(one("Qf_H", {{"Type", "Journey"}, {"ObjectiveReward", "Qf_G"}}, "qf_h_wait"));
    objectives.push_back(one("Qf_I", {{"Type", "Journey"}, {"AchievementId", "Qf_I_Done"}}, "qf_e_unlock"));
    // The faction gate of a character with no faction (bit 0 of the mask of the client, 0x3A4C166): Syndicate (0x2) needs a faction,
    // "!Covenant" (0x7) does not.
    objectives.push_back(one("Qf_J", {{"Type", "Journey"}, {"RequiredFaction", "Syndicate"}}, "qf_a_unlock"));
    objectives.push_back(one("Qf_K", {{"Type", "Journey"}, {"RequiredFaction", "!Covenant"}}, "qf_a_unlock"));
    json achievements = json::array();
    std::uint32_t bit = 700;
    for (const char* id : {"Qf_A_Bit", "Qf_A_Done", "Qf_B_Done", "Qf_E_Bit", "Qf_I_Done", "Qf_Never_Bit", "Archetype_Mercenary"}) {
        achievements.push_back(json::array({id, bit++}));
    }
    return {{"format", 1},
            {"objectives", objectives},
            {"tasks", tasks},
            {"achievements", achievements},
            {"territories", json::array({{{"territory", 6}, {"objective", "Qf_A"}, {"table", "t"}}})},
            {"backstories", json::array({{{"id", "Archetype_Soldier"}, {"objectives", json::array()}, {"achievements", json::array({"Archetype_Mercenary"})},
                                          {"archetype", true}},
                                         {{"id", "Level60_Qf"}, {"level", "60"}, {"objectives", json::array({"Qf_H"})},
                                          {"achievements", json::array()}, {"archetype", false}}})},
            {"events", {{"qf_e_success", {{"id", "Qf_E_Success"}, {"UniversalExpAmount", "10"}, {"AzothReward", "30"}}}}}};
}

/** QF4: the support manifest: the kinds, the supported rows, the reasons, the closure of each history, determinism (I21). */
void support_manifest(const Options&) {
    QuestShop shop;
    check(shop.host.quests.load_text(manifest_document().dump()), "control: the manifest data loads");
    world::QuestSupport support(shop.host.world);
    const std::vector<world::SupportProfile> profiles = support.default_profiles();
    check(profiles.size() == 3 && profiles[0].id == "new:Archetype_Soldier" && profiles[1].id == "level60:none" && profiles[2].id == "template:Level60_Qf",
          "the profiles: the new Soldier first, a level 60 character with no template, and each template");
    const json first = support.manifest(profiles);
    const json second = support.manifest(profiles);
    check(first.dump() == second.dump(), "two manifests of the same world are the same bytes (deterministic)");
    const json& counts = first["counts"];
    note("QF4 counts: " + counts.dump());
    const auto row = [&](const char* id) -> const json& {
        for (const json& entry : first["objectives"]) {
            if (entry["id"] == id) {
                return entry;
            }
        }
        for (const json& entry : first["missionTemplates"]) {
            if (entry["id"] == id) {
                return entry;
            }
        }
        throw std::runtime_error("no row");
    };
    check(counts["rows"] == 11 && counts["kinds"]["objective"] == 10 && counts["kinds"]["missionTemplate"] == 1 && first["missionTemplates"].size() == 1,
          "11 rows: 10 objectives and 1 mission template (apart)");
    const auto faction_gate = [&](const char* id) {
        for (const json& gate : row(id)["gates"]["blocked"]) {
            if (gate.get<std::string>().find("RequiredFaction") != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    check(faction_gate("Qf_J") && !faction_gate("Qf_K"), "the faction gate: Syndicate needs a faction; !Covenant does not");
    check(row("Qf_A")["supported"] == true && row("Qf_B")["supported"] == true && row("Qf_I")["supported"] == true && row("Qf_G")["supported"] == true
              && row("Qf_H")["supported"] == true && counts["supported"] == 5,
          "5 supported rows: A (the territory start), B (ObjectiveReward of A), I (the child of B), G (ObjectiveReward of H), H (a template start)");
    check(row("Qf_C")["supported"] == false && row("Qf_C")["blocked"][0].get<std::string>().find("taskfishingcatchfish") != std::string::npos
              && row("Qf_D")["blocked"][0].get<std::string>().find("no trigger volume of the tag qf_no_volume") != std::string::npos
              && row("Qf_E")["blocked"][0].get<std::string>().find("AzothReward") != std::string::npos
              && row("Qf_F")["kind"] == "missionTemplate",
          "the exact reasons: no evaluator (fishing), no trigger volume, a reward with no owner (Azoth), a mission template");
    const json& newCharacter = first["profiles"][0];
    const json& noTemplate = first["profiles"][1];
    const json& withTemplate = first["profiles"][2];
    check(newCharacter["completable"] == 3 && newCharacter["played"] == json::array({"Qf_A", "Qf_I", "Qf_B"}),
          "the new character plays A (start), then B with its child I (B waits for the bit of A, then grants I): 3");
    check(noTemplate["completable"] == 0 && withTemplate["completable"] == 0 && row("Qf_H")["blocked"].empty(),
          "the level 60 histories: H is supported but waits for a bit that no source gives (0 for each)");
    check(row("Qf_G")["primary"]["played"] == false && row("Qf_G")["primary"]["cause"] == "no source grants it in this history",
          "G: supported, and in the new history no source grants it (the cause of the row)");
}

/** The state of the artificer that a character selects now (its id). */
std::string selected_state(QuestShop& shop, const world::Entity& character) {
    for (const gamedata::InteractionObject& object : shop.host.interactions.objects()) {
        if (object.person && object.person->npc == "Test_Artificer") {
            const std::int32_t state = shop.persons().selected_state(character, *shop.host.conversations.npc("Test_Artificer"));
            return state < 0 ? std::string("none") : shop.host.conversations.state(static_cast<std::uint32_t>(state)).id;
        }
    }
    return "no person";
}

/** QF5 (QG9 of the research): two sessions across a relog, a completion, the selection of a source, and a personal change of a person. */
void two_sessions_relog(const Options&) {
    QuestShop shop;
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    check(shop.host.world.quests().grant(*a, kStart, "territory", 6).ok() && shop.host.world.quests().grant(*b, kStart, "territory", 6).ok(),
          "control: A and B have the start quest");
    shop.play_start(kCharacterA);
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.hand_in(kCharacterA, kStart);
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    check(questfix::quest_of(*a, kStart) == nullptr && shop.has_bit(kCharacterA, "Qb_Start_Done"), "control: A completed the start quest");
    // A relogs; B stays.
    const std::string stateB = selected_state(shop, *b);
    shop.host.disconnect(shop.clientA.peer);
    shop.host.steps(3, {&shop.clientB});
    FakeClient again(27011);
    shop.host.enter(again, shop.a.registration());
    shop.host.steps(5, {&again, &shop.clientB});
    a = shop.entity(kCharacterA);
    check(a != nullptr && shop.has_bit(kCharacterA, "Qb_Start_Done") && questfix::quest_of(*a, kStart) == nullptr && questfix::quest_of(*b, kStart) != nullptr,
          "after the relog: A keeps its completion (the bit, no instance); B keeps its active start quest");
    // The source: A selects the offer state after the relog, B (the same person) a state with no offer.
    const std::string stateA = selected_state(shop, *a);
    note("QF5 the states: A " + stateA + ", B " + stateB + " -> " + selected_state(shop, *b));
    check(stateA == "Art_Offer" && selected_state(shop, *b) == stateB && stateB != "Art_Offer",
          "the selection of the source after the relog: A the offer state, B its own state (no shared state)");
    // The personal person change: the pirate (a swap person) is there for A only, also across a relog of B.
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit), "control: the pirate is there for A");
    shop.host.steps(2, {&again, &shop.clientB});
    const gamedata::PersonBinding& pirate = *shop.host.interactions.objects()[1].person;
    shop.host.disconnect(shop.clientB.peer);
    shop.host.steps(3, {&again});
    FakeClient againB(27012);
    shop.host.enter(againB, shop.b.registration());
    shop.host.steps(5, {&again, &againB});
    b = shop.entity(kCharacterB);
    check(b != nullptr && shop.persons().present(*a, pirate) && !shop.persons().present(*b, pirate),
          "after the relog of B: the pirate is there for A only");
    check(questfix::foreign_fragments(againB, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0
              && questfix::foreign_fragments(again, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0,
          "no session gets the Objectives state of the other character");
    shop.host.disconnect(again.peer);
    shop.host.disconnect(againB.peer);
    shop.host.steps(2, {});
}

/** The work of one scenario of QF6: the counts of the quests and of the persons. */
struct ClosureWork {
    std::uint64_t questVisits{0};
    std::uint64_t instanceLookups{0};
    std::uint64_t subscribers{0};
    std::uint64_t stateVisits{0};
    std::uint64_t topicVisits{0};
    double seconds{0.0};
};

/** The start quest, its hand-in, the offer of the next quest, an achievement change, and 200 steps, with `extra` unrelated definitions. */
ClosureWork closure_scenario(std::size_t extra) {
    QuestShop shop;
    if (extra > 0) {
        json quests = quest_document_18b();
        json conversations = conversation_document_18b();
        for (std::size_t index = 0; index < extra; ++index) {
            const std::string id = "Qf_Unrelated_" + std::to_string(index);
            quests["objectives"].push_back({{"id", id},
                                            {"cells", {{"Type", "Journey"}, {"RequiredAchievementId", "Qb_Ready_Bit"}, {"AchievementId", "Qb_Keep"}}},
                                            {"tree", json::array({json::array({0, "qb_ready_unlock", json::array()})})}});
            if (index < extra / 10) {
                const std::string state = "Qf_State_" + std::to_string(index);
                conversations["states"].push_back({{"id", state}, {"type", "Linear_Objective"}, {"objectives", json::array({id})}});
                conversations["npcs"].push_back({{"id", "Qf_Npc_" + std::to_string(index)}, {"states", json::array({state})}});
            }
        }
        if (!shop.host.quests.load_text(quests.dump()) || !shop.host.conversations.load_text(conversations.dump())) {
            throw std::runtime_error("QF6: the large data did not load");
        }
    }
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    world::Entity* a = shop.entity(kCharacterA);
    (void)shop.host.world.quests().grant(*a, kStart, "territory", 6);
    const auto begin = std::chrono::steady_clock::now();
    const world::QuestCounts quests0 = shop.host.world.quests().counts();
    const world::PersonCounts persons0 = shop.persons().counts();
    shop.play_start(kCharacterA);
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.hand_in(kCharacterA, kStart);
    shop.accept(kCharacterA, kNext);
    (void)shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit);
    for (int step = 0; step < 200; ++step) {
        shop.host.steps(1, {&shop.clientA, &shop.clientB});
    }
    ClosureWork work;
    const world::QuestCounts& quests = shop.host.world.quests().counts();
    work.questVisits = quests.questVisits - quests0.questVisits;
    work.instanceLookups = quests.instanceLookups - quests0.instanceLookups;
    work.subscribers = quests.subscribersVisited - quests0.subscribersVisited;
    work.stateVisits = shop.persons().counts().stateVisits - persons0.stateVisits;
    work.topicVisits = shop.persons().counts().topicVisits - persons0.topicVisits;
    work.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - begin).count();
    if (questfix::quest_of(*a, kNext) == nullptr) {
        throw std::runtime_error("QF6: the offer of the next quest did not run");
    }
    return work;
}

/** QF6 (QG10 of the research): the availability and the offers with 20,000 unrelated objectives and 2,000 unrelated persons: the same work. */
void closure_load(const Options&) {
    const ClosureWork small = closure_scenario(0);
    const ClosureWork large = closure_scenario(20000);
    const auto text = [](const ClosureWork& work) {
        return std::to_string(work.questVisits) + " quest visits, " + std::to_string(work.instanceLookups) + " lookups, " + std::to_string(work.subscribers)
               + " subscribers, " + std::to_string(work.stateVisits) + " state visits, " + std::to_string(work.topicVisits) + " topic visits, "
               + std::to_string(work.seconds) + " s";
    };
    note("QF6 small data: " + text(small));
    note("QF6 20,000 unrelated objectives, 2,000 unrelated persons: " + text(large));
    check(large.questVisits == small.questVisits && large.instanceLookups == small.instanceLookups && large.subscribers == small.subscribers
              && large.stateVisits == small.stateVisits && large.topicVisits == small.topicVisits,
          "the same work with the unrelated definitions: no scan of the catalog in the grant, the hand-in, the offer, the bit change, or a tick");
    check(large.seconds < 2.0, "the scenario with the large data takes less than 2 s");
}

} // namespace

std::vector<Case> quest_closure_cases() {
    return {{"QF1", "quests 1.8e (I1): the destructible quest objects: reach, look, quest gate, work, claim, respawn, two sessions", destructible_objects},
            {"QF2", "quests 1.8e (I1): a strike of a basic attack (the attack graph of the server) reaches a destructible object", basic_attack_strike},
            {"QF3", "quests 1.8e (I2): an Enter trigger area stays satisfied out of its volume; Stay Inside regresses", trigger_entry},
            {"QF4", "quests 1.8e (I4): the support manifest: kinds, supported rows, reasons, histories, determinism", support_manifest},
            {"QF5", "quests 1.8e (I6, QG9): two sessions across a relog, a completion, a source selection, a personal person change", two_sessions_relog},
            {"QF6", "quests 1.8e (I6, QG10): the availability with 20,000 unrelated definitions: the same work", closure_load}};
}

} // namespace soulwarden::backend::tests
