// Cases of stage 2.5a: the interaction base (world/interactions.h, gamedata/interaction_data.h, server/rep/client_input.h,
// protocol notes 4.35). The objects have the form of the file of tools/gamedata/interactions.py: lore pages with ReadLore (47,
// committed) and AddLoreToJournal (50, one-shot), a person with OpenConversation (63), a chair with one place, a storage shed
// with no handler, a pet with a level requirement, and a lore page with an unresolved name. All keys, ids, positions, and
// achievement bits are made up (the bits 2032 and 3519 have the byte places of the bits of the research).
//
//   IT1   IT-REF: the loader of the file: a correct file, each refusal of a file that is not complete, no file
//   IT2   IT-REF: the reader of the block: the fields, the vectors, the limits of a text and of a flag, a cut body, the
//         inheritance of an older entry
//   IT3   I2: Enabled false hides the object (the record of the managed actor), a request is refused; true restores it
//   IT4   I4: wrong references, options, actions, a far 3D place, no exact position, no data, an unresolved name, a
//         requirement, no handler, a short or a wrong 2473: no change of the world
//   IT5   I3, IT-PERSIST: one read, one row, the bit with its neighbor in byte 254; a second read writes nothing; a new start of
//         the backend and a new world entry have the bit; a database that refuses the row changes nothing; two characters
//   IT6   I5, IT-SEQ: the same request in each report, an old report, the end of the block, a part of a committed
//         interaction and the restored request, a wrap of the input numbers, late and echo cancels, the cancel command
//   IT7   I6, IT-MP: two sessions: owner and observer messages, an unlimited page (users 2, 1, 0) and a chair with one place
//   IT8   I7, IT-LOAD: far objects do not change the work near a character; the time of the lookups and of the requests
//   IT9   I8: the commands of the development routes and of the admin routes: the list, the enable, the cancel
//   IT10  I9: the data tool with made-up sources (check_interactions_tool.py)
//   IT11  the ends: the death, the travel, the leave of a character; a conversation keeps the lock answer (6705, 6709)

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "server/http/admin_access.h"
#include "server/http/request_handler.h"
#include "server/http/router.h"
#include "server/http/routes.h"
#include "server/rep/client_input.h"
#include "server/rep/lock_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/fake_client.h"
#include "support/interaction_fixture.h"
#include "support/server_host.h"
#include "support/state_decoder.h"
#include "support/test_players.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;

/** IT1: the loader of the file of the tool. */
void loader(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "interaction-data.json";
    const json base = {
        {"format", 1},
        {"world", "newworld_vitaeeterna"},
        {"counts", {{"objects", 1}, {"bounds", 1}, {"options", 2}, {"achievements", 1}}},
        {"options",
         json::array({{{"name", "ReadLore"}, {"id", gamedata::interaction_option_id("ReadLore")}, {"action", 47}, {"committed", true},
                       {"delay", 0.0}, {"hold", 0.5}, {"instantCancel", false}, {"cooldown", 0}, {"maxUsage", 0.0}, {"inactive", 0.0},
                       {"requiredLevel", 0}, {"requiredItem", ""}, {"requiredItemCount", 0}, {"requiredCurrency", 0},
                       {"requiredAchievement", -1}, {"statusRules", false}},
                      {{"name", "AddLoreToJournal"}, {"id", gamedata::interaction_option_id("AddLoreToJournal")}, {"action", 50},
                       {"committed", false}, {"delay", 0.0}, {"hold", 0.5}, {"instantCancel", false}, {"cooldown", 0},
                       {"maxUsage", 0.0}, {"inactive", 0.0}, {"requiredLevel", 0}, {"requiredItem", ""}, {"requiredItemCount", 0},
                       {"requiredCurrency", 0}, {"requiredAchievement", -1}, {"statusRules", false}}})},
        {"achievements", json::array({json::array({"Lore_unlock_Test_Page_01", kPageBit})})},
        {"objects",
         json::array({{{"key", "0b1ec700-0000-4000-8000-000000000001"}, {"kind", "lorePage"}, {"slice", "slices/test/page.dynamicslice"},
                       {"asset", "a55e7001000000000000000000000001"}, {"sub", 2}, {"actor", "0b1122334f424a540000000000000000"},
                       {"region", "r_+00_+00"}, {"position", {100.0, 202.0, 30.0}},
                       {"bounds", json::array({{{"slot", 2}, {"component", "c000000000000001"}, {"target", "7100000000000001"},
                                                {"position", {100.0, 202.0, 30.0}}, {"rangeSq", 25.0}, {"capacity", -1},
                                                {"lineOfSight", false}, {"arcRequired", false}, {"arc", 180.0},
                                                {"options", json::array({json::array({0, "544a424f33221100b"}), json::array({1, "544a424f3322110b"})})},
                                                {"lore", 0}, {"requires", json::object()}, {"tag", ""}}})}}})}};
    const auto load = [&](const json& document, gamedata::InteractionData& data) {
        std::ofstream(path) << document.dump();
        return data.load(path.string());
    };
    gamedata::InteractionData data;
    // The option entity of the first option in the file above is not 16 digits: fix it for the base case.
    json good = base;
    good["objects"][0]["bounds"][0]["options"][0][1] = "544a424f3322110b";
    const bool loaded = load(good, data);
    const std::optional<gamedata::BoundRef> ref = data.find(0x544A424F3322110BULL, 0x7100000000000001ULL);
    check(loaded && data.objects().size() == 1 && data.bound_count() == 1 && ref && ref->object == 0 && ref->bound == 0
              && data.objects()[0].group == 0x544A424F3322110BULL && data.achievement_index("lore_unlock_test_page_01") == kPageBit
              && data.find_key("0B1EC700-0000-4000-8000-000000000001") != nullptr,
          "a file of the form of the tool: one object, its G from the actor id (little-endian), the reference map, the key, the bit "
          "of its achievement (" + data.error() + ")");
    check(!load(base, data) && !data.loaded(), "the loader refuses an option entity that is not 16 hexadecimal digits (" + data.error() + ")");
    const auto refused = [&](const char* what, const std::function<void(json&)>& change) {
        json document = good;
        change(document);
        gamedata::InteractionData other;
        check(!load(document, other) && !other.loaded(), std::string("the loader refuses the file: ") + what + " (" + other.error() + ")");
    };
    refused("an option index that the list does not have", [](json& d) { d["objects"][0]["bounds"][0]["options"][0][0] = 5; });
    refused("an achievement index that the list does not have", [](json& d) { d["objects"][0]["bounds"][0]["lore"] = 3; });
    refused("a range that is not above 0", [](json& d) { d["objects"][0]["bounds"][0]["rangeSq"] = 0.0; });
    refused("a position that is not a position of the world", [](json& d) { d["objects"][0]["bounds"][0]["position"][2] = 1e9; });
    refused("a kind that the file does not have", [](json& d) { d["objects"][0]["kind"] = "castle"; });
    refused("a count that is not the size of the list", [](json& d) { d["counts"]["bounds"] = 2; });
    refused("a member with a wrong form", [](json& d) { d["objects"][0]["bounds"][0]["capacity"] = "one"; });
    refused("a different format", [](json& d) { d["format"] = 2; });
    refused("two objects with one key", [](json& d) {
        d["objects"].push_back(d["objects"][0]);
        d["objects"][1]["actor"] = "131122334f424a540000000000000000";
        d["counts"]["objects"] = 2;
        d["counts"]["bounds"] = 2;
    });
    refused("two objects with one actor id", [](json& d) {
        d["objects"].push_back(d["objects"][0]);
        d["objects"][1]["key"] = "0b1ec700-0000-4000-8000-000000000002";
        d["counts"]["objects"] = 2;
        d["counts"]["bounds"] = 2;
    });
    gamedata::InteractionData none;
    check(!none.load((options.workDirectory / "no-such-file.json").string()) && !none.loaded() && none.error() == "the file is not there",
          "no file: an empty catalog, with the cause");
    // The backend starts with no file and refuses each request (IT4 has the reply).
    ServerHost host;
    check(host.world.interactions().data() == nullptr, "a world with no interaction data: the system has no data, and the world works");
}

