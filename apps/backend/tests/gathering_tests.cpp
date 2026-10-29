// The cases of stage 3.6a: gathering (docs/systems/gathering.md, protocol notes 4.44). Made-up accounts, ids, names, and data only.
//
//   GAT1  the data: no data (each gather is refused, the backend works), a document that is not complete, the records, a region file
//         that is not complete, the forms of a G
//   GAT2  the identity and the states: the G of live check L2, the XOR rule of a request, the node state (type 12) of each observer,
//         the Gathering state (81) of the owner and its counters
//   GAT3  the tool, the rank, and the range: each refusal changes nothing; the paperdoll slot, the speed of the tool, the rank part
//   GAT4  the cancel (a release, a walk out of the range, damage), the rest of a work, the reset of a work, a replay, no tool durability loss
//   GAT5  the loot: the graph of the root, a tie of a rare branch, the luck of the rank and of a status (ROL, EFF), the bonus kinds, the
//         trade skill experience
//   GAT6  the transaction: a refused write, a full bag (the kept roll), an item that the catalog does not have, a roll of three stacks
//   GAT7  the restart: the remaining respawn time (the run clock), a work at the stop, late observers, the same node back, old rows, no
//         new draw at a change of interest
//   GAT8  two sessions at one node: both begin, the first completion wins, the depletion and the return for each observer, private rewards
//   GAT9  the load: 2.5 million placements, 50 characters, 1,000 updates, the due list, a time limit, the same work with more far placements
//   GAT10 the skinning: a creature that lives, the body, its observers, the removal of the body (also in a work), a new creature

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "server/http/character_commands.h"
#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"

