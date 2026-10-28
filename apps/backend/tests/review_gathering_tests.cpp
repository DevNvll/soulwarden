// The cases of the code review of stage 3.6a (findings GA-R1 to GA-R7). Each case fails on the tree of the commit of stage 3.6a and passes
// with the correction. The cases come from the proof tests of the review (GAP1 to GAP7, and the source probes GAP-S1 and GAP-S2). Each
// case uses only interfaces of the tree of stage 3.6a, so that the same file proves the fault on that tree. Made-up accounts, ids, names,
// and data only.
//
//   GR1  GA-R1: a skinned body that stays in the world after 605 s (the old time of its entry) gives no second skinning to A or to B; its
//        entry stays while the body is there, and goes after the body leaves
//   GR2  GA-R2: at a start with no region file, a stored depletion of the region stays (also a capital depletion with no interaction
//        data), and a row of another version still goes; with the data back the nodes stay depleted until their saved time
//   GR3  GA-R3: a new request for node B during the work on node A: the end of A keeps its rest, and A continues with the rest; a refused
//        new request leaves the work on A as it was
//   GR4  GA-R4: the gathering tool stops at a source with no gate column, at an unknown flag text, and at an event table with no reward
//        column (check_gathering_tool.py: gate-columns, flag-texts, event-reward)
//   GR5  GA-R5: the loader refuses a row that the file marks supported with an item, with no item member, with a restriction that the
//        backend cannot check, or with more than one gatherer; ItemRequired with an empty item stays supported; an item that is not a text
//        refuses the file
//   GR6  GA-R6: a region file with another digest than the document gives no node, and a stored depletion of its region stays; a document
//        with no digest (the form of before) has no check; an export that stops leaves the published set (check_gathering_tool.py:
//        export-publish)
//   GR7  GA-R7: with a full bag, a gather of A, of B, and of A again makes two rolls; later each node gives its own kept roll

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <set>
#include <string>
#include <vector>

#include <mbedtls/sha256.h>

#include "support/cases.h"
#include "support/check.h"
#include "support/gathering_fixture.h"

namespace soulwarden::backend::tests {
namespace review_gathering {

using namespace gather;
using namespace std::chrono_literals;
using chests::bag_count;
using chests::gather_states;

/** The world goes on by `seconds` in steps of 1 s; both clients report no block. */
void wait_seconds(GatherShop& shop, int seconds) {
    for (int step = 0; step < seconds; ++step) {
        shop.report(shop.clientA, std::nullopt);
        shop.report(shop.clientB, std::nullopt);
        shop.host.clock.advance(1s);
        shop.host.steps(1, {&shop.clientA, &shop.clientB});
    }
}

/** @return A stored depletion row. */
state::GatheringNodeRow depletion(const std::string& key, const std::string& source, const char* row, std::uint64_t respawnRunNs) {
    state::GatheringNodeRow value;
    value.key = key;
    value.source = source;
    value.row = row;
    value.generation = 1;
    value.respawnRunNs = respawnRunNs;
    return value;
}

/** @return The bytes of a region file (the form of tools/gamedata/gathering.py): "SWGN", the format 1, x 0, y 0, the records, 2 tails. */
std::string region_file(const std::vector<gamedata::NaturalRecord>& records) {
    std::string data("SWGN", 4);
    const auto put = [&](const void* value, std::size_t size) { data.append(static_cast<const char*>(value), size); };
    const std::uint32_t format = 1;
    const std::int32_t place = 0;
    const std::uint32_t count = static_cast<std::uint32_t>(records.size());
    const std::uint32_t tails = 2;
    put(&format, 4);
    put(&place, 4);
    put(&place, 4);
    put(&count, 4);
    put(&tails, 4);
    for (const gamedata::NaturalRecord& record : records) {
        put(&record.type, 2);
        put(&record.x, 4);
        put(&record.y, 4);
        put(&record.z, 4);
    }
    return data;
}

/** @return The first 16 hexadecimal digits of the SHA-256 of the data (the digest of a region file of the tool). */
std::string digest_of(const std::string& data) {
    unsigned char hash[32] = {};
    (void)mbedtls_sha256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), hash, 0);
    std::string text;
    for (std::size_t index = 0; index < 8; ++index) {
        static constexpr char kDigits[] = "0123456789abcdef";
        text += kDigits[hash[index] >> 4];
        text += kDigits[hash[index] & 0x0F];
    }
    return text;
}

