// The corrections of the code review of stage 1.8b (in stage 1.8d; ~/Games/soulwarden/work/evidence/quests/18b/code-review/FINDINGS.md):
// the proof cases of the review, case QRBn for the finding QR8B-Rn, and the control QRBC. Each case fails on the tree of stage 1.8b and
// passes after its correction (docs/systems/quests.md). Made-up data only. The fixture helpers are the ones of quest_person_tests.cpp.

#include <sqlite3.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "server/rep/quest_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/chest_fixture.h"
#include "support/person_fixture.h"
#include "support/test_progression.h"
#include "support/quest_person_fixture.h"
#include "support/shop_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace personfix;
using namespace questperson;

json review_topic_quests(std::uint32_t target = 2) {
    json q = quest_document_18b();
    q["tasks"]["review_topic_task"] = questfix::task("Review_Topic_Task", "taskconversationtopic", 17,
        {{"ItemName", "Test_Artificer"}, {"UIScreenTag", "Qb_Topic"}, {"TargetQty", std::to_string(target)}});
    q["tasks"]["review_second_task"] = questfix::task("Review_Second_Task", "taskconversationtopic", 17,
        {{"ItemName", "Test_Artificer"}, {"UIScreenTag", "Review_Second_Topic"}, {"TargetQty", "1"}});
    q["tasks"]["review_topic_root"] = questfix::task("Review_Topic_Root", "simpletaskcontainer", 0);
    q["objectives"].push_back({{"id", "Review_Topics"}, {"cells", {{"Type", "Journey"}}},
        {"tree", json::array({json::array({0, "review_topic_task", json::array()}),
                              json::array({1, "review_second_task", json::array()}),
                              json::array({2, "review_topic_root", json::array({0, 1})})})}});
    return q;
}

json review_topic_conversations(bool switchAfterFirst = false) {
    json c = conversation_document_18b();
    c["npcs"][0]["states"] = json::array({"Review_Talk"});
    c["states"].push_back({{"id", "Review_Talk"}, {"topics", json::array({"Qb_Topic", "Review_Second_Topic"})}});
    c["topics"].push_back({{"id", "Review_Second_Topic"}, {"achievementUnlock", "Qb_Event_Bit"}});
    if (switchAfterFirst) {
        c["npcs"][0]["states"].push_back("Review_After");
        c["states"].push_back({{"id", "Review_After"}, {"requires", {{"RequiredAchievement", "Qb_Topic_Bit"}}}});
    }
    return c;
}

void prepare_topics(QuestShop& shop, bool switchAfterFirst = false) {
    check(shop.host.quests.load_text(review_topic_quests().dump()) &&
          shop.host.conversations.load_text(review_topic_conversations(switchAfterFirst).dump()), "control: topic data loads");
    shop.enter();
    check(shop.host.world.quests().grant(*shop.entity(kCharacterA), "Review_Topics", "command", 0).ok() &&
          shop.host.world.quests().grant(*shop.entity(kCharacterB), "Review_Topics", "command", 0).ok(),
          "control: two characters have the topic tasks");
    shop.request(shop.clientA, shop.talk(kArtificer));
    check(shop.entity(kCharacterA)->conversation && shop.entity(kCharacterA)->conversation->open &&
          shop.entity(kCharacterA)->conversation->selected.empty(), "control: A has an open conversation and no selected topic");
}

std::uint32_t topic_progress(QuestShop& shop, const char* character = kCharacterA, std::size_t index = 0) {
    return questfix::quest_of(*shop.entity(character), "Review_Topics")->tasks[index].progress;
}