/** IT2: the reader of the block of an input entry. */
void block_reader(const Options&) {
    gamedata::InteractionData data;
    (void)set_data(data);
    InteractionBlock block = block_for(data, kPage, kReadLore);
    block.text88 = "@00_Test_Title_07_02";
    InputEntry entry = report_entry(5, kHere.x, kHere.y, kHere.z);
    entry.interaction = block;
    auto input = server::rep::read_client_input(input_entry_body(entry));
    const bool parsed = input && input->tail == server::rep::InputTail::read && input->steps.size() == 1 && input->steps[0].interaction
                        && input->hasCharacter;
    const server::rep::InputInteraction* read = parsed ? &*input->steps[0].interaction : nullptr;
    check(read != nullptr && read->actor == block.actor && read->group == block.group && read->target == block.target && read->action == 47
              && read->baseAction == 47 && read->optionEntity == block.optionEntity && read->optionId == gamedata::interaction_option_id("ReadLore")
              && read->committed && read->bytes.size() == 131 + block.text88.size() && input->characterX == kHere.x,
          "a block of the client: the reference, the action, the base action, the option entity, the option id, the committed flag; "
          "the size 131 bytes plus the text; the position of the character after it");
    InteractionBlock vectors = block;
    vectors.vectors = true;
    entry.interaction = vectors;
    input = server::rep::read_client_input(input_entry_body(entry));
    check(input && input->tail == server::rep::InputTail::read && input->steps[0].interaction
              && input->steps[0].interaction->bytes.size() == 155 + block.text88.size() && input->hasCharacter,
          "a block with its two vectors (24 bytes more): the entry is read to its end");
    InteractionBlock badFlag = block;
    badFlag.committedByte = 2;
    entry.interaction = badFlag;
    input = server::rep::read_client_input(input_entry_body(entry));
    check(input && input->tail == server::rep::InputTail::badValue && input->steps.empty() && !input->hasCharacter,
          "a flag that is not 0 or 1: the entry has a value that the client does not send (no step, no position)");
    InteractionBlock longText = block;
    longText.text48 = std::string(server::rep::kMaxInteractionText + 1, 'x');
    entry.interaction = longText;
    input = server::rep::read_client_input(input_entry_body(entry));
    check(input && input->tail == server::rep::InputTail::badValue && input->steps.empty(),
          "a text above the limit (1,024 bytes): no request");
    entry.interaction = block;
    const Bytes full = input_entry_body(entry);
    bool everyCutFails = true;
    // Each cut inside the block: the reader reads no request (the body ends in the block).
    for (std::size_t cut = 1; cut <= 131; ++cut) {
        entry.cut = cut + 25; // the fields after the block are 25 bytes in this entry
        const auto cutInput = server::rep::read_client_input(input_entry_body(entry));
        if (cutInput && !cutInput->steps.empty() && cutInput->steps[0].interaction) {
            everyCutFails = false;
        }
    }
    entry.cut = 0;
    check(everyCutFails && full.size() > 131, "a body cut in the block: no request for each cut length");
    // Inheritance: the newest entry has the block; the older one has no bit 8 and has the block of the newer one; the oldest
    // has the bit with the flag 0 (no request).
    InputEntry newest = report_entry(12, kHere.x, kHere.y, kHere.z);
    newest.interaction = block;
    InputEntry older = report_entry(11, kHere.x, kHere.y, kHere.z);
    InputEntry oldest = report_entry(10, kHere.x, kHere.y, kHere.z);
    oldest.noInteraction = true;
    input = server::rep::read_client_input(input_entries_body({newest, older, oldest}));
    check(input && input->steps.size() == 3 && input->steps[0].interaction && input->steps[1].interaction
              && *input->steps[1].interaction == *input->steps[0].interaction && !input->steps[2].interaction,
          "the inheritance: an older entry with no bit 8 has the block of the entry before it; the bit with the flag 0 is no request");
    InputEntry alone = report_entry(13, kHere.x, kHere.y, kHere.z);
    input = server::rep::read_client_input(input_entry_body(alone));
    check(input && input->steps.size() == 1 && !input->steps[0].interaction,
          "the newest entry with no bit 8: no request (the default entry)");
}

