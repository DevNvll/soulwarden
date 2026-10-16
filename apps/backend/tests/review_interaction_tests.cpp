// Cases of the corrections of the code review of stage 2.5a (findings INT-R1 to INT-R6), in stage 2.5b. Each case fails on the tree
// of stage 2.5a (commit e055c4c) and passes with the correction. They use the fixture of the interaction cases
// (support/interaction_fixture.h): made-up ids, the form of the game data.
//
//   IR1  INT-R1: a committed request whose handler refuses (the database refuses the lore row) leaves the committed use before it as
//        it was: its users, its Interactor state, its reservation
//   IR2  INT-R2: after a matched 2473, a new input step with the same block (no report of an absent block between) is a new request
//   IR3  INT-R3: a client near a bound gets the state of its object when the root of the placement is more than 100 m away
//   IR4  INT-R4: the loader refuses a file with two achievements of one bit, an achievement with no id, or a bit above the limit
//   IR5  INT-R4 to INT-R6: the data tool stops for a layer with no list Capitals, a named slice with an asset id of a wrong form, one
//        placement id with two different placements, and two referenced achievements of one bit (check_interactions_tool.py --review)

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include "server/rep/lock_messages.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/interaction_fixture.h"

namespace soulwarden::backend::tests {
namespace {

using namespace interaction;

/** IR1: a refused replacement changes nothing (finding INT-R1). */
void refused_replacement(const Options& options) {
    const auto database = (options.workDirectory / "review-25a-replacement.sqlite3").string();
    std::filesystem::create_directories(options.workDirectory);
    std::filesystem::remove(database);
    Shop shop(true, database, true);
    shop.enter();
    shop.request(shop.clientA, block_for(shop.host.interactions, kPerson, kConversation));
    world::Entity* entity = shop.entity(kCharacterA);
    check(entity->interaction.has_value(), "control: the conversation is the committed use of A");
    const std::uint64_t serial = entity->interaction ? entity->interaction->serial : 0;
    const std::uint64_t ends = shop.host.world.interactions().counts().ends;
    sqlite3* connection = nullptr;
    const bool trigger = sqlite3_open(database.c_str(), &connection) == SQLITE_OK
                         && sqlite3_exec(connection, "CREATE TRIGGER review_refuse BEFORE INSERT ON achievements BEGIN SELECT RAISE(FAIL, 'refused'); END",
                                         nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    check(trigger, "control: the database refuses the next achievement row");
    const std::size_t from = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, block_for(shop.host.interactions, kPage, kReadLore));
    check(replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 2U}} && shop.host.store.list_achievements(kCharacterA).empty()
              && entity->achievements.empty(),
          "control: the read of the page is refused (3743 result 2), with no row and no bit");
    check(entity->interaction && entity->interaction->serial == serial && shop.host.world.interactions().counts().ends == ends
              && shop.host.world.interactions().active_count() == 1,
          "the conversation stays the committed use of A: no end, the same number of the use");
    // The observer B gets no empty Interactor value for A, and the person keeps its user.
    const auto view = shop.host.world.interactions().view(kCharacterA, 10.0F);
    const auto person = std::find_if(view.objects.begin(), view.objects.end(),
                                     [&](const auto& item) { return item.object->key == object_tagged(shop.host.interactions, kPerson).key; });
    bool emptyForB = false;
    for (const Seen& seen : fragments_of(shop.clientB, kInteractorSlot, kInteractorType, fromB)) {
        emptyForB = emptyForB || (seen.interest != 1 && u64_of(field_of(seen.fragment, 0, 0)) == kNoObject);
    }
    check(person != view.objects.end() && person->bounds[0].users == 1 && !emptyForB,
          "the person keeps A as its user, and the observer B gets no end of the use of A");
}