namespace soulwarden::backend::tests {
namespace gat {

using namespace gather;
using namespace std::chrono_literals;
using chests::bag_count;
using chests::gather_states;
using world::GatherRefusal;

constexpr std::uint32_t kRefused = 2;
constexpr std::uint32_t kAccepted = 1;

std::uint64_t refusals(GatherShop& shop, GatherRefusal refusal) {
    return shop.gathering().counts().refusals[static_cast<std::size_t>(refusal)];
}

/** @return The result of the last reply 3743 of a client from a message index on (0: none). */
unsigned last_reply(const FakeClient& client, std::size_t from) {
    const auto found = replies(client, from);
    return found.empty() ? 0 : found.back().second;
}

/** One request and the end of its block. @return The result of the reply. */
unsigned try_gather(GatherShop& shop, FakeClient& client, const InteractionBlock& block) {
    const std::size_t from = client.got.size();
    shop.request(client, block);
    shop.end_block(client);
    return last_reply(client, from);
}

bool run_sql(const std::filesystem::path& path, const std::string& sql) {
    sqlite3* connection = nullptr;
    const bool opened = sqlite3_open(path.string().c_str(), &connection) == SQLITE_OK;
    const bool ok = opened && sqlite3_exec(connection, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK;
    sqlite3_close(connection);
    return ok;
}

/** Writes a region file (the form of tools/gamedata/gathering.py): "SWGN", the format, x, y, the records, the tails, 14 bytes a record. */
void write_region(const std::filesystem::path& path, std::int32_t x, std::int32_t y, const std::vector<gamedata::NaturalRecord>& records,
                  std::uint32_t tails, const char* magic = "SWGN", std::uint32_t format = 1, std::size_t cut = 0) {
    std::string data(magic, 4);
    const auto put = [&](const void* value, std::size_t size) { data.append(static_cast<const char*>(value), size); };
    const std::uint32_t count = static_cast<std::uint32_t>(records.size());
    put(&format, 4);
    put(&x, 4);
    put(&y, 4);
    put(&count, 4);
    put(&tails, 4);
    for (const gamedata::NaturalRecord& record : records) {
        put(&record.type, 2);
        put(&record.x, 4);
        put(&record.y, 4);
        put(&record.z, 4);
    }
    data.resize(data.size() - std::min(cut, data.size()));
    std::ofstream(path, std::ios::binary) << data;
}

/** GAT1: the data rules. */
void data_rules(const Options& options) {
    // No gathering data (a game data folder of an earlier stage: the chest data, no gathering data): each gather is refused, the world
    // works.
    {
        GatherShop shop(":memory:", false, false);
        check(chests::set_chest_data(shop.host.chests), "setup: the chest data of an earlier stage");
        shop.enter();
        check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: A has the pick in slot 36");
        check(try_gather(shop, shop.clientA, shop.capital_block()) == kRefused && refusals(shop, GatherRefusal::noData) == 1,
              "no gathering data: the gather of a capital node is refused (no gathering data)");
        check(try_gather(shop, shop.clientA, GatherShop::natural_block(kStone)) == kRefused && shop.gathering().counts().begins == 0
                  && shop.gathering().node_count() == 0 && bag_count(*shop.entity(kCharacterA), "TestStoneT1") == 0,
              "no gathering data: a natural node is not an object (refused), and nothing changes");
        check(shop.host.world.inventory().give(kCharacterA, "TestOreT1", 3).ok() && bag_count(*shop.entity(kCharacterA), "TestOreT1") == 3,
              "no gathering data: the other systems work (a give)");
    }
    // No data of either system: the chest system refuses an object that neither knows ("no chest data", the rule of stage 3.5b.1).
    {
        GatherShop shop(":memory:", false, false);
        shop.enter();
        check(try_gather(shop, shop.clientA, shop.capital_block()) == kRefused
                  && shop.host.world.chests().counts().refusals[static_cast<std::size_t>(world::ChestRefusal::noData)] == 1
                  && shop.gathering().counts().begins == 0,
              "no data: the chest system refuses the object (no chest data); no work");
    }
    gamedata::GatheringData data;
    check(!data.load((options.workDirectory / "no-such-gathering").string()) && !data.loaded() && !data.error().empty(),
          "a folder with no gathering-data.json: no data, and a cause");
    // Each part of the document that is not correct: the whole file is not used.
    const json good = gathering_document();
    struct Broken {
        const char* what;
        std::function<void(json&)> change;
    };
    const std::vector<Broken> broken{
        {"the format 2", [](json& d) { d["format"] = 2; }},
        {"no member tools", [](json& d) { d.erase("tools"); }},
        {"the option action 15", [](json& d) { d["option"]["action"] = 15; }},
        {"a row with the time 0", [](json& d) { d["rows"][0]["time"] = 0; }},
        {"a row with a time above an hour", [](json& d) { d["rows"][0]["time"] = 4000; }},
        {"a row with the respawn 120 to 60", [](json& d) { d["rows"][0]["respawn"] = {120, 60}; }},
        {"a row with a respawn above a week", [](json& d) { d["rows"][0]["respawn"] = {60, 700000}; }},
        {"a row with an event that the file does not have", [](json& d) { d["rows"][0]["event"] = "Test_NoSuchEvent"; }},
        {"two rows with one id", [](json& d) { d["rows"][1]["id"] = "Test_OreSmall"; }},
        {"a tool with the minimum above the maximum", [](json& d) { d["tools"]["TestPick"]["min"] = 3.0; }},
        {"a template with the controller slot 0", [](json& d) { d["templates"][0]["controllerSlot"] = 0; }},
        {"a bound with no range", [](json& d) { d["templates"][0]["bound"]["rangeSq"] = 0; }},
        {"a capital key that is not a UUID", [](json& d) { d["capitals"] = {{"not-a-uuid", {0, 0}}}; }},
        {"a capital with a row that the file does not have", [](json& d) { d["capitals"].begin().value() = {0, 99}; }},
        {"a region at x 40", [](json& d) { d["regions"][0]["x"] = 40; }},
        {"a region file name with ..", [](json& d) { d["regions"][0]["file"] = "../r.swgn"; }},
        {"a slice index with no template and a row", [](json& d) { d["regions"][0]["types"][4] = {-1, 0, "Test_Rock"}; }},
        {"two regions at one place", [](json& d) { d["regions"].push_back(d["regions"][0]); }},
        {"a creature slice with no sub id", [](json& d) { d["creatures"] = {{std::string(32, '7') + ":x", {{"template", 4}, {"row", 4}}}}; }},
    };
    for (const Broken& entry : broken) {
        json document = good;
        entry.change(document);
        const bool refused = !data.set(document.dump(), {{kRegion, region_records()}}) && !data.loaded() && !data.error().empty();
        check(refused, std::string("a document with ") + entry.what + " is not used (" + data.error() + ")");
    }
    check(!data.set("not JSON") && !data.loaded(), "a text that is not JSON is not used");
    check(!data.set(good.dump(), {{kRegion, {}}}) && !data.loaded(), "records that do not fit the entry of their region are not used");
    check(data.set(good.dump(), {{kRegion, region_records()}}) && data.loaded() && data.rows().size() == kRowCount && data.templates().size() == 5
              && data.capital_count() == 1 && data.creature_count() == 1 && data.natural_count() == kRecordCount,
          "the good document loads: 9 rows, 5 templates, 1 capital node, 1 creature slice, the census of the records");
    // The forms of a G: the low 3 bits 1, the high 32 bits 0, a counter of a primary record.
    check(data.natural(node_group(kOre)).has_value() && data.natural(node_group(kOre))->position.y == 202.0F,
          "the G of record 0 finds the node and its place");
    check(!data.natural(node_group(kOre) | 0x2U) && !data.natural(node_group(kOre) | (1ULL << 40)) && !data.natural(1)
              && !data.natural(node_group(kRecordCount)) && !data.natural(node_group(kRock)),
          "no node: other low bits, high bits, the counter 0, a counter of a tail, a record of a slice that is not a gathering slice");
    // The region files: read at the first lookup of their region, then kept; a file that is not complete gives no node.
    const std::filesystem::path folder = options.workDirectory / "gathering-gat1";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / gamedata::kGatheringDataFile) << good.dump();
    write_region(folder / (std::string(kRegion) + ".swgn"), 0, 0, region_records(), 2);
    gamedata::GatheringData files;
    check(files.load(folder.string()) && files.regions_loaded() == 0, "the folder loads; no region file is read before a lookup");
    const auto first = files.natural(node_group(kTree));
    const auto second = files.natural(node_group(kHemp));
    check(first && first->row == kTreeRow && second && second->row == kHempRow && files.regions_loaded() == 1 && files.work().regionLoads == 1
              && files.work().recordsRead == kRecordCount,
          "two lookups read the region file one time (11 records)");
    struct BadFile {
        const char* what;
        std::function<void(const std::filesystem::path&)> write;
    };
    std::vector<gamedata::NaturalRecord> badType = region_records();
    badType[kOre].type = 99;
    std::vector<gamedata::NaturalRecord> badPlace = region_records();
    badPlace[kOre].x = std::nanf("");
    const std::vector<BadFile> bad{
        {"another magic", [](const std::filesystem::path& p) { write_region(p, 0, 0, region_records(), 2, "XXXX"); }},
        {"the format 2", [](const std::filesystem::path& p) { write_region(p, 0, 0, region_records(), 2, "SWGN", 2); }},
        {"another place", [](const std::filesystem::path& p) { write_region(p, 1, 0, region_records(), 2); }},
        {"another count of tails", [](const std::filesystem::path& p) { write_region(p, 0, 0, region_records(), 3); }},
        {"a cut record", [](const std::filesystem::path& p) { write_region(p, 0, 0, region_records(), 2, "SWGN", 1, 5); }},
        {"a record type outside the slice list", [&](const std::filesystem::path& p) { write_region(p, 0, 0, badType, 2); }},
        {"a place that is not a number", [&](const std::filesystem::path& p) { write_region(p, 0, 0, badPlace, 2); }},
        {"no file", [](const std::filesystem::path& p) { std::filesystem::remove(p); }},
    };
    for (const BadFile& entry : bad) {
        entry.write(folder / (std::string(kRegion) + ".swgn"));
        gamedata::GatheringData other;
        const bool loaded = other.load(folder.string());
        check(loaded && !other.natural(node_group(kStone)) && !other.natural(node_group(kStone)) && other.work().regionLoads == 1,
              std::string("a region file with ") + entry.what + ": no node of the region (the file is read one time)");
    }
    // The tool side: tools/gamedata/check_gathering_tool.py gives the stop rules of the tool made-up sources.
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_gathering_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the gathering tool is in the tree");
    const auto output = options.workDirectory / "gathering-tool-check.log";
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    const std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \""
                                + script.lexically_normal().string() + "\" \"" + (options.workDirectory / "gathering-tool").string() + "\" > \""
                                + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::size_t passed = 0;
    std::size_t failed = 0;
    for (std::string line; std::getline(log, line);) {
        note(line);
        passed += line.rfind("ok  ", 0) == 0 ? 1 : 0;
        failed += line.rfind("FAIL", 0) == 0 ? 1 : 0;
    }
    check(status == 0 && failed == 0 && passed == 11, "each case of the check of the gathering tool passes (" + std::to_string(passed) + " of 11)");
}

service::HttpResponse set_node_respawn_reply(GatherShop& shop, const service::HttpRequest& request) {
    const server::http::commands::QueryArguments args(request);
    service::HttpResponse refusal;
    auto command = server::http::commands::set_node_respawn(kCharacterA, args, refusal);
    return command ? command(shop.host.world) : refusal;
}

/** GAT2: the identity and the states. */
void identity_and_states(const Options&) {
    // The values of live check L2 (region r_+04_+02): record 18627 has G 0x09188221, 18628 0x0918A221, 18614 0x0916E221.
    check(gamedata::GatheringData::natural_group(4, 2, 18628) == 0x09188221ULL && gamedata::GatheringData::natural_group(4, 2, 18629) == 0x0918A221ULL
              && gamedata::GatheringData::natural_group(4, 2, 18615) == 0x0916E221ULL,
          "the G of a record is the offline id of the client (live check L2: records 18627, 18628, 18614)");
    GatherShop shop;
    shop.enter();
    // The reference of a request: the actor G and 8 zero bytes, the target (Interact entity XOR K) + component, the option entity.
    InteractionBlock wrongTarget = GatherShop::natural_block(kStone);
    wrongTarget.target += 1;
    InteractionBlock wrongActor = GatherShop::natural_block(kStone);
    wrongActor.actor[8] = 1;
    InteractionBlock wrongOption = GatherShop::natural_block(kStone);
    wrongOption.optionEntity ^= 0x100;
    check(try_gather(shop, shop.clientA, wrongTarget) == kRefused && try_gather(shop, shop.clientA, wrongActor) == kRefused
              && try_gather(shop, shop.clientA, wrongOption) == kRefused && shop.gathering().counts().begins == 0,
          "a request with another target, another actor, or another option entity is refused");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kStone));
    check(last_reply(shop.clientA, fromA) == kAccepted, "the request of the stone (no tool) with the XOR rule is accepted");
    shop.hold(shop.clientA, GatherShop::natural_block(kStone), 1700);
    shop.end_block(shop.clientA);
    const auto gathers = gather_states(shop.clientA, 1, fromA);
    check(gathers.size() >= 2 && gathers.front().status == 1 && std::fabs(gathers.front().remaining - 1.5F) < 1e-4F
              && gathers.front().speed == 1.0F && gathers.front().active && gathers.front().target == node_group(kStone),
          "the Gathering state of A: Success, the amount 1.5, the speed 1, active, the G of the node");
    check(!gathers.empty() && gathers.back().status == 3 && !gathers.back().active && gathers.back().reference == gathers.front().reference + 1,
          "at the end: Done, not active, the reference counter one more");
    check(gather_states(shop.clientB, 1, fromB).empty(), "the client of B gets no Gathering state of A (the owner view)");
    const auto statesA = node_states(shop.clientA, fromA);
    const auto statesB = node_states(shop.clientB, fromB);
    bool oneField = true;
    for (const Seen& seen : fragments_of(shop.clientA, kNodeSlot, kNodeType, fromA)) {
        oneField = oneField && seen.fragment.fields.size() == 1 && field_of(seen.fragment, 0, 0).size() == 1;
    }
    check(!statesA.empty() && statesA.back().second == kDepleted && !statesB.empty() && statesB.back().second == kDepleted && oneField,
          "each observer gets the node state (slot 3, type 12): one field of one byte, Depleted (8)");
    check(bag_count(*shop.entity(kCharacterA), "TestStoneT1") == 1 && shop.gathering().node(node_key(kStone)) != nullptr
              && shop.gathering().node(node_key(kStone))->depleted,
          "A has the stone, and the node is depleted");
    // The return: Healthy (4) for each observer on the same entity.
    const std::size_t later = shop.clientA.got.size();
    const std::size_t laterB = shop.clientB.got.size();
    check(shop.gathering().set_respawn(node_key(kStone), 1s) == world::NodeError::none, "setup: the respawn in 1 s (a command)");
    shop.hold(shop.clientA, std::nullopt, 1500);
    const auto backA = node_states(shop.clientA, later);
    const auto backB = node_states(shop.clientB, laterB);
    check(!backA.empty() && backA.back().second == kHealthy && backA.back().first == statesA.back().first && !backB.empty()
              && backB.back().second == kHealthy && backB.back().first == statesB.back().first,
          "at the respawn each observer gets Healthy on the same interest (the same node)");
    // A capital node: the entity of its placement gets the node state.
    check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: A has the pick");
    const std::size_t capital = shop.clientB.got.size();
    shop.gather(shop.clientA, shop.capital_block(), 3000);
    check(shop.gathering().node(shop.host.interactions.objects()[0].key) != nullptr && last_node_state(shop.clientB, capital) == kDepleted
              && bag_count(*shop.entity(kCharacterA), "TestOreT1") >= 2,
          "a capital node (a placement UUID): the ore, and B gets Depleted on the entity of the placement");
    // The commands of the admin and development routes: the view of a character, and the short respawn of a depleted node.
    const nlohmann::json view = server::http::commands::gathering_view(shop.host.world, kCharacterA, 30.0F);
    check(view["inWorld"] == true && view["data"] == true && view["nodes"].size() == 2 && view["counts"]["completions"] == 2
              && view["skills"]["Mining"]["tool"] == "TestPick" && view["skills"]["Mining"]["speed"] == 1.5,
          "the view: the two nodes with a state of the server, the counts, the speed and the tool of each trade skill");
    const auto respawn = [&](const nlohmann::json& object) {
        const server::http::commands::JsonArguments args(object);
        service::HttpResponse refusal;
        auto command = server::http::commands::set_node_respawn(kCharacterA, args, refusal);
        return command ? command(shop.host.world).status : refusal.status;
    };
    check(respawn({{"node", node_key(kStone)}, {"seconds", 5}}) == 404 && respawn({{"node", "r_+09_+09#1"}, {"seconds", 5}}) == 404
              && respawn({{"node", shop.host.interactions.objects()[0].key}, {"seconds", 0}}) == 400
              && respawn({{"node", shop.host.interactions.objects()[0].key}, {"seconds", 5}}) == 200,
          "the respawn command: 404 for a node that is not depleted or not known, 400 for 0 s, 200 for a depleted node");
    // The query of the development route: the key of a natural node has "#" and "+" (tools/world sends them as %23 and %2B).
    service::HttpRequest query;
    query.query = "character=x&node=r_%2B00_%2B00%231&seconds=5&bad=%2&worse=%zz";
    const server::http::commands::QueryArguments arguments(query);
    check(arguments.text("node") == std::optional<std::string>("r_+00_+00#1") && arguments.integer("seconds", 1, 10) == 5
              && !arguments.text("bad") && !arguments.text("worse"),
          "a query value has its escapes decoded; a malformed escape gives no value");
    const service::HttpResponse reply = set_node_respawn_reply(shop, query);
    check(reply.status == 404 && reply.body.find("not depleted") != std::string::npos,
          "the respawn of the key r_+00_+00#1 by the query finds the node: it is not depleted (404)");
}