/** IT3: Enabled false hides the object, and a request is refused; true restores it (I2). */
void enabled_state(const Options&) {
    Shop shop;
    shop.enter();
    const gamedata::InteractionObject& page = object_tagged(shop.host.interactions, kPage);
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    check(shop.host.world.interactions().set_enabled(page.key, false) == world::ObjectError::none, "the page is disabled (a command)");
    shop.host.steps(60, {&shop.clientA, &shop.clientB});
    const std::vector<Seen> metadata = fragments_of(shop.clientA, 0, 10, fromA);
    bool actorSeen = false;
    for (const Seen& seen : metadata) {
        const auto value = field_of(seen.fragment, 0, 1);
        actorSeen = actorSeen || (value.size() == 16 && std::equal(value.begin(), value.end(), page.actor.begin()));
    }
    const auto statesA = object_states(shop.clientA, fromA);
    const auto statesB = object_states(shop.clientB, fromB);
    check(actorSeen && statesA.size() == 1 && !statesA[0].first && statesA[0].second == 0 && statesB.size() == 1 && !statesB[0].first,
          "each session near the page gets the record of the page: the metadata state with the managed actor id of the placement, "
          "and slot 2, type 2930, Enabled false (live check L2: the prompt goes away)");
    const std::size_t before = shop.clientA.got.size();
    shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
    const auto refused = replies(shop.clientA, before);
    check(refused.size() == 1 && refused[0] == std::make_pair(47U, 2U) && shop.host.store.list_achievements(kCharacterA).empty()
              && !shop.entity(kCharacterA)->interaction,
          "a request while the page is disabled (a forged client): 3743 result 2, no row, no interaction");
    shop.end_block(shop.clientA);
    const std::size_t again = shop.clientA.got.size();
    check(shop.host.world.interactions().set_enabled(page.key, true) == world::ObjectError::none, "the page is enabled again");
    shop.host.steps(5, {&shop.clientA, &shop.clientB});
    const auto restored = object_states(shop.clientA, again);
    check(restored.size() == 1 && restored[0].first && restored[0].second == 0, "the next state: Enabled true (the prompt comes back)");
    const std::size_t accepted = shop.clientA.got.size();
    shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
    check(replies(shop.clientA, accepted) == std::vector<std::pair<unsigned, unsigned>>{{47U, 1U}}, "the request now: 3743 result 1");
    check(shop.host.world.interactions().set_enabled("0b1ec700-0000-4000-8000-0000000000ff", false) == world::ObjectError::unknownObject,
          "a key that the data does not have: refused");
}

/** IT4: requests that are not correct change nothing (I4). */
void wrong_requests(const Options&) {
    Shop shop;
    shop.enter(false);
    const gamedata::InteractionData& data = shop.host.interactions;
    const InteractionBlock good = block_for(data, kPage, kReadLore);
    const auto try_block = [&](const InteractionBlock& block, const std::string& what, unsigned expectedResult = 2) {
        const std::size_t from = shop.clientA.got.size();
        const world::Revision revision = shop.host.world.revision();
        shop.request(shop.clientA, block);
        const auto got = replies(shop.clientA, from);
        const world::Entity* entity = shop.entity(kCharacterA);
        check(got.size() == 1 && got[0].second == expectedResult && !entity->interaction && shop.host.store.list_achievements(kCharacterA).empty()
                  && shop.host.world.interactions().object_entities() == 0,
              what + ": 3743 result " + std::to_string(expectedResult) + ", no interaction, no row, no object state (revision "
                  + std::to_string(revision) + ")");
        shop.end_block(shop.clientA);
    };
    InteractionBlock block = good;
    block.target ^= 1;
    try_block(block, "a wrong facet target");
    block = good;
    block.group ^= 0x100;
    try_block(block, "a wrong G");
    block = good;
    block.actor[3] ^= 1;
    try_block(block, "a wrong actor id with the right G and target");
    block = good;
    block.optionId ^= 1;
    try_block(block, "a wrong option id");
    block = good;
    block.optionEntity ^= 1;
    try_block(block, "a wrong option entity");
    block = good;
    block.action = 50;
    try_block(block, "the action of a different option");
    try_block(block_for(data, kFarPage, kReadLore), "an object 60 m away (the range of the data is 5 m)");
    try_block(block_for(data, kUnresolvedPage, kReadLore), "a page with an unresolved name of the data");
    try_block(block_for(data, kShed, kStorage), "a storage shed: no handler of its option in this stage");
    try_block(block_for(data, kPetObject, kPet), "a pet whose option needs the level 40 (the character has the level 1)");
    // The 3D distance (research K4): a page 6 m below the floor is 6.3 m away (horizontal 2 m).
    try_block(block_for(data, kLowPage, kReadLore), "a page 6 m below the character (3D distance 6.3 m; horizontal 2 m)");
    // No exact position: a character whose position is an estimate (a world entry with no report yet).
    world::Entity* entity = shop.entity(kCharacterA);
    entity->positionEstimated = true;
    const std::size_t from = shop.clientA.got.size();
    shop.report(shop.clientA, good);
    // The report itself makes the position exact again: the request is checked after the position of the same report.
    shop.host.steps(3, {&shop.clientA});
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 1U}},
          "a report with the position and the request: the position comes first (the request is accepted at the exact position)");
    shop.end_block(shop.clientA);
    // The world rule directly: an estimated position refuses.
    entity = shop.entity(kCharacterA);
    entity->positionEstimated = true;
    world::InteractionRequest direct;
    direct.object = {good.actor, good.group, good.target};
    direct.action = good.action;
    direct.baseAction = good.baseAction;
    direct.optionEntity = good.optionEntity;
    direct.optionId = good.optionId;
    check(shop.host.world.interactions().request(1, direct) == world::InteractionVerdict::noPosition,
          "a character with an estimated position: refused (no exact position)");
    entity->positionEstimated = false;
    // No data: each request is refused.
    Shop empty(false, ":memory:", false);
    empty.enter(false);
    const std::size_t emptyFrom = empty.clientA.got.size();
    empty.request(empty.clientA, good);
    check(replies(empty.clientA, emptyFrom) == std::vector<std::pair<unsigned, unsigned>>{{47U, 2U}}
              && !empty.entity(kCharacterA)->interaction,
          "no interaction data: 3743 result 2, and nothing changes");
    // A short 2473 and a 2473 of a different reference while an interaction is active change nothing.
    shop.request(shop.clientA, good);
    const world::Entity* active = shop.entity(kCharacterA);
    const std::array<std::uint8_t, 16> route = active->actor.bytes;
    const std::uint64_t facet = active->id + server::rep::kInteractorComponent;
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_call(kCancelMessage, route, facet, active->id));
    server::hub::Writer wrong;
    wrong.u64(facet);
    wrong.u64(active->id);
    wrong.raw(good.actor);
    wrong.u64(good.group);
    wrong.u64(good.target ^ 1);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, route, wrong.data()));
    shop.host.steps(3, {&shop.clientA});
    check(shop.entity(kCharacterA)->interaction.has_value() && log_count("is not 48 bytes: no change") == 1
              && log_count("not the interaction of the character, no change") == 1,
          "a 2473 with 16 bytes (no reference) and a 2473 with a different reference: the interaction stays");
    shop.end_block(shop.clientA);
}

