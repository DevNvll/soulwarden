// The cases of stage QG4 (docs/systems/quests.md, the quest givers; docs/systems/persons.md): the grant of an artifact objective at the
// equip (OnEquipObjectiveId, the item level of the gear score), the presence of a person for each character, the loaders of the new members
// (equipGearScores, personsApart), and the support manifest (the equip grant, the causes of an NPC with no slot, the rows with no source).
// The world is the quest and person fixture of stage 1.8b (two characters A and B near the persons). Made-up accounts, characters, ids,
// items, and names only.
//
//   QG4A  the artifact grant: no grant at the world entry with the artifact in the paperdoll, no grant below the item level (a gear score
//         of 700 needs the level 6 of the test table), the grant at the equip (source equip), no second instance after a new equip, no
//         grant with the completion achievement, no grant with progression tables of before stage QG4; the Objectives state of A does not
//         reach B (MP-10)
//   QG4B  the presence of a person for each character (a person of a swap shown with its bit, the form of 06_NessaHarrower05): A with
//         the bit opens the conversation and accepts the offer; B with no bit is refused (notPresent) and its accept changes nothing; the
//         bit of A does not reach B (MP-10)
//   QG4C  the loaders: equipGearScores of the progression tables (the item level, a list of a wrong size or that goes down refuses the
//         file, a file with no member has no rule) and personsApart of the interaction data (the lookup of an NPC, a wrong form refuses)
//   QG4D  the support manifest: the equip grant of an artifact (its item level, the history at the level cap), an offer of an NPC with no
//         slot names the cause of personsApart (an instance) or no placement, the kinds of a row with no source (a faction rank-up row, a
//         mount race row), and a person slot with its NPC from the slice (a direct NPC template) gives an offer

#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

#include "support/cases.h"
#include "support/check.h"
#include "support/quest_giver_fixture.h"
#include "world/quest_support.h"