/** GAT3: the tool, the rank, and the range. */
void tools_and_gates(const Options&) {
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    const auto unchanged = [&](const std::string& what) {
        check(bag_count(a, "TestOreT1") == 0 && bag_count(a, "TestWoodT1") == 0 && bag_count(a, "TestStoneT1") == 0
                  && shop.gathering().node_count() == 0 && shop.gathering().work_count() == 0 && shop.host.store.list_gathering_nodes().empty()
                  && shop.skill(kCharacterA, "Mining").second == 0 && shop.skill(kCharacterA, "Logging").second == 0,
              what + ": nothing changes (no item, no node, no work, no row, no experience)");
    };
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kOre)) == kRefused && refusals(shop, GatherRefusal::noTool) == 1,
          "the ore with no pick is refused (no tool)");
    unchanged("no tool");
    check(shop.host.world.inventory().give(kCharacterA, "TestPick", 1).ok()
              && try_gather(shop, shop.clientA, GatherShop::natural_block(kOre)) == kRefused && refusals(shop, GatherRefusal::noTool) == 2,
          "a pick in the bag does not count (live check L6)");
    check(shop.equip(kCharacterA, "TestSickle", kSickleSlot) && try_gather(shop, shop.clientA, GatherShop::natural_block(kOre)) == kRefused
              && refusals(shop, GatherRefusal::noTool) == 3,
          "a sickle in slot 34 is not the tool of the ore");
    check(shop.equip(kCharacterA, "TestPick", kPickSlot, 150), "setup: the pick (gear score 150) in slot 36");
    world::ItemInstance* pick = &*a.equipment.slots[kPickSlot].item;
    const std::uint32_t durability = pick->durability;
    pick->durability = 0;
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kOre)) == kRefused && refusals(shop, GatherRefusal::brokenTool) == 1,
          "a broken pick (durability 0) is refused");
    pick->durability = durability;
    unchanged("a broken tool");
    // The speed: MinGatherEFF to MaxGatherEFF over the gear score range (live check L7): 1 + 0.5 x (2 - 1) = 1.5 at 150 of 100 to 200.
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kOre));
    auto states = gather_states(shop.clientA, 1, from);
    check(last_reply(shop.clientA, from) == kAccepted && !states.empty() && std::fabs(states.back().speed - 1.5F) < 1e-4F
              && std::fabs(states.back().remaining - 4.0F) < 1e-4F && std::fabs(shop.gathering().work_seconds(a) - 4.0F / 1.5F) < 1e-3F,
          "with the pick: accepted, the speed 1.5 of the tool, the amount 4, the time 2.67 s");
    shop.end_block(shop.clientA);
    // The rank part: GatheringEfficiency of the rank row (0.05 for each rank of the made-up table): the speed 1.5 x (1 + 0.1).
    check(shop.set_rank(kCharacterA, "Mining", 2), "setup: Mining rank 2");
    from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kOre));
    states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && std::fabs(states.back().speed - 1.65F) < 1e-4F, "Mining rank 2: the speed 1.5 x 1.1 = 1.65 (a decision of an agent)");
    shop.end_block(shop.clientA);
    // The rank gate: the tree needs Logging 50.
    check(shop.equip(kCharacterA, "TestAxe", kAxeSlot, 200) && shop.set_rank(kCharacterA, "Logging", 49), "setup: the axe and Logging 49");
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kTree)) == kRefused && refusals(shop, GatherRefusal::rank) == 1,
          "Logging 49 for a tree of rank 50 is refused (the rank gate)");
    check(shop.set_rank(kCharacterA, "Logging", 50), "setup: Logging 50");
    from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kTree));
    states = gather_states(shop.clientA, 1, from);
    check(last_reply(shop.clientA, from) == kAccepted && !states.empty() && std::fabs(states.back().speed - 3.0F) < 1e-4F,
          "Logging 50: accepted; the axe at its largest gear score has the speed 3");
    shop.end_block(shop.clientA);
    // No tool: the stone (NoToolNecessary) has the speed 1.
    from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kStone));
    states = gather_states(shop.clientA, 1, from);
    check(last_reply(shop.clientA, from) == kAccepted && !states.empty() && states.back().speed == 1.0F, "the stone needs no tool: the speed 1");
    shop.end_block(shop.clientA);
    // The range of the bound (3D, the data): the far ore is refused by the base.
    const std::uint64_t begins = shop.gathering().counts().begins;
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kFarOre)) == kRefused && shop.gathering().counts().begins == begins,
          "the ore 200 m away is refused (the range of its bound)");
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kQuest)) == kRefused && refusals(shop, GatherRefusal::unsupported) == 1,
          "a row with a rule that the backend cannot check is refused");
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kNoLoot)) == kRefused && refusals(shop, GatherRefusal::noLoot) == 1,
          "a row whose root has no table is refused");
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kRock)) == kRefused && shop.gathering().counts().begins == begins,
          "a record of a slice that is not a gathering slice is not an object");
    unchanged("the refusals");
}