/** IT5: the read of a page persists its achievement one time (I3, IT-PERSIST). */
void persistence(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto database = (options.workDirectory / "interaction-persist.sqlite3").string();
    std::filesystem::remove(database);
    {
        Shop shop(true, database);
        // A row of a different achievement of the list before the entry (the neighbor bit 2033 in the same byte 254).
        check(shop.host.store.grant_achievements(kCharacterA, {"Lore_unlock_Test_Page_03"}), "control: A has the neighbor achievement");
        shop.enter();
        const auto first = achievement_bits(shop.clientA);
        check(first && *first == std::vector<std::uint32_t>{kPageNeighborBit},
              "the first record of A: the achievement state with the bit of its row (2033)");
        const std::size_t from = shop.clientA.got.size();
        shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
        const auto bits = achievement_bits(shop.clientA, from);
        check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 1U}} && bits
                  && *bits == std::vector<std::uint32_t>{kPageBit, kPageNeighborBit}
                  && shop.host.store.list_achievements(kCharacterA).size() == 2
                  && has(shop.host.store.list_achievements(kCharacterA), "Lore_unlock_Test_Page_01"),
              "the read: 3743 result 1, one row, and the full vector with the bits 2032 and 2033 (byte 254 = 0x03: the bit of the "
              "page with its neighbor)");
        const std::size_t again = shop.clientA.got.size();
        shop.end_block(shop.clientA);
        shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
        shop.end_block(shop.clientA);
        shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kAddLore));
        check(replies(shop.clientA, again) == std::vector<std::pair<unsigned, unsigned>>({{47U, 1U}, {50U, 1U}})
                  && shop.host.store.list_achievements(kCharacterA).size() == 2 && !achievement_bits(shop.clientA, again),
              "a second read and an Add To Journal of the same page: accepted, no second row, no new achievement state");
        const auto bBits = achievement_bits(shop.clientB);
        check(!bBits && shop.host.store.list_achievements(kCharacterB).empty(),
              "B (a different character near the page): no achievement state, no row (owner only, no state of A)");
        shop.request(shop.clientB, block_for(shop.host.interactions, kSecondPage, kAddLore));
        const auto bAfter = achievement_bits(shop.clientB);
        check(bAfter && *bAfter == std::vector<std::uint32_t>{kSecondPageBit}
                  && shop.host.store.list_achievements(kCharacterB) == std::vector<std::string>{"Lore_unlock_Test_Page_02"}
                  && shop.host.store.list_achievements(kCharacterA).size() == 2,
              "B adds the second page to its journal (one-shot, no committed interaction): its bit 3519 and row only");
        shop.host.disconnect(shop.clientA.peer);
        shop.host.disconnect(shop.clientB.peer);
        shop.host.steps(5, {});
    }
    {
        // A new start of the backend with the same database, and a new world entry.
        Shop restart(true, database, true, true);
        restart.host.enter(restart.clientA, restart.a.registration());
        restart.host.steps(10, {&restart.clientA});
        const auto bits = achievement_bits(restart.clientA);
        check(bits && *bits == std::vector<std::uint32_t>{kPageBit, kPageNeighborBit} && !restart.entity(kCharacterA)->interaction,
              "a new start and a new world entry: the first record has the bit of the page (the journal keeps it) and no active "
              "interaction (no temporary use after a restart)");
    }
    {
        // A database that refuses the row: no change of the entity.
        const auto failing = (options.workDirectory / "interaction-refuse.sqlite3").string();
        std::filesystem::remove(failing);
        Shop shop(true, failing, false);
        shop.enter(false);
        sqlite3* connection = nullptr;
        const bool trigger = sqlite3_open(failing.c_str(), &connection) == SQLITE_OK
                             && sqlite3_exec(connection,
                                             "CREATE TRIGGER refuse_achievement BEFORE INSERT ON achievements BEGIN SELECT RAISE(FAIL, "
                                             "'test write failure'); END",
                                             nullptr, nullptr, nullptr) == SQLITE_OK;
        sqlite3_close(connection);
        const std::size_t from = shop.clientA.got.size();
        shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
        check(trigger && replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 2U}}
                  && shop.entity(kCharacterA)->achievements.empty() && !achievement_bits(shop.clientA, from)
                  && !shop.entity(kCharacterA)->interaction,
              "a database that refuses the row: 3743 result 2, no bit in the entity, no achievement state, no interaction");
    }
}