/**
 * Runs cases of tools/gamedata/check_gathering_tool.py (the review cases run only by name). @return The cases with a line "ok", and the
 * status of the script.
 */
std::pair<std::set<std::string>, int> run_tool_check(const Options& options, const std::string& name, const std::vector<std::string>& cases) {
    const std::filesystem::path script = options.goldenDirectory / ".." / ".." / ".." / ".." / "tools" / "gamedata" / "check_gathering_tool.py";
    check(std::filesystem::is_regular_file(script), "the check of the gathering tool is in the tree");
    const std::filesystem::path work = options.workDirectory / name;
    std::filesystem::remove_all(work);
    std::filesystem::create_directories(work);
    const auto output = options.workDirectory / (name + ".log");
    const char* python = std::getenv("SOULWARDEN_PYTHON");
    std::string command = std::string(python != nullptr && *python != '\0' ? python : "python3") + " -I -B \"" + script.lexically_normal().string()
                          + "\" \"" + work.string() + "\"";
    for (const std::string& entry : cases) {
        command += " " + entry;
    }
    command += " > \"" + output.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    std::ifstream log(output);
    std::set<std::string> passed;
    for (std::string line; std::getline(log, line);) {
        note(line.substr(0, 400));
        if (line.rfind("ok  ", 0) == 0) {
            const std::size_t start = line.find_first_not_of(' ', 2);
            passed.insert(line.substr(start, line.find(':') - start));
        }
    }
    return {passed, status};
}

/** GR1 (GA-R1): a skinned body that outlives the old time of its entry stays skinned. */
void body_lifetime(const Options&) {
    GatherShop shop;
    gamedata::SpawnPoint point;
    point.id = "review-long-lived-body";
    point.kind = 0;
    point.level = 20;
    point.position = bags::kKillPlace;
    point.respawnMin = 1200;
    point.respawnMax = 1200;
    check(shop.host.creatures.set_full({bags::bag_wolf_slice()}, {bags::bag_wolf_kind()}, {point}, {{20, bags::kWolfHealth, 0.0F, 0.0F}}),
          "setup: a spawn point with a respawn of 1,200 s");
    shop.enter();
    check(shop.equip(kCharacterA, "TestKnife", kKnifeSlot) && shop.equip(kCharacterB, "TestKnife", kKnifeSlot), "setup: A and B have a knife");
    world::Entity* body = nullptr;
    for (const auto& [id, entity] : shop.host.world.entities()) {
        if (entity.kind == world::EntityKind::creature && entity.creature.spawnPoint) {
            body = shop.host.world.find(id);
            break;
        }
    }
    if (body != nullptr) {
        (void)shop.host.world.creatures().hurt(body->id, bags::kWolfHealth * 2, shop.entity(kCharacterA)->id);
    }
    check(body != nullptr, "setup: the wolf of the spawn point dies (the body)");
    if (body == nullptr) {
        return;
    }
    const world::EntityId id = body->id;
    const InteractionBlock block = GatherShop::body_block(*body);
    const std::string key = "body/" + hex64(id);
    shop.gather(shop.clientA, block, 1700);
    check(bag_count(*shop.entity(kCharacterA), "TestHideT1") == 2, "control: the first skinning gives A two hides");
    wait_seconds(shop, 605);
    const world::GatherNode* entry = shop.gathering().node(key);
    note("GR1 body_exists=" + std::to_string(shop.host.world.find(id) != nullptr) + " entry_exists=" + std::to_string(entry != nullptr));
    check(shop.host.world.find(id) != nullptr, "control: the same body is in the world after 605 s (a character is near it)");
    check(entry != nullptr && entry->depleted, "the entry of the skinned body stays while the body is there (not a time of 600 s)");
    shop.gather(shop.clientA, block, 1700);
    shop.gather(shop.clientB, block, 1700);
    const auto hideA = bag_count(*shop.entity(kCharacterA), "TestHideT1");
    const auto hideB = bag_count(*shop.entity(kCharacterB), "TestHideT1");
    note("GR1 hide_after_second_skin A=" + std::to_string(hideA) + " B=" + std::to_string(hideB));
    check(hideA == 2 && hideB == 0, "one body gives no second skinning after 605 s: A keeps two hides, B gets none");
    check(shop.skill(kCharacterA, "Skinning").second == 32 && shop.skill(kCharacterB, "Skinning").second == 0,
          "one body gives the Skinning experience one time");
    // The body leaves (the creature system removes it): its entry goes at the next look of the due list.
    check(shop.host.world.creatures().remove(id).error == world::CreatureError::none, "setup: the body leaves the world");
    wait_seconds(shop, 11);
    check(shop.gathering().node(key) == nullptr, "the entry of the body goes after the body left the world");
}