/** GAT4: the cancel, the rest of a work, the reset of a work, a replay, the durability. */
void cancel_and_rest(const Options&) {
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.equip(kCharacterA, "TestPick", kPickSlot, 150) && shop.equip(kCharacterA, "TestSickle", kSickleSlot), "setup: the pick and the sickle");
    const std::uint32_t durability = a.equipment.slots[kPickSlot].item->durability;
    // A release after 1 s: Failure, no item, the node stays; the rest of the work (the ore row keeps it) is 4 - 1 x 1.5 = 2.5.
    std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kOre));
    shop.hold(shop.clientA, GatherShop::natural_block(kOre), 1000);
    shop.end_block(shop.clientA);
    auto states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && states.back().status == 2 && bag_count(a, "TestOreT1") == 0 && shop.gathering().node_count() == 0
              && shop.gathering().counts().cancels == 1 && a.equipment.slots[kPickSlot].item->durability == durability,
          "a release: Failure (live check L4), no item, the node stays, the pick keeps its durability");
    from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kOre));
    states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && states.back().remaining > 2.3F && states.back().remaining < 2.7F,
          "the next gather of the same node continues the work: the amount 2.5 (ResetGatherTimeOnEndInteraction false)");
    // A replay of the same block during the work: no second work.
    const std::uint64_t begins = shop.gathering().counts().begins;
    shop.hold(shop.clientA, GatherShop::natural_block(kOre), 2000);
    shop.end_block(shop.clientA);
    check(shop.gathering().counts().begins == begins && shop.gathering().counts().completions == 1 && bag_count(a, "TestOreT1") >= 2,
          "the same block again during the work is no new request; the work completes after 1.67 s");
    check(try_gather(shop, shop.clientA, GatherShop::natural_block(kOre)) == kRefused && refusals(shop, GatherRefusal::depleted) == 1,
          "a request of the depleted node is refused");
    check(a.equipment.slots[kPickSlot].item->durability == durability, "the pick keeps its durability after a gather (no loss until stage 3.7)");
    // The hemp resets its work at an end (ResetGatherTimeOnEndInteraction true).
    shop.request(shop.clientA, GatherShop::natural_block(kHemp));
    shop.hold(shop.clientA, GatherShop::natural_block(kHemp), 1000);
    shop.end_block(shop.clientA);
    from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kHemp));
    states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && std::fabs(states.back().remaining - 3.0F) < 1e-4F, "the hemp after a cancel: the whole amount 3 again");
    // A walk out of the range of the bound ends the work.
    shop.placeA = {110.0F, 200.0F, 30.0F};
    shop.hold(shop.clientA, GatherShop::natural_block(kHemp), 300);
    shop.end_block(shop.clientA);
    shop.placeA = kHere;
    shop.hold(shop.clientA, std::nullopt, 300);
    states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && states.back().status == 2 && bag_count(a, "TestFiberT1") == 0 && shop.gathering().work_count() == 0,
          "a walk out of the range: Failure, no fiber");
    // Damage ends the work (the action Gather of the client ends at a hit).
    from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kHemp));
    shop.host.world.interactions().damaged(a);
    shop.hold(shop.clientA, GatherShop::natural_block(kHemp), 300);
    shop.end_block(shop.clientA);
    states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && states.back().status == 2 && bag_count(a, "TestFiberT1") == 0 && count_type(shop.clientA, kServerCancel, from) >= 1,
          "damage: the use ends (3746), Failure, no fiber");
}