/** IT6: the sequence of the requests (I5, IT-SEQ). */
void sequence(const Options&) {
    Shop shop;
    shop.enter(false);
    const gamedata::InteractionData& data = shop.host.interactions;
    const InteractionBlock read = block_for(data, kPage, kReadLore);
    std::size_t from = shop.clientA.got.size();
    for (int report = 0; report < 6; ++report) {
        shop.report(shop.clientA, read);
        shop.host.steps(1, {&shop.clientA});
    }
    const world::InteractionCounts counts = shop.host.world.interactions().counts();
    check(replies(shop.clientA, from).size() == 1 && counts.begins == 1 && shop.entity(kCharacterA)->interaction,
          "the same block in six reports: one reply and one begin");
    // An older report (a second datagram of an old input) with no block: no change.
    InputEntry old = report_entry(2, kHere.x, kHere.y, kHere.z);
    shop.host.datagram(shop.clientA.peer, shop.clientA.input_report(old));
    shop.host.steps(2, {&shop.clientA});
    check(shop.entity(kCharacterA)->interaction.has_value(), "an old report with no block: the interaction stays (stale input)");
    // A part of the committed interaction: a one-shot with the base action 47 on the same object.
    InteractionBlock part = block_for(data, kPage, kAddLore);
    part.baseAction = 47;
    from = shop.clientA.got.size();
    shop.request(shop.clientA, part);
    const std::uint64_t serial = shop.entity(kCharacterA)->interaction ? shop.entity(kCharacterA)->interaction->serial : 0;
    // The client restores its committed record: the block of the read again.
    shop.request(shop.clientA, read);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{50U, 1U}} && shop.entity(kCharacterA)->interaction
              && shop.entity(kCharacterA)->interaction->serial == serial,
          "a part of the committed interaction (Add To Journal with the base action 47): accepted; the restored block of the read "
          "gets no reply and starts no new use");
    InteractionBlock wrongPart = block_for(data, kSecondPage, kAddLore);
    wrongPart.baseAction = 47;
    from = shop.clientA.got.size();
    shop.request(shop.clientA, wrongPart);
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{50U, 2U}}
              && !has(shop.host.store.list_achievements(kCharacterA), "Lore_unlock_Test_Page_02"),
          "a part with the base action 47 on a different object: refused, no row");
    shop.request(shop.clientA, read);
    // The block goes away: the interaction ends, and the Interactor state has the empty value.
    from = shop.clientA.got.size();
    shop.end_block(shop.clientA);
    check(!shop.entity(kCharacterA)->interaction && interactor_values(shop.clientA, 1, from) == std::vector<std::uint64_t>{kNoObject},
          "the block goes away (the close of the journal): the interaction ends, and the client gets the empty value");
    // A late 2473 (no interaction): no change.
    const world::Entity* entity = shop.entity(kCharacterA);
    const std::uint64_t facet = entity->id + server::rep::kInteractorComponent;
    server::hub::Writer cancel;
    cancel.u64(facet);
    cancel.u64(entity->id);
    cancel.raw(read.actor);
    cancel.u64(read.group);
    cancel.u64(read.target);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, entity->actor.bytes, cancel.data()));
    shop.host.steps(2, {&shop.clientA});
    check(log_count("not the interaction of the character, no change") >= 1 && shop.host.world.interactions().counts().ends == 1,
          "a late 2473 with no interaction: no change");
    // The cancel command: the client gets 3746, then its echo 2473 changes nothing.
    shop.request(shop.clientA, read);
    from = shop.clientA.got.size();
    check(shop.host.world.interactions().cancel(kCharacterA) && !shop.entity(kCharacterA)->interaction, "the cancel command ends the interaction");
    shop.host.steps(2, {&shop.clientA});
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, entity->actor.bytes, cancel.data()));
    shop.end_block(shop.clientA);
    check(count_type(shop.clientA, kServerCancel, from) == 1 && interactor_values(shop.clientA, 1, from) == std::vector<std::uint64_t>{kNoObject}
              && shop.host.world.interactions().counts().ends == 2,
          "the client gets 3746 and the empty value; its echo 2473 and the end of its block change nothing (one end)");

    // A committed request for a different object replaces the interaction (the old one ends first).
    shop.request(shop.clientA, read);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, block_for(data, kPerson, kConversation));
    const auto values = interactor_values(shop.clientA, 1, from);
    check(shop.entity(kCharacterA)->interaction && shop.entity(kCharacterA)->interaction->object == kPerson - 1U
              && values == std::vector<std::uint64_t>({kNoObject, object_tagged(data, kPerson).group}),
          "a committed request at a different object: the old interaction ends, and the client gets the empty value before the new "
          "G (research K8)");
    shop.end_block(shop.clientA);
    // A wrap of the input numbers: the reports of the entry are 65533 to 65535, and the request is input 0.
    Shop wrap(true, ":memory:", false);
    wrap.sequenceA = 65533;
    wrap.enter(false);
    from = wrap.clientA.got.size();
    wrap.request(wrap.clientA, read);
    check(wrap.sequenceA == 1 && replies(wrap.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 1U}},
          "a wrap of the input numbers (65535 to 0): the request of input 0 is a new request");
}