/** IR2: a new press of the same object after a matched 2473 (finding INT-R2). */
void retry_after_cancel(const Options&) {
    Shop shop;
    shop.enter(false);
    const InteractionBlock read = block_for(shop.host.interactions, kPage, kReadLore);
    shop.request(shop.clientA, read);
    world::Entity* entity = shop.entity(kCharacterA);
    check(entity->interaction.has_value(), "control: the read is active");
    server::hub::Writer cancel;
    cancel.u64(entity->id + server::rep::kInteractorComponent);
    cancel.u64(entity->id);
    cancel.raw(read.actor);
    cancel.u64(read.group);
    cancel.u64(read.target);
    shop.host.datagram(shop.clientA.peer, shop.clientA.component_message(kCancelMessage, entity->actor.bytes, cancel.data()));
    shop.host.steps(2, {&shop.clientA});
    check(!entity->interaction && shop.host.world.interactions().counts().ends == 1, "control: the matched 2473 ends the read");
    // The next input step has the same block: the absent interval of the client fell between two reports.
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, read);
    check(entity->interaction.has_value() && replies(shop.clientA, from) == std::vector<std::pair<unsigned, unsigned>>{{47U, 1U}},
          "the same block in a new input step after the matched 2473 is a new request: 3743 result 1 and a committed use");
    // The same block again in the next steps is the same request: no second reply.
    const std::size_t again = shop.clientA.got.size();
    shop.request(shop.clientA, read);
    check(replies(shop.clientA, again).empty() && shop.host.world.interactions().counts().begins == 2,
          "the block of the next step is the same request: no reply, no second begin");
}

/** IR3: a near bound of an object whose root is far (finding INT-R3). */
void near_bound_far_root(const Options&) {
    Shop shop(false, ":memory:", false);
    DataParts parts = data_parts();
    parts.objects[kPage - 1].position.x += 200.0F; // The root 200 m to the east; the bound stays 2 m from the character.
    check(shop.host.interactions.set(std::move(parts.options), std::move(parts.achievements), std::move(parts.objects)),
          "control: the page has its bound near the character and its root 200 m away");
    shop.enter(false);
    const gamedata::InteractionObject& page = object_tagged(shop.host.interactions, kPage);
    const std::size_t from = shop.clientA.got.size();
    check(shop.host.world.interactions().set_enabled(page.key, false) == world::ObjectError::none, "control: the page is disabled");
    shop.host.steps(120, {&shop.clientA});
    const auto states = object_states(shop.clientA, from);
    check(!states.empty() && !states.back().first, "the client near the bound gets the state 2930 with Enabled false");
}

/** A document of the form of the tool (format 1) with the objects of the fixture. */
nlohmann::json document_of(const DataParts& parts) {
    using nlohmann::json;
    const auto hex16 = [](const std::array<std::uint8_t, 16>& bytes) {
        std::string text;
        for (const std::uint8_t byte : bytes) {
            char digits[3];
            std::snprintf(digits, sizeof(digits), "%02x", byte);
            text += digits;
        }
        return text;
    };
    const auto hex8 = [](std::uint64_t value) {
        char digits[17];
        std::snprintf(digits, sizeof(digits), "%016llx", static_cast<unsigned long long>(value));
        return std::string(digits);
    };
    json options = json::array();
    for (const gamedata::InteractionOption& row : parts.options) {
        options.push_back({{"name", row.name}, {"id", row.id}, {"action", row.action}, {"committed", row.committed}, {"delay", row.delay},
                           {"hold", row.hold}, {"instantCancel", row.instantCancel}, {"cooldown", row.cooldown}, {"maxUsage", row.maxUsage},
                           {"inactive", row.inactive}, {"requiredLevel", row.requiredLevel}, {"requiredItem", row.requiredItem},
                           {"requiredItemCount", row.requiredItemCount}, {"requiredCurrency", row.requiredCurrency},
                           {"requiredAchievement", row.requiredAchievement}, {"statusRules", row.statusRules}});
    }
    json achievements = json::array();
    for (const gamedata::InteractionAchievement& row : parts.achievements) {
        achievements.push_back(json::array({row.id, row.index}));
    }
    json objects = json::array();
    std::size_t bounds = 0;
    for (const gamedata::InteractionObject& object : parts.objects) {
        json list = json::array();
        for (const gamedata::InteractionBound& bound : object.bounds) {
            json items = json::array();
            for (const gamedata::BoundOption& option : bound.options) {
                items.push_back(json::array({option.option, hex8(option.entity)}));
            }
            json needs = json::object();
            if (!bound.unresolved.empty()) {
                needs["unresolved"] = bound.unresolved;
            }
            list.push_back({{"slot", bound.slot}, {"component", hex8(bound.component)}, {"target", hex8(bound.target)},
                            {"position", {bound.position.x, bound.position.y, bound.position.z}}, {"rangeSq", bound.rangeSq},
                            {"capacity", bound.capacity}, {"lineOfSight", bound.lineOfSight}, {"arcRequired", bound.arcRequired},
                            {"arc", bound.arc}, {"lore", bound.lore}, {"options", items}, {"requires", needs}});
            ++bounds;
        }
        objects.push_back({{"key", object.key}, {"kind", gamedata::interaction_kind_text(object.kind)}, {"slice", object.slice},
                           {"asset", hex16(object.asset)}, {"sub", object.sub}, {"actor", hex16(object.actor)}, {"region", object.region},
                           {"position", {object.position.x, object.position.y, object.position.z}}, {"bounds", list}});
    }
    return {{"format", 1}, {"options", options}, {"achievements", achievements}, {"objects", objects},
            {"counts", {{"objects", objects.size()}, {"bounds", bounds}, {"options", options.size()}, {"achievements", achievements.size()}}}};
}