/** GAT5: the loot, the luck, the experience. */
void loot_and_luck(const Options&) {
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.equip(kCharacterA, "TestPick", kPickSlot, 150), "setup: the pick");
    const auto respawn = [&](std::uint32_t record) {
        (void)shop.gathering().set_respawn(node_key(record), 1s);
        shop.hold(shop.clientA, std::nullopt, 1100);
    };
    // Rank 0: the root (AND) gives 2 ore; the rare branch (1 in 50,000) gives nothing with this seed. Three gathers: 120 Mining experience
    // (100 for each rank of the made-up table): rank 1 and 20.
    for (int gather = 0; gather < 3; ++gather) {
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
        respawn(kOre);
    }
    check(bag_count(a, "TestOreT1") == 6 && bag_count(a, "TestGemA") + bag_count(a, "TestGemB") == 0,
          "three gathers at rank 0: 6 ore (2 each, the AND root), no gem");
    check(shop.skill(kCharacterA, "Mining") == std::pair<std::uint32_t, std::uint64_t>{1, 20},
          "the event of the row gives 40 Mining experience each time: rank 1 and 20");
    // Rank 5: the RollBonus 100,000 of the rank reaches the threshold 99,999 each time; the two gems have one threshold (a tie): each one
    // comes.
    check(shop.set_rank(kCharacterA, "Mining", 5), "setup: Mining rank 5");
    for (int gather = 0; gather < 12; ++gather) {
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
        respawn(kOre);
    }
    const std::uint32_t gemA = bag_count(a, "TestGemA");
    const std::uint32_t gemB = bag_count(a, "TestGemB");
    note("GAT5 rank 5: gem A " + std::to_string(gemA) + ", gem B " + std::to_string(gemB));
    check(gemA + gemB == 12 && gemA > 0 && gemB > 0, "rank 5: a gem each time, and each gem of the tie comes (12 gathers)");
    // The bonus kinds of a table (the roller of the client): AddToRoll adds the luck, ClampMax raises the low end, IgnoreBonus ignores it.
    world::Rewards& rewards = shop.host.world.rewards();
    const auto hits = [&](const char* table, std::uint32_t bonus) {
        world::LootContext context;
        context.rollBonus = bonus;
        int found = 0;
        for (int roll = 0; roll < 200; ++roll) {
            found += rewards.roll_root(*shop.host.loot.table_index(table), context).empty() ? 0 : 1;
        }
        return found;
    };
    const int add = hits("Test_BonusAdd", 100);
    const int clamp = hits("Test_BonusClamp", 99);
    const int ignore = hits("Test_BonusIgnore", 1000);
    const int none = hits("Test_BonusAdd", 0);
    note("GAT5 of 200 rolls: AddToRoll " + std::to_string(add) + ", ClampMax " + std::to_string(clamp) + ", IgnoreBonus " + std::to_string(ignore)
         + ", no luck " + std::to_string(none));
    check(add == 200 && clamp == 200 && ignore < 20 && none < 20,
          "the luck: AddToRoll and ClampMax reach the threshold 100 each time, IgnoreBonus and no luck about 1 in 100");
    // A status with EFFMining and ROLMining (a food of the data has such cells): the speed 1.5 x (1 + 0.25 + 0.5), and the luck.
    gamedata::AbilityTables tables;
    tables.cooldowns.push_back({"Test_Cooldown", 1.0F, 0.0F});
    gamedata::StatusRow food;
    food.id = "Test_MiningFood";
    food.cells.numbers = {{"EFFMining", 0.5F}, {"ROLMining", 100000.0F}};
    tables.statusEffects.push_back(food);
    check(shop.host.abilities.set(tables), "setup: the ability data with the status Test_MiningFood (" + shop.host.abilities.error() + ")");
    world::ActiveStatus status;
    status.status = "Test_MiningFood";
    status.permanent = true;
    a.statuses.push_back(status);
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::natural_block(kOre));
    const auto states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && std::fabs(states.back().speed - 1.5F * 1.75F) < 1e-3F,
          "a status with EFFMining 0.5 at rank 5 (0.25): the speed 1.5 x 1.75 (the EFF parts add to the rank part)");
    shop.end_block(shop.clientA);
    const world::GatherParts parts = shop.gathering().parts_of(a, shop.host.gathering.rows()[kOreRow]);
    check(std::fabs(parts.rollBonus - 100000.0F) < 1.0F && std::fabs(parts.rollEffects - 100000.0F) < 1.0F && parts.toolItem == "TestPick"
              && parts.toolGearScore == 150,
          "the parts: the luck of the rank and the ROL part of the status, the tool and its gear score");
}

/** GAT6: the transaction. */
void transactions(const Options& options) {
    std::filesystem::create_directories(options.workDirectory);
    for (const std::string table : {"gathering_nodes", "items", "progressions"}) {
        const auto path = options.workDirectory / ("gathering-failure-" + table + ".sqlite3");
        std::filesystem::remove(path);
        GatherShop shop(path.string());
        shop.enter();
        world::Entity& a = *shop.entity(kCharacterA);
        check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: the pick");
        const std::string verb = table == "progressions" ? "UPDATE" : "INSERT";
        check(run_sql(path, "CREATE TRIGGER refuse_gather BEFORE INSERT ON " + table + " BEGIN SELECT RAISE(FAIL, 'gather write failure'); END;"
                                "CREATE TRIGGER refuse_gather_update BEFORE UPDATE ON " + table
                                + " BEGIN SELECT RAISE(FAIL, 'gather write failure'); END;"),
              "setup: the database refuses a write of " + table);
        const std::size_t from = shop.clientA.got.size();
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
        const auto states = gather_states(shop.clientA, 1, from);
        check(bag_count(a, "TestOreT1") == 0 && shop.host.store.list_items(kCharacterA).size() == 1 && shop.skill(kCharacterA, "Mining").second == 0
                  && shop.gathering().node_count() == 0 && shop.host.store.list_gathering_nodes().empty()
                  && shop.gathering().counts().storeFailures == 1 && count_type(shop.clientA, kServerCancel, from) == 1 && !states.empty()
                  && states.back().status == 2,
              table + ": a refused write gives no item, no experience, no depletion; the use ends (3746, Failure)");
        check(last_node_state(shop.clientB, from) == 0, table + ": B gets no node state (the node stays Healthy)");
        check(run_sql(path, "DROP TRIGGER refuse_gather; DROP TRIGGER refuse_gather_update;"), "setup: the database writes again");
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
        check(bag_count(a, "TestOreT1") == 2 && shop.skill(kCharacterA, "Mining").second == 40 && shop.host.store.list_gathering_nodes().size() == 1,
              table + ": the next gather gives the ore, the experience, and the row of the node");
    }
    // A full bag (500 stacks): no item, no experience, no depletion; the roll stays with the character (no new roll).
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.equip(kCharacterA, "TestPick", kPickSlot)
              && shop.host.world.inventory().give(kCharacterA, "TestFiller", world::kMaxContainerSlots).ok()
              && bag_count(a, "TestFiller") == world::kMaxContainerSlots,
          "setup: the pick, and 500 items fill the bag");
    std::size_t from = shop.clientA.got.size();
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
    check(bag_count(a, "TestOreT1") == 0 && shop.skill(kCharacterA, "Mining").second == 0 && shop.gathering().node_count() == 0
              && shop.gathering().counts().fullBags == 1 && shop.gathering().counts().rolls == 1 && count_type(shop.clientA, kServerCancel, from) == 1,
          "a full bag: no item, no experience, the node stays, the use ends");
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
    check(shop.gathering().counts().fullBags == 2 && shop.gathering().counts().rolls == 1, "again with a full bag: no new roll");
    check(shop.host.world.inventory().remove(kCharacterA, 0).ok(), "setup: one place free");
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
    check(bag_count(a, "TestOreT1") == 2 && shop.gathering().counts().rolls == 1 && shop.skill(kCharacterA, "Mining").second == 40
              && shop.gathering().node(node_key(kOre)) != nullptr && shop.gathering().node(node_key(kOre))->depleted,
          "with a place: the kept roll (2 ore), the experience, the depletion");
    // An item that the catalog does not have: no item, the node stays.
    from = shop.clientA.got.size();
    shop.gather(shop.clientA, GatherShop::natural_block(kMystery), 1500);
    check(shop.gathering().node(node_key(kMystery)) == nullptr && shop.gathering().counts().fullBags == 3 && count_type(shop.clientA, kServerCancel, from) == 1,
          "an item that the catalog does not have: nothing changes, the node stays");
    // A roll of 2,500 stones (three stacks of 1,000) with two places: it is kept whole; with a third place it all comes.
    check(shop.host.world.inventory().remove(kCharacterA, 1).ok() && shop.host.world.inventory().remove(kCharacterA, 2).ok(),
          "setup: two more places free (one has the ore)");
    shop.gather(shop.clientA, GatherShop::natural_block(kHeap), 1500);
    check(bag_count(a, "TestStoneT1") == 0 && shop.gathering().node(node_key(kHeap)) == nullptr, "the heap with two places: nothing (it needs three)");
    check(shop.host.world.inventory().remove(kCharacterA, 3).ok(), "setup: a third place");
    shop.gather(shop.clientA, GatherShop::natural_block(kHeap), 1500);
    check(bag_count(a, "TestStoneT1") == 2500 && shop.gathering().node(node_key(kHeap)) != nullptr,
          "with three places: the whole roll of 2,500 in stacks of 1,000 (no cut at the stack limit)");
}