/** GR2 (GA-R2): a region file that is not there at a start does not delete a valid stored depletion. */
void missing_region(const Options& options) {
    const auto folder = options.workDirectory / "review-gathering-gr2";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto path = folder / "store.sqlite";
    std::string capital;
    {
        GatherShop shop(path.string());
        capital = shop.host.interactions.objects()[0].key;
        const std::string version = std::string("distribution:") + kVersion;
        check(shop.host.store.write_gathering_node(depletion(node_key(kOre), version, "Test_OreSmall", 1000000000000ULL))
                  && shop.host.store.write_gathering_node(depletion(capital, "capital", "Test_OreSmall", 1000000000000ULL))
                  && shop.host.store.write_gathering_node(depletion(node_key(kHemp), "distribution:old-version", "Test_Hemp", 1000000000000ULL)),
              "setup: three stored depletions with 1,000 s left (the ore, the capital node, the hemp of another version)");
        check(shop.host.gathering.set(gathering_document().dump(), {}, folder.string()), "setup: the document loads; its region file is not there");
        shop.host.interactions = gamedata::InteractionData{};
        shop.enter();
        std::set<std::string> kept;
        for (const state::GatheringNodeRow& row : shop.host.store.list_gathering_nodes()) {
            kept.insert(row.key);
        }
        note("GR2 rows_with_region_absent=" + std::to_string(kept.size()));
        check(kept.count(node_key(kOre)) == 1, "a region file that is not there does not delete a valid stored depletion of the region");
        check(kept.count(capital) == 1, "no interaction data does not delete a stored depletion of a capital node");
        check(kept.count(node_key(kHemp)) == 0, "control: a row of another version of the distribution file goes (the document proves it)");
        check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: A has a pick");
        shop.gather(shop.clientA, GatherShop::natural_block(kStone), 1700);
        check(bag_count(*shop.entity(kCharacterA), "TestStoneT1") == 0, "no node of the region with no file can be gathered");
    }
    {
        GatherShop shop(path.string(), true);
        shop.enter();
        check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: A has a pick after the data comes back");
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3100);
        const auto natural = bag_count(*shop.entity(kCharacterA), "TestOreT1");
        shop.gather(shop.clientA, shop.capital_block(), 3100);
        const auto all = bag_count(*shop.entity(kCharacterA), "TestOreT1");
        note("GR2 ore_before_saved_time natural=" + std::to_string(natural) + " capital=" + std::to_string(all - natural));
        check(natural == 0 && shop.gathering().node(node_key(kOre)) != nullptr && shop.gathering().node(node_key(kOre))->depleted,
              "with the data back the natural node stays depleted until its saved time");
        check(all == natural && shop.gathering().node(capital) != nullptr && shop.gathering().node(capital)->depleted,
              "with the interaction data back the capital node stays depleted until its saved time");
    }
}