bool review_sql(const std::string& path, const char* text) {
    sqlite3* db = nullptr;
    const bool ok = sqlite3_open(path.c_str(), &db) == SQLITE_OK && sqlite3_exec(db, text, nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(db);
    return ok;
}

void review_unselected_topic(const Options&) {
    const gamedata::SliceIds slice = quest_slice();
    QuestShop shop(0, ":memory:", &slice);
    prepare_topics(shop);
    shop.close(kCharacterA, kArtificer);
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    note("QRB3 unselected finish: progress=" + std::to_string(topic_progress(shop)) +
         ", bit=" + std::to_string(shop.has_bit(kCharacterA, "Qb_Topic_Bit")));
    check(topic_progress(shop) == 0 && !shop.has_bit(kCharacterA, "Qb_Topic_Bit"),
          "5397 must give no effect for a topic that A never selected");
    check(topic_progress(shop, kCharacterB) == 0 && !shop.has_bit(kCharacterB, "Qb_Topic_Bit"), "B receives no effect");
}

void review_topic_storage_retry(const Options& options) {
    const std::string path = (options.workDirectory / "review-topic.sqlite3").string();
    std::filesystem::remove(path);
    QuestShop shop(0, path);
    prepare_topics(shop);
    shop.topic18(kCharacterA, crc("Qb_Topic"));
    shop.close(kCharacterA, kArtificer);
    check(review_sql(path, "CREATE TRIGGER review_refuse BEFORE UPDATE ON character_quests BEGIN SELECT RAISE(FAIL, 'review'); END"),
          "control: the topic transaction is refused");
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    check(topic_progress(shop) == 0 && !shop.has_bit(kCharacterA, "Qb_Topic_Bit") &&
          shop.host.world.quests().counts().storeFailures == 1, "control: the refused effect changes no quest or achievement");
    check(review_sql(path, "DROP TRIGGER review_refuse"), "control: store writes work again");
    shop.host.advance(std::chrono::milliseconds(3000));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    note("QRB4 retried finish: progress=" + std::to_string(topic_progress(shop)) +
         ", bit=" + std::to_string(shop.has_bit(kCharacterA, "Qb_Topic_Bit")) +
         ", duplicates=" + std::to_string(shop.topic_count(world::TopicResult::duplicate)));
    check(topic_progress(shop) == 1 && shop.has_bit(kCharacterA, "Qb_Topic_Bit"),
          "a selected topic whose transaction failed must apply once when retried");
}

void review_topic_batch_reselect(const Options&) {
    QuestShop shop;
    prepare_topics(shop, true);
    shop.topic18(kCharacterA, crc("Qb_Topic"));
    shop.topic18(kCharacterA, crc("Review_Second_Topic"));
    check(shop.entity(kCharacterA)->conversation->selected.size() == 2, "control: A selected both offered topics");
    shop.close(kCharacterA, kArtificer);
    // One ordinary 5397 contains both topics of the close.
    const world::Entity* a = shop.entity(kCharacterA);
    const auto body = server::rep::finished_topics_body(0, a->id, {crc("Qb_Topic"), crc("Review_Second_Topic")});
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(server::rep::kTypeConversationTopicsFinished, a->actor.bytes, body));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    note("QRB5 batch: first=" + std::to_string(topic_progress(shop)) + ", second=" + std::to_string(topic_progress(shop, kCharacterA, 1)) +
         ", notOffered=" + std::to_string(shop.topic_count(world::TopicResult::notOffered)));
    check(topic_progress(shop) == 1 && topic_progress(shop, kCharacterA, 1) == 1 && shop.has_bit(kCharacterA, "Qb_Event_Bit"),
          "both selected topics of one 5397 must finish even when the first effect changes the state");
}

void review_closed_range(const Options&) {
    QuestShop shop;
    prepare_topics(shop);
    shop.topic18(kCharacterA, crc("Qb_Topic"));
    shop.close(kCharacterA, kArtificer);
    check(!shop.entity(kCharacterA)->interaction && shop.entity(kCharacterA)->conversation &&
          !shop.entity(kCharacterA)->conversation->open, "control: A closed the use and kept its closing topic context");
    shop.walk_to(kCharacterA, {100.0F, 212.0F, 30.0F});
    check(shop.entity(kCharacterA)->position.y > 211.0F, "control: an accepted position is outside the person range");
    shop.topic18(kCharacterA, crc("Qb_Topic"), true);
    note("QRB6 distant closed finish: progress=" + std::to_string(topic_progress(shop)) +
         ", context=" + std::to_string(shop.entity(kCharacterA)->conversation.has_value()));
    check(topic_progress(shop) == 0 && !shop.has_bit(kCharacterA, "Qb_Topic_Bit"),
          "range loss after a normal close must invalidate its topic effects");
}