/** GAT7: the restart. */
void restart(const Options& options) {
    const auto path = options.workDirectory / "gathering-restart.sqlite3";
    std::filesystem::remove(path);
    std::uint64_t left = 0;
    std::chrono::nanoseconds draw{0};
    {
        GatherShop shop(path.string());
        shop.enter();
        check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: the pick");
        const std::uint64_t before = shop.host.world.world_time().run_ns();
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
        const world::GatherNode* node = shop.gathering().node(node_key(kOre));
        check(node != nullptr && node->depleted, "the ore is depleted");
        if (node == nullptr) {
            return;
        }
        draw = std::chrono::nanoseconds(node->respawnRunNs - before);
        check(draw >= 59s && draw <= 123s, "one draw between the bounds 60 and 120 s of the row (at the depletion)");
        // A change of interest makes no new draw.
        const world::WorldClock::time_point respawnAt = node->respawn;
        shop.host.disconnect(shop.clientB.peer);
        shop.hold(shop.clientA, std::nullopt, 500);
        check(shop.gathering().node(node_key(kOre))->respawn == respawnAt, "B leaves: the respawn time stays");
        shop.hold(shop.clientA, std::nullopt, 20000);
        // A work at the stop: the stone.
        shop.request(shop.clientA, GatherShop::natural_block(kStone));
        shop.hold(shop.clientA, GatherShop::natural_block(kStone), 500);
        check(shop.gathering().work_count() == 1, "setup: a work at the stop");
        check(shop.host.world.world_time().save("test stop"), "the stop saves the run clock");
        left = node->respawnRunNs - shop.host.world.world_time().run_ns();
    }
    // A new start with the same database: the clock of the host goes on 1 hour (the downtime does not count).
    GatherShop shop(path.string(), true);
    shop.host.clock.advance(3600s);
    shop.enter();
    const world::GatherNode* node = shop.gathering().node(node_key(kOre));
    const double remaining = node != nullptr ? std::chrono::duration<double>(node->respawn - shop.host.clock.now()).count() : -1.0;
    note("GAT7 left at the stop " + std::to_string(static_cast<double>(left) / 1e9) + " s, after the start " + std::to_string(remaining) + " s");
    check(node != nullptr && node->depleted && std::fabs(remaining - static_cast<double>(left) / 1e9) < 1.0,
          "after the start the node is depleted, with the remaining time of the stop (the downtime does not count)");
    check(shop.gathering().work_count() == 0 && bag_count(*shop.entity(kCharacterA), "TestStoneT1") == 0
              && shop.gathering().node(node_key(kStone)) == nullptr,
          "the work of the stop ended: no stone, the stone node stays");
    check(last_node_state(shop.clientA) == kDepleted && last_node_state(shop.clientB) == kDepleted,
          "late observers (A and B after the start) get the node Depleted in their first record");
    const auto interestA = node_states(shop.clientA).back().first;
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    shop.hold(shop.clientA, std::nullopt, static_cast<int>(remaining * 1000.0) + 1500);
    const auto backA = node_states(shop.clientA, fromA);
    check(!backA.empty() && backA.back().second == kHealthy && backA.back().first == interestA && last_node_state(shop.clientB, fromB) == kHealthy
              && shop.host.store.list_gathering_nodes().empty(),
          "at its time the same node is back for each observer (the same interest), and its row goes");
    check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: the pick");
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3000);
    check(bag_count(*shop.entity(kCharacterA), "TestOreT1") >= 2, "the node back gives its ore again");
    // Old rows: another version of the distribution file, a placement that the data does not have, another row of the record.
    const auto stalePath = options.workDirectory / "gathering-stale.sqlite3";
    std::filesystem::remove(stalePath);
    {
        state::Store store;
        check(store.open(stalePath.string()), "setup: a store");
        const auto row = [](const std::string& key, const std::string& source, const char* rowId) {
            state::GatheringNodeRow value;
            value.key = key;
            value.source = source;
            value.row = rowId;
            value.generation = 1;
            value.respawnRunNs = 30000000000ULL;
            return value;
        };
        check(store.write_gathering_node(row(node_key(kOre), "distribution:old-version", "Test_OreSmall"))
                  && store.write_gathering_node(row("0b1ec700-0000-4000-8000-0000000000ff", "capital", "Test_OreSmall"))
                  && store.write_gathering_node(row(node_key(kTree), std::string("distribution:") + kVersion, "Test_Stone"))
                  && store.write_gathering_node(row(node_key(kHemp), std::string("distribution:") + kVersion, "Test_Hemp")),
              "setup: four stored rows");
    }
    GatherShop stale(stalePath.string(), true);
    stale.enter();
    check(stale.gathering().counts().staleRows == 3 && stale.gathering().counts().storedNodes == 1 && stale.host.store.list_gathering_nodes().size() == 1
              && stale.gathering().node(node_key(kHemp)) != nullptr && stale.gathering().node(node_key(kHemp))->depleted,
          "three old rows go (another version, an unknown placement, another row); the good one is a depleted node");
}