/** GR3 (GA-R3): a new request before the release of the old one keeps the rest of the old work. */
void replacement_rest(const Options&) {
    GatherShop shop;
    shop.enter();
    check(shop.equip(kCharacterA, "TestPick", kPickSlot), "setup: A has a pick (the speed 1.5)");
    const InteractionBlock first = GatherShop::natural_block(kOre);
    const InteractionBlock second = GatherShop::natural_block(kOre2);
    shop.request(shop.clientA, first);
    shop.hold(shop.clientA, first, 700);
    const auto before = shop.gathering().view(kCharacterA, 0).work;
    check(before.has_value() && before->node == node_key(kOre), "setup: the work on the ore A is active");
    // A refused new request (a row with a rule that the backend cannot check) leaves the use of A as it was.
    shop.request(shop.clientA, GatherShop::natural_block(kQuest));
    const auto unchanged = shop.gathering().view(kCharacterA, 0).work;
    check(unchanged && before && unchanged->node == before->node && unchanged->amount == before->amount && unchanged->started == before->started,
          "control: a refused new request leaves the work on A as it was");
    const std::size_t from = shop.clientA.got.size();
    shop.request(shop.clientA, second);
    const auto states = gather_states(shop.clientA, 1, from);
    check(!states.empty() && states.back().status == 1 && std::fabs(states.back().remaining - 4.0F) < 1e-4F
              && states.back().target == node_group(kOre2),
          "the new request for the ore B: the Gathering state Success, the amount 4, the G of B");
    shop.hold(shop.clientA, second, 200);
    shop.request(shop.clientA, first);
    const auto resumed = shop.gathering().view(kCharacterA, 0).work;
    note("GR3 original_amount=" + std::to_string(before ? before->amount : -1) + " resumed_amount=" + std::to_string(resumed ? resumed->amount : -1));
    check(resumed && before && resumed->node == node_key(kOre) && resumed->amount < before->amount - 0.5F && resumed->amount > 2.5F,
          "the return to A continues the rest of its work (about 4 - 0.8 x 1.5)");
    // The rest needs about 1.9 s; the whole work needs 2.67 s.
    shop.hold(shop.clientA, first, 2200);
    shop.end_block(shop.clientA);
    check(bag_count(*shop.entity(kCharacterA), "TestOreT1") == 2 && shop.gathering().node(node_key(kOre)) != nullptr,
          "A completes the ore with the rest of its work (2.2 s, less than the whole work)");
    shop.request(shop.clientA, second);
    const auto restB = shop.gathering().view(kCharacterA, 0).work;
    check(restB && restB->node == node_key(kOre2) && restB->amount > 3.4F && restB->amount < 3.9F,
          "B also kept the rest of its work (about 4 - 0.2 x 1.5)");
    shop.end_block(shop.clientA);
}

/** GR4 (GA-R4): the source gates of the gathering tool. */
void source_gates(const Options& options) {
    const auto [passed, status] = run_tool_check(options, "review-gathering-gr4", {"gate-columns", "flag-texts", "event-reward"});
    check(status == 0 && passed.size() == 3,
          "each source case of the check of the gathering tool passes (" + std::to_string(passed.size()) + " of 3)");
    check(passed.count("gate-columns") == 1,
          "a table with no IsFFARestricted, ItemRestrictionId, or ResetGatherTimeOnEndInteraction stops the tool; the named exception and "
          "ItemRequired with a blank item do not");
    check(passed.count("flag-texts") == 1, "an unknown flag text stops the tool; FALSE and TRUE are flags");
    check(passed.count("event-reward") == 1, "an event table with no column CategoricalProgressionReward stops the tool; a blank reward is 0");
}