/** IT7: two sessions (I6, MP-10, IT-MP). */
void two_sessions(const Options&) {
    Shop shop;
    shop.enter();
    const gamedata::InteractionData& data = shop.host.interactions;
    const gamedata::InteractionObject& page = object_tagged(data, kPage);
    std::size_t fromA = shop.clientA.got.size();
    std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, block_for(data, kPage, kReadLore));
    shop.host.steps(60, {&shop.clientA, &shop.clientB});
    const std::uint32_t aInB = shop.host.session(2).observer().known().at(shop.entity(kCharacterA)->id).interest;
    check(replies(shop.clientA, fromA).size() == 1 && replies(shop.clientB, fromB).empty() && count_type(shop.clientB, kReply, fromB) == 0,
          "A reads the page: A gets 3743, B gets no reply (owner only)");
    check(interactor_values(shop.clientA, 1, fromA) == std::vector<std::uint64_t>{page.group}
              && interactor_values(shop.clientB, aInB, fromB) == std::vector<std::uint64_t>{page.group},
          "the Interactor state of A (slot 26) has the G of the page: in the own record of A and in the record of A for B");
    const auto statesA = object_states(shop.clientA, fromA);
    const auto statesB = object_states(shop.clientB, fromB);
    check(!statesA.empty() && statesA.back() == std::make_pair(true, 1U) && !statesB.empty() && statesB.back() == std::make_pair(true, 1U)
              && shop.host.world.interactions().object_entities() == 1,
          "each session gets the record of the page with HasInteractors 1 (one object entity for all observers)");
    fromA = shop.clientA.got.size();
    shop.request(shop.clientB, block_for(data, kPage, kReadLore));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(object_states(shop.clientA, fromA).back() == std::make_pair(true, 2U), "B reads the same page (no limit): users 2");
    fromA = shop.clientA.got.size();
    shop.end_block(shop.clientA);
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    const auto one = object_states(shop.clientA, fromA);
    fromA = shop.clientA.got.size();
    shop.end_block(shop.clientB);
    shop.host.steps(2, {&shop.clientA, &shop.clientB});
    const auto zero = object_states(shop.clientA, fromA);
    check(!one.empty() && one.back() == std::make_pair(true, 1U) && !zero.empty() && zero.back() == std::make_pair(true, 0U),
          "A ends (users 1), B ends (users 0)");
    // The chair: one place.
    fromA = shop.clientA.got.size();
    fromB = shop.clientB.got.size();
    shop.request(shop.clientA, block_for(data, kChairObject, kChair));
    shop.request(shop.clientB, block_for(data, kChairObject, kChair));
    check(replies(shop.clientA, fromA) == std::vector<std::pair<unsigned, unsigned>>{{64U, 1U}}
              && replies(shop.clientB, fromB) == std::vector<std::pair<unsigned, unsigned>>{{64U, 2U}}
              && !shop.entity(kCharacterB)->interaction,
          "the chair (one place): A sits, B is refused (the object has its number of users)");
    shop.end_block(shop.clientB);
    shop.end_block(shop.clientA);
    fromB = shop.clientB.got.size();
    shop.request(shop.clientB, block_for(data, kChairObject, kChair));
    check(replies(shop.clientB, fromB) == std::vector<std::pair<unsigned, unsigned>>{{64U, 1U}}, "A stands up: B sits");
    std::size_t observedAchievements = 0;
    for (const Seen& seen : fragments_of(shop.clientB, kAchievementSlot, kAchievementType)) {
        observedAchievements += seen.interest != 1 ? 1 : 0;
    }
    const auto bBits = achievement_bits(shop.clientB);
    check(observedAchievements == 0 && bBits && *bBits == std::vector<std::uint32_t>{kPageBit}
              && has(shop.host.store.list_achievements(kCharacterA), "Lore_unlock_Test_Page_01")
              && shop.host.store.list_achievements(kCharacterB) == std::vector<std::string>{"Lore_unlock_Test_Page_01"},
          "each character has its own row of the page and its own bit; B got no achievement state in the record of A (owner only)");
    shop.end_block(shop.clientB);
}

/** IT8: the load (I7, IT-LOAD). */
void load(const Options&) {
    const auto with_far = [](std::size_t far) {
        DataParts parts = data_parts();
        for (std::size_t index = 0; index < far; ++index) {
            const float x = 1000.0F + static_cast<float>(index % 300) * 60.0F;
            const float y = 1000.0F + static_cast<float>(index / 300) * 60.0F;
            gamedata::InteractionObject object = object_of(9, InteractionKind::gathering, {x, y, 30.0F}, {kReadLore});
            char key[40];
            std::snprintf(key, sizeof(key), "0b1ec700-0000-4000-8000-%012zx", 0x1000 + index);
            object.key = key;
            object.actor[1] = static_cast<std::uint8_t>(index);
            object.actor[2] = static_cast<std::uint8_t>(index >> 8);
            object.actor[3] = static_cast<std::uint8_t>(index >> 16);
            object.group = group_of(object.actor);
            object.bounds[0].target = 0x7200000000000000ULL + index;
            parts.objects.push_back(std::move(object));
        }
        auto data = std::make_unique<gamedata::InteractionData>();
        if (!data->set(std::move(parts.options), std::move(parts.achievements), std::move(parts.objects))) {
            throw std::runtime_error("interaction load test: " + data->error());
        }
        return data;
    };
    const auto small = with_far(1000);
    const auto large = with_far(90000);
    gamedata::LookupWork smallWork;
    gamedata::LookupWork largeWork;
    std::size_t smallFound = 0;
    std::size_t largeFound = 0;
    small->for_each_near(kHere, 30.0F, [&](const gamedata::BoundRef&) { ++smallFound; }, &smallWork);
    large->for_each_near(kHere, 30.0F, [&](const gamedata::BoundRef&) { ++largeFound; }, &largeWork);
    check(smallFound == largeFound && smallFound == 8 && smallWork.cells == largeWork.cells && smallWork.bounds == largeWork.bounds,
          "1,000 and 90,000 far objects: the lookup near the character finds the 8 objects in 30 m with the same work (" +
              std::to_string(largeWork.cells) + " cells, " + std::to_string(largeWork.bounds) + " bounds)");
    const auto start = std::chrono::steady_clock::now();
    std::size_t hits = 0;
    std::size_t misses = 0;
    const std::uint64_t pageGroup = group_of(actor_of(kPage));
    for (std::size_t round = 0; round < 200000; ++round) {
        hits += large->find(pageGroup, 0x7100000000000001ULL) ? 1 : 0;
        misses += large->find(pageGroup ^ round, 0x7300000000000000ULL + round) ? 0 : 1;
    }
    const auto lookups = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    check(hits == 200000 && misses == 200000 && lookups.count() < 300,
          "400,000 reference lookups (each hit and each miss) in a catalog of 90,008 objects: " + std::to_string(lookups.count())
              + " ms (limit 300 ms)");
    // Requests through a session with the large catalog: the time of 300 begins and ends.
    Shop shop(false, ":memory:", false);
    shop.host.interactions = std::move(*large);
    shop.enter(false);
    const auto requests = std::chrono::steady_clock::now();
    for (int round = 0; round < 150; ++round) {
        shop.report(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
        shop.host.steps(1, {&shop.clientA});
        shop.report(shop.clientA, std::nullopt);
        shop.host.steps(1, {&shop.clientA});
    }
    const auto spent = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - requests);
    check(shop.host.world.interactions().counts().begins == 150 && shop.host.world.interactions().counts().ends == 150
              && shop.host.world.interactions().object_entities() == 1 && spent.count() < 2000,
          "150 reads and 150 ends with the large catalog: one object entity, " + std::to_string(spent.count()) +
              " ms for 300 reports and 300 ticks (limit 2 s)");
}