/** GAT8: two sessions at one node. */
void two_sessions(const Options&) {
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    check(shop.equip(kCharacterA, "TestPick", kPickSlot) && shop.equip(kCharacterB, "TestPick", kPickSlot), "setup: A and B have a pick");
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    // A begins; B begins 0.5 s later (the capacity 1 of the bound does not apply to GatherResource: client 0x35C3858).
    shop.request(shop.clientA, GatherShop::natural_block(kOre));
    for (int step = 0; step < 5; ++step) {
        shop.report(shop.clientA, GatherShop::natural_block(kOre));
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    shop.request(shop.clientB, GatherShop::natural_block(kOre));
    check(last_reply(shop.clientA, fromA) == kAccepted && last_reply(shop.clientB, fromB) == kAccepted && shop.gathering().work_count() == 2,
          "both begin the same node (two works)");
    for (int step = 0; step < 35; ++step) {
        shop.report(shop.clientA, GatherShop::natural_block(kOre));
        shop.report(shop.clientB, GatherShop::natural_block(kOre));
        shop.host.steps(10, {&shop.clientA, &shop.clientB});
    }
    shop.end_block(shop.clientA);
    shop.end_block(shop.clientB);
    const auto gathersA = gather_states(shop.clientA, 1, fromA);
    const auto gathersB = gather_states(shop.clientB, 1, fromB);
    check(bag_count(a, "TestOreT1") == 2 && shop.skill(kCharacterA, "Mining").second == 40 && !gathersA.empty() && gathersA.back().status == 3,
          "A completes first: the ore, the experience, Done");
    check(bag_count(b, "TestOreT1") == 0 && shop.skill(kCharacterB, "Mining").second == 0 && !gathersB.empty() && gathersB.back().status == 2
              && count_type(shop.clientB, kServerCancel, fromB) == 1 && shop.gathering().counts().contested == 1,
          "B completes on a depleted node: nothing, Failure, its use ends (3746; a decision of an agent)");
    const auto nodesA = node_states(shop.clientA, fromA);
    const auto nodesB = node_states(shop.clientB, fromB);
    check(!nodesA.empty() && nodesA.back().second == kDepleted && !nodesB.empty() && nodesB.back().second == kDepleted,
          "each observer gets the depletion (one state of the node, shared)");
    bool privateA = true;
    for (const auto& gather : gathersB) {
        privateA = privateA && gather.status != 3;
    }
    check(privateA, "the client of B gets no Done of A (the Gathering state is private)");
    // The return: each observer; then B gathers the node.
    const std::size_t laterA = shop.clientA.got.size();
    const std::size_t laterB = shop.clientB.got.size();
    check(shop.gathering().set_respawn(node_key(kOre), 2s) == world::NodeError::none, "setup: the respawn in 2 s");
    shop.hold(shop.clientA, std::nullopt, 2500);
    check(last_node_state(shop.clientA, laterA) == kHealthy && last_node_state(shop.clientB, laterB) == kHealthy, "each observer gets the return");
    shop.gather(shop.clientB, GatherShop::natural_block(kOre), 3000);
    check(bag_count(b, "TestOreT1") == 2 && bag_count(a, "TestOreT1") == 2, "B gathers the same node after its return; A keeps its ore");
}

/** GAT9: the load. */
void load(const Options& options) {
    // The time limit of each run (a budget of this project, written before the case runs).
    constexpr double kLimitSeconds = 5.0;
    constexpr std::uint32_t kNear = 200;
    struct Result {
        gamedata::GatheringDataWork data;
        world::GatheringCounts counts;
        std::uint64_t census{0};
        std::size_t regions{0};
        double seconds{0.0};
    };
    const auto run = [&](std::uint32_t far, const char* name) {
        // The files: the region of the cases with 200 more ore records near the characters, and five far regions of `far` records.
        const std::filesystem::path folder = options.workDirectory / name;
        std::filesystem::remove_all(folder);
        std::filesystem::create_directories(folder);
        std::vector<std::tuple<std::string, int, int, std::uint32_t>> regions;
        for (int index = 0; index < 5; ++index) {
            char region[16];
            std::snprintf(region, sizeof(region), "r_+%02d_+10", index + 10);
            regions.emplace_back(region, index + 10, 10, far);
        }
        std::ofstream(folder / gamedata::kGatheringDataFile) << gathering_document(regions, kNear).dump();
        std::vector<gamedata::NaturalRecord> near = region_records();
        for (std::uint32_t index = 0; index < kNear; ++index) {
            near.push_back({0, 99.0F + static_cast<float>(index % 20) * 0.1F, 201.0F + static_cast<float>(index / 20) * 0.1F, 29.0F});
        }
        write_region(folder / (std::string(kRegion) + ".swgn"), 0, 0, near, 2);
        for (const auto& [region, x, y, records] : regions) {
            std::vector<gamedata::NaturalRecord> list(records);
            for (std::uint32_t index = 0; index < records; ++index) {
                list[index] = {0, 20000.0F + static_cast<float>(x) * 1000.0F + static_cast<float>(index % 1000), 20000.0F + static_cast<float>(index / 1000),
                               30.0F};
            }
            write_region(folder / (region + ".swgn"), x, y, list, 0);
        }
        GatherShop shop(":memory:", false, false);
        check(shop.host.gathering.load(folder.string()), "setup: the gathering files load (" + shop.host.gathering.error() + ")");
        const auto start = std::chrono::steady_clock::now();
        shop.enter();
        // 48 more characters near the nodes (50 with A and B).
        for (unsigned index = 0; index < 48; ++index) {
            char id[37];
            std::snprintf(id, sizeof(id), "9a7e0000-0000-4000-8000-%012x", index);
            const state::WorldPosition place{100.0F + static_cast<float>(index % 8), 200.0F + static_cast<float>(index / 8), 30.0F};
            add_player(shop.host.store, "7656120225536" + std::to_string(1000 + index), "Tester", id, "Gather" + std::to_string(index), place);
            check(shop.host.world.spawn_character(id, place) == world::SpawnError::none, "setup: a character in the world");
        }
        check(shop.equip(kCharacterA, "TestPick", kPickSlot) && shop.equip(kCharacterB, "TestPick", kPickSlot), "setup: the picks");
        // 20 rounds: A and B each gather a near ore node (40 depletions, 40 entries of the due list).
        for (std::uint32_t round = 0; round < 20; ++round) {
            const InteractionBlock blockA = GatherShop::natural_block(kRecordCount + 2 * round);
            const InteractionBlock blockB = GatherShop::natural_block(kRecordCount + 2 * round + 1);
            shop.request(shop.clientA, blockA);
            shop.request(shop.clientB, blockB);
            for (int step = 0; step < 30; ++step) {
                shop.report(shop.clientA, blockA);
                shop.report(shop.clientB, blockB);
                shop.host.steps(10, {&shop.clientA, &shop.clientB});
            }
            shop.end_block(shop.clientA);
            shop.end_block(shop.clientB);
        }
        // 1,000 updates: a move of a character and a lookup of the node of a request (the resolver of the base).
        for (unsigned update = 0; update < 1000; ++update) {
            char id[37];
            std::snprintf(id, sizeof(id), "9a7e0000-0000-4000-8000-%012x", update % 48);
            world::Entity* entity = shop.host.world.find_character(id);
            (void)shop.host.world.set_position(entity->id, {100.0F + static_cast<float>(update % 7), 200.0F + static_cast<float>(update % 5), 30.0F}, false);
            world::InteractionReference reference;
            reference.group = node_group(kRecordCount + update % kNear);
            (void)shop.gathering().resolve(reference);
        }
        // One hour of ticks: each depleted node respawns once (the due list).
        for (int tick = 0; tick < 3600; ++tick) {
            shop.host.clock.advance(1s);
            shop.host.world.tick();
        }
        Result result;
        result.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        result.data = shop.host.gathering.work();
        result.counts = shop.gathering().counts();
        result.census = shop.host.gathering.natural_count();
        result.regions = shop.host.gathering.regions_loaded();
        std::filesystem::remove_all(folder);
        return result;
    };
    const Result small = run(5000, "gathering-gat9-small");
    const Result large = run(500000, "gathering-gat9-large");
    for (const auto& [name, result] : {std::pair<const char*, const Result&>{"5 x 5,000 far", small}, {"5 x 500,000 far", large}}) {
        note(std::string("GAT9 ") + name + ": " + std::to_string(result.seconds) + " s, census " + std::to_string(result.census) + ", lookups "
             + std::to_string(result.data.lookups) + ", region loads " + std::to_string(result.data.regionLoads) + ", records read "
             + std::to_string(result.data.recordsRead) + ", completions " + std::to_string(result.counts.completions) + ", due looks "
             + std::to_string(result.counts.dueLooks) + ", respawns " + std::to_string(result.counts.respawns));
    }
    check(large.census >= 2500000, "the large run has the census of 2.5 million placements and more");
    check(large.counts.completions == 40 && small.counts.completions == 40 && large.counts.respawns == 40 && small.counts.respawns == 40,
          "40 gathers and 40 respawns in each run");
    check(large.data.lookups == small.data.lookups && large.data.regionLoads == small.data.regionLoads
              && large.data.recordsRead == small.data.recordsRead && large.counts.dueLooks == small.counts.dueLooks
              && large.counts.resolved == small.counts.resolved,
          "the same work with 100 times more far placements (lookups, region loads, records read, due looks)");
    check(large.data.regionLoads == 1 && large.regions == 1 && large.data.recordsRead == kRecordCount + kNear,
          "only the region of the requests is read (211 records, not 2.5 million): no scan of the world");
    check(small.seconds <= kLimitSeconds && large.seconds <= kLimitSeconds, "each run takes 5 s or less");
}

/** GAT10: the skinning. */
void skinning(const Options&) {
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    world::Entity& b = *shop.entity(kCharacterB);
    // A creature that lives: no node.
    world::Entity* living = shop.host.world.creatures().spawn("Test_BagWolf", bags::kKillPlace, 0.0F).entity;
    check(living != nullptr, "setup: a wolf");
    if (living == nullptr) {
        return;
    }
    check(shop.equip(kCharacterA, "TestKnife", kKnifeSlot), "setup: A has a knife");
    check(try_gather(shop, shop.clientA, GatherShop::body_block(*living)) == kRefused && refusals(shop, GatherRefusal::alive) == 1,
          "a wolf that lives cannot be skinned");
    // The death: the body is a node (Healthy) for each observer.
    const std::size_t fromA = shop.clientA.got.size();
    const std::size_t fromB = shop.clientB.got.size();
    const world::EntityId first = living->id;
    (void)shop.host.world.creatures().hurt(first, bags::kWolfHealth * 2.0F, a.id);
    shop.hold(shop.clientA, std::nullopt, 1000);
    world::Entity* body = shop.host.world.find(first);
    check(body != nullptr && body->creature.dead && body->creature.skinState == kHealthy && last_node_state(shop.clientA, fromA) == kHealthy
              && last_node_state(shop.clientB, fromB) == kHealthy,
          "the death: the body has the node state Healthy (slot 3) for A and B");
    if (body == nullptr) {
        return;
    }
    check(try_gather(shop, shop.clientB, GatherShop::body_block(*body)) == kRefused && refusals(shop, GatherRefusal::noTool) == 1,
          "B with no knife is refused");
    const std::size_t skin = shop.clientA.got.size();
    const std::size_t skinB = shop.clientB.got.size();
    shop.request(shop.clientA, GatherShop::body_block(*body));
    const auto states = gather_states(shop.clientA, 1, skin);
    check(!states.empty() && std::fabs(states.back().speed - 2.2F) < 1e-4F && states.back().target == first,
          "the skinning: the speed 2.2 of the knife, the G of the body");
    shop.hold(shop.clientA, GatherShop::body_block(*body), 1600);
    shop.end_block(shop.clientA);
    check(bag_count(a, "TestHideT1") == 2 && bag_count(a, "TestMeatT1") == 1 && shop.skill(kCharacterA, "Skinning").second == 32,
          "A gets the hide, the meat, and 32 Skinning experience");
    check(last_node_state(shop.clientA, skin) == kDepleted && last_node_state(shop.clientB, skinB) == kDepleted
              && shop.host.store.list_gathering_nodes().empty(),
          "the body is Depleted for A and B; a body has no row in the store");
    check(shop.equip(kCharacterB, "TestKnife", kKnifeSlot) && try_gather(shop, shop.clientB, GatherShop::body_block(*body)) == kRefused
              && refusals(shop, GatherRefusal::depleted) == 1,
          "B cannot skin the same body again");
    // The body leaves (the creature system removes it at the respawn of its spawn point).
    const InteractionBlock firstBlock = GatherShop::body_block(*body);
    check(shop.host.world.creatures().remove(first).error == world::CreatureError::none && shop.host.world.find(first) == nullptr,
          "setup: the body leaves the world");
    check(try_gather(shop, shop.clientB, firstBlock) == kRefused, "a request of a body that left is refused");
    check(shop.host.world.interactions().external(first) == nullptr, "the external object of the body goes with it");
    check(shop.gathering().node("body/" + hex64(first)) == nullptr, "the entry of the skinned body goes at its removal (finding GA-R1)");
    // A new wolf (a respawn of the creature system is a new entity): a new node.
    world::Entity* second = shop.dead_wolf();
    shop.hold(shop.clientA, std::nullopt, 1000);
    check(second != nullptr && second->id != first && second->creature.skinState == kHealthy, "a new wolf is a new body, Healthy");
    if (second == nullptr) {
        return;
    }
    shop.gather(shop.clientB, GatherShop::body_block(*second), 1600);
    check(bag_count(b, "TestHideT1") == 2 && shop.skill(kCharacterB, "Skinning").second == 32, "B skins the new body");
    const world::EntityId secondId = second->id;
    // The body leaves during a work: the use ends with no reward.
    world::Entity* third = shop.dead_wolf();
    if (third == nullptr) {
        return;
    }
    const std::size_t during = shop.clientA.got.size();
    shop.request(shop.clientA, GatherShop::body_block(*third));
    shop.hold(shop.clientA, GatherShop::body_block(*third), 500);
    (void)shop.host.world.creatures().remove(third->id);
    shop.hold(shop.clientA, std::nullopt, 1500);
    const auto ended = gather_states(shop.clientA, 1, during);
    check(bag_count(a, "TestHideT1") == 2 && shop.gathering().work_count() == 0 && !ended.empty() && ended.back().status == 2,
          "the body leaves during the work: no hide, the work ends (Failure)");
    // The entry of a skinned body stays while the body is in the world, and goes after the body left (code review of stage 3.6a, finding
    // GA-R1: a time is no proof of the removal); a body has no respawn.
    check(shop.gathering().node_count() == 1, "the skinned body that is in the world has an entry");
    for (int tick = 0; tick < 610; ++tick) {
        shop.host.clock.advance(1s);
        shop.host.world.tick();
    }
    const std::string secondKey = "body/" + hex64(secondId);
    const world::Entity* stays = shop.host.world.find(secondId);
    check(shop.gathering().node_count() == 1 && shop.gathering().node("body/" + hex64(first)) == nullptr
              && shop.gathering().node(secondKey) != nullptr && shop.gathering().node(secondKey)->depleted && stays != nullptr
              && stays->creature.skinState == kDepleted && shop.gathering().counts().respawns == 0,
          "after 610 s the entry of the body that left is gone; the body that stays keeps its entry and stays Depleted, with no respawn");
    check(shop.host.world.creatures().remove(secondId).error == world::CreatureError::none, "setup: the second body leaves the world");
    check(shop.gathering().node(secondKey) == nullptr, "its entry goes at its removal");
    for (int tick = 0; tick < 11; ++tick) {
        shop.host.clock.advance(1s);
        shop.host.world.tick();
    }
    check(shop.gathering().node_count() == 0 && shop.gathering().counts().respawns == 0, "no entry and no respawn after it left");
}

} // namespace gat