json review_cutscene_document() {
    json q = quest_document_18b();
    q["objectives"].push_back({{"id", "Review_Cutscene"}, {"cells", {{"Type", "Journey"}}},
        {"tree", json::array({json::array({0, "qb_start_cutscene", json::array()})})}});
    return q;
}

void review_old_cutscene_end(const Options&) {
    const gamedata::SliceIds slice = quest_slice();
    QuestShop shop(0, ":memory:", &slice);
    check(shop.host.quests.load_text(review_cutscene_document().dump()), "control: a single cutscene task loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    auto& quests = shop.host.world.quests();
    const auto first = quests.grant(*a, "Review_Cutscene", "command", 0);
    check(first.ok(), "control: A has the original cutscene task");
    shop.send(kCharacterA, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    shop.send(kCharacterA, server::rep::kTypeCutsceneStarted, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    const auto replacement = quests.reset(*a, first.instance);
    check(replacement.ok() && replacement.instance != first.instance &&
          questfix::quest_of(*a, "Review_Cutscene")->tasks[0].state == world::quest_state::kActive,
          "control: the reset made a new task which received no grant or start");
    shop.cutscene_end(kCharacterA);
    note("QRB7 replacement cutscene task state=" + std::to_string(questfix::quest_of(*a, "Review_Cutscene")->tasks[0].state));
    check(questfix::quest_of(*a, "Review_Cutscene")->tasks[0].state == world::quest_state::kActive,
          "6705 of the old cutscene must not complete the replacement instance");
}

void review_duplicate_cutscene_request(const Options&) {
    QuestShop shop;
    check(shop.host.quests.load_text(review_cutscene_document().dump()), "control: a single cutscene task loads");
    shop.enter();
    world::Entity* a = shop.entity(kCharacterA);
    check(shop.host.world.quests().grant(*a, "Review_Cutscene", "command", 0).ok(), "control: A has the cutscene task");
    shop.send(kCharacterA, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    shop.send(kCharacterA, server::rep::kTypeCutsceneStarted, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    shop.send(kCharacterA, server::rep::kTypeRequestStartCutscene, server::rep::kPlayerCutsceneComponent, qcrc(kCutscene));
    shop.cutscene_end(kCharacterA);
    note("QRB8 duplicate request: task state=" + std::to_string(questfix::quest_of(*a, "Review_Cutscene")->tasks[0].state));
    check(shop.host.world.quests().ready(*questfix::quest_of(*a, "Review_Cutscene")),
          "a late duplicate 6703 must preserve the started cutscene and accept its 6705");
}

void review_missing_next(const Options&) {
    QuestShop shop;
    json q = quest_document_18b();
    for (auto& o : q["objectives"]) if (o["id"] == "Qb_Paid") o["cells"]["ObjectiveReward"] = "Review_Missing_Next";
    check(shop.host.quests.load_text(q.dump()), "control: the loader accepts a missing next-objective reference");
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    const auto grant = shop.host.world.quests().grant(*a, "Qb_Paid", "command", 0);
    const auto xp = a->progression.experience;
    const auto coin = a->wallet.coin;
    const auto result = shop.host.world.quests().hand_in(*a, grant.instance);
    note("QRB9 missing next: result=" + std::string(world::quest_error_text(result.error)) + ", next='" + result.next + "'");
    check(!result.ok() && questfix::quest_of(*a, "Qb_Paid") != nullptr && a->progression.experience == xp && a->wallet.coin == coin,
          "a missing next objective must refuse the completion and its whole reward");
}

void review_negative_event_quantity(const Options&) {
    QuestShop shop;
    json q = quest_document_18b();
    q["events"]["qb_paid_success"]["ItemRewardQty"] = "-1";
    check(shop.host.quests.load_text(q.dump()), "control: the loader accepts a negative event item quantity");
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    const auto grant = shop.host.world.quests().grant(*a, "Qb_Paid", "command", 0);
    const auto xp = a->progression.experience;
    const auto result = shop.host.world.quests().hand_in(*a, grant.instance);
    note("QRB10 negative event quantity: parsed=" + std::to_string(shop.host.quests.game_event("Qb_Paid_Success")->itemQty) +
         ", result=" + world::quest_error_text(result.error) + ", reward items=" + std::to_string(shop.count(kCharacterA, "QbPants")));
    check(result.error == world::QuestError::cannotPay && questfix::quest_of(*a, "Qb_Paid") != nullptr && a->progression.experience == xp,
          "an unsupported negative event quantity must refuse the whole hand-in");
}

void review_waiting_event_bag(const Options&) {
    QuestShop shop;
    json q = quest_document_18b();
    q["events"]["qb_whisper_event"]["ItemReward"] = "QbPants";
    q["objectives"].push_back({{"id", "Review_Event"}, {"cells", {{"Type", "Journey"}}},
        {"tree", json::array({json::array({0, "qb_next_whisper", json::array()})})}});
    check(shop.host.quests.load_text(q.dump()), "control: a task event with a direct item loads");
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    std::uint32_t given = 0;
    while (given < 600 && shop.host.world.inventory().give(kCharacterA, "QbStone", 1).ok()) ++given;
    const auto grant = shop.host.world.quests().grant(*a, "Review_Event", "command", 0);
    check(grant.ok() && questfix::quest_of(*a, "Review_Event")->tasks[0].state == world::quest_state::kActive &&
          shop.count(kCharacterA, "QbPants") == 0, "control: the full bag makes the task event wait");
    check(shop.host.world.inventory().remove(kCharacterA, 0, 1).ok(), "control: A frees a bag slot");
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    note("QRB11 free bag slot: event state=" + std::to_string(questfix::quest_of(*a, "Review_Event")->tasks[0].state) +
         ", items=" + std::to_string(shop.count(kCharacterA, "QbPants")) + ", whispers=" + std::to_string(shop.host.world.quests().counts().whispers));
    check(shop.host.world.quests().ready(*questfix::quest_of(*a, "Review_Event")) && shop.count(kCharacterA, "QbPants") == 1 &&
          shop.has_bit(kCharacterA, "Qb_Whisper_Bit"), "the waiting task event must run after the bag has room");
}

void review_trigger_axes(const Options&) {
    gamedata::TriggerData triggers;
    const json q = {{"format", 1}, {"volumes", json::array({{{"tag", "review"}, {"shape", "box"}, {"center", {100, 200, 30}},
        {"axes", {0, 0, 0, 0, 0, 0, 0, 0, 0}}, {"half", {1, 1, 1}}}})}};
    const bool loaded = triggers.load_text(q.dump());
    note("QRB12 zero axes: loaded=" + std::to_string(loaded) + ", distant point inside=" + std::to_string(triggers.inside("review", 9000, 9000, 900)));
    check(!loaded && !triggers.loaded(), "a volume with zero axes must be rejected instead of matching every position");
}


void review_missing_return_items(const Options&) {
    QuestShop shop;
    json q = quest_document_18b();
    q["tasks"]["review_return"] = questfix::task("Review_Return", "taskhaveandreturnitems", 26,
        {{"ItemName", "QbPotion"}, {"TargetQty", "2"}});
    q["objectives"].push_back({{"id", "Review_ReturnQuest"},
        {"cells", {{"Type", "Journey"}, {"NpcDestinationId", "Test_Artificer"}, {"SuccessGameEventId", "Qb_Paid_Success"}}},
        {"tree", json::array({json::array({0, "review_return", json::array()})})}});
    check(shop.host.quests.load_text(q.dump()), "control: a return task without StayActive loads");
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    check(shop.host.world.inventory().give(kCharacterA, "QbPotion", 2).ok(), "control: A has the two return items");
    const auto grant = shop.host.world.quests().grant(*a, "Review_ReturnQuest", "command", 0);
    check(grant.ok() && shop.host.world.quests().ready(*questfix::quest_of(*a, "Review_ReturnQuest")),
          "control: possession completes the return task");
    std::vector<world::ItemInstance> released;
    check(a->bag.slots[0].item && shop.host.world.inventory().release(kCharacterA,
          {world::DropEntry{0, a->bag.slots[0].item->instance, 2}}, released).ok() && released.size() == 1,
          "control: the client discard path accepts both ordinary return items");
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(shop.count(kCharacterA, "QbPotion") == 0, "control: A now has no return item");
    const auto xp = a->progression.experience;
    shop.request(shop.clientA, shop.talk(kArtificer));
    shop.hand_in(kCharacterA, "Review_ReturnQuest");
    note("QRB2 no return items: accepted=" + std::to_string(shop.hand_ins(world::QuestRequestResult::accepted)) +
         ", XP gain=" + std::to_string(a->progression.experience - xp) + ", reward items=" + std::to_string(shop.count(kCharacterA, "QbPants")));
    check(questfix::quest_of(*a, "Review_ReturnQuest") != nullptr && a->progression.experience == xp && shop.count(kCharacterA, "QbPants") == 0,
          "794 must require the current return items before paying or completing the quest");
}

void review_late_store_rollback(const Options& options) {
    const std::string path = (options.workDirectory / "review-late-write.sqlite3").string();
    std::filesystem::remove(path);
    QuestShop shop(0, path);
    json q = quest_document_18b();
    for (auto& o : q["objectives"]) if (o["id"] == "Qb_Paid") {
        o["cells"]["AchievementId"] = "Qb_Start_Done";
        o["cells"]["ObjectiveReward"] = "Qb_Cinema";
    }
    check(shop.host.quests.load_text(q.dump()), "control: the hand-in has items, coin, XP, an achievement, and a next quest");
    shop.enter();
    auto* a = shop.entity(kCharacterA);
    const auto grant = shop.host.world.quests().grant(*a, "Qb_Paid", "command", 0);
    const auto xp = a->progression.experience;
    const auto coin = a->wallet.coin;
    check(grant.ok() && review_sql(path,
        "CREATE TRIGGER review_refuse BEFORE INSERT ON character_progress BEGIN SELECT RAISE(FAIL, 'review'); END"),
        "control: the last owner write of the hand-in fails");
    const auto failed = shop.host.world.quests().hand_in(*a, grant.instance);
    check(failed.error == world::QuestError::storeFailed && questfix::quest_of(*a, "Qb_Paid") != nullptr &&
          questfix::quest_of(*a, "Qb_Cinema") == nullptr && a->progression.experience == xp && a->wallet.coin == coin &&
          shop.host.store.currency(kCharacterA, state::kCurrencyCoin) == coin && shop.count(kCharacterA, "QbPants") == 0 &&
          !shop.has_bit(kCharacterA, "Qb_Start_Done") && a->quests.completed.empty(),
          "a late progress write failure rolls back all earlier owner rows and changes no entity");
    bool storedCurrent = false;
    bool storedNext = false;
    bool storedItem = false;
    for (const auto& row : shop.host.store.list_quests(kCharacterA)) {
        storedCurrent = storedCurrent || row.objective == "Qb_Paid";
        storedNext = storedNext || row.objective == "Qb_Cinema";
    }
    for (const auto& row : shop.host.store.list_items(kCharacterA)) storedItem = storedItem || row.itemId == "QbPants";
    const auto achievements = shop.host.store.list_achievements(kCharacterA);
    check(storedCurrent && !storedNext && !storedItem && shop.host.store.progress(kCharacterA).experience == xp &&
          std::find(achievements.begin(), achievements.end(), "Qb_Start_Done") == achievements.end(),
          "fresh store reads prove rollback of the current quest, next quest, item, XP, and achievement rows");
    check(review_sql(path, "DROP TRIGGER review_refuse"), "control: progress writes work again");
    const auto accepted = shop.host.world.quests().hand_in(*a, grant.instance);
    const auto duplicate = shop.host.world.quests().hand_in(*a, grant.instance);
    check(accepted.ok() && !duplicate.ok() && a->progression.experience == xp + 100 && a->wallet.coin == coin + 100 &&
          shop.count(kCharacterA, "QbPants") == 1 && shop.has_bit(kCharacterA, "Qb_Start_Done") &&
          questfix::quest_of(*a, "Qb_Cinema") != nullptr && a->quests.completed == std::vector<std::uint64_t>{grant.instance},
          "the retry pays all rewards once and grants the next quest once");
}


void review_reward_perks(const Options&) {
    QuestShop shop;
    json q = quest_document_18b();
    q["events"]["qb_paid_success"]["ItemReward"] = "TestDropSword";
    check(shop.host.quests.load_text(q.dump()), "control: the success event gives a sword with perk buckets");
    auto items = quest_items();
    items.push_back(shop::definition("TestDropSword", "Weapon", "Weapon+EquippableMainHand+Sword+Melee", 3, 300, 400));
    shop.host.catalog.set(std::move(items));
    check(shop.host.perks.set(shop::shop_perks()), "control: the shared itemization perk data loads");
    auto progression = test_progression();
    progression.levelCap = 7;
    shop.host.progression.set(std::move(progression));
    shop.enter();
    check(shop.host.world.progress().set_level(kCharacterA, 7).ok(), "control: A reaches the minimum perk-roll level");
    auto* a = shop.entity(kCharacterA);
    const auto preview = shop.host.world.rewards().grant_items({world::LootDrop{"TestDropSword", 1, std::nullopt}}, a->progression.level);
    check(preview.size() == 1 && preview[0].perks != world::PerkIds{}, "control: the common reward generator returns at least one perk");
    const auto grant = shop.host.world.quests().grant(*a, "Qb_Paid", "command", 0);
    const auto result = shop.host.world.quests().hand_in(*a, grant.instance);
    check(result.ok() && shop.count(kCharacterA, "TestDropSword") == 1, "control: the hand-in gives the reward sword");
    world::PerkIds received;
    for (const auto& slot : a->bag.slots) if (slot.item && slot.item->itemId == "TestDropSword") received = slot.item->perks;
    world::PerkIds stored;
    for (const auto& row : shop.host.store.list_items(kCharacterA)) if (row.itemId == "TestDropSword") stored = row.perks;
    note("QRB1 sword perks: entity[0]='" + received[0] + "', stored[0]='" + stored[0] + "'");
    check(received != world::PerkIds{} && stored != world::PerkIds{},
          "the quest inventory plan must preserve the rolled perks in the entity and the stored reward");
}

} // namespace

std::vector<Case> review_quests_18b_cases() {
    return {{"QRB1", "review 1.8b, QR8B-R1: the reward items keep their rolled perks", review_reward_perks},
            {"QRB2", "review 1.8b, QR8B-R2: the hand-in needs the current return items", review_missing_return_items},
            {"QRB3", "review 1.8b, QR8B-R3: no effect of a topic that the character did not select", review_unselected_topic},
            {"QRB4", "review 1.8b, QR8B-R4: a refused topic write can be finished again", review_topic_storage_retry},
            {"QRB5", "review 1.8b, QR8B-R5: two selected topics of one 5397 both finish", review_topic_batch_reselect},
            {"QRB6", "review 1.8b, QR8B-R6: a walk out of the range after the close ends the topic authority", review_closed_range},
            {"QRB7", "review 1.8b, QR8B-R7: an old cutscene end does not complete a replacement instance", review_old_cutscene_end},
            {"QRB8", "review 1.8b, QR8B-R8: a second 6703 keeps the started cutscene", review_duplicate_cutscene_request},
            {"QRB9", "review 1.8b, QR8B-R9: a missing next objective refuses the hand-in", review_missing_next},
            {"QRB10", "review 1.8b, QR8B-R10: a negative event item quantity refuses the hand-in", review_negative_event_quantity},
            {"QRB11", "review 1.8b, QR8B-R11: a task game event runs when the bag has room", review_waiting_event_bag},
            {"QRB12", "review 1.8b, QR8B-R12: a trigger volume with zero axes is refused", review_trigger_axes},
            {"QRBC", "review 1.8b control: a late owner write rolls back, and the retry pays once", review_late_store_rollback}};
}

} // namespace soulwarden::backend::tests
