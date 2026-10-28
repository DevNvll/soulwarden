#pragma once

// The quest giver fixture of the cases of stage QG4 (made-up data only): the quest data of stage 1.8b with an artifact objective and the
// offer of a caged person, the conversation data, the item level of the progression tables, and GiverShop (the quest and person world of
// stage 1.8b with the artifact item). The cases of stage QG4 (quest_giver_tests.cpp) and the corrections of its code review
// (review_qg4_tests.cpp) use it.

#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "support/quest_person_fixture.h"

namespace soulwarden::backend::tests::questgiver {

using namespace questperson;
using questfix::json;

constexpr const char* kArtifactItem = "Qg4_Artifact_Ring";
constexpr const char* kArtifactObjective = "Qg4_Objective_Artifact_Ring";
constexpr const char* kArtifactDone = "Qg4_Artifact_Done";
constexpr std::uint32_t kArtifactDoneBit = 90;
constexpr const char* kCaged = "Qg4_Caged";

/** The quest data of the cases: the quests of stage 1.8b, an artifact objective (no hand-in person), and the offer of the caged person. */
inline json giver_quests() {
    json document = quest_document_18b();
    document["tasks"]["qg4_artifact_equip"] =
        questfix::task("Qg4_Artifact_Equip", "taskequipitem", 45, {{"ItemName", kArtifactItem}, {"TargetQty", "1"}});
    document["tasks"]["qg4_wait"] = questfix::task("Qg4_Wait", "taskachievement", 18, {{"ItemName", "Test_Never"}});
    document["tasks"]["qg4_artifact_root"] = questfix::task("Qg4_Artifact_Root", "consecutivetaskcontainer", 1);
    document["objectives"].push_back({{"id", kArtifactObjective},
                                      {"cells", {{"Type", "Artifact"}, {"AchievementId", kArtifactDone}, {"CanBeAbandoned", "1"}}},
                                      {"tree", json::array({json::array({0, "qg4_artifact_equip", json::array()}),
                                                            json::array({1, "qg4_wait", json::array()}),
                                                            json::array({2, "qg4_artifact_root", json::array({0, 1})})})}});
    document["objectives"].push_back({{"id", kCaged},
                                      {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Pirate"}}},
                                      {"tree", json::array({json::array({0, "qg4_wait", json::array()})})}});
    // QG4D: the kinds of a row with no source, and the offers of an NPC with no slot.
    document["objectives"].push_back({{"id", "Qg4_RankUp"}, {"cells", {{"Type", "FactionStory_Covenant"}}},
                                      {"tree", json::array({json::array({0, "qg4_wait", json::array()})})}});
    document["objectives"].push_back({{"id", "Qg4_Race"}, {"cells", {{"Type", "MountRace"}}},
                                      {"tree", json::array({json::array({0, "qg4_wait", json::array()})})}});
    document["objectives"].push_back({{"id", "Qg4_Dungeon"}, {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "qg4_wait", json::array()})})}});
    document["objectives"].push_back({{"id", "Qg4_Nowhere"}, {"cells", {{"Type", "Journey"}}},
                                      {"tree", json::array({json::array({0, "qg4_wait", json::array()})})}});
    document["achievements"].push_back(json::array({kArtifactDone, kArtifactDoneBit}));
    return document;
}

/** The conversation data: the persons of stage 1.8b, an offer state of the pirate (shown while its swap bit is set), two NPC rows with no slot. */
inline json giver_conversations() {
    json document = conversation_document_18b();
    document["npcs"][1]["states"] = json::array({"Pirate_Default", "Pirate_Offer"});
    document["states"].push_back(
        {{"id", "Pirate_Offer"}, {"type", "Linear_Objective"}, {"requires", {{"RequiredLevel", "1"}}}, {"objectives", json::array({kCaged})}});
    document["npcs"].push_back({{"id", "Qg4_Dungeon_Npc"}, {"states", json::array({"Qg4_Dungeon_State"})}});
    document["npcs"].push_back({{"id", "Qg4_Nowhere_Npc"}, {"states", json::array({"Qg4_Nowhere_State"})}});
    document["states"].push_back({{"id", "Qg4_Dungeon_State"}, {"type", "Linear_Objective"}, {"initial", true}, {"objectives", json::array({"Qg4_Dungeon"})}});
    document["states"].push_back({{"id", "Qg4_Nowhere_State"}, {"type", "Linear_Objective"}, {"initial", true}, {"objectives", json::array({"Qg4_Nowhere"})}});
    return document;
}

/** The progression tables of the tests with the item level of a gear score (a gear score of 700 needs the level 6). */
inline gamedata::ProgressionData giver_progression(bool equipLevels = true) {
    gamedata::ProgressionData data = test_progression();
    if (equipLevels) {
        data.equipGearScores = {{100, false}, {200, false}, {300, false}, {400, false}, {500, true}, {800, true}};
    }
    return data;
}

/** The quest and person world with the artifact item, its objective, and the item level of the progression tables. */
struct GiverShop : QuestShop {
    explicit GiverShop(bool equipLevels = true) {
        std::vector<gamedata::ItemDefinition> items = quest_items();
        items.push_back(chests::chest_item(kArtifactItem, "Armor", "Artifact+EquippableRing+Jewelry", 5, 1, 700, 700));
        host.catalog.set(items);
        host.catalog.set_artifact_objectives({{kArtifactItem, kArtifactObjective, kArtifactDone, kArtifactDoneBit}});
        host.progression.set(giver_progression(equipLevels));
        if (!host.quests.load_text(giver_quests().dump())) {
            throw std::runtime_error("quest giver test: the quest data did not load: " + host.quests.error());
        }
        if (!host.conversations.load_text(giver_conversations().dump())) {
            throw std::runtime_error("quest giver test: the conversation data did not load: " + host.conversations.error());
        }
    }
    world::Quests& quests() {
        return host.world.quests();
    }
    const world::QuestInstance* artifact(const char* character) {
        return questfix::quest_of(*entity(character), kArtifactObjective);
    }
    /** The artifact from the bag into the paperdoll (the equip path of a request), then the next ticks. */
    bool equip_artifact(const char* character) {
        const world::Entity* own = entity(character);
        const std::vector<world::BagSlot>& slots = own != nullptr ? own->bag.slots : std::vector<world::BagSlot>{};
        std::optional<std::uint32_t> slot;
        if (own != nullptr) {
            for (std::uint32_t index = 0; index < slots.size(); ++index) {
                if (slots[index].item && slots[index].item->itemId == kArtifactItem) {
                    slot = index;
                }
            }
        } else {
            const auto holdings = host.world.inventory().holdings(character);
            for (std::uint32_t index = 0; holdings && index < holdings->bag.slots.size(); ++index) {
                if (holdings->bag.slots[index].item && holdings->bag.slots[index].item->itemId == kArtifactItem) {
                    slot = index;
                }
            }
        }
        const bool equipped = slot && host.world.inventory().equip(character, *slot).ok();
        host.steps(3, {&clientA, &clientB});
        return equipped;
    }
    bool unequip_artifact(const char* character) {
        const bool done = host.world.inventory().unequip(character, world::paperdoll::kRing).ok();
        host.steps(3, {&clientA, &clientB});
        return done;
    }
};

} // namespace soulwarden::backend::tests::questgiver