std::vector<Case> gathering_cases() {
    using namespace gat;
    return {{"GAT1", "gathering: no data, a document or a region file that is not complete, the forms of a G", data_rules},
            {"GAT2", "gathering: the identity of a node (live check L2), the node state 12 and the Gathering state 81", identity_and_states},
            {"GAT3", "gathering: the tool, the rank gate, the range; each refusal changes nothing", tools_and_gates},
            {"GAT4", "gathering: the cancel, the rest of a work, the reset, a replay, no durability loss", cancel_and_rest},
            {"GAT5", "gathering: the loot graph, a rare tie, the luck of the rank and a status, the bonus kinds, the experience", loot_and_luck},
            {"GAT6", "gathering: a refused write, a full bag (the kept roll), an unknown item, a roll of three stacks", transactions},
            {"GAT7", "gathering: the restart (the run clock), a work at the stop, late observers, the same node back, old rows", restart},
            {"GAT8", "gathering: two sessions at one node: the first completion wins, shared states, private rewards", two_sessions},
            {"GAT9", "gathering: the load (2.5 million placements, 50 characters, 1,000 updates, the due list)", load},
            {"GAT10", "gathering: the skinning of a body, its observers, the removal of the body, a new creature", skinning}};
}

} // namespace soulwarden::backend::tests