namespace soulwarden::backend::tests {
namespace {

using namespace questperson;
using namespace questgiver;
using questfix::json;

/** QG4A: the artifact grant at the equip. */
void artifact_grant(const Options&) {
    GiverShop shop;
    // The entry: A has the artifact in its paperdoll before it enters (rows only: no equip in the world), at the item level.
    check(shop.host.world.progress().set_level(kCharacterA, 6).ok() && shop.host.world.inventory().give(kCharacterA, kArtifactItem, 1).ok()
              && shop.equip_artifact(kCharacterA),
          "setup: A (level 6, not in the world) has the artifact in its ring slot");
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    check(shop.artifact(kCharacterA) == nullptr && shop.quests().counts().equipChecks == 0,
          "the world entry with an equipped artifact grants nothing (an equipped item is no equip; no grant of each artifact at the entry)");
    // The equip in the world at the item level: the grant (source equip), one time.
    check(shop.unequip_artifact(kCharacterA) && shop.equip_artifact(kCharacterA), "control: A takes the artifact off and equips it again");
    const world::QuestInstance* first = shop.artifact(kCharacterA);
    check(first != nullptr && shop.quests().counts().equipGrants == 1 && shop.quests().counts().equipChecks == 1,
          "the equip of the artifact at the level 6 grants its objective (OnEquipObjectiveId)");
    const std::uint64_t instance = first != nullptr ? first->instance : 0;
    check(shop.unequip_artifact(kCharacterA) && shop.equip_artifact(kCharacterA) && shop.artifact(kCharacterA) != nullptr
              && shop.artifact(kCharacterA)->instance == instance && shop.quests().counts().equipGrants == 1
              && shop.quests().counts().equipChecks == 2,
          "a new equip with the active instance: no second instance (the availability: alreadyActive)");
    check(questfix::foreign_fragments(shop.clientB, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0
              && shop.artifact(kCharacterB) == nullptr,
          "the Objectives state of A does not reach the session of B, and B has no artifact quest (MP-10)");
    // The item level: B at the level 5 equips the artifact (700 needs the level 6): no grant.
    check(shop.host.world.progress().set_level(kCharacterB, 5).ok() && shop.host.world.inventory().give(kCharacterB, kArtifactItem, 1).ok()
              && shop.equip_artifact(kCharacterB),
          "control: B at the level 5 equips the artifact");
    check(shop.artifact(kCharacterB) == nullptr && shop.quests().counts().equipChecks == 3 && shop.quests().counts().equipGrants == 1,
          "below the item level of the gear score (ProgressionTables::item_level): no grant");
    // The completion achievement: no grant (the normal availability: completedBefore).
    check(shop.host.world.progress().set_level(kCharacterB, 6).ok()
              && shop.host.world.ownership().unlock_achievement(*shop.entity(kCharacterB), kArtifactDone, kArtifactDoneBit),
          "control: B has the level 6 and the completion achievement of the artifact objective");
    check(shop.unequip_artifact(kCharacterB) && shop.equip_artifact(kCharacterB) && shop.artifact(kCharacterB) == nullptr
              && shop.quests().counts().equipGrants == 1,
          "an equip with the completion achievement: no grant");
    // A plain ring is no artifact: no look.
    const std::uint64_t checks = shop.quests().counts().equipChecks;
    check(shop.host.world.inventory().give(kCharacterA, "QbStone", 1).ok() && shop.quests().counts().equipChecks == checks,
          "an item with no OnEquipObjectiveId is no look of the quest system");
    // Progression tables of before stage QG4 (no equipGearScores): no item level, no grant.
    GiverShop old(false);
    old.enter();
    check(old.host.world.progress().set_level(kCharacterA, 6).ok() && old.host.world.inventory().give(kCharacterA, kArtifactItem, 1).ok()
              && old.equip_artifact(kCharacterA) && old.artifact(kCharacterA) == nullptr && old.quests().counts().equipChecks == 1
              && old.quests().counts().equipGrants == 0,
          "progression tables with no equipGearScores have no item level: the equip grants nothing");
}

/** QG4B: the presence of a person for each character (MP-10). */
void presence_two_characters(const Options&) {
    GiverShop shop;
    shop.enter();
    questfix::send_2709(shop.host, shop.clientA, kCharacterA);
    questfix::send_2709(shop.host, shop.clientB, kCharacterB);
    world::Entity* a = shop.entity(kCharacterA);
    world::Entity* b = shop.entity(kCharacterB);
    const gamedata::PersonBinding& pirate = *shop.host.interactions.objects()[1].person;
    check(pirate.presence == gamedata::PersonPresence::showWhen && !shop.persons().present(*a, pirate) && !shop.persons().present(*b, pirate),
          "control: the person of the swap is there for nobody with no bit (showWhen, the form of 06_NessaHarrower05)");
    check(shop.host.world.ownership().unlock_achievement(*a, "Test_Swap_Pirate", kSwapPirateBit), "control: A gets the swap bit");
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    check(shop.persons().present(*a, pirate) && !shop.persons().present(*b, pirate), "the person is there for A only (no shared state)");
    const std::uint64_t notPresent = shop.persons().counts().notPresent;
    shop.request(shop.clientA, shop.talk(kPirate));
    shop.request(shop.clientB, shop.talk(kPirate));
    check(a->conversation && a->conversation->open && !b->conversation && shop.persons().counts().notPresent == notPresent + 1,
          "A opens the conversation of the person; B is refused (notPresent): the client of B does not show it");
    shop.accept(kCharacterA, kCaged);
    shop.accept(kCharacterB, kCaged);
    check(questfix::quest_of(*a, kCaged) != nullptr && questfix::quest_of(*b, kCaged) == nullptr
              && shop.offers(world::QuestRequestResult::accepted) == 1 && shop.offers(world::QuestRequestResult::noConversation) == 1,
          "the offer of the person (3772): A accepts it from the selected state; the accept of B changes nothing (noConversation)");
    check(questfix::foreign_fragments(shop.clientB, questfix::kObjectivesSlot, questfix::kObjectivesType) == 0
              && questfix::foreign_fragments(shop.clientB, 66, 3210) == 0,
          "the Objectives state and the achievement bits of A do not reach B (owner only, MP-10)");
}

/** QG4C: the loaders of the new members. */
void loaders(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    // The file of the progression tables (the form of tools/gamedata/progression_tables.py, a file of format 1 with the new member).
    const auto progressionPath = options.workDirectory / "qg4-progression.json";
    json levels = json::array();
    json caps = json::array();
    for (std::uint32_t level = 0; level < 7; ++level) {
        levels.push_back({level * 100, level == 0 ? 0 : 5, level * 50, 0});
        caps.push_back(json::array({100 + level * 100, level >= 5}));
    }
    const json progression = {{"format", 1},
                              {"levelCap", 5},
                              {"levels", levels},
                              {"attributes", {{"minimum", 5}, {"maximum", 6}, {"constitutionHealth", {0, 40}}}},
                              {"vitals", {{"health", 800}, {"stamina", 100}, {"mana", 100}}},
                              {"rankTables", {{"WeaponMastery", {{"experience", {10, 10, 10}}, {"resetCost", {0, 0, 0}}}}}},
                              {"progressions", json::array({json::array({"SwordAbilityTable", "weapon", 2, "WeaponMastery"})})},
                              {"abilities", json::array()},
                              {"equipGearScores", caps}};
    const auto load_progression = [&](const json& document, gamedata::ProgressionTables& tables) {
        std::ofstream(progressionPath) << document.dump();
        return tables.load(progressionPath.string());
    };
    gamedata::ProgressionTables fromFile;
    check(load_progression(progression, fromFile) && fromFile.item_level(700) == 7 && fromFile.item_level(150) == 2 && fromFile.item_level(100) == 1
              && fromFile.data().equipGearScores[5] == std::make_pair(600U, true),
          "the file loader reads equipGearScores: one row for each level (700: the level 7 of this file, 150: 2, 100: 1)");
    json shortList = progression;
    shortList["equipGearScores"].erase(6);
    json down = progression;
    down["equipGearScores"][3][0] = 50;
    json noMember = progression;
    noMember.erase("equipGearScores");
    gamedata::ProgressionTables refusedShort;
    gamedata::ProgressionTables refusedDown;
    gamedata::ProgressionTables older;
    check(!load_progression(shortList, refusedShort) && !load_progression(down, refusedDown) && load_progression(noMember, older)
              && older.item_level(700) == 0,
          "a list of a wrong size or that goes down refuses the file; a file with no member loads and has no rule");
    // The interaction data with the member personsApart (the form of tools/gamedata/interactions.py).
    const auto interactionPath = options.workDirectory / "qg4-interaction-data.json";
    const json interactions = {
        {"format", 1},
        {"world", "newworld_vitaeeterna"},
        {"counts", {{"objects", 1}, {"bounds", 1}, {"options", 1}, {"achievements", 0}}},
        {"options", json::array({{{"name", "OpenConversation"}, {"id", gamedata::interaction_option_id("OpenConversation")}, {"action", 63},
                                  {"committed", true}, {"delay", 0.0}, {"hold", 0.0}, {"instantCancel", false}, {"cooldown", 0}, {"maxUsage", 0.0},
                                  {"inactive", 0.0}, {"requiredLevel", 0}, {"requiredItem", ""}, {"requiredItemCount", 0}, {"requiredCurrency", 0},
                                  {"requiredAchievement", -1}, {"statusRules", false}}})},
        {"achievements", json::array()},
        {"objects",
         json::array({{{"key", "0b1ec700-0000-4000-8000-000000000001"}, {"kind", "npc"}, {"slice", "slices/test/npc_template.dynamicslice"},
                       {"asset", "a55e7001000000000000000000000001"}, {"sub", 2}, {"actor", "0b1122334f424a540000000000000000"},
                       {"region", "r_+00_+00"}, {"position", {100.0, 202.0, 30.0}},
                       {"bounds", json::array({{{"slot", 2}, {"component", "c000000000000001"}, {"target", "7100000000000001"},
                                                {"position", {100.0, 202.0, 30.0}}, {"rangeSq", 25.0}, {"capacity", -1},
                                                {"lineOfSight", false}, {"arcRequired", false}, {"arc", 180.0},
                                                {"options", json::array({json::array({0, "544a424f3322110b"})})}, {"lore", -1},
                                                {"requires", json::object()}}})},
                       {"person", {{"npc", "Qg4_Template_Npc"}, {"variant", ""}, {"table", ""}, {"template", true}, {"clientChild", true},
                                   {"spawner", "0b1ec700-0000-4000-8000-000000000001"}}}}})},
        {"personsApart", json::array({{{"npc", "Qg4_Dungeon_Npc"}, {"cause", "otherWorld"}, {"world", "nw_dungeon_test_00"},
                                       {"slice", "slices/test/dungeon_npc.dynamicslice"}, {"via", json::array()}, {"placement", "x y"}}})}};
    const auto load_interactions = [&](const json& document, gamedata::InteractionData& data) {
        std::ofstream(interactionPath) << document.dump();
        return data.load(interactionPath.string());
    };
    gamedata::InteractionData file;
    check(load_interactions(interactions, file) && file.objects()[0].person && file.objects()[0].person->npc == "Qg4_Template_Npc"
              && file.objects()[0].person->unresolved.empty() && file.persons_apart("QG4_DUNGEON_NPC").size() == 1
              && file.persons_apart("qg4_dungeon_npc")[0]->world == "nw_dungeon_test_00",
          "the file loader: a person with the NPC of its slice (template) is resolved; personsApart by its NPC id (" + file.error() + ")");
    json badApart = interactions;
    badApart["personsApart"][0].erase("cause");
    json notList = interactions;
    notList["personsApart"] = json::object();
    json oldFile = interactions;
    oldFile.erase("personsApart");
    gamedata::InteractionData refusedApart;
    gamedata::InteractionData refusedList;
    gamedata::InteractionData before;
    check(!load_interactions(badApart, refusedApart) && !refusedApart.loaded() && !load_interactions(notList, refusedList)
              && load_interactions(oldFile, before) && before.persons_apart("Qg4_Dungeon_Npc").empty(),
          "a person apart with no cause or a member that is not a list refuses the file; a file of before has none");
    gamedata::ProgressionTables tables;
    tables.set(giver_progression());
    check(tables.item_level(700) == 6 && tables.item_level(500) == 5 && tables.item_level(50) == 1 && tables.item_level(801) == gamedata::kNoItemLevel,
          "the item level: the first row whose MaxEquippableGearScore admits the gear score (700: the level 6; 801: no level)");
    gamedata::ProgressionTables none;
    none.set(giver_progression(false));
    check(none.item_level(700) == 0, "no member equipGearScores: no rule (0)");
    // The interaction data: the persons apart.
    gamedata::InteractionData data;
    check(personfix::set_person_data(data), "control: the person data");
    data.set_persons_apart({{"Qg4_Dungeon_Npc", "otherWorld", "nw_dungeon_test_00", "slices/test/dungeon_npc.dynamicslice", "x"},
                            {"QG4_DUNGEON_NPC", "levelScript", "newworld_vitaeeterna", "slices/test/other.dynamicslice", "y"}});
    check(data.persons_apart("qg4_dungeon_npc").size() == 2 && data.persons_apart("Qg4_Nowhere_Npc").empty(),
          "personsApart: the persons of an NPC id in any case; an NPC with none");
}

/** QG4D: the support manifest of the rules of stage QG4. */
void giver_manifest(const Options&) {
    GiverShop shop;
    gamedata::InteractionData& data = shop.host.interactions;
    data.set_persons_apart({{"Qg4_Dungeon_Npc", "otherWorld", "nw_dungeon_test_00", "slices/test/dungeon_npc.dynamicslice", "x"}});
    // The artifact has a source of the world: the loot of a chest is a later part; a history gets it as an item of the history only with a
    // source, so the row is checked for its grant source (and the history for the equip with no source).
    world::QuestSupport support(shop.host.world);
    const json manifest = support.manifest(support.default_profiles());
    const auto row = [&](const char* id) -> const json& {
        for (const json& entry : manifest["objectives"]) {
            if (entry["id"] == id) {
                return entry;
            }
        }
        throw std::runtime_error("no row " + std::string(id));
    };
    const auto grant = [&](const char* id, const char* source) -> json {
        for (const json& entry : row(id)["grants"]) {
            if (entry["source"] == source) {
                return entry;
            }
        }
        return json::object();
    };
    const json equip = grant(kArtifactObjective, "equip");
    note("QG4D equip " + equip.dump() + "; dungeon " + grant("Qg4_Dungeon", "offer").dump() + "; nowhere " + grant("Qg4_Nowhere", "offer").dump());
    check(equip.value("supported", false) && equip.value("detail", std::string{}) == std::string(kArtifactItem) + " level 6",
          "the artifact row has the grant source equip with the item level of its gear score (700: the level 6)");
    const std::string dungeon = grant("Qg4_Dungeon", "offer").value("reason", std::string{});
    const std::string nowhere = grant("Qg4_Nowhere", "offer").value("reason", std::string{});
    check(dungeon.find("has no person slot") != std::string::npos && dungeon.find("otherWorld in the world nw_dungeon_test_00") != std::string::npos
              && nowhere.find("no placement of the NPC Qg4_Nowhere_Npc in any world") != std::string::npos,
          "an offer of an NPC with no slot names its cause: a person of another world, or no placement in the data");
    const auto blocked = [&](const char* id) {
        std::string all;
        for (const json& entry : row(id)["blocked"]) {
            all += entry.get<std::string>() + " | ";
        }
        return all;
    };
    check(blocked("Qg4_Dungeon").find("grant: no source of a kind that the backend runs (a person of an instance)") != std::string::npos
              && blocked("Qg4_Nowhere").find("(a person with no placement in the data)") != std::string::npos,
          "the line of the grant block names the group of the cause (the first reasons of the counts)");
    check(grant("Qg4_RankUp", "none").value("reason", std::string{}).find("rank-up row of a faction story") != std::string::npos
              && blocked("Qg4_RankUp").find("(a faction rank-up row that the validation lists do not name)") != std::string::npos
              && grant("Qg4_Race", "none").value("reason", std::string{}).find("mount race row") != std::string::npos,
          "a row with no source: a faction rank-up row (Type FactionStory_) and a mount race row (Type MountRace) have their reasons");
    check(grant(kCaged, "offer").value("supported", false), "the offer of a person with a slot and a presence rule is a grant of a kind that the backend runs");
    // The history of the artifact: the new character has no source of the item (no world source in the fixture): not played.
    const json& newCharacter = manifest["profiles"][0];
    bool played = false;
    for (const json& entry : newCharacter["played"]) {
        played = played || entry == kArtifactObjective;
    }
    check(!played && row(kArtifactObjective)["primary"]["played"] == false, "a history with no source of the artifact item does not equip it");
}

} // namespace

std::vector<Case> quest_giver_cases() {
    return {{"QG4A", "quests QG4 (I1): the artifact grant at the equip (the item level, no second instance, no grant at the entry), two sessions",
             artifact_grant},
            {"QG4B", "quests QG4 (I3, MP-10): the presence of a person for two characters, the offer of A, the refusal of B", presence_two_characters},
            {"QG4C", "quests QG4: the item level of equipGearScores and the lookup of personsApart", loaders},
            {"QG4D", "quests QG4 (I2, I4): the manifest: the equip grant, the causes of an NPC with no slot, the rows with no source", giver_manifest}};
}

} // namespace soulwarden::backend::tests