/** IT9: the commands (I8). */
void commands(const Options&) {
    Shop shop;
    shop.enter(false);
    server::http::Router router;
    const server::http::Services services{shop.host.store, "127.0.0.1:32210", "test-version", "", shop.host.tables, shop.host.world};
    server::http::register_dev_routes(router, services);
    shop.host.tick();
    const auto send = [&](const char* method, const std::string& path, const std::string& query) {
        service::HttpRequest request;
        request.method = method;
        request.path = path;
        request.query = query;
        request.target = path + "?" + query;
        return server::http::handle_request(router, request);
    };
    const gamedata::InteractionObject& page = object_tagged(shop.host.interactions, kPage);
    const service::HttpResponse list = send("GET", "/v1/dev/interactions", std::string("character=") + kCharacterA + "&radius=10");
    const json listed = json::parse(list.body, nullptr, false);
    check(list.status == 200 && listed["objects"].size() == 8 && listed["objects"][0]["key"] == object_tagged(shop.host.interactions, kShed).key
              && listed["active"].is_null(),
          "the list: the 8 objects in 10 m, the nearest first (the shed, 1.4 m), and no active interaction");
    const service::HttpResponse off = send("POST", "/v1/dev/interactions/object",
                                           std::string("character=") + kCharacterA + "&object=" + page.key + "&enabled=0");
    shop.host.steps(60, {&shop.clientA});
    const json after = json::parse(send("GET", "/v1/dev/interactions", std::string("character=") + kCharacterA).body, nullptr, false);
    bool pageOff = false;
    for (const json& entry : after["objects"]) {
        pageOff = pageOff || (entry["key"] == page.key && entry["bounds"][0]["enabled"] == false && !entry["entity"].get<std::string>().empty());
    }
    check(off.status == 200 && pageOff && !object_states(shop.clientA).empty() && !object_states(shop.clientA).back().first,
          "the enable command (development route): the page is off in the list and in the record of the client");
    (void)send("POST", "/v1/dev/interactions/object", std::string("character=") + kCharacterA + "&object=" + page.key + "&enabled=1");
    shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
    const json active = json::parse(send("GET", "/v1/dev/interactions", std::string("character=") + kCharacterA).body, nullptr, false);
    const std::size_t from = shop.clientA.got.size();
    const service::HttpResponse cancel = send("POST", "/v1/dev/interactions/cancel", std::string("character=") + kCharacterA);
    shop.host.steps(2, {&shop.clientA});
    const service::HttpResponse again = send("POST", "/v1/dev/interactions/cancel", std::string("character=") + kCharacterA);
    check(active["active"]["option"] == "ReadLore" && active["active"]["object"] == page.key && cancel.status == 200
              && count_type(shop.clientA, kServerCancel, from) == 1 && again.status == 404,
          "the list has the active read; the cancel command: 200, the client gets 3746; a second cancel: 404");
    // The admin interface: the same commands, for a character of the account of the token, near the object.
    server::http::Router admin;
    server::http::AdminAccess access;
    check(access.add("token-of-the-interaction-test-a-0123456789abcdef", kSteamIdA), "control: the admin token of account A");
    server::http::register_admin_routes(admin, services, access);
    const auto command = [&](const char* name, const json& arguments) {
        service::HttpRequest request;
        request.method = "POST";
        request.path = "/admin/v1/commands";
        request.target = request.path;
        request.body = json{{"command", name}, {"character", kCharacterA}, {"args", arguments}}.dump();
        request.headers.emplace_back(server::http::kAdminTokenHeader, "token-of-the-interaction-test-a-0123456789abcdef");
        return server::http::handle_request(admin, request);
    };
    const service::HttpResponse near = command("set_object_enabled", {{"object", page.key}, {"enabled", 0}});
    const service::HttpResponse far =
        command("set_object_enabled", {{"object", object_tagged(shop.host.interactions, kFarPage).key}, {"enabled", 0}});
    shop.request(shop.clientA, block_for(shop.host.interactions, kSecondPage, kReadLore));
    const service::HttpResponse adminCancel = command("cancel_interaction", json::object());
    service::HttpRequest state;
    state.method = "GET";
    state.path = "/admin/v1/state";
    state.target = state.path;
    state.headers.emplace_back(server::http::kAdminTokenHeader, "token-of-the-interaction-test-a-0123456789abcdef");
    const json stateReply = json::parse(server::http::handle_request(admin, state).body, nullptr, false);
    check(near.status == 200 && far.status == 404 && adminCancel.status == 200 && stateReply.contains("interactions")
              && stateReply["interactions"]["objects"].size() == 8,
          "the admin commands: the enable of an object near the character (200), of an object 60 m away (404: not near), the cancel "
          "(200); the state has the list of the interactions");
    shop.end_block(shop.clientA);
}