/** GR5 (GA-R5): the loader checks the supported mark against the gates of the row. */
void loader_gates(const Options&) {
    struct Variant {
        const char* what;
        std::function<void(json&)> change;
        bool supported;
        const char* cause;
    };
    const std::vector<Variant> variants{
        // Stage QG5b: ItemRequired with an item is a gate of the gather (the bag check); the 2 gatherers stay a cause of the loader.
        {"ItemRequired with the item TestKey and 2 gatherers",
         [](json& row) {
             row["restriction"] = "ItemRequired";
             row["item"] = "TestKey";
             row["itemCount"] = 1;
             row["required"] = 2;
         },
         false, "gatherer"},
        {"ItemRequired with no item member", [](json& row) { row["restriction"] = "ItemRequired"; }, false, "ItemRequired"},
        {"the restriction WhisperRequired", [](json& row) { row["restriction"] = "WhisperRequired"; }, false, "WhisperRequired"},
        {"2 gatherers", [](json& row) { row["required"] = 2; }, false, "gatherer"},
        {"ItemRequired with an empty item (the exception)",
         [](json& row) {
             row["restriction"] = "ItemRequired";
             row["item"] = "";
             row["itemCount"] = 0;
         },
         true, ""},
    };
    for (const Variant& variant : variants) {
        GatherShop shop;
        json document = gathering_document();
        variant.change(document["rows"][kOreRow]);
        const bool accepted = shop.host.gathering.set(document.dump(), {{kRegion, region_records()}});
        shop.enter();
        check(accepted && shop.equip(kCharacterA, "TestPick", kPickSlot),
              std::string("setup: the file loads, and A has a pick (") + variant.what + ")");
        shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3100);
        const auto ore = bag_count(*shop.entity(kCharacterA), "TestOreT1");
        const std::vector<std::string>& causes = shop.host.gathering.rows()[kOreRow].unsupported;
        note(std::string("GR5 ") + variant.what + ": ore=" + std::to_string(ore)
             + " cause=" + (causes.empty() ? std::string("none") : causes.front()));
        if (variant.supported) {
            check(ore == 2 && causes.empty(), std::string("control: ") + variant.what + " is supported, and the gather gives the ore");
        } else {
            check(ore == 0 && !causes.empty() && causes.front().find(variant.cause) != std::string::npos,
                  std::string("a row that the file marks supported with ") + variant.what + " is refused with the cause of the loader");
        }
    }
    gamedata::GatheringData data;
    json document = gathering_document();
    document["rows"][kOreRow]["item"] = 5;
    check(!data.set(document.dump(), {{kRegion, region_records()}}) && !data.loaded(), "a row with an item that is not a text refuses the file");
}

/** GR6 (GA-R6): each region file belongs to its document. */
void region_digest(const Options& options) {
    const auto folder = options.workDirectory / "review-gathering-gr6";
    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    const auto regionPath = folder / (std::string(kRegion) + ".swgn");
    const auto write_text = [](const std::filesystem::path& path, const std::string& text) {
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    };
    const std::string original = region_file(region_records());
    json document = gathering_document();
    document["regions"][0]["digest"] = digest_of(original);
    write_text(folder / gamedata::kGatheringDataFile, document.dump());
    write_text(regionPath, original);
    gamedata::GatheringData good;
    const bool goodLoaded = good.load(folder.string());
    const auto goodNode = goodLoaded ? good.natural(node_group(kOre)) : std::nullopt;
    check(goodNode && goodNode->position.x == 100.0F, "control: the region file with the digest of the document gives its nodes");
    // The region file of an export that stopped after it wrote the file: another place of the ore, the same counts.
    std::vector<gamedata::NaturalRecord> moved = region_records();
    moved[kOre].x = 103.0F;
    write_text(regionPath, region_file(moved));
    gamedata::GatheringData mixed;
    const bool mixedLoaded = mixed.load(folder.string());
    const auto mixedNode = mixedLoaded ? mixed.natural(node_group(kOre)) : std::nullopt;
    note("GR6 mixed_set_loaded=" + std::to_string(mixedLoaded) + " node_x=" + std::to_string(mixedNode ? mixedNode->position.x : -1.0F));
    check(mixedLoaded && !mixedNode && !mixed.natural(node_group(kStone)), "a region file with another digest than the document gives no node");
    // A stored depletion of that region stays (finding GA-R2: the file is not the file of the data).
    const auto path = folder / "store.sqlite";
    {
        GatherShop shop(path.string(), false, false);
        const std::string version = std::string("distribution:") + kVersion;
        check(shop.host.store.write_gathering_node(depletion(node_key(kOre), version, "Test_OreSmall", 1000000000000ULL))
                  && shop.host.gathering.load(folder.string()),
              "setup: a stored depletion of the region, and the files with the region file of another export");
        shop.enter();
        check(shop.host.store.list_gathering_nodes().size() == 1, "the stored depletion of the region of the other region file stays");
    }
    // A document of before the review has no digest: no check (the files of the common game data folder load as before).
    document["regions"][0].erase("digest");
    write_text(folder / gamedata::kGatheringDataFile, document.dump());
    gamedata::GatheringData before;
    const bool beforeLoaded = before.load(folder.string());
    const auto beforeNode = beforeLoaded ? before.natural(node_group(kOre)) : std::nullopt;
    check(beforeNode && beforeNode->position.x == 103.0F, "control: a document with no digest (the form of before) reads its region file");
    document["regions"][0]["digest"] = "not-a-digest";
    write_text(folder / gamedata::kGatheringDataFile, document.dump());
    gamedata::GatheringData broken;
    check(!broken.load(folder.string()) && !broken.loaded(), "a digest that is not 16 hexadecimal digits refuses the document");
    // The tool side: an export that stops leaves the published set; the files of a set have their digests.
    const auto [passed, status] = run_tool_check(options, "review-gathering-gr6", {"export-publish"});
    check(status == 0 && passed.count("export-publish") == 1,
          "an export that stops after it made a region file leaves the manifest and the region files of the published set; a publication "
          "keeps the earlier set and removes an older one");
}