/** IR4: the achievements of the file: one id and one bit for each (finding INT-R4). */
void achievement_bits_of_file(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const auto path = options.workDirectory / "review-25a-achievements.json";
    const auto load = [&](const nlohmann::json& document) {
        std::ofstream(path) << document.dump();
        gamedata::InteractionData data;
        return data.load(path.string());
    };
    check(load(document_of(data_parts())), "control: the complete file of the fixture loads");
    nlohmann::json shared = document_of(data_parts());
    shared["achievements"][1][1] = shared["achievements"][0][1];
    check(!load(shared), "a file with two achievements of one bit (two pages, one journal bit) is refused as a whole");
    nlohmann::json noId = document_of(data_parts());
    noId["achievements"][2][0] = "";
    check(!load(noId), "a file with an achievement with no id is refused");
    nlohmann::json above = document_of(data_parts());
    above["achievements"][2][1] = 0x2000000U * 8U;
    check(!load(above), "a file with an achievement bit above the limit of the achievement state is refused");
    std::filesystem::remove(path);
}

/** IR5: the source rules of the data tool (findings INT-R4 to INT-R6). */
void tool_sources(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    const std::filesystem::path script = std::filesystem::path(__FILE__).parent_path() / "../../../tools/gamedata/check_interactions_tool.py";
    const std::filesystem::path output = options.workDirectory / "check-interactions-review.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr ? python : "python3") + " -B \"" + script.lexically_normal().string()
                                + "\" --review > \"" + output.string() + "\" 2>&1";
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
    check(status == 0 && fail == 0 && ok >= 5, "check_interactions_tool.py --review: each case passes (" + std::to_string(ok) + " cases)");
}

} // namespace

std::vector<Case> review_interaction_cases() {
    return {{"IR1", "review of 2.5a, INT-R1: a refused committed request leaves the committed use before it", refused_replacement},
            {"IR2", "review of 2.5a, INT-R2: the same block in a new step after a matched 2473 is a new request", retry_after_cancel},
            {"IR3", "review of 2.5a, INT-R3: a client near a bound gets the state of an object with a far root", near_bound_far_root},
            {"IR4", "review of 2.5a, INT-R4: the loader refuses two achievements of one bit, no id, a bit above the limit", achievement_bits_of_file},
            {"IR5", "review of 2.5a, INT-R4 to R6: the data tool stops for incomplete or conflicting sources", tool_sources}};
}

} // namespace soulwarden::backend::tests