/** IT10: the data tool with made-up sources (I9). */
void data_tool(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata/check_interactions_tool.py";
    const std::filesystem::path output = options.workDirectory / "check-interactions.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + script.lexically_normal().string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::size_t ok = 0;
    std::size_t fail = 0;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 150));
        ok += line.rfind("ok", 0) == 0 ? 1 : 0;
        fail += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    std::filesystem::remove(output);
    check(status == 0 && fail == 0 && ok >= 8, "check_interactions_tool.py: each case passes (" + std::to_string(ok) + " cases)");
}

/** IT11: the ends of an interaction, and the lock of a conversation. */
void ends(const Options&) {
    Shop shop;
    shop.enter();
    const gamedata::InteractionData& data = shop.host.interactions;
    // A conversation: accepted; the end of the cutscene (6705) gets 6709 as before; 2473 ends the interaction.
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, block_for(data, kPerson, kConversation));
    const world::Entity* entity = shop.entity(kCharacterA);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_call(server::rep::kTypeCutsceneEnded, entity->actor.bytes,
                                                                      entity->id + server::rep::kPlayerCutsceneComponent, entity->id));
    const InteractionBlock talk = block_for(data, kPerson, kConversation);
    server::hub::Writer cancel;
    cancel.u64(entity->id + server::rep::kInteractorComponent);
    cancel.u64(entity->id);
    cancel.raw(talk.actor);
    cancel.u64(talk.group);
    cancel.u64(talk.target);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, entity->actor.bytes, cancel.data()));
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{63U, 1U}}
              && count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1 && !shop.entity(kCharacterA)->interaction,
          "a conversation: 3743 result 1 (a refusal closes it: live check L1), 6705 gets 6709, and its 2473 ends the interaction");
    shop.end_block(shop.clientA);
    // The death: the interaction ends, and the client gets 3746.
    shop.request(shop.clientA, block_for(data, kPage, kReadLore));
    from = shop.clientA.got.size();
    (void)shop.host.world.health().hurt(kCharacterA, 1.0e9F);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) >= 1,
          "the death of the character: the interaction ends, and the client gets 3746");
    // A dead character: the request is refused.
    shop.end_block(shop.clientA);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, block_for(data, kPage, kReadLore));
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 2U}} && !shop.entity(kCharacterA)->interaction,
          "a request of a dead character: 3743 result 2, no interaction");
    (void)shop.host.world.health().heal(kCharacterA);
    shop.end_block(shop.clientA);
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    // The travel: the interaction ends (the session sends 3746 and 6709 itself).
    shop.request(shop.clientA, block_for(data, kPage, kReadLore));
    from = shop.clientA.got.size();
    (void)shop.host.world.travel(kCharacterA, state::WorldPosition{100.0F, 205.0F, 30.0F});
    shop.host.steps(3, {&shop.clientA, &shop.clientB});
    check(!shop.entity(kCharacterA)->interaction && count_type(shop.clientA, kServerCancel, from) == 1
              && count_type(shop.clientA, server::rep::kTypeCutsceneEndFinalized, from) == 1,
          "a travel: the interaction ends; the client gets 3746 and 6709 one time each (the lock rule of the travel)");
    // The leave: B uses the page, B leaves, the users of the page go to 0.
    shop.request(shop.clientB, block_for(data, kPage, kReadLore));
    from = shop.clientA.got.size();
    shop.host.disconnect(shop.clientB.peer);
    shop.host.steps(60, {&shop.clientA});
    const auto states = object_states(shop.clientA, from);
    check(shop.host.world.interactions().active_count() == 0 && !states.empty() && states.back() == std::make_pair(true, 0U),
          "B leaves the world during its read: its use ends, and A gets the page with 0 users");
}

} // namespace

std::vector<Case> interaction_cases() {
    return {{"IT1", "interactions (2.5a, IT-REF): the loader of the file of the tool, its refusals, no file", loader},
            {"IT2", "interactions (2.5a, IT-REF): the reader of the block: fields, vectors, limits, a cut body, the inheritance", block_reader},
            {"IT3", "interactions (2.5a, I2): Enabled false hides the object and refuses a request; true restores it", enabled_state},
            {"IT4", "interactions (2.5a, I4): wrong references, options, places, data, requirements, and cancels change nothing", wrong_requests},
            {"IT5", "interactions (2.5a, I3, IT-PERSIST): one read, one row, the bit with its neighbor; a restart; a refused row", persistence},
            {"IT6", "interactions (2.5a, I5, IT-SEQ): one reply for a request, a part and its restore, a wrap, late and echo cancels", sequence},
            {"IT7", "interactions (2.5a, I6, IT-MP): two sessions: owner and observer messages, an unlimited page, a chair of one", two_sessions},
            {"IT8", "interactions (2.5a, I7, IT-LOAD): far objects do not change the work near a character; time limits", load},
            {"IT9", "interactions (2.5a, I8): the list, the enable, and the cancel of the development and the admin routes", commands},
            {"IT10", "interactions (2.5a, I9): the data tool with made-up sources (check_interactions_tool.py)", data_tool},
            {"IT11", "interactions (2.5a): the ends at a death, a travel, a leave; a conversation keeps its lock answer", ends}};
}

} // namespace soulwarden::backend::tests