/** GR7 (GA-R7): one kept roll for each node of a character. */
void kept_rolls(const Options&) {
    GatherShop shop;
    shop.enter();
    world::Entity& a = *shop.entity(kCharacterA);
    check(shop.equip(kCharacterA, "TestPick", kPickSlot)
              && shop.host.world.inventory().give(kCharacterA, "TestFiller", world::kMaxContainerSlots).ok(),
          "setup: a pick and a full bag");
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3100);
    shop.gather(shop.clientA, GatherShop::natural_block(kStone), 1700);
    const auto before = shop.gathering().counts().rolls;
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3100);
    note("GR7 rolls_before_return=" + std::to_string(before) + " rolls_after_return=" + std::to_string(shop.gathering().counts().rolls));
    check(before == 2 && shop.gathering().counts().rolls == 2, "a gather of another node does not replace the kept roll of the ore");
    check(shop.gathering().node_count() == 0 && shop.skill(kCharacterA, "Mining").second == 0, "no completion depletes a node or gives experience");
    // With a free place, each node gives its own kept roll (no new roll).
    check(shop.host.world.inventory().remove(kCharacterA, 0).ok(), "setup: one place free");
    shop.gather(shop.clientA, GatherShop::natural_block(kOre), 3100);
    check(shop.host.world.inventory().remove(kCharacterA, 1).ok(), "setup: one more place free");
    shop.gather(shop.clientA, GatherShop::natural_block(kStone), 1700);
    note("GR7 ore=" + std::to_string(bag_count(a, "TestOreT1")) + " stone=" + std::to_string(bag_count(a, "TestStoneT1"))
         + " rolls=" + std::to_string(shop.gathering().counts().rolls));
    check(bag_count(a, "TestOreT1") == 2 && bag_count(a, "TestStoneT1") == 1 && shop.gathering().counts().rolls == 2,
          "the ore gives its kept roll, then the stone gives its kept roll: no new roll");
    check(shop.gathering().node(node_key(kOre)) != nullptr && shop.gathering().node(node_key(kOre))->depleted
              && shop.gathering().node(node_key(kStone)) != nullptr && shop.gathering().node(node_key(kStone))->depleted,
          "both nodes are depleted");
}

} // namespace review_gathering

std::vector<Case> review_gathering_cases() {
    using namespace review_gathering;
    return {{"GR1", "review of stage 3.6a, GA-R1: a skinned body stays skinned while it is in the world", body_lifetime},
            {"GR2", "review of stage 3.6a, GA-R2: a region file that is not there keeps a stored depletion", missing_region},
            {"GR3", "review of stage 3.6a, GA-R3: a new request keeps the rest of the old work", replacement_rest},
            {"GR4", "review of stage 3.6a, GA-R4: the gathering tool needs the gate columns and the flag texts", source_gates},
            {"GR5", "review of stage 3.6a, GA-R5: the loader checks the supported mark of a row", loader_gates},
            {"GR6", "review of stage 3.6a, GA-R6: a region file belongs to its document; a stopped export changes no file", region_digest},
            {"GR7", "review of stage 3.6a, GA-R7: one kept roll for each node", kept_rolls}};
}

} // namespace soulwarden::backend::tests
